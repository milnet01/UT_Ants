// umat -- see Enlarge.h.

#include "umat/Enlarge.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>

namespace uta::umat {

namespace {

using detail::Phase;

[[nodiscard]] std::size_t wrap(std::int64_t index, std::int64_t size) noexcept {
    const std::int64_t r = index % size;
    return static_cast<std::size_t>(r < 0 ? r + size : r);
}

/// The first source index output index `j` reads at factor `k`:
/// floor((2j + 1 - k) / 2k) - (A - 1), without a division of a negative.
[[nodiscard]] std::int64_t firstTap(std::int64_t j, std::int64_t k) noexcept {
    const std::int64_t whole = j / k;
    const std::int64_t phase = j % k;
    return whole + (2 * phase + 1 < k ? -1 : 0) - (detail::LANCZOS_A - 1);
}

/// A weighted sum back to a byte: round half up, then clamp, because the
/// kernel's negative lobes overshoot at a hard edge.
[[nodiscard]] std::byte rounded(std::int64_t sum) noexcept {
    const std::int64_t value = (sum + detail::WEIGHT_ONE / 2) >> detail::WEIGHT_BITS;
    return static_cast<std::byte>(std::clamp<std::int64_t>(value, 0, 255));
}

} // namespace

Result<Image> enlarge(const Image& rgba, std::uint32_t factor, JobSystem& jobs) {
    const auto code = ErrorCode::InvalidArgument;
    if (rgba.channels != 4)
        return fail(code, "enlarge: the image has " + std::to_string(rgba.channels)
                              + " channels; enlarge takes RGBA");
    if (rgba.width == 0 || rgba.height == 0)
        return fail(code, "enlarge: an image dimension is zero");
    if (rgba.pixels.size() != std::size_t{rgba.width} * rgba.height * 4)
        return fail(code, "enlarge: the image holds " + std::to_string(rgba.pixels.size())
                              + " bytes, which its dimensions do not");
    if (factor == 1) return rgba;

    std::span<const Phase> phases;
    if (factor == 2)
        phases = detail::kWeights2;
    else if (factor == 4)
        phases = detail::kWeights4;
    else
        return fail(code, "enlarge: factor " + std::to_string(factor) + " is not 1, 2 or 4");

    const std::int64_t k = factor;
    const std::int64_t width = rgba.width;
    const std::int64_t height = rgba.height;
    const auto wideWidth = static_cast<std::size_t>(width * k);

    // Horizontal pass, one job per row, each writing only its own row (INV-7).
    Image wide;
    wide.width = static_cast<std::uint32_t>(wideWidth);
    wide.height = rgba.height;
    wide.channels = 4;
    wide.pixels.resize(wideWidth * rgba.height * 4);
    std::size_t threw = jobs.parallelFor(rgba.height, [&](std::size_t row) {
        const std::byte* const in = rgba.pixels.data() + row * rgba.width * 4;
        std::byte* const out = wide.pixels.data() + row * wideWidth * 4;
        for (std::size_t x = 0; x < wideWidth; ++x) {
            const auto j = static_cast<std::int64_t>(x);
            const Phase& weights = phases[x % factor];
            const std::int64_t first = firstTap(j, k);
            for (std::size_t c = 0; c < 4; ++c) {
                std::int64_t sum = 0;
                for (std::size_t t = 0; t < weights.size(); ++t)
                    sum += std::to_integer<std::int64_t>(
                               in[wrap(first + static_cast<std::int64_t>(t), width) * 4 + c])
                           * weights[t];
                out[x * 4 + c] = rounded(sum);
            }
        }
    });

    // Vertical pass over the intermediate, which no job writes.
    Image out;
    out.width = wide.width;
    out.height = static_cast<std::uint32_t>(height * k);
    out.channels = 4;
    out.pixels.resize(wideWidth * out.height * 4);
    threw += jobs.parallelFor(out.height, [&](std::size_t row) {
        const Phase& weights = phases[row % factor];
        const std::int64_t first = firstTap(static_cast<std::int64_t>(row), k);
        std::byte* const dst = out.pixels.data() + row * wideWidth * 4;
        for (std::size_t x = 0; x < wideWidth; ++x) {
            for (std::size_t c = 0; c < 4; ++c) {
                std::int64_t sum = 0;
                for (std::size_t t = 0; t < weights.size(); ++t) {
                    const std::size_t sy = wrap(first + static_cast<std::int64_t>(t), height);
                    sum += std::to_integer<std::int64_t>(wide.pixels[(sy * wideWidth + x) * 4 + c])
                           * weights[t];
                }
                dst[x * 4 + c] = rounded(sum);
            }
        }
    });

    // A partly written image is a wrong material presented as a good one.
    if (threw != 0)
        return fail(ErrorCode::Unknown,
                    "enlarge: " + std::to_string(threw) + " rows threw while filtering");
    return out;
}

} // namespace uta::umat

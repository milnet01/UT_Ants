// umat -- see Derive.h.

#include "umat/Derive.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <utility>

namespace uta::umat {

namespace detail {

std::uint64_t isqrt(std::uint64_t n) noexcept {
    // Digit by digit in base 4: exact, and no maths library.
    std::uint64_t root = 0;
    std::uint64_t bit = std::uint64_t{1} << 62;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

} // namespace detail

namespace {

[[nodiscard]] Image blank(std::uint32_t width, std::uint32_t height, std::uint8_t channels) {
    Image out;
    out.width = width;
    out.height = height;
    out.channels = channels;
    out.pixels.resize(std::size_t{width} * height * channels);
    return out;
}

[[nodiscard]] std::uint32_t at(const Image& image, std::size_t x, std::size_t y,
                               std::size_t channel) noexcept {
    return std::to_integer<std::uint32_t>(
        image.pixels[(y * image.width + x) * image.channels + channel]);
}

/// One-channel `height` at (x, y), wrapping at every edge.
[[nodiscard]] std::int64_t wrapped(const Image& height, std::int64_t x, std::int64_t y) noexcept {
    const std::int64_t w = height.width;
    const std::int64_t h = height.height;
    const auto sx = static_cast<std::size_t>(((x % w) + w) % w);
    const auto sy = static_cast<std::size_t>(((y % h) + h) % h);
    return at(height, sx, sy, 0);
}

/// A normal component in [-len, len] to a byte: 127.5 * c + 128, rounded
/// down, in integers. vz > 0 keeps len >= |component|, so the numerator is
/// positive and the result is in [0, 255].
[[nodiscard]] std::byte encoded(std::int64_t component, std::int64_t length) noexcept {
    return static_cast<std::byte>((255 * component + 256 * length) / (2 * length));
}

} // namespace

std::vector<Image> mipChain(const Image& level) {
    std::vector<Image> chain{level};
    const auto count = static_cast<std::size_t>(std::bit_width(std::max(level.width, level.height)));
    while (chain.size() < count) {
        const Image& above = chain.back();
        Image below = blank(std::max<std::uint32_t>(1, above.width / 2),
                            std::max<std::uint32_t>(1, above.height / 2), above.channels);
        for (std::size_t y = 0; y < below.height; ++y) {
            const std::size_t y0 = y * 2;
            const std::size_t y1 = std::min<std::size_t>(y0 + 1, above.height - 1);
            for (std::size_t x = 0; x < below.width; ++x) {
                const std::size_t x0 = x * 2;
                const std::size_t x1 = std::min<std::size_t>(x0 + 1, above.width - 1);
                for (std::size_t c = 0; c < above.channels; ++c) {
                    const std::uint32_t sum = at(above, x0, y0, c) + at(above, x1, y0, c)
                                              + at(above, x0, y1, c) + at(above, x1, y1, c);
                    below.pixels[(y * below.width + x) * below.channels + c] =
                        static_cast<std::byte>((sum + 2) >> 2);
                }
            }
        }
        chain.push_back(std::move(below));
    }
    return chain;
}

Image heightOf(const Image& rgba) {
    Image out = blank(rgba.width, rgba.height, 1);
    for (std::size_t y = 0; y < rgba.height; ++y)
        for (std::size_t x = 0; x < rgba.width; ++x)
            out.pixels[y * rgba.width + x] = static_cast<std::byte>(
                (54 * at(rgba, x, y, 0) + 183 * at(rgba, x, y, 1) + 19 * at(rgba, x, y, 2) + 128)
                >> 8);
    return out;
}

Image normalOf(const Image& height, std::uint32_t factor, std::uint32_t level) {
    const std::int64_t scale = detail::NORMAL_STRENGTH * factor;
    const std::int64_t vz = std::int64_t{255} << level;
    Image out = blank(height.width, height.height, 2);
    for (std::int64_t y = 0; y < height.height; ++y) {
        for (std::int64_t x = 0; x < height.width; ++x) {
            const auto h = [&](std::int64_t dx, std::int64_t dy) {
                return wrapped(height, x + dx, y + dy);
            };
            // Sobel, with row y - 1 above: Gy is measured downward.
            const std::int64_t gx =
                (h(1, -1) + 2 * h(1, 0) + h(1, 1)) - (h(-1, -1) + 2 * h(-1, 0) + h(-1, 1));
            const std::int64_t gy =
                (h(-1, 1) + 2 * h(0, 1) + h(1, 1)) - (h(-1, -1) + 2 * h(0, -1) + h(1, -1));
            const std::int64_t vx = -scale * gx;
            const std::int64_t vy = scale * gy;
            const auto length = static_cast<std::int64_t>(detail::isqrt(
                static_cast<std::uint64_t>(vx * vx + vy * vy + vz * vz)));
            const auto i = static_cast<std::size_t>(y * height.width + x) * 2;
            out.pixels[i] = encoded(vx, length);
            out.pixels[i + 1] = encoded(vy, length);
        }
    }
    return out;
}

Image roughnessOf(const Image& height, std::uint8_t baseRoughness) {
    Image out = blank(height.width, height.height, 1);
    for (std::size_t i = 0; i < out.pixels.size(); ++i) {
        const auto h = std::to_integer<std::int32_t>(height.pixels[i]);
        out.pixels[i] = static_cast<std::byte>(std::clamp(baseRoughness + (128 - h) / 4, 0, 255));
    }
    return out;
}

Image emissiveOf(const Image& rgba, const Image& height, std::uint8_t threshold) {
    Image out = blank(rgba.width, rgba.height, 4);
    for (std::size_t i = 0; i < height.pixels.size(); ++i) {
        const bool glows = std::to_integer<std::uint8_t>(height.pixels[i]) >= threshold;
        for (std::size_t c = 0; c < 3; ++c)
            out.pixels[i * 4 + c] = glows ? rgba.pixels[i * 4 + c] : std::byte{0};
        out.pixels[i * 4 + 3] = std::byte{255};
    }
    return out;
}

} // namespace uta::umat

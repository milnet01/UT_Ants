// umat -- see Resolve.h.

#include "umat/Resolve.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace uta::umat {

namespace {

/// UT's see-through palette index on a masked surface.
constexpr std::size_t SEE_THROUGH_INDEX = 0;

/// SS 4.2's fill. Each pass gives every unfilled see-through texel with a
/// filled neighbour -- the eight around it, wrapping at the edges -- the
/// rounded mean of those neighbours' colours. A pass reads only the state the
/// previous pass left, so the result does not depend on scan order. A texture
/// with no opaque texel keeps its palette colours.
///
/// A pass visits only the neighbours of the texels the last one filled, so the
/// whole fill touches each texel a bounded number of times rather than once a
/// pass (UTA-0218).
void fill(Image& image) {
    const std::int64_t width = image.width;
    const std::int64_t height = image.height;
    const std::size_t count = std::size_t{image.width} * image.height;

    std::vector<bool> filled(count);
    std::vector<std::size_t> frontier;
    for (std::size_t i = 0; i < count; ++i) {
        filled[i] = image.pixels[i * 4 + 3] != std::byte{0};
        if (filled[i]) frontier.push_back(i);
    }
    if (frontier.empty()) return;

    const auto neighbour = [&](std::size_t i, std::int64_t dx, std::int64_t dy) {
        const std::int64_t x = static_cast<std::int64_t>(i) % width;
        const std::int64_t y = static_cast<std::int64_t>(i) / width;
        return static_cast<std::size_t>(((y + dy + height) % height) * width + (x + dx + width) % width);
    };

    std::vector<bool> queued(count);
    std::vector<std::size_t> candidates;
    std::vector<std::array<std::byte, 3>> colours;
    while (!frontier.empty()) {
        candidates.clear();
        for (const std::size_t i : frontier)
            for (std::int64_t dy = -1; dy <= 1; ++dy)
                for (std::int64_t dx = -1; dx <= 1; ++dx) {
                    const std::size_t j = neighbour(i, dx, dy);
                    if (filled[j] || queued[j]) continue;
                    queued[j] = true;
                    candidates.push_back(j);
                }

        // Every candidate reads before any is marked, so a pass sees only the
        // state the previous one left.
        colours.assign(candidates.size(), {});
        for (std::size_t k = 0; k < candidates.size(); ++k) {
            std::array<std::uint32_t, 3> sum{};
            std::uint32_t neighbours = 0;
            for (std::int64_t dy = -1; dy <= 1; ++dy)
                for (std::int64_t dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const std::size_t j = neighbour(candidates[k], dx, dy);
                    if (!filled[j]) continue;
                    for (std::size_t c = 0; c < 3; ++c)
                        sum[c] += std::to_integer<std::uint32_t>(image.pixels[j * 4 + c]);
                    ++neighbours;
                }
            for (std::size_t c = 0; c < 3; ++c)
                colours[k][c] = static_cast<std::byte>((sum[c] + neighbours / 2) / neighbours);
        }
        for (std::size_t k = 0; k < candidates.size(); ++k) {
            for (std::size_t c = 0; c < 3; ++c) image.pixels[candidates[k] * 4 + c] = colours[k][c];
            filled[candidates[k]] = true;
        }
        frontier.swap(candidates);
    }
}

} // namespace

Result<Image> resolve(const upkg::Mip& level, const upkg::Palette& palette, bool masked) {
    const auto code = ErrorCode::InvalidArgument;
    if (level.width == 0 || level.height == 0)
        return fail(code, "resolve: a level dimension is zero");
    const std::size_t count = std::size_t{level.width} * level.height;
    if (level.pixels.size() != count)
        return fail(code, "resolve: the level holds " + std::to_string(level.pixels.size())
                              + " bytes where its " + std::to_string(level.width) + "x"
                              + std::to_string(level.height) + " texels need "
                              + std::to_string(count));

    Image out;
    out.width = level.width;
    out.height = level.height;
    out.channels = 4;
    out.pixels.resize(count * 4);
    for (std::size_t i = 0; i < count; ++i) {
        const auto index = std::to_integer<std::size_t>(level.pixels[i]);
        if (index >= palette.entries.size())
            return fail(code, "resolve: texel " + std::to_string(i) + " names palette entry "
                                  + std::to_string(index) + " of a palette of "
                                  + std::to_string(palette.entries.size()));
        const upkg::PaletteEntry& entry = palette.entries[index];
        std::byte* const texel = out.pixels.data() + i * 4;
        texel[0] = static_cast<std::byte>(entry.r);
        texel[1] = static_cast<std::byte>(entry.g);
        texel[2] = static_cast<std::byte>(entry.b);
        texel[3] = masked && index == SEE_THROUGH_INDEX ? std::byte{0} : std::byte{255};
    }
    if (masked) fill(out);
    return out;
}

} // namespace uta::umat

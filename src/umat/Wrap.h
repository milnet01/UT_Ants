// umat: the tiling wrap Enlarge, Derive and Resolve share. World textures
// tile, so an index past either edge reads from the other side.

#pragma once

#include <cstddef>
#include <cstdint>

namespace uta::umat::detail {

/// `index` wrapped into [0, size): -1 is size - 1, and size is 0. `size` > 0.
[[nodiscard]] constexpr std::size_t wrap(std::int64_t index, std::int64_t size) noexcept {
    const std::int64_t r = index % size;
    return static_cast<std::size_t>(r < 0 ? r + size : r);
}

} // namespace uta::umat::detail

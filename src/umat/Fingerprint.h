// umat: the fingerprint of a texture's picture -- the key the curated library
// looks a texture up by.
//
// docs/specs/UTA-0010-curated-material-library.md SS 4.2.
//
// THE PICTURE, NOT THE NAME. A texture copied into a map lives in the map's own
// package, often renamed, so a key of package and name reaches none of those
// copies (SS 2 item 2). The fingerprint covers the bytes resolve() reads, plus
// any palette entry no index names, and nothing else.

#pragma once

#include "upkg/Texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace uta::umat {

namespace detail {

/// FNV-1a 64, fed one byte at a time. Source:
/// https://www.isthe.com/chongo/tech/comp/fnv/index.html, which gives the two
/// constants in decimal as 14695981039346656037 and 1099511628211.
struct Fnv1a {
    std::uint64_t value = 0xcbf29ce484222325ULL;

    constexpr void add(std::uint8_t byte) noexcept { value = (value ^ byte) * 0x100000001b3ULL; }
};

/// Whether `table` is sorted by `fingerprint` ascending with none twice: what
/// each list keyed by pictureFingerprint holds, so its binary search works.
template <typename Entry, std::size_t N>
constexpr bool sortedByFingerprint(const std::array<Entry, N>& table) noexcept {
    for (std::size_t i = 1; i < N; ++i)
        if (table[i - 1].fingerprint >= table[i].fingerprint) return false;
    return true;
}

} // namespace detail

/// Width and height as 4 bytes little-endian each, every index byte row by
/// row, then every palette entry's r, g and b. Palette alpha is left out
/// because resolve() ignores it.
///
/// Nothing when `base` is not one index byte per texel. The caller passes only
/// a texture carrying no `Format` property: a block format storing one byte a
/// texel would pass this check.
[[nodiscard]] std::optional<std::uint64_t> pictureFingerprint(
    const upkg::Mip& base, const upkg::Palette& palette) noexcept;

} // namespace uta::umat

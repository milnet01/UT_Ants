// umat: the curated material library -- hand-set settings for a generated
// material, found by the fingerprint of the texture's picture.
//
// docs/specs/UTA-0010-curated-material-library.md SS 4.3 to SS 4.6.
//
// SETTINGS, NOT ART. An entry replaces some of the MaterialSettings generate()
// runs with, and nothing else (SS 3 decision 3). The table is C++ data in
// CuratedMaterials.cpp, compiled into the baker (SS 3 decision 4).
//
// THE ORDER UTA-0011 BINDS TO (SS 4.5): MaterialSettings{}, then the library's
// entry, then the map recipe's assignment, each through applied(). A recipe's
// requested upscale is the exception, set directly.
//
// VERSIONED WITH THE BAKER. libraryDigest() changes whenever an entry's
// fingerprint or any override changes, and UTA-0011 folds it into the baker
// version (SS 4.6).

#pragma once

#include "umat/Generate.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace uta::umat {

/// Settings an entry replaces. An empty field keeps the value it had.
struct CuratedOverride {
    std::optional<bool> metallic;
    std::optional<std::uint8_t> baseRoughness;
    std::optional<bool> emissive;
    std::optional<std::uint8_t> emissiveThreshold;

    friend constexpr bool operator==(const CuratedOverride&, const CuratedOverride&) = default;
};

/// Why an entry exists (SS 4.7). Audit only: it reaches no bundle.
enum class CurationSource : std::uint8_t { MetalSound, GroupName, Play };

struct CuratedEntry {
    std::uint64_t fingerprint;
    /// The picture's usual `<package>.<path>`, for a human. Audit only.
    std::string_view note;
    CurationSource source;
    CuratedOverride settings;
};

/// Sorted by fingerprint ascending, no fingerprint twice (INV-4).
[[nodiscard]] std::span<const CuratedEntry> curatedLibrary() noexcept;

/// The entry for this fingerprint, or null. A binary search.
[[nodiscard]] const CuratedOverride* curated(std::uint64_t fingerprint) noexcept;

/// `settings` with every field `entry` sets replaced, and no other.
[[nodiscard]] MaterialSettings applied(MaterialSettings settings,
                                       const CuratedOverride& entry) noexcept;

/// digestOf(curatedLibrary()).
[[nodiscard]] std::uint64_t libraryDigest() noexcept;

namespace detail {

/// FNV-1a 64 over each entry in order: its fingerprint as 8 bytes
/// little-endian, then each override field in declaration order as a presence
/// byte followed, when present, by its value as one byte. `note` and `source`
/// are left out: neither changes a pixel, so neither may invalidate a bake.
[[nodiscard]] std::uint64_t digestOf(std::span<const CuratedEntry> entries) noexcept;

} // namespace detail

} // namespace uta::umat

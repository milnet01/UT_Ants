// umat: which pictures are cloth -- UTA-0055 SS 4.6.
//
// docs/specs/UTA-0055-swaying-cloth-and-water.md SS 4.6.
//
// A LIST, NOT A RULE. A picture is cloth when this list names its
// fingerprint, and not otherwise: no stored property tells a banner from a
// wall. Whether a surface wearing it sways is its flags' call (SS 4.1).
//
// KEYED AS THE CURATED LIBRARY IS, by pictureFingerprint. Every entry is a
// line of tests/real/cloth-labels.txt; tests/real/RealClothTest.cpp is what
// notices a disagreement.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace uta::umat {

struct ClothEntry {
    std::uint64_t fingerprint;
    /// The picture's `<package>.<name>`, for a human. Audit only.
    std::string_view note;
};

/// Sorted by fingerprint ascending, no fingerprint twice.
[[nodiscard]] std::span<const ClothEntry> clothLibrary() noexcept;

/// Whether this picture is cloth. A binary search.
[[nodiscard]] bool isCloth(std::uint64_t fingerprint) noexcept;

} // namespace uta::umat

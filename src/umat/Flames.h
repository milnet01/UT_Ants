// umat: which FireTextures are flames -- UTA-0263 SS 4.1.
//
// docs/specs/UTA-0263-shader-flames.md SS 4.1.
//
// A LIST, NOT A RULE. A FireTexture is a flame when this list names its
// picture's fingerprint, and not otherwise. No rule on its stored properties
// fitted the labels in tests/real/flame-labels.txt: UT99 draws shields,
// waterfalls and lightning with the same class and the same sparks.
//
// KEYED AS THE CURATED LIBRARY IS, by pictureFingerprint -- and a
// FireTexture's picture is the still ubake::fireStill makes, so a change to
// that simulation re-keys every entry. INV-3 over the reference install is
// what notices.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace uta::umat {

struct FlameEntry {
    std::uint64_t fingerprint;
    /// The picture's `<package>.<name>`, for a human. Audit only.
    std::string_view note;
};

/// Sorted by fingerprint ascending, no fingerprint twice.
[[nodiscard]] std::span<const FlameEntry> flameLibrary() noexcept;

/// Whether this picture is drawn as a flame. A binary search.
[[nodiscard]] bool isFlame(std::uint64_t fingerprint) noexcept;

} // namespace uta::umat

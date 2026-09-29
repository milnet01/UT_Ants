// A disk cache of made materials, shared between bakes -- UTA-0148 finding (2).
//
// umat::generate is where a bake spends its time: enlarging and compressing
// each texture. It is a pure function of the material id, the base picture and
// the settings, and stock textures are shared by many maps, so its result is
// kept on disk keyed by exactly those inputs plus bakerVersion(). A hit returns
// the bytes generate would have made, so a bake's output is the same with the
// cache cold, warm or off.
//
// Everything here is best effort: a file that is missing, damaged or written by
// another version is a miss, and a failed write is ignored. Several bakes may
// share one directory at once; each file is written atomically.
//
// Where it lives is the caller's: ut-bake's --texture-cache, off unless given
// (user, 2026-09-29). Measured that day, six random Monster Hunt maps shared 11
// of 491 materials while baking one map again took half the time, so it pays
// on a repeat bake rather than a new map. 4 GB cap, oldest-used removed first.

#pragma once

#include "umat/Generate.h"
#include "umat/Material.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace uta::ubake {

inline constexpr std::uint64_t TEXTURE_CACHE_CAP_BYTES = std::uint64_t{4} << 30;

using TextureCacheKey = std::array<std::byte, 32>;

/// SHA-256 over everything umat::generate reads, and bakerVersion(), which a
/// change to what generate makes must bump.
[[nodiscard]] TextureCacheKey textureCacheKey(std::string_view id, const umat::Image& base,
                                              const umat::MaterialSettings& settings);

class TextureCache {
public:
    explicit TextureCache(std::filesystem::path directory,
                          std::uint64_t capBytes = TEXTURE_CACHE_CAP_BYTES);

    /// The material stored under `key` for `id`, or nothing. Safe from several
    /// threads. A hit marks the file used now, for trim's order.
    [[nodiscard]] std::optional<umat::Material> find(const TextureCacheKey& key, std::string_view id);

    /// Keep `material` under `key`. Safe from several threads; a failure is
    /// ignored, since the bake already holds the material.
    void store(const TextureCacheKey& key, const umat::Material& material);

    /// Remove the least recently used files until the directory holds at most
    /// the cap. Files another bake removes meanwhile are skipped.
    void trim();

    [[nodiscard]] std::uint32_t hits() const noexcept { return hits_.load(); }
    [[nodiscard]] std::uint32_t misses() const noexcept { return misses_.load(); }

private:
    [[nodiscard]] std::filesystem::path pathOf(const TextureCacheKey& key) const;

    std::filesystem::path directory_;
    std::uint64_t capBytes_;
    std::atomic<std::uint32_t> hits_{0};
    std::atomic<std::uint32_t> misses_{0};
};

} // namespace uta::ubake

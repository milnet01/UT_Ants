// umat -- see Library.h. The table itself is CuratedMaterials.cpp.

#include "umat/Library.h"

#include "umat/Fingerprint.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

namespace uta::umat {

const CuratedOverride* curated(std::uint64_t fingerprint) noexcept {
    const std::span<const CuratedEntry> table = curatedLibrary();
    const auto found =
        std::ranges::lower_bound(table, fingerprint, {}, &CuratedEntry::fingerprint);
    return found != table.end() && found->fingerprint == fingerprint ? &found->settings
                                                                     : nullptr;
}

MaterialSettings applied(MaterialSettings settings, const CuratedOverride& entry) noexcept {
    if (entry.metallic) settings.metallic = *entry.metallic;
    if (entry.baseRoughness) settings.baseRoughness = *entry.baseRoughness;
    if (entry.emissive) settings.emissive = *entry.emissive;
    if (entry.emissiveThreshold) settings.emissiveThreshold = *entry.emissiveThreshold;
    return settings;
}

std::uint64_t libraryDigest() noexcept { return detail::digestOf(curatedLibrary()); }

namespace detail {

std::uint64_t digestOf(std::span<const CuratedEntry> entries) noexcept {
    Fnv1a hash;
    const auto field = [&hash]<typename T>(const std::optional<T>& value) {
        hash.add(value ? 1 : 0);
        if (value) hash.add(static_cast<std::uint8_t>(*value));
    };
    for (const CuratedEntry& entry : entries) {
        for (int shift = 0; shift < 64; shift += 8)
            hash.add(static_cast<std::uint8_t>(entry.fingerprint >> shift));
        field(entry.settings.metallic);
        field(entry.settings.baseRoughness);
        field(entry.settings.emissive);
        field(entry.settings.emissiveThreshold);
    }
    return hash.value;
}

} // namespace detail

} // namespace uta::umat

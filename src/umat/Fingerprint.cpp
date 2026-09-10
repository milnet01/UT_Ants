// umat -- see Fingerprint.h.

#include "umat/Fingerprint.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace uta::umat {

std::optional<std::uint64_t> pictureFingerprint(const upkg::Mip& base,
                                                const upkg::Palette& palette) noexcept {
    if (std::uint64_t{base.pixels.size()} != std::uint64_t{base.width} * base.height)
        return std::nullopt;

    detail::Fnv1a hash;
    for (const std::uint32_t dimension : {base.width, base.height})
        for (int shift = 0; shift < 32; shift += 8)
            hash.add(static_cast<std::uint8_t>(dimension >> shift));
    for (const std::byte index : base.pixels) hash.add(std::to_integer<std::uint8_t>(index));
    for (const upkg::PaletteEntry& entry : palette.entries) {
        hash.add(entry.r);
        hash.add(entry.g);
        hash.add(entry.b);
    }
    return hash.value;
}

} // namespace uta::umat

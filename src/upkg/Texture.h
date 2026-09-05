// Palettised textures and their palettes.
//
// docs/specs/UTA-0004-typed-level-content.md SS 4.6 and SS 4.7.
//
// LIFETIME: a Mip holds a VIEW of the caller's package bytes and copies no
// pixels (SS 3.3 item 3, INV-7). Mip pixels are the largest thing in a
// package, and copying them would reverse Package.h's own decision by the back
// door. The caller must keep those bytes alive for as long as it reads a Mip.
//
// This reader does not convert, decompress or interpret a pixel. Turning a
// 1999 texture into a material is `umat` (UTA-0009); ut-dump wants to show
// what the file holds. Both want the bytes as they are.

#ifndef UTA_UPKG_TEXTURE_H
#define UTA_UPKG_TEXTURE_H

#include "core/Error.h"
#include "upkg/Package.h"

#include <cstdint>
#include <span>
#include <vector>

namespace uta::upkg {

/// One level of a mip chain. `pixels` is a view into the package's bytes.
struct Mip {
    std::span<const std::byte> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bitsWidth = 0;
    std::uint8_t bitsHeight = 0;
};

struct Texture {
    std::vector<Mip> mips;
    /// Empty unless the export's `bHasComp` property is true (INV-6). The
    /// compression is named by its `CompFormat` property and is not
    /// interpreted here.
    std::vector<Mip> compressedMips;
};

struct PaletteEntry {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

struct Palette {
    std::vector<PaletteEntry> entries;
};

/// True for the classes SS 4.6 models: `Texture` and the three subclasses that
/// share its layout, plus `FireTexture`, which shares it and adds a tail.
/// A class outside this set is refused rather than read as its nearest
/// relative (INV-8) -- `WaveTexture` is the measured case.
[[nodiscard]] bool isModelledTextureClass(std::string_view className) noexcept;

/// A `Texture`-family export: its mip chain, and the compressed chain where
/// the export carries one.
[[nodiscard]] Result<Texture> readTexture(const Package& package,
                                          const ExportEntry& entry);

/// A `Palette` export: the colours a palettised texture's indices name.
[[nodiscard]] Result<Palette> readPalette(const Package& package,
                                          const ExportEntry& entry);

} // namespace uta::upkg

#endif // UTA_UPKG_TEXTURE_H

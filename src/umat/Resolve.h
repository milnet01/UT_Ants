// umat: resolve a palettised texture level to RGBA8 -- the first stage of
// turning a 1999 texture into a material.
//
// docs/specs/UTA-0009-material-from-texture.md SS 4.2.
//
// TWO VARIANTS, because palette index 0 means two things. On a masked surface
// it is see-through; on every other surface it is an ordinary colour, and
// surfaces sharing one texture disagree about which (SS 2 item 4). The bake
// asks for the masked variant only where a surface uses the texture masked.

#pragma once

#include "core/Error.h"
#include "umat/Material.h"
#include "upkg/Texture.h"

namespace uta::umat {

/// One palettised level to RGBA8. Each texel takes its palette entry's r, g
/// and b; the entry's own alpha is ignored.
///
/// Opaque (`masked` false): alpha 255 everywhere, index 0 an ordinary colour.
/// Masked: alpha 0 on index-0 texels and 255 elsewhere, and the index-0
/// texels' colour FILLED from their neighbours (SS 4.2), so no later filter
/// bleeds the index-0 colour into the edge of a cutout.
///
/// InvalidArgument for a zero dimension, a byte count that is not width *
/// height, or an index past the palette.
[[nodiscard]] Result<Image> resolve(const upkg::Mip& level, const upkg::Palette& palette,
                                    bool masked);

} // namespace uta::umat

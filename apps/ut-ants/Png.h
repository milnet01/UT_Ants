// Encoding an RGBA8 image as PNG -- UTA-0191's capture folder.
//
// docs/specs/UTA-0014-vulkan-draw-path.md SS 4.10 fixes the renderer's colour
// target at VK_FORMAT_R8G8B8A8_SRGB and has `readback` return those bytes
// tightly packed in that channel order, so a frame arrives here already
// display-encoded and needs no transfer applied.
//
// HERE rather than in core, although core is where Md5 and Sha256 live:
// docs/design.md rule 1 is that core depends on nothing beyond the C++
// standard library, and this wraps a vendored encoder. INV-14's assertion
// would not have caught it -- that check reads uta_core's LINK_LIBRARIES, and
// a third-party source compiled straight into the library adds no link edge --
// so the rule would have been breached with the build still green.
// third_party/stb/README.md records the copy. Rule 19 permits it here.
//
// This RETURNS the bytes rather than writing them. Every write in this project
// goes through core/FileSystem.h's writeFileAtomically, which renames over the
// destination so a crash leaves the old bytes rather than half the new ones,
// and an encoder that opened its own file would bypass that.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/Error.h"

namespace uta::client {

/// Bytes one RGBA8 pixel occupies.
inline constexpr std::size_t PNG_BYTES_PER_PIXEL = 4;

/// Encode `pixels` as a PNG.
///
/// `pixels` is RGBA8, tightly packed, top row first, and must be exactly
/// `width * height * PNG_BYTES_PER_PIXEL` bytes. A zero width or height, or a
/// span of any other length, is InvalidArgument -- so a caller that resized
/// between drawing and encoding is refused rather than writing a torn image.
/// IoFailure when the encoder itself fails, which it does only on allocation.
[[nodiscard]] Result<std::vector<std::byte>> encodePng(std::span<const std::byte> pixels,
                                                       std::uint32_t width, std::uint32_t height);

} // namespace uta::client

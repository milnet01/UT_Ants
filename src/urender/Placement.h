// Where things are: the camera's view and projection, UTA-0075's jitter, and a
// mover's model matrix. Device-free, so SS 4.12's first tier can grade each.
//
// INTERNAL, and deliberately free of Vulkan and glm in its signatures: the
// device-free tests include it, and a matrix here is the column-major float
// array a shader reads (ShaderTypes.h).

#pragma once

#include "ubundle/Bundle.h"
#include "urender/Renderer.h"
#include "urender/ShaderTypes.h"

#include <array>
#include <cstdint>

namespace uta::urender {

/// World to view. View space has +X right, +Y up and +Z along the view
/// direction, the camera's own axes: UTA-0119 SS 4.5's Y * P * R applied to
/// +Y, +Z and +X. UT99's units are kept; nothing is converted.
[[nodiscard]] gpu::Mat4 viewOf(const Camera& camera) noexcept;

/// View to Vulkan clip space: +Y down, depth 0 at the near plane and 1 at the
/// far one. The handedness and depth range are this library's alone (SS 4.3),
/// so no caller has a convention to match.
[[nodiscard]] gpu::Mat4 projectionOf(const Camera& camera, std::uint32_t width, std::uint32_t height) noexcept;

/// SS 4.11 provision 1: frame `frame`'s sub-pixel offset, in pixels, each
/// component in [-0.5, 0.5). A Halton sequence, base 2 across and base 3 down,
/// over eight frames.
[[nodiscard]] std::array<float, 2> haltonJitter(std::uint64_t frame) noexcept;

/// `projection` shifted by `pixels` on a target `width` by `height`.
[[nodiscard]] gpu::Mat4 jittered(const gpu::Mat4& projection, std::array<float, 2> pixels,
                                 std::uint32_t width, std::uint32_t height) noexcept;

/// A mover's pivot space to the world: UTA-0119 SS 4.5's
/// `location + postScale * (Y * P * R * q)`, with the exact sine and cosine of
/// 2 pi angle / 65536. Graded by INV-8 against tests/support/FCoordsPort.h.
[[nodiscard]] gpu::Mat4 moverModel(const ubundle::MoverShape& mover) noexcept;

/// The matrix a normal is carried by under `model`: the inverse transpose of
/// its upper 3x3, in a mat4. Zero where `model` is singular -- a zero
/// postScale, which SS 6 draws as a collapsed plane.
[[nodiscard]] gpu::Mat4 normalMatrixOf(const gpu::Mat4& model) noexcept;

[[nodiscard]] gpu::Mat4 multiply(const gpu::Mat4& a, const gpu::Mat4& b) noexcept;
[[nodiscard]] gpu::Mat4 identity() noexcept;

} // namespace uta::urender

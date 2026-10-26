// A mover placed in the level by the bake, and the geometry AOCC and SMSK are
// baked over -- docs/specs/UTA-0439-still-movers.md SS 4.3.
//
// The renderer places movers with exact sine and cosine (UTA-0119 SS 3
// decision 7); the bake uses LightModel.h's, so its bytes are the same on
// every compiler (UTA-0011 INV-5).

#pragma once

#include "ubake/CollisionQuery.h"
#include "ubundle/Bundle.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace uta::ubake {

/// UTA-0119 SS 4.5: a pivot-space point `q` placed at
/// location + postScale * (Y P R q).
[[nodiscard]] Vec3 placed(const Vec3& location, const std::array<std::int32_t, 3>& rotation,
                          const std::array<float, 3>& postScale, const Vec3& q) noexcept;

/// SS 4.3: `level` with each still mover's shape appended in `movers`' order,
/// placed in the world, or none when no mover is still. Its vertices are
/// GEOM's and then each still mover's, which is the order AOCC's and SMSK's
/// vertex entries take (SS 4.2).
[[nodiscard]] std::optional<ubundle::Geometry> chartedGeometry(const ubundle::Geometry& level,
                                                               const std::vector<ubundle::MoverShape>& movers);

} // namespace uta::ubake

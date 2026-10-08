// The light each material sends on, as the probe bake took it --
// docs/specs/UTA-0292-reference-path-tracer.md SS 4.2.
//
// A FILE OF ITS OWN because only UTA-0292's reference tracer reads it: the
// bundle keeps compressed textures, and the bake takes albedo from the picture
// before compression, so a tool reading a bundle cannot work these out again.
//
// One line per material: its id, a tab, then albedo r g b, emission r g b and
// 1 or 0 for an unlit liquid, space-separated. Numbers are written in the
// shortest form that reads back to the same bits (INV-2).

#pragma once

#include "core/Error.h"
#include "ubake/LightProbes.h"

#include <istream>
#include <ostream>
#include <span>
#include <string>
#include <vector>

namespace uta::ubake {

/// What the probe bake's albedo and own lookups returned for one material.
struct MaterialLight {
    std::string id;
    Rgb albedo; ///< DEFAULT_ALBEDO where the material has no opaque pixel
    OwnLight own;
};

void writeMaterialLight(std::ostream& out, std::span<const MaterialLight> materials);

/// MalformedData naming the line, when a line does not read.
[[nodiscard]] Result<std::vector<MaterialLight>> readMaterialLight(std::istream& in);

} // namespace uta::ubake

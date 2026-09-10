// umat: generate one PBR material from one texture's base level.
//
// docs/specs/UTA-0009-material-from-texture.md SS 4.6 and SS 4.7.
//
// The pipeline: enlarge the base (SS 4.3), build the base colour's and the
// height's mip chains (SS 4.5), derive normal, roughness and emissive per level
// (SS 4.4), then block-compress each map with compress() from Material.h.
// Nothing here runs an AI model, and nothing shrinks a texture to make it fit:
// the budget is enforceBudget's.

#pragma once

#include "core/Error.h"
#include "core/Jobs.h"
#include "ubundle/Bundle.h"
#include "umat/Material.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace uta::umat {

/// The maps a material carries, in the order Material::maps holds them.
enum class MapKind : std::uint8_t { Base, Normal, Rough, Height, Emit };

struct MaterialSettings {
    /// What the material asks for; upscaleFactor decides what applies.
    std::uint32_t requestedUpscale = ubundle::MAX_UPSCALE_FACTOR;
    /// One value per material, never a map (SS 3 decision 3).
    bool metallic = false;
    std::uint8_t baseRoughness = 191;
    bool emissive = false;
    std::uint8_t emissiveThreshold = 192;
};

struct Material {
    std::string id;
    bool metallic = false;
    /// In MapKind order. Emit is absent unless settings.emissive.
    std::vector<ubundle::CompressedTexture> maps;
};

/// `<package>.<path>[#masked]`, lower-cased (SS 4.6). `path` is each group
/// holding the texture, outermost first, then its name, already joined by '.'.
/// The bundle and the renderer look maps up by this name.
[[nodiscard]] std::string materialId(std::string_view package, std::string_view path,
                                     bool masked);

/// One material from an RGBA base level: a resolved texture or a
/// replacement, taken as given. Its dimensions become every map's
/// sourceWidth and sourceHeight, and each map is named `<id>:<kind>`.
///
/// InvalidArgument for a base that is not RGBA, not a power of two in [1,
/// 8192] in each axis, or not the size its dimensions say; and every refusal
/// of compress().
[[nodiscard]] Result<Material> generate(std::string id, const Image& base,
                                        const MaterialSettings& settings, JobSystem& jobs);

} // namespace uta::umat

// A bundle's materials on the GPU -- docs/specs/UTA-0014-vulkan-draw-path.md
// SS 4.5 and SS 4.10.
//
// INTERNAL (includes Vulkan).
//
// A MATS record carries only its id and metallic; its maps are the TEXS
// entries named `<id>:<map>` (UTA-0011 SS 4.10). Every map is one entry in a
// bindless texture array, and a material is a record of indices into it.
//
// NOTHING IS SKIPPED FOR BEING MISSING. An empty id, an id MATS does not
// carry, and a MATS id with no maps all draw with the built-in default
// material, so a bake that lost a material is visible instead of invisible;
// each id is logged once rather than per frame (SS 6).

#pragma once

#include "core/Error.h"
#include "ubundle/Bundle.h"
#include "urender/Device.h"
#include "urender/Resources.h"
#include "urender/ShaderTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uta::urender {

/// Index 0 in every MaterialSet: the built-in material.
inline constexpr std::uint32_t DEFAULT_MATERIAL = 0;

class MaterialSet {
public:
    /// Upload every map `bundle`'s materials name, and the defaults.
    [[nodiscard]] static Result<MaterialSet> upload(Gpu& gpu, const ubundle::Bundle& bundle);

    /// The material a batch naming `id` draws with.
    [[nodiscard]] std::uint32_t indexOf(const std::string& id);

    [[nodiscard]] const std::vector<Image>& textures() const noexcept { return textures_; }
    [[nodiscard]] const Buffer& records() const noexcept { return records_; }

private:
    std::vector<MemoryBlock> blocks_; ///< declared before the images bound into them
    std::vector<Image> textures_;
    Buffer records_;
    std::unordered_map<std::string, std::uint32_t> byId_;
    std::unordered_set<std::string> reported_;
};

} // namespace uta::urender

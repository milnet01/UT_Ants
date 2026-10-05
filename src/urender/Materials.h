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
#include "ufire/Fire.h"
#include "urender/Device.h"
#include "urender/Resources.h"
#include "urender/ShaderTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uta::urender {

/// Index 0 in every MaterialSet: the built-in material.
inline constexpr std::uint32_t DEFAULT_MATERIAL = 0;

/// UTA-0286 SS 4.4: the most steps an unpinned draw runs a fire, so a stalled
/// frame does not stall the next.
inline constexpr std::uint64_t FIRE_CATCH_UP_STEPS = 16;

class MaterialSet {
public:
    /// Upload every map `bundle`'s materials name, and the defaults. The
    /// default base is magenta with `showMissing`, else neutral grey (UTA-0177).
    [[nodiscard]] static Result<MaterialSet> upload(Gpu& gpu, const ubundle::Bundle& bundle, bool showMissing);

    /// The material a batch naming `id` draws with.
    [[nodiscard]] std::uint32_t indexOf(const std::string& id);

    [[nodiscard]] const std::vector<Image>& textures() const noexcept { return textures_; }
    [[nodiscard]] const Buffer& records() const noexcept { return records_; }

    /// UTA-0263 SS 4.2: the first FLAME_RAMPS entry of MATS record `record`'s
    /// flame look, or gpu::NONE where it has none or there is no such record.
    [[nodiscard]] std::uint32_t rampOf(std::uint32_t record) const noexcept;
    /// Every flame look's eight entries; one zero entry where there is none.
    [[nodiscard]] const Buffer& ramps() const noexcept { return ramps_; }
    /// UTA-0105 SS 4.4: every liquid look, in MATS order; one zero entry where
    /// there is none.
    [[nodiscard]] const Buffer& liquids() const noexcept { return liquids_; }

    /// UTA-0286 SS 4.4: run every fire look to `seconds` of light time and
    /// colour each one that stepped into the staging buffer. A light time
    /// below the steps already run restarts the fire from its primed state;
    /// unpinned, a fire runs at most FIRE_CATCH_UP_STEPS a call.
    void advanceFires(double seconds, bool pinned);
    /// Record the copies of every picture advanceFires changed. The renderer
    /// waits on each frame, so one staging buffer serves every frame.
    void recordFireUploads(VkCommandBuffer commands);

private:
    /// UTA-0286 SS 4.4: one fire look's replay, drawn into `texture`.
    struct FireReplay {
        std::uint32_t texture = 0;
        ufire::Fire primed;
        ufire::Fire fire;
        std::array<std::array<std::uint8_t, 3>, 256> palette{};
        bool masked = false;
        double stepsPerSecond = 0;
        std::uint64_t stepsRun = 0;
        VkDeviceSize stagingOffset = 0;
        bool changed = false;
    };

    std::vector<MemoryBlock> blocks_; ///< declared before the images bound into them
    std::vector<Image> textures_;
    Buffer records_;
    Buffer ramps_;
    Buffer liquids_;
    std::vector<FireReplay> fires_;
    Buffer fireStaging_;
    std::vector<std::uint32_t> rampByRecord_; ///< by MATS index
    std::unordered_map<std::string, std::uint32_t> byId_;
    std::unordered_set<std::string> reported_;
};

} // namespace uta::urender

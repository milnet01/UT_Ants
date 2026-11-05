// The props a map places as models, as PROP --
// docs/specs/UTA-0341-mesh-decorations.md SS 4.1 to SS 4.5.
//
// A FILE OF ITS OWN because it changes for its own reason: which actors are
// props, how UT99 places a mesh, and which picture a face wears are UT99's
// rules, and nothing else in the bake moves when one is corrected.
//
// TWO STEPS, because the materials come between them. findProps reads each
// prop's mesh and says where each face's picture comes from; the bake makes
// those pictures with the level's (Bake.cpp, step 7); placeProp then places
// the frame in the world, wearing the materials made.
//
// THE SAME BYTES ON EVERY COMPILER: placement in double, through
// LightModel.h's portable sine.

#pragma once

#include "core/Error.h"
#include "ubundle/Bundle.h"
#include "umap/Rooms.h"
#include "upkg/Class.h"
#include "upkg/Mesh.h"
#include "upkg/Package.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uta::ubake {

/// SS 4.4: the most a skin only props wear is enlarged, each way. Measured
/// 2026-11-05: at the default of 4, MH-Vale_Evelyn's prop skins took its
/// texture working set from 711,698,376 bytes to 1,180,221,888, past the
/// budget; a prop is seen from further off than the floor under it.
inline constexpr std::uint32_t PROP_UPSCALE = 2;

/// SS 4.4: where one material slot's picture comes from: an object path
/// PLAC carries (MultiSkins, Skin, Texture), or a reference the mesh's own
/// package holds (its Textures). Empty `path` and a null `reference` is no
/// picture.
struct SkinSource {
    std::string path; ///< folded `<package>.<outers>.<name>`, as PLAC stores an object
    const upkg::Package* holder = nullptr; ///< the mesh's package, for `reference`
    std::string holderName;                ///< its folded name
    upkg::ObjectReference reference;
    [[nodiscard]] bool empty() const noexcept {
        return path.empty() && reference.kind() == upkg::ObjectReferenceKind::Null;
    }
};

/// SS 4.1: one prop, its mesh read, ready to place once its materials exist.
struct PropSite {
    std::uint32_t exportIndex = 0; ///< the actor's, as PLAC's
    std::string actorPath;         ///< for the bake report
    upkg::LodMesh mesh;
    std::size_t frame = 0;         ///< SS 4.3: the frame drawn
    std::vector<SkinSource> skins; ///< one per mesh material, SS 4.4's first non-empty source
    std::uint32_t addedFlags = 0;  ///< SS 4.4: Style's and bUnlit's polyFlags
    std::array<float, 3> location{};
    std::array<float, 3> prePivot{};
    std::array<std::int32_t, 3> rotation{}; ///< pitch, yaw, roll
    float drawScale = 1;
};

/// A prop left out, and why -- the bake report's `skippedProps`.
struct SkippedProp {
    std::string actor;
    std::string reason;
};

struct FoundProps {
    std::vector<PropSite> props; ///< in PLAC's order
    std::vector<SkippedProp> skipped;
};

/// SS 4.1: every placed actor that is a prop. `map` and `mapName` (folded)
/// resolve an object PLAC names in the map itself; others go through
/// `resolver`. A mesh that does not resolve or read skips its prop.
[[nodiscard]] FoundProps findProps(const ubundle::Placements& placements, const upkg::Package& map,
                                   std::string_view mapName, const upkg::PackageResolver& resolver);

/// The MATS id a prop's material slot wears with these extra flags, or none
/// where its picture was not made.
using PropMaterialLookup = std::function<std::optional<std::string>(const SkinSource& skin, bool masked)>;

/// SS 4.3 and SS 4.4: `prop`'s frame in world space under GEOM's rules,
/// batched by material and polyFlags, each vertex in the zone at the actor's
/// location. A face whose slot has no picture is dropped.
[[nodiscard]] ubundle::Geometry placeProp(const PropSite& prop, const PropMaterialLookup& materials,
                                          const umap::RoomMap& rooms, std::size_t zoneCount);

/// SS 4.5: `parts` merged into one Geometry under GEOM's rules: batches
/// ascending by material, polyFlags and pan rate, each part's triangles kept
/// in order within its batch.
[[nodiscard]] ubundle::Geometry mergeGeometry(const std::vector<const ubundle::Geometry*>& parts);

/// A placed prop's world-space shape by its actor's export index, or null
/// where that actor is no prop -- what a recipe lamp's mesh fitting copies
/// (SS 4.5).
using PropShapeLookup = std::function<const ubundle::Geometry*(std::uint32_t exportIndex)>;

/// SS 4.4: whether a slot's polyFlags ask for the masked variant.
[[nodiscard]] bool wantsMasked(std::uint32_t polyFlags) noexcept;

} // namespace uta::ubake

// ut-export's scene: a baked map as ExperiRays' glTF 2.0 binary -- UTA-0476.
//
// The format is ExperiRays' EXRAY-0001 (its docs/specs/EXRAY-0001-gltf-extras.md),
// which UT_Ants reviewed and accepted. This file binds to it and cites its
// sections; UT_Ants keeps no copy of the format.
//
// TWO STEPS, so the meaning can be tested apart from the bytes: buildScene
// turns a bundle into glTF's axes, metres and winding, and writeGlb lays that
// out as a file. A test reads buildScene's answer without parsing glTF.
//
// NO IMAGES: a bundle holds its textures as BC7, BC4 and BC5 blocks and
// nothing here decodes them, so every material is white. ExperiRays agreed,
// 2026-11-17: its UT_Ants hypotheses judge light, not albedo. alphaMode is
// still set, so a cut-out reads as one once images come.

#pragma once

#include "core/Error.h"
#include "ubundle/Bundle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace uta::tools::exporter {

/// Unreal units in a metre -- EXRAY-0001 SS 4.2, from Epic's 2 cm a unit.
inline constexpr double UNITS_PER_METRE = 50.0;

using Vec3 = std::array<double, 3>;
using Quat = std::array<double, 4>;                ///< x, y, z, w
using Mat3 = std::array<std::array<double, 3>, 3>; ///< rows

/// A UT point in glTF's axes and metres: (X, Z, Y) / s -- SS 4.2. The mapping
/// is a mirror, which is why winding is tested rather than assumed.
[[nodiscard]] Vec3 toGltf(const Vec3& ut) noexcept;

/// UTA-0119 SS 4.5's Y * P * R for pitch, yaw and roll, in UT's own axes.
[[nodiscard]] Mat3 rotationOf(const std::array<std::int32_t, 3>& rotation) noexcept;

/// A UT rotation as glTF sees it, M R M with M the mapping's mirror -- SS 4.2.
[[nodiscard]] Mat3 toGltf(const Mat3& ut) noexcept;

/// The unit quaternion of a rotation matrix.
[[nodiscard]] Quat quaternionOf(const Mat3& m) noexcept;

/// `ut:<sha256 of the .unr>:<sha256 of the .utab>`, lower-case hex: SS 4.2's
/// key as ExperiRays amended it at UT_Ants' request, 2026-11-17. A recipe edit
/// renumbers lights under one baker revision; it always changes the bundle.
[[nodiscard]] std::string sceneKeyOf(std::span<const std::byte> unr, std::span<const std::byte> utab);

struct Material {
    std::string name; ///< the MATS id, or empty
    std::uint32_t polyFlags = 0;
    bool doubleSided = false;
    enum class Alpha : std::uint8_t { Opaque, Mask, Blend } alpha = Alpha::Opaque;
};

struct Primitive {
    std::uint32_t material = 0;
    std::vector<std::uint32_t> indices;       ///< three a triangle, into the mesh's vertices
    std::vector<std::uint32_t> triangleChart; ///< one a triangle; MASK_NO_CHART for none
    /// UTA-0496: one a triangle, EXRAY-0001 SS 4.4's triangle_class.
    std::vector<std::uint8_t> triangleClass;
};

/// UTA-0496: EXRAY-0001 SS 4.4's classes UT_Ants writes. A sky backdrop is
/// met by viewer rays alone; a surface light passes through is met by viewer
/// rays and passed by shadow and bounce rays, as ubake's baker passes it.
inline constexpr std::uint8_t CLASS_SURFACE = 0, CLASS_SKY = 4, CLASS_LIGHT_PASSES = 5;

/// The class of a triangle whose batch carries `polyFlags`.
[[nodiscard]] std::uint8_t triangleClassOf(std::uint32_t polyFlags);

struct Mesh {
    std::vector<std::array<float, 3>> positions; ///< glTF axes, metres
    std::vector<std::array<float, 3>> normals;   ///< unit length
    std::vector<std::array<float, 2>> uvs;
    std::vector<std::uint8_t> zones;
    std::vector<Primitive> primitives;
};

/// KHR_lights_punctual's light, in the engine's own linear brightness -- SS 4.5.
struct PunctualLight {
    enum class Type : std::uint8_t { Directional, Point, Spot } type = Type::Point;
    std::array<float, 3> color{1, 1, 1}; ///< each channel at most 1
    float intensity = 0;
    std::optional<float> range; ///< metres; absent for no cutoff
    float outerConeAngle = 0;   ///< radians; spots only
    std::uint32_t utIndex = 0;  ///< MaskPair::light's numbering
    ubundle::Light native;      ///< the bundle's record, untouched
    /// UTA-0494: whether KHR_lights_punctual lists it. A special-lit light
    /// reaches only special-lit surfaces, which ExperiRays' lab model cannot
    /// tell apart, so it is written as its node's native_light alone.
    bool punctual = true;
};

struct Node {
    std::string name;
    std::string id;     ///< xr.id
    std::string motion; ///< "static", "moving" or "sway"
    std::optional<std::uint32_t> mesh;
    std::optional<std::uint32_t> light;
    Vec3 translation{};
    Quat rotation{0, 0, 0, 1};
    Vec3 scale{1, 1, 1};
};

/// SS 4.4's light pairs, one element each.
struct LightPairs {
    std::vector<std::uint32_t> chart, light;
    std::vector<std::uint8_t> moverReach, allLit;
};

/// What buildScene did that its output does not show.
struct Report {
    std::size_t triangles = 0;
    std::size_t reversed = 0;      ///< reordered so the cross product follows the normal
    std::size_t edgeOn = 0;        ///< cross product zero or across the normal: left as stored
    std::size_t swayTriangles = 0;
    std::size_t mixedCharts = 0;   ///< corners naming different charts; the first corner's is kept
    std::size_t zeroNormals = 0;   ///< replaced by the triangle's own
    std::size_t bakedMovers = 0;   ///< put in world space: a scale glTF's TRS cannot carry
    std::size_t skippedSpots = 0;  ///< cone 0, which lights nothing (SS 4.5)
    std::size_t droppedPairs = 0;  ///< naming a lamp of the other added-lamps state
};

struct Scene {
    std::string sceneKey;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Node> nodes;
    std::vector<PunctualLight> lights;
    std::optional<LightPairs> pairs;
    Report report;
};

/// Whether glTF's T * R * S says what UT's T * S * R does for `mover`: its
/// scale is uniform, or there is no rotation for it to come before. Where it
/// does not, buildScene puts the mover's mesh in world space under a node at
/// the origin.
[[nodiscard]] bool trsCarries(const ubundle::MoverShape& mover) noexcept;

/// `bundle` laid out as SS 4 says. With `addedLamps`, the scene the game draws
/// by default: LITE, and the recipe's added lamps with their fittings.
/// Without, LITE and the map's own lights the recipe switched off. Pairs naming
/// the other state's lamps are dropped, so no pair names a missing light. A
/// recipe's sun is a directional light either way.
///
/// No mesh is left without a vertex: ExperiRays' reader refuses one.
///
/// Refuses a bundle with no GEOM, and any non-finite position, normal or uv.
[[nodiscard]] Result<Scene> buildScene(const ubundle::Bundle& bundle, std::string sceneKey, bool addedLamps);

/// `scene` as a glTF 2.0 binary file, laid out as SS 4.1 says.
[[nodiscard]] std::vector<std::byte> writeGlb(const Scene& scene);

} // namespace uta::tools::exporter

// ut-export's scene -- UTA-0476. Scene.h says what binds it.

#include "Scene.h"

#include "JsonNumbers.h"
#include "common/Json.h"
#include "core/Angles.h"
#include "core/Sha256.h"
#include "ubake/Geometry.h"
#include "ubake/LightModel.h"
#include "ubake/SurfaceRays.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string_view>

namespace uta::tools::exporter {

namespace {

using ubundle::Geometry;
using ubundle::GeometryVertex;
using ubundle::MASK_NO_CHART;

Vec3 operator-(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 operator+(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 apply(const Mat3& m, const Vec3& v) {
    return {dot(m[0], v), dot(m[1], v), dot(m[2], v)};
}
Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 out{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
    return out;
}
Vec3 widen(const std::array<float, 3>& v) { return {v[0], v[1], v[2]}; }
std::array<float, 3> narrow(const Vec3& v) {
    return {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
}
bool finite(const Vec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

/// A rotation taking glTF's -Z, where KHR_lights_punctual points a light, to
/// +X, where a UT rotation points one: a quarter turn about Y.
constexpr Mat3 MINUS_Z_TO_PLUS_X{{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}};

/// Where a pivot-space point lands in UT's world: location + postScale * (R q),
/// UTA-0119 SS 4.5.
struct Place {
    Mat3 rotation{};
    Vec3 location{};
    Vec3 scale{1, 1, 1};

    [[nodiscard]] Vec3 point(const Vec3& q) const {
        const Vec3 r = apply(rotation, q);
        return {location[0] + scale[0] * r[0], location[1] + scale[1] * r[1], location[2] + scale[2] * r[2]};
    }
    /// A normal under that map: S^-1 R n, which the caller normalises.
    [[nodiscard]] Vec3 normal(const Vec3& n) const {
        const Vec3 r = apply(rotation, n);
        return {r[0] / scale[0], r[1] / scale[1], r[2] / scale[2]};
    }
};

Place placeOf(const ubundle::MoverShape& mover) {
    return {rotationOf(mover.rotation), widen(mover.location), widen(mover.postScale)};
}

Material::Alpha alphaOf(std::uint32_t polyFlags) {
    using namespace ubake; // the PF_ constants' one home
    if ((polyFlags & (PF_TRANSLUCENT | PF_MODULATED)) != 0) return Material::Alpha::Blend;
    if ((polyFlags & PF_MASKED) != 0) return Material::Alpha::Mask;
    return Material::Alpha::Opaque;
}

/// Appends GEOM-shaped geometry to the scene's meshes.
class MeshWriter {
public:
    MeshWriter(Scene& scene, Report& report) : scene_(scene), report_(report) {}

    /// `geometry` into `mesh`, and where `sway` is given, each triangle with a
    /// swaying corner into it instead (EXRAY-0001 SS 4.3: one pinned corner
    /// still moves). `place` moves pivot-space points into the world; `charts`
    /// is one SMSK chart a vertex, or empty for none.
    Result<void> add(const Geometry& geometry, const Place* place, std::span<const std::uint32_t> charts,
                     std::uint32_t mesh, std::optional<std::uint32_t> sway) {
        std::vector<std::uint32_t> toMesh(geometry.vertices.size(), UNSET), toSway;
        if (sway) toSway.assign(geometry.vertices.size(), UNSET);
        for (const ubundle::GeometryBatch& batch : geometry.batches) {
            const std::uint32_t material = materialOf(batch);
            for (std::uint32_t at = batch.firstIndex; at + 3 <= batch.firstIndex + batch.indexCount; at += 3) {
                std::array<std::uint32_t, 3> corner{geometry.indices[at], geometry.indices[at + 1],
                                                    geometry.indices[at + 2]};
                const bool swaying = sway && std::ranges::any_of(corner, [&](std::uint32_t v) {
                    return geometry.vertices[v].swayWeight > 0;
                });
                if (swaying) ++report_.swayTriangles;
                const std::uint32_t into = swaying ? *sway : mesh;
                UTA_CHECK(triangle(geometry, place, charts, corner, into, material, swaying ? toSway : toMesh));
            }
        }
        return {};
    }

private:
    static constexpr std::uint32_t UNSET = 0xFFFFFFFFu;

    struct Corner {
        Vec3 position, normal;
    };

    Corner cornerOf(const GeometryVertex& vertex, const Place* place) const {
        Vec3 p = widen(vertex.position), n = widen(vertex.normal);
        if (place != nullptr) {
            p = place->point(p);
            n = place->normal(n);
        }
        return {toGltf(p), toGltf(n)};
    }

    Result<void> triangle(const Geometry& geometry, const Place* place, std::span<const std::uint32_t> charts,
                          std::array<std::uint32_t, 3> corner, std::uint32_t mesh, std::uint32_t material,
                          std::vector<std::uint32_t>& remap) {
        ++report_.triangles;
        std::array<Corner, 3> c{};
        for (int k = 0; k < 3; ++k) c[k] = cornerOf(geometry.vertices[corner[k]], place);
        // SS 4.2: (v1 - v0) x (v2 - v0) follows the surface's normal, whatever
        // order the bundle stored -- the mapping is a mirror.
        Vec3 face = cross(c[1].position - c[0].position, c[2].position - c[0].position);
        const double along = dot(face, c[0].normal + c[1].normal + c[2].normal);
        if (along < 0) {
            std::swap(corner[1], corner[2]);
            face = {-face[0], -face[1], -face[2]};
            ++report_.reversed;
        } else if (!(along > 0)) {
            ++report_.edgeOn;
        }

        std::uint32_t chart = MASK_NO_CHART;
        if (!charts.empty()) {
            chart = charts[corner[0]];
            if (charts[corner[1]] != chart || charts[corner[2]] != chart) ++report_.mixedCharts;
        }

        Mesh& out = scene_.meshes[mesh];
        Primitive& primitive = primitiveOf(out, material);
        for (const std::uint32_t v : corner) {
            if (remap[v] == UNSET) {
                UTA_CHECK(vertex(geometry.vertices[v], cornerOf(geometry.vertices[v], place), face, out));
                remap[v] = static_cast<std::uint32_t>(out.positions.size() - 1);
            }
            primitive.indices.push_back(remap[v]);
        }
        primitive.triangleChart.push_back(chart);
        primitive.triangleClass.push_back(triangleClassOf(scene_.materials[material].polyFlags));
        return {};
    }

    Result<void> vertex(const GeometryVertex& source, const Corner& corner, const Vec3& face, Mesh& out) {
        if (!finite(corner.position) || !finite(corner.normal) || !std::isfinite(source.u) || !std::isfinite(source.v))
            return fail(ErrorCode::MalformedData, "a vertex has a part that is not finite");
        Vec3 normal = corner.normal;
        double length = std::sqrt(dot(normal, normal));
        if (!(length > 1e-12)) {
            ++report_.zeroNormals;
            normal = face;
            length = std::sqrt(dot(normal, normal));
            if (!(length > 1e-12)) normal = {0, 1, 0}, length = 1;
        }
        out.positions.push_back(narrow(corner.position));
        out.normals.push_back(narrow({normal[0] / length, normal[1] / length, normal[2] / length}));
        out.uvs.push_back({source.u, source.v});
        out.zones.push_back(source.zone);
        return {};
    }

    std::uint32_t materialOf(const ubundle::GeometryBatch& batch) {
        const auto found = std::ranges::find_if(scene_.materials, [&](const Material& m) {
            return m.name == batch.material && m.polyFlags == batch.polyFlags;
        });
        if (found != scene_.materials.end()) return static_cast<std::uint32_t>(found - scene_.materials.begin());
        scene_.materials.push_back(
            {batch.material, batch.polyFlags, (batch.polyFlags & ubake::PF_TWO_SIDED) != 0, alphaOf(batch.polyFlags)});
        return static_cast<std::uint32_t>(scene_.materials.size() - 1);
    }

    static Primitive& primitiveOf(Mesh& mesh, std::uint32_t material) {
        for (Primitive& p : mesh.primitives)
            if (p.material == material) return p;
        mesh.primitives.push_back({material, {}, {}});
        return mesh.primitives.back();
    }

    Scene& scene_;
    Report& report_;
};

/// The placed actor's own path, or empty where PLAC has none for it.
std::string pathOf(const ubundle::Bundle& bundle, std::uint32_t exportIndex) {
    if (!bundle.placements) return {};
    const auto& actors = bundle.placements->actors;
    const auto at = std::lower_bound(actors.begin(), actors.end(), exportIndex,
                                     [](const ubundle::ActorPlacement& a, std::uint32_t e) { return a.exportIndex < e; });
    return at != actors.end() && at->exportIndex == exportIndex ? at->path : std::string{};
}

/// SS 4.5: a light in the engine's own linear brightness, `color` at most 1.
PunctualLight punctualOf(const ubundle::Light& light, std::uint32_t utIndex, bool directional) {
    PunctualLight out;
    out.utIndex = utIndex;
    out.native = light;
    out.punctual = !light.specialLit; // UTA-0494
    if (directional) {
        out.type = PunctualLight::Type::Directional;
    } else {
        out.type = ubundle::isSpotEffect(light.effect) ? PunctualLight::Type::Spot : PunctualLight::Type::Point;
        out.range = static_cast<float>(ubundle::worldLightRadius(light.radius) / UNITS_PER_METRE);
    }
    if (out.type == PunctualLight::Type::Spot)
        out.outerConeAngle = static_cast<float>(std::acos(1.0 - light.cone / 256.0));
    ubake::Rgb colour = ubake::lightColour(light.hue, light.saturation);
    const double gain = ubake::lightIntensity(light.brightness) * light.levelBrightness;
    colour = {colour.r * gain, colour.g * gain, colour.b * gain};
    const double peak = std::max({colour.r, colour.g, colour.b});
    out.intensity = static_cast<float>(peak);
    if (peak > 0)
        out.color = {static_cast<float>(colour.r / peak), static_cast<float>(colour.g / peak),
                     static_cast<float>(colour.b / peak)};
    return out;
}

void addLight(Scene& scene, const ubundle::Light& light, std::uint32_t utIndex, std::string name, bool directional) {
    if (!directional && ubundle::isSpotEffect(light.effect) && light.cone == 0) {
        ++scene.report.skippedSpots; // SS 4.5: it lights nothing
        return;
    }
    Node node;
    node.name = std::move(name);
    node.id = "light:" + std::to_string(utIndex);
    node.motion = "static";
    node.translation = toGltf(widen(light.location));
    node.rotation = quaternionOf(multiply(toGltf(rotationOf(light.rotation)), MINUS_Z_TO_PLUS_X));
    node.light = static_cast<std::uint32_t>(scene.lights.size());
    scene.lights.push_back(punctualOf(light, utIndex, directional));
    scene.nodes.push_back(std::move(node));
}

std::uint32_t newMesh(Scene& scene) {
    scene.meshes.emplace_back();
    return static_cast<std::uint32_t>(scene.meshes.size() - 1);
}

/// Removes each mesh with no vertex, which ExperiRays' reader refuses, and
/// moves the nodes' mesh numbers down to match. No node names an empty one.
void dropEmptyMeshes(Scene& scene) {
    std::vector<std::uint32_t> movedTo(scene.meshes.size());
    std::uint32_t kept = 0;
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        movedTo[i] = kept;
        if (scene.meshes[i].positions.empty()) continue;
        if (kept != i) scene.meshes[kept] = std::move(scene.meshes[i]);
        ++kept;
    }
    scene.meshes.resize(kept);
    for (Node& node : scene.nodes)
        if (node.mesh) node.mesh = movedTo[*node.mesh];
}

} // namespace

bool trsCarries(const ubundle::MoverShape& mover) noexcept {
    const auto& s = mover.postScale;
    return (s[0] == s[1] && s[1] == s[2]) || mover.rotation == std::array<std::int32_t, 3>{};
}

Vec3 toGltf(const Vec3& ut) noexcept {
    return {ut[0] / UNITS_PER_METRE, ut[2] / UNITS_PER_METRE, ut[1] / UNITS_PER_METRE};
}

Mat3 rotationOf(const std::array<std::int32_t, 3>& rotation) noexcept {
    const double cp = core::cosineOf(rotation[0]), sp = core::sineOf(rotation[0]);
    const double cy = core::cosineOf(rotation[1]), sy = core::sineOf(rotation[1]);
    const double cr = core::cosineOf(rotation[2]), sr = core::sineOf(rotation[2]);
    const Mat3 yaw{{{cy, -sy, 0}, {sy, cy, 0}, {0, 0, 1}}};
    const Mat3 pitch{{{cp, 0, -sp}, {0, 1, 0}, {sp, 0, cp}}};
    const Mat3 roll{{{1, 0, 0}, {0, cr, sr}, {0, -sr, cr}}};
    return multiply(multiply(yaw, pitch), roll);
}

Mat3 toGltf(const Mat3& ut) noexcept {
    // M swaps the second and third axes, and is its own inverse.
    constexpr std::array<int, 3> swap{0, 2, 1};
    Mat3 out{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out[r][c] = ut[swap[r]][swap[c]];
    return out;
}

Quat quaternionOf(const Mat3& m) noexcept {
    // Shepperd's method: divide by the largest of the four candidates.
    const double trace = m[0][0] + m[1][1] + m[2][2];
    Quat q{};
    if (trace > 0) {
        const double s = std::sqrt(trace + 1.0) * 2;
        q = {(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s};
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const double s = std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
        q = {0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s};
    } else if (m[1][1] > m[2][2]) {
        const double s = std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
        q = {(m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s};
    } else {
        const double s = std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
        q = {(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s};
    }
    const double length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (double& part : q) part /= length;
    if (q[3] < 0)
        for (double& part : q) part = -part; // one sign, so equal rotations print equal
    return q;
}

std::string sceneKeyOf(std::span<const std::byte> unr, std::span<const std::byte> utab) {
    const auto hex = [](const std::array<std::byte, 32>& digest) {
        static constexpr char DIGITS[] = "0123456789abcdef";
        std::string out;
        for (const std::byte b : digest) {
            out += DIGITS[std::to_integer<unsigned>(b) >> 4];
            out += DIGITS[std::to_integer<unsigned>(b) & 15];
        }
        return out;
    };
    return "ut:" + hex(sha256(unr)) + ":" + hex(sha256(utab));
}

std::uint8_t triangleClassOf(std::uint32_t polyFlags) {
    if ((polyFlags & ubake::PF_FAKE_BACKDROP) != 0) return CLASS_SKY;
    // The baker's own set (UTA-0168), so the lab's rays pass what it passes.
    if ((polyFlags & ubake::LIGHT_PASSES_FLAGS) != 0) return CLASS_LIGHT_PASSES;
    return CLASS_SURFACE;
}

Result<Scene> buildScene(const ubundle::Bundle& bundle, std::string sceneKey, bool addedLamps) {
    if (!bundle.geometry) return fail(ErrorCode::InvalidArgument, "the bundle has no GEOM section");
    Scene scene;
    scene.sceneKey = std::move(sceneKey);
    MeshWriter writer(scene, scene.report);

    const std::span<const std::uint32_t> allCharts =
        bundle.shadowMask ? std::span<const std::uint32_t>(bundle.shadowMask->vertexChart) : std::span<const std::uint32_t>{};
    const auto chartsFor = [&](std::size_t first, std::size_t count) {
        return allCharts.empty() ? allCharts : allCharts.subspan(first, count);
    };

    // The level, with its still movers and the props and fittings that stand
    // in it; each triangle that sways goes to a node of its own (SS 4.3).
    const std::uint32_t level = newMesh(scene), sway = newMesh(scene);
    const Geometry& geometry = *bundle.geometry;
    UTA_CHECK(writer.add(geometry, nullptr, chartsFor(0, geometry.vertices.size()), level, sway));
    // UTA-0439 SS 4.2: SMSK charts still movers after GEOM, in MOVR order.
    std::size_t charted = geometry.vertices.size();
    if (bundle.movers)
        for (const ubundle::MoverShape& mover : *bundle.movers) {
            if (mover.still == 0) continue;
            const Place place = placeOf(mover);
            UTA_CHECK(writer.add(mover.geometry, &place, chartsFor(charted, mover.geometry.vertices.size()), level, sway));
            charted += mover.geometry.vertices.size();
        }
    if (bundle.props) UTA_CHECK(writer.add(*bundle.props, nullptr, {}, level, sway));
    if (bundle.lamps)
        for (const ubundle::AddedLamp& lamp : *bundle.lamps)
            if (addedLamps && !lamp.restores) UTA_CHECK(writer.add(lamp.shape, nullptr, {}, level, sway));

    for (const auto& [mesh, id, motion] : {std::tuple{level, "level", "static"}, std::tuple{sway, "level:sway", "sway"}})
        if (!scene.meshes[mesh].positions.empty()) {
            Node node;
            node.name = node.id = id;
            node.motion = motion;
            node.mesh = mesh;
            scene.nodes.push_back(std::move(node));
        }

    // SS 4.4: the movers that can move, each a moving node with its mesh.
    if (bundle.movers)
        for (const ubundle::MoverShape& mover : *bundle.movers) {
            if (mover.still != 0) continue;
            Node node;
            node.name = pathOf(bundle, mover.exportIndex);
            node.id = "mover:" + std::to_string(mover.exportIndex);
            node.motion = "moving";
            node.mesh = newMesh(scene);
            const Place place = placeOf(mover);
            if (trsCarries(mover)) {
                UTA_CHECK(writer.add(mover.geometry, nullptr, {}, *node.mesh, std::nullopt));
                node.translation = toGltf(place.location);
                node.rotation = quaternionOf(toGltf(place.rotation));
                node.scale = {place.scale[0], place.scale[2], place.scale[1]};
            } else {
                UTA_CHECK(writer.add(mover.geometry, &place, {}, *node.mesh, std::nullopt));
                ++scene.report.bakedMovers;
            }
            if (scene.meshes[*node.mesh].positions.empty()) node.mesh.reset();
            if (node.mesh) scene.nodes.push_back(std::move(node));
        }
    dropEmptyMeshes(scene);

    // SS 4.4 and 4.5: every light a pair can name, numbered as MaskPair::light.
    const std::size_t liteCount = bundle.lights ? bundle.lights->size() : 0;
    const std::size_t lampCount = bundle.lamps ? bundle.lamps->size() : 0;
    const auto lampKept = [&](std::size_t j) { return (*bundle.lamps)[j].restores != addedLamps; };
    if (bundle.lights)
        for (std::size_t i = 0; i < liteCount; ++i) {
            const ubundle::Light& light = (*bundle.lights)[i];
            addLight(scene, light, static_cast<std::uint32_t>(i), pathOf(bundle, light.exportIndex), false);
        }
    for (std::size_t j = 0; j < lampCount; ++j)
        if (lampKept(j))
            addLight(scene, (*bundle.lamps)[j].light, static_cast<std::uint32_t>(liteCount + j),
                     "lamp:" + std::to_string(j), false);
    if (bundle.sun)
        addLight(scene, ubundle::lightOfSun(*bundle.sun), static_cast<std::uint32_t>(liteCount + lampCount), "sun", true);

    if (bundle.shadowMask) {
        LightPairs pairs;
        const auto& mask = *bundle.shadowMask;
        for (std::size_t chart = 0; chart < mask.charts.size(); ++chart) {
            const ubundle::MaskChart& c = mask.charts[chart];
            for (std::uint32_t k = c.firstPair; k < c.firstPair + c.pairCount; ++k) {
                const ubundle::MaskPair& pair = mask.pairs[k];
                if (pair.light >= liteCount && pair.light < liteCount + lampCount && !lampKept(pair.light - liteCount)) {
                    ++scene.report.droppedPairs;
                    continue;
                }
                pairs.chart.push_back(static_cast<std::uint32_t>(chart));
                pairs.light.push_back(pair.light);
                pairs.moverReach.push_back(pair.moverReach);
                pairs.allLit.push_back(pair.x == ubundle::MASK_ALL_LIT && pair.y == ubundle::MASK_ALL_LIT ? 1 : 0);
            }
        }
        scene.pairs = std::move(pairs);
    }
    return scene;
}

// ---------------------------------------------------------------- the file

namespace {

/// The glTF JSON and its one binary buffer, built side by side.
class GlbWriter {
public:
    /// A buffer view over `bytes`, 4-byte aligned; its index.
    std::size_t view(std::span<const std::byte> bytes, std::optional<int> target, std::optional<int> stride) {
        bin_.resize((bin_.size() + 3) & ~std::size_t{3}, std::byte{0});
        std::ostringstream v;
        v << "{\"buffer\":0,\"byteOffset\":" << bin_.size() << ",\"byteLength\":" << bytes.size();
        if (stride) v << ",\"byteStride\":" << *stride;
        if (target) v << ",\"target\":" << *target;
        v << '}';
        bin_.insert(bin_.end(), bytes.begin(), bytes.end());
        views_.push_back(v.str());
        return views_.size() - 1;
    }

    template <typename T>
    std::size_t view(const std::vector<T>& items, std::optional<int> target, std::optional<int> stride = {}) {
        return view(std::as_bytes(std::span(items)), target, stride);
    }

    /// An accessor; `extra` is appended inside its object (min and max).
    std::size_t accessor(std::size_t view, int componentType, std::size_t count, std::string_view type,
                         std::string_view extra = {}) {
        std::ostringstream a;
        a << "{\"bufferView\":" << view << ",\"componentType\":" << componentType << ",\"count\":" << count
          << ",\"type\":\"" << type << '"' << extra << '}';
        accessors_.push_back(a.str());
        return accessors_.size() - 1;
    }

    std::vector<std::byte> finish(std::string json) const;

    std::vector<std::string> views_, accessors_;
    std::vector<std::byte> bin_;
};

constexpr int UNSIGNED_BYTE = 5121, UNSIGNED_INT = 5125, FLOAT = 5126;
constexpr int ARRAY_BUFFER = 34962, ELEMENT_ARRAY_BUFFER = 34963;

std::string quoted(std::string_view text) {
    std::ostringstream out;
    writeJsonString(out, text);
    return out.str();
}

std::string nativeLight(const PunctualLight& light) {
    const ubundle::Light& l = light.native;
    std::ostringstream o;
    o << "{\"ut_index\":" << light.utIndex << ",\"export_index\":" << l.exportIndex
      << ",\"location\":" << list(l.location) << ",\"rotation\":[" << l.rotation[0] << ',' << l.rotation[1] << ','
      << l.rotation[2] << ']';
    const std::pair<const char*, unsigned> bytes[] = {
        {"type", l.type}, {"effect", l.effect}, {"brightness", l.brightness}, {"hue", l.hue},
        {"saturation", l.saturation}, {"radius", l.radius}, {"period", l.period}, {"phase", l.phase},
        {"cone", l.cone}, {"volume_brightness", l.volumeBrightness}, {"volume_radius", l.volumeRadius},
        {"volume_fog", l.volumeFog}, {"special_lit", l.specialLit}, {"actor_shadows", l.actorShadows},
        {"corona", l.corona}, {"lens_flare", l.lensFlare}, {"strip", l.strip}};
    for (const auto& [name, value] : bytes) o << ",\"" << name << "\":" << value;
    o << ",\"strip_from\":" << list(l.stripFrom) << ",\"strip_to\":" << list(l.stripTo)
      << ",\"level_brightness\":" << number(l.levelBrightness) << '}';
    return o.str();
}

std::vector<std::byte> GlbWriter::finish(std::string json) const {
    while (json.size() % 4 != 0) json += ' ';
    std::vector<std::byte> bin = bin_;
    while (bin.size() % 4 != 0) bin.push_back(std::byte{0});
    std::vector<std::byte> out;
    const auto put32 = [&](std::uint32_t value) {
        for (int k = 0; k < 4; ++k) out.push_back(static_cast<std::byte>((value >> (8 * k)) & 0xFF));
    };
    const std::size_t total = 12 + 8 + json.size() + (bin.empty() ? 0 : 8 + bin.size());
    put32(0x46546C67u); // "glTF"
    put32(2);
    put32(static_cast<std::uint32_t>(total));
    put32(static_cast<std::uint32_t>(json.size()));
    put32(0x4E4F534Au); // "JSON"
    for (const char ch : json) out.push_back(static_cast<std::byte>(ch));
    if (!bin.empty()) {
        put32(static_cast<std::uint32_t>(bin.size()));
        put32(0x004E4942u); // "BIN"
        out.insert(out.end(), bin.begin(), bin.end());
    }
    return out;
}

std::string joined(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + items[i];
    return out + "]";
}

} // namespace

std::size_t colourMaterials(Scene& scene, std::span<const ubake::MaterialLight> materials) {
    std::size_t coloured = 0;
    for (Material& m : scene.materials) {
        const auto found = std::ranges::find(materials, m.name, &ubake::MaterialLight::id);
        if (found == materials.end()) continue;
        m.baseColor = {found->albedo.r, found->albedo.g, found->albedo.b};
        ++coloured;
    }
    return coloured;
}

std::vector<std::byte> writeGlb(const Scene& scene) {
    GlbWriter glb;

    std::vector<std::string> meshes;
    for (const Mesh& mesh : scene.meshes) {
        std::array<float, 3> low{}, high{};
        if (!mesh.positions.empty()) low = high = mesh.positions.front();
        for (const auto& p : mesh.positions)
            for (int k = 0; k < 3; ++k) low[k] = std::min(low[k], p[k]), high[k] = std::max(high[k], p[k]);
        const std::size_t n = mesh.positions.size();
        const std::size_t position = glb.accessor(glb.view(mesh.positions, ARRAY_BUFFER), FLOAT, n, "VEC3",
                                                  ",\"min\":" + list(low) + ",\"max\":" + list(high));
        const std::size_t normal = glb.accessor(glb.view(mesh.normals, ARRAY_BUFFER), FLOAT, n, "VEC3");
        const std::size_t uv = glb.accessor(glb.view(mesh.uvs, ARRAY_BUFFER), FLOAT, n, "VEC2");
        // SS 4.1: a vertex attribute starts on 4 bytes, so each zone byte is padded.
        std::vector<std::uint8_t> zones(4 * n, 0);
        for (std::size_t i = 0; i < n; ++i) zones[4 * i] = mesh.zones[i];
        const std::size_t zone = glb.accessor(glb.view(zones, ARRAY_BUFFER, 4), UNSIGNED_BYTE, n, "SCALAR");

        std::vector<std::string> primitives;
        for (const Primitive& p : mesh.primitives) {
            const std::size_t indices =
                glb.accessor(glb.view(p.indices, ELEMENT_ARRAY_BUFFER), UNSIGNED_INT, p.indices.size(), "SCALAR");
            const std::size_t chart = glb.accessor(glb.view(p.triangleChart, std::nullopt), UNSIGNED_INT,
                                                   p.triangleChart.size(), "SCALAR");
            const std::size_t kind = glb.accessor(glb.view(p.triangleClass, std::nullopt), UNSIGNED_BYTE,
                                                  p.triangleClass.size(), "SCALAR");
            std::ostringstream o;
            o << "{\"attributes\":{\"POSITION\":" << position << ",\"NORMAL\":" << normal << ",\"TEXCOORD_0\":" << uv
              << ",\"_XR_ZONE\":" << zone << "},\"indices\":" << indices << ",\"material\":" << p.material
              << ",\"mode\":4,\"extras\":{\"xr\":{\"triangle_chart\":" << chart
              << ",\"triangle_class\":" << kind << "}}}";
            primitives.push_back(o.str());
        }
        meshes.push_back("{\"primitives\":" + joined(primitives) + "}");
    }

    std::vector<std::string> materials;
    for (const Material& m : scene.materials) {
        std::ostringstream o;
        o << "{\"name\":" << quoted(m.name)
          << ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" << number(m.baseColor[0]) << ','
          << number(m.baseColor[1]) << ',' << number(m.baseColor[2])
          << ",1],\"metallicFactor\":0,\"roughnessFactor\":1}";
        if (m.doubleSided) o << ",\"doubleSided\":true";
        if (m.alpha == Material::Alpha::Mask) o << ",\"alphaMode\":\"MASK\"";
        if (m.alpha == Material::Alpha::Blend) o << ",\"alphaMode\":\"BLEND\"";
        o << ",\"extras\":{\"ut_poly_flags\":" << m.polyFlags << "}}";
        materials.push_back(o.str());
    }

    // UTA-0494: KHR_lights_punctual lists the punctual lights only, so a
    // node's index into it is that light's place among them.
    std::vector<std::string> lights;
    std::vector<std::optional<std::uint32_t>> listedAs;
    for (const PunctualLight& l : scene.lights) {
        listedAs.push_back(l.punctual ? std::optional{static_cast<std::uint32_t>(lights.size())} : std::nullopt);
        if (!l.punctual) continue;
        static constexpr const char* TYPES[] = {"directional", "point", "spot"};
        std::ostringstream o;
        o << "{\"type\":\"" << TYPES[static_cast<int>(l.type)] << "\",\"color\":" << list(l.color)
          << ",\"intensity\":" << number(l.intensity);
        if (l.range) o << ",\"range\":" << number(*l.range);
        if (l.type == PunctualLight::Type::Spot)
            o << ",\"spot\":{\"innerConeAngle\":0,\"outerConeAngle\":" << number(l.outerConeAngle) << '}';
        o << '}';
        lights.push_back(o.str());
    }

    std::vector<std::string> nodes, roots;
    for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
        const Node& n = scene.nodes[i];
        std::ostringstream o;
        o << "{\"name\":" << quoted(n.name);
        if (n.mesh) o << ",\"mesh\":" << *n.mesh;
        o << ",\"translation\":" << list(n.translation) << ",\"rotation\":" << list(n.rotation)
          << ",\"scale\":" << list(n.scale);
        if (n.light && listedAs[*n.light])
            o << ",\"extensions\":{\"KHR_lights_punctual\":{\"light\":" << *listedAs[*n.light] << "}}";
        o << ",\"extras\":{\"xr\":{\"id\":" << quoted(n.id) << ",\"motion\":" << quoted(n.motion);
        if (n.light) o << ",\"native_light\":" << nativeLight(scene.lights[*n.light]);
        o << "}}}";
        nodes.push_back(o.str());
        roots.push_back(std::to_string(i));
    }

    std::ostringstream xr;
    xr << "{\"version\":1,\"engine\":\"ut_ants\",\"scene_key\":" << quoted(scene.sceneKey)
       << ",\"source_frame\":{\"units_per_metre\":" << number(UNITS_PER_METRE)
       << ",\"axes\":\"X forward, Y right, Z up\"}";
    if (scene.pairs) {
        const LightPairs& p = *scene.pairs;
        const std::size_t count = p.chart.size();
        const std::size_t chart = glb.accessor(glb.view(p.chart, std::nullopt), UNSIGNED_INT, count, "SCALAR");
        const std::size_t light = glb.accessor(glb.view(p.light, std::nullopt), UNSIGNED_INT, count, "SCALAR");
        const std::size_t reach = glb.accessor(glb.view(p.moverReach, std::nullopt), UNSIGNED_BYTE, count, "SCALAR");
        const std::size_t lit = glb.accessor(glb.view(p.allLit, std::nullopt), UNSIGNED_BYTE, count, "SCALAR");
        xr << ",\"ut_light_pairs\":{\"chart\":" << chart << ",\"light\":" << light << ",\"mover_reach\":" << reach
           << ",\"all_lit\":" << lit << '}';
    }
    xr << '}';

    std::ostringstream json;
    json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"UT_Ants ut-export (UTA-0476)\",\"extras\":{\"xr\":"
         << xr.str() << "}}";
    if (!lights.empty())
        json << ",\"extensionsUsed\":[\"KHR_lights_punctual\"],\"extensions\":{\"KHR_lights_punctual\":{\"lights\":"
             << joined(lights) << "}}";
    json << ",\"scene\":0,\"scenes\":[{\"nodes\":" << joined(roots) << "}],\"nodes\":" << joined(nodes);
    if (!meshes.empty()) json << ",\"meshes\":" << joined(meshes); // glTF 2.0 allows no empty array
    if (!materials.empty()) json << ",\"materials\":" << joined(materials);
    json << ",\"accessors\":" << joined(glb.accessors_) << ",\"bufferViews\":" << joined(glb.views_);
    if (!glb.bin_.empty()) json << ",\"buffers\":[{\"byteLength\":" << ((glb.bin_.size() + 3) & ~std::size_t{3}) << "}]";
    json << '}';
    return glb.finish(json.str());
}

} // namespace uta::tools::exporter

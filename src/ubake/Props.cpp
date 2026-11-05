// The props a map places as models -- docs/specs/UTA-0341-mesh-decorations.md
// SS 4.1 to SS 4.5.

#include "ubake/Props.h"

#include "core/Ascii.h"
#include "ubake/Actors.h"
#include "ubake/Geometry.h"
#include "ubake/LightModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace uta::ubake {
namespace {

constexpr std::uint8_t DT_MESH = 2;
/// SS 4.4: Actor.uc's ERenderStyle.
constexpr std::uint8_t STY_MASKED = 3;
constexpr std::uint8_t STY_TRANSLUCENT = 4;
constexpr std::uint8_t STY_MODULATED = 5;
/// Actor.uc's MultiSkins has eight slots.
constexpr std::int32_t MULTI_SKINS = 8;
/// A wedge placeProp has not yet made a vertex for.
constexpr std::uint32_t NOT_MADE = 0xFFFFFFFFu;

using Vec = std::array<double, 3>;

/// A placed actor's property, own value first then its class's, at
/// `arrayIndex` -- detail::resolvedRecord for a static array's later slots.
const ubundle::PropertyRecord* recordAt(std::string_view name, ubundle::ValueKind kind, std::uint32_t arrayIndex,
                                        const ubundle::ActorPlacement& actor, const ubundle::ActorClass& actorClass) {
    if (arrayIndex == 0) return detail::resolvedRecord(name, kind, actor.properties, actorClass.defaults);
    for (const auto* list : {&actor.properties, &actorClass.defaults})
        for (const ubundle::PropertyRecord& record : *list)
            if (record.arrayIndex == arrayIndex && record.kind == kind && asciiFolded(record.name) == name)
                return &record;
    return nullptr;
}

std::string objectPath(const ubundle::PropertyRecord* record) {
    const auto* const path = record == nullptr ? nullptr : std::get_if<std::string>(&record->value);
    return path == nullptr ? std::string{} : *path;
}

/// The names of `entry`'s outers, outermost first, then its own, joined by
/// '.' and folded -- the path PLAC writes an object as (UTA-0110 SS 4.3).
std::string foldedExportPath(const upkg::Package& package, const upkg::ExportEntry& entry) {
    const auto nameOf = [&package](std::uint32_t index) {
        const auto name = package.name(index);
        return name.has_value() ? asciiFolded(*name) : std::string{};
    };
    std::string path = nameOf(entry.objectName);
    upkg::ObjectReference outer = entry.outer;
    for (std::size_t depth = 0; outer.kind() == upkg::ObjectReferenceKind::Export
                                && outer.index() < package.exports().size() && depth < package.exports().size();
         ++depth) {
        const upkg::ExportEntry& group = package.exports()[outer.index()];
        path = nameOf(group.objectName) + "." + path;
        outer = group.outer;
    }
    return path;
}

struct MeshFound {
    const upkg::Package* holder = nullptr;
    std::string holderName;
    upkg::LodMesh mesh;
};

/// SS 4.1 item 4: the LodMesh `path` names, or why not.
std::expected<MeshFound, std::string> meshAt(const std::string& path, const upkg::Package& map,
                                             std::string_view mapName, const upkg::PackageResolver& resolver) {
    const auto dot = path.find('.');
    if (dot == std::string::npos) return std::unexpected("its Mesh " + path + " names a package, not a mesh");
    const std::string package = path.substr(0, dot);
    const std::string rest = path.substr(dot + 1);
    const upkg::Package* holder = &map;
    if (package != mapName) {
        const auto found = resolver(package);
        if (!found.has_value() || *found == nullptr)
            return std::unexpected("its Mesh's package " + package + " is not in the install, or does not open");
        holder = *found;
    }
    for (const upkg::ExportEntry& entry : holder->exports()) {
        if (foldedExportPath(*holder, entry) != rest) continue;
        // A class export of the same name -- SpVale's SPWheel is both -- has
        // no class of its own; the mesh is another export.
        if (entry.objectClass.kind() == upkg::ObjectReferenceKind::Null) continue;
        const auto className = holder->objectName(entry.objectClass);
        const std::string folded = className.has_value() ? asciiFolded(*className) : std::string{};
        if (folded == "texture" || folded == "palette") continue; // a namesake, not the mesh
        if (folded != "lodmesh") return std::unexpected("its Mesh " + path + " is a " + folded + ", not a LodMesh");
        auto mesh = upkg::readLodMesh(*holder, entry);
        if (!mesh.has_value()) return std::unexpected("its Mesh " + path + " did not read: " + std::string{mesh.error().message()});
        return MeshFound{holder, package, std::move(*mesh)};
    }
    return std::unexpected("its Mesh's package " + package + " holds no " + rest);
}

/// SS 4.3: the frame `AnimSequence` and `AnimFrame` name, else 0.
std::size_t frameOf(const upkg::LodMesh& mesh, const std::string& sequence, float animFrame) {
    for (const upkg::MeshSequence& candidate : mesh.sequences) {
        if (asciiFolded(candidate.name) != sequence || candidate.frameCount <= 0) continue;
        const double within = std::floor(static_cast<double>(animFrame) * candidate.frameCount);
        const auto offset = static_cast<std::int64_t>(std::clamp(within, 0.0, candidate.frameCount - 1.0));
        const std::int64_t frame = candidate.startFrame + offset;
        return frame >= 0 && frame < mesh.animFrames ? static_cast<std::size_t>(frame) : 0;
    }
    return 0;
}

/// UTA-0119 SS 4.5's Y * P * R applied to `v`: roll about X, then pitch about
/// Y, then yaw about Z, as urender's rotationOf builds it.
Vec rotated(const Vec& v, const std::array<std::int32_t, 3>& rotation) noexcept {
    const double cp = cosineOf(rotation[0]), sp = sineOf(rotation[0]);
    const double cy = cosineOf(rotation[1]), sy = sineOf(rotation[1]);
    const double cr = cosineOf(rotation[2]), sr = sineOf(rotation[2]);
    const Vec rolled = {v[0], cr * v[1] + sr * v[2], -sr * v[1] + cr * v[2]};
    const Vec pitched = {cp * rolled[0] - sp * rolled[2], rolled[1], sp * rolled[0] + cp * rolled[2]};
    return {cy * pitched[0] - sy * pitched[1], sy * pitched[0] + cy * pitched[1], pitched[2]};
}

Vec minus(const Vec& a, const Vec& b) noexcept { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

Vec cross(const Vec& a, const Vec& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

} // namespace

bool wantsMasked(std::uint32_t polyFlags) noexcept { return (polyFlags & PF_MASKED) != 0; }

FoundProps findProps(const ubundle::Placements& placements, const upkg::Package& map, std::string_view mapName,
                     const upkg::PackageResolver& resolver) {
    FoundProps out;
    for (const ubundle::ActorPlacement& actor : placements.actors) {
        if (actor.classIndex >= placements.classes.size()) continue;
        const ubundle::ActorClass& actorClass = placements.classes[actor.classIndex];
        // SS 4.1 items 1 to 3.
        if (!actorClass.resolved || !ubundle::isOrDescendsFrom(actorClass, "engine.decoration")) continue;
        const auto get = [&](std::string_view name, ubundle::ValueKind kind) {
            return detail::resolvedRecord(name, kind, actor.properties, actorClass.defaults);
        };
        const auto* const drawType = get("drawtype", ubundle::ValueKind::Byte);
        if (drawType == nullptr || std::get<std::uint8_t>(drawType->value) != DT_MESH) continue;
        const auto* const hidden = get("bhidden", ubundle::ValueKind::Bool);
        if (hidden != nullptr && std::get<bool>(hidden->value)) continue;

        // SS 4.1 item 4.
        const std::string meshPath = objectPath(get("mesh", ubundle::ValueKind::Object));
        if (meshPath.empty()) {
            out.skipped.push_back({actor.path, "its Mesh is None"});
            continue;
        }
        auto found = meshAt(meshPath, map, mapName, resolver);
        if (!found.has_value()) {
            out.skipped.push_back({actor.path, std::move(found).error()});
            continue;
        }

        PropSite prop;
        prop.exportIndex = actor.exportIndex;
        prop.actorPath = actor.path;
        prop.mesh = std::move(found->mesh);
        const auto* const sequence = get("animsequence", ubundle::ValueKind::Name);
        const auto* const animFrame = get("animframe", ubundle::ValueKind::Float);
        prop.frame = frameOf(prop.mesh, sequence == nullptr ? std::string{} : asciiFolded(std::get<std::string>(sequence->value)),
                             animFrame == nullptr ? 0.0F : std::get<float>(animFrame->value));

        // SS 4.4: each material slot's first source.
        const std::string skin = objectPath(get("skin", ubundle::ValueKind::Object));
        const std::string texture = objectPath(get("texture", ubundle::ValueKind::Object));
        for (const upkg::MeshMaterial& material : prop.mesh.materials) {
            const std::int32_t slot = material.textureIndex;
            SkinSource source;
            if (slot >= 0 && slot < MULTI_SKINS)
                source.path = objectPath(
                    recordAt("multiskins", ubundle::ValueKind::Object, static_cast<std::uint32_t>(slot), actor, actorClass));
            if (source.path.empty()) source.path = skin;
            if (source.path.empty() && slot >= 0 && std::cmp_less(slot, prop.mesh.textures.size())
                && prop.mesh.textures[static_cast<std::size_t>(slot)].kind() != upkg::ObjectReferenceKind::Null) {
                source.holder = found->holder;
                source.holderName = found->holderName;
                source.reference = prop.mesh.textures[static_cast<std::size_t>(slot)];
            }
            if (source.empty()) source.path = texture;
            prop.skins.push_back(std::move(source));
        }

        const auto* const style = get("style", ubundle::ValueKind::Byte);
        const std::uint8_t styleValue = style == nullptr ? 0 : std::get<std::uint8_t>(style->value);
        if (styleValue == STY_MASKED) prop.addedFlags |= PF_MASKED;
        if (styleValue == STY_TRANSLUCENT) prop.addedFlags |= PF_TRANSLUCENT;
        if (styleValue == STY_MODULATED) prop.addedFlags |= PF_MODULATED;
        const auto* const unlit = get("bunlit", ubundle::ValueKind::Bool);
        if (unlit != nullptr && std::get<bool>(unlit->value)) prop.addedFlags |= PF_UNLIT;

        const auto vector = [&](std::string_view name) {
            const auto* const record = get(name, ubundle::ValueKind::Vector);
            return record == nullptr ? std::array<float, 3>{} : std::get<std::array<float, 3>>(record->value);
        };
        prop.location = vector("location");
        prop.prePivot = vector("prepivot");
        const auto* const rotation = get("rotation", ubundle::ValueKind::Rotator);
        if (rotation != nullptr) prop.rotation = std::get<std::array<std::int32_t, 3>>(rotation->value);
        const auto* const drawScale = get("drawscale", ubundle::ValueKind::Float);
        // Actor.uc's own default is 1; absent means Actor's defaults did not
        // resolve, and a zero would draw nothing.
        prop.drawScale = drawScale == nullptr ? 1.0F : std::get<float>(drawScale->value);
        out.props.push_back(std::move(prop));
    }
    return out;
}

ubundle::Geometry placeProp(const PropSite& prop, const PropMaterialLookup& materials, const umap::RoomMap& rooms,
                            std::size_t zoneCount) {
    const upkg::LodMesh& mesh = prop.mesh;
    const auto frameVerts = static_cast<std::size_t>(mesh.frameVerts);
    const std::size_t first = prop.frame * frameVerts;

    // SS 4.3: every vertex of the frame in the world.
    const Vec scale = {static_cast<double>(mesh.scale.x) * prop.drawScale,
                       static_cast<double>(mesh.scale.y) * prop.drawScale,
                       static_cast<double>(mesh.scale.z) * prop.drawScale};
    const std::array<std::int32_t, 3> rotOrigin = {mesh.rotOrigin.pitch, mesh.rotOrigin.yaw, mesh.rotOrigin.roll};
    std::vector<Vec> world(frameVerts);
    for (std::size_t i = 0; i < frameVerts; ++i) {
        const auto& v = mesh.vertices[first + i];
        const Vec local = {(v[0] - static_cast<double>(mesh.origin.x)) * scale[0],
                           (v[1] - static_cast<double>(mesh.origin.y)) * scale[1],
                           (v[2] - static_cast<double>(mesh.origin.z)) * scale[2]};
        const Vec turned = rotated(rotated(local, rotOrigin), prop.rotation);
        for (std::size_t k = 0; k < 3; ++k)
            world[i][k] = turned[k] + static_cast<double>(prop.location[k]) + static_cast<double>(prop.prePivot[k]);
    }
    // A mirror -- an odd number of negative scale components -- turns each
    // triangle's winding inside out; swapping two corners turns it back.
    const bool mirrored = (scale[0] < 0) != ((scale[1] < 0) != (scale[2] < 0));

    // Each kept face, with its corners in GEOM's winding.
    struct Face {
        std::array<std::uint16_t, 3> wedges{};
        std::string material;
        std::uint32_t polyFlags = 0;
    };
    std::vector<Face> faces;
    std::vector<Vec> normals(frameVerts, Vec{0, 0, 0});
    for (const upkg::MeshFace& meshFace : mesh.faces) {
        const upkg::MeshMaterial& material = mesh.materials[meshFace.material];
        const std::uint32_t polyFlags = material.polyFlags | prop.addedFlags;
        const auto id = materials(prop.skins[meshFace.material], wantsMasked(polyFlags));
        if (!id.has_value()) continue;
        Face face{meshFace.wedges, *id, polyFlags};
        if (mirrored) std::swap(face.wedges[1], face.wedges[2]);
        const auto at = [&](std::size_t k) { return world[mesh.wedges[face.wedges[k]].vertex]; };
        // Area-weighted: the cross product's length is twice the area.
        const Vec n = cross(minus(at(1), at(0)), minus(at(2), at(0)));
        for (const std::uint16_t wedge : face.wedges)
            for (std::size_t k = 0; k < 3; ++k) normals[mesh.wedges[wedge].vertex][k] += n[k];
        faces.push_back(std::move(face));
    }

    // Batched by material and flags, faces in mesh order within each.
    std::stable_sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) {
        return std::tie(a.material, a.polyFlags) < std::tie(b.material, b.polyFlags);
    });
    const std::uint8_t zone = ubundle::zoneAt(rooms, prop.location, zoneCount);
    ubundle::Geometry out;
    // One vertex per wedge, made the first time a face names it: a wedge is a
    // corner's position and texture coordinate, so faces sharing one share
    // the vertex. Measured 2026-11-05: MH-Vale_Evelyn's props are some 333,000
    // stock-mesh triangles, so a vertex per corner would triple PROP.
    std::vector<std::uint32_t> made(mesh.wedges.size(), NOT_MADE);
    for (const Face& face : faces) {
        if (out.batches.empty() || out.batches.back().material != face.material
            || out.batches.back().polyFlags != face.polyFlags)
            out.batches.push_back(ubundle::GeometryBatch{face.material, face.polyFlags,
                                                         static_cast<std::uint32_t>(out.indices.size()), 0, {}});
        for (const std::uint16_t wedgeIndex : face.wedges) {
            if (made[wedgeIndex] != NOT_MADE) {
                out.indices.push_back(made[wedgeIndex]);
                continue;
            }
            const upkg::MeshWedge& wedge = mesh.wedges[wedgeIndex];
            const Vec& n = normals[wedge.vertex];
            const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            ubundle::GeometryVertex vertex;
            for (std::size_t k = 0; k < 3; ++k) {
                vertex.position[k] = static_cast<float>(world[wedge.vertex][k]);
                vertex.normal[k] = length > 0 ? static_cast<float>(n[k] / length) : (k == 2 ? 1.0F : 0.0F);
            }
            vertex.u = static_cast<float>(wedge.u) / 256.0F;
            vertex.v = static_cast<float>(wedge.v) / 256.0F;
            vertex.zone = zone;
            made[wedgeIndex] = static_cast<std::uint32_t>(out.vertices.size());
            out.indices.push_back(made[wedgeIndex]);
            out.vertices.push_back(vertex);
        }
        out.batches.back().indexCount += 3;
    }
    return out;
}

ubundle::Geometry mergeGeometry(const std::vector<const ubundle::Geometry*>& parts) {
    // Each batch's triangles by GEOM's batch key, parts in order.
    using Key = std::tuple<std::string, std::uint32_t, float, float>;
    std::map<Key, std::vector<std::pair<const ubundle::Geometry*, const ubundle::GeometryBatch*>>> byKey;
    for (const ubundle::Geometry* part : parts)
        for (const ubundle::GeometryBatch& batch : part->batches)
            byKey[{batch.material, batch.polyFlags, batch.panRate[0], batch.panRate[1]}].emplace_back(part, &batch);
    // Each part's vertices copied once, the first time an index names one, so
    // what a part shared stays shared.
    std::map<const ubundle::Geometry*, std::vector<std::uint32_t>> moved;
    for (const ubundle::Geometry* part : parts) moved[part].assign(part->vertices.size(), NOT_MADE);
    ubundle::Geometry out;
    for (const auto& [key, runs] : byKey) {
        ubundle::GeometryBatch merged{std::get<0>(key), std::get<1>(key), static_cast<std::uint32_t>(out.indices.size()),
                                      0, {std::get<2>(key), std::get<3>(key)}};
        for (const auto& [part, batch] : runs) {
            std::vector<std::uint32_t>& where = moved[part];
            for (std::uint32_t i = batch->firstIndex; i < batch->firstIndex + batch->indexCount; ++i) {
                const std::uint32_t old = part->indices[i];
                if (where[old] == NOT_MADE) {
                    where[old] = static_cast<std::uint32_t>(out.vertices.size());
                    out.vertices.push_back(part->vertices[old]);
                }
                out.indices.push_back(where[old]);
                ++merged.indexCount;
            }
        }
        out.batches.push_back(std::move(merged));
    }
    return out;
}

} // namespace uta::ubake

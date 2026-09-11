// A level's movers and their shapes -- docs/specs/UTA-0119-mover-shapes.md
// SS 4.3 to SS 4.5.

#include "ubake/Movers.h"

#include "ubake/Actors.h"
#include "ubake/Install.h"
#include "upkg/Properties.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace uta::ubake {
namespace {

using ubundle::PropertyRecord;
using ubundle::ValueKind;
using Vec3 = std::array<double, 3>;

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

/// SS 4.3 item 1: resolved, and Engine.Brush itself or one of its ancestry. A
/// class whose walk stopped short of it, at a missing package or class, has
/// no such path, so it is left out too.
bool descendsFromBrush(const ubundle::ActorClass& actorClass) {
    if (!actorClass.resolved) return false;
    if (actorClass.path == "engine.brush") return true;
    return std::find(actorClass.ancestry.begin(), actorClass.ancestry.end(), "engine.brush")
           != actorClass.ancestry.end();
}

const PropertyRecord* resolved(std::string_view name, ValueKind kind,
                               const ubundle::ActorPlacement& actor,
                               const ubundle::ActorClass& actorClass) {
    return detail::resolvedRecord(name, actor.properties, actorClass.defaults,
                                  [kind](const PropertyRecord& record) { return record.kind == kind; });
}

/// SS 4.4: a Raw value of struct Scale, seventeen bytes -- three f32, SheerRate
/// as f32 and SheerAxis as a byte, little-endian. The shear is read past and
/// not applied (SS 3 decision 3), so only the three components come back.
std::optional<Vec3> scaleOf(const PropertyRecord& record) {
    const auto* raw = std::get_if<ubundle::RawValue>(&record.value);
    if (raw == nullptr || detail::fold(raw->structName) != "scale" || raw->bytes.size() != 17)
        return std::nullopt;
    Vec3 out{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        std::uint32_t bits = 0;
        for (std::size_t b = 0; b < 4; ++b)
            bits |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(raw->bytes[axis * 4 + b]))
                    << (8U * b);
        out[axis] = std::bit_cast<float>(bits);
    }
    return out;
}

/// A Scale resolved as SS 4.4 resolves every number; one on each axis where
/// neither the actor nor its class carries one.
Vec3 scaleNamed(std::string_view name, const ubundle::ActorPlacement& actor,
                const ubundle::ActorClass& actorClass) {
    const PropertyRecord* record = detail::resolvedRecord(
        name, actor.properties, actorClass.defaults,
        [](const PropertyRecord& candidate) { return scaleOf(candidate).has_value(); });
    return record == nullptr ? Vec3{1, 1, 1} : *scaleOf(*record);
}

std::array<float, 3> vectorNamed(std::string_view name, const ubundle::ActorPlacement& actor,
                                 const ubundle::ActorClass& actorClass) {
    const PropertyRecord* record = resolved(name, ValueKind::Vector, actor, actorClass);
    return record == nullptr ? std::array<float, 3>{} : std::get<std::array<float, 3>>(record->value);
}

} // namespace

Result<std::vector<MoverSite>> findMovers(const upkg::Package& map, const ubundle::Placements& actors) {
    std::vector<MoverSite> movers;
    const auto exports = map.exports();
    for (std::size_t i = 0; i < actors.actors.size(); ++i) {
        const ubundle::ActorPlacement& actor = actors.actors[i];
        const ubundle::ActorClass& actorClass = actors.classes[actor.classIndex];

        // SS 4.3 items 1 and 2. bStatic absent everywhere is Actor.uc's false.
        if (!descendsFromBrush(actorClass)) continue;
        const PropertyRecord* isStatic = resolved("bstatic", ValueKind::Bool, actor, actorClass);
        if (isStatic != nullptr && std::get<bool>(isStatic->value)) continue;

        // Item 3: the actor's own Brush, read again for the reference itself --
        // PLAC keeps only its path. buildActors checked the slot, so the
        // export is in the table.
        auto properties = upkg::readProperties(map, exports[actor.exportIndex]);
        if (!properties.has_value())
            return std::unexpected(properties.error().withContext("reading mover " + actor.path));
        std::optional<upkg::ObjectReference> brush;
        for (const upkg::Property& property : *properties) {
            const auto* reference = std::get_if<upkg::ObjectReference>(&property.value);
            if (reference == nullptr || property.arrayIndex != 0) continue;
            UTA_TRY(const std::string_view name, map.name(property.nameIndex));
            if (detail::fold(name) == "brush") {
                brush = *reference;
                break;
            }
        }
        if (!brush.has_value() || brush->kind() == upkg::ObjectReferenceKind::Null) continue;

        if (brush->kind() != upkg::ObjectReferenceKind::Export || brush->index() >= exports.size())
            return std::unexpected(malformed("mover " + actor.path + "'s Brush holds reference "
                                             + std::to_string(brush->raw())
                                             + ", which is not an export of the map"));
        const upkg::ExportEntry& model = exports[brush->index()];
        const auto className = map.objectName(model.objectClass);
        if (model.objectClass.kind() == upkg::ObjectReferenceKind::Null || !className.has_value()
            || detail::fold(*className) != "model")
            return std::unexpected(malformed("mover " + actor.path + "'s Brush names export "
                                             + std::to_string(brush->index()) + ", which is not a Model"));
        movers.push_back(MoverSite{i, &model});
    }
    return movers;
}

std::array<float, 3> PivotSpace::point(const std::array<float, 3>& p) const noexcept {
    std::array<float, 3> out{};
    for (std::size_t axis = 0; axis < 3; ++axis)
        out[axis] = static_cast<float>(mainScale[axis] * (static_cast<double>(p[axis]) - prePivot[axis]));
    return out;
}

Result<PivotSpace> pivotSpaceOf(const MoverSite& mover, const ubundle::Placements& actors) {
    const ubundle::ActorPlacement& actor = actors.actors[mover.placement];
    const ubundle::ActorClass& actorClass = actors.classes[actor.classIndex];
    PivotSpace pivot;
    pivot.mainScale = scaleNamed("mainscale", actor, actorClass);
    if (pivot.mainScale[0] == 0 || pivot.mainScale[1] == 0 || pivot.mainScale[2] == 0)
        return std::unexpected(malformed("mover " + actor.path + " has a MainScale with a zero component"));
    pivot.prePivot = vectorNamed("prepivot", actor, actorClass);
    return pivot;
}

Result<ubundle::MoverShape> buildMover(const MoverSite& mover, const upkg::Model& model,
                                       const ubundle::Placements& actors, const MaterialLookup& lookup) {
    const ubundle::ActorPlacement& actor = actors.actors[mover.placement];
    const ubundle::ActorClass& actorClass = actors.classes[actor.classIndex];
    const std::string where = "mover " + actor.path;

    UTA_TRY(const PivotSpace pivot, pivotSpaceOf(mover, actors));

    // SS 4.5 step 1.
    auto built = buildGeometry(model, lookup);
    if (!built.has_value()) return std::unexpected(built.error().withContext(where));

    ubundle::MoverShape shape;
    shape.exportIndex = actor.exportIndex;
    shape.geometry = std::move(*built);

    // Step 2: MainScale times (p - PrePivot), and the normal divided by
    // MainScale and normalised -- the inverse transpose of a diagonal scale.
    // u and v stay as buildGeometry made them, in the brush's own space.
    for (ubundle::GeometryVertex& vertex : shape.geometry.vertices) {
        vertex.position = pivot.point(vertex.position);
        Vec3 normal{};
        for (std::size_t axis = 0; axis < 3; ++axis)
            normal[axis] = static_cast<double>(vertex.normal[axis]) / pivot.mainScale[axis];
        const double length =
            std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length > 0)
            for (std::size_t axis = 0; axis < 3; ++axis)
                vertex.normal[axis] = static_cast<float>(normal[axis] / length);
    }

    // Step 3: a mirror turns every triangle over; swapping its last two
    // corners keeps the face its normal points out of in front.
    if (pivot.mainScale[0] * pivot.mainScale[1] * pivot.mainScale[2] < 0)
        for (std::size_t i = 0; i + 2 < shape.geometry.indices.size(); i += 3)
            std::swap(shape.geometry.indices[i + 1], shape.geometry.indices[i + 2]);

    // Step 4.
    shape.location = vectorNamed("location", actor, actorClass);
    if (const PropertyRecord* rotation = resolved("rotation", ValueKind::Rotator, actor, actorClass))
        shape.rotation = std::get<std::array<std::int32_t, 3>>(rotation->value);
    const Vec3 postScale = scaleNamed("postscale", actor, actorClass);
    shape.postScale = {static_cast<float>(postScale[0]), static_cast<float>(postScale[1]),
                       static_cast<float>(postScale[2])};
    return shape;
}

} // namespace uta::ubake

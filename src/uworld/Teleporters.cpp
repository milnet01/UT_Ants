// Teleporters in one level -- Teleporters.h.

#include "uworld/Teleporters.h"

#include "core/Ascii.h"

#include <cmath>
#include <numbers>
#include <string_view>
#include <variant>

namespace uta::uworld {
namespace {

using ubundle::PropertyRecord;

/// The actor's own value of `name` (folded), else its class's default.
const PropertyRecord* resolved(std::string_view name, ubundle::ValueKind kind, const ubundle::ActorPlacement& actor,
                               const ubundle::ActorClass& actorClass) {
    for (const auto* list : {&actor.properties, &actorClass.defaults})
        for (const PropertyRecord& record : *list)
            if (record.arrayIndex == 0 && record.kind == kind && asciiFolded(record.name) == name) return &record;
    return nullptr;
}

/// A heading as a unit vector in the horizontal plane: UT's vector(Rotator)
/// with pitch 0.
Vec3 headingOf(double yaw) noexcept {
    const double angle = yaw * (2 * std::numbers::pi / 65536);
    return {std::cos(angle), std::sin(angle), 0};
}

bool finite(const Vec3& v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

/// UT's Teleporter.Touch tells a URL naming another level by a / or a #.
bool namesAnotherLevel(std::string_view url) noexcept {
    return url.find('/') != std::string_view::npos || url.find('#') != std::string_view::npos;
}

} // namespace

Teleporters Teleporters::fromPlacements(const ubundle::Placements& placements) {
    using ubundle::ValueKind;
    std::vector<Teleporter> list;
    for (const ubundle::ActorPlacement& actor : placements.actors) {
        if (actor.classIndex >= placements.classes.size()) continue;
        const ubundle::ActorClass& actorClass = placements.classes[actor.classIndex];
        if (!ubundle::isOrDescendsFrom(actorClass, "engine.teleporter")) continue;

        const auto vector = [&](std::string_view name) {
            const PropertyRecord* record = resolved(name, ValueKind::Vector, actor, actorClass);
            if (record == nullptr) return Vec3{};
            const auto& v = std::get<std::array<float, 3>>(record->value);
            return Vec3{v[0], v[1], v[2]};
        };
        const auto number = [&](std::string_view name) {
            const PropertyRecord* record = resolved(name, ValueKind::Float, actor, actorClass);
            return record == nullptr ? 0.0 : double(std::get<float>(record->value));
        };
        const auto text = [&](std::string_view name, ValueKind kind) {
            const PropertyRecord* record = resolved(name, kind, actor, actorClass);
            return record == nullptr ? std::string() : asciiFolded(std::get<std::string>(record->value));
        };
        const auto flag = [&](std::string_view name) {
            const PropertyRecord* record = resolved(name, ValueKind::Bool, actor, actorClass);
            return record != nullptr && std::get<bool>(record->value);
        };

        Teleporter teleporter;
        teleporter.location = vector("location");
        if (const PropertyRecord* rotation = resolved("rotation", ValueKind::Rotator, actor, actorClass))
            teleporter.yaw = std::get<std::array<std::int32_t, 3>>(rotation->value)[1];
        teleporter.tag = text("tag", ValueKind::Name);
        teleporter.url = text("url", ValueKind::String);
        teleporter.enabled = flag("benabled");
        // PostBeginPlay: an empty URL is a destination only, with no collision.
        const bool stock = actorClass.path == "engine.teleporter" || actorClass.path == "botpack.visibleteleporter"
                           || actorClass.path == "unrealshare.favoritesteleporter";
        teleporter.sends = stock && flag("bcollideactors") && !teleporter.url.empty();
        teleporter.radius = number("collisionradius");
        teleporter.height = number("collisionheight");
        teleporter.changesYaw = flag("bchangesyaw");
        teleporter.changesVelocity = flag("bchangesvelocity");
        teleporter.reversesX = flag("breversesx");
        teleporter.reversesY = flag("breversesy");
        teleporter.reversesZ = flag("breversesz");
        teleporter.targetVelocity = vector("targetvelocity");
        // A crafted map's NaN would teleport a body to nowhere.
        if (!finite(teleporter.location) || !finite(teleporter.targetVelocity) || !std::isfinite(teleporter.radius)
            || !std::isfinite(teleporter.height))
            continue;
        list.push_back(std::move(teleporter));
    }
    return Teleporters(std::move(list));
}

Teleporters::Teleporters(std::vector<Teleporter> list) : list_(std::move(list)), touching_(list_.size(), false) {}

namespace {

/// UT's cylinder touch: the two cylinders overlap on both axes.
bool overlaps(const Teleporter& teleporter, const BodySize& size, const Vec3& at) noexcept {
    if (!teleporter.sends) return false;
    const double dx = at.x - teleporter.location.x, dy = at.y - teleporter.location.y;
    const double reach = teleporter.radius + size.radius;
    return std::abs(at.z - teleporter.location.z) < teleporter.height + size.halfHeight
           && dx * dx + dy * dy < reach * reach;
}

} // namespace

void Teleporters::markTouching(const BodySize& size, const Vec3& at) {
    for (std::size_t i = 0; i < list_.size(); ++i) touching_[i] = overlaps(list_[i], size, at);
}

std::optional<Arrival> Teleporters::touch(const BodyWorld& world, const BodySize& size, const Vec3& location,
                                          const Vec3& velocity, double yaw) {
    std::optional<Arrival> arrival;
    for (std::size_t i = 0; i < list_.size() && !arrival; ++i) {
        const Teleporter& source = list_[i];
        if (touching_[i] || !overlaps(source, size, location)) continue;
        if (!source.enabled || namesAnotherLevel(source.url)) continue;

        std::vector<std::size_t> destinations;
        for (std::size_t j = 0; j < list_.size(); ++j)
            if (j != i && list_[j].tag == source.url) destinations.push_back(j);
        if (destinations.empty()) continue;
        const Teleporter& dest =
            list_[destinations[std::uniform_int_distribution<std::size_t>(0, destinations.size() - 1)(pick_)]];
        if (!world.fits(size, dest.location)) continue;

        // Teleporter.Accept, for a pawn.
        Arrival landed{dest.location, velocity, yaw};
        if (dest.changesYaw) {
            landed.yaw = std::fmod(double(dest.yaw) + 32768 + yaw - double(source.yaw), 65536.0);
            if (landed.yaw < 0) landed.yaw += 65536;
        }
        if (dest.changesVelocity) {
            landed.velocity = dest.targetVelocity;
        } else {
            if (dest.changesYaw) {
                const Vec3 oldDir = headingOf(yaw);
                const double along = dot(velocity, oldDir);
                landed.velocity = velocity - oldDir * along + headingOf(landed.yaw) * along;
            }
            if (dest.reversesX) landed.velocity.x = -landed.velocity.x;
            if (dest.reversesY) landed.velocity.y = -landed.velocity.y;
            if (dest.reversesZ) landed.velocity.z = -landed.velocity.z;
        }
        arrival = landed;
    }
    markTouching(size, arrival ? arrival->location : location);
    return arrival;
}

} // namespace uta::uworld

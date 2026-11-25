// The placed actors that block a body -- Blockers.h.

#include "uworld/Blockers.h"

#include <array>
#include <cmath>
#include <string_view>
#include <variant>

namespace uta::uworld {
namespace {

using ubundle::PropertyRecord;
using ubundle::ValueKind;

/// SS 4.7: these move, so a still cylinder would block where they are not.
/// A mover is SS 4.3's.
constexpr std::array MOVING{std::string_view("engine.pawn"), std::string_view("engine.mover"),
                            std::string_view("engine.projectile")};

bool flag(std::string_view name, const ubundle::ActorPlacement& actor, const ubundle::ActorClass& actorClass) {
    const PropertyRecord* record = ubundle::resolvedProperty(name, ValueKind::Bool, actor, actorClass);
    return record != nullptr && std::get<bool>(record->value);
}

double number(std::string_view name, const ubundle::ActorPlacement& actor, const ubundle::ActorClass& actorClass) {
    const PropertyRecord* record = ubundle::resolvedProperty(name, ValueKind::Float, actor, actorClass);
    return record == nullptr ? 0.0 : double(std::get<float>(record->value));
}

} // namespace

std::vector<Blocker> blockersOf(const ubundle::Placements& placements) {
    std::vector<Blocker> out;
    for (const ubundle::ActorPlacement& actor : placements.actors) {
        if (actor.classIndex >= placements.classes.size()) continue;
        const ubundle::ActorClass& actorClass = placements.classes[actor.classIndex];
        // An unread class's defaults are unknown, and it may be a pawn.
        if (!actorClass.resolved) continue;
        bool moves = false;
        for (const std::string_view path : MOVING) moves = moves || ubundle::isOrDescendsFrom(actorClass, path);
        if (moves) continue;
        if (!flag("bcollideactors", actor, actorClass) || !flag("bblockplayers", actor, actorClass)) continue;

        Blocker blocker;
        if (const PropertyRecord* at = ubundle::resolvedProperty("location", ValueKind::Vector, actor, actorClass)) {
            const auto& v = std::get<std::array<float, 3>>(at->value);
            blocker.location = {v[0], v[1], v[2]};
        }
        blocker.radius = number("collisionradius", actor, actorClass);
        blocker.halfHeight = number("collisionheight", actor, actorClass);
        const Vec3& p = blocker.location;
        // `!(x > 0)` also refuses a NaN, which a crafted map could carry.
        if (!(blocker.radius > 0) || !(blocker.halfHeight > 0) || !std::isfinite(blocker.radius)
            || !std::isfinite(blocker.halfHeight) || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            continue;
        out.push_back(blocker);
    }
    return out;
}

} // namespace uta::uworld

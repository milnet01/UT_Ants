// Teleporters in one level -- Teleporters.h.

#include "uworld/Teleporters.h"

#include "core/Ascii.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

/// What a Teleporter class's Touch does, as far as this reads it.
enum class Touches : std::uint8_t { Stock, JumpPad, Unknown };

/// UTA-0463's census of the install's subclasses (2026-11-11), by the
/// functions each defines. These replace only PlayTeleportEffect, or nothing,
/// so they send as Teleporter does. A map's own class is named by the map, so
/// those are matched by class name with Teleporter as the parent.
constexpr std::array STOCK_PATHS{std::string_view("engine.teleporter"), std::string_view("botpack.visibleteleporter"),
                                 std::string_view("unrealshare.favoritesteleporter"),
                                 std::string_view("teleporter.myteleporter"),
                                 std::string_view("sevenb.sbstealthteleporter")};
constexpr std::array EFFECTS_ONLY_NAMES{std::string_view("teleporterquestionmark"), std::string_view("teleporter_q"),
                                        std::string_view("silentteleporter"), std::string_view("extremeteleporter"),
                                        std::string_view("extendedapteleporter"), std::string_view("teleporterfix")};
/// swJumpPad, and BCEJumpPad: the same source with bBotsOnly added.
constexpr std::array JUMP_PAD_NAMES{std::string_view("swjumppad"), std::string_view("bcejumppad")};

Touches touchesOf(const ubundle::ActorClass& actorClass) {
    const auto among = [](const auto& names, std::string_view name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    };
    if (among(STOCK_PATHS, actorClass.path)) return Touches::Stock;
    if (actorClass.ancestry.empty() || actorClass.ancestry.front() != "engine.teleporter") return Touches::Unknown;
    const std::string_view path = actorClass.path;
    const std::string_view name = path.substr(path.rfind('.') + 1); // npos + 1 is 0: the whole path
    if (among(EFFECTS_ONLY_NAMES, name)) return Touches::Stock;
    if (among(JUMP_PAD_NAMES, name)) return Touches::JumpPad;
    return Touches::Unknown;
}

/// An actor's fields, its own value else its class's default; an absent
/// field is UnrealScript's zero.
struct Fields {
    const ubundle::ActorPlacement& actor;
    const ubundle::ActorClass& actorClass;

    [[nodiscard]] Vec3 vector(std::string_view name) const {
        const PropertyRecord* record = resolved(name, ubundle::ValueKind::Vector, actor, actorClass);
        if (record == nullptr) return {};
        const auto& v = std::get<std::array<float, 3>>(record->value);
        return {v[0], v[1], v[2]};
    }
    [[nodiscard]] double number(std::string_view name) const {
        const PropertyRecord* record = resolved(name, ubundle::ValueKind::Float, actor, actorClass);
        return record == nullptr ? 0.0 : double(std::get<float>(record->value));
    }
    [[nodiscard]] std::uint8_t byte(std::string_view name) const {
        const PropertyRecord* record = resolved(name, ubundle::ValueKind::Byte, actor, actorClass);
        return record == nullptr ? 0 : std::get<std::uint8_t>(record->value);
    }
    [[nodiscard]] std::string text(std::string_view name, ubundle::ValueKind kind) const {
        const PropertyRecord* record = resolved(name, kind, actor, actorClass);
        return record == nullptr ? std::string() : asciiFolded(std::get<std::string>(record->value));
    }
    [[nodiscard]] bool flag(std::string_view name) const {
        const PropertyRecord* record = resolved(name, ubundle::ValueKind::Bool, actor, actorClass);
        return record != nullptr && std::get<bool>(record->value);
    }
};

/// The class's own default of a float: UnrealScript's `default.` reads it,
/// whatever the actor sets.
double classDefault(std::string_view name, const ubundle::ActorClass& actorClass) {
    for (const PropertyRecord& record : actorClass.defaults)
        if (record.arrayIndex == 0 && record.kind == ubundle::ValueKind::Float && asciiFolded(record.name) == name)
            return std::get<float>(record.value);
    return 0;
}

JumpPad jumpPadOf(const Fields& fields) {
    JumpPad pad;
    pad.angle = fields.number("jumpangle");
    pad.angleRand = fields.number("anglerand");
    pad.angleMode = fields.byte("anglerandmode");
    pad.minAngle = fields.number("minangle");
    pad.maxAngle = fields.number("maxangle");
    pad.minAngleCurve = fields.number("minanglecurve");
    pad.minAngleFinder = fields.number("minanglefinder");
    pad.targetZOffset = fields.number("targetzoffset");
    pad.targetRand = fields.vector("targetrand");
    pad.traceGround = fields.flag("btraceground");
    pad.teamOnly = fields.flag("bteamonly");
    pad.botsOnly = fields.flag("bbotsonly");
    pad.teamNumber = fields.byte("teamnumber");
    pad.wait = classDefault("jumpwait", fields.actorClass);
    return pad;
}

/// UT's Teleporter.Touch tells a URL naming another level by a / or a #.
bool namesAnotherLevel(std::string_view url) noexcept {
    return url.find('/') != std::string_view::npos || url.find('#') != std::string_view::npos;
}

} // namespace

Teleporters Teleporters::fromPlacements(const ubundle::Placements& placements) {
    using ubundle::ValueKind;
    std::vector<Teleporter> list;
    std::vector<std::size_t> padActors; ///< for each jump pad in `list`, in order, its actor's index
    for (std::size_t a = 0; a < placements.actors.size(); ++a) {
        const ubundle::ActorPlacement& actor = placements.actors[a];
        if (actor.classIndex >= placements.classes.size()) continue;
        const ubundle::ActorClass& actorClass = placements.classes[actor.classIndex];
        if (!ubundle::isOrDescendsFrom(actorClass, "engine.teleporter")) continue;
        const Fields fields{actor, actorClass};
        const Touches touches = touchesOf(actorClass);

        Teleporter teleporter;
        teleporter.location = fields.vector("location");
        if (const PropertyRecord* rotation = resolved("rotation", ValueKind::Rotator, actor, actorClass))
            teleporter.yaw = std::get<std::array<std::int32_t, 3>>(rotation->value)[1];
        teleporter.tag = fields.text("tag", ValueKind::Name);
        teleporter.url = fields.text("url", ValueKind::String);
        teleporter.enabled = fields.flag("benabled");
        // PostBeginPlay: an empty URL is a destination only, with no collision.
        // A jump pad skips that, but with no URL its PostTouch finds nothing.
        teleporter.sends = touches != Touches::Unknown && fields.flag("bcollideactors") && !teleporter.url.empty();
        teleporter.radius = fields.number("collisionradius");
        teleporter.height = fields.number("collisionheight");
        teleporter.changesYaw = fields.flag("bchangesyaw");
        teleporter.changesVelocity = fields.flag("bchangesvelocity");
        teleporter.reversesX = fields.flag("breversesx");
        teleporter.reversesY = fields.flag("breversesy");
        teleporter.reversesZ = fields.flag("breversesz");
        teleporter.targetVelocity = fields.vector("targetvelocity");
        if (touches == Touches::JumpPad) teleporter.jumpPad = jumpPadOf(fields);
        // A crafted map's NaN would teleport a body to nowhere.
        if (!finite(teleporter.location) || !finite(teleporter.targetVelocity) || !std::isfinite(teleporter.radius)
            || !std::isfinite(teleporter.height))
            continue;
        if (teleporter.jumpPad) padActors.push_back(a);
        list.push_back(std::move(teleporter));
    }

    // Each jump pad's target: the last actor, of any class, whose Tag its URL
    // names, as PostTouch's AllActors loop leaves JumpTarget.
    std::size_t next = 0;
    for (Teleporter& teleporter : list) {
        if (!teleporter.jumpPad) continue;
        const std::size_t self = padActors[next++];
        if (teleporter.url.empty()) continue;
        for (std::size_t a = 0; a < placements.actors.size(); ++a) {
            const ubundle::ActorPlacement& actor = placements.actors[a];
            if (a == self || actor.classIndex >= placements.classes.size()) continue;
            const Fields fields{actor, placements.classes[actor.classIndex]};
            if (fields.text("tag", ValueKind::Name) != teleporter.url) continue;
            const Vec3 at = fields.vector("location");
            const double height = fields.number("collisionheight");
            if (!finite(at) || !std::isfinite(height)) continue;
            teleporter.jumpPad->target = at;
            teleporter.jumpPad->targetHeight = height;
        }
    }
    return Teleporters(std::move(list));
}

Teleporters::Teleporters(std::vector<Teleporter> list)
    : list_(std::move(list)), touching_(list_.size(), false), extremeSwitch_(list_.size(), false),
      launchedAt_(list_.size(), -std::numeric_limits<double>::infinity()) {}

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
                                          const Vec3& velocity, double yaw, const MoveRules& rules, double dt) {
    now_ += dt;
    std::optional<Arrival> arrival;
    for (std::size_t i = 0; i < list_.size() && !arrival; ++i) {
        const Teleporter& source = list_[i];
        if (touching_[i] || !overlaps(source, size, location)) continue;
        if (!source.enabled) continue;
        if (source.jumpPad) {
            arrival = launch(i, world, size, location, rules);
            if (arrival) arrival->yaw = yaw;
            continue;
        }
        if (namesAnotherLevel(source.url)) continue;

        std::vector<std::size_t> destinations;
        for (std::size_t j = 0; j < list_.size(); ++j)
            if (j != i && list_[j].tag == source.url) destinations.push_back(j);
        if (destinations.empty()) continue;
        const Teleporter& dest =
            list_[destinations[std::uniform_int_distribution<std::size_t>(0, destinations.size() - 1)(pick_)]];
        // A jump pad's Accept refuses every arrival.
        if (dest.jumpPad || !world.fits(size, dest.location)) continue;

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

namespace {

// swJumpPad's own constants, as its source spells them.
constexpr double DEGREE_TO_RADIAN = 0.01745329252;
constexpr double DEGREE_TO_UROT = 182.04444444;
constexpr double UROT_TO_DEGREE = 0.00549316;
/// The player's team: PlayerReplicationInfo's "255 = None for player".
constexpr std::uint8_t PLAYER_TEAM = 255;

/// UnrealScript's FClamp.
double clampTo(double v, double low, double high) noexcept { return v < low ? low : (v > high ? high : v); }

/// A rotator component from a radian angle: an int, so truncated.
std::int32_t urot(double radians) noexcept {
    return static_cast<std::int32_t>(radians * (32768 / std::numbers::pi));
}

double radians(std::int32_t rot) noexcept { return rot * (std::numbers::pi / 32768); }

/// UnrealScript's VRand: a random unit vector.
Vec3 randomUnit(std::minstd_rand& pick) {
    std::normal_distribution<double> normal;
    for (;;) {
        const Vec3 v{normal(pick), normal(pick), normal(pick)};
        const double len = length(v);
        if (len > 1e-9) return v * (1 / len);
    }
}

/// TraceGround: the floor straight below `from`, found by a body too small to
/// matter, as UT's zero-extent Trace finds it. None where there is no floor
/// within 32768.
std::optional<Vec3> groundBelow(const BodyWorld& world, const Vec3& from) {
    constexpr BodySize POINT{0.5, 0.5};
    const Vec3 to{from.x, from.y, from.z - 32768};
    const BodyHit hit = world.sweep(POINT, from, to);
    if (hit.fraction >= 1) return std::nullopt;
    const Vec3 centre = from + (to - from) * hit.fraction;
    return Vec3{centre.x, centre.y, centre.z - POINT.halfHeight};
}

} // namespace

std::optional<Arrival> Teleporters::launch(std::size_t pad, const BodyWorld& world, const BodySize& size,
                                           const Vec3& location, const MoveRules& rules) {
    const JumpPad& jump = *list_[pad].jumpPad;
    // PostTouch, in its order: a target, then BCEJumpPad's bots and the team.
    if (!jump.target || jump.botsOnly) return std::nullopt;
    if (jump.teamOnly && PLAYER_TEAM != jump.teamNumber) return std::nullopt;
    // Not again for the same body within the class's JumpWait.
    if (!(now_ - launchedAt_[pad] > jump.wait)) return std::nullopt;
    launchedAt_[pad] = now_;

    // CalcVelocity, from the feet to the target's.
    const Vec3 origin{location.x, location.y, location.z - size.halfHeight};
    const Vec3 below{jump.target->x, jump.target->y, jump.target->z - jump.targetHeight};
    Vec3 targetAt = below;
    if (jump.traceGround) targetAt = groundBelow(world, *jump.target).value_or(below);
    if (jump.targetRand.x != 0 || jump.targetRand.y != 0 || jump.targetRand.z != 0) {
        const Vec3 r = randomUnit(pick_);
        targetAt = targetAt + Vec3{r.x * jump.targetRand.x, r.y * jump.targetRand.y, r.z * jump.targetRand.z};
    }
    targetAt.z += jump.targetZOffset;

    const Vec3 delta = targetAt - origin;
    const double distance = std::hypot(delta.x, delta.y);
    const std::int32_t pitchToTarget = urot(std::atan2(delta.z, distance));
    const std::int32_t yawToTarget = urot(std::atan2(delta.y, delta.x));
    const double gravity = -rules.gravityZ;

    // GetAngle.
    double wanted = 0;
    switch (jump.angleMode) {
    case 0: // AM_Random
        wanted = jump.angleRand == 0
                     ? jump.angle
                     : std::uniform_real_distribution<double>(jump.angle, jump.angle + jump.angleRand)(pick_);
        break;
    case 1: // AM_Extremes: JumpAngle + AngleRand first, then each in turn
        extremeSwitch_[pad] = !extremeSwitch_[pad];
        wanted = jump.angle + (extremeSwitch_[pad] ? jump.angleRand : 0);
        break;
    case 2: // AM_Owned: the other teams take the steeper angle
        wanted = jump.angle + (PLAYER_TEAM != jump.teamNumber ? jump.angleRand : 0);
        break;
    default: break; // GetAngle falls off its switch: 0
    }
    double angle = clampTo(wanted, jump.minAngle, jump.maxAngle);
    // Steeper than the line to the target, or the arc cannot reach it.
    const double least =
        clampTo(double(static_cast<std::int32_t>(pitchToTarget * UROT_TO_DEGREE)) + 1, jump.minAngle, jump.maxAngle);
    if (least > angle) {
        const double alpha = 1 - std::pow(1 - least / jump.maxAngle, jump.minAngleCurve);
        angle = clampTo(least + (jump.maxAngle - least) * jump.minAngleFinder * alpha, jump.minAngle, jump.maxAngle);
    }

    const double tangent = std::tan(angle * DEGREE_TO_RADIAN);
    double speed = distance * std::sqrt(gravity * (tangent * tangent + 1) / (2 * (distance * tangent - delta.z)));
    if (speed == 0) speed = rules.jumpZ;
    // UT would launch a NaN velocity where the arc has no solution; this
    // launches nothing.
    if (!std::isfinite(speed)) return std::nullopt;
    const double pitch = radians(static_cast<std::int32_t>(angle * DEGREE_TO_UROT));
    const double yaw = radians(yawToTarget);
    Arrival launched;
    launched.location = location;
    launched.velocity = Vec3{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)} * speed;
    launched.launched = true;
    return launched;
}

} // namespace uta::uworld

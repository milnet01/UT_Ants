// What ut-ants draws of a game -- StandIns.h.

#include "StandIns.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>

namespace uta::client {
namespace {

using urender::Shape;

/// The smallest a projectile is drawn: a flak chunk's own radius is too
/// small to see.
constexpr float SMALLEST_SHOT = 3;

/// A projectile kind's colour, by its place in the set.
constexpr std::array<std::array<float, 3>, 6> SHOT_COLOURS{{
    {1.0f, 0.55f, 0.1f}, {0.3f, 1.0f, 0.3f}, {0.4f, 0.6f, 1.0f}, {1.0f, 0.9f, 0.2f}, {1.0f, 0.3f, 0.9f},
    {0.3f, 1.0f, 1.0f}}};

std::array<float, 3> place(const ugame::Vec3& at) {
    return {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)};
}

bool isBlast(ugame::Event::Kind kind) {
    return kind == ugame::Event::Kind::Explosion || kind == ugame::Event::Kind::Combo;
}

double lifeOf(ugame::Event::Kind kind) {
    return isBlast(kind) ? StandIns::BLAST_SECONDS : StandIns::LINE_SECONDS;
}

} // namespace

void StandIns::advance(double seconds, std::span<const ugame::Event> events) {
    for (Effect& effect : effects_) effect.age += seconds;
    std::erase_if(effects_, [](const Effect& effect) { return effect.age >= lifeOf(effect.event.kind); });
    for (const ugame::Event& event : events) effects_.push_back({event, 0});
}

std::vector<Shape> StandIns::shapes(const ugame::Game& game, const ugame::WeaponSet& set) const {
    std::vector<Shape> out;
    for (std::size_t i = 1; i < game.bodies.size(); ++i) {
        const ugame::Body& body = game.bodies[i];
        if (!body.alive()) continue;
        // Green at full health, through yellow, to red as it nears none.
        const auto share = static_cast<float>(std::clamp(body.health / ugame::START_HEALTH, 0.0, 1.0));
        out.push_back({.kind = Shape::Kind::Cylinder,
                       .a = place(body.move.location),
                       .radius = static_cast<float>(body.size.radius),
                       .halfHeight = static_cast<float>(body.size.halfHeight),
                       .colour = {1 - share, share, 0.1f, 1}});
    }
    for (const ugame::Shot& shot : game.shots) {
        const float radius =
            shot.kind < set.projectiles.size() ? static_cast<float>(set.projectiles[shot.kind].radius) : 0;
        const std::array<float, 3>& colour = SHOT_COLOURS[shot.kind % SHOT_COLOURS.size()];
        out.push_back({.kind = Shape::Kind::Sphere,
                       .a = place(shot.location),
                       .radius = std::max(radius, SMALLEST_SHOT),
                       .colour = {colour[0], colour[1], colour[2], 1}});
    }
    for (const Effect& effect : effects_) {
        const ugame::Event& event = effect.event;
        const auto spent = static_cast<float>(effect.age / lifeOf(event.kind)); // 0 new, 1 gone
        if (isBlast(event.kind)) {
            const bool combo = event.kind == ugame::Event::Kind::Combo;
            out.push_back({.kind = Shape::Kind::Sphere,
                           .a = place(event.from),
                           .radius = static_cast<float>(event.radius) * (0.3f + 0.7f * spent),
                           .colour = {combo ? 0.5f : 1.0f, combo ? 0.4f : 0.5f, combo ? 1.0f : 0.1f,
                                      0.7f * (1 - spent)}});
            continue;
        }
        const bool beam = event.kind == ugame::Event::Kind::Beam;
        out.push_back({.kind = Shape::Kind::Line,
                       .a = place(event.from),
                       .b = place(event.to),
                       .radius = beam ? 2.0f : 1.0f,
                       .colour = {beam ? 0.4f : 1.0f, beam ? 1.0f : 0.9f, 0.5f, 1 - spent}});
    }
    return out;
}

std::string hudText(const ugame::Body& body, const ugame::WeaponSet& set,
                    const std::optional<Walker::Hit>& lastHit) {
    std::string text;
    if (body.weapon >= 0 && static_cast<std::size_t>(body.weapon) < set.weapons.size()) {
        const auto held = static_cast<std::size_t>(body.weapon);
        text = set.weapons[held].name + "\n";
        // A weapon that holds no ammo, such as the hammer, spends none.
        if (set.weapons[held].ammoMax > 0) text += std::format("AMMO {}\n", body.weapons[held].ammo);
    }
    text += std::format("HEALTH {}", static_cast<long long>(std::max(body.health, 0.0)));
    if (lastHit) {
        text += std::format("\nHIT {}", static_cast<long long>(lastHit->damage));
        text += lastHit->healthLeft > 0 ? std::format(" TARGET {}", static_cast<long long>(lastHit->healthLeft))
                                        : std::string(" TARGET DOWN");
    }
    return text;
}

} // namespace uta::client

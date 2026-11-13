// What ut-ants draws of a game -- docs/specs/UTA-0019-weapon-set.md SS 4.8.
// Projectiles are spheres, an instant hit or a beam a short-lived line, an
// explosion a sphere that grows and fades, a dummy a cylinder coloured by its
// health; and the overlay's lines name the weapon, its ammo and the player's
// health. Modern explosions replace the sphere in UTA-0470; weapon models are
// UTA-0467.
//
// No SDL and no device here, so the unit tests can drive it.

#pragma once

#include "Walker.h"

#include "ugame/Game.h"
#include "urender/Shapes.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace uta::client {

class StandIns {
public:
    /// Seconds a shot's line and an explosion's sphere stay on screen.
    static constexpr double LINE_SECONDS = 0.12;
    static constexpr double BLAST_SECONDS = 0.35;

    /// Ages what is showing by `seconds`, drops what has run out, and adds
    /// `events`, each drawn at least once.
    void advance(double seconds, std::span<const ugame::Event> events);

    /// The frame's shapes: every body of `game` but the first, which is the
    /// player's own; every shot, by its kind in `set`; and what `advance` holds.
    [[nodiscard]] std::vector<urender::Shape> shapes(const ugame::Game& game, const ugame::WeaponSet& set) const;

private:
    struct Effect {
        ugame::Event event;
        double age = 0;
    };
    std::vector<Effect> effects_;
};

/// The overlay's lines for `body`: its weapon's name, that weapon's ammo
/// where it holds any, its health, and what `lastHit` did to a dummy.
[[nodiscard]] std::string hudText(const ugame::Body& body, const ugame::WeaponSet& set,
                                  const std::optional<Walker::Hit>& lastHit);

} // namespace uta::client

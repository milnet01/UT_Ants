// Walking in ut-ants -- docs/specs/UTA-0017-movement-rules.md SS 4.8. A body
// moving by UT99's rules on the fixed tick, driven by the same uinput frame the
// fly camera reads: Forward, Back, Left and Right move, a double tap of one or
// the Dodge action dodges, Up jumps, Down ducks and Walk walks.
//
// docs/specs/UTA-0019-weapon-set.md SS 4.8: the body carries a weapon set with
// full ammo and fires it, a key stands a target dummy ahead, and a weapon with
// no alt fire zooms the view on AltFire, as UT99's sniper rifle does.
//
// docs/specs/UTA-0020-weapon-wheel.md SS 4.8: Wheel held opens the weapon
// wheel, which the frame points; letting go picks. While it is open, a lone
// player's game runs slowed, and the clock follows it.
//
// docs/specs/UTA-0021-landing-marker.md SS 4.6: in the air, where the body
// will land, and the drawn view tipped down to show it; the aim stays put.
//
// No SDL here, so the unit tests can drive it.

#pragma once

#include "FlyCamera.h"

#include "ugame/Game.h"
#include "uworld/FixedTick.h"
#include "uworld/Movement.h"
#include "uui/Wheel.h"
#include "uworld/Teleporters.h"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace uta::client {

class Walker {
public:
    /// UT99's BaseEyeHeight: the eye above the body's centre (the PAWN line).
    static constexpr double EYE_HEIGHT = 27;

    /// How far ahead a dummy is stood: a few metres.
    static constexpr double DUMMY_AHEAD = 200;
    /// Dummies standing at once; a drop past it is refused.
    static constexpr std::size_t MAX_DUMMIES = 32;
    /// Seconds of AltFire a zoom takes to reach its narrowest, and how much of
    /// the view is left there: PlayerPawn's ZoomLevel, to 0.9, each whole one
    /// taking 88 of the default 90 degrees.
    static constexpr double ZOOM_SECONDS = 0.9;
    static constexpr double ZOOM_NARROWS = 88.0 / 90.0;

    /// A place the player comes back at: a body's centre and its facing.
    struct Spawn {
        std::array<double, 3> location{};
        double yaw = 0;
    };

    /// What the last shot to hurt a dummy did.
    struct Hit {
        double damage = 0;
        double healthLeft = 0; ///< 0 or less: it is gone
    };

    /// A body with its centre at `location`, falling until it meets a floor,
    /// in a level with `teleporters` (UTA-0205), carrying every weapon of
    /// `set` with its most ammo. Killed, it comes back at each of `spawns` in
    /// turn; with none, at `location`.
    Walker(std::array<double, 3> location, double pitch, double yaw, uworld::Teleporters teleporters = {},
           ugame::WeaponSet set = {}, std::vector<Spawn> spawns = {});

    /// One frame: the look now, then every tick `seconds` makes due, each
    /// with this frame's actions, each followed by any teleport it ends in.
    /// A Dodge press dodges the way the move
    /// points most, on the next tick; with no move, or while a dodge is not
    /// ready, it is dropped, as a double tap's press would be. Fire, AltFire
    /// and a weapon key reach the next tick even where this frame runs none.
    /// While the weapon wheel is open the frame points it instead of turning
    /// the view, and sends no fire; a weapon picked on it is asked for on
    /// the next tick. The clock runs at the game's time scale.
    void update(const uinput::Frame& input, double seconds, const uworld::BodyWorld& world);

    /// The view from the body's eye with a `fov`-degree field of view, drawn
    /// as far from the last tick's place to this one's as the clock's blend.
    [[nodiscard]] FlyCamera eye(double fov) const noexcept;

    /// `eye(fov)` at `width` by `height`, narrowed by the zoom.
    [[nodiscard]] urender::Camera view(double fov, std::uint32_t width, std::uint32_t height) const noexcept;

    [[nodiscard]] const uworld::MoveState& state() const noexcept { return game_.bodies[0].move; }
    /// The player's body: its health, its weapon in hand and its ammo.
    [[nodiscard]] const ugame::Body& body() const noexcept { return game_.bodies[0]; }
    /// Every body, the player first and then the dummies, and the live shots.
    [[nodiscard]] const ugame::Game& game() const noexcept { return game_; }
    [[nodiscard]] const ugame::WeaponSet& set() const noexcept { return set_; }
    /// What the ticks of the last `update` did, for drawing.
    [[nodiscard]] const std::vector<ugame::Event>& events() const noexcept { return events_; }
    /// The last update's cues, over all its ticks: what WeaponSounds plays (UTA-0467).
    [[nodiscard]] const std::vector<ugame::Cue>& cues() const noexcept { return cues_; }
    [[nodiscard]] const std::optional<Hit>& lastHit() const noexcept { return lastHit_; }
    /// The dummies standing.
    [[nodiscard]] std::size_t dummies() const noexcept;
    /// The weapon wheel, open while Wheel is held.
    [[nodiscard]] const uui::Wheel& wheel() const noexcept { return wheel_; }
    /// The game seconds the last `update` ran: its wall seconds times the
    /// game's time scale, for what is drawn on game time.
    [[nodiscard]] double gameSeconds() const noexcept { return gameSeconds_; }
    /// The share of the view the zoom leaves: 1 with none.
    [[nodiscard]] double zoom() const noexcept { return 1 - zoomLevel_ * ZOOM_NARROWS; }

    /// docs/specs/UTA-0021-landing-marker.md SS 4.6: the player's visual
    /// platforming assists, each on by default. Neither changes a command.
    struct Assists {
        bool landingMarker = true;
        bool takeOffPitch = true;
    };
    void setAssists(Assists assists) noexcept { assists_ = assists; }
    /// UTA-0022: the physical platforming assists, each off by default. Each
    /// changes the rules the body moves by, so it is a server's setting; this
    /// viewer is its own server.
    struct PhysicalAssists {
        bool coyoteTime = false;
        bool mantle = false;
        bool highStep = false;
    };
    void setPhysicalAssists(PhysicalAssists assists) noexcept;
    /// UTA-0108: whether a hard landing hurts; on by default, and a server's
    /// setting as the assists are.
    void setLandingDamage(bool on) noexcept { rules_.landingDamage = on; }
    [[nodiscard]] const uworld::MoveRules& rules() const noexcept { return rules_; }
    /// UTA-0493: the level's zones, so leaving the level kills and a pain
    /// zone hurts. Without them neither does.
    void setZones(std::optional<uworld::ZoneWorld> zones) noexcept { zones_ = zones; }
    /// The drawn view's horizontal field of view and width over height,
    /// which the take-off pitch frames the landing in.
    void setViewShape(double fov, double aspect) noexcept;
    /// Where the body lands, from the last tick: none while walking, past
    /// the horizon, or with the marker off.
    [[nodiscard]] std::optional<uworld::Landing> landing() const noexcept {
        return assists_.landingMarker ? projected_ : std::nullopt;
    }
    /// Degrees the drawn view tips below the aim now.
    [[nodiscard]] double tip() const noexcept { return tip_; }
    /// The crosshair's height for `view(fov, width, height)`: the aim ray's,
    /// as a share of the half-height above the image's centre.
    [[nodiscard]] double crosshairUp(double fov, std::uint32_t width, std::uint32_t height) const noexcept;

private:
    uworld::FixedTick clock_;
    uworld::DoubleTap tap_;
    uworld::DodgeDir dodgeAsked_ = uworld::DodgeDir::None; ///< a Dodge press waiting for a tick
    uworld::MoveRules rules_;
    std::optional<uworld::ZoneWorld> zones_;
    uworld::Teleporters teleporters_;
    ugame::WeaponSet set_;
    ugame::Game game_; ///< bodies[0] is the player; a dead dummy's place is used again
    std::vector<uworld::MoveCommand> commands_; ///< one per body; a dummy's asks for nothing
    std::vector<ugame::Event> events_;
    std::vector<ugame::Cue> cues_;
    std::vector<Spawn> spawns_;
    std::size_t nextSpawn_ = 0;
    std::optional<Hit> lastHit_;
    uworld::MoveState previous_;
    bool fireAsked_ = false, altAsked_ = false; ///< held in a frame that ran no tick
    int slotAsked_ = -1, stepAsked_ = 0;        ///< a weapon key waiting for a tick
    uui::Wheel wheel_;
    int pickAsked_ = -1;      ///< a weapon picked on the wheel, waiting for a tick
    double gameSeconds_ = 0;  ///< the last update's
    double zoomLevel_ = 0; ///< seconds of zooming in, to ZOOM_SECONDS
    bool zooming_ = false, altWas_ = false;
    /// The eye above the body's centre after the last two ticks: it lags a
    /// step up or down and eases back to EYE_HEIGHT (spec SS 4.8).
    double eyeHeight_ = EYE_HEIGHT, previousEyeHeight_ = EYE_HEIGHT;
    double pitch_, yaw_; ///< UT angle units, as FlyCamera keeps them; the aim, never tipped
    Assists assists_;
    std::optional<uworld::Landing> projected_; ///< the last tick's landing, kept for either assist
    double tip_ = 0;                           ///< degrees, eased on the wall clock
    double viewFov_ = FlyCamera::DEFAULT_FOV, viewAspect_ = 16.0 / 9.0;

    /// The pitch the view is drawn at: the aim less the tip, in UT units.
    [[nodiscard]] std::int32_t drawnPitch() const noexcept;

    [[nodiscard]] ugame::Body armed(const Spawn& at) const;
    void respawn();
    void dropDummy(const uworld::BodyWorld& world);
    void zoomBy(bool altFire, double seconds) noexcept;
};

/// `centre` where a standing body overlaps nothing there; else a place above
/// it, within a body's height, where one is clear of the floor and falls
/// onto it: a walker made in a floor could never move (UTA-0472). `centre`
/// itself where nothing that near fits.
[[nodiscard]] std::array<double, 3> clearOf(const uworld::BodyWorld& world, std::array<double, 3> centre);

/// UTA-0493 SS 4.4: each of `starts` lifted clear of the floor by `clearOf`,
/// less any whose lifted centre `zones` puts outside the level. UT99's spawn
/// fails at such a start and the engine tries another; a body placed there
/// here would die on its first tick. Without `zones` none is dropped.
[[nodiscard]] std::vector<Walker::Spawn> standable(const uworld::BodyWorld& world, const uworld::ZoneWorld* zones,
                                                   const std::vector<Walker::Spawn>& starts);

} // namespace uta::client

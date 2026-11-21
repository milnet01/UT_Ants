// Walking in ut-ants -- Walker.h.

#include "Walker.h"

#include "uui/Platforming.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace uta::client {
namespace {

/// A stick or key pushed this far along an axis counts as the key held, for
/// a double tap or a Dodge press.
constexpr float TAP_PRESS = 0.5f;

/// The eye's height above the body's centre after one tick from `before` to
/// `after`: UT99's PlayerPawn.UpdateEyeHeight, run once a tick. Walking, the
/// eye holds its height in the world while the body steps, then closes ten
/// times a second's share of the gap to EYE_HEIGHT; otherwise, and on the
/// tick it lands, it closes at least 35% of the gap.
double eyeAfterTick(double eye, const uworld::MoveState& before, const uworld::MoveState& after) noexcept {
    // The original's DeltaTime / TimeDilation: a tick in real seconds.
    const double realDt = 1.0 / uworld::TICKS_PER_SECOND;
    const bool justLanded = before.physics == uworld::Physics::Falling;
    if (after.physics != uworld::Physics::Walking || justLanded) {
        const double smooth = std::clamp(10 * realDt, 0.35, 1.0);
        return eye * (1 - smooth) + Walker::EYE_HEIGHT * smooth;
    }
    const double smooth = std::min(1.0, 10 * realDt);
    const double rise = after.location.z - before.location.z;
    eye = (eye - rise) * (1 - smooth) + Walker::EYE_HEIGHT * smooth;
    const double halfHeight = uworld::BodySize{}.halfHeight;
    const double drop = std::clamp(-rise, 0.0, uworld::MoveRules{}.maxStepHeight);
    return std::clamp(eye, -0.5 * halfHeight, halfHeight + drop);
}

/// The way a Dodge press goes: along the axis the move points most, or none
/// when nothing is pushed past TAP_PRESS.
uworld::DodgeDir dodgeToward(float forward, float right) noexcept {
    if (std::max(std::abs(forward), std::abs(right)) <= TAP_PRESS) return uworld::DodgeDir::None;
    if (std::abs(forward) >= std::abs(right)) return forward > 0 ? uworld::DodgeDir::Forward : uworld::DodgeDir::Back;
    return right > 0 ? uworld::DodgeDir::Right : uworld::DodgeDir::Left;
}

/// The number key pressed in `input`, 0 to 9, or -1 for none.
int slotPressed(const uinput::Frame& input) noexcept {
    const auto first = static_cast<std::size_t>(uinput::Action::Weapon1);
    for (std::size_t key = 0; key < 10; ++key) {
        if (input.pressed.test(first + key)) return static_cast<int>((key + 1) % 10); // 1 to 9, then 0
    }
    return -1;
}

std::int32_t units(double angle) noexcept {
    return static_cast<std::int32_t>(std::lround(angle));
}

} // namespace

std::array<double, 3> clearOf(const uworld::BodyWorld& world, std::array<double, 3> centre) {
    const uworld::BodySize size;
    constexpr double STEP = 3;
    const auto raised = [&centre](double lift) { return uworld::Vec3{centre[0], centre[1], centre[2] + lift}; };
    for (double lift = 0; lift <= 2 * size.halfHeight; lift += STEP) {
        if (!world.fits(size, raised(lift))) continue;
        // A step more where there is room: a body lifted to just touch the
        // floor fits, and still cannot move along it. It falls the step back.
        if (lift > 0 && world.fits(size, raised(lift + STEP))) lift += STEP;
        return {centre[0], centre[1], centre[2] + lift};
    }
    return centre;
}

std::vector<Walker::Spawn> standable(const uworld::BodyWorld& world, const uworld::ZoneWorld* zones,
                                     const std::vector<Walker::Spawn>& starts) {
    std::vector<Walker::Spawn> out;
    for (const Walker::Spawn& start : starts) {
        const std::array<double, 3> centre = clearOf(world, start.location);
        if (zones != nullptr && zones->at({centre[0], centre[1], centre[2]}).kind == umap::ZoneHit::Kind::Outside)
            continue;
        out.push_back({centre, start.yaw});
    }
    return out;
}

Walker::Walker(std::array<double, 3> location, double pitch, double yaw, uworld::Teleporters teleporters,
               ugame::WeaponSet set, std::vector<Spawn> spawns)
    : teleporters_(std::move(teleporters)), set_(std::move(set)), spawns_(std::move(spawns)), pitch_(pitch),
      yaw_(yaw) {
    rules_.airJumps = 1; // a double jump (user, 2026-11-10)
    if (spawns_.empty()) spawns_.push_back({location, yaw});
    game_.bodies.push_back(armed({location, yaw}));
    previous_ = game_.bodies[0].move;
}

void Walker::setPhysicalAssists(PhysicalAssists assists) noexcept {
    rules_.coyoteTime = assists.coyoteTime ? uworld::COYOTE_TIME_ON : 0;
    rules_.mantleHeight = assists.mantle ? uworld::MANTLE_HEIGHT_ON : 0;
    rules_.extraStepHeight = assists.highStep ? uworld::EXTRA_STEP_HEIGHT_ON : 0;
}

ugame::Body Walker::armed(const Spawn& at) const {
    ugame::Body body = ugame::bodyAt(set_, {at.location[0], at.location[1], at.location[2]}, units(at.yaw));
    ugame::giveAll(set_, body);
    body.move.physics = uworld::Physics::Falling;
    body.human = true; // UTA-0020 SS 4.8: the player; a dummy is not
    return body;
}

void Walker::respawn() {
    const Spawn& at = spawns_[nextSpawn_++ % spawns_.size()];
    game_.bodies[0] = armed(at);
    yaw_ = at.yaw;
    pitch_ = 0;
    previous_ = game_.bodies[0].move; // the view jumps, as at a teleporter
    eyeHeight_ = previousEyeHeight_ = EYE_HEIGHT;
    zoomLevel_ = 0;
    zooming_ = false;
}

void Walker::dropDummy(const uworld::BodyWorld& world) {
    const ugame::Body& self = game_.bodies[0];
    // As far ahead as a body can be pushed, so it never stands in a wall --
    // pushed from as high as the ceiling lets it be lifted, up to its own
    // half-height, so ground that rises ahead does not stop it; it then falls.
    const uworld::Vec3 lift{0, 0, self.size.halfHeight};
    const uworld::BodyHit raised = world.sweep(self.size, self.move.location, self.move.location + lift);
    const uworld::Vec3 from = self.move.location + lift * (raised.startsInside ? 0.0 : raised.fraction);
    const uworld::Vec3 ahead = ugame::aimOf(units(yaw_), 0) * DUMMY_AHEAD;
    const uworld::BodyHit reach = world.sweep(self.size, from, from + ahead);
    if (reach.startsInside || reach.fraction * DUMMY_AHEAD < 3 * self.size.radius) return; // no room
    const uworld::Vec3 at = from + ahead * reach.fraction;

    if (dummies() >= MAX_DUMMIES) return;
    std::size_t place = 1;
    while (place < game_.bodies.size() && game_.bodies[place].alive()) ++place;
    if (place == game_.bodies.size()) game_.bodies.emplace_back();
    // Facing the player, and falling, so it comes to stand on the floor.
    game_.bodies[place] = ugame::bodyAt(set_, at, units(yaw_) + 32768);
    game_.bodies[place].move.physics = uworld::Physics::Falling;
}

std::size_t Walker::dummies() const noexcept {
    std::size_t standing = 0;
    for (std::size_t i = 1; i < game_.bodies.size(); ++i) standing += game_.bodies[i].alive() ? 1 : 0;
    return standing;
}

void Walker::zoomBy(bool altFire, double seconds) noexcept {
    const ugame::Body& self = game_.bodies[0];
    const bool zooms = self.weapon >= 0 && !set_.weapons[static_cast<std::size_t>(self.weapon)].alt.has_value();
    if (!zooms) {
        zoomLevel_ = 0;
        zooming_ = false;
    } else if (altFire && !altWas_) { // PlayerPawn.ToggleZoom: a press zooms in, or ends a zoom
        zooming_ = zoomLevel_ == 0;
        zoomLevel_ = 0;
    } else if (!altFire) { // StopZoom: a release holds the view where it is
        zooming_ = false;
    }
    if (zooming_) zoomLevel_ = std::min(zoomLevel_ + seconds, ZOOM_SECONDS);
    altWas_ = altFire;
}

void Walker::update(const uinput::Frame& input, double seconds, const uworld::BodyWorld& world) {
    // UTA-0020 SS 4.8: held, the weapon wheel is open and takes the look and
    // the triggers; let go, its highlighted weapon waits for a tick.
    if (input.wheel && !wheel_.isOpen()) {
        wheel_.open(ugame::wheelWeapons(set_, game_.bodies[0]));
    } else if (!input.wheel && wheel_.isOpen()) {
        if (const int picked = wheel_.close(); picked >= 0) pickAsked_ = picked;
    }
    wheel_.point(input);
    const bool wheelOpen = wheel_.isOpen();
    if (wheelOpen) fireAsked_ = altAsked_ = false;
    const double lookRight = wheelOpen ? 0 : input.lookRight;
    const double lookUp = wheelOpen ? 0 : input.lookUp;
    const bool fire = input.fire && !wheelOpen;
    const bool altFire = input.altFire && !wheelOpen;

    // The look as the fly camera's, so switching between them keeps the view;
    // a zoomed view turns as much slower as it is narrower, as UT99's does.
    yaw_ = std::fmod(yaw_ + lookRight * zoom(), 65536.0);
    pitch_ = std::clamp(pitch_ + lookUp * zoom(), -double(FlyCamera::PITCH_LIMIT),
                        double(FlyCamera::PITCH_LIMIT));
    if (input.wasPressed(uinput::Action::Dodge)) dodgeAsked_ = dodgeToward(input.forward, input.right);
    if (const int slot = slotPressed(input); slot >= 0) slotAsked_ = slot;
    if (input.wasPressed(uinput::Action::NextWeapon) != input.wasPressed(uinput::Action::PrevWeapon))
        stepAsked_ = input.wasPressed(uinput::Action::NextWeapon) ? 1 : -1;
    fireAsked_ = fireAsked_ || fire;
    altAsked_ = altAsked_ || altFire;
    if (input.wasPressed(uinput::Action::DropDummy)) dropDummy(world);
    zoomBy(altFire, seconds);
    events_.clear();
    cues_.clear();

    uworld::MoveCommand command;
    command.forward = input.forward;
    command.strafe = input.right;
    command.yaw = units(yaw_);
    command.pitch = units(pitch_);
    command.jump = input.up > 0;
    command.duck = input.up < 0;
    command.walk = input.walk;
    command.wheelOpen = wheelOpen;
    const uworld::DirectionKeys keys{input.forward > TAP_PRESS, input.forward < -TAP_PRESS, input.right < -TAP_PRESS,
                                     input.right > TAP_PRESS};

    // The scale the last tick set (UTA-0020 SS 4.4): the frame that opens
    // the wheel still runs at full speed, and the slow starts with the next.
    const double scale = game_.timeScale;
    gameSeconds_ = seconds * scale;
    const int due = clock_.advance(seconds, scale);
    for (int t = 0; t < due; ++t) {
        const bool dodgeReady = uworld::dodgeReady(game_.bodies[0].move);
        command.dodge = tap_.update(keys, clock_.dt(), dodgeReady);
        if (dodgeAsked_ != uworld::DodgeDir::None) {
            if (command.dodge == uworld::DodgeDir::None && dodgeReady) command.dodge = dodgeAsked_;
            dodgeAsked_ = uworld::DodgeDir::None;
        }
        command.fire = fire || fireAsked_;
        command.altFire = altFire || altAsked_;
        command.weaponSlot = static_cast<std::int8_t>(slotAsked_);
        command.weaponStep = static_cast<std::int8_t>(stepAsked_);
        command.weaponPick = static_cast<std::int8_t>(pickAsked_);
        fireAsked_ = altAsked_ = false;
        slotAsked_ = -1;
        stepAsked_ = 0;
        pickAsked_ = -1;

        std::vector<double> healthBefore;
        for (const ugame::Body& body : game_.bodies) healthBefore.push_back(body.health);
        commands_.assign(game_.bodies.size(), uworld::MoveCommand{});
        commands_[0] = command;
        previous_ = game_.bodies[0].move;
        ugame::tick(world, set_, rules_, game_, commands_, clock_.dt(), zones_ ? &*zones_ : nullptr);
        events_.insert(events_.end(), game_.events.begin(), game_.events.end());
        cues_.insert(cues_.end(), game_.cues.begin(), game_.cues.end());
        for (std::size_t i = 1; i < game_.bodies.size(); ++i) {
            if (game_.bodies[i].health < healthBefore[i])
                lastHit_ = Hit{healthBefore[i] - game_.bodies[i].health, game_.bodies[i].health};
        }
        if (!game_.bodies[0].alive()) {
            respawn();
            command.yaw = units(yaw_);
            command.pitch = units(pitch_);
            continue;
        }
        uworld::MoveState& state = game_.bodies[0].move;
        previousEyeHeight_ = eyeHeight_;
        eyeHeight_ = eyeAfterTick(eyeHeight_, previous_, state);
        if (const auto arrival = teleporters_.touch(world, game_.bodies[0].size, state.location, state.velocity,
                                                    yaw_, rules_, clock_.dt())) {
            if (arrival->launched) { // a jump pad: SetPhysics(PHYS_Falling), in place
                state.velocity = arrival->velocity;
                state.physics = uworld::Physics::Falling;
                continue;
            }
            state.location = arrival->location;
            state.velocity = arrival->velocity;
            yaw_ = arrival->yaw;
            command.yaw = units(yaw_);
            previous_ = state; // the view jumps, rather than sweeping across the map
            previousEyeHeight_ = eyeHeight_;
        }
    }

    // UTA-0021 SS 4.6: where the body lands, after this frame's ticks, and
    // how far the drawn view tips to show it, eased on the wall clock.
    const ugame::Body& self = game_.bodies[0];
    if (due > 0) {
        projected_ = assists_.landingMarker || assists_.takeOffPitch
                         ? uworld::projectLanding(world, self.size, rules_, self.move, command, clock_.dt())
                         : std::nullopt;
    }
    double target = 0;
    if (assists_.takeOffPitch && projected_ && zoomLevel_ == 0) { // a moving crosshair would spoil a zoomed shot
        const uworld::Vec3& at = self.move.location;
        const uworld::Vec3& lands = projected_->location;
        target = uui::tipTarget({at.x, at.y, at.z + eyeHeight_}, pitch_, yaw_,
                                {lands.x, lands.y, lands.z - self.size.halfHeight}, viewFov_, viewAspect_);
    }
    tip_ = assists_.takeOffPitch ? uui::easeTip(tip_, target, seconds) : 0;
}

void Walker::setViewShape(double fov, double aspect) noexcept {
    if (fov > 0) viewFov_ = fov;
    if (aspect > 0) viewAspect_ = aspect;
}

std::int32_t Walker::drawnPitch() const noexcept {
    return units(std::max(pitch_ - tip_ * 65536.0 / 360, -double(FlyCamera::PITCH_LIMIT)));
}

double Walker::crosshairUp(double fov, std::uint32_t width, std::uint32_t height) const noexcept {
    const double tipped = (units(pitch_) - drawnPitch()) * 2 * std::numbers::pi / 65536;
    if (tipped <= 0 || width == 0 || height == 0) return 0;
    const double halfHeight = view(fov, width, height).verticalFovDegrees * std::numbers::pi / 360;
    return std::tan(tipped) / std::tan(halfHeight);
}

FlyCamera Walker::eye(double fov) const noexcept {
    const double blend = clock_.blend();
    const uworld::Vec3 now = game_.bodies[0].move.location;
    const uworld::Vec3 at = previous_.location + (now - previous_.location) * blend;
    const double eyeHeight = previousEyeHeight_ + (eyeHeight_ - previousEyeHeight_) * blend;
    FlyCamera view({static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z + eyeHeight)},
                   drawnPitch(), units(yaw_));
    view.setFov(fov);
    return view;
}

urender::Camera Walker::view(double fov, std::uint32_t width, std::uint32_t height) const noexcept {
    urender::Camera camera = eye(fov).camera(width, height);
    if (zoomLevel_ > 0 && width > 0 && height > 0) {
        // FlyCamera::camera's own sum, on a view narrower than a FlyCamera goes.
        const double half = fov * zoom() * std::numbers::pi / 360.0;
        camera.verticalFovDegrees = static_cast<float>(
            2 * std::atan(std::tan(half) * static_cast<double>(height) / width) * 180.0 / std::numbers::pi);
    }
    return camera;
}

} // namespace uta::client

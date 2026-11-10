// UTA-0017's real-asset case, INV-2 of docs/specs/UTA-0017-movement-rules.md:
// replaying every run UTA-0456 recorded on DM-Deck16][, row by row, each
// row's keys over that row's dt, our tick matches the original row by row.
//
// The recordings are read from the directory UTA_MOVEMENT_RECORDINGS names,
// one ut-record run, through readMovementRecording; with no such directory the
// case fails, never skips. Every run at 30, 60 and 85 fps is replayed,
// including those summary.tsv marks missed: each row runs under its own dt.
// Friction on a tick longer than LONG_TICK is replayed and not compared.
//
// EACH ROW STARTS FROM THE RECORDING'S STATE: its position, velocity and
// physics. Our own dodge, jump and double-tap state carry on from our ticks.
//
// THE ORIGINAL HOVERS, OURS DOES NOT (spec SS 3 item 4). Row 0's body is
// dropped to the floor, and the original's height above ours there is taken
// off every recorded height before comparing.
//
// THE LEVEL ONLY, NO MOVERS, as UTA-0457's case: no recorded run rides one.

#include "uworld/BodyCollision.h"
#include "uworld/Movement.h"

#include "RealLevel.h"
#include "support/MovementRecording.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace uta::uworld;

/// INV-2's tolerances, in units and units a second. Measured 2026-11-10 at
/// most SKIN (the floor probe stepping off a leaning wall), 0.10 (a stair)
/// and 0.73 (air control); tight enough that falling by the end velocity
/// instead of the mean, half g dt^2 a tick (0.08 at 85 fps), fails the
/// height.
constexpr double MAX_POSITION_ERROR = 0.15;
constexpr double MAX_HEIGHT_ERROR = 0.15;
constexpr double MAX_VELOCITY_ERROR = 1;

struct Recorded {
    std::string run, rate;
};

/// summary.tsv's rows at 30, 60 and 85 fps, whatever their status.
std::vector<Recorded> recordedRuns(const fs::path& directory) {
    std::ifstream summary(directory / "summary.tsv");
    REQUIRE(summary.good());
    std::vector<Recorded> runs;
    std::string line;
    std::getline(summary, line); // the header
    while (std::getline(summary, line)) {
        std::istringstream cells(line);
        std::string run, rate;
        std::getline(cells, run, '\t');
        std::getline(cells, rate, '\t');
        if (rate == "30" || rate == "60" || rate == "85") runs.push_back({run, rate});
    }
    return runs;
}

Vec3 vec(const std::array<double, 3>& a) { return {a[0], a[1], a[2]}; }

bool has(const std::string& keys, char key) { return keys.find(key) != std::string::npos; }

Physics physicsOf(const std::string& recorded) {
    return recorded == "PHYS_Falling" ? Physics::Falling : Physics::Walking;
}

struct Errors {
    double position = 0, velocity = 0, height = 0, wallPosition = 0;
    std::size_t positionRow = 0, velocityRow = 0, heightRow = 0;
    std::size_t rows = 0, physicsMisses = 0, firstMiss = 0, wallRows = 0, longFriction = 0, earlyLandings = 0, overlapping = 0;
};

/// How far off a wall the original stops short of touching it: up to 2.8 on
/// the tick it hits one in the air (dodge-left). UTA-0457 measured 0.24 to
/// 1.57 walking.
constexpr double WALL_GAP = 3;

/// Above this a tick is a frame the original dropped, and friction on it --
/// braking, or turning towards the keys -- follows a rule nobody has found
/// (spec SS 14); 1 - 16 dt holds to 0.0206. Our own tick is 1.1 / 85, so the
/// rule never reaches the game.
constexpr double LONG_TICK = 0.025;

/// How far the original's box corner and gap reach past our round body.
double wallReach(const BodySize& body) { return body.radius * (std::numbers::sqrt2 - 1) + WALL_GAP; }

/// The level normal of a surface too steep to stand on that a body at `at`
/// touches, as ours or as the original's; none where it touches none.
///
/// THE ORIGINAL IS A BOX AGAINST THE LEVEL. UT99 meets level geometry with
/// an axis-aligned box `radius` each side, so against a wall turned from
/// the axes its corner reaches radius * (|nx| + |ny| - 1) nearer than our
/// round body does (wall-angle: 5.6 nearer, at normal (0.42, 0.91)).
std::optional<Vec3> wallNear(const BodyWorld& world, const BodySize& body, const MoveRules& rules, const Vec3& at) {
    constexpr double D = 0.70710678118654752;
    constexpr std::array<std::array<double, 2>, 8> WAYS{
        {{1, 0}, {D, D}, {0, 1}, {-D, D}, {-1, 0}, {-D, -D}, {0, -1}, {D, -D}}};
    const double reach = wallReach(body);
    for (const auto& w : WAYS) {
        const Vec3 way{w[0], w[1], 0};
        const BodyHit hit = world.sweep(body, at, at + way * reach);
        if (hit.fraction >= 1 || hit.startsInside || hit.normal.z >= rules.walkableFloorZ) continue;
        Vec3 n{hit.normal.x, hit.normal.y, 0};
        n = n * (1.0 / length(n));
        const double apart = hit.fraction * reach * -dot(way, n);
        const double corner = body.radius * (std::abs(n.x) + std::abs(n.y) - 1);
        if (apart - corner <= WALL_GAP) return n;
    }
    return std::nullopt;
}

/// Whether walking friction acted on `row`'s tick: braking, with no keys
/// held, or turning, the velocity not along the keys. `next` holds the
/// acceleration that tick used.
bool frictionActs(const uta::test::RecordedTick& row, const uta::test::RecordedTick& next) {
    const Vec3 v{row.velocity[0], row.velocity[1], 0};
    const Vec3 a{next.acceleration[0], next.acceleration[1], 0};
    if (v == Vec3{}) return false;
    if (a == Vec3{}) return true;
    return std::abs(v.x * a.y - v.y * a.x) > 1e-3 * length(v) * length(a);
}

/// Where our body starts a row: the original's place less its hover, and,
/// walking, dropped onto the floor beneath the original, which on a stair
/// hovers by other than row 0's height.
Vec3 startOf(const BodyWorld& world, const BodySize& body, const MoveRules& rules,
             const uta::test::RecordedTick& row, double hover) {
    Vec3 at = vec(row.location);
    if (physicsOf(row.physics) == Physics::Falling) {
        at.z -= hover;
        return at;
    }
    const Vec3 drop{0, 0, -(hover + rules.maxStepHeight)};
    const BodyHit floor = world.sweep(body, at, at + drop);
    return floor.fraction < 1 && !floor.startsInside ? at + drop * floor.fraction : Vec3{at.x, at.y, at.z - hover};
}

Errors replay(const BodyWorld& world, const uta::test::MovementRecording& recording) {
    const BodySize body{recording.pawn.at("CollisionRadius"), recording.pawn.at("CollisionHeight")};
    const MoveRules rules;
    const auto& rows = recording.ticks;
    const auto yaw = static_cast<std::int32_t>(recording.pose[3]);

    MoveState state;
    state.location = vec(rows[0].location);
    state.velocity = vec(rows[0].velocity);
    state.physics = physicsOf(rows[0].physics);
    double hover = 0;
    if (state.physics == Physics::Walking) {
        const Vec3 drop{0, 0, -50};
        const BodyHit floor = world.sweep(body, state.location, state.location + drop);
        REQUIRE(floor.fraction < 1);
        state.location = state.location + drop * floor.fraction;
        hover = rows[0].location[2] - state.location.z;
    }

    Errors e;
    DoubleTap tap(rules.dodgeClickTime);
    for (std::size_t k = 0; k + 1 < rows.size(); ++k) {
        const auto& row = rows[k];
        // Each tick starts where the original's did. Replayed open, a hair
        // the original decides by -- a speed within float noise of stopSpeed
        // or airSpeed -- would become a drift for the rest of the run.
        const Physics before = physicsOf(row.physics);
        state.location = startOf(world, body, rules, row, hover);
        state.velocity = vec(row.velocity);
        state.physics = before;

        MoveCommand command;
        command.forward = row.forward / 6000;
        command.strafe = row.strafe / 6000;
        command.yaw = yaw;
        command.jump = has(row.keys, 'J');
        command.walk = has(row.keys, 'W');
        command.duck = has(row.keys, 'D');
        const DirectionKeys keys{has(row.keys, 'F'), has(row.keys, 'B'), has(row.keys, 'L'), has(row.keys, 'R')};
        command.dodge = tap.update(keys, row.dt, dodgeReady(state));
        // Where the original's hover lifts it clear of a wall's leaning foot,
        // our body placed beneath it can overlap the foot and not move.
        const bool startFits = world.fits(body, state.location);
        state = tick(world, body, rules, state, command, row.dt);

        // One tick's grace either side of a change of physics: the target row
        // and its neighbours share the start's physics.
        const std::size_t j = k + 1;
        const auto& want = rows[j];
        const auto same = [&](std::size_t i) { return i >= rows.size() || physicsOf(rows[i].physics) == before; };
        const bool settled = same(j) && same(j + 1) && (k == 0 || same(k - 1));
        if (!settled) continue;
        if (!startFits) {
            ++e.overlapping;
            continue;
        }
        ++e.rows;
        Vec3 at = vec(want.location);
        at.z -= hover;
        if (state.physics != before) {
            // Falling, the original drops the hover it walks at before its
            // box touches, so ours stands on the floor first.
            if (state.physics == Physics::Walking && std::abs(state.location.z - at.z) <= hover + 1) {
                ++e.earlyLandings;
                continue;
            }
            if (e.physicsMisses++ == 0) e.firstMiss = j;
            continue;
        }
        if (before == Physics::Walking && row.dt > LONG_TICK && frictionActs(row, want)) {
            ++e.longFriction;
            continue;
        }
        // Against a wall the original stops short of it by a gap ours does
        // not copy (spec SS 3 item 4), and its velocity jitters with the gap:
        // there the position is held along the wall only, and the velocity
        // not at all.
        auto wall = wallNear(world, body, rules, state.location);
        if (!wall) wall = wallNear(world, body, rules, at);
        // The original's own body rides `hover` higher, and meets a leaning
        // wall sooner there (wall-angle).
        if (!wall) wall = wallNear(world, body, rules, vec(want.location));
        if (!wall) wall = wallNear(world, body, rules, startOf(world, body, rules, row, hover));
        Vec3 off = state.location - at;
        const double height = std::abs(off.z);
        off.z = 0;
        if (height > e.height) e.height = height, e.heightRow = j;
        if (wall) {
            // Along the wall, the original stops short of ours by at most its
            // box corner and gap.
            ++e.wallRows;
            e.wallPosition = std::max(e.wallPosition, length(off - *wall * dot(off, *wall)));
            continue;
        }
        const double position = length(off);
        if (position > e.position) e.position = position, e.positionRow = j;
        const double velocity = length(state.velocity - vec(want.velocity));
        if (velocity > e.velocity) e.velocity = velocity, e.velocityRow = j;
    }
    return e;
}

} // namespace

TEST_CASE("UTA-0017 INV-2: our rules copy the original's tick for tick, on DM-Deck16][",
          "[real][movement]") {
    const char* recordings = std::getenv("UTA_MOVEMENT_RECORDINGS");
    INFO("UTA_MOVEMENT_RECORDINGS names a ut-record run's directory");
    REQUIRE(recordings != nullptr);
    const fs::path directory(recordings);

    uta::ubundle::Collision collision;
    collision.level = uta::test::real::levelTreeOf(fs::path(UTA_UT_INSTALL_DIR) / "Maps" / "DM-Deck16][.unr");
    auto built = BodyWorld::build(collision, {});
    REQUIRE(built.has_value());

    const auto runs = recordedRuns(directory);
    REQUIRE(!runs.empty());
    for (const Recorded& r : runs) {
        std::ifstream file(directory / (r.run + "-" + r.rate + ".utarec"));
        REQUIRE(file.good());
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto recording = uta::test::readMovementRecording(text);
        REQUIRE(recording.has_value());
        const Errors e = replay(*built, *recording);
        std::cout << "UTA-0017 " << r.run << " " << r.rate << " fps, " << e.rows << " rows: position "
                  << e.position << " (row " << e.positionRow << "), velocity " << e.velocity << " (row "
                  << e.velocityRow << "), height " << e.height << " (row " << e.heightRow << "), physics misses "
                  << e.physicsMisses << ", " << e.wallRows << " against a wall (along it " << e.wallPosition << "), " << e.earlyLandings << " landed early" << (e.physicsMisses > 0 ? " from row " + std::to_string(e.firstMiss) : "")
                  << ", " << e.longFriction << " with friction on a long tick, " << e.overlapping << " starting overlapped\n";
        INFO(r.run << " at " << r.rate << " fps");
        CHECK(e.physicsMisses == 0);
        CHECK(e.position <= MAX_POSITION_ERROR);
        CHECK(e.velocity <= MAX_VELOCITY_ERROR);
        CHECK(e.height <= MAX_HEIGHT_ERROR);
        CHECK(e.wallPosition <= wallReach({recording->pawn.at("CollisionRadius"), recording->pawn.at("CollisionHeight")}));
    }
}

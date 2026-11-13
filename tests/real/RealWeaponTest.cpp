// UTA-0019's real-asset case, INV-6 of docs/specs/UTA-0019-weapon-set.md:
// replaying every weapon run SS 4.7 recorded on DM-Deck16][, the stock set
// matches the original.
//
// The recordings are read from the directory UTA_WEAPON_RECORDINGS names,
// one ut-record run, through readMovementRecording; with no such directory
// the case fails, never skips. Every run in summary.tsv is replayed, each row
// under its own dt; a run summary.tsv marks missed is checked for damage,
// momentum and flight, never timing (SS 4.7).
//
// TWO BODIES: 0 is the firer, 1 the target. EACH ROW STARTS BOTH FROM THE
// RECORDING'S STATE -- position, velocity, physics -- as RealMovementTest's
// rows do. The weapon's own state (refire, charge, ammo) and the shots carry
// on from our ticks: they are what is under test.
//
// ROWS AND TICKS. Our tick k runs row k's keys over row k's dt, and its
// result is row k's W and P lines: the recorder writes a row after the
// tick's press has fired (UTARecPawn.PlayerInput calls Fire before LogTick)
// and before the tick's actors move. A D line carries the row it lands
// before, so a hit after row k's log reads k + 1: within INV-6's tick. So
// does a shot or a spend the weapon's own tick makes, after the row: a row
// past the first is read as either frame (ticksFrom), and a shot's flight is
// compared age for age with ours.
//
// A ROW IS LOGGED AFTER ITS TICK'S HITS. A body a D line at tick k hit
// starts tick k from that line's place, velocity and physics from before
// the hit, not from row k's, which already carry the push. A target ours
// has yet to hit, a tick late, starts the next tick from them too.
// THE WEAPON'S OWN TRACE HURTS THE TARGET BEFORE THAT FRAME MOVES IT, ours
// after: a target hit so while falling is judged without the gravity our
// tick gave it. Shots, and hits on the firer, come after the move in both.
//
// THE ORIGINAL HOVERS, OURS DOES NOT (UTA-0017 SS 3 item 4). Each body's
// height above our floor at row 0 is taken off its recorded heights, and the
// firer's off its shots', so a shot meets a body as the recorded one did.
// THE LEVEL STANDS WHERE THE ORIGINAL'S DOES, so a shot that low meets a
// floor early. A kind that bounces and has no blast is raised by the firer's
// hover from the tick of its first bounce, and flown at the recorded height
// from then on. One with a blast is judged against the bodies and stays low:
// its bounces are taken from the recording and not compared; a bounce that
// would leave it under the floor raises it too, from that tick. A kind that
// sticks is raised the same way from the tick ours first sticks, so it lands
// where the recorded one did; stuck to a floor it is as near the bodies.
//
// THE EYE IS THE RECORDING'S. The original's view shakes with each shot
// (PlayerPawn.ShakeView), which moves where the next one starts; a server's
// eye, and ours, stays put. Each tick's firer takes the recorded eye height.
//
// THE LEVEL ONLY, NO MOVERS: the runs stand on open floor.

#include "ugame/Game.h"
#include "ugame/WeaponSet.h"
#include "uworld/BodyCollision.h"
#include "uworld/Movement.h"

#include "RealLevel.h"
#include "support/MovementRecording.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace ugame = uta::ugame;
namespace uworld = uta::uworld;
using uworld::Vec3;

/// INV-6's tolerances: ticks, units, units a second.
constexpr std::int64_t MAX_TICK_ERROR = 1;
constexpr double MAX_POSITION_ERROR = 1;
constexpr double MAX_VELOCITY_ERROR = 1;

constexpr int FIRER = 0, TARGET = 1;

/// Below this speed a bounce is not compared: the shot is settling, several
/// bounces to a tick, and does no damage (UTChunk.ProcessTouch).
constexpr double SETTLING = 200;

/// Below this speed a projectile alive at row 0 is the level's, not the run's.
constexpr double STILL = 50;

struct Run {
    std::string name, rate;
    bool timed = false; ///< summary.tsv's ok: the run held its rate
};

/// Every summary.tsv row.
std::vector<Run> runsOf(const fs::path& directory) {
    std::ifstream summary(directory / "summary.tsv");
    REQUIRE(summary.good());
    std::vector<Run> runs;
    std::string line;
    std::getline(summary, line); // the header
    while (std::getline(summary, line)) {
        std::istringstream cells(line);
        std::vector<std::string> cell;
        for (std::string c; std::getline(cells, c, '\t');) cell.push_back(c);
        REQUIRE(cell.size() >= 3);
        runs.push_back({cell[0], cell[1], cell.back() == "ok"});
    }
    return runs;
}

Vec3 vec(const std::array<double, 3>& a) { return {a[0], a[1], a[2]}; }

uworld::Physics physicsOf(const std::string& recorded) {
    return recorded == "PHYS_Falling" ? uworld::Physics::Falling : uworld::Physics::Walking;
}

std::string folded(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// The set's weapon standing for `className` (`Botpack.UT_Eightball`), or -1.
int weaponOf(const ugame::WeaponSet& set, const std::string& className) {
    for (std::size_t i = 0; i < set.weapons.size(); ++i)
        if (set.weapons[i].className == folded(className)) return static_cast<int>(i);
    return -1;
}

/// The set's projectile a recorded class stands for; empty for one that is
/// not a shot (a shell case, a tracer, a beam's piece).
std::string kindOf(const std::string& cls) {
    static const std::map<std::string, std::string, std::less<>> KINDS{
        {"RocketMk2", "rocket"},     {"UT_Grenade", "grenade"},     {"UTChunk1", "flak-chunk"},
        {"UTChunk2", "flak-chunk"},  {"UTChunk3", "flak-chunk"},    {"UTChunk4", "flak-chunk"},
        {"flakslug", "flak-shell"},  {"UT_BioGel", "bio-gel"},      {"BioGlob", "bio-glob"},
        {"ShockProj", "shock-ball"}, {"PlasmaSphere", "plasma"},    {"Razor2", "razor"},
        {"Razor2Alt", "razor-alt"}};
    const auto found = KINDS.find(cls);
    return found == KINDS.end() ? std::string{} : found->second;
}

/// How far above our floor the original stands at `at`.
double hoverAt(const uworld::BodyWorld& world, const uworld::BodySize& size, const Vec3& at) {
    const Vec3 drop{0, 0, -50};
    const uworld::BodyHit floor = world.sweep(size, at, at + drop);
    REQUIRE(floor.fraction < 1);
    return -drop.z * floor.fraction;
}

/// One recorded shot: its rows, by row; each is logged before that frame moves it.
struct RecordedShot {
    std::string kind;
    std::int64_t first = 0;
    std::vector<std::pair<std::int64_t, uta::test::RecordedProjectile>> rows;
};

/// One of our shots, followed from tick to tick.
struct OurShot {
    std::uint32_t kind = 0;
    std::int64_t first = 0;
    std::map<std::int64_t, ugame::Shot> rows; ///< k + 1: after our tick k; its first, the launch
    int recorded = -1;                        ///< the recorded shot it was matched to
    std::int64_t late = 0;                    ///< rows the recorded one's launch is after ours
    std::uint32_t bounces = 0;                ///< as of our last tick
    bool raised = false;                      ///< flown at the recorded height; its rows are kept lowered
};

/// A hit as both sides see it: damage summed over a tick, and the victim's
/// velocity after it.
struct Hit {
    std::int64_t tick = 0;
    int victim = 0;
    double damage = 0;
    Vec3 velocity{};
    bool traced = false; ///< ours: by the weapon's own trace, not a shot
};

/// The worst of each comparison over a run.
struct Errors {
    std::size_t ourShots = 0, theirShots = 0, ourHits = 0, theirHits = 0, ourSpends = 0, theirSpends = 0;
    std::int64_t shotTick = 0, hitTick = 0, spendTick = 0;
    double damage = 0, velocity = 0, spawn = 0, flight = 0, bounce = 0;
    std::string damageAt, velocityAt, spawnAt, flightAt, bounceAt;
    std::size_t unmatched = 0;
    bool randomHits = false;    ///< the mode draws its hits: counts are not compared
    std::size_t outOfRange = 0; ///< recorded draws outside our rule's range
    std::string outOfRangeAt;
};

/// The angle between two directions, in UT99's units (65536 a turn).
double angleUnits(const Vec3& a, const Vec3& b) {
    const double c = uworld::dot(a, b) / (uworld::length(a) * uworld::length(b));
    return std::acos(std::clamp(c, -1.0, 1.0)) * 65536 / (2 * 3.14159265358979323846);
}

/// What a launch may draw at random (SS 3 item 6): the kind's own speed and
/// turn, and, for several shots from one press under a rule, Fire.cpp's
/// UT_Eightball draws -- a rocket after the first x 0.9 to 1.1, a grenade
/// turned up to 750 in pitch, yaw and roll, each started up to 4 back.
struct LaunchRange {
    double speed = 0, turn = 0, back = 0;
    [[nodiscard]] bool random() const { return speed > 0 || turn > 0 || back > 0; }
};

LaunchRange rangeOf(const ugame::FireMode& mode, const ugame::Projectile& kind, bool several) {
    LaunchRange range{kind.speedRandom, 2 * std::numbers::sqrt2 * kind.turnRandom, 0};
    if (several && mode.rule == ugame::FireRule::RocketFan) {
        range.back = 4;
        range.speed += 0.1 * kind.speed;
    }
    if (several && mode.rule == ugame::FireRule::GrenadeCluster) {
        range.back = 4;
        range.turn += 2 * std::sqrt(3.0) * 750;
    }
    return range;
}

/// Whether a recorded launch velocity lies in `range`, against `ours`, our
/// own launch of it: its speed less its lift about the kind's, and its
/// direction within both launches' turns.
bool launchInRange(const ugame::Projectile& kind, const LaunchRange& range, Vec3 recorded, Vec3 ours) {
    recorded.z -= kind.lift;
    ours.z -= kind.lift;
    constexpr double SPEED_SLACK = 1, TURN_SLACK = 20;
    const bool speed = std::abs(uworld::length(recorded) - kind.speed) <= range.speed + SPEED_SLACK;
    const bool turn = angleUnits(recorded, ours) <= range.turn + TURN_SLACK;
    return speed && turn;
}

/// How far our tick is from a recorded row. A row past the first shows what
/// the press did in its own frame, or what the weapon's tick did in the
/// frame before, after that frame's row: the recording cannot say which.
std::int64_t ticksFrom(std::int64_t ours, std::int64_t row) {
    const std::int64_t same = std::abs(ours - row);
    return row > 0 ? std::min(same, std::abs(ours - (row - 1))) : same;
}

/// The worst distance between our sorted ticks and the recorded rows, of one length.
std::int64_t worstTick(const std::vector<std::int64_t>& ours, const std::vector<std::int64_t>& rows) {
    std::int64_t worst = 0;
    for (std::size_t i = 0; i < std::min(ours.size(), rows.size()); ++i)
        worst = std::max(worst, ticksFrom(ours[i], rows[i]));
    return worst;
}

/// UTA_WEAPON_RUN names a run whose every hit, shot and spend is printed.
bool detailed(const uta::test::MovementRecording& recording) {
    const char* run = std::getenv("UTA_WEAPON_RUN");
    return run != nullptr && recording.run == run;
}

std::string str(const Vec3& v) {
    std::ostringstream out;
    out << "(" << v.x << " " << v.y << " " << v.z << ")";
    return out.str();
}

/// THE ORIGINAL'S SLEEP ENDS AT THE TICK NEAREST ITS TIME, and carries
/// nothing over: a mode that sleeps between shots keeps a cadence of whole
/// ticks, which moves with the frame rate. Ours keeps its times at any rate
/// (INV-5). So a run is replayed with each such mode's times rounded to that
/// run's ticks, at least one, and a hair under, so a row a hair longer or
/// shorter than `dt` does not move a shot by a tick. The sleepers: both
/// Minigun modes, the Enforcer's alt, the Pulse Gun's primary and the
/// Chainsaw's, which sleeps 0.15 twice a cut and once, and a tick, after
/// the press's.
ugame::WeaponSet slept(ugame::WeaponSet set, double dt) {
    const auto ticks = [dt](double time) { return std::max(1.0, std::round(time / dt)); };
    const auto whole = [&](double& time) {
        if (time > 0) time = (ticks(time) - 0.002) * dt;
    };
    const auto sleeper = [&whole](ugame::FireMode& fire) {
        whole(fire.windUp);
        whole(fire.refire);
        whole(fire.refireLead);
        whole(fire.spinRefire);
    };
    // PBolt grows a piece a tick, whatever the tick: here `extend` is its
    // 81 over this run's.
    const auto grown = [dt](ugame::FireMode& fire) {
        if (fire.extend > 0) fire.extend = 81 / dt;
    };
    for (ugame::Weapon& weapon : set.weapons) {
        grown(weapon.primary);
        if (weapon.alt) grown(*weapon.alt);
        if (weapon.name == "minigun" || weapon.name == "pulse-gun") sleeper(weapon.primary);
        if (weapon.alt && (weapon.name == "minigun" || weapon.name == "enforcer")) sleeper(*weapon.alt);
        if (weapon.name == "chainsaw") {
            const double sleep = ticks(0.15);
            weapon.primary.refire = (2 * sleep - 0.002) * dt;
            weapon.primary.refireLead = (1 - sleep) * dt;
        }
    }
    return set;
}

Errors replay(const uworld::BodyWorld& world, const ugame::WeaponSet& stock,
              const uta::test::MovementRecording& recording) {
    const bool detail = detailed(recording);
    const auto& rows = recording.ticks;
    std::vector<double> dts;
    for (const auto& row : rows) dts.push_back(row.dt);
    std::ranges::nth_element(dts, dts.begin() + static_cast<std::ptrdiff_t>(dts.size() / 2));
    const ugame::WeaponSet set = slept(stock, dts[dts.size() / 2]);
    REQUIRE(recording.weapon.size() == rows.size());
    // A run with no target logs no G lines; its target is dead and away.
    const bool targeted = !recording.target.empty();
    REQUIRE((!targeted || recording.target.size() == rows.size()));

    const uworld::BodySize size{recording.pawn.at("CollisionRadius"), recording.pawn.at("CollisionHeight")};
    const uworld::MoveRules rules;
    const int inHand = weaponOf(set, recording.begin.at("weapon"));
    REQUIRE(inHand >= 0);
    const auto switchTo = recording.begin.find("switch");
    const int other = switchTo == recording.begin.end() || switchTo->second.empty()
                          ? -1
                          : weaponOf(set, switchTo->second);

    // The mode under test: alt where the run ever holds alt fire.
    const ugame::Weapon& weapon = set.weapons[static_cast<std::size_t>(inHand)];
    const bool alt = std::ranges::any_of(rows, [](const auto& row) { return row.keys.find('A') != std::string::npos; });
    const ugame::FireMode* mode = alt && weapon.alt ? &*weapon.alt : &weapon.primary;

    // The bodies, each carrying only the run's weapons.
    ugame::Game game;
    for (int i = 0; i < 2; ++i) {
        ugame::Body body;
        body.size = size;
        body.weapons.resize(set.weapons.size());
        body.health = 1e9;
        game.bodies.push_back(body);
    }
    ugame::Body& firer = game.bodies[FIRER];
    // The recorder gives full ammo (UTARec.GiveWeapon); row 0 is after its
    // first press.
    firer.weapons[static_cast<std::size_t>(inHand)] =
        ugame::Carried{true, set.weapons[static_cast<std::size_t>(inHand)].ammoMax};
    if (other >= 0)
        firer.weapons[static_cast<std::size_t>(other)] =
            ugame::Carried{true, set.weapons[static_cast<std::size_t>(other)].ammoMax};
    firer.weapon = inHand;
    const double firerHover = hoverAt(world, size, vec(rows[0].location));
    const double targetHover = targeted ? hoverAt(world, size, vec(recording.target[0].location)) : 0;
    if (!targeted) game.bodies[TARGET].health = 0;

    // The kinds this run's weapons launch, and what those split into.
    std::set<std::string> launched;
    for (const int carried : {inHand, other}) {
        if (carried < 0) continue;
        const ugame::Weapon& gun = set.weapons[static_cast<std::size_t>(carried)];
        for (const ugame::FireMode* fire : {&gun.primary, gun.alt ? &*gun.alt : nullptr}) {
            if (fire == nullptr || fire->projectile.empty()) continue;
            launched.insert(fire->projectile);
            const ugame::Projectile* kind = set.projectile(fire->projectile);
            if (kind != nullptr && !kind->splitInto.empty()) launched.insert(kind->splitInto);
        }
    }

    // The recorded shots. One alive at row 0 and all but still is the level's
    // own, left by an earlier run; so is one of a kind this run's weapons
    // do not launch (an earlier run's grenade, still in the air).
    std::map<std::string, RecordedShot> theirs;
    std::vector<std::string> theirOrder;
    std::vector<std::string> before;
    for (const auto& p : recording.projectiles) {
        if (p.tick == 0 && uworld::length(vec(p.velocity)) < STILL) before.push_back(p.name);
        if (std::ranges::find(before, p.name) != before.end()) continue;
        const std::string kind = kindOf(p.cls);
        if (kind.empty() || !launched.contains(kind)) continue;
        auto [at, fresh] = theirs.try_emplace(p.name, RecordedShot{kind, p.tick, {}});
        if (fresh) theirOrder.push_back(p.name);
        at->second.rows.emplace_back(p.tick, p);
    }

    // The recorded hits, summed per tick and victim, and the spends.
    std::vector<Hit> theirHits;
    for (const auto& d : recording.damage) {
        const int victim = d.victim.starts_with("UTARecTarget") ? TARGET : FIRER;
        if (!theirHits.empty() && theirHits.back().tick == d.tick && theirHits.back().victim == victim) {
            theirHits.back().damage += d.health0 - d.health1;
            theirHits.back().velocity = vec(d.velocity1);
        } else {
            theirHits.push_back({d.tick, victim, d.health0 - d.health1, vec(d.velocity1)});
        }
    }
    std::vector<std::int64_t> theirSpends;
    if (recording.weapon[0].ammo >= 0 && recording.weapon[0].ammo < set.weapons[static_cast<std::size_t>(inHand)].ammoMax)
        theirSpends.push_back(0);
    for (std::size_t k = 1; k < recording.weapon.size(); ++k) {
        const auto& was = recording.weapon[k - 1];
        const auto& now = recording.weapon[k];
        if (was.weapon == now.weapon && now.ammo < was.ammo) theirSpends.push_back(static_cast<std::int64_t>(k));
    }

    Errors e;
    const auto outOfRange = [&e](const std::string& at) {
        if (e.outOfRange++ == 0) e.outOfRangeAt = at;
    };
    std::vector<bool> claimed(theirOrder.size(), false);

    // Our replay.
    std::vector<OurShot> ours;
    std::vector<int> live; // per game.shots, an index into `ours`
    std::vector<Hit> ourHits;
    std::vector<std::int64_t> ourSpends;
    for (std::size_t k = 0; k + 1 < rows.size(); ++k) {
        const auto& row = rows[k];
        const auto& w = recording.weapon[k];

        auto place = [](ugame::Body& body, Vec3 at, const std::array<double, 3>& v, const std::string& physics,
                        double hover) {
            at.z -= hover;
            body.move.location = at;
            body.move.velocity = vec(v);
            body.move.physics = physicsOf(physics);
        };
        // The first hit at this tick on each body holds its state before.
        std::array<const uta::test::RecordedDamage*, 2> firstHit{};
        for (const auto& d : recording.damage)
            if (d.tick == static_cast<std::int64_t>(k)) {
                auto& slot = firstHit[d.victim.starts_with("UTARecTarget") ? TARGET : FIRER];
                if (slot == nullptr) slot = &d;
            }
        const auto* firerHit = firstHit[FIRER];
        bool afterPhysics = false; // the target is placed as a D line found it: a shot's is after that frame's physics
        place(game.bodies[FIRER], vec(firerHit ? firerHit->location : row.location),
              firerHit ? firerHit->velocity0 : row.velocity,
              firerHit ? firerHit->physics0 : row.physics, firerHover);
        if (targeted) {
            const auto& g = recording.target[k];
            // A recorded hit ours has yet to land (a refire a tick late)
            // holds the target as it was before the hit one tick more.
            const auto* hit = firstHit[TARGET];
            if (hit == nullptr && k > 0) {
                const auto last = static_cast<std::int64_t>(k) - 1;
                const auto late = std::ranges::find_if(recording.damage, [last](const auto& d) {
                    return d.tick == last && d.victim.starts_with("UTARecTarget");
                });
                const bool landed = std::ranges::any_of(
                    ourHits, [last](const Hit& h) { return h.victim == TARGET && h.tick >= last - 1; });
                if (late != recording.damage.end() && !landed) hit = &*late;
            }
            // A shot's hit is logged a row after the tick ours lands in. Once
            // ours has landed as many ticks of shots' hits as are logged by
            // this row, its D line is spent: the target is as its own row
            // has it, hit and yet to move.
            const auto now = static_cast<std::int64_t>(k);
            std::set<std::int64_t> logged, landed;
            for (const auto& d : recording.damage)
                if (d.tick <= now && d.victim.starts_with("UTARecTarget")) logged.insert(d.tick);
            for (const Hit& h : ourHits)
                if (h.victim == TARGET && !h.traced) landed.insert(h.tick);
            if (hit != nullptr && hit->tick == now && landed.size() >= logged.size()) hit = nullptr;
            afterPhysics = hit != nullptr;
            place(game.bodies[TARGET], vec(hit ? hit->location : g.location), hit ? hit->velocity0 : g.velocity,
                  hit ? hit->physics0 : g.physics, targetHover);
        }

        // The original's view shakes as it fires, and a shot starts from
        // the shaken eye; ours holds still, as a server's does. A press
        // fires before its frame moves the eye, at this row's height; the
        // weapon's own tick fires after, at the next row's.
        const ugame::Body& hand = game.bodies[FIRER];
        const bool atRest = hand.refireLeft <= 0 && hand.winding < 0 && hand.charging < 0;
        game.bodies[FIRER].eyeHeight = recording.weapon[atRest ? k : k + 1].eyeHeight;

        std::array<uworld::MoveCommand, 2> commands{};
        auto& c = commands[FIRER];
        c.forward = row.forward / 6000;
        c.strafe = row.strafe / 6000;
        c.yaw = w.yaw;
        c.pitch = w.pitch;
        c.jump = row.keys.find('J') != std::string::npos;
        c.duck = row.keys.find('D') != std::string::npos;
        c.fire = row.keys.find('P') != std::string::npos;
        c.altFire = row.keys.find('A') != std::string::npos;
        if (other >= 0 && row.keys.find('N') != std::string::npos)
            c.weaponSlot = static_cast<std::int8_t>(set.weapons[static_cast<std::size_t>(other)].slot);
        commands[TARGET].yaw = 0;

        const std::vector<ugame::Carried> carriedBefore = game.bodies[FIRER].weapons;
        const std::array<double, 2> healthBefore{game.bodies[0].health, game.bodies[1].health};
        const ugame::Game gameBefore = game;
        const std::vector<ugame::Shot>& shotsBefore = gameBefore.shots;

        ugame::tick(world, set, rules, game, commands, row.dt);
        const auto tick = static_cast<std::int64_t>(k);
        if (detail)
            for (const ugame::Event& event : game.events)
                if (event.kind == ugame::Event::Kind::Explosion || event.kind == ugame::Event::Kind::Combo)
                    std::cout << "  explosion " << tick << " at " << str(event.from) << " radius " << event.radius << "\n";
                else if (event.kind == ugame::Event::Kind::Melee)
                    std::cout << "  melee " << tick << " from " << str(event.from) << " to " << str(event.to) << "\n";

        // A weapon may fire in the tick it comes up: any weapon's ammo counts.
        for (std::size_t i = 0; i < carriedBefore.size(); ++i)
            if (game.bodies[FIRER].weapons[i].ammo < carriedBefore[i].ammo) {
                ourSpends.push_back(tick);
                break;
            }
        for (int b = 0; b < 2; ++b) {
            const double lost = healthBefore[static_cast<std::size_t>(b)] - game.bodies[static_cast<std::size_t>(b)].health;
            if (lost <= 0) continue;
            // The weapon's own trace hurts the target before that frame
            // moves it (the weapon ticks first), ours after: a target falling
            // through the tick has gained its gravity. A shot ticks after
            // both bodies, and a firer before its weapon, as ours do; but a
            // target placed as a shot found it has fallen that tick already.
            const auto at = static_cast<std::size_t>(b);
            Vec3 velocity = game.bodies[at].move.velocity;
            const bool traced = std::ranges::any_of(game.events, [](const ugame::Event& event) {
                return event.kind == ugame::Event::Kind::Melee || event.kind == ugame::Event::Kind::InstantHit ||
                       event.kind == ugame::Event::Kind::Beam;
            });
            if (b == TARGET && (traced || afterPhysics) && gameBefore.bodies[at].move.physics == uworld::Physics::Falling &&
                game.bodies[at].move.physics == uworld::Physics::Falling)
                velocity.z -= rules.gravityZ * row.dt;
            ourHits.push_back({tick, b, lost, velocity, traced});
        }

        // Follow our shots: survivors keep their order, the new ones come
        // last, having flown this one tick.
        // The survivors keep their order. Where some are gone, a survivor
        // is, of the shots it could be, the one that would have carried on
        // nearest to it: shots of one kind and launch are alike but for that.
        std::vector<int> next;
        std::vector<std::size_t> was; // per game.shots, its index before; a new one's is unused
        std::size_t from = 0;
        std::size_t gone = shotsBefore.size();
        for (const ugame::Shot& shot : game.shots)
            if (shot.age != row.dt || shot.stuck) --gone;
        for (ugame::Shot& shot : game.shots) {
            const bool fresh = shot.age == row.dt && !shot.stuck;
            int id = -1;
            if (!fresh) {
                std::size_t nearest = shotsBefore.size();
                double least = 0;
                for (std::size_t j = from; j < shotsBefore.size() && j <= from + gone; ++j) {
                    const ugame::Shot& was = shotsBefore[j];
                    if (was.kind != shot.kind || was.lifetime != shot.lifetime || was.damageScale != shot.damageScale)
                        continue;
                    const double off = uworld::length(was.location + was.velocity * row.dt - shot.location);
                    if (nearest == shotsBefore.size() || off < least) nearest = j, least = off;
                }
                REQUIRE(nearest < shotsBefore.size());
                gone -= nearest - from;
                from = nearest + 1;
                id = live[nearest];
            } else {
                id = static_cast<int>(ours.size());
                ours.push_back(OurShot{shot.kind, tick, {}, -1});
            }
            next.push_back(id);
            was.push_back(fresh ? 0 : from - 1);
        }
        live = std::move(next);

        // The recording logs a shot before it moves in a frame, so its row
        // k + 1 is ours after tick k, and its first row is the launch. Ours
        // has flown once since its launch, which is backed out of it here.
        const auto launched = [&](const ugame::Shot& shot) {
            const ugame::Projectile& kind = set.projectiles[shot.kind];
            ugame::Shot was = shot;
            const Vec3 moving = shot.velocity - Vec3{0, 0, rules.gravityZ * kind.gravityAfter} * row.dt;
            was.location = shot.location - moving * row.dt;
            was.velocity = moving - (Vec3{0, 0, rules.gravityZ * kind.gravity} + shot.acceleration) * row.dt;
            return was;
        };
        // Each new shot of ours to the first unclaimed recorded one of its
        // kind starting within a tick: both launch in the same order. A
        // random launch (SS 3 item 6) in its range takes the recorded first
        // move, so its flight can be compared.
        std::map<std::uint32_t, int> freshOfKind;
        for (std::size_t i = 0; i < game.shots.size(); ++i)
            if (ours[static_cast<std::size_t>(live[i])].first == tick) ++freshOfKind[game.shots[i].kind];
        for (std::size_t i = 0; i < game.shots.size(); ++i) {
            OurShot& s = ours[static_cast<std::size_t>(live[i])];
            ugame::Shot& shot = game.shots[i];
            if (s.first == tick && s.recorded < 0) {
                ugame::Shot launch = launched(shot);
                const ugame::Projectile& kind = set.projectiles[shot.kind];
                int best = -1;
                for (std::size_t r = 0; r < theirOrder.size(); ++r) {
                    const RecordedShot& t = theirs.at(theirOrder[r]);
                    if (claimed[r] || t.kind != kind.name || ticksFrom(tick, t.first) > MAX_TICK_ERROR) continue;
                    best = static_cast<int>(r);
                    break;
                }
                if (best >= 0) {
                    claimed[static_cast<std::size_t>(best)] = true;
                    s.recorded = best;
                    const RecordedShot& t = theirs.at(theirOrder[static_cast<std::size_t>(best)]);
                    const auto& first = t.rows.front().second;
                    const LaunchRange range = rangeOf(*mode, kind, freshOfKind[shot.kind] > 1);
                    if (range.random()) {
                        const std::string at = theirOrder[static_cast<std::size_t>(best)] + " launch";
                        // A shot first logged a row late has flown once (below):
                        // its velocity is judged against ours of that age.
                        Vec3 logged = vec(first.location);
                        logged.z -= firerHover;
                        const bool flown =
                            uworld::length(logged - shot.location) < uworld::length(logged - launch.location);
                        if (!launchInRange(kind, range, vec(first.velocity), (flown ? shot : launch).velocity))
                            outOfRange(at);
                        // A start drawn behind ours takes the recorded one. One
                        // that has flown may also be off by what its launch drew,
                        // flown a tick.
                        if (range.back > 0 && t.first == tick + (flown ? 1 : 0)) {
                            Vec3& start = flown ? shot.location : launch.location;
                            const double turn = range.turn * 2 * std::numbers::pi / 65536;
                            const double drawn = flown ? (range.speed + kind.speed * turn) * row.dt : 0;
                            if (uworld::length(start - logged) > range.back + drawn + MAX_POSITION_ERROR) {
                                outOfRange(at);
                                if (detail)
                                    std::cout << "  " << at << ": starts " << str(start) << " | " << str(logged) << "\n";
                            }
                            start = logged;
                        }
                        // Its row of our shot's age: the next, or the one
                        // as many rows on as its launch is after ours.
                        const std::int64_t aged = t.first + (flown ? 0 : 1);
                        const auto moved = std::ranges::find(t.rows, aged, [](const auto& r) { return r.first; });
                        if (moved != t.rows.end()) {
                            Vec3 want = vec(moved->second.location);
                            want.z -= firerHover;
                            shot.location = want;
                            shot.velocity = vec(moved->second.velocity);
                        }
                    }
                }
                s.rows[tick] = launch;
                s.rows[tick + 1] = shot;
                // Only a shot the press itself fires is logged at its launch.
                // One the weapon's own tick fires -- a refire, a released
                // charge, a split -- comes after that frame's row, and is
                // first logged a row later, having flown once: nearer our
                // first flown row than our launch.
                if (best >= 0) {
                    const RecordedShot& t = theirs.at(theirOrder[static_cast<std::size_t>(best)]);
                    Vec3 logged = vec(t.rows.front().second.location);
                    logged.z -= firerHover;
                    const bool flown = uworld::length(logged - shot.location) < uworld::length(logged - launch.location);
                    s.late = t.first - (flown ? 1 : 0) - tick;
                }
            } else if (s.recorded >= 0) {
                const std::string& name = theirOrder[static_cast<std::size_t>(s.recorded)];
                const RecordedShot& t = theirs.at(name);
                const ugame::Projectile& kind = set.projectiles[shot.kind];
                const auto rowAt = [&t](std::int64_t at) {
                    return std::ranges::find(t.rows, at, [](const auto& r) { return r.first; });
                };
                const auto before = rowAt(tick + s.late), after = rowAt(tick + 1 + s.late);
                ugame::Shot start = gameBefore.shots[was[i]];
                // The tick again with this shot alone, keeping `off` of its
                // speed off a surface, over the recorded shot's tick of this
                // age: a launch a tick from ours flew each move over another
                // row's time.
                const auto aged = std::clamp<std::int64_t>(tick + s.late, 0, static_cast<std::int64_t>(rows.size()) - 1);
                const double flownFor = rows[static_cast<std::size_t>(aged)].dt;
                const auto with = [&](double off) {
                    ugame::WeaponSet probe = set;
                    probe.projectiles[shot.kind].bounceOff = off;
                    probe.projectiles[shot.kind].bounceOffRandom = 0;
                    ugame::Game alone = gameBefore;
                    alone.shots = {start};
                    for (ugame::Body& body : alone.bodies) body.weapon = -1;
                    ugame::tick(world, probe, rules, alone, std::array<uworld::MoveCommand, 2>{}, flownFor);
                    return alone.shots;
                };
                if (s.late != 0 && !start.stuck) {
                    const auto own = with(kind.bounceOff);
                    if (own.size() == 1) shot = own[0];
                }
                // Its first bounce, ours or the recorded one's: a kind that
                // flies straight till then turns at nothing else.
                const bool turned = before != t.rows.end() && after != t.rows.end() && kind.gravity == 0 &&
                                    uworld::length(vec(after->second.velocity) - vec(before->second.velocity)) > 0.01;
                const bool lands = kind.strike == ugame::Strike::Stick && shot.stuck && !start.stuck;
                const bool raise = !s.raised && (lands || (kind.strike == ugame::Strike::Bounce &&
                                                           kind.splashRadius == 0 && (shot.bounces > s.bounces || turned)));
                if (raise) {
                    s.raised = true;
                    start.location.z += firerHover;
                }
                const auto all = raise || shot.bounces > s.bounces ? with(1) : std::vector<ugame::Shot>{};
                if (raise && all.size() == 1 && all[0].bounces == start.bounces) {
                    // Raised, it has yet to bounce; or it sticks from the
                    // recorded height, this tick or a later one.
                    shot = all[0];
                } else if (after != t.rows.end() && (raise || shot.bounces > s.bounces)) {
                    // A bounce draws how much speed it keeps off the surface
                    // (SS 3 item 6), so ours is flown again with the recorded
                    // draw. Keeping all its speed off the surface and then
                    // half gives what the draw scales; the recorded velocity
                    // then gives the draw. That draw is held to our rule's
                    // range, the tick flown with it to the recorded row, and
                    // the flight goes on from that row. Not compared: a
                    // settling shot's bounce, a tick the recorded shot
                    // bounced in more than once, each bounce with a draw of
                    // its own, and a bounce of a shot not raised.
                    Vec3 at = vec(after->second.location);
                    if (!s.raised) at.z -= firerHover;
                    // A shot kept low is under the floor the recorded one
                    // bounced off; left there, it would start its next tick
                    // in solid and strike at once. It is flown at the
                    // recorded height from here on, as a raised kind is.
                    // This bounce was ours while low, so it is not compared.
                    const bool buried = !s.raised && world.trace(at, at).startsInside;
                    if (buried) {
                        s.raised = true;
                        at.z += firerHover;
                    }
                    const Vec3 velocity = vec(after->second.velocity);
                    const std::string where = name + " bounce at tick " + std::to_string(tick);
                    const auto half = with(0.5);
                    const auto alongOf = [](const Vec3& v, const Vec3& off) {
                        const double size = uworld::dot(off, off);
                        return uworld::length(size > 0 ? v - off * (uworld::dot(v, off) / size) : v);
                    };
                    const bool both = all.size() == 1 && half.size() == 1;
                    const Vec3 scaled = both ? (all[0].velocity - half[0].velocity) * 2 : Vec3{};
                    const double size = uworld::dot(scaled, scaled);
                    const bool once = both && size > 0 &&
                                      alongOf(velocity, scaled) > alongOf(all[0].velocity, scaled) * (1 + kind.bounceKeep) / 2;
                    if (s.raised && !buried && once && uworld::length(start.velocity) >= SETTLING) {
                        const double draw = 1 - uworld::dot(all[0].velocity - velocity, scaled) / size;
                        // A unit a second of the speed the draw scales.
                        const double slack = MAX_VELOCITY_ERROR / std::sqrt(size);
                        if (draw < kind.bounceOff - kind.bounceOffRandom - slack ||
                            draw > kind.bounceOff + kind.bounceOffRandom + slack)
                            outOfRange(where);
                        const auto drawn = with(std::clamp(draw, 0.0, 1.0));
                        const double off = drawn.size() == 1 ? uworld::length(drawn[0].location - at) : 0;
                        if (off > e.bounce) e.bounce = off, e.bounceAt = where;
                        if (detail && off > MAX_POSITION_ERROR)
                            std::cout << "  " << where << ": draw " << draw << " of " << std::sqrt(size) << ", "
                                      << (drawn.size() == 1 ? str(drawn[0].location) + " v " + str(drawn[0].velocity) : "gone")
                                      << " | " << str(at) << " v " << str(velocity) << " off " << off << "\n";
                    }
                    shot.location = at;
                    shot.velocity = velocity;
                }
            }
            s.bounces = shot.bounces;
            s.rows[tick + 1] = shot;
            if (s.raised) s.rows[tick + 1].location.z -= firerHover;
        }
    }

    if (detail) {
        for (const auto t : ourSpends) std::cout << "  our spend " << t << "\n";
        for (const auto t : theirSpends) std::cout << "  their spend " << t << "\n";
        for (const Hit& h : ourHits)
            std::cout << "  our hit " << h.tick << " on " << h.victim << ": " << h.damage << " v " << str(h.velocity) << "\n";
        for (const Hit& h : theirHits)
            std::cout << "  their hit " << h.tick << " on " << h.victim << ": " << h.damage << " v " << str(h.velocity) << "\n";
        for (const OurShot& s : ours) {
            std::cout << "  our shot " << set.projectiles[s.kind].name << " from " << s.first << " matched "
                      << (s.recorded >= 0 ? theirOrder[static_cast<std::size_t>(s.recorded)] : "-") << "\n";
            for (const auto& [tick, shot] : s.rows) {
                std::cout << "    " << tick << " " << str(shot.location) << " v " << str(shot.velocity);
                if (s.recorded >= 0)
                    for (const auto& [t, p] : theirs.at(theirOrder[static_cast<std::size_t>(s.recorded)]).rows)
                        if (t == tick)
                            std::cout << " | " << str(vec(p.location) - Vec3{0, 0, firerHover}) << " v "
                                      << str(vec(p.velocity));
                std::cout << "\n";
            }
        }
    }
    e.ourSpends = ourSpends.size();
    e.theirSpends = theirSpends.size();
    if (ourSpends.size() == theirSpends.size()) e.spendTick = worstTick(ourSpends, theirSpends);
    e.ourShots = ours.size();
    e.theirShots = theirOrder.size();
    e.ourHits = ourHits.size();
    e.theirHits = theirHits.size();

    // A mode drawing its spread, damage or push (SS 3 item 6) hits at
    // random: each recorded hit lands within a tick of one of our shots,
    // with damage and momentum our rule could draw.
    e.randomHits = mode->archetype == ugame::Archetype::InstantHit &&
                   (mode->spread > 0 || mode->damageRandom > 0 || mode->pushChance > 0);
    if (e.randomHits) {
        const double least = std::trunc(std::trunc(mode->damage) * set.damageScale);
        const double most = std::trunc(std::trunc(mode->damage + mode->damageRandom) * set.damageScale);
        for (const Hit& h : theirHits) {
            std::int64_t nearest = 1000000;
            for (const std::int64_t spend : ourSpends) nearest = std::min(nearest, ticksFrom(spend, h.tick));
            e.hitTick = std::max(e.hitTick, nearest);
            if (h.damage < least || h.damage > most) outOfRange("damage at tick " + std::to_string(h.tick));
        }
        for (const auto& d : recording.damage) {
            const double m = uworld::length(vec(d.momentum));
            const double off = std::min(std::abs(m - mode->momentum), std::abs(m - mode->momentum * mode->pushScale));
            if (off > 1) outOfRange("momentum at tick " + std::to_string(d.tick));
        }
        return e;
    }

    // Hits, in order: tick, damage, the victim's velocity after. One blast
    // hurts its bodies in no order the recording keeps: within a tick, by victim.
    const auto byTickThenVictim = [](const Hit& a, const Hit& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.victim < b.victim;
    };
    std::ranges::stable_sort(ourHits, byTickThenVictim);
    std::ranges::stable_sort(theirHits, byTickThenVictim);
    for (std::size_t i = 0; i < std::min(ourHits.size(), theirHits.size()); ++i) {
        const Hit& a = ourHits[i];
        const Hit& b = theirHits[i];
        const std::string at = "tick " + std::to_string(b.tick);
        e.hitTick = std::max(e.hitTick, ticksFrom(a.tick, b.tick));
        if (std::abs(a.damage - b.damage) > e.damage) e.damage = std::abs(a.damage - b.damage), e.damageAt = at;
        const double v = uworld::length(a.velocity - b.velocity);
        if (v > e.velocity) e.velocity = v, e.velocityAt = at;
    }

    // Shots: each matched pair's start and flight.
    for (const OurShot& s : ours) {
        if (s.recorded < 0) continue;
        const std::string& name = theirOrder[static_cast<std::size_t>(s.recorded)];
        const RecordedShot& t = theirs.at(name);
        e.shotTick = std::max(e.shotTick, std::abs(s.late));
        // A launch within INV-6's tick of ours is judged above; its flight
        // is then compared age for age, not row for row.
        const std::int64_t late = s.late;
        bool first = true;
        for (const auto& [tick, p] : t.rows) {
            const auto row = s.rows.find(tick - late);
            if (row == s.rows.end()) continue;
            Vec3 want = vec(p.location);
            want.z -= firerHover;
            const double d = uworld::length(row->second.location - want);
            const std::string at = name + " tick " + std::to_string(tick);
            if (first && d > e.spawn) e.spawn = d, e.spawnAt = at;
            if (d > e.flight) e.flight = d, e.flightAt = at;
            first = false;
        }
    }
    e.unmatched = static_cast<std::size_t>(std::ranges::count(claimed, false));
    return e;
}

} // namespace

TEST_CASE("UTA-0019 INV-6: the stock set matches the original's weapon runs on DM-Deck16][", "[real][weapons]") {
    const char* recordings = std::getenv("UTA_WEAPON_RECORDINGS");
    INFO("UTA_WEAPON_RECORDINGS names a ut-record weapon run's directory");
    REQUIRE(recordings != nullptr);
    const fs::path directory(recordings);

    const auto set = ugame::load(ugame::shippedDirectory() / "stock.weapons");
    REQUIRE(set.has_value());
    uta::ubundle::Collision collision;
    collision.level = uta::test::real::levelTreeOf(fs::path(UTA_UT_INSTALL_DIR) / "Maps" / "DM-Deck16][.unr");
    auto built = uworld::BodyWorld::build(collision, {});
    REQUIRE(built.has_value());

    const auto runs = runsOf(directory);
    REQUIRE(!runs.empty());
    for (const Run& r : runs) {
        std::ifstream file(directory / (r.name + "-" + r.rate + ".utarec"));
        INFO(r.name << " at " << r.rate << " fps");
        REQUIRE(file.good());
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto recording = uta::test::readMovementRecording(text);
        REQUIRE(recording.has_value());
        const Errors e = replay(*built, *set, *recording);
        std::cout << "UTA-0019 " << r.name << " " << r.rate << (r.timed ? "" : " (missed)") << ": spends "
                  << e.ourSpends << "/" << e.theirSpends << " off " << e.spendTick << ", shots " << e.ourShots << "/"
                  << e.theirShots << " off " << e.shotTick << " unmatched " << e.unmatched << ", hits " << e.ourHits
                  << "/" << e.theirHits << " off " << e.hitTick << ", damage " << e.damage << " (" << e.damageAt
                  << "), velocity " << e.velocity << " (" << e.velocityAt << "), spawn " << e.spawn << " ("
                  << e.spawnAt << "), flight " << e.flight << " (" << e.flightAt << "), bounce " << e.bounce << " ("
                  << e.bounceAt << ")"
                  << (e.randomHits ? ", hits drawn" : "") << ", out of range " << e.outOfRange << " ("
                  << e.outOfRangeAt << ")\n";
        CHECK(e.outOfRange == 0);
        if (!e.randomHits) CHECK(e.ourHits == e.theirHits);
        CHECK(e.ourShots == e.theirShots);
        CHECK(e.unmatched == 0);
        CHECK(e.damage == 0);
        CHECK(e.velocity <= MAX_VELOCITY_ERROR);
        CHECK(e.spawn <= MAX_POSITION_ERROR);
        CHECK(e.flight <= MAX_POSITION_ERROR);
        CHECK(e.bounce <= MAX_POSITION_ERROR);
        if (r.timed) {
            CHECK(e.ourSpends == e.theirSpends);
            CHECK(e.spendTick <= MAX_TICK_ERROR);
            CHECK(e.shotTick <= MAX_TICK_ERROR);
            CHECK(e.hitTick <= MAX_TICK_ERROR);
        }
    }
}

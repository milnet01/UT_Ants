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
// before, so a hit after row k's log reads k + 1: within INV-6's tick.
//
// A ROW IS LOGGED AFTER ITS TICK'S HITS. A body a D line at tick k hit
// starts tick k from that line's velocity and physics from before the hit,
// not from row k's, which already carry the push.
//
// THE ORIGINAL HOVERS, OURS DOES NOT (UTA-0017 SS 3 item 4). Each body's
// height above our floor at row 0 is taken off its recorded heights, and the
// firer's off its shots'.
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

/// One recorded shot: its rows, by our tick (row - 1).
struct RecordedShot {
    std::string kind;
    std::int64_t first = 0;
    std::vector<std::pair<std::int64_t, uta::test::RecordedProjectile>> rows;
};

/// One of our shots, followed from tick to tick.
struct OurShot {
    std::uint32_t kind = 0;
    std::int64_t first = 0;
    std::map<std::int64_t, ugame::Shot> rows; ///< after our tick k
    int recorded = -1;                        ///< the recorded shot it was matched to
};

/// A hit as both sides see it: damage summed over a tick, and the victim's
/// velocity after it.
struct Hit {
    std::int64_t tick = 0;
    int victim = 0;
    double damage = 0;
    Vec3 velocity{};
};

/// The worst of each comparison over a run.
struct Errors {
    std::size_t ourShots = 0, theirShots = 0, ourHits = 0, theirHits = 0, ourSpends = 0, theirSpends = 0;
    std::int64_t shotTick = 0, hitTick = 0, spendTick = 0;
    double damage = 0, velocity = 0, spawn = 0, flight = 0;
    std::string damageAt, velocityAt, spawnAt, flightAt;
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
    if (several && mode.rule != ugame::FireRule::None) {
        range.back = 4;
        if (mode.rule == ugame::FireRule::RocketFan) range.speed += 0.1 * kind.speed;
        if (mode.rule == ugame::FireRule::GrenadeCluster) range.turn += 2 * std::sqrt(3.0) * 750;
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

/// The worst distance between two sorted tick lists of one length.
std::int64_t worstTick(const std::vector<std::int64_t>& a, const std::vector<std::int64_t>& b) {
    std::int64_t worst = 0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) worst = std::max(worst, std::abs(a[i] - b[i]));
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

Errors replay(const uworld::BodyWorld& world, const ugame::WeaponSet& set,
              const uta::test::MovementRecording& recording) {
    const bool detail = detailed(recording);
    const auto& rows = recording.ticks;
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

    // The recorded shots. One alive at row 0 and all but still is the level's
// own, left by an earlier run.
    std::map<std::string, RecordedShot> theirs;
    std::vector<std::string> theirOrder;
    std::vector<std::string> before;
    for (const auto& p : recording.projectiles) {
        if (p.tick == 0 && uworld::length(vec(p.velocity)) < STILL) before.push_back(p.name);
        if (std::ranges::find(before, p.name) != before.end()) continue;
        const std::string kind = kindOf(p.cls);
        if (kind.empty()) continue;
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
        place(game.bodies[FIRER], vec(row.location), firerHit ? firerHit->velocity0 : row.velocity,
              firerHit ? firerHit->physics0 : row.physics, firerHover);
        if (targeted) {
            const auto& g = recording.target[k];
            const auto* hit = firstHit[TARGET];
            place(game.bodies[TARGET], vec(g.location), hit ? hit->velocity0 : g.velocity,
                  hit ? hit->physics0 : g.physics, targetHover);
        }

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

        const int heldBefore = game.bodies[FIRER].weapon;
        const std::uint32_t ammoBefore =
            heldBefore >= 0 ? game.bodies[FIRER].weapons[static_cast<std::size_t>(heldBefore)].ammo : 0;
        const std::array<double, 2> healthBefore{game.bodies[0].health, game.bodies[1].health};
        const std::vector<ugame::Shot> shotsBefore = game.shots;

        ugame::tick(world, set, rules, game, commands, row.dt);
        const auto tick = static_cast<std::int64_t>(k);
        if (detail)
            for (const ugame::Event& event : game.events)
                if (event.kind == ugame::Event::Kind::Explosion || event.kind == ugame::Event::Kind::Combo)
                    std::cout << "  explosion " << tick << " at " << str(event.from) << " radius " << event.radius << "\n";

        if (heldBefore >= 0 && game.bodies[FIRER].weapon == heldBefore &&
            game.bodies[FIRER].weapons[static_cast<std::size_t>(heldBefore)].ammo < ammoBefore)
            ourSpends.push_back(tick);
        for (int b = 0; b < 2; ++b) {
            const double lost = healthBefore[static_cast<std::size_t>(b)] - game.bodies[static_cast<std::size_t>(b)].health;
            if (lost > 0) ourHits.push_back({tick, b, lost, game.bodies[static_cast<std::size_t>(b)].move.velocity});
        }

        // Follow our shots: survivors keep their order, the new ones come last.
        std::vector<int> next;
        std::size_t from = 0;
        for (const ugame::Shot& shot : game.shots) {
            const bool fresh = shot.age == 0 && !shot.stuck;
            int id = -1;
            if (!fresh) {
                for (std::size_t j = from; j < shotsBefore.size(); ++j) {
                    const ugame::Shot& was = shotsBefore[j];
                    if (was.kind == shot.kind && was.lifetime == shot.lifetime &&
                        was.damageScale == shot.damageScale) {
                        id = live[j];
                        from = j + 1;
                        break;
                    }
                }
                REQUIRE(id >= 0);
            } else {
                id = static_cast<int>(ours.size());
                ours.push_back(OurShot{shot.kind, tick, {}, -1});
            }
            next.push_back(id);
        }
        live = std::move(next);

        // Each new shot of ours to the nearest unclaimed recorded one of its
        // kind starting within a tick. A random launch (SS 3 item 6) in its
        // range takes the recorded velocity, so its flight can be compared.
        std::map<std::uint32_t, int> freshOfKind;
        for (std::size_t i = 0; i < game.shots.size(); ++i)
            if (ours[static_cast<std::size_t>(live[i])].first == tick) ++freshOfKind[game.shots[i].kind];
        for (std::size_t i = 0; i < game.shots.size(); ++i) {
            OurShot& s = ours[static_cast<std::size_t>(live[i])];
            ugame::Shot& shot = game.shots[i];
            if (s.first == tick && s.recorded < 0) {
                const ugame::Projectile& kind = set.projectiles[shot.kind];
                int best = -1;
                double bestDistance = 1e300;
                for (std::size_t r = 0; r < theirOrder.size(); ++r) {
                    const RecordedShot& t = theirs.at(theirOrder[r]);
                    if (claimed[r] || t.kind != kind.name || std::abs(t.first - tick) > MAX_TICK_ERROR) continue;
                    Vec3 want = vec(t.rows.front().second.location);
                    want.z -= firerHover;
                    const double d = uworld::length(shot.location - want);
                    if (d < bestDistance) best = static_cast<int>(r), bestDistance = d;
                }
                if (best >= 0) {
                    claimed[static_cast<std::size_t>(best)] = true;
                    s.recorded = best;
                    const RecordedShot& t = theirs.at(theirOrder[static_cast<std::size_t>(best)]);
                    const auto& first = t.rows.front().second;
                    const LaunchRange range = rangeOf(*mode, kind, freshOfKind[shot.kind] > 1);
                    if (range.random()) {
                        const std::string at = theirOrder[static_cast<std::size_t>(best)] + " launch";
                        if (!launchInRange(kind, range, vec(first.velocity), shot.velocity)) outOfRange(at);
                        shot.velocity = vec(first.velocity);
                        // A start drawn behind ours takes the recorded one,
                        // where the recording has it unmoved.
                        if (range.back > 0 && t.first == tick) {
                            if (bestDistance > range.back + MAX_POSITION_ERROR) outOfRange(at);
                            Vec3 want = vec(first.location);
                            want.z -= firerHover;
                            shot.location = want;
                        }
                    }
                }
            }
            s.rows[tick] = shot;
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
            for (const std::int64_t spend : ourSpends) nearest = std::min(nearest, std::abs(spend - h.tick));
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

    // Hits, in order: tick, damage, the victim's velocity after.
    for (std::size_t i = 0; i < std::min(ourHits.size(), theirHits.size()); ++i) {
        const Hit& a = ourHits[i];
        const Hit& b = theirHits[i];
        const std::string at = "tick " + std::to_string(b.tick);
        e.hitTick = std::max(e.hitTick, std::abs(a.tick - b.tick));
        if (std::abs(a.damage - b.damage) > e.damage) e.damage = std::abs(a.damage - b.damage), e.damageAt = at;
        const double v = uworld::length(a.velocity - b.velocity);
        if (v > e.velocity) e.velocity = v, e.velocityAt = at;
    }

    // Shots: each matched pair's start and flight.
    for (const OurShot& s : ours) {
        if (s.recorded < 0) continue;
        const std::string& name = theirOrder[static_cast<std::size_t>(s.recorded)];
        const RecordedShot& t = theirs.at(name);
        e.shotTick = std::max(e.shotTick, std::abs(s.first - t.first));
        bool first = true;
        for (const auto& [tick, p] : t.rows) {
            const auto row = s.rows.find(tick);
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
                  << e.spawnAt << "), flight " << e.flight << " (" << e.flightAt << ")"
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
        if (r.timed) {
            CHECK(e.ourSpends == e.theirSpends);
            CHECK(e.spendTick <= MAX_TICK_ERROR);
            CHECK(e.shotTick <= MAX_TICK_ERROR);
            CHECK(e.hitTick <= MAX_TICK_ERROR);
        }
    }
}

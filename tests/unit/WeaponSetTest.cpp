// Locks docs/specs/UTA-0019-weapon-set.md INV-1 (a set file is strict),
// INV-2 (write reads back) and INV-3 (the shipped stock set parses and names
// each stock class once).

#include "ugame/WeaponSet.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

namespace {

/// A small set using every archetype, strike, charge and optional key.
constexpr std::string_view SMALL = R"(ut-ants weapons 1
[set]
name = "Small # set"
self-damage = 0.5
head-height = 0.62
damage-scale = 1.5

[weapon gun]
class = Botpack.ShockRifle
slot = 4
ammo-max = 50
ammo-start = 20
switch-time = 0.3

[fire gun primary]
archetype = instant-hit
refire = 0.7
ammo = 1
spread = 100
offset = 4.4 6.7 -9.6
damage = 40
momentum = 60000
range = 10000
pellets = 3
head-damage = 100
head-momentum = 35000
damage-random = 5
push-chance = 0.2
push-scale = 5

[fire gun alt]
archetype = projectile
refire = 0.6
ammo = 2
charge-time = 4
charge-min = 0.25
charge-max = 1
charge-step = 0.5
charge-step-last = 0.4
charge-scales = count
rule = rocket-fan
projectile = ball
count = 4
spiral-radius = 4
spiral-turn = 18775

[weapon hammer]
class = botpack.impacthammer
slot = 1
ammo-max = 0
ammo-start = 0
switch-time = 0

[fire hammer primary]
archetype = melee
refire = 0.75
ammo = 0
charge-time = 1.5
charge-min = 0.2
charge-max = 1
charge-scales = damage
damage = 60
momentum = 66000
range = 60
self-momentum = 66000

[fire hammer alt]
archetype = beam
refire = 0.1
refire-lead = 0.05
wind-up = 0.25
ammo = 10
press-ammo = 2
repress = 0.8
spin-up = 0.5
spin-refire = 0.2
damage = 72
momentum = 8500
range = 1100
hit-every = 0.22

[projectile ball]
speed = 1000
radius = 12
lifetime = 10
acceleration = 50
max-speed = 1600
damage = 55
momentum = 70000
splash-radius = 70
splash-from = moved
body-hit = direct
splash-up-max = 0.45
splash-radius-max = 140
strike = explode
combo = boom

[projectile shell]
speed = 1200
radius = 0
gravity = 1
gravity-after = 0.25
lifetime = 6
damage = 70
momentum = 75000
strike = split
split-into = chunk
split-count = 5
split-spread = 6000

[projectile chunk]
speed = 2500
speed-random = 100
turn-random = 1000
radius = 0
lifetime = 2.9
lifetime-random = 0.5
damage = 16
momentum = 10000
strike = bounce
bounces = 2
bounce-keep = 0.6
bounce-off = 0.3
bounce-off-random = 0.2
bounce-gravity = 0.5

[projectile gel]
speed = 840
radius = 2
gravity = 1
lifetime = 12
lift = 120
inherit = 0.4
expire = explode
damage = 20
momentum = 20000
strike = stick
fuse = 3

[combo boom]
damage = 165
momentum = 120000
splash-radius = 250
)";

/// `SMALL` with `from` replaced by `to`, once; `from` must be in it.
std::string edited(std::string_view from, std::string_view to) {
    std::string text(SMALL);
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

/// The line `needle` starts on in `text`, counting from 1.
std::size_t lineOf(std::string_view text, std::string_view needle) {
    const std::size_t at = text.find(needle);
    REQUIRE(at != std::string_view::npos);
    return static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n')) + 1;
}

} // namespace

TEST_CASE("INV-2: the small set parses and reads back from write", "[ugame][weapons]") {
    const auto set = uta::ugame::parse(SMALL);
    REQUIRE(set.has_value());
    CHECK(set->name == "Small # set");
    REQUIRE(set->weapons.size() == 2);
    CHECK(set->weapons[0].className == "botpack.shockrifle");
    CHECK(set->weapons[0].primary.pellets == 3);
    CHECK(set->damageScale == 1.5);
    CHECK(set->weapons[0].primary.offset == std::array<double, 3>{4.4, 6.7, -9.6});
    CHECK(set->weapons[0].primary.headDamage == 100);
    CHECK(set->weapons[0].primary.headMomentum == 35000);
    CHECK(set->weapons[0].primary.damageRandom == 5);
    CHECK(set->weapons[0].primary.pushChance == 0.2);
    CHECK(set->weapons[1].alt->hitEvery == 0.22);
    CHECK(set->weapons[1].alt->refireLead == 0.05);
    CHECK(set->weapons[1].alt->windUp == 0.25);
    CHECK(set->weapons[1].alt->pressAmmo == 2);
    CHECK(set->weapons[1].alt->repress == 0.8);
    CHECK(set->weapons[1].alt->spinUp == 0.5);
    CHECK(set->weapons[1].alt->spinRefire == 0.2);
    CHECK(set->projectile("ball")->acceleration == 50);
    CHECK(set->projectile("chunk")->turnRandom == 1000);
    CHECK(set->projectile("chunk")->bounceOff == 0.3);
    CHECK(set->projectile("chunk")->bounceOffRandom == 0.2);
    CHECK(set->projectile("chunk")->bounceGravity == 0.5);
    CHECK(set->projectile("gel")->lift == 120);
    REQUIRE(set->weapons[0].alt);
    REQUIRE(set->weapons[0].alt->charge);
    CHECK(set->weapons[0].alt->charge->scales == uta::ugame::ChargeScales::Count);
    CHECK(set->weapons[0].alt->charge->step == 0.5);
    CHECK(set->weapons[0].alt->charge->stepLast == 0.4);
    CHECK(set->weapons[0].alt->spiralRadius == 4);
    CHECK(set->weapons[0].alt->spiralTurn == 18775);
    CHECK(set->weapons[0].primary.spiralRadius == 0);
    CHECK(set->weapons[0].alt->rule == uta::ugame::FireRule::RocketFan);
    CHECK(set->weapons[0].primary.rule == uta::ugame::FireRule::None);
    CHECK(set->weapons[1].alt->archetype == uta::ugame::Archetype::Beam);
    CHECK(set->projectile("gel")->expireExplodes);
    CHECK_FALSE(set->projectile("chunk")->expireExplodes);
    CHECK(set->projectile("shell")->splitInto == "chunk");
    CHECK(set->projectile("shell")->gravityAfter == 0.25);
    CHECK(set->combo("boom")->splashRadius == 250);
    CHECK(set->projectile("ball")->splashFromMoved);
    CHECK(set->projectile("ball")->bodyDirect);
    CHECK(set->projectile("ball")->splashUpMax == 0.45);
    CHECK_FALSE(set->projectile("gel")->splashFromMoved);
    CHECK_FALSE(set->projectile("gel")->bodyDirect);
    CHECK(set->projectile("gel")->splashUpMax == 1);
    CHECK(set->projectile("ball")->splashRadiusMax == 140);
    CHECK(set->projectile("gel")->splashRadiusMax == 0);

    const std::string written = uta::ugame::write(*set);
    const auto again = uta::ugame::parse(written);
    REQUIRE(again.has_value());
    CHECK(*again == *set);
    CHECK(uta::ugame::write(*again) == written);
}

TEST_CASE("INV-2: a decimal is written so it reads back exactly", "[ugame][weapons]") {
    auto set = uta::ugame::parse(SMALL).value();
    set.weapons[0].primary.refire = 1.0 / 3.0;
    set.projectiles[0].speed = 1234.5678901234567;
    const auto again = uta::ugame::parse(uta::ugame::write(set));
    REQUIRE(again.has_value());
    CHECK(*again == set);
}

TEST_CASE("INV-1: each refusal names its line", "[ugame][weapons]") {
    struct Refusal {
        std::string_view what;
        std::string text;
        std::string_view at; ///< the text the refused line starts with
        std::string_view says;
    };
    const std::vector<Refusal> refusals{
        {"an unknown section", edited("[combo boom]", "[bomb boom]"), "[bomb boom]", "there is no [bomb boom] section"},
        {"an unknown key", edited("pellets = 3", "pelets = 3"), "pelets", "has no key 'pelets'"},
        {"a repeated key", edited("pellets = 3", "pellets = 3\npellets = 4"), "pellets = 4", "given twice"},
        {"a repeated section", edited("[combo boom]", "[projectile gel]"), "[projectile gel]\ndamage = 165",
         "is given twice"},
        {"a repeated fire mode", edited("[weapon hammer]", "[fire gun alt]\n[weapon hammer]"),
         "[fire gun alt]\n[weapon hammer]", "is given twice"},
        {"a missing required key", edited("refire = 0.7\n", ""), "[fire gun primary]", "has no `refire`"},
        {"a missing [set] key", edited("head-height = 0.62\n", ""), "[set]", "has no `head-height`"},
        {"an out-of-range decimal", edited("bounce-keep = 0.6", "bounce-keep = 1.5"), "bounce-keep",
         "is not a number from 0 to 1"},
        {"an out-of-range number", edited("slot = 4", "slot = 10"), "slot = 10", "from 0 to 9"},
        {"a count of zero", edited("count = 4", "count = 0"), "count = 0", "count is from 1 to 64"},
        {"start ammo above the most", edited("ammo-start = 20", "ammo-start = 51"), "ammo-start",
         "from 0 to 50"},
        {"another archetype's key", edited("pellets = 3", "projectile = ball"), "projectile = ball\nhead-damage",
         "an instant-hit fire has no key 'projectile'"},
        {"another strike's key", edited("fuse = 3", "fuse = 3\nbounces = 2"), "bounces = 2\n\n[combo",
         "a projectile that strikes to stick has no key 'bounces'"},
        {"a charge on a beam", edited("ammo = 10", "ammo = 10\ncharge-time = 1"), "charge-time = 1\n",
         "a beam fire has no key 'charge-time'"},
        {"a part of a charge", edited("charge-min = 0.2\n", ""), "[fire hammer primary]", "has no `charge-min`"},
        {"a count charge on a melee", edited("charge-scales = damage", "charge-scales = count"),
         "charge-scales = count\ndamage", "only a projectile fire charges its count"},
        {"an unknown archetype", edited("archetype = melee", "archetype = laser"), "archetype = laser",
         "'laser' is not one of: instant-hit, projectile, beam, melee"},
        {"a missing projectile", edited("projectile = ball", "projectile = bal"), "[fire gun alt]",
         "there is no [projectile bal]"},
        {"a missing split kind", edited("split-into = chunk", "split-into = chunks"), "[projectile shell]",
         "there is no [projectile chunks]"},
        {"a missing combo", edited("combo = boom", "combo = bom"), "[projectile ball]", "there is no [combo bom]"},
        {"a fire before its weapon", edited("[fire gun primary]", "[fire gnu primary]"), "[fire gnu primary]",
         "there is no [weapon gnu] above this line"},
        {"a bad name", edited("[weapon hammer]", "[weapon Hammer]"), "[weapon Hammer]", "is not a [weapon] name"},
        {"a bad class", edited("class = Botpack.ShockRifle", "class = ShockRifle"), "class = ShockRifle",
         "is not a class"},
        {"an offset of two numbers", edited("offset = 4.4 6.7 -9.6", "offset = 4.4 6.7"), "offset",
         "offset is three numbers"},
        {"a head damage with no head momentum", edited("head-momentum = 35000\n", ""), "[fire gun primary]",
         "has no `head-momentum`"},
        {"a speed draw past the speed", edited("speed-random = 100", "speed-random = 2501"), "[projectile chunk]",
         "speed-random is more than speed"},
        {"a bounce draw below nothing", edited("bounce-off-random = 0.2", "bounce-off-random = 0.4"),
         "[projectile chunk]", "bounce-off-random is more than bounce-off"},
        {"a spin-up with no spin-refire", edited("spin-refire = 0.2\n", ""), "[fire hammer alt]", "has no `spin-refire`"},
        {"a spin-refire with no spin-up", edited("spin-up = 0.5\n", ""), "spin-refire = 0.2", "has no key 'spin-refire'"},
        {"a lifetime draw to zero", edited("lifetime-random = 0.5", "lifetime-random = 2.9"), "[projectile chunk]",
         "lifetime-random is not less than lifetime"},
        {"an unknown rule", edited("rule = rocket-fan", "rule = spiral"), "rule = spiral",
         "'spiral' is not one of: none, rocket-fan, grenade-cluster"},
        {"another archetype's rule", edited("pellets = 3", "pellets = 3\nrule = rocket-fan"),
         "rule = rocket-fan\nhead", "'rocket-fan' is not a rule of an instant-hit fire"},
        {"a rule of none", edited("rule = rocket-fan", "rule = none"), "rule = none",
         "'none' is not a rule of a projectile fire"},
        {"an unknown splash centre", edited("splash-from = moved", "splash-from = middle"), "splash-from",
         "'middle' is not one of: contact, moved"},
        {"an unknown body hit", edited("body-hit = direct", "body-hit = glancing"), "body-hit",
         "'glancing' is not one of: splash, direct"},
        {"an upward cap past 1", edited("splash-up-max = 0.45", "splash-up-max = 1.5"), "splash-up-max",
         "is not a number from 0 to 1"},
        {"a second [set]", edited("[weapon gun]", "[set]\n[weapon gun]"), "[set]\n[weapon gun]", "a second [set]"},
    };
    for (const Refusal& refusal : refusals) {
        CAPTURE(refusal.what);
        const auto set = uta::ugame::parse(refusal.text);
        REQUIRE_FALSE(set.has_value());
        CHECK(set.error().code() == uta::ErrorCode::MalformedData);
        const std::string message(set.error().message());
        CAPTURE(message);
        CHECK(message.starts_with(std::format("weapon set line {}: ", lineOf(refusal.text, refusal.at))));
        CHECK(message.find(refusal.says) != std::string::npos);
    }
}

TEST_CASE("INV-1: a weapon with no primary fire is refused at its heading", "[ugame][weapons]") {
    std::string onlyAlt(SMALL);
    const std::size_t from = onlyAlt.find("[fire hammer primary]");
    const std::size_t to = onlyAlt.find("[fire hammer alt]");
    onlyAlt.erase(from, to - from);
    const auto set = uta::ugame::parse(onlyAlt);
    REQUIRE_FALSE(set.has_value());
    const std::string message(set.error().message());
    CHECK(message.starts_with(std::format("weapon set line {}: ", lineOf(onlyAlt, "[weapon hammer]"))));
    CHECK(message.find("has no [fire hammer primary]") != std::string::npos);
}

TEST_CASE("INV-1: a newer set is unsupported, and no [set] is refused", "[ugame][weapons]") {
    const auto newer = uta::ugame::parse(edited("ut-ants weapons 1", "ut-ants weapons 2"));
    REQUIRE_FALSE(newer.has_value());
    CHECK(newer.error().code() == uta::ErrorCode::UnsupportedVersion);
    const auto noSet = uta::ugame::parse("ut-ants weapons 1\n");
    REQUIRE_FALSE(noSet.has_value());
    CHECK(noSet.error().message() == "weapon set line 1: the weapon set has no [set] section");
}

TEST_CASE("INV-3: the stock set parses and maps each stock class to one weapon", "[ugame][weapons]") {
    const auto set = uta::ugame::load(uta::ugame::shippedDirectory() / "stock.weapons");
    REQUIRE(set.has_value());
    for (const std::string_view stockClass :
         {"botpack.impacthammer", "botpack.chainsaw", "botpack.enforcer", "botpack.ut_biorifle", "botpack.shockrifle",
          "botpack.pulsegun", "botpack.ripper", "botpack.minigun2", "botpack.ut_flakcannon", "botpack.ut_eightball",
          "botpack.sniperrifle"}) {
        CAPTURE(stockClass);
        CHECK(std::ranges::count(set->weapons, stockClass, &uta::ugame::Weapon::className) == 1);
    }
    CHECK(set->weapons.size() == 11);
    // INV-2 holds for it too.
    const auto again = uta::ugame::parse(uta::ugame::write(*set));
    REQUIRE(again.has_value());
    CHECK(*again == *set);
}

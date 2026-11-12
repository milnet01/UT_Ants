// Locks UTA-0456's reader (docs/specs/UTA-0456-movement-recorder.md § 4.7):
// INV-9, it reads a well-formed recording exactly and refuses each malformed
// case; and INV-4, consecutive rows are consecutive ticks.
//
// Each refusal fixture differs from the good one in ONE place, so a refusal
// can only come from the check it names. The good fixture's rows are a real
// recording's shape (ut-record, DM-Deck16][, 60 fps cap, 2026-11-10), cut to
// three ticks.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest.cpp
// says why.

#include "support/MovementRecording.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <string>
#include <string_view>

namespace {

using uta::ErrorCode;
using uta::test::readMovementRecording;

const std::string kGood =
    "1 BEGIN run=forward map=DM-Deck16][ rate=60.000000 pose=891.129211,-1558.009521,-727.897827,26904\n"
    "1 PAWN class=UTARecPawn GroundSpeed=400.000000 JumpZ=357.500000 TimeDilation=1.100000 Bots=0\n"
    "1 T 0 1.657526 0.031288 F 6000.000000 0.000000 891.129211 -1558.009521 -724.200012 0.000000 0.000000"
    " 0.000000 -0.000000 0.000000 0.000000 PHYS_Walking DODGE_None Breath2 LevelInfo0\n"
    "1 T 1 1.690145 0.032619 F 6000.000000 0.000000 889.432739 -1556.940674 -724.200012 -54.220387 34.161087"
    " 0.000000 -1732.899048 1091.767700 0.000000 PHYS_Walking DODGE_Forward RunSm LevelInfo0\n"
    "1 T 2 1.719145 0.029000 FL 6000.000000 -6000.000000 887.0 -1555.0 -724.200012 -100.5 60.25"
    " 0.0 -1732.899048 1091.767700 0.0 PHYS_Falling DODGE_Active DodgeF None\n"
    "1 END run=forward ticks=3\n";

/// kGood with `from` replaced by `to`; `from` must occur exactly once.
std::string changed(std::string_view from, std::string_view to) {
    std::string text = kGood;
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    REQUIRE(text.find(from, at + 1) == std::string::npos);
    return text.replace(at, from.size(), to);
}

/// The reader refuses `text` with `code`, and its message names `why`.
void refused(const std::string& text, ErrorCode code, std::string_view why) {
    const auto read = readMovementRecording(text);
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().code() == code);
    CHECK_THAT(std::string(read.error().message()), Catch::Matchers::ContainsSubstring(std::string(why)));
}

}  // namespace

TEST_CASE("UTA-0456 INV-9: the reader reads a well-formed recording exactly") {
    const auto read = readMovementRecording(kGood);
    REQUIRE(read.has_value());
    const auto& r = *read;
    CHECK(r.version == 1);
    CHECK(r.run == "forward");
    CHECK(r.map == "DM-Deck16][");
    CHECK(r.rate == 60.0);
    CHECK(r.pose[0] == 891.129211);
    CHECK(r.pose[1] == -1558.009521);
    CHECK(r.pose[2] == -727.897827);
    CHECK(r.pose[3] == 26904.0);
    CHECK(r.pawnClass == "UTARecPawn");
    CHECK(r.pawn.size() == 4);
    CHECK(r.pawn.at("GroundSpeed") == 400.0);
    CHECK(r.pawn.at("JumpZ") == 357.5);
    CHECK(r.pawn.at("TimeDilation") == 1.1);
    CHECK(r.pawn.at("Bots") == 0.0);
    REQUIRE(r.ticks.size() == 3);

    const auto& t = r.ticks[1];
    CHECK(t.tick == 1);
    CHECK(t.time == 1.690145);
    CHECK(t.dt == 0.032619);
    CHECK(t.keys == "F");
    CHECK(t.forward == 6000.0);
    CHECK(t.strafe == 0.0);
    CHECK(t.location == std::array{889.432739, -1556.940674, -724.200012});
    CHECK(t.velocity == std::array{-54.220387, 34.161087, 0.0});
    CHECK(t.acceleration == std::array{-1732.899048, 1091.7677, 0.0});
    CHECK(t.physics == "PHYS_Walking");
    CHECK(t.dodge == "DODGE_Forward");
    CHECK(t.anim == "RunSm");
    CHECK(t.base == "LevelInfo0");

    const auto& last = r.ticks[2];
    CHECK(last.keys == "FL");
    CHECK(last.strafe == -6000.0);
    CHECK(last.velocity == std::array{-100.5, 60.25, 0.0});
    CHECK(last.physics == "PHYS_Falling");
    CHECK(last.base == "None");
}

TEST_CASE("UTA-0456 INV-9: a version other than 1 is refused") {
    refused(changed("1 BEGIN", "2 BEGIN"), ErrorCode::UnsupportedVersion, "format '2'");
}

TEST_CASE("UTA-0456 INV-9: a recording with no BEGIN is refused") {
    refused(changed("1 BEGIN run=forward", "1 BEGUN run=forward"), ErrorCode::MalformedData, "no BEGIN");
}

TEST_CASE("UTA-0456 INV-9: a recording with no PAWN is refused") {
    refused(changed("1 PAWN class", "1 PAWNS class"), ErrorCode::MalformedData, "no PAWN");
}

TEST_CASE("UTA-0456 INV-9: a recording with no END is refused") {
    refused(changed("1 END run=forward ticks=3\n", ""), ErrorCode::MalformedData, "no END");
}

TEST_CASE("UTA-0456 INV-9: an END count that differs from the rows is refused") {
    refused(changed("ticks=3", "ticks=4"), ErrorCode::MalformedData, "END counts 4 ticks and there are 3");
}

TEST_CASE("UTA-0456 INV-9: a row with the wrong column count is refused") {
    refused(changed(" LevelInfo0\n1 T 1", "\n1 T 1"), ErrorCode::MalformedData, "18 columns");
}

TEST_CASE("UTA-0456 INV-9: a field that does not parse is refused") {
    refused(changed("-54.220387", "-54.22o387"), ErrorCode::MalformedData, "'-54.22o387' is not a number");
}

TEST_CASE("UTA-0456 INV-9: ticks out of order are refused") {
    refused(changed("1 T 1 ", "1 T 2 "), ErrorCode::MalformedData, "tick 2 where tick 1 comes next");
}

TEST_CASE("UTA-0456 INV-4: a gap between rows is refused") {
    // Row 2's time moves on by 0.05 s more than its dt: a tick went unlogged.
    refused(changed("1.719145", "1.769145"), ErrorCode::MalformedData, "INV-4");
}

// UTA-0019 § 4.7's weapon lines, from a real rocket run (ut-record,
// DM-Deck16][, 85 fps cap, 2026-11-12), placed before kGood's row 1.
namespace {

const std::string kWeaponLines =
    "1 W 1 UT_Eightball NormalFire Rotate1 -0.105705 47 -1000 -15144 26.999840 1 0\n"
    "1 P 1 RocketMk20 RocketMk2 -110.255997 -488.521332 -703.964783 900.5 -0.25 3.445891 PHYS_Projectile\n"
    "1 G 1 -114.128799 506.338287 -660.200012 0.000000 0.000000 0.000000 PHYS_Walking 100000\n"
    "1 D 1 42.382153 UTARecTarget7 UTARecPawn0 229 Corroded -92.382248 510.108612 -642.970032 -71225.406250"
    " -12348.803711 -56432.496094 100000 99657 0.000000 0.000000 0.000000 -712.254028 -123.488037 366.828064"
    " PHYS_Walking -114.128799 506.338287 -660.200012\n";

/// kGood with the weapon lines `lines` before row 1.
std::string withWeapon(std::string_view lines) {
    return changed("1 T 1 ", std::string(lines) + "1 T 1 ");
}

}  // namespace

TEST_CASE("UTA-0019 SS 4.7: the reader reads a weapon run's lines exactly") {
    const auto read = readMovementRecording(withWeapon(kWeaponLines));
    REQUIRE(read.has_value());
    const auto& r = *read;
    CHECK(r.ticks.size() == 3);
    CHECK(r.begin.at("run") == "forward");

    REQUIRE(r.weapon.size() == 1);
    const auto& w = r.weapon[0];
    CHECK(w.tick == 1);
    CHECK(w.weapon == "UT_Eightball");
    CHECK(w.state == "NormalFire");
    CHECK(w.anim == "Rotate1");
    CHECK(w.frame == -0.105705);
    CHECK(w.ammo == 47);
    CHECK(w.pitch == -1000);
    CHECK(w.yaw == -15144);
    CHECK(w.eyeHeight == 26.99984);
    CHECK(w.fire);
    CHECK_FALSE(w.altFire);

    REQUIRE(r.projectiles.size() == 1);
    const auto& p = r.projectiles[0];
    CHECK(p.name == "RocketMk20");
    CHECK(p.cls == "RocketMk2");
    CHECK(p.location == std::array{-110.255997, -488.521332, -703.964783});
    CHECK(p.velocity == std::array{900.5, -0.25, 3.445891});
    CHECK(p.physics == "PHYS_Projectile");

    REQUIRE(r.target.size() == 1);
    CHECK(r.target[0].location == std::array{-114.128799, 506.338287, -660.200012});
    CHECK(r.target[0].physics == "PHYS_Walking");
    CHECK(r.target[0].health == 100000.0);

    REQUIRE(r.damage.size() == 1);
    const auto& d = r.damage[0];
    CHECK(d.time == 42.382153);
    CHECK(d.victim == "UTARecTarget7");
    CHECK(d.instigator == "UTARecPawn0");
    CHECK(d.damage == 229.0);
    CHECK(d.type == "Corroded");
    CHECK(d.momentum == std::array{-71225.40625, -12348.803711, -56432.496094});
    CHECK(d.health0 == 100000.0);
    CHECK(d.health1 == 99657.0);
    CHECK(d.velocity1 == std::array{-712.254028, -123.488037, 366.828064});
    CHECK(d.physics0 == "PHYS_Walking");
    CHECK(d.location == std::array{-114.128799, 506.338287, -660.200012});
}

TEST_CASE("UTA-0019 SS 4.7: a weapon line with the wrong column count is refused") {
    refused(withWeapon("1 G 1 0 0 0 0 0 0 PHYS_Walking\n"), ErrorCode::MalformedData, "a G line has 8 columns, not 9");
}

TEST_CASE("UTA-0019 SS 4.7: a weapon line out of its tick is refused") {
    refused(withWeapon("1 G 2 0 0 0 0 0 0 PHYS_Walking 100\n"), ErrorCode::MalformedData,
            "a G line at tick 2 where tick 1 comes next");
}

TEST_CASE("UTA-0019 SS 4.7: a weapon line's field that does not parse is refused") {
    refused(withWeapon("1 W 1 Enforcer NormalFire Shoot 0.5 4x 0 0 27 1 0\n"), ErrorCode::MalformedData,
            "a W line's field does not parse");
}

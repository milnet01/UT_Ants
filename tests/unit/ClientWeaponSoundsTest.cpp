// UTA-0467 INV-9 and INV-10: the client plays the game's cues and keeps each
// body's loop (docs/specs/UTA-0467-weapon-sounds.md SS 4.5).
// apps/ut-ants/WeaponSounds.cpp is compiled into this binary.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "ut-ants/WeaponSounds.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using uta::client::WEAPON_SOUND_RADIUS;
using uta::client::WeaponSounds;
using uta::uaudio::Clip;
using uta::uaudio::Group;
using uta::uaudio::GroupTaps;
using uta::uaudio::Listener;
using uta::uaudio::Mixer;
using uta::uaudio::SoundBank;
using uta::ugame::Body;
using uta::ugame::Carried;
using uta::ugame::Cue;
using uta::ugame::FireMode;
using uta::ugame::Vec3;
using uta::ugame::Weapon;
using uta::ugame::WeaponSet;

namespace {

void put(std::vector<std::byte>& out, std::string_view tag) {
    for (const char c : tag) out.push_back(static_cast<std::byte>(c));
}

void putLe(std::vector<std::byte>& out, std::uint32_t value, int width) {
    for (int i = 0; i < width; ++i) out.push_back(static_cast<std::byte>(value >> (8 * i) & 0xffu));
}

/// A mono 8-bit RIFF WAV of `samples` loud samples, as ClientMapSoundsTest.cpp builds one.
std::vector<std::byte> wav(std::uint32_t samples) {
    std::vector<std::byte> body;
    put(body, "WAVE");
    put(body, "fmt ");
    putLe(body, 16, 4);
    putLe(body, 1, 2);
    putLe(body, 1, 2);
    putLe(body, 22050, 4);
    putLe(body, 22050, 4);
    putLe(body, 1, 2);
    putLe(body, 8, 2);
    put(body, "data");
    putLe(body, samples, 4);
    for (std::uint32_t i = 0; i < samples; ++i) body.push_back(static_cast<std::byte>(200u));
    std::vector<std::byte> out;
    put(out, "RIFF");
    putLe(out, static_cast<std::uint32_t>(body.size()), 4);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

/// One weapon whose primary names a shot, a loop and its end, and whose alt
/// names a sound and a loop the bank will not hold; a second weapon with
/// nothing; a third whose primary loops another sound.
WeaponSet testSet() {
    WeaponSet set;
    Weapon gun;
    gun.name = "gun";
    gun.primary.ammo = 1;
    gun.primary.sound = "t.shot";
    gun.primary.loopSound = "t.loop";
    gun.primary.endSound = "t.end";
    gun.alt = FireMode{};
    gun.alt->sound = "t.gone";
    gun.alt->loopSound = "t.gone";
    Weapon other;
    other.name = "other";
    Weapon saw;
    saw.name = "saw";
    saw.primary.loopSound = "t.saw";
    set.weapons = {gun, other, saw};
    return set;
}

SoundBank testBank() {
    SoundBank bank;
    // Long enough that a one-shot is still playing after the blocks a test mixes.
    for (const char* const path : {"t.shot", "t.loop", "t.end", "t.saw"})
        REQUIRE(bank.add(path, wav(4000)).has_value());
    return bank;
}

/// A living body holding fire on the gun, a shot into its hold: its primary loops.
Body firing(const Vec3& at) {
    Body body;
    body.move.location = at;
    body.weapons = {Carried{true, 10}, Carried{true, 0}, Carried{true, 0}};
    body.weapon = 0;
    body.fireHeld = true;
    body.burst = 1;
    return body;
}

/// Mixes `blocks` blocks, so a stopped loop's ramp runs out.
void mixBlocks(Mixer& mixer, int blocks) {
    std::vector<float> out(Mixer::BLOCK * 2);
    for (int i = 0; i < blocks; ++i) mixer.mix(out);
}

const Listener AT_ORIGIN{};

} // namespace

TEST_CASE("UTA-0467 INV-9: a cue in earshot whose sound the bank holds starts one weapons voice",
          "[client][weapon-sounds]") {
    const WeaponSet set = testSet();
    const SoundBank bank = testBank();
    const Clip* const shot = bank.find("t.shot");
    REQUIRE(shot != nullptr);
    WeaponSounds sounds(set, bank);
    CHECK(sounds.missing() == 1); // t.gone

    Mixer mixer;
    const std::vector<Cue> near{Cue{Cue::Kind::Fire, 0, 0, 0, Vec3{100, 0, 0}}};
    sounds.update(AT_ORIGIN, near, {}, mixer);
    CHECK(mixer.voicesOf(*shot) == 1);

    // It plays in the weapons group and in no other.
    std::vector<float> out(Mixer::BLOCK * 2);
    std::array<std::vector<float>, uta::uaudio::GROUP_COUNT> heard;
    GroupTaps taps;
    for (std::size_t group = 0; group < heard.size(); ++group) {
        heard[group].assign(out.size(), 0.0f);
        taps.buffers[group] = heard[group];
    }
    mixer.mix(out, &taps);
    const auto loud = [&heard](Group group) {
        return std::ranges::any_of(heard[static_cast<std::size_t>(group)], [](float sample) { return sample != 0.0f; });
    };
    CHECK(loud(Group::Weapons));
    CHECK_FALSE(loud(Group::World));
    CHECK_FALSE(loud(Group::Speech));
    CHECK_FALSE(loud(Group::Music));
}

TEST_CASE("UTA-0467 INV-9: a cue past earshot or with no sound in the bank starts no voice",
          "[client][weapon-sounds]") {
    const WeaponSet set = testSet();
    const SoundBank bank = testBank();
    const Clip* const shot = bank.find("t.shot");
    REQUIRE(shot != nullptr);
    WeaponSounds sounds(set, bank);

    // The same cue, differing in `at` alone: just inside, then just outside.
    Mixer inside;
    sounds.update(AT_ORIGIN, std::vector<Cue>{Cue{Cue::Kind::Fire, 0, 0, 0, Vec3{WEAPON_SOUND_RADIUS - 1, 0, 0}}}, {},
                  inside);
    CHECK(inside.voicesOf(*shot) == 1);
    Mixer outside;
    sounds.update(AT_ORIGIN, std::vector<Cue>{Cue{Cue::Kind::Fire, 0, 0, 0, Vec3{WEAPON_SOUND_RADIUS + 1, 0, 0}}}, {},
                  outside);
    CHECK(outside.voicesOf(*shot) == 0);

    // The alt names a sound the bank lacks; the other weapon names none; a
    // cue for a weapon past the set names nothing. None is read through.
    Mixer silent;
    const std::vector<Cue> unheard{Cue{Cue::Kind::Fire, 0, 0, 1, Vec3{100, 0, 0}},
                                   Cue{Cue::Kind::Fire, 0, 1, 0, Vec3{100, 0, 0}},
                                   Cue{Cue::Kind::Select, 0, 0, -1, Vec3{100, 0, 0}},
                                   Cue{Cue::Kind::Explode, 0, 7, -1, Vec3{100, 0, 0}},
                                   Cue{Cue::Kind::Fire, 0, 99, 0, Vec3{100, 0, 0}}};
    sounds.update(AT_ORIGIN, unheard, {}, silent);
    for (const char* const path : {"t.shot", "t.loop", "t.end"}) CHECK(silent.voicesOf(*bank.find(path)) == 0);
}

TEST_CASE("UTA-0467 INV-10: a looping mode holds one voice and ends with its end sound",
          "[client][weapon-sounds]") {
    const WeaponSet set = testSet();
    const SoundBank bank = testBank();
    const Clip* const loop = bank.find("t.loop");
    const Clip* const end = bank.find("t.end");
    REQUIRE(loop != nullptr);
    REQUIRE(end != nullptr);
    WeaponSounds sounds(set, bank);
    Mixer mixer;

    std::vector<Body> bodies{firing({100, 0, 0})};
    REQUIRE(uta::ugame::loopingMode(set, bodies[0]) == 0);
    for (int frame = 0; frame < 5; ++frame) {
        sounds.update(AT_ORIGIN, {}, bodies, mixer);
        mixBlocks(mixer, 1);
        CHECK(mixer.voicesOf(*loop) == 1);
    }
    CHECK(mixer.voicesOf(*end) == 0);

    // The trigger up, the gun still in hand: the loop stops and its end plays once.
    bodies[0].fireHeld = false;
    REQUIRE(uta::ugame::loopingMode(set, bodies[0]) == -1);
    sounds.update(AT_ORIGIN, {}, bodies, mixer);
    CHECK(mixer.voicesOf(*end) == 1);
    mixBlocks(mixer, 2);
    CHECK(mixer.voicesOf(*loop) == 0);
    sounds.update(AT_ORIGIN, {}, bodies, mixer);
    CHECK(mixer.voicesOf(*end) == 1); // not again
}

TEST_CASE("UTA-0467 INV-10: a loop ended by a switch or a death plays no end sound", "[client][weapon-sounds]") {
    const WeaponSet set = testSet();
    const SoundBank bank = testBank();
    const Clip* const loop = bank.find("t.loop");
    const Clip* const end = bank.find("t.end");
    for (const std::string_view how : {"a switch", "a death", "the body gone", "another weapon in hand"}) {
        CAPTURE(how);
        WeaponSounds sounds(set, bank);
        Mixer mixer;
        std::vector<Body> bodies{firing({100, 0, 0})};
        sounds.update(AT_ORIGIN, {}, bodies, mixer);
        REQUIRE(mixer.voicesOf(*loop) == 1);
        if (how == "a switch") bodies[0].pending = 1;
        else if (how == "a death") bodies[0].health = 0;
        else if (how == "the body gone") bodies.clear();
        else bodies[0].weapon = 1;
        sounds.update(AT_ORIGIN, {}, bodies, mixer);
        CHECK(mixer.voicesOf(*end) == 0);
        mixBlocks(mixer, 2);
        CHECK(mixer.voicesOf(*loop) == 0);
    }
}

TEST_CASE("UTA-0467 INV-10: a loop past earshot holds no voice until it comes near", "[client][weapon-sounds]") {
    const WeaponSet set = testSet();
    const SoundBank bank = testBank();
    const Clip* const loop = bank.find("t.loop");
    WeaponSounds sounds(set, bank);
    Mixer mixer;
    std::vector<Body> bodies{firing({WEAPON_SOUND_RADIUS + 1, 0, 0})};
    sounds.update(AT_ORIGIN, {}, bodies, mixer);
    CHECK(mixer.voicesOf(*loop) == 0);
    bodies[0].move.location = {100, 0, 0};
    sounds.update(AT_ORIGIN, {}, bodies, mixer);
    CHECK(mixer.voicesOf(*loop) == 1);
    bodies[0].move.location = {WEAPON_SOUND_RADIUS + 1, 0, 0};
    sounds.update(AT_ORIGIN, {}, bodies, mixer);
    mixBlocks(mixer, 2);
    CHECK(mixer.voicesOf(*loop) == 0);
    // Walking out of earshot is not the loop ending: no end sound.
    CHECK(mixer.voicesOf(*bank.find("t.end")) == 0);
}

TEST_CASE("UTA-0467 INV-10: another looping weapon in hand ends the first loop and starts its own",
          "[client][weapon-sounds]") {
    const WeaponSet set = testSet();
    const SoundBank bank = testBank();
    const Clip* const loop = bank.find("t.loop");
    const Clip* const saw = bank.find("t.saw");
    WeaponSounds sounds(set, bank);
    Mixer mixer;
    std::vector<Body> bodies{firing({100, 0, 0})};
    sounds.update(AT_ORIGIN, {}, bodies, mixer);
    REQUIRE(mixer.voicesOf(*loop) == 1);
    // The same mode number on another weapon, from one update to the next.
    bodies[0].weapon = 2;
    REQUIRE(uta::ugame::loopingMode(set, bodies[0]) == 0);
    sounds.update(AT_ORIGIN, {}, bodies, mixer);
    mixBlocks(mixer, 2);
    CHECK(mixer.voicesOf(*loop) == 0);
    CHECK(mixer.voicesOf(*saw) == 1);
    CHECK(mixer.voicesOf(*bank.find("t.end")) == 0);
}

TEST_CASE("UTA-0467 INV-10: a looping mode whose sound the bank lacks holds no voice", "[client][weapon-sounds]") {
    const WeaponSet set = testSet();
    const SoundBank bank = testBank();
    WeaponSounds sounds(set, bank);
    Mixer mixer;
    std::vector<Body> bodies{firing({100, 0, 0})};
    bodies[0].fireHeld = false;
    bodies[0].altHeld = true;
    REQUIRE(uta::ugame::loopingMode(set, bodies[0]) == 1);
    for (int frame = 0; frame < 3; ++frame) sounds.update(AT_ORIGIN, {}, bodies, mixer);
    for (const char* const path : {"t.shot", "t.loop", "t.end", "t.saw"}) CHECK(mixer.voicesOf(*bank.find(path)) == 0);
    // And it ends without one: its mode names no end sound.
    bodies[0].altHeld = false;
    sounds.update(AT_ORIGIN, {}, bodies, mixer);
    CHECK(mixer.voicesOf(*bank.find("t.end")) == 0);
}

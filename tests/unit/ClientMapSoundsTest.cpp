// UTA-0480 INV-6 to INV-8: a map's ambient emitters and the voices they hold
// (docs/specs/UTA-0480-map-sounds.md SS 4.3 and 4.4).
// apps/ut-ants/MapSounds.cpp is compiled into this binary.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "ut-ants/MapSounds.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::WithinAbs;
using uta::client::ambientEmitters;
using uta::client::Emitter;
using uta::client::EmitterScan;
using uta::client::listenerAt;
using uta::client::MapSounds;
using uta::uaudio::Clip;
using uta::uaudio::Listener;
using uta::uaudio::Mixer;
using uta::uaudio::SoundBank;
using uta::ubundle::ActorClass;
using uta::ubundle::ActorPlacement;
using uta::ubundle::Placements;
using uta::ubundle::PropertyRecord;
using uta::ubundle::ValueKind;

namespace {

void put(std::vector<std::byte>& out, std::string_view tag) {
    for (const char c : tag) out.push_back(static_cast<std::byte>(c));
}

void putLe(std::vector<std::byte>& out, std::uint32_t value, int width) {
    for (int i = 0; i < width; ++i) out.push_back(static_cast<std::byte>(value >> (8 * i) & 0xffu));
}

/// A mono 8-bit RIFF WAV of four samples, as SoundBankTest.cpp builds one.
std::vector<std::byte> wav() {
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
    putLe(body, 4, 4);
    for (const unsigned sample : {200u, 200u, 200u, 200u}) body.push_back(static_cast<std::byte>(sample));
    std::vector<std::byte> out;
    put(out, "RIFF");
    putLe(out, static_cast<std::uint32_t>(body.size()), 4);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

PropertyRecord sound(std::string name, std::string path) {
    return {.name = std::move(name), .arrayIndex = 0, .kind = ValueKind::Object, .value = std::move(path)};
}

PropertyRecord byte(std::string name, std::uint8_t value) {
    return {.name = std::move(name), .arrayIndex = 0, .kind = ValueKind::Byte, .value = value};
}

PropertyRecord location(float x, float y, float z) {
    return {.name = "Location", .arrayIndex = 0, .kind = ValueKind::Vector, .value = std::array<float, 3>{x, y, z}};
}

ActorPlacement actor(std::uint32_t exportIndex, std::uint32_t classIndex, std::vector<PropertyRecord> properties) {
    return {.exportIndex = exportIndex, .path = "map.actor", .classIndex = classIndex, .properties = std::move(properties)};
}

Clip constant(float value) {
    Clip clip;
    clip.rate = Mixer::RATE;
    clip.channels = 1;
    clip.samples.assign(64, value);
    return clip;
}

/// Mix `frames` and return each side's sum over the last block.
std::array<float, 2> sides(Mixer& mixer, std::size_t frames) {
    std::vector<float> out(frames * 2);
    mixer.mix(out);
    std::array<float, 2> sum{};
    for (std::size_t i = frames - Mixer::BLOCK; i < frames; ++i) {
        sum[0] += std::abs(out[i * 2]);
        sum[1] += std::abs(out[i * 2 + 1]);
    }
    return sum;
}

void settle(Mixer& mixer) {
    std::vector<float> out(Mixer::BLOCK * 2 * 2);
    mixer.mix(out);
}

} // namespace

TEST_CASE("UTA-0480 INV-6: an emitter is the actor's own values over its class's", "[client][uaudio]") {
    SoundBank bank;
    REQUIRE(bank.add("pkg.hum", wav()).has_value());
    Placements placements;
    // A class that hums, with a radius of its own; one that says nothing.
    placements.classes.push_back({.path = "pkg.lamp", .defaults = {sound("AmbientSound", "pkg.hum"), byte("SoundRadius", 10)}});
    placements.classes.push_back({.path = "pkg.rock"});
    placements.actors = {
        actor(1, 0, {location(1, 2, 3), byte("SoundRadius", 63), byte("SoundVolume", 51), byte("SoundPitch", 32)}),
        actor(2, 0, {}),                                       // the class's radius, the fallbacks for the rest
        actor(3, 1, {sound("ambientsound", "pkg.hum")}),       // its own sound, whatever the case of the name
        actor(4, 1, {}),                                       // no sound
        actor(5, 1, {sound("AmbientSound", "pkg.gone")}),      // a sound the bank lacks
        actor(6, 0, {byte("SoundVolume", 0)}),                 // silenced
        actor(7, 1, {sound("AmbientSound", "")}),              // cleared
        // A name that happens to spell a sound's path is not a sound.
        actor(8, 1, {{.name = "AmbientSound", .arrayIndex = 0, .kind = ValueKind::Name, .value = std::string("pkg.hum")}}),
    };

    const EmitterScan scan = ambientEmitters(placements, bank);
    REQUIRE(scan.emitters.size() == 3);
    CHECK(scan.missing == 1);
    CHECK(scan.guessed == 2); // all but the first, which stores all three

    const Emitter& own = scan.emitters[0];
    CHECK(own.clip == bank.find("pkg.hum"));
    CHECK(own.position == std::array<float, 3>{1, 2, 3});
    CHECK_THAT(own.radius, WithinAbs(1600.0, 1e-3)); // 25 x (63 + 1), not the class's 10
    CHECK_THAT(own.volume, WithinAbs(51.0 / 255.0, 1e-6));
    CHECK_THAT(own.pitch, WithinAbs(0.5, 1e-6));

    const Emitter& inherited = scan.emitters[1];
    CHECK_THAT(inherited.radius, WithinAbs(275.0, 1e-3)); // 25 x (10 + 1)
    CHECK_THAT(inherited.volume, WithinAbs(128.0 / 255.0, 1e-6));
    CHECK_THAT(inherited.pitch, WithinAbs(1.0, 1e-6));
    CHECK(inherited.position == std::array<float, 3>{});

    CHECK_THAT(scan.emitters[2].radius, WithinAbs(825.0, 1e-3)); // 25 x (32 + 1): stored nowhere
}

TEST_CASE("UTA-0480 INV-7: the loudest sixteen play and no more", "[client][uaudio]") {
    std::deque<Clip> clips;
    std::vector<Emitter> emitters;
    for (int i = 0; i < 20; ++i)
        emitters.push_back({.position = {}, .clip = &clips.emplace_back(constant(0.01f)), .radius = 1000.0f,
                            .volume = static_cast<float>(i + 1) / 20.0f});
    MapSounds sounds(emitters);
    Mixer mixer;
    sounds.update(Listener{}, mixer);
    settle(mixer);
    for (int i = 0; i < 20; ++i) {
        INFO("emitter " << i);
        CHECK(mixer.voicesOf(clips[static_cast<std::size_t>(i)]) == (i >= 4 ? 1 : 0));
    }
    // A second frame in the same place changes nothing: no voice doubles.
    sounds.update(Listener{}, mixer);
    settle(mixer);
    for (const Clip& clip : clips) CHECK(mixer.voicesOf(clip) <= 1);

    // Beyond every radius, none is left.
    sounds.update(Listener{.position = {5000, 0, 0}}, mixer);
    settle(mixer);
    for (const Clip& clip : clips) CHECK(mixer.voicesOf(clip) == 0);
}

TEST_CASE("UTA-0480 INV-7: a near-equal emitter does not take a playing one's voice", "[client][uaudio]") {
    std::deque<Clip> clips;
    std::vector<Emitter> emitters;
    // Fifteen loud ones and a quiet one, heard alike wherever the listener
    // stands; and a challenger whose loudness is the listener's X / 1000.
    for (int i = 0; i < 15; ++i)
        emitters.push_back({.position = {}, .clip = &clips.emplace_back(constant(0.01f)), .radius = 1e7f, .volume = 1.0f});
    const Clip& quiet = clips.emplace_back(constant(0.01f));
    emitters.push_back({.position = {}, .clip = &quiet, .radius = 1e7f, .volume = 0.5f});
    const Clip& challenger = clips.emplace_back(constant(0.01f));
    emitters.push_back({.position = {1000, 0, 0}, .clip = &challenger, .radius = 1000.0f, .volume = 1.0f});
    MapSounds sounds(emitters);
    Mixer mixer;

    sounds.update(Listener{}, mixer);
    settle(mixer);
    CHECK(mixer.voicesOf(quiet) == 1);
    CHECK(mixer.voicesOf(challenger) == 0);

    // 1.05 times as loud as the quiet one: it keeps its voice.
    sounds.update(Listener{.position = {525, 0, 0}}, mixer);
    settle(mixer);
    CHECK(mixer.voicesOf(quiet) == 1);
    CHECK(mixer.voicesOf(challenger) == 0);

    // 1.2 times as loud: it takes it.
    sounds.update(Listener{.position = {600, 0, 0}}, mixer);
    settle(mixer);
    CHECK(mixer.voicesOf(quiet) == 0);
    CHECK(mixer.voicesOf(challenger) == 1);

    // And now the quiet one is the challenger: back at 525 nothing swaps.
    sounds.update(Listener{.position = {525, 0, 0}}, mixer);
    settle(mixer);
    CHECK(mixer.voicesOf(quiet) == 0);
    CHECK(mixer.voicesOf(challenger) == 1);
}

TEST_CASE("UTA-0480 INV-7: a stolen voice is forgotten and comes back", "[client][uaudio]") {
    const Clip hum = constant(0.01f);
    MapSounds sounds({{.position = {}, .clip = &hum, .radius = 1000.0f, .volume = 1.0f}});
    Mixer mixer;
    sounds.update(Listener{}, mixer);
    settle(mixer);
    REQUIRE(mixer.voicesOf(hum) == 1);
    mixer.stopAll(); // as a steal: the voice is gone behind its back
    sounds.update(Listener{}, mixer);
    sounds.update(Listener{}, mixer);
    settle(mixer);
    CHECK(mixer.voicesOf(hum) == 1);
}

TEST_CASE("UTA-0480 INV-8: a hum is heard on the side it is on", "[client][uaudio]") {
    const Clip hum = constant(0.5f);
    // UT's Y is to the right of a camera at yaw 0, which looks along +X.
    MapSounds sounds({{.position = {0, 300, 0}, .clip = &hum, .radius = 1000.0f, .volume = 1.0f}});
    Mixer mixer;

    sounds.update(listenerAt({0, 0, 0}, {0, 0, 0}), mixer);
    std::array<float, 2> heard = sides(mixer, Mixer::BLOCK * 2);
    CHECK(heard[1] > heard[0] * 1.5f);
    CHECK(heard[0] > 0.0f); // never one ear only

    // Turned round, it is on the left.
    sounds.update(listenerAt({0, 0, 0}, {0, 32768, 0}), mixer);
    heard = sides(mixer, Mixer::BLOCK * 2);
    CHECK(heard[0] > heard[1] * 1.5f);

    // Looking up or down does not tilt the ears.
    const Listener up = listenerAt({0, 0, 0}, {16000, 0, 0});
    CHECK_THAT(up.right[1], WithinAbs(1.0, 1e-6));
    CHECK_THAT(up.right[2], WithinAbs(0.0, 1e-6));
}

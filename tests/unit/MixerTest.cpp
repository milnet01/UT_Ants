// UTA-0056 INV-2 to INV-9: the mixer, on clips made here
// (docs/specs/UTA-0056-audio-mixer.md SS 4.3 to 4.6), and UTA-0480 INV-1 to
// INV-4: its looping voices (docs/specs/UTA-0480-map-sounds.md SS 4.1).
//
// INV-9 counts allocations by replacing the global operator new and delete
// for this whole binary. They pass through to malloc and free, and count only
// while a case asks them to. NOT UNDER A SANITIZER: its runtime defines them
// itself, and Clang 19's ThreadSanitizer leg failed to link a second copy.
// There the case says it cannot count; the other legs grade INV-9.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "uaudio/Mixer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <new>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using uta::uaudio::Clip;
using uta::uaudio::Group;
using uta::uaudio::GroupTaps;
using uta::uaudio::Mixer;
using uta::uaudio::Settings;
using uta::uaudio::VoiceId;

#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
#define UTA_COUNTS_ALLOCATIONS 0
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define UTA_COUNTS_ALLOCATIONS 0
#endif
#endif
#ifndef UTA_COUNTS_ALLOCATIONS
#define UTA_COUNTS_ALLOCATIONS 1
#endif

namespace {

thread_local bool g_counting = false;
thread_local std::size_t g_allocations = 0;

} // namespace

#if UTA_COUNTS_ALLOCATIONS
void* operator new(std::size_t size) {
    if (g_counting) ++g_allocations;
    if (void* const p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

namespace {

const float CENTRE = std::cos(std::numbers::pi_v<float> / 4.0f);

constexpr std::size_t ms(double milliseconds) {
    return static_cast<std::size_t>(milliseconds * Mixer::RATE / 1000.0);
}

Clip constant(float value, std::size_t frames, std::uint32_t rate = Mixer::RATE) {
    Clip clip;
    clip.rate = rate;
    clip.channels = 1;
    clip.samples.assign(frames, value);
    return clip;
}

/// Mix `frames` frames and return them, interleaved.
std::vector<float> run(Mixer& mixer, std::size_t frames, const GroupTaps* taps = nullptr) {
    std::vector<float> out(frames * 2);
    mixer.mix(out, taps);
    return out;
}

Settings noDuck() {
    Settings settings;
    settings.duck = false;
    return settings;
}

float dbGain(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

TEST_CASE("UTA-0056 INV-2: a clip lasts its length at its own rate", "[uaudio]") {
    for (const std::uint32_t rate : {11025u, 22050u, 44100u}) {
        CAPTURE(rate);
        const Clip clip = constant(0.5f, 1000, rate);
        Mixer mixer;
        mixer.play(clip, {});
        const std::vector<float> out = run(mixer, 6000);
        std::size_t sounding = 0;
        for (std::size_t i = 0; i < out.size() / 2; ++i)
            if (out[i * 2] != 0.0f) ++sounding;
        const auto expected = static_cast<std::size_t>(std::ceil(1000.0 * Mixer::RATE / rate));
        CHECK(sounding + 1 >= expected);
        CHECK(sounding <= expected + 1);
        CHECK_THAT(out[0], WithinAbs(0.5 * CENTRE, 1e-6));
        CHECK_THAT(out[1], WithinAbs(0.5 * CENTRE, 1e-6));
        CHECK_THAT(out[(expected - 2) * 2], WithinAbs(0.5 * CENTRE, 1e-6));
        CHECK(mixer.voicesOf(clip) == 0);
    }
}

TEST_CASE("UTA-0056 INV-2: interpolation runs between neighbours", "[uaudio]") {
    Clip ramp;
    ramp.rate = Mixer::RATE / 2;
    ramp.channels = 1;
    ramp.samples = {0.0f, 0.4f, 0.8f};
    Mixer mixer;
    mixer.play(ramp, {.pan = -1.0f});
    const std::vector<float> out = run(mixer, 8);
    CHECK_THAT(out[0], WithinAbs(0.0, 1e-6));
    CHECK_THAT(out[2], WithinAbs(0.2, 1e-6));
    CHECK_THAT(out[4], WithinAbs(0.4, 1e-6));
    CHECK_THAT(out[6], WithinAbs(0.6, 1e-6));
    CHECK_THAT(out[8], WithinAbs(0.8, 1e-6));
    CHECK_THAT(out[10], WithinAbs(0.8, 1e-6)); // the last frame holds; nothing past it is read
    CHECK_THAT(out[1], WithinAbs(0.0, 1e-6)); // hard left
}

TEST_CASE("UTA-0056 INV-3: each volume scales only its own group or the whole", "[uaudio]") {
    const std::vector<Clip> clips(4, constant(0.25f, 4800));
    std::vector<std::vector<float>> tapped(4, std::vector<float>(480 * 2));
    GroupTaps taps;
    for (std::size_t g = 0; g < 4; ++g) taps.buffers[g] = tapped[g];

    for (std::size_t loud = 0; loud <= 4; ++loud) {
        CAPTURE(loud);
        Settings settings = noDuck();
        if (loud < 4) settings.groupVolume[loud] = 0.5f;
        else settings.master = 0.5f;
        Mixer mixer;
        mixer.setSettings(settings);
        for (std::size_t g = 0; g < 4; ++g) mixer.play(clips[g], {.group = static_cast<Group>(g)});
        const std::vector<float> out = run(mixer, 480, &taps);

        float sum = 0;
        for (std::size_t g = 0; g < 4; ++g) {
            const float expected = 0.25f * CENTRE * (g == loud ? 0.5f : 1.0f) * (loud == 4 ? 0.5f : 1.0f);
            CHECK_THAT(tapped[g][100], WithinAbs(expected, 1e-6));
            sum += expected;
        }
        CHECK_THAT(out[100], WithinAbs(sum, 1e-6));
    }
}

TEST_CASE("UTA-0056 INV-4: speech dips the other groups and they recover", "[uaudio]") {
    const Clip speech = constant(0.1f, ms(1000));
    const Clip shortLine = constant(0.1f, ms(200));
    const Clip gun = constant(0.1f, ms(5000));
    const float dip = dbGain(-12.0f);
    Mixer mixer;
    mixer.play(gun, {.group = Group::Weapons});
    run(mixer, ms(100));
    for (const float gain : mixer.stats().duckGain) CHECK_THAT(gain, WithinAbs(1.0, 1e-6)); // a gun dips nothing

    mixer.play(speech, {.group = Group::Speech});
    mixer.play(shortLine, {.group = Group::Speech});
    run(mixer, ms(25));
    const auto halfway = mixer.stats().duckGain;
    CHECK_THAT(halfway[0], WithinAbs(dbGain(-6.0f), 1e-3));
    run(mixer, ms(25));
    auto stats = mixer.stats();
    CHECK_THAT(stats.duckGain[static_cast<std::size_t>(Group::Music)], WithinAbs(dip, 1e-3));
    CHECK_THAT(stats.duckGain[static_cast<std::size_t>(Group::World)], WithinAbs(dip, 1e-3));
    CHECK_THAT(stats.duckGain[static_cast<std::size_t>(Group::Weapons)], WithinAbs(dip, 1e-3));
    CHECK_THAT(stats.duckGain[static_cast<std::size_t>(Group::Speech)], WithinAbs(1.0, 1e-6));

    // The short line ends at 200 ms, mid-block; the long one holds the dip on
    // every frame, which the gun's own share shows.
    std::vector<std::vector<float>> tapped(4, std::vector<float>(ms(750) * 2));
    GroupTaps taps;
    for (std::size_t g = 0; g < 4; ++g) taps.buffers[g] = tapped[g];
    run(mixer, ms(750), &taps);
    float drift = 0;
    for (const float sample : tapped[static_cast<std::size_t>(Group::Weapons)])
        drift = std::max(drift, std::abs(sample - 0.1f * CENTRE * dip));
    CHECK(drift < 2e-5f);
    CHECK_THAT(mixer.stats().duckGain[0], WithinAbs(dip, 1e-3));

    // The long line ends at 1000 ms, here; release takes 500 ms.
    run(mixer, ms(200));
    run(mixer, ms(250));
    const float midRelease = mixer.stats().duckGain[0];
    CHECK_THAT(midRelease, WithinAbs(dbGain(-6.0f), 1e-3));
    run(mixer, ms(250));
    CHECK_THAT(mixer.stats().duckGain[0], WithinAbs(1.0, 1e-3));
}

TEST_CASE("UTA-0056 INV-4: speech is louder than the dipped gun in the output", "[uaudio]") {
    const Clip speech = constant(0.1f, ms(500));
    const Clip gun = constant(0.1f, ms(500));
    std::vector<std::vector<float>> tapped(4, std::vector<float>(ms(100) * 2));
    GroupTaps taps;
    for (std::size_t g = 0; g < 4; ++g) taps.buffers[g] = tapped[g];
    Mixer mixer;
    mixer.play(gun, {.group = Group::Weapons});
    mixer.play(speech, {.group = Group::Speech});
    run(mixer, ms(100), &taps);
    const std::size_t late = ms(80) * 2;
    CHECK_THAT(tapped[static_cast<std::size_t>(Group::Speech)][late], WithinAbs(0.1 * CENTRE, 1e-6));
    CHECK_THAT(tapped[static_cast<std::size_t>(Group::Weapons)][late], WithinAbs(0.1 * CENTRE * dbGain(-12), 1e-4));
}

TEST_CASE("UTA-0056 INV-4: with the duck off no group dips", "[uaudio]") {
    const Clip speech = constant(0.1f, ms(1000));
    Mixer mixer;
    mixer.setSettings(noDuck());
    mixer.play(speech, {.group = Group::Speech});
    run(mixer, ms(100));
    for (const float gain : mixer.stats().duckGain) CHECK_THAT(gain, WithinAbs(1.0, 1e-6));
}

TEST_CASE("UTA-0056 INV-5: a clip over its cap loses its oldest voice", "[uaudio]") {
    const Clip clip = constant(0.01f, ms(1000));
    const Clip other = constant(0.01f, ms(2000));
    Mixer mixer;
    mixer.play(other, {});
    for (int i = 0; i < 5; ++i) {
        mixer.play(clip, {});
        run(mixer, ms(100));
    }
    CHECK(mixer.voicesOf(clip) == 4);
    CHECK(mixer.voicesOf(other) == 1);
    // At 1050 ms the voice that started at 0 would have ended; the four left
    // started at 100 to 400 ms.
    run(mixer, ms(550));
    CHECK(mixer.voicesOf(clip) == 4);
}

TEST_CASE("UTA-0056 INV-6: requests within 40 ms collapse into one voice", "[uaudio]") {
    const Clip clip = constant(0.5f, ms(1000));
    Mixer mixer;
    mixer.setSettings(noDuck());
    const auto first = mixer.play(clip, {.gain = 0.6f});
    run(mixer, ms(40));
    const auto second = mixer.play(clip, {.gain = 0.8f});
    CHECK(second.value == first.value);
    CHECK(mixer.voicesOf(clip) == 1);
    const std::vector<float> out = run(mixer, 16);
    CHECK_THAT(out[0], WithinAbs(0.5 * CENTRE * 1.0, 1e-5)); // sqrt(0.36 + 0.64)

    Mixer loud;
    loud.setSettings(noDuck());
    loud.play(clip, {.gain = 1.5f});
    loud.play(clip, {.gain = 1.5f});
    const std::vector<float> capped = run(loud, 16);
    CHECK_THAT(capped[0], WithinAbs(0.5 * CENTRE * 2.0, 1e-5)); // not sqrt(4.5)

    Mixer apart;
    apart.play(clip, {});
    run(apart, ms(41));
    apart.play(clip, {});
    CHECK(apart.voicesOf(clip) == 2);

    Mixer sameTick;
    sameTick.play(clip, {});
    sameTick.play(clip, {});
    CHECK(sameTick.voicesOf(clip) == 1);
}

TEST_CASE("UTA-0056 INV-7: a full mixer steals by group then priority then age", "[uaudio]") {
    std::deque<Clip> clips;
    const auto fresh = [&] { return &clips.emplace_back(constant(0.001f, ms(5000))); };

    Mixer mixer;
    // Oldest first: speech, then weapons, then world, then music.
    std::vector<const Clip*> speech, music;
    for (int i = 0; i < 16; ++i) speech.push_back(fresh());
    for (const Clip* c : speech) mixer.play(*c, {.group = Group::Speech});
    for (int i = 0; i < 32; ++i) mixer.play(*fresh(), {.group = i < 16 ? Group::Weapons : Group::World});
    for (int i = 0; i < 16; ++i) music.push_back(fresh());
    for (int i = 0; i < 16; ++i) mixer.play(*music[i], {.group = Group::Music, .priority = i == 5 ? -1 : 0});

    CHECK(mixer.play(*fresh(), {.group = Group::Weapons}).valid());
    CHECK(mixer.voicesOf(*music[5]) == 0); // the lowest priority
    CHECK(mixer.voicesOf(*music[0]) == 1);
    CHECK(mixer.play(*fresh(), {.group = Group::Weapons}).valid());
    CHECK(mixer.voicesOf(*music[0]) == 0); // then the oldest
    CHECK(mixer.voicesOf(*speech[0]) == 1);

    Mixer allSpeech;
    for (int i = 0; i < 64; ++i) allSpeech.play(*fresh(), {.group = Group::Speech});
    CHECK_FALSE(allSpeech.play(*fresh(), {.group = Group::Music}).valid());
    CHECK(allSpeech.stats().dropped == 1);
    CHECK(allSpeech.play(*fresh(), {.group = Group::Speech}).valid());
}

TEST_CASE("UTA-0056 INV-8: no sample leaves full scale", "[uaudio]") {
    std::deque<Clip> clips;
    Mixer mixer;
    for (int v = 0; v < 64; ++v) {
        Clip& clip = clips.emplace_back();
        clip.rate = 22050 + static_cast<std::uint32_t>(v) * 300;
        clip.channels = 1;
        const int period = 20 + v;
        for (int i = 0; i < 44100; ++i) clip.samples.push_back(i / period % 2 == 0 ? 1.0f : -1.0f);
        // Panned both ways, so either channel can be a frame's peak.
        mixer.play(clip, {.group = static_cast<Group>(v % 4), .gain = 2.0f, .pan = static_cast<float>(v % 3 - 1) * 0.5f});
    }
    const std::vector<float> out = run(mixer, ms(1000));
    float loudest = 0;
    for (const float sample : out) loudest = std::max(loudest, std::abs(sample));
    CHECK(loudest <= 1.0f);
    CHECK(loudest > 0.9f);
    CHECK(mixer.stats().limiterReductionDb > 6.0f);
}

TEST_CASE("UTA-0056 INV-9: mix allocates nothing", "[uaudio]") {
    if (!UTA_COUNTS_ALLOCATIONS) {
        WARN("INV-9 not graded: a sanitizer owns operator new in this build");
        return;
    }
    std::deque<Clip> clips;
    Mixer mixer;
    for (int v = 0; v < 64; ++v) mixer.play(clips.emplace_back(constant(0.01f, ms(500))), {.group = static_cast<Group>(v % 4)});
    std::vector<float> out(ms(100) * 2);
    std::vector<std::vector<float>> tapped(4, std::vector<float>(out.size()));
    GroupTaps taps;
    for (std::size_t g = 0; g < 4; ++g) taps.buffers[g] = tapped[g];

    g_allocations = 0;
    g_counting = true;
    mixer.mix(out, &taps);
    g_counting = false;
    CHECK(g_allocations == 0);
    CHECK(out[0] != 0.0f); // it mixed something
}

namespace {

/// The largest step between neighbouring frames of either channel.
float largestStep(const std::vector<float>& out) {
    float largest = 0;
    for (std::size_t i = 2; i < out.size(); ++i) largest = std::max(largest, std::abs(out[i] - out[i - 2]));
    return largest;
}

} // namespace

TEST_CASE("UTA-0480 INV-1: a loop repeats its clip exactly past its end", "[uaudio]") {
    Clip clip;
    clip.rate = Mixer::RATE;
    clip.channels = 1;
    for (int i = 0; i < 100; ++i) clip.samples.push_back(static_cast<float>(i + 1) / 200.0f);
    Mixer mixer;
    mixer.setSettings(noDuck());
    REQUIRE(mixer.startLoop(clip, {.gain = 1.0f}).valid());
    const std::vector<float> out = run(mixer, 2000);
    CHECK(mixer.voicesOf(clip) == 1); // still playing, twenty times its length on
    // Past the start ramp, frame n is frame n + 100, on both sides.
    for (std::size_t n = Mixer::BLOCK; n + 100 < 2000; ++n) {
        REQUIRE(out[n * 2] == out[(n + 100) * 2]);
        REQUIRE(out[n * 2 + 1] == out[(n + 100) * 2 + 1]);
    }
    // And it is the clip: frame 300 is the clip's frame 0.
    CHECK_THAT(out[300 * 2], WithinAbs(clip.samples[0] * CENTRE, 1e-6));
    CHECK_THAT(out[399 * 2], WithinAbs(clip.samples[99] * CENTRE, 1e-6));

    // At half the mixer's rate every other output frame lies between two of
    // the clip's, and the one after its last frame lies between the last and
    // the FIRST: the loop has no seam.
    Clip slow;
    slow.rate = Mixer::RATE / 2;
    slow.channels = 1;
    slow.samples = {0.0f, 0.8f};
    Mixer second;
    second.setSettings(noDuck());
    REQUIRE(second.startLoop(slow, {.gain = 1.0f}).valid());
    const std::vector<float> seam = run(second, Mixer::BLOCK + 8);
    const std::size_t at = Mixer::BLOCK; // a multiple of 4, so the clip's frame 0
    CHECK_THAT(seam[at * 2], WithinAbs(0.0, 1e-6));
    CHECK_THAT(seam[(at + 1) * 2], WithinAbs(0.4f * CENTRE, 1e-6));
    CHECK_THAT(seam[(at + 2) * 2], WithinAbs(0.8f * CENTRE, 1e-6));
    CHECK_THAT(seam[(at + 3) * 2], WithinAbs(0.4f * CENTRE, 1e-6));
}

TEST_CASE("UTA-0480 INV-2: a loop's start and its changes and its stop ramp across one block", "[uaudio]") {
    const Clip clip = constant(1.0f, 64);
    const float limit = 2.0f / static_cast<float>(Mixer::BLOCK);
    Mixer mixer;
    mixer.setSettings(noDuck());
    const VoiceId loop = mixer.startLoop(clip, {.gain = 0.0f});
    REQUIRE(loop.valid());
    run(mixer, Mixer::BLOCK);

    REQUIRE(mixer.setLoop(loop, 1.0f, 0.0f));
    std::vector<float> out = run(mixer, Mixer::BLOCK * 2);
    CHECK(largestStep(out) <= limit);
    CHECK(largestStep(out) > 0.0f); // it did move
    // The target is reached within the block, exactly.
    CHECK_THAT(out[(Mixer::BLOCK - 1) * 2], WithinAbs(CENTRE, 1e-6));
    CHECK_THAT(out[(Mixer::BLOCK * 2 - 1) * 2], WithinAbs(CENTRE, 1e-6));

    // Hard right: the left side falls to nothing and the right rises to 1.
    REQUIRE(mixer.setLoop(loop, 1.0f, 1.0f));
    out = run(mixer, Mixer::BLOCK);
    CHECK(largestStep(out) <= limit);
    CHECK_THAT(out[(Mixer::BLOCK - 1) * 2], WithinAbs(0.0, 1e-6));
    CHECK_THAT(out[(Mixer::BLOCK - 1) * 2 + 1], WithinAbs(1.0, 1e-6));

    mixer.stop(loop);
    out = run(mixer, Mixer::BLOCK * 2);
    CHECK(largestStep(out) <= limit);
    CHECK(out[1] > 0.5f);                     // not cut at once
    CHECK(out[(Mixer::BLOCK * 2 - 1) * 2 + 1] == 0.0f);
    CHECK(mixer.voicesOf(clip) == 0);
    CHECK_FALSE(mixer.setLoop(loop, 1.0f, 0.0f));
}

TEST_CASE("UTA-0480 INV-3: stop ends one voice and no other", "[uaudio]") {
    const Clip first = constant(0.25f, 64);
    const Clip second = constant(0.5f, 64);
    Mixer mixer;
    mixer.setSettings(noDuck());
    const VoiceId a = mixer.startLoop(first, {.gain = 1.0f});
    const VoiceId b = mixer.startLoop(second, {.gain = 1.0f});
    REQUIRE((a.valid() && b.valid()));
    run(mixer, Mixer::BLOCK);

    // The second, so a stop that took the first voice it met is seen.
    mixer.stop(b);
    const std::vector<float> out = run(mixer, Mixer::BLOCK * 2);
    CHECK(mixer.voicesOf(second) == 0);
    CHECK(mixer.voicesOf(first) == 1);
    // What is left is the first loop alone, untouched.
    CHECK_THAT(out[(Mixer::BLOCK * 2 - 1) * 2], WithinAbs(0.25f * CENTRE, 1e-6));

    // A second stop, and a stop of an id never given, change nothing.
    mixer.stop(b);
    mixer.stop(VoiceId{9999});
    mixer.stop(VoiceId{});
    run(mixer, Mixer::BLOCK);
    CHECK(mixer.voicesOf(first) == 1);

    // A one-shot stops too, at once.
    const Clip shot = constant(0.1f, ms(500));
    const VoiceId once = mixer.play(shot, {});
    mixer.stop(once);
    CHECK(mixer.voicesOf(shot) == 0);
}

TEST_CASE("UTA-0480 INV-4: loops skip the collapse and the clip cap and are still stolen", "[uaudio]") {
    const Clip hum = constant(0.01f, 64);
    Mixer mixer;
    // Six of one clip between two mixes: no collapse, and past the cap of 4.
    std::vector<VoiceId> loops;
    for (int i = 0; i < 6; ++i) loops.push_back(mixer.startLoop(hum, {.gain = 1.0f}));
    run(mixer, 16);
    CHECK(mixer.voicesOf(hum) == 6);
    // A one-shot of the same clip neither joins a loop nor steals one.
    mixer.play(hum, {});
    CHECK(mixer.voicesOf(hum) == 7);

    // Fill the mixer with loops; a weapons shot then steals the oldest world
    // voice, which is the first loop.
    mixer.stopAll();
    loops.clear();
    for (std::size_t i = 0; i < Mixer::MAX_VOICES; ++i) loops.push_back(mixer.startLoop(hum, {.gain = 1.0f}));
    CHECK(mixer.voicesOf(hum) == static_cast<int>(Mixer::MAX_VOICES));
    const Clip shot = constant(0.01f, ms(100));
    CHECK(mixer.play(shot, {.group = Group::Weapons}).valid());
    CHECK(mixer.voicesOf(shot) == 1);
    CHECK_FALSE(mixer.setLoop(loops.front(), 1.0f, 0.0f));
    CHECK(mixer.setLoop(loops.back(), 1.0f, 0.0f));
    // A full mixer of weapons voices drops a world loop: nothing lower to take.
    mixer.stopAll();
    std::deque<Clip> shots;
    for (std::size_t i = 0; i < Mixer::MAX_VOICES; ++i)
        mixer.play(shots.emplace_back(constant(0.01f, ms(100))), {.group = Group::Weapons});
    CHECK_FALSE(mixer.startLoop(hum, {.gain = 1.0f}).valid());
}

TEST_CASE("UTA-0480: loops keep UTA-0056 INV-8 and INV-9", "[uaudio]") {
    std::deque<Clip> clips;
    Mixer mixer;
    for (int v = 0; v < 64; ++v) {
        Clip& clip = clips.emplace_back();
        clip.rate = Mixer::RATE;
        clip.channels = 1;
        for (int i = 0; i < 48; ++i) clip.samples.push_back(i < 24 ? 1.0f : -1.0f);
        mixer.startLoop(clip, {.gain = 1.0f, .pan = v % 2 == 0 ? -1.0f : 1.0f});
    }
    std::vector<float> out(ms(100) * 2);
    g_allocations = 0;
    g_counting = true;
    mixer.mix(out);
    g_counting = false;
    if (UTA_COUNTS_ALLOCATIONS) CHECK(g_allocations == 0);
    float peak = 0;
    for (const float sample : out) peak = std::max(peak, std::abs(sample));
    CHECK(peak <= 1.0f);
    CHECK(peak > 0.5f); // 64 full-scale loops did reach the limiter
}

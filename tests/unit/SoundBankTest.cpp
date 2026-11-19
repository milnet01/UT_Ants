// UTA-0478 INV-8: SoundBank, on WAV payloads built here
// (docs/specs/UTA-0478-baked-sounds.md SS 4.5).
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "uaudio/SoundBank.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using uta::uaudio::Clip;
using uta::uaudio::SoundBank;

namespace {

void put(std::vector<std::byte>& out, std::string_view tag) {
    for (const char c : tag) out.push_back(static_cast<std::byte>(c));
}

void putLe(std::vector<std::byte>& out, std::uint32_t value, int width) {
    for (int i = 0; i < width; ++i) out.push_back(static_cast<std::byte>(value >> (8 * i) & 0xffu));
}

/// A mono 8-bit RIFF WAV at 22050 Hz holding `samples`; `tag` 1 is PCM.
std::vector<std::byte> wav(std::vector<unsigned> samples, std::uint16_t tag = 1) {
    std::vector<std::byte> body;
    put(body, "WAVE");
    put(body, "fmt ");
    putLe(body, 16, 4);
    putLe(body, tag, 2);
    putLe(body, 1, 2);
    putLe(body, 22050, 4);
    putLe(body, 22050, 4);
    putLe(body, 1, 2);
    putLe(body, 8, 2);
    put(body, "data");
    putLe(body, static_cast<std::uint32_t>(samples.size()), 4);
    for (const unsigned sample : samples) body.push_back(static_cast<std::byte>(sample));
    if (samples.size() % 2 != 0) body.push_back(std::byte{0});
    std::vector<std::byte> out;
    put(out, "RIFF");
    putLe(out, static_cast<std::uint32_t>(body.size()), 4);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

} // namespace

TEST_CASE("UTA-0478 INV-8: find returns the clip decoded from the bytes added under that path", "[uaudio]") {
    SoundBank bank;
    REQUIRE(bank.add("ambient.hum", wav({0, 255})).has_value());
    REQUIRE(bank.add("doors.open", wav({255, 255, 255})).has_value());
    CHECK(bank.size() == 2);

    const Clip* const hum = bank.find("ambient.hum");
    REQUIRE(hum != nullptr);
    CHECK(hum->rate == 22050);
    REQUIRE(hum->frames() == 2);
    CHECK(hum->samples[0] == -1.0F);

    const Clip* const open = bank.find("doors.open");
    REQUIRE(open != nullptr);
    CHECK(open->frames() == 3);

    CHECK(bank.find("ambient.hu") == nullptr);
    CHECK(bank.find("") == nullptr);
}

TEST_CASE("UTA-0478 INV-8: a second add under a held path changes nothing", "[uaudio]") {
    SoundBank bank;
    REQUIRE(bank.add("ambient.hum", wav({0, 255})).has_value());
    const Clip* const first = bank.find("ambient.hum");
    REQUIRE(bank.add("ambient.hum", wav({255, 255, 255, 255})).has_value());
    CHECK(bank.size() == 1);
    CHECK(bank.find("ambient.hum") == first);
    CHECK(first->frames() == 2);
}

TEST_CASE("UTA-0478 INV-8: a payload decodeWav refuses is not held and its error comes back", "[uaudio]") {
    SoundBank bank;
    const auto refused = bank.add("voice.adpcm", wav({1, 2}, 17));
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(std::string(refused.error().message()), ContainsSubstring("17"));
    CHECK(bank.size() == 0);
    CHECK(bank.find("voice.adpcm") == nullptr);
}

TEST_CASE("UTA-0478 INV-8: a clip's pointer outlives a hundred later adds", "[uaudio]") {
    SoundBank bank;
    REQUIRE(bank.add("m.first", wav({0, 255})).has_value());
    const Clip* const first = bank.find("m.first");
    REQUIRE(first != nullptr);
    const float* const samples = first->samples.data();
    // Names sorting both sides of "m.first", so a sorted vector would move it.
    for (int i = 0; i < 100; ++i)
        REQUIRE(bank.add((i % 2 == 0 ? "a." : "z.") + std::to_string(i), wav({128})).has_value());
    CHECK(bank.size() == 101);
    CHECK(bank.find("m.first") == first);
    CHECK(first->samples.data() == samples);
    CHECK(first->samples[1] == 127.0F / 128.0F);
}

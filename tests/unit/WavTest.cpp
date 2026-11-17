// UTA-0056 INV-1: decodeWav, on WAV payloads built here
// (docs/specs/UTA-0056-audio-mixer.md SS 4.2).
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "uaudio/Wav.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using uta::uaudio::decodeWav;

namespace {

void put(std::vector<std::byte>& out, std::string_view tag) {
    for (const char c : tag) out.push_back(static_cast<std::byte>(c));
}

void putLe(std::vector<std::byte>& out, std::uint32_t value, int width) {
    for (int i = 0; i < width; ++i) out.push_back(static_cast<std::byte>(value >> (8 * i) & 0xffu));
}

struct Shape {
    std::uint16_t tag = 1;
    std::uint16_t channels = 1;
    std::uint32_t rate = 22050;
    std::uint16_t bits = 16;
    bool fmt = true;
    bool data = true;
    /// Added to the data chunk's declared size, to run it past the end.
    std::uint32_t dataOverrun = 0;
};

/// A RIFF WAV: a LIST chunk the decoder must skip, then fmt and data.
std::vector<std::byte> wav(const std::vector<std::byte>& payload, const Shape& shape = {}) {
    std::vector<std::byte> body;
    put(body, "WAVE");
    put(body, "LIST");
    putLe(body, 3, 4);
    put(body, "abc");
    body.push_back(std::byte{0}); // the pad byte of an odd chunk
    if (shape.fmt) {
        put(body, "fmt ");
        putLe(body, 16, 4);
        putLe(body, shape.tag, 2);
        putLe(body, shape.channels, 2);
        putLe(body, shape.rate, 4);
        putLe(body, shape.rate * shape.channels * shape.bits / 8u, 4);
        putLe(body, shape.channels * shape.bits / 8u, 2);
        putLe(body, shape.bits, 2);
    }
    if (shape.data) {
        put(body, "data");
        putLe(body, static_cast<std::uint32_t>(payload.size()) + shape.dataOverrun, 4);
        body.insert(body.end(), payload.begin(), payload.end());
    }
    std::vector<std::byte> out;
    put(out, "RIFF");
    putLe(out, static_cast<std::uint32_t>(body.size()), 4);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> out;
    for (const unsigned v : values) out.push_back(static_cast<std::byte>(v));
    return out;
}

std::vector<std::byte> samples16(std::initializer_list<std::int16_t> values) {
    std::vector<std::byte> out;
    for (const std::int16_t v : values) putLe(out, static_cast<std::uint16_t>(v), 2);
    return out;
}

std::string refusal(const std::vector<std::byte>& payload) {
    const auto clip = decodeWav(payload);
    REQUIRE_FALSE(clip.has_value());
    return std::string(clip.error().message());
}

} // namespace

TEST_CASE("UTA-0056 INV-1: 8-bit samples are unsigned and centred on 128", "[uaudio]") {
    const auto mono = decodeWav(wav(bytes({0, 128, 255, 64}), {.bits = 8}));
    REQUIRE(mono.has_value());
    CHECK(mono->rate == 22050);
    CHECK(mono->channels == 1);
    REQUIRE(mono->frames() == 4);
    CHECK_THAT(mono->samples[0], WithinAbs(-1.0, 1e-6));
    CHECK_THAT(mono->samples[1], WithinAbs(0.0, 1e-6));
    CHECK_THAT(mono->samples[2], WithinAbs(127.0 / 128.0, 1e-6));
    CHECK_THAT(mono->samples[3], WithinAbs(-0.5, 1e-6));

    const auto stereo = decodeWav(wav(bytes({0, 255, 192, 128}), {.channels = 2, .rate = 11025, .bits = 8}));
    REQUIRE(stereo.has_value());
    CHECK(stereo->channels == 2);
    CHECK(stereo->rate == 11025);
    REQUIRE(stereo->frames() == 2);
    CHECK_THAT(stereo->samples[0], WithinAbs(-1.0, 1e-6));
    CHECK_THAT(stereo->samples[1], WithinAbs(127.0 / 128.0, 1e-6));
    CHECK_THAT(stereo->samples[2], WithinAbs(0.5, 1e-6));
    CHECK_THAT(stereo->samples[3], WithinAbs(0.0, 1e-6));
}

TEST_CASE("UTA-0056 INV-1: 16-bit samples are signed over 32768", "[uaudio]") {
    const auto stereo = decodeWav(wav(samples16({-32768, 32767, 0, 16384, -8192, 1}), {.channels = 2, .rate = 44100}));
    REQUIRE(stereo.has_value());
    CHECK(stereo->channels == 2);
    REQUIRE(stereo->frames() == 3);
    CHECK_THAT(stereo->samples[0], WithinAbs(-1.0, 1e-6));
    CHECK_THAT(stereo->samples[1], WithinAbs(32767.0 / 32768.0, 1e-6));
    CHECK_THAT(stereo->samples[2], WithinAbs(0.0, 1e-6));
    CHECK_THAT(stereo->samples[3], WithinAbs(0.5, 1e-6));
    CHECK_THAT(stereo->samples[4], WithinAbs(-0.25, 1e-6));
    CHECK_THAT(stereo->samples[5], WithinAbs(1.0 / 32768.0, 1e-9));

    const auto mono = decodeWav(wav(samples16({-16384, 8192})));
    REQUIRE(mono.has_value());
    REQUIRE(mono->frames() == 2);
    CHECK_THAT(mono->samples[0], WithinAbs(-0.5, 1e-6));
    CHECK_THAT(mono->samples[1], WithinAbs(0.25, 1e-6));
}

TEST_CASE("UTA-0056 INV-1: a compressed format is refused by its tag", "[uaudio]") {
    // IMA ADPCM, as four stock sounds are. Its bytes would read as PCM noise.
    CHECK_THAT(refusal(wav(samples16({1, 2, 3, 4}), {.tag = 17})), ContainsSubstring("format tag 17"));
    CHECK_THAT(refusal(wav(samples16({1, 2}), {.tag = 85})), ContainsSubstring("format tag 85"));
}

TEST_CASE("UTA-0056 INV-1: each malformed shape is refused with its reason", "[uaudio]") {
    CHECK_THAT(refusal(wav(samples16({1}), {.fmt = false})), ContainsSubstring("no fmt chunk"));
    CHECK_THAT(refusal(wav(samples16({1}), {.data = false})), ContainsSubstring("no data chunk"));
    CHECK_THAT(refusal(wav(samples16({1, 2}), {.dataOverrun = 2})), ContainsSubstring("running past its end"));
    CHECK_THAT(refusal(wav(samples16({1, 2, 3}), {.channels = 3})), ContainsSubstring("3 channels"));
    CHECK_THAT(refusal(wav(bytes({1, 2, 3}), {.bits = 24})), ContainsSubstring("24-bit"));
    CHECK_THAT(refusal(wav(samples16({1}), {.rate = 3999})), ContainsSubstring("3999 Hz"));
    CHECK_THAT(refusal(bytes({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' '})),
               ContainsSubstring("RIFF WAVE header"));
}

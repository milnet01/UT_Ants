// Locks UTA-0202: the one JSON escaper the tools write with cannot emit a
// string that is not valid UTF-8, whatever bytes it is handed.
//
// UE1 stores a name as 8-bit bytes with no declared encoding, and upkg passes
// them through untouched. JSON is UTF-8 by definition (RFC 8259 SS 8.1), so a
// writer that copies such a byte produces a file a strict reader cannot open
// at all -- it raises before parsing, losing the whole run rather than the one
// line. Measured over the install's Maps directory: 11 of 1443 --ndjson lines,
// the first a texture named `Telarana` carrying 0xf1 for the n-tilde.
//
// The repair rule is not new. UTA-0101 measured it over the same library for
// a map's free text: keep bytes that are already valid UTF-8, else decode as
// Windows-1252, falling back to Latin-1 for the bytes Windows-1252 leaves
// undefined. UTA-0202 moves that rule INSIDE writeJsonString, so a call site
// cannot pick the unsafe writer -- which is how the defect arose.
//
// THE PROPERTY CASE IS THE ONE THAT MATTERS. The others are worked examples of
// it. It uses isUtf8 as its oracle, so isUtf8 is graded first against vectors
// of its own -- otherwise a broken oracle would pass itself.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest.cpp
// says why.

#include "common/Json.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <string_view>

namespace {

using uta::tools::isUtf8;
using uta::tools::writeJsonString;

/// `text` through the escaper, quotes and all.
std::string written(std::string_view text) {
    std::ostringstream out;
    writeJsonString(out, text);
    return out.str();
}

/// The escaped payload without its surrounding quotes.
std::string payload(std::string_view text) {
    const std::string whole = written(text);
    REQUIRE(whole.size() >= 2);
    REQUIRE(whole.front() == '"');
    REQUIRE(whole.back() == '"');
    return whole.substr(1, whole.size() - 2);
}

} // namespace

TEST_CASE("UTA-0202: isUtf8 accepts well-formed sequences of every length") {
    CHECK(isUtf8(""));
    CHECK(isUtf8("plain ascii"));
    CHECK(isUtf8("\xc3\xb1"));                 // U+00F1, two bytes
    CHECK(isUtf8("\xe2\x82\xac"));             // U+20AC, three bytes
    CHECK(isUtf8("\xf0\x9f\x92\xa9"));         // U+1F4A9, four bytes
    CHECK(isUtf8("\xf4\x8f\xbf\xbf"));         // U+10FFFF, the last code point
}

TEST_CASE("UTA-0202: isUtf8 rejects the ill-formed sequences that matter here") {
    CHECK_FALSE(isUtf8("\xf1"));               // a bare Latin-1 n-tilde
    CHECK_FALSE(isUtf8("Telara\xf1" "a"));     // the texture name that was measured
    CHECK_FALSE(isUtf8("\x80"));               // a continuation byte with no lead
    CHECK_FALSE(isUtf8("\xc3"));               // a lead byte that is truncated
    CHECK_FALSE(isUtf8("\xc0\xaf"));           // overlong encoding of '/'
    CHECK_FALSE(isUtf8("\xed\xa0\x80"));       // U+D800, a surrogate
    CHECK_FALSE(isUtf8("\xf5\x80\x80\x80"));   // past U+10FFFF
}

TEST_CASE("UTA-0202: text that is already valid UTF-8 is passed through byte for byte") {
    CHECK(payload("plain ascii") == "plain ascii");
    CHECK(payload("\xc3\xb1") == "\xc3\xb1");
    CHECK(payload("\xe2\x82\xac") == "\xe2\x82\xac");
}

TEST_CASE("UTA-0202: JSON's required escapes and the C0 range are still escaped") {
    CHECK(payload("a\"b") == "a\\\"b");
    CHECK(payload("a\\b") == "a\\\\b");
    CHECK(payload("a\nb") == "a\\nb");
    CHECK(payload("a\rb") == "a\\rb");
    CHECK(payload("a\tb") == "a\\tb");
    CHECK(payload(std::string_view{"a\0b", 3}) == "a\\u0000b");
    CHECK(payload("a\x1f" "b") == "a\\u001fb");
}

TEST_CASE("UTA-0202: the measured texture name comes out as UTF-8") {
    // 0xf1 is U+00F1 under Windows-1252 and Latin-1 alike, so both readings
    // agree here. UT_MonsterHunt's sweep reports 11 affected maps and did not
    // record which bytes the others carry, which is why the rule below is
    // general rather than a fix for this character.
    CHECK(payload("Telara\xf1" "a") == "Telara\xc3\xb1" "a");
}

TEST_CASE("UTA-0202: a byte Windows-1252 defines is decoded as Windows-1252") {
    CHECK(payload("\x80") == "\xe2\x82\xac");          // euro sign, NOT U+0080
    CHECK(payload("\x93") == "\xe2\x80\x9c");          // left double quotation mark
    CHECK(payload("\x99") == "\xe2\x84\xa2");          // trade mark sign
}

TEST_CASE("UTA-0202: a byte Windows-1252 leaves undefined falls back to Latin-1") {
    CHECK(payload("\x81") == "\xc2\x81");
    CHECK(payload("\x8d") == "\xc2\x8d");
    CHECK(payload("\x9d") == "\xc2\x9d");
}

TEST_CASE("UTA-0202: a string is repaired whole rather than per byte") {
    // The rule is all-or-nothing by design, inherited from UTA-0101. A string
    // mixing a valid UTF-8 sequence with a stray 8-bit byte is not valid UTF-8,
    // so the WHOLE string is decoded and the valid part reads as mojibake.
    //
    // That is deliberate and was measured. A UE1 name is 8-bit throughout, so a
    // name that happens to parse as UTF-8 is a coincidence rather than an
    // intent, and decoding byte-wise would corrupt the common case to flatter
    // the rare one. This case exists so the trade-off is a recorded choice.
    CHECK(payload("\xc3\xa9\xf1") == "\xc3\x83\xc2\xa9\xc3\xb1");
}

TEST_CASE("UTA-0202: no single byte can make the escaper emit invalid UTF-8") {
    for (int value = 0; value <= 0xff; ++value) {
        const auto raw = static_cast<char>(static_cast<unsigned char>(value));
        const std::string one{raw};
        const std::string out = written(one);
        INFO("byte 0x" << std::hex << value);
        CHECK(isUtf8(out));
    }
}

TEST_CASE("UTA-0202: no byte pair can make the escaper emit invalid UTF-8") {
    // Every lead-plus-continuation combination, which is where a half-decoded
    // sequence would show up. 65536 strings, each cheap.
    for (int first = 0; first <= 0xff; ++first) {
        for (int second = 0; second <= 0xff; ++second) {
            std::string pair;
            pair += static_cast<char>(static_cast<unsigned char>(first));
            pair += static_cast<char>(static_cast<unsigned char>(second));
            if (!isUtf8(written(pair))) {
                INFO("bytes 0x" << std::hex << first << " 0x" << second);
                FAIL("the escaper emitted invalid UTF-8");
            }
        }
    }
    SUCCEED("every byte pair emitted valid UTF-8");
}

// The compact index is the encoding every count, length and object reference
// in an Unreal package is written in, so a misreading of it corrupts
// everything downstream. These vectors are hand-computed from the format
// rather than captured from this code, which is the whole point: a test built
// from the implementation's own output can only prove it is consistent.

#include "support/UnrealPackageBuilder.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using uta::test::encodeCompactIndex;
using Bytes = std::vector<std::uint8_t>;

TEST_CASE("small non-negative values fit in the first byte's six value bits", "[compact-index]") {
    CHECK(encodeCompactIndex(0) == Bytes{0x00});
    CHECK(encodeCompactIndex(1) == Bytes{0x01});
    CHECK(encodeCompactIndex(63) == Bytes{0x3F});
}

TEST_CASE("the sign lives in bit 7 of the first byte and nowhere else", "[compact-index]") {
    CHECK(encodeCompactIndex(-1) == Bytes{0x81});
    CHECK(encodeCompactIndex(-63) == Bytes{0xBF});
}

TEST_CASE("bit 6 of the first byte continues into a second", "[compact-index]") {
    // 64 does not fit in six bits: the low six are zero, the continuation bit
    // is set, and the remaining 1 is the whole of the second byte.
    CHECK(encodeCompactIndex(64) == Bytes{0x40, 0x01});
    CHECK(encodeCompactIndex(-64) == Bytes{0xC0, 0x01});
}

TEST_CASE("later bytes carry seven value bits each", "[compact-index]") {
    // 8191 = 0b1111111111111. Six bits (0x3F) in byte one, then seven more.
    CHECK(encodeCompactIndex(8191) == Bytes{0x7F, 0x7F});
    // 8192 needs a third byte: 6 + 7 bits hold only 8191.
    CHECK(encodeCompactIndex(8192) == Bytes{0x40, 0x80, 0x01});
}

TEST_CASE("the extremes encode without overflowing", "[compact-index]") {
    // INT32_MIN is the case that catches a negation done in 32 bits.
    const Bytes encoded = encodeCompactIndex(-2147483647 - 1);
    REQUIRE(encoded.size() == 5);
    CHECK((encoded[0] & 0x80u) != 0);      // negative
    CHECK((encoded.back() & 0x80u) == 0);  // and terminated

    CHECK(encodeCompactIndex(2147483647).size() == 5);
}

TEST_CASE("every encoding terminates, and the two uses of bit 7 do not collide",
          "[compact-index]") {
    // Bit 7 means different things in different positions: in the first byte
    // it is the sign, and continuation is bit 6; in every later byte it is
    // continuation. Reading the first byte by the later-byte rule is how a
    // decoder mistakes -1 (0x81, complete) for an unterminated sequence.
    for (std::int32_t value : {0, 1, -1, 63, -63, 64, -64, 1000, -1000, 70000, -70000}) {
        const Bytes encoded = encodeCompactIndex(value);
        REQUIRE_FALSE(encoded.empty());
        REQUIRE(encoded.size() <= 5);

        const bool continues = (encoded[0] & 0x40u) != 0;
        CHECK(continues == (encoded.size() > 1));

        CHECK(((encoded[0] & 0x80u) != 0) == (value < 0));

        if (encoded.size() > 1) {
            CHECK((encoded.back() & 0x80u) == 0);
            for (std::size_t i = 1; i + 1 < encoded.size(); ++i) {
                CHECK((encoded[i] & 0x80u) != 0);
            }
        }
    }
}

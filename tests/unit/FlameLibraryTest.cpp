// UTA-0263 SS 4.1's flame list: the lookup, and the digest that ties an edit
// to a baker revision. INV-3, that the list agrees with the labels, is a
// real-asset test in tests/real/RealFlamesTest.cpp.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "umat/Flames.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using uta::umat::FlameEntry;
using uta::umat::flameLibrary;
using uta::umat::isFlame;

TEST_CASE("the flame list finds each entry and no neighbour of one", "[umat][flames]") {
    REQUIRE_FALSE(flameLibrary().empty());
    for (const FlameEntry& entry : flameLibrary()) {
        INFO(entry.note);
        CHECK(isFlame(entry.fingerprint));
        // No two entries are adjacent integers, so each neighbour is absent.
        CHECK_FALSE(isFlame(entry.fingerprint + 1));
        CHECK_FALSE(isFlame(entry.fingerprint - 1));
    }
}

TEST_CASE("the flame list is the one a baker revision was recorded against", "[umat][flames]") {
    // FNV-1a 64 over each fingerprint as 8 bytes little-endian, in order.
    std::uint64_t digest = 0xcbf29ce484222325ULL;
    for (const FlameEntry& entry : flameLibrary())
        for (int shift = 0; shift < 64; shift += 8)
            digest = (digest ^ static_cast<std::uint8_t>(entry.fingerprint >> shift)) * 0x100000001b3ULL;
    // A changed list changes what a bake writes, and a cached bake is reused
    // until BAKER_REVISION moves. So: bump src/ubake/Name.h's BAKER_REVISION,
    // then record the new digest here.
    CHECK(digest == 0x57c5ec8ff00dbf27ULL);
}

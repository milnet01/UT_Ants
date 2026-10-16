// UTA-0055 SS 4.6's cloth list: the lookup, and the digest that ties an edit
// to a baker revision. INV-10's other half, that the list agrees with the
// labels, is a real-asset test in tests/real/RealClothTest.cpp.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "umat/Cloth.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using uta::umat::clothLibrary;
using uta::umat::ClothEntry;
using uta::umat::isCloth;

TEST_CASE("the cloth list finds each entry and no neighbour of one", "[umat][cloth]") {
    REQUIRE_FALSE(clothLibrary().empty());
    for (const ClothEntry& entry : clothLibrary()) {
        INFO(entry.note);
        CHECK(isCloth(entry.fingerprint));
        // No two entries are adjacent integers, so each neighbour is absent.
        CHECK_FALSE(isCloth(entry.fingerprint + 1));
        CHECK_FALSE(isCloth(entry.fingerprint - 1));
    }
}

TEST_CASE("the cloth list is the one a baker revision was recorded against", "[umat][cloth]") {
    // FNV-1a 64 over each fingerprint as 8 bytes little-endian, in order.
    std::uint64_t digest = 0xcbf29ce484222325ULL;
    for (const ClothEntry& entry : clothLibrary())
        for (int shift = 0; shift < 64; shift += 8)
            digest = (digest ^ static_cast<std::uint8_t>(entry.fingerprint >> shift)) * 0x100000001b3ULL;
    // A changed list changes what a bake writes, and a cached bake is reused
    // until BAKER_REVISION moves. So: bump src/ubake/Name.h's BAKER_REVISION,
    // then record the new digest here.
    CHECK(digest == 0xcddc21f04746e4deULL);
}

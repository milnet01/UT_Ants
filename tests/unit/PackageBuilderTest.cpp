// These assert the fixture builder produces a package whose header agrees
// with its own contents. A fixture that lies is worse than no fixture: every
// test built on it goes green while proving nothing, and the reader has no
// way to tell.

#include "support/UnrealPackageBuilder.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using uta::test::UnrealPackageBuilder;

namespace {

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    REQUIRE(offset + 4 <= bytes.size());
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[offset + static_cast<std::size_t>(i)]) << (8 * i);
    }
    return value;
}

constexpr std::size_t NAME_COUNT_OFFSET = 12;
constexpr std::size_t NAME_TABLE_OFFSET = 16;

} // namespace

TEST_CASE("an empty package still opens with the signature and its version", "[package-builder]") {
    const std::vector<std::uint8_t> bytes = UnrealPackageBuilder{}.build();

    CHECK(readU32(bytes, 0) == UnrealPackageBuilder::SIGNATURE);
    CHECK(bytes[4] == 68);  // package version, little-endian low byte
    CHECK(bytes[5] == 0);
    CHECK(readU32(bytes, NAME_COUNT_OFFSET) == 0);
}

TEST_CASE("the header's name count matches the names actually written", "[package-builder]") {
    UnrealPackageBuilder builder;
    builder.addName("Core").addName("Engine").addName("DM-Deck16");

    const std::vector<std::uint8_t> bytes = builder.build();
    CHECK(readU32(bytes, NAME_COUNT_OFFSET) == 3);
}

TEST_CASE("the name-table offset points at the first entry's length prefix", "[package-builder]") {
    UnrealPackageBuilder builder;
    builder.addName("Core");

    const std::vector<std::uint8_t> bytes = builder.build();
    const std::uint32_t tableOffset = readU32(bytes, NAME_TABLE_OFFSET);

    REQUIRE(tableOffset < bytes.size());
    // "Core" plus its terminating null is five characters, and five fits in
    // the first byte's six value bits.
    CHECK(bytes[tableOffset] == 0x05);
    CHECK(bytes[tableOffset + 1] == 'C');
    CHECK(bytes[tableOffset + 5] == 0x00);
}

TEST_CASE("a name too long for one index byte still round-trips its length", "[package-builder]") {
    // 64 characters plus a null is 65, which needs the continuation bit --
    // the boundary a fixture with only short names never exercises.
    UnrealPackageBuilder builder;
    builder.addName(std::string(64, 'A'));

    const std::vector<std::uint8_t> bytes = builder.build();
    const std::uint32_t tableOffset = readU32(bytes, NAME_TABLE_OFFSET);

    CHECK(bytes[tableOffset] == 0x41);      // 65 & 0x3F = 1, plus the continuation bit
    CHECK(bytes[tableOffset + 1] == 0x01);
}

TEST_CASE("the export and import offsets sit past the name table", "[package-builder]") {
    UnrealPackageBuilder builder;
    builder.addName("Core").addName("Engine");

    const std::vector<std::uint8_t> bytes = builder.build();
    const std::uint32_t nameOffset = readU32(bytes, NAME_TABLE_OFFSET);
    const std::uint32_t exportOffset = readU32(bytes, 24);
    const std::uint32_t importOffset = readU32(bytes, 32);

    CHECK(exportOffset > nameOffset);
    CHECK(exportOffset == importOffset);      // both tables are empty here
    CHECK(exportOffset == bytes.size());
}

// The compiled-script walker.
//
// Locks INV-3 (MalformedData on an unrecognised opcode, on a walk that
// overshoots ScriptSize, and on a negative ScriptSize) and INV-4 (memory
// widths come from fixed constants, so the walk does not depend on the host's
// pointer size).
//
// Why the walker exists at all: ScriptSize is the only length the file
// stores and it counts the bytes the script occupies IN MEMORY, while an
// object reference or a name is a compact index on disk. So the disk position
// a walk ends at is NOT ScriptSize bytes on, and every case below states both
// numbers -- what the instruction costs in memory, and where the cursor
// should land. docs/specs/UTA-0005-class-tables-and-ancestry.md SS 4.4.

#include "support/UnrealPackageBuilder.h"
#include "upkg/ByteReader.h"
#include "upkg/Package.h"
#include "upkg/Script.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using uta::ErrorCode;
using uta::test::asBytes;
using uta::test::UnrealPackageBuilder;
using uta::upkg::ByteReader;
using uta::upkg::Package;
using uta::upkg::skipScript;

namespace {

/// A package whose only job is to own a name table the walker can resolve a
/// label against. The script bytes are handed to the reader separately, so
/// each case below isolates the walk and nothing else.
std::vector<std::uint8_t> hostPackage() {
    UnrealPackageBuilder builder;
    builder.addName("None");
    return builder.build();
}

void append(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& more) {
    out.insert(out.end(), more.begin(), more.end());
}

} // namespace

TEST_CASE("a script of one no-operand instruction walks to its end") {
    const std::vector<std::uint8_t> packageBytes = hostPackage();
    const auto package = Package::open(asBytes(packageBytes));
    REQUIRE(package.has_value());

    const std::vector<std::uint8_t> body = uta::test::script::nothing();
    ByteReader reader{asBytes(body)};

    // EX_Nothing costs one memory byte, and one disk byte.
    REQUIRE(skipScript(*package, reader, 1).has_value());
    CHECK(reader.position() == 1);
}

TEST_CASE("INV-4: an object reference costs four memory bytes, not the host's pointer size") {
    const std::vector<std::uint8_t> packageBytes = hostPackage();
    const auto package = Package::open(asBytes(packageBytes));
    REQUIRE(package.has_value());

    // EX_ObjectConst with a small reference: one opcode byte plus a
    // single-byte compact index on disk, against one plus FOUR in memory.
    const std::vector<std::uint8_t> body = uta::test::script::objectConst(1);
    REQUIRE(body.size() == 2);

    ByteReader reader{asBytes(body)};
    const auto walked = skipScript(*package, reader, 5);

    // Asserting the literal disk position as well as success is what catches a
    // wrong width that happens to sum to ScriptSize anyway. A walker charging
    // sizeof(void*) reaches nine on the 64-bit hosts this project builds on,
    // steps past five, and is refused by the exactness rule instead.
    REQUIRE(walked.has_value());
    CHECK(reader.position() == 2);
}

TEST_CASE("INV-3: an unrecognised opcode is MalformedData") {
    const std::vector<std::uint8_t> packageBytes = hostPackage();
    const auto package = Package::open(asBytes(packageBytes));
    REQUIRE(package.has_value());

    const std::vector<std::uint8_t> body = uta::test::script::unknownOpcode();
    ByteReader reader{asBytes(body)};

    const auto walked = skipScript(*package, reader, 1);
    REQUIRE_FALSE(walked.has_value());
    CHECK(walked.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("INV-3: a walk that cannot sum to ScriptSize exactly is MalformedData") {
    const std::vector<std::uint8_t> packageBytes = hostPackage();
    const auto package = Package::open(asBytes(packageBytes));
    REQUIRE(package.has_value());

    // One EX_ObjectConst costs five memory bytes, so no walk of this script
    // lands on four: the first instruction takes the total straight past it.
    // Nothing downstream would catch this -- a wrong script end still leaves a
    // property list that parses and ends where it should -- so the exactness
    // rule is the only check there is.
    const std::vector<std::uint8_t> body = uta::test::script::objectConst(1);
    ByteReader reader{asBytes(body)};

    const auto walked = skipScript(*package, reader, 4);
    REQUIRE_FALSE(walked.has_value());
    CHECK(walked.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("INV-3: a negative ScriptSize is MalformedData before the walk begins") {
    const std::vector<std::uint8_t> packageBytes = hostPackage();
    const auto package = Package::open(asBytes(packageBytes));
    REQUIRE(package.has_value());

    const std::vector<std::uint8_t> body = uta::test::script::nothing();
    ByteReader reader{asBytes(body)};

    const auto walked = skipScript(*package, reader, -1);
    REQUIRE_FALSE(walked.has_value());
    CHECK(walked.error().code() == ErrorCode::MalformedData);
    // Refused before reading: the cursor has not moved.
    CHECK(reader.position() == 0);
}

TEST_CASE("several instructions walk to the sum of their memory costs") {
    const std::vector<std::uint8_t> packageBytes = hostPackage();
    const auto package = Package::open(asBytes(packageBytes));
    REQUIRE(package.has_value());

    // EX_Nothing (1 memory, 1 disk), EX_ObjectConst (5 memory, 2 disk),
    // EX_IntConst (5 memory, 5 disk): eleven memory bytes across eight on
    // disk. The gap between the two totals is the whole reason this walker
    // exists rather than a seek.
    std::vector<std::uint8_t> body;
    append(body, uta::test::script::nothing());
    append(body, uta::test::script::objectConst(1));
    append(body, uta::test::script::intConst(7));
    REQUIRE(body.size() == 8);

    ByteReader reader{asBytes(body)};
    REQUIRE(skipScript(*package, reader, 11).has_value());
    CHECK(reader.position() == 8);
}

TEST_CASE("INV-5: a ScriptSize larger than the bytes available is MalformedData") {
    const std::vector<std::uint8_t> packageBytes = hostPackage();
    const auto package = Package::open(asBytes(packageBytes));
    REQUIRE(package.has_value());

    const std::vector<std::uint8_t> body = uta::test::script::nothing();
    ByteReader reader{asBytes(body)};

    // The walk is bounded by the reader's own span, which a caller builds over
    // one export -- so a corrupt ScriptSize runs out of bytes and fails rather
    // than reading on.
    const auto walked = skipScript(*package, reader, 4096);
    REQUIRE_FALSE(walked.has_value());
    CHECK(walked.error().code() == ErrorCode::MalformedData);
}

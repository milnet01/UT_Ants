// Hostile and broken input.
//
// Locks INV-1 (no entry point throws, terminates or reads outside its span,
// for any input bytes), INV-2 (no allocation is sized by an unchecked value
// from the file) and INV-8 (a serial range never escapes the caller's bytes).
//
// What checks INV-1's out-of-span clause is worth stating plainly: nothing
// does, today. The one sanitizer leg in scripts/ci.sh is ThreadSanitizer,
// which finds races rather than overruns, and CMakeLists.txt records why
// AddressSanitizer is not offered. So these cases prove the reader REFUSES
// bad input; that it refuses without ever having read past the end is
// currently unmeasured, and section 10 of the spec grades INV-1 on that.

#include "support/UnrealPackageBuilder.h"
#include "upkg/ByteReader.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::test::asBytes;
using uta::test::ExportEntry;
using uta::test::TaggedPropertyWriter;
using uta::test::UnrealPackageBuilder;
using uta::upkg::ByteReader;
using uta::upkg::Package;
using uta::upkg::readProperties;

namespace {

constexpr std::int32_t NAME_NONE = 0;
constexpr std::int32_t NAME_FIRST = 1;

/// A well-formed package with one export carrying a real property list -- the
/// thing every case below then damages.
std::vector<std::uint8_t> healthyPackage() {
    TaggedPropertyWriter writer;
    writer.addInt(NAME_FIRST, 4242).addBool(NAME_FIRST, true).addStr(NAME_FIRST, "Deck16");

    UnrealPackageBuilder builder;
    builder.addName("None").addName("FirstProperty");

    uta::test::ImportEntry import;
    import.classPackage = NAME_NONE;
    import.className = NAME_FIRST;
    import.objectName = NAME_FIRST;
    builder.addImport(import);

    ExportEntry entry;
    entry.objectName = NAME_FIRST;
    entry.objectClass = -1;
    entry.serialData = writer.build(NAME_NONE);
    builder.addExport(entry);

    return builder.build();
}

} // namespace

TEST_CASE("every truncation of a valid package is refused, never crashed on",
          "[package-malformed]") {
    // INV-1. Each prefix is a package that stops mid-field somewhere -- inside
    // a compact index, a name, a table entry, a property tag. The assertion is
    // not which error comes back but that one does, from every cut point.
    const std::vector<std::uint8_t> healthy = healthyPackage();
    REQUIRE(healthy.size() > 64);

    for (std::size_t length = 0; length < healthy.size(); ++length) {
        const std::vector<std::uint8_t> truncated(healthy.begin(),
                                                  healthy.begin() +
                                                      static_cast<std::ptrdiff_t>(length));
        const auto package = Package::open(asBytes(truncated));

        // A prefix can never be a whole package: every table sits at an offset
        // the header names, and cutting the file moves the end before it.
        REQUIRE_FALSE(package.has_value());
    }
}

TEST_CASE("the complete package the truncations are cut from does open",
          "[package-malformed]") {
    // Without this, every assertion above passes against a fixture that was
    // never valid, and the case proves nothing at all.
    const std::vector<std::uint8_t> healthy = healthyPackage();
    const auto package = Package::open(asBytes(healthy));

    REQUIRE(package.has_value());
    REQUIRE(package->exports().size() == 1);

    const auto properties = readProperties(*package, package->exports()[0]);
    REQUIRE(properties.has_value());
    CHECK(properties->size() == 3);
}

TEST_CASE("a single flipped byte never crashes the reader", "[package-malformed]") {
    // INV-1 again, from the other direction: truncation shortens the file,
    // this keeps the length and corrupts counts, offsets, sizes and type
    // codes in place. Either answer is correct; neither may be a crash.
    const std::vector<std::uint8_t> healthy = healthyPackage();

    for (std::size_t index = 0; index < healthy.size(); ++index) {
        for (std::uint8_t pattern : {std::uint8_t{0x00}, std::uint8_t{0xFF}}) {
            std::vector<std::uint8_t> damaged = healthy;
            damaged[index] = pattern;

            const auto package = Package::open(asBytes(damaged));
            if (!package.has_value()) {
                continue;
            }
            // It opened, so every table validated. Reading an object's
            // properties must still refuse rather than run off the end.
            for (const auto& entry : package->exports()) {
                const auto properties = readProperties(*package, entry);
                (void)properties;
            }
        }
    }
    SUCCEED("no input crashed the reader");
}

TEST_CASE("a header claiming more names than the file holds is refused",
          "[package-malformed]") {
    // INV-2. Four bytes ask for a hundred million entries; a reader that
    // reserves from the count before checking it asks for gigabytes.
    UnrealPackageBuilder builder;
    builder.addName("None");
    builder.overrideNameCount(100000000u);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().code() == ErrorCode::MalformedData);

    // The message, not just the code. Whether the refusal arrives BEFORE the
    // table is sized is otherwise unobservable from out here: delete the count
    // check and the read still fails, one reserve() of a hundred million
    // entries later. Naming the check in the message is what makes the
    // ordering testable, and this assertion is what fails if it is removed.
    CHECK(package.error().message().find("claims 100000000 entries") !=
          std::string_view::npos);
}

TEST_CASE("a header claiming more exports or imports than the file holds is refused",
          "[package-malformed]") {
    for (int which = 0; which < 2; ++which) {
        // The offset is overridden to a real one inside the file as well as
        // the count. An empty table's honest offset is the end of the file, so
        // without this the offset check refuses first and the count is never
        // reached -- which would test something other than INV-2.
        UnrealPackageBuilder builder;
        builder.addName("None");
        if (which == 0) {
            builder.overrideExportCount(100000000u).overrideExportOffset(16u);
        } else {
            builder.overrideImportCount(100000000u).overrideImportOffset(16u);
        }

        const std::vector<std::uint8_t> bytes = builder.build();
        const auto package = Package::open(asBytes(bytes));

        REQUIRE_FALSE(package.has_value());
        CHECK(package.error().code() == ErrorCode::MalformedData);
        CHECK(package.error().message().find("claims 100000000 entries") !=
              std::string_view::npos);
    }
}

TEST_CASE("a table offset outside the file is refused", "[package-malformed]") {
    UnrealPackageBuilder builder;
    builder.addName("None").addName("Core");
    builder.overrideNameOffset(0xFFFFFF00u);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("an export whose serial range overruns the file is refused at open",
          "[package-malformed]") {
    // INV-8. Validating the offset alone lets offset + size overflow or
    // overrun, and serialBytes then hands out a span past the caller's bytes.
    UnrealPackageBuilder builder;
    builder.addName("None").addName("Core");

    ExportEntry entry;
    entry.objectName = 1;
    entry.serialData = {0x01, 0x02, 0x03, 0x04};
    entry.serialSizeOverride = 0x7FFFFFFFu; // far past the end
    builder.addExport(entry);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("an export whose serial offset and size overflow is refused at open",
          "[package-malformed]") {
    // The addition itself is the hazard: a large offset plus a large size
    // wraps, and a range check written as offset + size <= end then passes.
    UnrealPackageBuilder builder;
    builder.addName("None").addName("Core");

    ExportEntry entry;
    entry.objectName = 1;
    entry.serialData = {0x01, 0x02, 0x03, 0x04};
    entry.serialOffsetOverride = 0x7FFFFFF0u;
    entry.serialSizeOverride = 0x7FFFFFF0u;
    builder.addExport(entry);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("a name whose declared length runs past the file is refused",
          "[package-malformed]") {
    // The name table is the first thing read at a header-named offset, and a
    // length prefix is the first attacker-controlled size in the file.
    UnrealPackageBuilder builder;
    builder.addName("None").addName("Core");

    std::vector<std::uint8_t> bytes = builder.build();
    // The name table's offset is the second header word after the name count.
    std::uint32_t nameOffset = 0;
    for (int i = 0; i < 4; ++i) {
        nameOffset |= static_cast<std::uint32_t>(bytes[16 + static_cast<std::size_t>(i)])
                      << (8 * i);
    }
    REQUIRE(nameOffset < bytes.size());
    // "None" plus its null is five, which fits the first byte's six value
    // bits; 0x3F asks for sixty-three characters that are not there.
    bytes[nameOffset] = 0x3F;

    const auto package = Package::open(asBytes(bytes));
    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("a ByteReader refuses every read past its span", "[package-malformed]") {
    // The one type that owns every bounds check, exercised directly: the
    // alternative to this being total is a bounds fix landing one call site at
    // a time afterwards.
    const std::vector<std::uint8_t> two{0x01, 0x02};
    ByteReader reader{asBytes(two)};

    CHECK(reader.readU32().has_value() == false);
    CHECK(reader.readI64().has_value() == false);
    CHECK(reader.readFloat().has_value() == false);
    CHECK(reader.readBytes(3).has_value() == false);
    CHECK(reader.seek(3).has_value() == false);
    CHECK(reader.skip(3).has_value() == false);
    // None of those moved the cursor.
    CHECK(reader.position() == 0);
    CHECK(reader.remaining() == 2);

    // And a read that fits still works, so the guard is not simply refusing
    // everything -- which would pass every assertion above.
    const auto value = reader.readU16();
    REQUIRE(value.has_value());
    CHECK(*value == 0x0201);
    CHECK(reader.remaining() == 0);
}

TEST_CASE("an empty span is readable and yields nothing", "[package-malformed]") {
    ByteReader reader{std::span<const std::byte>{}};
    CHECK(reader.remaining() == 0);
    CHECK_FALSE(reader.readU8().has_value());
    CHECK(reader.readBytes(0).has_value());
}

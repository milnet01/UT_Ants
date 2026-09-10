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
#include "upkg/Class.h"
#include "upkg/Geometry.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
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

void appendU8(std::vector<std::uint8_t>& into, std::uint8_t value) {
    into.push_back(value);
}

void appendU32(std::vector<std::uint8_t>& into, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        into.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void appendFloat(std::vector<std::uint8_t>& into, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(into, bits);
}

void appendIndex(std::vector<std::uint8_t>& into, std::int32_t value) {
    const std::vector<std::uint8_t> encoded = uta::test::encodeCompactIndex(value);
    into.insert(into.end(), encoded.begin(), encoded.end());
}

void appendVector(std::vector<std::uint8_t>& into, float x, float y, float z) {
    appendFloat(into, x);
    appendFloat(into, y);
    appendFloat(into, z);
}

/// A package holding one export whose bytes are `data`. The class arrives as
/// an import, as `healthyPackage` above does: a null `objectClass` reads as
/// "this export IS a class" and `readPropertyList` refuses it outright,
/// before `readModel` ever sees the count this fixture exists to exercise.
/// `packageVersion` is left alone unless a caller asks for one, so the number
/// the builder defaults to is stated in one place -- there -- rather than
/// restated here where it would drift.
std::vector<std::uint8_t> packageWithExport(const std::vector<std::uint8_t>& data,
                                            std::optional<std::uint16_t> packageVersion = {}) {
    UnrealPackageBuilder builder;
    if (packageVersion.has_value()) {
        builder.setPackageVersion(*packageVersion);
    }
    builder.addName("None").addName("FirstProperty");

    uta::test::ImportEntry import;
    import.classPackage = NAME_NONE;
    import.className = NAME_FIRST;
    import.objectName = NAME_FIRST;
    builder.addImport(import);

    ExportEntry entry;
    entry.objectName = NAME_FIRST;
    entry.objectClass = -1; // import 0
    entry.serialData = data;
    builder.addExport(entry);
    return builder.build();
}

/// A `Model` body (UTA-0069 SS 4.4) that declares `nodeCount` in the `Nodes`
/// table's index prefix and stops right there. `checkCount` refuses before
/// reading a single element, so nothing past this point is ever reached --
/// the two count-check fixtures need nothing else.
std::vector<std::uint8_t> modelDeclaringNodeCount(std::int32_t nodeCount) {
    std::vector<std::uint8_t> data = TaggedPropertyWriter{}.build(NAME_NONE);
    appendVector(data, 0.0F, 0.0F, 0.0F); // BoundingBox.min
    appendVector(data, 0.0F, 0.0F, 0.0F); // BoundingBox.max
    appendU8(data, 0);                    // BoundingBox.valid
    appendVector(data, 0.0F, 0.0F, 0.0F); // BoundingSphere centre
    appendFloat(data, 0.0F);              // BoundingSphere radius
    appendIndex(data, 0);                 // Vectors: empty
    appendIndex(data, 0);                 // Points: empty
    appendIndex(data, nodeCount);         // Nodes: the declared count
    return data;
}

/// A `Model` body (UTA-0069 SS 4.4) that is well-formed all the way up to
/// `Leaves` -- every table before it (Vectors, Points, Nodes, Surfs, Verts,
/// the zone run, `Polys`, LightMap, LightBits, Bounds, LeafHulls) declares
/// itself empty, so the cursor arrives at `Leaves` honestly rather than by
/// accident -- and then declares `leavesCount` there and stops. SS 4.1: a
/// non-zero count is a table this reader does not yet describe (SS 4.6), and
/// `readModel` refuses before ever reading `Lights` whenever the count
/// cannot be honoured, so nothing past this point is needed.
std::vector<std::uint8_t> modelDeclaringLeavesCount(std::int32_t leavesCount) {
    std::vector<std::uint8_t> data = TaggedPropertyWriter{}.build(NAME_NONE);
    appendVector(data, 0.0F, 0.0F, 0.0F); // BoundingBox.min
    appendVector(data, 0.0F, 0.0F, 0.0F); // BoundingBox.max
    appendU8(data, 0);                    // BoundingBox.valid
    appendVector(data, 0.0F, 0.0F, 0.0F); // BoundingSphere centre
    appendFloat(data, 0.0F);              // BoundingSphere radius
    appendIndex(data, 0);                 // Vectors: empty
    appendIndex(data, 0);                 // Points: empty
    appendIndex(data, 0);                 // Nodes: empty
    appendIndex(data, 0);                 // Surfs: empty
    appendIndex(data, 0);                 // Verts: empty
    appendU32(data, 0);                   // NumSharedSides -- a raw i32
    appendU32(data, 0);                   // NumZones -- a raw i32, zero records
    appendIndex(data, 0);                 // Polys: null
    appendIndex(data, 0);                 // LightMap: empty
    appendIndex(data, 0);                 // LightBits: empty
    appendIndex(data, 0);                 // Bounds: empty
    appendIndex(data, 0);                 // LeafHulls: empty
    appendIndex(data, leavesCount);       // Leaves: the declared count
    return data;
}

/// A complete, well-formed `Model` body: every table empty, both trailing
/// `i32` present. The reader consumes this exactly, which is what lets the
/// version test below vary the VERSION and nothing else. The encoder that
/// wrote it moved to tests/support/ for UTA-0011's fixtures.
std::vector<std::uint8_t> emptyModelBody() {
    return uta::test::ModelExportWriter{}.build();
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

TEST_CASE("a Model declaring more nodes than the export can hold is refused",
          "[package-malformed]") {
    // UTA-0069 INV-2, oversized: the same shape as the two header-count cases
    // above, applied to one of Model's eleven tables. The count is checked
    // against what is left before anything is reserved.
    //
    // Both assertions below name the OVERSIZE branch's own wording, and
    // neither is satisfied by the negative branch's message ("a Model
    // declares N nodes", with no suffix) -- a mutation that disables this
    // branch and lets the walk fall through to a per-element short read
    // produces neither substring, and a mutation that disables the OTHER
    // (negative) branch and lets it fall through to THIS one is caught by
    // the sibling test below, whose assertion this wording cannot satisfy.
    const std::vector<std::uint8_t> bytes =
        packageWithExport(modelDeclaringNodeCount(0x00FFFFFF));
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto model = uta::upkg::readModel(*package, package->exports()[0]);
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code() == ErrorCode::MalformedData);
    CHECK(model.error().message().find("more than the") != std::string_view::npos);
    CHECK(model.error().message().find("bytes remaining can hold") != std::string_view::npos);
    CHECK(model.error().message().find("nodes") != std::string_view::npos);
}

TEST_CASE("a Model declaring a negative node count is refused", "[package-malformed]") {
    // UTA-0069 INV-2, negative: readIndex takes the sign from bit 7 of the
    // first byte, so a negative count is encodable and must be refused
    // before any reserve. SS 4.7 names this as its own fixture, separate
    // from the oversized one, because a fixture holding only one leaves half
    // the invariant unexercised.
    //
    // The assertion is an EXACT match on the count<0 branch's own message --
    // "a Model declares -1 nodes", with no trailing text -- rather than a
    // substring the oversize branch's wording could also satisfy. That
    // branch's message for the same count is "a Model declares -1 nodes,
    // more than the ... bytes remaining can hold": a strict prefix of it, so
    // a `find` on either half would pass whichever branch actually fired.
    // A mutation that disables the count<0 branch and lets -1 fall through
    // to the oversize check (checkCount's static_cast<size_t>(-1) wraps to
    // SIZE_MAX, which is always "more than" any remaining) produces exactly
    // that longer message, and the exact-equality check below refuses it.
    const std::vector<std::uint8_t> bytes = packageWithExport(modelDeclaringNodeCount(-1));
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto model = uta::upkg::readModel(*package, package->exports()[0]);
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code() == ErrorCode::MalformedData);
    CHECK(model.error().message() == "a Model declares -1 nodes");
    CHECK(model.error().message().find("more than the") == std::string_view::npos);
}

TEST_CASE("a Model declaring more leaves than it holds is refused",
          "[package-malformed]") {
    // This locked SS 4.1's DESIGN refusal -- a populated `Leaves` was a table
    // the reader did not describe -- until 2026-09-08, when SS 4.6's element
    // was derived and the table became readable. What it locks now is the
    // count check (INV-2): the fixture declares one leaf and supplies no
    // bytes for it, and an element is eleven bytes at its smallest, so the
    // count cannot be honoured. The refusal is the same and its cause is not.
    //
    // `Leaves` sits between `LeafHulls` and `Lights` in SS 4.4's order, so
    // the fixture is well-formed everywhere before it (every earlier table
    // empty) so the cursor arrives there honestly rather than by an
    // upstream accident. The message assertion names "leaves" specifically,
    // so this cannot be satisfied by some earlier table refusing instead.
    const std::vector<std::uint8_t> bytes = packageWithExport(modelDeclaringLeavesCount(1));
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto model = uta::upkg::readModel(*package, package->exports()[0]);
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code() == ErrorCode::MalformedData);
    CHECK(model.error().message().find("leaves") != std::string_view::npos);
}

TEST_CASE("a Model below package version 62 is refused by name", "[package-malformed]") {
    // Measured 2026-09-07 over MH-SPNaliRescue.unr, the reference install's
    // only version-61 package: a Model there holds no inline BSP tables at
    // all. It holds six object references -- Vectors, Points, BspNodes,
    // BspSurfs, Verts, Polys -- behind a 37-byte prefix rather than 41, and
    // the class histogram corroborates it (one BspNodes, BspSurfs, Verts and
    // Polys export per Model, two Vectors). UTA-0072 is what reads that
    // layout; this reader must say so rather than blaming its first table for
    // a difference that is the whole shape of the export.
    //
    // The SAME bytes are read at both versions, so the only thing this can be
    // measuring is the version. Without the first half, a refusal that came
    // from a malformed body would look identical to the one under test.
    const std::vector<std::uint8_t> body = emptyModelBody();

    const std::vector<std::uint8_t> modern = packageWithExport(body);
    const auto modernPackage = Package::open(asBytes(modern));
    REQUIRE(modernPackage.has_value());
    REQUIRE(uta::upkg::readModel(*modernPackage, modernPackage->exports()[0]).has_value());

    const std::vector<std::uint8_t> old = packageWithExport(body, 61);
    const auto oldPackage = Package::open(asBytes(old));
    REQUIRE(oldPackage.has_value());

    const auto model = uta::upkg::readModel(*oldPackage, oldPackage->exports()[0]);
    REQUIRE_FALSE(model.has_value());
    // UnsupportedVersion rather than MalformedData: the bytes are not
    // malformed, they are a layout this reader does not describe.
    CHECK(model.error().code() == ErrorCode::UnsupportedVersion);
    CHECK(model.error().message().find("61") != std::string_view::npos);
    CHECK(model.error().message().find("separate exports") != std::string_view::npos);
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

// UTA-0005 INV-5: no reader reads outside its export's byte range, for any
// input bytes. A class whose ScriptSize runs past the export is the case that
// would otherwise drive an unbounded read.
TEST_CASE("a class whose ScriptSize exceeds its export is MalformedData") {
    uta::test::UnrealPackageBuilder builder;
    builder.addName("None");
    builder.addName("Runaway");

    uta::test::ClassExportWriter writer;
    writer.setFriendlyName(1)
        .setDefaults(uta::test::TaggedPropertyWriter{}.build(0))
        // The body is empty and the declared size is enormous, so the walk
        // runs out of bytes rather than reading on.
        .setScriptSizeOverride(1 << 20);

    uta::test::ExportEntry entry;
    entry.objectClass = 0;
    entry.objectName = 1;
    entry.serialData = writer.build(68);
    builder.addExport(entry);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = uta::upkg::Package::open(uta::test::asBytes(bytes));
    REQUIRE(package.has_value());

    const auto info = uta::upkg::readClass(*package, package->exports()[0]);
    REQUIRE_FALSE(info.has_value());
    CHECK(info.error().code() == uta::ErrorCode::MalformedData);
}

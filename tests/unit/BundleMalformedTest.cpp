// UTA-0008's malformed corpora: totality, the allocation bound, the
// structural rules and the section table.
//
// docs/specs/UTA-0008-bundle-container-and-origin.md, INV-1, INV-2, INV-3 and
// INV-11. The golden byte array and the writer are INV-4 to INV-9 and INV-12,
// and they live in tests/unit/BundleFormatTest.cpp.
//
// THE ENCODER BELOW DOES NOT VALIDATE, and that is why it exists. `write`
// refuses a bundle whose structures violate SS 4.9, so it cannot produce the
// bytes these cases need; every case here frames a file correctly and then
// states one structure `read` must refuse.
//
// THE TWO CORPORA GRADE DIFFERENT RULES -- INV-1. Truncating a valid bundle
// at every offset grades SS 4.4's TABLE rules: a truncation shortens the file,
// so the section-extent check rejects it before a payload byte is read and
// SS 4.2's per-read bound is never reached. Delete that bound and every
// truncation case still goes red, so on its own that corpus would report a
// mutation as killed that in fact survived. The cases that isolate SS 4.2
// have a table wholly consistent with the file size and a payload that runs
// out mid-element. This project's CLAUDE.md SS Build and test records the
// same failure from UTA-0007: the fixture was rejected by a rule other than
// the one under test, so the sanitizer had nothing to see.
//
// This is the file worth running under AddressSanitizer, in a build directory
// of its own -- CLAUDE.md SS Build and test. INV-1 and INV-2 are bounds
// properties: removing the check they name produces undefined behaviour
// rather than a wrong answer, which a plain test cannot grade.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::ubundle::read;
using uta::umap::INDEX_NONE;
using uta::umap::NO_ROOM;
using uta::umap::ZONE_REFUSED;

namespace {

/// Emits SS 4.2's primitives. Knows no layout and checks nothing.
using uta::testing::Bytes;

struct Section {
    std::string id;
    Bytes payload;
};

/// One header, one section table, the payloads. The table tiles the file, so
/// every case below fails on the ONE structure it states rather than on the
/// framing.
std::vector<std::byte> file(const std::vector<Section>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(5); // formatVersion -- 5 since UTA-0110 added PLAC and LITE
    out.u8(1); // origin: Authored
    out.u8(0); // kind: Map
    out.u16(0);
    out.u32(static_cast<std::uint32_t>(sections.size()));

    std::uint64_t offset = 16 + sections.size() * 24;
    for (const Section& section : sections) {
        out.id(section.id);
        out.u64(offset);
        out.u64(section.payload.size());
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.u8(0);
        offset += section.payload.size();
    }
    for (const Section& section : sections) out.append(section.payload);
    return out.data();
}

// ---------------------------------------------------------------------------
// The valid baseline each case mutates ONE rule of.
// ---------------------------------------------------------------------------

struct RoomParts {
    std::uint32_t roomCount = 1;
    std::uint32_t zoneIndex = 2;
    std::uint16_t floor = 0;
    std::uint32_t bandCount = 2;
    std::vector<float> bands = {-64.0F, 128.0F};
    std::vector<std::uint32_t> roomForZone = {NO_ROOM, NO_ROOM, 0};
    // iFront, iBack, iLeaf[0], iLeaf[1] for the one node.
    std::int32_t iFront = INDEX_NONE;
    std::int32_t iBack = INDEX_NONE;
    std::int32_t iLeaf0 = 0;
    std::int32_t iLeaf1 = 1;
    std::vector<std::uint8_t> leafZone = {2, ZONE_REFUSED};
};

/// A ROOM payload built from `parts`. Valid unless a caller changed one field.
Bytes roomPayload(const RoomParts& parts) {
    Bytes out;

    out.u32(parts.roomCount);
    for (std::uint32_t i = 0; i < parts.roomCount; ++i) {
        out.u32(parts.zoneIndex);
        out.u32(0);      // parts: no footprint -- UTA-0007 SS 4.4 produces these
        out.f32(-1.0F);  // minZ
        out.f32(1.0F);   // maxZ
        out.u32(1);      // floors
        out.u16(parts.floor);
    }

    out.u32(static_cast<std::uint32_t>(parts.bands.size()));
    for (const float band : parts.bands) out.f32(band);

    out.u32(static_cast<std::uint32_t>(parts.roomForZone.size()));
    for (const std::uint32_t entry : parts.roomForZone) out.u32(entry);

    out.u32(1); // one node
    out.f32(0.0F);
    out.f32(0.0F);
    out.f32(1.0F);
    out.f32(0.0F);
    out.i32(parts.iFront);
    out.i32(parts.iBack);
    out.i32(parts.iLeaf0);
    out.i32(parts.iLeaf1);
    out.u8(0);
    out.u8(0);

    out.u32(static_cast<std::uint32_t>(parts.leafZone.size()));
    for (const std::uint8_t zone : parts.leafZone) out.u8(zone);

    return out;
}

struct NavParts {
    std::vector<std::uint32_t> exportIndices = {3, 17};
    std::uint32_t firstEdge0 = 0;
    std::uint32_t edgeCount0 = 1;
    std::uint32_t firstEdge1 = 1;
    std::uint32_t edgeCount1 = 1;
    /// from and to per edge, in order.
    std::vector<std::uint32_t> edgeFrom = {0, 1};
    std::vector<std::uint32_t> edgeTo = {1, 0};
};

Bytes navPayload(const NavParts& parts) {
    Bytes out;

    out.u32(static_cast<std::uint32_t>(parts.exportIndices.size()));
    const std::uint32_t firsts[2] = {parts.firstEdge0, parts.firstEdge1};
    const std::uint32_t counts[2] = {parts.edgeCount0, parts.edgeCount1};
    for (std::size_t i = 0; i < parts.exportIndices.size(); ++i) {
        out.u32(parts.exportIndices[i]);
        out.str("Ax");
        out.u32(firsts[i]);
        out.u32(counts[i]);
    }

    out.u32(static_cast<std::uint32_t>(parts.edgeFrom.size()));
    for (std::size_t i = 0; i < parts.edgeFrom.size(); ++i) {
        out.u32(parts.edgeFrom[i]);
        out.u32(parts.edgeTo[i]);
        out.i32(1);
        out.i32(2);
        out.i32(3);
        out.i32(4);
        out.u8(0);
    }

    out.u32(0); // discardedEndpoints
    return out;
}

struct WiringParts {
    std::vector<std::uint32_t> exportIndices = {5, 13};
    /// firstOutgoing, outgoingCount, firstIncoming, incomingCount per node.
    ///
    /// `edges` is grouped by `from` and `incoming` by `to`, and the two
    /// groupings do NOT line up: edge 0 runs 0 -> 1 and edge 1 runs 1 -> 0, so
    /// node 0 owns edge 0 outgoing and edge 1 incoming.
    std::uint32_t runs[2][4] = {{0, 1, 0, 1}, {1, 1, 1, 1}};
    std::vector<std::uint32_t> edgeFrom = {0, 1};
    std::vector<std::uint32_t> edgeTo = {1, 0};
    /// Indices into the edge arrays above, giving `incoming`'s order: edge 1
    /// (to == 0) first, then edge 0 (to == 1).
    std::vector<std::size_t> incomingOrder = {1, 0};
    std::uint32_t danglingFrom = 1;
};

Bytes wiringPayload(const WiringParts& parts) {
    Bytes out;

    out.u32(static_cast<std::uint32_t>(parts.exportIndices.size()));
    for (std::size_t i = 0; i < parts.exportIndices.size(); ++i) {
        out.u32(parts.exportIndices[i]);
        out.str("tg");
        out.u32(parts.runs[i][0]);
        out.u32(parts.runs[i][1]);
        out.u32(parts.runs[i][2]);
        out.u32(parts.runs[i][3]);
    }

    out.u32(static_cast<std::uint32_t>(parts.edgeFrom.size()));
    for (std::size_t i = 0; i < parts.edgeFrom.size(); ++i) {
        out.u32(parts.edgeFrom[i]);
        out.u32(parts.edgeTo[i]);
        out.str("ev");
    }

    out.u32(static_cast<std::uint32_t>(parts.incomingOrder.size()));
    for (const std::size_t index : parts.incomingOrder) {
        out.u32(parts.edgeFrom[index]);
        out.u32(parts.edgeTo[index]);
        out.str("ev");
    }

    out.u32(1); // one dangling event
    out.u32(parts.danglingFrom);
    out.str("dg");

    return out;
}

/// The whole valid baseline -- every case below changes exactly one thing.
std::vector<std::byte> validFile() {
    return file({{"ROOM", roomPayload({})}, {"NAVG", navPayload({})}, {"WIRG", wiringPayload({})}});
}

/// `read` refused, with the code the failure table gives.
void refused(const std::vector<std::byte>& bytes,
             ErrorCode code = ErrorCode::MalformedData) {
    const auto result = read(bytes);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == code);
}

} // namespace

TEST_CASE("the baseline this file mutates is itself valid", "[ubundle]") {
    // Without this, a case below could pass because the baseline was already
    // broken -- the vacuous-fixture failure CLAUDE.md SS Build and test
    // records from UTA-0007. Each case then differs from a KNOWN-GOOD file by
    // one field.
    const auto result = read(validFile());
    REQUIRE(result.has_value());
    CHECK(result->rooms.has_value());
    CHECK(result->nav.has_value());
    CHECK(result->wiring.has_value());
}

TEST_CASE("truncating a valid bundle at every offset returns rather than crashes",
          "[ubundle]") {
    // INV-1's first corpus. It proves TOTALITY and nothing more: a truncation
    // shortens the file, so SS 4.4's section-extent check rejects it before a
    // payload byte is read.
    const std::vector<std::byte> whole = validFile();
    REQUIRE(whole.size() > 16);

    for (std::size_t length = 0; length < whole.size(); ++length) {
        const std::vector<std::byte> cut(whole.begin(), whole.begin() + static_cast<long>(length));
        const auto result = read(cut);
        // Every one is refused, and no input reaches past its own span.
        CHECK_FALSE(result.has_value());
    }

    // The untruncated file still reads, so the loop above is refusing
    // truncation rather than refusing this fixture.
    CHECK(read(whole).has_value());
}

TEST_CASE("a payload that runs out mid-element is refused inside its own section",
          "[ubundle]") {
    // INV-1's second corpus, and the one that isolates SS 4.2's per-read
    // bound. The section table is wholly consistent with the file size here,
    // so only that bound can reject these.
    SECTION("a vector count larger than the bytes left in its section") {
        Bytes payload;
        payload.u32(4); // four rooms, and nothing after this
        refused(file({{"ROOM", payload}}));
    }

    SECTION("a string length larger than the bytes left in its section") {
        // The section is deliberately LONG enough for the node count to pass:
        // a NavNode's minimum is sixteen bytes, so a shorter section is
        // rejected by the count bound and the string length is never reached.
        // Measured -- the first draft of this case was that shorter section
        // and graded a rule it did not name.
        Bytes payload;
        payload.u32(1);   // one nav node, and room for it
        payload.u32(3);   // exportIndex
        payload.u32(100); // a className of 100 bytes, with twelve left
        payload.u32(0);
        payload.u32(0);
        payload.u32(0);
        refused(file({{"NAVG", payload}}));
    }

    SECTION("a nested ring count larger than its section") {
        Bytes payload;
        payload.u32(1); // one room
        payload.u32(2); // zoneIndex
        payload.u32(1); // one footprint
        payload.u32(0); // outer: empty
        payload.u32(9); // nine holes, with no bytes for them
        refused(file({{"ROOM", payload}}));
    }
}

TEST_CASE("a count is never allowed to size an allocation before it is checked",
          "[ubundle]") {
    // INV-2, two cases, one per half of its Breaks when.
    SECTION("an enormous count") {
        // Grades the unvalidated-reserve half. It does NOT grade the
        // multiplication half: 0xFFFFFFFF * 20 wraps to 4294967276, still far
        // above the bytes remaining, so a multiplying reader rejects it too.
        Bytes payload;
        payload.u32(0xFFFFFFFFU);
        payload.u32(0); // a few bytes of section left, and nowhere near enough
        payload.u32(0);
        refused(file({{"ROOM", payload}}));
    }

    SECTION("a count whose product with the element size wraps small") {
        // 0xCCCCCCCD against a Room's 20-byte minimum wraps to 4, which a
        // MULTIPLYING check waves through and a DIVIDING check refuses. This
        // is the case that grades the division, and it is the whole reason
        // SS 4.2 rule 1 is written as one.
        Bytes payload;
        payload.u32(0xCCCCCCCDU);
        payload.u32(0); // eight bytes remain: 8 / 20 is 0, so the count fails
        payload.u32(0);
        refused(file({{"ROOM", payload}}));
    }
}

TEST_CASE("the section table is validated whole before any section is decoded",
          "[ubundle]") {
    // INV-11.
    SECTION("an undefined section id is refused rather than skipped") {
        // Under SS 4.3's exact version match there is no such thing as a file
        // from a later version this reader should tolerate, so an unknown id
        // means a corrupt or hand-edited file.
        //
        // The payload is a VALID WIRG one, so the id is the only thing wrong
        // with this file. A short payload would be rejected for running out
        // of bytes whether the id was checked or not.
        refused(file({{"XXXX", wiringPayload({})}}));
    }

    SECTION("the same id twice") {
        refused(file({{"NAVG", navPayload({})}, {"NAVG", navPayload({})}}));
    }

    SECTION("descriptors not in ascending offset order") {
        // The overlap pass is a single linear walk and DETECTS overlap only
        // in an ordered table. Without this case an implementer may drop the
        // ordering rule, keep the linear pass, and let an out-of-order table
        // with overlapping sections decode cleanly.
        //
        // MEASURED: with the tiling rule in place this case is graded by
        // tiling rather than by the ordering check -- an out-of-order table
        // cannot also have each section beginning where the last ended.
        // Deleting the ordering check alone leaves every case here green. It
        // is SS 4.4's rule and it stays; this note is what stops a later
        // session reading the case as proof of it.
        const Bytes room = roomPayload({});
        const Bytes nav = navPayload({});

        Bytes out;
        out.id("UTAB");
        out.u32(5); // formatVersion -- 5 since UTA-0110 added PLAC and LITE
        out.u8(1);
        out.u8(0);
        out.u16(0);
        out.u32(2);

        const std::uint64_t tableEnd = 16 + 2 * 24;
        // NAVG is described first while sitting second in the file.
        out.id("NAVG");
        out.u64(tableEnd + room.size());
        out.u64(nav.size());
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.id("ROOM");
        out.u64(tableEnd);
        out.u64(room.size());
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.u8(0);

        out.append(room);
        out.append(nav);
        refused(out.data());
    }

    SECTION("a section reaching past the end of the file") {
        std::vector<std::byte> bytes = validFile();
        // The first descriptor's `size` is eight bytes at offset 16 + 12.
        bytes[16 + 12] = static_cast<std::byte>(0xFF);
        bytes[16 + 13] = static_cast<std::byte>(0xFF);
        refused(bytes);
    }

    SECTION("a gap between two sections") {
        // Graded jointly by the tiling rule and the trailing-bytes rule: a gap
        // leaves the section sizes summing short of the file. The case above
        // is the one that isolates tiling.
        const Bytes room = roomPayload({});
        Bytes out;
        out.id("UTAB");
        out.u32(5); // formatVersion -- 5 since UTA-0110 added PLAC and LITE
        out.u8(1);
        out.u8(0);
        out.u16(0);
        out.u32(1);
        out.id("ROOM");
        out.u64(16 + 24 + 4); // four bytes of gap after the table
        out.u64(room.size());
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.u32(0); // the gap
        out.append(room);
        refused(out.data());
    }

    SECTION("bytes trailing the last section") {
        // Trailing bytes mean the layout was misread, not that there is
        // slack -- SS 4.4 makes the sections tile the file exactly.
        std::vector<std::byte> bytes = validFile();
        bytes.push_back(std::byte{0});
        refused(bytes);
    }

    SECTION("two sections whose ranges overlap") {
        const Bytes room = roomPayload({});
        const Bytes nav = navPayload({});
        Bytes out;
        out.id("UTAB");
        out.u32(5); // formatVersion -- 5 since UTA-0110 added PLAC and LITE
        out.u8(1);
        out.u8(0);
        out.u16(0);
        out.u32(2);

        const std::uint64_t tableEnd = 16 + 2 * 24;
        out.id("ROOM");
        out.u64(tableEnd);
        out.u64(room.size());
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.id("NAVG");
        out.u64(tableEnd + room.size() - 4); // four bytes back into ROOM
        out.u64(nav.size());
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.u8(0);

        out.append(room);
        out.append(nav);
        refused(out.data());
    }

    SECTION("two sections declaring the same offset") {
        // The ONE fixture the tiling rule alone rejects, and it took building
        // deliberately. A gap or a backwards overlap changes the sum of the
        // section sizes, so the trailing-bytes check catches those and the
        // tiling rule is never reached. Here both sections start at the end
        // of the table, their sizes still sum to the bytes after it, and each
        // decodes cleanly from all-zero bytes -- twenty of them are an empty
        // RoomMap and twelve are an empty NavGraph. Nothing but "each section
        // begins where the last ended" can see it.
        Bytes out;
        out.id("UTAB");
        out.u32(5); // formatVersion -- 5 since UTA-0110 added PLAC and LITE
        out.u8(1);
        out.u8(0);
        out.u16(0);
        out.u32(2);

        const std::uint64_t tableEnd = 16 + 2 * 24;
        out.id("ROOM");
        out.u64(tableEnd);
        out.u64(20); // five empty counts
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.id("NAVG");
        out.u64(tableEnd); // the same offset, not the end of ROOM
        out.u64(12);       // three empty counts
        out.u8(0);
        out.u8(0);
        out.u8(0);
        out.u8(0);

        for (int i = 0; i < 32; ++i) out.u8(0);
        refused(out.data());
    }

    SECTION("a section count that overruns the file") {
        std::vector<std::byte> bytes = validFile();
        // sectionCount sits at offset 12.
        bytes[12] = static_cast<std::byte>(0xAB);
        bytes[13] = static_cast<std::byte>(0xAA);
        bytes[14] = static_cast<std::byte>(0xAA);
        bytes[15] = static_cast<std::byte>(0xAA);
        // 24 * 0xAAAAAAAB wraps to 8 in 32 bits, so a multiplying bound
        // passes this on a file of any size and then walks 2 863 311 531
        // descriptors. SS 4.4 is written as a division for exactly this.
        refused(bytes);
    }

    SECTION("a section ending with bytes unread") {
        Bytes payload = navPayload({});
        payload.u32(0); // four bytes the layout does not account for
        refused(file({{"NAVG", payload}}));
    }
}

TEST_CASE("a ROOM section that decodes cleanly must still satisfy its own rules",
          "[ubundle]") {
    // INV-3, one case per rule in SS 4.9's ROOM list. Each of these decodes
    // without running out of bytes and describes a structure UTA-0007 forbids.
    SECTION("a roomForZone entry naming no room") {
        // The bad entry sits at zone 1, which NO room names, so the
        // table-agreement rule below still holds and only this bound can
        // reject it. Putting it at zone 2 -- the zone the room does name --
        // makes the agreement rule the thing that fires, and the case then
        // reports on a rule it did not mean to test.
        RoomParts parts;
        parts.roomForZone = {NO_ROOM, 7, 0};
        refused(file({{"ROOM", roomPayload(parts)}}));
    }

    SECTION("a non-empty roomForZone whose index 0 is not NO_ROOM") {
        RoomParts parts;
        parts.roomForZone = {0, NO_ROOM, 0};
        refused(file({{"ROOM", roomPayload(parts)}}));
    }

    SECTION("an empty roomForZone with no rooms is NOT an error") {
        // UTA-0007 SS 6 records two maps in the reference install whose zone
        // table is empty, and the result is a valid RoomMap with no rooms. A
        // rule requiring it non-empty would refuse a bundle they legitimately
        // produce.
        RoomParts parts;
        parts.roomCount = 0;
        parts.roomForZone = {};
        parts.iLeaf0 = INDEX_NONE;
        parts.iLeaf1 = INDEX_NONE;
        parts.leafZone = {};
        CHECK(read(file({{"ROOM", roomPayload(parts)}})).has_value());
    }

    SECTION("a room naming zone 0") {
        // MEASURED as graded by another rule and kept anyway. A room naming
        // zone 0 needs roomForZone[0] to be its own position for the
        // agreement rule to hold, and that is exactly what the index-0 rule
        // forbids -- so no fixture can isolate this rule, and deleting the
        // zone-zero check from the reader leaves every case here green. It is
        // SS 4.9's rule and it stays; this comment is what stops a later
        // session reading the case as proof of it.
        RoomParts parts;
        parts.zoneIndex = 0;
        refused(file({{"ROOM", roomPayload(parts)}}));
    }

    SECTION("a room whose zoneIndex is outside roomForZone") {
        RoomParts parts;
        parts.zoneIndex = 9;
        refused(file({{"ROOM", roomPayload(parts)}}));
    }

    SECTION("roomForZone and rooms disagreeing about who owns a zone") {
        RoomParts parts;
        parts.roomForZone = {NO_ROOM, 0, NO_ROOM}; // room 0 says zone 2
        refused(file({{"ROOM", roomPayload(parts)}}));
    }

    SECTION("two rooms naming one zone") {
        // Distinctness. The agreement rule above forces it -- both rooms
        // would have to be the single value roomForZone holds for that zone.
        RoomParts parts;
        parts.roomCount = 2;
        refused(file({{"ROOM", roomPayload(parts)}}));
    }

    SECTION("a room with no floors") {
        // Built by hand: roomPayload always writes one floor.
        Bytes payload;
        payload.u32(1);
        payload.u32(2); // zoneIndex
        payload.u32(0); // parts
        payload.f32(-1.0F);
        payload.f32(1.0F);
        payload.u32(0); // floors: empty
        payload.u32(1); // bands
        payload.f32(0.0F);
        payload.u32(3); // roomForZone
        payload.u32(NO_ROOM);
        payload.u32(NO_ROOM);
        payload.u32(0);
        payload.u32(0); // nodes
        payload.u32(0); // leafZone
        refused(file({{"ROOM", payload}}));
    }

    SECTION("a room naming a floor band that does not exist") {
        RoomParts parts;
        parts.floor = 5;
        refused(file({{"ROOM", roomPayload(parts)}}));
    }

    SECTION("bands out of ascending order") {
        RoomParts parts;
        parts.bands = {128.0F, -64.0F};
        refused(file({{"ROOM", roomPayload(parts)}}));
    }

    SECTION("a leafZone entry outside roomForZone") {
        RoomParts parts;
        parts.leafZone = {2, 99};
        refused(file({{"ROOM", roomPayload(parts)}}));
    }

    SECTION("a node child index out of range") {
        RoomParts parts;
        parts.iFront = 4; // one node exists
        refused(file({{"ROOM", roomPayload(parts)}}));

        RoomParts back;
        back.iBack = 4;
        refused(file({{"ROOM", roomPayload(back)}}));
    }

    SECTION("a node leaf index out of range") {
        RoomParts parts;
        parts.iLeaf0 = 9; // two leafZone entries exist
        refused(file({{"ROOM", roomPayload(parts)}}));

        RoomParts front;
        front.iLeaf1 = 9;
        refused(file({{"ROOM", roomPayload(front)}}));
    }

    SECTION("an out-of-range iZone byte is NOT refused") {
        // SS 4.9's one deliberate exception, and the difference from
        // uta::unav::edgesFrom. roomAt reaches iZone only through
        // roomForZone, which returns NO_ROOM for zone 0, for ZONE_REFUSED and
        // for any zone past the table -- so the accessor is already total
        // over this case (UTA-0007's INV-3), and a load-time refusal would
        // reject a bundle the runtime handles correctly.
        Bytes payload;
        payload.u32(0); // no rooms
        payload.u32(0); // bands
        payload.u32(0); // roomForZone: empty
        payload.u32(1); // one node
        payload.f32(0.0F);
        payload.f32(0.0F);
        payload.f32(1.0F);
        payload.f32(0.0F);
        payload.i32(INDEX_NONE);
        payload.i32(INDEX_NONE);
        payload.i32(INDEX_NONE);
        payload.i32(INDEX_NONE);
        payload.u8(200); // a zone far past an empty roomForZone
        payload.u8(201);
        payload.u32(0); // leafZone

        const auto result = read(file({{"ROOM", payload}}));
        REQUIRE(result.has_value());
        CHECK(result->rooms->nodes[0].iZone[0] == 200);
        CHECK(result->rooms->nodes[0].iZone[1] == 201);
    }
}

TEST_CASE("a NAVG section that decodes cleanly must still satisfy its own rules",
          "[ubundle]") {
    // INV-3, one case per rule in SS 4.9's NAVG list.
    SECTION("nodes not in strictly ascending exportIndex order") {
        // nodeOf relies on the order; an unsorted table makes it return
        // ANOTHER actor's edges rather than fail.
        NavParts parts;
        parts.exportIndices = {17, 3};
        refused(file({{"NAVG", navPayload(parts)}}));
    }

    SECTION("two nodes sharing an exportIndex") {
        NavParts parts;
        parts.exportIndices = {3, 3};
        refused(file({{"NAVG", navPayload(parts)}}));
    }

    SECTION("a node run reaching past the edge table") {
        // Deferring this to the accessors is what INV-3's Breaks when names:
        // uta::unav::edgesFrom builds a std::span from firstEdge and
        // edgeCount with no guard of its own, so the run is undefined
        // behaviour in the CONSUMER and not a wrong answer this library can
        // be blamed for later.
        NavParts parts;
        parts.edgeCount1 = 9;
        refused(file({{"NAVG", navPayload(parts)}}));
    }

    SECTION("a run whose start alone is past the edge table") {
        NavParts parts;
        parts.firstEdge1 = 0xFFFFFFF0U;
        parts.edgeCount1 = 0;
        refused(file({{"NAVG", navPayload(parts)}}));
    }

    SECTION("an edge in a node's run not naming that node") {
        NavParts parts;
        parts.edgeFrom = {1, 1}; // node 0's run holds an edge whose from is 1
        parts.edgeTo = {0, 0};
        refused(file({{"NAVG", navPayload(parts)}}));
    }

    SECTION("an edge endpoint naming no node") {
        NavParts parts;
        parts.edgeTo = {9, 0};
        refused(file({{"NAVG", navPayload(parts)}}));
    }
}

TEST_CASE("a WIRG section that decodes cleanly must still satisfy its own rules",
          "[ubundle]") {
    // INV-3, one case per rule in SS 4.9's WIRG list.
    SECTION("nodes not in strictly ascending exportIndex order") {
        WiringParts parts;
        parts.exportIndices = {13, 5};
        refused(file({{"WIRG", wiringPayload(parts)}}));
    }

    SECTION("an outgoing run reaching past the edge table") {
        WiringParts parts;
        parts.runs[1][1] = 9;
        refused(file({{"WIRG", wiringPayload(parts)}}));
    }

    SECTION("an edge in an outgoing run not naming that node") {
        // The two edges are swapped end for end, so `incoming` -- which is
        // grouped by `to` -- still groups correctly and the INCOMING rule is
        // satisfied. Only the outgoing rule can reject this. Breaking `from`
        // alone leaves the incoming grouping wrong too, and the case then
        // reports on that rule instead.
        WiringParts parts;
        parts.edgeFrom = {1, 0};
        parts.edgeTo = {0, 1};
        parts.incomingOrder = {0, 1};
        refused(file({{"WIRG", wiringPayload(parts)}}));
    }

    SECTION("an incoming run reaching past the incoming table") {
        WiringParts parts;
        parts.runs[0][3] = 9;
        refused(file({{"WIRG", wiringPayload(parts)}}));
    }

    SECTION("an edge in an incoming run not naming that node") {
        // incoming is grouped by `to`, so reversing the order puts each edge
        // in the wrong node's run without changing any other field.
        WiringParts parts;
        parts.incomingOrder = {0, 1};
        refused(file({{"WIRG", wiringPayload(parts)}}));
    }

    SECTION("an edge endpoint naming no node") {
        WiringParts parts;
        parts.edgeTo = {9, 0};
        refused(file({{"WIRG", wiringPayload(parts)}}));
    }

    SECTION("incoming and edges holding different numbers of edges") {
        // Weaker than the real invariant, which is that incoming is a
        // PERMUTATION of edges. SS 10 records it as partial rather than
        // claiming the check it is not.
        // Node 1's incoming run is emptied so that the ONLY rule this case
        // breaks is the size equality -- an empty run constrains nothing, and
        // node 0's remaining run still names the one edge with to == 0.
        WiringParts parts;
        parts.incomingOrder = {1};
        parts.runs[1][2] = 0;
        parts.runs[1][3] = 0;
        refused(file({{"WIRG", wiringPayload(parts)}}));
    }

    SECTION("a dangling event naming no node") {
        WiringParts parts;
        parts.danglingFrom = 9;
        refused(file({{"WIRG", wiringPayload(parts)}}));
    }
}

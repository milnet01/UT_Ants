// The .utab framing: the header, the section table, and read and write.
//
// Each section's own layout is its codec's -- RoomSection.cpp,
// NavSection.cpp, WiringSection.cpp, TextureSection.cpp, MaterialSection.cpp,
// GeometrySection.cpp, PlacementSection.cpp and LightSection.cpp, declared in
// Sections.h (UTA-0091). What stays here is
// what every section shares: where it sits in the file, and in what order.

#include "ubundle/Bundle.h"

#include "Sections.h"

#include <string>
#include <utility>
#include <vector>

namespace uta::ubundle {
using namespace detail;

namespace {

constexpr SectionId MAGIC = {'U', 'T', 'A', 'B'};

struct RawHeader {
    BundleHeader header;
    /// Read but NOT bounded here -- SS 4.5. `read` bounds it once it knows
    /// the file's total size; readHeader cannot and must not.
    std::uint32_t sectionCount = 0;
};

[[nodiscard]] Result<RawHeader> decodeHeader(std::span<const std::byte> bytes) {
    if (bytes.size() < HEADER_SIZE)
        return fail(ErrorCode::MalformedData, "a bundle is at least 16 bytes; this is shorter");

    Cursor cursor(bytes.first(HEADER_SIZE));

    // INV-4: magic first, before anything else is read.
    for (const std::uint8_t expected : MAGIC) {
        UTA_TRY(const std::uint8_t actual, cursor.readU8());
        if (actual != expected)
            return fail(ErrorCode::MalformedData, "not a .utab bundle: the magic is wrong");
    }

    RawHeader raw;
    // Checked for EQUALITY and before the section table is read. A lower
    // bound would let a later version's table parse as garbage rather than
    // being refused -- INV-4.
    UTA_TRY(raw.header.formatVersion, cursor.readU32());
    if (raw.header.formatVersion != FORMAT_VERSION)
        return fail(ErrorCode::UnsupportedVersion,
                    "bundle format version " + std::to_string(raw.header.formatVersion)
                        + "; this build reads version " + std::to_string(FORMAT_VERSION));

    // INV-5. Never defaulted to a value: a file whose origin cannot be read
    // has no origin, and the distinction between unreadable and derived is
    // what lets a caller log the difference even though both fail closed.
    UTA_TRY(const std::uint8_t origin, cursor.readU8());
    if (origin > static_cast<std::uint8_t>(Origin::Authored))
        return fail(ErrorCode::MalformedData,
                    "origin byte " + std::to_string(origin) + " is not 0 or 1");
    raw.header.origin = static_cast<Origin>(origin);

    UTA_TRY(const std::uint8_t kind, cursor.readU8());
    if (kind > static_cast<std::uint8_t>(BundleKind::Character))
        return fail(ErrorCode::MalformedData,
                    "kind byte " + std::to_string(kind) + " is not 0 or 1");
    raw.header.kind = static_cast<BundleKind>(kind);

    // Refusing a non-zero reserved field is the only thing that makes a
    // reserved field a contract.
    UTA_TRY(const std::uint16_t reserved, cursor.readU16());
    if (reserved != 0)
        return fail(ErrorCode::MalformedData, "the header's reserved field is not zero");

    UTA_TRY(raw.sectionCount, cursor.readU32());
    return raw;
}

struct Descriptor {
    SectionId id{};
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

[[nodiscard]] bool knownId(const SectionId& id) noexcept {
    return id == ID_ROOM || id == ID_NAVG || id == ID_WIRG || id == ID_TEXS || id == ID_MATS
           || id == ID_GEOM || id == ID_PLAC || id == ID_LITE;
}

} // namespace

Result<BundleHeader> readHeader(std::span<const std::byte> bytes) {
    UTA_TRY(const RawHeader raw, decodeHeader(bytes));
    return raw.header;
}

Result<Bundle> read(std::span<const std::byte> bytes) {
    UTA_TRY(const RawHeader raw, decodeHeader(bytes));

    const std::uint64_t fileSize = bytes.size();

    // SS 4.4, and written as a DIVISION because SS 4.2 rule 1 requires it:
    // 16 + 24 * sectionCount <= fileSize is precisely the overflow that rule
    // forbids -- sectionCount is a u32, 24 * 0xAAAAAAAB wraps to 8, and the
    // test then passes on a file of any size while the reader walks
    // 2,863,311,531 descriptors.
    if (raw.sectionCount > (fileSize - HEADER_SIZE) / SECTION_DESCRIPTOR_SIZE)
        return fail(ErrorCode::MalformedData, "the section count overruns the file");

    Cursor table(bytes.subspan(HEADER_SIZE));
    std::vector<Descriptor> descriptors;
    descriptors.reserve(raw.sectionCount);
    for (std::uint32_t i = 0; i < raw.sectionCount; ++i) {
        Descriptor descriptor;
        for (std::uint8_t& part : descriptor.id) {
            UTA_TRY(part, table.readU8());
        }
        UTA_TRY(descriptor.offset, table.readU64());
        UTA_TRY(descriptor.size, table.readU64());

        // The byte is DEFINED and its value is not, so this is a version
        // refusal rather than corruption -- SS 6.
        UTA_TRY(const std::uint8_t compression, table.readU8());
        if (compression != 0)
            return fail(ErrorCode::UnsupportedVersion,
                        "section compression " + std::to_string(compression)
                            + " is not defined in format version "
                            + std::to_string(FORMAT_VERSION));

        for (int part = 0; part < 3; ++part) {
            UTA_TRY(const std::uint8_t reserved, table.readU8());
            if (reserved != 0)
                return fail(ErrorCode::MalformedData,
                            "a section descriptor's reserved field is not zero");
        }

        // Refused, not skipped. Under SS 4.3's exact version match there is
        // no such thing as a file from a later version this reader should
        // tolerate, so an unknown id means a corrupt or hand-edited file.
        if (!knownId(descriptor.id))
            return fail(ErrorCode::MalformedData, "a section id is not defined in this version");

        descriptors.push_back(descriptor);
    }

    // The WHOLE table is validated before any section is decoded -- INV-11.
    // Trusting it and decoding each section from its own descriptor lets
    // overlapping sections decode without error, one of them wrong.
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        for (std::size_t j = i + 1; j < descriptors.size(); ++j)
            if (descriptors[i].id == descriptors[j].id)
                return fail(ErrorCode::MalformedData, "a section id appears twice");

        // Ascending offset order. This is what makes the tiling pass below a
        // single linear pass rather than a comparison of every pair.
        if (i > 0 && descriptors[i].offset < descriptors[i - 1].offset)
            return fail(ErrorCode::MalformedData,
                        "section descriptors are not in ascending offset order");

        // Computed so offset + size cannot overflow.
        if (descriptors[i].offset > fileSize || descriptors[i].size > fileSize - descriptors[i].offset)
            return fail(ErrorCode::MalformedData, "a section's byte range lies outside the file");
    }

    // The sections tile the file exactly: the first begins at the end of the
    // table, each next begins where the last ended, and the last ends at the
    // end of the file. `write` never produces a gap or a trailing byte, so
    // accepting either would be one reader tolerating what another refuses.
    std::uint64_t expected =
        HEADER_SIZE + static_cast<std::uint64_t>(raw.sectionCount) * SECTION_DESCRIPTOR_SIZE;
    for (const Descriptor& descriptor : descriptors) {
        if (descriptor.offset != expected)
            return fail(ErrorCode::MalformedData,
                        "the sections do not tile the file: a gap or an overlap");
        expected += descriptor.size;
    }
    if (expected != fileSize)
        return fail(ErrorCode::MalformedData, "bytes trail the last section");

    Bundle bundle;
    bundle.header = raw.header;

    for (const Descriptor& descriptor : descriptors) {
        Cursor payload(bytes.subspan(static_cast<std::size_t>(descriptor.offset),
                                     static_cast<std::size_t>(descriptor.size)));
        if (descriptor.id == ID_ROOM) {
            UTA_TRY(bundle.rooms, readRoomMap(payload));
            UTA_CHECK(validateRoomMap(*bundle.rooms, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_NAVG) {
            UTA_TRY(bundle.nav, readNavGraph(payload));
            UTA_CHECK(validateNavGraph(*bundle.nav, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_WIRG) {
            UTA_TRY(bundle.wiring, readWiringGraph(payload));
            UTA_CHECK(validateWiringGraph(*bundle.wiring, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_TEXS) {
            // No post-decode validator beside this one, unlike the three
            // above. UTA-0052's INV-1 and INV-2 are decode-time refusals
            // applied inside readCompressedTexture, in SS 4.4's class rather
            // than SS 4.9's -- that item's SS 11 says so and says why the
            // rules are not copied into SS 4.9.
            UTA_TRY(bundle.textures, readTextures(payload));
        } else if (descriptor.id == ID_MATS) {
            UTA_TRY(bundle.materials, readMaterials(payload));
            UTA_CHECK(validateMaterials(*bundle.materials, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_GEOM) {
            UTA_TRY(bundle.geometry, readGeometry(payload));
            UTA_CHECK(validateGeometry(*bundle.geometry, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_PLAC) {
            UTA_TRY(bundle.placements, readPlacements(payload));
            UTA_CHECK(validatePlacements(*bundle.placements, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_LITE) {
            UTA_TRY(bundle.lights, readLights(payload));
            UTA_CHECK(validateLights(*bundle.lights, ErrorCode::MalformedData));
        } else {
            // Unreachable: knownId() refused every other id while the table
            // was being validated. Named rather than folded into the WIRG arm
            // so that each id appears in exactly ONE arm -- an `else` standing
            // for WIRG decodes a NEW section id as a WiringGraph the moment
            // one is added to knownId() and not to this dispatch, and that is
            // silent. Refusing is loud and wrong in the safe direction.
            return fail(ErrorCode::MalformedData, "a section id has no decoder");
        }

        // Trailing bytes mean the layout was misread, not that there is
        // slack -- SS 6.
        if (payload.remaining() != 0)
            return fail(ErrorCode::MalformedData, "a section ends with bytes unread");
    }

    return bundle;
}

Result<std::vector<std::byte>> write(const Bundle& bundle) {
    // Refused rather than written, so a bad bundle cannot be produced here
    // and then blamed on the reader -- SS 6.
    if (bundle.rooms) UTA_CHECK(validateRoomMap(*bundle.rooms, ErrorCode::InvalidArgument));
    if (bundle.nav) UTA_CHECK(validateNavGraph(*bundle.nav, ErrorCode::InvalidArgument));
    if (bundle.wiring) UTA_CHECK(validateWiringGraph(*bundle.wiring, ErrorCode::InvalidArgument));
    if (bundle.materials) UTA_CHECK(validateMaterials(*bundle.materials, ErrorCode::InvalidArgument));
    if (bundle.geometry) UTA_CHECK(validateGeometry(*bundle.geometry, ErrorCode::InvalidArgument));
    if (bundle.placements)
        UTA_CHECK(validatePlacements(*bundle.placements, ErrorCode::InvalidArgument));
    if (bundle.lights) UTA_CHECK(validateLights(*bundle.lights, ErrorCode::InvalidArgument));

    // The fixed order ROOM, NAVG, WIRG, TEXS, MATS, GEOM, PLAC, LITE. Fixed rather than incidental because
    // docs/design.md SS Close calls names a bundle written by any tool other
    // than ubake by the hash of its own contents, and a hash over an
    // incidentally-ordered file names one world two things.
    std::vector<std::pair<SectionId, std::vector<std::byte>>> sections;
    if (bundle.rooms) sections.emplace_back(ID_ROOM, encodeRoomMap(*bundle.rooms));
    if (bundle.nav) sections.emplace_back(ID_NAVG, encodeNavGraph(*bundle.nav));
    if (bundle.wiring) sections.emplace_back(ID_WIRG, encodeWiringGraph(*bundle.wiring));
    // TEXS is APPENDED rather than inserted, so UTA-0008 SS 4.10's existing
    // order clause is extended rather than contradicted. It also keeps the
    // small graph sections near the front of a file whose largest section is
    // by far this one.
    if (bundle.textures) sections.emplace_back(ID_TEXS, encodeTextures(*bundle.textures));
    // MATS is appended after TEXS for the same reason -- UTA-0011 SS 4.10.
    if (bundle.materials) sections.emplace_back(ID_MATS, encodeMaterials(*bundle.materials));
    // GEOM is appended after MATS -- UTA-0109 SS 4.2.
    if (bundle.geometry) sections.emplace_back(ID_GEOM, encodeGeometry(*bundle.geometry));
    // PLAC then LITE are appended after GEOM -- UTA-0110 SS 4.4.
    if (bundle.placements) sections.emplace_back(ID_PLAC, encodePlacements(*bundle.placements));
    if (bundle.lights) sections.emplace_back(ID_LITE, encodeLights(*bundle.lights));

    Sink sink;
    sink.putId(MAGIC);
    // FORMAT_VERSION, not bundle.header.formatVersion. `read` only ever
    // yields 1 and this only ever emits 1, so the two cannot disagree; a
    // caller's value is an input this function has no way to honour.
    sink.putU32(FORMAT_VERSION);
    sink.putU8(static_cast<std::uint8_t>(bundle.header.origin));
    sink.putU8(static_cast<std::uint8_t>(bundle.header.kind));
    sink.putU16(0);
    sink.putU32(static_cast<std::uint32_t>(sections.size()));

    std::uint64_t offset = HEADER_SIZE + sections.size() * SECTION_DESCRIPTOR_SIZE;
    for (const auto& [id, payload] : sections) {
        sink.putId(id);
        sink.putU64(offset);
        sink.putU64(payload.size());
        sink.putU8(0); // compression: none, and no version defines another
        sink.putU8(0);
        sink.putU8(0);
        sink.putU8(0);
        offset += payload.size();
    }

    for (const auto& section : sections) sink.append(section.second);

    return std::move(sink).take();
}

} // namespace uta::ubundle

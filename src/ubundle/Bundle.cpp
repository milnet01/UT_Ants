// The .utab framing: the header, the section table, and read and write.
//
// Each section's own layout is its codec's, one file per section, declared in
// Sections.h (UTA-0091). What stays here is what every section shares: where
// it sits in the file, and in what order.

#include "ubundle/Bundle.h"

#include "Sections.h"

#include <algorithm>
#include <array>
#include <optional>
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

/// Every section id this version defines. A new section joins this list, the
/// dispatch in `read` and the order in `write`.
constexpr std::array KNOWN_IDS = {ID_ROOM, ID_NAVG, ID_WIRG, ID_TEXS, ID_MATS, ID_GEOM, ID_PLAC,
                                  ID_LITE, ID_MOVR, ID_COLL, ID_LPRB, ID_ZONE, ID_AOCC, ID_FLAM,
                                  ID_SMSK, ID_LAMP, ID_SUN,  ID_PSKY, ID_BAKR};

[[nodiscard]] bool knownId(const SectionId& id) noexcept {
    return std::ranges::find(KNOWN_IDS, id) != KNOWN_IDS.end();
}

/// How many section ids this version defines -- the most a table written by
/// `write` holds, so the most readBaker's prefix must carry.
constexpr std::size_t KNOWN_SECTIONS = KNOWN_IDS.size();
static_assert(HEADER_SIZE + KNOWN_SECTIONS * SECTION_DESCRIPTOR_SIZE + 4 + MAX_BAKER_LENGTH
                  <= BAKER_PREFIX_SIZE,
              "readBaker's prefix must hold the header, a full table and the longest BAKR");

/// One descriptor, with its own fields checked -- SS 4.4. What it says about
/// the rest of the table is the caller's to check.
[[nodiscard]] Result<Descriptor> readDescriptor(Cursor& table) {
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
                        + " is not defined in format version " + std::to_string(FORMAT_VERSION));

    for (int part = 0; part < 3; ++part) {
        UTA_TRY(const std::uint8_t reserved, table.readU8());
        if (reserved != 0)
            return fail(ErrorCode::MalformedData, "a section descriptor's reserved field is not zero");
    }

    // Refused, not skipped. Under SS 4.3's exact version match there is
    // no such thing as a file from a later version this reader should
    // tolerate, so an unknown id means a corrupt or hand-edited file.
    if (!knownId(descriptor.id))
        return fail(ErrorCode::MalformedData, "a section id is not defined in this version");
    return descriptor;
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
        UTA_TRY(const Descriptor descriptor, readDescriptor(table));
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
            // No post-decode validator beside this one. UTA-0052's INV-1 and
            // INV-2 are decode-time refusals applied inside readCompressedTexture, in SS 4.4's class rather
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
        } else if (descriptor.id == ID_MOVR) {
            UTA_TRY(bundle.movers, readMovers(payload));
            UTA_CHECK(validateMovers(*bundle.movers, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_COLL) {
            UTA_TRY(bundle.collision, readCollision(payload));
            UTA_CHECK(validateCollision(*bundle.collision, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_LPRB) {
            UTA_TRY(bundle.lightProbes, readLightProbes(payload));
            UTA_CHECK(validateLightProbes(*bundle.lightProbes, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_ZONE) {
            UTA_TRY(bundle.zones, readZones(payload));
            UTA_CHECK(validateZones(*bundle.zones, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_AOCC) {
            UTA_TRY(bundle.occlusion, readOcclusion(payload));
            UTA_CHECK(validateOcclusion(*bundle.occlusion, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_FLAM) {
            // Checked across sections below, as are LAMP and SUN.
            UTA_TRY(bundle.flames, readFlames(payload));
        } else if (descriptor.id == ID_SMSK) {
            UTA_TRY(bundle.shadowMask, readShadowMask(payload));
            UTA_CHECK(validateShadowMask(*bundle.shadowMask, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_LAMP) {
            // Checked across sections below.
            UTA_TRY(bundle.lamps, readLamps(payload));
        } else if (descriptor.id == ID_SUN) {
            // Checked across sections below.
            UTA_TRY(bundle.sun, readSun(payload));
        } else if (descriptor.id == ID_PSKY) {
            UTA_TRY(bundle.paintedSky, readPaintedSky(payload));
            UTA_CHECK(validatePaintedSky(*bundle.paintedSky, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_BAKR) {
            UTA_TRY(bundle.baker, readBakerSection(payload));
            UTA_CHECK(validateBaker(*bundle.baker, ErrorCode::MalformedData));
        } else {
            // Unreachable: knownId() refused every other id while the table
            // was being validated. Named rather than folded into the last arm
            // so that each id appears in exactly ONE arm -- an `else` standing
            // for BAKR decodes a NEW section id as a baker record the moment
            // one is added to knownId() and not to this dispatch, and that is
            // silent. Refusing is loud and wrong in the safe direction.
            return fail(ErrorCode::MalformedData, "a section id has no decoder");
        }

        // Trailing bytes mean the layout was misread, not that there is
        // slack -- SS 6.
        if (payload.remaining() != 0)
            return fail(ErrorCode::MalformedData, "a section ends with bytes unread");
    }

    // UTA-0156 SS 4.2: a rule across GEOM, MOVR, LAMP and ZONE, so it waits for all four.
    UTA_CHECK(validateVertexZones(bundle, ErrorCode::MalformedData));
    // UTA-0164 SS 4.1: AOCC's uv count is GEOM's vertex count.
    UTA_CHECK(validateOcclusionVertices(bundle, ErrorCode::MalformedData));
    // UTA-0263 SS 4.3: a record names MATS and LITE, so it waits for both.
    UTA_CHECK(validateFlames(bundle, ErrorCode::MalformedData));
    // UTA-0326 SS 4.2: SMSK's vertices are GEOM's and its lights LITE's.
    UTA_CHECK(validateShadowMaskAcross(bundle, ErrorCode::MalformedData));
    // UTA-0256 SS 4.2: a lamp's flames name MATS, and LPRB's added cubes count LAMP.
    UTA_CHECK(validateLamps(bundle, ErrorCode::MalformedData));
    // UTA-0338 SS 4.2: LPRB's sun visibility counts SUN, and SMSK holds its pairs.
    UTA_CHECK(validateSun(bundle, ErrorCode::MalformedData));
    return bundle;
}

Result<std::optional<std::string>> readBaker(std::span<const std::byte> prefix) {
    UTA_TRY(const RawHeader raw, decodeHeader(prefix));
    // Bounded here, not left to the table running out of bytes: the loop
    // stops at BAKR, so a count past it would never be read.
    if (raw.sectionCount > KNOWN_SECTIONS)
        return fail(ErrorCode::MalformedData, "the section count is more than this version defines");
    Cursor table(prefix.subspan(HEADER_SIZE));
    for (std::uint32_t i = 0; i < raw.sectionCount; ++i) {
        UTA_TRY(const Descriptor descriptor, readDescriptor(table));
        if (descriptor.id != ID_BAKR) continue;
        // Computed so offset + size cannot overflow. A BAKR pointing back into
        // the header or the table would read those bytes as a version, which
        // read refuses; so does this (review-code 2026-10-15).
        if (descriptor.offset < HEADER_SIZE + std::uint64_t{raw.sectionCount} * SECTION_DESCRIPTOR_SIZE)
            return fail(ErrorCode::MalformedData, "BAKR lies inside the header or the section table");
        if (descriptor.offset > prefix.size() || descriptor.size > prefix.size() - descriptor.offset)
            return fail(ErrorCode::MalformedData, "BAKR lies outside the bytes given");
        Cursor payload(prefix.subspan(static_cast<std::size_t>(descriptor.offset),
                                      static_cast<std::size_t>(descriptor.size)));
        UTA_TRY(std::string baker, readBakerSection(payload));
        UTA_CHECK(validateBaker(baker, ErrorCode::MalformedData));
        return std::optional<std::string>(std::move(baker));
    }
    return std::optional<std::string>{};
}

Result<std::vector<std::byte>> write(const Bundle& bundle) {
    // Refused rather than written, so a bad bundle cannot be produced here
    // and then blamed on the reader -- SS 6.
    // The header's two enums are one byte each, and read refuses either above
    // its last value (UTA-0223).
    if (static_cast<std::uint8_t>(bundle.header.origin) > static_cast<std::uint8_t>(Origin::Authored))
        return fail(ErrorCode::InvalidArgument, "the header's origin is not Derived or Authored");
    if (static_cast<std::uint8_t>(bundle.header.kind) > static_cast<std::uint8_t>(BundleKind::Character))
        return fail(ErrorCode::InvalidArgument, "the header's kind is not Map or Character");
    if (bundle.rooms) UTA_CHECK(validateRoomMap(*bundle.rooms, ErrorCode::InvalidArgument));
    if (bundle.nav) UTA_CHECK(validateNavGraph(*bundle.nav, ErrorCode::InvalidArgument));
    if (bundle.wiring) UTA_CHECK(validateWiringGraph(*bundle.wiring, ErrorCode::InvalidArgument));
    if (bundle.materials) UTA_CHECK(validateMaterials(*bundle.materials, ErrorCode::InvalidArgument));
    if (bundle.geometry) UTA_CHECK(validateGeometry(*bundle.geometry, ErrorCode::InvalidArgument));
    if (bundle.placements)
        UTA_CHECK(validatePlacements(*bundle.placements, ErrorCode::InvalidArgument));
    if (bundle.lights) UTA_CHECK(validateLights(*bundle.lights, ErrorCode::InvalidArgument));
    // TEXS is put straight into the file, so its rules run here rather than
    // in an encoder (review-code 2026-10-15).
    if (bundle.textures) UTA_CHECK(validateTextures(*bundle.textures, ErrorCode::InvalidArgument));
    if (bundle.movers) UTA_CHECK(validateMovers(*bundle.movers, ErrorCode::InvalidArgument));
    if (bundle.collision)
        UTA_CHECK(validateCollision(*bundle.collision, ErrorCode::InvalidArgument));
    if (bundle.lightProbes)
        UTA_CHECK(validateLightProbes(*bundle.lightProbes, ErrorCode::InvalidArgument));
    if (bundle.zones) UTA_CHECK(validateZones(*bundle.zones, ErrorCode::InvalidArgument));
    UTA_CHECK(validateVertexZones(bundle, ErrorCode::InvalidArgument));
    if (bundle.occlusion) UTA_CHECK(validateOcclusion(*bundle.occlusion, ErrorCode::InvalidArgument));
    UTA_CHECK(validateOcclusionVertices(bundle, ErrorCode::InvalidArgument));
    UTA_CHECK(validateFlames(bundle, ErrorCode::InvalidArgument));
    if (bundle.shadowMask) UTA_CHECK(validateShadowMask(*bundle.shadowMask, ErrorCode::InvalidArgument));
    UTA_CHECK(validateShadowMaskAcross(bundle, ErrorCode::InvalidArgument));
    UTA_CHECK(validateLamps(bundle, ErrorCode::InvalidArgument));
    UTA_CHECK(validateSun(bundle, ErrorCode::InvalidArgument));
    if (bundle.paintedSky) UTA_CHECK(validatePaintedSky(*bundle.paintedSky, ErrorCode::InvalidArgument));
    if (bundle.baker) UTA_CHECK(validateBaker(*bundle.baker, ErrorCode::InvalidArgument));

    // The fixed order BAKR, ROOM, NAVG, WIRG, TEXS, MATS, GEOM, PLAC, LITE, MOVR, COLL, LPRB, ZONE, AOCC, FLAM, SMSK, LAMP, SUN, PSKY. Fixed rather than incidental because
    // docs/design.md SS Close calls names a bundle written by any tool other
    // than ubake by the hash of its own contents, and a hash over an
    // incidentally-ordered file names one world two things.
    //
    // Every section but TEXS is encoded here. TEXS is only counted, and its
    // bytes are put straight into the file below (UTA-0143).
    struct Section {
        SectionId id;
        std::vector<std::byte> payload;
        std::uint64_t size = 0;
    };
    std::vector<Section> sections;
    // An encoder refuses a count its u32 cannot hold (UTA-0223); the first
    // refusal is kept and returned once every section has been tried.
    std::optional<Error> refused;
    const auto encoded = [&sections, &refused](const SectionId& id, Result<std::vector<std::byte>> payload) {
        if (!payload) {
            if (!refused) refused = std::move(payload).error();
            return;
        }
        const std::uint64_t size = payload->size();
        sections.push_back(Section{id, std::move(*payload), size});
    };
    // BAKR first -- UTA-0349 -- so readBaker finds it in the file's first bytes.
    if (bundle.baker) encoded(ID_BAKR, encodeBaker(*bundle.baker));
    if (bundle.rooms) encoded(ID_ROOM, encodeRoomMap(*bundle.rooms));
    if (bundle.nav) encoded(ID_NAVG, encodeNavGraph(*bundle.nav));
    if (bundle.wiring) encoded(ID_WIRG, encodeWiringGraph(*bundle.wiring));
    // TEXS is APPENDED rather than inserted, so UTA-0008 SS 4.10's existing
    // order clause is extended rather than contradicted. It also keeps the
    // small graph sections near the front of a file whose largest section is
    // by far this one.
    if (bundle.textures) sections.push_back(Section{ID_TEXS, {}, texturesSize(*bundle.textures)});
    // MATS is appended after TEXS for the same reason -- UTA-0011 SS 4.10.
    if (bundle.materials) encoded(ID_MATS, encodeMaterials(*bundle.materials));
    // GEOM is appended after MATS -- UTA-0109 SS 4.2.
    if (bundle.geometry) encoded(ID_GEOM, encodeGeometry(*bundle.geometry));
    // PLAC then LITE are appended after GEOM -- UTA-0110 SS 4.4.
    if (bundle.placements) encoded(ID_PLAC, encodePlacements(*bundle.placements));
    if (bundle.lights) encoded(ID_LITE, encodeLights(*bundle.lights));
    // MOVR is appended after LITE -- UTA-0119 SS 4.2.
    if (bundle.movers) encoded(ID_MOVR, encodeMovers(*bundle.movers));
    // COLL is appended after MOVR -- UTA-0111 SS 4.2.
    if (bundle.collision) encoded(ID_COLL, encodeCollision(*bundle.collision));
    // LPRB is appended after COLL -- UTA-0112 SS 4.2.
    if (bundle.lightProbes) encoded(ID_LPRB, encodeLightProbes(*bundle.lightProbes));
    // ZONE is appended after LPRB -- UTA-0156 SS 4.1.
    if (bundle.zones) encoded(ID_ZONE, encodeZones(*bundle.zones));
    // AOCC is appended after ZONE -- UTA-0164 SS 4.1.
    if (bundle.occlusion) encoded(ID_AOCC, encodeOcclusion(*bundle.occlusion));
    // FLAM is appended after AOCC -- UTA-0263 SS 4.3.
    if (bundle.flames) encoded(ID_FLAM, encodeFlames(*bundle.flames));
    // SMSK is appended after FLAM -- UTA-0326 SS 4.2.
    if (bundle.shadowMask) encoded(ID_SMSK, encodeShadowMask(*bundle.shadowMask));
    // LAMP is appended after SMSK -- UTA-0256 SS 4.2.
    if (bundle.lamps) encoded(ID_LAMP, encodeLamps(*bundle.lamps));
    // SUN is appended after LAMP -- UTA-0338 SS 4.2.
    if (bundle.sun) encoded(ID_SUN, encodeSun(*bundle.sun));
    // PSKY is appended after SUN -- UTA-0350 SS 4.3.
    if (bundle.paintedSky) encoded(ID_PSKY, encodePaintedSky(*bundle.paintedSky));

    if (refused) return std::unexpected(*std::move(refused));
    // The file's size is known before a byte is written, so the buffer grows
    // once, and each section is freed once it is copied in: the whole file,
    // every section and a doubling buffer are never all held at once
    // (UTA-0143).
    std::uint64_t fileSize = HEADER_SIZE + sections.size() * SECTION_DESCRIPTOR_SIZE;
    for (const Section& section : sections) fileSize += section.size;
    Sink sink;
    sink.reserve(static_cast<std::size_t>(fileSize));
    sink.putId(MAGIC);
    // FORMAT_VERSION, not bundle.header.formatVersion. `read` refuses any
    // other version and this only ever emits the current one, so the two
    // cannot disagree; a caller's value is an input this function has no way
    // to honour.
    sink.putU32(FORMAT_VERSION);
    sink.putU8(static_cast<std::uint8_t>(bundle.header.origin));
    sink.putU8(static_cast<std::uint8_t>(bundle.header.kind));
    sink.putU16(0);
    sink.putCount(sections.size());

    std::uint64_t offset = HEADER_SIZE + sections.size() * SECTION_DESCRIPTOR_SIZE;
    for (const Section& section : sections) {
        sink.putId(section.id);
        sink.putU64(offset);
        sink.putU64(section.size);
        sink.putU8(0); // compression: none, and no version defines another
        sink.putU8(0);
        sink.putU8(0);
        sink.putU8(0);
        offset += section.size;
    }

    for (Section& section : sections) {
        if (section.id == ID_TEXS) {
            // Its descriptor was written from the COUNT, so a count that
            // drifted from what putTextures emits would point every later
            // section at the wrong bytes. Refused rather than written.
            const std::size_t start = sink.size();
            putTextures(sink, *bundle.textures);
            if (sink.size() - start != section.size)
                return fail(ErrorCode::InvalidArgument,
                            "TEXS: the counted size disagrees with the bytes written");
            continue;
        }
        sink.append(section.payload);
        std::vector<std::byte>().swap(section.payload);
    }

    return std::move(sink).finish("the bundle");
}

} // namespace uta::ubundle

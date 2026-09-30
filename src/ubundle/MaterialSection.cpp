// The MATS section: each material's own values --
// docs/specs/UTA-0011-map-baker.md SS 4.10, INV-18.

#include "Sections.h"

#include <cmath>
#include <string>

namespace uta::ubundle::detail {
namespace {

/// UTA-0011 SS 4.10: a u32 length for an empty id, then one u8; UTA-0040
/// SS 4.1's depth byte; and UTA-0263 SS 4.2's flame byte.
constexpr std::uint64_t MIN_MATERIAL = 7;

[[nodiscard]] Result<MaterialRecord> readMaterialRecord(Cursor& cursor) {
    MaterialRecord record;
    UTA_TRY(record.id, readString(cursor));

    // Refused, never defaulted, as TEXS's format byte is: a value other than
    // 0 or 1 is not defined in this version, and reading it as a bool would
    // publish one of two answers the file never gave.
    UTA_TRY(const std::uint8_t metallic, cursor.readU8());
    if (metallic > 1)
        return fail(ErrorCode::MalformedData,
                    "MATS: metallic byte " + std::to_string(metallic) + " is not 0 or 1");
    record.metallic = metallic == 1;
    // UTA-0040 SS 4.1: every value is defined.
    UTA_TRY(record.parallaxDepth, cursor.readU8());
    // UTA-0263 SS 4.2: 0, or 1 and the eight ramp colours. Refused otherwise,
    // as the metallic byte is.
    UTA_TRY(const std::uint8_t flame, cursor.readU8());
    if (flame > 1)
        return fail(ErrorCode::MalformedData, "MATS: flame byte " + std::to_string(flame) + " is not 0 or 1");
    if (flame == 1) {
        FlameLook& look = record.flame.emplace();
        for (auto& colour : look.ramp)
            for (float& channel : colour) {
                // Braced: UTA_TRY is three statements.
                UTA_TRY(channel, cursor.readF32());
            }
    }
    return record;
}

void putMaterialRecord(Sink& sink, const MaterialRecord& record) {
    sink.putString(record.id);
    sink.putU8(record.metallic ? 1 : 0);
    sink.putU8(record.parallaxDepth);
    sink.putU8(record.flame ? 1 : 0);
    if (record.flame)
        for (const auto& colour : record.flame->ramp)
            for (const float channel : colour) sink.putF32(channel);
}

} // namespace

Result<std::vector<MaterialRecord>> readMaterials(Cursor& cursor) {
    return readVector<MaterialRecord>(cursor, MIN_MATERIAL, "materials", readMaterialRecord);
}

Result<void> validateMaterials(const std::vector<MaterialRecord>& materials, ErrorCode code) {
    // Strictly ascending bytewise, which also makes the ids unique. Checked
    // rather than left to the writer's good behaviour (INV-18). std::string's
    // `<` compares through char_traits<char>, which compares as unsigned char,
    // so this is the bytewise order SS 4.10 names on every compiler.
    for (std::size_t i = 1; i < materials.size(); ++i) {
        if (!(materials[i - 1].id < materials[i].id))
            return fail(code, "MATS: material " + std::to_string(i)
                                  + "'s id does not sort strictly after the one before it");
    }
    for (std::size_t i = 0; i < materials.size(); ++i) {
        if (!materials[i].flame) continue;
        for (const auto& colour : materials[i].flame->ramp)
            for (const float channel : colour)
                if (!std::isfinite(channel))
                    return fail(code, "MATS: material " + std::to_string(i)
                                          + "'s flame ramp holds a value that is not finite");
    }
    return {};
}

Result<std::vector<std::byte>> encodeMaterials(const std::vector<MaterialRecord>& materials) {
    Sink sink;
    sink.putVector(materials, putMaterialRecord);
    return std::move(sink).finish("MATS");
}

} // namespace uta::ubundle::detail

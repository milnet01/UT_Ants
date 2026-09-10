// The MATS section: each material's own values --
// docs/specs/UTA-0011-map-baker.md SS 4.10, INV-18.

#include "Sections.h"

#include <string>

namespace uta::ubundle::detail {
namespace {

/// UTA-0011 SS 4.10: a u32 length for an empty id, then one u8.
constexpr std::uint64_t MIN_MATERIAL = 5;

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
    return record;
}

void putMaterialRecord(Sink& sink, const MaterialRecord& record) {
    sink.putString(record.id);
    sink.putU8(record.metallic ? 1 : 0);
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
    return {};
}

std::vector<std::byte> encodeMaterials(const std::vector<MaterialRecord>& materials) {
    Sink sink;
    sink.putVector(materials, putMaterialRecord);
    return std::move(sink).take();
}

} // namespace uta::ubundle::detail

// The MATS section: each material's own values --
// docs/specs/UTA-0011-map-baker.md SS 4.10, INV-18.

#include "Sections.h"

#include <cmath>
#include <string>

namespace uta::ubundle::detail {
namespace {

/// UTA-0011 SS 4.10: a u32 length for an empty id, then one u8; UTA-0040
/// SS 4.1's depth byte; UTA-0263 SS 4.2's flame byte; and UTA-0105 SS 4.2's
/// liquid byte.
constexpr std::uint64_t MIN_MATERIAL = 8;

/// UTA-0105 SS 4.2's refusals, shared by read and write. Empty when the look
/// is valid; otherwise what is wrong with it.
[[nodiscard]] std::string liquidFault(const LiquidLook& look) {
    if (look.moveIce > 1) return "MoveIce byte " + std::to_string(look.moveIce) + " is not 0 or 1"; // UTA-0270
    if (look.panning > LIQUID_PANNING_MAX)
        return "panning style " + std::to_string(look.panning) + " is past "
               + std::to_string(LIQUID_PANNING_MAX);
    for (const std::uint16_t side : look.size)
        if (side == 0 || side > LIQUID_SIZE_MAX)
            return "size side " + std::to_string(side) + " is not 1 to " + std::to_string(LIQUID_SIZE_MAX);
    for (const auto& colour : look.ramp)
        for (const float channel : colour)
            if (!std::isfinite(channel)) return "ramp holds a value that is not finite";
    return {};
}

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
    // UTA-0105 SS 4.2: 0, or the kind and the look's fields.
    UTA_TRY(const std::uint8_t liquid, cursor.readU8());
    if (liquid > static_cast<std::uint8_t>(LiquidKind::Wave))
        return fail(ErrorCode::MalformedData, "MATS: liquid byte " + std::to_string(liquid) + " is not 0 to 3");
    if (liquid != 0) {
        LiquidLook& look = record.liquid.emplace();
        look.kind = static_cast<LiquidKind>(liquid);
        UTA_TRY(look.amplitude, cursor.readU8());
        UTA_TRY(look.frequency, cursor.readU8());
        UTA_TRY(look.panning, cursor.readU8());
        UTA_TRY(look.moveIce, cursor.readU8()); // UTA-0270
        for (std::uint8_t& pan : look.pan) {
            UTA_TRY(pan, cursor.readU8());
        }
        for (std::uint8_t& bump : look.bump) {
            UTA_TRY(bump, cursor.readU8());
        }
        for (std::uint16_t& side : look.size) {
            UTA_TRY(side, cursor.readU16());
        }
        for (auto& colour : look.ramp)
            for (float& channel : colour) {
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
    sink.putU8(record.liquid ? static_cast<std::uint8_t>(record.liquid->kind) : 0);
    if (!record.liquid) return;
    const LiquidLook& look = *record.liquid;
    sink.putU8(look.amplitude);
    sink.putU8(look.frequency);
    sink.putU8(look.panning);
    sink.putU8(look.moveIce); // UTA-0270
    for (const std::uint8_t pan : look.pan) sink.putU8(pan);
    for (const std::uint8_t bump : look.bump) sink.putU8(bump);
    for (const std::uint16_t side : look.size) sink.putU16(side);
    for (const auto& colour : look.ramp)
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
    for (std::size_t i = 0; i < materials.size(); ++i) {
        if (!materials[i].liquid) continue;
        const LiquidKind kind = materials[i].liquid->kind;
        if (kind != LiquidKind::Wet && kind != LiquidKind::Ice && kind != LiquidKind::Wave)
            return fail(code, "MATS: material " + std::to_string(i) + "'s liquid kind is not 1 to 3");
        if (const std::string fault = liquidFault(*materials[i].liquid); !fault.empty())
            return fail(code, "MATS: material " + std::to_string(i) + "'s liquid look: " + fault);
    }
    return {};
}

Result<std::vector<std::byte>> encodeMaterials(const std::vector<MaterialRecord>& materials) {
    Sink sink;
    sink.putVector(materials, putMaterialRecord);
    return std::move(sink).finish("MATS");
}

} // namespace uta::ubundle::detail

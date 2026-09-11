// The LITE section: the level's lights, resolved --
// docs/specs/UTA-0110-lights-and-placements.md SS 4.4, INV-1 and INV-2.
//
// The twelve light bytes are UT99's own numbers and are not range-checked
// (SS 4.4): a value past the last light type is the renderer's to handle.

#include "Sections.h"

#include <string>
#include <utility>

namespace uta::ubundle::detail {
namespace {

/// SS 4.4: the export index, three f32, three i32, twelve bytes and four
/// bools -- fixed.
constexpr std::uint64_t LIGHT_SIZE = 44;

[[nodiscard]] Result<Light> readLight(Cursor& cursor) {
    Light light;
    UTA_TRY(light.exportIndex, cursor.readU32());
    for (float& part : light.location) {
        UTA_TRY(part, cursor.readF32());
    }
    for (std::int32_t& part : light.rotation) {
        UTA_TRY(part, cursor.readI32());
    }
    for (std::uint8_t* field :
         {&light.type, &light.effect, &light.brightness, &light.hue, &light.saturation,
          &light.radius, &light.period, &light.phase, &light.cone, &light.volumeBrightness,
          &light.volumeRadius, &light.volumeFog}) {
        UTA_TRY(const std::uint8_t value, cursor.readU8());
        *field = value;
    }
    // Refused here, on `read` alone: a bool in memory holds no other value.
    for (bool* flag : {&light.specialLit, &light.actorShadows, &light.corona, &light.lensFlare}) {
        UTA_TRY(const std::uint8_t byte, cursor.readU8());
        if (byte > 1)
            return fail(ErrorCode::MalformedData,
                        "LITE: a light's bool byte " + std::to_string(byte) + " is not 0 or 1");
        *flag = byte == 1;
    }
    return light;
}

void putLight(Sink& sink, const Light& light) {
    sink.putU32(light.exportIndex);
    for (const float part : light.location) sink.putF32(part);
    for (const std::int32_t part : light.rotation) sink.putI32(part);
    for (const std::uint8_t field :
         {light.type, light.effect, light.brightness, light.hue, light.saturation, light.radius,
          light.period, light.phase, light.cone, light.volumeBrightness, light.volumeRadius,
          light.volumeFog})
        sink.putU8(field);
    for (const bool flag : {light.specialLit, light.actorShadows, light.corona, light.lensFlare})
        sink.putU8(flag ? 1 : 0);
}

} // namespace

Result<std::vector<Light>> readLights(Cursor& cursor) {
    return readVector<Light>(cursor, LIGHT_SIZE, "lights", readLight);
}

Result<void> validateLights(const std::vector<Light>& lights, ErrorCode code) {
    // Strictly ascending, which also makes each slot unique.
    for (std::size_t i = 1; i < lights.size(); ++i)
        if (!(lights[i - 1].exportIndex < lights[i].exportIndex))
            return fail(code, "LITE: light " + std::to_string(i)
                                  + "'s exportIndex does not sort strictly after the one before it");
    return {};
}

std::vector<std::byte> encodeLights(const std::vector<Light>& lights) {
    Sink sink;
    sink.putVector(lights, putLight);
    return std::move(sink).take();
}

} // namespace uta::ubundle::detail

// The FLAM section: the flames the renderer draws facing the camera --
// docs/specs/UTA-0263-shader-flames.md SS 4.3 and INV-2.

#include "Sections.h"

#include <cmath>
#include <string>

namespace uta::ubundle::detail {
namespace {

/// SS 4.3's record: u32 material, three f32 of base, f32 width, f32 height,
/// u32 seed, i32 light.
constexpr std::uint64_t FLAME_RECORD = 32;

[[nodiscard]] bool finitePositive(float value) noexcept { return std::isfinite(value) && value > 0; }

} // namespace

Result<Flame> readFlame(Cursor& cursor) {
    Flame flame;
    UTA_TRY(flame.material, cursor.readU32());
    for (float& coordinate : flame.base) {
        // Braced: UTA_TRY is three statements.
        UTA_TRY(coordinate, cursor.readF32());
    }
    UTA_TRY(flame.width, cursor.readF32());
    UTA_TRY(flame.height, cursor.readF32());
    UTA_TRY(flame.seed, cursor.readU32());
    UTA_TRY(flame.light, cursor.readI32());
    return flame;
}

void putFlame(Sink& sink, const Flame& flame) {
    sink.putU32(flame.material);
    for (const float coordinate : flame.base) sink.putF32(coordinate);
    sink.putF32(flame.width);
    sink.putF32(flame.height);
    sink.putU32(flame.seed);
    sink.putI32(flame.light);
}

Result<std::vector<Flame>> readFlames(Cursor& cursor) {
    return readVector<Flame>(cursor, FLAME_RECORD, "flames", readFlame);
}

Result<std::vector<std::byte>> encodeFlames(const std::vector<Flame>& flames) {
    Sink sink;
    sink.putVector(flames, putFlame);
    return std::move(sink).finish("FLAM");
}

Result<void> validateFlames(const Bundle& bundle, ErrorCode code) {
    if (!bundle.flames) return {};
    const std::size_t lights = bundle.lights ? bundle.lights->size() : 0;
    for (std::size_t i = 0; i < bundle.flames->size(); ++i)
        UTA_CHECK(validateFlame(bundle, (*bundle.flames)[i], lights, "FLAM: record " + std::to_string(i), code));
    return {};
}

Result<void> validateFlame(const Bundle& bundle, const Flame& flame, std::size_t lights, const std::string& where,
                           ErrorCode code) {
    const std::size_t materials = bundle.materials ? bundle.materials->size() : 0;
    if (flame.material >= materials)
        return fail(code, where + " names material " + std::to_string(flame.material) + ", and MATS holds "
                              + std::to_string(materials));
    if (!(*bundle.materials)[flame.material].flame)
        return fail(code, where + " names material " + std::to_string(flame.material) + ", which has no flame look");
    if (flame.light < -1 || (flame.light >= 0 && static_cast<std::size_t>(flame.light) >= lights))
        return fail(code, where + " names light " + std::to_string(flame.light) + ", and only "
                              + std::to_string(lights) + " may be named");
    if (!finitePositive(flame.width) || !finitePositive(flame.height))
        return fail(code, where + "'s width or height is not finite and positive");
    for (const float coordinate : flame.base)
        if (!std::isfinite(coordinate)) return fail(code, where + "'s base is not finite");
    return {};
}

} // namespace uta::ubundle::detail

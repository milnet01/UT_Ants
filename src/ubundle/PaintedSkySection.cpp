// The PSKY section: the painted sky a map's recipe opens --
// docs/specs/UTA-0350-painted-skies.md SS 4.3 and INV-3.
//
// Eight f32: top, horizon, clouds and (UTA-0391) the haze's start. Its rules
// are its own, so it is validated as soon as it is decoded.

#include "Sections.h"

#include <string>
#include <utility>

namespace uta::ubundle::detail {
namespace {

/// `NaN` fails both comparisons, so it is refused with the out-of-range values.
bool inUnit(float value) noexcept { return value >= 0 && value <= 1; }

} // namespace

Result<PaintedSky> readPaintedSky(Cursor& cursor) {
    PaintedSky sky;
    for (float& channel : sky.top) {
        UTA_TRY(channel, cursor.readF32());
    }
    for (float& channel : sky.horizon) {
        UTA_TRY(channel, cursor.readF32());
    }
    UTA_TRY(sky.clouds, cursor.readF32());
    UTA_TRY(sky.hazeStart, cursor.readF32());
    return sky;
}

Result<void> validatePaintedSky(const PaintedSky& sky, ErrorCode code) {
    for (std::size_t c = 0; c < 3; ++c) {
        if (!inUnit(sky.top[c]))
            return fail(code, "PSKY: top channel " + std::to_string(c) + " is not in [0, 1]");
        if (!inUnit(sky.horizon[c]))
            return fail(code, "PSKY: horizon channel " + std::to_string(c) + " is not in [0, 1]");
    }
    if (!inUnit(sky.clouds)) return fail(code, "PSKY: the cloud cover is not in [0, 1]");
    if (!(sky.hazeStart >= 0.1f && sky.hazeStart <= 0.9f))
        return fail(code, "PSKY: the haze's start is not in [0.1, 0.9]");
    return {};
}

Result<std::vector<std::byte>> encodePaintedSky(const PaintedSky& sky) {
    Sink sink;
    for (const float channel : sky.top) sink.putF32(channel);
    for (const float channel : sky.horizon) sink.putF32(channel);
    sink.putF32(sky.clouds);
    sink.putF32(sky.hazeStart);
    return std::move(sink).finish("PSKY");
}

} // namespace uta::ubundle::detail

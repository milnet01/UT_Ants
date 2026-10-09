// The SUN section: the sun a map's recipe declares --
// docs/specs/UTA-0338-baked-sun.md SS 4.2 and INV-3.
//
// Its own record is read and written here. Its rules reach LPRB's `sunSeen`
// and SMSK's pairs, so validateSun is a rule across sections and runs once
// every section is decoded.

#include "Sections.h"

#include <cmath>
#include <string>
#include <utility>

namespace uta::ubundle::detail {

Result<Sun> readSun(Cursor& cursor) {
    Sun sun;
    UTA_TRY(sun.yaw, cursor.readI32());
    UTA_TRY(sun.pitch, cursor.readI32());
    UTA_TRY(sun.hue, cursor.readU8());
    UTA_TRY(sun.saturation, cursor.readU8());
    UTA_TRY(sun.brightness, cursor.readU8());
    UTA_TRY(sun.levelBrightness, cursor.readF32());
    return sun;
}

Result<std::vector<std::byte>> encodeSun(const Sun& sun) {
    Sink sink;
    sink.putI32(sun.yaw);
    sink.putI32(sun.pitch);
    sink.putU8(sun.hue);
    sink.putU8(sun.saturation);
    sink.putU8(sun.brightness);
    sink.putF32(sun.levelBrightness);
    return std::move(sink).finish("SUN");
}

Result<void> validateSun(const Bundle& bundle, ErrorCode code) {
    if (bundle.sun) {
        const Sun& sun = *bundle.sun;
        if (sun.pitch <= 0 || sun.pitch > 16384)
            return fail(code, "SUN: pitch " + std::to_string(sun.pitch) + " is not in (0, 16384]");
        if (sun.brightness == 0) return fail(code, "SUN: brightness is 0");
        if (!(std::isfinite(sun.levelBrightness) && sun.levelBrightness >= 0))
            return fail(code, "SUN: the level brightness is negative or not finite");
    }
    // LPRB's visibility is there exactly when the sun is, one per probe.
    if (bundle.lightProbes) {
        const LightProbes& probes = *bundle.lightProbes;
        const std::size_t want = bundle.sun ? probes.probes.size() : 0;
        if (probes.sunSeen.size() != want)
            return fail(code, "LPRB: " + std::to_string(probes.sunSeen.size()) + " sun visibilities, and "
                                  + (bundle.sun ? "its " + std::to_string(want) + " probes need as many"
                                                : std::string("SUN is absent")));
        for (std::size_t p = 0; p < probes.sunSeen.size(); ++p)
            if (!(probes.sunSeen[p] >= 0 && probes.sunSeen[p] <= 1))
                return fail(code, "LPRB: probe " + std::to_string(p) + "'s sun visibility is not in [0, 1]");
    }
    // SS 3: movers cast no sun shadow, so no sun pair waits on one.
    if (bundle.sun && bundle.shadowMask && bundle.lights) {
        const std::size_t index = bundle.lights->size() + (bundle.lamps ? bundle.lamps->size() : 0);
        const std::vector<MaskPair>& pairs = bundle.shadowMask->pairs;
        for (std::size_t k = 0; k < pairs.size(); ++k)
            if (pairs[k].light == index && pairs[k].moverReach != 0)
                return fail(code, "SMSK: pair " + std::to_string(k) + " is the sun's and has moverReach");
    }
    return {};
}

} // namespace uta::ubundle::detail

// The LPRB section: the level's light probes --
// docs/specs/UTA-0112-baked-light-probes.md SS 4.2, INV-1.
//
// Every rule is the validator's, on both paths: the spacing, the order, and
// each cube value. Nothing is refused inside the element reader.

#include "Sections.h"

#include <cmath>
#include <string>
#include <tuple>
#include <utility>

namespace uta::ubundle::detail {
namespace {

/// SS 4.2: three i32, then eighteen f32 -- fixed.
constexpr std::uint64_t LIGHT_PROBE_SIZE = 84;

[[nodiscard]] Result<LightProbe> readProbe(Cursor& cursor) {
    LightProbe probe;
    for (std::int32_t& part : probe.cell) {
        UTA_TRY(part, cursor.readI32());
    }
    for (auto& face : probe.cube)
        for (float& channel : face) {
            UTA_TRY(channel, cursor.readF32());
        }
    return probe;
}

void putProbe(Sink& sink, const LightProbe& probe) {
    for (const std::int32_t part : probe.cell) sink.putI32(part);
    for (const auto& face : probe.cube)
        for (const float channel : face) sink.putF32(channel);
}

/// SS 4.2's order: z, then y, then x.
[[nodiscard]] std::tuple<std::int32_t, std::int32_t, std::int32_t> orderOf(
    const LightProbe& probe) noexcept {
    return {probe.cell[2], probe.cell[1], probe.cell[0]};
}

} // namespace

Result<LightProbes> readLightProbes(Cursor& cursor) {
    LightProbes probes;
    UTA_TRY(probes.spacing, cursor.readU32());
    UTA_TRY(probes.probes,
            readVector<LightProbe>(cursor, LIGHT_PROBE_SIZE, "light probes", readProbe));
    return probes;
}

Result<void> validateLightProbes(const LightProbes& probes, ErrorCode code) {
    if (probes.spacing == 0) return fail(code, "LPRB: the spacing is 0");
    for (std::size_t p = 0; p < probes.probes.size(); ++p) {
        // Strictly ascending, which also makes each cell unique.
        if (p > 0 && !(orderOf(probes.probes[p - 1]) < orderOf(probes.probes[p])))
            return fail(code, "LPRB: probe " + std::to_string(p)
                                  + "'s cell does not sort strictly after the one before it");
        for (std::size_t face = 0; face < 6; ++face)
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const float value = probes.probes[p].cube[face][channel];
                // -0.0 is not below zero, so it is kept.
                if (!std::isfinite(value) || value < 0)
                    return fail(code, "LPRB: probe " + std::to_string(p) + "'s face "
                                          + std::to_string(face) + " channel "
                                          + std::to_string(channel)
                                          + " is negative or not finite");
            }
    }
    return {};
}

std::vector<std::byte> encodeLightProbes(const LightProbes& probes) {
    Sink sink;
    sink.putU32(probes.spacing);
    sink.putVector(probes.probes, putProbe);
    return std::move(sink).take();
}

} // namespace uta::ubundle::detail

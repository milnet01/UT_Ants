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
/// UTA-0256 SS 4.2: an added-lamps cube, eighteen f32.
constexpr std::uint64_t CUBE_SIZE = 72;

using Cube = std::array<std::array<float, 3>, 6>;

[[nodiscard]] Result<Cube> readCube(Cursor& cursor) {
    Cube cube{};
    for (auto& face : cube)
        for (float& channel : face) {
            UTA_TRY(channel, cursor.readF32());
        }
    return cube;
}

void putCube(Sink& sink, const Cube& cube) {
    for (const auto& face : cube)
        for (const float channel : face) sink.putF32(channel);
}

/// Every value finite and not below zero; -0.0 is not below zero, so it is kept.
[[nodiscard]] Result<void> validateCube(const Cube& cube, const std::string& what, ErrorCode code) {
    for (std::size_t face = 0; face < 6; ++face)
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const float value = cube[face][channel];
            if (!std::isfinite(value) || value < 0)
                return fail(code, "LPRB: " + what + "'s face " + std::to_string(face) + " channel "
                                      + std::to_string(channel) + " is negative or not finite");
        }
    return {};
}

[[nodiscard]] Result<LightProbe> readProbe(Cursor& cursor) {
    LightProbe probe;
    for (std::int32_t& part : probe.cell) {
        UTA_TRY(part, cursor.readI32());
    }
    UTA_TRY(probe.cube, readCube(cursor));
    return probe;
}

void putProbe(Sink& sink, const LightProbe& probe) {
    for (const std::int32_t part : probe.cell) sink.putI32(part);
    putCube(sink, probe.cube);
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
    UTA_TRY(probes.added, readVector<Cube>(cursor, CUBE_SIZE, "added-lamp cubes", readCube));
    // UTA-0338 SS 4.2: its count and range are SUN's rules, in validateSun.
    UTA_TRY(probes.sunSeen, readVector<float>(cursor, 4, "sun visibilities", readF32Element));
    return probes;
}

Result<void> validateLightProbes(const LightProbes& probes, ErrorCode code) {
    if (probes.spacing == 0) return fail(code, "LPRB: the spacing is 0");
    for (std::size_t p = 0; p < probes.probes.size(); ++p) {
        // Strictly ascending, which also makes each cell unique.
        if (p > 0 && !(orderOf(probes.probes[p - 1]) < orderOf(probes.probes[p])))
            return fail(code, "LPRB: probe " + std::to_string(p)
                                  + "'s cell does not sort strictly after the one before it");
        UTA_CHECK(validateCube(probes.probes[p].cube, "probe " + std::to_string(p), code));
    }
    // How many there should be is LAMP's rule, in validateLamps.
    for (std::size_t p = 0; p < probes.added.size(); ++p)
        UTA_CHECK(validateCube(probes.added[p], "added-lamp cube " + std::to_string(p), code));
    return {};
}

Result<std::vector<std::byte>> encodeLightProbes(const LightProbes& probes) {
    Sink sink;
    sink.putU32(probes.spacing);
    sink.putVector(probes.probes, putProbe);
    sink.putVector(probes.added, putCube);
    sink.putVector(probes.sunSeen, [](Sink& out, float seen) { out.putF32(seen); });
    return std::move(sink).finish("LPRB");
}

} // namespace uta::ubundle::detail

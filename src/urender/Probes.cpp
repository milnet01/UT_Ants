// A level's probes on the GPU -- Probes.h.

#include "urender/Probes.h"

#include <algorithm>

namespace uta::urender {

namespace {

/// The smallest table this builds, so an empty level still binds a buffer.
constexpr std::uint64_t MINIMUM_SLOTS = 16;

} // namespace

ProbeTable probeTable(const std::optional<ubundle::LightProbes>& lightProbes) {
    ProbeTable table;
    const std::size_t count = lightProbes ? lightProbes->probes.size() : 0;

    std::uint64_t slots = MINIMUM_SLOTS;
    while (slots < 2 * static_cast<std::uint64_t>(count)) slots <<= 1;
    table.tableMask = static_cast<std::uint32_t>(slots - 1);
    table.cells.assign(slots, gpu::ProbeCell{{0, 0, 0}, -1});
    if (count == 0) return table;

    table.spacing = lightProbes->spacing;
    table.probes.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const ubundle::LightProbe& probe = lightProbes->probes[i];
        gpu::Probe record{};
        for (std::size_t face = 0; face < 6; ++face)
            for (std::size_t channel = 0; channel < 3; ++channel)
                record.faces[face * 4 + channel] = probe.cube[face][channel];
        table.probes.push_back(record);

        // Linear probing, as probes.glsl's probeAt searches. LPRB's cells are
        // strictly ascending, so no cell is placed twice.
        const std::uint32_t home = probeHash(probe.cell) & table.tableMask;
        std::uint32_t run = 0;
        while (table.cells[(home + run) & table.tableMask].probe >= 0) ++run;
        table.cells[(home + run) & table.tableMask] = {probe.cell, static_cast<std::int32_t>(i)};
        table.longestRun = std::max(table.longestRun, run);
    }
    return table;
}

} // namespace uta::urender

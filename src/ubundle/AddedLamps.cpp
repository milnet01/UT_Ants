// Folding a bundle's added lamps into what the renderer draws --
// docs/specs/UTA-0256-added-lamps.md SS 4.4 and SS 4.8, and UTA-0338 SS 4.2's
// rename of the sun's pairs.

#include "ubundle/Bundle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace uta::ubundle {

namespace {

/// Each cube of `layer` added to its probe's -- light adds.
void addLayer(LightProbes& probes, const std::vector<std::array<std::array<float, 3>, 6>>& layer) {
    if (layer.size() != probes.probes.size()) return;
    for (std::size_t p = 0; p < probes.probes.size(); ++p)
        for (std::size_t face = 0; face < 6; ++face)
            for (std::size_t channel = 0; channel < 3; ++channel)
                probes.probes[p].cube[face][channel] += layer[p][face][channel];
}

} // namespace

void applyAddedLamps(Bundle& bundle, bool on) {
    if (!bundle.lights) bundle.lights.emplace();
    const auto lite = static_cast<std::uint32_t>(bundle.lights->size());
    const std::size_t records = bundle.lamps ? bundle.lamps->size() : 0;

    // SS 4.8: every record names light `lite + k`; the kept kind's lights join
    // LITE in record order, so record k's new index is `rename[k]`, or none.
    std::vector<std::optional<std::uint32_t>> rename(records);
    std::uint32_t joined = 0;
    for (std::size_t k = 0; k < records; ++k) {
        AddedLamp& lamp = (*bundle.lamps)[k];
        if (lamp.restores == on) continue;
        const auto index = lite + joined++;
        rename[k] = index;
        for (Flame flame : lamp.flames) {
            flame.light = static_cast<std::int32_t>(index);
            if (!bundle.flames) bundle.flames.emplace();
            bundle.flames->push_back(flame);
        }
        if (!lamp.shape.indices.empty()) {
            if (!bundle.movers) bundle.movers.emplace();
            bundle.movers->push_back(MoverShape{lamp.light.exportIndex, {}, {}, {1, 1, 1}, std::move(lamp.shape)});
        }
        bundle.lights->push_back(lamp.light);
    }
    // SS 4.7: the probes hold the map's own light whole, which is what off
    // draws; on, the cubes gathered without the switched-off lights stand in
    // for them, and the lamps' bounce adds.
    if (bundle.lightProbes && on) {
        LightProbes& probes = *bundle.lightProbes;
        if (probes.switched.size() == probes.probes.size())
            for (std::size_t p = 0; p < probes.probes.size(); ++p) probes.probes[p].cube = probes.switched[p];
        addLayer(probes, probes.added);
    }

    // The dropped kind's pairs go; every chart keeps its own, in order. The
    // sun's, past LAMP's, are renamed to LITE's new size, so `applySun` puts
    // the sun at the index they name (UTA-0338 SS 4.2).
    if (bundle.shadowMask && records != 0) {
        const auto sun = static_cast<std::uint32_t>(lite + records);
        ShadowMask& mask = *bundle.shadowMask;
        std::vector<MaskPair> kept;
        kept.reserve(mask.pairs.size());
        for (MaskChart& chart : mask.charts) {
            const auto first = static_cast<std::uint32_t>(kept.size());
            for (std::uint32_t k = chart.firstPair; k < chart.firstPair + chart.pairCount; ++k) {
                const MaskPair& pair = mask.pairs[k];
                if (pair.light < lite) {
                    kept.push_back(pair);
                } else if (pair.light < sun) {
                    if (const auto index = rename[pair.light - lite]) {
                        kept.push_back(pair);
                        kept.back().light = *index;
                    }
                } else if (bundle.sun && pair.light == sun) {
                    kept.push_back(pair);
                    kept.back().light = lite + joined;
                }
            }
            chart.firstPair = first;
            chart.pairCount = static_cast<std::uint32_t>(kept.size()) - first;
        }
        mask.pairs = std::move(kept);
    }
    if (bundle.lightProbes) {
        bundle.lightProbes->added.clear();
        bundle.lightProbes->switched.clear();
    }
    bundle.lamps.reset();
}

} // namespace uta::ubundle

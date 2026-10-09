// Folding a bundle's added lamps into what the renderer draws --
// docs/specs/UTA-0256-added-lamps.md SS 4.4, and UTA-0338 SS 4.2's rename of the
// sun's pairs.

#include "ubundle/Bundle.h"

#include <cstddef>
#include <utility>

namespace uta::ubundle {

void applyAddedLamps(Bundle& bundle, bool on) {
    if (!bundle.lights) bundle.lights.emplace();
    const auto lite = static_cast<std::uint32_t>(bundle.lights->size());

    if (on && bundle.lamps) {
        for (std::size_t k = 0; k < bundle.lamps->size(); ++k) {
            AddedLamp& lamp = (*bundle.lamps)[k];
            const auto index = static_cast<std::int32_t>(lite + k);
            for (Flame flame : lamp.flames) {
                flame.light = index;
                if (!bundle.flames) bundle.flames.emplace();
                bundle.flames->push_back(flame);
            }
            if (!lamp.shape.indices.empty()) {
                if (!bundle.movers) bundle.movers.emplace();
                bundle.movers->push_back(MoverShape{lamp.light.exportIndex, {}, {}, {1, 1, 1}, std::move(lamp.shape)});
            }
            bundle.lights->push_back(lamp.light);
        }
        if (bundle.lightProbes && bundle.lightProbes->added.size() == bundle.lightProbes->probes.size())
            for (std::size_t p = 0; p < bundle.lightProbes->probes.size(); ++p)
                for (std::size_t face = 0; face < 6; ++face)
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        bundle.lightProbes->probes[p].cube[face][channel] +=
                            bundle.lightProbes->added[p][face][channel];
    } else if (bundle.shadowMask) {
        // The lamps' pairs go; every chart keeps its own, in order. The sun's,
        // past the lamps', are renamed to LITE's size, so `applySun` puts the
        // sun at the index they name (UTA-0338 SS 4.2).
        const auto sun = static_cast<std::uint32_t>(lite + (bundle.lamps ? bundle.lamps->size() : 0));
        ShadowMask& mask = *bundle.shadowMask;
        std::vector<MaskPair> kept;
        kept.reserve(mask.pairs.size());
        for (MaskChart& chart : mask.charts) {
            const auto first = static_cast<std::uint32_t>(kept.size());
            for (std::uint32_t k = chart.firstPair; k < chart.firstPair + chart.pairCount; ++k)
                if (mask.pairs[k].light < lite) {
                    kept.push_back(mask.pairs[k]);
                } else if (bundle.sun && mask.pairs[k].light == sun) {
                    kept.push_back(mask.pairs[k]);
                    kept.back().light = lite;
                }
            chart.firstPair = first;
            chart.pairCount = static_cast<std::uint32_t>(kept.size()) - first;
        }
        mask.pairs = std::move(kept);
    }
    if (bundle.lightProbes) bundle.lightProbes->added.clear();
    bundle.lamps.reset();
}

} // namespace uta::ubundle

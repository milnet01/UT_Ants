// docs/specs/UTA-0105-shader-liquids.md SS 4.1 to SS 4.3.

#include "ubake/Liquids.h"

#include "ubake/LightModel.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

namespace uta::ubake {

std::optional<ubundle::LiquidKind> liquidKindOf(std::string_view foldedClassName) noexcept {
    if (foldedClassName == "wettexture") return ubundle::LiquidKind::Wet;
    if (foldedClassName == "icetexture") return ubundle::LiquidKind::Ice;
    if (foldedClassName == "wavetexture") return ubundle::LiquidKind::Wave;
    return std::nullopt;
}

std::optional<ubundle::LiquidLook> liquidLookOf(ubundle::LiquidKind kind, const LiquidSetting& setting,
                                                std::uint32_t width, std::uint32_t height,
                                                const upkg::Palette& palette) {
    if (width == 0 || height == 0 || width > ubundle::LIQUID_SIZE_MAX || height > ubundle::LIQUID_SIZE_MAX)
        return std::nullopt;
    const auto byteOf = [&](std::string_view folded) { return setting(folded).value_or(0); };

    ubundle::LiquidLook look;
    look.kind = kind;
    look.size = {static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height)};
    if (kind == ubundle::LiquidKind::Ice) {
        // Epic's manual: the pan speeds' zero is 128. A class that sets none
        // leaves them at UnrealScript's 0, which is a pan, and is carried so.
        look.amplitude = byteOf("amplitude");
        look.frequency = byteOf("frequency");
        look.panning = byteOf("panningstyle");
        look.pan = {byteOf("horizpanspeed"), byteOf("vertpanspeed")};
        return look;
    }
    look.amplitude = byteOf("waveamp");
    look.frequency = byteOf("fx_frequency");
    if (kind == ubundle::LiquidKind::Wave) {
        look.bump = {byteOf("bumpmaplight"), byteOf("bumpmapangle"), byteOf("phongsize")};
        look.ramp = liquidRampOf(palette);
    }
    return look;
}

std::array<std::array<float, 3>, 8> liquidRampOf(const upkg::Palette& palette) {
    std::array<std::array<float, 3>, 8> ramp{};
    if (palette.entries.empty()) return ramp;
    // Rec. 709 weights on the stored values: an order, not a measurement, so
    // the encoding does not matter. Stable, so equal lumas keep palette order.
    const auto luma = [&](std::size_t i) {
        const upkg::PaletteEntry& e = palette.entries[i];
        return 0.2126 * e.r + 0.7152 * e.g + 0.0722 * e.b;
    };
    std::vector<std::size_t> order(palette.entries.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return luma(a) < luma(b); });
    const std::size_t last = ramp.size() - 1;
    const std::size_t top = order.size() - 1;
    for (std::size_t i = 0; i < ramp.size(); ++i) {
        const upkg::PaletteEntry& entry = palette.entries[order[(i * top + last / 2) / last]];
        ramp[i] = {static_cast<float>(linearOf(entry.r)), static_cast<float>(linearOf(entry.g)),
                   static_cast<float>(linearOf(entry.b))};
    }
    return ramp;
}

} // namespace uta::ubake

// The SMSK section: the level's baked shadow mask, and the rules tying it to
// GEOM's vertices and LITE's lights --
// docs/specs/UTA-0326-baked-shadow-mask.md SS 4.2 and INV-1.

#include "Sections.h"

#include <cmath>
#include <cstring>
#include <string>

namespace uta::ubundle::detail {
namespace {

/// Two f32.
constexpr std::uint64_t TEXEL_ENTRY = 8;
/// Two u32 and two u16.
constexpr std::uint64_t CHART_ENTRY = 12;
/// A u32, two u16 and four u8.
constexpr std::uint64_t PAIR_ENTRY = 12;

[[nodiscard]] Result<std::array<float, 2>> readTexel(Cursor& cursor) {
    std::array<float, 2> texel{};
    UTA_TRY(texel[0], cursor.readF32());
    UTA_TRY(texel[1], cursor.readF32());
    return texel;
}

void putTexel(Sink& sink, const std::array<float, 2>& texel) {
    sink.putF32(texel[0]);
    sink.putF32(texel[1]);
}

[[nodiscard]] Result<MaskChart> readChart(Cursor& cursor) {
    MaskChart chart;
    UTA_TRY(chart.firstPair, cursor.readU32());
    UTA_TRY(chart.pairCount, cursor.readU32());
    UTA_TRY(chart.width, cursor.readU16());
    UTA_TRY(chart.height, cursor.readU16());
    return chart;
}

void putChart(Sink& sink, const MaskChart& chart) {
    sink.putU32(chart.firstPair);
    sink.putU32(chart.pairCount);
    sink.putU16(chart.width);
    sink.putU16(chart.height);
}

[[nodiscard]] Result<MaskPair> readPair(Cursor& cursor) {
    MaskPair pair;
    UTA_TRY(pair.light, cursor.readU32());
    UTA_TRY(pair.x, cursor.readU16());
    UTA_TRY(pair.y, cursor.readU16());
    UTA_TRY(pair.moverReach, cursor.readU8());
    for (std::uint8_t& part : pair.reserved) {
        // Braced: UTA_TRY is three statements.
        UTA_TRY(part, cursor.readU8());
    }
    return pair;
}

void putPair(Sink& sink, const MaskPair& pair) {
    sink.putU32(pair.light);
    sink.putU16(pair.x);
    sink.putU16(pair.y);
    sink.putU8(pair.moverReach);
    for (const std::uint8_t part : pair.reserved) sink.putU8(part);
}

[[nodiscard]] bool within(float c, std::uint16_t side) noexcept {
    return std::isfinite(c) && c >= 0 && c <= static_cast<float>(side);
}

} // namespace

Result<ShadowMask> readShadowMask(Cursor& cursor) {
    ShadowMask mask;
    UTA_TRY(mask.texelSize, cursor.readF32());
    UTA_TRY(mask.width, cursor.readU32());
    UTA_TRY(mask.height, cursor.readU32());
    UTA_TRY(mask.vertexChart, readVector<std::uint32_t>(cursor, MIN_U32, "mask vertex charts", readU32Element));
    using Texel = std::array<float, 2>;
    UTA_TRY(mask.vertexTexel, readVector<Texel>(cursor, TEXEL_ENTRY, "mask vertex texels", readTexel));
    UTA_TRY(mask.charts, readVector<MaskChart>(cursor, CHART_ENTRY, "mask charts", readChart));
    UTA_TRY(mask.pairs, readVector<MaskPair>(cursor, PAIR_ENTRY, "mask pairs", readPair));
    // The texels as one run, as AOCC's: the bound is readBytes'.
    UTA_TRY(const std::uint32_t count, cursor.readU32());
    UTA_TRY(const std::span<const std::byte> raw, cursor.readBytes(count));
    mask.texels.resize(raw.size());
    if (!raw.empty()) std::memcpy(mask.texels.data(), raw.data(), raw.size());
    return mask;
}

Result<void> validateShadowMask(const ShadowMask& mask, ErrorCode code) {
    if (!std::isfinite(mask.texelSize) || mask.texelSize <= 0)
        return fail(code, "SMSK: a texel size of " + std::to_string(mask.texelSize)
                              + ", where only a finite positive size is allowed");
    for (const std::uint32_t side : {mask.width, mask.height})
        if (side == 0 || side > SHADOW_MASK_ATLAS_LIMIT)
            return fail(code, "SMSK: an atlas side of " + std::to_string(side) + ", where 1 to "
                                  + std::to_string(SHADOW_MASK_ATLAS_LIMIT) + " are allowed");
    if (mask.texels.size() != std::uint64_t{mask.width} * mask.height)
        return fail(code, "SMSK: " + std::to_string(mask.texels.size()) + " texels for a "
                              + std::to_string(mask.width) + " by " + std::to_string(mask.height) + " atlas");
    // Both are GEOM's vertex count, which validateShadowMaskVertices checks;
    // equal here so the loop below indexes neither past its end.
    if (mask.vertexChart.size() != mask.vertexTexel.size())
        return fail(code, "SMSK: " + std::to_string(mask.vertexChart.size()) + " vertex charts and "
                              + std::to_string(mask.vertexTexel.size()) + " vertex texels");
    for (std::size_t i = 0; i < mask.vertexChart.size(); ++i) {
        const std::uint32_t chart = mask.vertexChart[i];
        const std::array<float, 2>& texel = mask.vertexTexel[i];
        const std::string where = "SMSK: vertex " + std::to_string(i);
        if (chart == MASK_NO_CHART) {
            if (!std::isfinite(texel[0]) || !std::isfinite(texel[1]))
                return fail(code, where + " has a texel that is not finite");
            continue;
        }
        if (chart >= mask.charts.size())
            return fail(code, where + " names chart " + std::to_string(chart) + ", and SMSK holds "
                                  + std::to_string(mask.charts.size()));
        if (!within(texel[0], mask.charts[chart].width) || !within(texel[1], mask.charts[chart].height))
            return fail(code, where + " has a texel outside its chart's rectangle");
    }
    // Every run inside `pairs`, and no pair in two runs.
    std::vector<bool> claimed(mask.pairs.size(), false);
    for (std::size_t c = 0; c < mask.charts.size(); ++c) {
        const MaskChart& chart = mask.charts[c];
        const std::string where = "SMSK: chart " + std::to_string(c);
        if (!runWithin(chart.firstPair, chart.pairCount, mask.pairs.size()))
            return fail(code, where + "'s pair run passes the end of the pairs");
        for (std::uint32_t k = chart.firstPair; k < chart.firstPair + chart.pairCount; ++k) {
            if (claimed[k]) return fail(code, where + "'s pair run overlaps another chart's");
            claimed[k] = true;
            const MaskPair& pair = mask.pairs[k];
            if (k > chart.firstPair && pair.light <= mask.pairs[k - 1].light)
                return fail(code, where + "'s pairs are not strictly ascending by light");
            if (pair.x == MASK_ALL_LIT && pair.y == MASK_ALL_LIT) continue;
            if (std::uint32_t{pair.x} + chart.width > mask.width || std::uint32_t{pair.y} + chart.height > mask.height)
                return fail(code, where + "'s pair " + std::to_string(k) + " has a rectangle passing the atlas");
        }
    }
    for (std::size_t k = 0; k < mask.pairs.size(); ++k) {
        const MaskPair& pair = mask.pairs[k];
        if (pair.moverReach > 1)
            return fail(code, "SMSK: pair " + std::to_string(k) + "'s mover byte "
                                  + std::to_string(pair.moverReach) + " is not 0 or 1");
        for (const std::uint8_t part : pair.reserved)
            if (part != 0) return fail(code, "SMSK: pair " + std::to_string(k) + "'s reserved bytes are not zero");
    }
    return {};
}

Result<std::vector<std::byte>> encodeShadowMask(const ShadowMask& mask) {
    Sink sink;
    sink.putF32(mask.texelSize);
    sink.putU32(mask.width);
    sink.putU32(mask.height);
    sink.putVector(mask.vertexChart, [](Sink& out, std::uint32_t chart) { out.putU32(chart); });
    sink.putVector(mask.vertexTexel, putTexel);
    sink.putVector(mask.charts, putChart);
    sink.putVector(mask.pairs, putPair);
    sink.putCount(mask.texels.size());
    for (const std::uint8_t texel : mask.texels) sink.putU8(texel);
    return std::move(sink).finish("SMSK");
}

Result<void> validateShadowMaskAcross(const Bundle& bundle, ErrorCode code) {
    if (!bundle.shadowMask) return {};
    const ShadowMask& mask = *bundle.shadowMask;
    if (!bundle.geometry) return fail(code, "SMSK is present and the bundle has no GEOM");
    const std::size_t vertices = bundle.geometry->vertices.size();
    if (mask.vertexChart.size() != vertices || mask.vertexTexel.size() != vertices)
        return fail(code, "SMSK holds " + std::to_string(mask.vertexChart.size()) + " vertex charts and "
                              + std::to_string(mask.vertexTexel.size()) + " vertex texels, and GEOM "
                              + std::to_string(vertices) + " vertices");
    if (!bundle.lights) return fail(code, "SMSK is present and the bundle has no LITE");
    // UTA-0256 SS 4.2: past LITE's end, a pair names a LAMP lamp's light.
    const std::size_t lamps = bundle.lamps ? bundle.lamps->size() : 0;
    const std::size_t lights = bundle.lights->size() + lamps;
    for (std::size_t k = 0; k < mask.pairs.size(); ++k)
        if (mask.pairs[k].light >= lights)
            return fail(code, "SMSK: pair " + std::to_string(k) + " names light "
                                  + std::to_string(mask.pairs[k].light) + ", and LITE holds "
                                  + std::to_string(bundle.lights->size())
                                  + (lamps == 0 ? "" : " and LAMP " + std::to_string(lamps)));
    return {};
}

} // namespace uta::ubundle::detail

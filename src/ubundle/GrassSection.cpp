// The VEGE section: the grass a bake planted on a map's floors --
// docs/specs/UTA-0293-grass.md SS 4.3 and INV-7.
//
// Two vectors: the clumps, then the cells that hold runs of them. A clump
// names MATS and SMSK, so validateGrass is a rule across sections and runs
// once every section is decoded.

#include "Sections.h"

#include <cmath>
#include <string>
#include <utility>

namespace uta::ubundle::detail {
namespace {

/// Three f32, a u32, two f32, a u32, two f32 and a u32.
constexpr std::uint64_t CLUMP_ENTRY = 40;
/// Two u32 and six f32.
constexpr std::uint64_t CELL_ENTRY = 32;

template <std::size_t N>
[[nodiscard]] Result<void> readFloats(Cursor& cursor, std::array<float, N>& values) {
    for (float& value : values) {
        // Braced: UTA_TRY is three statements.
        UTA_TRY(value, cursor.readF32());
    }
    return {};
}

template <std::size_t N>
void putFloats(Sink& sink, const std::array<float, N>& values) {
    for (const float value : values) sink.putF32(value);
}

[[nodiscard]] Result<GrassClump> readClump(Cursor& cursor) {
    GrassClump clump;
    UTA_CHECK(readFloats(cursor, clump.base));
    UTA_TRY(clump.material, cursor.readU32());
    UTA_CHECK(readFloats(cursor, clump.uv));
    UTA_TRY(clump.maskChart, cursor.readU32());
    UTA_CHECK(readFloats(cursor, clump.maskTexel));
    UTA_TRY(clump.seed, cursor.readU32());
    return clump;
}

void putClump(Sink& sink, const GrassClump& clump) {
    putFloats(sink, clump.base);
    sink.putU32(clump.material);
    putFloats(sink, clump.uv);
    sink.putU32(clump.maskChart);
    putFloats(sink, clump.maskTexel);
    sink.putU32(clump.seed);
}

[[nodiscard]] Result<GrassCell> readCell(Cursor& cursor) {
    GrassCell cell;
    UTA_TRY(cell.firstClump, cursor.readU32());
    UTA_TRY(cell.clumpCount, cursor.readU32());
    UTA_CHECK(readFloats(cursor, cell.low));
    UTA_CHECK(readFloats(cursor, cell.high));
    return cell;
}

void putCell(Sink& sink, const GrassCell& cell) {
    sink.putU32(cell.firstClump);
    sink.putU32(cell.clumpCount);
    putFloats(sink, cell.low);
    putFloats(sink, cell.high);
}

} // namespace

Result<Grass> readGrass(Cursor& cursor) {
    Grass grass;
    UTA_TRY(grass.clumps, readVector<GrassClump>(cursor, CLUMP_ENTRY, "clumps", readClump));
    UTA_TRY(grass.cells, readVector<GrassCell>(cursor, CELL_ENTRY, "cells", readCell));
    return grass;
}

Result<std::vector<std::byte>> encodeGrass(const Grass& grass) {
    Sink sink;
    sink.putVector(grass.clumps, putClump);
    sink.putVector(grass.cells, putCell);
    return std::move(sink).finish("VEGE");
}

Result<void> validateGrass(const Bundle& bundle, ErrorCode code) {
    if (!bundle.grass) return {};
    const Grass& grass = *bundle.grass;
    if (grass.clumps.size() > GRASS_CLUMP_LIMIT)
        return fail(code, "VEGE holds " + std::to_string(grass.clumps.size()) + " clumps, past the limit of "
                              + std::to_string(GRASS_CLUMP_LIMIT));
    const std::size_t materials = bundle.materials ? bundle.materials->size() : 0;
    const std::size_t charts = bundle.shadowMask ? bundle.shadowMask->charts.size() : 0;
    for (std::size_t i = 0; i < grass.clumps.size(); ++i) {
        const GrassClump& clump = grass.clumps[i];
        const std::string where = "VEGE: clump " + std::to_string(i);
        if (clump.material >= materials)
            return fail(code, where + " names material " + std::to_string(clump.material) + ", and MATS holds "
                                  + std::to_string(materials));
        if (clump.maskChart != MASK_NO_CHART && clump.maskChart >= charts)
            return fail(code, where + " names chart " + std::to_string(clump.maskChart) + ", and SMSK holds "
                                  + std::to_string(charts));
        for (const float coordinate : clump.base)
            if (!std::isfinite(coordinate)) return fail(code, where + "'s base is not finite");
    }
    for (std::size_t i = 0; i < grass.cells.size(); ++i) {
        const GrassCell& cell = grass.cells[i];
        // Widened: two u32 near their top would wrap past the check.
        if (std::uint64_t{cell.firstClump} + cell.clumpCount > grass.clumps.size())
            return fail(code, "VEGE: cell " + std::to_string(i) + "'s run passes the "
                                  + std::to_string(grass.clumps.size()) + " clumps");
    }
    return {};
}

} // namespace uta::ubundle::detail

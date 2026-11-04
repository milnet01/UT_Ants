// Which grass a frame draws -- docs/specs/UTA-0293-grass.md SS 4.4 and SS 4.6.

#include "urender/Grass.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace uta::urender {
namespace {

/// Whether all eight corners of the box lie outside one side of the view, in
/// clip space: left, right, bottom, top, or behind the eye.
[[nodiscard]] bool outsideView(const GrassRun& run, const gpu::Mat4& m) noexcept {
    std::array<int, 5> outside{};
    for (int corner = 0; corner < 8; ++corner) {
        const float x = (corner & 1) != 0 ? run.high[0] : run.low[0];
        const float y = (corner & 2) != 0 ? run.high[1] : run.low[1];
        const float z = (corner & 4) != 0 ? run.high[2] : run.low[2];
        const auto row = [&](int r) { return m[0 * 4 + r] * x + m[1 * 4 + r] * y + m[2 * 4 + r] * z + m[3 * 4 + r]; };
        const float cx = row(0), cy = row(1), cw = row(3);
        outside[0] += cx < -cw ? 1 : 0;
        outside[1] += cx > cw ? 1 : 0;
        outside[2] += cy < -cw ? 1 : 0;
        outside[3] += cy > cw ? 1 : 0;
        outside[4] += cw <= 0 ? 1 : 0;
    }
    return std::ranges::any_of(outside, [](int count) { return count == 8; });
}

/// The distance from `eye` to the nearest point of the run's box.
[[nodiscard]] float distanceTo(const GrassRun& run, const std::array<float, 3>& eye) noexcept {
    float sum = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float d = std::max({run.low[axis] - eye[axis], 0.0f, eye[axis] - run.high[axis]});
        sum += d * d;
    }
    return std::sqrt(sum);
}

} // namespace

GrassReach grassReachOf(Tier tier) noexcept {
    if (!enabled(Feature::Grass, tier)) return {};
    const float end = tier == Tier::Ultra ? 2560.0f : 1536.0f;
    return {end / 3, end * 3 / 5, end};
}

std::vector<GrassRun> grassRunsOf(const ubundle::Grass& grass) {
    std::vector<GrassRun> runs;
    runs.reserve(grass.cells.size());
    for (const ubundle::GrassCell& cell : grass.cells) {
        GrassRun run{cell.firstClump, 0, cell.clumpCount, cell.low, cell.high};
        for (std::uint32_t k = cell.firstClump; k < cell.firstClump + cell.clumpCount; ++k)
            if (grass.clumps[k].seed % 2 == 0) ++run.evenCount;
        runs.push_back(run);
    }
    return runs;
}

std::vector<GrassDraw> visibleGrass(std::span<const GrassRun> runs, const gpu::Mat4& viewProj,
                                    const std::array<float, 3>& eye, Tier tier) {
    std::vector<GrassDraw> draws;
    const GrassReach reach = grassReachOf(tier);
    if (reach.end == 0) return draws;
    const bool all = tier == Tier::Ultra;
    for (const GrassRun& run : runs) {
        const std::uint32_t count = all ? run.count : run.evenCount;
        if (count == 0) continue;
        const float distance = distanceTo(run, eye);
        if (distance >= reach.end || outsideView(run, viewProj)) continue;
        draws.push_back({run.first, count, distance > reach.far ? BLADES_FAR : BLADES_NEAR});
    }
    return draws;
}

} // namespace uta::urender

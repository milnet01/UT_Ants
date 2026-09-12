// UTA-0014 INV-7 -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.7.
//
// The indirect term is UTA-0112 SS 4.9's ambient-cube sum; a point inside the
// lattice is the trilinear blend of the corners present; a point with no corner
// present receives zero indirect light.
//
// A DEVICE TEST ON PURPOSE. SS 4.7's evaluation is shader arithmetic, and
// grading it on the CPU would grade a C++ copy -- which SS 3 decision 5 forbids.
// The kernel includes shaders/probes.glsl, the file the shading pass includes,
// over the table urender::probeTable built.
//
// SIX DISTINCT FACE COLOURS ON EVERY PROBE, so a face selected by the wrong
// sign of n changes the answer; a symmetric cube would hide it. The fixture
// also crosses negative cells and forces hash collisions, since a probe the
// builder placed where the shader does not look is found by no pixel.
//
// The expected values below are this test's own reading of the two formulas,
// written from the specs rather than from probes.glsl.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/ComputeFixture.h"
#include "device/DeviceFixture.h"
#include "urender/Probes.h"
#include "urender/ShaderTypes.h"

#include "probe_eval.comp.spv.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

using uta::ubundle::LightProbe;
using uta::ubundle::LightProbes;
namespace gpu = uta::urender::gpu;

namespace {

struct Lattice {
    std::uint32_t spacing, count, tableMask, longestRun;
};
static_assert(sizeof(Lattice) == 16);

struct ProbeCase {
    std::array<float, 4> x;
    std::array<float, 4> n;
};
static_assert(sizeof(ProbeCase) == 32);
static_assert(offsetof(ProbeCase, n) == 16);

constexpr std::uint32_t SPACING = 128;
using Cell = std::array<std::int32_t, 3>;
using Vec = std::array<double, 3>;

/// Probe `id`'s face `face`, channel by channel -- every face of every probe
/// different from every other.
std::array<float, 3> faceColour(std::size_t id, std::size_t face) {
    return {static_cast<float>(0.1 * (face + 1) + 0.013 * id), static_cast<float>(0.07 * (face + 1) + 0.021 * id),
            static_cast<float>(0.9 - 0.11 * face + 0.005 * id)};
}

/// UTA-0112 SS 4.9's sum, as that spec writes it.
Vec cubeAt(const LightProbe& probe, const Vec& n) {
    const auto face = [&](int axis) -> const std::array<float, 3>& {
        return probe.cube[axis * 2 + (n[axis] >= 0 ? 0 : 1)];
    };
    Vec out{};
    for (int axis = 0; axis < 3; ++axis)
        for (int c = 0; c < 3; ++c) out[c] += n[axis] * n[axis] * face(axis)[c];
    return out;
}

/// SS 4.7's blend, as that spec writes it.
Vec expectedAt(const std::map<Cell, LightProbe>& byCell, const Vec& x, const Vec& n) {
    Vec cell{x[0] / SPACING, x[1] / SPACING, x[2] / SPACING};
    Vec sum{};
    double weights = 0;
    for (int corner = 0; corner < 8; ++corner) {
        Cell at{};
        double weight = 1;
        for (int axis = 0; axis < 3; ++axis) {
            const double base = std::floor(cell[axis]);
            const double f = cell[axis] - base;
            const int step = (corner >> axis) & 1;
            at[axis] = static_cast<std::int32_t>(base) + step;
            weight *= step ? f : 1 - f;
        }
        const auto found = byCell.find(at);
        if (found == byCell.end()) continue;
        const Vec value = cubeAt(found->second, n);
        for (int c = 0; c < 3; ++c) sum[c] += weight * value[c];
        weights += weight;
    }
    if (weights == 0) return {};
    return {sum[0] / weights, sum[1] / weights, sum[2] / weights};
}

} // namespace

TEST_CASE("INV-7: indirect light is the trilinear blend of the probes present", "[device]") {
    uta::test::render::removeDisplay();

    // Two full cells meeting at x = 0 -- one of them in negative cells -- a
    // sparse cell with two corners of eight, and a row long enough to collide.
    std::vector<Cell> cells;
    for (int x = -1; x <= 1; ++x)
        for (int y = 0; y <= 1; ++y)
            for (int z = 0; z <= 1; ++z) cells.push_back({x, y, z});
    cells.push_back({5, 0, 0});
    cells.push_back({6, 1, 1});
    for (int x = 10; x <= 40; ++x) cells.push_back({x, 0, 3});
    // LPRB is strictly ascending by z, then y, then x (UTA-0112 SS 4.2).
    std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
        return std::tie(a[2], a[1], a[0]) < std::tie(b[2], b[1], b[0]);
    });

    LightProbes lightProbes;
    lightProbes.spacing = SPACING;
    std::map<Cell, LightProbe> byCell;
    for (std::size_t id = 0; id < cells.size(); ++id) {
        LightProbe probe;
        probe.cell = cells[id];
        for (std::size_t face = 0; face < 6; ++face) probe.cube[face] = faceColour(id, face);
        lightProbes.probes.push_back(probe);
        byCell.emplace(probe.cell, probe);
    }
    const uta::urender::ProbeTable table = uta::urender::probeTable(lightProbes);
    // The fixture reaches the probing path, not just the home slot: some probe
    // sits past its hash, and a case below is evaluated AT that probe, so a
    // lookup that stopped at the home slot would miss the one corner there is.
    // A collision no case touches proves nothing about the lookup.
    std::optional<Cell> displaced;
    for (std::uint32_t slot = 0; slot <= table.tableMask && !displaced; ++slot) {
        const gpu::ProbeCell& entry = table.cells[slot];
        if (entry.probe >= 0 && (uta::urender::probeHash(entry.cell) & table.tableMask) != slot) displaced = entry.cell;
    }
    REQUIRE(displaced.has_value());
    CHECK(table.longestRun > 0u);

    const std::array<Vec, 8> normals = {{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
                                         {1.0 / 3, 2.0 / 3, -2.0 / 3}, {-2.0 / 3, 1.0 / 3, 2.0 / 3}}};
    // In cells: a probe's own point, the insides of both full cells, the sparse
    // cell, the row, and a point no probe is near.
    const std::vector<Vec> points = {{1, 1, 0},
                                     {0.25, 0.5, 0.75},
                                     {-0.6, 0.3, 0.9},
                                     {5.4, 0.3, 0.8},
                                     {17.3, 0.0, 3.0},
                                     {static_cast<double>((*displaced)[0]), static_cast<double>((*displaced)[1]),
                                      static_cast<double>((*displaced)[2])},
                                     {20.5, 20.5, 20.5}};

    std::vector<ProbeCase> cases;
    std::vector<Vec> expected;
    std::size_t zero = 0;
    for (const Vec& point : points) {
        for (const Vec& n : normals) {
            const Vec x{point[0] * SPACING, point[1] * SPACING, point[2] * SPACING};
            // The CPU reads exactly the floats the GPU does.
            const std::array<float, 4> xf{static_cast<float>(x[0]), static_cast<float>(x[1]),
                                          static_cast<float>(x[2]), 1};
            const std::array<float, 4> nf{static_cast<float>(n[0]), static_cast<float>(n[1]),
                                          static_cast<float>(n[2]), 0};
            cases.push_back({xf, nf});
            expected.push_back(expectedAt(byCell, {xf[0], xf[1], xf[2]}, {nf[0], nf[1], nf[2]}));
            if (expected.back() == Vec{}) ++zero;
        }
    }

    const Lattice lattice{table.spacing, static_cast<std::uint32_t>(table.probes.size()), table.tableMask,
                          table.longestRun};
    const std::vector<std::byte> output = uta::test::render::runCompute(
        probe_eval_comp_spv,
        {std::as_bytes(std::span(&lattice, 1)), std::as_bytes(std::span(table.cells)),
         std::as_bytes(std::span(table.probes)), std::as_bytes(std::span(cases))},
        cases.size() * 4 * sizeof(float), static_cast<std::uint32_t>(cases.size()));

    for (std::size_t i = 0; i < cases.size(); ++i) {
        std::array<float, 4> actual{};
        std::memcpy(actual.data(), output.data() + i * sizeof(actual), sizeof(actual));
        CAPTURE(i, cases[i].x[0], cases[i].x[1], cases[i].x[2], cases[i].n[0], cases[i].n[1], cases[i].n[2]);
        CAPTURE(expected[i][0], expected[i][1], expected[i][2], actual[0], actual[1], actual[2]);
        for (int c = 0; c < 3; ++c) CHECK(std::abs(actual[c] - expected[i][c]) <= 1e-4 * std::max(1.0, expected[i][c]));
    }
    // The one point with no corner present is the zero cases, and only it.
    CHECK(zero == normals.size());
}

TEST_CASE("a surface with probes and no lights receives indirect light", "[device]") {
    using namespace uta::test::render;
    removeDisplay();
    uta::urender::Config config;
    config.width = 160;
    config.height = 64;
    config.linearOutput = true;
    uta::urender::Renderer renderer = requireRenderer(config);

    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 40, "white", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "white", {255, 255, 255, 255});

    // Every probe of every cell the square's centre can fall in gives 0.25 on
    // every face, so the indirect light there is 0.25 whatever the normal.
    bundle.lightProbes.emplace();
    bundle.lightProbes->spacing = SPACING;
    for (int z = -1; z <= 0; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = 0; x <= 1; ++x) {
                LightProbe probe;
                probe.cell = {x, y, z};
                for (auto& face : probe.cube) face = {0.25f, 0.25f, 0.25f};
                bundle.lightProbes->probes.push_back(probe);
            }

    requireOk(renderer.draw(bundle, uta::urender::Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    const int red = pixelAt(*pixels, 160, 80, 32).r;
    CAPTURE(red, srgbByte(0.25));
    CHECK(std::abs(red - srgbByte(0.25)) <= 2.0);
}

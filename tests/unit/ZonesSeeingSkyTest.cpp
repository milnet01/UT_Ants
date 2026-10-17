// Which zones see the sky -- docs/specs/UTA-0089-water-reflections.md SS 4.1
// and INV-3. What the water draws with the flag is tests/device/RenderWaterTest.cpp's.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/ShaderTypes.h"
#include "urender/Zones.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using uta::ubundle::Geometry;
using uta::ubundle::GeometryBatch;
using uta::ubundle::GeometryVertex;
using uta::urender::zonesSeeingSky;
namespace gpu = uta::urender::gpu;

namespace {

GeometryVertex inZone(std::uint8_t zone) {
    GeometryVertex vertex;
    vertex.zone = zone;
    return vertex;
}

// One triangle per batch, every corner in `zone`.
void addTriangle(Geometry& geometry, std::uint8_t zone, std::uint32_t polyFlags) {
    const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
    for (int corner = 0; corner < 3; ++corner) geometry.vertices.push_back(inZone(zone));
    const auto firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
    for (std::uint32_t i = 0; i < 3; ++i) geometry.indices.push_back(first + i);
    geometry.batches.push_back(GeometryBatch{"", polyFlags, firstIndex, 3});
}

} // namespace

TEST_CASE("UTA-0089 INV-3: a zone sees the sky where a backdrop triangle has a vertex in it", "[render]") {
    Geometry geometry;
    addTriangle(geometry, 1, gpu::PF_FAKE_BACKDROP | gpu::PF_TWO_SIDED); // bit by bit, not by equality
    addTriangle(geometry, 2, gpu::PF_MASKED);                            // a plain surface
    addTriangle(geometry, 3, gpu::PF_FAKE_BACKDROP);                     // a zone at zoneCount
    CHECK(zonesSeeingSky(geometry, 3) == std::vector<std::uint8_t>{0, 1, 0});
}

TEST_CASE("UTA-0089 INV-3: no geometry gives every zone no sky", "[render]") {
    CHECK(zonesSeeingSky(Geometry{}, 2) == std::vector<std::uint8_t>{0, 0});
    CHECK(zonesSeeingSky(Geometry{}, 0).empty());
}

TEST_CASE("UTA-0337: the outdoor extent is the wider horizontal span of the zones that see the sky", "[render]") {
    Geometry geometry;
    const auto at = [&](std::uint8_t zone, float x, float y, float z) {
        GeometryVertex vertex = inZone(zone);
        vertex.position = {x, y, z};
        geometry.vertices.push_back(vertex);
    };
    at(1, -100, 50, 0);
    at(1, 300, -50, 9000); // height is not width
    at(2, 5000, 5000, 0);  // a zone that does not see the sky
    at(3, -9000, 0, 0);    // a zone past the flags
    CHECK(uta::urender::outdoorExtent(geometry, {0, 1, 0}) == 400.0f);
    at(1, 0, 650, 0); // now Y is the wider
    CHECK(uta::urender::outdoorExtent(geometry, {0, 1, 0}) == 700.0f);
    CHECK(uta::urender::outdoorExtent(geometry, {0, 0, 0}) == 0.0f);
}

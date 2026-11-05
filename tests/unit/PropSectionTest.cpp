// UTA-0341 INV-9: PROP reads back what was written, and refuses what GEOM
// refuses, by name.
//
// docs/specs/UTA-0341-mesh-decorations.md SS 4.5 and SS 4.6.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubundle/Bundle.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using uta::ubundle::Bundle;
using uta::ubundle::Geometry;

namespace {

/// One triangle wearing no material.
Geometry triangle() {
    Geometry geometry;
    for (int i = 0; i < 3; ++i) {
        uta::ubundle::GeometryVertex vertex;
        vertex.position = {static_cast<float>(i), static_cast<float>(i * i), 1.0F};
        vertex.normal = {0, 0, 1};
        geometry.vertices.push_back(vertex);
    }
    geometry.indices = {0, 1, 2};
    geometry.batches = {uta::ubundle::GeometryBatch{"", 0, 0, 3, {}}};
    return geometry;
}

} // namespace

TEST_CASE("UTA-0341 INV-9: PROP reads back what was written", "[ubundle][props]") {
    Bundle bundle;
    bundle.props = triangle();
    const auto written = uta::ubundle::write(bundle);
    REQUIRE(written.has_value());
    const auto read = uta::ubundle::read(*written);
    if (!read.has_value()) FAIL(read.error().message());
    REQUIRE(read->props.has_value());
    CHECK(read->props->indices == bundle.props->indices);
    REQUIRE(read->props->vertices.size() == 3);
    CHECK(read->props->vertices[2].position == bundle.props->vertices[2].position);

    // A bundle with no props writes none, and reads none.
    const auto none = uta::ubundle::write(Bundle{});
    REQUIRE(none.has_value());
    const auto readNone = uta::ubundle::read(*none);
    REQUIRE(readNone.has_value());
    CHECK_FALSE(readNone->props.has_value());
}

TEST_CASE("UTA-0341 INV-9: PROP refuses what GEOM refuses", "[ubundle][props]") {
    Bundle bundle;
    bundle.props = triangle();
    bundle.props->indices[2] = 3; // past the vertices
    const auto written = uta::ubundle::write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(std::string(written.error().message()).starts_with("PROP: "));

    Bundle zoned;
    zoned.props = triangle();
    zoned.props->vertices[0].zone = 1; // no ZONE section: only zone 0
    CHECK_FALSE(uta::ubundle::write(zoned).has_value());
}

TEST_CASE("UTA-0341: applyProps moves PROP into MOVR as one still shape", "[ubundle][props]") {
    Bundle bundle;
    bundle.props = triangle();
    uta::ubundle::applyProps(bundle);
    CHECK_FALSE(bundle.props.has_value());
    REQUIRE(bundle.movers.has_value());
    REQUIRE(bundle.movers->size() == 1);
    const auto& shape = bundle.movers->front();
    CHECK(shape.exportIndex == uta::ubundle::PROPS_EXPORT);
    CHECK(shape.still == 0);
    CHECK(shape.geometry.indices.size() == 3);

    // A bundle with no PROP is left as it was.
    Bundle empty;
    uta::ubundle::applyProps(empty);
    CHECK_FALSE(empty.movers.has_value());
}

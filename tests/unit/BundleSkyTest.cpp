// UTA-0350's container and daylight cases: the PSKY section -- INV-3 -- and
// paintedSkyLight, the C++ half of INV-6.
//
// docs/specs/UTA-0350-painted-skies.md SS 4.3.
//
// THE GOLDEN BYTES ARE AUTHORED FROM SS 4.3, never produced by `write`, for
// BundleTextureTest.cpp's reason: a transposition present in both the reader
// and the writer round-trips perfectly.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "ubundle/Bundle.h"
#include "ubundle/Sky.h"

#include "Bytes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::Bundle;
using uta::ubundle::Origin;
using uta::ubundle::PAINTED_SKY_CLOUD;
using uta::ubundle::PaintedSky;
using uta::ubundle::paintedSkyLight;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

/// Every component different, so a swap shows.
PaintedSky goldenSky() {
    return PaintedSky{.top = {0.1f, 0.2f, 0.7f}, .horizon = {0.5f, 0.6f, 0.8f}, .clouds = 0.3f};
}

/// SS 4.3's record: top, horizon, clouds, seven f32.
Bytes skyPayload(const PaintedSky& sky) {
    Bytes out;
    for (const float channel : sky.top) out.f32(channel);
    for (const float channel : sky.horizon) out.f32(channel);
    out.f32(sky.clouds);
    return out;
}

/// A whole .utab holding `sections` in the order given.
std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(30); // formatVersion -- 30 since UTA-0387 gave LPRB its sky visibility
    out.u8(1);   // origin: Authored
    out.u8(0);   // kind: Map
    out.u16(0);  // reserved
    out.u32(static_cast<std::uint32_t>(sections.size()));
    std::uint64_t offset = 16 + 24 * sections.size();
    for (const auto& [id, payload] : sections) {
        out.id(id);
        out.u64(offset);
        out.u64(payload.size());
        out.u32(0); // compression and reserved
        offset += payload.size();
    }
    for (const auto& section : sections) out.append(section.second);
    return out.data();
}

Bundle bundleWith(const PaintedSky& sky) {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.paintedSky = sky;
    return bundle;
}

/// Each of the seven components, in byte order.
std::vector<std::function<float&(PaintedSky&)>> components() {
    std::vector<std::function<float&(PaintedSky&)>> out;
    for (std::size_t c = 0; c < 3; ++c) out.emplace_back([c](PaintedSky& s) -> float& { return s.top[c]; });
    for (std::size_t c = 0; c < 3; ++c) out.emplace_back([c](PaintedSky& s) -> float& { return s.horizon[c]; });
    out.emplace_back([](PaintedSky& s) -> float& { return s.clouds; });
    return out;
}

} // namespace

TEST_CASE("UTA-0350 INV-3: the PSKY golden bytes decode to the sky they encode", "[ubundle][paintedsky]") {
    const auto result = read(fileWith({{"PSKY", skyPayload(goldenSky())}}));
    REQUIRE(result.has_value());
    REQUIRE(result->paintedSky.has_value());
    CHECK(result->paintedSky->top == std::array<float, 3>{0.1f, 0.2f, 0.7f});
    CHECK(result->paintedSky->horizon == std::array<float, 3>{0.5f, 0.6f, 0.8f});
    CHECK(result->paintedSky->clouds == 0.3f);
    CHECK_FALSE(read(fileWith({})).value().paintedSky.has_value());
}

TEST_CASE("UTA-0350 INV-3: write emits PSKY as the golden bytes", "[ubundle][paintedsky]") {
    const auto written = write(bundleWith(goldenSky()));
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"PSKY", skyPayload(goldenSky())}}));
}

TEST_CASE("UTA-0350 INV-3: read and write each refuse a component outside [0 1] or not finite",
          "[ubundle][paintedsky]") {
    const float bad[] = {-0.001f, 1.001f, std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity()};
    const auto each = components();
    for (std::size_t k = 0; k < each.size(); ++k) {
        for (const float value : bad) {
            CAPTURE(k, value);
            PaintedSky sky = goldenSky();
            each[k](sky) = value;
            const auto result = read(fileWith({{"PSKY", skyPayload(sky)}}));
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == ErrorCode::MalformedData);
            CHECK(result.error().message().starts_with("PSKY: "));
            const auto written = write(bundleWith(sky));
            REQUIRE_FALSE(written.has_value());
            CHECK(written.error().code() == ErrorCode::InvalidArgument);
        }
        // The bounds themselves are in range.
        for (const float edge : {0.0f, 1.0f}) {
            CAPTURE(k, edge);
            PaintedSky sky = goldenSky();
            each[k](sky) = edge;
            CHECK(read(fileWith({{"PSKY", skyPayload(sky)}})).has_value());
            CHECK(write(bundleWith(sky)).has_value());
        }
    }
}

TEST_CASE("UTA-0350 INV-3: read refuses a second PSKY", "[ubundle][paintedsky]") {
    const auto result = read(fileWith({{"PSKY", skyPayload(goldenSky())}, {"PSKY", skyPayload(goldenSky())}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("UTA-0350 INV-6: paintedSkyLight is the horizon low down and the clouded top overhead",
          "[ubundle][paintedsky]") {
    const PaintedSky sky = goldenSky();
    const auto horizon = paintedSkyLight(sky, {1, 0, 0});
    const auto below = paintedSkyLight(sky, {0, 0.6, -0.8});
    const auto overhead = paintedSkyLight(sky, {0, 0, 1});
    for (std::size_t c = 0; c < 3; ++c) {
        CAPTURE(c);
        CHECK(horizon[c] == Catch::Approx(sky.horizon[c]));
        CHECK(below[c] == Catch::Approx(sky.horizon[c]));
        CHECK(overhead[c] == Catch::Approx(sky.top[c] + (PAINTED_SKY_CLOUD - sky.top[c]) * sky.clouds));
    }
    // Halfway up the gradient by sqrt(d.z), and above the fade's top the
    // clouds are at their full cover.
    const double z = 0.36;
    const auto mid = paintedSkyLight(sky, {std::sqrt(1 - z * z), 0, z});
    for (std::size_t c = 0; c < 3; ++c) {
        const double gradient = sky.horizon[c] + (sky.top[c] - sky.horizon[c]) * std::sqrt(z);
        CHECK(mid[c] == Catch::Approx(gradient + (PAINTED_SKY_CLOUD - gradient) * sky.clouds));
    }
    // Inside the fade, the clouds show at part of their cover. A quarter of
    // the way in, smoothstep is 0.15625 -- not its midpoint, where a straight
    // line agrees with it.
    const double quarter = 0.02 + (0.25 - 0.02) / 4;
    const auto fading = paintedSkyLight(sky, {std::sqrt(1 - quarter * quarter), 0, quarter});
    for (std::size_t c = 0; c < 3; ++c) {
        const double gradient = sky.horizon[c] + (sky.top[c] - sky.horizon[c]) * std::sqrt(quarter);
        CHECK(fading[c] == Catch::Approx(gradient + (PAINTED_SKY_CLOUD - gradient) * sky.clouds * 0.15625));
    }
}

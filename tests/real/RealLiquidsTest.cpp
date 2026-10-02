// UTA-0105 INV-4: real liquid textures bake with a look of their class's kind,
// carrying their own stored settings, and every class default reads from
// Fire.u.
//
// docs/specs/UTA-0105-shader-liquids.md SS 4.1 and SS 4.2. Two of SS 4.6's
// maps: DM-ArcaneTemple holds a WetTexture (RainFX.swater4a) and a
// WaveTexture (HubEffects.waterrings2); DOM-MetalDream an IceTexture
// (XbpFX.blueplasma). The settings checked are the stored ones the census
// read (~/.cache/uta-scratch/u105/liquid_census.json, 2026-10-02).

#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"
#include "upkg/Package.h"

#include "real/RealSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace uta::test::real;
using uta::ubundle::LiquidKind;
using uta::ubundle::LiquidLook;

namespace {

/// The look of the one unmasked material whose id ends in `.<name>`, folded.
LiquidLook lookOf(const uta::ubake::BakeResult& result, std::string_view name) {
    REQUIRE(result.bundle.materials.has_value());
    const std::string suffix = "." + std::string(name);
    const auto found = std::ranges::find_if(*result.bundle.materials, [&](const auto& record) {
        return record.id.size() > suffix.size() && record.id.ends_with(suffix);
    });
    INFO("material ending " << suffix);
    REQUIRE(found != result.bundle.materials->end());
    REQUIRE(found->liquid.has_value());
    return *found->liquid;
}

uta::ubake::BakeResult baked(std::string_view map) {
    const fs::path root{UTA_UT_INSTALL_DIR};
    auto install = uta::ubake::Install::open(root);
    REQUIRE(install.has_value());
    const fs::path file = root / "Maps" / (std::string(map) + ".unr");
    REQUIRE(fs::is_regular_file(file));
    const std::vector<std::byte> raw = readWhole(file);
    const auto package = uta::upkg::Package::open(viewOf(raw));
    REQUIRE(package.has_value());
    uta::JobSystem jobs;
    auto result = uta::ubake::bake(*package, uta::ubake::detail::mapNameOf(file), *install, jobs);
    if (!result.has_value()) FAIL(map << " was refused: " << result.error().message());
    for (const auto& skipped : result->skippedLiquids) UNSCOPED_INFO(skipped.material << ": " << skipped.reason);
    CHECK(result->skippedLiquids.empty());
    return std::move(*result);
}

} // namespace

TEST_CASE("UTA-0105 INV-4: real Wet and Wave textures carry their own settings", "[real-assets][ubake][liquids]") {
    const uta::ubake::BakeResult result = baked("DM-ArcaneTemple");

    const LiquidLook water = lookOf(result, "swater4a");
    CHECK(water.kind == LiquidKind::Wet);
    CHECK(water.amplitude == 255);
    CHECK(water.frequency == 9);
    CHECK(water.size == std::array<std::uint16_t, 2>{256, 256});

    const LiquidLook rings = lookOf(result, "waterrings2");
    CHECK(rings.kind == LiquidKind::Wave);
    CHECK(rings.amplitude == 104);
    CHECK(rings.frequency == 166);
    CHECK(rings.bump == std::array<std::uint8_t, 3>{43, 54, 255});
    CHECK(rings.size == std::array<std::uint16_t, 2>{128, 128});
    // SS 4.3: darkest first.
    const auto luma = [](const std::array<float, 3>& c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; };
    CHECK(luma(rings.ramp[0]) <= luma(rings.ramp[7]));
}

TEST_CASE("UTA-0105 INV-4: a real Ice texture carries its pan speeds", "[real-assets][ubake][liquids]") {
    const uta::ubake::BakeResult result = baked("DOM-MetalDream");
    const LiquidLook plasma = lookOf(result, "blueplasma");
    CHECK(plasma.kind == LiquidKind::Ice);
    CHECK(plasma.pan == std::array<std::uint8_t, 2>{200, 128});
    CHECK(plasma.frequency == 20);
    CHECK(plasma.amplitude == 50);
}

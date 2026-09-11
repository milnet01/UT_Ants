// UTA-0112's real-asset case: the LPRB step over a list of UT99's own maps.
//
// docs/specs/UTA-0112-baked-light-probes.md SS 7 and SS 13. It PRINTS what
// those sections leave to measurement -- each map's triangles, its lights and
// the ones that bake, its probes, and the step's time -- and checks every
// stored value is finite and not below zero.
//
// SURFACES WEAR NO MATERIAL HERE, so every one reflects DEFAULT_ALBEDO. Making
// the materials is UTA-0011's and by far the slowest part of a bake; the probe
// count and the step's time do not depend on the albedo.
//
// ONE Install PER MAP -- UTA-0011 SS 4.11.

#include "core/FileSystem.h"
#include "core/Jobs.h"
#include "ubake/Actors.h"
#include "ubake/Bake.h"
#include "ubake/Collision.h"
#include "ubake/Geometry.h"
#include "ubake/Install.h"
#include "ubake/LightProbes.h"
#include "ubake/Name.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#ifndef UTA_UT_INSTALL_DIR
#error "UTA_UT_INSTALL_DIR must be defined for the real-asset tier"
#endif

namespace {

namespace fs = std::filesystem;
namespace detail = uta::ubake::detail;
using uta::ubake::DEFAULT_ALBEDO;
using uta::ubake::Rgb;

/// UT99's own maps, by file name. One that is not installed is skipped, and
/// the output says so.
constexpr std::array<std::string_view, 12> STOCK = {
    "AS-Frigate.unr",   "CTF-Coret.unr",   "CTF-Face.unr",   "CTF-LavaGiant.unr",
    "DM-Codex.unr",     "DM-Deck16][.unr", "DM-Fractal.unr", "DM-Liandri.unr",
    "DM-Morpheus.unr",  "DM-Phobos.unr",   "DM-Turbine.unr", "DOM-Condemned.unr",
};

} // namespace

TEST_CASE("the probe step over UT99's own maps", "[real-assets][probes]") {
    const fs::path root{UTA_UT_INSTALL_DIR};
    uta::JobSystem jobs;
    const uta::ubake::AlbedoLookup flat = [](std::string_view) {
        return Rgb{DEFAULT_ALBEDO, DEFAULT_ALBEDO, DEFAULT_ALBEDO};
    };
    const uta::ubake::MaterialLookup none = [](uta::upkg::ObjectReference,
                                               bool) -> const uta::ubake::SurfaceMaterial* {
        return nullptr;
    };

    std::size_t maps = 0, largest = 0;
    double slowest = 0;
    std::string largestMap, slowestMap;
    std::cout << "UTA-0112 probe step, surfaces at DEFAULT_ALBEDO\n";
    for (const std::string_view file : STOCK) {
        const fs::path path = root / "Maps" / std::string(file);
        if (!fs::exists(path)) {
            std::cout << "  " << file << ": not installed\n";
            continue;
        }
        const std::string mapName = detail::mapNameOf(path);
        const auto bytes = uta::fs::readFile(path);
        REQUIRE(bytes.has_value());
        const auto map = uta::upkg::Package::open(*bytes);
        REQUIRE(map.has_value());
        const auto levelExport = detail::findLevel(*map, mapName);
        REQUIRE(levelExport.has_value());
        const auto level = uta::upkg::readLevel(*map, **levelExport);
        REQUIRE(level.has_value());
        const auto modelExport = detail::findModel(*map, *level, mapName);
        REQUIRE(modelExport.has_value());
        const auto model = uta::upkg::readModel(*map, **modelExport);
        REQUIRE(model.has_value());

        auto install = uta::ubake::Install::open(root);
        REQUIRE(install.has_value());
        const auto actors = uta::ubake::buildActors(*map, mapName, *level, install->resolver());
        if (!actors.has_value()) {
            // UTA-0124's refusal, on the maps that carry it: not this item's.
            std::cout << "  " << file << ": actors refused: " << actors.error().message() << "\n";
            continue;
        }
        const auto geometry = uta::ubake::buildGeometry(*model, none);
        REQUIRE(geometry.has_value());
        const auto tree = uta::ubake::buildCollision(*model);
        REQUIRE(tree.has_value());
        const auto lights = uta::ubake::bakedLights(actors->lights, actors->placements);

        const auto began = std::chrono::steady_clock::now();
        const auto probes = uta::ubake::bakeLightProbes(*geometry, *tree, lights, flat, jobs);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        REQUIRE(probes.has_value());

        bool sane = true;
        for (const auto& probe : probes->probes)
            for (const auto& face : probe.cube)
                for (const float channel : face) sane = sane && std::isfinite(channel) && channel >= 0;
        CHECK(sane);

        ++maps;
        if (probes->probes.size() > largest) {
            largest = probes->probes.size();
            largestMap = std::string(file);
        }
        if (seconds > slowest) {
            slowest = seconds;
            slowestMap = std::string(file);
        }
        std::cout << "  " << file << ": " << geometry->indices.size() / 3 << " triangles, "
                  << actors->lights.size() << " lights of which " << lights.size() << " bake, "
                  << probes->probes.size() << " probes, " << seconds << " s\n";
    }
    std::cout << "  most probes: " << largestMap << " (" << largest << "); slowest: " << slowestMap
              << " (" << slowest << " s); workers: " << jobs.workerCount() << "\n";
    CHECK(maps > 0);
}

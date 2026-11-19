// ut-export -- UTA-0476. Writes a baked map as ExperiRays' scene and camera
// path: <out>.glb and <out>.xrpath.json, per its EXRAY-0001. Scene.h and
// Path.h say what each holds. The output is game data: write it under
// content/ or scratch, never into the repository (ExperiRays' ADR-0005).
//
//   ut-export --map <file.unr> --bundle <file.utab> --out <path stem>
//             [--no-added-lamps] [--walkers N] [--seconds N]

#include "Path.h"
#include "Scene.h"

#include "common/Options.h"
#include "core/FileSystem.h"
#include "ubundle/Bundle.h"

#include <optional>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view USAGE =
    "usage: ut-export --map <file.unr> --bundle <file.utab> --out <path stem>\n"
    "                 [--no-added-lamps] [--walkers N] [--seconds N]\n";

int write(const std::string& path, std::span<const std::byte> bytes) {
    if (auto written = uta::fs::writeFileAtomically(path, bytes); !written) {
        std::cerr << "ut-export: " << path << ": " << written.error().message() << "\n";
        return 1;
    }
    std::cerr << "ut-export: wrote " << path << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    uta::tools::OptionValues values("ut-export", args, std::cerr);
    std::string map, bundlePath, out;
    bool addedLamps = true;
    uta::tools::exporter::PathOptions pathOptions;
    for (std::size_t at = 0; at < args.size(); ++at) {
        const std::string_view arg = args[at];
        std::optional<std::string_view> value;
        unsigned whole = 0;
        if (arg == "--map" && (value = values.take(at))) map = *value;
        else if (arg == "--bundle" && (value = values.take(at))) bundlePath = *value;
        else if (arg == "--out" && (value = values.take(at))) out = *value;
        else if (arg == "--no-added-lamps") addedLamps = false;
        else if (arg == "--walkers" && values.takeWhole(at, whole, 1)) pathOptions.walkers = whole;
        else if (arg == "--seconds" && values.takeWhole(at, whole, 1)) pathOptions.seconds = whole;
        else {
            if (arg != "--map" && arg != "--bundle" && arg != "--out" && arg != "--walkers" && arg != "--seconds")
                std::cerr << "ut-export: unknown argument " << arg << "\n" << USAGE;
            return 2;
        }
    }
    if (map.empty() || bundlePath.empty() || out.empty()) {
        std::cerr << USAGE;
        return 2;
    }

    auto unr = uta::fs::readFile(map);
    auto utab = uta::fs::readFile(bundlePath);
    for (const auto* read : {&unr, &utab})
        if (!*read) {
            std::cerr << "ut-export: " << read->error().message() << "\n";
            return 1;
        }
    auto bundle = uta::ubundle::read(*utab);
    if (!bundle) {
        std::cerr << "ut-export: " << bundlePath << " did not read: " << bundle.error().message() << "\n";
        return 1;
    }

    const std::string key = uta::tools::exporter::sceneKeyOf(*unr, *utab);
    auto scene = uta::tools::exporter::buildScene(*bundle, key, addedLamps);
    if (!scene) {
        std::cerr << "ut-export: " << scene.error().message() << "\n";
        return 1;
    }
    const auto& r = scene->report;
    std::cerr << "ut-export: scene_key " << key << "\n"
              << "ut-export: " << r.triangles << " triangles, " << r.reversed << " rewound, " << r.edgeOn
              << " edge-on, " << r.swayTriangles << " swaying, " << r.mixedCharts << " with mixed charts, "
              << r.zeroNormals << " zero normals\n"
              << "ut-export: " << scene->lights.size() << " lights (" << r.skippedSpots << " cone-0 spots left out), "
              << (scene->pairs ? scene->pairs->chart.size() : 0) << " light pairs (" << r.droppedPairs
              << " of the other lamp state dropped), " << r.bakedMovers << " movers put in world space\n";
    if (const int status = write(out + ".glb", uta::tools::exporter::writeGlb(*scene)); status != 0) return status;

    uta::tools::exporter::PathReport report;
    auto path = uta::tools::exporter::writeCameraPath(*bundle, *scene, pathOptions, report);
    if (!path) {
        std::cerr << "ut-export: no camera path: " << path.error().message() << "\n";
        return 1;
    }
    std::cerr << "ut-export: " << report.walkers << " walkers over " << report.locatedNodes
              << " placed navigation points, " << report.tourStops << " tour stops, " << report.frames << " frames\n"
              << "ut-export: " << report.movingMovers << " movers move, in " << report.moverEntries
              << " frame entries; " << report.unturnedEntries << " lose a turn (the mover is in world space)\n";
    return write(out + ".xrpath.json", std::as_bytes(std::span(*path)));
}

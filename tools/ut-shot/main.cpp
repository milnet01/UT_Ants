// ut-shot -- UTA-0156. Draws a baked map from the cameras given on standard
// input, on the surfaceless path, and writes each frame as a binary PPM. It
// exists so a frame can be measured against the original game's view of the
// same place, rather than judged by eye.
//
// Each input line is one camera, in UT99's own units and angles:
//   x y z pitch yaw roll horizontalFovDegrees
// UT99's FOVAngle is horizontal; urender::Camera takes a vertical one, so it is
// converted at the frame's aspect.
//
// UTA-0199: the options are parsed by Cli.cpp, so they are tested without a
// Vulkan device. --from-capture draws a UTA-0191 capture folder's own view at
// the tier, render scale, light time and size it recorded.

#include "Cli.h"

#include "core/FileSystem.h"
#include "ubundle/Bundle.h"
#include "urender/Renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numbers>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    const auto options = uta::shot::parseOptions(args, std::cerr);
    if (!options) return 2;

    uta::urender::Config config;
    config.width = options->width;
    config.height = options->height;
    config.linearOutput = options->linearOutput;

    auto bytes = uta::fs::readFile(options->bundle);
    if (!bytes) {
        std::cerr << "ut-shot: " << bytes.error().message() << "\n";
        return 1;
    }
    auto bundle = uta::ubundle::read(*bytes);
    if (!bundle) {
        std::cerr << "ut-shot: " << options->bundle << " did not read: " << bundle.error().message() << "\n";
        return 1;
    }
    *bytes = {};
    // What the baked indirect light holds, so a weak bounce can be told from a
    // missing one.
    if (bundle->lightProbes.has_value()) {
        const auto& all = bundle->lightProbes->probes;
        double sum = 0, peak = 0;
        std::size_t dark = 0;
        for (const uta::ubundle::LightProbe& probe : all) {
            double here = 0;
            for (const auto& face : probe.cube) here += (face[0] * 0.2126 + face[1] * 0.7152 + face[2] * 0.0722) / 6;
            sum += here;
            peak = std::max(peak, here);
            if (here < 1e-4) ++dark;
        }
        std::cerr << "PROBES count=" << all.size() << " spacing=" << bundle->lightProbes->spacing
                  << " meanLuma=" << (all.empty() ? 0 : sum / all.size()) << " peakLuma=" << peak
                  << " dark=" << dark << "\n";

        // Coverage: the share of triangle area whose centroid's lattice cell
        // has at least one of its eight corner probes -- what indirectAt needs
        // to return anything but zero.
        if (bundle->geometry.has_value() && bundle->lightProbes->spacing != 0) {
            std::set<std::array<std::int32_t, 3>> present;
            for (const uta::ubundle::LightProbe& probe : all) present.insert(probe.cell);
            const auto& g = *bundle->geometry;
            const double spacing = bundle->lightProbes->spacing;
            double covered = 0, total = 0;
            for (std::size_t i = 0; i + 3 <= g.indices.size(); i += 3) {
                std::array<std::array<double, 3>, 3> p{};
                for (int k = 0; k < 3; ++k)
                    for (int a = 0; a < 3; ++a) p[k][a] = g.vertices[g.indices[i + k]].position[a];
                const std::array<double, 3> u{p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
                const std::array<double, 3> v{p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
                const double area = 0.5 * std::sqrt(std::pow(u[1] * v[2] - u[2] * v[1], 2) +
                                                    std::pow(u[2] * v[0] - u[0] * v[2], 2) +
                                                    std::pow(u[0] * v[1] - u[1] * v[0], 2));
                std::array<std::int32_t, 3> base{};
                for (int a = 0; a < 3; ++a)
                    base[a] = static_cast<std::int32_t>(std::floor((p[0][a] + p[1][a] + p[2][a]) / 3 / spacing));
                bool any = false;
                for (int c = 0; c < 8 && !any; ++c)
                    any = present.contains({base[0] + (c & 1), base[1] + ((c >> 1) & 1), base[2] + ((c >> 2) & 1)});
                total += area;
                if (any) covered += area;
            }
            std::cerr << "PROBES coverage=" << (total > 0 ? covered / total : 0) << " of triangle area\n";
        }
    }
    if (!options->probes) bundle->lightProbes.reset();

    // The lights the bundle carries, one per line on standard error, so they
    // can be set against the lights the original game loads.
    if (bundle->lights.has_value())
        for (const uta::ubundle::Light& light : *bundle->lights)
            std::cerr << "LIGHT export=" << light.exportIndex << " at=" << light.location[0] << ","
                      << light.location[1] << "," << light.location[2] << " type=" << int(light.type)
                      << " effect=" << int(light.effect) << " b=" << int(light.brightness) << " h=" << int(light.hue)
                      << " s=" << int(light.saturation) << " r=" << int(light.radius) << "\n";

    // UTA-0199: unset draws at high and at full scale, as this tool always
    // has; --from-capture or --tier says otherwise.
    config.tier = options->tier ? options->tier : uta::urender::tierNamed("high");
    config.fixedRenderScale = options->renderScale.value_or(1.0);
    auto created = uta::urender::Renderer::create(config);
    if (!created) {
        std::cerr << "ut-shot: the renderer did not start: " << created.error().message() << "\n";
        return 1;
    }
    uta::urender::Renderer renderer = std::move(*created);
    // UTA-0199: without this the renderer reads its own clock, so a redraw
    // disagrees with the capture beside it on a pulsing light's phase.
    if (options->lightSeconds) renderer.pinLightSeconds(*options->lightSeconds);

    const double aspect = static_cast<double>(config.height) / config.width;

    // --from-capture names the folder's camera.txt, which UTA-0191 writes in
    // exactly this form so the view can be drawn again without a field being
    // copied out by hand.
    std::ifstream captureCameras;
    if (options->cameraFile) {
        captureCameras.open(*options->cameraFile);
        if (!captureCameras) {
            std::cerr << "ut-shot: " << *options->cameraFile << " does not open\n";
            return 1;
        }
    }
    std::istream& cameras = options->cameraFile ? static_cast<std::istream&>(captureCameras) : std::cin;

    std::string line;
    for (int index = 0; std::getline(cameras, line); ++index) {
        std::istringstream fields(line);
        uta::urender::Camera camera;
        double horizontalFov = 90;
        if (!(fields >> camera.location[0] >> camera.location[1] >> camera.location[2] >> camera.rotation[0] >>
              camera.rotation[1] >> camera.rotation[2] >> horizontalFov)) {
            std::cerr << "ut-shot: camera line " << index << " does not read: " << line << "\n";
            return 2;
        }
        const double halfRadians = horizontalFov * std::numbers::pi / 360.0;
        camera.verticalFovDegrees =
            static_cast<float>(std::atan(std::tan(halfRadians) * aspect) * 360.0 / std::numbers::pi);

        // A few frames, so the shadow atlas has drawn this view's tiles.
        for (int frame = 0; frame < 4; ++frame) {
            if (const auto drawn = renderer.draw(*bundle, camera); !drawn) {
                std::cerr << "ut-shot: a frame did not draw: " << drawn.error().message() << "\n";
                return 1;
            }
        }
        const auto pixels = renderer.readback();
        if (!pixels) {
            std::cerr << "ut-shot: readback failed: " << pixels.error().message() << "\n";
            return 1;
        }
        const std::string path = options->prefix + "-" + std::to_string(index) + ".ppm";
        std::ofstream out(path, std::ios::binary);
        out << "P6\n" << config.width << " " << config.height << "\n255\n";
        for (std::size_t i = 0; i < pixels->size(); i += 4) out.write(reinterpret_cast<const char*>(&(*pixels)[i]), 3);
        if (!out) {
            std::cerr << "ut-shot: could not write " << path << "\n";
            return 1;
        }
        std::cout << path << "\n";
    }
    return 0;
}

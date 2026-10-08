// ut-ref -- docs/specs/UTA-0292-reference-path-tracer.md SS 4.4. Exact light
// in UTA-0112's model, and a score of ut-shot's light terms against it.

#include "Reference.h"

#include "core/FileSystem.h"
#include "ubake/LightProbes.h"
#include "ubake/MaterialLight.h"
#include "ubundle/Bundle.h"
#include "ubundle/Sky.h"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace uta;

namespace {

void usage() {
    std::cerr
        << "usage: ut-ref trace <bundle> <material light file> <width> <height> <samples> <depth> <out prefix> < cameras\n"
           "       ut-ref score <ref prefix> <shot prefix> <width> <height> <views>\n"
           "\n"
           "trace: each line of standard input is one camera, as ut-shot reads it:\n"
           "x y z pitch yaw roll horizontalFovDegrees. Writes <out prefix>-<line>.f32,\n"
           "width x height x 5 little-endian floats, row 0 at the top: per pixel the\n"
           "direct light, the first bounce, later bounces to <depth>, sky light, and\n"
           "1 where the pixel is a lit surface -- each a luma, as ut-shot --light-terms\n"
           "writes its own. The material light file is ut-bake --light-materials'.\n"
           "\n"
           "score: reads <ref prefix>-<n>.f32 and <shot prefix>-<n>-light.pfm for each\n"
           "n below <views>, and prints per view and on average the reference's and\n"
           "the renderer's light, their gaps as a share of the reference's, the sky's\n"
           "and later bounces' shares, and the gap in stops. Draw the shots with\n"
           "ut-shot --light-terms --light-time 0 at the same size.\n";
}

bool number(std::string_view text, auto& value) {
    const auto read = std::from_chars(text.data(), text.data() + text.size(), value);
    return read.ec == std::errc{} && read.ptr == text.data() + text.size();
}

std::vector<float> readFloats(const std::string& path, std::size_t count) {
    std::ifstream in(path, std::ios::binary);
    std::vector<float> out(count);
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(count * sizeof(float)));
    if (!in) out.clear();
    return out;
}

/// ut-shot's PFM -- three floats a pixel, rows bottom to top -- turned top first.
std::vector<float> readPfm(const std::string& path, std::uint32_t width, std::uint32_t height) {
    std::ifstream in(path, std::ios::binary);
    std::string magic, scale;
    std::uint32_t w = 0, h = 0;
    in >> magic >> w >> h >> scale;
    in.get(); // the one whitespace byte before the data
    if (!in || magic != "PF" || w != width || h != height) return {};
    const std::size_t row = std::size_t{width} * 3;
    std::vector<float> out(row * height);
    for (std::uint32_t y = height; y-- > 0;)
        in.read(reinterpret_cast<char*>(out.data() + y * row), static_cast<std::streamsize>(row * sizeof(float)));
    if (!in) out.clear();
    return out;
}

int trace(const std::vector<std::string_view>& args) {
    std::uint32_t width = 0, height = 0;
    int samples = 0, depth = 0;
    if (args.size() != 7 || !number(args[2], width) || !number(args[3], height) || !number(args[4], samples)
        || !number(args[5], depth) || width == 0 || height == 0 || samples < 1 || depth < 1) {
        usage();
        return 2;
    }
    auto bytes = fs::readFile(std::string(args[0]));
    if (!bytes) {
        std::cerr << "ut-ref: " << bytes.error().message() << "\n";
        return 1;
    }
    auto bundle = ubundle::read(*bytes);
    if (!bundle || !bundle->geometry || !bundle->lights || !bundle->placements) {
        std::cerr << "ut-ref: " << (bundle ? "the bundle has no geometry, lights or placements" : bundle.error().message())
                  << "\n";
        return 1;
    }
    std::ifstream file{std::string(args[1]), std::ios::binary};
    auto materials = ubake::readMaterialLight(file);
    if (!file.eof() || !materials) {
        std::cerr << "ut-ref: " << (materials ? std::string("cannot read ") + std::string(args[1])
                                              : materials.error().message())
                  << "\n";
        return 1;
    }
    // SS 6: a file from another bake fails nothing, so say how much it covered.
    std::unordered_set<std::string> named;
    for (const auto& material : *materials) named.insert(material.id);
    std::size_t covered = 0, total = 0;
    if (bundle->materials)
        for (const auto& record : *bundle->materials) {
            ++total;
            covered += named.contains(record.id);
        }
    std::cerr << "ut-ref: the material light file names " << covered << " of the bundle's " << total
              << " materials\n";

    std::optional<ubake::Vec3> sky;
    if (const auto view = ubundle::skyViewOf(*bundle))
        sky = ubake::Vec3{view->location[0], view->location[1], view->location[2]};
    const ref::Reference reference(*bundle->geometry, ubake::bakedLights(*bundle->lights, *bundle->placements), sky,
                                   *materials);
    const unsigned workers = std::max(1u, std::thread::hardware_concurrency() - 2);

    std::string line;
    for (int index = 0; std::getline(std::cin, line); ++index) {
        std::istringstream fields(line);
        urender::Camera camera;
        double fov = 90;
        fields >> camera.location[0] >> camera.location[1] >> camera.location[2] >> camera.rotation[0] >>
            camera.rotation[1] >> camera.rotation[2] >> fov;
        if (!fields) {
            std::cerr << "ut-ref: camera line " << index << " does not read\n";
            return 1;
        }
        const auto out = reference.trace(camera, fov, width, height, samples, depth, workers);
        const std::string path = std::string(args[6]) + "-" + std::to_string(index) + ".f32";
        std::ofstream write(path, std::ios::binary);
        write.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size() * sizeof(float)));
        write.close();
        if (!write) {
            std::cerr << "ut-ref: could not write " << path << "\n";
            return 1;
        }
        std::cout << path << "\n";
    }
    return 0;
}

int score(const std::vector<std::string_view>& args) {
    std::uint32_t width = 0, height = 0;
    int views = 0;
    if (args.size() != 5 || !number(args[2], width) || !number(args[3], height) || !number(args[4], views)
        || views < 1) {
        usage();
        return 2;
    }
    std::printf("%5s %6s %8s %8s %7s %7s %7s %8s %6s %6s\n", "view", "blocks", "ref", "ren", "total", "stops",
                "direct", "indirect", "sky", "later");
    ref::Score mean;
    for (int n = 0; n < views; ++n) {
        const auto reference = readFloats(std::string(args[0]) + "-" + std::to_string(n) + ".f32",
                                          std::size_t{width} * height * ref::CHANNELS);
        const auto terms = readPfm(std::string(args[1]) + "-" + std::to_string(n) + "-light.pfm", width, height);
        if (reference.empty() || terms.empty()) {
            std::cerr << "ut-ref: view " << n << ": a file is missing or not " << width << " x " << height << "\n";
            return 1;
        }
        const ref::Score s = ref::scoreView(reference, terms, width, height);
        std::printf("%5d %6zu %8.4f %8.4f %7.3f %7.3f %7.3f %8.3f %6.3f %6.3f\n", n, s.blocks, s.reference,
                    s.renderer, s.total, s.stops, s.direct, s.indirect, s.sky, s.later);
        mean.reference += s.reference / views;
        mean.renderer += s.renderer / views;
        mean.total += s.total / views;
        mean.stops += s.stops / views;
        mean.direct += s.direct / views;
        mean.indirect += s.indirect / views;
        mean.sky += s.sky / views;
        mean.later += s.later / views;
    }
    std::printf("%5s %6s %8.4f %8.4f %7.3f %7.3f %7.3f %8.3f %6.3f %6.3f\n", "mean", "", mean.reference,
                mean.renderer, mean.total, mean.stops, mean.direct, mean.indirect, mean.sky, mean.later);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) {
        usage();
        return 2;
    }
    const std::vector<std::string_view> rest(args.begin() + 1, args.end());
    if (args[0] == "trace") return trace(rest);
    if (args[0] == "score") return score(rest);
    usage();
    return 2;
}

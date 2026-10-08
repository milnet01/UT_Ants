// The light each material sends on -- MaterialLight.h says what and why.

#include "ubake/MaterialLight.h"

#include <array>
#include <charconv>
#include <string_view>
#include <system_error>
#include <utility>

namespace uta::ubake {
namespace {

void writeNumber(std::ostream& out, double value) {
    std::array<char, 32> text{};
    // Shortest round trip, and no locale: what from_chars reads back exactly.
    const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
    out.write(text.data(), written.ptr - text.data());
}

} // namespace

void writeMaterialLight(std::ostream& out, std::span<const MaterialLight> materials) {
    for (const MaterialLight& material : materials) {
        out << material.id << '\t';
        const std::array values = {material.albedo.r,     material.albedo.g,     material.albedo.b,
                                   material.own.emission.r, material.own.emission.g, material.own.emission.b};
        for (const double value : values) {
            writeNumber(out, value);
            out << ' ';
        }
        out << (material.own.unlitGlows ? '1' : '0') << '\n';
    }
}

Result<std::vector<MaterialLight>> readMaterialLight(std::istream& in) {
    std::vector<MaterialLight> materials;
    std::string line;
    for (std::size_t number = 1; std::getline(in, line); ++number) {
        const auto refuse = [number](std::string_view why) {
            return fail(ErrorCode::MalformedData,
                        "material light line " + std::to_string(number) + ": " + std::string(why));
        };
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) return refuse("no id before a tab");
        MaterialLight material;
        material.id = line.substr(0, tab);
        std::array<double*, 6> into = {&material.albedo.r,       &material.albedo.g,
                                       &material.albedo.b,       &material.own.emission.r,
                                       &material.own.emission.g, &material.own.emission.b};
        const char* at = line.data() + tab + 1;
        const char* const end = line.data() + line.size();
        for (double* value : into) {
            const auto read = std::from_chars(at, end, *value);
            if (read.ec != std::errc{} || read.ptr == end || *read.ptr != ' ')
                return refuse("six numbers, each followed by a space, then 0 or 1");
            at = read.ptr + 1;
        }
        if (end - at != 1 || (*at != '0' && *at != '1')) return refuse("the last field is not 0 or 1");
        material.own.unlitGlows = *at == '1';
        materials.push_back(std::move(material));
    }
    return materials;
}

} // namespace uta::ubake

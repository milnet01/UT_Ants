// UTA-0055 INV-10: every texture in tests/real/cloth-labels.txt is on umat's
// cloth list, and the list holds nothing the labels do not name.
//
// docs/specs/UTA-0055-swaying-cloth-and-water.md SS 4.6. The key is the
// bake's: the texture's top mip through its own palette, fingerprinted as the
// curated library keys a picture.

#include "real/RealSupport.h"

#include "umat/Cloth.h"
#include "umat/Fingerprint.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"
#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#ifndef UTA_CLOTH_LABELS
#error "UTA_CLOTH_LABELS must name tests/real/cloth-labels.txt"
#endif

namespace {

using namespace uta;

struct Label {
    std::string package;
    std::string texture;
};

std::vector<Label> readLabels() {
    std::vector<Label> out;
    std::ifstream file(UTA_CLOTH_LABELS);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream fields(line);
        Label label;
        fields >> label.package >> label.texture;
        REQUIRE_FALSE(label.texture.empty());
        out.push_back(label);
    }
    return out;
}

/// Every package under System, Maps and Textures, by folded stem; the first in
/// path order wins, so a run names the same file every time.
std::map<std::string, std::filesystem::path> packagesByStem() {
    std::vector<std::filesystem::path> all;
    for (const char* directory : {"System", "Maps", "Textures"}) {
        const std::filesystem::path root = std::filesystem::path(UTA_UT_INSTALL_DIR) / directory;
        if (!std::filesystem::is_directory(root)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
            if (entry.is_regular_file() && test::real::isPackageExtension(uta::asciiFolded(entry.path().extension().string())))
                all.push_back(entry.path());
    }
    std::ranges::sort(all);
    std::map<std::string, std::filesystem::path> out;
    for (const auto& path : all) out.emplace(uta::asciiFolded(path.stem().string()), path);
    return out;
}

/// The fingerprint of `texture`'s top mip in `package`, or nothing with the
/// reason in `why`.
std::optional<std::uint64_t> pictureFingerprintOf(const upkg::Package& package, std::string_view texture,
                                                  std::string& why) {
    for (const upkg::ExportEntry& entry : package.exports()) {
        const auto name = package.name(entry.objectName);
        if (!name || uta::asciiFolded(*name) != uta::asciiFolded(texture)) continue;
        const auto properties = upkg::readProperties(package, entry);
        const auto read = upkg::readTexture(package, entry);
        if (!properties || !read || read->mips.empty()) continue; // a same-named non-texture
        std::optional<upkg::ObjectReference> paletteReference;
        for (const upkg::Property& property : *properties)
            if (const auto* const reference = std::get_if<upkg::ObjectReference>(&property.value);
                reference && uta::asciiFolded(package.name(property.nameIndex).value_or("")) == "palette")
                paletteReference = *reference;
        if (!paletteReference || paletteReference->kind() != upkg::ObjectReferenceKind::Export) {
            why = "its palette is not an export of its own package";
            return std::nullopt;
        }
        const auto palette = upkg::readPalette(package, package.exports()[paletteReference->index()]);
        if (!palette) {
            why = "its palette does not read";
            return std::nullopt;
        }
        const auto fingerprint = umat::pictureFingerprint(read->mips[0], *palette);
        if (!fingerprint) why = "its picture has no fingerprint";
        return fingerprint;
    }
    why = "no texture of that name";
    return std::nullopt;
}

} // namespace

TEST_CASE("each labelled texture is on the cloth list and nothing else is", "[real-assets][umat][cloth]") {
    const std::vector<Label> labels = readLabels();
    REQUIRE_FALSE(labels.empty());
    const auto packages = packagesByStem();
    std::set<std::uint64_t> labelled;
    for (const Label& label : labels) {
        INFO(label.package << "." << label.texture);
        const auto found = packages.find(uta::asciiFolded(label.package));
        REQUIRE(found != packages.end());
        const std::vector<std::byte> bytes = test::real::readWhole(found->second);
        const auto package = upkg::Package::open(test::real::viewOf(bytes));
        REQUIRE(package.has_value());
        std::string why;
        const auto fingerprint = pictureFingerprintOf(*package, label.texture, why);
        INFO(why);
        REQUIRE(fingerprint.has_value());
        // Printed so a new label's entry can be written from a failing run.
        INFO(std::format("{{0x{:016x}ULL, \"{}.{}\"}}", *fingerprint, uta::asciiFolded(label.package),
                         uta::asciiFolded(label.texture)));
        CHECK(umat::isCloth(*fingerprint));
        labelled.insert(*fingerprint);
    }
    for (const umat::ClothEntry& entry : umat::clothLibrary()) {
        INFO(entry.note);
        CHECK(labelled.contains(entry.fingerprint));
    }
}

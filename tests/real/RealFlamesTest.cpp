// UTA-0263 INV-3: every texture in tests/real/flame-labels.txt is judged a
// flame, or not, as it is labelled.
//
// docs/specs/UTA-0263-shader-flames.md SS 4.1. The judgement is the bake's:
// the still ubake::fireStill makes from the export's own sparks and settings,
// through its own palette, fingerprinted as the curated library keys a
// picture, then looked up in umat's flame list.

#include "real/RealSupport.h"

#include "ubake/FireStill.h"
#include "umat/Fingerprint.h"
#include "umat/Flames.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"
#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#ifndef UTA_FLAME_LABELS
#error "UTA_FLAME_LABELS must name tests/real/flame-labels.txt"
#endif

namespace {

using namespace uta;
using test::real::foldCase;

struct Label {
    std::string package;
    std::string texture;
    bool flame = false;
};

std::vector<Label> readLabels() {
    std::vector<Label> out;
    std::ifstream file(UTA_FLAME_LABELS);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream fields(line);
        Label label;
        std::string verdict;
        fields >> label.package >> label.texture >> verdict;
        REQUIRE((verdict == "flame" || verdict == "other"));
        label.flame = verdict == "flame";
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
            if (entry.is_regular_file() && test::real::isPackageExtension(foldCase(entry.path().extension().string())))
                all.push_back(entry.path());
    }
    std::ranges::sort(all);
    std::map<std::string, std::filesystem::path> out;
    for (const auto& path : all) out.emplace(foldCase(path.stem().string()), path);
    return out;
}

/// The fingerprint of the still the bake makes for `texture` in `package`, or
/// nothing with the reason in `why`.
std::optional<std::uint64_t> stillFingerprint(const upkg::Package& package, std::string_view texture,
                                              std::string& why) {
    for (const upkg::ExportEntry& entry : package.exports()) {
        const auto name = package.name(entry.objectName);
        const auto className = package.objectName(entry.objectClass);
        if (!name || !className || foldCase(*name) != foldCase(texture) || foldCase(*className) != "firetexture")
            continue;
        const auto properties = upkg::readProperties(package, entry);
        const auto read = upkg::readTexture(package, entry);
        if (!properties || !read || read->mips.empty()) {
            why = "it does not read";
            return std::nullopt;
        }
        std::optional<upkg::ObjectReference> paletteReference;
        for (const upkg::Property& property : *properties)
            if (const auto* const reference = std::get_if<upkg::ObjectReference>(&property.value);
                reference && foldCase(package.name(property.nameIndex).value_or("")) == "palette")
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
        upkg::Mip still = read->mips[0];
        const std::vector<std::byte> indices =
            ubake::fireStill(still.width, still.height, read->sparks, ubake::fireSettingsOf(package, *properties));
        still.pixels = indices;
        const auto fingerprint = umat::pictureFingerprint(still, *palette);
        if (!fingerprint) why = "its still has no fingerprint";
        return fingerprint;
    }
    why = "no FireTexture of that name";
    return std::nullopt;
}

} // namespace

TEST_CASE("each labelled FireTexture is judged a flame as it is labelled", "[real-assets][umat][flames]") {
    const std::vector<Label> labels = readLabels();
    REQUIRE_FALSE(labels.empty());
    const auto packages = packagesByStem();

    std::set<std::uint64_t> labelledFlames;
    for (const Label& label : labels) {
        INFO(label.package << "." << label.texture << " is labelled " << (label.flame ? "flame" : "other"));
        const auto found = packages.find(foldCase(label.package));
        REQUIRE(found != packages.end());
        const std::vector<std::byte> bytes = test::real::readWhole(found->second);
        const auto package = upkg::Package::open(test::real::viewOf(bytes));
        REQUIRE(package.has_value());
        std::string why;
        const auto fingerprint = stillFingerprint(*package, label.texture, why);
        INFO(why);
        REQUIRE(fingerprint.has_value());
        CHECK(umat::isFlame(*fingerprint) == label.flame);
        if (label.flame) labelledFlames.insert(*fingerprint);
    }

    // And the list holds nothing the labels do not call a flame.
    for (const umat::FlameEntry& entry : umat::flameLibrary()) {
        INFO(entry.note);
        CHECK(labelledFlames.contains(entry.fingerprint));
    }
}

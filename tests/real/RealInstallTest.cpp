// The second test tier. Built only when UTA_REAL_ASSET_TESTS is ON, because
// S7 requires the default suite to pass with no Unreal Tournament present.
//
// What this tier is for: the synthetic fixtures prove we read what we wrote,
// which is a closed loop. Only real packages prove we read what Epic and the
// community actually shipped. The first two cases assert the harness -- that
// the configured install is there and looks like one -- and the third points
// upkg's reader at every package in it.

#include "upkg/Class.h"
#include "upkg/Geometry.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"
#include "upkg/Sound.h"
#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cctype>
#include <map>
#include <span>
#include <utility>
#include <string>
#include <vector>

#ifndef UTA_UT_INSTALL_DIR
#error "UTA_UT_INSTALL_DIR must be defined for the real-asset tier"
#endif

namespace fs = std::filesystem;

TEST_CASE("the configured Unreal Tournament install is present", "[real-assets]") {
    const fs::path root{UTA_UT_INSTALL_DIR};
    REQUIRE(fs::exists(root));
    REQUIRE(fs::is_directory(root));
}

TEST_CASE("the install holds a Maps directory with packages in it", "[real-assets]") {
    const fs::path maps = fs::path{UTA_UT_INSTALL_DIR} / "Maps";
    REQUIRE(fs::exists(maps));

    int packages = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(maps)) {
        if (entry.is_regular_file() && entry.path().extension() == ".unr") {
            ++packages;
        }
    }
    // No expected count: the library grows, and a census here would go stale
    // the first time a map is added.
    CHECK(packages > 0);
}

// --- The reader against what actually shipped -------------------------------
//
// The synthetic fixtures prove we read what we wrote, which is a closed loop.
// This is the only test that can catch the class section 2.1 exists to name:
// an assumption that holds against the fixtures and not against 1999.
//
// It asserts no counts. Section 2.1's figures are one install's, and a census
// here would go stale the first time a map is added.

TEST_CASE("every package in the install either opens or is refused by version",
          "[real-assets]") {
    const fs::path root{UTA_UT_INSTALL_DIR};

    int opened = 0;
    int unsupported = 0;
    int truncated = 0;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string extension = entry.path().extension().string();
        if (extension != ".unr" && extension != ".utx" && extension != ".uax" &&
            extension != ".umx" && extension != ".u") {
            continue;
        }

        std::ifstream file(entry.path(), std::ios::binary);
        REQUIRE(file);
        const std::vector<char> raw{std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>()};
        const std::span<const std::byte> bytes{
            reinterpret_cast<const std::byte*>(raw.data()), raw.size()};

        const auto package = uta::upkg::Package::open(bytes);
        if (package.has_value()) {
            ++opened;
            // Whatever opened must also be self-consistent: every name index
            // resolves, and every export's bytes are inside the file.
            for (const auto& object : package->exports()) {
                const auto name = package->name(object.objectName);
                REQUIRE(name.has_value());
                const auto data = package->serialBytes(object);
                REQUIRE(data.has_value());
            }
            continue;
        }

        INFO("package: " << entry.path().string());
        INFO("error: " << package.error().message());

        if (package.error().code() == uta::ErrorCode::UnsupportedVersion) {
            ++unsupported;
            continue;
        }

        // A real install can hold a damaged file, and this one does: a
        // truncated M1.utx whose header names tables ~71 MB into a 3.6 MB
        // file. Tolerating that as a blanket "some failures are fine" would
        // let a reader that refused EVERYTHING pass, so the exemption is
        // proven per file instead -- read the three table offsets straight
        // out of the header here, without going through upkg, and require
        // that at least one of them really is past the end. A package that
        // fails for any other reason is one we are getting wrong.
        REQUIRE(raw.size() >= 36);
        const auto headerWord = [&raw](std::size_t at) {
            std::uint32_t value = 0;
            for (std::size_t i = 0; i < 4; ++i) {
                value |= static_cast<std::uint32_t>(
                             static_cast<unsigned char>(raw[at + i]))
                         << (8 * i);
            }
            return value;
        };
        const bool tablesPastEnd = headerWord(16) >= raw.size() ||
                                   headerWord(24) >= raw.size() ||
                                   headerWord(32) >= raw.size();
        REQUIRE(tablesPastEnd);
        ++truncated;
    }

    INFO("opened " << opened << ", refused by version " << unsupported
                   << ", provably truncated " << truncated);
    CHECK(opened > 0);
    // The install is overwhelmingly readable. A reader that broke would push
    // packages out of `opened` and into the arm above, where each one has to
    // prove itself truncated -- this is the backstop if some future damage
    // pattern satisfied that proof by accident.
    CHECK(truncated < opened / 100);
}

// --- The typed readers against what actually shipped ------------------------
//
// docs/specs/UTA-0004-typed-level-content.md SS 7 tier 3, and the acceptance
// for INV-1. Section 4.3's rule -- a reader that models a layout correctly
// ends exactly at its export's end -- is total: it fires on every field of
// every object without anyone predicting which field will be wrong.
//
// Refusals are BOUNDED, not merely recorded. An earlier draft of the spec
// allowed any refusal to be recorded rather than failed, which would have made
// this tier incapable of failing; two review lanes found it independently.

namespace {

struct ContentTotals {
    int polys = 0;
    int palettes = 0;
    int textures = 0;
    int sounds = 0;
    int recordedBadPropertyList = 0;
    int recordedOffsetMismatch = 0;
};

/// One of the two refusal shapes this install actually produces, and it is a
/// PRECONDITION failure rather than a typed-reading
/// one: the property list itself does not parse, so the typed reader never
/// reached its own layout. That layer is UTA-0003's and its own case in this
/// file governs it.
///
/// Proven per export rather than tolerated in bulk -- the same shape as the
/// truncated-M1.utx exemption above. The measured case is dUXmas.utx, whose
/// `USize` tag declares a one-byte size code against a four-byte Int value; a
/// second, independently written parser rejects it identically, which is what
/// makes "the package is malformed" the finding rather than "our reader is".
bool hasUnparseablePropertyList(const uta::upkg::Package& package,
                                const uta::upkg::ExportEntry& entry) {
    return !uta::upkg::readPropertyList(package, entry).has_value();
}

/// The third, and it is INV-5 doing its job: a mip's WidthOffset contradicts
/// where its own data ends, so the export disagrees with itself and the
/// reader refuses it.
///
/// Measured across this install: 289102 mips agree and 165 do not, and all
/// 165 sit in two community files. That ratio is why this is an exemption
/// rather than a reason to drop INV-5 -- and why the case below caps the
/// recorded total. A wrong serialOffset term in the reader would refuse
/// EVERY texture with this same message, which is exactly the systematic
/// error the cap catches and a per-export exemption alone would hide.
bool isOffsetMismatch(std::string_view message) {
    return message.find("WidthOffset") != std::string_view::npos ||
           message.find("NextOffset") != std::string_view::npos;
}

} // namespace

TEST_CASE("every modelled export in the install is consumed exactly",
          "[real-assets]") {
    const fs::path root{UTA_UT_INSTALL_DIR};
    ContentTotals totals;

    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string extension = entry.path().extension().string();
        if (extension != ".unr" && extension != ".utx" && extension != ".uax" &&
            extension != ".umx" && extension != ".u") {
            continue;
        }

        std::ifstream file(entry.path(), std::ios::binary);
        REQUIRE(file);
        const std::vector<char> raw{std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>()};
        const std::span<const std::byte> bytes{
            reinterpret_cast<const std::byte*>(raw.data()), raw.size()};

        const auto package = uta::upkg::Package::open(bytes);
        if (!package.has_value()) {
            continue; // the case above owns which packages may fail to open
        }

        for (const auto& object : package->exports()) {
            const auto className = package->objectName(object.objectClass);
            if (!className.has_value()) {
                continue;
            }
            const auto exportName = package->name(object.objectName);
            INFO("package: " << entry.path().string());
            INFO("export: " << (exportName.has_value() ? *exportName : "<unnamed>")
                            << " class " << *className);

            if (*className == "Polys") {
                const auto result = uta::upkg::readPolys(*package, object);
                REQUIRE(result.has_value());
                ++totals.polys;
            } else if (*className == "Palette") {
                const auto result = uta::upkg::readPalette(*package, object);
                REQUIRE(result.has_value());
                ++totals.palettes;
            } else if (*className == "Sound") {
                const auto result = uta::upkg::readSound(*package, object);
                REQUIRE(result.has_value());
                ++totals.sounds;
            } else if (uta::upkg::isModelledTextureClass(*className)) {
                const auto result = uta::upkg::readTexture(*package, object);
                if (result.has_value()) {
                    ++totals.textures;
                    continue;
                }
                INFO("error: " << result.error().message());
                // Bounded: only the two shapes section 7 names, each proven
                // per export. Anything else is a layout this reader gets
                // wrong, and the tier fails on it.
                if (hasUnparseablePropertyList(*package, object)) {
                    ++totals.recordedBadPropertyList;
                    continue;
                }
                if (isOffsetMismatch(result.error().message())) {
                    ++totals.recordedOffsetMismatch;
                    continue;
                }
                // Neither named shape. A layout this reader gets wrong.
                FAIL("unrecognised refusal shape: " << result.error().message());
            }
        }
    }

    // Printed rather than asserted: section 7 requires this tier to report its
    // own totals, so the spec's figures are an output of the suite rather than
    // prose nobody re-derives. No count is asserted -- the library grows.
    WARN("consumed exactly -- Polys " << totals.polys << ", Palette "
                                      << totals.palettes << ", Texture family "
                                      << totals.textures << ", Sound " << totals.sounds
                                      << "; recorded -- unparseable property list "
                                      << totals.recordedBadPropertyList
                                      << ", offset mismatch "
                                      << totals.recordedOffsetMismatch);
    CHECK(totals.polys > 0);
    CHECK(totals.palettes > 0);
    CHECK(totals.textures > 0);
    CHECK(totals.sounds > 0);

    // The backstop, and it is what keeps the three exemptions above from
    // becoming a blanket tolerance. Each is proven per export, but a
    // SYSTEMATIC reader error -- a wrong serialOffset term, a missed version
    // branch -- would push every texture into one of them and each would still
    // "prove" itself. The install is overwhelmingly readable, so a recorded
    // total anywhere near the read total is a defect in this reader rather
    // than in 1999's content.
    const int recorded = totals.recordedBadPropertyList + totals.recordedOffsetMismatch;
    CHECK(recorded < totals.textures / 100);
}

// --- UTA-0005: the class table against what actually shipped ----------------
//
// INV-1: every class export in the install is consumed exactly -- the reader
// finishes at serialOffset + serialSize. That rule is what makes SS 4.3's
// field order and SS 4.4's script walker checkable against content this
// project did not write, and it is the only check the walker's silent failure
// mode cannot slip past: a wrong script end still leaves a property list that
// parses and terminates where it should.

namespace {

/// Read a file whole. The Package holds a VIEW of these bytes, so the caller
/// keeps them alive.
std::vector<char> readWhole(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<char>{std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>()};
}

std::span<const std::byte> viewOf(const std::vector<char>& raw) {
    return std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(raw.data()), raw.size()};
}

bool isPackageExtension(const std::string& extension) {
    return extension == ".unr" || extension == ".utx" || extension == ".uax" ||
           extension == ".umx" || extension == ".u";
}

bool isClassExport(const uta::upkg::ExportEntry& entry) {
    return entry.objectClass.kind() == uta::upkg::ObjectReferenceKind::Null &&
           entry.serialSize > 0;
}

} // namespace

TEST_CASE("every class export in the install is consumed exactly", "[real-assets]") {
    const fs::path root{UTA_UT_INSTALL_DIR};

    int packagesWithClasses = 0;
    int packagesFullyConsumed = 0;
    int attempted = 0;
    int consumed = 0;

    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() ||
            !isPackageExtension(entry.path().extension().string())) {
            continue;
        }

        const std::vector<char> raw = readWhole(entry.path());
        const auto package = uta::upkg::Package::open(viewOf(raw));
        if (!package.has_value()) {
            // Whether a package SHOULD open is the case above, which proves
            // each failure per file. Here a package that did not open simply
            // contributes nothing.
            continue;
        }

        int here = 0;
        int hereConsumed = 0;
        for (const auto& object : package->exports()) {
            if (!isClassExport(object)) {
                continue;
            }
            ++here;
            const auto info = uta::upkg::readClass(*package, object);
            if (info.has_value()) {
                ++hereConsumed;
                continue;
            }
            INFO("package: " << entry.path().string());
            INFO("class: " << package->name(object.objectName).value_or("?"));
            INFO("error: " << info.error().message());
            FAIL("a class export was not consumed exactly");
        }

        if (here > 0) {
            ++packagesWithClasses;
            attempted += here;
            consumed += hereConsumed;
            if (here == hereConsumed) {
                ++packagesFullyConsumed;
            }
        }
    }

    INFO("class exports attempted: " << attempted);
    INFO("packages carrying classes: " << packagesWithClasses);

    // The install really does carry classes, so a reader that silently
    // recognised none of them cannot pass this.
    CHECK(attempted > 0);
    CHECK(packagesWithClasses > 0);

    // Consumed must equal attempted: a reader that starts refusing individual
    // exports has to FAIL rather than quietly report fewer successes.
    CHECK(consumed == attempted);

    // And the per-package tally, because a regression that refused a whole
    // package would lower attempted and consumed together and pass the line
    // above. No literal count is asserted -- the library grows, and a census
    // here would go stale the first time a map or mod is added -- so what is
    // checked is that every package carrying classes had ALL of them consumed.
    // A package that stops opening at all is caught by the case above, which
    // proves each failure against the file's own header.
    CHECK(packagesFullyConsumed == packagesWithClasses);
}

TEST_CASE("class ancestry resolves across the install's own packages", "[real-assets]") {
    // Scoped to System/*.u, which is where the class hierarchy lives. The
    // resolver holds every package it opens alive for the duration, so
    // widening this to the whole install would mean holding the texture and
    // sound packages in memory as well for no extra coverage of the walk.
    const fs::path system = fs::path{UTA_UT_INSTALL_DIR} / "System";
    if (!fs::exists(system)) {
        SUCCEED("no System directory in this install");
        return;
    }

    std::map<std::string, fs::path> byName;
    for (const fs::directory_entry& entry : fs::directory_iterator(system)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".u") {
            continue;
        }
        std::string stem = entry.path().stem().string();
        for (char& character : stem) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        byName.emplace(stem, entry.path());
    }
    REQUIRE_FALSE(byName.empty());

    // The resolver owns the lifetime of what it returns, which is the bargain
    // SS 4.6 states. These two maps are that ownership.
    std::map<std::string, std::vector<char>> bytes;
    std::map<std::string, uta::upkg::Package> opened;

    const uta::upkg::PackageResolver resolver =
        [&](std::string_view name) -> uta::Result<const uta::upkg::Package*> {
        const std::string key{name}; // already folded by readAncestry
        if (const auto cached = opened.find(key); cached != opened.end()) {
            return &cached->second;
        }
        const auto path = byName.find(key);
        if (path == byName.end()) {
            return nullptr; // not present: an ordinary case, not an error
        }
        auto& raw = bytes[key];
        raw = readWhole(path->second);
        auto package = uta::upkg::Package::open(viewOf(raw));
        if (!package.has_value()) {
            return nullptr;
        }
        return &opened.emplace(key, std::move(*package)).first->second;
    };

    int walked = 0;
    int reachedRoot = 0;
    int incomplete = 0;

    for (const auto& [name, path] : byName) {
        const std::vector<char> raw = readWhole(path);
        const auto package = uta::upkg::Package::open(viewOf(raw));
        if (!package.has_value()) {
            continue;
        }
        for (const auto& object : package->exports()) {
            if (!isClassExport(object)) {
                continue;
            }
            const auto ancestry = uta::upkg::readAncestry(*package, object, resolver);
            INFO("package: " << path.string());
            INFO("class: " << package->name(object.objectName).value_or("?"));
            // Terminating is the invariant. A chain that cannot be completed
            // ends SUCCESSFULLY in the state saying which content is absent --
            // an install that lacks a mod is the ordinary case.
            REQUIRE(ancestry.has_value());
            ++walked;
            if (ancestry->end == uta::upkg::AncestryEnd::Root) {
                ++reachedRoot;
            } else {
                ++incomplete;
            }
        }
    }

    INFO("chains walked: " << walked);
    INFO("reached a root: " << reachedRoot);
    INFO("ended incomplete: " << incomplete);
    CHECK(walked > 0);
    // Most of the hierarchy resolves within the install; the rest names
    // content it does not carry, which is legible rather than an error.
    CHECK(reachedRoot > 0);
}

// The second test tier. Built only when UTA_REAL_ASSET_TESTS is ON, because
// S7 requires the default suite to pass with no Unreal Tournament present.
//
// What this tier is for: the synthetic fixtures prove we read what we wrote,
// which is a closed loop. Only real packages prove we read what Epic and the
// community actually shipped. The first two cases assert the harness -- that
// the configured install is there and looks like one -- and the third points
// upkg's reader at every package in it.
//
// UTA-0006 SS 7 tier 3 adds the last case: it builds both graphs for every map
// and PRINTS the figures that spec's SS 2.1 asserts, so those numbers are an
// output of the suite rather than a transcription in a document. What it
// asserts is a population and two rates, aggregated over the whole install --
// never per map, because a level whose path network disagrees with its own
// nodes is content rather than a builder defect.

#include "unav/Build.h"
#include "unav/Graphs.h"
#include "upkg/Class.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"
#include "upkg/Sound.h"
#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <variant>
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
    int levels = 0;
    int models = 0;
    int modelsRefused = 0;
    int modelPolysResolved = 0;
    int modelPolysNull = 0;
    int recordedBadPropertyList = 0;
    int recordedOffsetMismatch = 0;
};

/// The class of whatever a reference points at, or nothing for a null one.
///
/// An export names its class by reference and an import by name index, so the
/// two tables answer this differently and INV-5 has to reach both.
std::optional<std::string_view> referencedClass(const uta::upkg::Package& package,
                                                uta::upkg::ObjectReference reference) {
    if (reference.kind() == uta::upkg::ObjectReferenceKind::Null) {
        return std::nullopt;
    }
    if (reference.kind() == uta::upkg::ObjectReferenceKind::Export) {
        if (reference.index() >= package.exports().size()) {
            return std::nullopt;
        }
        const auto name =
            package.objectName(package.exports()[reference.index()].objectClass);
        return name.has_value() ? std::optional{*name} : std::nullopt;
    }
    if (reference.index() >= package.imports().size()) {
        return std::nullopt;
    }
    const auto name = package.name(package.imports()[reference.index()].className);
    return name.has_value() ? std::optional{*name} : std::nullopt;
}

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
            } else if (*className == "Level") {
                // UTA-0057 SS 7: Level joins this zero-refusal set. Every map
                // has one, and this reader has no version branch of its own,
                // so a refusal is a reader defect until shown otherwise.
                const auto result = uta::upkg::readLevel(*package, object);
                REQUIRE(result.has_value());
                ++totals.levels;
            } else if (*className == "Model") {
                // UTA-0069 SS 7 tier 3: INV-1 at install scale, INV-4 and
                // INV-5. A refusal is TALLIED rather than asserted here so
                // that the residue is printed -- SS 4.6 is open, and the
                // figure is the derivation's instrument. INV-4 is the bar and
                // it is asserted at the end of this case, at zero.
                const auto result = uta::upkg::readModel(*package, object);
                if (!result.has_value()) {
                    ++totals.modelsRefused;
                    continue;
                }
                ++totals.models;

                // INV-5, and it is the one check that separates a correct
                // field assignment from a byte count that merely adds up.
                // Null is legitimate -- SS 6 -- and a Model need not own
                // brush polygons.
                const auto polysClass = referencedClass(*package, result->polys);
                if (!polysClass.has_value()) {
                    ++totals.modelPolysNull;
                } else {
                    INFO("Polys reference resolves to class " << *polysClass);
                    REQUIRE(*polysClass == "Polys");
                    ++totals.modelPolysResolved;
                }
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
                                      << ", Level " << totals.levels
                                      << ", Model " << totals.models
                                      << "; recorded -- unparseable property list "
                                      << totals.recordedBadPropertyList
                                      << ", offset mismatch "
                                      << totals.recordedOffsetMismatch);
    CHECK(totals.polys > 0);
    CHECK(totals.palettes > 0);
    CHECK(totals.textures > 0);
    CHECK(totals.sounds > 0);
    CHECK(totals.levels > 0);

    // The backstop, and it is what keeps the three exemptions above from
    // becoming a blanket tolerance. Each is proven per export, but a
    // SYSTEMATIC reader error -- a wrong serialOffset term, a missed version
    // branch -- would push every texture into one of them and each would still
    // "prove" itself. The install is overwhelmingly readable, so a recorded
    // total anywhere near the read total is a defect in this reader rather
    // than in 1999's content.
    const int recorded = totals.recordedBadPropertyList + totals.recordedOffsetMismatch;
    CHECK(recorded < totals.textures / 100);

    // UTA-0069 SS 7: this tier prints the figures that spec asserts, so they
    // are an output of the suite rather than prose nobody re-derives.
    WARN("Model -- consumed exactly " << totals.models << ", refused "
                                      << totals.modelsRefused << "; Polys reference -- "
                                      << totals.modelPolysResolved << " resolved to a "
                                      << "Polys-classed object, " << totals.modelPolysNull
                                      << " null");
    CHECK(totals.models > 0);
    // UTA-0069 INV-4, and it is the item's acceptance rather than a progress
    // measure: every Model export in the install, no tolerance. This is RED
    // while SS 4.6's residue is open, which SS 6 states as the honest
    // outcome -- a rate written in here would freeze unfinished derivation
    // into a permanent tolerance.
    CHECK(totals.modelsRefused == 0);
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

// --- UTA-0057 SS 4.6: are Paths values indices into the reach-spec array? ----
//
// INV-2 and INV-3. They have no fixture that could establish them: they are
// claims about content this project did not write, and a fixture would only
// assert what the fixture builder was told.
//
// Both are stated as RATES rather than as universal claims, and the reason is
// measurement rather than caution. Over the reference install every entry
// resolves on the overwhelming majority of maps, and the failures are confined
// to a handful whose path network disagrees with their own navigation points --
// maps carrying Paths values with NO reach-spec array at all, and maps whose
// network was built and then edited. That is content disagreeing with itself,
// which SS 6 already refuses to treat as a refusal.
//
// A rate is what the texture case above uses for the same shape, and it is
// scale-free: unlike a floor on a count it does not go stale as the library
// grows. It still catches what these invariants exist for. A reader that
// partitions the array wrongly, or that transposes a spec's start and end,
// does not lose a few tenths of a percent -- it loses nearly all of them.

namespace {

/// Does this actor's class descend from `NavigationPoint`?
///
/// SS 4.6: no export of the literal class carries a Paths entry, so an exact
/// name match resolves nothing and a name LIST does not close -- the corpus
/// was still introducing Paths-carrying classes in its last tenth. Ancestry is
/// the only approach that survives, which is why this needs UTA-0005.
bool descendsFromNavigationPoint(const uta::upkg::Package& package,
                                 const uta::upkg::ExportEntry& actor,
                                 const uta::upkg::PackageResolver& resolver,
                                 const std::map<std::string, fs::path>& systemPackages,
                                 std::map<std::string, bool>& cache);

/// The package an import ultimately lives in: follow its outer chain to the
/// root, which is where the package name sits.
std::string importPackageName(const uta::upkg::Package& package,
                              uta::upkg::ObjectReference reference) {
    for (int depth = 0; depth < 32; ++depth) {
        if (reference.kind() != uta::upkg::ObjectReferenceKind::Import) {
            return {};
        }
        const uta::upkg::ImportEntry& import = package.imports()[reference.index()];
        if (import.outer.kind() == uta::upkg::ObjectReferenceKind::Null) {
            const auto name = package.name(import.objectName);
            return name.has_value() ? std::string{*name} : std::string{};
        }
        reference = import.outer;
    }
    return {};
}

std::string foldCase(std::string_view value) {
    std::string folded{value};
    for (char& character : folded) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return folded;
}

bool descendsFromNavigationPoint(const uta::upkg::Package& package,
                                 const uta::upkg::ExportEntry& actor,
                                 const uta::upkg::PackageResolver& resolver,
                                 const std::map<std::string, fs::path>& systemPackages,
                                 std::map<std::string, bool>& cache) {
    (void)systemPackages;
    const auto className = package.objectName(actor.objectClass);
    if (!className.has_value()) {
        return false;
    }
    const bool imported = actor.objectClass.kind() == uta::upkg::ObjectReferenceKind::Import;
    const std::string home =
        imported ? foldCase(importPackageName(package, actor.objectClass)) : "<map>";
    // Cached per CLASS, not per actor: a map holds thousands of actors of a
    // few dozen classes, and the walk is what costs.
    const std::string key = home + "/" + std::string{*className};
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }

    const uta::upkg::Package* classHome = nullptr;
    const uta::upkg::ExportEntry* classExport = nullptr;
    if (!imported) {
        classHome = &package;
        classExport = &package.exports()[actor.objectClass.index()];
    } else if (!home.empty()) {
        const auto resolved = resolver(home);
        if (resolved.has_value() && *resolved != nullptr) {
            for (const auto& candidate : (*resolved)->exports()) {
                if (!isClassExport(candidate)) {
                    continue;
                }
                const auto name = (*resolved)->name(candidate.objectName);
                if (name.has_value() && *name == *className) {
                    classHome = *resolved;
                    classExport = &candidate;
                    break;
                }
            }
        }
    }

    bool descends = false;
    if (classHome != nullptr && classExport != nullptr) {
        const auto ancestry = uta::upkg::readAncestry(*classHome, *classExport, resolver);
        if (ancestry.has_value()) {
            for (const auto& link : ancestry->chain) {
                const auto name = link.package->name(link.entry->objectName);
                if (name.has_value() && *name == "NavigationPoint") {
                    descends = true;
                    break;
                }
            }
        }
    }
    return cache.emplace(key, descends).first->second;
}

} // namespace

TEST_CASE("a navigation point's Paths entries index the level's reach-spec array",
          "[real-assets]") {
    const fs::path maps = fs::path{UTA_UT_INSTALL_DIR} / "Maps";
    const fs::path system = fs::path{UTA_UT_INSTALL_DIR} / "System";
    if (!fs::exists(maps) || !fs::exists(system)) {
        SUCCEED("no Maps or System directory in this install");
        return;
    }

    std::map<std::string, fs::path> systemPackages;
    for (const fs::directory_entry& entry : fs::directory_iterator(system)) {
        if (entry.is_regular_file() && entry.path().extension() == ".u") {
            systemPackages.emplace(foldCase(entry.path().stem().string()), entry.path());
        }
    }
    REQUIRE_FALSE(systemPackages.empty());

    // The resolver owns the lifetime of what it returns, as the ancestry case
    // above does.
    std::map<std::string, std::vector<char>> systemBytes;
    std::map<std::string, uta::upkg::Package> systemOpened;
    const uta::upkg::PackageResolver resolver =
        [&](std::string_view name) -> uta::Result<const uta::upkg::Package*> {
        const std::string key{name}; // already folded by readAncestry
        if (const auto cached = systemOpened.find(key); cached != systemOpened.end()) {
            return &cached->second;
        }
        const auto path = systemPackages.find(key);
        if (path == systemPackages.end()) {
            return nullptr; // not present: an ordinary case, not an error
        }
        auto& raw = systemBytes[key];
        raw = readWhole(path->second);
        auto package = uta::upkg::Package::open(viewOf(raw));
        if (!package.has_value()) {
            return nullptr;
        }
        return &systemOpened.emplace(key, std::move(*package)).first->second;
    };

    std::map<std::string, bool> navigationClasses;
    long long entries = 0;
    long long inRange = 0;
    long long startIsTheNode = 0;
    long long navigationPoints = 0;
    long long negatives = 0;
    long long actorsUsingEverySlot = 0;

    for (const fs::directory_entry& entry : fs::directory_iterator(maps)) {
        if (!entry.is_regular_file() || foldCase(entry.path().extension().string()) != ".unr") {
            continue;
        }
        const std::vector<char> raw = readWhole(entry.path());
        const auto package = uta::upkg::Package::open(viewOf(raw));
        if (!package.has_value()) {
            continue; // an earlier case owns which packages may fail to open
        }

        const uta::upkg::ExportEntry* levelExport = nullptr;
        for (const auto& object : package->exports()) {
            const auto className = package->objectName(object.objectClass);
            if (className.has_value() && *className == "Level") {
                levelExport = &object;
                break;
            }
        }
        if (levelExport == nullptr) {
            continue;
        }
        INFO("map: " << entry.path().string());
        const auto level = uta::upkg::readLevel(*package, *levelExport);
        REQUIRE(level.has_value());

        for (std::size_t index = 0; index < package->exports().size(); ++index) {
            const uta::upkg::ExportEntry& actor = package->exports()[index];
            if (actor.objectClass.kind() == uta::upkg::ObjectReferenceKind::Null) {
                continue;
            }
            if (!descendsFromNavigationPoint(*package, actor, resolver, systemPackages,
                                             navigationClasses)) {
                continue;
            }
            ++navigationPoints;

            const auto properties = uta::upkg::readProperties(*package, actor);
            if (!properties.has_value()) {
                continue; // that layer is UTA-0003's, and SS 6 keeps its row
            }
            int slotsPresent = 0;
            for (const auto& property : *properties) {
                const auto name = package->name(property.nameIndex);
                if (!name.has_value() || *name != "Paths") {
                    continue;
                }
                ++slotsPresent;
                if (!std::holds_alternative<std::int32_t>(property.value)) {
                    continue;
                }
                const std::int32_t value = std::get<std::int32_t>(property.value);
                ++entries;
                if (value < 0) {
                    ++negatives;
                    continue;
                }
                if (static_cast<std::size_t>(value) >= level->reachSpecs.size()) {
                    continue;
                }
                ++inRange;
                // INV-3, and it is the check that separates a reader consuming
                // the right bytes from one assigning them to the right fields.
                const uta::upkg::ObjectReference start =
                    level->reachSpecs[static_cast<std::size_t>(value)].start;
                if (start.kind() == uta::upkg::ObjectReferenceKind::Export &&
                    start.index() == static_cast<std::uint32_t>(index)) {
                    ++startIsTheNode;
                }
            }
            // SS 4.6: if EVERY navigation point returned sixteen slots, unused
            // entries would be stored as a sentinel and the absent-property
            // rule would be wrong. Reported so the answer is re-derived rather
            // than trusted.
            if (slotsPresent == 16) {
                ++actorsUsingEverySlot;
            }
        }
    }

    WARN("navigation points " << navigationPoints << ", Paths entries " << entries
                              << ", in range " << inRange << ", start is the node "
                              << startIsTheNode << ", negative values " << negatives
                              << ", actors using all sixteen slots " << actorsUsingEverySlot);

    // SS 7: the tier asserts its own population, or both invariants pass
    // vacuously. A property-reading regression, or a class filter that
    // resolves nothing, surfaces no entries and satisfies every rate below by
    // checking nothing.
    REQUIRE(entries > 0);
    REQUIRE(navigationPoints > 0);

    // INV-2 and INV-3. Ninety-nine percent is a floor, not the measurement:
    // what is measured is far higher, and what a reader defect produces is far
    // lower. The gap between those two is what makes the floor stable.
    CHECK(inRange * 100 >= entries * 99);
    CHECK(startIsTheNode * 100 >= entries * 99);
}

TEST_CASE("both graphs build over every map, and the rates SS 2.1 measured hold",
          "[real-assets]") {
    const fs::path maps = fs::path{UTA_UT_INSTALL_DIR} / "Maps";
    const fs::path system = fs::path{UTA_UT_INSTALL_DIR} / "System";
    if (!fs::exists(maps) || !fs::exists(system)) {
        SUCCEED("no Maps or System directory in this install");
        return;
    }

    std::map<std::string, fs::path> systemPackages;
    for (const fs::directory_entry& entry : fs::directory_iterator(system)) {
        if (entry.is_regular_file() && entry.path().extension() == ".u") {
            systemPackages.emplace(foldCase(entry.path().stem().string()), entry.path());
        }
    }
    REQUIRE_FALSE(systemPackages.empty());

    // Built the same way as the Paths case above rather than shared with it:
    // folding the two together would edit that case to serve this one, and a
    // second copy is not yet a third.
    std::map<std::string, std::vector<char>> systemBytes;
    std::map<std::string, uta::upkg::Package> systemOpened;
    const uta::upkg::PackageResolver resolver =
        [&](std::string_view name) -> uta::Result<const uta::upkg::Package*> {
        const std::string key{name}; // already folded by the caller
        if (const auto cached = systemOpened.find(key); cached != systemOpened.end()) {
            return &cached->second;
        }
        const auto path = systemPackages.find(key);
        if (path == systemPackages.end()) {
            return nullptr; // not present: an ordinary case, not an error
        }
        auto& raw = systemBytes[key];
        raw = readWhole(path->second);
        auto package = uta::upkg::Package::open(viewOf(raw));
        if (!package.has_value()) {
            return nullptr;
        }
        return &systemOpened.emplace(key, std::move(*package)).first->second;
    };

    long long levels = 0;
    long long navNodes = 0;
    long long navEdges = 0;
    long long endpoints = 0;
    long long discardedEndpoints = 0;
    long long wiringNodes = 0;
    long long wiringEdges = 0;
    long long resolvedEvents = 0;
    long long danglingEvents = 0;
    // SS 13: the per-map maxima are printed here rather than asserted in the
    // document, which is what keeps the worst case a measurement.
    long long widestNavMap = 0;
    long long widestWiringMap = 0;

    for (const fs::directory_entry& entry : fs::directory_iterator(maps)) {
        if (!entry.is_regular_file() || foldCase(entry.path().extension().string()) != ".unr") {
            continue;
        }
        const std::vector<char> raw = readWhole(entry.path());
        const auto package = uta::upkg::Package::open(viewOf(raw));
        if (!package.has_value()) {
            continue; // an earlier case owns which packages may fail to open
        }
        INFO("map: " << entry.path().string());

        const uta::upkg::ExportEntry* levelExport = nullptr;
        for (const auto& object : package->exports()) {
            const auto className = package->objectName(object.objectClass);
            if (className.has_value() && *className == "Level") {
                levelExport = &object;
                break;
            }
        }
        if (levelExport == nullptr) {
            continue;
        }
        const auto level = uta::upkg::readLevel(*package, *levelExport);
        REQUIRE(level.has_value());
        ++levels;

        const auto nav = uta::unav::buildNavGraph(*package, *level, resolver);
        REQUIRE(nav.has_value());
        const auto wiring = uta::unav::buildWiringGraph(*package);
        REQUIRE(wiring.has_value());

        navNodes += static_cast<long long>(nav->nodes.size());
        navEdges += static_cast<long long>(nav->edges.size());
        // Two per spec, which is the denominator SS 2.1 measures against.
        endpoints += 2 * static_cast<long long>(level->reachSpecs.size());
        discardedEndpoints += nav->discardedEndpoints;
        widestNavMap = std::max(widestNavMap, static_cast<long long>(nav->edges.size()));

        wiringNodes += static_cast<long long>(wiring->nodes.size());
        wiringEdges += static_cast<long long>(wiring->edges.size());
        danglingEvents += static_cast<long long>(wiring->dangling.size());
        widestWiringMap =
            std::max(widestWiringMap, static_cast<long long>(wiring->edges.size()));

        // An event that resolved yields ONE EDGE PER TARGET (INV-3), so the
        // edge count is not the event count. The distinct (source, event) pairs
        // are, which is also how a consumer would recover them.
        std::set<std::pair<std::uint32_t, std::string>> firedAndResolved;
        for (const uta::unav::WiringEdge& edge : wiring->edges) {
            firedAndResolved.emplace(edge.from, edge.event);
        }
        resolvedEvents += static_cast<long long>(firedAndResolved.size());
    }

    const long long resolvedEndpoints = endpoints - discardedEndpoints;
    const long long events = resolvedEvents + danglingEvents;

    WARN("levels " << levels << "; nav nodes " << navNodes << ", edges " << navEdges
                   << ", endpoints " << endpoints << ", discarded " << discardedEndpoints
                   << ", widest map " << widestNavMap << " edges"
                   << "; wiring nodes " << wiringNodes << ", edges " << wiringEdges
                   << ", events " << events << ", resolved " << resolvedEvents
                   << ", dangling " << danglingEvents << ", widest map " << widestWiringMap
                   << " edges");

    // SS 7: the tier asserts its own population, or every rate below passes
    // vacuously. A node filter that resolves nothing, or a property-reading
    // regression, surfaces no graph and satisfies a rate by checking nothing.
    REQUIRE(levels > 0);
    REQUIRE(navEdges > 0);
    REQUIRE(resolvedEvents > 0);

    // Both floors sit well under their measurement and well over what a defect
    // produces: a builder using the wrong index space does not lose a few
    // percent of endpoints but nearly all of them.
    CHECK(resolvedEndpoints * 100 >= endpoints * 95);
    CHECK(resolvedEvents * 100 >= events * 90);
}

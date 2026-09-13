// The second test tier. Built only when UTA_REAL_ASSET_TESTS is ON, because
// S7 requires the default suite to pass with no Unreal Tournament present.
//
// What this tier is for: the synthetic fixtures prove we read what we wrote,
// which is a closed loop. Only real packages prove we read what Epic and the
// community actually shipped. The first two cases assert the harness -- that
// the configured install is there and looks like one -- and the third points
// upkg's reader at every package in it.
//
//
// The tier is split by subject (UTA-0103): the graphs, the room map, the
// material census and the baker each have a file of their own, and what
// more than one of them needs is tests/real/RealSupport.h.

#include "core/FileSystem.h"
#include "upkg/Class.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"
#include "upkg/Sound.h"
#include "upkg/Texture.h"
#include "real/RealSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace uta::test::real;

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

        const auto read = uta::fs::readFile(entry.path());
        REQUIRE(read.has_value());
        const std::vector<std::byte>& raw = *read;
        const std::span<const std::byte> bytes{raw};

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
    int packagesWithAModel = 0;
    int packagesWhoseLargestModelParsed = 0;
    int recordedBadPropertyList = 0;
    int recordedOffsetMismatch = 0;
};

/// Which table a refused `Model` stopped in.
///
/// UTA-0069 SS 4.6 derives its residue in FILE ORDER, and says why: fixing
/// the tables out of order attributes one table's failures to another, which
/// is what makes a residue look irreducible when it is not. That ordering
/// needs the failures sorted, so this is the derivation's instrument rather
/// than a diagnostic nicety.
///
/// The reader names the table in every refusal -- the count check writes it,
/// and a short read inside an element carries it as context.
std::string modelRefusalBucket(std::string_view message) {
    // Longest-first where one name contains another: "leaves count" is a
    // short read on the count itself, "leaves" the refusal of a populated
    // table, and they are different findings.
    static constexpr std::string_view TABLES[] = {
        "property list",     "serialised bytes", "bounding prefix",
        "shared-side count", "zone count",       "Polys reference",
        "leaves count",      "vectors",          "points",
        "nodes",             "surfs",            "verts",
        "zones",             "lightmap entries", "lightmap bytes",
        "bounds",            "leaf hulls",       "leaves",
        "lights",            "trailing fields"};
    // Not a table at all, and checked first so no table can claim it: SS 4.6's
    // version-61 class, where the tables are separate exports rather than
    // inline and the reader refuses the export by name. UTA-0072 reads it.
    if (message.find("package version") != std::string_view::npos) {
        return "package version below 62";
    }
    for (const std::string_view table : TABLES) {
        if (message.find(table) != std::string_view::npos) {
            return std::string(table);
        }
    }
    // SS 4.6's second class: the walk consumed a plausible number of bytes and
    // still landed wrong, so no table reports an error.
    if (message.find("unread") != std::string_view::npos) {
        return "completed at the wrong offset";
    }
    return "unclassified";
}

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
    std::map<std::string, int> modelRefusalsByTable;
    std::set<std::string> packagesHoldingAModel;
    std::set<std::string> packagesWithARefusedModel;

    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string extension = entry.path().extension().string();
        if (extension != ".unr" && extension != ".utx" && extension != ".uax" &&
            extension != ".umx" && extension != ".u") {
            continue;
        }

        const auto read = uta::fs::readFile(entry.path());
        REQUIRE(read.has_value());
        const std::vector<std::byte>& raw = *read;
        const std::span<const std::byte> bytes{raw};

        const auto package = uta::upkg::Package::open(bytes);
        if (!package.has_value()) {
            continue; // the case above owns which packages may fail to open
        }

        // A map holds hundreds of Models -- one per editor brush -- but its
        // geometry lives in the LARGEST one. Whether THAT one parses is the
        // question UTA-0007 and UTA-0011 turn on; a refused brush model costs
        // them nothing. The export count cannot answer it, so track it here.
        std::uint32_t largestModelBytes = 0;
        bool largestModelParsed = false;
        bool sawAModel = false;

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
                packagesHoldingAModel.insert(entry.path().string());
                const auto result = uta::upkg::readModel(*package, object);
                sawAModel = true;
                if (object.serialSize >= largestModelBytes) {
                    largestModelBytes = object.serialSize;
                    largestModelParsed = result.has_value();
                }
                if (!result.has_value()) {
                    ++totals.modelsRefused;
                    ++modelRefusalsByTable[modelRefusalBucket(result.error().message())];
                    packagesWithARefusedModel.insert(entry.path().string());
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

        if (sawAModel) {
            ++totals.packagesWithAModel;
            if (largestModelParsed) {
                ++totals.packagesWhoseLargestModelParsed;
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
    // Where the walk stops, sorted by table -- SS 4.6's file-order rule needs
    // this, and it is what says which table to derive next.
    for (const auto& [table, count] : modelRefusalsByTable) {
        WARN("Model refusals at " << table << ": " << count);
    }
    // How much of the LIBRARY the residue costs, which the export count does
    // not say: one refused Model can make a whole map unusable to a consumer.
    WARN("packages whose LARGEST Model parses -- "
         << totals.packagesWhoseLargestModelParsed << " of " << totals.packagesWithAModel);
    WARN("packages holding a Model -- " << packagesHoldingAModel.size() << ", of which "
                                        << packagesWithARefusedModel.size()
                                        << " hold at least one refused Model");

    CHECK(totals.models > 0);
    // UTA-0069 INV-4, and it is the item's acceptance rather than a progress
    // measure: every Model export this reader CLAIMS, no tolerance.
    //
    // One class is scoped out, by the user's ruling of 2026-09-07 rather than
    // by this test's judgement: below package version 62 a Model keeps its
    // BSP tables in separate exports, readModel refuses it by name, and
    // UTA-0072 is the item that reads it. So the bar is that every refusal is
    // THAT class and there is no other. It is not a rate -- a rate would
    // freeze unfinished derivation into a permanent tolerance -- and it
    // tightens to zero on its own when UTA-0072 lands, because the bucket
    // empties and this reads `refused == 0`.
    const auto scopedOut = modelRefusalsByTable.find("package version below 62");
    const int refusedForVersion =
        scopedOut == modelRefusalsByTable.end() ? 0 : scopedOut->second;
    CHECK(totals.modelsRefused == refusedForVersion);
}

// --- UTA-0005: the class table against what actually shipped ----------------
//
// INV-1: every class export in the install is consumed exactly -- the reader
// finishes at serialOffset + serialSize. That rule is what makes SS 4.3's
// field order and SS 4.4's script walker checkable against content this
// project did not write, and it is the only check the walker's silent failure
// mode cannot slip past: a wrong script end still leaves a property list that
// parses and terminates where it should.

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

        const std::vector<std::byte> raw = readWhole(entry.path());
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

    // The resolver owns the lifetime of what it returns, which is the bargain
    // SS 4.6 states. SystemPackages is that ownership.
    SystemPackages packages{system};
    REQUIRE_FALSE(packages.empty());
    const uta::upkg::PackageResolver resolver = packages.resolver();

    int walked = 0;
    int reachedRoot = 0;
    int incomplete = 0;

    for (const auto& [name, path] : packages.paths()) {
        const std::vector<std::byte> raw = readWhole(path);
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

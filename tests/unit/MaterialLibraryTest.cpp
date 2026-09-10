// UTA-0010's curated-library cases.
//
// docs/specs/UTA-0010-curated-material-library.md, INV-1 to INV-7. INV-4 is
// also a static_assert in src/umat/CuratedMaterials.cpp; INV-8 and INV-9 are a
// real-asset census in tests/real/RealInstallTest.cpp.
//
// EVERY LEVEL IS SYNTHETIC. Nothing here reads a package, so the whole file
// runs on a clone with no Unreal Tournament install (S7).
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "umat/Fingerprint.h"
#include "umat/Generate.h"
#include "umat/Library.h"

#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <set>
#include <vector>

using uta::umat::applied;
using uta::umat::curated;
using uta::umat::CuratedEntry;
using uta::umat::curatedLibrary;
using uta::umat::CuratedOverride;
using uta::umat::CurationSource;
using uta::umat::libraryDigest;
using uta::umat::MaterialSettings;
using uta::umat::pictureFingerprint;
using uta::upkg::Mip;
using uta::upkg::Palette;
namespace detail = uta::umat::detail;

namespace {

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int value : values) out.push_back(static_cast<std::byte>(value));
    return out;
}

/// A level viewing `indices`, which must outlive it.
Mip level(const std::vector<std::byte>& indices, std::uint32_t width, std::uint32_t height) {
    Mip out;
    out.pixels = indices;
    out.width = width;
    out.height = height;
    return out;
}

/// INV-1's palette. Alpha differs per entry so a hash that reads it would move.
Palette fixturePalette() {
    return Palette{{{10, 20, 30, 255}, {40, 50, 60, 0}, {70, 80, 90, 128}, {100, 110, 120, 7}}};
}

bool same(const MaterialSettings& a, const MaterialSettings& b) {
    return a.requestedUpscale == b.requestedUpscale && a.metallic == b.metallic
           && a.baseRoughness == b.baseRoughness && a.emissive == b.emissive
           && a.emissiveThreshold == b.emissiveThreshold;
}

} // namespace

TEST_CASE("a picture's fingerprint matches its golden value", "[umat][library]") {
    // INV-1. Computed outside this codebase, by a Python FNV-1a written from
    // the source's decimal constants, so a wrong hex constant here disagrees.
    const auto indices = bytes({0, 1, 2, 3, 3, 2, 1, 0});
    CHECK(pictureFingerprint(level(indices, 4, 2), fixturePalette()) == 0xbd7a9d4a4faf78f3ULL);
}

TEST_CASE("the fingerprint follows the pixels the palette colours and the shape",
          "[umat][library]") {
    // INV-2.
    const auto indices = bytes({0, 1, 2, 3, 3, 2, 1, 0});
    const Palette palette = fixturePalette();
    const auto original = pictureFingerprint(level(indices, 4, 2), palette);
    REQUIRE(original.has_value());

    CHECK(pictureFingerprint(level(indices, 2, 4), palette) != original);

    auto changedIndex = indices;
    changedIndex[5] = std::byte{0};
    CHECK(pictureFingerprint(level(changedIndex, 4, 2), palette) != original);

    Palette changedGreen = palette;
    changedGreen.entries[2].g = 81;
    CHECK(pictureFingerprint(level(indices, 4, 2), changedGreen) != original);

    Palette changedAlpha = palette;
    changedAlpha.entries[2].a = 0;
    CHECK(pictureFingerprint(level(indices, 4, 2), changedAlpha) == original);
}

TEST_CASE("a level that is not one byte a texel has no fingerprint", "[umat][library]") {
    // INV-3: 16 bytes is four a texel at 2x2, the size of a high-colour level.
    const std::vector<std::byte> sixteen(16, std::byte{1});
    CHECK_FALSE(pictureFingerprint(level(sixteen, 2, 2), fixturePalette()).has_value());
}

TEST_CASE("the curated table is sorted with no fingerprint twice", "[umat][library]") {
    // INV-4, the runtime twin of CuratedMaterials.cpp's static_assert.
    const auto table = curatedLibrary();
    REQUIRE_FALSE(table.empty());
    CHECK(std::ranges::adjacent_find(table, std::greater_equal{}, &CuratedEntry::fingerprint)
          == table.end());
}

TEST_CASE("curated finds every entry and nothing either side of one", "[umat][library]") {
    // INV-5. A neighbour that is itself an entry is skipped, not expected null.
    const auto table = curatedLibrary();
    REQUIRE_FALSE(table.empty());
    std::set<std::uint64_t> fingerprints;
    for (const CuratedEntry& entry : table) fingerprints.insert(entry.fingerprint);

    for (const CuratedEntry& entry : table) {
        CHECK(curated(entry.fingerprint) == &entry.settings);
        for (const std::uint64_t beside : {entry.fingerprint - 1, entry.fingerprint + 1})
            if (!fingerprints.contains(beside)) CHECK(curated(beside) == nullptr);
    }
}

TEST_CASE("applied replaces exactly the fields an entry sets", "[umat][library]") {
    // INV-6. `before` differs from `full` in every field, and from the
    // defaults in every field, so a field reset to its default is caught.
    MaterialSettings before;
    before.requestedUpscale = 1;
    before.metallic = true;
    before.baseRoughness = 10;
    before.emissive = true;
    before.emissiveThreshold = 20;
    REQUIRE(before.requestedUpscale != MaterialSettings{}.requestedUpscale);

    CuratedOverride full;
    full.metallic = false;
    full.baseRoughness = 200;
    full.emissive = false;
    full.emissiveThreshold = 100;

    MaterialSettings expected = before;
    CuratedOverride one;
    one.metallic = full.metallic;
    expected.metallic = false;
    CHECK(same(applied(before, one), expected));

    expected = before;
    one = {};
    one.baseRoughness = full.baseRoughness;
    expected.baseRoughness = 200;
    CHECK(same(applied(before, one), expected));

    expected = before;
    one = {};
    one.emissive = full.emissive;
    expected.emissive = false;
    CHECK(same(applied(before, one), expected));

    expected = before;
    one = {};
    one.emissiveThreshold = full.emissiveThreshold;
    expected.emissiveThreshold = 100;
    CHECK(same(applied(before, one), expected));

    expected = before;
    expected.metallic = false;
    expected.baseRoughness = 200;
    expected.emissive = false;
    expected.emissiveThreshold = 100;
    CHECK(same(applied(before, full), expected));

    CHECK(same(applied(before, CuratedOverride{}), before));
}

TEST_CASE("the digest follows every fingerprint and override but not the audit fields",
          "[umat][library]") {
    // INV-7. Entry 0 sets nothing and entry 1 sets everything, so every field
    // can be moved from empty to present and changed in value.
    std::array<CuratedEntry, 2> table{{{0x10, "a.b", CurationSource::GroupName, {}},
                                       {0x20, "c.d", CurationSource::MetalSound, {}}}};
    table[1].settings.metallic = true;
    table[1].settings.baseRoughness = 50;
    table[1].settings.emissive = true;
    table[1].settings.emissiveThreshold = 60;
    const std::uint64_t original = detail::digestOf(table);
    const auto digestAfter = [&table](const std::function<void(std::array<CuratedEntry, 2>&)>& edit) {
        auto copy = table;
        edit(copy);
        return detail::digestOf(copy);
    };

    CHECK(digestAfter([](auto& t) { t[1].fingerprint = 0x21; }) != original);

    CHECK(digestAfter([](auto& t) { t[0].settings.metallic = false; }) != original);
    CHECK(digestAfter([](auto& t) { t[0].settings.baseRoughness = 0; }) != original);
    CHECK(digestAfter([](auto& t) { t[0].settings.emissive = false; }) != original);
    CHECK(digestAfter([](auto& t) { t[0].settings.emissiveThreshold = 0; }) != original);

    CHECK(digestAfter([](auto& t) { t[1].settings.metallic = false; }) != original);
    CHECK(digestAfter([](auto& t) { t[1].settings.baseRoughness = 51; }) != original);
    CHECK(digestAfter([](auto& t) { t[1].settings.emissive = false; }) != original);
    CHECK(digestAfter([](auto& t) { t[1].settings.emissiveThreshold = 61; }) != original);

    // A value moving from one field to the next is a change too: without the
    // presence byte these two would hash the same bytes.
    CHECK(digestAfter([](auto& t) { t[0].settings.metallic = true; })
          != digestAfter([](auto& t) { t[0].settings.baseRoughness = 1; }));

    CHECK(digestAfter([](auto& t) { t[1].note = "elsewhere.else"; }) == original);
    CHECK(digestAfter([](auto& t) { t[1].source = CurationSource::Play; }) == original);

    CHECK(libraryDigest() == detail::digestOf(curatedLibrary()));
}

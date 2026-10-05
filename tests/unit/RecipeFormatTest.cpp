// UTA-0113's format cases -- INV-1, INV-2 and INV-3.
//
// docs/specs/UTA-0113-recipe-format.md SS 4.2 and SS 4.4.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "core/Error.h"
#include "core/Sha256.h"
#include "urecipe/Recipe.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::urecipe::MaterialAssignment;
using uta::urecipe::Recipe;
namespace urecipe = uta::urecipe;

namespace {

/// SS 4.2's example, in an order and spelling `write` does not use: materials
/// out of order, mixed case, comments, CR LF line ends and a quoted name.
constexpr std::string_view HAND_WRITTEN =
    "# my recipe\r\n"
    "ut-ants recipe 1   # the header\r\n"
    "\r\n"
    "[map]\r\n"
    "file = DM-Deck16][\r\n"
    "sha256 = 2F8A000000000000000000000000000000000000000000000000000000000Ae1\r\n"
    "name = \"Deck 16 # the \\\"original\\\" = \\\\ \"   # a comment\r\n"
    "\r\n"
    "[material UTtech1.Floor.bmFloor4]\r\n"
    "upscale = 2\r\n"
    "\r\n"
    "[material uttech1.wall.bmdirtyt]\r\n"
    "metallic = false\r\n"
    "emissive = true\r\n"
    "base-roughness = 0\r\n"
    "emissive-threshold = 200\r\n"
    "parallax-depth = 255\r\n";

Recipe parsed(std::string_view text) {
    auto recipe = urecipe::parse(text);
    if (!recipe.has_value()) FAIL("refused: " << recipe.error().message());
    return *recipe;
}

std::array<std::byte, 32> digestFromHex(std::string_view hex) {
    std::array<std::byte, 32> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<std::byte>(std::stoi(std::string(hex.substr(2 * i, 2)), nullptr, 16));
    return out;
}

Recipe sample() {
    Recipe recipe;
    recipe.map = "dm-deck16][";
    recipe.mapDigest = digestFromHex("2f8a000000000000000000000000000000000000000000000000000000000ae1");
    recipe.friendlyName = "Deck 16, the original";
    MaterialAssignment wall;
    wall.texture = "uttech1.wall.bmdirtyt";
    wall.metallic = false;
    wall.emissive = true;
    wall.emissiveThreshold = 200;
    MaterialAssignment floor;
    floor.texture = "uttech1.floor.bmfloor4";
    floor.upscale = 2;
    recipe.materials = {floor, wall};
    return recipe;
}

} // namespace

TEST_CASE("INV-1: a hand-written recipe reads every field", "[urecipe][format]") {
    const Recipe recipe = parsed(HAND_WRITTEN);
    CHECK(recipe.map == "dm-deck16][");
    REQUIRE(recipe.mapDigest.has_value());
    CHECK(*recipe.mapDigest == digestFromHex("2f8a000000000000000000000000000000000000000000000000000000000ae1"));
    CHECK(recipe.friendlyName == "Deck 16 # the \"original\" = \\ ");
    REQUIRE(recipe.materials.size() == 2);

    // Sorted by texture, and folded.
    const MaterialAssignment& floor = recipe.materials[0];
    CHECK(floor.texture == "uttech1.floor.bmfloor4");
    CHECK(floor.upscale == 2U);
    CHECK_FALSE(floor.metallic.has_value());
    CHECK_FALSE(floor.emissive.has_value());
    CHECK_FALSE(floor.baseRoughness.has_value());
    CHECK_FALSE(floor.emissiveThreshold.has_value());
    CHECK_FALSE(floor.parallaxDepth.has_value());

    const MaterialAssignment& wall = recipe.materials[1];
    CHECK(wall.texture == "uttech1.wall.bmdirtyt");
    CHECK(wall.metallic == false);
    CHECK(wall.emissive == true);
    CHECK(wall.baseRoughness == std::uint8_t{0});
    CHECK(wall.emissiveThreshold == std::uint8_t{200});
    CHECK(wall.parallaxDepth == std::uint8_t{255});
    CHECK_FALSE(wall.upscale.has_value());
}

TEST_CASE("INV-1: parse of write gives back the recipe", "[urecipe][format]") {
    const Recipe original = sample();
    CHECK(parsed(urecipe::write(original)) == original);

    const Recipe handWritten = parsed(HAND_WRITTEN);
    CHECK(parsed(urecipe::write(handWritten)) == handWritten);

    Recipe bare;
    bare.map = "dm-x";
    CHECK(parsed(urecipe::write(bare)) == bare);

    // A map name `write` must quote to read back.
    Recipe awkward;
    awkward.map = "dm-a#b";
    CHECK(parsed(urecipe::write(awkward)) == awkward);
}

TEST_CASE("INV-1: write emits the canonical form", "[urecipe][format]") {
    CHECK(urecipe::write(sample())
          == "ut-ants recipe 1\n"
             "\n"
             "[map]\n"
             "file = dm-deck16][\n"
             "sha256 = 2f8a000000000000000000000000000000000000000000000000000000000ae1\n"
             "name = \"Deck 16, the original\"\n"
             "\n"
             "[material uttech1.floor.bmfloor4]\n"
             "upscale = 2\n"
             "\n"
             "[material uttech1.wall.bmdirtyt]\n"
             "metallic = false\n"
             "emissive = true\n"
             "emissive-threshold = 200\n");
    // A parsed hand-written file writes canonically, keys in SS 4.2's order.
    const std::string rewritten = urecipe::write(parsed(HAND_WRITTEN));
    CHECK(rewritten.find("metallic = false\nemissive = true\nbase-roughness = 0\n"
                         "emissive-threshold = 200\nparallax-depth = 255\n")
          != std::string::npos);
}

TEST_CASE("INV-1: a byte-order mark before the header is accepted", "[urecipe][format]") {
    CHECK(parsed("\xEF\xBB\xBFut-ants recipe 1\n[map]\nfile = dm-x\n").map == "dm-x");
}

TEST_CASE("INV-2: every refusal names its line", "[urecipe][format]") {
    struct Refusal {
        std::string_view what;
        std::string text;
        std::size_t line;
        ErrorCode code = ErrorCode::MalformedData;
    };
    const std::string head = "ut-ants recipe 1\n[map]\nfile = dm-x\n"; // lines 1 to 3
    const std::vector<Refusal> refusals = {
        {"no header", "[map]\nfile = dm-x\n", 1},
        {"an empty file", "", 1},
        {"a version above RECIPE_VERSION", "\n# c\nut-ants recipe 2\n[map]\nfile = dm-x\n", 3,
         ErrorCode::UnsupportedVersion},
        {"version zero", "ut-ants recipe 0\n", 1},
        {"an unknown section", head + "[fog]\n", 4},
        {"an unknown map key", head + "colour = red\n", 4},
        {"an unknown material key", head + "[material a.b]\nshiny = true\n", 5},
        {"a repeated key", head + "file = dm-x\n", 4},
        {"a repeated material", head + "[material a.b]\n[material A.B]\n", 5},
        {"a repeated map section", head + "[map]\n", 4},
        {"a byte out of range", head + "[material a.b]\nbase-roughness = 256\n", 5},
        {"a negative byte", head + "[material a.b]\nparallax-depth = -1\n", 5},
        {"an upscale not 1 2 or 4", head + "[material a.b]\nupscale = 3\n", 5},
        {"a bool not true or false", head + "[material a.b]\nmetallic = yes\n", 5},
        {"a short sha256", "ut-ants recipe 1\n[map]\nsha256 = abc\n", 3},
        {"a line with no equals sign", head + "file dm-x\n", 4},
        {"a key with no value", head + "name =   # nothing\n", 4},
        {"an unclosed quote", head + "name = \"Deck 16\n", 4},
        {"an unknown escape", head + "name = \"a\\nb\"\n", 4},
        {"text after a closing quote", head + "name = \"a\" b\n", 4},
        {"a map section with no file", "ut-ants recipe 1\n\n[map]\nname = x\n", 3},
        {"no map section", "ut-ants recipe 1\n", 1},
        {"a key before any section", "ut-ants recipe 1\nfile = dm-x\n", 2},
        {"a masked texture name", head + "[material a.b#masked]\n", 4},
        {"a texture with no package", head + "[material wall]\n", 4},
        {"a material with no texture", head + "[material]\n", 4},
        {"a heading with no bracket", head + "[material a.b\n", 4},
    };
    for (const Refusal& refusal : refusals) {
        DYNAMIC_SECTION(refusal.what) {
            const auto result = urecipe::parse(refusal.text);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == refusal.code);
            const std::string message(result.error().message());
            INFO("message: " << message);
            CHECK(message.starts_with("recipe line " + std::to_string(refusal.line) + ": "));
        }
    }
}

TEST_CASE("INV-2: a newer recipe names both versions", "[urecipe][format]") {
    const auto result = urecipe::parse("ut-ants recipe 7\n");
    REQUIRE_FALSE(result.has_value());
    const std::string message(result.error().message());
    CHECK(message.find("version 7") != std::string::npos);
    CHECK(message.find("version 1") != std::string::npos);
}

TEST_CASE("INV-3: bakeDigest covers the materials and nothing else", "[urecipe][format]") {
    const Recipe base = sample();
    const auto digest = urecipe::bakeDigest(base);

    SECTION("the friendly name and the map's digest are left out") {
        Recipe renamed = base;
        renamed.friendlyName = "another name";
        CHECK(urecipe::bakeDigest(renamed) == digest);
        Recipe undigested = base;
        undigested.mapDigest.reset();
        CHECK(urecipe::bakeDigest(undigested) == digest);
    }

    SECTION("the order materials are held in is left out") {
        Recipe reversed = base;
        std::swap(reversed.materials[0], reversed.materials[1]);
        CHECK(urecipe::bakeDigest(reversed) == digest);
    }

    SECTION("every material field and a material's presence count") {
        std::vector<Recipe> changed(9, base);
        changed[0].materials[1].metallic = true;
        changed[1].materials[1].emissive.reset();
        changed[2].materials[1].baseRoughness = 0;
        changed[3].materials[1].emissiveThreshold = 201;
        changed[4].materials[1].parallaxDepth = 0;
        changed[5].materials[0].upscale = 4;
        changed[6].materials.pop_back();
        changed[7].materials[0].texture = "uttech1.floor.bmfloor5";
        changed[8].materials[1].metallic.reset(); // present-false and absent differ
        for (std::size_t i = 0; i < changed.size(); ++i) {
            INFO("change " << i);
            CHECK(urecipe::bakeDigest(changed[i]) != digest);
        }
    }

    SECTION("the digest is the SHA-256 of SS 4.4's bytes") {
        Recipe one;
        one.map = "dm-x";
        MaterialAssignment material;
        material.texture = "a.b";
        material.emissive = true;
        material.upscale = 4;
        one.materials = {material};
        std::string bytes = "uta-recipe-bake-1\na.b\n";
        bytes += std::string("\x00", 1);          // metallic absent
        bytes += std::string("\x01\x01", 2);      // emissive true
        bytes += std::string("\x00\x00\x00", 3);  // the three bytes absent
        bytes += std::string("\x01\x04", 2);      // upscale 4
        CHECK(urecipe::bakeDigest(one)
              == uta::sha256(std::as_bytes(std::span<const char>(bytes.data(), bytes.size()))));
    }
}

// UTA-0113's format cases -- INV-1, INV-2 and INV-3 -- and UTA-0256's lamp
// cases, its INV-1 and INV-2.
//
// docs/specs/UTA-0113-recipe-format.md SS 4.2 and SS 4.4;
// docs/specs/UTA-0256-added-lamps.md SS 4.1.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "core/Error.h"
#include "core/Sha256.h"
#include "urecipe/Recipe.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::urecipe::AddedLamp;
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
        {"a version above RECIPE_VERSION",
         "\n# c\nut-ants recipe " + std::to_string(urecipe::RECIPE_VERSION + 1) + "\n[map]\nfile = dm-x\n", 3,
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
    const auto result = urecipe::parse("ut-ants recipe 9\n");
    REQUIRE_FALSE(result.has_value());
    const std::string message(result.error().message());
    CHECK(message.find("version 9") != std::string::npos);
    CHECK(message.find("version " + std::to_string(urecipe::RECIPE_VERSION)) != std::string::npos);
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

namespace {

/// A recipe holding two lamps, the second with no yaw, so `write` must keep
/// file order and leave the default out.
Recipe withLamps() {
    Recipe recipe = sample();
    recipe.lamps = {
        AddedLamp{.name = "east-hall", .light = "Light23", .fitting = {"Brush95", "Brush99"},
                  .at = {-1100.5f, 900, 200}, .yaw = 16384},
        AddedLamp{.name = "a-crypt", .light = "Light7", .fitting = {"Brush12"}, .at = {0, -3.25f, 1e-3f}},
    };
    return recipe;
}

} // namespace

TEST_CASE("UTA-0256 INV-1: a recipe with lamps round-trips", "[urecipe][format][lamp]") {
    const Recipe recipe = withLamps();
    const std::string text = urecipe::write(recipe);
    CHECK(text.starts_with("ut-ants recipe 2\n"));
    CHECK(text.find("\n[lamp east-hall]\nlight = Light23\nfitting = Brush95 Brush99\nat = -1100.5 900 200\n"
                    "yaw = 16384\n")
          != std::string::npos);
    CHECK(text.find("\n[lamp a-crypt]\nlight = Light7\nfitting = Brush12\nat = 0 -3.25 0.001\n")
          != std::string::npos);
    CHECK(text.find("yaw = 0") == std::string::npos);
    CHECK(parsed(text) == recipe);
}

TEST_CASE("UTA-0256 INV-1: a recipe with no lamp is still written as version 1", "[urecipe][format][lamp]") {
    CHECK(urecipe::write(sample()).starts_with("ut-ants recipe 1\n"));
}

TEST_CASE("UTA-0256 INV-1: a hand-written lamp reads", "[urecipe][format][lamp]") {
    const Recipe recipe = parsed("ut-ants recipe 2\n[map]\nfile = dm-x\nsha256 = "
                                 "2f8a000000000000000000000000000000000000000000000000000000000ae1\n"
                                 "[lamp hall]\n  fitting =  Brush1\tBrush_2   # two\n"
                                 "at = 1 2.5 -3\nlight = Light1\n");
    REQUIRE(recipe.lamps.size() == 1);
    CHECK(recipe.lamps[0] == AddedLamp{.name = "hall", .light = "Light1", .fitting = {"Brush1", "Brush_2"},
                                       .at = {1, 2.5f, -3}, .yaw = 0});
}

TEST_CASE("UTA-0256 INV-1: every lamp refusal names its line", "[urecipe][format][lamp]") {
    struct Refusal {
        const char* what;
        std::string text;
        std::size_t line;
    };
    // Lines 1 to 4; a lamp section opens on line 5.
    const std::string head = "ut-ants recipe 2\n[map]\nfile = dm-x\n"
                             "sha256 = 2f8a000000000000000000000000000000000000000000000000000000000ae1\n";
    const std::string whole = "light = L1\nfitting = B1\nat = 0 0 0\n"; // lines 6 to 8
    std::string many = head;
    for (std::size_t i = 0; i <= urecipe::LAMP_LIMIT; ++i) many += "[lamp l" + std::to_string(i) + "]\n" + whole;
    const std::vector<Refusal> refusals = {
        {"a lamp in a version-1 recipe", "ut-ants recipe 1\n[map]\nfile = dm-x\n[lamp a]\n", 4},
        {"a lamp with no light", head + "[lamp a]\nfitting = B1\nat = 0 0 0\n", 5},
        {"a lamp with no fitting", head + "[lamp a]\nlight = L1\nat = 0 0 0\n", 5},
        {"a lamp with no place", head + "[lamp a]\nlight = L1\nfitting = B1\n[lamp b]\n" + whole, 5},
        {"an unknown lamp key", head + "[lamp a]\ncolour = red\n", 6},
        {"a repeated lamp", head + "[lamp a]\n" + whole + "[lamp a]\n", 9},
        {"a lamp name with a capital", head + "[lamp Hall]\n", 5},
        {"a lamp with no name", head + "[lamp]\n", 5},
        {"a lamp name too long", head + "[lamp " + std::string(33, 'a') + "]\n", 5},
        {"a yaw of a whole turn", head + "[lamp a]\nyaw = 65536\n", 6},
        {"a fitting brush given twice", head + "[lamp a]\nfitting = B1 B2 B1\n", 6},
        {"a fitting that is not a name", head + "[lamp a]\nfitting = B1 B-2\n", 6},
        {"a quoted fitting of no names", head + "[lamp a]\nfitting = \"  \"\n", 6},
        {"a place of two numbers", head + "[lamp a]\nat = 1 2\n", 6},
        {"a place that is not finite", head + "[lamp a]\nat = 1 inf 2\n", 6},
        {"a lamp with no map sha256", "ut-ants recipe 2\n[map]\nfile = dm-x\n[lamp a]\n" + whole, 4},
        {"a lamp past the limit", many, 5 + 4 * urecipe::LAMP_LIMIT},
    };
    for (const Refusal& refusal : refusals) {
        DYNAMIC_SECTION(refusal.what) {
            const auto result = urecipe::parse(refusal.text);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == ErrorCode::MalformedData);
            const std::string message(result.error().message());
            INFO("message: " << message);
            CHECK(message.starts_with("recipe line " + std::to_string(refusal.line) + ": "));
        }
    }
}

TEST_CASE("UTA-0256 INV-2: every lamp field moves the digest", "[urecipe][format][lamp]") {
    const Recipe base = withLamps();
    const auto digest = urecipe::bakeDigest(base);
    CHECK(digest != urecipe::bakeDigest(sample()));
    std::vector<Recipe> changed(7, base);
    changed[0].lamps[0].name = "west-hall";
    changed[1].lamps[0].light = "Light24";
    changed[2].lamps[0].fitting.push_back("Brush100");
    changed[3].lamps[0].at[2] = 201;
    changed[4].lamps[0].yaw = 16385;
    changed[5].lamps.pop_back();
    std::swap(changed[6].lamps[0], changed[6].lamps[1]);
    for (std::size_t i = 0; i < changed.size(); ++i) {
        INFO("change " << i);
        CHECK(urecipe::bakeDigest(changed[i]) != digest);
    }
}

// UTA-0338 INV-1 and INV-2: the recipe's sun -- docs/specs/UTA-0338-baked-sun.md SS 4.1.

namespace {

Recipe withSun() {
    Recipe recipe = withLamps();
    recipe.sun = urecipe::Sun{.yaw = 8192, .pitch = 9000, .hue = 28, .saturation = 210, .brightness = 180};
    return recipe;
}

const std::string SUN_HEAD = "ut-ants recipe 3\n[map]\nfile = dm-x\n"
                             "sha256 = 2f8a000000000000000000000000000000000000000000000000000000000ae1\n";

} // namespace

TEST_CASE("UTA-0338 INV-1: a recipe with a sun round-trips", "[urecipe][format][sun]") {
    const Recipe recipe = withSun();
    const std::string text = urecipe::write(recipe);
    CHECK(text.starts_with("ut-ants recipe 3\n"));
    CHECK(text.find("\n[sun]\nyaw = 8192\npitch = 9000\nhue = 28\nsaturation = 210\nbrightness = 180\n")
          != std::string::npos);
    CHECK(parsed(text) == recipe);
    Recipe lampless = withSun();
    lampless.lamps.clear();
    CHECK(urecipe::write(lampless).starts_with("ut-ants recipe 3\n"));
    CHECK(parsed(urecipe::write(lampless)) == lampless);
}

TEST_CASE("UTA-0338 INV-1: a hand-written sun reads", "[urecipe][format][sun]") {
    const Recipe recipe = parsed(SUN_HEAD + "[sun]\nbrightness = 1 # dim\nyaw = 65535\nsaturation = 0\n"
                                            "pitch = 16384\nhue = 255\n");
    REQUIRE(recipe.sun.has_value());
    CHECK(*recipe.sun == urecipe::Sun{.yaw = 65535, .pitch = 16384, .hue = 255, .saturation = 0, .brightness = 1});
    CHECK_FALSE(parsed(SUN_HEAD).sun.has_value());
}

TEST_CASE("UTA-0338 INV-1: every sun refusal names its line", "[urecipe][format][sun]") {
    struct Refusal {
        const char* what;
        std::string text;
        std::size_t line;
    };
    // Lines 1 to 4; the sun section opens on line 5.
    const std::string whole = "yaw = 1\npitch = 2\nhue = 3\nsaturation = 4\nbrightness = 5\n"; // lines 6 to 10
    const auto without = [&](std::string_view key) {
        std::string text = whole;
        const std::size_t at = text.find(std::string(key) + " =");
        text.erase(at, text.find('\n', at) + 1 - at);
        return SUN_HEAD + "[sun]\n" + text;
    };
    const std::vector<Refusal> refusals = {
        {"a sun in a version-2 recipe", "ut-ants recipe 2\n[map]\nfile = dm-x\n[sun]\n", 4},
        {"a sun with no yaw", without("yaw"), 5},
        {"a sun with no pitch", without("pitch"), 5},
        {"a sun with no hue", without("hue"), 5},
        {"a sun with no saturation", without("saturation"), 5},
        {"a sun with no brightness", without("brightness"), 5},
        {"an unclosed sun before a lamp", without("hue") + "[lamp a]\n", 5},
        {"an unknown sun key", SUN_HEAD + "[sun]\nradius = 3\n", 6},
        {"a second sun", SUN_HEAD + "[sun]\n" + whole + "[sun]\n", 11},
        {"a sun with a name", SUN_HEAD + "[sun noon]\n", 5},
        {"a yaw of a whole turn", SUN_HEAD + "[sun]\nyaw = 65536\n", 6},
        {"a pitch of 0", SUN_HEAD + "[sun]\npitch = 0\n", 6},
        {"a pitch past straight up", SUN_HEAD + "[sun]\npitch = 16385\n", 6},
        {"a negative pitch", SUN_HEAD + "[sun]\npitch = -5\n", 6},
        {"a hue of 256", SUN_HEAD + "[sun]\nhue = 256\n", 6},
        {"a saturation of 256", SUN_HEAD + "[sun]\nsaturation = 256\n", 6},
        {"a brightness of 0", SUN_HEAD + "[sun]\nbrightness = 0\n", 6},
        {"a brightness of 256", SUN_HEAD + "[sun]\nbrightness = 256\n", 6},
        {"a sun with no map sha256", "ut-ants recipe 3\n[map]\nfile = dm-x\n[sun]\n" + whole, 4},
    };
    for (const Refusal& refusal : refusals) {
        DYNAMIC_SECTION(refusal.what) {
            const auto result = urecipe::parse(refusal.text);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == ErrorCode::MalformedData);
            const std::string message(result.error().message());
            INFO("message: " << message);
            CHECK(message.starts_with("recipe line " + std::to_string(refusal.line) + ": "));
        }
    }
}

TEST_CASE("UTA-0338 INV-2: a sun-free digest is unchanged and every sun field moves it",
          "[urecipe][format][sun]") {
    // withLamps()' digest under the version-2 code, recorded before the sun existed.
    CHECK(urecipe::bakeDigest(withLamps())
          == digestFromHex("eb210e342bc2e34e66e974062226f0989e4202f8fd49d90d259229bb9ad822f7"));
    const Recipe base = withSun();
    const auto digest = urecipe::bakeDigest(base);
    CHECK(digest != urecipe::bakeDigest(withLamps()));
    std::vector<Recipe> changed(5, base);
    changed[0].sun->yaw += 1;
    changed[1].sun->pitch += 1;
    changed[2].sun->hue += 1;
    changed[3].sun->saturation += 1;
    changed[4].sun->brightness += 1;
    for (std::size_t i = 0; i < changed.size(); ++i) {
        INFO("change " << i);
        CHECK(urecipe::bakeDigest(changed[i]) != digest);
    }
}

// UTA-0256 INV-8 and INV-9: the map's own lights switched off, and lamps with
// no fitting -- docs/specs/UTA-0256-added-lamps.md SS 4.6 (UTA-0308).

namespace {

Recipe withSwitchOff() {
    Recipe recipe = withSun();
    recipe.switchedOff = {"Light9", "Light2", "Light_3"};
    return recipe;
}

const std::string V4_HEAD = "ut-ants recipe 4\n[map]\nfile = dm-x\n"
                            "sha256 = 2f8a000000000000000000000000000000000000000000000000000000000ae1\n";

} // namespace

TEST_CASE("UTA-0256 INV-8: a recipe that switches lights off round-trips as version 4",
          "[urecipe][format][switch-off]") {
    const Recipe recipe = withSwitchOff();
    const std::string text = urecipe::write(recipe);
    CHECK(text.starts_with("ut-ants recipe 4\n"));
    CHECK(text.find("\n[switch-off]\nlights = Light9 Light2 Light_3\n") != std::string::npos);
    CHECK(parsed(text) == recipe);

    Recipe bare = sample(); // nothing but materials and the switch-off
    bare.mapDigest = withSwitchOff().mapDigest;
    bare.switchedOff = {"Light1"};
    CHECK(urecipe::write(bare).starts_with("ut-ants recipe 4\n"));
    CHECK(parsed(urecipe::write(bare)) == bare);
}

TEST_CASE("UTA-0256 INV-8: a lamp with no fitting round-trips as version 4", "[urecipe][format][switch-off]") {
    Recipe recipe = withLamps();
    recipe.lamps[1].fitting.clear();
    const std::string text = urecipe::write(recipe);
    CHECK(text.starts_with("ut-ants recipe 4\n"));
    CHECK(text.find("\n[lamp a-crypt]\nlight = Light7\nat = 0 -3.25 0.001\n") != std::string::npos);
    CHECK(parsed(text) == recipe);
    CHECK(parsed(V4_HEAD + "[lamp hall]\nlight = Light1\nat = 1 2 3\n").lamps.at(0).fitting.empty());
}

TEST_CASE("UTA-0256 INV-8: every switch-off refusal names its line", "[urecipe][format][switch-off]") {
    struct Refusal {
        const char* what;
        std::string text;
        std::size_t line;
    };
    // Lines 1 to 4; a section opens on line 5.
    const std::string v3 = "ut-ants recipe 3\n[map]\nfile = dm-x\n"
                           "sha256 = 2f8a000000000000000000000000000000000000000000000000000000000ae1\n";
    const std::vector<Refusal> refusals = {
        {"a switch-off with no lights", V4_HEAD + "[switch-off]\n", 5},
        {"an unclosed switch-off before a lamp", V4_HEAD + "[switch-off]\n[lamp a]\n", 5},
        {"an unknown switch-off key", V4_HEAD + "[switch-off]\nlight = L1\n", 6},
        {"a light named twice", V4_HEAD + "[switch-off]\nlights = L1 L2 L1\n", 6},
        {"a light that is not a name", V4_HEAD + "[switch-off]\nlights = L-1\n", 6},
        {"a quoted list of no lights", V4_HEAD + "[switch-off]\nlights = \"\"\n", 6},
        {"a second switch-off", V4_HEAD + "[switch-off]\nlights = L1\n[switch-off]\n", 7},
        {"a switch-off with a name", V4_HEAD + "[switch-off hall]\n", 5},
        {"a switch-off with no map sha256", "ut-ants recipe 4\n[map]\nfile = dm-x\n[switch-off]\nlights = L1\n", 4},
        {"a switch-off in a version-3 recipe", v3 + "[switch-off]\nlights = L1\n", 5},
        {"a fitting-less lamp in a version-3 recipe", v3 + "[lamp a]\nlight = L1\nat = 0 0 0\n", 5},
    };
    for (const Refusal& refusal : refusals) {
        DYNAMIC_SECTION(refusal.what) {
            const auto result = urecipe::parse(refusal.text);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == ErrorCode::MalformedData);
            const std::string message(result.error().message());
            INFO("message: " << message);
            CHECK(message.starts_with("recipe line " + std::to_string(refusal.line) + ": "));
        }
    }
}

TEST_CASE("UTA-0256 INV-9: a version-3 digest is unchanged and every switch-off change moves it",
          "[urecipe][format][switch-off]") {
    // withSun()'s digest under the version-3 code, recorded before version 4 existed.
    CHECK(urecipe::bakeDigest(withSun())
          == digestFromHex("6983e94432d340e80d70d270a41aafbe8fee009528ca772182b9f81856147a1b"));
    const Recipe base = withSwitchOff();
    const auto digest = urecipe::bakeDigest(base);
    CHECK(digest != urecipe::bakeDigest(withSun()));
    std::vector<Recipe> changed(4, base);
    changed[0].switchedOff.push_back("Light10");
    changed[1].switchedOff.pop_back();
    std::swap(changed[2].switchedOff[0], changed[2].switchedOff[1]);
    changed[3].switchedOff[0] = "Light8";
    for (std::size_t i = 0; i < changed.size(); ++i) {
        INFO("change " << i);
        CHECK(urecipe::bakeDigest(changed[i]) != digest);
    }
    Recipe fittingless = withSun();
    fittingless.lamps[1].fitting.clear();
    CHECK(urecipe::bakeDigest(fittingless) != urecipe::bakeDigest(withSun()));
}

// UTA-0350 INV-1 and INV-2: a sky the map only paints on its walls, opened --
// docs/specs/UTA-0350-painted-skies.md SS 4.1.

namespace {

Recipe withSky() {
    Recipe recipe = withSwitchOff();
    recipe.sky = urecipe::Sky{.textures = {"mylevel.dayskyb", "skies.cloudy"},
                              .top = {0x10, 0x20, 0x30},
                              .horizon = {0xa0, 0xb0, 0xc0},
                              .clouds = 35};
    return recipe;
}

const std::string V5_HEAD = "ut-ants recipe 5\n[map]\nfile = dm-x\n"
                            "sha256 = 2f8a000000000000000000000000000000000000000000000000000000000ae1\n";

} // namespace

TEST_CASE("UTA-0350 INV-1: a recipe with a sky round-trips as version 5", "[urecipe][format][sky]") {
    const Recipe recipe = withSky();
    const std::string text = urecipe::write(recipe);
    CHECK(text.starts_with("ut-ants recipe 5\n"));
    CHECK(text.find("\n[sky]\ntextures = mylevel.dayskyb skies.cloudy\ntop = 102030\nhorizon = a0b0c0\n"
                    "clouds = 35\n")
          != std::string::npos);
    CHECK(parsed(text) == recipe);
}

TEST_CASE("UTA-0350 INV-1: a hand-written sky reads, with its defaults", "[urecipe][format][sky]") {
    const Recipe recipe = parsed(V5_HEAD + "[sky]\ntextures = Skies.Cloudy MyLevel.DayskyB # both\n");
    REQUIRE(recipe.sky.has_value());
    CHECK(recipe.sky->textures == std::vector<std::string>{"mylevel.dayskyb", "skies.cloudy"});
    CHECK(recipe.sky->top == std::array<std::uint8_t, 3>{0x3a, 0x78, 0xc8});
    CHECK(recipe.sky->horizon == std::array<std::uint8_t, 3>{0xb4, 0xd4, 0xee});
    CHECK(recipe.sky->clouds == 20);
    const Recipe set = parsed(V5_HEAD + "[sky]\nclouds = 100\nhorizon = FFfF00\ntop = 000000\ntextures = a.b\n");
    CHECK(*set.sky == urecipe::Sky{.textures = {"a.b"}, .top = {0, 0, 0}, .horizon = {0xff, 0xff, 0}, .clouds = 100});
    CHECK_FALSE(parsed(V5_HEAD).sky.has_value());
}

TEST_CASE("UTA-0350 INV-1: every sky refusal names its line", "[urecipe][format][sky]") {
    struct Refusal {
        const char* what;
        std::string text;
        std::size_t line;
    };
    // Lines 1 to 4; the sky section opens on line 5.
    const std::vector<Refusal> refusals = {
        {"a sky in a version-4 recipe", V4_HEAD + "[sky]\ntextures = a.b\n", 5},
        {"a sky with no textures", V5_HEAD + "[sky]\nclouds = 3\n", 5},
        {"an unclosed sky before a lamp", V5_HEAD + "[sky]\n[lamp a]\n", 5},
        {"an unknown sky key", V5_HEAD + "[sky]\ncolour = 102030\n", 6},
        {"a second sky", V5_HEAD + "[sky]\ntextures = a.b\n[sky]\n", 7},
        {"a sky with a name", V5_HEAD + "[sky day]\n", 5},
        {"a texture named twice", V5_HEAD + "[sky]\ntextures = a.b c.d A.B\n", 6},
        {"a texture with no package", V5_HEAD + "[sky]\ntextures = dayskyb\n", 6},
        {"a quoted list of no textures", V5_HEAD + "[sky]\ntextures = \"\"\n", 6},
        {"a texture opening with a quote", V5_HEAD + "[sky]\ntextures = \"\\\"a.b\"\n", 6},
        {"nine textures", V5_HEAD + "[sky]\ntextures = a.a a.b a.c a.d a.e a.f a.g a.h a.i\n", 6},
        {"a colour of five digits", V5_HEAD + "[sky]\ntop = 10203\n", 6},
        {"a colour of seven digits", V5_HEAD + "[sky]\nhorizon = 1020304\n", 6},
        {"a colour that is not hex", V5_HEAD + "[sky]\ntop = 10g030\n", 6},
        {"a colour with a sign", V5_HEAD + "[sky]\ntop = +10203\n", 6},
        {"clouds of 101", V5_HEAD + "[sky]\nclouds = 101\n", 6},
        {"a sky with no map sha256", "ut-ants recipe 5\n[map]\nfile = dm-x\n[sky]\ntextures = a.b\n", 4},
    };
    for (const Refusal& refusal : refusals) {
        DYNAMIC_SECTION(refusal.what) {
            const auto result = urecipe::parse(refusal.text);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == ErrorCode::MalformedData);
            const std::string message(result.error().message());
            INFO("message: " << message);
            CHECK(message.starts_with("recipe line " + std::to_string(refusal.line) + ": "));
        }
    }
}

TEST_CASE("UTA-0350 INV-2: a version-4 digest is unchanged and every sky field moves it", "[urecipe][format][sky]") {
    // withSwitchOff()'s digest under the version-4 code, recorded before version 5 existed.
    CHECK(urecipe::bakeDigest(withSwitchOff())
          == digestFromHex("f19b68bf1e291936dd3984999715aaad1cbff9861b28582da057cad11bf26387"));
    const Recipe base = withSky();
    const auto digest = urecipe::bakeDigest(base);
    CHECK(digest != urecipe::bakeDigest(withSwitchOff()));
    std::vector<Recipe> changed(6, base);
    changed[0].sky->textures.pop_back();
    changed[1].sky->textures[0] = "mylevel.dayskya";
    changed[2].sky->top[1] += 1;
    changed[3].sky->horizon[2] += 1;
    changed[4].sky->clouds += 1;
    changed[5].sky->textures.push_back("zz.sky");
    for (std::size_t i = 0; i < changed.size(); ++i) {
        INFO("change " << i);
        CHECK(urecipe::bakeDigest(changed[i]) != digest);
    }
}

// UTA-0256 INV-13: a lamp's mesh fitting -- docs/specs/UTA-0256-added-lamps.md
// SS 4.1, version 6.

namespace {

Recipe withMesh() {
    Recipe recipe = withSky();
    recipe.lamps[1].mesh = "Lantern5";
    return recipe;
}

const std::string V6_HEAD = "ut-ants recipe 6\n[map]\nfile = dm-x\n"
                            "sha256 = 2f8a000000000000000000000000000000000000000000000000000000000ae1\n";

} // namespace

TEST_CASE("UTA-0256 INV-13: a lamp with a mesh round-trips as version 6", "[urecipe][format][lamp]") {
    const Recipe recipe = withMesh();
    const std::string text = urecipe::write(recipe);
    CHECK(text.starts_with("ut-ants recipe 6\n"));
    CHECK(text.find("\n[lamp a-crypt]\nlight = Light7\nfitting = Brush12\nat = 0 -3.25 0.001\nmesh = Lantern5\n")
          != std::string::npos);
    CHECK(parsed(text) == recipe);
    CHECK(urecipe::write(withSky()).find("mesh") == std::string::npos);
    CHECK(parsed(V6_HEAD + "[lamp hall]\nlight = Light1\nat = 1 2 3\nmesh = Lantern_2\n").lamps.at(0).mesh
          == "Lantern_2");
}

TEST_CASE("UTA-0256 INV-13: every mesh refusal names its line", "[urecipe][format][lamp]") {
    struct Refusal {
        const char* what;
        std::string text;
        std::size_t line;
    };
    // Lines 1 to 4; the lamp opens on line 5 and its mesh is line 8.
    const std::string lamp = "[lamp hall]\nlight = Light1\nat = 1 2 3\n";
    const std::vector<Refusal> refusals = {
        {"a mesh in a version-5 recipe", V5_HEAD + lamp + "mesh = Lantern5\n", 8},
        {"a mesh that is not a name", V6_HEAD + lamp + "mesh = Lantern-5\n", 8},
        {"a mesh of two names", V6_HEAD + lamp + "mesh = Lantern5 Lantern4\n", 8},
        {"an empty mesh", V6_HEAD + lamp + "mesh = \"\"\n", 8},
        {"a mesh given twice", V6_HEAD + lamp + "mesh = Lantern5\nmesh = Lantern4\n", 9},
    };
    for (const Refusal& refusal : refusals) {
        DYNAMIC_SECTION(refusal.what) {
            const auto result = urecipe::parse(refusal.text);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == ErrorCode::MalformedData);
            const std::string message(result.error().message());
            INFO("message: " << message);
            CHECK(message.starts_with("recipe line " + std::to_string(refusal.line) + ": "));
        }
    }
}

TEST_CASE("UTA-0256 INV-13: a version-5 digest is unchanged and a mesh moves it", "[urecipe][format][lamp]") {
    // withSky()'s digest under the version-5 code, recorded before version 6 existed.
    CHECK(urecipe::bakeDigest(withSky())
          == digestFromHex("c5dfa5a1484ce62b754b34313efb0cf10a1152b5cbcdc6e068a8cfde0e6e42e2"));
    const Recipe base = withMesh();
    const auto digest = urecipe::bakeDigest(base);
    CHECK(digest != urecipe::bakeDigest(withSky()));
    std::vector<Recipe> changed(3, base);
    changed[0].lamps[1].mesh = "Lantern4";
    changed[1].lamps[0].mesh = "Lantern5"; // the same mesh on the other lamp
    changed[1].lamps[1].mesh.clear();
    changed[2].lamps[0].mesh = "Lantern9";
    for (std::size_t i = 0; i < changed.size(); ++i) {
        INFO("change " << i);
        CHECK(urecipe::bakeDigest(changed[i]) != digest);
    }
}

// UTA-0391: a painted sky's haze pushed further out, version 7 --
// docs/specs/UTA-0350-painted-skies.md SS 4.1.

namespace {

Recipe withHaze() {
    Recipe recipe = withMesh();
    recipe.sky->haze = 40;
    return recipe;
}

const std::string V7_HEAD = "ut-ants recipe 7\n[map]\nfile = dm-x\n"
                            "sha256 = 2f8a000000000000000000000000000000000000000000000000000000000ae1\n";

} // namespace

TEST_CASE("UTA-0391: a sky's haze round-trips as version 7", "[urecipe][format][sky]") {
    const Recipe recipe = withHaze();
    const std::string text = urecipe::write(recipe);
    CHECK(text.starts_with("ut-ants recipe 7\n"));
    CHECK(text.find("\nclouds = 35\nhaze = 40\n") != std::string::npos);
    CHECK(parsed(text) == recipe);
    // The default is written as nothing, so a sky without it stays version 5.
    CHECK(urecipe::write(withSky()).find("haze") == std::string::npos);
    CHECK(parsed(V7_HEAD + "[sky]\ntextures = a.b\n").sky->haze == 10);
    CHECK(parsed(V7_HEAD + "[sky]\ntextures = a.b\nhaze = 90\n").sky->haze == 90);
}

TEST_CASE("UTA-0391: every haze refusal names its line", "[urecipe][format][sky]") {
    struct Refusal {
        const char* what;
        std::string text;
        std::size_t line;
    };
    // Lines 1 to 4; the sky opens on line 5 and its haze is line 7.
    const std::string sky = "[sky]\ntextures = a.b\n";
    const std::vector<Refusal> refusals = {
        {"a haze in a version-6 recipe", V6_HEAD + sky + "haze = 40\n", 7},
        {"a haze of 9", V7_HEAD + sky + "haze = 9\n", 7},
        {"a haze of 91", V7_HEAD + sky + "haze = 91\n", 7},
        {"a haze that is not a number", V7_HEAD + sky + "haze = far\n", 7},
    };
    for (const Refusal& refusal : refusals) {
        DYNAMIC_SECTION(refusal.what) {
            const auto result = urecipe::parse(refusal.text);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == ErrorCode::MalformedData);
            const std::string message(result.error().message());
            INFO("message: " << message);
            CHECK(message.starts_with("recipe line " + std::to_string(refusal.line) + ": "));
        }
    }
}

TEST_CASE("UTA-0391: a version-6 digest is unchanged and a haze moves it", "[urecipe][format][sky]") {
    // withMesh()'s digest under the version-6 code, recorded before version 7 existed.
    const auto before = urecipe::bakeDigest(withMesh());
    CHECK(before == digestFromHex("fe479bdf8931e82d9d6eb0c38513ac475f0a02aa9180ee2a768c6447fb4458ea"));
    Recipe stated = withMesh();
    stated.sky->haze = 10; // the default, said aloud
    CHECK(urecipe::bakeDigest(stated) == before);
    CHECK(urecipe::bakeDigest(withHaze()) != before);
    Recipe further = withHaze();
    further.sky->haze = 41;
    CHECK(urecipe::bakeDigest(further) != urecipe::bakeDigest(withHaze()));
}

// UTA-0396: a brush hidden and its opening covered, version 8 --
// docs/specs/UTA-0109-map-geometry.md SS 4.6.

namespace {

Recipe withHide() {
    Recipe recipe = withHaze();
    recipe.hidden.push_back(
        uta::urecipe::HiddenBrush{.name = "pit", .brush = "Brush12", .coverBrush = "Brush2", .coverPolygon = 5});
    recipe.hidden.push_back(
        uta::urecipe::HiddenBrush{.name = "ledge", .brush = "Brush40", .coverBrush = "Brush3", .coverPolygon = 0});
    return recipe;
}

const std::string V8_HEAD = "ut-ants recipe 8\n[map]\nfile = dm-x\n"
                            "sha256 = 2f8a000000000000000000000000000000000000000000000000000000000ae1\n";

} // namespace

TEST_CASE("UTA-0396: hidden brushes round-trip as version 8, in file order", "[urecipe][format][hide]") {
    const Recipe recipe = withHide();
    const std::string text = urecipe::write(recipe);
    CHECK(text.starts_with("ut-ants recipe 8\n"));
    CHECK(text.ends_with("\n[hide pit]\nbrush = Brush12\ncover = Brush2 5\n"
                         "\n[hide ledge]\nbrush = Brush40\ncover = Brush3 0\n"));
    CHECK(parsed(text) == recipe);
    // No [hide] stays below version 8.
    CHECK(urecipe::write(withHaze()).starts_with("ut-ants recipe 7\n"));
    const Recipe read = parsed(V8_HEAD + "[hide a]\ncover = Brush2   7\nbrush = Brush9\n");
    REQUIRE(read.hidden.size() == 1);
    CHECK(read.hidden[0] == uta::urecipe::HiddenBrush{.name = "a", .brush = "Brush9", .coverBrush = "Brush2",
                                                      .coverPolygon = 7});
}

TEST_CASE("UTA-0396: every [hide] refusal names its line", "[urecipe][format][hide]") {
    struct Refusal {
        const char* what;
        std::string text;
        std::size_t line;
    };
    // Lines 1 to 4; a [hide] opens on line 5.
    const std::string noDigest = "ut-ants recipe 8\n[map]\nfile = dm-x\n\n";
    std::string full = V8_HEAD;
    for (std::size_t i = 0; i <= uta::urecipe::HIDE_LIMIT; ++i)
        full += std::format("[hide h{}]\nbrush = B\ncover = C 0\n", i);
    const std::vector<Refusal> refusals = {
        {"a [hide] in a version-7 recipe", V7_HEAD + "[hide a]\nbrush = B\ncover = C 0\n", 5},
        {"a [hide] without the map's sha256", noDigest + "[hide a]\nbrush = B\ncover = C 0\n", 5},
        {"a [hide] with no brush", V8_HEAD + "[hide a]\ncover = C 0\n", 5},
        {"a [hide] with no cover", V8_HEAD + "[hide a]\nbrush = B\n", 5},
        {"a [hide] with no name", V8_HEAD + "[hide]\nbrush = B\ncover = C 0\n", 5},
        {"a [hide] given twice", V8_HEAD + "[hide a]\nbrush = B\ncover = C 0\n[hide a]\n", 8},
        {"a brush that is not a name", V8_HEAD + "[hide a]\nbrush = B.x\n", 6},
        {"a cover with no polygon", V8_HEAD + "[hide a]\ncover = C\n", 6},
        {"a cover with a word for its polygon", V8_HEAD + "[hide a]\ncover = C five\n", 6},
        {"a cover with three words", V8_HEAD + "[hide a]\ncover = C 0 1\n", 6},
        {"a key [hide] lacks", V8_HEAD + "[hide a]\nfitting = B\n", 6},
        {"more [hide] sections than the limit", full, 5 + 3 * uta::urecipe::HIDE_LIMIT},
    };
    for (const Refusal& refusal : refusals) {
        DYNAMIC_SECTION(refusal.what) {
            const auto result = urecipe::parse(refusal.text);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == ErrorCode::MalformedData);
            const std::string message(result.error().message());
            INFO("message: " << message);
            CHECK(message.starts_with("recipe line " + std::to_string(refusal.line) + ": "));
        }
    }
}

TEST_CASE("UTA-0406: a bad section name is refused as that section's name", "[urecipe][format][hide]") {
    // [hide] takes a lamp's naming rule (UTA-0109 SS 4.6), but the player
    // wrote a [hide], so the refusal must not call it a lamp.
    const auto messageFor = [](const std::string& text) {
        const auto result = urecipe::parse(text);
        REQUIRE_FALSE(result.has_value());
        return std::string(result.error().message());
    };
    const std::string hide = messageFor(V8_HEAD + "[hide Pit]\nbrush = B\ncover = C 0\n");
    INFO("hide: " << hide);
    CHECK(hide.find("'Pit' is not a [hide] name") != std::string::npos);
    CHECK(hide.find("lamp") == std::string::npos);
    const std::string lamp = messageFor(V8_HEAD + "[lamp Hall]\n");
    INFO("lamp: " << lamp);
    CHECK(lamp.find("'Hall' is not a [lamp] name") != std::string::npos);
}

TEST_CASE("UTA-0396: a version-7 digest is unchanged and each hidden field moves it", "[urecipe][format][hide]") {
    // This text's digest under the version-7 code, recorded before version 8 existed.
    const std::string v7 = V7_HEAD + "[sky]\ntextures = a.b\nhaze = 40\n";
    CHECK(urecipe::bakeDigest(parsed(v7))
          == digestFromHex("29731a559e86a66f11a1fb89ad9ac01104ac8bcceabb58a02642e1d2a2fff89e"));
    const auto digest = urecipe::bakeDigest(withHide());
    CHECK(digest != urecipe::bakeDigest(withHaze()));
    std::vector<Recipe> changed(5, withHide());
    changed[0].hidden[0].name = "pot";
    changed[1].hidden[0].brush = "Brush13";
    changed[2].hidden[0].coverBrush = "Brush1";
    changed[3].hidden[0].coverPolygon = 6;
    std::swap(changed[4].hidden[0], changed[4].hidden[1]);
    for (std::size_t i = 0; i < changed.size(); ++i) {
        INFO("change " << i);
        CHECK(urecipe::bakeDigest(changed[i]) != digest);
    }
}

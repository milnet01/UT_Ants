// The recipe format -- docs/specs/UTA-0113-recipe-format.md SS 4.2 and SS 4.4,
// its lamps, docs/specs/UTA-0256-added-lamps.md SS 4.1, its sun,
// docs/specs/UTA-0338-baked-sun.md SS 4.1, and the map's lights it switches
// off, UTA-0256 SS 4.6 (UTA-0308), and its painted sky opened,
// docs/specs/UTA-0350-painted-skies.md SS 4.1, with its haze (UTA-0391), and
// its hidden brushes, docs/specs/UTA-0109-map-geometry.md SS 4.6 (UTA-0396).

#include "urecipe/Recipe.h"

#include "core/Ascii.h"
#include "core/Sha256.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <span>
#include <utility>

namespace uta::urecipe {
namespace {

constexpr std::string_view HEADER = "ut-ants recipe ";
constexpr std::string_view BOM = "\xEF\xBB\xBF";

/// The same fold umat::materialId lowers a texture's id with, so a recipe's
/// texture names compare equal to the ids the bake makes.
using uta::asciiFolded;

bool isBlank(char character) { return character == ' ' || character == '\t'; }

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && isBlank(text.front())) text.remove_prefix(1);
    while (!text.empty() && isBlank(text.back())) text.remove_suffix(1);
    return text;
}

/// What is left after a value or a heading: nothing, or a comment.
bool onlyComment(std::string_view rest) {
    rest = trimmed(rest);
    return rest.empty() || rest.front() == '#';
}

std::unexpected<Error> refuse(std::size_t line, std::string_view why) {
    return fail(ErrorCode::MalformedData, std::format("recipe line {}: {}", line, why));
}

/// A key's value: quoted, with `\"` and `\\` its only escapes, or bare up to
/// a comment.
Result<std::string> valueOf(std::string_view rest, std::size_t line) {
    rest = trimmed(rest);
    if (rest.empty() || rest.front() != '"') {
        const std::string_view bare = trimmed(rest.substr(0, rest.find('#')));
        if (bare.empty()) return refuse(line, "the key has no value");
        return std::string(bare);
    }
    std::string value;
    for (std::size_t at = 1; at < rest.size(); ++at) {
        const char character = rest[at];
        if (character == '"') {
            if (!onlyComment(rest.substr(at + 1))) return refuse(line, "text after the closing quote");
            return value;
        }
        if (character == '\\') {
            if (at + 1 >= rest.size() || (rest[at + 1] != '"' && rest[at + 1] != '\\'))
                return refuse(line, "a quoted value's only escapes are \\\" and \\\\");
            ++at;
        }
        value += rest[at];
    }
    return refuse(line, "an unclosed quote");
}

Result<bool> boolOf(std::string_view value, std::size_t line) {
    if (value == "true") return true;
    if (value == "false") return false;
    return refuse(line, std::format("'{}' is not true or false", value));
}

/// A decimal in [0, max], digits only.
Result<std::uint32_t> numberOf(std::string_view value, std::uint32_t max, std::size_t line) {
    std::uint32_t number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc{} || end != value.data() + value.size() || number > max)
        return refuse(line, std::format("'{}' is not a whole number from 0 to {}", value, max));
    return number;
}

Result<std::array<std::byte, 32>> digestOf(std::string_view value, std::size_t line) {
    const auto nibble = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    std::array<std::byte, 32> digest{};
    if (value.size() != digest.size() * 2) return refuse(line, "sha256 is not 64 hex digits");
    for (std::size_t i = 0; i < digest.size(); ++i) {
        const int high = nibble(value[2 * i]);
        const int low = nibble(value[2 * i + 1]);
        if (high < 0 || low < 0) return refuse(line, "sha256 is not 64 hex digits");
        digest[i] = static_cast<std::byte>(high * 16 + low);
    }
    return digest;
}

/// The text of a `[...]` heading, or a refusal.
Result<std::string_view> headingOf(std::string_view text, std::size_t line) {
    const std::size_t close = text.find(']');
    if (close == std::string_view::npos) return refuse(line, "a [section] heading with no ]");
    if (!onlyComment(text.substr(close + 1))) return refuse(line, "text after a [section] heading");
    return trimmed(text.substr(1, close - 1));
}

Result<std::string> textureOf(std::string_view name, std::size_t line) {
    if (name.empty()) return refuse(line, "[material] names no texture");
    if (std::ranges::any_of(name, isBlank)) return refuse(line, "a texture name holds no spaces");
    // write never quotes a list word, so one opening with `"` would not read back.
    if (name.find('"') != std::string_view::npos) return refuse(line, "a texture name holds no quotes");
    if (name.find('#') != std::string_view::npos)
        return refuse(line, "name the texture without #masked: both of its variants take the assignment");
    if (name.find('.') == std::string_view::npos)
        return refuse(line, "name the texture as <package>.<texture>");
    return asciiFolded(name);
}

/// The name in a `[lamp NAME]` or `[hide NAME]` heading: 1 to 32 of a-z, 0-9
/// and -, the one rule both sections share (UTA-0109 SS 4.6). `section` is the
/// heading's keyword, so a refusal names the section the player wrote.
Result<std::string> sectionNameOf(std::string_view section, std::string_view name, std::size_t line) {
    const bool good = !name.empty() && name.size() <= 32 && std::ranges::all_of(name, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-';
    });
    if (!good)
        return refuse(line, std::format("'{}' is not a [{}] name: 1 to 32 of a-z, 0-9 and -", name, section));
    return std::string(name);
}

/// An actor's name as the map spells it: letters, digits and _.
bool isObjectName(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
               || (character >= '0' && character <= '9') || character == '_';
    });
}

/// The blank-separated words of a value.
std::vector<std::string_view> wordsOf(std::string_view value) {
    std::vector<std::string_view> words;
    while (!(value = trimmed(value)).empty()) {
        const std::size_t end = std::min(value.find(' '), value.find('\t'));
        words.push_back(value.substr(0, end));
        value.remove_prefix(end == std::string_view::npos ? value.size() : end);
    }
    return words;
}

/// Three finite numbers. from_chars, not stod: no locale reaches it.
Result<std::array<float, 3>> pointOf(std::string_view value, std::size_t line) {
    const std::vector<std::string_view> words = wordsOf(value);
    std::array<float, 3> point{};
    bool good = words.size() == point.size();
    for (std::size_t i = 0; good && i < point.size(); ++i) {
        const auto [end, error] = std::from_chars(words[i].data(), words[i].data() + words[i].size(), point[i]);
        good = error == std::errc{} && end == words[i].data() + words[i].size() && std::isfinite(point[i]);
    }
    if (!good) return refuse(line, std::format("'{}' is not three finite numbers", value));
    return point;
}

Result<void> setLampKey(AddedLamp& lamp, std::string_view key, const std::string& value, std::uint32_t version,
                        std::size_t line) {
    if (key == "light") {
        if (!isObjectName(value)) return refuse(line, std::format("'{}' is not an actor's name", value));
        lamp.light = value;
    } else if (key == "fitting") {
        if (wordsOf(value).empty()) return refuse(line, "fitting names no brush: leave the key out instead");
        for (const std::string_view word : wordsOf(value)) {
            if (!isObjectName(word)) return refuse(line, std::format("'{}' is not an actor's name", word));
            if (std::ranges::find(lamp.fitting, word) != lamp.fitting.end())
                return refuse(line, std::format("the fitting names {} twice", word));
            lamp.fitting.emplace_back(word);
        }
    } else if (key == "at") {
        UTA_TRY(lamp.at, pointOf(value, line));
    } else if (key == "yaw") {
        UTA_TRY(lamp.yaw, numberOf(value, 65535, line));
    } else if (key == "mesh") {
        if (version < 6) return refuse(line, "a lamp's `mesh` needs `ut-ants recipe 6`");
        if (!isObjectName(value)) return refuse(line, std::format("'{}' is not an actor's name", value));
        lamp.mesh = value;
    } else {
        return refuse(line, std::format("[lamp] has no key '{}'", key));
    }
    return {};
}

Result<void> setSunKey(Sun& sun, std::string_view key, const std::string& value, std::size_t line) {
    if (key == "yaw") {
        UTA_TRY(sun.yaw, numberOf(value, 65535, line));
    } else if (key == "pitch") {
        UTA_TRY(sun.pitch, numberOf(value, 16384, line));
        if (sun.pitch == 0) return refuse(line, "pitch is from 1 to 16384: a sun stands above the horizon");
    } else if (key == "hue" || key == "saturation") {
        UTA_TRY(const std::uint32_t number, numberOf(value, 255, line));
        (key == "hue" ? sun.hue : sun.saturation) = static_cast<std::uint8_t>(number);
    } else if (key == "brightness") {
        UTA_TRY(const std::uint32_t number, numberOf(value, 255, line));
        if (number == 0) return refuse(line, "brightness is from 1 to 255");
        sun.brightness = static_cast<std::uint8_t>(number);
    } else {
        return refuse(line, std::format("[sun] has no key '{}'", key));
    }
    return {};
}

/// UTA-0256 SS 4.6: `lights`, one or more actor names, none twice.
Result<void> setSwitchOffKey(std::vector<std::string>& lights, std::string_view key, const std::string& value,
                             std::size_t line) {
    if (key != "lights") return refuse(line, std::format("[switch-off] has no key '{}'", key));
    if (wordsOf(value).empty()) return refuse(line, "lights names no light");
    for (const std::string_view word : wordsOf(value)) {
        if (!isObjectName(word)) return refuse(line, std::format("'{}' is not an actor's name", word));
        if (std::ranges::find(lights, word) != lights.end())
            return refuse(line, std::format("[switch-off] names {} twice", word));
        lights.emplace_back(word);
    }
    return {};
}

/// UTA-0350 SS 4.1: six hex digits, an sRGB colour.
Result<std::array<std::uint8_t, 3>> colourOf(std::string_view value, std::size_t line) {
    std::array<std::uint8_t, 3> colour{};
    bool good = value.size() == 2 * colour.size();
    for (std::size_t i = 0; good && i < colour.size(); ++i) {
        const auto [end, error] = std::from_chars(value.data() + 2 * i, value.data() + 2 * i + 2, colour[i], 16);
        good = error == std::errc{} && end == value.data() + 2 * i + 2;
    }
    if (!good) return refuse(line, std::format("'{}' is not a colour: six hex digits, rrggbb", value));
    return colour;
}

/// UTA-0350 SS 4.1: `textures`, 1 to 8, none twice; `top`, `horizon`, `clouds`;
/// UTA-0391: `haze`, from version 7.
Result<void> setSkyKey(Sky& sky, std::string_view key, const std::string& value, std::uint32_t version,
                       std::size_t line) {
    if (key == "textures") {
        if (wordsOf(value).empty()) return refuse(line, "textures names no texture");
        for (const std::string_view word : wordsOf(value)) {
            UTA_TRY(std::string texture, textureOf(word, line));
            if (std::ranges::find(sky.textures, texture) != sky.textures.end())
                return refuse(line, std::format("[sky] names {} twice", texture));
            if (sky.textures.size() == SKY_TEXTURE_LIMIT)
                return refuse(line, std::format("[sky] names at most {} textures", SKY_TEXTURE_LIMIT));
            sky.textures.push_back(std::move(texture));
        }
        std::ranges::sort(sky.textures);
    } else if (key == "top") {
        UTA_TRY(sky.top, colourOf(value, line));
    } else if (key == "horizon") {
        UTA_TRY(sky.horizon, colourOf(value, line));
    } else if (key == "clouds") {
        UTA_TRY(const std::uint32_t number, numberOf(value, 100, line));
        sky.clouds = static_cast<std::uint8_t>(number);
    } else if (key == "haze") {
        if (version < 7) return refuse(line, "a sky's `haze` needs `ut-ants recipe 7`");
        UTA_TRY(const std::uint32_t number, numberOf(value, SKY_HAZE_MAX, line));
        if (number < SKY_HAZE_MIN)
            return refuse(line, std::format("haze {} is below {}", number, SKY_HAZE_MIN));
        sky.haze = static_cast<std::uint8_t>(number);
    } else {
        return refuse(line, std::format("[sky] has no key '{}'", key));
    }
    return {};
}

/// UTA-0109 SS 4.6: `brush`, an actor's name; `cover`, an actor's name and
/// a polygon number.
Result<void> setHideKey(HiddenBrush& hide, std::string_view key, const std::string& value, std::size_t line) {
    if (key == "brush") {
        if (!isObjectName(value)) return refuse(line, std::format("'{}' is not an actor's name", value));
        hide.brush = value;
    } else if (key == "cover") {
        const std::vector<std::string_view> words = wordsOf(value);
        if (words.size() != 2 || !isObjectName(words[0]))
            return refuse(line, std::format("'{}' is not a brush's name and a polygon number", value));
        UTA_TRY(hide.coverPolygon, numberOf(words[1], UINT32_MAX, line));
        hide.coverBrush = words[0];
    } else {
        return refuse(line, std::format("[hide] has no key '{}'", key));
    }
    return {};
}

/// The lowest version that holds `recipe`: 8 with a hidden brush (UTA-0109
/// SS 4.6), 7 with a sky's haze other than the
/// default (UTA-0391), 6 with a lamp's mesh (UTA-0256 SS 4.1), 5 with a sky
/// (UTA-0350 SS 4.1),
/// 4 with a switched-off light or a lamp with no fitting (UTA-0256 SS 4.6),
/// 3 with a sun, 2 with a lamp, else 1.
std::uint32_t versionOf(const Recipe& recipe) {
    if (!recipe.hidden.empty()) return 8;
    if (recipe.sky && recipe.sky->haze != SKY_HAZE_DEFAULT) return 7;
    if (std::ranges::any_of(recipe.lamps, [](const AddedLamp& lamp) { return !lamp.mesh.empty(); })) return 6;
    if (recipe.sky) return 5;
    if (!recipe.switchedOff.empty()
        || std::ranges::any_of(recipe.lamps, [](const AddedLamp& lamp) { return lamp.fitting.empty(); }))
        return 4;
    if (recipe.sun) return 3;
    return recipe.lamps.empty() ? 1 : 2;
}

/// Every `key = value` line of one section is handed here.
Result<void> setMapKey(Recipe& recipe, std::string_view key, const std::string& value, std::size_t line) {
    if (key == "file") {
        if (value.empty()) return refuse(line, "file names no map");
        recipe.map = asciiFolded(value);
    } else if (key == "sha256") {
        UTA_TRY(recipe.mapDigest, digestOf(value, line));
    } else if (key == "name") {
        recipe.friendlyName = value;
    } else {
        return refuse(line, std::format("[map] has no key '{}'", key));
    }
    return {};
}

Result<void> setMaterialKey(MaterialAssignment& material, std::string_view key, const std::string& value,
                            std::size_t line) {
    if (key == "metallic") {
        UTA_TRY(material.metallic, boolOf(value, line));
    } else if (key == "emissive") {
        UTA_TRY(material.emissive, boolOf(value, line));
    } else if (key == "base-roughness" || key == "emissive-threshold" || key == "parallax-depth") {
        UTA_TRY(const std::uint32_t number, numberOf(value, 255, line));
        auto& field = key == "base-roughness"       ? material.baseRoughness
                      : key == "emissive-threshold" ? material.emissiveThreshold
                                                    : material.parallaxDepth;
        field = static_cast<std::uint8_t>(number);
    } else if (key == "upscale") {
        UTA_TRY(const std::uint32_t number, numberOf(value, 4, line));
        if (number != 1 && number != 2 && number != 4) return refuse(line, "upscale is 1, 2 or 4");
        material.upscale = number;
    } else {
        return refuse(line, std::format("[material] has no key '{}'", key));
    }
    return {};
}

/// `value` as `write` emits it: bare where it reads back the same, else quoted.
std::string quotedIfNeeded(std::string_view value, bool always) {
    const bool needs = always || value.empty() || isBlank(value.front()) || isBlank(value.back())
                       || value.front() == '"' || value.find('#') != std::string_view::npos;
    if (!needs) return std::string(value);
    std::string out = "\"";
    for (const char character : value) {
        if (character == '"' || character == '\\') out += '\\';
        out += character;
    }
    return out + '"';
}

std::vector<MaterialAssignment> sortedMaterials(const Recipe& recipe) {
    std::vector<MaterialAssignment> sorted = recipe.materials;
    std::ranges::sort(sorted, {}, &MaterialAssignment::texture);
    return sorted;
}

} // namespace

Result<Recipe> parse(std::string_view text) {
    if (text.starts_with(BOM)) text.remove_prefix(BOM.size()); // what Windows editors write

    enum class In { Header, Nothing, Map, Material, Lamp, Sun, SwitchOff, Sky, Hide };
    In in = In::Header;
    Recipe recipe;
    std::size_t headerLine = 1;
    std::optional<std::size_t> mapLine;
    bool fileSeen = false;
    std::map<std::string, MaterialAssignment> materials;
    MaterialAssignment* material = nullptr;
    std::set<std::string, std::less<>> keysSeen; // in the current section
    std::uint32_t version = 0;
    std::vector<std::size_t> lampLines;
    std::optional<std::size_t> sunLine;
    std::optional<std::size_t> switchOffLine;
    std::optional<std::size_t> skyLine;
    std::vector<std::size_t> hideLines;
    // A lamp's required keys -- `fitting` only below version 4 (UTA-0256
    // SS 4.6) -- a sun's five and [switch-off]'s one, checked when the
    // section closes.
    const auto sectionComplete = [&]() -> Result<void> {
        if (in == In::Lamp) {
            for (const std::string_view key : {"light", "fitting", "at"})
                if (!keysSeen.contains(key) && (key != "fitting" || version < 4))
                    return refuse(lampLines.back(),
                                  std::format("[lamp {}] has no `{}`", recipe.lamps.back().name, key));
        } else if (in == In::Sun) {
            for (const std::string_view key : {"yaw", "pitch", "hue", "saturation", "brightness"})
                if (!keysSeen.contains(key)) return refuse(*sunLine, std::format("[sun] has no `{}`", key));
        } else if (in == In::SwitchOff && !keysSeen.contains("lights")) {
            return refuse(*switchOffLine, "[switch-off] has no `lights`");
        } else if (in == In::Sky && !keysSeen.contains("textures")) {
            return refuse(*skyLine, "[sky] has no `textures`");
        } else if (in == In::Hide) {
            for (const std::string_view key : {"brush", "cover"})
                if (!keysSeen.contains(key))
                    return refuse(hideLines.back(),
                                  std::format("[hide {}] has no `{}`", recipe.hidden.back().name, key));
        }
        return {};
    };

    std::size_t line = 0;
    while (!text.empty() || line == 0) {
        ++line;
        const std::size_t end = text.find('\n');
        std::string_view raw = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (raw.ends_with('\r')) raw.remove_suffix(1);
        const std::string_view content = trimmed(raw);
        if (content.empty() || content.front() == '#') {
            if (text.empty()) break;
            continue;
        }

        if (in == In::Header) {
            const std::string_view header = trimmed(content.substr(0, content.find('#')));
            if (!header.starts_with(HEADER))
                return refuse(line, std::format("the first line must be `{}{}`", HEADER, RECIPE_VERSION));
            const std::string_view digits = header.substr(HEADER.size());
            const auto [last, error] = std::from_chars(digits.data(), digits.data() + digits.size(), version);
            if (error != std::errc{} || last != digits.data() + digits.size() || version == 0)
                return refuse(line, std::format("'{}' is not a recipe version", digits));
            if (version > RECIPE_VERSION)
                return fail(ErrorCode::UnsupportedVersion,
                            std::format("recipe line {}: the recipe is version {}, and this game reads up to "
                                        "version {}",
                                        line, version, RECIPE_VERSION));
            headerLine = line;
            in = In::Nothing;
        } else if (content.front() == '[') {
            UTA_TRY(const std::string_view heading, headingOf(content, line));
            UTA_CHECK(sectionComplete());
            keysSeen.clear();
            if (heading == "map") {
                if (mapLine) return refuse(line, std::format("a second [map]; the first is on line {}", *mapLine));
                mapLine = line;
                in = In::Map;
            } else if (heading == "material" || (heading.starts_with("material") && isBlank(heading[8]))) {
                UTA_TRY(std::string texture, textureOf(trimmed(heading.substr(8)), line));
                if (materials.contains(texture))
                    return refuse(line, std::format("[material {}] is given twice", texture));
                material = &materials[texture];
                material->texture = std::move(texture);
                in = In::Material;
            } else if (heading == "lamp" || (heading.starts_with("lamp") && isBlank(heading[4]))) {
                if (version < 2) return refuse(line, "a [lamp] needs `ut-ants recipe 2`");
                UTA_TRY(std::string name, sectionNameOf("lamp", trimmed(heading.substr(4)), line));
                for (std::size_t i = 0; i < recipe.lamps.size(); ++i)
                    if (recipe.lamps[i].name == name)
                        return refuse(line, std::format("[lamp {}] is given twice; the first is on line {}", name,
                                                        lampLines[i]));
                if (recipe.lamps.size() == LAMP_LIMIT)
                    return refuse(line, std::format("a recipe holds at most {} lamps", LAMP_LIMIT));
                recipe.lamps.push_back(AddedLamp{.name = std::move(name)});
                lampLines.push_back(line);
                in = In::Lamp;
            } else if (heading == "sun") {
                if (version < 3) return refuse(line, "a [sun] needs `ut-ants recipe 3`");
                if (sunLine) return refuse(line, std::format("a second [sun]; the first is on line {}", *sunLine));
                sunLine = line;
                recipe.sun.emplace();
                in = In::Sun;
            } else if (heading == "switch-off") {
                if (version < 4) return refuse(line, "a [switch-off] needs `ut-ants recipe 4`");
                if (switchOffLine)
                    return refuse(line, std::format("a second [switch-off]; the first is on line {}", *switchOffLine));
                switchOffLine = line;
                in = In::SwitchOff;
            } else if (heading == "sky") {
                if (version < 5) return refuse(line, "a [sky] needs `ut-ants recipe 5`");
                if (skyLine) return refuse(line, std::format("a second [sky]; the first is on line {}", *skyLine));
                skyLine = line;
                recipe.sky.emplace();
                in = In::Sky;
            } else if (heading == "hide" || (heading.starts_with("hide") && isBlank(heading[4]))) {
                if (version < 8) return refuse(line, "a [hide] needs `ut-ants recipe 8`");
                UTA_TRY(std::string name, sectionNameOf("hide", trimmed(heading.substr(4)), line));
                for (std::size_t i = 0; i < recipe.hidden.size(); ++i)
                    if (recipe.hidden[i].name == name)
                        return refuse(line, std::format("[hide {}] is given twice; the first is on line {}", name,
                                                        hideLines[i]));
                if (recipe.hidden.size() == HIDE_LIMIT)
                    return refuse(line, std::format("a recipe holds at most {} [hide] sections", HIDE_LIMIT));
                recipe.hidden.push_back(HiddenBrush{.name = std::move(name)});
                hideLines.push_back(line);
                in = In::Hide;
            } else {
                return refuse(line, std::format("there is no [{}] section", heading));
            }
        } else {
            const std::size_t equals = content.find('=');
            if (equals == std::string_view::npos) return refuse(line, "expected `key = value`");
            const std::string_view key = trimmed(content.substr(0, equals));
            if (in == In::Nothing) return refuse(line, "a key before any [section]");
            if (!keysSeen.insert(std::string(key)).second)
                return refuse(line, std::format("'{}' is given twice in one section", key));
            UTA_TRY(const std::string value, valueOf(content.substr(equals + 1), line));
            if (in == In::Map) {
                UTA_CHECK(setMapKey(recipe, key, value, line));
                if (key == "file") fileSeen = true;
            } else if (in == In::Lamp) {
                UTA_CHECK(setLampKey(recipe.lamps.back(), key, value, version, line));
            } else if (in == In::Sun) {
                UTA_CHECK(setSunKey(*recipe.sun, key, value, line));
            } else if (in == In::SwitchOff) {
                UTA_CHECK(setSwitchOffKey(recipe.switchedOff, key, value, line));
            } else if (in == In::Sky) {
                UTA_CHECK(setSkyKey(*recipe.sky, key, value, version, line));
            } else if (in == In::Hide) {
                UTA_CHECK(setHideKey(recipe.hidden.back(), key, value, line));
            } else {
                UTA_CHECK(setMaterialKey(*material, key, value, line));
            }
        }
        if (text.empty()) break;
    }

    if (in == In::Header) return refuse(1, std::format("the recipe is empty; its first line must be `{}{}`",
                                                       HEADER, RECIPE_VERSION));
    if (!mapLine) return refuse(headerLine, "the recipe has no [map] section");
    if (!fileSeen) return refuse(*mapLine, "[map] has no `file`");
    UTA_CHECK(sectionComplete());
    if (!recipe.lamps.empty() && !recipe.mapDigest)
        return refuse(lampLines.front(), "a recipe with a [lamp] must give the map's sha256 in [map]: actor names "
                                         "and places mean nothing in another file");
    if (sunLine && !recipe.mapDigest)
        return refuse(*sunLine, "a recipe with a [sun] must give the map's sha256 in [map]: where the sky is open "
                                "means nothing in another file");
    if (switchOffLine && !recipe.mapDigest)
        return refuse(*switchOffLine, "a recipe with a [switch-off] must give the map's sha256 in [map]: actor names "
                                      "mean nothing in another file");
    if (skyLine && !recipe.mapDigest)
        return refuse(*skyLine, "a recipe with a [sky] must give the map's sha256 in [map]: which textures paint "
                                "the sky means nothing in another file");
    if (!hideLines.empty() && !recipe.mapDigest)
        return refuse(hideLines.front(), "a recipe with a [hide] must give the map's sha256 in [map]: actor names "
                                         "mean nothing in another file");
    for (auto& [texture, assignment] : materials) recipe.materials.push_back(std::move(assignment));
    return recipe;
}

std::string write(const Recipe& recipe) {
    const std::uint32_t version = versionOf(recipe);
    std::string out = std::format("{}{}\n\n[map]\nfile = {}\n", HEADER, version,
                                  quotedIfNeeded(recipe.map, false));
    if (recipe.mapDigest) {
        out += "sha256 = ";
        for (const std::byte part : *recipe.mapDigest) out += std::format("{:02x}", std::to_integer<unsigned>(part));
        out += '\n';
    }
    if (!recipe.friendlyName.empty()) out += "name = " + quotedIfNeeded(recipe.friendlyName, true) + "\n";

    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    for (const MaterialAssignment& material : sortedMaterials(recipe)) {
        out += std::format("\n[material {}]\n", material.texture);
        if (material.metallic) out += std::format("metallic = {}\n", boolean(*material.metallic));
        if (material.emissive) out += std::format("emissive = {}\n", boolean(*material.emissive));
        if (material.baseRoughness) out += std::format("base-roughness = {}\n", *material.baseRoughness);
        if (material.emissiveThreshold)
            out += std::format("emissive-threshold = {}\n", *material.emissiveThreshold);
        if (material.parallaxDepth) out += std::format("parallax-depth = {}\n", *material.parallaxDepth);
        if (material.upscale) out += std::format("upscale = {}\n", *material.upscale);
    }
    for (const AddedLamp& lamp : recipe.lamps) {
        out += std::format("\n[lamp {}]\nlight = {}\n", lamp.name, lamp.light);
        if (!lamp.fitting.empty()) {
            out += "fitting =";
            for (const std::string& brush : lamp.fitting) out += ' ' + brush;
            out += '\n';
        }
        out += std::format("at = {} {} {}\n", lamp.at[0], lamp.at[1], lamp.at[2]);
        if (lamp.yaw != 0) out += std::format("yaw = {}\n", lamp.yaw);
        if (!lamp.mesh.empty()) out += std::format("mesh = {}\n", lamp.mesh);
    }
    if (recipe.sun)
        out += std::format("\n[sun]\nyaw = {}\npitch = {}\nhue = {}\nsaturation = {}\nbrightness = {}\n",
                           recipe.sun->yaw, recipe.sun->pitch, recipe.sun->hue, recipe.sun->saturation,
                           recipe.sun->brightness);
    if (!recipe.switchedOff.empty()) {
        out += "\n[switch-off]\nlights =";
        for (const std::string& light : recipe.switchedOff) out += ' ' + light;
        out += '\n';
    }
    if (recipe.sky) {
        out += "\n[sky]\ntextures =";
        for (const std::string& texture : recipe.sky->textures) out += ' ' + texture;
        const auto hex = [](const std::array<std::uint8_t, 3>& colour) {
            return std::format("{:02x}{:02x}{:02x}", colour[0], colour[1], colour[2]);
        };
        out += std::format("\ntop = {}\nhorizon = {}\nclouds = {}\n", hex(recipe.sky->top),
                           hex(recipe.sky->horizon), recipe.sky->clouds);
        if (recipe.sky->haze != SKY_HAZE_DEFAULT) out += std::format("haze = {}\n", recipe.sky->haze);
    }
    for (const HiddenBrush& hide : recipe.hidden)
        out += std::format("\n[hide {}]\nbrush = {}\ncover = {} {}\n", hide.name, hide.brush, hide.coverBrush,
                           hide.coverPolygon);
    return out;
}

std::array<std::byte, 32> bakeDigest(const Recipe& recipe) {
    Sha256 hasher;
    const auto addText = [&hasher](std::string_view text) {
        hasher.add(std::as_bytes(std::span<const char>(text.data(), text.size())));
    };
    const auto addByte = [&hasher](std::uint32_t value) {
        const std::byte byte = static_cast<std::byte>(value);
        hasher.add(std::span<const std::byte>(&byte, 1));
    };
    // Each field as a presence byte, then its value as one byte when present.
    const auto addField = [&addByte](const auto& field) {
        addByte(field.has_value() ? 1 : 0);
        if (field.has_value()) addByte(static_cast<std::uint32_t>(*field));
    };

    addText(std::format("uta-recipe-bake-{}\n", versionOf(recipe)));
    for (const MaterialAssignment& material : sortedMaterials(recipe)) {
        addText(material.texture);
        addText("\n");
        addField(material.metallic);
        addField(material.emissive);
        addField(material.baseRoughness);
        addField(material.emissiveThreshold);
        addField(material.parallaxDepth);
        addField(material.upscale);
    }
    // UTA-0256 SS 4.1: each lamp in file order, every text ended by a line
    // break and every number as four little-endian bytes.
    const auto addWord = [&addByte](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) addByte((value >> shift) & 0xFF);
    };
    for (const AddedLamp& lamp : recipe.lamps) {
        addText("lamp\n");
        addText(lamp.name);
        addText("\n");
        addText(lamp.light);
        addText("\n");
        addWord(static_cast<std::uint32_t>(lamp.fitting.size()));
        for (const std::string& brush : lamp.fitting) {
            addText(brush);
            addText("\n");
        }
        for (const float coordinate : lamp.at) addWord(std::bit_cast<std::uint32_t>(coordinate));
        addWord(lamp.yaw);
        // UTA-0256 SS 4.1, version 6: the mesh only when there is one, so a
        // recipe without keeps its digest.
        if (!lamp.mesh.empty()) {
            addText("mesh\n");
            addText(lamp.mesh);
            addText("\n");
        }
    }
    // UTA-0338 SS 4.1: the sun after the lamps, each field as four bytes.
    if (recipe.sun) {
        addText("sun\n");
        for (const std::uint32_t field : {recipe.sun->yaw, recipe.sun->pitch, std::uint32_t{recipe.sun->hue},
                                          std::uint32_t{recipe.sun->saturation}, std::uint32_t{recipe.sun->brightness}})
            addWord(field);
    }
    // UTA-0256 SS 4.6: the switched-off names last, in file order.
    if (!recipe.switchedOff.empty()) {
        addText("switch-off\n");
        for (const std::string& light : recipe.switchedOff) {
            addText(light);
            addText("\n");
        }
    }
    // UTA-0350 SS 4.1: the sky last -- its textures in order, each ended by a
    // line break, then its seven bytes.
    if (recipe.sky) {
        addText("sky\n");
        for (const std::string& texture : recipe.sky->textures) {
            addText(texture);
            addText("\n");
        }
        for (const std::uint8_t part : recipe.sky->top) addByte(part);
        for (const std::uint8_t part : recipe.sky->horizon) addByte(part);
        addByte(recipe.sky->clouds);
        // UTA-0391: the haze only where it is not the default, so a sky
        // without one keeps its version-6 digest.
        if (recipe.sky->haze != SKY_HAZE_DEFAULT) {
            addText("haze\n");
            addByte(recipe.sky->haze);
        }
    }
    // UTA-0109 SS 4.6: the hidden brushes last, in file order.
    for (const HiddenBrush& hide : recipe.hidden) {
        addText("hide\n");
        for (const std::string& text : {hide.name, hide.brush, hide.coverBrush}) {
            addText(text);
            addText("\n");
        }
        addWord(hide.coverPolygon);
    }
    return hasher.finish();
}

} // namespace uta::urecipe

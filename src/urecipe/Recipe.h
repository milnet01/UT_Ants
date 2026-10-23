// urecipe: the recipe format, read and write --
// docs/specs/UTA-0113-recipe-format.md SS 4.1, SS 4.2 and SS 4.4.
//
// A recipe is a hand-written text file of our own changes to somebody else's
// map, and the one thing this project distributes that describes one
// (ADR-0003). Version 1 holds only what the baker reads -- per-texture
// material settings -- and a friendly name. Version 2 adds lamps the map lacks
// (docs/specs/UTA-0256-added-lamps.md SS 4.1); version 3 a sun
// (docs/specs/UTA-0338-baked-sun.md SS 4.1); version 4 the map's own lights
// switched off, and lamps with no fitting (UTA-0256 SS 4.6); version 5 a sky
// the map only paints on its walls (docs/specs/UTA-0350-painted-skies.md
// SS 4.1); version 6 a lamp's mesh fitting (UTA-0256 SS 4.1); version 7 a
// sky's haze (UTA-0391); version 8 a brush hidden and its opening covered
// (docs/specs/UTA-0109-map-geometry.md SS 4.6, UTA-0396).
//
// CROSSES THE SEAM. docs/design.md rule 3 makes urecipe a vocabulary the
// runtime reads too, so it links uta_core and nothing else: no package reader,
// and no umat, whose CuratedOverride a MaterialAssignment's fields mirror,
// `upscale` aside. ubake makes the one from the other.
//
// STRICT. An unknown section or key, a repeat, a value out of range or a
// malformed line refuses the whole recipe, naming its line (SS 4.2): a
// misspelt key that changed nothing in silence would leave its author unable
// to see why.
//
// THE TRUST BOUNDARY. A recipe comes from other people. parse reads it as text
// only, opens no path it names and bounds every value.

#pragma once

#include "core/Error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uta::urecipe {

inline constexpr std::uint32_t RECIPE_VERSION = 8;
/// The most `[lamp]` sections one recipe holds -- UTA-0256 SS 4.1.
inline constexpr std::size_t LAMP_LIMIT = 64;

/// One texture's assignment: any subset of UTA-0010's override fields, and
/// the upscale factor UTA-0010 SS 4.5 sets directly. Declared in SS 4.2's key
/// order, which `write` and `bakeDigest` both follow.
struct MaterialAssignment {
    std::string texture; ///< `<package>.<path>`, lower-cased, no `#masked`
    std::optional<bool> metallic;
    std::optional<bool> emissive;
    std::optional<std::uint8_t> baseRoughness;
    std::optional<std::uint8_t> emissiveThreshold;
    std::optional<std::uint8_t> parallaxDepth;
    std::optional<std::uint32_t> upscale; ///< 1, 2 or 4

    friend bool operator==(const MaterialAssignment&, const MaterialAssignment&) = default;
};

/// A lamp the map lacks: a copy of one of its lights and of the brushes of
/// that light's fitting, moved -- UTA-0256 SS 4.1 and SS 4.3.
struct AddedLamp {
    std::string name;                 ///< 1 to 32 of a-z, 0-9 and -
    std::string light;                ///< the map's Light actor it copies, as named in the map
    std::vector<std::string> fitting; ///< brush actors whose surfaces it copies, each once; empty from version 4
    std::array<float, 3> at{};        ///< where the copy's light stands; finite
    std::uint32_t yaw = 0;            ///< the turn about the light, 65536 to a turn; below 65536
    std::string mesh;                 ///< a mesh decoration of the map it wears; empty for none; version 6

    friend bool operator==(const AddedLamp&, const AddedLamp&) = default;
};

/// The map's sun, standing infinitely far off -- UTA-0338 SS 4.1. UT's own
/// units: a turn is 65536, and the colour is a LightHue, LightSaturation and
/// LightBrightness.
struct Sun {
    std::uint32_t yaw = 0;   ///< compass direction it stands in; below 65536
    std::uint32_t pitch = 0; ///< height above the horizon, in (0, 16384]
    std::uint8_t hue = 0;
    std::uint8_t saturation = 0;
    std::uint8_t brightness = 0; ///< 1 to 255

    friend bool operator==(const Sun&, const Sun&) = default;
};

/// The most textures one `[sky]` names -- UTA-0350 SS 4.1.
inline constexpr std::size_t SKY_TEXTURE_LIMIT = 8;
/// UTA-0391: a `[sky]`'s `haze` when it gives none, and its bounds.
inline constexpr std::uint8_t SKY_HAZE_DEFAULT = 10;
inline constexpr std::uint8_t SKY_HAZE_MIN = 10;
inline constexpr std::uint8_t SKY_HAZE_MAX = 90;

/// A sky the map only paints on its walls, opened -- UTA-0350 SS 4.1. The
/// surfaces wearing `textures` become sky; the rest is how it looks.
struct Sky {
    std::vector<std::string> textures; ///< as MaterialAssignment::texture; sorted, each once, 1 to 8
    std::array<std::uint8_t, 3> top{0x3a, 0x78, 0xc8};     ///< sRGB, overhead
    std::array<std::uint8_t, 3> horizon{0xb4, 0xd4, 0xee}; ///< sRGB, at the horizon
    std::uint8_t clouds = 20;                              ///< percent of the sky covered, 0 to 100
    /// UTA-0391: where the outdoor haze starts, in percent of the outdoor
    /// extent, 10 to 90; version 7. The default is the haze every map has.
    std::uint8_t haze = SKY_HAZE_DEFAULT;

    friend bool operator==(const Sky&, const Sky&) = default;
};

/// The most `[hide]` sections one recipe holds -- UTA-0109 SS 4.6.
inline constexpr std::size_t HIDE_LIMIT = 64;

/// A brush of the map not drawn, and the hole it leaves in another surface's
/// plane drawn as that surface -- UTA-0109 SS 4.6 (UTA-0396).
struct HiddenBrush {
    std::string name;          ///< as AddedLamp::name
    std::string brush;         ///< the brush actor whose surfaces are not drawn, as named in the map
    std::string coverBrush;    ///< the brush actor of the surface drawn over the opening
    std::uint32_t coverPolygon = 0; ///< that surface's polygon in its brush, from 0

    friend bool operator==(const HiddenBrush&, const HiddenBrush&) = default;
};

struct Recipe {
    std::string map; ///< the map file's stem, lower-cased
    std::optional<std::array<std::byte, 32>> mapDigest; ///< when set, the SHA-256 the map must have
    std::string friendlyName; ///< runtime only; never hashed
    std::vector<MaterialAssignment> materials; ///< ascending by `texture`, each once
    std::vector<AddedLamp> lamps; ///< in file order; any lamp needs `mapDigest`
    std::optional<Sun> sun;       ///< at most one; needs `mapDigest`
    /// The map's Light actors switched off, in file order, each once; any
    /// needs `mapDigest` -- UTA-0256 SS 4.6.
    std::vector<std::string> switchedOff;
    std::optional<Sky> sky; ///< at most one; needs `mapDigest` -- UTA-0350 SS 4.1
    std::vector<HiddenBrush> hidden; ///< in file order; any needs `mapDigest` -- UTA-0109 SS 4.6

    friend bool operator==(const Recipe&, const Recipe&) = default;
};

/// SS 4.2's text. MalformedData naming the line for every refusal SS 4.2
/// lists; UnsupportedVersion, naming the line and both versions, for a
/// header above RECIPE_VERSION. `materials` comes back sorted; `lamps` keep
/// file order. A `[lamp]` needs header version 2, a `[sun]` version 3,
/// a `[switch-off]` or a lamp with no `fitting` version 4, a `[sky]`
/// version 5, a lamp's `mesh` version 6, a sky's `haze` version 7, and a
/// `[hide]` version 8.
[[nodiscard]] Result<Recipe> parse(std::string_view text);

/// SS 4.2's canonical form. `friendlyName` must hold no line break. The
/// header is the lowest version that holds the recipe -- 8 with a hidden
/// brush, 7 with a sky's haze
/// other than SKY_HAZE_DEFAULT, 6 with a lamp's mesh,
/// 5 with a sky, 4 with
/// a switched-off light or a lamp with no fitting, 3 with a sun, 2 with a
/// lamp, else 1 -- so
/// an older game still reads a recipe that needs no more.
[[nodiscard]] std::string write(const Recipe& recipe);

/// SHA-256 of the recipe's canonical bytes (SS 4.4). The map, its digest and
/// the friendly name are left out -- the first two are already in the bake
/// name, and a name changes no pixel. The bytes, in this order; a section the
/// recipe lacks adds none:
///   1. `uta-recipe-bake-<N>` and a line break, N the header write gives it.
///      A recipe using nothing a later version added keeps its older digest.
///   2. Each material, sorted: its texture, a line break, then each override
///      field as a presence byte and, when present, its value as one byte.
///   3. Each lamp, in file order (UTA-0256 SS 4.1): name, light and fitting
///      as text, then its place and yaw as four-byte words; its mesh, only
///      when it has one.
///   4. The sun (UTA-0338 SS 4.1): five four-byte words.
///   5. The switched-off names, in file order (UTA-0256 SS 4.6).
///   6. The sky (UTA-0350 SS 4.1): its textures, top, horizon and clouds; its
///      haze only when it is not SKY_HAZE_DEFAULT (UTA-0391).
///   7. Each hidden brush, in file order (UTA-0109 SS 4.6).
[[nodiscard]] std::array<std::byte, 32> bakeDigest(const Recipe& recipe);

} // namespace uta::urecipe

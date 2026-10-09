// urecipe: the recipe format, read and write --
// docs/specs/UTA-0113-recipe-format.md SS 4.1, SS 4.2 and SS 4.4.
//
// A recipe is a hand-written text file of our own changes to somebody else's
// map, and the one thing this project distributes that describes one
// (ADR-0003). Version 1 holds only what the baker reads -- per-texture
// material settings -- and a friendly name. Version 2 adds lamps the map lacks
// (docs/specs/UTA-0256-added-lamps.md SS 4.1).
//
// CROSSES THE SEAM. docs/design.md rule 3 makes urecipe a vocabulary the
// runtime reads too, so it links uta_core and nothing else: no package reader,
// and no umat, whose CuratedOverride the six fields below mirror. ubake makes
// the one from the other.
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

inline constexpr std::uint32_t RECIPE_VERSION = 2;
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
    std::vector<std::string> fitting; ///< brush actors whose surfaces it copies, each once
    std::array<float, 3> at{};        ///< where the copy's light stands; finite
    std::uint32_t yaw = 0;            ///< the turn about the light, 65536 to a turn; below 65536

    friend bool operator==(const AddedLamp&, const AddedLamp&) = default;
};

struct Recipe {
    std::string map; ///< the map file's stem, lower-cased
    std::optional<std::array<std::byte, 32>> mapDigest; ///< when set, the SHA-256 the map must have
    std::string friendlyName; ///< runtime only; never hashed
    std::vector<MaterialAssignment> materials; ///< ascending by `texture`, each once
    std::vector<AddedLamp> lamps; ///< in file order; any lamp needs `mapDigest`

    friend bool operator==(const Recipe&, const Recipe&) = default;
};

/// SS 4.2's text. MalformedData naming the line for every refusal SS 4.2
/// lists; UnsupportedVersion, naming the line and both versions, for a
/// header above RECIPE_VERSION. `materials` comes back sorted; `lamps` keep
/// file order. A `[lamp]` needs header version 2.
[[nodiscard]] Result<Recipe> parse(std::string_view text);

/// SS 4.2's canonical form. `friendlyName` must hold no line break. The
/// header is version 1 unless the recipe holds a lamp, so a game that reads
/// only version 1 still reads a recipe that needs no more.
[[nodiscard]] std::string write(const Recipe& recipe);

/// SHA-256 of SS 4.4's canonical bytes: the materials only. The map, its
/// digest and the friendly name are left out -- the first two are already in
/// the bake name, and a name changes no pixel. A recipe with a lamp hashes
/// UTA-0256 SS 4.1's version-2 bytes instead; one with none keeps its
/// version-1 digest.
[[nodiscard]] std::array<std::byte, 32> bakeDigest(const Recipe& recipe);

} // namespace uta::urecipe

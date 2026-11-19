// The sounds a map's actors name, and the weapon set's sound bundle --
// docs/specs/UTA-0478-baked-sounds.md SS 4.3 and SS 4.4.
//
// A FILE OF ITS OWN because it changes for its own reason: which references
// are sounds, and which classes a weapon set reaches, are rules nothing else
// in the bake reads.
//
// A sound is found by what a reference points at, never by the property's
// name (SS 3 decision 3): any object path that leads to an export of class
// Sound.

#pragma once

#include "core/Error.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"
#include "upkg/Class.h"
#include "upkg/Package.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace uta::ubake {

struct FoundSounds {
    std::vector<ubundle::BakedSound> sounds; ///< strictly ascending by path, as SNDS wants
    std::vector<SkippedSound> skipped;
};

/// SS 4.3: every sound an actor's own properties or its class's defaults
/// name. `map` goes by `mapName` (folded), which heads the path of a sound
/// the map itself holds.
[[nodiscard]] Result<FoundSounds> mapSounds(const ubundle::Placements& placements, const upkg::Package& map,
                                            std::string_view mapName, const upkg::PackageResolver& resolver);

struct SoundSetRequest {
    std::filesystem::path install;
    std::vector<std::string> classes; ///< "package.class", any case, any order
    std::filesystem::path outDir;
    bool force = false;
};

struct SoundSetOutcome {
    Verdict verdict = Verdict::Written; ///< Written or Cached
    std::string name;
    std::filesystem::path path;         ///< outDir / (name + ".utab")
    std::size_t soundCount = 0;         ///< zero when Cached
    std::uint64_t soundBytes = 0;
    std::vector<std::string> unresolved; ///< listed classes not found, ascending
    std::vector<SkippedSound> skippedSounds;
};

/// SS 4.4: name, look in the cache, gather, write a bundle of kind Sounds.
[[nodiscard]] Result<SoundSetOutcome> bakeSoundSet(const SoundSetRequest& request);

namespace detail {

struct SoundSet {
    FoundSounds found;
    std::vector<std::string> unresolved;
};

/// SS 4.4 "Which sounds": the listed classes' effective defaults, and those
/// of the classes they name by a class-valued default -- one step, no
/// further. A test seam: every lookup goes through `resolver`.
[[nodiscard]] Result<SoundSet> soundSetOf(const std::vector<std::string>& classes,
                                          const upkg::PackageResolver& resolver);

/// SS 4.4 "The name", from its parts. `classes` in any order and repeated;
/// `closure` in any order.
[[nodiscard]] std::string soundSetNameOf(std::string_view bakerVersion, const std::vector<std::string>& classes,
                                         const std::vector<ClosureEntry>& closure);

/// The name for `classes` over `install`: the closure is the classes'
/// packages and everything detail::closure reaches from each.
[[nodiscard]] Result<std::string> soundSetName(const std::vector<std::string>& classes, Install& install);

} // namespace detail

} // namespace uta::ubake

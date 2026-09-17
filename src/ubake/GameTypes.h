// The game types an install registers, and the map-name prefix each one lists
// maps by -- UTA-0179.
//
// UT99'S OWN RULE. Its menus list, for a game type, the maps whose file name
// starts with that game type's MapPrefix. So a Maps file no installed game type
// claims -- CityIntro, the opening movie; Entry, the menu's backdrop -- is not a
// map anyone is offered. The game types are the .int files' `Object=` entries
// whose MetaClass is Botpack.TournamentGameInfo.
//
// BAKE-SIDE ONLY: it reads packages, so docs/design.md rule 2 keeps it out of
// the runtime targets, which ask `ut-bake --game-types` (rule 16).

#pragma once

#include "Install.h"

#include <string>
#include <vector>

namespace uta::ubake {

struct GameType {
    std::string name;      ///< `Package.Class`, as the .int file spells it
    std::string mapPrefix; ///< the class family's MapPrefix; empty when none sets one
};

struct GameTypes {
    /// Every game type whose class was found, sorted by name ignoring case.
    std::vector<GameType> found;
    /// Names whose package is absent, or does not hold the class.
    std::vector<std::string> unresolved;
};

/// The game types registered by `System/*.int` and `SystemLocalized/int/*.int`,
/// directories and extension matched case-insensitively. A name registered
/// twice is one game type. An .int file may be UTF-8, with or without a byte
/// order mark, or UTF-16 with one.
[[nodiscard]] GameTypes readGameTypes(Install& install);

} // namespace uta::ubake

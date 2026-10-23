// The map launcher -- UTA-0170. The SDL half; MapList.h holds what needs no
// window. Run by hand: no CI leg has a display.

#pragma once

#include "Cli.h"

#include <string>

namespace uta::client {

/// The program `name` beside this one, as UTF-8 for SDL_CreateProcess, with
/// Windows' .exe added. How ut-ants finds ut-bake (docs/design.md rule 16),
/// and how the launcher finds the viewer.
[[nodiscard]] std::string besideThisProgram(const char* name);

/// Lists every map in `options.install`, bakes a picked one with ut-bake and
/// opens it in a second ut-ants, and keeps a notes file per map. Returns an
/// exit code. The install has already been checked.
[[nodiscard]] int runLauncher(const Options& options);

} // namespace uta::client

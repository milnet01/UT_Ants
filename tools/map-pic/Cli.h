// map-pic: save the picture a map ships with as a PNG -- UTA-0316, for
// UT_MonsterHunt's server launcher (their GAME-0196) and our own map browser.
//
// A map's picture is its LevelInfo's `Screenshot` property: an object
// reference to a palettised texture, usually in the map's own MyLevel group,
// sometimes in another package. Only that route is built. Drawing a picture of
// a map that ships none needs a bake per map, so UT_MonsterHunt shoots those
// with its own rig for now (agreed 2026-10-06).
//
// Compiled into the map-pic binary and into the unit tests, so the picture and
// the command line are tested without starting a process.

#pragma once

#include "core/Error.h"
#include "umat/Material.h"
#include "upkg/Class.h"
#include "upkg/Package.h"

#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace uta::mappic {

/// What a map offers as its picture.
struct Preview {
    /// The Screenshot texture's base level as opaque RGBA8; empty when there
    /// is none to give.
    std::optional<umat::Image> image;
    /// Why `image` is empty: the map names no Screenshot, or the one it names
    /// does not resolve or decode. Empty when `image` is set.
    std::string none;
};

/// The picture `map` ships with. A map that names none, or names one that
/// cannot be read, is a Preview without an image rather than an error: either
/// way the caller has to find a picture elsewhere. An error is the map itself
/// not reading -- no Level, or a Level that does not parse.
[[nodiscard]] Result<Preview> previewOf(const upkg::Package& map, std::string_view mapName,
                                        const upkg::PackageResolver& resolver);

/// `map-pic <install> <map.unr> <out.png>`; `args` excludes the program name.
/// Prints `preview` and writes the PNG, or prints `none` and writes nothing,
/// with the reason on `err`; both return 0. Returns 1 when the install or the
/// map does not open or the PNG cannot be written, and 2 on wrong arguments.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err);

} // namespace uta::mappic

// An Unreal Tournament install: which file holds each package, a resolver
// over them, and whether the directory is an install at all.
//
// docs/specs/UTA-0011-map-baker.md SS 4.2 and SS 4.9.
//
// BAKE-SIDE ONLY, as everything in ubake is: it links the package reader, so
// docs/design.md rule 2 keeps it out of both runtime targets. They learn
// whether an install is usable by running `ut-bake --check` (rule 16).
//
// UT99'S OWN ORDER, NOT THE PLAYER'S. The search order is fixed rather than
// read from the player's .ini, because two players with different settings
// would otherwise give one map two names (SS 8).

#pragma once

#include "core/Error.h"
#include "upkg/Class.h"
#include "upkg/Package.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace uta::ubake {

/// An Unreal Tournament install, indexed once by `open`.
///
/// Packages are searched in `System/*.u`, `Maps/*.unr`, `Textures/*.utx`,
/// `Sounds/*.uax`, `Music/*.umx` order, each directory and extension matched
/// case-insensitively. An earlier directory shadows a later one; within one
/// directory, two files whose names fold to one package resolve to the first
/// by bytewise comparison of their file names (SS 4.2).
class Install {
public:
    /// NotFound when `root` is not a directory; IoFailure when it cannot be
    /// listed.
    [[nodiscard]] static Result<Install> open(const std::filesystem::path& root);

    Install(Install&&) noexcept;
    Install& operator=(Install&&) noexcept;
    ~Install();

    /// Folds its input as SS 4.4 defines, then looks it up in SS 4.2's order.
    /// A name that is absent, or whose file does not open as a package, gives
    /// nullptr and no error. Each package is read and opened at most once.
    /// Valid for the lifetime of the Install, across a move of it.
    ///
    /// Not safe to call from two threads at once: it opens and caches.
    [[nodiscard]] upkg::PackageResolver resolver();

    /// The file SS 4.2's order finds for a package name, whether or not it
    /// opens, or empty when there is none.
    [[nodiscard]] std::filesystem::path pathOf(std::string_view packageName) const;

    /// The bytes of a package the resolver has opened, or an empty span.
    [[nodiscard]] std::span<const std::byte> bytesOf(std::string_view packageName) const;

    [[nodiscard]] const std::filesystem::path& root() const noexcept;

private:
    struct State;
    explicit Install(std::unique_ptr<State> state) noexcept;

    /// On the heap so a resolver handed out before a move still points at
    /// live state after it.
    std::unique_ptr<State> state_;
};

/// One reason a directory is not a usable install.
struct Problem {
    std::string what; ///< the file or directory
    std::string why;  ///< a sentence
};

struct CheckReport {
    bool ok = false; ///< true when `problems` is empty
    std::vector<Problem> problems;
};

/// Whether `root` holds `Core`, `Engine` and `Botpack`, each opening as a
/// package -- SS 4.9. Botpack is Unreal Tournament's own game package, which
/// is what separates its install from any other Unreal Engine 1 game's.
[[nodiscard]] CheckReport checkInstall(const std::filesystem::path& root);

namespace detail {

/// SS 4.4's fold: ASCII `A`-`Z` become `a`-`z` and every other byte is kept.
/// One fold for the resolver, the closure and the bake name, so a package
/// cannot be named one way in one and another way in the next.
[[nodiscard]] std::string fold(std::string_view text);

/// A path as UTF-8, whatever the platform's own narrow encoding is -- for
/// messages, JSON and the bytewise file-name order SS 4.2 names.
[[nodiscard]] std::string utf8(const std::filesystem::path& path);

} // namespace detail

} // namespace uta::ubake

// Locks INV-6, INV-7, INV-8 and the uta::fs half of INV-3, per
// docs/specs/UTA-0002-core-foundations.md.
//
// Every failure fixture here is permission-independent. A run as root can
// write to a directory it has no permission for, so a chmod-based fixture
// fails to fail -- which is worse than no test, because the reader concludes
// the code is sound.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "core/FileSystem.h"

namespace fs = std::filesystem;
using uta::ErrorCode;

namespace {

/// A directory that removes itself, so a failing test cannot leave litter for
/// the next run to trip over.
class TempDir {
public:
    TempDir() {
        // Not getpid(): Windows is a first-class target and does not have it.
        // A random_device read once per process plus a counter is portable
        // and enough to keep two concurrent runs apart.
        static const unsigned long long salt = std::random_device{}();
        static int counter = 0;
        path_ = fs::temp_directory_path() /
                ("uta-fs-test-" + std::to_string(salt) + "-" +
                 std::to_string(counter++));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

std::vector<std::byte> bytesOf(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const char c : text) out.push_back(static_cast<std::byte>(c));
    return out;
}

std::string textOf(std::span<const std::byte> bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (const std::byte b : bytes) out.push_back(static_cast<char>(b));
    return out;
}

void writeText(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

/// Counts entries, so a test can assert a directory is exactly as it was.
std::vector<std::string> listing(const fs::path& dir) {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir))
        names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace

// ---------------------------------------------------------------- INV-3, fs

TEST_CASE("a missing file is NotFound, not a throw", "[core][fs]") {
    const TempDir dir;
    const auto result = uta::fs::readFile(dir.path() / "absent.unr");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::NotFound);
    CHECK_FALSE(result.error().message().empty());
}

TEST_CASE("a directory passed where a file is expected is InvalidArgument",
          "[core][fs]") {
    const TempDir dir;
    const auto result = uta::fs::readFile(dir.path());

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("a file round-trips through write and read", "[core][fs]") {
    const TempDir dir;
    const fs::path target = dir.path() / "bundle.utab";

    const auto written = uta::fs::writeFileAtomically(target, bytesOf("hello map"));
    REQUIRE(written.has_value());

    const auto read = uta::fs::readFile(target);
    REQUIRE(read.has_value());
    CHECK(textOf(*read) == "hello map");
}

TEST_CASE("an empty file round-trips", "[core][fs]") {
    const TempDir dir;
    const fs::path target = dir.path() / "empty.utab";

    REQUIRE(uta::fs::writeFileAtomically(target, {}).has_value());
    const auto read = uta::fs::readFile(target);
    REQUIRE(read.has_value());
    CHECK(read->empty());
}

// ------------------------------------------------------------------- INV-7

// (a) The destination's parent is a regular file, so no temporary can be
// created at all.
TEST_CASE("a write whose parent is a regular file fails and creates nothing",
          "[core][fs]") {
    const TempDir dir;
    const fs::path parentAsFile = dir.path() / "not-a-directory";
    writeText(parentAsFile, "I am a file");

    const auto before = listing(dir.path());
    const auto result =
        uta::fs::writeFileAtomically(parentAsFile / "child.utab", bytesOf("x"));

    REQUIRE_FALSE(result.has_value());
    // Either code is correct and the platform picks: opening under a file
    // parent sets ENOTDIR on Linux and ENOENT on Windows. What INV-7 asserts
    // is that it failed and left nothing behind, not which of the two it is.
    CHECK((result.error().code() == ErrorCode::IoFailure ||
           result.error().code() == ErrorCode::NotFound));
    CHECK_FALSE(result.error().message().empty());
    CHECK(listing(dir.path()) == before);
}

// (b) The destination is an existing directory, so the rename fails AFTER the
// temporary has been written. This is the case that catches an implementation
// which returns early without unlinking.
TEST_CASE("a failed rename leaves no temporary and does not touch the tree",
          "[core][fs]") {
    const TempDir dir;
    const fs::path destination = dir.path() / "occupied";
    fs::create_directory(destination);

    const fs::path sibling = dir.path() / "sibling.utab";
    writeText(sibling, "original bytes");

    const auto before = listing(dir.path());

    const auto result = uta::fs::writeFileAtomically(destination, bytesOf("new bytes"));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::IoFailure);

    // No temporary survives, and nothing else in the directory moved.
    CHECK(listing(dir.path()) == before);

    const auto siblingNow = uta::fs::readFile(sibling);
    REQUIRE(siblingNow.has_value());
    CHECK(textOf(*siblingNow) == "original bytes");

    CHECK(fs::is_directory(destination));
}

TEST_CASE("an overwrite replaces the destination completely", "[core][fs]") {
    const TempDir dir;
    const fs::path target = dir.path() / "bundle.utab";
    writeText(target, "a much longer set of original bytes");

    REQUIRE(uta::fs::writeFileAtomically(target, bytesOf("short")).has_value());

    const auto read = uta::fs::readFile(target);
    REQUIRE(read.has_value());
    CHECK(textOf(*read) == "short");

    // Exactly one file: the temporary was renamed, not left beside it.
    CHECK(listing(dir.path()) == std::vector<std::string>{"bundle.utab"});
}

// ------------------------------------------------------------------- INV-6

TEST_CASE("resolveUnder accepts a relative path that stays inside", "[core][fs]") {
    const TempDir dir;
    const auto result = uta::fs::resolveUnder(dir.path(), "maps/DM-Deck16.unr");

    REQUIRE(result.has_value());
    CHECK(result->string().starts_with(fs::weakly_canonical(dir.path()).string()));
}

TEST_CASE("resolveUnder refuses a path that escapes through ..", "[core][fs]") {
    const TempDir dir;
    const auto result = uta::fs::resolveUnder(dir.path(), "../escaped.unr");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("resolveUnder refuses an absolute path even inside the root",
          "[core][fs]") {
    const TempDir dir;
    // Inside the root, and still refused: the contract is relative-only, so
    // callers cannot pass through a value a remote named.
    const auto result = uta::fs::resolveUnder(dir.path(), dir.path() / "inside.unr");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("resolveUnder refuses an empty path", "[core][fs]") {
    const TempDir dir;
    const auto result = uta::fs::resolveUnder(dir.path(), "");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("resolveUnder refuses a sibling whose name merely shares a prefix",
          "[core][fs]") {
    const TempDir dir;
    const fs::path root = dir.path() / "data";
    fs::create_directory(root);
    fs::create_directory(dir.path() / "data-other");

    // A string-prefix comparison would accept this; an element-wise one does
    // not.
    const auto result = uta::fs::resolveUnder(root, "../data-other/x.unr");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
}

#ifndef _WIN32
// A DANGLING symlink is the case weakly_canonical does not resolve: status()
// follows links, so a link to a missing target reads as not_found and its own
// name is appended unresolved. It would pass containment while still pointing
// outside root, and a caller creating through it writes outside root.
TEST_CASE("resolveUnder refuses a dangling symlink", "[core][fs]") {
    const TempDir dir;
    const fs::path root = dir.path() / "root";
    fs::create_directory(root);

    std::error_code ec;
    fs::create_symlink(dir.path() / "does-not-exist-yet", root / "dangling", ec);
    if (ec) SUCCEED("symlinks unavailable here");

    const auto result = uta::fs::resolveUnder(root, "dangling");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("resolveUnder refuses an escape through a symlink", "[core][fs]") {
    const TempDir dir;
    const fs::path root = dir.path() / "root";
    const fs::path outside = dir.path() / "outside";
    fs::create_directory(root);
    fs::create_directory(outside);

    std::error_code ec;
    fs::create_directory_symlink(outside, root / "link", ec);
    if (ec) SUCCEED("symlinks unavailable here");

    // Lexically this stays under root. It does not.
    const auto result = uta::fs::resolveUnder(root, "link/escaped.unr");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
}
#endif

// ------------------------------------------------------------------- INV-8

namespace {

/// Sets an environment variable for the duration of a test and restores it.
class EnvScope {
public:
    EnvScope(const char* name, const char* value) : name_(name) {
        if (const char* previous = std::getenv(name)) {
            had_ = true;
            previous_ = previous;
        }
        set(value);
    }
    ~EnvScope() { set(had_ ? previous_.c_str() : nullptr); }
    EnvScope(const EnvScope&) = delete;
    EnvScope& operator=(const EnvScope&) = delete;

private:
    void set(const char* value) {
#ifdef _WIN32
        _putenv_s(name_, value ? value : "");
#else
        if (value) ::setenv(name_, value, 1);
        else ::unsetenv(name_);
#endif
    }

    const char* name_;
    bool had_ = false;
    std::string previous_;
};

}  // namespace

// (a) Both platforms: the variables the platform reads are honoured.
TEST_CASE("the platform's own variables are honoured where set", "[core][fs]") {
    const TempDir dir;
#ifdef _WIN32
    const EnvScope appData("APPDATA", dir.path().string().c_str());
    const EnvScope localAppData("LOCALAPPDATA", dir.path().string().c_str());
#else
    const EnvScope config("XDG_CONFIG_HOME", dir.path().string().c_str());
    const EnvScope cache("XDG_CACHE_HOME", dir.path().string().c_str());
    const EnvScope state("XDG_STATE_HOME", dir.path().string().c_str());
#endif

    const auto configDir = uta::fs::configDirectory();
    const auto cacheDir = uta::fs::cacheDirectory();
    const auto logDir = uta::fs::logDirectory();

    REQUIRE(configDir.has_value());
    REQUIRE(cacheDir.has_value());
    REQUIRE(logDir.has_value());

    const std::string root = dir.path().string();
    CHECK(configDir->string().starts_with(root));
    CHECK(cacheDir->string().starts_with(root));
    CHECK(logDir->string().starts_with(root));
}

#ifndef _WIN32
// (b) Linux only, because it is the only platform with a fallback.
TEST_CASE("an unset XDG variable falls back to an absolute path", "[core][fs]") {
    const TempDir dir;
    const EnvScope config("XDG_CONFIG_HOME", nullptr);
    const EnvScope cache("XDG_CACHE_HOME", nullptr);
    const EnvScope state("XDG_STATE_HOME", nullptr);
    const EnvScope home("HOME", dir.path().string().c_str());

    const auto configDir = uta::fs::configDirectory();
    const auto cacheDir = uta::fs::cacheDirectory();
    const auto logDir = uta::fs::logDirectory();

    REQUIRE(configDir.has_value());
    REQUIRE(cacheDir.has_value());
    REQUIRE(logDir.has_value());

    // The point of the invariant: never relative to the current working
    // directory, which for a test run is the build tree.
    CHECK(configDir->is_absolute());
    CHECK(cacheDir->is_absolute());
    CHECK(logDir->is_absolute());
}
#endif

#ifndef _WIN32
TEST_CASE("a relative XDG value is ignored, not honoured", "[core][fs]") {
    const TempDir dir;
    // XDG requires a relative value to be ignored, and INV-8 promises every
    // fallback is absolute -- which a relative HOME would otherwise break.
    const EnvScope config("XDG_CONFIG_HOME", "relative/not/absolute");
    const EnvScope home("HOME", dir.path().string().c_str());

    const auto configDir = uta::fs::configDirectory();
    REQUIRE(configDir.has_value());
    CHECK(configDir->is_absolute());
    CHECK(configDir->string().starts_with(dir.path().string()));
}
#endif

// (c) Both platforms: nothing to read from is NotFound, not a guess.
TEST_CASE("no variable and no fallback is NotFound", "[core][fs]") {
#ifdef _WIN32
    const EnvScope appData("APPDATA", nullptr);
    const EnvScope localAppData("LOCALAPPDATA", nullptr);
#else
    const EnvScope config("XDG_CONFIG_HOME", nullptr);
    const EnvScope cache("XDG_CACHE_HOME", nullptr);
    const EnvScope state("XDG_STATE_HOME", nullptr);
    const EnvScope home("HOME", nullptr);
#endif

    const auto configDir = uta::fs::configDirectory();
    const auto cacheDir = uta::fs::cacheDirectory();
    const auto logDir = uta::fs::logDirectory();

    REQUIRE_FALSE(configDir.has_value());
    REQUIRE_FALSE(cacheDir.has_value());
    REQUIRE_FALSE(logDir.has_value());
    CHECK(configDir.error().code() == ErrorCode::NotFound);
    CHECK(cacheDir.error().code() == ErrorCode::NotFound);
    CHECK(logDir.error().code() == ErrorCode::NotFound);
}

// (d) Both platforms: a variable that is SET but RELATIVE is treated as
// unset. Without this case the rule is asserted by nothing, and deleting the
// branch leaves the suite green while a user's config lands under whatever
// directory the process happened to start in (INV-8).
TEST_CASE("a relative variable is treated as unset", "[core][fs]") {
#ifdef _WIN32
    // Windows has no fallback, so an ignored variable becomes NotFound.
    const EnvScope appData("APPDATA", "relative\\not\\absolute");

    const auto configDir = uta::fs::configDirectory();
    REQUIRE_FALSE(configDir.has_value());
    CHECK(configDir.error().code() == ErrorCode::NotFound);
#else
    const TempDir home;
    const EnvScope homeVar("HOME", home.path().string().c_str());
    const EnvScope config("XDG_CONFIG_HOME", "relative/not/absolute");

    const auto configDir = uta::fs::configDirectory();
    REQUIRE(configDir.has_value());
    // The fallback, not the relative value.
    CHECK(configDir->is_absolute());
    CHECK(*configDir == home.path() / ".config" / "ut-ants");
#endif
}

// INV-6 -- a root spelled with a trailing separator is accepted. Whether
// weakly_canonical keeps the empty final element differs between standard
// libraries (measured: libstdc++ 16.2 strips it), so this is the case that
// proves the behaviour per leg rather than arguing it from one of them.
TEST_CASE("a root written with a trailing separator is accepted", "[core][fs]") {
    const TempDir dir;
    const fs::path withSeparator = dir.path().string() + std::string(1, fs::path::preferred_separator);

    // Asserted as "the separator makes no difference" rather than as a string
    // prefix of dir.path(). A prefix test fails on Windows for a reason that
    // has nothing to do with this invariant: the CI runner's temp directory is
    // a short (8.3) path, and weakly_canonical expands it -- measured, the
    // MSVC leg went red on exactly that while resolveUnder had accepted the
    // root correctly. Comparing the two spellings against each other depends
    // on no platform's idea of how a path is written.
    const auto withTrailing = uta::fs::resolveUnder(withSeparator, "maps/DM-Deck16.unr");
    const auto without = uta::fs::resolveUnder(dir.path(), "maps/DM-Deck16.unr");

    REQUIRE(withTrailing.has_value());
    REQUIRE(without.has_value());
    CHECK(*withTrailing == *without);
}

// INV-15 -- the lexical pass over the relative path's components. Enforced on
// EVERY platform, not only Windows: a rule that holds on one and not the other
// means a Linux server and a Windows client disagree about which content is
// safe, and unet moves content between exactly those.
TEST_CASE("a Win32 reserved device name is refused, on every platform", "[core][fs]") {
    const TempDir dir;

    // Bare, with an extension, lower case, and nested -- a device name
    // resolves to a device in ANY directory on Win32, extension included.
    for (const char* relative : {"CON", "NUL", "COM1", "LPT9",
                                 "COM1.txt", "com1", "lpt9.log",
                                 "maps/AUX", "maps/PRN.unr"}) {
        const auto result = uta::fs::resolveUnder(dir.path(), relative);
        INFO("relative = " << relative);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("a component ending in a dot or a space is refused", "[core][fs]") {
    const TempDir dir;

    // Win32 strips both, so "a." and "a" are one file after a check has
    // passed on two names.
    for (const char* relative : {"a.", "a ", "maps/deck.", "maps /x.unr"}) {
        const auto result = uta::fs::resolveUnder(dir.path(), relative);
        INFO("relative = " << relative);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("a component containing a colon is refused", "[core][fs]") {
    const TempDir dir;

    // "a.txt:s" is an alternate data stream on Win32, which an extension
    // check cannot see; "C:foo" is drive-relative, so is_absolute() does not
    // catch it.
    for (const char* relative : {"a.txt:s", "C:foo", "maps/deck.unr:hidden"}) {
        const auto result = uta::fs::resolveUnder(dir.path(), relative);
        INFO("relative = " << relative);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
}

// INV-15's ORDER half. Every other case above fails whether the lexical pass
// runs before or after canonicalisation, so none of them can falsify the
// ordering. This one can: weakly_canonical resolves "COM1/../safe.unr" to
// "<root>/safe.unr" (measured, libstdc++ 16.2), so an implementation that
// canonicalises first never sees COM1 and accepts.
TEST_CASE("the lexical pass runs before canonicalisation", "[core][fs]") {
    const TempDir dir;

    const auto result = uta::fs::resolveUnder(dir.path(), "COM1/../safe.unr");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
}

// The rules must not refuse ordinary names, or every caller works around them.
TEST_CASE("ordinary names still resolve", "[core][fs]") {
    const TempDir dir;

    for (const char* relative : {"maps/DM-Deck16.unr", "Textures/SkyCity.utx",
                                 "a.b.c", "CONSOLE", "COM10", "com1x",
                                 "LPT0", "nullify.unr"}) {
        const auto result = uta::fs::resolveUnder(dir.path(), relative);
        INFO("relative = " << relative);
        CHECK(result.has_value());
    }
}

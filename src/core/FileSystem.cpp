#include "core/FileSystem.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <system_error>

namespace uta::fs {
namespace {

/// An environment variable, or nullopt when unset or empty. An empty value is
/// treated as unset: XDG says a relative or empty value must be ignored, and
/// an empty %APPDATA% is no more usable than a missing one.
[[nodiscard]] std::optional<std::filesystem::path> envPath(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::filesystem::path(value);
}

[[nodiscard]] Error missing(const char* what) {
    return Error(ErrorCode::NotFound,
                 std::string("no ") + what + " directory: the environment names none "
                 "and this platform has no fallback");
}

#ifndef _WIN32
/// The Linux shape: $VARIABLE if set, else $HOME/<fallback>, each with
/// "ut-ants" appended. HOME unset with the variable unset is NotFound -- the
/// alternative is guessing at the current directory, and a program that
/// cannot find where to write should say so.
[[nodiscard]] Result<std::filesystem::path> xdgDirectory(const char* variable,
                                                         const char* fallback,
                                                         const char* what) {
    if (const auto fromEnv = envPath(variable)) return *fromEnv / "ut-ants";

    const auto home = envPath("HOME");
    if (!home) return std::unexpected(missing(what));
    return *home / fallback / "ut-ants";
}
#endif

}  // namespace

#ifdef _WIN32

Result<std::filesystem::path> configDirectory() {
    const auto appData = envPath("APPDATA");
    if (!appData) return std::unexpected(missing("configuration"));
    return *appData / "UT_Ants";
}

Result<std::filesystem::path> cacheDirectory() {
    const auto localAppData = envPath("LOCALAPPDATA");
    if (!localAppData) return std::unexpected(missing("cache"));
    return *localAppData / "UT_Ants" / "cache";
}

Result<std::filesystem::path> logDirectory() {
    const auto localAppData = envPath("LOCALAPPDATA");
    if (!localAppData) return std::unexpected(missing("log"));
    return *localAppData / "UT_Ants" / "logs";
}

#else

Result<std::filesystem::path> configDirectory() {
    return xdgDirectory("XDG_CONFIG_HOME", ".config", "configuration");
}

Result<std::filesystem::path> cacheDirectory() {
    return xdgDirectory("XDG_CACHE_HOME", ".cache", "cache");
}

Result<std::filesystem::path> logDirectory() {
    return xdgDirectory("XDG_STATE_HOME", ".local/state", "log");
}

#endif

Result<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);

    // Read the TYPE before the error_code. libstdc++ sets ec to ENOENT for a
    // missing file even though it also reports file_type::not_found, so an
    // `if (ec)` first would answer IoFailure for every absent path -- which
    // is what this test caught.
    if (status.type() == std::filesystem::file_type::not_found)
        return fail(ErrorCode::NotFound, "no such file: " + path.string());
    if (ec)
        return fail(ErrorCode::IoFailure,
                    "cannot stat " + path.string() + ": " + ec.message());
    if (std::filesystem::is_directory(status))
        return fail(ErrorCode::InvalidArgument, "is a directory, not a file: " + path.string());

    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return fail(ErrorCode::IoFailure, "cannot size " + path.string() + ": " + ec.message());

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));

    // The error_code overloads above keep std::filesystem from throwing;
    // stdio keeps the read itself from throwing too (INV-3).
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr)
        return fail(ErrorCode::PermissionDenied, "cannot open " + path.string());

    const std::size_t read =
        bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
    const bool failed = std::ferror(file) != 0;
    (void)std::fclose(file);  // read path: nothing to flush, nothing to report

    if (failed || read != bytes.size())
        return fail(ErrorCode::IoFailure, "short read on " + path.string());

    return bytes;
}

Result<void> writeFileAtomically(const std::filesystem::path& path,
                                 std::span<const std::byte> bytes) {
    const std::filesystem::path parent =
        path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");

    // A per-process counter, so two threads writing different files in one
    // directory cannot pick the same temporary.
    static std::atomic<unsigned long long> counter{0};
    const std::filesystem::path temporary =
        parent / (path.filename().string() + ".tmp-" +
                  std::to_string(counter.fetch_add(1, std::memory_order_relaxed)));

    std::FILE* file = std::fopen(temporary.string().c_str(), "wb");
    if (file == nullptr)
        return fail(ErrorCode::IoFailure,
                    "cannot create a temporary beside " + path.string());

    // From here every failure path unlinks the temporary before returning.
    // That is INV-7, and it is why this is not written as early returns.
    const std::size_t written =
        bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), file);
    const bool writeFailed = written != bytes.size() || std::ferror(file) != 0;
    const bool closeFailed = std::fclose(file) != 0;

    std::error_code ec;
    if (writeFailed || closeFailed) {
        std::filesystem::remove(temporary, ec);
        return fail(ErrorCode::IoFailure, "cannot write " + path.string());
    }

    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return fail(ErrorCode::IoFailure,
                    "cannot replace " + path.string() + ": " + ec.message());
    }

    return {};
}

Result<std::filesystem::path> resolveUnder(const std::filesystem::path& root,
                                           const std::filesystem::path& relative) {
    if (relative.empty())
        return fail(ErrorCode::InvalidArgument, "empty path under " + root.string());
    if (relative.is_absolute())
        return fail(ErrorCode::InvalidArgument,
                    "absolute path not allowed under " + root.string() + ": " +
                        relative.string());

    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec)
        return fail(ErrorCode::IoFailure, "cannot resolve root " + root.string());

    auto candidate = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec)
        return fail(ErrorCode::IoFailure,
                    "cannot resolve " + relative.string() + " under " + root.string());

    // Compare by path elements rather than by string prefix: a string compare
    // would accept "/data-other" as being under "/data".
    auto rootIt = canonicalRoot.begin();
    auto candidateIt = candidate.begin();
    for (; rootIt != canonicalRoot.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == candidate.end() || *candidateIt != *rootIt)
            return fail(ErrorCode::InvalidArgument,
                        relative.string() + " escapes " + root.string());
    }

    return candidate;  // not const: a const local cannot be moved out
}

}  // namespace uta::fs

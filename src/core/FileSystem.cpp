#include "core/FileSystem.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <optional>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace uta::fs {
namespace {

/// The largest file readFile will read into memory. Core reads whole files,
/// so a cap is what turns "too big" into a reported error rather than an
/// allocation failure or a 32-bit truncation. Well above any package or
/// bundle this engine expects, and deliberately far below address-space size.
constexpr std::uintmax_t kMaxReadBytes = 4ULL * 1024 * 1024 * 1024;

/// Identifies this process in a temporary filename, so two processes writing
/// the same destination cannot choose the same name.
[[nodiscard]] std::string processTag() {
#ifdef _WIN32
    return std::to_string(_getpid());
#else
    return std::to_string(::getpid());
#endif
}

/// Push a written file to the device. Best effort: a platform that cannot do
/// it still gets the rename, which is strictly better than not writing.
void syncToDevice(std::FILE* file) noexcept {
    if (std::fflush(file) != 0) return;
#ifdef _WIN32
    (void)_commit(_fileno(file));
#else
    (void)::fsync(::fileno(file));
#endif
}

/// An environment variable, or nullopt when unset or empty. An empty value is
/// treated as unset: XDG says a relative or empty value must be ignored, and
/// an empty %APPDATA% is no more usable than a missing one.
[[nodiscard]] std::optional<std::filesystem::path> envPath(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;

    // A relative value is ignored, which XDG requires and INV-8 needs: a
    // relative HOME would otherwise make the fallback relative to the current
    // working directory, which is exactly what that invariant rules out.
    std::filesystem::path candidate(value);
    if (!candidate.is_absolute()) return std::nullopt;
    return candidate;
}

/// Open a file by its NATIVE path.
///
/// Never path::string(): that is UTF-8 on Windows while fopen decodes the
/// process ANSI code page, so any non-ASCII path fails to open -- %APPDATA%
/// for a user whose profile name is not ASCII, among others. It can also throw
/// on an unconvertible path, which would escape uta::fs against INV-3.
/// path::c_str() is already the native encoding on both platforms.
[[nodiscard]] std::FILE* openNative(const std::filesystem::path& path,
                                    const char* mode) noexcept {
#ifdef _WIN32
    const std::wstring wide(mode, mode + std::char_traits<char>::length(mode));
    return _wfopen(path.c_str(), wide.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

/// Map errno to a code chosen for the failure. One code standing for every
/// cause does not satisfy INV-1.
[[nodiscard]] ErrorCode codeForErrno(int e) noexcept {
    switch (e) {
    case EACCES:
    case EPERM:  return ErrorCode::PermissionDenied;
    case ENOENT: return ErrorCode::NotFound;
    case EEXIST: return ErrorCode::AlreadyExists;
    case EISDIR: return ErrorCode::InvalidArgument;
    case ENOMEM: return ErrorCode::OutOfMemory;
    default:     return ErrorCode::IoFailure;
    }
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

    // Refuse a file too large to address before ALLOCATING for it. The size
    // is attacker-influenced wherever the path came through resolveUnder, and
    // an unguarded vector here throws bad_alloc straight out of uta::fs
    // against INV-3. A 32-bit build would otherwise truncate the cast and the
    // short-read guard below would pass on silently truncated content.
    if (size > static_cast<std::uintmax_t>(kMaxReadBytes))
        return fail(ErrorCode::InvalidArgument,
                    "file is larger than core will read: " + path.string());

    std::vector<std::byte> bytes;
    try {
        bytes.resize(static_cast<std::size_t>(size));
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::OutOfMemory, "cannot allocate to read " + path.string());
    }

    // The error_code overloads above keep std::filesystem from throwing;
    // stdio keeps the read itself from throwing too (INV-3).
    errno = 0;
    std::FILE* file = openNative(path, "rb");
    if (file == nullptr)
        return fail(codeForErrno(errno), "cannot open " + path.string());

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

    // The temporary name must be unique across PROCESSES, not just threads:
    // design.md rule 16 has both runtime targets shell out to ut-bake, and a
    // client and a dedicated server share one cache directory. A per-process
    // counter alone gives two processes the same name, and "wb" truncates --
    // so both would interleave into one file and each rename it into place,
    // producing a corrupt destination reported as SUCCESS.
    //
    // So: counter, process id, and an exclusive create that fails rather than
    // truncating. "x" is C11 and is the portable spelling of O_EXCL.
    static std::atomic<unsigned long long> counter{0};
    std::FILE* file = nullptr;
    std::filesystem::path temporary;
    for (int attempt = 0; attempt < 64 && file == nullptr; ++attempt) {
        temporary = parent / (path.filename().string() + ".tmp-" + processTag() + "-" +
                              std::to_string(counter.fetch_add(1, std::memory_order_relaxed)));
        errno = 0;
        file = openNative(temporary, "wbx");
        if (file == nullptr && errno != EEXIST)
            return fail(codeForErrno(errno),
                        "cannot create a temporary beside " + path.string());
    }
    if (file == nullptr)
        return fail(ErrorCode::AlreadyExists,
                    "no free temporary name beside " + path.string());

    // From here every failure path unlinks the temporary before returning.
    // That is INV-7, and it is why this is not written as early returns.
    const std::size_t written =
        bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), file);
    const bool writeFailed = written != bytes.size() || std::ferror(file) != 0;

    // Flush to the DEVICE before the rename. fclose only reaches the page
    // cache, so a power loss could commit the rename while the data blocks
    // were still unwritten -- leaving the destination empty and the old bytes
    // gone, which is the opposite of what this function promises.
    if (!writeFailed) syncToDevice(file);
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

    // weakly_canonical resolves only the leading elements that EXIST, and it
    // tests existence with status(), which follows links. So a symlink whose
    // target does not exist reads as not_found and its own name is appended
    // UNRESOLVED -- it passes the containment check above while still pointing
    // wherever it points. A caller that then creates through it writes outside
    // root, which is the one thing this function exists to prevent.
    //
    // writeFileAtomically happens to be immune because it renames over the
    // name rather than opening through it, but the promise is made to every
    // caller, and readFile does open through it.
    if (std::filesystem::symlink_status(candidate, ec).type() ==
            std::filesystem::file_type::symlink &&
        std::filesystem::status(candidate, ec).type() ==
            std::filesystem::file_type::not_found) {
        return fail(ErrorCode::InvalidArgument,
                    relative.string() + " is an unresolved symlink under " + root.string());
    }

    return candidate;  // not const: a const local cannot be moved out
}

}  // namespace uta::fs

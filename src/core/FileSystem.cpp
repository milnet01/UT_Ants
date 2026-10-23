#include "core/FileSystem.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <optional>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace uta::fs {
namespace {

/// The largest file readFile will read into memory. Core reads whole files,
/// so a cap is what turns "too big" into a reported error rather than an
/// allocation failure or a 32-bit truncation. Well above any package or
/// bundle this engine expects, and deliberately far below address-space size.
/// Never above what a std::size_t holds: on a 32-bit build 4 GiB itself would
/// cast to 0 and pass the short-read guard on nothing (UTA-0229).
constexpr std::uintmax_t kMaxReadBytes =
    std::min<std::uintmax_t>(4ULL * 1024 * 1024 * 1024, std::numeric_limits<std::size_t>::max());

/// Identifies this process in a temporary filename, so two processes writing
/// the same destination cannot choose the same name.
[[nodiscard]] std::string processTag() {
#ifdef _WIN32
    return std::to_string(_getpid());
#else
    return std::to_string(::getpid());
#endif
}

/// Push a flushed file to the device, returning errno on failure and 0 on
/// success, for detail::syncErrorRefuses to judge.
[[nodiscard]] int syncToDevice(std::FILE* file) noexcept {
#ifdef _WIN32
    if (_commit(_fileno(file)) != 0) return errno;
#else
    if (::fsync(::fileno(file)) != 0) return errno;
#endif
    return 0;
}

/// An environment variable, or nullopt when unset or empty. An empty value is
/// treated as unset: XDG says a relative or empty value must be ignored, and
/// an empty %APPDATA% is no more usable than a missing one.
[[nodiscard]] std::optional<std::filesystem::path> envPath(const char* name) {
#ifdef _WIN32
    // The wide variable: getenv narrows to the ANSI code page, so a profile
    // path outside it -- a non-ASCII user name -- arrived garbled (UTA-0229).
    const std::wstring wideName(name, name + std::char_traits<char>::length(name));
    const wchar_t* value = _wgetenv(wideName.c_str());
    if (value == nullptr || *value == L'\0') return std::nullopt;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
#endif

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

}  // namespace

bool detail::syncErrorRefuses(int error) noexcept {
    switch (error) {
    case EIO:
    case ENOSPC:
#ifdef EDQUOT
    case EDQUOT:
#endif
        return true;
    default:
        return false;
    }
}

namespace {

/// Map errno to a code chosen for the failure. One code standing for every
/// cause does not satisfy INV-1. Lives in Error.h because fileSink reports an
/// fopen failure through the same rule, and two copies would disagree.
using uta::errorCodeFromErrno;

/// ASCII only, and deliberately not std::toupper: that one is locale
/// dependent, and a name's safety must not depend on the machine's locale.
[[nodiscard]] constexpr char upperAscii(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

/// A Win32 reserved device name resolves to a device in ANY directory, and
/// does so with an extension too -- so COM1.txt is the same device. The test
/// is on the component's text up to its first '.', case-insensitively.
[[nodiscard]] bool isReservedDeviceName(std::string_view component) noexcept {
    static constexpr std::string_view kReserved[] = {
        "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$",
        "COM0", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT0", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
        // Windows reads Latin-1's superscript digits as digits here, so these
        // are devices too -- written as UTF-8, as the component arrives.
        "COM\xC2\xB9", "COM\xC2\xB2", "COM\xC2\xB3", "LPT\xC2\xB9", "LPT\xC2\xB2", "LPT\xC2\xB3",
    };

    const std::string_view stem = component.substr(0, component.find('.'));
    for (const std::string_view reserved : kReserved) {
        if (stem.size() != reserved.size()) continue;
        bool same = true;
        for (std::size_t i = 0; i < stem.size(); ++i) {
            if (upperAscii(stem[i]) != reserved[i]) {
                same = false;
                break;
            }
        }
        if (same) return true;
    }
    return false;
}

/// INV-15's lexical pass, over `relative`'s own components and BEFORE any
/// filesystem call. Running it after weakly_canonical would be a different
/// rule: canonicalisation erases a barred component that a later ".." undoes,
/// so "COM1/../safe.unr" would reach the filesystem unexamined.
///
/// Enforced on every platform, not only Windows. A rule holding on one and
/// not the other means a Linux server and a Windows client disagree about
/// which content is safe, and unet moves content between exactly those; and a
/// lexical rule is testable on all three legs, where a Windows-only one is
/// checked by one leg and by no local run at all.
[[nodiscard]] Result<void> checkComponentNames(const std::filesystem::path& root,
                                               const std::filesystem::path& relative) {
    const auto refuse = [&](const std::string& component, const char* why) {
        return fail(ErrorCode::InvalidArgument,
                    "component \"" + component + "\" of " + utf8(relative) +
                        " " + why + ", under " + utf8(root));
    };

    for (const std::filesystem::path& part : relative) {
        // As UTF-8 on every platform: string() is the ANSI code page on
        // Windows, where the superscript device names would never match.
        const std::string component = utf8(part);

        // An empty component is a trailing separator; "." and ".." are
        // navigation, and ".." would otherwise trip the trailing-dot rule.
        // Containment, not naming, is what answers ".." -- INV-6's job.
        if (component.empty() || component == "." || component == "..") continue;

        // On Win32 "a.txt:s" writes an alternate data stream, which an
        // extension check cannot see, and "C:foo" is drive-relative rather
        // than absolute, so is_absolute() does not catch it.
        if (component.find(':') != std::string::npos)
            return refuse(component, "contains a colon");

        // Win32 strips both, so "a." and "a" are one file after the check
        // has passed on two names.
        if (component.back() == '.' || component.back() == ' ')
            return refuse(component, "ends in a dot or a space");

        if (isReservedDeviceName(component))
            return refuse(component, "is a reserved device name");
    }

    return {};
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

Result<std::filesystem::path> dataDirectory() {
    const auto appData = envPath("APPDATA");
    if (!appData) return std::unexpected(missing("data"));
    return *appData / "UT_Ants" / "data";
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

Result<std::filesystem::path> dataDirectory() {
    return xdgDirectory("XDG_DATA_HOME", ".local/share", "data");
}

Result<std::filesystem::path> cacheDirectory() {
    return xdgDirectory("XDG_CACHE_HOME", ".cache", "cache");
}

Result<std::filesystem::path> logDirectory() {
    return xdgDirectory("XDG_STATE_HOME", ".local/state", "log");
}

#endif

namespace {

/// The checks readFile and MappedFile::open both make before opening. A FIFO
/// or a device would block the open, or hand back what is not a file's bytes,
/// so anything but a regular file is InvalidArgument (UTA-0229).
[[nodiscard]] Result<void> checkReadable(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    // Read the TYPE before the error_code. libstdc++ sets ec to ENOENT for a
    // missing file even though it also reports file_type::not_found, so an
    // `if (ec)` first would answer IoFailure for every absent path.
    if (status.type() == std::filesystem::file_type::not_found)
        return fail(ErrorCode::NotFound, "no such file: " + utf8(path));
    if (ec) return fail(ErrorCode::IoFailure, "cannot stat " + utf8(path) + ": " + ec.message());
    if (status.type() == std::filesystem::file_type::directory)
        return fail(ErrorCode::InvalidArgument, utf8(path) + " is a directory, not a file");
    if (status.type() != std::filesystem::file_type::regular)
        return fail(ErrorCode::InvalidArgument, utf8(path) + " is not a regular file");
    return {};
}

#ifndef _WIN32
/// An open descriptor and the size fstat gave it.
struct OpenRegular {
    int fd;
    std::uintmax_t size;
};

/// Open `path` read-only, refusing anything but a regular file. The check
/// above can be outrun: a FIFO swapped in after it would block a plain open.
/// O_NONBLOCK keeps the open from blocking, and S_ISREG on the open
/// descriptor refuses what was swapped in (review-code 2026-10-15, UTA-0407).
/// A regular file reads the same either way. The caller closes `fd`.
[[nodiscard]] Result<OpenRegular> openRegular(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        const int error = errno;
        return fail(errorCodeFromErrno(error), "cannot open " + utf8(path) + ": " + std::strerror(error));
    }
    struct stat info {};
    if (::fstat(fd, &info) != 0) {
        ::close(fd);
        return fail(ErrorCode::IoFailure, "cannot stat " + utf8(path));
    }
    if (!S_ISREG(info.st_mode)) {
        ::close(fd);
        return fail(ErrorCode::InvalidArgument, utf8(path) + " is not a regular file");
    }
    return OpenRegular{fd, static_cast<std::uintmax_t>(info.st_size)};
}
#endif

}  // namespace

Result<MappedFile> MappedFile::open(const std::filesystem::path& path) {
    UTA_CHECK(checkReadable(path));

    MappedFile mapped;
#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        const ErrorCode code = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ErrorCode::NotFound
                               : error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION
                                   ? ErrorCode::PermissionDenied
                                   : ErrorCode::IoFailure;
        return fail(code, "cannot open " + utf8(path));
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        return fail(ErrorCode::IoFailure, "cannot size " + utf8(path));
    }
    if (size.QuadPart == 0) {
        CloseHandle(file);
        return mapped; // an empty file maps to nothing; the view is empty
    }
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(file); // the mapping holds the file open
    if (mapping == nullptr) return fail(ErrorCode::IoFailure, "cannot map " + utf8(path));
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        return fail(ErrorCode::IoFailure, "cannot map " + utf8(path));
    }
    mapped.mapping_ = mapping;
    mapped.data_ = static_cast<const std::byte*>(view);
    mapped.size_ = static_cast<std::size_t>(size.QuadPart);
#else
    UTA_TRY(const OpenRegular file, openRegular(path));
    const int fd = file.fd;
    if (file.size > std::numeric_limits<std::size_t>::max()) {
        ::close(fd);
        return fail(ErrorCode::InvalidArgument, "file is larger than this build can map: " + utf8(path));
    }
    const auto size = static_cast<std::size_t>(file.size);
    if (size == 0) {
        ::close(fd);
        return mapped; // mmap refuses a length of 0; the view is empty
    }
    const void* view = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd); // the mapping holds its own reference
    if (view == MAP_FAILED) return fail(ErrorCode::IoFailure, "cannot map " + utf8(path) + ": " + std::strerror(errno));
    mapped.data_ = static_cast<const std::byte*>(view);
    mapped.size_ = size;
#endif
    return mapped;
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0))
#ifdef _WIN32
      , mapping_(std::exchange(other.mapping_, nullptr))
#endif
{
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        release();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
#ifdef _WIN32
        mapping_ = std::exchange(other.mapping_, nullptr);
#endif
    }
    return *this;
}

MappedFile::~MappedFile() { release(); }

void MappedFile::release() noexcept {
    if (data_ == nullptr) return;
#ifdef _WIN32
    UnmapViewOfFile(data_);
    CloseHandle(mapping_);
    mapping_ = nullptr;
#else
    ::munmap(const_cast<std::byte*>(data_), size_);
#endif
    data_ = nullptr;
    size_ = 0;
}

Result<std::FILE*> detail::openForReading(const std::filesystem::path& path) {
#ifdef _WIN32
    errno = 0;
    std::FILE* file = openNative(path, "rb");
    if (file == nullptr) return fail(errorCodeFromErrno(errno), "cannot open " + utf8(path));
    return file;
#else
    UTA_TRY(const OpenRegular opened, openRegular(path));
    const int fd = opened.fd;
    std::FILE* file = ::fdopen(fd, "rb");
    if (file == nullptr) {
        const int error = errno;
        ::close(fd);
        return fail(errorCodeFromErrno(error), "cannot open " + utf8(path) + ": " + std::strerror(error));
    }
    return file;
#endif
}

Result<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    UTA_CHECK(checkReadable(path));

    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return fail(ErrorCode::IoFailure, "cannot size " + utf8(path) + ": " + ec.message());

    // Refuse a file too large to address before ALLOCATING for it. The size
    // is attacker-influenced wherever the path came through resolveUnder, and
    // an unguarded vector here throws bad_alloc straight out of uta::fs
    // against INV-3. A 32-bit build would otherwise truncate the cast and the
    // short-read guard below would pass on silently truncated content.
    if (size > static_cast<std::uintmax_t>(kMaxReadBytes))
        return fail(ErrorCode::InvalidArgument,
                    "file is larger than core will read: " + utf8(path));

    std::vector<std::byte> bytes;
    try {
        bytes.resize(static_cast<std::size_t>(size));
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::OutOfMemory, "cannot allocate to read " + utf8(path));
    }

    // The error_code overloads above keep std::filesystem from throwing;
    // stdio keeps the read itself from throwing too (INV-3).
    UTA_TRY(std::FILE* file, detail::openForReading(path));

    const std::size_t read =
        bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
    // The size was read before the open, so a file that grew in between
    // still has bytes left: that is a short read too (UTA-0370).
    const bool grew = read == bytes.size() && std::fgetc(file) != EOF;
    const bool failed = std::ferror(file) != 0;
    (void)std::fclose(file);  // read path: nothing to flush, nothing to report

    if (failed || grew || read != bytes.size())
        return fail(ErrorCode::IoFailure, "short read on " + utf8(path));

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
        // Appended to the native name, never through path::string(), which
        // narrows to the ANSI code page on Windows (UTA-0229).
        std::filesystem::path name = path.filename();
        name += ".tmp-" + processTag() + "-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
        temporary = parent / name;
        errno = 0;
        file = openNative(temporary, "wbx");
        if (file == nullptr && errno != EEXIST)
            return fail(errorCodeFromErrno(errno),
                        "cannot create a temporary beside " + utf8(path));
    }
    if (file == nullptr)
        return fail(ErrorCode::AlreadyExists,
                    "no free temporary name beside " + utf8(path));

    // From here every failure return must append discard() below, which
    // unlinks the temporary (INV-7). Nothing does it for a new one.
    const std::size_t written =
        bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), file);
    bool writeFailed = written != bytes.size() || std::ferror(file) != 0;

    // Flush to the DEVICE before the rename. fclose only reaches the page
    // cache, so a power loss could commit the rename while the data blocks
    // were still unwritten -- leaving the destination empty and the old bytes
    // gone, which is the opposite of what this function promises. A device
    // error there refuses the save and keeps the old file (UTA-0229).
    // A failed flush is the write's own failure, whatever errno says.
    if (!writeFailed) writeFailed = std::fflush(file) != 0 || detail::syncErrorRefuses(syncToDevice(file));
    const bool closeFailed = std::fclose(file) != 0;

    std::error_code ec;
    // INV-7 unlinks the temporary on every failure; one that will not go is
    // named, rather than left behind unmentioned (UTA-0229).
    const auto discard = [&temporary]() -> std::string {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        return removeError ? "; the temporary " + utf8(temporary) + " could not be removed: " + removeError.message()
                           : std::string();
    };
    if (writeFailed || closeFailed) return fail(ErrorCode::IoFailure, "cannot write " + utf8(path) + discard());

    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        const std::string why = ec.message();
        return fail(ErrorCode::IoFailure, "cannot replace " + utf8(path) + ": " + why + discard());
    }

#ifndef _WIN32
    // The rename is an entry in the directory, which reaches the device only
    // when the directory itself is synced. Best effort: the new bytes are in
    // place either way, and a crash before this keeps the old file whole.
    if (const int dir = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); dir >= 0) {
        (void)::fsync(dir);
        ::close(dir);
    }
#endif
    return {};
}

Result<std::filesystem::path> resolveUnder(const std::filesystem::path& root,
                                           const std::filesystem::path& relative) {
    if (relative.empty())
        return fail(ErrorCode::InvalidArgument, "empty path under " + utf8(root));
    if (relative.is_absolute())
        return fail(ErrorCode::InvalidArgument,
                    "absolute path not allowed under " + utf8(root) + ": " +
                        utf8(relative));

    // Before any filesystem call: INV-15. See checkComponentNames for why the
    // order is load-bearing rather than tidy.
    UTA_CHECK(checkComponentNames(root, relative));

    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec)
        return fail(ErrorCode::IoFailure, "cannot resolve root " + utf8(root));

    auto candidate = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec)
        return fail(ErrorCode::IoFailure,
                    "cannot resolve " + utf8(relative) + " under " + utf8(root));

    // Compare by path elements rather than by string prefix: a string compare
    // would accept "/data-other" as being under "/data".
    //
    // An EMPTY element is skipped rather than compared: a root spelled with a
    // trailing separator yields one, and whether weakly_canonical keeps it
    // differs between standard libraries -- measured, libstdc++ 16.2 strips
    // it. Comparing it would refuse such a root on whichever library keeps
    // it, which INV-6 forbids on every leg.
    auto rootIt = canonicalRoot.begin();
    auto candidateIt = candidate.begin();
    for (; rootIt != canonicalRoot.end(); ++rootIt) {
        if (rootIt->empty()) continue;
        if (candidateIt == candidate.end() || *candidateIt != *rootIt)
            return fail(ErrorCode::InvalidArgument,
                        utf8(relative) + " escapes " + utf8(root));
        ++candidateIt;
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
    //
    // EVERY element below root, not only the last (UTA-0229): a dangling link
    // earlier in the path stops the resolution there, and the rest is appended
    // after it. Any link still present was one weakly_canonical could not
    // resolve, since it resolves every link whose target exists.
    std::filesystem::path prefix;
    for (auto it = candidate.begin(); it != candidateIt; ++it) prefix /= *it;
    for (; candidateIt != candidate.end(); ++candidateIt) {
        prefix /= *candidateIt;
        if (std::filesystem::symlink_status(prefix, ec).type() == std::filesystem::file_type::symlink)
            return fail(ErrorCode::InvalidArgument,
                        utf8(relative) + " passes an unresolved symlink under " + utf8(root));
    }

    return candidate;  // not const: a const local cannot be moved out
}

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path pathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

}  // namespace uta::fs

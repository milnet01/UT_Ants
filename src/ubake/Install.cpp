// An install, its resolver and the install check --
// docs/specs/UTA-0011-map-baker.md SS 4.2 and SS 4.9.

#include "ubake/Install.h"

#include "core/FileSystem.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <system_error>
#include <utility>

namespace uta::ubake {

namespace fsys = std::filesystem; // `fs` is uta::fs, core's file I/O

namespace {

/// One directory of SS 3 decision 6's search order, both parts folded. Read
/// from the reference install's System/Default.ini, whose `[Core.System]`
/// section lists `Paths` in this order.
struct SearchDirectory {
    std::string_view directory;
    std::string_view extension;
};

constexpr std::array<SearchDirectory, 5> SEARCH_ORDER = {{
    {"system", ".u"},
    {"maps", ".unr"},
    {"textures", ".utx"},
    {"sounds", ".uax"},
    {"music", ".umx"},
}};

/// The packages SS 4.9 requires, spelled as a player's install spells them.
constexpr std::array<std::string_view, 3> REQUIRED = {"Core", "Engine", "Botpack"};

/// The entries of `directory` that `keep` accepts, sorted bytewise by file
/// name so "the first" names the same file on every run and every platform.
template <class Keep>
Result<std::vector<fsys::path>> listSorted(const fsys::path& directory, Keep keep) {
    std::vector<fsys::path> out;
    std::error_code ec;
    fsys::directory_iterator entry(directory, ec);
    for (; !ec && entry != fsys::directory_iterator{}; entry.increment(ec)) {
        if (keep(*entry)) out.push_back(entry->path());
    }
    if (ec)
        return fail(ErrorCode::IoFailure,
                    "cannot list " + detail::utf8(directory) + ": " + ec.message());
    std::sort(out.begin(), out.end(), [](const fsys::path& a, const fsys::path& b) {
        return detail::utf8(a.filename()) < detail::utf8(b.filename());
    });
    return out;
}

} // namespace

namespace detail {

std::string fold(std::string_view text) {
    std::string out{text};
    for (char& character : out) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    return out;
}

std::string utf8(const fsys::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

} // namespace detail

struct Install::State {
    fsys::path root;
    /// Folded package name to the file SS 4.2's order finds for it.
    std::map<std::string, fsys::path> paths;
    /// Every map below is node-based, so a Package's view of its bytes and a
    /// pointer the resolver handed out both survive later insertions.
    std::map<std::string, std::vector<std::byte>> bytes;
    std::map<std::string, upkg::Package> opened;
    /// Names whose file did not read or did not open, so each is tried once.
    std::set<std::string> failed;
};

Install::Install(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
Install::Install(Install&&) noexcept = default;
Install& Install::operator=(Install&&) noexcept = default;
Install::~Install() = default;

Result<Install> Install::open(const fsys::path& root) {
    std::error_code ec;
    if (!fsys::is_directory(root, ec))
        return fail(ErrorCode::NotFound, detail::utf8(root) + " is not a directory");

    auto state = std::make_unique<State>();
    state->root = root;

    UTA_TRY(const std::vector<fsys::path> children,
            listSorted(root, [](const fsys::directory_entry& entry) {
                std::error_code ignored;
                return entry.is_directory(ignored);
            }));

    for (const SearchDirectory& search : SEARCH_ORDER) {
        for (const fsys::path& child : children) {
            if (detail::fold(detail::utf8(child.filename())) != search.directory) continue;
            UTA_TRY(const std::vector<fsys::path> files,
                    listSorted(child, [&search](const fsys::directory_entry& entry) {
                        std::error_code ignored;
                        return entry.is_regular_file(ignored)
                               && detail::fold(detail::utf8(entry.path().extension()))
                                      == search.extension;
                    }));
            // emplace, never assign: the first file to claim a name keeps it,
            // which is both "an earlier directory shadows a later one" and
            // "the first by bytewise file name" (SS 4.2).
            for (const fsys::path& file : files)
                state->paths.emplace(detail::fold(detail::utf8(file.stem())), file);
        }
    }
    return Install(std::move(state));
}

upkg::PackageResolver Install::resolver() {
    State* const state = state_.get();
    return [state](std::string_view packageName) -> Result<const upkg::Package*> {
        const std::string key = detail::fold(packageName);
        if (const auto opened = state->opened.find(key); opened != state->opened.end())
            return &opened->second;
        const auto path = state->paths.find(key);
        // Absent is an ordinary answer, not an error (upkg/Class.h).
        if (path == state->paths.end() || state->failed.contains(key)) return nullptr;

        auto read = uta::fs::readFile(path->second);
        if (!read.has_value()) {
            state->failed.insert(key);
            return nullptr;
        }
        const std::vector<std::byte>& stored =
            state->bytes.emplace(key, std::move(*read)).first->second;
        auto package = upkg::Package::open(stored);
        if (!package.has_value()) {
            state->failed.insert(key);
            state->bytes.erase(key);
            return nullptr;
        }
        return &state->opened.emplace(key, std::move(*package)).first->second;
    };
}

fsys::path Install::pathOf(std::string_view packageName) const {
    const auto found = state_->paths.find(detail::fold(packageName));
    return found == state_->paths.end() ? fsys::path{} : found->second;
}

std::span<const std::byte> Install::bytesOf(std::string_view packageName) const {
    const std::string key = detail::fold(packageName);
    if (!state_->opened.contains(key)) return {};
    const auto found = state_->bytes.find(key);
    return found == state_->bytes.end() ? std::span<const std::byte>{} : found->second;
}

const fsys::path& Install::root() const noexcept {
    return state_->root;
}

CheckReport checkInstall(const fsys::path& root) {
    CheckReport report;
    const auto install = Install::open(root);
    if (!install.has_value()) {
        report.problems.push_back({detail::utf8(root), std::string(install.error().message())});
    } else {
        for (const std::string_view name : REQUIRED) {
            const fsys::path path = install->pathOf(name);
            if (path.empty()) {
                const std::string expected = "System/" + std::string(name) + ".u";
                report.problems.push_back(
                    {expected, expected + " is missing, so this is not an Unreal Tournament install."});
                continue;
            }
            // Read and opened here rather than through the resolver, because
            // the resolver folds a refusal into "absent" and SS 4.9 wants
            // upkg's own message.
            const auto bytes = uta::fs::readFile(path);
            if (!bytes.has_value()) {
                report.problems.push_back({detail::utf8(path), std::string(bytes.error().message())});
                continue;
            }
            const auto package = upkg::Package::open(*bytes);
            if (!package.has_value())
                report.problems.push_back(
                    {detail::utf8(path), std::string(package.error().message())});
        }
    }
    report.ok = report.problems.empty();
    return report;
}

} // namespace uta::ubake

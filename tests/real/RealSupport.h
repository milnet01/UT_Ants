// What more than one of the real-asset tier's files needs -- UTA-0103.
//
// Header-only, and included by every file of the tier, which is also what puts
// UTA_UT_INSTALL_DIR's guard in front of each of them.

#pragma once

#include "core/FileSystem.h"
#include "upkg/Class.h"
#include "upkg/Package.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef UTA_UT_INSTALL_DIR
#error "UTA_UT_INSTALL_DIR must be defined for the real-asset tier"
#endif

namespace uta::test::real {

/// Read a file whole. The Package holds a VIEW of these bytes, so the caller
/// keeps them alive.
inline std::vector<std::byte> readWhole(const std::filesystem::path& path) {
    // One read through uta::fs, not a character-at-a-time stream: the stream
    // was most of this tier's CPU (UTA-0095).
    auto bytes = uta::fs::readFile(path);
    return bytes.has_value() ? std::move(*bytes) : std::vector<std::byte>{};
}

inline std::span<const std::byte> viewOf(const std::vector<std::byte>& raw) {
    return std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(raw.data()), raw.size()};
}

inline bool isPackageExtension(const std::string& extension) {
    return extension == ".unr" || extension == ".utx" || extension == ".uax" ||
           extension == ".umx" || extension == ".u";
}

inline bool isClassExport(const uta::upkg::ExportEntry& entry) {
    return entry.objectClass.kind() == uta::upkg::ObjectReferenceKind::Null &&
           entry.serialSize > 0;
}

inline std::string foldCase(std::string_view value) {
    std::string folded{value};
    for (char& character : folded) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return folded;
}

/// The package files with `extension` directly under `directory`, sorted, so
/// "the first copy" names the same one on every run.
inline std::vector<std::filesystem::path> sortedPackages(const std::filesystem::path& directory,
                                                         std::string_view extension) {
    std::vector<std::filesystem::path> out;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file() && entry.path().extension() == extension)
            out.push_back(entry.path());
    std::ranges::sort(out);
    return out;
}

/// An install's System/*.u, keyed by folded stem, each opened on first use and
/// kept. The resolver owns the lifetime of what it returns, which is the
/// bargain UTA-0005 SS 4.6 states; these maps are that ownership. It was
/// written out in three cases until UTA-0103 shared it.
///
/// Node-based maps, so a Package's view of its bytes and a pointer the
/// resolver handed out both survive later insertions.
class SystemPackages {
public:
    explicit SystemPackages(const std::filesystem::path& system) {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(system)) {
            if (entry.is_regular_file() && entry.path().extension() == ".u") {
                paths_.emplace(foldCase(entry.path().stem().string()), entry.path());
            }
        }
    }
    SystemPackages(const SystemPackages&) = delete;
    SystemPackages& operator=(const SystemPackages&) = delete;

    [[nodiscard]] bool empty() const noexcept { return paths_.empty(); }
    [[nodiscard]] const std::map<std::string, std::filesystem::path>& paths() const noexcept {
        return paths_;
    }

    [[nodiscard]] uta::upkg::PackageResolver resolver() {
        return [this](std::string_view name) -> uta::Result<const uta::upkg::Package*> {
            const std::string key{name}; // already folded by the caller
            if (const auto cached = opened_.find(key); cached != opened_.end()) {
                return &cached->second;
            }
            const auto path = paths_.find(key);
            if (path == paths_.end()) {
                return nullptr; // not present: an ordinary case, not an error
            }
            // UTA-0144: mapped, not copied, so a run over many packages
            // holds only the pages it reads.
            auto mapped = uta::fs::MappedFile::open(path->second);
            if (!mapped.has_value()) return nullptr;
            const uta::fs::MappedFile& raw = bytes_.emplace(key, std::move(*mapped)).first->second;
            auto package = uta::upkg::Package::open(raw.bytes());
            if (!package.has_value()) {
                return nullptr;
            }
            return &opened_.emplace(key, std::move(*package)).first->second;
        };
    }

private:
    std::map<std::string, std::filesystem::path> paths_;
    std::map<std::string, uta::fs::MappedFile> bytes_;
    std::map<std::string, uta::upkg::Package> opened_;
};

} // namespace uta::test::real

// The game types an install registers -- UTA-0179.

#include "GameTypes.h"

#include "core/FileSystem.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string_view>
#include <system_error>
#include <variant>

namespace uta::ubake {

namespace fsys = std::filesystem; // `fs` is uta::fs, core's file I/O

namespace {

using detail::fold;
using detail::utf8;

/// The child of `parent` whose name folds to `folded`, or empty.
fsys::path childFolded(const fsys::path& parent, std::string_view folded, bool directory) {
    std::vector<fsys::path> matches;
    std::error_code ec;
    for (fsys::directory_iterator entry(parent, ec), end; !ec && entry != end; entry.increment(ec)) {
        std::error_code ignored;
        const bool kind = directory ? entry->is_directory(ignored) : entry->is_regular_file(ignored);
        if (kind && fold(utf8(entry->path().filename())) == folded) matches.push_back(entry->path());
    }
    // Two spellings of one name: the first by bytewise file name, as SS 4.2 picks.
    std::ranges::sort(matches, {}, [](const fsys::path& path) { return utf8(path.filename()); });
    return matches.empty() ? fsys::path() : matches.front();
}

/// Every `*.int` in `directory`, sorted bytewise by file name.
std::vector<fsys::path> intFiles(const fsys::path& directory) {
    std::vector<fsys::path> files;
    if (directory.empty()) return files;
    std::error_code ec;
    for (fsys::directory_iterator entry(directory, ec), end; !ec && entry != end; entry.increment(ec)) {
        std::error_code ignored;
        if (entry->is_regular_file(ignored) && fold(utf8(entry->path().extension())) == ".int")
            files.push_back(entry->path());
    }
    std::ranges::sort(files, {}, [](const fsys::path& path) { return utf8(path.filename()); });
    return files;
}

/// An .int file's text. UTF-16 with a byte order mark keeps each code unit's
/// low byte, which is exact for the ASCII the `Object=` entries are written in.
std::string textOf(const std::vector<std::byte>& bytes) {
    const auto at = [&bytes](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    std::string text;
    if (bytes.size() >= 2 && at(0) == 0xFF && at(1) == 0xFE) {
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) text += at(i + 1) == 0 ? static_cast<char>(at(i)) : '?';
        return text;
    }
    std::size_t start = bytes.size() >= 3 && at(0) == 0xEF && at(1) == 0xBB && at(2) == 0xBF ? 3 : 0;
    for (std::size_t i = start; i < bytes.size(); ++i) text += static_cast<char>(at(i));
    return text;
}

std::string_view trimmed(std::string_view text) {
    const auto space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
    while (!text.empty() && space(text.front())) text.remove_prefix(1);
    while (!text.empty() && space(text.back())) text.remove_suffix(1);
    return text;
}

/// The Name of an `Object=(Name=...,Class=Class,MetaClass=Botpack.TournamentGameInfo)`
/// line, keys and values compared ignoring case; nothing for any other line.
std::optional<std::string> gameTypeOf(std::string_view line) {
    line = trimmed(line);
    const std::string_view head = "object=";
    if (line.size() < head.size() || fold(line.substr(0, head.size())) != head) return std::nullopt;
    line = trimmed(line.substr(head.size()));
    if (!line.starts_with('(') || !line.ends_with(')')) return std::nullopt;
    line = line.substr(1, line.size() - 2);

    std::optional<std::string> name;
    bool isClass = false, isGameType = false;
    while (!line.empty()) {
        const std::size_t comma = line.find(',');
        const std::string_view pair = line.substr(0, comma);
        line = comma == std::string_view::npos ? std::string_view() : line.substr(comma + 1);
        const std::size_t equals = pair.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string key = fold(trimmed(pair.substr(0, equals)));
        const std::string_view value = trimmed(pair.substr(equals + 1));
        if (key == "name") name = std::string(value);
        if (key == "class") isClass = fold(value) == "class";
        if (key == "metaclass") isGameType = fold(value) == "botpack.tournamentgameinfo";
    }
    if (!name || !isClass || !isGameType) return std::nullopt;
    return name;
}

/// The MapPrefix `name`'s class family sets, or nothing when the class is not
/// found. Empty when it is found and no class in the family sets one.
std::optional<std::string> mapPrefixOf(std::string_view name, const upkg::PackageResolver& resolver) {
    const std::size_t dot = name.find('.');
    if (dot == std::string_view::npos) return std::nullopt;
    const auto package = resolver(fold(name.substr(0, dot)));
    if (!package.has_value() || *package == nullptr) return std::nullopt;
    const upkg::ExportEntry* const entry = upkg::findClassExport(**package, name.substr(dot + 1));
    if (entry == nullptr) return std::nullopt;
    const auto ancestry = upkg::readAncestry(**package, *entry, resolver);
    if (!ancestry.has_value()) return std::nullopt;
    const auto defaults = upkg::effectiveDefaults(*ancestry);
    if (!defaults.has_value()) return std::nullopt;
    std::string prefix;
    for (const upkg::EffectiveProperty& effective : *defaults) {
        if (effective.property.arrayIndex != 0 || fold(effective.name) != "mapprefix") continue;
        if (const auto* text = std::get_if<std::string>(&effective.property.value)) prefix = *text;
    }
    return prefix;
}

} // namespace

GameTypes readGameTypes(Install& install) {
    const fsys::path& root = install.root();
    std::vector<fsys::path> files = intFiles(childFolded(root, "system", true));
    const fsys::path localized = childFolded(root, "systemlocalized", true);
    if (!localized.empty()) {
        const std::vector<fsys::path> more = intFiles(childFolded(localized, "int", true));
        files.insert(files.end(), more.begin(), more.end());
    }

    // By folded name: the first spelling read is the one reported.
    std::map<std::string, std::string> names;
    for (const fsys::path& file : files) {
        const auto bytes = fs::readFile(file);
        if (!bytes) continue;
        const std::string text = textOf(*bytes);
        for (std::size_t start = 0; start < text.size();) {
            const std::size_t end = std::min(text.find('\n', start), text.size());
            if (auto name = gameTypeOf(std::string_view(text).substr(start, end - start)))
                names.emplace(fold(*name), std::move(*name));
            start = end + 1;
        }
    }

    GameTypes types;
    const upkg::PackageResolver resolver = install.resolver();
    for (auto& [folded, name] : names) {
        if (auto prefix = mapPrefixOf(name, resolver)) {
            types.found.push_back({std::move(name), std::move(*prefix)});
        } else {
            types.unresolved.push_back(std::move(name));
        }
    }
    return types;
}

} // namespace uta::ubake

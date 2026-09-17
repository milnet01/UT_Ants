// The map launcher's parts that need no window -- UTA-0170.

#include "MapList.h"

#include "core/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <span>
#include <system_error>

namespace uta::client {
namespace {

std::string lowered(std::string_view text) {
    std::string folded(text);
    std::ranges::transform(folded, folded.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return folded;
}

/// The entry of `directory` whose name folds to `name`, or `directory / name`
/// when none does -- an install's directories are matched ignoring case.
std::filesystem::path childIgnoringCase(const std::filesystem::path& directory, std::string_view name) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (lowered(it->path().filename().string()) == name) return it->path();
    }
    return directory / name;
}

// A small reader for ut-bake's one JSON object: enough to walk it and keep the
// top level's string values, which are all SS 4.8 gives a caller to bind to.
class JsonScanner {
public:
    explicit JsonScanner(std::string_view text) : text_(text) {}

    /// The top-level string member named `key`, or nothing.
    std::optional<std::string> topLevelString(std::string_view key) {
        at_ = 0;
        skipSpace();
        if (!take('{')) return std::nullopt;
        while (true) {
            skipSpace();
            if (take('}')) return std::nullopt;
            const std::optional<std::string> name = string();
            if (!name) return std::nullopt;
            skipSpace();
            if (!take(':')) return std::nullopt;
            skipSpace();
            if (peek() == '"') {
                std::optional<std::string> value = string();
                if (!value) return std::nullopt;
                if (*name == key) return value;
            } else if (!skipValue()) {
                return std::nullopt;
            }
            skipSpace();
            if (take(',')) continue;
            return std::nullopt; // a '}' here ends the object without the key
        }
    }

private:
    char peek() const { return at_ < text_.size() ? text_[at_] : '\0'; }
    bool take(char ch) {
        if (peek() != ch) return false;
        ++at_;
        return true;
    }
    void skipSpace() {
        while (at_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[at_]))) ++at_;
    }

    static void appendUtf8(std::string& out, std::uint32_t code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    std::optional<std::string> string() {
        if (!take('"')) return std::nullopt;
        std::string out;
        while (at_ < text_.size()) {
            const char ch = text_[at_++];
            if (ch == '"') return out;
            if (ch != '\\') {
                out += ch;
                continue;
            }
            if (at_ >= text_.size()) return std::nullopt;
            const char escape = text_[at_++];
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (at_ + 4 > text_.size()) return std::nullopt;
                std::uint32_t code = 0;
                for (int i = 0; i < 4; ++i) {
                    const auto digit = static_cast<unsigned char>(text_[at_++]);
                    code <<= 4;
                    if (digit >= '0' && digit <= '9') code |= digit - '0';
                    else if (digit >= 'a' && digit <= 'f') code |= digit - 'a' + 10;
                    else if (digit >= 'A' && digit <= 'F') code |= digit - 'A' + 10;
                    else return std::nullopt;
                }
                appendUtf8(out, code);
                break;
            }
            default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    /// Past one value that is not a string: an object, an array, or a bare word
    /// or number. Strings inside are walked, so a bracket in one is not counted.
    bool skipValue() {
        if (peek() != '{' && peek() != '[') {
            const std::size_t start = at_;
            while (at_ < text_.size() && text_[at_] != ',' && text_[at_] != '}' && text_[at_] != ']' &&
                   !std::isspace(static_cast<unsigned char>(text_[at_])))
                ++at_;
            return at_ > start;
        }
        int depth = 0;
        while (at_ < text_.size()) {
            const char ch = peek();
            if (ch == '"') {
                if (!string()) return false;
                continue;
            }
            ++at_;
            if (ch == '{' || ch == '[') ++depth;
            if (ch == '}' || ch == ']') {
                if (--depth == 0) return true;
            }
        }
        return false;
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

std::filesystem::path fileFor(const std::filesystem::path& directory, std::string_view map) {
    return directory / (std::string(map) + ".txt");
}

Result<void> writeText(const std::filesystem::path& path, std::string_view text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return std::unexpected(Error(ErrorCode::IoFailure,
                                     "cannot make " + path.parent_path().string() + ": " + ec.message()));
    return fs::writeFileAtomically(path, std::as_bytes(std::span(text.data(), text.size())));
}

std::string readText(const std::filesystem::path& path) {
    const auto bytes = fs::readFile(path);
    if (!bytes) return {};
    return {reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

} // namespace

std::vector<MapFile> listMaps(const std::filesystem::path& install) {
    std::vector<MapFile> maps;
    const std::filesystem::path directory = childIgnoringCase(install, "maps");
    std::error_code ec;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path& path = it->path();
        if (lowered(path.extension().string()) != ".unr") continue;
        maps.push_back({path.stem().string(), path});
    }
    std::ranges::sort(maps, [](const MapFile& a, const MapFile& b) {
        const std::string left = lowered(a.name), right = lowered(b.name);
        return left != right ? left < right : a.name < b.name;
    });
    return maps;
}

std::vector<std::size_t> filterMaps(const std::vector<MapFile>& maps, std::string_view filter) {
    const std::string needle = lowered(filter);
    std::vector<std::size_t> kept;
    for (std::size_t i = 0; i < maps.size(); ++i) {
        if (lowered(maps[i].name).find(needle) != std::string::npos) kept.push_back(i);
    }
    return kept;
}

BakeAnswer readBakeAnswer(std::string_view output, int exitCode) {
    JsonScanner json(output);
    const std::optional<std::string> verdict = json.topLevelString("verdict");
    BakeAnswer answer;
    if (!verdict) {
        answer.failure = "ut-bake gave no answer (exit code " + std::to_string(exitCode) + ")";
        return answer;
    }
    if (*verdict == "written" || *verdict == "cached") {
        const std::optional<std::string> path = json.topLevelString("path");
        if (exitCode == 0 && path && !path->empty()) {
            answer.baked = true;
            answer.path = std::filesystem::path(std::u8string(path->begin(), path->end()));
            return answer;
        }
        answer.failure = "ut-bake said " + *verdict + " but gave no bundle (exit code " +
                         std::to_string(exitCode) + ")";
        return answer;
    }
    if (*verdict == "refused") {
        const std::optional<std::string> error = json.topLevelString("error");
        answer.failure = error && !error->empty() ? *error : "refused";
        return answer;
    }
    if (*verdict == "over-budget") {
        answer.failure = "its textures are over the memory budget";
        return answer;
    }
    answer.failure = "ut-bake gave an unknown verdict: " + *verdict;
    return answer;
}

Result<LauncherPaths> launcherPaths() {
    const auto cache = fs::cacheDirectory();
    if (!cache) return std::unexpected(cache.error());
    const auto state = fs::logDirectory();
    if (!state) return std::unexpected(state.error());
    return LauncherPaths{
        .bakes = *cache / "content" / "bakes",
        .notes = *state / "map-notes",
        .results = *state / "map-results",
    };
}

std::optional<MapResult> readResult(const std::filesystem::path& results, std::string_view map) {
    const std::string text = readText(fileFor(results, map));
    if (text.starts_with("baked")) return MapResult{};
    if (!text.starts_with("failed")) return std::nullopt;
    std::string why = text.substr(std::min(text.size(), std::string_view("failed\n").size()));
    while (!why.empty() && (why.back() == '\n' || why.back() == '\r')) why.pop_back();
    return MapResult{.failed = true, .failure = std::move(why)};
}

Result<void> writeResult(const std::filesystem::path& results, std::string_view map, const MapResult& result) {
    return writeText(fileFor(results, map), result.failed ? "failed\n" + result.failure + "\n" : "baked\n");
}

std::string readNotes(const std::filesystem::path& notes, std::string_view map) {
    return readText(fileFor(notes, map));
}

Result<void> writeNotes(const std::filesystem::path& notes, std::string_view map, std::string_view text) {
    const std::filesystem::path path = fileFor(notes, map);
    if (text.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec)
            return std::unexpected(Error(ErrorCode::IoFailure, "cannot remove " + path.string() + ": " + ec.message()));
        return {};
    }
    return writeText(path, text);
}

std::string withoutLastCharacter(std::string text) {
    if (text.empty()) return text;
    std::size_t end = text.size() - 1;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    text.erase(end);
    return text;
}

std::vector<std::string> wrapText(std::string_view text, std::size_t width) {
    width = std::max<std::size_t>(width, 1);
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t newline = text.find('\n', start);
        std::string_view paragraph = text.substr(start, newline == std::string_view::npos ? text.npos : newline - start);
        do {
            if (paragraph.size() <= width) {
                lines.emplace_back(paragraph);
                paragraph = {};
                break;
            }
            std::size_t cut = paragraph.rfind(' ', width);
            std::size_t resume = cut + 1;
            if (cut == std::string_view::npos || cut == 0) cut = resume = width;
            lines.emplace_back(paragraph.substr(0, cut));
            paragraph.remove_prefix(resume);
        } while (!paragraph.empty());
        if (newline == std::string_view::npos) break;
        start = newline + 1;
    }
    return lines;
}

} // namespace uta::client

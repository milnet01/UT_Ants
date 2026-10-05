// The player's answers about which pictures may move -- docs/specs/UTA-0277-
// per-tile-variation.md SS 4.5.

#include "ubundle/TileAnswers.h"

#include "core/FileSystem.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace uta::ubundle {
namespace {

using Hash = std::array<std::byte, 32>;

std::optional<int> hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return std::nullopt;
}

std::optional<Hash> hashOf(std::string_view text) {
    if (text.size() != 64) return std::nullopt;
    Hash out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto high = hexDigit(text[2 * i]);
        const auto low = hexDigit(text[2 * i + 1]);
        if (!high || !low) return std::nullopt;
        out[i] = static_cast<std::byte>(*high * 16 + *low);
    }
    return out;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    return text;
}

} // namespace

TileAnswers parseTileAnswers(std::string_view text) {
    TileAnswers out;
    std::map<Hash, TileKind> given;
    std::set<Hash> conflicted;
    std::size_t number = 0;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        ++number;
        // A comment runs to the end of the line, after an answer or alone.
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        line = trimmed(line);
        if (line.empty()) continue;
        std::size_t gap = 0;
        while (gap < line.size() && !isSpace(line[gap])) ++gap;
        const auto parsed = hashOf(line.substr(0, gap));
        const std::string_view word = trimmed(line.substr(gap));
        const std::optional<TileKind> kind = word == "shuffle" ? std::optional{TileKind::Shuffle}
                                             : word == "fixed" ? std::optional{TileKind::Fixed}
                                                               : std::nullopt;
        if (!parsed || !kind) {
            out.warnings.push_back("tile-kinds.txt line " + std::to_string(number)
                                   + " is not '<64 hex digits> shuffle|fixed'; ignored");
            continue;
        }
        const auto [at, fresh] = given.try_emplace(*parsed, *kind);
        if (!fresh && at->second != *kind && conflicted.insert(*parsed).second)
            out.warnings.push_back("tile-kinds.txt line " + std::to_string(number)
                                   + " answers a picture an earlier line answered otherwise; both ignored");
    }
    for (const auto& [hash, kind] : given)
        if (!conflicted.contains(hash)) out.answers.push_back(TileAnswer{hash, kind});
    return out;
}

std::size_t applyTileAnswers(Bundle& bundle, const TileAnswers& answers) {
    if (!bundle.materials) return 0;
    std::size_t changed = 0;
    for (MaterialRecord& record : *bundle.materials) {
        if (record.tileHash == Hash{}) continue;
        const auto found = std::ranges::lower_bound(answers.answers, record.tileHash, {}, &TileAnswer::hash);
        if (found == answers.answers.end() || found->hash != record.tileHash) continue;
        if (record.tileKind != found->kind) ++changed;
        record.tileKind = found->kind;
    }
    return changed;
}

Result<std::filesystem::path> tileAnswersPath() {
    UTA_TRY(const std::filesystem::path data, fs::dataDirectory());
    return data / "tile-kinds.txt";
}

std::vector<std::string> applyTileAnswersFile(Bundle& bundle) {
    const auto path = tileAnswersPath();
    if (!path.has_value())
        return {"no data directory, so no tile answers: " + std::string(path.error().message())};
    const auto bytes = fs::readFile(*path);
    if (!bytes.has_value()) {
        if (bytes.error().code() == ErrorCode::NotFound) return {};
        return {"tile answers not read: " + std::string(bytes.error().message())};
    }
    TileAnswers answers = parseTileAnswers(
        std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    applyTileAnswers(bundle, answers);
    return std::move(answers.warnings);
}

} // namespace uta::ubundle

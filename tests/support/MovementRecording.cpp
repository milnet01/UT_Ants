#include "support/MovementRecording.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <utility>

namespace uta::test {
namespace {

// A T row after its "1 T": tick, time, dt, keys, forward, strafe, then three
// vectors, then physics, dodge, anim and base (§ 4.5).
constexpr std::size_t kRowColumns = 19;
// INV-4: the log's six decimals, with room for the float the engine summed.
constexpr double kTimeTolerance = 1e-4;

std::vector<std::string_view> wordsOf(std::string_view line) {
    std::vector<std::string_view> words;
    std::size_t at = 0;
    while (at < line.size()) {
        const std::size_t start = line.find_first_not_of(' ', at);
        if (start == std::string_view::npos) break;
        const std::size_t end = std::min(line.find(' ', start), line.size());
        words.push_back(line.substr(start, end - start));
        at = end;
    }
    return words;
}

std::unexpected<Error> refuse(std::size_t line, std::string message) {
    return fail(ErrorCode::MalformedData, std::format("recording line {}: {}", line, message));
}

/// A finite number. from_chars, not stod: no locale reaches it.
std::optional<double> numberOf(std::string_view word) {
    double value = 0;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (error != std::errc{} || end != word.data() + word.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<std::int64_t> wholeOf(std::string_view word) {
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (error != std::errc{} || end != word.data() + word.size()) return std::nullopt;
    return value;
}

/// The key=value words of a BEGIN, PAWN or END line, from `first` on.
Result<std::map<std::string, std::string, std::less<>>> pairsOf(const std::vector<std::string_view>& words,
                                                                  std::size_t first, std::size_t line) {
    std::map<std::string, std::string, std::less<>> pairs;
    for (std::size_t i = first; i < words.size(); ++i) {
        const std::size_t equals = words[i].find('=');
        if (equals == std::string_view::npos || equals == 0)
            return refuse(line, std::format("'{}' is not key=value", words[i]));
        pairs.emplace(words[i].substr(0, equals), words[i].substr(equals + 1));
    }
    return pairs;
}

Result<RecordedTick> rowOf(const std::vector<std::string_view>& words, std::size_t line) {
    if (words.size() != 2 + kRowColumns)
        return refuse(line, std::format("a T row has {} columns, not {}", words.size() - 2, kRowColumns));
    RecordedTick tick;
    const auto tickNumber = wholeOf(words[2]);
    if (!tickNumber) return refuse(line, std::format("tick '{}' is not a whole number", words[2]));
    tick.tick = *tickNumber;
    std::array<double*, 14> numbers{&tick.time,           &tick.dt,           nullptr,
                                    &tick.forward,        &tick.strafe,       &tick.location[0],
                                    &tick.location[1],    &tick.location[2],  &tick.velocity[0],
                                    &tick.velocity[1],    &tick.velocity[2],  &tick.acceleration[0],
                                    &tick.acceleration[1], &tick.acceleration[2]};
    for (std::size_t i = 0; i < numbers.size(); ++i) {
        if (numbers[i] == nullptr) continue;  // the keys column
        const auto value = numberOf(words[3 + i]);
        if (!value) return refuse(line, std::format("'{}' is not a number", words[3 + i]));
        *numbers[i] = *value;
    }
    tick.keys = words[5];
    tick.physics = words[17];
    tick.dodge = words[18];
    tick.anim = words[19];
    tick.base = words[20];
    return tick;
}

}  // namespace

Result<MovementRecording> readMovementRecording(std::string_view text) {
    MovementRecording recording;
    bool begun = false, pawned = false, ended = false;
    std::size_t line = 0;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view current = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        ++line;
        if (!current.empty() && current.back() == '\r') current.remove_suffix(1);
        const std::vector<std::string_view> words = wordsOf(current);
        if (words.empty()) continue;
        if (words[0] != "1")
            return fail(ErrorCode::UnsupportedVersion,
                        std::format("recording line {}: format '{}', and this reader reads 1", line, words[0]));
        recording.version = 1;
        if (words.size() < 2) return refuse(line, "no line kind after the version");
        if (ended) return refuse(line, "a line after END");
        const std::string_view kind = words[1];

        if (kind == "BEGIN") {
            if (begun) return refuse(line, "a second BEGIN");
            auto pairs = pairsOf(words, 2, line);
            if (!pairs) return std::unexpected(pairs.error());
            for (const char* key : {"run", "map", "rate", "pose"})
                if (!pairs->contains(key)) return refuse(line, std::format("BEGIN has no {}=", key));
            recording.run = pairs->find("run")->second;
            recording.map = pairs->find("map")->second;
            const auto rate = numberOf(pairs->find("rate")->second);
            if (!rate) return refuse(line, "BEGIN's rate is not a number");
            recording.rate = *rate;
            std::string_view pose = pairs->find("pose")->second;
            for (std::size_t i = 0; i < recording.pose.size(); ++i) {
                const std::size_t comma = i + 1 < recording.pose.size() ? pose.find(',') : pose.size();
                const auto value = comma == std::string_view::npos ? std::nullopt : numberOf(pose.substr(0, comma));
                if (!value) return refuse(line, "BEGIN's pose is not x,y,z,yaw");
                recording.pose[i] = *value;
                pose = pose.substr(std::min(comma + 1, pose.size()));
            }
            begun = true;
        } else if (!begun) {
            return refuse(line, "no BEGIN before it");
        } else if (kind == "PAWN") {
            if (pawned) return refuse(line, "a second PAWN");
            auto pairs = pairsOf(words, 2, line);
            if (!pairs) return std::unexpected(pairs.error());
            for (const auto& [key, value] : *pairs) {
                if (key == "class") {
                    recording.pawnClass = value;
                    continue;
                }
                const auto number = numberOf(value);
                if (!number) return refuse(line, std::format("PAWN's {} is not a number", key));
                recording.pawn.emplace(key, *number);
            }
            if (recording.pawnClass.empty()) return refuse(line, "PAWN has no class=");
            pawned = true;
        } else if (!pawned) {
            return refuse(line, "no PAWN before it");
        } else if (kind == "T") {
            auto tick = rowOf(words, line);
            if (!tick) return std::unexpected(tick.error());
            const auto expected = static_cast<std::int64_t>(recording.ticks.size());
            if (tick->tick != expected)
                return refuse(line, std::format("tick {} where tick {} comes next", tick->tick, expected));
            if (!recording.ticks.empty() &&
                std::abs(tick->time - recording.ticks.back().time - tick->dt) > kTimeTolerance)
                return refuse(line, std::format("time steps by {} over a tick of {} (INV-4)",
                                                tick->time - recording.ticks.back().time, tick->dt));
            recording.ticks.push_back(std::move(*tick));
        } else if (kind == "END") {
            auto pairs = pairsOf(words, 2, line);
            if (!pairs) return std::unexpected(pairs.error());
            const auto found = pairs->find("ticks");
            const auto count = found == pairs->end() ? std::nullopt : wholeOf(found->second);
            if (!count) return refuse(line, "END has no whole ticks=");
            if (*count != static_cast<std::int64_t>(recording.ticks.size()))
                return refuse(line, std::format("END counts {} ticks and there are {}", *count,
                                                recording.ticks.size()));
            ended = true;
        } else {
            return refuse(line, std::format("unknown line kind '{}'", kind));
        }
    }
    if (!begun) return refuse(line, "no BEGIN");
    if (!pawned) return refuse(line, "no PAWN");
    if (!ended) return refuse(line, "no END");
    return recording;
}

}  // namespace uta::test

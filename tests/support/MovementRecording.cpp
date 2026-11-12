#include "support/MovementRecording.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <initializer_list>
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

/// Reads `words[first..]` into `out`, each a number; false where one is not.
bool numbersOf(const std::vector<std::string_view>& words, std::size_t first, std::initializer_list<double*> out) {
    std::size_t i = first;
    for (double* value : out) {
        const auto number = numberOf(words[i++]);
        if (!number) return false;
        *value = *number;
    }
    return true;
}

/// A W, P, G or D line's tick: a whole number, and for every kind but D
/// the tick of the T row still to come; a D line's, at most that.
Result<std::int64_t> weaponTickOf(const std::vector<std::string_view>& words, std::size_t line,
                                  std::size_t rows, bool damage) {
    const auto tick = wholeOf(words[2]);
    if (!tick) return refuse(line, std::format("tick '{}' is not a whole number", words[2]));
    const auto next = static_cast<std::int64_t>(rows);
    if (damage ? *tick > next : *tick != next)
        return refuse(line, std::format("a {} line at tick {} where tick {} comes next", words[1], *tick, next));
    return *tick;
}

/// A weapon run's line (UTA-0019 § 4.7) into `recording`; its column counts
/// are UTARec's header's.
Result<void> weaponLineOf(const std::vector<std::string_view>& words, std::size_t line,
                          MovementRecording& recording) {
    const std::string_view kind = words[1];
    const std::size_t columns = kind == "W" ? 11 : kind == "P" ? 10 : kind == "G" ? 9 : 24;
    if (words.size() != 2 + columns)
        return refuse(line, std::format("a {} line has {} columns, not {}", kind, words.size() - 2, columns));
    const auto tick = weaponTickOf(words, line, recording.ticks.size(), kind == "D");
    if (!tick) return std::unexpected(tick.error());
    const auto bad = [&] { return refuse(line, std::format("a {} line's field does not parse", kind)); };
    if (kind == "W") {
        RecordedWeapon w{.tick = *tick, .weapon = std::string(words[3]), .state = std::string(words[4]),
                         .anim = std::string(words[5])};
        double ammo = 0, pitch = 0, yaw = 0, fire = 0, alt = 0;
        if (!numbersOf(words, 6, {&w.frame, &ammo, &pitch, &yaw, &w.eyeHeight, &fire, &alt})) return bad();
        w.ammo = static_cast<std::int64_t>(ammo);
        w.pitch = static_cast<std::int32_t>(pitch);
        w.yaw = static_cast<std::int32_t>(yaw);
        w.fire = fire != 0;
        w.altFire = alt != 0;
        recording.weapon.push_back(std::move(w));
    } else if (kind == "P") {
        RecordedProjectile p{.tick = *tick, .name = std::string(words[3]), .cls = std::string(words[4])};
        if (!numbersOf(words, 5, {&p.location[0], &p.location[1], &p.location[2], &p.velocity[0], &p.velocity[1],
                                  &p.velocity[2]}))
            return bad();
        p.physics = words[11];
        recording.projectiles.push_back(std::move(p));
    } else if (kind == "G") {
        RecordedTarget g{.tick = *tick};
        if (!numbersOf(words, 3, {&g.location[0], &g.location[1], &g.location[2], &g.velocity[0], &g.velocity[1],
                                  &g.velocity[2]}) ||
            !numbersOf(words, 10, {&g.health}))
            return bad();
        g.physics = words[9];
        recording.target.push_back(std::move(g));
    } else {
        RecordedDamage d{.tick = *tick, .victim = std::string(words[4]), .instigator = std::string(words[5]),
                         .type = std::string(words[7])};
        if (!numbersOf(words, 3, {&d.time}) || !numbersOf(words, 6, {&d.damage}) ||
            !numbersOf(words, 8, {&d.hit[0], &d.hit[1], &d.hit[2], &d.momentum[0], &d.momentum[1], &d.momentum[2],
                                  &d.health0, &d.health1, &d.velocity0[0], &d.velocity0[1], &d.velocity0[2],
                                  &d.velocity1[0], &d.velocity1[1], &d.velocity1[2]}) ||
            !numbersOf(words, 23, {&d.location[0], &d.location[1], &d.location[2]}))
            return bad();
        d.physics0 = words[22];
        recording.damage.push_back(std::move(d));
    }
    return {};
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
            recording.begin = *pairs;
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
        } else if (kind == "W" || kind == "P" || kind == "G" || kind == "D") {
            const auto read = weaponLineOf(words, line, recording);
            if (!read) return std::unexpected(read.error());
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

// Strip lights -- Strips.h. Every candidate row is found from a pair of end
// lights, then the candidates are taken largest first, so the result is a
// function of the light list alone.

#include "ubake/Strips.h"

#include "ubake/LightModel.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <tuple>
#include <utility>

namespace uta::ubake {
namespace {

/// ELightEffect's and ELightType's values -- the 432 headers'
/// Engine/Inc/EngineClasses.h.
constexpr std::uint8_t LE_CYLINDER = 17;
constexpr std::uint8_t LT_BACKDROP_LIGHT = 6;

/// SS 4.2 rule 2. Only LE_Cylinder lights: UTA-0258 measured plain rows over
/// UTA-0334's 127 maps, and their lamps overlap, so the original's sum along a
/// typical row is 1.8 times one lamp -- the strip, as bright as one, halved
/// it. Cylinder rows sum to about one lamp, the premise SS 8 rests on. The
/// rest are never drawn or never lit.
bool excluded(const ubundle::Light& light) noexcept {
    return light.effect != LE_CYLINDER || light.type == LT_BACKDROP_LIGHT || light.specialLit
           || light.brightness == 0;
}

/// SS 4.2 rule 1: every field but exportIndex, location and the strip fields.
auto keyOf(const ubundle::Light& l) noexcept {
    return std::tuple(l.type, l.effect, l.brightness, l.hue, l.saturation, l.radius, l.period, l.phase, l.cone,
                      l.volumeBrightness, l.volumeRadius, l.volumeFog, l.rotation, l.specialLit,
                      l.actorShadows, l.corona, l.lensFlare);
}

Vec3 at(const ubundle::Light& light) noexcept {
    return {light.location[0], light.location[1], light.location[2]};
}

struct Row {
    std::vector<std::size_t> members; ///< indices into the light list, ascending
    std::size_t from = 0;             ///< the end with the lower index
    std::size_t to = 0;               ///< the other end
};

/// The row whose ends are lights `i` and `j`: every light of `group` on the
/// segment between them and within the line tolerance of it, when there are
/// at least three, no gap along the line is too wide (rule 4), each member
/// sees the next (rule 5), and the ends are the two members furthest apart
/// (rule 3).
std::optional<Row> rowBetween(const std::vector<ubundle::Light>& lights, const std::vector<std::size_t>& group,
                              std::size_t i, std::size_t j, double gapBound, const Blocked& blocked) {
    const Vec3 a = at(lights[i]);
    const Vec3 s = at(lights[j]) - a;
    const double span = length(s);
    if (span == 0) return std::nullopt;
    const Vec3 u = s * (1.0 / span);

    std::vector<std::pair<double, std::size_t>> along;
    for (const std::size_t k : group) {
        if (k == i || k == j) {
            along.emplace_back(k == i ? 0.0 : span, k);
            continue;
        }
        const Vec3 w = at(lights[k]) - a;
        const double t = dot(w, u);
        if (t < 0 || t > span) continue;
        if (length(w - u * t) > STRIP_LINE_TOLERANCE) continue;
        along.emplace_back(t, k);
    }
    if (along.size() < 3) return std::nullopt;

    std::sort(along.begin(), along.end());
    for (std::size_t m = 1; m < along.size(); ++m) {
        const double gap = along[m].first - along[m - 1].first;
        if (!(gap > 0 && gap <= gapBound)) return std::nullopt;
        // Rule 5 (UTA-0255). Alike lights in line through a floor or a wall are
        // not one fixture, and SS 4.5 would cast their shadow from a point no
        // member lights from.
        if (blocked(at(lights[along[m - 1].second]), at(lights[along[m].second]))) return std::nullopt;
    }
    // Rule 3. Every member lies within the tolerance of the line, so two whose
    // spread along it is at most sqrt(span^2 - 4 tolerance^2) cannot be further
    // apart than the ends. `along` is sorted, so each member checks only the far
    // members, nearest-last -- linear for a row of even gaps, where checking
    // every pair made a group of m lights cost m^4 over all its pairs.
    const double safe = span * span - 4 * STRIP_LINE_TOLERANCE * STRIP_LINE_TOLERANCE;
    for (std::size_t m = 0; m < along.size(); ++m)
        for (std::size_t n = along.size() - 1; n > m; --n) {
            const double spread = along[n].first - along[m].first;
            if (safe > 0 && spread * spread <= safe) break;
            if (length(at(lights[along[m].second]) - at(lights[along[n].second])) > span) return std::nullopt;
        }

    Row row;
    for (const auto& entry : along) row.members.push_back(entry.second);
    std::sort(row.members.begin(), row.members.end());
    row.from = std::min(i, j);
    row.to = std::max(i, j);
    return row;
}

} // namespace

void markStrips(std::vector<ubundle::Light>& lights, const Blocked& blocked) {
    using Key = decltype(keyOf(std::declval<const ubundle::Light&>()));
    std::map<Key, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < lights.size(); ++i)
        if (!excluded(lights[i])) groups[keyOf(lights[i])].push_back(i);

    std::vector<Row> candidates;
    std::size_t held = 0; // members across every candidate kept so far
    for (const auto& [key, group] : groups) {
        // STRIP_GROUP_LIMIT and STRIP_MEMBER_BUDGET: past either, the group
        // makes no strip, in group order, so the result is still the light
        // list's alone.
        if (group.size() < 3 || group.size() > STRIP_GROUP_LIMIT) continue;
        const double gapBound = STRIP_GAP_REACH * lightRadius(lights[group.front()].radius);
        // No row is longer than its gaps allow, so farther pairs are not tried.
        const double longest = gapBound * static_cast<double>(group.size() - 1);
        const std::size_t first = candidates.size();
        std::size_t groupHeld = 0;
        bool over = false;
        for (std::size_t p = 0; p < group.size() && !over; ++p)
            for (std::size_t q = p + 1; q < group.size() && !over; ++q) {
                if (length(at(lights[group[q]]) - at(lights[group[p]])) > longest) continue;
                if (auto row = rowBetween(lights, group, group[p], group[q], gapBound, blocked)) {
                    groupHeld += row->members.size();
                    over = held + groupHeld > STRIP_MEMBER_BUDGET;
                    if (!over) candidates.push_back(std::move(*row));
                }
            }
        if (over) {
            candidates.resize(first);
            continue;
        }
        held += groupHeld;
    }

    // Larger first; on a tie the lower lowest member, then the lower ends. Two
    // candidates with the same ends have the same members, so the order is total.
    std::sort(candidates.begin(), candidates.end(), [](const Row& x, const Row& y) {
        if (x.members.size() != y.members.size()) return x.members.size() > y.members.size();
        return std::tie(x.members.front(), x.from, x.to) < std::tie(y.members.front(), y.from, y.to);
    });

    std::vector<bool> taken(lights.size(), false);
    for (const Row& row : candidates) {
        if (std::any_of(row.members.begin(), row.members.end(), [&taken](std::size_t k) { return taken[k]; }))
            continue;
        for (const std::size_t k : row.members) {
            taken[k] = true;
            lights[k].strip = ubundle::STRIP_ABSORBED;
        }
        ubundle::Light& leader = lights[row.members.front()];
        leader.strip = ubundle::STRIP_LEADER;
        leader.stripFrom = lights[row.from].location;
        leader.stripTo = lights[row.to].location;
    }
}

} // namespace uta::ubake

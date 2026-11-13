// The weapon-set format -- docs/specs/UTA-0019-weapon-set.md SS 4.2 and SS 4.4.
//
// A section's keys are gathered as it is read and interpreted when it
// closes, so a fire mode's or a projectile's keys may come in any order and
// still be checked against its archetype or its strike.

#include "ugame/WeaponSet.h"

#include "core/Ascii.h"
#include "core/FileSystem.h"
#include "core/IniText.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <format>
#include <utility>

namespace uta::ugame {
namespace {

constexpr ini::Format FORMAT{.noun = "weapon set", .header = "ut-ants weapons ", .version = WEAPON_SET_VERSION};

std::unexpected<Error> refuse(std::size_t line, std::string_view why) { return ini::refuse(FORMAT, line, why); }

/// Lower-case kebab words: 1 to 32 of a-z, 0-9 and -.
bool isName(std::string_view name) {
    return !name.empty() && name.size() <= 32 && std::ranges::all_of(name, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-';
    });
}

/// `package.class`: letters, digits and _ either side of one dot.
bool isClassName(std::string_view name) {
    const std::size_t dot = name.find('.');
    const auto word = [](std::string_view part) {
        return !part.empty() && std::ranges::all_of(part, [](char character) {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                   || (character >= '0' && character <= '9') || character == '_';
        });
    };
    return dot != std::string_view::npos && word(name.substr(0, dot)) && word(name.substr(dot + 1));
}

/// Forward, right, up: a comma-free name, so it passes through UTA_TRY.
using Triple = std::array<double, 3>;

struct Entry {
    std::string key;
    std::string value;
    std::size_t line;
};

/// One section's keys, taken one by one as the section is interpreted.
/// Whatever is left untaken is a key this section does not have.
class Keys {
public:
    Keys(std::vector<Entry> entries, std::string label, std::size_t heading)
        : entries_(std::move(entries)), label_(std::move(label)), heading_(heading) {}

    [[nodiscard]] bool has(std::string_view key) const {
        return std::ranges::find(entries_, key, &Entry::key) != entries_.end();
    }

    /// The key's entry, removed; or a refusal naming the heading where it is required.
    Result<std::optional<Entry>> take(std::string_view key, bool required) {
        const auto found = std::ranges::find(entries_, key, &Entry::key);
        if (found == entries_.end()) {
            if (required) return refuse(heading_, std::format("{} has no `{}`", label_, key));
            return std::optional<Entry>{};
        }
        Entry entry = std::move(*found);
        entries_.erase(found);
        return std::optional<Entry>{std::move(entry)};
    }

    Result<std::optional<double>> decimal(std::string_view key, bool required, double min, double max) {
        UTA_TRY(const std::optional<Entry> entry, take(key, required));
        if (!entry) return std::optional<double>{};
        UTA_TRY(const double value, ini::decimalOf(FORMAT, entry->value, min, max, entry->line));
        return std::optional<double>{value};
    }

    Result<std::optional<std::uint32_t>> number(std::string_view key, bool required, std::uint32_t min,
                                                std::uint32_t max) {
        UTA_TRY(const std::optional<Entry> entry, take(key, required));
        if (!entry) return std::optional<std::uint32_t>{};
        UTA_TRY(const std::uint32_t value, ini::numberOf(FORMAT, entry->value, max, entry->line));
        if (value < min) return refuse(entry->line, std::format("{} is from {} to {}", key, min, max));
        return std::optional<std::uint32_t>{value};
    }

    /// Three decimals, each from `-limit` to `limit`.
    Result<std::optional<Triple>> triple(std::string_view key, double limit) {
        UTA_TRY(const std::optional<Entry> entry, take(key, false));
        if (!entry) return std::optional<Triple>{};
        const std::vector<std::string_view> words = ini::wordsOf(entry->value);
        if (words.size() != 3) return refuse(entry->line, std::format("{} is three numbers: forward right up", key));
        Triple value{};
        for (std::size_t i = 0; i < 3; ++i) {
            UTA_TRY(value[i], ini::decimalOf(FORMAT, words[i], -limit, limit, entry->line));
        }
        return std::optional<Triple>{value};
    }

    Result<std::optional<std::string>> name(std::string_view key, bool required) {
        UTA_TRY(const std::optional<Entry> entry, take(key, required));
        if (!entry) return std::optional<std::string>{};
        if (!isName(entry->value))
            return refuse(entry->line,
                          std::format("'{}' is not a name: 1 to 32 of a-z, 0-9 and -", entry->value));
        return std::optional<std::string>{entry->value};
    }

    /// Refuses the first key nothing took. `owner` says whose keys these are
    /// not: "[set]", "an instant-hit fire", "a bouncing projectile".
    Result<void> finish(std::string_view owner) const {
        if (entries_.empty()) return {};
        const Entry& first = *std::ranges::min_element(entries_, {}, &Entry::line);
        return refuse(first.line, std::format("{} has no key '{}'", owner, first.key));
    }

private:
    std::vector<Entry> entries_;
    std::string label_;
    std::size_t heading_;
};

// The ranges. Wide enough for any set a player would write; their job is
// to refuse a typo, not to fix a design.
constexpr double DISTANCE_MAX = 1e6;
constexpr double TIME_MAX = 600;
constexpr double AMOUNT_MAX = 1e6;
constexpr std::uint32_t AMMO_MAX = 10000;
constexpr std::uint32_t COUNT_MAX = 64;
constexpr std::uint32_t SPREAD_MAX = 16384; // a quarter turn
constexpr double OFFSET_MAX = 1000;

constexpr std::string_view ARCHETYPE_NAMES[] = {"instant-hit", "projectile", "beam", "melee"};
constexpr std::string_view STRIKE_NAMES[] = {"explode", "bounce", "stick", "split"};
constexpr std::string_view SCALES_NAMES[] = {"damage", "count"};
constexpr std::string_view EXPIRE_NAMES[] = {"vanish", "explode"};
constexpr std::string_view SPLASH_FROM_NAMES[] = {"contact", "moved"};
constexpr std::string_view BODY_HIT_NAMES[] = {"splash", "direct"};
/// FireRule's names, in its order; "none" is never written.
constexpr std::string_view RULE_NAMES[] = {"none", "rocket-fan", "grenade-cluster", "hammer"};
/// The archetype each FireRule belongs to, in its order.
constexpr Archetype RULE_ARCHETYPES[] = {Archetype::InstantHit, Archetype::Projectile, Archetype::Projectile,
                                         Archetype::Melee};

/// The index of `value` in `names`, or a refusal listing them.
template <std::size_t N>
Result<std::size_t> choiceOf(const std::string_view (&names)[N], const Entry& entry) {
    for (std::size_t i = 0; i < N; ++i)
        if (entry.value == names[i]) return i;
    std::string list;
    for (std::size_t i = 0; i < N; ++i) list += std::format("{}{}", i == 0 ? "" : ", ", names[i]);
    return refuse(entry.line, std::format("'{}' is not one of: {}", entry.value, list));
}

Result<void> readSet(WeaponSet& set, Keys& keys) {
    UTA_TRY(const std::optional<Entry> name, keys.take("name", true));
    set.name = name->value;
    UTA_TRY(const std::optional<double> selfDamage, keys.decimal("self-damage", true, 0, 10));
    set.selfDamage = *selfDamage;
    UTA_TRY(const std::optional<double> headHeight, keys.decimal("head-height", true, 0, 1));
    set.headHeight = *headHeight;
    UTA_TRY(const std::optional<double> damageScale, keys.decimal("damage-scale", false, 0, 10));
    set.damageScale = damageScale.value_or(1);
    return keys.finish("[set]");
}

Result<void> readWeapon(Weapon& weapon, Keys& keys) {
    UTA_TRY(const std::optional<Entry> className, keys.take("class", true));
    if (!isClassName(className->value))
        return refuse(className->line, std::format("'{}' is not a class: package.class", className->value));
    weapon.className = asciiFolded(className->value);
    UTA_TRY(const std::optional<std::uint32_t> slot, keys.number("slot", true, 0, 9));
    weapon.slot = *slot;
    UTA_TRY(const std::optional<std::uint32_t> ammoMax, keys.number("ammo-max", true, 0, AMMO_MAX));
    weapon.ammoMax = *ammoMax;
    UTA_TRY(const std::optional<Entry> start, keys.take("ammo-start", true));
    UTA_TRY(weapon.ammoStart, ini::numberOf(FORMAT, start->value, weapon.ammoMax, start->line));
    UTA_TRY(const std::optional<double> switchTime, keys.decimal("switch-time", true, 0, TIME_MAX));
    weapon.switchTime = *switchTime;
    return keys.finish(std::format("[weapon {}]", weapon.name));
}

Result<void> readCharge(FireMode& mode, Keys& keys) {
    if (!keys.has("charge-time") && !keys.has("charge-min") && !keys.has("charge-max") && !keys.has("charge-scales") &&
        !keys.has("charge-step") && !keys.has("charge-step-last"))
        return {};
    if (mode.archetype == Archetype::Beam) return {}; // left untaken: finish refuses them
    Charge charge;
    UTA_TRY(const std::optional<double> time, keys.decimal("charge-time", true, 0.01, TIME_MAX));
    charge.time = *time;
    UTA_TRY(const std::optional<double> min, keys.decimal("charge-min", true, 0, 100));
    charge.min = *min;
    UTA_TRY(const std::optional<double> max, keys.decimal("charge-max", true, 0, 100));
    charge.max = *max;
    UTA_TRY(const std::optional<double> step, keys.decimal("charge-step", false, 0, TIME_MAX));
    charge.step = step.value_or(0);
    UTA_TRY(const std::optional<double> stepLast, keys.decimal("charge-step-last", false, 0, TIME_MAX));
    charge.stepLast = stepLast.value_or(0);
    UTA_TRY(const std::optional<Entry> scales, keys.take("charge-scales", true));
    UTA_TRY(const std::size_t which, choiceOf(SCALES_NAMES, *scales));
    charge.scales = static_cast<ChargeScales>(which);
    if (charge.scales == ChargeScales::Count && mode.archetype != Archetype::Projectile)
        return refuse(scales->line, "only a projectile fire charges its count");
    mode.charge = charge;
    return {};
}

Result<void> readFire(FireMode& mode, Keys& keys) {
    UTA_TRY(const std::optional<Entry> archetype, keys.take("archetype", true));
    UTA_TRY(const std::size_t which, choiceOf(ARCHETYPE_NAMES, *archetype));
    mode.archetype = static_cast<Archetype>(which);
    UTA_TRY(const std::optional<double> refire, keys.decimal("refire", true, 0.001, TIME_MAX));
    mode.refire = *refire;
    UTA_TRY(const std::optional<Entry> leadAt, keys.take("refire-lead", false));
    if (leadAt) {
        UTA_TRY(mode.refireLead, ini::decimalOf(FORMAT, leadAt->value, -TIME_MAX, TIME_MAX, leadAt->line));
        if (mode.refire + mode.refireLead <= 0) return refuse(leadAt->line, "refire-lead takes all of refire");
    }
    UTA_TRY(const std::optional<double> windUp, keys.decimal("wind-up", false, 0, TIME_MAX));
    mode.windUp = windUp.value_or(0);
    UTA_TRY(const std::optional<std::uint32_t> ammo, keys.number("ammo", true, 0, AMMO_MAX));
    mode.ammo = *ammo;
    UTA_TRY(const std::optional<std::uint32_t> pressAmmo, keys.number("press-ammo", false, 0, AMMO_MAX));
    mode.pressAmmo = pressAmmo.value_or(0);
    UTA_TRY(const std::optional<double> repress, keys.decimal("repress", false, 0, TIME_MAX));
    mode.repress = repress.value_or(0);
    UTA_TRY(const std::optional<double> spinUp, keys.decimal("spin-up", false, 0, TIME_MAX));
    mode.spinUp = spinUp.value_or(0);
    // Read only beside a spin-up: without one it is a key the mode does not have.
    if (mode.spinUp > 0) {
        UTA_TRY(const std::optional<double> spinRefire, keys.decimal("spin-refire", true, 0.001, TIME_MAX));
        mode.spinRefire = *spinRefire;
    }
    UTA_TRY(const std::optional<std::uint32_t> spread, keys.number("spread", false, 0, SPREAD_MAX));
    mode.spread = spread.value_or(0);
    UTA_TRY(const std::optional<Triple> offset, keys.triple("offset", OFFSET_MAX));
    mode.offset = offset.value_or(Triple{});
    UTA_CHECK(readCharge(mode, keys));
    UTA_TRY(const std::optional<Entry> rule, keys.take("rule", false));
    if (rule) {
        UTA_TRY(const std::size_t named, choiceOf(RULE_NAMES, *rule));
        if (named == 0 || RULE_ARCHETYPES[named] != mode.archetype)
            return refuse(rule->line, std::format("'{}' is not a rule of {} {} fire", rule->value,
                                                  ARCHETYPE_NAMES[which].front() == 'i' ? "an" : "a",
                                                  ARCHETYPE_NAMES[which]));
        mode.rule = static_cast<FireRule>(named);
    }

    if (mode.archetype == Archetype::Projectile) {
        UTA_TRY(const std::optional<std::string> projectile, keys.name("projectile", true));
        mode.projectile = *projectile;
        UTA_TRY(const std::optional<std::uint32_t> count, keys.number("count", false, 1, COUNT_MAX));
        mode.count = count.value_or(1);
        UTA_TRY(const std::optional<double> spiral, keys.decimal("spiral-radius", false, 0, OFFSET_MAX));
        mode.spiralRadius = spiral.value_or(0);
        UTA_TRY(const std::optional<std::uint32_t> turn, keys.number("spiral-turn", false, 0, 65535));
        mode.spiralTurn = turn.value_or(0);
    } else {
        UTA_TRY(const std::optional<double> damage, keys.decimal("damage", true, 0, AMOUNT_MAX));
        mode.damage = *damage;
        const double pull = mode.archetype == Archetype::Melee ? -AMOUNT_MAX : 0;
        UTA_TRY(const std::optional<double> momentum, keys.decimal("momentum", true, pull, AMOUNT_MAX));
        mode.momentum = *momentum;
        UTA_TRY(const std::optional<double> range, keys.decimal("range", true, 1, DISTANCE_MAX));
        mode.range = *range;
    }
    if (mode.archetype == Archetype::InstantHit) {
        UTA_TRY(const std::optional<std::uint32_t> pellets, keys.number("pellets", false, 1, COUNT_MAX));
        mode.pellets = pellets.value_or(1);
        UTA_TRY(const std::optional<std::uint32_t> random, keys.number("damage-random", false, 0, AMMO_MAX));
        mode.damageRandom = random.value_or(0);
        UTA_TRY(const std::optional<double> chance, keys.decimal("push-chance", false, 0, 1));
        mode.pushChance = chance.value_or(0);
        UTA_TRY(const std::optional<double> pushScale, keys.decimal("push-scale", mode.pushChance > 0, 0, 100));
        mode.pushScale = pushScale.value_or(1);
        UTA_TRY(mode.headDamage, keys.decimal("head-damage", false, 0, AMOUNT_MAX));
        UTA_TRY(const std::optional<double> headMomentum,
                keys.decimal("head-momentum", mode.headDamage.has_value(), 0, AMOUNT_MAX));
        mode.headMomentum = headMomentum.value_or(0);
    }
    if (mode.archetype == Archetype::Beam) {
        UTA_TRY(const std::optional<double> every, keys.decimal("hit-every", false, 0, TIME_MAX));
        mode.hitEvery = every.value_or(0);
        UTA_TRY(const std::optional<double> ammoEvery, keys.decimal("ammo-every", false, 0.001, TIME_MAX));
        mode.ammoEvery = ammoEvery.value_or(1);
        UTA_TRY(const std::optional<double> extend, keys.decimal("extend", false, 0, AMOUNT_MAX));
        mode.extend = extend.value_or(0);
        UTA_TRY(const std::optional<double> firstHit, keys.decimal("first-hit", false, 0, TIME_MAX));
        mode.firstHit = firstHit.value_or(0);
    }
    if (mode.archetype == Archetype::Melee) {
        UTA_TRY(const std::optional<double> side, keys.decimal("momentum-side", false, -AMOUNT_MAX, AMOUNT_MAX));
        mode.momentumSide = side.value_or(0);
        UTA_TRY(const std::optional<double> self, keys.decimal("self-momentum", false, 0, AMOUNT_MAX));
        mode.selfMomentum = self.value_or(0);
        UTA_TRY(const std::optional<double> selfDamage, keys.decimal("self-damage", false, 0, AMOUNT_MAX));
        mode.selfDamage = selfDamage.value_or(0);
        UTA_TRY(const std::optional<double> reach, keys.decimal("reach", false, 0, DISTANCE_MAX));
        mode.reach = reach.value_or(0);
        UTA_TRY(const std::optional<double> scale, keys.decimal("distance-scale", false, 0, DISTANCE_MAX));
        mode.distanceScale = scale.value_or(0);
    }
    const std::string_view name = ARCHETYPE_NAMES[which];
    return keys.finish(std::format("{} {} fire", name.front() == 'i' ? "an" : "a", name));
}

Result<void> readProjectile(Projectile& projectile, Keys& keys, std::size_t heading) {
    const auto need = [&keys](std::string_view key, double min, double max) -> Result<double> {
        UTA_TRY(const std::optional<double> value, keys.decimal(key, true, min, max));
        return *value;
    };
    UTA_TRY(projectile.speed, need("speed", 1, DISTANCE_MAX));
    UTA_TRY(projectile.radius, need("radius", 0, 1000));
    UTA_TRY(projectile.lifetime, need("lifetime", 0.01, TIME_MAX));
    UTA_TRY(projectile.damage, need("damage", 0, AMOUNT_MAX));
    UTA_TRY(projectile.momentum, need("momentum", 0, AMOUNT_MAX));
    for (auto [key, field, max] : {std::tuple{"gravity", &projectile.gravity, 10.0},
                                   std::tuple{"gravity-after", &projectile.gravityAfter, 10.0},
                                   std::tuple{"acceleration", &projectile.acceleration, DISTANCE_MAX},
                                   std::tuple{"max-speed", &projectile.maxSpeed, DISTANCE_MAX},
                                   std::tuple{"lift", &projectile.lift, DISTANCE_MAX},
                                   std::tuple{"inherit", &projectile.inherit, 1.0},
                                   std::tuple{"speed-random", &projectile.speedRandom, DISTANCE_MAX},
                                   std::tuple{"lifetime-random", &projectile.lifetimeRandom, TIME_MAX}}) {
        UTA_TRY(const std::optional<double> value, keys.decimal(key, false, 0, max));
        *field = value.value_or(0);
    }
    if (projectile.speedRandom > projectile.speed)
        return refuse(heading, "speed-random is more than speed");
    if (projectile.lifetimeRandom >= projectile.lifetime)
        return refuse(heading, "lifetime-random is not less than lifetime");
    UTA_TRY(const std::optional<std::uint32_t> turn, keys.number("turn-random", false, 0, SPREAD_MAX));
    projectile.turnRandom = turn.value_or(0);
    UTA_TRY(const std::optional<double> splash, keys.decimal("splash-radius", false, 0, DISTANCE_MAX));
    projectile.splashRadius = splash.value_or(0);
    UTA_TRY(const std::optional<double> splashMax, keys.decimal("splash-radius-max", false, 0, DISTANCE_MAX));
    projectile.splashRadiusMax = splashMax.value_or(0);
    UTA_TRY(const std::optional<Entry> from, keys.take("splash-from", false));
    if (from) {
        UTA_TRY(const std::size_t moved, choiceOf(SPLASH_FROM_NAMES, *from));
        projectile.splashFromMoved = moved == 1;
    }
    UTA_TRY(const std::optional<Entry> bodyHit, keys.take("body-hit", false));
    if (bodyHit) {
        UTA_TRY(const std::size_t direct, choiceOf(BODY_HIT_NAMES, *bodyHit));
        projectile.bodyDirect = direct == 1;
    }
    UTA_TRY(const std::optional<double> upMax, keys.decimal("splash-up-max", false, 0, 1));
    projectile.splashUpMax = upMax.value_or(1);
    UTA_TRY(const std::optional<Entry> expire, keys.take("expire", false));
    if (expire) {
        UTA_TRY(const std::size_t explodes, choiceOf(EXPIRE_NAMES, *expire));
        projectile.expireExplodes = explodes == 1;
    }
    UTA_TRY(const std::optional<std::string> combo, keys.name("combo", false));
    projectile.combo = combo.value_or("");

    UTA_TRY(const std::optional<Entry> strike, keys.take("strike", true));
    UTA_TRY(const std::size_t which, choiceOf(STRIKE_NAMES, *strike));
    projectile.strike = static_cast<Strike>(which);
    switch (projectile.strike) {
    case Strike::Explode: break;
    case Strike::Bounce: {
        UTA_TRY(const std::optional<std::uint32_t> bounces, keys.number("bounces", true, 0, 100));
        projectile.bounces = *bounces;
        UTA_TRY(projectile.bounceKeep, need("bounce-keep", 0, 1));
        UTA_TRY(const std::optional<double> off, keys.decimal("bounce-off", false, 0, 1));
        projectile.bounceOff = off.value_or(projectile.bounceKeep);
        UTA_TRY(const std::optional<double> offRandom, keys.decimal("bounce-off-random", false, 0, 1));
        projectile.bounceOffRandom = offRandom.value_or(0);
        if (projectile.bounceOffRandom > projectile.bounceOff)
            return refuse(heading, "bounce-off-random is more than bounce-off");
        UTA_TRY(projectile.bounceGravity, keys.decimal("bounce-gravity", false, 0, 10.0));
        break;
    }
    case Strike::Stick: {
        UTA_TRY(projectile.fuse, need("fuse", 0, TIME_MAX));
        break;
    }
    case Strike::Split: {
        UTA_TRY(const std::optional<std::string> into, keys.name("split-into", true));
        projectile.splitInto = *into;
        UTA_TRY(const std::optional<std::uint32_t> count, keys.number("split-count", true, 1, COUNT_MAX));
        projectile.splitCount = *count;
        UTA_TRY(const std::optional<std::uint32_t> spread, keys.number("split-spread", false, 0, SPREAD_MAX));
        projectile.splitSpread = spread.value_or(0);
        break;
    }
    }
    return keys.finish(std::format("a projectile that strikes to {}", STRIKE_NAMES[which]));
}

Result<void> readCombo(Combo& combo, Keys& keys) {
    for (auto [key, field] : {std::pair{"damage", &combo.damage}, std::pair{"momentum", &combo.momentum},
                              std::pair{"splash-radius", &combo.splashRadius}}) {
        UTA_TRY(const std::optional<double> value, keys.decimal(key, true, 0, AMOUNT_MAX));
        *field = *value;
    }
    return keys.finish(std::format("[combo {}]", combo.name));
}

enum class SectionKind : std::uint8_t { Set, Weapon, Fire, Projectile, Combo };

/// The section being read: where it opened, what it is, its keys so far.
struct Open {
    SectionKind kind;
    std::size_t line;
    std::string label;
    FireMode* fire = nullptr; ///< SectionKind::Fire: the mode it fills
    std::vector<Entry> entries;
};

/// Where each name was given, so a repeat names the first.
struct NameLines {
    std::vector<std::pair<std::string, std::size_t>> lines;
    std::optional<std::size_t> find(std::string_view name) const {
        const auto found = std::ranges::find(lines, name, &std::pair<std::string, std::size_t>::first);
        return found == lines.end() ? std::nullopt : std::optional{found->second};
    }
};

} // namespace

const Projectile* WeaponSet::projectile(std::string_view wanted) const {
    const auto found = std::ranges::find(projectiles, wanted, &Projectile::name);
    return found == projectiles.end() ? nullptr : &*found;
}

const Combo* WeaponSet::combo(std::string_view wanted) const {
    const auto found = std::ranges::find(combos, wanted, &Combo::name);
    return found == combos.end() ? nullptr : &*found;
}

Result<WeaponSet> parse(std::string_view text) {
    ini::Reader reader(FORMAT, text);
    WeaponSet set;
    std::optional<Open> open;
    std::optional<std::size_t> setLine;
    NameLines weaponLines, projectileLines, comboLines;
    // A reference to check once the whole file is read, and where it was made.
    std::vector<std::pair<std::string, std::size_t>> projectileRefs, comboRefs;

    const auto close = [&]() -> Result<void> {
        if (!open) return {};
        Keys keys(std::move(open->entries), open->label, open->line);
        const std::size_t heading = open->line;
        switch (open->kind) {
        case SectionKind::Set: UTA_CHECK(readSet(set, keys)); break;
        case SectionKind::Weapon: UTA_CHECK(readWeapon(set.weapons.back(), keys)); break;
        case SectionKind::Fire: {
            UTA_CHECK(readFire(*open->fire, keys));
            if (open->fire->archetype == Archetype::Projectile)
                projectileRefs.emplace_back(open->fire->projectile, heading);
            break;
        }
        case SectionKind::Projectile: {
            Projectile& projectile = set.projectiles.back();
            UTA_CHECK(readProjectile(projectile, keys, heading));
            if (projectile.strike == Strike::Split) projectileRefs.emplace_back(projectile.splitInto, heading);
            if (!projectile.combo.empty()) comboRefs.emplace_back(projectile.combo, heading);
            break;
        }
        case SectionKind::Combo: UTA_CHECK(readCombo(set.combos.back(), keys)); break;
        }
        open.reset();
        return {};
    };

    // A named section's name: valid, and not given before for its kind.
    const auto newName = [](std::string_view keyword, std::string_view name, NameLines& lines,
                            std::size_t line) -> Result<std::string> {
        if (!isName(name))
            return refuse(line, std::format("'{}' is not a [{}] name: 1 to 32 of a-z, 0-9 and -", name, keyword));
        if (const auto first = lines.find(name))
            return refuse(line, std::format("[{} {}] is given twice; the first is on line {}", keyword, name, *first));
        lines.lines.emplace_back(std::string(name), line);
        return std::string(name);
    };

    while (true) {
        UTA_TRY(std::optional<ini::Line> next, reader.next());
        if (!next) break;
        const std::size_t line = next->number;
        if (next->kind == ini::Line::Kind::Header) continue;
        if (next->kind == ini::Line::Kind::Key) {
            open->entries.push_back(Entry{std::string(next->key), std::move(next->value), line});
            continue;
        }
        UTA_CHECK(close());
        const std::string_view heading = next->heading;
        if (heading == "set") {
            if (setLine) return refuse(line, std::format("a second [set]; the first is on line {}", *setLine));
            setLine = line;
            open = Open{.kind = SectionKind::Set, .line = line, .label = "[set]"};
        } else if (const auto name = ini::nameAfter(heading, "weapon")) {
            UTA_TRY(std::string valid, newName("weapon", *name, weaponLines, line));
            open = Open{.kind = SectionKind::Weapon, .line = line, .label = std::format("[weapon {}]", valid)};
            set.weapons.push_back(Weapon{.name = std::move(valid)});
        } else if (const auto rest = ini::nameAfter(heading, "fire")) {
            const std::vector<std::string_view> words = ini::wordsOf(*rest);
            if (words.size() != 2 || (words[1] != "primary" && words[1] != "alt"))
                return refuse(line, "a [fire] heading is [fire WEAPON primary] or [fire WEAPON alt]");
            const auto weapon = std::ranges::find(set.weapons, words[0], &Weapon::name);
            if (weapon == set.weapons.end())
                return refuse(line, std::format("there is no [weapon {}] above this line", words[0]));
            const std::string label = std::format("[fire {} {}]", words[0], words[1]);
            FireMode* mode = nullptr;
            if (words[1] == "alt") {
                if (weapon->alt) return refuse(line, std::format("{} is given twice", label));
                mode = &weapon->alt.emplace();
            } else {
                // A weapon's primary is written once; its archetype key is
                // required, so a filled one has a refire above zero.
                if (weapon->primary.refire > 0) return refuse(line, std::format("{} is given twice", label));
                mode = &weapon->primary;
            }
            open = Open{.kind = SectionKind::Fire, .line = line, .label = label, .fire = mode};
        } else if (const auto name = ini::nameAfter(heading, "projectile")) {
            UTA_TRY(std::string valid, newName("projectile", *name, projectileLines, line));
            open = Open{.kind = SectionKind::Projectile, .line = line,
                        .label = std::format("[projectile {}]", valid)};
            set.projectiles.push_back(Projectile{.name = std::move(valid)});
        } else if (const auto name = ini::nameAfter(heading, "combo")) {
            UTA_TRY(std::string valid, newName("combo", *name, comboLines, line));
            open = Open{.kind = SectionKind::Combo, .line = line, .label = std::format("[combo {}]", valid)};
            set.combos.push_back(Combo{.name = std::move(valid)});
        } else {
            return refuse(line, std::format("there is no [{}] section", heading));
        }
    }
    UTA_CHECK(close());

    if (!setLine) return refuse(1, "the weapon set has no [set] section");
    for (const Weapon& weapon : set.weapons)
        if (weapon.primary.refire <= 0)
            return refuse(*weaponLines.find(weapon.name), std::format("[weapon {}] has no [fire {} primary]",
                                                                      weapon.name, weapon.name));
    for (const auto& [name, line] : projectileRefs)
        if (!set.projectile(name)) return refuse(line, std::format("there is no [projectile {}]", name));
    for (const auto& [name, line] : comboRefs)
        if (!set.combo(name)) return refuse(line, std::format("there is no [combo {}]", name));
    return set;
}

namespace {

std::string fireText(std::string_view weapon, std::string_view which, const FireMode& mode) {
    const auto decimal = ini::decimalText;
    const auto archetype = ARCHETYPE_NAMES[static_cast<std::size_t>(mode.archetype)];
    std::string out = std::format("\n[fire {} {}]\narchetype = {}\nrefire = {}\nrefire-lead = {}\nwind-up = {}\n"
                                  "ammo = {}\npress-ammo = {}\nspread = {}\noffset = {} {} {}\n",
                                  weapon, which, archetype, decimal(mode.refire), decimal(mode.refireLead),
                                  decimal(mode.windUp), mode.ammo, mode.pressAmmo, mode.spread,
                                  decimal(mode.offset[0]), decimal(mode.offset[1]), decimal(mode.offset[2]));
    if (mode.windUp > 0)
        out += std::format("repress = {}\nspin-up = {}\n", decimal(mode.repress), decimal(mode.spinUp));
    if (mode.spinUp > 0) out += std::format("spin-refire = {}\n", decimal(mode.spinRefire));
    if (mode.charge)
        out += std::format("charge-time = {}\ncharge-min = {}\ncharge-max = {}\ncharge-step = {}\n"
                           "charge-step-last = {}\ncharge-scales = {}\n",
                           decimal(mode.charge->time), decimal(mode.charge->min), decimal(mode.charge->max),
                           decimal(mode.charge->step), decimal(mode.charge->stepLast),
                           SCALES_NAMES[static_cast<std::size_t>(mode.charge->scales)]);
    if (mode.rule != FireRule::None)
        out += std::format("rule = {}\n", RULE_NAMES[static_cast<std::size_t>(mode.rule)]);
    if (mode.archetype == Archetype::Projectile) {
        out += std::format("projectile = {}\ncount = {}\nspiral-radius = {}\nspiral-turn = {}\n", mode.projectile,
                           mode.count, decimal(mode.spiralRadius), mode.spiralTurn);
        return out;
    }
    out += std::format("damage = {}\nmomentum = {}\nrange = {}\n", decimal(mode.damage), decimal(mode.momentum),
                       decimal(mode.range));
    if (mode.archetype == Archetype::InstantHit) {
        out += std::format("pellets = {}\ndamage-random = {}\npush-chance = {}\npush-scale = {}\n", mode.pellets,
                           mode.damageRandom, decimal(mode.pushChance), decimal(mode.pushScale));
        if (mode.headDamage)
            out += std::format("head-damage = {}\nhead-momentum = {}\n", decimal(*mode.headDamage),
                               decimal(mode.headMomentum));
    }
    if (mode.archetype == Archetype::Beam)
        out += std::format("hit-every = {}\nammo-every = {}\nextend = {}\nfirst-hit = {}\n", decimal(mode.hitEvery),
                           decimal(mode.ammoEvery), decimal(mode.extend), decimal(mode.firstHit));
    if (mode.archetype == Archetype::Melee)
        out += std::format("momentum-side = {}\nself-momentum = {}\nself-damage = {}\nreach = {}\n"
                           "distance-scale = {}\n",
                           decimal(mode.momentumSide), decimal(mode.selfMomentum), decimal(mode.selfDamage),
                           decimal(mode.reach), decimal(mode.distanceScale));
    return out;
}

} // namespace

std::string write(const WeaponSet& set) {
    const auto decimal = ini::decimalText;
    std::string out = std::format("{}{}\n\n[set]\nname = {}\nself-damage = {}\nhead-height = {}\ndamage-scale = {}\n",
                                  FORMAT.header, WEAPON_SET_VERSION, ini::quotedIfNeeded(set.name, true),
                                  decimal(set.selfDamage), decimal(set.headHeight), decimal(set.damageScale));
    for (const Weapon& weapon : set.weapons) {
        out += std::format("\n[weapon {}]\nclass = {}\nslot = {}\nammo-max = {}\nammo-start = {}\nswitch-time = {}\n",
                           weapon.name, weapon.className, weapon.slot, weapon.ammoMax, weapon.ammoStart,
                           decimal(weapon.switchTime));
        out += fireText(weapon.name, "primary", weapon.primary);
        if (weapon.alt) out += fireText(weapon.name, "alt", *weapon.alt);
    }
    for (const Projectile& projectile : set.projectiles) {
        out += std::format("\n[projectile {}]\nspeed = {}\nradius = {}\ngravity = {}\ngravity-after = {}\nlifetime = {}\n"
                           "expire = {}\n"
                           "damage = {}\nmomentum = {}\nsplash-radius = {}\nstrike = {}\n",
                           projectile.name, decimal(projectile.speed), decimal(projectile.radius),
                           decimal(projectile.gravity), decimal(projectile.gravityAfter), decimal(projectile.lifetime),
                           EXPIRE_NAMES[projectile.expireExplodes ? 1 : 0], decimal(projectile.damage),
                           decimal(projectile.momentum), decimal(projectile.splashRadius),
                           STRIKE_NAMES[static_cast<std::size_t>(projectile.strike)]);
        out += std::format("acceleration = {}\nmax-speed = {}\nlift = {}\ninherit = {}\nspeed-random = {}\n"
                           "turn-random = {}\nlifetime-random = {}\n",
                           decimal(projectile.acceleration), decimal(projectile.maxSpeed), decimal(projectile.lift),
                           decimal(projectile.inherit), decimal(projectile.speedRandom), projectile.turnRandom,
                           decimal(projectile.lifetimeRandom));
        out += std::format("splash-from = {}\nbody-hit = {}\nsplash-up-max = {}\nsplash-radius-max = {}\n",
                           SPLASH_FROM_NAMES[projectile.splashFromMoved ? 1 : 0],
                           BODY_HIT_NAMES[projectile.bodyDirect ? 1 : 0], decimal(projectile.splashUpMax),
                           decimal(projectile.splashRadiusMax));
        switch (projectile.strike) {
        case Strike::Explode: break;
        case Strike::Bounce:
            out += std::format("bounces = {}\nbounce-keep = {}\nbounce-off = {}\nbounce-off-random = {}\n",
                               projectile.bounces, decimal(projectile.bounceKeep), decimal(projectile.bounceOff),
                               decimal(projectile.bounceOffRandom));
            if (projectile.bounceGravity)
                out += std::format("bounce-gravity = {}\n", decimal(*projectile.bounceGravity));
            break;
        case Strike::Stick: out += std::format("fuse = {}\n", decimal(projectile.fuse)); break;
        case Strike::Split:
            out += std::format("split-into = {}\nsplit-count = {}\nsplit-spread = {}\n", projectile.splitInto,
                               projectile.splitCount, projectile.splitSpread);
            break;
        }
        if (!projectile.combo.empty()) out += std::format("combo = {}\n", projectile.combo);
    }
    for (const Combo& combo : set.combos)
        out += std::format("\n[combo {}]\ndamage = {}\nmomentum = {}\nsplash-radius = {}\n", combo.name,
                           decimal(combo.damage), decimal(combo.momentum), decimal(combo.splashRadius));
    return out;
}

std::filesystem::path shippedDirectory() { return fs::pathFromUtf8(UTA_SHIPPED_WEAPONS_DIR); }

Result<WeaponSet> load(const std::filesystem::path& file) {
    const std::string shown = fs::utf8(file);
    auto bytes = fs::readFile(file);
    if (!bytes.has_value()) return std::unexpected(bytes.error().withContext("reading the weapon set " + shown));
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    auto set = parse(text);
    if (!set.has_value()) return std::unexpected(set.error().withContext("the weapon set " + shown));
    return set;
}

} // namespace uta::ugame

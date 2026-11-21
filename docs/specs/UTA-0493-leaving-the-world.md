<!-- ants-spec-format: 1 -->
# UTA-0493 — leaving the world kills, and a hurting zone hurts

**Status:** implemented (2026-11-21).
**Kind:** feature.
**Source:** ROADMAP UTA-0493 (in session 2026-11-20, building UTA-0108).

**Layman:** Falling out of the map kills you, and standing in lava or slime
hurts, whatever the fall-damage setting says.

## 1. Goal

A body whose centre leaves the level dies on that tick. A body standing in
a zone UT99 marks as hurting loses health on UT99's clock, and a zone that
heals restores it. Neither reads `MoveRules::landingDamage`, so
`ut-ants --no-landing-damage` spares a hard landing and nothing else.

## 2. Problem

1. **Nothing kills a body outside the level.** `ugame::tick`
   (`src/ugame/Game.cpp`) changes health at two places, a shot and a
   landing. A body that falls through a hole falls for ever.
2. **No zone hurts.** `ubundle::Zone` (`src/ubundle/Bundle.h`) carries light,
   fog, pan speeds and the underwater view. `bPainZone` and `DamagePerSec`
   are read nowhere in `src/` or `tools/`.
3. **The game cannot ask where a body is.** `uworld::BodyWorld`
   (`src/uworld/BodyCollision.h`) answers sweeps, traces and fits. Nothing
   in `src/ugame/` or `src/uworld/` calls `umap::roomAt` or
   `ubundle::zoneAt`.
4. **`roomAt`'s one "no room" hides six cases.** `umap::roomAt`
   (`src/umap/Rooms.cpp`) returns `NO_ROOM` for the engine's null zone, a
   refused zone, a zone past the table, a bad child or leaf index, a cycle,
   and an empty tree. Only the first means "outside the level". Treating
   the rest as outside would kill a body standing in a zone the bake failed
   to read.

UT99's rules, read 2026-11-21 from the install's script, as exported to
`UT_MonsterHunt/work/engine-classes/`:

- `Pawn.FellOutOfWorld` sets `Health = -1` and calls `Died(None, 'Fell')`
  directly. No damage hook sees it.
- `Pawn.FootZoneChange` starts the pain clock at 0.01 s when the feet enter
  a `bPainZone`, and stops it when they leave.
- `Pawn.PainTimer`: while the feet are in a `bPainZone`, `depth` is 0.4, plus
  0.4 if the centre's zone is one, plus 0.2 if the head's is. With
  `DamagePerSec > 0` the body takes `int(DamagePerSec * depth)`. Otherwise,
  below full health, health rises by `-depth * DamagePerSec`, capped at full.
  The clock restarts at 1.0 s while the body lives.
- `LavaZone` and `SlimeZone` (UnrealShare) default to `bPainZone=True` and
  `DamagePerSec=40`.

UT_MonsterHunt's runs, recorded on UTA-0108 (2026-09-10): a bot in a
LavaZone died in about 2 s of `'Burned'` damage through `TakeDamage`. A bot
placed below the world died 5 s later as `'Fell'`. The 5 s is unexplained.

## 3. Scope decisions (agreed with the user)

- **The two deaths are separate from landing damage** (user, 2026-09-10, on
  UTA-0108). Only a landing reads the setting.
- **Monsters keep landing damage** (user, 2026-09-10). It is not built here:
  no body kind exists to give rules to. It moves to UTA-0024, which brings
  the first monster (§ 9).
- Every other choice below follows from § 2.

## 4. Design

### 4.1 Where a point is: `umap::zoneNumberAt`

```cpp
// src/umap/Rooms.h
/// What a point is in, for a rule that must not act on a lookup that failed.
struct ZoneHit {
    enum class Kind : std::uint8_t { Zone, Outside, Unknown } kind = Kind::Unknown;
    std::uint8_t zone = 0; ///< the source Model's zone number; set only for Zone
};
[[nodiscard]] ZoneHit zoneNumberAt(const RoomMap& map, Point3 point);
```

It descends the tree exactly as `roomAt` does, so the two always agree on
the leaf. It answers:

- **Outside** where the descent ends at zone 0, and the map has at least
  one room.
- **Zone** where it ends at a zone that names a room.
- **Unknown** for every other `NO_ROOM` case: a refused zone, a zone past
  the table, a zone with no room, a bad index, a cycle, an empty tree, and
  zone 0 on a map with no rooms.

`roomAt` keeps its signature and callers.

### 4.2 The zone's damage: `ZONE` gains two fields

```cpp
// src/ubundle/Bundle.h, struct Zone: after viewFlash
std::uint8_t pain = 0;          ///< UTA-0493: 1 where bPainZone is set; never above 1
std::int32_t damagePerSec = 0;  ///< UTA-0493: DamagePerSec; below 0 heals
```

On the wire they follow `viewFlash`: u8 then i32, so an entry is 34 bytes.
`validateZones` refuses `pain` above 1. `FORMAT_VERSION` becomes 38 and
`BAKER_REVISION` 80. `ubake`'s `zoneOf` (`src/ubake/Zones.cpp`) reads
`bpainzone` and `damagepersec` through `detail::resolvedRecord`, so a
class's defaults count, as they do for the fields beside them.

UTA-0280 (dangerous liquids bubble) reads the same two fields.

### 4.3 The game's lookup

`ugame::tick` gains a zone lookup beside the `BodyWorld`: the bundle's
`ROOM` map and its zones. The carrier, a new argument or a member of an
existing one, is settled in code. **A tick with no lookup applies neither
rule.** That covers the tests' worlds and a bundle with no `ROOM` section.
`ut-ants` builds the lookup from the bundle it builds `BodyWorld` from.

A zone number at or past the bundle's zone count reads as a zone that
neither hurts nor heals.

### 4.4 Leaving the world

After a body moves, `tick` asks `zoneNumberAt` for its centre. On
**Outside** the body's health becomes -1 on that tick. It is not a damage
step and does not pass the landing. **Unknown** and **Zone** do nothing.

This kills at once where UT99 was seen to take 5 s. The player is already
past every wall, so a delay shows nothing but a fall.

**A start a body cannot stand at is never used.** `ut-ants` lifts each
player start clear of the floor (`client::clearOf`), then drops any whose
lifted centre is Outside (`client::standable`). UT99's spawn fails at a start
in solid space and tries another. Measured 2026-11-21 over the reference
library: 11 of 20213 starts are Outside as placed. Three of them sit just
below a floor and the lift saves them; the rest are in the void.

### 4.5 A hurting zone

Each body carries `painClock`, in seconds. Below 0 means stopped, which is
its start value. Three points are looked up, all after the move: the
**feet** at `centre.z - size.halfHeight`, the **centre**, and the
**head** at `centre.z + eyeHeight`. UT99 finds the head's zone natively, and
that code is unread, so the eye stands in for it.

Each tick, for a living body:

1. Feet in a pain zone and the clock stopped: set it to 0.01. It counts
   from the next tick, as UT99's timer does from the next frame.
   Feet not in a pain zone: stop it.
2. A clock that was running before this tick counts down by `dt`. On
   reaching 0 or below it fires:
   - `tenths` is 4, plus 4 if the centre is in a pain zone, plus 2 if the head is.
   - `damagePerSec > 0`: health drops by `damagePerSec * tenths / 10`,
     an integer division.
   - Otherwise, below `START_HEALTH`: health rises by
     `-damagePerSec * tenths / 10`, capped at `START_HEALTH`.
   - The clock restarts at 1.0 while the body lives.

The arithmetic is integers, so every compiler gets the same health.
UT99 multiplies in float. The two differ only where its product lands just
below a whole number.

`stateHash` covers `painClock`, as it covers `landedZ`.

## 5. Invariants

- **INV-1** — `zoneNumberAt` answers Outside only where the descent ends at
  zone 0 on a map that has a room. A refused zone, an out-of-range zone,
  a bad index, a cycle, an empty tree and a map with no rooms all answer
  Unknown.
  *Test:* `tests/unit/RoomMapTest.cpp`, one case per `NO_ROOM` source, built
  as `roomAt`'s cases are.
  *Breaks when:* `zoneNumberAt` maps `NO_ROOM` to Outside wholesale. The
  refused-zone case is the one only this rule rejects.

- **INV-2** — `ZONE`'s `pain` and `damagePerSec` round-trip, including a
  negative `damagePerSec`. A `pain` above 1 is refused.
  *Test:* `tests/unit/BundleZonesTest.cpp`.
  *Breaks when:* a field is dropped from the writer or the reader, or the
  check on `pain` is missing.

- **INV-3** — The bake writes a LavaZone's class defaults: `pain` 1 and
  `damagePerSec` 40, with no value saved on the placement.
  *Test:* `tests/unit/BakeZonesTest.cpp`, a zone whose class carries the
  defaults and whose placement carries none.
  *Breaks when:* `zoneOf` reads the placement's properties alone.

- **INV-4** — A body whose centre is Outside dies on that tick, with
  `MoveRules::landingDamage` off.
  *Test:* `tests/unit/GameTest.cpp`, a lookup that answers Outside, rules
  with `landingDamage = false`, a body that is not landing.
  *Breaks when:* the death goes through the landing, or reads the flag.
  The body does not land, so only the out-of-world rule can kill it.

- **INV-5** — Unknown never kills, and a tick with no lookup changes no
  health.
  *Test:* `tests/unit/GameTest.cpp`, the INV-4 body with a lookup answering
  Unknown, and again with no lookup.
  *Breaks when:* Unknown is treated as Outside.

- **INV-6** — A body with feet, centre and head in a zone with
  `damagePerSec` 40 loses nothing on the tick it enters, 40 on the next
  tick, and 40 a second after that. Feet alone lose 16 each time. With `landingDamage` off
  the damage is the same.
  *Test:* `tests/unit/GameTest.cpp`, ticks at the fixed rate through 2.1 s.
  *Breaks when:* the clock starts at 1.0, the depth ignores the head, or the
  pain reads the landing flag.

- **INV-7** — A zone with `damagePerSec` -10 raises a body at 50 health by 10
  a second, and never above `START_HEALTH`.
  *Test:* `tests/unit/GameTest.cpp`.
  *Breaks when:* the cap is missing, or negative damage is applied as
  damage.

- **INV-8** — Every map in the reference library with a player start has
  one that is not Outside, and the client drops a start still Outside once
  lifted clear.
  *Test:* `tests/real/RealRoomsTest.cpp` (`[zone-out]`), each map's player
  starts through `zoneNumberAt`, naming each Outside one; and
  `tests/unit/ClientWalkerTest.cpp`, a sunk start kept and a start in the
  void dropped.
  *Breaks when:* a map has no start inside the level, or `standable` checks
  the start as placed rather than as lifted.

## 6. Failure modes

- **A map's tree misplaces a point** (`umap/Rooms.cpp`'s front-side rule on a
  plane). A body's centre is a radius from walls and half its height above
  the floor, so it never lies on a surface. INV-8 checks every start.
- **Walking begins where the flying camera is.** The camera stops at walls,
  so it is inside the level. A body that starts outside dies and respawns at
  a standable start.
- **A map whose zones did not read** answers Unknown, so nothing dies.
  Its pain zones do not hurt either; the bake report already names the
  refusal.
- **A bundle with no `ROOM` section** gets no lookup, so neither rule runs.
- **Feet on a floor plane** take the front side, which is open space, so a
  body on a lava floor finds the lava's zone above the plane. A body on solid
  ground at a pain zone's edge may read either side. That costs at most one
  hit of pain, never a death.

## 7. Tests

INV-1 to INV-7 run in the `unit` label. INV-8's library half runs in the
real-asset tier under `[zone-out]`, and its client half in `unit`.
`scripts/mutations/zones.py` holds a mutation for every invariant but INV-8,
whose client half was mutated by hand.

## 8. Alternatives considered (and rejected)

- **Use `zoneAt`'s 0 as outside.** It is also every lookup that failed, so
  a refused zone would kill (§ 2 item 4).
- **Wait 5 s before killing, as UT99 was seen to.** The cause of the delay is
  unknown, so copying it copies a guess. Nothing visible depends on it.
- **Give each zone a kill flag from `bKillZone`.** UT99 kills there in native
  code nobody has read. The run that saw a LavaZone with `bKillZone=True`
  saw a burn, not an instant kill.
- **Float arithmetic, as UT99.** It risks a different health per compiler.

## 9. Out of scope

- Monsters keeping landing damage — moved to UTA-0024.
- `bKillZone`'s native rule, and `PressureZone`'s triggered kill — deferred;
  not yet queued.
- A zone that starts hurting when triggered (`ZoneInfo.Trigger`) — needs
  the trigger code of UTA-0485.
- Drowning, the other half of `PainTimer` — UTA-0090.
- `bNeutralZone` — deferred; not yet queued.
- What follows a death (respawn, a death message) — as for any other death.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/RoomMapTest.cpp` |
| INV-2 | `tests/unit/BundleZonesTest.cpp` |
| INV-3 | `tests/unit/BakeZonesTest.cpp` |
| INV-4 to INV-7 | `tests/unit/GameTest.cpp` |
| INV-8 | `tests/real/RealRoomsTest.cpp`, real-asset tier only; `tests/unit/ClientWalkerTest.cpp` |

## 11. Cross-doc impact

- `docs/specs/UTA-0156-zone-ambient-light.md` § 4.1, the zone record: it
  grows, as UTA-0215 grew it.
- `src/ubundle/Bundle.h`'s format history comment: 38.
- `CLAUDE.md` § Standing facts: format 38, baker revision 80.
- `CHANGELOG.md`.
- The roadmap: UTA-0493's body names this spec; UTA-0024 takes the monsters.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0493-leaving-the-world-loop-log.md`.

## 13. Migration / compatibility

`FORMAT_VERSION` 38: every map is baked again.

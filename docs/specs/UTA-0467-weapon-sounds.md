<!-- ants-spec-format: 1 -->
# UTA-0467 — weapons play their sounds

**Status:** built (2026-11-20). No review: contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0467 (user-request-2026-11-11).
**Pairs with:** UTA-0019, UTA-0478, UTA-0480, each amended in § 11.

**Layman:** Guns make their noises: a shot, a weapon coming up, a hum while a beam is held, an explosion where the rocket lands.

## 1. Goal

After this ships, firing a weapon, switching to one, holding a beam or a
spinning barrel, and a shot exploding are each heard, from where they
happen. Which sound each plays is written in the weapons file.

This spec covers the sounds of UTA-0467 only. Its models and muzzle
flashes are § 9's.

## 2. Problem

1. **No weapon names a sound.** `ugame::Weapon`, `ugame::FireMode`,
   `ugame::Projectile` and `ugame::Combo` (`src/ugame/WeaponSet.h`) carry
   no sound, and `weapons/stock.weapons` names none.
2. **The game does not say a shot was fired.** `ugame::Event`
   (`src/ugame/Game.h`) has the kinds `InstantHit`, `Beam`, `Melee`,
   `Explosion` and `Combo`, each a place to draw. None names the weapon,
   the mode or the firer. A projectile's launch and a weapon switch raise
   no event.
3. **The sounds are baked and loaded, and nothing plays them.**
   `ut-bake --weapon-sounds` writes the weapon classes' default sounds
   into one bundle, and `loadSounds` in `apps/ut-ants/main.cpp` adds it to
   the `uaudio::SoundBank`. The client's only caller of the mixer is
   `apps/ut-ants/MapSounds.cpp`, for ambient loops.
4. **Some weapon sounds are out of the baker's reach.** UTA-0478 § 3
   gathers a weapon class's defaults and those of the classes it names,
   and no further. An explosion's sound is named by an effect class a
   script spawns (`UT_SpriteBallExplosion.EffectSound1`). UTA-0478 § 9
   leaves those to this item, "by naming it in the weapons file".

## 3. Scope decisions (agreed with the user)

The user asked for weapon sounds (2026-11-11) and set this item next
(2026-11-20). Every choice below is the session's, made under
`CLAUDE.md`'s cheapest-methods rule.

1. **The weapons file names each sound by its path.** No sound is looked
   up from a class's property at run time: the bundle stores paths, not
   which property named them.
2. **Heard in this item:** a shot, a weapon switch, a loop while a beam,
   a wound-up mode or a mode fired shot after shot is held, the sound
   that ends that loop, a projectile's explosion and a combo's.
3. **Not heard in this item:** loading and cocking sounds, the rocket
   launcher's lock-on tones, a charge's rising tone, an empty weapon's
   click, a projectile's sound in flight, a hit on a body or a wall by an
   instant shot. § 9.
4. **The switch sound plays when the new weapon starts to rise**, as the
   original's `TournamentWeapon.PlaySelect` does: the tick the weapon
   going down hands over (`UTA-0501`).
5. **Every weapon sound is heard out to 1600 world units**, falling in a
   straight line as UTA-0480 § 3 item 2's do. 1600 is a recollection of
   the engine's value for a `PlaySound` with no radius, not a reading of
   it. `Actor.uc` sets no `TransientSoundRadius` default. § 15 item 1.
6. **Every weapon sound plays at full volume.** The original's vary:
   `ShockRifle.PlayFiring` passes `SoundDampening*4.0`, and
   `UT_SpriteBallExplosion` passes 12 with a radius of 2200. A volume and
   a radius per sound wait for a paired capture (§ 15 item 1).
7. **`WEAPON_SET_VERSION` stays 1.** No release holds a weapons file:
   `git log v0.1.0 -- weapons/stock.weapons` is empty (2026-11-20).

## 4. Design

### 4.1 Files

| File | Change |
|---|---|
| `src/ugame/WeaponSet.h`, `WeaponSet.cpp` | the sound keys, `soundPaths` |
| `src/ugame/Game.h`, `Game.cpp`, `Fire.cpp` | `Cue`, `Game::cues`, `soundOf`, `loopingMode` |
| `weapons/stock.weapons` | each stock weapon's sounds |
| `src/ubake/Sounds.h`, `Sounds.cpp`, `tools/ut-bake/Cli.cpp` | sounds the file names join the set |
| `apps/ut-ants/WeaponSounds.h`, `WeaponSounds.cpp`, new | plays cues and loops |
| `apps/ut-ants/Walker.h`, `Walker.cpp`, `main.cpp` | carries a frame's cues to `WeaponSounds` |

### 4.2 The weapons file

Five optional keys. Each value is a sound's path as the baker stores it:
lower case, two or three parts joined by dots, each part of `a` to `z`,
digits and `_`. Anything else is `MalformedData` naming the line.

```ini
[weapon shock-rifle]
select-sound = unrealshare.asmd.tazerselect   # when a switch brings it up

[fire shock-rifle primary]
sound = unrealshare.asmd.tazerfire            # once a shot

[fire pulse-gun alt]
loop-sound = botpack.pulsegun.pulsebolt       # while the mode sounds (§ 4.3)
end-sound = botpack.pulsegun.pulsedown        # once, when that loop stops

[projectile rocket]
sound = …                                     # where it explodes

[combo shock-combo]
sound = …                                     # where it goes off
```

- `sound` and `loop-sound` are legal on a fire mode of any archetype.
- `end-sound` without `loop-sound` on the same mode is refused.
- `write` emits each key that is set, and `parse(write(s))` equals `s`
  (UTA-0019 INV-2).

```cpp
struct Weapon     { /* … */ std::string selectSound; };
struct FireMode   { /* … */ std::string sound, loopSound, endSound; };
struct Projectile { /* … */ std::string sound; };
struct Combo      { /* … */ std::string sound; };

/// Every sound path `set` names, sorted, each once.
[[nodiscard]] std::vector<std::string> soundPaths(const WeaponSet& set);
```

`weapons/stock.weapons` gains each stock weapon's sounds. Each line says
where its path came from, as that file's numbers do: a class default, or
the script that names it.

### 4.3 What the game says

```cpp
/// What a tick did that a client plays a sound for.
struct Cue {
    enum class Kind : std::uint8_t { Fire, Select, Explode, Combo };
    Kind kind;
    int owner = -1;          ///< the body; a shot's firer
    std::uint32_t index = 0; ///< Fire, Select: the weapon. Explode: the projectile kind. Combo: the combo.
    int mode = -1;           ///< Fire: 0 primary, 1 alt
    Vec3 at{};               ///< where it is heard from
    bool operator==(const Cue&) const = default;
};

struct Game { /* … */ std::vector<Cue> cues; }; ///< this tick's; cleared at its start

/// The path `cue` plays, or empty where its weapon, mode, projectile or
/// combo names none.
[[nodiscard]] std::string_view soundOf(const WeaponSet& set, const Cue& cue);

/// The mode of the weapon in `body`'s hand that sounds now: 0, 1, or -1.
[[nodiscard]] int loopingMode(const WeaponSet& set, const Body& body);
```

- **Fire**: one each time `fireBody` (`src/ugame/Game.cpp`) fires a
  shot through `detail::fire`, at the body's place. A charge released
  with several projectiles is one cue. A beam raises none.
- **Select**: one when `switchWeapon` starts bringing a weapon up,
  naming it.
- **Explode**: one each time `explode` runs, at the shot's place.
- **Combo**: one where `Fire.cpp` raises its `Combo` event.
- A cue is raised whether or not a sound is named. `soundOf` decides.
- `stateHash` does not read `cues`, as it does not read `events`.
- **`loopingMode`** is a mode with a `loop-sound`, on a living body
  that is not switching. A beam's sounds while the beam is on. Another
  mode's sounds while it winds up, while the press that wound it up is
  still held, and from its first shot of a hold until the trigger is up
  or the weapon cannot pay for a shot. The last is how the chainsaw and
  the pulse gun's primary hum, as the original sets `AmbientSound`
  while they fire. It reads `Body` as it stands and adds no field to
  it.

### 4.4 The baker

```cpp
struct SoundSetRequest { /* … */ std::vector<std::string> sounds; }; ///< paths the weapons file names
struct SoundSetOutcome { /* … */ std::vector<std::string> missing; }; ///< of `sounds`, those that name no Sound
```

- Each path of `sounds` is resolved as a default's is (UTA-0478 § 4.3
  items 2 and 3) and joins the set. One that leads to no `Sound` export
  goes in `missing`, and the bake goes on.
- The name's first line becomes `uta-sound-set-name-2`. After the
  classes comes the line `sounds`, then the sorted paths of `sounds`,
  each once and each followed by LF. That line has no dot, so it is
  neither a class nor a path, and a class cannot pass for a named
  sound. The closure's roots gain each named path's package.
  `BAKER_REVISION` does not move: no map's bake changes.
- `ut-bake --weapon-sounds` passes `ugame::soundPaths` of the file. Its
  `written` report gains `"missing": ["path", …]`.

### 4.5 The client

```cpp
namespace uta::client {

/// World units a weapon's sound carries. A recollection: spec SS 15 item 1.
inline constexpr float WEAPON_SOUND_RADIUS = 1600.0f;

class WeaponSounds {
public:
    WeaponSounds(const ugame::WeaponSet& set, const uaudio::SoundBank& bank);

    /// Plays this frame's cues and keeps each body's loop. The caller
    /// excludes the mixer's mix() (Audio::withMixer). Once a rendered frame.
    void update(const uaudio::Listener& listener, std::span<const ugame::Cue> cues,
                std::span<const ugame::Body> bodies, uaudio::Mixer& mixer);

    /// Paths the set names that the bank does not hold.
    [[nodiscard]] std::size_t missing() const noexcept;
};

}
```

- **A cue** whose `soundOf` path the bank holds plays once through
  `Mixer::play`, group `Weapons`, with the gain and pan
  `uaudio::place(listener, cue.at, WEAPON_SOUND_RADIUS)` gives. A gain
  of nothing plays nothing.
- **A loop**: each frame, each body's `loopingMode` is compared with the
  frame before. A mode that starts sounding starts a loop with
  `Mixer::startLoop`. One that goes on is moved with `Mixer::setLoop` to
  the body's place. One that stops is stopped with `Mixer::stop`, and
  its `end-sound` plays once where the same weapon is still in hand.
- `Walker` gathers `Game::cues` over a frame's ticks beside its events,
  and `main.cpp` passes them with `Game::bodies` once a frame. A frame
  with no tick has no cues and keeps its loops. A frame spent flying
  passes no bodies, which stops every loop.
- A loop out of earshot holds no voice and starts one when it comes
  near. Leaving earshot is not the loop ending: no `end-sound` plays.
- `ut-ants` says on stderr, once, how many paths are missing.
- A headless run with no audio device runs the same code against a
  mixer nothing drains, as `MapSounds` does.

## 5. Invariants

- **INV-1** — `parse(write(s))` equals `s` for a set carrying every key
  of § 4.2, and `parse` refuses a path with an upper-case letter, one
  with four parts, and an `end-sound` whose mode has no `loop-sound`.
  *Test:* `tests/unit/WeaponSetTest.cpp`.
  *Breaks when:* `write` drops a sound key, or `parse` takes a path the
  bank could never hold. The round trip isolates `write`; each refusal's
  fixture is a file that parses once the one fault is mended.

- **INV-2** — A tick in which a body fires one shot raises exactly one
  `Fire` cue, naming that body, its weapon and the mode. A press with
  too little ammo raises none, and a held beam raises none.
  *Test:* `tests/unit/GameTest.cpp`.
  *Breaks when:* the cue is raised at the press and not at the shot, so
  a refused shot is heard; or once per projectile, so a flak shot is
  heard eight times. The low-ammo fixture differs from the firing one
  in ammo alone.

- **INV-3** — A switch raises one `Select` cue, naming the weapon asked
  for, on the tick it starts to rise; the press and the other ticks of
  the switch raise none (amended by `UTA-0501`).
  *Test:* `tests/unit/GameTest.cpp`.
  *Breaks when:* the cue is raised every tick of the switch, on the
  press, or names the weapon being put away.

- **INV-4** — A projectile that explodes raises one `Explode` cue with
  its kind, at the place its `Explosion` event has; a combo raises one
  `Combo` cue with its combo.
  *Test:* `tests/unit/GameTest.cpp`.
  *Breaks when:* a split shell's pieces take the shell's kind, or the
  cue is placed at the firer.

- **INV-5** — `stateHash` is the same for two games that differ only in
  `cues`, and UTA-0019's replay hashes do not move.
  *Test:* `tests/unit/GameTest.cpp`, and `tests/unit/FireReplayTest.cpp`
  unedited.
  *Breaks when:* raising a cue draws from the game's generator or is
  hashed.

- **INV-6** — `loopingMode` is the beam's mode while it is on and -1 the
  tick after its trigger is released; it is -1 for a mode with no
  `loop-sound`, for a dead body and during a switch.
  *Test:* `tests/unit/GameTest.cpp`.
  *Breaks when:* it reads the trigger and not the beam or the shots, so
  an empty weapon hums; or it ignores `pending`, so the put-away weapon
  hums through the switch on the strength of its last burst.

- **INV-7** — `soundOf` returns the path of the cue's own weapon and
  mode, projectile or combo, and empty where that names none.
  *Test:* `tests/unit/GameTest.cpp`, on a set whose two modes and two
  weapons each name a different path.
  *Breaks when:* the alt mode plays the primary's sound.

- **INV-8** — `bakeSoundSet` stores a sound that `sounds` names and no
  listed class's defaults reach; a path that names no sound is in
  `missing` and the bake succeeds; and the set's name differs between a
  request with that path and one without.
  *Test:* `tests/unit/BakeSoundsTest.cpp`, on a fixture package holding
  a sound no class points at.
  *Breaks when:* the named paths are left out of the name, so a file
  that gains a sound finds the old bundle and stays silent.

- **INV-9** — Given a `Fire` cue whose sound the bank holds, within
  earshot, `WeaponSounds::update` starts one voice in group `Weapons`;
  the same cue beyond `WEAPON_SOUND_RADIUS` starts none; and a cue whose
  sound the bank lacks starts none.
  *Test:* `tests/unit/ClientWeaponSoundsTest.cpp`, new, against a mixer
  with no device.
  *Breaks when:* a far sound still takes a voice, or a missing path is
  dereferenced. The two distance fixtures differ in `at` alone.

- **INV-10** — A body whose `loopingMode` turns on holds one loop voice
  for as many updates as it stays on; when it turns off the loop stops
  and the mode's `end-sound` starts once; when the weapon changes in
  that same update no `end-sound` starts.
  *Test:* `tests/unit/ClientWeaponSoundsTest.cpp`.
  *Breaks when:* each update starts another loop, or a loop outlives
  its body's switch.

- **INV-11** — Looked up in the real install with no class listed,
  every sound `weapons/stock.weapons` names is found and decodes; and
  every stock weapon names a `select-sound`, but the Translocator, whose
  original has none (`Weapon.SelectSound`'s default), and each of its
  fire modes a `sound` or a `loop-sound`.
  *Test:* `tests/real/RealSoundsTest.cpp`, the real-asset tier, for the
  first half; `tests/unit/WeaponSetTest.cpp` for the second.
  *Breaks when:* a path in the stock file is misspelt, or a weapon is
  added with no sound. Listing no class keeps a default that reaches
  the same sound from hiding a misspelt path.

## 6. Failure modes

- **The bank lacks a path the set names** (a stale `--sounds` bundle, or
  another set's): that sound is silent, `missing()` counts it and stderr
  says so. Nothing is refused.
- **Many cues in one frame** (a flak shell's pieces striking together):
  each asks the mixer, and UTA-0056 § 4.5's rules collapse and cap them.
- **A body dies or leaves holding a loop**: `loopingMode` is -1 for it,
  so its loop stops in the next update. A body removed from the list
  has its loop stopped in the same update.
- **No audio device**: § 4.5's last item.
- **A weapons file names a sound the install lacks**: `missing` in the
  bake's report, and that sound is silent in play.

## 7. Tests

- `tests/unit/WeaponSetTest.cpp` gains INV-1.
- `tests/unit/GameTest.cpp` gains INV-2 to INV-7. `FireReplayTest.cpp`
  is not edited (INV-5).
- `tests/unit/BakeSoundsTest.cpp` gains INV-8.
- `tests/unit/ClientWeaponSoundsTest.cpp`, new, carries INV-9 and
  INV-10.
- `tests/real/RealSoundsTest.cpp` gains INV-11's first half, run where
  `UTA_REAL_ASSET_TESTS` is on; `tests/unit/WeaponSetTest.cpp` its
  second. `tests/unit/BakeCliTest.cpp` checks the report's `missing`.
- The unit tests are label `unit`. Each rule was broken by hand and a
  test seen to fail for it (2026-11-20); no file in
  `scripts/mutations/` holds those mutations yet.
- By ear, once: start `ut-ants`, fire each weapon both ways, switch
  through them, hold the pulse gun's beam and release it, and land a
  rocket near and far.

## 8. Alternatives considered (and rejected)

- **Read each sound from the class's property when playing**
  (`FireSound`, `SelectSound`). Rejected: the bundle would have to store
  which property named each sound, a format bump and a re-bake of every
  map, and it still could not reach a sound a script names.
- **New kinds on `ugame::Event`.** Rejected: `StandIns::advance` draws
  every event, so each new kind would need a case that draws nothing.
- **A field on `Body` saying which mode sounds.** Rejected: `stateHash`
  would gain it and every replay hash would move, for a fact the
  existing fields already give.
- **A cue each tick a loop should go on.** Rejected: a rendered frame
  can hold no tick (`Walker`'s clock), and the loop would stop and
  start.
- **Bump `BAKER_REVISION`.** Rejected: every map would re-bake for a
  change to the weapon bundle alone. Its own name's first line moves.

## 9. Out of scope

- Weapon models in hand and muzzle flashes — tracked by UTA-0467, whose
  body says they need animated meshes first.
- § 3 item 3's sounds — deferred; not yet queued.
- A volume, radius or pitch per sound — deferred; not yet queued.
- Walls muffling a shot — deferred; not yet queued.
- Volume sliders — tracked by UTA-0120.
- Pickup sounds — tracked by UTA-0486.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/WeaponSetTest.cpp` |
| INV-2 to INV-7 | `tests/unit/GameTest.cpp` |
| INV-5, replay hashes | `tests/unit/FireReplayTest.cpp` |
| INV-8 | `tests/unit/BakeSoundsTest.cpp` |
| INV-9, INV-10 | `tests/unit/ClientWeaponSoundsTest.cpp` |
| INV-11 | Partial: `tests/unit/WeaponSetTest.cpp` holds the census; `tests/real/RealSoundsTest.cpp` holds the paths and runs only with the install, so GitHub's matrix does not run it |
| § 3 item 5: 1600 units matches UT99 | **nothing** — § 15 item 1 |
| § 4.2: each stock sound is the one the original plays for that event | **nothing** — each line cites its class default or script; a wrong but existing path passes INV-11 |
| § 4.5: a sound is heard by a person | **nothing** — § 7's recipe, by ear; the user did it 2026-11-20 and found the sounds good |
| § 4.5: `main.cpp` passes the cues and bodies each frame | **nothing** — one hidden run walked, fired and switched without a fault (2026-11-20); it cannot hear |

## 11. Cross-doc impact

- `docs/specs/UTA-0019-weapon-set.md`: § 4.2 lists the sound keys by
  pointing here; § 4.5 names `Game::cues`; § 9 says sounds are built
  here.
- `docs/specs/UTA-0478-baked-sounds.md`: § 4.4's request, outcome, name
  and report gain § 4.4's of this spec; INV-7's name rule gains the
  named paths; § 9's line on UTA-0467 points here.
- `docs/specs/UTA-0480-map-sounds.md` § 9: its one-shot line points
  here.
- `CLAUDE.md` § Where this project is.
- `CHANGELOG.md`: weapon sounds, under the unreleased version.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0467-weapon-sounds-loop-log.md`.

## 13. Resource cost

`WeaponSounds` holds one voice id per body and a clip pointer per sound
the set names. The weapon bundle grows by the sounds the file names
beyond the classes' defaults; the stock set's bundle went from 40 sounds
and 1 508 648 bytes to 50 and 2 000 664
(`ut-bake --weapon-sounds weapons/stock.weapons`, before and after the
file named its sounds, 2026-11-20). No new
dependency.

## 15. Open questions

1. **Is 1600 the right reach, and is full volume close enough?** Check
   by a paired capture of the original with UT_MonsterHunt, as UTA-0480
   § 14 item 1 does for ambient sounds. Until then each is one named
   constant.

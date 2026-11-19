<!-- ants-spec-format: 1 -->
# UTA-0480 — play a map's ambient sounds, placed in the world

**Status:** spec draft (2026-11-19). § 3 item 1 awaits the user. No review: contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0480 (in-session-2026-11-19, UTA-0478's spec).
**Blocked by:** UTA-0478 (shipped).  **Blocker for:** UTA-0467.  **Pairs with:** UTA-0056.

**Layman:** Maps get their hums back. Machinery, lava, wind and the like are heard from where they sit: louder as you walk up, quieter as you leave, and from the left or the right.

## 1. Goal

A map's ambient sounds play in the client, each from its actor's place. The
mixer can loop a voice, change its loudness and side while it plays, and
stop it. A function turns a listener and a source into a loudness and a
side, for this item and for UTA-0467.

## 2. Problem

1. UTA-0478 loads each map's sounds into the client's bank and nothing plays
   them. `apps/ut-ants/Audio.h` says so, and no caller of `Audio::withMixer`
   exists.
2. `uaudio::Mixer` cannot play a hum. A voice plays its clip once and frees
   its slot; `PlayRequest` fixes its gain and pan at the start; `stopAll` is
   the only stop (`src/uaudio/Mixer.h`).
3. Nothing places a sound. `docs/specs/UTA-0056-audio-mixer.md` § 3 item 8
   left distance and direction to the first caller that needs them.
4. Nothing reads an actor's sound properties. They are in the bundle:
   `ubundle::ActorPlacement::properties` holds each actor's own list and
   `ubundle::ActorClass::defaults` its class family's.
5. The roadmap item also names movers, triggers and pickups. The client
   has no code that opens a door, fires a trigger or takes a pickup, and
   no roadmap item builds one, so nothing can say when those sounds start.

## 3. Scope decisions

1. **Proposed, awaiting the user (2026-11-19):** this item plays ambient
   sounds only. A mover's, a trigger's and a pickup's sounds wait for the
   code that moves, fires and takes them, and are built with it.
2. **Loudness falls in a straight line with distance and is silent at the
   radius.** Epic's own note on Unreal's audio says sounds "diminish
   linearly according to distance and are culled when too far away to
   hear". Source: <https://www.zx.net.nz/mirror/unreal.epicgames.com/Audio.htm>.
   The session's choice, following `CLAUDE.md`'s cheapest-methods rule.
3. **The radius is `25 × (SoundRadius + 1)` world units.** The factor 25 is
   a recollection of UT99's audio driver, backed only by a wiki
   contributor's note. Source:
   <https://unrealarchive.org/wikis/unreal-wiki/Legacy_talk:Actor_(UT)/Functions.html>.
   It is one named constant; § 14 says how it gets checked.
4. **Walls do not muffle a sound.** UT99's did not either. The session's
   choice.
5. **A sound is never in one ear only.** The pan stops at 0.8 of full. The
   session's choice, for comfort on headphones.
6. **At most 16 ambient voices play at once**, the loudest. The mixer has
   64 voices for everything (`Mixer::MAX_VOICES`). The session's choice.

## 4. Design

### 4.1 The mixer: looping voices

Added to `src/uaudio/Mixer.h`:

```cpp
struct LoopRequest {
    Group group = Group::World;
    float gain = 0.0f;
    float pan = 0.0f;   ///< -1 left to 1 right
    float pitch = 1.0f; ///< the clip's rate is multiplied by it; clamped to 0.5..2
    int priority = 0;
};

/// Start `clip` and play it round and round until stop(). An invalid id
/// when no voice could be had.
VoiceId startLoop(const Clip& clip, LoopRequest request);
/// A playing loop's gain and pan from the next mix(). False: no such voice.
bool setLoop(VoiceId voice, float gain, float pan) noexcept;
/// Stop one voice, of either kind. No such voice: nothing happens.
void stop(VoiceId voice) noexcept;
```

- A loop wraps to the clip's first frame when it passes its last.
- A loop skips UTA-0056 § 4.5's collapse and per-clip cap: forty lamps
  share one hum, and their caller budgets them (§ 4.4). It counts toward
  the 64 and is stolen by § 4.5 item 3 like any voice.
- A loop starts from gain 0. Every change of its gain or pan, the start
  and `stop` included, is a straight ramp across one `Mixer::BLOCK` of
  output, so no change clicks. A stopped loop frees its slot when its
  ramp ends. A stopped one-shot stops at once, as a stolen voice does.
- The frame after a loop's last is its first, so a clip played off its
  own rate has no seam.
- `play` and one-shot voices behave as before. `mix` still allocates
  nothing.

### 4.2 Placing a sound

New, `src/uaudio/Spatial.h`, pure and linking nothing new:

```cpp
struct Listener {
    std::array<float, 3> position{};
    std::array<float, 3> right{1.0f, 0.0f, 0.0f}; ///< unit length
};
struct Placed {
    float gain = 0.0f; ///< 0..1
    float pan = 0.0f;  ///< -MAX_PAN..MAX_PAN
};
inline constexpr float MAX_PAN = 0.8f;
/// World units within which the pan fades to the centre.
inline constexpr float PAN_NEAR = 64.0f;

[[nodiscard]] Placed place(const Listener& listener, std::array<float, 3> source, float radius) noexcept;
```

With `d` the distance from listener to source:
`gain = max(0, 1 − d / radius)`, and 0 for a radius of 0 or less.
`pan = MAX_PAN × dot(right, toSource / d) × min(1, d / PAN_NEAR)`, and 0
at `d = 0`. The last factor stops a sound jumping sides as the listener
walks through it.

### 4.3 A map's emitters

New, `apps/ut-ants/MapSounds.h`:

```cpp
struct Emitter {
    std::array<float, 3> position{};
    const uaudio::Clip* clip = nullptr;
    float radius = 0.0f; ///< world units
    float volume = 0.0f; ///< 0..1
    float pitch = 1.0f;
};
inline constexpr float RADIUS_UNITS = 25.0f;

struct EmitterScan {
    std::vector<Emitter> emitters;
    std::size_t missing = 0; ///< actors naming a sound the bank lacks
    std::size_t guessed = 0; ///< emitters that used a fallback below
};

[[nodiscard]] EmitterScan ambientEmitters(const ubundle::Placements& placements,
                                          const uaudio::SoundBank& bank);
```

For each actor, a property is the actor's own record, else its class's
effective default, at array index 0. Names are matched whatever their
case, as `main.cpp`'s `playerStarts` matches them: a map spells them as
its author did.

| Property | Kind read | Becomes | Stored nowhere |
|---|---|---|---|
| `AmbientSound` | Object | `bank.find(path)` | no emitter |
| `Location` | Vector | `position` | the origin |
| `SoundRadius` | Byte | `radius = RADIUS_UNITS × (value + 1)` | 32 |
| `SoundVolume` | Byte | `volume = value / 255` | 128 |
| `SoundPitch` | Byte | `pitch = value / 64` | 64 |

The three fallbacks are `Actor`'s defaults in the install's own script
source (`Engine.u`'s `Actor.uc`, `defaultproperties`, read 2026-11-19).
An actor whose `AmbientSound` is empty, is not an object
reference, is not in the bank, or whose volume is 0 gives no emitter.
The client says once, on stderr, how many emitters a map has, how many
sounds the bank lacked, and how many emitters used a fallback.

### 4.4 Each frame

`MapSounds` owns the emitters and which of them hold a voice:

```cpp
class MapSounds {
public:
    static constexpr std::size_t VOICES = 16;
    explicit MapSounds(std::vector<Emitter> emitters);
    /// Start, move and stop voices for a listener here. Call under
    /// Audio::withMixer, once a rendered frame.
    void update(const uaudio::Listener& listener, uaudio::Mixer& mixer);
};
```

1. Each emitter's loudness is `place(...).gain × volume`.
2. The wanted set is the `VOICES` loudest above 0. An emitter already
   playing is kept ahead of a silent one unless that one is more than
   1.1 times as loud, so two near-equal hums do not swap every frame.
3. Emitters leaving the set are stopped; those joining are started with
   `startLoop`; the rest get `setLoop`. A voice the mixer stole
   (`setLoop` false) is forgotten and may rejoin next frame.
4. The listener is the camera the frame is drawn from: its position and
   its right-hand direction. The fly camera counts.
5. Ambient loops play in `Group::World` at priority 0. Sound keeps the
   wall clock (UTA-0056 § 3 item 5), so slowed time does not slow a hum.

`main.cpp` builds `MapSounds` after `loadSounds`. The client opens one
bundle a run, and its `Audio` closes before the bank is destroyed, so no
voice outlives its clip. `listenerAt` makes the listener from the
camera's place and yaw.

## 5. Invariants

- **INV-1** — A loop plays past its clip's end, repeating it exactly: with one loop of a 100-frame clip at the mixer's rate, gain 1, pan 0, output frame `n` equals output frame `n + 100` once the start ramp is over.
  *Test:* `tests/unit/MixerTest.cpp`, a clip of 100 distinct samples, 2000 frames mixed.
  *Breaks when:* a loop frees its slot at the clip's end, or wraps a frame early or late.

- **INV-2** — A loop's gain or pan change reaches its target within one `Mixer::BLOCK` and no click: on a clip of constant sample 1, no two neighbouring output frames of one channel differ by more than `2 / BLOCK` across a `setLoop` from gain 0 to 1, the start and a `stop`.
  *Test:* `tests/unit/MixerTest.cpp`.
  *Breaks when:* a gain is applied in one step, or `stop` cuts mid-waveform.

- **INV-3** — `stop` ends one voice and no other: of two loops, stopping the first leaves `voicesOf` 1 after one block, the second's output unchanged; stopping an id twice, or a stolen one, does nothing.
  *Test:* `tests/unit/MixerTest.cpp`, the two loops on different clips so each one's output is told apart.
  *Breaks when:* `stop` matches by clip or by slot instead of by id, or leaves the slot taken.

- **INV-4** — Loops skip the collapse and the per-clip cap, and no other limit: six `startLoop` calls of one clip between two `mix` calls give six voices; with 64 loops playing, a `Group::Weapons` `play` steals one, and `setLoop` on the stolen id returns false.
  *Test:* `tests/unit/MixerTest.cpp`.
  *Breaks when:* loops go through `collapseTarget`, a fifth loop steals the first, or loops are exempt from stealing.

- **INV-5** — `place` follows § 4.2: at radius 1000, a source 0, 500, 1000 and 1500 units away has gain 1, 0.5, 0 and 0; 500 units to the listener's right has pan 0.8, to the left −0.8, straight ahead 0; 32 units to the right has pan 0.4; at the listener, pan 0; radius 0 has gain 0.
  *Test:* `tests/unit/SpatialTest.cpp`, new.
  *Breaks when:* falloff is not linear, the pan's sign follows the world's axes rather than the listener's, or the near fade is missing.

- **INV-6** — `ambientEmitters` reads the actor's own value over its class's: a class defaulting `SoundRadius` 10 and an actor storing 63 gives radius 1600; an actor storing none gives 275; one storing no `SoundVolume` anywhere gives volume 128/255. An actor naming a sound the bank lacks, one with no `AmbientSound`, one whose `AmbientSound` is a name and not an object, and one with `SoundVolume` 0 give no emitter. A property spelled `ambientsound` is read as `AmbientSound` is.
  *Test:* `tests/unit/ClientMapSoundsTest.cpp`, new, on hand-built `ubundle::Placements`.
  *Breaks when:* defaults win over the actor, the `+ 1` is dropped, a missing sound yields a null clip, or names are matched only in one case.

- **INV-7** — `MapSounds::update` holds the loudest `VOICES` and no more: with 20 emitters of 20 clips in earshot, 16 voices play and they are the 16 loudest; moving the listener beyond every radius leaves none after one block; an emitter 1.05 times as loud as the quietest playing one does not replace it, and one 1.2 times as loud does.
  *Test:* `tests/unit/ClientMapSoundsTest.cpp`, driving a `uaudio::Mixer` with no device and counting `voicesOf` per clip.
  *Breaks when:* voices leak as the listener moves, the budget is unbounded, or near-equal emitters swap each frame.

- **INV-8** — A hum is heard on the side it is on: one emitter 300 units to the listener's right, of a constant clip, gives a right channel louder than the left after mixing; turning the listener round swaps them. Looking up or down leaves the listener's right level.
  *Test:* `tests/unit/ClientMapSoundsTest.cpp`, comparing the two channels' sums.
  *Breaks when:* `listenerAt` gives a world axis for `right`, or `update` drops the pan.

UTA-0056's INV-8 (no sample outside [-1, 1]), INV-9 (`mix` allocates
nothing) and INV-11 (what links what) hold unchanged; § 7 extends the
first two's cases to loops.

## 6. Failure modes

| What fails | What happens |
|---|---|
| No audio device | `update` still runs, on a mixer nothing reads (`Audio::withMixer`) |
| A map with no ambient sound, or a bundle baked before UTA-0478 | No emitters; one stderr line; silence |
| A sound the bank lacks | That emitter is left out and counted in the stderr line |
| All 64 voices busy at `startLoop` | The mixer steals or drops (UTA-0056 § 4.5 item 3); the emitter tries again next frame |
| A clip freed while a loop plays it | Cannot happen: `Audio` closes before the bank is destroyed (§ 4.4) |
| A radius or position that is not finite | `place` returns gain 0 |

## 7. Tests

- `tests/unit/MixerTest.cpp` gains INV-1 to INV-4, and a loop in the
  existing INV-8 and INV-9 cases of UTA-0056.
- `tests/unit/SpatialTest.cpp`, new, carries INV-5.
- `tests/unit/ClientMapSoundsTest.cpp`, new, carries INV-6, INV-7 and
  INV-8.
- All are label `unit`. Each is seen to fail before its code exists.
- `scripts/mutations/uaudio-loops.py` holds the mutations for INV-1 to
  INV-8.
- By ear, once: start `ut-ants` on a map the stderr line says has
  emitters, walk to one and past it. DM-Deck16][ has 23, DM-Morpheus 14,
  DM-Liandri 7 and CTF-Face none (hidden runs, 2026-11-19).

## 8. Alternatives considered (and rejected)

- **Loop by re-requesting the clip when it ends.** The caller cannot see
  the end to the frame, so every repeat has a gap or an overlap.
- **Inverse-square falloff.** Truer to air, but a map's radii were
  authored against a straight line, so every hum would be heard too far
  or die too soon.
- **Play every emitter in earshot.** DM maps can hold more same-clip
  lamps than the mixer has voices, and they would starve weapon sounds.
- **Put the emitters in `ugame`.** UTA-0056 INV-11 bars `ugame` from
  linking `uaudio`, and nothing in the game's rules depends on a hum.
- **A new bundle section of emitters, made at bake.** `PLAC` already
  holds every property; a section would cost a format bump and a re-bake
  of every map to save a scan the client does once a map.

## 9. Out of scope

- A mover's, a trigger's and a pickup's sounds (§ 3 item 1) — deferred; not yet queued.
- One-shot sounds placed in the world, such as another player's shot — tracked by UTA-0467, which uses `place`.
- Walls muffling sound, Doppler, reverb by zone — deferred; not yet queued.
- Volume sliders — tracked by UTA-0120.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 to INV-4 | `tests/unit/MixerTest.cpp`, on every CI leg |
| INV-5 | `tests/unit/SpatialTest.cpp`, on every CI leg |
| INV-6 to INV-8 | `tests/unit/ClientMapSoundsTest.cpp`, on every CI leg |
| § 3 item 3: the factor 25 matches UT99 | **nothing** — § 14 item 1 |
| § 4.3: the three fallbacks are UT99's defaults | **nothing** — read once from `Actor.uc` (§ 14 item 2); the stderr line says when one is used |
| § 4.4: no voice outlives its clip | **nothing** — it rests on the order of two declarations in `main.cpp` |
| § 4.4 item 4: the listener is the drawn camera | **Partial:** INV-8 checks `listenerAt` and that `update` uses what it is given; nothing checks what `main.cpp` passes |
| How a map sounds | **nothing** — by ear (§ 7) |

## 11. Cross-doc impact

- `docs/specs/UTA-0056-audio-mixer.md`: § 3 item 8 and § 9 point here for
  positional sound; § 4.5 says loops skip its items 1 and 2.
- `apps/ut-ants/Audio.h`'s "Nothing plays yet" comment, corrected.
- `CHANGELOG.md`: an entry under Added.
- `CLAUDE.md` § Where this project is: `Next:` moves on.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0480-map-sounds-loop-log.md`.

## 13. Resource cost

At most 16 of the mixer's 64 voices. Each frame's `update` is one pass
over the map's emitters and one short hold of the audio lock. No new
dependency, no new bundle section, no state on disk.

## 14. Open questions

1. **Is the factor 25 right?** Check by a paired capture of the original
   with UT_MonsterHunt: stand at a known distance from one emitter in
   both and compare loudness. Until then it is a named constant.
2. **Are 32, 128 and 64 `Actor`'s defaults?** Yes: `Actor.uc`'s
   `defaultproperties` in the install's `Engine.u` sets `SoundRadius=32`,
   `SoundVolume=128` and `SoundPitch=64` (read 2026-11-19). On
   DM-Deck16][, DM-Morpheus and DM-Liandri no emitter needed a fallback
   (hidden runs, the same day).
3. **§ 3 item 1**, the user's.

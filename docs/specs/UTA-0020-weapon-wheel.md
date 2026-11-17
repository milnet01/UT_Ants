<!-- ants-spec-format: 1 -->
# UTA-0020 — the weapon wheel, and the slowed time it opens

**Status:** accepted (2026-11-17), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0020 (design-2026-09-03; time settled by ADR-0009, user 2026-11-17, after playing the prototype).
**Blocked by:** UTA-0019.

**Layman:** Hold R1, or Q on the keyboard, and your guns appear in a ring. Point at one with the right stick or the mouse and let go to pick it. While the ring is open the game slows to a fifth of its speed, but only when you are the only person playing.

## 1. Goal

A player holds one button to open a ring of the weapons they carry,
points at one, and releases to pick it. It works fully on a gamepad
and on mouse and keyboard. While the ring is open and exactly one human
is in the game, game time runs at 20% of the wall clock
([ADR-0009](../decisions/ADR-0009-weapon-wheel-slows-time-for-a-lone-player.md)).
The slow-down travels in the input command, so a server and a replay
agree with the client tick for tick.

## 2. Problem

1. A weapon is chosen only by number key or by stepping:
   `uworld::MoveCommand` carries `weaponSlot` and `weaponStep`, and
   `ugame`'s `askedWeapon` (in `src/ugame/Game.cpp`) reads only those.
   On a pad, stepping through a full set with the D-pad is slow.
2. The tick clock has no time scale. `uworld::FixedTick::advance` turns
   wall seconds into ticks at a fixed `TICKS_PER_SECOND`. ADR-0009 needs
   game time to follow the wall clock at 20% while the wheel is open.
3. Nothing marks a body as a human's. `ugame::Body` holds movement,
   health and inventory only, so the rule *exactly one human* has
   nothing to count.
4. `uui` does not exist. `docs/design.md` § The parts gives it the
   weapon wheel, and rule 13 splits the wheel: `ugame` owns what is in
   it, `uui` how it is drawn.
5. The prototype (branch `wheel-proto`, never merged) scaled wall time
   in `apps/ut-ants/main.cpp` and sent a pick as a repeated
   `weaponSlot`. A server would not see the first. The second cannot
   name one weapon of two sharing a slot.

## 3. Scope decisions (agreed with the user)

1. **The open wheel slows time to 20%, only while exactly one human is
   playing.** ADR-0009 (user, 2026-11-17). Changing who gets it, or
   pausing, needs a new ADR.
2. **Hold R1 on a pad, or Q on the keyboard, to open it; point; release
   to pick.** R1 stops jumping; Cross and Space still jump (user,
   2026-11-17, keeping the prototype's binding).
3. **Pointing as prototyped:** the right stick points while pushed, and
   the highlight stays when it is let go, so a flick then a release
   picks. The mouse moves the pointer. Look and fire stop while the
   wheel is open; moving, jumping and ducking go on. This is the
   prototype the user played on 2026-11-17.
4. **What follows the slowed clock (the session's call, 2026-11-17).**
   ADR-0009 left it to this spec. Everything the game simulates slows:
   bodies, shots, and the stand-in effects drawn from its events. The
   level's own animation (water, flames, sky, light pulses) and sound
   keep the wall clock. Reason: the wheel is open for a second or two,
   and slowing those needs a game clock fed to `urender`, and a
   resampler in `uaudio`, for this alone. The user may overturn it.
5. **The ring shows weapon names** in the overlay's font. Icons wait
   for weapon models (UTA-0467). This follows the standing fact on
   cheapest methods first.

## 4. Design

### 4.1 Files

| File | Change |
|---|---|
| `src/uworld/Movement.h` | `MoveCommand` gains `wheelOpen` and `weaponPick` (§ 4.2) |
| `src/uworld/FixedTick.h`, `.cpp` | `advance` takes a time scale (§ 4.4) |
| `src/ugame/Game.h`, `Game.cpp` | `Body::human`, `Game::timeScale`, `WHEEL_TIME_SCALE`, `wheelWeapons`; the tick sets the scale and honours a pick; `stateHash` covers both new fields (§ 4.3, § 4.5) |
| `src/uinput/Input.h`, `.cpp` | `Action::Wheel`, `Key::Q`, the right stick's position on `Frame`, the new bindings (§ 4.7) |
| `src/uui/` (new: `Wheel.h`, `.cpp`, `CMakeLists.txt`) | `uta_uui`: pointing and the ring's layout (§ 4.6) |
| `src/urender/Overlay.h`, `.cpp`, `Renderer.h`, `Frame.cpp` | `OverlayLabel` and `Renderer::setOverlayLabels` (§ 4.6) |
| `apps/ut-ants/Walker.h`, `.cpp`, `main.cpp`, `Cli.cpp` | open, point, pick; the clock's scale; the stand-ins on game time; the help text (§ 4.8) |
| `tests/unit/` | `GameTest`, `FixedTickTest`, `InputTest`, `RenderOverlayTest`, `ClientWalkerTest`, and a new `UiWheelTest` (§ 7) |

### 4.2 The command (amends UTA-0017 § 4.2 and UTA-0019 § 4.6)

```cpp
struct MoveCommand {
    // ...every field UTA-0017 and UTA-0019 gave it, unchanged...
    bool wheelOpen = false;       ///< the weapon wheel is open this tick
    std::int8_t weaponPick = -1;  ///< a weapon picked on the wheel this tick, an index into the set; -1 for none
};
```

`MoveCommand` is the whole of a tick's input (design rule 12), so it is
what UTA-0026's `unet` will send. Both fields are tick inputs a server
judges. Neither is trusted: § 4.3's rule decides what the bit does,
and § 4.5 refuses a pick the body cannot make. `uworld`'s own tick
reads neither.

### 4.3 Who gets the slow-down (`ugame`, rule 8)

```cpp
inline constexpr double WHEEL_TIME_SCALE = 0.2; // ADR-0009

struct Body {
    // ...
    bool human = false; ///< a person plays it; a dummy or a bot does not
};

struct Game {
    // ...
    double timeScale = 1; ///< what the last tick set: game seconds per wall second, over the tick's own speed
};
```

At the end of each tick, `ugame::tick` sets `game.timeScale`. It is
`WHEEL_TIME_SCALE` when all three hold, and 1 otherwise:

- exactly one body in `game.bodies` has `human` set, alive or dead;
- that body is alive, so its command is read (a dead body's never is);
- its command has `wheelOpen`.

A second human joining, or the human dying, gives 1 on the next tick.
The bit changes nothing else in the tick: `dt` is the caller's, as
before. `stateHash` adds `timeScale` and each body's `human`.

### 4.4 The tick clock (amends UTA-0017 § 4.7)

```cpp
/// Adds `wallSeconds` at `scale` game speed and returns the whole ticks
/// now due, at most MAX_TICKS_PER_ADVANCE. A scale outside (0, 1], or
/// not finite, counts as 1.
int advance(double wallSeconds, double scale = 1) noexcept;
```

Ticks owed grow by `wallSeconds * TICKS_PER_SECOND * scale`. `dt()` is
unchanged, so a tick moves bodies and shots exactly as before (UTA-0017
INV-1 and INV-2 hold). `blend()` keeps its meaning. A scale of 0 is
refused because it would deadlock: no tick would run, so no command
could carry the bit that closes the wheel.

The caller passes `game.timeScale`, which the last tick set. So the
frame on which the wheel opens still runs at full speed, and the slow
starts with the next. At 60 fps that is one frame.

### 4.5 What is in the wheel, and a pick (`ugame`, rule 13)

```cpp
/// The weapons `body` carries, in NextWeapon's order: by slot, the 0
/// key last, then by file order. The wheel shows these, clockwise from the top.
[[nodiscard]] std::vector<int> wheelWeapons(const WeaponSet& set, const Body& body);
```

`askedWeapon` builds its carried list through `wheelWeapons`, so the
wheel and stepping cannot disagree on order.

A command's `weaponPick` is read before `weaponSlot` and `weaponStep`,
and wins over both. A pick that is in range and carried is brought up
exactly as a number key's choice is (`switchWeapon`). A pick that is
out of range or not carried changes nothing, and the slot and step in
the same command are still read. A pick of the weapon already in hand,
or already coming up, changes nothing.

### 4.6 Pointing and drawing (`uui`, `urender`)

`uta_uui` is a new static library. It links `uta_core`, `uta_uinput`
and `uta_urender`, and nothing in `uworld` or `ugame` may link it
(design rules 2 and 4; their configure-time link assertions already
refuse it).

```cpp
namespace uta::uui {

inline constexpr double STICK_POINTS = 0.5;  ///< stick travel past which the stick sets the pointer's direction
inline constexpr double PICKS_FROM = 0.35;   ///< pointer length past which it highlights
inline constexpr double MOUSE_REACH = 1600;  ///< UT angle units of mouse look from the centre to the rim

class Wheel {
public:
    /// Opens on `weapons` (from ugame::wheelWeapons), the pointer at the centre, nothing highlighted.
    void open(std::vector<int> weapons);
    /// One frame's pointing: the right stick past STICK_POINTS sets the
    /// pointer's direction at full length; otherwise the frame's look
    /// moves it, clamped to the rim. Past PICKS_FROM, the nearest item
    /// to the pointer's direction is highlighted.
    void point(const uinput::Frame& frame);
    /// Closes; the highlighted weapon, or -1 for none.
    [[nodiscard]] int close();
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] int highlighted() const noexcept; ///< a weapon index; -1 for none
    // ...the weapons and the pointer, for layOut...
};

/// The ring as overlay labels: item i of n at i/n of a turn clockwise
/// from the top, `names` giving each weapon's text, and the pointer.
[[nodiscard]] std::vector<urender::OverlayLabel> layOut(const Wheel& wheel, std::span<const std::string> names);

}
```

`urender` gains the prototype's `OverlayLabel` (text centred at a
position given as shares of the image's height from its centre, a
frame when highlighted, a small square for empty text) and
`Renderer::setOverlayLabels`. `layOutOverlay` drops a label that would
leave the image or touch the text's box or an earlier label. The
crosshair is hidden while the wheel is open.

### 4.7 Bindings and the input frame (`uinput`)

`Action::Wheel` is a held action. The default bindings hold it on
`Key::Q` (HID usage 20) and `PadButton::RightShoulder`, which no longer
asks for `Up`. `PadButton::South` and `Key::Space` still jump.

`Frame` gains `pointRight` and `pointDown`: the right stick's position
past its dead zone, with the round dead zone `Settings::stickDeadZone`
already uses, clamped to the unit circle and not scaled by frame time.
With several pads, the one pushed furthest. `Frame::wheel` is
`Action::Wheel` held.

### 4.8 What `ut-ants` does

`Walker` holds a `uui::Wheel`. In `Walker::update`:

- `Frame::wheel` going down opens it on `wheelWeapons` for the player's
  body; going up closes it, and a highlighted weapon becomes a pick.
  That pick waits for the next tick, as a number key's does.
- While it is open, the frame points it, the view does not turn, and
  no fire or alt-fire is sent. Every command carries `wheelOpen`.
- The clock advances at `game().timeScale` (§ 4.4).
- `Walker` reports the game seconds of its last update, wall seconds
  times that scale. `main.cpp` advances the stand-ins by those.

`body(0).human` is set. A dummy's is not. The fly camera has no
wheel. `Cli.cpp`'s help names R1 and Q.

## 5. Invariants

- **INV-1** — `game.timeScale` after a tick is `WHEEL_TIME_SCALE`
  exactly when one body is human, it is alive, and its command has
  `wheelOpen`; otherwise it is 1.
  *Test:* `tests/unit/GameTest.cpp`: one human with the bit, 0.2;
  without it, 1; two humans, one with the bit, 1; a human without the
  bit beside a dummy whose command has it, 1; a dead human with it, 1.
  *Breaks when:* the rule counts bodies, not humans, or reads a
  non-human's bit. The dummy case isolates the second: only the
  "the human's bit" rule rejects it.

- **INV-2** — The wheel bit changes nothing but `timeScale`. A script
  run with `wheelOpen` set on some ticks gives, tick for tick, the same
  `stateHash` as without it, once `timeScale` is set back to 1.
  *Test:* `tests/unit/GameTest.cpp`, a script that moves and fires.
  *Breaks when:* the tick scales `dt`, or stops fire, while the wheel is open.

- **INV-3** — `FixedTick::advance(w, s)` owes `w * TICKS_PER_SECOND * s`
  ticks for `s` in (0, 1], and counts any other `s` as 1. `dt()` never
  changes.
  *Test:* `tests/unit/FixedTickTest.cpp`: one wall second in 1/60 s
  frames runs 17 ticks at 0.2, within one, and 85 at 1; scales of 0,
  -1, 2 and NaN each run 85.
  *Breaks when:* the scale divides `dt` instead, or a scale of 0
  freezes the clock.

- **INV-4** — A script that opens and closes the wheel replays
  bit-identically across runs and across GCC, Clang and MSVC. Two
  states differing only in `timeScale`, or only in one body's `human`,
  hash differently.
  *Test:* `tests/unit/GameTest.cpp` on GitHub's matrix, with UTA-0019's
  INV-5 script.
  *Breaks when:* the rule reads anything outside the game and its
  commands, or `stateHash` leaves out either field.

- **INV-5** — `wheelWeapons` lists exactly the weapons the body
  carries, in NextWeapon's order.
  *Test:* `tests/unit/GameTest.cpp`: a body carrying the weapons of
  slots 0, 1 and 2 but not slot 3, two of them sharing slot 1, lists the two
  slot-1 weapons in file order, then slot 2, then slot 0.
  *Breaks when:* an uncarried weapon is listed, or slot 0 comes first.

- **INV-6** — A pick of a carried weapon brings it up and wins over
  `weaponSlot` and `weaponStep` in the same command. A pick out of
  range or not carried changes nothing, and that command's slot or
  step still applies.
  *Test:* `tests/unit/GameTest.cpp`: a pick with a different slot in
  the same command; a pick of a weapon taken away; a pick of 99.
  *Breaks when:* an uncarried weapon is honoured (a client could pick
  a gun it never collected), or the slot overrides the pick.

- **INV-7** — `uui::Wheel` highlights item i of n when the pointer,
  longer than `PICKS_FROM`, points within half a sector of i/n of a
  turn clockwise from the top. A stick let go keeps the highlight.
  `close` returns the highlight or -1.
  *Test:* `tests/unit/UiWheelTest.cpp`: four weapons, the stick
  straight up, right, down and left; then the stick at rest; then
  mouse look across the rim; an empty wheel closes to -1.
  *Breaks when:* the angle is measured from the right, or a resting
  stick clears the highlight (a pad player's flick would be lost).

- **INV-8** — While the wheel is open, `Walker`'s commands keep the
  view's yaw and pitch from the frame it opened and send no fire, and
  each carries `wheelOpen`. A release on a highlighted weapon sends its
  pick on the next tick.
  *Test:* `tests/unit/ClientWalkerTest.cpp`, frames with the right
  stick and the fire trigger held while the wheel is open.
  *Breaks when:* the stick turns the view while pointing, or the
  trigger still fires.

- **INV-9** — The default bindings hold `Action::Wheel` on Q and on the
  right shoulder; the right shoulder asks for nothing else; South and
  Space still ask for `Up`. `pointRight` and `pointDown` are the right
  stick's position past its dead zone, inside the unit circle, the same
  at any frame time.
  *Test:* `tests/unit/InputTest.cpp`.
  *Breaks when:* R1 still jumps (every open would jump), or the pointer
  is scaled by `seconds`.

- **INV-10** — `layOutOverlay` places each label centred where asked,
  frames a highlighted one, and drops one that would leave the image or
  touch the text's box or an earlier label.
  *Test:* `tests/unit/RenderOverlayTest.cpp`.
  *Breaks when:* a label is drawn over the HUD text.

## 6. Failure modes

- **A second human joins while time is slowed** — INV-1 gives 1 on the
  next tick.
- **The player dies with the wheel open** — the wheel stays open on
  its list. A pick of a weapon the respawned body lacks is refused
  (INV-6).
- **A frame at 20% runs no tick** — a pick waits for the next tick, as
  a number key does (§ 4.8). A frame at 60 fps runs a tick about one
  frame in four.
- **A scale of 0 or not finite reaches `advance`** — it counts as 1
  (INV-3). A pause needs a new ADR and another mechanism.
- **A long stall** — `MAX_TICKS_PER_ADVANCE` still caps it.
- **An empty wheel** (nothing carried) — it opens, slows time, and
  closes to no pick.

## 7. Tests

Unit tier, label `unit`, on every leg of GitHub's matrix:

- `GameTest.cpp`: INV-1, INV-2, INV-4, INV-5, INV-6.
- `FixedTickTest.cpp`: INV-3.
- `UiWheelTest.cpp` (new): INV-7.
- `ClientWalkerTest.cpp`: INV-8.
- `InputTest.cpp`: INV-9.
- `RenderOverlayTest.cpp`: INV-10.

Each new case is seen failing before its code lands.
`scripts/mutations/ugame.py` gains entries for INV-1 and INV-6. INV-3
is mutated by hand: `uworld` has no mutation file.

## 8. Alternatives considered (and rejected)

- **Scale wall time in the client only**, as the prototype did. Lost:
  a server would run full speed against a slowed client (rules 8 and 9).
- **Scale `dt` inside the tick.** Lost: every movement constant was
  fitted at a fixed `dt` (UTA-0017 § 4.3), so a slowed tick would move
  differently, not just slower.
- **Send a pick as a repeated `weaponSlot`**, as the prototype did.
  Lost: a slot press cycles a shared slot, so it cannot name one
  weapon, and the wire schema should carry what was picked.
- **Point the wheel in `ugame`.** Lost: how a pointer maps to an item
  is drawing, `uui`'s under rule 13. `ugame` keeps what may be picked.
- **Slow the level's animation and sound too.** Lost on cost (§ 3
  item 4).

## 9. Out of scope

- Sending the command over the network — UTA-0026.
- Rebinding the wheel — UTA-0120.
- Weapon icons on the ring — with UTA-0467's weapon models; not yet queued as its own item.
- Slowing sound — UTA-0056's mixer plays on the wall clock (§ 3 item 4).
- A pause — needs a new ADR (ADR-0009).

## 10. What checks this

| Invariant | Checked by |
|---|---|
| INV-1, INV-2, INV-5, INV-6 | `GameTest`, and the `ugame` mutation file |
| INV-3 | `FixedTickTest`, mutated by hand |
| INV-4 | `GameTest` on GitHub's matrix |
| INV-7 | `UiWheelTest` |
| INV-8 | `ClientWalkerTest` |
| INV-9 | `InputTest` |
| INV-10 | `RenderOverlayTest` |
| § 3 item 3, the wheel feels right on a pad | The user, playing it |
| § 4.8, stand-ins slow with the game | **nothing** — `main.cpp` passes the seconds; no test drives it |

## 11. Cross-doc impact

- UTA-0017's spec § 4.2 and § 4.7: a pointer to § 4.2 and § 4.4 here,
  as § 4.2 there already points to UTA-0019. § 4.8 there: R1 no longer
  jumps.
- UTA-0019's spec § 4.5: a pointer to § 4.3 and § 4.5 here.
- `docs/design.md` § The parts: `uui` exists; its first code is the wheel.
  Its **Time** bullet: the open wheel's scale.
- `src/uinput/Input.h`'s `defaultBindings` comment and `Cli.cpp`'s
  help: R1 and Q open the wheel.

## 12. Cold-eyes loop log

No review: the gate is cancelled here (`CLAUDE.md` § Contract
reviews). Building it is the reviewer. Record:
`docs/reviews/UTA-0020-weapon-wheel-loop-log.md`.

## 13. Resource cost

A new static library, `uta_uui`, linked by `ut-ants` and the unit
tests. The wheel lays out at most one label per weapon in the set,
each frame it is open. Unmeasured; `ut-bench frame` shows it if it
costs anything.

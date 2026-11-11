<!-- ants-spec-format: 1 -->
# UTA-0017 — UT99's movement rules on a fixed tick

**Status:** accepted (2026-11-10), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0017 (design-2026-09-03; split by the user 2026-11-10).
**Blocker for:** UTA-0377.

**Layman:** Running, jumping, dodging and falling move exactly as in UT99 played on a good machine, the same at every frame rate, with the player's feet on the floor.

## 1. Goal

`uworld` gains the player's movement: walking, falling, air control,
jumping, dodging, ground friction, stepping up and walking off ledges.
A tick takes a body's state and one input command and returns the next
state, using UTA-0457's `BodyWorld` to stop at what it hits. It copies
the original's rules tick for tick, at the original's 85 fps, and runs
on a fixed tick, so a player moves the same at every frame rate.
`ut-ants` gains a walking mode, so the movement can be felt.

## 2. Problem

Nothing in `uworld` moves a body by UT99's rules; `ut-ants` has only a
fly camera (`apps/ut-ants/FlyCamera.h`). UTA-0456 recorded the original
(`~/.cache/ut-ants/movement/20261110-130929/`, `DM-Deck16][`, 15 runs at
30, 60, 85, 120 and 200 fps). What those recordings show, measured
2026-11-10 by throwaway scripts over them (§ 7's real case prints each
figure again):

- **The original's movement depends on its frame rate.** Letting go of
  the keys at 400 units/s, a walking player brakes by `1 − 16·dt` a tick
  (`dt` in game seconds), exactly, at 60, 85, 120 and 200 fps. So the
  slide to a stop is 15.1 units at 30 fps, 17.1 at 60, 19.2 at 85 and
  19.7 near 90 (`run-release`; 120 and 200 reached only about 90 fps).
  At 30 fps the braking is about 0.5 a tick, not `1 − 16·dt`'s 0.40;
  why is not yet known (§ 14).
- **The game runs at 110% speed.** The `PAWN` line records
  `TimeDilation=1.1` (deathmatch's default Hardcore mode), and every
  `dt` is in game seconds. Its measured constants, `JumpZ=357.5`
  included, are at that speed.
- **A walking player hovers 4.8 above the floor and slides 0.24 to 0.8
  off a wall**, the gap shrinking as the rate rises (UTA-0457 § 2).
- **A dodge** leaves at 600 units/s across and about 210 up
  (`dodge-forward`, 85 fps: `vz` 196.2 one tick of 0.0144 s after
  take-off, under gravity 950). Landing from it keeps a tenth of the
  horizontal speed (596.5 to 59.7), and no new dodge starts for about
  0.35 s (`DODGE_Done`, 26 ticks).
- **A player walking off a ledge** keeps its running speed into the fall
  (`ledge`, 85 fps).

Design rule 12 (`docs/design.md`) has a tick consume an input command,
never a device. `docs/design.md` § Time has the simulation run on a
fixed tick with rendering interpolating between ticks; § Determinism
bans the platform maths library from the simulation.

## 3. Scope decisions (agreed with the user)

1. **Movement is the same at every frame rate** (user, 2026-11-10). The
   simulation runs on a fixed tick; frames draw between ticks.
2. **The tick copies the original at 85 fps** (user, 2026-11-10): 85
   ticks a second. It is the fastest rate the original held in every
   recording, so ours can be checked against it tick by tick.
3. **The game runs at 110% speed by default, as UT99's deathmatch does**
   (user, 2026-11-10). Game speed is a server setting; a tick is
   `gameSpeed / 85` game seconds.
4. **Feet touch the floor and sides touch walls** (user, 2026-11-10). The
   original's hover and wall gap are not copied.
5. **Bodies bumping into each other waits for bots** (user, 2026-11-10):
   UTA-0062 and UTA-0025 take it. No recording has a second body.
6. **Every constant is measured from the recordings**, with how, in
   § 4.3's table.
7. **ut-ants allows a double jump** (user, 2026-11-10), which the
   original lacks. A second jump in the air rises exactly as high as the
   first. It is a rule, `airJumps`, so a menu can turn it off
   (UTA-0460). The default copies the original, so INV-2 replays
   recordings with none.

## 4. Design

### 4.1 Files

| File | Holds |
|---|---|
| `src/core/Angles.h` | § 4.6: sine and cosine of UT99's angle units, the same on every compiler, moved from `ubake/LightModel.h` |
| `src/uworld/Movement.h`, `.cpp` | § 4.2 to § 4.5: the command, the state, the rules and the tick |
| `src/uworld/FixedTick.h`, `.cpp` | § 4.7: wall time in, whole ticks and the draw's blend out |
| `src/uworld/BodyCollision.cpp` | a mover's rotation through `Angles.h` (§ 4.6) |
| `apps/ut-ants/Walker.h`, `.cpp` | § 4.8: the walking mode |
| `tests/unit/MovementTest.cpp`, `FixedTickTest.cpp` | § 7's unit cases |
| `tests/real/RealMovementTest.cpp` | § 7's recording case |
| `tests/real/RealLevel.h` | a map's level collision tree, moved out of `RealBodyCollisionTest.cpp` for both cases |

### 4.2 The command and the state

```cpp
namespace uta::uworld {

enum class DodgeDir : std::uint8_t { None, Forward, Back, Left, Right };

/// One tick's movement input (design rule 12). Axes are -1 to 1.
struct MoveCommand {
    double forward = 0, strafe = 0;
    std::int32_t yaw = 0;          // the view's, 65536 to a turn
    bool jump = false, walk = false, duck = false;
    DodgeDir dodge = DodgeDir::None;  // a dodge asked for this tick
};

enum class Physics : std::uint8_t { Walking, Falling };

struct MoveState {
    Vec3 location{}, velocity{};
    Physics physics = Physics::Walking;
    DodgeState dodge = DodgeState::None;  // None, Active or Done
    double dodgeDoneTime = 0;
    bool jumpHeld = false;  // the last command's jump: a held key jumps once
};

MoveState tick(const BodyWorld&, const BodySize&, const MoveRules&,
               const MoveState&, const MoveCommand&, double dt);
}
```

`tick` is a pure function of its arguments. `dt` is game seconds; the
game passes § 4.7's fixed value, and the recording tests pass each
row's own.

UTA-0019 § 4.6 (`docs/specs/UTA-0019-weapon-set.md`) adds `pitch` and
fire and weapon buttons to `MoveCommand`, and a push to a body's
velocity. A tick with no push moves as above.

**A double tap becomes a dodge before the command.** `DoubleTap`, in
`Movement.h`, takes each tick's held direction keys and `dt`, and
returns the `DodgeDir` a second press inside `DodgeClickTime` makes.
`ut-ants` and the tests both call it; a gamepad's dodge button sets
`dodge` directly (UTA-0018). While a dodge is in the air or just landed
(`dodgeReady` false), presses are forgotten, so no dodge chains off
another.

### 4.3 The rules and their constants

`MoveRules` holds the constants; its defaults are the player's. Each was
read from the `PAWN` line of every recording, or fitted to the rows:

| Constant | Value | Measured by |
|---|---|---|
| `groundSpeed`, `airSpeed` | 400 | `PAWN` |
| `accelRate` | 2048 | `PAWN` |
| `groundFriction` | 8 | `PAWN` (`ZoneGroundFriction`); braking fits `1 − 2·8·dt` |
| `gravityZ` | −950 | `PAWN` (`ZoneGravityZ`) |
| `jumpZ` | 357.5 | `PAWN` |
| `airControl` | 0.35 | `PAWN` |
| `maxStepHeight` | 25 | `PAWN` |
| `dodgeClickTime` | 0.25 | `PAWN` |
| `walkingPct` | 0.3 | `walk` and `duck-walk` top out at 120 and accelerate at 614.4 |
| `stopSpeed` | 10 | braking that leaves 9.987 stops dead; 10.065 does not (every run, every rate) |
| `dodgeSpeedXY`, `dodgeSpeedZ` | 600, 210 | § 2's dodge figures |
| `dodgeLandingKeep` | 0.1 | landing from a dodge keeps a tenth of the whole velocity |
| `dodgeDoneTime` | 0.35 | `DODGE_Done` to `DODGE_None`, `dodge-forward` 85 fps |
| `walkableFloorZ` | 0.7 | `slope-0707` walks up a ramp of normal Z 0.7071; `slope-0698` slides back off one of 0.6983 (2026-11-11, `UTA-0459`) |

How the rules combine, each fitted to the rows at 60 and 85 fps
(2026-11-10):

- **Walking, no key held:** `v −= 2·friction·dt·v`; reversing, or below
  `stopSpeed`, stops it.
- **Walking, a key held:** `v −= (v − push·|v|)·friction·dt`, turning the
  velocity towards the keys at its own speed, then `v += push·accelRate·dt`,
  capped at `groundSpeed`. Only the keys' direction counts.
- **Falling:** air control adds `push·airControl·accelRate·dt`. Below
  `airSpeed` it adds freely (`running-jump` took off at 399.996 and rose
  to 409.3); at or above it, it turns the velocity without speeding it
  (`air-strafe` took off at 400.087). Gravity adds `gravityZ·dt`, and the
  body moves by the mean of the old and new velocity.
- **A jump or dodge** sets the velocity, and the same tick then falls.
- **A jump pressed in the air is not held over to landing** (`dodge-jump`
  presses it for 8 ticks in the air and lands without jumping).

**A value recalled from UT99's source or its documentation is a
hypothesis until a recording confirms it.**

### 4.4 Walking and falling

- **Walking** accelerates the body along the floor towards the command's
  direction, brakes when no key is held, and sweeps with
  `BodyWorld::slide`.
- **Standing on the floor.** After each walking move, a sweep down of
  `maxStepHeight` finds the floor; the body ends `SKIN` above it. No
  floor within reach, or one too steep to stand on, starts falling with
  the velocity kept. A wall whose foot leans out can stop the sweep
  before the floor; the body then steps `SKIN` off it and sweeps again.
- **Stepping up.** A walking move that falls short tries again
  `maxStepHeight` higher, then sweeps back down: up to 25 units climbs,
  more stops. Falling short includes a lip of floor the body's rounded
  rim catches edge-on, which a slide cannot pass. A move stopped short
  leaves the velocity it managed.
- **Falling** applies gravity and air control and sweeps with `slide`.
  Hitting a floor it can stand on lands it: walking, the body `SKIN`
  above it.
- **Jumping** from walking sets `velocity.z = jumpZ` and falls.
- **Jumping in the air**, up to `airJumps` times before landing, sets
  `velocity.z = jumpZ`, whatever the fall's speed, so each rises as high
  as a jump from the floor. Landing gives them back. The jump key must
  be pressed again: a held key jumps once.

### 4.5 Dodging

A `dodge` command while walking, not ducking and not in `DODGE_Done`,
launches the body `dodgeSpeedXY` along the dodge's direction from the
view's yaw and `dodgeSpeedZ` up. Landing from a dodge applies § 4.3's
landing row. The recorded runs `dodge-forward`, `dodge-left` and
`dodge-jump` are the contract for its timing.

### 4.6 Angles without the maths library

`core::sineOf(std::int32_t)` and `core::cosineOf(std::int32_t)` take
UT99's angle units, 65536 to a turn. They reduce the angle to an eighth
of a turn in integers and evaluate a fixed polynomial there, using only
`+`, `−` and `×` in `double`, so every compiler gives the same bits. The
baker already had them (UTA-0112); the build moved them from
`ubake/LightModel.h` to `core/Angles.h`, unchanged, rather than write a
second copy. A dodge's
direction and the view's movement axes use them.
`BodyCollision.cpp`'s mover rotation, which today calls `std::sin` and
`std::cos`, moves onto them in the same change: a mover turning in a
networked game is simulation, and § Determinism bans the library there.

### 4.7 The fixed tick

```cpp
inline constexpr int TICKS_PER_SECOND = 85;
inline constexpr double DEFAULT_GAME_SPEED = 1.1;

class FixedTick {
public:
    explicit FixedTick(double gameSpeed = DEFAULT_GAME_SPEED);
    int advance(double wallSeconds);  // whole ticks now due
    double dt() const;                // gameSpeed / TICKS_PER_SECOND
    double blend() const;             // 0..1: how far the draw is past the last tick
};
```

The caller measures wall time and passes it in; `uworld` reads no clock.
A frame draws the body at `blend` between the last two tick states.
`advance` runs at most 8 ticks a call, dropping the rest, so a stall
does not freeze the game catching up.

### 4.8 Walking in `ut-ants`

A key toggles the fly camera and a walking body at the camera's place,
dropped to the floor. Keyboard, mouse and gamepad drive it through the
same `uinput` frame `FlyCamera` reads (UTA-0018), building a
`MoveCommand` a tick and `DoubleTap` for dodges. A pad's Dodge press
dodges the way the left stick points most. Its rules allow one air jump
(§ 3 item 7). G, or pressing a pad's right stick, toggles it; Space, the
bottom face button or the right trigger jumps, Ctrl or the left trigger
ducks, and Shift or pressing the left stick walks
(`apps/ut-ants/Cli.cpp`'s help).

**The eye** rests at `BaseEyeHeight` (27, `PAWN`) above the body's
centre. It copies UT99's `PlayerPawn.UpdateEyeHeight`, read from the
script text in `Engine.u`, run once a tick:

- **Walking**, the eye keeps its height in the world when the body
  steps. Each tick it then closes `min(1, 10·t)` of the gap to 27, `t`
  being a tick in real seconds. It stays between `−0.5·halfHeight` and
  `halfHeight` plus the tick's drop, the drop capped at `maxStepHeight`.
- **In the air, and on the tick it lands**, it closes at least 0.35 of
  the gap a tick.

So a stair lifts the view over several ticks, not in one.

## 5. Invariants

- **INV-1** — Movement is the same at every frame rate. A fixed script of
  commands run through `FixedTick` with frames of 1/30, 1/60, 1/144 s
  and an uneven mix gives bit-identical tick states.
  *Test:* `FixedTickTest.cpp`.
  *Breaks when:* a tick takes the frame's time as its `dt`.

- **INV-2** — Our rules copy the original's, tick for tick. Replaying
  every recorded run at 30, 60 and 85 fps, each row starting from the
  recording's position, velocity and physics and applying that row's
  command over its `dt`, every row matches the next: the same physics,
  horizontal position within 0.15, height within 0.15 once the
  original's hover is subtracted, and velocity within 1 unit/s. A run
  `summary.tsv` marks missed still counts: each row is compared under its
  own `dt`, never pooled into a rate. Exempt, and counted:
  - one tick either side of a change of physics, and a landing up to the
    original's hover early: falling, the original drops its hover before
    its box touches;
  - against a wall, velocity, and position across the wall: the
    original meets the level as an axis-aligned box, which reaches a
    turned wall sooner, and stops short by a gap (§ 3 item 4). Position
    along the wall is held within that reach;
  - walking friction on a tick over 0.025 s (§ 14);
  - a row whose start our body cannot fit, where the original's hover
    clears what ours touches.

  Each row starts from the recording because an open replay turns a
  hair the original decides by (a speed within float noise of
  `stopSpeed` or `airSpeed`) into drift for the rest of the run.
  *Test:* `RealMovementTest.cpp`, which prints each run's worst error.
  *Breaks when:* a constant or rule is wrong, or friction ignores `dt`.

- **INV-3** — Feet touch the floor. A walking body's centre stands
  `halfHeight + SKIN` above the floor, within 0.01, on a flat floor,
  after a step up, after a step down and after landing.
  *Test:* `MovementTest.cpp`, on a hand tree with a step.
  *Breaks when:* the original's 4.8 hover is copied, or the body floats
  after a step down.

- **INV-4** — A step of 25 units is climbed without leaving `Walking`; a
  step of 26 stops the body.
  *Test:* `MovementTest.cpp`.
  *Breaks when:* there is no step-up, or it has no limit.

- **INV-5** — A dodge comes only from a double tap inside
  `DodgeClickTime`: taps 0.2 s apart dodge, 0.3 s apart do not, and a
  dodge is refused in the air and during `DODGE_Done`.
  *Test:* `MovementTest.cpp`.
  *Breaks when:* a single press dodges, or a dodge chains off a dodge.

- **INV-6** — `sineOf` and `cosineOf` are exact at quarter turns, within
  1e-12 of the true value at all 65536 angles, and use no maths library.
  *Test:* `BakeLightModelTest.cpp`, which already held them to this
  before the move, comparing with `std::sin` inside the test only.
  *Breaks when:* the reduction is wrong in any octant.

- **INV-7** — Results match on every compiler. A fixed script of commands
  over a hand level, with a turning mover, hashes to one constant
  recorded in the test, on GCC, Clang and MSVC.
  *Test:* `MovementTest.cpp`, on GitHub's matrix.
  *Breaks when:* the simulation calls `std::sin`, `std::cos`,
  `std::hypot` or another library function that may round differently.

## 6. Failure modes

- A recording or the install missing: the real case fails and names
  what it lacks; it never skips.
- A body that starts inside solid (`BodyWorld::sweep`'s `startsInside`)
  does not move that tick and stays in its physics state.
- A stall longer than 8 ticks: `FixedTick` drops the rest (§ 4.7).

## 7. Tests

Unit cases on hand trees: INV-1 in `FixedTickTest.cpp`; INV-3, INV-4,
INV-5 and INV-7 in `MovementTest.cpp`; INV-6 in `BakeLightModelTest.cpp`.
INV-2's real case, label `real`, builds `DM-Deck16][`'s level collision
from the install and reads the recordings `UTA_MOVEMENT_RECORDINGS`
names, through `readMovementRecording`, as `RealBodyCollisionTest.cpp`
does. It prints each run's worst errors per rate, and how many rows each
exemption took.

## 8. Alternatives considered (and rejected)

- **Variable `dt` with the original's per-tick rules.** Movement would
  change with frame rate, as the original's does; the user rejected
  that.
- **Exact continuous-time rules** (friction as an exponential). The
  same at every rate, but matching no rate of the original, so nothing
  could check it tick by tick.
- **Replaying each recorded run open from its row 0** (INV-2 as first
  written). Building it showed the original decides two rules by a hair:
  a speed within float noise of `stopSpeed` or `airSpeed`. Replayed open,
  one such hair became a drift of several units for the rest of the run,
  so each row now starts from the recording.
- **60 or 120 ticks a second.** 60 copies a slower-feeling original;
  120 matches no rate the original held.
- **Jolt's `CharacterVirtual`.** Its stepping and floor rules are not
  UT99's (UTA-0457 § 8).

## 9. Out of scope

Bodies against bodies (UTA-0062, UTA-0025). Swimming and water
(UTA-0090). Landing damage (UTA-0108). Platforming assists (UTA-0021,
UTA-0022). Riding movers and teleporters (UTA-0205). The device-to-
command mapping beyond `ut-ants`' walking mode (UTA-0018). Drawing the
body (UTA-0458).

## 10. What checks this

| Invariant | Checked by |
|---|---|
| INV-1 | `FixedTickTest` |
| INV-2 | `RealMovementTest`, run by hand: nothing runs the real tier automatically |
| INV-3, INV-4, INV-5 | `MovementTest` |
| INV-6 | `BakeLightModelTest` |
| INV-7 | `MovementTest` on GitHub's matrix |
| § 4.8, the walking mode feels like UT99 | The user, playing it |

## 11. Cross-doc impact

- `docs/design.md` § Time: the tick is 85 a second and game speed 1.1.
- UTA-0457's spec § 11: its mover rotation no longer uses `std::sin`.
- UTA-0020: game speed is the tick's time scale it must agree with.
- UTA-0018: builds `MoveCommand` from every device.
- UTA-0062, UTA-0025: own bodies against bodies (§ 3 item 5).

## 12. Cold-eyes loop log

No review: the gate is cancelled here (`CLAUDE.md` § Contract
reviews). Building it is the reviewer. Record:
`docs/reviews/UTA-0017-movement-rules-loop-log.md`.

## 13. Resource cost

A tick is a few sweeps a body; its time is printed by the real case,
unmeasured until then.

## 14. Open questions

- Why walking friction on a long tick is not `friction·dt`. Braking
  holds `1 − 16·dt` to `dt` 0.0206 and departs above it: 0.5185 at
  0.0324, 0.5097 at 0.0367, 0.4570 at 0.0493, the same at every speed.
  Turning towards the keys departs the same way. Two half-ticks, a cap
  on `dt` and an exponential were tried (2026-11-10) and miss. Our tick
  is 0.0129 s, so the game never meets it; INV-2 exempts it.

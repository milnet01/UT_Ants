<!-- ants-spec-format: 1 -->
# UTA-0456 — record the original's player movement, tick by tick

**Status:** accepted (2026-11-10), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0456 (user-decision-2026-11-10, split from UTA-0017).
**Blocker for:** UTA-0457, UTA-0017.

**Layman:** A tool that plays set keypresses in the real UT99 and writes down exactly where the player goes, every frame, at several frame rates, so our movement can be checked against it.

## 1. Goal

`ut-record` runs Unreal Tournament 469 headless, as `ut-compare` does, and
plays scripted runs: a start pose and a list of keys held over time. For
every tick of every run it writes the player's position, velocity,
acceleration, physics state and the input applied that tick. It records each
run at several frame rates. UTA-0457 and UTA-0017 read these recordings
through one C++ reader, and UTA-0017 picks its fixed tick from them.

## 2. Problem

1. **Movement is to be measured, not guessed** (UTA-0017). Nothing logs the
   original's player per tick today. `ut-compare`'s `UTACam` mutator only
   poses a camera and takes a screenshot.
2. **The original's movement depends on its frame rate.** Engine
   `PlayerPawn.uc` (exported from the 469 install with `ucc batchexport`)
   shows it. In state `PlayerWalking`, `PlayerMove` counts the dodge
   double-tap window down by `DeltaTime` (`DodgeClickTimer`), and a jump
   pressed while the `Dodge` animation plays is held over (`bSaveJump`).
   Animation advances per tick too. The user wants our movement the same at
   every frame rate (2026-11-10), so the tick it copies must be chosen from
   recordings at several rates.
3. **This install is not the game as shipped.** Its `User.ini` sets
   `DodgeClickTime=0`, which turns dodging off; `DefUser.ini` ships `0.25`.
   Its `[Botpack.DeathMatchPlus]` sets `MinPlayers=16` and `InitialBots=4`, so
   a plain run fills with bots. A recorder that reads the player's own ini
   records the wrong game.

**How a key reaches movement** (Engine `PlayerPawn.uc`, Botpack, and this
install's `User.ini` aliases). `MoveForward` is `Axis aBaseY Speed=+300.0`;
`StrafeLeft` and `StrafeRight` are `Axis aStrafe` at `-300` and `+300`;
`Jump` is `Jump | Axis aUp Speed=+300.0`; `Duck` is
`Button bDuck | Axis aUp Speed=-300.0`; `Walking` is `Button bRun`. The
event `PlayerInput` reads `aBaseY` and `aStrafe` at its start to set the dodge
edges (`bEdgeForward`, `bWasForward` and the rest), then folds `aBaseY` into
`aForward`. `PlayerWalking.PlayerMove` scales `aForward` and `aStrafe` by 0.4
into `NewAccel` and calls `ProcessMove`. The exec function `Jump` sets
`bPressedJump`.

## 3. Scope decisions (agreed with the user)

- **Split, order and purpose:** recorder, then body collision (UTA-0457),
  then movement rules (UTA-0017). The user, 2026-11-10.
- **Movement is the same at every frame rate; the tick is chosen from these
  recordings.** The user, 2026-11-10.
- **The game as shipped, not this player's settings.** The session,
  2026-11-10: the recorder pins the shipped values (§ 4.4). A player who
  turned dodging off is not the reference.
- **Recordings are not committed.** The session, 2026-11-10, reading
  ADR-0003: a recording is derived from Epic's game running Epic's maps.
  Constants measured from them go into code with their measurement, as
  UTA-0017 already requires.
- **Input enters through a player subclass**, not through key events
  (§ 4.2, § 8). The session, 2026-11-10.

## 4. Design

### 4.1 Files

```
tools/common/ut_original.py        shared with ut-compare: the install copy, set_ini,
                                   headless_env, read_only, compiling a package,
                                   changed_since, free_display
tools/ut-compare/ut-compare.py     imports the above; behaviour unchanged
tools/ut-record/ut-record.py       the recorder
tools/ut-record/runs.txt           the scripted runs
tools/ut-record/UTARec/Classes/UTARec.uc       mutator: sequences the runs
tools/ut-record/UTARec/Classes/UTARecPawn.uc   player: applies keys, logs ticks
tests/support/MovementRecording.{h,cpp}        the C++ reader
tests/unit/MovementRecordingTest.cpp
```

`ut-compare`'s helpers move to `tools/common/ut_original.py` rather than
being copied. Compiling a package takes its name and source folder instead of
the `MUTATOR` constant.

### 4.2 How input is driven

`UTARecPawn` extends `Botpack.TMale1` and declares no `defaultproperties`,
so every movement value is the stock player's. The client URL asks for it
with `?Class=UTARec.UTARecPawn`; `TournamentGameInfo.Login` accepts any
subclass of `TournamentPlayer`.

It overrides `PlayerInput`. While a run is active it first writes the
tick's log row (§ 4.5), then sets the input variables to what the held keys
would leave, then calls `Super.PlayerInput`:

| Key | Letter | Sets |
|-----|--------|------|
| MoveForward / MoveBackward | `F` / `B` | `aBaseY` = +K / −K |
| StrafeLeft / StrafeRight | `L` / `R` | `aStrafe` = −K / +K |
| Jump | `J` | `aUp` = +K; on its first tick, calls `Jump()` |
| Duck | `D` | `bDuck` = 1, `aUp` = −K |
| Walking | `W` | `bRun` = 1 |

K is the value a held key leaves in its axis when `PlayerInput` starts. The
aliases say `Speed=300`; the native code that turns that into a value is not
local. So a calibration run (§ 4.6) measures K, and the mutator's config
holds the measured value. Measured 2026-11-10: 6000 on every held tick, at
30 and 120 fps.

While a run is active the pawn also zeroes the mouse and turn axes: turning
is out of scope (§ 9).

Everything after `PlayerInput` is the original's own code: the dodge edges,
`PlayerMove`, `ProcessMove` and native physics.

### 4.3 Runs

`runs.txt` holds up to 64 runs (the mutator's `Runs[64]`; the tool refuses
more). One run:

```
run <name> <map> <x> <y> <z> <yaw>
<seconds> <keys>
...
end <seconds>
```

`<keys>` is a string of the letters above, or `-` for none. A line holds from
its time until the next line's. Times are game seconds from the run's first
tick.

The mutator places the player at the pose, zeroes velocity, and waits until
the player has been `PHYS_Walking` and still for 0.5 s with `DodgeDir`
`DODGE_None`. The run's first tick is the next. A run ends at the first tick
at or past `end`. Then the next run starts. After the last, the mutator logs
`UTAREC DONE` and exits.

The first set covers what UTA-0457 and UTA-0017 need: standing still; run
forward, then release; strafe; forward and strafe together; walk; duck-walk;
jump in place; running jump; forward and side dodges; a jump pressed during a
dodge; strafing in the air; running into a wall at an angle; up a stair; off
a ledge. Poses are chosen on stock maps while building. Water, ladders,
movers as floors and weapons are out (§ 9).

### 4.4 Settings and frame rates

The recorder keeps its own install copy (`--work`, default
`~/.cache/ut-ants/record/install`), separate from `ut-compare`'s, so its ini
edits never reach a compare run. Before each launch it sets:

| Ini section | Key | Value |
|-------------|-----|-------|
| `Engine.PlayerPawn` (User.ini) | `DodgeClickTime` | `0.25` |
| `Botpack.DeathMatchPlus` | `bHardCoreMode` | `True` |
| `Botpack.DeathMatchPlus` | `AirControl` | `0.35` |
| `Botpack.DeathMatchPlus` | `MinPlayers`, `InitialBots` | `0` |
| `Engine.GameInfo` | `GameSpeed` | `1.0` |
| `SDLDrv.SDLClient` | `FrameRateLimit` | the run's rate |
| `SDLDrv.SDLClient` | `UseJoystick` | `False` |
| `Engine.Input` (User.ini) | every `Joy…` binding | empty |
| `OpenGLDrv.OpenGLRenderDevice` | `FrameRateLimit`, `UseVSync` | `0`, `False` |

Hardcore mode makes `DeathMatchPlus.SetGameSpeed` set `TimeDilation` to
1.1 (`bChallengeMode` is set only by `ChallengeDMP`). The viewport is 640x480, so drawing does not hold the frame rate down.

The joystick rows exist because this machine's game controller reaches the
client. This install binds `JoyX` to `aStrafe`, and the first calibration
logged its drift on every tick. The render device has a cap of its own; `0`
leaves the client's in charge.

The rates are 30, 60, 85, 120 and 200 frames a second: one client launch per
map and rate, running every run on that map. The log records the achieved
rate; the cap is only a request (§ 6). Measured 2026-11-10, load average
about 13: the 120 and 200 fps caps reached about 90 fps under Xvfb, and
every such run was marked `missed`.

### 4.5 The recording

The client log carries one line per event, prefixed `UTAREC`. The tool
writes each run's lines, prefix and log decoration removed, to
`<out>/<run>-<rate>.utarec`. So a file's lines begin with the version. `<out>` defaults to
`~/.cache/ut-ants/movement/<time>/`.

```
UTAREC 1 BEGIN run=<name> map=<map> rate=<cap> pose=<x>,<y>,<z>,<yaw>
UTAREC 1 PAWN class=<class> <key>=<value> ...
UTAREC 1 T <tick> <time> <dt> <keys> <fwd> <strafe> <x> <y> <z> <vx> <vy> <vz> <ax> <ay> <az> <physics> <dodge> <anim> <base>
UTAREC 1 END run=<name> ticks=<n>
```

- `1` is the format version.
- `PAWN` lists the live values at the run's start: `GroundSpeed`,
  `AirSpeed`, `AccelRate`, `JumpZ`, `AirControl`, `MaxStepHeight`,
  `CollisionRadius`, `CollisionHeight`, `DodgeClickTime`, `BaseEyeHeight`,
  `Mass`, `TimeDilation`, the zone's `ZoneGravity.Z` and
  `ZoneGroundFriction` (as `ZoneGravityZ` and `ZoneGroundFriction`), and
  `Bots`, the number of bots in the level (INV-2).
- A `T` row is written at the start of `PlayerInput`. Its state columns
  (`x` to `base`) are as the previous tick left them. `keys`, `fwd` and
  `strafe` are this tick's input: the letters held, and `aBaseY` and
  `aStrafe` as `Super.PlayerInput` receives them. `time` is
  `Level.TimeSeconds`; `dt` is `PlayerInput`'s `DeltaTime`.
- `physics`, `dodge` and `anim` are names (`PHYS_Walking`, `DODGE_None`,
  the `AnimSequence`); `base` is the actor's name or `None`.
- Numbers are UnrealScript's float-to-string, six decimals (as `UTACAM POSE`
  lines show).

So UTA-0017 replays a run as: start from row 0's state; apply row k's input
over row k's `dt`; compare with row k+1's state.

The tool also writes `summary.tsv`: per run and rate, the achieved rate
(median real frame time), the spread of `dt`, the end position, the highest
rise above the start, and the highest horizontal speed. A run that missed its
rate is marked `missed` (INV-7).

### 4.6 Calibration

`ut-record --calibrate` runs one script with no scripted input. Under Xvfb,
`xdotool` holds the forward and strafe keys for a second each, and
`UTARecPawn` logs `aBaseY` and `aStrafe` as `PlayerInput` receives them. The
tool reports K, or fails if the value is not the same on every held tick.
The measured K goes into `UTARec.uc`'s default config. Calibration is run
once, and again only if the input path changes.

### 4.7 The reader

```cpp
namespace uta::test {
struct RecordedTick {
    std::int64_t tick;
    double time, dt;
    std::string keys;                  // "-" for none
    double forward, strafe;            // aBaseY, aStrafe
    std::array<double, 3> location, velocity, acceleration;
    std::string physics, dodge, anim, base;
};
struct MovementRecording {
    int version;
    std::string run, map;
    double rate;
    std::array<double, 4> pose;        // x, y, z, yaw
    std::map<std::string, double, std::less<>> pawn;
    std::string pawnClass;
    std::vector<RecordedTick> ticks;
};
uta::Result<MovementRecording> readMovementRecording(std::string_view text);
}
```

It refuses a version other than 1, a missing `BEGIN`, `PAWN` or `END`, an
`END` count that differs from the rows, a row with the wrong column count or
a field that does not parse, rows whose ticks are not 0, 1, 2 in order, and
rows whose times step by other than the later row's `dt` (INV-4).
Numbers parse with `std::from_chars`.

## 5. Invariants

- **INV-1** — A recording run never writes the live install. The client runs
  under `read_only`, from a copy, and the run fails if `changed_since` finds a
  file in the install's System folders changed.
  *Test:* manual: run `ut-record`, then `find <install>/System64 -newer <stamp>`
  finds nothing.
  *Breaks when:* the client starts without the `bwrap` prefix, or a program or
  `.so` in the copy is a link to the live install.

- **INV-2** — Every recording is of the game as shipped: its `PAWN` line shows
  `DodgeClickTime=0.25`, `AirControl=0.35` and `TimeDilation=1.1`, and no
  bot joined. The tool fails a run otherwise.
  *Test:* `ut-record`'s header check, run on each recording.
  *Breaks when:* an ini edit misses its section, so this install's
  `DodgeClickTime=0` or its bots reach the run.

- **INV-3** — The recorded player is the stock player: `PAWN` names
  `UTARecPawn`, and `UTARecPawn.uc` declares no `defaultproperties`.
  *Test:* the header check (class), and
  `grep -c defaultproperties tools/ut-record/UTARec/Classes/UTARecPawn.uc`
  → `0`.
  *Breaks when:* the URL's `?Class=` is refused and the stock class spawns,
  or the subclass gains a default.

- **INV-4** — Consecutive rows are consecutive ticks:
  `time[k+1] − time[k]` equals `dt[k+1]` to within 1e-4 s for every k.
  *Test:* `MovementRecordingTest.cpp`, on a fixture with a gap.
  *Breaks when:* the pawn logs from somewhere other than once per
  `PlayerInput`, or a tick is skipped. If the first real recording shows the
  engine advances time after `PlayerInput`, it is `dt[k]`; that is a
  Step 8 fold-back.

- **INV-5** — Row k's keys are the script's keys at game time
  `time[k] − time[0]`.
  *Test:* `ut-record`'s script check, run on each recording.
  *Breaks when:* the pawn counts ticks or reads wall time instead of
  `Level.TimeSeconds`.

- **INV-6** — Scripted input reaches `PlayerInput` with the same values a
  held key gives.
  *Test:* `ut-record --calibrate` measures K; a scripted run of the same
  keys logs `fwd` and `strafe` equal to it.
  *Breaks when:* K is set by hand, or the input path scales a held key by
  the frame time.

- **INV-7** — A run whose median real frame time is more than 3% from its
  cap, or whose longest `dt` is more than 1.5 times its shortest, is marked
  `missed` in `summary.tsv`, never pooled with the runs that met it.
  *Test:* manual: a 200 fps run on a loaded machine is marked.
  *Breaks when:* `FrameRateLimit` is ignored and the tool trusts the cap.

- **INV-8** — A script whose shortest key interval is under two ticks at the
  run's rate is refused before launch (at 30 fps that is 2 × 1.1/30 s of game
  time).
  *Test:* manual: a dodge with a 0.05 s release, at 30 fps, is refused.
  *Breaks when:* a tap falls between ticks and vanishes from the run.

- **INV-9** — The reader refuses every malformed case in § 4.7, and reads a
  well-formed one exactly.
  *Test:* `MovementRecordingTest.cpp`, one fixture per refusal, each
  differing from the good fixture in one field only.
  *Breaks when:* a check is dropped, so a truncated log reads as a short run.

- **INV-10** — Recordings stay out of the repository: the tool refuses an
  `--out` inside it.
  *Test:* manual: `--out tools/` is refused.
  *Breaks when:* the check compares unresolved paths.

## 6. Failure modes

- **The client does not start or load the map** → no `UTAREC DONE` within
  the deadline; the tool fails and names the log, as `ut-compare` does.
- **`?Class=` is refused** → INV-3's header check fails the run.
- **The match does not start** (the player waits to spawn) → the mutator
  starts it; if no run begins within 30 s the tool fails.
- **The cap is not reached or not honoured** → the run is kept and marked
  `missed` (INV-7). The rates are a request; the log is the truth.
- **A pose is in a wall, or the player never settles** → the run is skipped
  after 5 s with `UTAREC SKIP <name>` and the tool reports it.
- **The player leaves `PlayerWalking`** (dies in a pit, falls into water) →
  the run is skipped with `UTAREC SKIP`, a dead player is restarted for the
  next run, and the tool reports it.
- **K varies with the frame time** → calibration fails; § 4.2 is amended to
  reproduce the measured rule (Step 8).
- **`xdotool` cannot reach the window** → calibration fails, naming it.
- **Two recorders share one `--work` copy** → not guarded; one at a time.

## 7. Tests

- `tests/unit/MovementRecordingTest.cpp`, label `unit`: INV-4 and INV-9. Each
  refusal fixture must fail against a reader without that check.
- The tool's own checks run on every recording: INV-2, INV-3, INV-5, INV-7.
  The gate does not run them; they need the install.
- Manual, once when built: INV-1, INV-6, INV-8, INV-10, and one full record
  at all five rates with `summary.tsv` read.

## 8. Alternatives considered (and rejected)

- **Real key events from `xdotool` for every run.** The truest input path,
  but sent on the wall clock: a press lands on whichever tick is running, so
  runs at two rates differ by timing, not by the engine. Kept for
  calibration only.
- **Set the axes from the mutator's `Tick`.** The player's own tick reads
  input after it, and what the native input read does to the axes is not
  local. The subclass sets them inside `PlayerInput`, where the read is
  already done.
- **Set `Acceleration` directly.** Skips `PlayerInput`, the dodge edges and
  `PlayerMove`, which UTA-0017 must copy.
- **A fixed time step in the original** (a benchmark mode). Unverified for
  469, and it would record a game nobody plays. The cap gives real frames,
  and the log gives their real lengths.
- **Commit the recordings as test fixtures.** ADR-0003.
- **Extend `ut-compare` with a mode.** Different output, different ini
  settings and a different package; shared code moves to `tools/common`
  instead.

## 9. Out of scope

- Water, ladders, flying, movers as floors, weapon jumps — deferred; not yet
  queued.
- Turning during a run (mouse or turn keys) — deferred; not yet queued.
- Online play: a client predicting its moves (`ReplicateMove`). The recorder
  is standalone, where `PlayerMove` calls `ProcessMove` directly.
- Comparing our movement with a recording — UTA-0457 and UTA-0017.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `changed_since` in the tool, every run |
| INV-2 | the tool's header check, every recording |
| INV-3 | the tool's header check; **nothing** automatic for the source half |
| INV-4 | `MovementRecordingTest.cpp` |
| INV-5 | the tool's script check, every recording |
| INV-6 | **nothing** automatic — calibration, by hand |
| INV-7 | the tool's summary |
| INV-8 | the tool's script check before launch |
| INV-9 | `MovementRecordingTest.cpp` |
| INV-10 | **Partial:** the tool's `--out` check; nothing tests the check |

## 11. Cross-doc impact

- `docs/build-and-test.md`: a line on `ut-record` beside `ut-compare`.
- `CHANGELOG.md`: one Added line.
- UTA-0457 and UTA-0017's roadmap bodies: recordings are read through
  `readMovementRecording`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0456-movement-recorder-loop-log.md`.

## 13. Resource cost

A recording is about 200 bytes a row. Five rates of the first set, about
fifteen runs of up to five seconds, is under 20 MB. Recordings live in
`~/.cache/ut-ants/movement`, one folder per session; nothing deletes them.
A full record is one client launch per rate, each a map load plus the runs:
minutes, on the CPU (Xvfb draws with Mesa's software driver). New tool
dependency: `xdotool`, for calibration only.

## 14. Open questions

None. The first full record (2026-11-10, DM-Deck16][, all five rates)
settled each:

- Native physics runs after `PlayerInput` in the same tick: a jump pressed
  at row k shows a rising `vz` at row k+1, at every rate.
- INV-4 holds as written, with `dt[k+1]`: worst error 4e-6 s over 75
  recordings.
- K is 6000 (§ 4.2).

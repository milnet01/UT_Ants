<!-- ants-spec-format: 1 -->
# UTA-0021 — the landing marker and the take-off pitch

**Status:** accepted (2026-11-17), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0021 (design-2026-09-03; when each shows, settled by the user 2026-11-17).
**Blocked by:** UTA-0017, UTA-0019.

**Layman:** In the air, a ring on the floor shows where you will land, and follows as you steer. If that spot is below the bottom of the screen, the view tips down just far enough to show it. Your aim never moves: the crosshair slides up the screen instead.

## 1. Goal

A player crossing platforms in first person sees where they will land.
While the body is in the air, a marker sits where its current arc meets
a floor. When that spot is below the view, the drawn camera tips down,
at most `MAX_TIP`, until it is in frame. Neither touches the
simulation: the command a tick reads is the same with both on or off,
so **S2** holds (`docs/discovery.md`, **S11**). Each is the player's
own setting, on by default (`docs/design.md`, the movement-assist
bullet).

## 2. Problem

1. Nothing predicts where a body lands. `uworld::tick` is the only
   integrator. The design forbids a second one: gravity, jump impulse
   and air control are fitted constants (UTA-0017 § 4.3), and a copy
   would drift the first time one is tuned.
2. The drawn view is the aim. `Walker::eye` builds its `FlyCamera` from
   `pitch_`, the pitch every command carries, so the view cannot tip
   without moving the aim.
3. The crosshair is pinned to the image's centre (`layOutCrosshair` in
   `src/urender/Overlay.cpp`; UTA-0019 § 4.8). With a tipped camera the
   aim ray no longer passes through the centre.
4. There is no player-settings store yet (UTA-0297 is the menu), so a
   setting has nowhere to persist.

## 3. Scope decisions (agreed with the user)

1. **The marker shows only in the air** — after a jump, a dodge, a
   walk off a ledge or a push — never while walking (user, 2026-11-17).
   This is the roadmap item's "current jump arc".
2. **The view tips only as far as needed**, to bring the landing into
   frame, at most `MAX_TIP`, and not at all when it is already in view
   (user, 2026-11-17).
3. **Two settings, the marker and the pitch, each on by default**
   (the session's call, 2026-11-17). The design names them "the
   player's own setting" without saying one or two; two lets a player
   keep the marker and refuse a moving view. Until UTA-0297's menu they
   are `ut-ants` flags and are not saved.
4. **The arc assumes the player keeps holding what they hold now**:
   this frame's move and facing, with no new jump or dodge. Steering in
   the air moves the marker on the next frame. It follows from § 2
   item 1: the arc is the real tick, fed a held command.
5. **The marker is a flat translucent disc**, a `urender::Shape`
   cylinder the width of the body, drawn depth-tested on the floor. No
   arc line. This follows the standing fact on cheapest methods first;
   its look is the session's to tune.

## 4. Design

### 4.1 Files

| File | Change |
|---|---|
| `src/uworld/Movement.h`, `.cpp` | `Landing`, `LANDING_HORIZON_TICKS`, `projectLanding` (§ 4.2) |
| `src/uui/Platforming.h`, `.cpp` (new) | the marker's shape and the pitch's target and easing (§ 4.3, § 4.4) |
| `src/urender/Overlay.h`, `.cpp`, `Renderer.h`, `Frame.cpp` | the crosshair's height (§ 4.5) |
| `apps/ut-ants/Walker.h`, `.cpp`, `main.cpp`, `Cli.h`, `Cli.cpp` | projecting, tipping, the two flags (§ 4.6) |
| `tests/unit/` | `MovementTest`, `ClientWalkerTest`, `ClientCliTest`, `RenderOverlayTest`, and a new `UiPlatformingTest` (§ 7) |
| `tests/real/RealMovementTest.cpp` | a hidden case timing `projectLanding` on a real map (§ 13) |

### 4.2 The arc (`uworld`)

```cpp
/// Where a falling body first lands: its centre on that tick.
struct Landing {
    Vec3 location{};
    int ticks = 0; ///< ticks from the state given until it lands
};

/// Ticks looked ahead: two seconds at TICKS_PER_SECOND.
inline constexpr int LANDING_HORIZON_TICKS = 170;

/// Where `state` first turns Walking if `command` is held every tick,
/// with its jump and dodge cleared, `dt` game seconds a tick; nullopt
/// when `state` is not Falling or no landing comes within maxTicks.
/// A pure function: it calls `tick` and nothing else moves the body.
[[nodiscard]] std::optional<Landing> projectLanding(const BodyWorld& world, const BodySize& size,
                                                    const MoveRules& rules, const MoveState& state,
                                                    const MoveCommand& command, double dt,
                                                    int maxTicks = LANDING_HORIZON_TICKS);
```

Clearing `jump` starts no jump, so an air jump the rules allow is not
assumed. A body that starts inside something does not move (`tick`'s own
comment), so it never lands and the answer is nullopt.

### 4.3 The marker (`uui`)

```cpp
namespace uta::uui {

/// The landing marker: a disc of the body's radius, centred under
/// `centre` (a Landing's location), lying on its floor.
[[nodiscard]] urender::Shape landingMarker(std::array<double, 3> centre, double radius, double halfHeight);

}
```

It is a `Shape::Kind::Cylinder`, `halfHeight` 1, its centre 1.5 units
above the body's feet (`centre.z - halfHeight`), translucent. It stands
upright, so on a slope part of it is hidden in the floor; that is
accepted for the first iteration (§ 3 item 5).

### 4.4 The pitch (`uui`)

```cpp
namespace uta::uui {

inline constexpr double MAX_TIP_DEGREES = 15;
/// The share of the way from the view's centre to its bottom edge the
/// landing must be within to count as in frame.
inline constexpr double IN_FRAME = 0.8;
/// Seconds of wall time the tip takes to close most of its gap.
inline constexpr double TIP_SECONDS = 0.12;

/// The tip, in degrees down, that brings `landing` into frame from an
/// eye at `eye` aimed at `pitch` and `yaw` (UT units): 0 when it is
/// already in frame, above the view, behind the eye, or outside the
/// view's width; else just enough, at most MAX_TIP_DEGREES.
[[nodiscard]] double tipTarget(std::array<double, 3> eye, double pitch, double yaw,
                               std::array<double, 3> landing,
                               double horizontalFovDegrees, double aspect);

/// The tip after `seconds` of wall time, easing from `tip` toward
/// `target`: it closes 1 - exp(-seconds / TIP_SECONDS) of the gap.
[[nodiscard]] double easeTip(double tip, double target, double seconds);

}
```

The target is measured from the aim, not the tipped view, so the tip
does not feed back on itself. "In frame" uses the view's vertical half
angle `v`: the landing is in frame when its angle below the aim is at
most `atan(IN_FRAME * tan(v))`. The target is that angle less the
limit, clamped to `[0, MAX_TIP_DEGREES]`. Easing runs on wall seconds,
because the pitch is drawing, not game state; while UTA-0020's wheel
slows the game it eases at its usual speed.

### 4.5 The crosshair (`urender`)

`Renderer::setCrosshair(bool shown, double up = 0)` and
`layOutOverlay`'s crosshair take `up`: the crosshair's centre as a
share of the image's half-height above its centre. 0 is today's
crosshair. The existing rule still drops a crosshair that would not
fit or would touch the text's box.

For a camera tipped `t` below the aim with vertical half angle `v`, the
aim ray crosses the image at `up = tan(t) / tan(v)`. At the default 90°
horizontal field on a 16:9 screen and the full 15° tip that is about
0.48, inside the image.

### 4.6 What `ut-ants` does

`Walker` gains:

```cpp
struct Assists {
    bool landingMarker = true;
    bool takeOffPitch = true;
};
void setAssists(Assists assists) noexcept;
void setViewShape(double fov, double aspect) noexcept; ///< the drawn view's field and width over height
[[nodiscard]] std::optional<uworld::Landing> landing() const noexcept;
[[nodiscard]] double tip() const noexcept;      ///< degrees down, now
[[nodiscard]] double crosshairUp(double fov, std::uint32_t width, std::uint32_t height) const noexcept;
```

In `Walker::update`, after the frame's ticks:

- When any tick ran, `landing()` is `projectLanding` from the player's
  state with the last command sent. With the marker off, or the body
  not Falling, it is nullopt. With no tick, it keeps its last value.
- The tip eases toward `tipTarget` for that landing over the frame's
  wall seconds. The target is 0 with the pitch off, with no landing,
  or while zoomed (UTA-0019 § 4.8), where a moving crosshair would
  spoil the shot.

`Walker::eye` and `Walker::view` draw at `pitch_` less the tip, clamped
at `-FlyCamera::PITCH_LIMIT`. `pitch_`, and every command's `pitch`,
never see the tip. `main.cpp` gives `setViewShape` the view's field and shape before each
update, adds `landingMarker` to the shapes it sets and passes
`crosshairUp` to `setCrosshair`. The launcher passes both flags on to
the map it opens (`inheritedArguments`). `Cli.cpp` adds
`--no-landing-marker` and `--no-take-off-pitch`, and its help names
both. The fly camera has neither.

## 5. Invariants

- **INV-1** — `projectLanding` predicts the real landing exactly. For
  a body that jumps and then holds one command, the landing projected
  from the first Falling state equals, bit for bit, the location and
  tick on which ticking that command first leaves it Walking.
  *Test:* `tests/unit/MovementTest.cpp`: a jump forward off a box onto
  the floor below, holding forward and a turned yaw.
  *Breaks when:* the projection integrates on its own, uses another
  `dt`, or keeps the jump bit (an air jump would lengthen the arc).
  The projection is given a fresh jump press from a state whose key
  was let go, which isolates the last: only the cleared bit stops the
  air jump the fixture's rules allow.

- **INV-2** — `projectLanding` is nullopt for a Walking body and for a
  fall that meets no floor within `maxTicks`.
  *Test:* `tests/unit/MovementTest.cpp`: a body standing; a body above
  a floor too far below to reach in `maxTicks`.
  *Breaks when:* a Walking body reports a landing (the marker would
  sit under the player's feet), or an unfinished arc reports its last
  point.

- **INV-3** — The assists change no command and no game state. A
  `Walker` script that jumps, dodges, looks and fires sends the same
  commands, tick for tick, with both assists on as with both off, and
  its player's state is identical after each tick.
  *Test:* `tests/unit/ClientWalkerTest.cpp`, the same frames fed to two
  walkers; each tick compares `stateHash` and the body's aim, since a
  shot that hits nothing leaves no trace in the hash.
  *Breaks when:* the tip is written into `pitch_` or a command's
  `pitch` (the shot would follow the tipped view, failing **S2**).

- **INV-4** — `tipTarget` is 0 for a landing in frame, above the aim,
  behind the eye or outside the view's width. Below the frame it is the
  angle past `IN_FRAME`'s limit, capped at `MAX_TIP_DEGREES`.
  `easeTip` moves toward its target and never past it.
  *Test:* `tests/unit/UiPlatformingTest.cpp`: a landing just inside
  the limit, 0; 5° past it, 5; 40° past it, 15; behind and to the side,
  0; a step of 10 seconds lands on the target, and of 0 stays put.
  *Breaks when:* the limit is measured from the centre, not `IN_FRAME`
  of the way to the edge, or the cap is missing.

- **INV-5** — The crosshair sits on the aim ray. `crosshairUp` is
  `tan(t) / tan(v)` for the tip `t` and the drawn view's vertical half
  angle `v`, and 0 with no tip. `layOutOverlay` centres the crosshair
  `up` of the half-height above the image's centre.
  *Test:* `tests/unit/ClientWalkerTest.cpp`: a walker tipped by a set
  amount, its aim point projected through `view()` lands at
  `crosshairUp`; `tests/unit/RenderOverlayTest.cpp`: `up` of 0 and 0.5.
  *Breaks when:* the crosshair stays centred (the shot would land above
  it), or `up` is measured from the top.

- **INV-6** — With the marker off, `landing()` is always nullopt. With
  the pitch off, `tip()` is always 0.
  *Test:* `tests/unit/ClientWalkerTest.cpp`, a jump off a ledge with
  each assist off, and the pitch turned off mid-air.
  *Breaks when:* a flag is read only at start, one turns off both, or
  the pitch eases back instead of stopping.

## 6. Failure modes

- **The arc crosses a teleporter** — the marker shows where the body
  would land without it. The teleport moves the body; the next frame's
  projection starts from there.
- **The arc ends in lava or a pit with a floor** — the marker shows it.
  That is the warning the marker is for.
- **No floor within the horizon** — no marker, no tip.
- **A mover is under the arc** — the projection sees the world as the
  tick does, at that moment.
- **The player looks straight down** — the drawn pitch is clamped at
  `-PITCH_LIMIT`; the crosshair still follows the tip actually drawn.
- **The wheel is open** — the game slows (UTA-0020); the marker
  follows the slowed body, and the tip eases on the wall clock.

## 7. Tests

Unit tier, label `unit`, on every leg of GitHub's matrix:

- `MovementTest.cpp`: INV-1, INV-2.
- `ClientWalkerTest.cpp`: INV-3, INV-5's walker half, INV-6.
- `UiPlatformingTest.cpp` (new): INV-4.
- `RenderOverlayTest.cpp`: INV-5's layout half.

`tests/real/RealMovementTest.cpp` holds a hidden case that prints
`projectLanding`'s cost (§ 13). It asserts nothing about speed.

Each new case is seen failing before its code lands. No file in
`scripts/mutations/` covers `uworld`, `uui` or the walker, so each
invariant is mutated by hand.

## 8. Alternatives considered (and rejected)

- **A second integrator for the arc.** Lost: it drifts from the
  fitted constants the first time one is tuned (`docs/design.md`).
- **Tip the view by changing the view angles.** Lost: the aim would
  move, so the shot would go where the player did not point (**S2**).
- **A fixed tip on every jump.** Lost to the user's choice (§ 3
  item 2): it moves the view when the landing was already visible.
- **Show the marker while walking too.** Lost to the user's choice
  (§ 3 item 1): a mark would always sit on the floor ahead.
- **Draw the marker as an overlay mark.** Lost: the overlay has no
  depth, so the mark would show through walls and not lie on the floor.
- **One setting for both.** Lost: a player may want the marker without
  a moving view (§ 3 item 3).

## 9. Out of scope

- Saving the settings, and a menu for them — UTA-0297 and UTA-0120.
- An arc line, or a marker that lies along a slope — later polish.
- The physical assists — UTA-0022.
- Sending anything over the network — nothing here is sent; UTA-0026.

## 10. What checks this

| Invariant | Checked by |
|---|---|
| INV-1, INV-2 | `MovementTest`, mutated by hand |
| INV-3 | `ClientWalkerTest`, mutated by hand |
| INV-4 | `UiPlatformingTest`, mutated by hand |
| INV-5 | `ClientWalkerTest` and `RenderOverlayTest`, mutated by hand |
| INV-6 | `ClientWalkerTest`, mutated by hand |
| **S11**'s first half: platforms can be crossed without falling off for want of seeing the feet | The user, playing it |
| § 4.3, the marker reads well on the floor | **nothing** — its look is tuned by eye |
| § 4.6, `main.cpp` adds the marker and passes `crosshairUp` | **nothing** — no test drives `main.cpp` |

## 11. Cross-doc impact

- UTA-0017's spec § 4.8: a pointer to § 4.6 here, for the drawn view.
- UTA-0019's spec § 4.8: the crosshair follows § 4.5 here.
- `docs/design.md` § The parts: `uui` also holds the platforming
  assists.
- `apps/ut-ants/Cli.cpp`'s help: the two flags.

## 12. Cold-eyes loop log

No review: the gate is cancelled here (`CLAUDE.md` § Contract
reviews). Building it is the reviewer. Record:
`docs/reviews/UTA-0021-landing-marker-loop-log.md`.

## 13. Resource cost

While the body is in the air, each update that runs a tick runs up to
`LANDING_HORIZON_TICKS` movement ticks for the projection.

Measured 2026-11-19 on DM-Deck16][ over three runs, at a load average
of 2 to 4 on 12 cores: 2.2 to 2.4 µs a tick, so 0.38 to 0.41 ms for a
full horizon on average. A fall that finds a floor stops early. The
slowest single projection, a full horizon and the same one each run,
took 1.6 to 1.75 ms (`UTA-0479`). Measure with
`uta_real_asset_tests "[timing]"` (the real-asset tier).
Projecting every other tick, the fallback this section first named,
was not taken: it halves the average but not the slowest frame.

<!-- ants-spec-format: 1 -->
# UTA-0387 — fade to the sky only where the sky reaches

**Status:** accepted (2026-10-21), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** fix.
**Source:** ROADMAP UTA-0387 (user, 2026-10-19: TheBoat's haze lights the closed decks).
**Pairs with:** UTA-0391.

**Layman:** The distance mist only shows where daylight can reach the air, so rooms sealed from the sky stay dark.

## 1. Goal

The outdoor haze (UTA-0337 part 3) fades a surface toward the sky only as far
as the sky can be seen near it. A sealed room in a zone that also holds the
outdoors draws no outdoor haze; open ground draws it as now.

## 2. Problem

`scene.frag`'s outdoor haze mixes a surface toward `horizonSkyAt` by
`outdoorHaze(distance)`, wherever the surface's zone has a sky window
(`zones[zone].sky`). Its only test of the sky is the zone. TheBoat has a
painted sky and one zone, so the hull's holds are hazed like the open deck.

1. Measured 2026-10-21 from capture `MH-()mG-TheBoat-V2mini-BP-20261019-165909Z`
   (the forward hold; bake `81aea088…`, ultra, 1280x720): the hold's far end
   draws blue-white; forcing `hazed` to 0 in `scene.frag` leaves it dark
   (scratch `~/.cache/uta-scratch/u387/onoff.png`). The volumetric haze
   (`fog_scatter.comp`) is not the cause: it scatters only lights with shadow
   faces, and the sun has none.
2. It undoes UTA-0344, whose lamps were tuned against a dark lower deck.

## 3. Scope decisions (agreed with the user)

None with the user. The session's calls, each recorded here:

- The sky's reach is baked per probe, as the sun's is (UTA-0338 § 4.4), not
  computed at load or per map by recipe.
- The haze reads the sky's reach at the surface, not along the view ray.

## 4. Design

### 4.1 `skySeen` per probe — `ubake`

`LightProbes::skySeen` is, for each probe, the share of `directions()` along
which `sunSeen(p, w, rays)` passes from the probe's point: the ray first meets
a `PF_FakeBackdrop` surface. The bake flags a recipe's painted-sky surfaces
`PF_FakeBackdrop` (UTA-0350), so both kinds of sky count.

- **Empty exactly when no `GEOM` batch carries `PF_FakeBackdrop`.** A level
  with no sky draws no outdoor haze (`outdoorExtent` is 0), so it needs none.
- Gathered in `bakeLightProbes`'s per-probe job, beside `sunSeen`.
- `BAKER_REVISION` becomes 62.

### 4.2 The `LPRB` field — `ubundle`

```cpp
struct LightProbes {
    // ... as now ...
    /// UTA-0387: the share of directions in which each probe sees the sky,
    /// in [0, 1], one per probe in `probes`' order -- or none.
    std::vector<float> skySeen;
};
```

- Written after `sunSeen`, as a `u32` count then `f32` values.
- **Refused on read and on write:** a size neither 0 nor `|probes|`; a value
  outside [0, 1] or not finite.
- `FORMAT_VERSION` becomes 30.

### 4.3 The haze — `urender`

- `Probes.cpp` puts `skySeen` in the second face's spare fourth float,
  `faces[1].a`; 0 where LPRB holds none.
- `shapeOf` samples `skySeen`'s ends, as it samples `sunSeen`'s.
- `probes.glsl` gains `skyOpenAt(lattice, x)`: `skySeen` blended over the
  cell's corners as `sunSeenAt` blends, **and 1 where no corner is present**.
  The probes are seeded near the play area; past their reach stands the far
  scenery the haze exists to fade.
- `scene.frag`'s outdoor haze becomes

```glsl
float hazed = outdoorHaze(distance(frame.eye, worldPosition))
            * min(1.0, skyOpenAt(lattice, worldPosition + surface * (0.5 * float(frame.probeSpacing))) / OPEN_SKY_SHARE);
```

  with `OPEN_SKY_SHARE = 0.5`: a point on open flat ground sees the upper
  half of its directions, so open ground keeps the haze it has now.

## 5. Invariants

- **INV-1** — `LPRB` round-trips `skySeen`, and `read` and `write` each refuse
  a size neither 0 nor `|probes|`, and a value outside [0, 1].
  *Test:* `tests/unit/BundleLightProbesTest.cpp`.
  *Breaks when:* the reader skips the vector, or either size or range check
  is dropped.

- **INV-2** — The bake leaves `skySeen` empty on a level with no
  `PF_FakeBackdrop` batch. On one with such a batch, a probe under a backdrop
  roof has `skySeen` above 0, and a probe the roof is hidden from by a plain
  floor has `skySeen` 0.
  *Test:* `tests/unit/BakeSkyTest.cpp`.
  *Breaks when:* the share counts a ray that meets nothing or meets a plain
  surface as sky, or the vector is filled on a level with no sky.

- **INV-3** — A surface whose probe corners all hold `skySeen` 0 draws no
  outdoor haze; with the probes' `skySeen` set to 0.5 it draws the haze it
  drew before this item; with no probe corner present it is hazed fully.
  *Test:* `tests/device/RenderSunTest.cpp`, one bundle edited in place.
  *Breaks when:* `scene.frag` ignores `skyOpenAt`, `skyOpenAt` returns 0 with
  no corner, or `Probes.cpp` loads `skySeen` into the wrong float.

- **INV-4** — A bundle differing from the last only in `skySeen` re-uploads
  its probes.
  *Test:* `tests/device/RenderSunTest.cpp`, one bundle edited in place, as
  its `sunSeen` case.
  *Breaks when:* `shapeOf` does not sample `skySeen`.

## 6. Failure modes

- **A probe-less surface indoors** keeps the full haze. Probes are seeded
  within one spacing of every non-sky triangle inside the reach, so this is a
  surface outside the reach, which is scenery.
- **A probe beside an opening** sees some sky, so a doorway's inside edge
  keeps part of the haze. That is light reaching the air there.
- **A map whose open ground is mostly walled in** sees less than half its
  directions as sky and draws less haze than now. Measured on TheFortress
  and TheBoat before shipping (§ 7).

## 7. Tests

INV-1 in `BundleLightProbesTest`, INV-2 in `BakeSkyTest` (label `unit`);
INV-3 and INV-4 in `RenderSunTest` (label `device`). Each is seen to fail
against the code before this item; eight hand mutants of § 4, one per rule,
each turned its test red (2026-10-21).

Measured by hand, 2026-10-21, mean sRGB per frame, baker 62 against 61:

- TheBoat's hold capture: (84.6, 78.9, 74.5) before, (79.8, 72.1, 65.1)
  after, against (79.7, 72.0, 65.0) with the outdoor haze forced off. The
  blue-white far end is gone.
- TheBoat's deck capture: (196.9, 212.1, 238.5) in the capture's own frame,
  (191.5, 204.2, 228.5) after.
- TheFortress's eight poses, 640x360: 9 to 19 levels darker each. Its plain
  is walled by canyon, so much of it sees less than half its directions as
  sky (§ 6). The castle shows through again; the edge mountains still fade.

## 8. Alternatives considered (and rejected)

- **The sun's share as the reach.** Free, but a shaded outdoor face, such as
  the far side of the edge mountains, would lose its haze and the map's box
  would show again.
- **A recipe box marking indoor space.** Per map and hand-made; every other
  map with enclosed rooms under one zone would need one.
- **Rays at load.** `SurfaceRays` belongs to `ubake`, and casting them for
  every probe on every load costs time the bake already spends once.
- **The reach along the view ray.** Truer for a camera inside looking out,
  but many samples a pixel; the surface's reach is one lookup.

## 9. Out of scope

- The haze's strength on surfaces it should reach, which swamps a castle at
  middle distance on TheFortress — tracked by UTA-0391.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleLightProbesTest.cpp` |
| INV-2 | `tests/unit/BakeSkyTest.cpp` |
| INV-3 | `tests/device/RenderSunTest.cpp` |
| INV-4 | `tests/device/RenderSunTest.cpp` |

## 11. Cross-doc impact

`CLAUDE.md` § Standing facts (format 30, baker 62); `CHANGELOG.md`;
`docs/specs/UTA-0112-baked-light-probes.md` § 4.2 gains a pointer to § 4.2
here.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0387-sky-seen-haze-loop-log.md`.

## 13. Migration / compatibility

Format 30 refuses format 29 bundles; every map is re-baked, as at every
format change.

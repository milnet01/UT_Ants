<!-- ants-spec-format: 1 -->
# UTA-0439 — chart the movers a map can never move

**Status:** accepted (2026-10-26), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** perf.
**Source:** ROADMAP UTA-0439 (user-request-2026-10-26, from UTA-0352).

**Layman:** Some maps build fixed glass and decoration out of parts that could move but never do; light them like walls, which is cheaper.

## 1. Goal

A mover the map can never move is baked as the level is: its surfaces get
AOCC and SMSK charts, and it shadows the level's texels in both. It is still
drawn, collided with and placed as a mover. A map with no such mover bakes
and draws as today.

## 2. Problem

1. **Every mover lights through the cluster path.** No mover vertex carries
   a chart (UTA-0326 § 4.5; `SceneGeometry::upload` gives AOCC and SMSK to
   object 0 alone), so each of its fragments runs `clusterLight` with
   `softShadowOf` per light.
2. **Some movers never move.** DM-48SC-InversalLiandri's glass panels are
   movers: 32 of its 33 carry `InitialState` `None`. On its two glass views at
   2560x1440 High, direct light is about 2.6 ms of a 3.2 ms translucent pass,
   and about 0.5 ms of that is special-lit (UTA-0438 INV-6, measured
   2026-10-26).
3. **Such movers are rare today.** `ut-dump --wiring-graph` over the survey's
   127 maps finds 37 of 825 movers with `InitialState` `None`, on 3 maps
   (UTA-0439's census, 2026-10-26). The user kept the item for maps to come.

**Why `None` means never.** Engine `Mover.uc` (ut-sdk-v469e) moves a mover
only through `DoOpen`, which is called only inside its states
(`BumpOpenTimed`, `TriggerOpenTimed` and the rest). `Actor.uc`'s
`SetInitialState` sends an empty `InitialState` to state `Auto`, and
`Mover.uc` declares no `auto` state. Its default is
`InitialState=BumpOpenTimed`, so only a mapper's explicit `None` stops it.
Two routes still move a stateless mover: `PostBeginPlay` bases a mover with
`bSlave` on its master, and an actor's `AttachTag` naming its `Tag` carries it
(`AttachMover`, `MixMover`).

## 3. Scope decisions (agreed with the user)

Decisions 1 and 2 were the user's, 2026-10-26. Decisions 3 to 5 are the
session's own, the same day.

1. **A mover the map can never move is baked as level geometry**: charted,
   masked and an occluder.
2. **Only movers that can never move.** Charting every mover at rest, with
   the cluster path while one moves, was offered and not chosen. It belongs
   with gameplay, when movers move.
3. **A still mover is drawn as a mover, not copied into `GEOM`.** It keeps
   UTA-0311's depth tie and its collision tree, and `MOVR` and `COLL` keep
   their order. Its chart entries follow the level's (§ 4.2).
4. **Still is decided only from what the bake can read**, and anything in
   doubt is not still (§ 4.1). A class other than `Engine.Mover` itself may
   move by its own script, so it is not still.
5. **Probes, strips and the sun's shadow tiles are unchanged.** A still mover
   occludes in AOCC and SMSK only.

## 4. Design

### 4.1 Which movers are still — `ubake`

`findMovers`' movers gain `still`, true exactly when all hold:

- its class's `path` is `engine.mover` and the class `resolved`;
- its resolved `InitialState` is a `Name` that folds to `none`. Absent from
  the actor and its class, it is not still;
- its resolved `bSlave` is absent or false;
- no actor in PLAC has a resolved `AttachTag` equal to its resolved `Tag`,
  folded. An `AttachTag` of `None` matches nothing.

### 4.2 The bundle — `ubundle`

- `MoverShape` gains `still`, one byte in `MOVR` after `postScale`, 0 or 1.
  The reader refuses any other value. `FORMAT_VERSION` goes to 33.
- AOCC's `uv` and SMSK's `vertexChart` and `vertexTexel` hold GEOM's vertices,
  then each still mover's, in `MOVR` order. The cross-section checks in
  `OcclusionSection.cpp` and `ShadowMaskSection.cpp` count that total.
- `applyAddedLamps` appends fittings with `still` 0, so the order of still
  movers is kept.

### 4.3 The bake — `ubake`

A *charted geometry* is GEOM with each still mover's shape appended, placed
in the world by `placed()` (UTA-0119 § 4.5, with `LightModel.h`'s sine). Its
normals turn by the same rotation, divide by `postScale`, and are
renormalised; a negative product of `postScale`'s components swaps each
triangle's winding. `bakeOcclusion` and `bakeShadowMask` take it in place of
GEOM, so its charts, holes and occluders follow without other change.
`moverReaches` skips still movers. `BAKER_REVISION` goes to 72.

### 4.4 The renderer — `urender`

`SceneGeometry::upload` hands AOCC and SMSK entries out in order: GEOM's
first, then each still mover's, as `append` reaches it. A mover that is not
still gets the white block and `MASK_NO_CHART`, as today. `scene.frag` is
unchanged: a still mover's fragment takes `chartedLight` by its `maskChart`.

## 5. Invariants

- **INV-1** — a mover is still exactly when § 4.1's four conditions hold.
  *Test:* `tests/unit/BakeMoversTest.cpp`, new case: one `Engine.Mover` with
  `InitialState` `None` is still; each of these is not: no `InitialState`, a
  subclass of `Mover`, `bSlave` true, another actor's `AttachTag` naming its
  `Tag`.
  *Breaks when:* a condition is dropped, or `InitialState` is compared
  unfolded.

- **INV-2** — `MOVR` round-trips `still`; the reader refuses a value above 1.
  *Test:* `tests/unit/BundleMoversTest.cpp`, the golden bytes re-recorded with
  `still` 1 on one mover, and a new refusal case.
  *Breaks when:* the byte is not written, or is read unchecked.

- **INV-3** — AOCC and SMSK hold one vertex entry per GEOM vertex plus per
  still-mover vertex; a bundle with another count is refused.
  *Test:* `tests/unit/BundleShadowMaskTest.cpp` and
  `tests/unit/BundleOcclusionTest.cpp`, a case each with one still mover.
  *Breaks when:* the check counts GEOM alone.

- **INV-4** — a still mover between a light and the floor shadows the floor's
  texel, charts its own lit faces, and marks no `moverReach`. The same mover
  not still marks the pair and does not shadow the texel.
  *Test:* `tests/unit/BakeShadowMaskTest.cpp`, new case beside INV-5's.
  *Breaks when:* the charted geometry leaves the mover out, or
  `moverReaches` counts it.

- **INV-5** — a still mover's fragments read its own charts; a mover that is
  not still reads none.
  *Test:* `tests/device/RenderShadowMaskTest.cpp`, UTA-0326 INV-9's case
  extended: a still mover's vertices carry SMSK entries, and its fragment's
  direct term follows its pair's texels.
  *Breaks when:* `upload` hands out entries to object 0 alone, or out of
  order.

- **INV-6** — DM-48SC-InversalLiandri's translucent pass on its two glass
  views costs less after the change than before.
  *Test:* manual. `ut-bench frame` at `--size 3840x2160 --render-scale
  0.6667` on High, the survey's cameras, old bake and build against new,
  alternated inside one `cc-job`. Record both on the roadmap item.
  *Breaks when:* the glass is not charted, or the charted path costs as much.

## 6. Failure modes

- **A bundle at format 32** is refused; re-bake it (`CLAUDE.md` § Standing
  facts).
- **A script outside Engine moves a stateless mover** (a custom class calling
  `GotoState` on it): the mover is drawn where it moves, but its charts and
  its shadow on the level stay where it was baked. No map in the survey is
  known to do this.
- **A still mover's charts take atlas space**: AOCC and SMSK grow by its lit
  faces. The atlas limits are unchanged.

## 7. Tests

| Invariant | Test | Label |
|---|---|---|
| INV-1 | `BakeMoversTest.cpp` | unit |
| INV-2 | `BundleMoversTest.cpp` | unit |
| INV-3 | `BundleShadowMaskTest.cpp`, `BundleOcclusionTest.cpp` | unit |
| INV-4 | `BakeShadowMaskTest.cpp` | unit |
| INV-5 | `RenderShadowMaskTest.cpp` | device |
| INV-6 | manual `ut-bench frame` recipe | — |

`BakeGoldenTest.cpp` is re-recorded for revision 72 and format 33; every test
pinning `formatVersion == 32` moves to 33. Each new case is seen to fail
against the pre-change code.

## 8. Alternatives considered (and rejected)

- **Copying a still mover into `GEOM` and not drawing its `MOVR` shape.** The
  bakes would need no new input, but GEOM's batches sort by material, so the
  copy loses UTA-0311's per-object depth tie, and the renderer needs a rule
  for skipping a shape.
- **Charting every mover at rest** (§ 3 decision 2).
- **Reading `numKeys`.** DoOpen goes to key 1 whatever `NumKeys` holds, so a
  mover with one key can still move.

## 9. Out of scope

- A still mover in probe visibility, strips or shadow tiles (§ 3 decision 5).
- Added lamps' fittings stay uncharted.
- Hidden movers: no code reads `bHidden` today, so every mover is drawn.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `BakeMoversTest.cpp` |
| INV-2 | `BundleMoversTest.cpp` |
| INV-3 | `BundleShadowMaskTest.cpp`, `BundleOcclusionTest.cpp` |
| INV-4 | `BakeShadowMaskTest.cpp` |
| INV-5 | `RenderShadowMaskTest.cpp` |
| INV-6 | **nothing** automatic — a manual measurement recorded on the roadmap item |

## 11. Cross-doc impact

- UTA-0119 § 4.2: `MOVR` gains `still`.
- UTA-0164 § 4.1 and UTA-0326 § 4.2 and § 4.5: AOCC's and SMSK's vertex
  entries cover still movers after GEOM; a still mover is an occluder and
  carries charts.
- UTA-0326 § 4.4: still movers have no reach.
- `CLAUDE.md` § Standing facts: format 33, baker revision 72.
- `CHANGELOG.md`: one Changed line.

## 12. Cold-eyes loop log

None: contract reviews are cancelled for this project.

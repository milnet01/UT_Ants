<!-- ants-spec-format: 1 -->
# UTA-0438 — light PF_SpecialLit surfaces only by special lights

**Status:** accepted (2026-10-26), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** fix.
**Source:** ROADMAP UTA-0438 (split from UTA-0352, 2026-10-24).

**Layman:** Some surfaces in UT maps are lit only by a few special lights; they stop taking every light nearby, which was wrong and slow.

## 1. Goal

A surface carrying `PF_SpecialLit` takes direct light only from lights with
`bSpecialLit`, and those lights light no other surface. A map with neither
draws exactly as today.

## 2. Problem

1. **Nothing reads `PF_SpecialLit`.** No constant names the bit in `src/`
   (`rg -n 'PF_SPECIAL' src` → no match), so such a surface is lit like any
   other. UTA-0014 § 9 deferred it with no home.
2. **Every special light is dropped.** Four predicates refuse `specialLit`:
   `ubundle::litDirectly`, `urender::drawsDirectly`, `ubake::bakedLights`
   and `Strips.cpp`'s `excluded`. So the lights such a surface should take are
   the ones it never gets.
3. **It costs frame time.** DM-48SC-InversalLiandri's large mover glass
   panels carry the flag. Measured by UTA-0352 on 2026-10-24 at 3840x2160
   High: its translucent pass was 7.0 ms of a 21 ms worst view, 5.6 ms of
   it direct light and 3.9 ms the cluster path's shadow-map reads, all from
   ordinary lights UT99 would not apply there.

**The bit is `0x00100000`.** `ut-dump --surface-list` over
DM-48SC-InversalLiandri finds it on 75 of 3249 surfaces, which matches the
234 level triangles UTA-0352 counted. `0x00000020` is a different flag: it is
set on 1791 of those surfaces. The counting command was run on 2026-10-26:
`ut-dump --install <UT> --surface-list <map>`, then masking each surface's
`polyFlags`.

## 3. Scope decisions (agreed with the user)

Decisions 1 and 2 were agreed with the user on 2026-10-26. Decisions 3 and 4
were the session's own, made the same day. Each is cheap to reverse.

1. **The sun lights no `PF_SpecialLit` surface.** UT99's rule gives such a
   surface direct light from special lights only, and the recipe's sun is
   not one.
2. **Probe bounce still reaches a `PF_SpecialLit` surface.** Bounce light
   stays (`CLAUDE.md` § Standing facts). It is made from ordinary lights,
   but it is indirect light, not direct light.
3. **A special light does not bounce, glow in fog, or cast shadow maps.**
   Bounce and fog keep today's behaviour (UTA-0112 § 4.4, UTA-0015 § 6).
   Shadows are covered in § 4.4.
4. **The flashlight lights every surface.** It is this project's own lamp,
   not a UT99 light.

## 4. Design

### 4.1 The flag and the light classes — `ubundle`, `ubake`, `urender`

`PF_SPECIAL_LIT = 0x00100000u` joins the flag lists in `src/ubake/Geometry.h`,
`src/urender/ShaderTypes.h` and `types.glsl`.

`ubundle::litDirectly` stops refusing `specialLit`, so it now means "lights
some surface directly". Each consumer that must still leave special lights
out does so explicitly:

| Consumer | Special lights |
|---|---|
| SMSK baker (`ShadowMask.cpp`) | paired with special charts only (§ 4.2) |
| `urender::shadowFacesOf` | 0 faces |
| `Lamps.cpp` lamp validation | still refused |
| `urender::drawsDirectly` | drawn (§ 4.3) |
| `ubake::bakedLights` (probes) | unchanged: left out |
| `Strips.cpp` `excluded` | unchanged: never absorbed into a strip |
| `urender::volumeLights` (fog) | left out |

### 4.2 The bake — `ubake`

A chart is *special* when its batch carries `PF_SPECIAL_LIT`. `charts::Chart`
records it from the batch's flags in `chartsOf`. `bakeChart` tries only the
sites whose class matches the chart:

- a special chart tries the special LITE lights;
- an ordinary chart tries everything else, which is today's set: the
  ordinary LITE lights, the LAMP lights and the sun.

A pair's record shape does not change, so `FORMAT_VERSION` stays at 32.
`BAKER_REVISION` goes to 70.

### 4.3 The drawn list — `urender`

`directLights`, `drawnIndices` and `drawnLights` keep the ordinary lights in
LITE order and then append the special lights in LITE order. On a bundle with
no special light, every index is the same as today. `FrameData` gains
`specialFirst` and `specialCount`, which give the range.

- `cluster.comp` still prepares each light's `shade`, but it bins none of the
  special range. Ordinary clusters lose no slot to a special light.
- `uploadShadowMask` maps a special pair's LITE index through
  `drawnIndices`, as it already does for every other pair.

### 4.4 The shader — `scene.frag`

In the lit branch, a fragment whose `draw.polyFlags` carries `PF_SPECIAL_LIT`
takes its direct light this way:

- **Charted** (a level surface): `chartedLight`, unchanged. Its pairs are
  already special lights only (§ 4.2).
- **Uncharted** (a mover, or the sky room): a new `specialLight` loops over the
  special range. It tests each light by distance and applies no shadow. Then
  it adds the flashlight. The sun term is skipped.

Every other fragment is lit as today. Probe indirect light applies to both
(§ 3 decision 2).

A charted pair with `moverReach` calls `softShadowOf`. A special light has no
tiles, so that call returns 1: such a light shines through a mover onto a
special level surface. That is the cost of § 3 decision 3.

## 5. Invariants

- **INV-1** — a special chart pairs only special lights, and an ordinary chart
  pairs no special light.
  *Test:* `tests/unit/BakeShadowMaskTest.cpp`, new case: one wall with the
  flag and one without, one special and one ordinary light reaching both.
  *Breaks when:* `bakeChart` ignores the chart's class. The pre-change baker
  pairs the ordinary light with both walls and the special light with
  neither.

- **INV-2** — `drawnLights` lists ordinary lights in LITE order, then special
  lights in LITE order. `drawnIndices` maps each light to its slot. A bundle
  with no special light keeps today's indices.
  *Test:* `tests/unit/RenderLightsTest.cpp`, the case "every LITE light draws
  but backdrop and special-lit lights", rewritten.
  *Breaks when:* special lights are still dropped, or are interleaved in LITE
  order.

- **INV-3** — no cluster lists a special light.
  *Test:* `tests/device/RenderSurfaceFlagsTest.cpp`, new case: an ordinary wall
  beside a bright special light draws exactly as it does with that light
  removed.
  *Breaks when:* `cluster.comp` bins the special range.

- **INV-4** — an uncharted `PF_SpecialLit` fragment takes direct light from
  special lights only, and no sun.
  *Test:* `tests/device/RenderSurfaceFlagsTest.cpp`, new case: a mover face with
  the flag, an ordinary light and a special light, each with its own hue. The
  face shows the special hue and not the ordinary one.
  *Breaks when:* the shader keeps `clusterLight` for the flag.

- **INV-5** — a special light gets no shadow faces, and no volume light slot.
  *Test:* `tests/unit/RenderShadowsTest.cpp` and
  `tests/unit/RenderVolumeLightsTest.cpp`, a special-light case in each.
  *Breaks when:* `litDirectly`'s widening reaches those consumers.

- **INV-6** — DM-48SC-InversalLiandri's translucent pass costs less after the
  change than before, on its worst view.
  *Test:* manual. Run `ut-bench frame` at 2560x1440 (render scale 2/3 of 4K)
  on High, before and after, inside one `cc-job` (frame timing drifts between
  runs). Record both medians on the roadmap item.
  *Breaks when:* the special loop costs more than the ordinary light it
  replaces. § 8 names the fallback.

## 6. Failure modes

- **A bundle baked at revision 69** gives special charts ordinary pairs, so
  its level special surfaces draw as today, while its movers already draw by
  the new rule. Re-bake it (`CLAUDE.md` § Standing facts).
- **A map with many special lights** makes the uncharted loop long. The loop
  runs only on uncharted special fragments, and INV-6 measures the worst map
  known.
- **A special light behind a mover** lights the special surface through it
  (§ 4.4).

## 7. Tests

| Invariant | Test | Label |
|---|---|---|
| INV-1 | `BakeShadowMaskTest.cpp` | unit |
| INV-2 | `RenderLightsTest.cpp` | unit |
| INV-3, INV-4 | `RenderSurfaceFlagsTest.cpp` | device |
| INV-5 | `RenderShadowsTest.cpp`, `RenderVolumeLightsTest.cpp` | unit |
| INV-6 | manual `ut-bench frame` recipe | — |

`BakeGoldenTest.cpp` is re-recorded for revision 70. Each new case is seen to
fail against the pre-change code.

## 8. Alternatives considered (and rejected)

- **A second cluster list for special lights**, binned by `cluster.comp`.
  It scales to any light count, but it adds a buffer and work to every frame
  on every map. The brute-force loop costs nothing where no special light
  exists. It is the fallback if INV-6 fails.
- **A flag bit inside a `gpu::Light` byte field.** It would need a mask in
  every reader of that field. The contiguous range needs no mask.
- **Bumping `FORMAT_VERSION`.** No record changes shape. The baker revision
  is what goes stale.

## 9. Out of scope

- `ut-ref`'s reference renderer still lights every surface with
  `bakedLights` — deferred; not yet queued.
- Mesh actors' own `bSpecialLit`: the renderer draws no meshes yet.
- Folding the four light predicates into one — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `BakeShadowMaskTest.cpp` |
| INV-2 | `RenderLightsTest.cpp` |
| INV-3 | `RenderSurfaceFlagsTest.cpp` |
| INV-4 | `RenderSurfaceFlagsTest.cpp` |
| INV-5 | `RenderShadowsTest.cpp`, `RenderVolumeLightsTest.cpp` |
| INV-6 | **nothing** automatic — a manual measurement recorded on the roadmap item |

## 11. Cross-doc impact

- UTA-0014 § 4.6 *Which lights draw*, § 4.8 and § 9: special lights draw,
  on special surfaces only. The `PF_SpecialLit` deferral moves here.
- UTA-0326 § 4.1: `litDirectly`'s comment drops "not specialLit".
- UTA-0015 § 6: a special light does not glow because `volumeLights` leaves
  it out, not because it is undrawn.
- `CLAUDE.md` § Standing facts: baker revision 70.
- `CHANGELOG.md`: one Fixed line.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0438-special-lit-surfaces-loop-log.md`.

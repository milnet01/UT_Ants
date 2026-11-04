<!-- ants-spec-format: 1 -->
# UTA-0293 — grass: blades grown on a map's grass floors

**Status:** accepted (2026-11-04), unreviewed. Contract reviews are cancelled
for this project (`CLAUDE.md` § Contract reviews); no review runs unless the
user asks.
**Kind:** feature.
**Source:** ROADMAP UTA-0293 (user-request-2026-10-05).
**Builds on:** UTA-0326 (`docs/specs/UTA-0326-baked-shadow-mask.md`): its
charts and texels light each blade as they light the ground. UTA-0263
(`docs/specs/UTA-0263-shader-flames.md`): a baked list the renderer draws
one instance per entry, the shape this follows.

**Layman:** floors painted with grass grow real blades of grass that sway in
the wind, lit and shadowed as the ground under them is.

## 1. Goal

Where a map's floor is painted with grass, the bake plants clumps of grass
on it and the renderer grows each clump into blades that sway, are lit as
the ground beneath them is, and thin out with distance. Below the tier that
draws them, a map draws exactly as before.

## 2. Problem

A UT99 grass floor is a flat picture. Seen at a low angle it reads as a
painted carpet, which is what the user asked to replace (roadmap body,
2026-10-05). Nothing in the bundle says which surfaces are grass: the only
name test the baker makes is `isGlassPane` (`src/ubake/Panes.h`), and the
recipe's `MaterialAssignment` (`src/urecipe/Recipe.h`) has no kind field.
Nothing draws geometry the level does not hold, apart from flames and water
motes (`recordFrame`, `src/urender/Frame.cpp`).

**Census, 2026-11-04**: `ut-dump --surface-list` over every map in the
reference install's `Maps` folder (1611 read, none failed), counting
surfaces whose normal's up component is at least 0.7 and whose texture's
short name matches a word. 483 maps have such a floor named for grass
(`gras`, `grss`, `lawn`, `turf`, `meadow`); 919 once `dirt`, `earth`,
`ground` and their like are added. The commonest names are `Grass`,
`Dirt2`, `mGrass7` and `Dirt`. The wider words also match `dirtmrbl`,
`SG_dirtypill2` and `Background`, so § 4.1 drops them. The
most grass-named floors are on MH-Vale_Evelyn and
MH-SDM-FatherDougalUM_beta1. The census read short names only; the baker
also reads group names, so it can match more.

## 3. Scope decisions (agreed with the user)

1. **Cheap methods that still look modern** (user, 2026-10-05, and
   `CLAUDE.md` § Standing facts, 2026-09-14).
2. **Taken by this spec, 2026-11-04**, each recorded here because the
   roadmap body left them open:
   - **Grass only.** Bushes, flowers, ferns, and UTA-0055's hanging moss,
     vines, cobwebs and seaweed need models and are a later item (§ 9).
     Grass needs no model: its blades are made in the vertex shader (§ 4.4),
     so no Blender asset is involved.
   - **A floor is grass by its texture's name, not by its colour or whether
     it sees the sky.** Colour only thins it (§ 4.2). A garden under a roof is still grass; the roof's shadow
     reaches it through the shadow mask and the probes.
   - **Grass blocks nothing.** It has no collision, so it never blocks a
     path. It is kept clear of pickups so it never hides one (§ 4.2).
   - **Drawn from High, at half density; full density at Ultra** (§ 4.6),
     unless § 13's measurement says otherwise.

## 4. Design

### 4.1 Which surfaces grow grass

The baker grows grass on a GEOM triangle when all of these hold:

1. its material id's name or any group (`umat::materialId`'s
   `<package>.<group...>.<name>`, lower case, `#masked` dropped) holds one
   of `gras`, `grss`, `lawn`, `turf` or `meadow`: `isGroundCover`
   (`src/ubake/Grass.h`);
2. it is not a liquid, a pane, a flame or a fire, and its polyFlags carry
   none of `PF_MASKED`, `PF_TRANSLUCENT`, `PF_MODULATED`, `PF_FAKE_BACKDROP`
   or `PF_INVISIBLE` (`src/ubake/Geometry.h`);
3. its normal's up component is at least `GRASS_SLOPE`, 0.7;
4. its zone is not a water zone (`Zone::water`).

`ground`, `earth` and `dirt` are left out. UT99 paints grass olive and earth
brown, too close for a colour test to part them (§ 8).

### 4.2 Planting

Each qualifying triangle is sampled at `CLUMP_SPACING` units, 16, by a
jittered grid in the world's plan, each sample lifted onto the triangle. A
sample keeps its clump with probability `density`, read from the material's
base picture at the sample's UV, at the mip nearest 8 texels a clump. Each
texel's green share is judged against the mean share of that mip:

```
share   = (g + SHARE_GREY / 3) / (r + g + b + SHARE_GREY)       linear
density = smoothstep(GREEN_LOW, GREEN_HIGH, share - mean share)  0 to 1
```

`SHARE_GREY`, 0.01, makes a near-black texel read as grey. A picture of one
colour, whatever its hue, has full density everywhere. Its bare patches,
browner or greyer than its mean, grow none.

A sample is dropped when it lies within `PICKUP_CLEAR` units, 40,
horizontally of the `location` of any placed actor descending from
`engine.inventory`, resolved through class defaults as `probeReachOf`
reads a navigation point's (`src/ubake/LightProbes.cpp`).

Every random choice is a hash, so the same map bakes the same clumps. A
sample's place in its grid cell hashes the cell alone, so a polygon cut
into triangles plants evenly across its seams. Whether it is kept, and its
seed, hash the triangle's index too.

Each kept clump records its foot (on the surface), its material, its UV,
the shadow-mask chart and texel the surface has there (interpolated from
the triangle's `vertexChart` and `vertexTexel`, which share one chart), and
a seed.

### 4.3 The bundle

A new section, `VEGE`, written after `PSKY`, holds:

- `clumps`: each `{ base[3], material, uv[2], maskChart, maskTexel[2],
  seed }`, `material` indexing MATS, `maskChart` indexing SMSK's charts or
  `MASK_NO_CHART`;
- `cells`: the clumps sorted into a grid of `GRASS_CELL` units, 1024, each
  cell `{ firstClump, clumpCount, low[3], high[3] }`, so the renderer culls
  by cell. Within a cell, clumps with even seeds come first, so High draws
  the head of each run (§ 4.6).

At most `GRASS_CLUMP_LIMIT` clumps, 1 << 20. A map past it is planted again
on a grid widened by `sqrt(count / GRASS_CLUMP_LIMIT)`, with a 2% margin,
until it fits. Its grass is sparser, never absent. The bake report's
`grass` entry gives the grid as `spacing`. A map with no grass writes no
`VEGE`. `FORMAT_VERSION` becomes 35 and `BAKER_REVISION` 76.
`shapeOf` samples `VEGE` (`docs/fitting-a-map.md`).

### 4.4 Drawing

A new pass, after opaque and before translucent, timed as its own
`GpuPass::Grass`. It writes depth and colour with blending off, so water
and glass in front of grass draw over it.

Each frame the host culls cells by the view frustum and by `GRASS_END`, and
draws each visible cell as one instanced draw, an instance a clump. The
vertex shader builds a clump's blades from `gl_VertexIndex` and the seed:
`BLADES_NEAR` blades, 12, each a tapered strip of 7 vertices, 8 to 20 units
tall, leaning at random. Cells beyond `GRASS_FAR`, a third of the way to
`GRASS_END`, draw `BLADES_FAR`, 4. Between `GRASS_FADE` and `GRASS_END`,
blades shrink to nothing, so no clump pops out.

### 4.5 Look

- **Colour**: the ground's own picture at the clump's UV, from a coarse mip,
  darker at the root and lighter at the tip. So blades match the grass they
  stand on.
- **Light**: the fragment lights itself through the scene shader's
  `chartedLight`, with the clump's chart and texel, at the blade's own
  position, with a normal bent from the blade's face toward up. Lit from
  both sides. Clumps with `MASK_NO_CHART` take `clusterLight` instead. The
  probes' indirect light (`indirectAt`) is added as on the ground. The
  ground's baked occlusion and the outdoor haze are left out: a clump
  carries no occlusion coordinate, and grass ends nearer than haze begins
  on most maps.
  `chartedLight` and `clusterLight` move into a shared include that both
  shaders use.
- **Wind**: one direction for every map, `WIND_DIRECTION`. Each blade bends
  along it by a travelling gust, `sin(dot(base.xy, WIND_DIRECTION) *
  GUST_WAVE - time * GUST_SPEED)`, plus its own flutter from the seed. The
  bend grows with the square of the height up the blade. `time` is
  `frame.flameSeconds`; motion vectors use `previousFlameSeconds`.

`GREEN_LOW`, `GREEN_HIGH`, `GRASS_FADE`, `GRASS_END`, `WIND_DIRECTION`,
`GUST_WAVE` and `GUST_SPEED` are first values, set by eye on § 13's maps and
recorded there.

### 4.6 Tiers

`Feature::Grass` starts at `Tier::High` (`Tiers.h`). High draws the clumps
whose seed is even, with `GRASS_END` 1536 units; Ultra draws all of them,
with `GRASS_END` 2560. Below High no clump is uploaded, culled or drawn,
and no grass pipeline is made; the binding holds one zero record. A face of
the sky draws no grass.

## 5. Invariants

- **INV-1** — Grass grows only on grass names. A fixture level with
  an upward floor whose texture is green grows clumps when the texture is
  named `Grass1` and none when it is named `Tile1`.
  *Test:* `tests/unit/GrassBakeTest.cpp`, "UTA-0293 INV-1".
  *Breaks when:* `isGroundCover` is skipped or matches every name.
- **INV-2** — Grass thins where the picture is less green than its own mean.
  A `Grass1` floor of one colour, grey or olive, grows clumps everywhere.
  Half olive and half bare earth, the olive half is full and the earth half
  near empty; half green and half grey, clumps stand on the green half only.
  *Test:* `tests/unit/GrassBakeTest.cpp`, "UTA-0293 INV-2".
  *Breaks when:* `density` is not read, is read from the wrong texel, or
  judges a texel by its absolute hue rather than against the mean.
- **INV-3** — Grass grows only on floors. The green `Grass1` texture on a
  wall, normal horizontal, grows no clump; on a floor tilted 40 degrees, it
  does.
  *Test:* `tests/unit/GrassBakeTest.cpp`, "UTA-0293 INV-3".
  *Breaks when:* the `GRASS_SLOPE` test is skipped or reversed.
- **INV-4** — Grass never grows under water. The green `Grass1` floor in a
  water zone grows no clump.
  *Test:* `tests/unit/GrassBakeTest.cpp`, "UTA-0293 INV-4".
  *Breaks when:* `Zone::water` is not read.
- **INV-5** — Grass never hides a pickup. With a placed actor of a class
  descending from `engine.inventory` at the floor's centre, no clump lies
  within `PICKUP_CLEAR` of it horizontally, and clumps lie beyond it.
  *Test:* `tests/unit/GrassBakeTest.cpp`, "UTA-0293 INV-5".
  *Breaks when:* the clearance is not applied, or its distance is
  three-dimensional.
- **INV-6** — The same map plants the same grass. Two bakes of the INV-1
  fixture write byte-identical `VEGE` sections.
  *Test:* `tests/unit/GrassBakeTest.cpp`, "UTA-0293 INV-6".
  *Breaks when:* a random choice is seeded from anything but the triangle
  and the grid cell.
- **INV-7** — `VEGE` reads back what was written, and refuses a clump whose
  material is outside MATS, whose chart is outside SMSK and not
  `MASK_NO_CHART`, or whose base is not finite; and a cell whose run passes
  the clump count.
  *Test:* `tests/unit/GrassSectionTest.cpp`, "UTA-0293 INV-7".
  *Breaks when:* any one of those checks is dropped.
- **INV-8** — Below High, grass draws nothing. A grass floor drawn at Medium
  with its `VEGE` and without it gives the same image.
  *Test:* `tests/device/RenderGrassTest.cpp`, "UTA-0293 INV-8".
  *Breaks when:* the grass pass runs below High.
- **INV-9** — At High, grass stands above the floor. The same pair at High
  differs in pixels above the floor's horizon line.
  *Test:* `tests/device/RenderGrassTest.cpp`, "UTA-0293 INV-9".
  *Breaks when:* the pass draws nothing, or draws flat on the floor.
- **INV-10** — Grass takes the ground's shadow. A grass floor half under a
  slab, lit by one light whose mask shadows that half: blades over the
  shadowed half are darker than blades over the lit half.
  *Test:* `tests/device/RenderGrassTest.cpp`, "UTA-0293 INV-10".
  *Breaks when:* blades light themselves without the clump's chart and
  texel.
- **INV-11** — Grass ends at range. A grass floor whose every cell lies
  beyond `GRASS_END` draws the same at High with and without its `VEGE`.
  *Test:* `tests/device/RenderGrassTest.cpp`, "UTA-0293 INV-11"; and
  `tests/unit/GrassCullTest.cpp`, "UTA-0293 INV-11", for the cull alone.
  *Breaks when:* cells are not culled by distance, or the fade does not
  reach zero. The device case breaks only when both fail; the unit case
  when the cull does.
- **INV-12** — Grass sways. At High, the floor drawn at light times 0 and 1
  differs; drawn twice at 0, it does not.
  *Test:* `tests/device/RenderGrassTest.cpp`, "UTA-0293 INV-12".
  *Breaks when:* the wind ignores `time`.
- **INV-13** — A changed `VEGE` re-uploads. At High, moving every clump of
  one bundle in place, between two draws by one renderer, changes the image.
  *Test:* `tests/device/RenderGrassTest.cpp`, "UTA-0293 INV-13", which
  edits one bundle in place as `RenderSunTest.cpp` does.
  *Breaks when:* `shapeOf` does not sample `VEGE`.

- **INV-14** — A map past the clump limit keeps sparser grass. A green
  `pkg.grass1` floor 20480 units a side plants on a grid wider than
  `CLUMP_SPACING`, keeping more than four fifths of `GRASS_CLUMP_LIMIT`
  clumps and no more than it; a 512-unit floor keeps `CLUMP_SPACING`.
  *Test:* `tests/unit/GrassBakeTest.cpp`, "UTA-0293 INV-14".
  *Breaks when:* the map past the limit bakes without grass, or the grid
  widens too far.

## 6. Failure modes

- **A name the test misses or wrongly matches.** A meadow named `Ground3`
  or `Dirt2` grows none; nor does a lawn named `Floor7`. A grey stone named
  `GrassStone` grows grass everywhere. Accepted for a first iteration; a
  recipe override is § 9's.
- **Grass through thin geometry.** A clump on a floor beside a wall can lean
  through it. Accepted: blades are at most 20 units tall.
- **A mover over grass.** Blades can poke through a lift or door resting on
  grass, since nothing pushes them aside. Accepted for a first iteration.
- **Too many clumps.** A big terrain past `GRASS_CLUMP_LIMIT` grows sparser
  grass (§ 4.3). MH-SDM-FatherDougalUM_beta1's grass would have planted
  5,771,780 clumps at full density (bake of 2026-11-04, under the wider
  names).

## 7. Tests

INV-1, INV-2, INV-3 and INV-6 bake fixture levels; INV-2 also reads
`greenMapOf` directly; INV-4, INV-5 and INV-14 hand `plantGrass` a bundle
(`tests/unit/GrassBakeTest.cpp`).
`tests/unit/GrassCullTest.cpp` grades the host's culling and tiers. INV-7 is the section codec's unit test. INV-8, INV-9,
INV-10, INV-11, INV-12 and INV-13 are device tests, run on Mesa's
CPU driver and the GPU under the validation layer and sync validation, as
every device test is. Every
existing device test draws a map with no `VEGE` and must pass unchanged.

## 8. Alternatives considered (and rejected)

- **Alpha-tested grass cards from a picture.** The usual cheap method, but
  every card overdraws and discards, which defeats the early depth test that
  keeps the opaque pass cheap, and it needs a made picture. Blades built in
  the vertex shader draw opaque, with no picture. Rejected.
- **Plant at load time, not at bake time.** No format change, but the
  density test needs the base pictures decoded, which the baker already
  has; a load-time scatter would repeat that work on every load. Rejected.
- **An absolute green test, and grass on earth by colour.** The first build
  grew grass on `ground`, `earth` and `dirt` floors too, where
  `smoothstep` over `g - max(r, b)` found green. A design premise moved:
  UT99's grass is not green. Mean linear albedo (`ut-bake
  --light-materials`, 2026-11-04): `mh-vale_evelyn.grass` 0.084 0.058
  0.016, `genterra` `grasc*` about 0.079 0.053 0.018, `genearth`
  `dirt_sw2` 0.025 0.014 0.007. Red stands above green, so the test planted
  4675 clumps on MH-Vale_Evelyn, 66 of them on `mh-vale_evelyn.grass`,
  which holds 526,944 at full density. No threshold parts olive from brown. Replaced by the name
  alone and a density relative to the picture's mean (§ 4.1, § 4.2).
- **A map past the limit bakes without grass.** The first build's rule. It
  would leave the largest grass map bare. Replaced by a wider grid
  (§ 4.3).
- **Grass only where the sky is seen.** Grass under a roof is still grass,
  and the light already darkens it. Rejected (§ 3).
- **A compute pass that culls clumps one by one.** Finer than cells, but a
  second pass and an indirect draw for a first iteration. Kept for a later
  performance item if § 13 shows the vertex cost matters.

## 9. Out of scope

- Bushes, flowers, ferns, trees, and UTA-0055's moss, vines, cobwebs and
  seaweed: they need models, made by headless Blender scripts (user,
  2026-11-04). `UTA-0449`.
- A recipe override that forces a material to grow grass or not.
- Grass parting around players, being trampled or burnt.
- Grass casting shadows.
- Impostors for grass beyond `GRASS_END`.

## 10. What checks this

| Claim | Check |
|---|---|
| INV-1 to INV-14 | the tests in § 7 |
| No upload, cull or draw below High | **Partial:** INV-8 checks the image; `GrassCullTest.cpp` that no draw is planned; nothing counts the upload |
| Grass looks like grass | nothing; set by eye in § 13 |
| Existing maps unchanged below High | every existing device test |

## 11. Cross-doc impact

- `docs/specs/UTA-0055-swaying-cloth-and-water.md` § 3 sends moss, vines,
  cobwebs and seaweed here; this spec moves them to a later item (§ 9). No
  clause there changes.
- `docs/specs/UTA-0326-baked-shadow-mask.md`: read, not changed.
- `CLAUDE.md` § Standing facts names the bundle format and baker revision;
  they become 35 and 76 when this ships.

## 12. Cold-eyes loop log

`docs/reviews/UTA-0293-grass-loop-log.md`. No review runs: the gate is
cancelled for this project.

## 13. Resource cost

To be measured when built: `ut-bench frame` at High, 2560x1440 (the
frame-rate target), on MH-Vale_Evelyn and MH-SDM-FatherDougalUM_beta1
(§ 2's census), with grass
on against off in one job, the grass step named apart. **If grass costs
more than 1 ms at High on either map, `Feature::Grass` moves to Ultra**
until a performance item brings it under. Recorded here with the date, as
are the first values of § 4.5 and how they were chosen.

**Measured 2026-11-05** at `b8ea973` on the RX 6600 (RADV), `ut-bench
frame --tier high --size 2560x1440`, four views a map, 60 still frames
each and 120 moving between them. `ut-bench` warned the machine was busy
(load 24 to 28 over 12 cores), so host figures are slow by an unknown
amount; the grass step is a GPU timestamp.

| Map | Clumps, grid | Grass step, still median | Worst view |
|---|---|---|---|
| MH-Vale_Evelyn | 460,375 at 16 units | 0.497 ms | 0.616 ms |
| MH-SDM-FatherDougalUM_beta1 | 1,007,535 at 18.09 units | 0.650 ms | 0.768 ms |

Under 1 ms on both, so `Feature::Grass` stays at High. Refute by re-running
the same command on a quiet machine.

**First values**, kept as built: each view, drawn by `ut-shot` at High
beside the same view at Medium, shows tufts on the grass floors coloured
and shaded as the ground under them, and none at Medium. Nothing was
tuned by eye beyond that check.

- § 4.2: `GREEN_LOW` −0.03, `GREEN_HIGH` −0.01, `SHARE_GREY` 0.01
  (`src/ubake/Grass.h`). Chosen so a texel at its picture's mean share is
  full and one 0.03 below is bare: MH-Vale_Evelyn's olive grass and bare
  earth differ by about 0.06 in share (`tests/unit/GrassBakeTest.cpp`'s
  `OLIVE` and `EARTH`).
- § 4.4: `GRASS_END` 1536 at High and 2560 at Ultra; `GRASS_FAR` a third
  of it; `GRASS_FADE` three fifths (`src/urender/Grass.h`).
- § 4.5: `WIND_DIRECTION` (0.8, 0.6), `GUST_WAVE` 0.004, `GUST_SPEED` 1.3
  (`src/urender/shaders/grass.vert`); root and tip shade 0.55 and 1.15,
  colour from mip 4 (`grass.frag`).

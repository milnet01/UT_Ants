<!-- ants-spec-format: 1 -->
# UTA-0292 — a path-traced reference to measure the renderer's light against

**Status:** accepted (2026-10-08), unreviewed. Contract reviews are cancelled for this
project (`CLAUDE.md` § Contract reviews); no review runs unless the user asks.
**Kind:** implement.
**Source:** ROADMAP UTA-0292 (user decision 2026-10-08: the scratch path
tracer becomes a permanent tool, so later lighting items are measured
against exact light).
**Pairs with:** UTA-0112 (§ 4.12 is the light model this measures).

No user-visible change: a developer tool that says how far the renderer's
light is from exact light, view by view.

## 1. Goal

`ut-ref` traces exact light in UTA-0112's model for a list of views, and
scores `ut-shot`'s light against it. The scratch copy that measured
UTA-0292's gaps worked through three hand patches; after this, the same
measurement runs from a clean tree with three commands, and a later lighting
item (UTA-0254, UTA-0256, UTA-0164) is scored the same way.

## 2. Problem

The scratch tool (`~/.cache/uta-scratch/u292/wt`, `tools/ut-ref/main.cpp`
plus `git diff` there) cannot be committed as it is:

1. **It needs each material's light values, and the bundle does not hold
   them.** The probe bake reads `albedo` and `own` lookups built in
   `ubake::bake` from `Materials::albedo`, `Materials::emission` and each
   record's `liquid`. The bundle keeps only compressed textures, and the
   bake takes albedo from the picture before compression (`meanAlbedo` on
   the resolved picture in `makeVariant`), so the values cannot be
   recomputed exactly from a bundle. The scratch dumped them through a
   `UTA_PROBE_ALBEDO` environment hook in `Bake.cpp`.
2. **It needs the renderer's light terms, and nothing writes them.** The
   scratch overwrote `scene.frag`'s `outColour` with the terms and turned off
   bloom in `post.frag`, into an 8-bit picture.
3. **Its light model is a second copy of the probe bake's.** It differs
   already: it gives a backdrop hit its own albedo where § 4.12 item 2 sends
   the ray on from the sky view, it scales albedo by an environment variable,
   and it treats an unlit surface that is not a liquid as dark where
   `radianceAlong` lights it.
4. **Its scorer is a Python script needing numpy**, which nothing in the
   repository uses (`workspace_search "import numpy"` → 0 files, 2026-10-08).

## 3. Scope decisions (agreed with the user)

- The tool is permanent and lands with UTA-0292's build (user, 2026-10-08).
- Everything else follows from § 2. Where § 8 weighs two designs, the choice
  was the session's.

## 4. Design

### 4.1 One light model, shared (ubake)

`LightProbes.cpp`'s `radianceAlong` splits into three public functions in
`src/ubake/LightProbes.h`; `radianceAlong` becomes their composition and its
results do not change.

```cpp
/// SS 4.7 steps 1-4: the lit side of the first surface a ray from `p` along
/// `w` meets, sent on once from `sky` where it meets the sky (SS 4.12 item 2).
struct SurfaceHit {
    Vec3 at;                               // the hit point
    Vec3 normal;                           // facing the ray
    const ubundle::GeometryBatch* batch;
    bool viaSky;                           // reached through the sky view
};
[[nodiscard]] std::optional<SurfaceHit> surfaceAlong(const Vec3& p, const Vec3& w,
    const SurfaceRays& rays, const ubundle::Geometry& geometry,
    const std::optional<Vec3>& sky);

/// SS 4.7 step 5: the light of `lights` reaching `x`, facing `n`, each ray-tested.
[[nodiscard]] Rgb lightReaching(const Vec3& x, const Vec3& n, const SurfaceRays& rays,
                                const std::vector<ubundle::Light>& lights);

/// SS 4.7 step 4's unlit liquid and step 6, with SS 4.12 item 1's scale:
/// the light `hit` sends back along the ray.
[[nodiscard]] Rgb sentFrom(const SurfaceHit& hit, const SurfaceRays& rays,
                           const std::vector<ubundle::Light>& lights,
                           const AlbedoLookup& albedo, const OwnLightLookup& own);

/// SS 4.12 item 1: the share of the light reaching it a surface sends on.
[[nodiscard]] Rgb reflectanceOf(const Rgb& albedo) noexcept;
```

### 4.2 The material light file (ubake, ut-bake)

`BakeResult` gains `std::vector<MaterialLight> materialLight`: one entry per
`MaterialRecord` the bake writes, in the records' order, holding what the
probe bake's `albedo` and `own` lookups return for that id.

```cpp
struct MaterialLight {
    std::string id;
    Rgb albedo;       // the albedo lookup's value, DEFAULT_ALBEDO included
    OwnLight own;     // the own lookup's value
};
void writeMaterialLight(std::ostream&, std::span<const MaterialLight>);
[[nodiscard]] Result<std::vector<MaterialLight>> readMaterialLight(std::istream&);
```

The file is text, one line per entry:
`<id> TAB <albedo r g b> <emission r g b> <0|1>`, each number followed by a
space and written by `std::to_chars` in the shortest form that reads back to
the same bits, with no locale. `ut-bake --light-materials <file>` writes it. The flag bakes
afresh, as `--force` does, since a reused bake computes no material values.

### 4.3 The renderer's light terms (urender, ut-shot)

`Config::lightTerms`, unset in normal play, sets `scene.frag`'s new
specialization constant `LIGHT_TERMS` (`constant_id = 6`, a `SceneConstants`
field). With it set, a lit surface writes to `outEmission`, in place of its
emission:

```glsl
outEmission = vec4(luma(pow(LIGHT_GAIN * direct, vec3(DISPLAY_LIGHT_POWER))),
                   luma(indirect * (open * pow(LIGHT_GAIN, DISPLAY_LIGHT_POWER))),
                   0.0, 1.0);
```

These are the two summands `colour` multiplies by reflectance. Any other
surface writes zero terms. Bloom strength is 0 while `lightTerms` is set, so
the colour picture carries no glow of the terms.

`ut-shot --light-terms` sets it and writes `<out prefix>-<line>-light.pfm`
beside each picture, from `Renderer::Target::Emission`, as `--emission`
writes its file. The two flags together are refused: they read one target.

### 4.4 The reference (tools/ut-ref)

`tools/ut-ref/Reference.{h,cpp}`, a library the unit tests link, and
`main.cpp`:

```
ut-ref trace <bundle> <material light file> <width> <height> <samples> <depth> <out prefix> < cameras
ut-ref score <ref prefix> <shot prefix> <width> <height> <views>
```

The cameras are `ut-shot`'s: one per line of standard input, in its format.

`trace` writes `<out prefix>-<line>.f32` per camera: `width × height × 5`
little-endian floats, row 0 at the top. Per pixel, each a luma:

| Channel | Holds |
|---|---|
| 0 | `shownLight` of `lightReaching` at the eye ray's hit |
| 1 | first bounce: the mean over `samples` cosine-sampled rays of `sentFrom(surfaceAlong(...))`, where the hit is not `viaSky` |
| 2 | later bounces: at each first-bounce hit, `reflectanceOf` its albedo times the light along one further cosine-sampled ray, recursively, to `depth` |
| 3 | sky: channel 1's sum over hits that are `viaSky` |
| 4 | 1 where the eye ray's first surface is lit and lets no light through (`LIGHT_PASSES_FLAGS`), else 0, and channels 0–3 are 0 |

A material the bundle uses and the file lacks takes `DEFAULT_ALBEDO` and no
own light, as the bake's lookups do. The sky view is `ubundle::skyViewOf`.
Each pixel's random numbers are seeded from its coordinates alone.

`score` reads `<ref prefix>-<n>.f32` and `<shot prefix>-<n>-light.pfm` for
`n` below `views`, splits each view into 8×8 blocks, and keeps a block where
at least 48 pixels are lit in both: channel 4 set, and the two terms not both
0. Each block's values are means over those pixels. It prints one row per
view and a mean row: the reference's and the renderer's mean light; the
total, direct and indirect gaps (mean absolute block difference over the
reference's mean), the indirect term set against first bounce plus sky,
which is what a probe holds (UTA-0112 § 4.12); the sky and later-bounce
shares; and the gap in stops, over blocks where both are above 0.

## 5. Invariants

- **INV-1** — The probe bake's output does not change: every probe a bake
  wrote before § 4.1's split, it writes after, bit for bit.
  *Test:* `tests/unit/BakeLightProbesTest.cpp`, unchanged, passes.
  *Breaks when:* the split reorders a sum or drops a case of `radianceAlong`,
  such as the unlit liquid or the sky hop.

- **INV-2** — The material light file round-trips: `readMaterialLight` of
  what `writeMaterialLight` wrote gives back every entry, bit for bit.
  *Test:* `tests/unit/MaterialLightTest.cpp`.
  *Breaks when:* a value is printed short of its shortest round-trip form,
  or an id holding a space is split.

- **INV-3** — A bake's `materialLight` names each of its `MaterialRecord`
  ids once, in the bundle's order, with the values the probe bake's lookups
  gave it. The values come from the same two lookups the probe bake calls.
  *Test:* `tests/unit/BakeCliTest.cpp`, on the fixture install: the file
  `--light-materials` writes names the bundle's records in order.
  *Breaks when:* a record is left out, as one with no opaque pixel would be
  if the list were built from the albedo table rather than the records.

- **INV-4** — `ut-bake --light-materials` bakes afresh.
  *Test:* `tests/unit/BakeCliTest.cpp`: the flag parses to a forced bake.
  *Breaks when:* a reused bake leaves the file unwritten or stale.

- **INV-5** — With `lightTerms` unset, no frame changes.
  *Test:* `ctest --test-dir build -L '^device$'`, unchanged, passes.
  *Breaks when:* `LIGHT_TERMS`' branch reaches a normal frame.

- **INV-6** — With `lightTerms` set, a lit pixel's emission readback holds
  the two terms of § 4.3: with no probes, green is 0 and red is the direct
  term; with probes, green is the indirect term.
  *Test:* `tests/device/RenderLightTermsTest.cpp`.
  *Breaks when:* a term misses `open` or the gain, or the frame's emission
  overwrites it.

- **INV-7** — `ut-shot --light-terms` with `--emission` is refused.
  *Test:* `tests/unit/ShotCliTest.cpp`.
  *Breaks when:* one file silently holds the other's contents.

- **INV-8** — The reference's direct term is exact: at a point a light
  reaches unblocked it is `luma(shownLight(lightAt))`, and with an occluder
  between them it is 0.
  *Test:* `tests/unit/ReferenceTest.cpp`.
  *Breaks when:* the shadow ray starts on the surface, or `shownLight` is
  skipped.

- **INV-9** — Sky light is counted as sky: in a fixture whose only light
  reaches the point through the sky view, channel 3 is above 0 and channel 1
  is 0; with no sky view both are 0.
  *Test:* `tests/unit/ReferenceTest.cpp`.
  *Breaks when:* the backdrop's own albedo stands in for the sky view, as in
  the scratch copy.

- **INV-10** — `trace`'s output does not depend on how many workers run it.
  *Test:* `tests/unit/ReferenceTest.cpp`: one worker and four give equal bytes.
  *Breaks when:* pixels share a random-number stream.

- **INV-11** — `score` reads a renderer equal to the reference as a 0 gap,
  and one at twice the reference as a total gap of 1 and one stop.
  *Test:* `tests/unit/ReferenceTest.cpp`.
  *Breaks when:* blocks are averaged over unlit pixels, or the gap is divided
  by the renderer's mean.

## 6. Failure modes

- **A material file from another bake.** Ids the bundle lacks are ignored and
  ids it has fall back to `DEFAULT_ALBEDO`, so the scores are wrong without
  an error. `trace` prints how many of the bundle's materials the file named;
  a count below the bundle's is the sign.
- **Pulsing lights.** The renderer multiplies a light by its flicker; the
  reference does not. Draw with `ut-shot --light-time 0`, and read a view
  dominated by a pulsing light with that in mind.
- **`LIGHT_GAIN` other than 1.** The reference's `shownLight` carries no
  gain. `light.glsl` sets it to 1 today; a change to it changes the
  renderer's terms and not the reference's.
- **Shadow-map blur.** The renderer's shadows are soft-edged and the
  reference's are exact, so the direct gap is never 0 on a shadowed view.
- **Noise.** Channels 1–3 are Monte Carlo estimates; their error falls with
  the square root of `samples`.

## 7. Tests

| Test | Label | Invariants |
|---|---|---|
| `tests/unit/BakeLightProbesTest.cpp` (existing) | unit | INV-1 |
| `tests/unit/MaterialLightTest.cpp` (new) | unit | INV-2 |
| `tests/unit/BakeCliTest.cpp` (existing, one case added) | unit | INV-3, INV-4 |
| the device tier (existing) | device | INV-5 |
| `tests/device/RenderLightTermsTest.cpp` (new) | device | INV-6 |
| `tests/unit/ShotCliTest.cpp` (existing, one case added) | unit | INV-7 |
| `tests/unit/ReferenceTest.cpp` (new) | unit | INV-8, INV-9, INV-10, INV-11 |

Each new case is seen to fail against the code before its change.

## 8. Alternatives considered (and rejected)

- **Store material light values in the bundle.** A format change, so every
  map is baked again for a developer tool's sake. The side file costs one
  flag.
- **Recompute albedo from the bundle's textures.** They are compressed, so
  the values differ from the bake's.
- **Write the terms over `outColour`**, as the scratch did. Eight bits, and
  bloom must be turned off in `post.frag`. The emission target is float and
  already read back.
- **Keep the scorer in Python.** It needs numpy, a new dependency; in C++ it
  is unit-tested beside the tracer.

## 9. Out of scope

- A test that bakes a level with a sky through `ubake::bake`, the survivor of
  UTA-0292's mutation run — deferred; not yet queued. The re-score of
  DM-ArcaneTemple grades that wiring meanwhile.
- RGB terms rather than luma — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BakeLightProbesTest.cpp` |
| INV-2 | `tests/unit/MaterialLightTest.cpp` |
| INV-3 | `tests/unit/BakeCliTest.cpp` |
| INV-4 | `tests/unit/BakeCliTest.cpp` |
| INV-5 | the device tier |
| INV-6 | `tests/device/RenderLightTermsTest.cpp` |
| INV-7 | `tests/unit/ShotCliTest.cpp` |
| INV-8 | `tests/unit/ReferenceTest.cpp` |
| INV-9 | `tests/unit/ReferenceTest.cpp` |
| INV-10 | `tests/unit/ReferenceTest.cpp` |
| INV-11 | `tests/unit/ReferenceTest.cpp` |

## 11. Cross-doc impact

- `docs/build-and-test.md`: how to run `ut-ref` against `ut-shot`.
- `CHANGELOG.md`: the new tool and the two flags.
- UTA-0112: none; § 4.1 changes no clause of it.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0292-reference-path-tracer-loop-log.md`.

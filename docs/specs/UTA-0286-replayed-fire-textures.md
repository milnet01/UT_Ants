<!-- ants-spec-format: 1 -->
# UTA-0286 — replayed fire textures

**Status:** accepted (2026-10-05), unreviewed. Contract reviews are cancelled for this
project (`CLAUDE.md` § Contract reviews); no review runs unless the user asks.
**Kind:** feature.
**Source:** ROADMAP UTA-0286 (user-request-2026-10-05; the replay decision,
user 2026-10-05).

**Layman:** blue sparks, lightning, shields, sparkles and the other
fire-system effects that stood frozen now move again, the way the original
game animated them.

## 1. Goal

A surface whose texture is a FireTexture that is not a flame moves. The bake
stores the texture's sparks, settings and palette; the renderer runs UT99's
FireTexture animation from them on the CPU, a fixed number of steps a second,
and uploads each new picture over the surface's frozen one. Flames keep
UTA-0263's shader.

## 2. Problem

1. The user's capture `CTF-Beatitude-20261005-121904` shows blue spray and
   sparks standing still over two fire basins. A `ut-shot` probe that dropped
   one material at a time showed `GreatFire2.offred` draws all of it
   (UTA-0286's roadmap body).
2. A FireTexture stores no pixels. `ubake/FireStill.h`'s `fireStill` runs the
   animation for `FIRE_STILL_FRAMES` steps and the bake keeps the last as an
   ordinary picture, so nothing moves.
3. `fireStill` does not model `Wheel` or `SphereLightning`, whose angles need
   sine and cosine, which the numeric contract keeps out of the baker
   (`docs/design.md`). It heats a scattered point for each instead.
   `tests/real/flame-labels.txt` calls `offred` blue sphere lightning, so its
   still is a field of scattered dots — the frozen shapes.
4. UTA-0263 replaced flames and left the rest still. 111 FireTextures that
   maps draw on BSP surfaces are labelled `other` there
   (`grep -c ' other$' tests/real/flame-labels.txt`): waterfalls, snow,
   lightning, shields, energy, sparkles.

## 3. Scope decisions (agreed with the user)

- **Non-flame FireTextures replay UT99's own animation, for now** — the user,
  2026-10-05, of three offered. The other two were one generic modern shimmer
  over the still, and a hand-made modern effect per family. § 8 says what
  lost.
- **Modern effects come later** — the user, same day: *"I would like this
  project to create better looking effects using modern techniques later."*
  Filed as UTA-0290.
- **Sparks, embers and smoke above flames are split out** — the user, same
  day. Filed as UTA-0291.
- **Flames are untouched** — this spec's call, following UTA-0263 § 3's
  "replaced, not replayed", which the user made for flames.
- **The bake's still is unchanged** — this spec's call. It stays the picture
  a renderer without replay would show, and the numeric contract stays whole.

## 4. Design

### 4.1 The animation, shared — new library `ufire`

`fireStill`'s simulation moves out of `ubake` into a new library,
`uta_ufire`, linked by `ubake` and `urender`. It depends on `uta_core` only.

```cpp
namespace uta::ufire {
/// One spark as UT99 stores it -- the type, its heat, its place, four bytes
/// the type gives meaning to.
struct Spark { std::uint8_t type, heat, x, y, byteA, byteB, byteC, byteD; };

struct Settings {
    std::uint8_t renderHeat = 0;  ///< how slowly heat fades; 255 fades least
    bool rising = false;          ///< the field moves up a row a step
    std::int32_t sparksLimit = 0; ///< sparks and live particles together
};

/// Wheel and SphereLightning: heat a scattered point, as the baker must
/// (Scatter), or turn and draw lines as UT99 does (Model).
enum class Turning { Scatter, Model };

class Fire {
public:
    Fire(std::uint32_t width, std::uint32_t height, std::vector<Spark> sparks,
         const Settings& settings, Turning turning);
    void step();                          ///< one UT99 frame of the animation
    std::span<const std::uint8_t> heat() const noexcept; ///< row by row, top first
};
}
```

**`Turning::Scatter` is today's `fireStill` exactly.** `fireStill` becomes a
wrapper: it converts its `upkg::Spark`s, builds a `Fire` with `Scatter` and
steps it `FIRE_STILL_FRAMES` times. Its xorshift sequence, particle rules and
fade table are moved, not changed.

**`Turning::Model` adds two types**, adapted from SurrealEngine's
`UFireTexture::UpdateFrame`, as `fireStill` already is:

- **`Wheel` (26)** emits a twirl particle while `canEmit` holds: it starts at
  the spark, angle `byteA * 360/256` degrees, turning
  `byteD * (16/256) * (360/256)` degrees a step, for `byteB` steps; the
  spark's own angle then advances by `byteC`. A twirl heats its pixel at
  `heat`, then moves half a texel along `(sin angle, cos angle)`.
- **`SphereLightning` (25)**, when a random byte is at least `byteD`, draws a
  line from the spark at a random angle, `byteC / 2` texels long, its heat
  falling from `heat` to `heat / 4` along it. Each half-texel the line's
  origin wanders by a random amount in -1 to 1 on each axis, from the same
  xorshift sequence.

Every other type behaves as under `Scatter`.

### 4.2 `MATS` gains a fire look — `ubundle`

```cpp
inline constexpr std::uint16_t FIRE_SIZE_MAX = 1024;
inline constexpr std::uint16_t FIRE_SPARKS_MAX = 4096;

/// A non-flame FireTexture's own values -- UTA-0286 SS 4.2.
struct FireLook {
    std::array<std::uint16_t, 2> size{}; ///< the texture's own width and height, texels
    std::uint8_t renderHeat = 0;
    std::uint8_t rising = 0;             ///< 0 or 1
    std::uint8_t masked = 0;             ///< 1: heat 0 is see-through, as the material's variant
    std::int32_t sparksLimit = 0;
    float maxFrameRate = 0;              ///< as stored; finite, 0 or more
    std::array<std::array<std::uint8_t, 3>, 256> palette{}; ///< sRGB bytes, index = heat
    std::vector<ufire::Spark> sparks;    ///< in stored order
    friend bool operator==(const FireLook&, const FireLook&) = default;
};
```

`MaterialRecord` gains `std::optional<FireLook> fire`, after `liquid`. On the
wire it is one byte — 0 for none, 1 for a look — then, for a look, the fields
in the order above, `sparks` as a `u16` count and eight bytes a spark.
`read` and `write` refuse a `size` side of 0 or above `FIRE_SIZE_MAX`, more
than `FIRE_SPARKS_MAX` sparks, a `rising` or `masked` byte above 1, a
`maxFrameRate` that is negative or not finite, and a record carrying both a
flame look and a fire look.

`ubundle` gains a dependency on `uta_ufire` for `Spark`.

`FORMAT_VERSION` becomes `21` and `BAKER_REVISION` becomes `39`. `shapeOf`
in `urender/Frame.cpp` samples each fire look, since the renderer uploads it
(`new-bundle-section-needs-the-fingerprint` is the failure this prevents).

### 4.3 Which materials carry one — `ubake`

A material carries a fire look when its texture's class is `FireTexture`, it
stores no pixels (the branch that calls `fireStill` today), and the flame
list (`umat::isFlame`) does not name it. Its fields are the texture's stored
`RenderHeat`, `bRising`, `SparksLimit` and `MaxFrameRate`, each 0 or false
where the export stores none, as `fireSettingsOf` reads the first three
today; the texture's sparks; its palette's first 256 entries; and whether the
material is the masked variant.

A palette with fewer than 256 entries, or a size or spark count past § 4.2's
limits, leaves the material with no fire look and its still; the bake
reports it as it reports a skipped liquid look (§ 6).

### 4.4 Drawing — `urender`

**The picture.** For each material with a fire look, the renderer builds a
`ufire::Fire` with `Turning::Model`, steps it `FIRE_STILL_FRAMES` times, and
uploads its picture as an uncompressed `R8G8B8A8_SRGB` image of the look's
size with one mip level, appended to the bindless texture array. The
material's `base` names that image; its `normal`, `rough` and `height` are
the defaults and its `emit` is none, since the bake made them from the still.
A texel's colour is the palette entry its heat names; its alpha is 0 where
the look is masked and the heat is 0, and 255 otherwise. `scene.frag` is
unchanged: it samples `base` and discards masked texels as it does today.

**The clock.** Steps run at `stepsPerSecond`: the look's `maxFrameRate` when
it is above 0, capped at 60; 30 when it is 0. UT99 steps a FireTexture whose
`MaxFrameRate` is 0 once a drawn frame, and UTA-0263 § 4.6's fit measured the
original at 30 frames a second. The renderer reads the same light seconds the
flames read (`Renderer::pinLightSeconds` pins both). A draw's target is
`floor(seconds * stepsPerSecond)` steps:

- below the steps already run, the fire restarts from its primed state;
- above, it runs the difference — all of it when the light time is pinned,
  at most `FIRE_CATCH_UP_STEPS` (16) when it is not, counting the rest as
  run so a stalled frame does not stall the next.

A draw that ran any step uploads the new picture through one host-visible
staging buffer, recorded before the scene pass. The renderer has one frame
in flight and waits on it (`Renderer::draw`), so one staging buffer is safe.

### 4.5 What does not change

The bake's still and its maps; flames; liquids; `scene.frag`; a FireTexture
drawn by an actor, which `urender` does not draw yet.

## 5. Invariants

- **INV-1** — `MATS` round-trips a material with a fire look and one without,
  and refuses a `size` side of 0 or above `FIRE_SIZE_MAX`, more than
  `FIRE_SPARKS_MAX` sparks, a `rising` or `masked` byte above 1, a
  `maxFrameRate` that is negative or not finite, and a record with both a
  flame and a fire look.
  *Test:* `tests/unit/BundleMaterialTest.cpp`, extended.
  *Breaks when:* the look is dropped, misplaced or read into the wrong
  record, or one of the invalid looks is accepted.

- **INV-2** — a baked non-flame FireTexture carries a fire look holding its
  stored sparks, settings and palette and its masked variant; a setting it
  does not store reads 0 or false; a flame FireTexture, a plain Texture and a
  WetTexture carry none.
  *Test:* `tests/unit/BakeFireLookTest.cpp`, new, on `BakeFixture` with a
  non-flame FireTexture storing `MaxFrameRate` and one storing none, a flame
  FireTexture through the flame lookup, a plain Texture and a WetTexture.
  *Breaks when:* a flame gains a look, a non-flame loses one, a stored setting
  is ignored, or the masked variant is not recorded.

- **INV-3** — `Turning::Scatter` gives `fireStill`'s pictures byte for byte as
  they were before the move.
  *Test:* `tests/unit/BakeFireStillTest.cpp`, extended: one still per spark
  type 0 to 28 on a 32x32 field, hashed, against hashes recorded from the
  pre-move build.
  *Breaks when:* the move changes the random sequence, a particle rule, the
  fade table or the order sparks run in.

- **INV-4** — under `Turning::Model`, one step from cold of a lone
  `SphereLightning` spark with `byteD` 0 heats a texel at least 8 texels from
  the spark, and a lone `Wheel` spark's twirl moves from the spark within its
  `byteB` steps; under `Scatter` neither heats anything over 2 texels away.
  *Test:* `tests/unit/FireReplayTest.cpp`, new.
  *Breaks when:* the two types fall back to scatter under `Model`, or `Model`
  leaks into `Scatter`.

- **INV-5** — a texel's colour is the palette entry of its heat; its alpha is
  0 only where the look is masked and the heat is 0.
  *Test:* `tests/unit/FireReplayTest.cpp`, on the colouring function, masked
  and opaque.
  *Breaks when:* heat 0 is see-through on an opaque look or solid on a masked
  one, or the palette is indexed off by one.

- **INV-6** — `stepsPerSecond` is `maxFrameRate` in (0, 60], 60 above that,
  and 30 at 0.
  *Test:* `tests/unit/FireReplayTest.cpp`.
  *Breaks when:* a stored rate is ignored, the cap is missing, or 0 reads as
  no steps.

- **INV-7** — a surface wearing a fire material draws the same pixels at one
  pinned light time on two draws, and different pixels at a second pinned
  time.
  *Test:* `tests/device/RenderFireReplayTest.cpp`, new, a one-quad bundle
  whose material carries a fire look with a `Sparkle` spark.
  *Breaks when:* the renderer draws the still, never steps, or steps on
  anything but the light time.

## 6. Failure modes

- **A palette short of 256 entries, or a look past § 4.2's limits** — no fire
  look; the still is drawn; the bake's report names the material and why, as
  for a skipped liquid look.
- **A bundle with a fire look whose TEXS maps are missing** — the material
  draws the replayed picture with default normal, roughness and height; the
  look does not depend on the maps.
- **A light time that is not finite** — reads as 0, as the flames' clock does.

## 7. Tests

| Test | Invariants |
|------|-----------|
| `tests/unit/BundleMaterialTest.cpp` | INV-1 |
| `tests/unit/BakeFireLookTest.cpp` | INV-2 |
| `tests/unit/BakeFireStillTest.cpp` | INV-3 |
| `tests/unit/FireReplayTest.cpp` | INV-4, INV-5, INV-6 |
| `tests/device/RenderFireReplayTest.cpp` | INV-7 |

## 8. Alternatives considered (and rejected)

- **A generic modern shimmer over the still** — offered; the user chose
  replay. Every family would move alike, and none as its original does.
- **A hand-made modern effect per family** — offered; deferred to UTA-0290 as
  the later step. Many times the work, and a family nobody wrote stays still.
- **Running the animation on the GPU** — a compute pass per texture. The
  fields are small (`FIRE_SIZE_MAX` bounds them) and the particle list is
  serial, so the CPU step is cheaper to write and to test, and costs little.
- **A flipbook baked into the bundle** — N stills a texture. Megabytes a map
  for a loop that visibly repeats, where the sparks cost bytes.
- **Modelling Wheel and SphereLightning in the bake's still too** — puts sine
  and cosine in the baker against the numeric contract, and changes every
  still for a picture the renderer now replaces anyway.

## 9. Out of scope

- Modern replacements for these effects — UTA-0290.
- Sparks, embers and smoke above flames — UTA-0291.
- The spark types `fireStill` does not model besides Wheel and
  SphereLightning (Cylinder, Lissajous, Jugglers, Fountain, Flocks, Eels,
  Organic, the clouds, Stars, the two other lightnings, Gametes, Sprinkler)
  keep scatter's rule. Deferred; not yet queued.
- `DrawMode`'s lathe modes and `PrimeCount` — not read. Deferred; not yet
  queued.
- FireTextures on actors: `urender` draws no actors yet.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleMaterialTest.cpp` |
| INV-2 | `tests/unit/BakeFireLookTest.cpp` |
| INV-3 | `tests/unit/BakeFireStillTest.cpp` |
| INV-4, INV-5, INV-6 | `tests/unit/FireReplayTest.cpp` |
| INV-7 | `tests/device/RenderFireReplayTest.cpp` |
| § 4.4's catch-up cap when unpinned | **nothing** — every test pins the light time |
| `shapeOf` samples the fire look | **Partial:** a stale upload shows only on a re-bake in a running viewer; no test re-bakes under a live renderer |
| The replay looks like the original | **nothing** — no paired capture of the original is taken |

## 11. Cross-doc impact

- `docs/specs/UTA-0263-shader-flames.md` § 9: non-flame FireTextures moving
  is this item's, not UTA-0105's; sparks above a flame are UTA-0291's.
- `src/ubake/FireStill.h`'s header: moving the picture is UTA-0286's.
- `CLAUDE.md` § Standing facts: the format and baker revision numbers.

## 12. Cold-eyes loop log

`docs/reviews/UTA-0286-replayed-fire-textures-loop-log.md`.

## 13. Resource cost

- **Bundle:** about 790 bytes a fire look, plus 8 a spark.
- **GPU:** one RGBA8 image a fire material, its size times 4 bytes, and a
  staging buffer of the same total.
- **CPU:** a step costs one pass over the field plus the sparks and
  particles; at most 60 steps a second a texture.
- **Frame time:** measured with `ut-bench frame` at `--tier ultra --size
  3840x2160` on CTF-Beatitude, before and after. Recorded on the roadmap
  item. No budget is set in advance.

## 14. Migration / compatibility

`FORMAT_VERSION` becomes `21`. `ubundle::read` refuses any other version, so
every map baked before this item must be baked again. `BAKER_REVISION`
becomes `39`, so the bake cache re-bakes on its own.

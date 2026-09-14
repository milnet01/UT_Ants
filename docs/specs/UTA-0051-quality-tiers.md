# UTA-0051 — quality tiers and dynamic resolution

**Status:** accepted (2026-09-14), at the review's cap of two loops for a spec — a calm cap. The loop log is [`docs/reviews/UTA-0051-quality-tiers-loop-log.md`](../reviews/UTA-0051-quality-tiers-loop-log.md).
**Kind:** implement.
**Source:** ROADMAP UTA-0051 (user-request-2026-09-04).
**Blocked by:** none — `UTA-0014`'s draw path shipped.
**Blocker for:** `UTA-0015`, `UTA-0040`, `UTA-0044` and `UTA-0045`, each of
which declares its tier in § 4.1's table.
**Pairs with:** `UTA-0052` (the texture figure § 4.2 maps), `UTA-0053` (the
sharpening pass that will read the render scale), `UTA-0076` (the upscaler),
`UTA-0152` (smaller texture copies for a low tier).

**Layman:** The game picks a quality level that suits your graphics card, lets
you choose another, and quietly lowers its internal resolution when frames slow
down instead of stuttering.

## 1. Goal

`urender` has four named quality tiers. Every visual feature that costs enough
to switch off declares, in one table, the lowest tier that switches it on. The
tier is chosen from the device unless the caller names one. Dynamic resolution
holds a frame-time target by drawing the scene at a fraction of the output size
and stretching it to full size. `ut-ants` takes `--tier` and turns dynamic
resolution on.

## 2. Problem

1. **Nowhere to declare cost.** The roadmap bodies of `UTA-0015`, `UTA-0040`,
   `UTA-0044` and `UTA-0045` each say they need *"a tier assigned by UTA-0051
   rather than a default chosen here"*. Without a table, each invents its own
   switch.
2. **No way to hold a frame rate.** `Renderer::Impl::createTargets` sizes every
   target to `Config::width` × `Config::height`, so on a weak card the only
   lever is a smaller window.
3. **A promise nobody has kept.** `UTA-0052` § 3 decision 1: *"UTA-0051 later
   declares which tier maps to which figure, and it binds to this number."*

## 3. Scope decisions (agreed with the user)

All made by the user on 2026-09-14.

1. **A short spec.** Only what later items bind to is pinned here. How the
   resolution controller steps and damps is the implementation's.
2. **Dynamic resolution is part of this item.**
3. **Four tiers: Low, Medium, High, Ultra.**
4. **The default comes from graphics memory**: built-in graphics, or under
   2 GB, is Low; 2 to 4 GB Medium; 4 to 8 GB High; 8 GB or more Ultra. The
   caller may override it. § 4.3 gives the thresholds as measured values.
5. **The target is 60 frames a second.** The render scale may fall to 0.50 on
   Low, 0.60 on Medium and 0.75 on High. Ultra never falls below 1.
6. **Every tier maps to the one baked texture figure.** A smaller figure for a
   low tier needs smaller texture copies at upload, which is `UTA-0152`.

## 4. Design

### 4.1 The tiers and the feature table

Public, in `src/urender/Renderer.h`, which may carry no Vulkan type (`UTA-0014`
INV-2):

```cpp
enum class Tier : std::uint8_t { Low = 0, Medium = 1, High = 2, Ultra = 3 };

/// "low", "medium", "high", "ultra", case-insensitive; empty otherwise.
[[nodiscard]] std::optional<Tier> tierNamed(std::string_view name) noexcept;
[[nodiscard]] std::string_view tierName(Tier tier) noexcept; // lower case

struct Config {
    // ...existing fields...
    std::optional<Tier> tier;       ///< unset: chosen from the device (§ 4.3)
    bool dynamicResolution = false; ///< § 4.4's controller
    /// Fixes the render scale, with or without `dynamicResolution`, clamped to
    /// the tier's floor. For tests and diagnosis; unset in normal play.
    std::optional<double> fixedRenderScale;
};

struct FrameStats {
    // ...existing fields...
    Tier tier = Tier::Low;         ///< the tier in use
    double renderScale = 1;        ///< the scale this frame was drawn at
    double frameMilliseconds = 0;  ///< § 4.4's measurement of this frame
};
```

Internal, in a new `src/urender/Tiers.h` with no device in it, so the unit tier
grades it:

```cpp
struct TierSettings {
    double minimumRenderScale;
    double frameTimeTargetMilliseconds;
    std::uint64_t textureBudgetBytes;
};
[[nodiscard]] constexpr TierSettings settingsOf(Tier tier) noexcept;

/// One enumerator per visual feature a tier switches. Empty until UTA-0015 and
/// UTA-0040 add the first.
enum class Feature : std::uint8_t {};
/// A switch over every enumerator, with no default case.
[[nodiscard]] constexpr Tier minimumTier(Feature feature) noexcept;
[[nodiscard]] constexpr bool enabled(Feature feature, Tier tier) noexcept;

[[nodiscard]] Tier defaultTier(VkPhysicalDeviceType type, std::uint64_t deviceLocalBytes) noexcept;
[[nodiscard]] double nextRenderScale(double current, double measuredMilliseconds,
                                     const TierSettings& settings) noexcept;
```

**A feature reads its tier only through `enabled`.** A later item adds one
`Feature` enumerator and one `minimumTier` case, and never a `Config` field of
its own. Everything `UTA-0014` draws today stays on at every tier: shadows that
move are `S1`'s. The cluster grid stays 16 × 8 × 24 at every tier. Varying it is
a `Feature` row for whichever item needs it.

### 4.2 The texture figure

`settingsOf(tier).textureBudgetBytes` is 1024 MiB for every tier, equal to
`umat::TEXTURE_BUDGET_BYTES`. `urender` may not link `umat` (`docs/design.md`
rule 2, asserted for `ut-ants` by `apps/ut-ants/CMakeLists.txt`). So the figure
is written twice and a unit test holds the two equal.

### 4.3 The default tier

`Renderer::create` picks the tier once, from the chosen device. `Config::tier`
replaces the choice when set.

| Device | Tier |
|---|---|
| `VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU` or `_CPU`, any memory | Low |
| otherwise, device-local memory under 1.5 GiB | Low |
| 1.5 GiB up to 3.5 GiB | Medium |
| 3.5 GiB up to 7.5 GiB | High |
| 7.5 GiB and over | Ultra |

**Device-local memory** is the sum of the sizes of the heaps flagged
`VK_MEMORY_HEAP_DEVICE_LOCAL_BIT`, read from `Gpu`'s
`VkPhysicalDeviceMemoryProperties`. A card reports a little less than the size
it is sold as, so each threshold sits half a GiB below the user's figure.
Integrated graphics is Low whatever it reports, because it reports system
memory.

The renderer logs the tier at startup, and whether it was chosen or given.

### 4.4 Dynamic resolution

**Scaling applies when `Config::dynamicResolution` is set, or at the given
scale when `Config::fixedRenderScale` is.** With neither, every frame draws at
scale 1, exactly as `UTA-0014` does.

**The scene draws into a region, and no target is resized.** `hdr`, `velocity`
and `depth` keep the output size. At scale `s` the scene draws into the top-left
`max(1, ceil(s·W))` × `max(1, ceil(s·H))` pixels. The projection, the cluster
grid, the jitter and `FrameData::viewportSize`, which `scene.frag` divides by to
find a pixel's cluster, all use that region's size. **Shadow planning keeps
`W` × `H`.** A tile's size follows its light's size on the target
(`shadowTileSize`), so planning at the region's size would re-place every tile
at each change of scale, and a still camera would no longer draw no tiles, as
`FrameStats::renderedShadowTiles` says it does. The post stage
samples the region with linear filtering and writes `output` at `W` × `H`. A
change of scale creates or destroys no image; `resize` alone rebuilds targets,
as `UTA-0014` § 4.3 says.

**The measurement is the wall time of the frame's `Gpu::run`**, which submits
and waits for the GPU to finish. It excludes `Swapchain::present`, because the
swapchain uses `VK_PRESENT_MODE_FIFO_KHR`, so time between frames sits at the
display's refresh whatever the GPU's headroom.

**The controller is `nextRenderScale`, and INV-5 states what it must do.** Its
step size and damping are the implementation's.

**No mip bias.** `UTA-0014` § 4.11's formula is for a temporal upscaler, which
is `UTA-0076`.

**`readback(Velocity)` is refused while the last frame's scale is below 1.** The
region is smaller than the image, and the target's consumer, `UTA-0075`, has not
been built. `readback(Colour)` still returns `W` × `H`.

### 4.5 `ut-ants`

`--tier <low|medium|high|ultra>` sets `Config::tier`, and `ut-ants` always sets
`Config::dynamicResolution`. A `--frames` run's report adds the tier and the
last frame's render scale. `ut-ants`'s command line is not a breaking surface
(`docs/standards/versioning-overrides.md` § Breaking surfaces).

## 5. Invariants

- **INV-1** — `settingsOf` gives Ultra a floor of exactly 1, each lower tier a
  floor no higher than the tier above it, and every tier a target of
  1000 / 60 ms. Low 0.50, Medium 0.60, High 0.75.
  *Test:* `tests/unit/RenderTiersTest.cpp`, one case per tier asserting its
  literal.
  *Breaks when:* a floor rises above its upper neighbour, or Ultra's falls below
  1.

- **INV-2** — every tier's `textureBudgetBytes` equals
  `umat::TEXTURE_BUDGET_BYTES`.
  *Test:* `tests/unit/RenderTiersTest.cpp`, whose binary links both libraries.
  *Breaks when:* the figure changes in one of its two places.

- **INV-3** — `defaultTier` follows § 4.3's table.
  *Test:* `tests/unit/RenderTiersTest.cpp`, one row per boundary: 1.5 GiB less
  one byte and exactly 1.5 GiB, the same at 3.5 and 7.5, and an integrated
  device and a CPU device, each at 16 GiB. Only the threshold under test separates each pair.
  *Breaks when:* a threshold moves, a comparison is inclusive on the wrong
  side, or memory is read before the device type.

- **INV-4** — the tier in use is `Config::tier` when set, else
  `defaultTier` of the chosen device, and `FrameStats::tier` reports it.
  *Test:* `tests/device/RenderTiersTest.cpp`: `tier` set to Medium, then to
  Ultra, draws a frame reporting that tier. Neither is Low, `FrameStats`'s
  starting value, so only the override can produce it.
  *Breaks when:* the override is ignored.

- **INV-5** — `nextRenderScale` stays in `[minimumRenderScale, 1]`, and at 1
  for Ultra. A frame slower than the target never raises the scale, and a frame
  faster than half the target never lowers it. On Low, Medium and High, a run of
  slow frames reaches the floor within 120 calls, and a run of fast ones reaches
  1 within 120 calls. The function keeps no state: the renderer may pass a
  frame time averaged over recent frames, and that is where smoothing lives.
  *Test:* `tests/unit/RenderTiersTest.cpp`, feeding each tier 120 frames at
  twice the target, then 120 at a quarter of it. Every returned scale is in
  bounds and never moves against its frame, and the last of each run is at its
  bound.
  *Breaks when:* the bounds are not applied, the comparison with the target is
  reversed, or the scale never reaches its bound.

- **INV-6** — with `dynamicResolution` off and no `fixedRenderScale`, every
  frame reports `renderScale` 1 and draws as `UTA-0014` does.
  *Test:* `tests/device/RenderOffscreenTest.cpp` asserts `renderScale` is 1
  after its draws; `UTA-0014`'s pixel tests stay unchanged and green.
  *Breaks when:* scaling runs without being asked for.

- **INV-7** — at a scale below 1 the output is the whole scene, stretched to
  `W` × `H`.
  *Test:* `tests/device/RenderTiersTest.cpp`: a fixture filling the view's left
  half red and its right half blue, drawn at `fixedRenderScale` 0.5 on Low. The
  frame reports `renderScale` 0.5, and `readback(Colour)` is `W` × `H`, with red
  at (`W`/4, 3`H`/4) and blue at (3`W`/4, 3`H`/4). Both points lie below an
  unstretched half-size region, so only the stretch colours them. With `W` a
  multiple of 4, the pixel just left of the centre line, (`W`/2 − 1, 3`H`/4),
  matches neither probe: the linear stretch blends red and blue there, where a
  full-size draw leaves it the red probe's colour.
  *Breaks when:* the post stage reads the whole target instead of the region, or
  the fixed scale is not applied.

- **INV-8** — `readback(Velocity)` is refused with `InvalidArgument` after a
  frame drawn at a scale below 1.
  *Test:* `tests/device/RenderTiersTest.cpp`.
  *Breaks when:* it returns a buffer whose pixels outside the region are stale.

## 6. Failure modes

- **A driver reports no device-local heap.** The sum is 0, so the tier is Low.
  The log line says so.
- **The GPU cannot hold the target even at the floor.** The scale stays at the
  floor and the frame rate falls. `FrameStats` shows both, which is what
  `UTA-0039`'s frame-rate floor measures.
- **A card idling between paced frames measures slower.** Measured on this
  machine's RX 6600 with `ut-ants --frames 300`, averaging the last 60 frames:
  8.59 ms a frame under FIFO against 4.80 ms with
  `MESA_VK_WSI_PRESENT_MODE=immediate`. GPU timestamps read 8.37 ms and
  4.23 ms, so the work itself took longer, not the wait. Near the target this
  can lower the scale on a card that would hold it at full power, and nothing
  here corrects for that.
- **Frame times jitter around the target.** INV-5 fixes only which way the scale
  moves. Damping, so the scale does not hunt, is the implementation's.
- **A window a few pixels wide at a low scale.** The region is at least 1 × 1,
  per § 4.4.
- **An unknown `--tier` name.** `ut-ants` refuses it with exit code 2 and the
  four names.

## 7. Tests

| File | Label | Locks |
|---|---|---|
| `tests/unit/RenderTiersTest.cpp` | `unit` | INV-1, INV-2, INV-3, INV-5, `tierNamed` |
| `tests/device/RenderTiersTest.cpp` | `device` | INV-4, INV-7, INV-8 |
| `tests/device/RenderOffscreenTest.cpp` | `device` | INV-6 |
| `tests/unit/ClientCliTest.cpp` | `unit` | `--tier`, and its refusal |

Each is watched failing before its code exists. INV-7 is also watched failing
with the stretch removed and the region kept, which is the mistake it is for.

## 8. Alternatives considered (and rejected)

- **A switch per feature.** Every later item would own a setting, and nothing
  would say which belong together on a weak card. The roadmap body rejects it.
- **Resizing the targets whenever the scale changes.** Every change allocates
  and frees GPU memory mid-play, which stutters, the opposite of the goal.
- **Timing the interval between frames.** Under FIFO it sits at the refresh
  interval, so the controller could never see headroom.
- **A short speed test at start.** The user declined it: nothing is saved
  between runs, so every start would pay for it.
- **A temporal upscaler as the stretch.** `UTA-0075` and `UTA-0076` are
  deferred out of 0.1.0; a linear stretch needs neither.

## 9. Out of scope

- Smaller texture copies so a low tier fits a smaller figure — tracked by
  UTA-0152.
- Sharpening the stretched image — tracked by UTA-0053.
- Temporal upscaling and its mip bias — tracked by UTA-0076.
- Saving a chosen tier between runs — tracked by UTA-0120, the menus.
- Holding a frame-rate floor across the map library — tracked by UTA-0039.
- A cluster grid that varies by tier — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|---|---|
| INV-1, INV-2, INV-3, INV-5 | `tests/unit/RenderTiersTest.cpp` |
| INV-4, INV-7, INV-8 | **Partial:** `tests/device/RenderTiersTest.cpp`, on the Linux legs; the Windows leg installs no Vulkan driver and runs none of it |
| INV-6 | **Partial:** `tests/device/RenderOffscreenTest.cpp`, on the Linux legs; not on Windows, for the same reason |
| INV-4, the tier chosen when none is given | **nothing** on CI — Mesa's CPU driver reports a CPU device, whose default is Low, and Low is also `FrameStats`'s starting value, so a renderer that never chooses still passes |
| § 4.1, a feature reading its tier only through `enabled` | **nothing** — no check looks for a feature reading `Config` directly |
| § 4.1, `minimumTier` covering every `Feature` | **nothing** — the build enables no warning flags, so a missing case compiles silently |
| § 4.3, the thresholds matching real cards | **nothing** — measured only on this machine's card; no small card is on hand |
| § 4.4, the presenting path at a reduced scale | **nothing** in CI — no leg has a display; checked by hand with `ut-ants --frames` |
| § 4.4, shadow planning at `W` × `H` | **nothing** — a mutation planning at the region's size passed every unit and device test, whose shadows draw at scale 1 |
| § 4.4, `FrameData::viewportSize` sized to the region | **nothing** — a mutation leaving it at `W` × `H` passed every test; INV-7's fixture is unlit, so no cluster lookup is graded at a reduced scale |
| § 4.4, the measurement leaving out display pacing | **nothing** in CI — checked by hand: wall time around `Gpu::run` matched GPU timestamps to within 0.6 ms a frame, under FIFO and with `MESA_VK_WSI_PRESENT_MODE=immediate` |

## 11. Cross-doc impact

- `docs/specs/UTA-0052-texture-memory-budget.md`: § 9's *"Mapping a quality
  tier onto a megabyte figure — tracked by UTA-0051"* becomes done, citing
  § 4.2.
- `README.md`: `ut-ants --tier`.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in [`../reviews/UTA-0051-quality-tiers-loop-log.md`](../reviews/UTA-0051-quality-tiers-loop-log.md).

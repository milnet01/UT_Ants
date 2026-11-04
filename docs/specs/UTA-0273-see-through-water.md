<!-- ants-spec-format: 1 -->
# UTA-0273 — see-through water: refraction, depth tint and the shore fade

**Status:** accepted (2026-11-04), unreviewed. Contract reviews are cancelled
for this project (`CLAUDE.md` § Contract reviews); no review runs unless the
user asks.
**Kind:** feature.
**Source:** ROADMAP UTA-0273 (user-request-2026-10-02, split from UTA-0089).
**Builds on:** UTA-0089 (`docs/specs/UTA-0089-water-reflections.md`): its
water normal, its reflection and its tier switch.

**Layman:** looking into water shows what is under it, bent by the ripples;
deeper water looks darker; and the water's own picture fades out at the
shore, so the edge of a pool is soft rather than a hard line.

## 1. Goal

A Wet or Wave liquid drawn translucent shows what lies behind it, bent by
its ripple, darkened by how much water the light crosses, and with its own
picture faded where the water is shallow. At the tiers that do not draw it,
water draws exactly as UTA-0089 left it.

## 2. Problem

The translucent pass blends `ONE, ONE_MINUS_SRC_COLOR`
(`Pipelines.cpp`, the `blended` attachment state): a translucent surface adds
its colour and dims what is behind by it. That blend can only lighten or
dim the background uniformly. It cannot move it (refraction), cannot darken
it by an amount the surface does not itself add (depth tint), and the
surface cannot know how far behind it the background is: the depth image
has `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` alone
(`Renderer::Impl::createTargets`), so no shader reads it.

## 3. Scope decisions (agreed with the user)

1. **Water first, the cheap part already shipped** (user, 2026-10-02):
   UTA-0089 drew reflections, Fresnel and less tiling; this item is the
   costlier part, "measured".
2. **Its own tier row and a frame measurement** (the roadmap item, 2026-10-02).
3. **Ultra only, at first** (this spec's call, 2026-11-04). The target is
   60 fps on High with headroom (`CLAUDE.md` § Standing facts), and six maps
   still miss it (UTA-0352). A feature that costs on every frame with water
   in view goes where it costs that target nothing. Lowering it to High is a
   later decision, on § 13's measurement.

## 4. Design

### 4.1 Which surfaces refract

A batch refracts when the tier draws `Feature::WaterRefraction` and its
material is a liquid UTA-0089 reflects (`waterReflects`: Wet or Wave) and
its polyFlags are Translucent and not Modulated. Ice, opaque liquids,
modulated sheets and every other translucent batch keep the existing blend.

### 4.2 The pass order and the copy

The translucent pass splits in two:

1. every translucent batch that does not refract, as today;
2. then, only if a refracting batch is drawn this frame, `hdr`'s drawn
   region is copied into a new image, `sceneCopy`, of the same format and
   size; and the refracting batches draw.

So water refracts everything drawn before it, other translucent sheets
included. A translucent sheet in front of the water is in the copy too, and
reappears through the water slightly bent: an accepted approximation (§ 6).
No frame without a refracting batch pays for the copy.

### 4.3 Reading depth

The depth image gains `VK_IMAGE_USAGE_SAMPLED_BIT`. For the refracting
draws it is in `VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL`, attached for the
depth test and bound for sampling at once, which Vulkan allows because
nothing writes it. `sceneCopy` and the depth image are bound to the scene
shader as two sampled images. The texture array stays the last binding of
its set, since its count is variable.

### 4.4 The refracting blend

A fourth `Blending`, `Refract`: blending off, depth tested, not written.
The shader computes what the old blend did, and more:

```
thickness  = linear(depth at the pixel) - linear(the water's own depth)
offset     = the ripple's tilt (UTA-0089's water normal), in screen pixels,
             scaled by REFRACTION_PIXELS and divided by the water's distance
behind     = sceneCopy at the pixel + offset, or at the pixel itself where
             the offset sample's depth is nearer than the water
tint       = exp(-thickness * ABSORPTION)        per channel, red the most
fade       = smoothstep(0, SHORE_UNITS, thickness)
picture    = the liquid's picture (UTA-0105), times fade
colour     = picture + behind * tint * (1 - picture)
colour    += reflection * share                   UTA-0089's, unchanged
```

At `thickness` 0 with no offset, `tint` is 1 and `fade` is 0: the water
draws as the background alone, which is the soft shore. Where `fade` is 1,
`ABSORPTION` 0 and the offset 0, `colour` is the old blend's result,
`picture + background * (1 - picture)`.

`REFRACTION_PIXELS`, `ABSORPTION` and `SHORE_UNITS` are first values, set by
eye on § 13's maps and recorded there with how they were chosen. They are not
fitted to the original game, which draws none of this.

### 4.5 Tiers

`Feature::WaterRefraction` is drawn from `Tier::Ultra` (`Tiers.h`). Below
it, no batch refracts, nothing is copied and the depth image is never
sampled.

## 5. Invariants

- **INV-1** — Below Ultra, water does not show its depth. A Wet square over
  a lit floor draws the same pixel at its centre whether the floor lies 16
  or 256 units below it, at High.
  *Test:* `tests/device/RenderRefractionTest.cpp`, "UTA-0273 INV-1".
  *Breaks when:* the refracting path or the copy runs below Ultra.
- **INV-2** — Deeper water is darker, red first. The same square at Ultra:
  the centre over the 256-unit floor is darker than over the 16-unit floor
  in every channel, and darker by more in red than in blue.
  *Test:* `tests/device/RenderRefractionTest.cpp`, "UTA-0273 INV-2".
  *Breaks when:* `thickness` is not read from the depth image, or `tint`
  is grey.
- **INV-3** — The shore is soft. Where the floor meets the water's plane,
  the pixel just inside the water's edge differs from the floor seen without
  any water by at most 4 levels a channel, at Ultra.
  *Test:* `tests/device/RenderRefractionTest.cpp`, "UTA-0273 INV-3".
  *Breaks when:* the picture is not faded by `thickness`.
- **INV-4** — A ripple bends what is behind. Over a checker floor, at Ultra,
  a Wave liquid with amplitude 40 changes more pixels against amplitude 0
  than the same pair does at High.
  *Test:* `tests/device/RenderRefractionTest.cpp`, "UTA-0273 INV-4".
  *Breaks when:* `behind` is read without the offset.
- **INV-5** — Nothing in front of the water shows through it. A red pillar
  standing in front of the pool, and nearer than it, never colours a water
  pixel beside its edge, at Ultra.
  *Test:* `tests/device/RenderRefractionTest.cpp`, "UTA-0273 INV-5".
  *Breaks when:* the offset sample is taken without the depth check.

## 6. Failure modes

- **A translucent sheet in front of the water** is in the copy and is drawn
  over by the water, so it shows bent through the water rather than crisp
  over it. Accepted: drawing it after the water would put sheets behind the
  water over it instead, which is worse.
- **The offset reaches past the drawn region** under dynamic resolution or a
  render scale: the sample is clamped to the region, so it repeats the edge.
- **Seen from below**, `thickness` is the distance to whatever is behind the
  surface above, and the same formula applies. Not tuned.
- **Two refracting surfaces overlap**: the second reads a copy without the
  first. Accepted, as both are water.

## 7. Tests

INV-1, INV-2, INV-3, INV-4 and INV-5 are device tests, run on Mesa's CPU driver and the GPU under
the validation layer and sync validation, as every device test is. The
existing UTA-0089 and UTA-0105 device tests run at Medium and Low and must
pass unchanged.

## 8. Alternatives considered (and rejected)

- **Keep the existing blend and draw only the tint and the shore fade.** The
  blend cannot darken the background by more than the water adds, so the
  tint would be a wash of colour, not depth. Rejected.
- **A copy every frame.** It costs every frame, water or not. Rejected for
  the copy only when a refracting batch is drawn.
- **Screen-space refraction from the previous frame's image.** No copy, but
  it shows last frame's picture and its history breaks on a cut. Rejected.

## 9. Out of scope

- Refraction for glass (UTA-0272's panes): a later item if wanted.
- Caustics changes: UTA-0215 owns them.
- Lowering the tier to High: § 3 item 3.

## 10. What checks this

| Claim | Check |
|---|---|
| INV-1 to INV-5 | the device tests in § 7 |
| No copy without a refracting batch | **Partial:** § 13's measurement; no test counts copies |
| UTA-0089's look unchanged below Ultra | its own device tests, at Medium and Low |

## 11. Cross-doc impact

- `docs/specs/UTA-0089-water-reflections.md` § 9 names this item; no
  clause there changes.
- `CLAUDE.md` names no tier contents; nothing to change there.

## 12. Cold-eyes loop log

`docs/reviews/UTA-0273-see-through-water-loop-log.md`. No review runs: the
gate is cancelled for this project.

## 13. Resource cost

To be measured when built: `ut-bench frame` at Ultra, 3840x2160, on maps
with water in view (DM-ArcaneTemple's pool, AS-OceanFloor), the refraction
on against off in one job, the copy's time named apart. Recorded here with
the date.

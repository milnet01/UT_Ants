# UTA-0051 — quality tiers and dynamic resolution — loop log

The review record for `docs/specs/UTA-0051-quality-tiers.md`, kept outside the
spec per `~/.claude/standards/spec-format.md` § 6. `review-contract` writes one
row per loop as it closes. Rows are never back-filled and a landed row is never
edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-14 | 3 | 0 | 1 | 0 | 3 | 4 findings, 4 verified / 0 dismissed, 4 fixed. All three lanes: INV-5 passed for a controller that never moves [Q4]; INV-7's probes passed for a draw at scale 1 [Q4]; § 4.4 made scaling depend on dynamicResolution while INV-6 to INV-8 used fixedRenderScale alone [Q2]. Two lanes: INV-4's unset-tier half could not fail on Mesa's CPU driver [Q4], now a nothing row in § 10 (llvmpipe reports PHYSICAL_DEVICE_TYPE_CPU, checked with vulkaninfo). Five open questions resolved clean and are not counted. Loop 2 dispatched. |
| 2 | 2026-09-14 | 3 | 0 | 2 | 1 | 2 | 5 findings, 5 verified / 0 dismissed, 5 fixed. Lane A: INV-5 required every slow frame to lower the scale while damping was the implementation's [Q2]; INV-7 still passed a full-size draw reporting 0.5, now caught by a blend probe [Q4]. Lane C: § 4.4's region list omitted FrameData::viewportSize, which scene.frag divides by [Q3]. Lane B: INV-3 had no CPU-device row [Q4]. Orchestrator, from lane B's open question: shadow planning at the region's size would re-place tiles on every scale change, against FrameStats::renderedShadowTiles; it keeps W x H [Q2]. **Cap reached (2 for a spec). A calm cap: 2 of this loop's 5 verified findings landed on text this run wrote (INV-5, INV-7).** 9 of the run's 9 verified findings fall inside the gated span 2b7785d, the commit adding the spec. The cap sweep found no collateral. 322 lines. Accepted; implementation is the next reviewer. |
| 2-impl | 2026-09-14 | 0 | — | — | — | — | Implementation, not a review loop: no reviewer was dispatched, and the Q columns count nothing. Mutation found two section 4.4 rules no test grades (shadow planning at W x H, viewportSize at the region), now nothing rows in section 10. A hand run suspected the wall-time measurement carried display pacing, and the clause was switched to GPU timestamps; a probe then printed both per frame (FIFO 8.59 wall / 8.37 GPU ms, immediate 4.80 / 4.23), so the pacing premise was false and the change was reverted to the reviewed wording. What it did find, a card idling under FIFO measuring slower, is a section 6 failure mode. |

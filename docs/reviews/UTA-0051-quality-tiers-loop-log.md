# UTA-0051 — quality tiers and dynamic resolution — loop log

The review record for `docs/specs/UTA-0051-quality-tiers.md`, kept outside the
spec per `~/.claude/standards/spec-format.md` § 6. `review-contract` writes one
row per loop as it closes. Rows are never back-filled and a landed row is never
edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-14 | 3 | 0 | 1 | 0 | 3 | 4 findings, 4 verified / 0 dismissed, 4 fixed. All three lanes: INV-5 passed for a controller that never moves [Q4]; INV-7's probes passed for a draw at scale 1 [Q4]; § 4.4 made scaling depend on dynamicResolution while INV-6 to INV-8 used fixedRenderScale alone [Q2]. Two lanes: INV-4's unset-tier half could not fail on Mesa's CPU driver [Q4], now a nothing row in § 10 (llvmpipe reports PHYSICAL_DEVICE_TYPE_CPU, checked with vulkaninfo). Five open questions resolved clean and are not counted. Loop 2 dispatched. |

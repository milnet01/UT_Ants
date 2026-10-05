# UTA-0277 — per-tile variation for natural textures — loop log

The review record for `docs/specs/UTA-0277-per-tile-variation.md`, kept
outside the spec per `~/.claude/standards/spec-format.md` § 6. `review-contract`
writes one row per loop as it closes. Rows are never back-filled and a landed
row is never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| impl | 2026-10-05 | none: no reviewer dispatched (gate cancelled for this project) | - | - | - | - | Implementation fold-back, not a review. Building it proved three clauses false: answers cannot enter the bake's name, which exists before any picture is decoded, so they apply where a bundle loads (SS 4.3, INV-3, INV-6); excluding parallax excluded every generated material, so the march blends the height (SS 4.2, SS 4.4); and the scores were refit as built (box high-pass, diagonal bands, 64 x 64 autocorrelation; limits 5/12 and 0.5/0.75). Also: the view distance capped at 192 units, the tier set at Medium from a measurement. |

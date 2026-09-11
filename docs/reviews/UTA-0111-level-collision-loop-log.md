# UTA-0111 — `ubake`: write the level's collision — loop log

The review record for `docs/specs/UTA-0111-level-collision.md`, kept outside
the spec per `~/.claude/standards/spec-format.md` § 6. `review-contract` writes
one row per loop as it closes. Rows are never back-filled and a landed row is
never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-11 | 3 | 1 | 2 | 2 | 2 | 7 verified, 0 dismissed, all fixed. § 4.5 now says which child a point in front of a plane takes, settled by walking every `PlayerStart` both ways, and that outline order says nothing about facing; no outline is reversed under a mirror. INV-7's cases call `buildCollision` directly, and its bake cases break only a coplanar link no earlier step reads. `polyFlags` are read only for nodes with vertices, found from two lanes' open questions. § 6 and INV-7 name the `Model` for `rootOutside`. INV-3, INV-4 and INV-5 build their `Model` in memory; the test writer's `iFront`/`iBack` order is filed as UTA-0122. Of three open questions, one resolved clean, one became a finding, and one was measured into § 4.5's hull sentence. |

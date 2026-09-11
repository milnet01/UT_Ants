# UTA-0121 — bot path seeds for UT99's maps — loop log

The review record for `docs/specs/UTA-0121-bot-path-seeds.md`, kept outside
the spec per `~/.claude/standards/spec-format.md` § 6. `review-contract` writes
one row per loop as it closes. Rows are never back-filled and a landed row is
never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-11 | 3 | 1 | 5 | 0 | 1 | 7 verified, 0 dismissed, 7 fixed. A route's last spot yields to a navigation point within 50, which takes its place in the chain, and the next hop is measured from it. Routes are `none` only when nothing of the start's part is on the walk graph. A hop must also miss every mover box and keep walkable floor under it. Positions are rounded once to float and written as that float. The "Scout didn't fit" premise is deleted: their GAME-0023 is a crash with no seeds, not a refused seed; the Scout's test size is § 14's open question. Spots on one ramp join past the step. INV-4's wall is thin enough that both spots exist. Two sentences the fixes left wrong, § 3 decision 5 and § 8's editor claim, were corrected in the same pass. Also this loop, an authoring edit, not a finding: UT_MonsterHunt asked that a map with no file in `Maps/` be skipped, not refused, and confirmed § 14's four questions. Of the open questions, the Scout's size became § 14's; the material lookup, coplanar nodes and their seed grid resolved clean. |

# UTA-0121 — bot path seeds for UT99's maps — loop log

The review record for `docs/specs/UTA-0121-bot-path-seeds.md`, kept outside
the spec per `~/.claude/standards/spec-format.md` § 6. `review-contract` writes
one row per loop as it closes. Rows are never back-filled and a landed row is
never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-11 | 3 | 1 | 5 | 0 | 1 | 7 verified, 0 dismissed, 7 fixed. A route's last spot yields to a navigation point within 50, which takes its place in the chain, and the next hop is measured from it. Routes are `none` only when nothing of the start's part is on the walk graph. A hop must also miss every mover box and keep walkable floor under it. Positions are rounded once to float and written as that float. The "Scout didn't fit" premise is deleted: their GAME-0023 is a crash with no seeds, not a refused seed; the Scout's test size is § 14's open question. Spots on one ramp join past the step. INV-4's wall is thin enough that both spots exist. Two sentences the fixes left wrong, § 3 decision 5 and § 8's editor claim, were corrected in the same pass. Also this loop, an authoring edit, not a finding: UT_MonsterHunt asked that a map with no file in `Maps/` be skipped, not refused, and confirmed § 14's four questions. Of the open questions, the Scout's size became § 14's; the material lookup, coplanar nodes and their seed grid resolved clean. |
| 2 | 2026-09-11 | 3 | 0 | 3 | 2 | 0 | 5 verified, 0 dismissed, 5 fixed. The run summary's entries are pinned (status written, skipped or refused; why; exits and nodes), and the summary is also written into the output directory, where UT_MonsterHunt reads. § 4.3's example numbers are written as `to_chars` prints them. On a `PARTITIONED` map the search stops at the part of the network that reaches the exit, so the chain bridges the gap without repeating that part; `propose` takes the group. A navigation point takes a spot's place only over an allowed hop. The directory summary came from lane 2's open question and lane 1's finding. The run-order and seed-grid questions resolved clean. At the cap, a spec's being 2, and a calm one: 2 of the 5 (the substitution hop and the directory summary) landed on text loop 1 wrote, the other 3 on the draft. The gated span is the whole document, so all 12 of the run's findings fall inside it. The tail is empty. Accepted at the cap. |

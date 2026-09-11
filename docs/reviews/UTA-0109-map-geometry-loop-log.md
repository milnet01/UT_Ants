# UTA-0109 — `ubake`: the map's geometry — loop log

The review record for `docs/specs/UTA-0109-map-geometry.md`, kept outside the
spec per `~/.claude/standards/spec-format.md` § 6. `review-contract` writes
one row per loop as it closes. Rows are never back-filled and a landed row
is never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-11 | 3, cold, genre pinned spec | 1 | 1 | 1 | 4 | 7 findings (13 lane reports merged), 6 verified / 1 dismissed, 6 fixed. Q4: INV-5's only portal fixture also carried `PF_Invisible`, so a kept portal was untested (all three lanes); no INV-2 case could reach the 32-bit tiling wrap its own *Breaks when:* names (all three); INV-2 lacked an equal-key case and a no-batches case; INV-8 put no two nodes in one batch. Q2: INV-9 and § 6 checked `iSurf` on a node § 4.3 skips before step 2 (all three). Q3: the vertex guard left the index count to overflow first. Q1, dismissed on materiality: "as every other `f32` in the format is" — `ROOM` validates its `bands`; changes nothing built, and the false clause was deleted under the project's writing rule rather than kept. Two open questions resolved clean: the packet's commit is current, and the driver paraphrase changes nothing built. The whole document is new, so all seven sit inside the gated span. Loop 2 dispatched. |

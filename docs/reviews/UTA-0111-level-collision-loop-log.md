# UTA-0111 — `ubake`: write the level's collision — loop log

The review record for `docs/specs/UTA-0111-level-collision.md`, kept outside
the spec per `~/.claude/standards/spec-format.md` § 6. `review-contract` writes
one row per loop as it closes. Rows are never back-filled and a landed row is
never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-11 | 3 | 1 | 2 | 2 | 2 | 7 verified, 0 dismissed, all fixed. § 4.5 now says which child a point in front of a plane takes, settled by walking every `PlayerStart` both ways, and that outline order says nothing about facing; no outline is reversed under a mirror. INV-7's cases call `buildCollision` directly, and its bake cases break only a coplanar link no earlier step reads. `polyFlags` are read only for nodes with vertices, found from two lanes' open questions. § 6 and INV-7 name the `Model` for `rootOutside`. INV-3, INV-4 and INV-5 build their `Model` in memory; the test writer's `iFront`/`iBack` order is filed as UTA-0122. Of three open questions, one resolved clean, one became a finding, and one was measured into § 4.5's hull sentence. |
| 2 | 2026-09-11 | 3 | 0 | 1 | 1 | 2 | 4 verified, 0 dismissed, 4 fixed. § 4.3's refusals reach the nodes `buildGeometry` skips, since an invisible node can be solid; § 11 narrows UTA-0109 § 4.3's closing sentence to `GEOM`. § 4.5 says a node no walk reaches is not solid and a mesh skips its outline, the user choosing that over a baker refusal; a probe over every map found no unreached node carrying an outline or a hull. INV-5's `Model` gains a hull and a second node as node 0's `front`, and asserts links and outline unchanged under a mirror, which § 7 now mutates. INV-7's bake cases are two bakes, found from lane 3's open question. Of seven open questions, one became that finding, one fed the unreached-node probe, one was answered by INV-5's new fixture, and four resolved clean. At the cap, a spec's being 2, and a calm one: 2 of the 4 (INV-5's and INV-7's test clauses) landed on text loop 1 wrote, the other 2 on the draft. The gated span is the whole document, so all 11 of the run's findings fall inside it. Accepted at the cap. |

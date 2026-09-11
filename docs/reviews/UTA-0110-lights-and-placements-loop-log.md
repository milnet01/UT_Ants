# UTA-0110 — `ubake`: the level's lights and actor placements — loop log

The review record for `docs/specs/UTA-0110-lights-and-placements.md`, kept
outside the spec per `~/.claude/standards/spec-format.md` § 6.
`review-contract` writes one row per loop as it closes. Rows are never
back-filled and a landed row is never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-11 | 3 | 0 | 0 | 5 | 2 | 7 verified, 0 dismissed, all fixed. `ClassSite` gains `end`, and INV-3 and INV-6 tell the two unresolved cases apart; a placement carries its own path; an export path takes the name of whichever package holds it; `missing`'s form is stated per case; `Raw` bytes are copied as read; INV-2 gains three rules and INV-5 a three-level chain. |
| 2 | 2026-09-11 | 3 | 0 | 0 | 1 | 3 | 4 verified, 0 dismissed, all fixed; the cap, so the spec is accepted. A class entry gains `resolved`, so a class whose parent is missing no longer reads as one never found; the bool-byte rules are refused on `read` alone and the value-kind rule on `write` alone; INV-2 gains the cases its rules needed; INV-4's fixture separates an actor's slot from its position. One finding filed as Q2 is counted as Q4, being about test coverage. |

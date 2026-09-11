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
| 3 | 2026-09-11 | 3 | 0 | 1 | 0 | 0 | 1 verified, 0 dismissed, 1 fixed. UTA-0124's amendment, § 4.5 step 1's skip of a repeated actor slot, is consistent everywhere it appears, and INV-4's new case kills both new mutations. § 4.8's record said every mutation § 7 lists was killed, while the amendment added two that have not run; it now speaks of the list on that date. Three open questions resolved clean: the real-asset case counts repeats from `Level::actors` itself; no `BAKER_REVISION` bump, since no map that baked has a repeated slot; INV-4 reads wider than the class-export refusal, as before. A fourth is outside this spec and goes to UTA-0124's build: ut-paths' `sceneOf` walks `Level::actors` too, and would list a repeated exit twice. Unrunnable region stated: what UT does with a repeated slot at run time. |

# UTA-0011 — `ubake` and `ut-bake`: the map baker — loop log

The review record for `docs/specs/UTA-0011-map-baker.md`, kept outside the
spec per `~/.claude/standards/spec-format.md` § 6. `review-contract` writes
one row per loop as it closes. Rows are never back-filled and a landed row
is never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-10 | 3, cold, genre pinned spec | 0 | 2 | 1 | 6 | 9 findings (18 lane reports merged), 9 verified / 0 dismissed, 9 fixed. Q2: an unopenable closure package was 0x01 plus its digest in § 4.4 and absent in § 6 (all three lanes); § 4.8 gave a refused bake a name and path that `Verdict` could not carry. Q3: the material path left out the export's own name. Q4: INV-3's recipe byte could not be varied; INV-2 had no resolver or closure seam; INV-7's stub was not keyed on the fingerprint; `TEXS` ascending order was graded by nothing (all three lanes); INV-15's over-budget case needed over 1 GiB of textures; INV-9's fixture could be skipped for another reason. Four open questions resolved clean, none a finding; one: an invalid box gives an empty lattice, not a refusal. The whole document is new, so all nine sit inside the gated span. Loop 2 dispatched. |
| 2 | 2026-09-10 | 3, cold, identical brief, packet rebuilt from disk | 1 | 3 | 2 | 0 | 6 findings (8 lane reports merged), 6 verified / 0 dismissed, 6 fixed. Q1: § 4.1 named the type libraries, not `uta_umap_build` and `uta_unav_build`. Q2: INV-17's bytewise order contradicted § 4.6's `MapKind` grouping (all three lanes); `--check` tested missing through a resolver that hides a file that does not open; nothing said an empty section is written, and UTA-0008 § 4.4's example says absent. Q3: folded and the stem's bytes were undefined; `BAKER_REVISION`'s comment read narrower than INV-5. Five open questions resolved clean, none a finding. **Cap reached (2 for a spec); the tail is empty, and the spec is accepted.** A calm cap: 1 of this loop's 6 landed on text this run wrote (INV-17's order clause, from loop 1). All 15 of the run's verified findings sit inside the gated span, the whole document being new. |

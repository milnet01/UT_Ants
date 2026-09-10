# UTA-0011 — `ubake` and `ut-bake`: the map baker — loop log

The review record for `docs/specs/UTA-0011-map-baker.md`, kept outside the
spec per `~/.claude/standards/spec-format.md` § 6. `review-contract` writes
one row per loop as it closes. Rows are never back-filled and a landed row
is never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-10 | 3, cold, genre pinned spec | 0 | 2 | 1 | 6 | 9 findings (18 lane reports merged), 9 verified / 0 dismissed, 9 fixed. Q2: an unopenable closure package was 0x01 plus its digest in § 4.4 and absent in § 6 (all three lanes); § 4.8 gave a refused bake a name and path that `Verdict` could not carry. Q3: the material path left out the export's own name. Q4: INV-3's recipe byte could not be varied; INV-2 had no resolver or closure seam; INV-7's stub was not keyed on the fingerprint; `TEXS` ascending order was graded by nothing (all three lanes); INV-15's over-budget case needed over 1 GiB of textures; INV-9's fixture could be skipped for another reason. Four open questions resolved clean, none a finding; one: an invalid box gives an empty lattice, not a refusal. The whole document is new, so all nine sit inside the gated span. Loop 2 dispatched. |

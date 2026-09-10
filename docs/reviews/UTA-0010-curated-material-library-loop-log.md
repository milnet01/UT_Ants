# UTA-0010 — `umat`: the curated material library — loop log

The review record for `docs/specs/UTA-0010-curated-material-library.md`,
kept outside the spec per `~/.claude/standards/spec-format.md` § 6.
`review-contract` writes one row per loop as it closes. Rows are never
back-filled and a landed row is never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-10 | 3 | 1 | 2 | 4 | 1 | 8 findings, 8 verified / 0 dismissed, 8 fixed. Q1: § 4.2 claimed a shared fingerprint and a shared picture imply each other; only one direction holds. Q2: INV-8 would fail on the first `Play` correction § 6 prescribes (all three lanes); `docs/design.md`'s `umat` row describes the library as replacing generation, and § 11 did not list it. Q3: how a recipe's assignment merges; the `Format` loophole in the fingerprint's byte check; which source a picture meeting several seed rules takes; the seed's class and resolve variant. Q4: INV-6's test left two of four fields unexercised. The whole document is new, so all eight sit inside the gated span. Open question resolved clean: the UTA-0106 pairing wording changes nothing built. Loop 2 dispatched. |

# UTA-0009 — `umat`: generate a PBR material from a 1999 texture — loop log

The review record for `docs/specs/UTA-0009-material-from-texture.md`,
kept outside the spec per `~/.claude/standards/spec-format.md` § 6.
`review-contract` writes one row per loop as it closes. Rows are never
back-filled and a landed row is never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-10 | 3 | 2 | 2 | 2 | 3 | 9 findings, 9 verified / 0 dismissed, 9 fixed. Eight merged from the lanes' reports, one (INV-2's 2048² case against the per-test timeout) from an open question, its verdict unrunnable before code. Q1: `FireTexture` and three other classes pass `isModelledTextureClass`; normal strength fell with the upscale factor. Q2: § 15's a = 2 question had no measurement; `sqrt` in first-party code against the Determinism bullet — now integer. Q3: identity lacked the group path; a replacement's alpha was unstated. Q4: INV-3 compared against the raw kernel, which does not sum to 1 at a fractional offset; INV-9's "every map" had no observer. All nine sit inside the gated span (the whole document is new). Loop 2 dispatched. |

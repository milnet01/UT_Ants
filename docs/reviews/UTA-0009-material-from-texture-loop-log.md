# UTA-0009 — `umat`: generate a PBR material from a 1999 texture — loop log

The review record for `docs/specs/UTA-0009-material-from-texture.md`,
kept outside the spec per `~/.claude/standards/spec-format.md` § 6.
`review-contract` writes one row per loop as it closes. Rows are never
back-filled and a landed row is never edited.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-10 | 3 | 2 | 2 | 2 | 3 | 9 findings, 9 verified / 0 dismissed, 9 fixed. Eight merged from the lanes' reports, one (INV-2's 2048² case against the per-test timeout) from an open question, its verdict unrunnable before code. Q1: `FireTexture` and three other classes pass `isModelledTextureClass`; normal strength fell with the upscale factor. Q2: § 15's a = 2 question had no measurement; `sqrt` in first-party code against the Determinism bullet — now integer. Q3: identity lacked the group path; a replacement's alpha was unstated. Q4: INV-3 compared against the raw kernel, which does not sum to 1 at a fractional offset; INV-9's "every map" had no observer. All nine sit inside the gated span (the whole document is new). Loop 2 dispatched. |
| 2 | 2026-09-10 | 3 | 0 | 2 | 1 | 1 | 4 findings, 3 verified / 1 dismissed, 3 fixed. Q2: INV-1 gave masked index-0 texels their palette colour, against § 4.2's fill. Q2 dismissed: a lane estimated INV-3's absorbing tap could exceed one unit; the a = 3 table computed at factors 2 and 4 stays within 0.84. Q3: the joiner inside `<path>` was unstated; pinned to `.`, with an exact-string case in INV-11. Q4: INV-10 demanded equal bytes at levels 0 and 1, which the integer square root breaks by one on correct code (computed); now "within one". Previous-loop share: 1 of 3 verified findings landed on text loop 1 wrote (§ 4.6's `<path>`), a calm cap. Gated-span share: all 12 of the run's verified findings fall inside it. Open questions resolved clean: the palette lookup is `upkg::readProperties`, UTA-0011's; no outer-chain path helper exists; a multi-byte level fails `resolve`'s byte-count check; texture bool flags filed on UTA-0104. Cap reached (2 for a spec); shipped as accepted. The user's decisions of the same day were recorded alongside: still pictures (UTA-0105), PNG replacements (UTA-0106), the AI tool (UTA-0107), a package fingerprint (UTA-0104). |

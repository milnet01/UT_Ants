# Session handoff — 2026-09-07

Written because the terminal was restarted mid-item. It records what is on
disk, what is not, and where to pick up.

## Where UTA-0069 stands

**🚧, and the claim is stale on purpose.** The item was flipped to in-progress
by session `ut-ants-b8`, which no longer exists. Per `CLAUDE.md` § Running two
sessions at once rule 5, work still under way stays 🚧 across a session
boundary; rule 2 says a 🚧 whose named holder is absent from `ListAgents` may
be resumed. Resume it — do not re-file it.

**The spec is drafted, committed and mechanically clean.**
`docs/specs/UTA-0069-model-bsp-tables.md`, committed at `a6053d2`.

**Loop 1 of the review gate HAS run**, at `44d85f0`: three cold lanes,
fourteen verified findings, fourteen fixed, three collateral. The row is in
`docs/reviews/UTA-0069-model-bsp-tables-loop-log.md`.

**Loop 2 is owed and has NOT run.** It was dispatched and then deliberately
stopped, because the user needed to restart the terminal and a loop whose
results arrive after the restart is pure waste. Nothing was folded in from it
and no row was written for it.

**So the resume point is `review-contract` loop 2**, run as
`review-contract docs/specs/UTA-0069-model-bsp-tables.md --genre spec`. **The
cap for a spec is 2, so loop 2 is the last one** — at the cap the run files any
tail and ships, and the spec takes `accepted (DATE)`. Brief loop 2 cold: no
list of what loop 1 fixed, because the cold re-read is what verifies the fixes
held.

The packet is still on disk at `/tmp/review-contract-uta0069/` and may not
survive a reboot; regenerating it is Phase 1b and costs nothing but time.

## What the derivation established

This is the expensive part of the session and it is fully captured in the spec.
§ 2.2 holds the measurements, § 4.4 the derived top-level order, § 4.5 the four
element layouts that are settled, § 4.6 what is still open.

The headline: UTA-0004 § 4.5 recorded the array order as underivable and called
it "the largest single risk in this item". It is now derived. Measured over the
reference install's 837 map packages and 556,452 `Model` exports, 545,652
consume their export exactly under the derived order, and every walked export's
`Polys` reference resolves to a `Polys`-classed export. The community order
yields five nodes for `DM-Deck16][.unr`'s 464,396-byte `Model`; this one yields
1,720.

**The probe that produced those figures is not in the tree**, and does not need
to be — § 4.4 and § 4.5 state every field and width it exercised, so it is
reconstructible directly from the spec. Its shape, for whoever rebuilds it:
compile against the built `libuta_upkg.a` and `libuta_core.a`, walk each
`Model` export from `readPropertyList(...).nativeOffset` with a `ByteReader`,
and count `reader.remaining() == 0` at the end. Two things cost a full
iteration each and are worth not rediscovering: `iLeaf[2]` in `FBspNode` is two
raw `i32` and not compact indices, and `iLightActors` in `FLightMapIndex` *is*
a compact index and not a raw `i32`.

## What loop 1 changed, in one line each

INV-4 stated a 99% floor where UTA-0004 § 7 requires zero refusals of `Model` —
the floor was this session's unfinished derivation written in as a permanent
tolerance, and INV-4 now states the acceptance the ROADMAP already carried.
`Vectors` and `Points` are byte-identical under transposition, so § 10 now
records that nothing checks which is which. Member and element-struct names are
spelled out, because UTA-0007 binds to them. UTA-0004 § 4.5's "second, shorter
run" is superseded — shorter in bytes, longer in count. Tier 1 is owed now
rather than after § 4.6. INV-5 gained a failure condition. And re-measuring the
brush-model size refuted my own correction: 69/70/71 bytes, plus 198 exports at
65 bytes that point at a version branch, now recorded in § 4.6 as a lead.

## Checks already run, so they need not be repeated

- `spec_query` — 5 invariants parse. This is the project trap CLAUDE.md warns
  about (bullet form, or `spec_query` sees zero while the lint still reads
  clean). It is clear.
- `spec_lint` — no findings; `sections_checked: true` against
  `~/.claude/standards/spec-format.md`. `surfaces_checked: false`
  as always on this project, so that result is silent about test surfaces —
  the three test paths were resolved by hand instead and all exist.
- `doc_integrity` — 0 broken links, 0 dead anchors, 0 heading-sequence defects.
- One finding was fixed during drafting: the spec named
  `tests/unit/PackageMalformedContentTest.cpp`, which does not exist. Corrected
  to `tests/unit/PackageMalformedTest.cpp`.

## Two things noticed and deliberately not acted on

**`tests/unit/PackageMalformedContentTest.cpp` is named in sibling specs and
resolves to nothing.** UTA-0004 and others cite it; the real file is
`PackageMalformedTest.cpp`. Not this item's to fix — recorded so it is not
rediscovered as new.

**UTA-0059 is the only open review-sourced item, and its own body defers it**
until the renderer (UTA-0014) lands. Priority rule 1 was checked and is
genuinely clear; that is why UTA-0069 was picked under rule 2.

## Ants MCP feedback

`ANTS-4900` was confirmed fixed on the running binary: `feedback_log` with no
`path` now derives to
`/mnt/Games/Scripts/Linux/Ants_MCP_Feedback_Files/UT_Ants_Ants_MCP_Feedback.md`
rather than stranding a new file at the shared root. **Written up** in the
feedback file, through the derived path with no `path` argument, which is itself
the confirmation. Nothing further owed there. The project memory saying to
always pass `path` explicitly was corrected the same day: it had become false.

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

**The review gate has NOT run.** Three `review-lane` subagents were dispatched
and were lost with the terminal before any returned.
`docs/reviews/UTA-0069-model-bsp-tables-loop-log.md` is correctly still empty —
an empty log means nobody has looked, and that is the truth here. Do not write
a row for the loop that did not finish.

**So the resume point is `write-spec` Step 5:** run
`review-contract docs/specs/UTA-0069-model-bsp-tables.md --genre spec`, from
loop 1. Steps 1–4 are done and need not be repeated.

## What the derivation established

This is the expensive part of the session and it is fully captured in the spec.
§ 2.2 holds the measurements, § 4.4 the derived top-level order, § 4.5 the four
element layouts that are settled, § 4.6 what is still open.

The headline: UTA-0004 § 4.5 recorded the array order as underivable and called
it "the largest single risk in this item". It is now derived. Measured over the
reference install's 838 map packages and 556,452 `Model` exports, 545,652
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

## Checks already run, so they need not be repeated

- `spec_query` — 5 invariants parse. This is the project trap CLAUDE.md warns
  about (bullet form, or `spec_query` sees zero while the lint still reads
  clean). It is clear.
- `spec_lint` — no findings; `sections_checked: true` against
  `~/.claude/standards/spec-format.md`; 411 lines. `surfaces_checked: false`
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
rather than stranding a new file at the shared root. Not yet written up in the
feedback file — that write is still owed.

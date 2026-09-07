# Session handoff — 2026-09-07

Records what is on disk, what is not, and where to pick up. Rewritten late
in the day by session `ut-ants-84`; the earlier version was written after a
mid-item terminal restart and told the reader to go and write `readModel`,
which has since shipped.

## Where UTA-0069 stands

**🚧, and it stays 🚧.** Its acceptance is INV-4 — every `Model` export in
the reference install consumed exactly, no tolerance — and the tier-3 run
is red. Per `CLAUDE.md` § Running two sessions at once rule 5, that is the
correct state to leave it in.

**`readModel` is written, committed and pushed** in
`src/upkg/Geometry.{h,cpp}` at `15cd182`, green on the full matrix (GCC,
Clang, MSVC). It implements § 4.4's table order and § 4.5's nine element
layouts, and § 4.1's rule that `leaves` is neither returned nor stepped
over.

**Tier 1 exists**: six cases across `tests/unit/PackageContentTest.cpp` and
`tests/unit/PackageMalformedTest.cpp` — § 4.7's four, plus a
bytes-left-over case and a populated-`Leaves` refusal. The fixtures
populate every table § 4.5 settles, at distinct counts with a per-index
label.

**Tier 3 exists and is RED**, in `tests/real/RealInstallTest.cpp`. It
tallies rather than asserting per export so the residue prints, and
asserts INV-4 at zero refusals at the end.

## The finding that should change the plan

**Of the 847 packages holding a `Model`, only FOUR have their largest
`Model` parse.**

A map carries hundreds of `Model` exports, one per editor brush, nearly all
empty stubs. The level's BSP geometry — what UTA-0007 and UTA-0011 need —
is the largest one. So the spec's headline figure of 98% exact consumption
is carried almost entirely by trivia, and the content this item exists for
parses almost nowhere.

**That has a methodological consequence for § 4.5**, and it is why the
derivation stalled. Its later layouts were each chosen by maximising exact
consumption across ALL exports — a population dominated by ~550,000 empty
models that never reach those tables. A wrong width in `LightMap`,
`Bounds`, `LeafHulls` or `Lights` barely moves that figure, so the metric
hardly constrains the layouts it was used to settle.

**Derive against the new metric instead**: packages whose largest `Model`
parses, printed by the tier-3 walk. It starts at 4 of 847 and has signal.

**This is recorded on the ROADMAP bullet, not in the spec.** Editing the
spec would re-arm its review gate; do that deliberately, if at all, and not
as a side effect.

## Where the failures are

Tier 3 prints a per-table histogram. Every refusal now names the part it
stopped in — the table, or the prefix, count or reference between tables —
through `Error::withContext`, added at `69805f9`. Unclassified refusals
went from 3,751 to zero.

**The histogram reconciles with § 4.6 table for table.** `Vectors`,
`Points`, `Nodes`, `Surfs` and the trailing `i32` are identical; the rest
differ by +1 to +116 and sum to exactly 180, against a population 4,900
larger that splits 4,720 exact and 180 refused. So the reader is faithful
to the spec's derivation. Do not treat a difference against § 2.2 as a
regression — run the histogram and compare.

## The immediate next action

Work the residue in § 4.6's file order, which that section requires and
gives its reason for. In order:

1. **`Vectors`, 215 failures.** A failure at the first array means the
cursor was already wrong on arrival, so the fault is in the prefix. § 4.6
scopes a version-61 branch at 198 exports in a single package, and 215 is
close to it. Start there — it is the smallest, most bounded class.
2. **`LightMap`, then `LightBits` (1,849) and `Bounds` (5,237).** This is
where the mass begins, and where the § 4.5 method problem above bites
hardest, because these tables are exercised only by the large models the
old metric could not see.

An unverified arithmetic lead on the version-61 class, offered as a
hypothesis and nothing more: a 37-byte prefix (`FBox` 25 + a 12-byte
`FVector` with no sphere radius) plus 28 bytes of empty tables and scalars
sums to the observed 65. § 4.6 records that dropping `FSphere`'s `W` was
measured and changed nothing, so this either is wrong or was measured
against a different second half. Re-measure; do not assume.

## Checks already run, so they need not be repeated

- Gate green at `69805f9`: 172 unit tests, ThreadSanitizer clean.
- The suite is also clean under AddressSanitizer + UndefinedBehaviorSanitizer,
  built per `CLAUDE.md`'s recipe. That is the leg that grades a bounds
  check rather than a wrong answer.
- Seven mutation routes probed against the tier-1 tests, all seven killed:
  `iLeaf`, `LeafHulls` and `LightMapIndex::uClamp` each read as a compact
  index, both INV-2 count guards, INV-1's exact-consumption check, and
  § 4.1's `Leaves` refusal.
- `spec_query` — 5 invariants parse. This is the project trap `CLAUDE.md`
  warns about; it is clear.
- `spec_lint` — no findings, `sections_checked: true`; `surfaces_checked:
  false` as always here, so it is silent about test surfaces.
- `doc_integrity` — clean.

## Do not redo or reopen

- **The review gate.** It reached its cap (2 for a spec) and the spec is
  accepted. Route it to implementation, not to a third loop.
- **`leaves` returned, or given a placeholder element type.** The user
  ruled twice. An empty table is consumed, a populated one refused, and
  there is now a test for the refusal.
- **Reading `UClamp`/`VClamp` as compact indices** — measured and refuted,
  and the mutation probe re-confirms the test catches it.
- **A 37-byte `UPrimitive` prefix for version 61** was measured once and
  changed nothing. The lead above is a different arithmetic, not a licence
  to re-run the same experiment blind.

## Two things noticed and deliberately not acted on

**`tests/unit/PackageMalformedContentTest.cpp` is named in sibling specs and
resolves to nothing.** UTA-0004 and others cite it; the real file is
`PackageMalformedTest.cpp`. Not this item's to fix — recorded so it is not
rediscovered as new.

**UTA-0059 is the only open review-sourced item, and its own body defers it**
until the renderer (UTA-0014) lands. Priority rule 1 was checked and is
genuinely clear.

## Open question for the user

**Milestone scope for v0.1.0.** It holds 39 items, 15 shipped. Nine of the
24 open ones are `urender` work, and they are not equal: `UTA-0014` (Vulkan
bring-up) and `UTA-0016` (load a bundle and walk through it) are what the
milestone means, while eight others are visual polish on a renderer that
does not exist yet — `UTA-0040`, `UTA-0044`, `UTA-0045`, `UTA-0051`,
`UTA-0052`, `UTA-0053`, `UTA-0054`, `UTA-0055`. Moving those eight to
0.2.0 would cut the remaining work by a third without changing what 0.1.0
delivers. **Proposed and not decided** — the user's call, raised
2026-09-07 and awaiting an answer.

## Ants MCP feedback

Two entries in
`/mnt/Games/Scripts/Linux/Ants_MCP_Feedback_Files/UT_Ants_Ants_MCP_Feedback.md`.

`ANTS-4900` was confirmed fixed earlier: `feedback_log` with no `path`
derives correctly.

**New, and it cost real data:** `roadmap_log` op:`annotate` discarded a
note it had itself written minutes earlier, reporting
`discarded_external_edits: true` inside an `ok: true` envelope. Nothing
external had edited the file. The text survived only because an
intervening commit happened to include it; it was re-recorded. Treat a
successful `roadmap_log` write as needing verification — grep the file for
what you just wrote.

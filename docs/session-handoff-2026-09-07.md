# Session handoff — 2026-09-07

Records what is on disk, what is not, and where to pick up. Rewritten by
session `ut-ants-b2`, which resumed the 🚧 left by `ut-ants-b8` /
`ut-ants-4e` and closed § 4.6's version-61 class. The version it replaces
told the reader to start on the `Vectors` failures; there are none left.

## Where UTA-0069 stands

**🚧, and it stays 🚧.** Its acceptance is INV-4 — every `Model` export in
the reference install consumed exactly, no tolerance — and the tier-3 run
is red. Per `CLAUDE.md` § Running two sessions at once rule 5, that is the
correct state to leave it in.

`readModel` is in `src/upkg/Geometry.{h,cpp}`, green on the full matrix. It
implements § 4.4's table order and § 4.5's nine element layouts, refuses a
populated `Leaves` per § 4.1, and now refuses a `Model` below package
version 62 by name.

Tier 1 is in `tests/unit/PackageContentTest.cpp` and
`tests/unit/PackageMalformedTest.cpp`. Tier 3 is
`tests/real/RealInstallTest.cpp`; it tallies rather than asserting per
export so the residue prints, and asserts INV-4 at the end.

## What this session settled

**Version 61 is a different serialisation, not a wrong field width.** A
`Model` there holds no inline BSP tables. It holds six object references —
`Vectors`, `Points` (a second `Vectors`), `BspNodes`, `BspSurfs`, `Verts`,
`Polys` — behind a 37-byte prefix (`FBox`, then a 12-byte vector with no
sphere radius), ahead of eight index-prefixed arrays and the two trailing
`i32`. There is no `NumSharedSides` and no `NumZones`.

Two independent proofs, both in `MH-SPNaliRescue.unr`, the corpus's only
version-61 package. The export-class histogram gives one `BspNodes`, one
`BspSurfs`, one `Verts` and one `Polys` per `Model`, and `Vectors` twice.
And the six indices resolve by name to exactly those exports, on every
export sampled.

That is why the handoff's 37-byte lead "changed nothing" when it was first
measured: the prefix was right and the whole second half was wrong.

**Under the derived layout 223 of the 234 version-61 `Model` exports
consume exactly, against none before.** The last two of the eight arrays
are not determined by this corpus — sweeping both widths to 48, the best
pair beats almost every other pair by one export, which is § 4.6's own
alias warning for `Leaves`. No width is stated for them.

**The user ruled the implementation out of this item.** It is `UTA-0072`,
in the 0.4.0 section, and it carries the derivation. `readModel` refuses
version 61 with `ErrorCode::UnsupportedVersion` instead — the bytes are not
malformed, they are a layout this reader does not describe, which is
§ 4.1's `Leaves` ruling applied to a whole export. The boundary is 62
rather than 63 because that is the smallest claim the measurement
supports: 61 is the only version below 62 the container accepts, and 62
appears nowhere in the reference install.

## The spec is now stale in two places, deliberately

§ 4.6's version-61 paragraph describes the class as unexplained, and its
residue list names `Vectors`, `Points`, `Nodes` and `Surfs` as stop
positions. Measured 2026-09-07 with the refusal in place, those four
buckets are **empty across all 847 packages** — their counts summed to
exactly the version-61 export count, so every refusal at the first four
tables in the whole install was that one class.

Amending § 4.6 re-arms the review gate under `CLAUDE.md` rule 14. It was
left alone rather than edited as a side effect. The `UTA-0069` ROADMAP
bullet carries the current numbers. **Decide the amendment deliberately;
do not fold it into unrelated work.**

## Where the failures are now

Run the walk and read its histogram rather than trusting any figure here:

```sh
cmake -S . -B build-real -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DUTA_REAL_ASSET_TESTS=ON \
  -DUTA_UT_INSTALL_DIR="/mnt/Games/PC Games/UT/UnrealTournament-469"
./build-real/tests/uta_real_asset_tests \
  "every modelled export in the install is consumed exactly"
```

As of 2026-09-07 the mass is `bounds`, then `lightmap bytes`, then `leaf
hulls` and `leaves`, with a smaller class completing at the wrong offset.
Total refusals did not move this session: the same exports refuse, and only
their classification did.

## The immediate next action

**`bounds`, then `lightmap bytes`.** That is where the mass is, and it is
what the level geometry is stuck behind — only four of 847 packages have
their largest `Model` parse.

§ 4.6's file-order rule still governs *within* one walk: a wrong element
width upstream garbles every later count, so fixing out of order attributes
one table's failures to another. It does **not** link version 61 to this
work — that was a disjoint population, one package, and closing it moved
none of these buckets.

**Derive against "packages whose largest `Model` parses", not against
exact consumption.** A map holds hundreds of `Model` exports, one per
editor brush, nearly all empty stubs; the level geometry is the largest
one. § 4.5's later layouts were each chosen by maximising exact
consumption over a population dominated by empty models that never reach
those tables, so that metric hardly constrains the layouts it settled. The
tier-3 walk prints the better metric.

The anchor sweep is the method that made this session's answer a
measurement rather than a guess: walk to the position, sweep the span to a
signature you can recognise — the end of the payload, or a reference that
must resolve to a known class — and accept a span only where the signature
holds. It needs no hypothesis about the layout and self-checks wherever the
answer is known in advance.

## Checks already run, so they need not be repeated

- Local gate green at `cc80cad`: 173 unit tests, ThreadSanitizer clean.
- The tier-1 version refusal was mutated to `if (false && ...)` and the
  test fails, so it is not vacuous.
- Earlier in the item: the suite clean under AddressSanitizer +
  UndefinedBehaviorSanitizer, and seven mutation routes probed against the
  tier-1 tests, all seven killed.
- `spec_query` returns five invariants — the project trap `CLAUDE.md` warns
  about is clear.

## Do not redo or reopen

- **The review gate.** It reached its cap and the spec is accepted. Route
  it to implementation, not to a third loop.
- **`leaves` returned, or given a placeholder element type.** The user
  ruled twice, and a test locks the refusal.
- **`UClamp`/`VClamp` as compact indices** — measured and refuted.
- **The version-61 derivation.** It is done and recorded on `UTA-0072`.
  What is open there is the last two arrays' widths and the four export
  classes, not the layout.
- **A 37-byte `UPrimitive` prefix as a fix for version 61.** The prefix was
  never the problem on its own.

## Noticed and deliberately not acted on

**`tests/unit/PackageMalformedContentTest.cpp` is named in sibling specs
and resolves to nothing.** UTA-0004 and others cite it; the real file is
`PackageMalformedTest.cpp`. Not this item's to fix — recorded so it is not
rediscovered as new.

**`UTA-0059` is the only open review-sourced item, and its own body defers
it** until the renderer lands. Priority rule 1 was checked and is clear.

**The `UTA-0069` bullet's `Evidence:` field holds prose fragments, not
paths.** The field is comma-split, so a sentence written there is stored as
several fragments. Cosmetic; `roadmap_log op:"amend_field"` fixes it.

## Open question for the user

**Milestone scope for v0.1.0.** Eight of its open items are visual polish
on a renderer that does not exist yet — `UTA-0040`, `UTA-0044`, `UTA-0045`,
`UTA-0051`, `UTA-0052`, `UTA-0053`, `UTA-0054`, `UTA-0055`. Moving them to
0.2.0 would cut the remaining work substantially without changing what
0.1.0 delivers. **Proposed and not decided** — raised 2026-09-07 and still
awaiting an answer. Do not act on it unprompted.

## Ants MCP feedback

In `/mnt/Games/Scripts/Linux/Ants_MCP_Feedback_Files/UT_Ants_Ants_MCP_Feedback.md`.

`roadmap_log` op:`annotate` once discarded a note it had itself written,
reporting `discarded_external_edits: true` inside an `ok: true` envelope.
It did **not** recur this session — three writes all reported
`discarded_external_edits: false` and all three verified present. Still
grep for what you wrote before trusting the success; note that the stored
text is hard-wrapped, so a grep phrase spanning a wrap finds nothing and
looks exactly like a lost write.

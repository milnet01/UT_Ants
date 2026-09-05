# Where this project stands — 2026-09-05

Supersedes `session-handoff-2026-09-04b.md`. **Everything here is checkable
from the repository, the roadmap store, git and GitHub Actions; nothing is
the only copy of anything.**

## State

`~/.claude/workflow.md` state **5 — on an item**. `UTA-0005` is 🚧 and its
spec is accepted; no code has been written for it.

Only one session ran today. The `UT_Ants-session-b` worktree is still
parked, built and push-proven, with `docs/session-b-brief-2026-09-05.md`
holding its brief and `UTA-0043` then `UTA-0013` assigned to it. The user
was asked and chose not to start it.

## What this session did

**`UTA-0005`'s spec was written and accepted** —
`docs/specs/UTA-0005-class-tables-and-ancestry.md`, through two
`review-contract` loops: 22 findings, 22 fixed, none deferred. The loop log
is `docs/reviews/UTA-0005-class-tables-and-ancestry-loop-log.md`.

**The scope of the item grew, by the user's decision.** Reaching a class's
default properties requires walking its compiled script instruction by
instruction, because the only length the file stores is the size the script
occupies **in memory**, not on disk. The user was offered three options on
2026-09-05 and chose to build the walker inside `UTA-0005` rather than
split it out, on the ground that every monster inherits from classes that
carry a script. Nothing is executed; the walker computes a length. This is
recorded in the spec's § 3.1 and does not breach `ADR-0004`.

**`scripts/class-census.py` is new** and re-derives every format claim the
spec rests on, over the install `UTA_UT_INSTALL_DIR` names. It exits
non-zero when any class export is not consumed exactly. Last run: 2,595
packages, 16,912 class exports, all consumed exactly; 16,895 reaching a
root; 17 stopping at one absent mod package; deepest chain 12.

## The lesson worth carrying, because it will recur

**A probe that disagrees with the shipped reader is evidence about the
probe.** The draft claimed some community packages carry an out-of-range
parent reference in the export table, and built § 4.5 on it. It was false.
The throwaway probe accumulated the format's compact index without
narrowing to 32 bits, where `ByteReader::readIndex` narrows once at the
end — so a five-byte encoding with bit 31 set read as a large positive
instead of a small negative. Re-measured correctly: **no** class export in
the install has an out-of-range parent in either place.

All three cold lanes caught the *sentence* (it contradicted `UTA-0003`,
which validates both tables at open, so such a package could not open at
all). Running it caught the *fact*. The same bug was in
`scripts/class-census.py` and is fixed there; its numbers did not move,
which is what proves it never reached the ancestry walk.

`UTA-0004` § 2.1's method stands, with this added to it.

## What is NOT done

**No implementation.** `src/upkg/Script.{h,cpp}` and
`src/upkg/Class.{h,cpp}` do not exist. Neither do
`tests/unit/PackageClassTest.cpp`, `PackageScriptTest.cpp` or
`PackageAncestryTest.cpp`. The spec's § 4.1 and § 7 name all of them.

**`Properties` must grow one entry point** — `readPropertiesAt`, taking a
cursor — because both existing entry points start at an export's beginning
and a class's defaults are at its end. That is a change to `UTA-0003`'s
surface and the spec's § 11 records it.

**`UTA-0003` § 4.8 needs a correction** once this item lands: it says a
struct's layout "is only knowable from the class table (UTA-0005)", which
reads as a promise this item does not keep. The spec's § 3.2 records the
measurement behind that decision. Not corrected yet, deliberately —
amending a shipped spec on the strength of a draft is backwards, and the
draft is now accepted, so it can be done.

## Open questions for the user, unchanged and still unaddressed

- `~/.claude/standards/spec-format.md` § 5.4 says no check reports line
  count; `spec_lint` reports `line_count`. Needs a `~/.claude` session.
- The sibling specs keep their review logs **inline** in § 12;
  `spec-format.md` § 6 puts them in `docs/reviews/`, which is what
  `UTA-0005` does. The three are now inconsistent. Somebody should decide
  whether to move `UTA-0003`'s and `UTA-0004`'s.

## Traps this session confirmed or added

- **`spec_lint` reports `surfaces_checked: false` here, always** — already
  in `CLAUDE.md`. Confirmed again. Its `findings: []` is silent about test
  surfaces, and two of this spec's test clauses were defective.
- **`spec_lint` also reports `test_coverage_checked: false` on a
  single-file call** while returning `true` for the same document in a
  directory walk, and its hint blames the document. Filed in
  `../UT_Ants_Ants_MCP_Feedback.md`. Read the flag, then try the directory.
- **`roadmap_log` refuses a note naming a trailer key mid-line**
  (`body_shadowed`) — writing "Lanes: upkg" in a progress note is refused,
  correctly. Reword rather than fighting it.

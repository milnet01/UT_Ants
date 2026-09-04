# `CLAUDE.md` — contract-gate record, 2026-09-04

The loop log for this project's `CLAUDE.md` lives here rather than in the
document. `CLAUDE.md` is read in full by every session on every turn, so a
table appended forever is a permanent context cost carried by sessions
that will never read it.

**Trigger.** A standing priority order was added as § Which item comes
next, and the `In flight:` line was updated. That changes which item a
session picks up, so rule 14's gate was owed.

## Loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-04 | 3, genre pinned `standard` | 1 | 3 | 2 | n/a | **Six verified, six fixed; two dismissed.** Three of the six landed on text added an hour earlier by the change being gated, which is the pattern this gate exists to catch. **All three lanes independently found the same Q1**: `.githooks/pre-push` was described as running `scripts/ci.sh`, and it does not — it delegates to the machine-wide hook, and where that hook is absent it prints `NOTHING WAS CHECKED` and **exits 0**, so a green push was being read as a green gate. **One lane alone found the sharpest Q2**: § Stack said "the gate builds GCC, Clang and MSVC" while § Build and test defines the gate as `ci.sh`, which builds one compiler — and the document already records that this exact confusion shipped an item while Windows was red. The three collateral findings were all on the new section: `Next:` and the priority order each claimed the other's job; the deferral was to be recorded on a bullet in `ROADMAP.md`, which is generated from the store and drops a hand edit silently; and rule 1 named a category — "a review finding" — that nothing on the roadmap distinguishes, so a session could breach the order without being able to tell. **Dismissed:** two lanes reported that `docs/design.md § The stack` does not resolve, one of them specific about why; it **does** resolve (`section_slug: the-stack-and-what-it-rules-out`), verified by a live call here and independently by the third lane, which correctly declined to file it. And `docs/standards/`'s "if that directory is empty" test is unreachable because a README always sits there — true, but nothing anyone does today changes, so it was dismissed as immaterial rather than fixed. **A fix of my own was wrong and step 3 caught it before it landed**: the replacement claimed one config key chose the documentation-only path, and there are two — `docsGlob` decides what counts as documentation, `docsMode` supplies the flag — with the glob's fallback default *wider* than this repository's. |

| 2 | 2026-09-04 | 3, identical brief, packet rebuilt from disk | 2 | 3 | 1 | n/a | **Six verified, six fixed, plus three more found by my own sweep before dispatching loop 3.** **The most consequential finding of the whole gate came from one lane here**, and it is pre-existing rather than collateral: the push gate is driven by *three* `git config` keys, not the one loop 1 named, and `ants.gate.command` is load-bearing — unset, the machine-wide hook falls back to a fixed discovery list (`scripts/local-ci.sh`, `ci-local.sh` and similar) that **does not contain `scripts/ci.sh`**, so a fresh clone takes the no-gate branch and exits 0. Loop 1 had rewritten this very paragraph and missed it. **Two lanes independently found a defect loop 1 introduced**: `In flight:` was set to `nothing` while `State:` stayed 5, and `workflow.md` § 1 defines state 4 as exactly "nothing is in flight" — both lanes verified it against the standard rather than asserting it. The rest were loop 1's collateral: the rewritten gate paragraph had dropped the fact that a documentation-only push runs `ci.sh --docs` at all, so a green docs-only push read as a compiled green; the delegation was stated with `$ANTS_GLOBAL_HOOKS` as the fallback when it is the override, and as a file when it names a directory; and rule 1's handle said `Source:` "names a review", which **excludes two of the three review tokens** in `roadmap-format.md` § 3.5.3 — `audit-<date>` and `debt-sweep-<date>` — the debt and codebase findings rule 1 puts first. **Step 3 caught two errors in this loop's own fixes before they landed**: the claim that `--docs` fires no compiler leg was unverified when written (it is true — `ci.sh` exits at its docs branch before the build), and the citation named a heading that does not exist, the token table being a bold label inside § 3.5.3. **The sweep then found three more**: "the two cannot disagree" left over from a three-line rule, a deferral paragraph still moving one position line when the block above now moves two, and — the one that mattered — the newly-named tokens do not include `review-code-<date>`, which is what this project has actually been writing, so the new rule would have matched none of its own review items. |

## A limitation of this gate, worth knowing before the next one

All three lanes disclosed that the harness injects the project
`CLAUDE.md` into their system context as project instructions. They were
therefore **not cold on this document** — each arrived holding the
pre-change version and could see the diff.

That is inherent to gating a project `CLAUDE.md` with subagents on this
harness, and no briefing removes it: the lanes read a scrubbed copy at a
temporary path, and the harness supplies the original anyway. The lanes
handled it correctly — each said so unprompted and treated the injected
copy as evidence of nothing — but a reader of this record should not
credit the run with a fully cold read.

What survives is still worth having: the injected copy was the *old*
version, so every finding above was made against text the lanes had to
read fresh.

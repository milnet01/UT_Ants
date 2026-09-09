# `CLAUDE.md` — contract-gate record, 2026-09-09

The loop log for this project's `CLAUDE.md` lives here rather than in the
document, for the reason `docs/claude-md-review-2026-09-04.md` gives: the
file is read in full by every session on every turn.

**This is a new run, and the loop numbers continue the earlier records'** —
they are the document's history, not this run's. The 2026-09-05 run reached
its cap; an authoring edit that changes direction re-arms the gate normally,
and this is one.

**Trigger.** `UTA-0084`. Rule 3's start-up test was `git worktree list` and
`ListAgents`, and neither counts this project's sessions. The test was
rewritten to read the holder names rule 1 writes into the 🚧 progress note.
A conformer now runs a different check at start-up, so the gate was owed.

**Gated span.** `CLAUDE.md` § Running two sessions at once, rule 3, as
committed in `6251f85`.

## A limit on how cold this gate can be

Two of the three lanes disclosed, unprompted, that the **live**
`/mnt/Games/Scripts/Linux/UT_Ants/CLAUDE.md` was already in their session
context, injected by the harness as project instructions. The scrubbed copy
and the withheld review history do not change that.

So a lane reviewing this particular document is not cold on its text, and no
briefing can make it so. The withholding still works for the review
*history*, which is what the scrubbed copy exists for. Recorded because a
reader is otherwise entitled to assume the same coldness the gate claims for
a spec.

## Loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|---|---|---|---|---|---|---|---|
| 7 | 2026-09-09 | 3, cold — genre pinned `standard`; packet carried the measured `git config` output, `.githooks/pre-push`, `CMakeLists.txt`'s `UTA_SANITIZE` block, `workflow.md` § 1, `roadmap-format.md` § 3.5.4 and `ci.sh`'s head; MSVC/Windows-runner claims declared an unrunnable region | 3 | 1 | 0 | n/a | **Four verified, four fixed; one dismissed.** **All three lanes independently found the same defect, and it was introduced by the fix that armed this gate**: the new rule 3 claimed `git worktree list` shows whether the main checkout is free. It shows which worktrees exist and reports no occupancy, so a second session would have read one entry as "main is free" and stayed in it — the breach rule 3 opens by naming. Fixed by having rule 1 record the holder's **checkout** beside its session name, which is the only place that fact exists. Two lanes then found a Q2 the author had already tripped over: § Which item comes next enumerated four `Source:` tokens and omitted `review-contract-<date>`, the only one either open review-sourced item carries — a conformer matching the list literally reports rule 1's set **empty** and skips both. The list is now stated as examples with the store query as the operative route. Two further Q1s were the orchestrator's, both found by 4a step 3 executing the claims the fixes *added*: § This file's own review history named the first record alone, leaving later runs unreachable; and a sentence written this loop said § 3.5.3 recognises four `Source:` values when it defines five. **Dismissed:** both lanes that read packet fact 8 concluded `ListAgents` cannot report your own session name. It can — the header line names the caller above the peer list. The packet said "returned two peers" and caused the finding; the defect was the packet's, not the document's. |

| 8 | 2026-09-09 | 3, cold — identical brief; packet rebuilt from disk after the history restructure, extended with the three new history files in full, and fact 8 corrected | 1 | 2 | 2 | n/a | **Five verified, five fixed; none dismissed.** **Two of the five landed on text loop 7 wrote, and all three lanes found both** — the collateral pattern this loop exists to catch. First: *"needs no token enumerated correctly first"* was false. A `source` array of prefixes IS a literal token match, only a cheaper one, so the sentence contradicted the *"tokens are examples"* rule two paragraphs above and reinstated the failure loop 7 had just fixed. Now states what the query cannot do, gives the call verbatim, and names listing every open item as the only complete route. Second, and worse: loop 7 put the occupancy fact on the HELD ITEM's note, but a session between items holds nothing — which is state 4, this project's ordinary state. So `roadmap_query status:"in-progress"` returns empty while somebody sits in the main checkout, and a conformer reads that silence as permission. Rule 3 now says an empty list is not evidence, and defaults to taking a worktree. **Three pre-existing:** rule 5's stated reason for keeping 🚧 across a session boundary contradicted rule 2's abandonment test — the marker records a STATE (half-built) rather than a claim, and rule 5 now says so; `./scripts/mutation-probe.py <lane>` implied any lane works when `SUBJECTS` holds only `ubundle`, which matters because the next item is in another lane (two lanes dropped this as immaterial on argparse `choices`, one kept it — kept, because the RULE has no route outside `ubundle` and the document never said to mutate by hand); and `review-contract-` sat beside a *"file with the nearest one"* fallback that makes `doc-review-` look correct for a rule 14 gate. |

| 9 | 2026-09-09 | 3, cold — identical brief; packet rebuilt from disk after the history restructure and extended with facts settling the three open questions loop 8's lanes could not (`roadmap_query`'s `source` array, `mutation-probe.py`'s single subject, the measured map counts) | 1 | 2 | 1 | n/a | **Four verified, four fixed; none dismissed. CAP REACHED (3 for a standard); the run ships.** Two lanes found the same pointer defect and it is collateral of the history restructure, not of a gate fix: § Where this project is still said *"§ Build and test records a session setting it so while Windows was red"* after that record moved to `docs/build-and-test-lessons.md`, so the in-document pointer led nowhere — the exact failure the restructure's own rule warns about. Worse, and the same class: `docs/claude-md-history.md` asserted *"the measurements … were not moved"* and quoted three, **all three of which are absent from `CLAUDE.md`** — left standing it would have told a maintainer to duplicate the lessons back inline. **The sharpest finding was one lane's and lands on loop 8's own fix:** *"Default to a worktree. Take the main checkout only if you know you are this project's first live session"* is unconditional in practice, because the same rule removes every route to that knowledge — so a SOLE session vacates `main`, and rules 6 and 7 then leave nobody able to merge, commit `ROADMAP.md`, or advance `Next:`. Now conditioned on ruling out a live peer, with the sole-session case named and a route back. One pre-existing Q3: git skips a non-executable hook in silence, a fourth way a green push checks nothing, and the prescribed confirmation read config only — the mode is not config. `.githooks/pre-push` is executable today, so this was latent. |

## The cap — which kind it was

**Calm on the document; oscillating on rule 3.** Both measurements, so a
reader can disagree.

**Share of the final loop landing on text this run wrote: 3 of 4.** Read
alone that is a violent cap. It is not, and the split matters: two of those
three (the dead pointer, the false "were not moved" paragraph) are
collateral of the **history restructure**, a large user-requested change
that landed between loops 8 and 9 — not of the gate repairing its own
repairs. **Gate-fix collateral alone is 1 of 4.**

**Share of the whole run landing inside the gated span** (1c's record:
rule 3, `6251f85`): **3 of 13** verified findings across the three loops.
The other ten were pre-existing defects this run found because every lane
reads the whole document. So the run was mostly audit, and the audit paid.

**But rule 3 was repaired in all three loops, and each repair produced the
next finding** — the start-up test, then the empty-list case, then the
sole-session case. That is a genuine oscillation, confined to one rule, and
it is evidence about the rule rather than about the review: rule 3 is
trying to answer a question no command on this machine supports, which is
what UTA-0084 said in the first place. It now says so itself, and defers to
a judgement instead of pretending to a test.

**Verdict taken: ship.** A fourth loop would land on rule 3 again.

**Size:** `CLAUDE.md` is not oversized for its genre and the cap is not a
size signal here. It grew across the run (343 → 430 lines) while 245 lines
of history moved out to three files.

**The final 4b sweep, run before exiting rather than counted as a loop,
yielded ZERO.** Every prose pointer claiming another document records
something was re-checked, the three history files were checked for further
claims about what `CLAUDE.md` contains, and five moved measurements were
confirmed absent from `CLAUDE.md` rather than duplicated. Reported because
a sweep that found nothing is otherwise the same artefact as one never run.

## What the deterministic pass found

`doc_integrity` returned zero findings before and after the fixes.

`doc_citations` returned a **count of zero citations**, which its own hint
calls silence about this document rather than a clean bill — backticked
spans in it are not in a form that verb recognises. This run did not treat
the document as citation-checked, and no later one should either.

## Phase 1b yield

Every citation in the packet was cut from live source and **none was
defective**: the four `ants.gate.*` and `core.hooksPath` values, the absent
`clang-19`, `UTA_SANITIZE`'s accepted values and MSVC refusal, and
`.githooks/pre-push`'s `${ANTS_GLOBAL_HOOKS:-…}` fallback all matched what
the document claims. Reported because a zero yield is otherwise
indistinguishable from the phase having been skipped.

## Collateral and out-of-scope

`docs/session-b-brief-2026-09-05.md` restates the superseded start-up test
(*"`git worktree list` and `ListAgents` — confirm nobody else is in this
worktree"*). It is a dated session brief, not a live contract; editing it
would destroy what it records. Left as written, recorded here.

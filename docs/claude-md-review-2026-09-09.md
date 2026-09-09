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

# Running two sessions — what was measured

The evidence behind `CLAUDE.md` § Running two sessions at once, and behind
the `In flight:` line in § Where this project is.

**This is history, not instruction. `CLAUDE.md` governs.** Read this when
you want to know whether one of those rules still earns its place.

## `In flight:` stopped being kept by hand

**2026-09-05.** It was a hand-kept line until the project began running two
sessions at once, and one line could no longer name what two sessions held.

The principle that survived: a line that must be right in two places at
once is a line that will be wrong in one of them. The roadmap answers it
instead.

## Neither start-up command counts this project's sessions

**Measured 2026-09-08 and again 2026-09-09.** `ListAgents` is machine-wide.
On 2026-09-08 it returned `ut-monsterhunt-05`, `claude-39`, `pressless-dd`
and `ants-terminal-82`; on 2026-09-09, `ants-terminal-ff` and
`ut-monsterhunt-b9`. None of those worked this project, and `claude-39`
named no project at all. So a count read off it is a count of the machine.

`git worktree list` is project-scoped but answers a different question. A
worktree outlives the session that made it, so its presence proves nothing
about a live holder — and the main checkout is in that list whether or not
a session occupies it. It reports no occupancy at all.

This is the whole content of UTA-0084, and the reason rule 1 now records
the holder's checkout: nothing else on this machine holds that fact.

**`ListAgents` does report the calling session's own name**, on its first
line, above the peer list. Two lanes of the 2026-09-09 gate concluded
otherwise from a packet that described the return as "two peers". It does.

## The roadmap store is keyed to the main checkout

**Measured 2026-09-05.** A `roadmap_query` from the worktree answered
`source: "markdown"` where the same call from the main checkout answered
`source: "store"`, and a dry-run flip reported `write_path: "patch"`.

So a worktree session that calls a roadmap verb with its own path silently
patches that worktree's copy of `ROADMAP.md`, and the next render from the
main checkout overwrites it. The claim on an item would vanish with no
error anywhere.

This is why every roadmap verb passes the main checkout as `caller_cwd`,
and why only the main checkout commits `ROADMAP.md`.

## `.git/config` is shared across worktrees

**Verified 2026-09-05.** `core.hooksPath` and the three `ants.gate.*`
settings apply in a new worktree without being set again.

Worth having measured rather than assumed: a missing gate is silent, so
this is the thing most likely to be taken on trust.

## The push gate does run from a worktree

**Measured 2026-09-05, from the second worktree.** The `pre-push` hook ran
`ci.sh --docs` and named the commit it was gating.

Recorded because the alternative was inferring it from the settings being
present, which the point above shows is not the same thing.

# Brief for the second session (2026-09-05)

Read this first, then `CLAUDE.md` § Running two sessions at once, which is
the rule set. This file is the assignment; that section is the contract.

## Where you work

```sh
cd /mnt/Games/Scripts/Linux/UT_Ants-session-b   # branch: session-b
```

**Not the main checkout.** Git refuses to check one branch out twice, so
you are on `session-b` and the other session keeps `main`. Your build
directory is your own, which is why the two do not fight over `build/`.

The worktree was created, configured, built and tested green on
2026-09-05, and it inherits the push gate: `.git/config` is shared across
worktrees, so `core.hooksPath` and the three `ants.gate.*` settings are
already set for you. Confirm rather than assume —
`git config --get-regexp 'hooksPath|^ants\.gate\.'` — because a missing
gate is silent.

## What you take

**`UTA-0043` first, then `UTA-0013`.**

- **UTA-0043** — decide how dependencies are acquired, on both platforms.
  CMake and documentation.
- **UTA-0013** — the quarantine guard, in `.githooks/pre-push` and in CI.
  `.githooks/` and `.github/`.

Chosen because neither touches `src/upkg/`, which is where the other
session is working, and neither shares a lane with it.

## What you must not touch

- **`src/upkg/`** — the other session owns it (`UTA-0005`, then
  `UTA-0057`).
- **`ROADMAP.md` by hand** — it is generated from the roadmap store and a
  hand edit is dropped without saying so. Write through `roadmap_log`.
- **`CLAUDE.md`'s `Next:` line** — it is a decision, and changing it
  changes what the other session picks up next.

## Before you start anything

1. `roadmap_query status:"in-progress"` — an item already 🚧 is held by
   the other session, whatever the priority order says about it.
2. Flip your item to 🚧 **before** working on it, with a progress note.
   That flip is your claim, and it is the only way the other session
   learns the item is taken. Working first and recording afterwards has
   told nobody.

## When you are done

Run `./scripts/ci.sh`, then **push your own branch** —
`git push origin session-b`. Do not try to merge into `main` from here:
`main` is checked out in the other worktree, and both a checkout and a
push at it are refused.

The session holding `main` merges your branch. The `pre-push` hook gates
your push — measured from this worktree on 2026-09-05, so it is known to
fire rather than assumed — but a local green is one leg of three, and the
matrix (GCC, Clang, MSVC) is what decides whether an item may be flipped
✅.

**Hold one item at a time.** `CLAUDE.md` rule 2: the override buys two
sessions one item each, not two items each.

## What is already done, so you do not redo it

`UTA-0004` shipped on 2026-09-05: `upkg` reads `Polys`, `Palette`, the
`Texture` family and `Sound`. `UTA-0057` carries what it could not — the
`Model` BSP tables and the tail of `Level`, whose layouts have to be
derived by experiment.

# Where this project stands — 2026-09-04

Supersedes `session-handoff-2026-09-03.md`. **Everything here is checkable
from the repository, the roadmap store and git; nothing is the only copy of
anything.**

## State

`~/.claude/workflow.md` state **5 — on an item**. Discovery and design are
agreed and gated. Nothing is in flight.

## What this session did

**`UTA-0041` shipped — the CI pipeline and the local gate.** `scripts/ci.sh`
owns the step list; `.github/workflows/ci.yml` calls it and duplicates none
of it, and `.githooks/pre-push` runs it through the machine-wide hook over
the commits being pushed. The three `ants.gate.*` keys are set in this
repository's git config — **they are local config, not files, so a fresh
clone must set them again**; `scripts/ci.sh --help` and the roadmap bullet
say what they are.

**`scripts/quarantine-guard.sh`** brings forward two of `UTA-0013`'s three
checks. It reads the git index, never the working tree, because the working
tree holds the player's own install under `content/`. Its extension list is
parsed out of `.gitignore` between two markers, so the guard cannot pass what
git was ignoring. `UTA-0013` stays planned: its third check needs the `.utab`
origin field.

**Windows became a first-class target**, at the user's direction, built with
MSVC. `docs/design.md` previously said the opposite. The gate builds GCC,
Clang and MSVC.

**The design gate ran and is logged** in `docs/design-review-2026-09-03.md`
as loop 7 — one loop, at the user's direction. Read that row rather than
re-deriving it.

## The two things that are easy to get wrong

**The roadmap's source of truth is the store, not the file.** Read it with
`roadmap_query`, write it with `roadmap_log`. `ROADMAP.md` is a render and a
hand edit is discarded by the next write. Verified in sync at the end of this
session.

**The Windows build has never actually run.** There is no git remote, so
GitHub Actions has never executed the Windows leg. It is written and
lint-clean and that is all. Do not report it as working. Two ways to make it
real: add a remote and push, or install a toolchain on the Windows box (see
below). The Clang leg *was* proven — the whole gate ran green under Clang on
this machine — though local Clang is newer than the version the job pins.

## Next

`UTA-0002` — core: error type, logging, filesystem, the job system. It
depends on nothing beyond the standard library, so it is not blocked by the
open dependency question.

`UTA-0043` wants settling before the first third-party dependency lands: how
SDL3, glm, shaderc, Assimp and the Vulkan SDK are acquired on both platforms.
The design settles this for Catch2 and Dear ImGui and for nothing else, which
Windows made load-bearing against **S7**.

`UTA-0042` is self-updating, filed at the user's request and modelled on
finbreak. Planned only. Its body carries the part that is expensive to
rediscover: the relaunch step, where that project lost four releases.

## Not done, and deliberately

- **No git remote.** Publishing is outward-facing and nobody has authorised
  it. The quarantine guard now exists, so the guard-before-first-push
  ordering the last session wanted is satisfied.
- **The Windows test machine is reachable but cannot build.** It has no
  compiler, CMake, git or bash — it runs binaries. Its address is
  deliberately not recorded in this repository, which is intended to become
  public; it is in this project's agent memory instead.
- Three ADRs give the map count as 610 where `docs/discovery.md` says 515 of
  612. Reported by the gate, not carried into the design, which cites no
  count. Somebody should settle it once.

## Verifying this file rather than trusting it

```sh
cd /mnt/Games/Scripts/Linux/UT_Ants
./scripts/ci.sh          # the whole gate: guard, docs, lint, build, test
./scripts/ci.sh --docs   # what a documentation-only push runs
git config --get-regexp '^ants\.gate'
```

The suite passes with no Unreal Tournament present. That is **S7**, and it is
still the one claim here worth re-checking first.

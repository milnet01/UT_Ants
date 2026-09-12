# Dependency holds

What this project runs below its latest release, why, and what would end
each hold. The rules are the machine-wide dependency standard § 3, read in place at
the global standards directory; this file is the record it requires, not
a copy of it.

**Floors are not holds and are not listed here** (§ 3 says so outright).
The compiler floors — GCC 14, Clang 19, MSVC 19.40 — are deliberate
reach, owned by [`design.md`](design.md) § The stack. A floor is the oldest thing
supported; a hold is a newer thing declined.

**Every sweep walks this file**, checking each row's broke-at against
what is now released. A row whose broke-at has been overtaken is due for
a retest, and either the hold lifts or the row moves up.

## Held

### `ubuntu-24.04` CI runner image

| Field | Value |
|---|---|
| What | the Linux runner image in `.github/workflows/ci.yml` |
| Held at | `ubuntu-24.04` |
| Broke at | `ubuntu-26.04` |
| What breaks | It is a **preview** image. `actions/runner-images` states that a workflow running on a beta image is outside the GitHub Actions SLA. This image runs the push gate on both Linux compilers, so an unsupported runner is a gate that can stop answering with no recourse. |
| What would release it | `ubuntu-26.04` reaching GA, **and** carrying the `g++-14`, `clang-19` and `libclang-rt-19-dev` packages the matrix installs by name. Both need checking — a GA image that has dropped the floor compilers does not release this hold. |
| Decided | 2026-09-12 |
| Last retested | 2026-09-12 |

### `windows-2022` CI runner image

| Field | Value |
|---|---|
| What | the Windows runner image in `.github/workflows/ci.yml` |
| Held at | `windows-2022` |
| Broke at | `windows-2025` |
| What breaks | `actions/runner-images` carries one Windows Server 2025 row, and `windows-latest`, `windows-2025` and `windows-2025-vs2026` all resolve to the **Visual Studio 2026** image. `docs/design.md` § The stack floors MSVC at 19.40 (VS2022 17.10) and the gate builds all three floors, so moving this leg leaves MSVC 19.40 exercised by no job. A floor no job exercises is not a floor. |
| What would release it | Either a Windows Server 2025 image label that ships VS2022, or this project raising its MSVC floor deliberately — which is a `docs/design.md` decision and an ADR, not a CI edit. |
| Decided | 2026-09-12 |
| Last retested | 2026-09-12 |

## Checked and not held

Recorded so a later sweep does not re-derive them. All checked
2026-09-07 by UTA-0071 and unchanged at 2026-09-12.

- **Catch2** — pinned v3.16.0, which is the latest release.
- **`actions/checkout`** — pinned to the commit for v7.0.1, the latest,
  and the SHA pin is the form `security.md` wants.
- **`cmake_minimum_required` 3.28** — a floor, not a hold.

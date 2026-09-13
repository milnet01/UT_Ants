# Dependency acquisition — UT_Ants

A standard this project owns outright. It says how each dependency is
obtained — fetched, vendored, or found on the platform — and what each route
requires of the build, CI and README. Whoever adds, moves or removes a
dependency complies.

[ADR-0008](../decisions/ADR-0008-acquire-dependencies-by-route.md) owns the
decision and its reasons. Versions, holds and sweeps belong to
`~/.claude/standards/dependencies.md`, and nothing here restates them.

## 1. Principles

- **The route follows from the dependency.** § 2's question decides it, so
  two people routing one dependency reach one answer.
- **A build input is asserted where the build configures.** One observable,
  so CMake, the gate and the README cannot each check something different.
- **What nothing checks is said to be unchecked.** An admitted gap can be
  found by reading; a silent one is found by a user.

## 2. Routing a new dependency

Ask in order. The first yes decides.

1. **Must its version match something already installed** — a driver, a
   kernel ABI, a vendor runtime? **Route 3.** The test is matching, not
   talking to hardware. SDL3 opens input and audio devices and is route 1,
   because any recent SDL3 drives them.
2. **Does a route-3 acquisition supply it, and does this project need no
   particular version of it?** **Route 3**, named with that acquisition.
   `glslc` is the case. glm is not: ADR-0008 pins it, so it falls through.
3. **Does building it need sources it does not ship?** **Route 2**, vendored
   with those sources, and the step that syncs them recorded beside them.
   shaderc would take this branch if nothing supplied `glslc`.
4. **Does its build system produce nothing anyone would link?** **Route 2.**
   The test is what the build produces, not whether one exists. Dear ImGui
   ships no build system; `bc7enc`'s builds only a demo executable.
5. **Is it linked by exactly one target, and is that target not a runtime
   one?** **Route 4.** A test-only dependency stays route 1: Catch2 serves
   the whole suite, and the test build is not shipped.
6. **Otherwise route 1.**

## 3. Route 1 — fetched

- `FetchContent_Declare` with a `GIT_TAG` naming an exact release tag. Never a
  branch.
- Declared where every target linking it can see it. A dependency a `src/`
  target links is declared in the root `CMakeLists.txt`, because `tests/` is
  not configured when `UTA_BUILD_TESTS` is off. glm is the case. A test-only
  dependency is declared in `tests/CMakeLists.txt`, as Catch2 is.
- **Fetching removes a version decision, not a package list.** Where a
  fetched dependency compiles against system development headers, the change
  that adds it names them in `README.md`. SDL3's X11, Wayland and audio
  backends are the case on Linux: without their headers SDL3 builds with
  those backends missing.

## 4. Route 2 — vendored

- The copy lives in `third_party/<name>/`, with the upstream licence file
  beside it.
- `third_party/<name>/README.md` records the upstream repository and the
  exact commit. The copy is its own pin; that file says which copy it is.
- Its sources are compiled into the target that uses it.
- **Nothing reads that record.** Neither `scripts/ci.sh` nor
  `.githooks/pre-push` mentions `third_party`, so whether a copy is stale is
  asked by hand.

`third_party/bc7enc/` is the case, compiled into `uta_umat`.

## 5. Route 3 — found on the platform

**The inputs** are the Vulkan headers, the loader and `glslc`, at the Vulkan
floor `docs/design.md` § The stack sets. Any acquisition supplying all three
qualifies: the LunarG SDK on either platform, or on Linux the distribution's
packages.

**One `find_package` asserts all three.** `src/urender/CMakeLists.txt` calls
`find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc GLOBAL)`.

- **`glslc` is named in `COMPONENTS`.** `FindVulkan` appends it to the
  component list itself, which leaves it optional, so a bare
  `find_package(Vulkan REQUIRED)` configures with no `glslc` present. Named by
  the caller, a missing `glslc` stops configuration. Both directions were
  measured during ADR-0007's review
  (`docs/reviews/ADR-0007-acquire-dependencies-by-route-loop-log.md`).
- **`GLOBAL` makes the imported targets visible outside that directory.**
  `tests/` uses them without a second `find_package`, so the floor lives in
  one place.
- A machine missing any input fails at configure, before anything compiles.

**CI installs; configure asserts.** `.github/workflows/ci.yml` installs the
inputs on each leg: `libvulkan-dev` and `glslc` on Linux, the LunarG SDK on
Windows. `scripts/ci.sh` adds no Vulkan check of its own. Its configure step
is the assertion, and `--docs` exits before that step, so a
documentation-only run needs no Vulkan.

**The validation layers are a development prerequisite, not an input.** The
loader loads them at run time. `FindVulkan` searches for a layer library only
under `IOS`, so no configure result reports them on Linux or Windows.
`README.md` names them. Nothing checks them.

**The build never acquires a graphics driver.** It is the machine's. CI's
Linux legs install Mesa's CPU driver so the device tier has a device to draw
on; that is provisioning a test machine, not acquiring a build input.

## 6. Route 4 — fetched for one target

The `FetchContent` is guarded by whether its one target is in the build. A
build without that target neither fetches nor compiles the dependency.
Assimp for `ut-ed` is the case. The guarding option's name and default arrive
with `ut-ed`.

## 7. Recording the route

A dependency gets a row in `docs/design.md` § The stack's table in the change
that adds it. The row's acquisition column names its route.

## 8. Anti-patterns

- ❌ Routing by what one platform happens to package.
- ❌ A `GIT_TAG` naming a branch.
- ❌ A vendored copy with no `README.md` naming its repository and commit.
- ❌ `find_package(Vulkan REQUIRED)` without `COMPONENTS glslc`, which
  configures with no shader compiler.
- ❌ A second `find_package(Vulkan)` in another directory — a second place
  the floor can drift.
- ❌ A Vulkan probe step in `scripts/ci.sh`, which `--docs` would then have to
  skip separately.
- ❌ A fetched dependency whose system headers `README.md` does not name.
- ❌ A dependency with no row in `docs/design.md` § The stack.

## What checks this

| Rule | What catches a breach |
|------|----------------------|
| § 2 routing | **nothing mechanical** — review of the change that adds the dependency |
| § 3 exact tag | **nothing mechanical** |
| § 3 system headers named | **nothing** — a clone lacking them still builds, with backends missing |
| § 4 provenance record | **nothing** — neither gate script reads `third_party` |
| § 5 inputs present | configure, in `scripts/ci.sh`'s configure step, on every leg |
| § 5 `glslc` named, one `find_package` | **nothing** — removing either still configures on a machine that has `glslc` |
| § 5 validation layers | **nothing** |
| § 7 design.md row | **nothing** |

## Cold-eyes loop log

Rows live in
[`docs/reviews/dependency-acquisition-loop-log.md`](../reviews/dependency-acquisition-loop-log.md).

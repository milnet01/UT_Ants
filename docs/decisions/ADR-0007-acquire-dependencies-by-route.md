# ADR-0007: Acquire each dependency by the route its own nature dictates, not by one mechanism for all

- **Status:** Accepted
- **Date:** 2026-09-06

## Context

`docs/design.md` § The stack names this project's libraries, and its own prose
sends a dependency that is not in that table here. Before
this decision it settled how two of them were obtained — Catch2 fetched by the
build, Dear ImGui vendored — and SDL3, glm, shaderc, Assimp and the Vulkan SDK
had no stated answer. The acquisition column that section now carries is an
index into this ADR, added by this change.

That gap was tolerable while Linux was the primary target, because a distro
package was the unspoken default. Windows is now first-class and has no such
default, so the gap became load-bearing. **S7** is what makes it urgent —
*"a stranger clones the public repository with no Unreal Tournament on their
machine, and the build and the test suite both pass"* — a sign this project
must satisfy at `0.1.0`, which has not been cut, and now on two platforms
rather than one.

The three candidate mechanisms are not equally good per dependency, and two of
them fail on facts rather than on taste. Measured 2026-09-06, against an
`ubuntu:24.04` container and each project's upstream releases:

| Dependency | On `ubuntu-24.04` | Upstream | On `windows-2022` |
|---|---|---|---|
| SDL3 | **absent** — SDL2 only | `release-3.4.16` | absent |
| glm | `0.9.9.8` | `1.0.3` | absent |
| Assimp | `5.3.1` | `v6.0.5` | absent |
| Vulkan loader, headers, layers | `1.3.275` | — | **absent** |
| `glslc` / shaderc | `2023.8` | tags only, no releases | absent |

The Windows runner preinstalls vcpkg, CMake and Ninja, and **no Vulkan SDK**.

Two further facts decided as much as the table did. **shaderc cannot be
fetched on its own**: its `third_party/CMakeLists.txt` fails with
`SPIRV-Tools was not found - required for compilation` unless glslang,
SPIRV-Tools and SPIRV-Headers are synced beside it by its own
`utils/git-sync-deps`, so building it from source is a four-repository
lockstep pin — for something this design uses as a build-time *tool* rather
than as a library to link. And **the Vulkan components come as a set the
platform already publishes** — headers, loader, validation layers and
`glslc` — where fetching them means four pins that must agree, against an
installed graphics driver this project never supplies and `docs/design.md`
already requires.

## Decision

Four routes. Each dependency takes the one its nature dictates, and the
question a new dependency is asked is written down so the answer is mechanical
rather than re-argued.

**Route 1 — fetched by the build, pinned to an exact tag.** Catch2, glm, SDL3.
Each ships a CMake build that stands alone, and each is ordinary code the
compiler can build from source: SDL3 *calls* the platform's window, input and
audio interfaces, but nothing about it has to match a version already
installed. Fetching puts both platforms on one version, which is what a
route-1 dependency buys.

**It does not follow that a fetched dependency needs no system packages, and
SDL3 is the case that proves it.** SDL's own `docs/README-linux.md` lists the
development headers its X11, Wayland and audio backends are compiled against;
without them SDL3 still builds and those backends are simply absent. So route 1
removes a *version* decision, not a package list, and the Consequences below
are where the README's obligation is set.

**Route 2 — vendored in the repository.** Dear ImGui, as already decided. It
ships no build system; its sources are compiled into the target that uses it,
so fetching would buy nothing a copy does not already give. `bc7enc` takes the
same route on the same ground, per
`docs/specs/UTA-0052-texture-memory-budget.md` § 3 decision 4: its build system
builds a demo executable, not a library.

**A vendored copy records its upstream repository and its exact commit beside
the sources**, so the copy is its own pin and staleness is a question somebody
can ask of it. Route 1 states the same obligation as an exact tag, and a copy
with no provenance is one nobody can tell is behind.

**Route 3 — required from the platform, found and never fetched.** Three build
inputs: the Vulkan **headers**, a **loader**, and a **GLSL-to-SPIR-V
compiler**, which is `glslc`. Route 3 is satisfied by any acquisition supplying
all three at Vulkan **1.3 or newer**, the level `docs/design.md` requires — the
LunarG SDK on either platform, or on Linux the distribution's own packages,
Ubuntu 24.04 carrying `libvulkan-dev` at `1.3.275.0` and `glslc` separately at
`2023.8`.

**The validation layers are a development prerequisite of this route and are
deliberately not one of the three.** They are loaded by the loader at run time
rather than linked, and CMake's `FindVulkan` has no result that reports them,
so a gate cannot assert them the way it asserts the other three. The README
names them; nothing checks them, and this paragraph is where that is admitted
rather than left for someone to discover from a gate that passes.

**The graphics driver is not part of this and is never acquired.** It is the
machine's, and `docs/design.md` already rules out one below Vulkan 1.3.

**A floor rather than an exact version, deliberately — and the reason is worth
stating, because the opposite is easy to argue.** Nothing route 3 supplies ends
up inside a bundle. `docs/design.md` § The parts lists what a map
bundle carries — geometry, materials, collision, lights, baked indirect light,
entity placements and the graphs — and compiled shaders are not among them;
they are built into the engine binary. So `ADR-0002`'s requirement that one
map, recipe and baker version hash to one bundle on any machine does not reach
`glslc`, and two contributors on two SDK versions produce the same bundles. The
glm pin below is not the same case and is not weakened by this one: glm is
arithmetic the baker *runs*, and its results do go into the bundle.

**Route 4 — fetched, but only for the target that needs it.** Assimp, which
`ut-ed` links and no runtime target may — `docs/design.md` § The stack says so
in the entry itself, rule 2 naming only this project's own parts. **What
selects it is whether the editor target is in the build**, so the fetch is
guarded by that and by nothing new: a build configured without `ut-ed` does not
pay to fetch or compile Assimp. The option's name and default arrive with
`ut-ed`, and are its to choose.

**The question a new dependency is asked, in this order.**

1. **Must its version match something already installed on this machine** — a
   graphics driver's ICD, a kernel ABI, a vendor runtime? Route 3. The test is
   the matching, not whether the library talks to hardware: SDL3 opens input
   and audio devices and is still route 1, because any recent SDL3 drives them.
2. **Does a route-3 acquisition supply it?** Route 3, named alongside the rest
   of that acquisition rather than given one of its own. `glslc` is the case,
   and it is the case in both spellings: the LunarG SDK ships it, and on Linux
   the distribution ships it as a separate package installed beside the loader.
   *Supplied by* is not *free* — on the distribution route it still has to be
   named in the install line.
3. **Does building it need sources it does not ship?** Route 2 — vendored with
   the sibling sources it needs, and the sync step recorded beside them. This
   is the branch `shaderc` would take if nothing already supplied `glslc`, and
   it is worded about the sources rather than about a build system because
   `shaderc` ships a perfectly good CMake build and still cannot stand alone.
4. **Does its build system produce nothing anyone would link?** Route 2 — its
   sources are compiled into the target that uses it. The test is what the
   build system *produces*, not whether one exists. Dear ImGui ships none and
   passes trivially; `bc7enc` ships a root `CMakeLists.txt` that builds a demo
   executable from `test.cpp` and a bundled `lodepng`, and no library, so it
   passes too. Worded as *ships no build system* the question answered no for
   `bc7enc` and fell through to question 5 — route 4, a guarded fetch of
   sources that are compiled into the target regardless.
5. **Is it linked by exactly one target, and that target not a runtime one?**
   Route 4. A test-only dependency is the exception and stays route 1: Catch2
   is fetched for the whole suite rather than for one target, and the test
   build is not a shipped artefact whose weight anyone carries.
6. **Otherwise route 1.**

Questions 2 and 3 are what stop the question answering *route 1* for `glslc`
and for `shaderc`, which this document has just shown cannot be fetched.

**A dependency routed here is added to `docs/design.md`'s acquisition column**,
which is that table's index into this ADR. `bc7enc` is the open case: it is
routed above and has no row yet, and `docs/specs/UTA-0052-texture-memory-budget.md`
is what brings one.

**glm is fetched rather than found even though a package exists**, and the
reason is not its age. `ADR-0002` requires one map, recipe and baker version to
hash to one bundle on any machine, and `docs/specs/UTA-0049-numeric-contract.md`
is what holds the arithmetic still. A maths library taken from whatever the
machine happens to carry puts a version difference inside that contract, where
two contributors bake one map and get two bundles. Pinning it is part of
determinism, not tidiness.

**vcpkg is rejected on cost, not on capability**, and the distinction matters
because the capability argument is the tempting one and it is false. vcpkg
carries real `vulkan-loader`, `vulkan-headers`, `shaderc` and `glslang` ports
that build from source; only its `vulkan` metaport is find-only, and citing
that alone would misrepresent what the tool can do. It was the strongest
alternative — already on the Windows runner, and the conventional Windows
answer. What it costs is a tool and a manifest in every contributor's loop, and
a first build that compiles the whole set from source, to replace one SDK
installer on Windows and one `apt` line on Linux. That trade is not worth
taking here, and it is worth re-opening the moment a second dependency needs
the same treatment.

## Consequences

**The Vulkan components are a prerequisite on both platforms — the LunarG SDK
on Windows, that or the distribution's packages on Linux — and on Linux SDL3's
X11, Wayland and audio development headers are one too, for the reason route 1
gives. The README must name both before `0.1.0` ships.** This is the cost, and it is real —
but it does not change **S7**, whose subject is a machine with no Unreal
Tournament on it rather than a machine with no toolchain. The SDK step sits
outside that sign and is a README obligation, which is what
`docs/design.md` § The stack says. It is defensible only because that document
already rules out any machine without Vulkan 1.3 — a contributor who cannot
install the SDK could not have run the result. It is not defensible if it stays
undocumented, and a fresh clone is what settles that, not a reading of this
file.

**When the renderer lands, `ci.yml` will install the Vulkan components and
`scripts/ci.sh` will assert they are there.** Neither does today. Provisioning
a toolchain is the runner's job and stays in the
workflow, as the compiler lines there already do. What the shared gate script
owns is the *assertion*, and it reads what CMake reads: `find_package(Vulkan)`
resolves the headers and loader and reports `Vulkan_VERSION`, and locates
`glslc` as `Vulkan::glslc`.

**`glslc` has to be asserted by name.** `FindVulkan` appends `glslc` to the
component list itself rather than the caller requesting it, so
`find_package_handle_standard_args` never treats it as required and a bare
`find_package(Vulkan REQUIRED)` succeeds on a machine with no `glslc` at all —
its `REQUIRED_VARS` are the loader and the headers and nothing else. Measured
against the CMake this machine carries, 4.4.3. So the gate names
`COMPONENTS glslc`, or reads `Vulkan_glslc_FOUND`, and fails when the headers,
the loader or `glslc` is absent or the version is below the floor.

That is one observable for the three build inputs
rather than a choice between `$VULKAN_SDK` and a system path, so the gate,
CMake and the README cannot each pick a different one. **It asserts three of
route 3's components and not the validation layers**, for the reason that route
gives: no CMake result reports them on the platforms this project builds.
**The assertion sits in the gate's configure step**, which `--docs` exits
before reaching, so a documentation-only push from a machine with no Vulkan SDK
still passes its own gate — which is what that mode exists for. The local gate and the pipeline share the script so
neither can drift (`local-gate.md` § 3), and a developer whose machine is short
a component finds out from their own gate rather than from a red pipeline.

**The first configure needs the network, and an offline clone cannot
configure.** That is already true of Catch2 and this widens it to SDL3, glm and
Assimp. A build directory once configured does not need the network again.

**A cold build gets slower**, because SDL3 and Assimp are compiled from source
rather than linked from a package. `ccache` is already wired and optional, and
its two settings matter more after this than before.

**Every fetched dependency pins an exact tag, never a branch or a moving
release.** Staleness is then a question somebody asks deliberately —
`check-dependencies` owns it — rather than a build that changes underneath the
project without a commit saying so.

**What is closed off:** a contributor cannot substitute their distro's SDL3 or
glm for the fetched one without editing the build, and that is deliberate for
glm, where the substitution would break `ADR-0002`.

**What has to be true, and is not yet:** none of these dependencies has landed
except Catch2, which `tests/CMakeLists.txt` already fetches at an exact tag and
which therefore already conforms to route 1. Dear ImGui's route was settled
before this decision and it has not landed either — the repository has no
vendored copy of it. This ADR is the rule the rest follow; the CMake and CI
machinery is written when the first one arrives with the renderer, and the
route table above is what that work conforms to.

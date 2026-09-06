# ADR-0007: Acquire each dependency by the route its own nature dictates, not by one mechanism for all

- **Status:** Accepted
- **Date:** 2026-09-06

## Context

`docs/design.md` § The stack names every library this project will use and
settles how two of them are obtained: Catch2 is fetched by the build, Dear
ImGui is vendored. SDL3, glm, shaderc, Assimp and the Vulkan SDK have no
stated answer.

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
than as a library to link. And **the Vulkan SDK is one acquisition carrying four
things** — headers, loader, validation layers and `glslc` — where fetching
them means four pins that must agree, against an installed graphics driver
this project never supplies and `docs/design.md` already requires.

## Decision

Four routes. Each dependency takes the one its nature dictates, and the
question a new dependency is asked is written down so the answer is mechanical
rather than re-argued.

**Route 1 — fetched by the build, pinned to an exact tag.** Catch2, glm, SDL3.
Each ships a CMake build that stands alone, and each is ordinary code the
compiler can build from source: SDL3 *calls* the platform's window, input and
audio interfaces, but nothing about it has to match a version already
installed. Fetching puts both platforms on one version with no instructions to
follow, which is the shortest route to **S7**.

**Route 2 — vendored in the repository.** Dear ImGui, as already decided. It
ships no build system; its sources are compiled into the target that uses it,
so fetching would buy nothing a copy does not already give.

**Route 3 — required from the platform, found and never fetched.** The **LunarG
Vulkan SDK, 1.3.275 or newer, on both platforms**: the headers, the loader, the
validation layers and the `glslc` this project compiles its shaders with. The
floor is the version Ubuntu 24.04 packages, so it is not set above the older of
the two platforms this project builds on. The graphics *driver* is the
machine's, is never acquired, and `docs/design.md` already rules out a machine
whose driver is below Vulkan 1.3.

**Naming LunarG rather than "the Vulkan SDK" is the whole point of this
paragraph**, because on Linux that phrase is not one thing: the loader, the
layers and `glslc` are three separate distro packages, and Ubuntu's `glslc` is
`2023.8` against the SDK's own much later build. `glslc` output is SPIR-V, and
SPIR-V goes into a bundle whose hash `ADR-0002` requires to be equal across
machines — so two compilers is the same defect as two glm versions, one step
further down the pipeline. One acquisition, one version, both platforms.

**Route 4 — fetched, but only for the target that needs it.** Assimp, which
`ut-ed` links and no runtime target may — `docs/design.md` § The stack says so
in the entry itself, rule 2 naming only this project's own parts. A
runtime-only build does not pay to fetch or compile it.

**The question a new dependency is asked, in this order.**

1. **Must its version match something already installed on this machine** — a
   graphics driver's ICD, a kernel ABI, a vendor runtime? Route 3. The test is
   the matching, not whether the library talks to hardware: SDL3 opens input
   and audio devices and is still route 1, because any recent SDL3 drives them.
2. **Is it already carried by a route-3 acquisition, or unbuildable on its
   own?** Route 3, on that acquisition's back. `glslc` is both.
3. **Does it ship no build system of its own?** Route 2.
4. **Is it linked by exactly one target, and that target not a runtime one?**
   Route 4. A test-only dependency is the exception and stays route 1: Catch2
   is fetched for the whole suite rather than for one target, and the test
   build is not a shipped artefact whose weight anyone carries.
5. **Otherwise route 1.**

Question 2 is what stops the question answering *route 1* for `glslc`, which
this document has just shown cannot be fetched.

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

**Installing the Vulkan SDK is a prerequisite on both platforms, and the
README must say so before `0.1.0` ships.** This is the cost, and it is real —
but it does not change **S7**, whose subject is a machine with no Unreal
Tournament on it rather than a machine with no toolchain. The SDK step sits
outside that sign and is a README obligation, which is what
`docs/design.md` § The stack says. It is defensible only because that document
already rules out any machine without Vulkan 1.3 — a contributor who cannot
install the SDK could not have run the result. It is not defensible if it stays
undocumented, and a fresh clone is what settles that, not a reading of this
file.

**`ci.yml` installs the SDK; `scripts/ci.sh` checks which one it found.**
Provisioning a toolchain is the runner's job and stays in the workflow, as the
compiler lines there already do. What the shared gate script owns is the
*assertion* — that an SDK is present and that it satisfies the floor above —
because the local gate and the pipeline share that script so neither can drift
(`local-gate.md` § 3), and a developer whose machine has a different SDK must
find out from their own gate rather than from a red pipeline.

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

**What has to be true, and is not yet:** none of these dependencies has landed.
This ADR is the rule their arrival follows; the CMake and CI machinery is
written when the first one arrives with the renderer, and the route table above
is what that work conforms to.

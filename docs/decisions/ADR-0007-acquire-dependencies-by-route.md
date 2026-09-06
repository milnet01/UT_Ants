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
default, so the gap became load-bearing. **S7** is what makes it urgent: a
stranger clones the repository and the build and the suite both pass — cut at
`0.1.0`, and now true on two platforms or not at all.

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
than as a library to link. And **the Vulkan loader is a system component**: it
dispatches into the installed graphics driver, so a copy the build fetched for
itself is wrong by construction rather than merely redundant.

## Decision

Four routes. Each dependency takes the one its nature dictates, and the
question a new dependency is asked is written down so the answer is mechanical
rather than re-argued.

**Route 1 — fetched by the build, pinned to an exact tag.** Catch2, glm, SDL3.
Each ships a CMake build, is self-contained, and talks to no system component.
Fetching puts both platforms on one version with no instructions to follow,
which is the shortest route to **S7**.

**Route 2 — vendored in the repository.** Dear ImGui, as already decided. It
ships no build system; its sources are compiled into the target that uses it,
so fetching would buy nothing a copy does not already give.

**Route 3 — required from the platform, found and never fetched.** The Vulkan
SDK: the loader, the headers, the validation layers, and the `glslc` this
project compiles its shaders with. The loader must match the installed driver,
and the SDK carries `glslc`, so one acquisition settles both and the four-repo
shaderc pin is never taken.

**Route 4 — fetched, but only for the target that needs it.** Assimp, which
`ut-ed` links and no runtime target may — `docs/design.md` § The stack says so
in the entry itself, rule 2 naming only this project's own parts. A
runtime-only build does not pay to fetch or compile it.

**The question a new dependency is asked, in this order.** Does it contain or
dispatch into a system component — a driver, a kernel interface, a device? Then
route 3. Does it ship no build system of its own? Route 2. Is it needed by one
non-runtime target only? Route 4. Otherwise route 1.

**glm is fetched rather than found even though a package exists**, and the
reason is not its age. `ADR-0002` requires one map, recipe and baker version to
hash to one bundle on any machine, and `docs/specs/UTA-0049-numeric-contract.md`
is what holds the arithmetic still. A maths library taken from whatever the
machine happens to carry puts a version difference inside that contract, where
two contributors bake one map and get two bundles. Pinning it is part of
determinism, not tidiness.

**vcpkg is rejected**, having been the strongest alternative: it is already on
the Windows runner and it is the conventional Windows answer. It adds a tool to
every contributor's loop, and its Vulkan port is find-only — it expects an
installed SDK rather than providing one — so the single manual step it would
have justified itself by removing remains either way.

## Consequences

**Installing the Vulkan SDK is a prerequisite on both platforms, and the
README must say so before `0.1.0` ships.** This is the cost, and it is real:
**S7** now reads "clone, install one SDK, build" rather than "clone, build".
It is defensible only because `docs/design.md` already rules out any machine
without Vulkan 1.3 — a contributor who cannot install the SDK could not have
run the result. It is not defensible if it stays undocumented, and a fresh
clone is what settles that, not a reading of this file.

**CI must acquire the SDK on both runners, and not only in `ci.yml`.** The
local gate and the pipeline share `scripts/ci.sh` so neither can drift
(`local-gate.md` § 3), so an acquisition step that exists only in the workflow
would be a check a developer's machine never performs. Provisioning is the
runner's job and stays in `ci.yml`; what must not diverge is which SDK the
build then finds.

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

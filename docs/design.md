# UT_Ants — Design

> **Purpose — so the shape is decided once, and anyone can tell where a
> new piece of work belongs and what it is allowed to touch.**

**This document is a gate.** Work is not broken into items until it is
agreed — `~/.claude/workflow.md` § 2. It passes when someone can take any
item off the queue and say which part it belongs in and what it may
touch.

**Status:** drafted 2026-09-03, awaiting the `review-contract` gate.

What this is for is [docs/discovery.md](discovery.md); the signs of
success cited below as **S1**–**S8** live there.

## The parts

Three groups. The split between the first two is load-bearing and is
enforced, not merely described — see *What may depend on what*.

### Build-time only — never linked into the game

| Part | Responsible for |
|---|---|
| `upkg` | Reading Unreal Engine 1 packages: `.unr`, `.utx`, `.uax`, `.umx`, `.u`. Names, imports, exports, object serialisation, class tables and default properties. Data in, structures out |
| `umat` | Turning a 1999 texture into a PBR material — resolving the curated library first, generating base colour, normal, roughness, metallic, height and emissive otherwise |
| `ubake` | The map baker. Drives `upkg`, `umat` and the graph builders, and writes one map bundle |

### The bundle — the seam between the two halves

| Part | Responsible for |
|---|---|
| `ubundle` | The map bundle format, read and write. Geometry, materials, collision, lights, baked indirect light, entity placements, and the two graphs. The only content vocabulary the runtime knows |
| `unav` | The two graphs a bot reasons over — *where you can go*, and *what opens what* — plus the queries over them. Built at bake time, read at runtime |

### Runtime

| Part | Responsible for |
|---|---|
| `core` | Types, math, memory, error type, logging, filesystem, job system. Depends on nothing |
| `uinput` | Devices to actions. Keyboard, mouse and gamepad are three sources of one action set, with per-device bindings, dead zones and response curves |
| `uworld` | The simulation. Entities, collision, movement, physics, the fixed tick. Knows how a body moves; knows nothing about scoring |
| `urender` | Vulkan. Draws a bundle: dynamic lights and shadows, PBR materials, volumetrics, light shafts, ambient occlusion, post-processing |
| `uaudio` | Sound playback, positional mixing, music |
| `unet` | Transport, replication, and content transfer against a fingerprint manifest |
| `uai` | Bots. Navigation, combat, and the planner that gets them through door puzzles |
| `ugame` | The rules. Deathmatch, Team Deathmatch, Monster Hunt, weapons, monsters, pickups, mutators, chat, map voting. The only part that knows what a frag is |
| `uui` | Menus, HUD, scoreboard, map browser, settings, and the weapon wheel |
| `ued` | The editor. Maps, enemies, player characters, and the packaging that makes them downloadable |

### Programs

`ut-ants` (client), `ut-ants-server` (dedicated, headless), `ut-bake`
(convert a map), `ut-dump` (inspect a package), `ut-ed` (editor).

## What may depend on what

Thirteen rules. The first three are what make ADR-0002 and ADR-0003 true
in code rather than in prose; rules 4, 9 and 12 are what make **S2**
reachable.

1. **`core` depends on nothing** beyond the C++ standard library.
2. **No runtime target links `upkg`, `umat` or `ubake`.** The game cannot
   read a `.unr` file even by accident, because the code to do so is not
   in it. A test asserts the link closure of `ut-ants` and
   `ut-ants-server` contains none of the three.
3. **`ubundle` is the only content vocabulary shared across the seam.**
   The baker writes it; the runtime and the editor read it. A change to
   what a bundle contains is a change to this one part.
4. **`uworld` must not depend on `urender`, `uaudio` or `uui`.** The
   dedicated server links no Vulkan and opens no audio device. This is
   the rule that keeps the simulation pure, and it is checked the same
   way rule 2 is.
5. **`urender` reads `uworld`; it never writes to it.** Rendering has no
   opinions about where anything is.
6. **`uai` depends on `uworld` and `unav`, never on `urender`.** A bot
   may not know anything the server does not simulate — which is also
   what stops bots quietly cheating.
7. **`unet` does not include game headers.** It moves bytes described by
   a schema that `ugame` owns. Swapping the transport must not touch
   game code, and adding a weapon must not touch transport code.
8. **`ugame` is the only home for rules.** If a constant decides who
   wins, it lives here.
9. **Client and server compile `uworld` and `ugame` from the same
   sources with the same flags.** No `#ifdef CLIENT` inside either.
   Prediction is only correct when both sides run the same simulation,
   and a divergence introduced by a conditional is invisible until it
   costs somebody a match.
10. **`ued` may depend on anything.** It is the only part allowed to.
11. **Nothing may depend on `ued`.**
12. **`uworld` must not depend on `uinput`.** A tick consumes an input
    *command* — a small fixed struct of movement axes, view angles and
    button bits — and has no idea which device produced it. This is what
    lets a gamepad, a keyboard and a replayed demo drive the same
    simulation, and it is what keeps rule 9's prediction correct when the
    two ends are on different hardware.
13. **`ugame` owns what is in the weapon wheel; `uui` owns how it is
    drawn.** The wheel's contents are the player's inventory and the
    server's rules about it, so the rules half stays in `ugame` like
    every other rule.

## What every part does the same way

- **Errors.** `std::expected<T, Error>` across every module boundary.
  Exceptions may be used inside a part and must never escape one. Only a
  program in `Programs` above may terminate the process.
- **Logging.** One logger, in `core`, with a category per part and
  levels. No `printf`, no `std::cout` outside a program's own startup.
- **Configuration.** One TOML file per user, one per server. Any setting
  that changes gameplay is replicated to clients on join, so both sides
  simulate under the same numbers.
- **Content addressing.** A bundle is named by the hash of its source
  map, its recipe and the baker version. That triple is what lets two
  players confirm they are on the same level (ADR-0002), and it is why a
  baker change invalidates caches rather than silently producing a
  different world.
- **Units and axes.** Unreal units, X forward, Y right, Z up — inherited
  deliberately, so a movement constant measured against the original
  game transfers with no conversion and no rounding. **S2** is a
  numbers-matching exercise, and a coordinate change would add error to
  every one of them.
- **Time.** The simulation runs on a fixed tick and is authoritative on
  the server. Rendering interpolates between ticks. **No gameplay code
  reads wall-clock time**, or the same input produces different results
  on two machines.
- **Determinism.** Given the same starting state and the same inputs,
  `uworld` produces the same result. No global mutable state except the
  logger, no reading of frame timing, no dependence on iteration order
  of unordered containers.
- **Threading.** One job system, in `core`. Simulation is single-threaded
  and deterministic. Rendering, asset loading and baking use jobs.
- **Tests.** Catch2 v3, fetched by the build rather than installed.
  `upkg`'s tests build **synthetic** packages in the test itself, so the
  suite passes with no Unreal Tournament present — that is **S7**. A
  second tier, off by default and enabled by a CMake option pointing at a
  real install, runs the same readers against real files locally.

## The stack, and what it rules out

| Choice | Why | Runner-up |
|---|---|---|
| **C++23** | `std::expected` is the error model above, not a nicety | C++20, which would need a hand-rolled equivalent |
| **CMake + Ninja** | What the machine already runs; hooks and CI are trivial | Meson |
| **Vulkan 1.3** | Explicit control of the exact features the renderer needs; already proven on this GPU by `DOOM_Ants` | `wgpu`, rejected for putting a layer between us and those features |
| **SDL3** | Window, input and gamepads in one dependency — its controller database already knows a DualShock 4 (**S9**) | GLFW, which has no gamepad database |
| **glm** | Well understood, header-only, matches the maths in every reference | Our own, later, if it earns it |
| **shaderc** | Compile GLSL to SPIR-V at build time | Hand-run `glslangValidator` |
| **Dear ImGui** | Editor and developer overlays, vendored | Nothing else is close for this job |
| **Assimp** | Model import for character authoring — **build-time only**, never linked into the game | Writing a glTF reader |
| **Catch2 v3** | Fetched, not installed, so a stranger's clone builds (**S7**) | GoogleTest |

**What this rules out.** No scripting virtual machine of any kind
(ADR-0004). No managed runtime. No OpenGL fallback path — a machine
without Vulkan 1.3 does not run this. No compiler older than GCC 14 or
Clang 18. **Linux is the primary target**; nothing in `core` may use a
Linux-only API without a stated port path, but Windows is not tested
before `1.0`. And ray tracing is neither required nor planned — the
visual target is reached with shadow maps, baked indirect light and
volumetrics, so no feature may be designed on the assumption that rays
are available.

## Decided later, deliberately

Named here so they are not mistaken for oversights. Each becomes an ADR
when it is settled, at the milestone shown.

- **Aim assist for gamepads (M6).** An arena shooter played on a stick is
  at a real disadvantage against a mouse, and the three answers —
  assist for everyone, assist only in Monster Hunt and against bots, or
  no assist and accept it — have different consequences for competitive
  play. Settling it now would be guessing; **S9** only requires that the
  controller is *playable*, and that is achievable either way.
- **Bot difficulty model (M6).** Whether bots are made harder by better
  decisions or by tighter aim, and where the honest ceiling sits.
- **Whether the weapon wheel pauses or slows time (M6).** UT99 has no
  precedent and the answer changes how the game plays, not just how it
  looks.

## Close calls

| | |
|---|---|
| [ADR-0001](decisions/ADR-0001-from-scratch-engine.md) | Build a new engine rather than host in Godot or Unreal 5 |
| [ADR-0002](decisions/ADR-0002-bake-maps-offline.md) | Convert maps offline rather than read `.unr` at runtime |
| [ADR-0003](decisions/ADR-0003-ship-the-recipe-not-the-content.md) | Distribute recipes and code; never Epic's content |
| [ADR-0004](decisions/ADR-0004-resolve-classes-by-ancestry.md) | Understand custom actors by ancestry, not by running UnrealScript |
| [ADR-0005](decisions/ADR-0005-own-network-protocol.md) | Our own protocol, not UT99 wire compatibility |

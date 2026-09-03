# UT_Ants — Design

> **Purpose — so the shape is decided once, and anyone can tell where a
> new piece of work belongs and what it is allowed to touch.**

**This document is a gate.** Work is not broken into items until it is
agreed — `~/.claude/workflow.md` § 2. It passes when someone can take any
item off the queue and say which part it belongs in and what it may
touch.

**Status:** drafted 2026-09-03, awaiting the `review-contract` gate.

What this is for is [docs/discovery.md](discovery.md); the signs of
success cited below live there.

## The parts

The split between build-time and runtime is load-bearing and is
enforced, not merely described — see *What may depend on what*.

### Build-time only — never linked into a runtime target

| Part | Responsible for |
|---|---|
| `upkg` | Reading Unreal Engine 1 packages: `.unr`, `.utx`, `.uax`, `.umx`, `.u`. Names, imports, exports, object serialisation, class tables and default properties. Data in, structures out |
| `umat` | Turning a 1999 texture into a PBR material — resolving the curated library first, generating base colour, normal, roughness, metallic, height and emissive otherwise. **The curated library ships with the baker**: it holds our own material definitions and any art we have the right to distribute, it lives in the repository rather than under `content/`, and changing it is a baker version change |
| `ubake` | The map baker. Drives `upkg`, `umat` and the graph builders, and writes one map bundle |

### The bundle — the seam between the two halves

| Part | Responsible for |
|---|---|
| `ubundle` | The container format for everything this project ships as content — a baked map, or a character `ued` authored — written `.utab`. Owns the file layout and its version, never the meaning of what a section holds. A map carries geometry, materials, collision, lights, baked indirect light, entity placements and the graphs `unav` defines; a character carries its mesh, skeleton, skins and attachment points |
| `unav` | The two graphs a bot reasons over — *where you can go*, and *what opens what*. Owns their **types and their queries**; `ubundle` owns how they are written to a file. Built at bake time, read at runtime |
| `urecipe` | The recipe format, read and write — per-map material assignments, atmosphere, friendly name, bot hints, rule defaults, and the **class overrides** ADR-0004 requires so a badly-resolved custom actor can be given a better answer once rather than rediscovered by every player. Read by the baker as an input and by `ugame` for its rule defaults, so it crosses the seam like the two above. The one thing this project distributes that describes somebody else's map (ADR-0003) |

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

### Tools — not runtime targets

| Part | Responsible for |
|---|---|
| `ued` | The editor. Maps, enemies, player characters, and the packaging that makes them downloadable |

### Programs

`ut-ants` (client), `ut-ants-server` (dedicated, headless), `ut-bake`
(convert a map), `ut-dump` (inspect a package), `ut-ed` (editor).

**`ut-ants` and `ut-ants-server` are the runtime targets, and they are
the only two.** `ut-bake`, `ut-dump` and `ut-ed` are tools, and rule 2
is written about the runtime targets by name, so it does not reach them.

**Being a tool says nothing about who gets it.** `ut-bake` **ships with
the client**, because rule 16 has `ut-ants` run it. `ut-ed` **ships to
anyone authoring content**, which is what **S6** asks for. `ut-dump` is
a developer tool and ships with neither.

## What may depend on what

**Rules 2 and 3 make ADR-0002 true in code rather than in prose; rules
15 and 16 do the same for ADR-0003. Rules 4, 9 and 12 are what make
S2 reachable.**

1. **`core` depends on nothing** beyond the C++ standard library.
2. **Neither runtime target links `upkg`, `umat` or `ubake`.** The game
   cannot read a `.unr` file even by accident, because the code to do so
   is not in it. A test asserts the link closure of `ut-ants` and
   `ut-ants-server` contains none of the three. **The rule is about
   those two programs and no others** — `ut-bake`, `ut-dump` and `ut-ed`
   are tools, they link whatever they need, and the test does not name
   them.
3. **`ubundle`, `unav` and `urecipe` are the content vocabularies
   shared across the seam, and there are no others.** The baker writes
   bundles and reads recipes; the runtime reads both; `ued` reads and
   writes both, which is the whole of the `0.5.0` and `0.6.0`
   milestones. A change to what a bundle contains is a change to
   `ubundle`.
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
14. **A recipe expresses defaults; the server decides.** `urecipe` may
    say a map *wants* the super weapons, because its author knows the
    map. The running server's rotation configuration overrides that and
    is authoritative, and the resolved answer is replicated to every
    client on map change. Rule 8 is why: if a recipe could set a rule
    outright, the rules would live in two places and a downloaded file
    would be able to change how a server plays.
15. **Everything derived from the player's Unreal Tournament install
    lives under `content/`, and nothing under `content/` is committed or
    published from this repository.** That is the install itself, every
    `.utab` baked from it, and the host-download cache. **Everything
    authored here lives outside it** — code, recipes, material
    definitions, and the maps and characters `ued` writes, which
    **S6** requires be distributable. Configuration and logs are
    neither, and live where the platform puts them. This is what makes
    ADR-0003's quarantine true in code, and rule 2 does not do it: a
    program can link no package reader and still write a baked map into
    the repository. **What a running SERVER may send is a different
    question and ADR-0006 owns it** — community work travels, Epic's
    does not, and where a file is cached decides neither. **Its check is a guard in `.githooks/pre-push` and
    in CI** that fails on an Unreal asset or a `.utab` staged outside
    `content/`.
16. **The runtime never reads the player's Unreal Tournament install
    directly. It shells out to `ut-bake`, which already links the
    package reader.** `ut-ants` validates an install by running
    `ut-bake --check <path>` and reports what it says; it triggers a
    bake the same way. Neither happens inside a runtime target, so rule
    2 survives both, and there is no separate manifest format for the
    two programs to disagree about. This is what ADR-0003's *refuse to
    start and say plainly why* runs, and what **S5**'s bake runs.
17. **`unav` owns the graph types; `ubundle` owns their bytes on
    disk.** So `ubundle` depends on `unav`, never the reverse. Adding an
    edge type is a change to `unav` and a bundle-format version bump;
    changing how a section is framed is `ubundle` alone.
18. **Any dependency these rules do not prohibit is allowed.** The rules
    above are prohibitions, not a whitelist, so the check that enforces
    them is a check for forbidden edges — never a list of permitted
    ones. Without this line rule 10's *"may depend on anything"* implies
    a whitelist, and half the rules above become redundant under it.

## What every part does the same way

- **Baking happens ahead of need wherever there is warning.** A bake
  is expensive on purpose (ADR-0002), and **S5** gives a joining player
  one minute — so a bake on the join path cannot meet it. The client
  fetches and bakes the **next** map while the current one is still
  being played, which Monster Hunt makes easy because the map vote
  settles the next map before it starts, and the server browser names a
  server's rotation before anyone connects. **The cold case is real and
  is not hidden**: a player joining a map nobody has baked waits, is
  told what is happening and how far along it is, and **S5** is measured
  against the ordinary case rather than that one.
- **A movement assist is either visual or physical, and the two are
  built differently.** First-person platforming fails because the player
  cannot see their feet, and Metroid Prime's answer is mostly *showing*
  rather than *changing*: a marker on the ground where the current arc
  lands, and a small automatic downward pitch on take-off so the landing
  is in frame. Those are **visual**, live in `uui` and `urender`, are the
  player's own setting, and touch the simulation nowhere — so they cost
  **S2** nothing and are on by default. Assists that change what the
  body does — a grace window after leaving a ledge, mantling onto one,
  step-up over small obstacles — are **physical**: they live in
  `uworld`, are a server setting replicated on join like every other
  rule (rule 14), and are **off in Deathmatch and Team Deathmatch and on
  in Monster Hunt**, because that is where the platforming is and where
  **S2** is not being measured. **S11** is written to fail if either
  half is got wrong.
- **Weapons, monsters and pickups are data, not code.** A weapon is a
  definition — damage, fire rate, projectile, spread, ammo, model,
  sounds — and a **weapon set** is a named collection of them, selectable
  per map (**S10**). Behaviour that genuinely differs gets code behind a
  small set of firing archetypes; everything else is numbers. This is
  what lets a super-weapon set be a table rather than a parallel
  codebase, and it is the same mechanism ADR-0004 uses to give a custom
  class read out of a package somewhere to land.
- **Errors.** `std::expected<T, Error>` across every module boundary.
  Exceptions may be used inside a part and must never escape one. Only a
  program in `Programs` above may terminate the process.
- **Logging.** One logger, in `core`, with a category per part and
  levels. No `printf`, no `std::cout` outside a program's own startup.
- **Configuration.** One TOML file per user, one per server. Any setting
  that changes gameplay is replicated to clients on join, so both sides
  simulate under the same numbers.
- **Content addressing.** A bundle is named by the hash of its source
  map, its recipe and the baker version. Those three are what let two
  players confirm they are on the same level (ADR-0002), and they are
  why a baker change invalidates caches rather than silently producing a
  different world. **Every other bake input must be covered by one of
  the three, or the name is a lie** — which is why `umat`'s curated
  library ships with the baker and is versioned with it. A library that
  could change independently would let two players compute one name for
  two different worlds. **A `.utab` with no source map — one `ued`
  authored, a character included — is named by the hash of its own
  contents and the baker version.**
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
| **Assimp** | Model import for character authoring — linked by `ut-ed` only, and **never by a runtime target** | Writing a glTF reader |
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
when it is settled — at the release shown, using the version labels
`docs/standards/versioning-overrides.md` defines. **Each must be settled
before that release is built, not during it**, because all three change
what `uworld` ticks.

- **Aim assist for gamepads (`0.3.0`).** An arena shooter played on a stick is
  at a real disadvantage against a mouse, and the three answers —
  assist for everyone, assist only in Monster Hunt and against bots, or
  no assist and accept it — have different consequences for competitive
  play. Settling it now would be guessing; **S9** only requires that the
  controller is *playable*, and that is achievable either way. `0.3.0`
  is where there is first something to aim at.
- **Bot difficulty model (`0.3.0`).** Whether bots are made harder by
  better decisions or by tighter aim, and where the honest ceiling sits.
  `0.3.0` is where bots first ship.
- **Whether the weapon wheel pauses or slows time (`0.2.0`).** UT99 has
  no precedent and the answer changes how the game plays, not just how
  it looks. It is `0.2.0` because **S9** makes the wheel a `0.2.0` cut
  criterion, and because a time scale the server must agree on is an
  input to the fixed tick — added later it would cross rules 9 and 12.

## Close calls

| | |
|---|---|
| [ADR-0001](decisions/ADR-0001-from-scratch-engine.md) | Build a new engine rather than host in Godot or Unreal 5 |
| [ADR-0002](decisions/ADR-0002-bake-maps-offline.md) | Convert maps offline rather than read `.unr` at runtime |
| [ADR-0003](decisions/ADR-0003-ship-the-recipe-not-the-content.md) | Distribute recipes and code; never Epic's content |
| [ADR-0004](decisions/ADR-0004-resolve-classes-by-ancestry.md) | Understand custom actors by ancestry, not by running UnrealScript |
| [ADR-0005](decisions/ADR-0005-own-network-protocol.md) | Our own protocol, not UT99 wire compatibility |
| [ADR-0006](decisions/ADR-0006-community-content-may-be-served.md) | The quarantine restricts Epic-derived content; community work may be served |

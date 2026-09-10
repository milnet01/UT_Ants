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
| `umat` | Turning a 1999 texture into a PBR material — generating base colour, normal, roughness, height and emissive maps, with settings the curated library adjusts where it holds an entry for the texture's picture (UTA-0010). Metallic is one value per material, never a map (UTA-0009). **The curated library ships with the baker**: it holds our own material definitions and any art we have the right to distribute, it lives in the repository rather than under `content/`, and a change its digest covers is a baker version change |
| `ubake` | The map baker. Drives `upkg`, `umat` and the graph builders, and writes one map bundle |

### The bundle — the seam between the two halves

| Part | Responsible for |
|---|---|
| `ubundle` | The container format for everything this project ships as content — a baked map, or a character `ued` authored — written `.utab`. Owns the file layout and its version, never the meaning of what a section holds. A map carries geometry, materials, collision, lights, baked indirect light, entity placements, the graphs `unav` defines, and the **level-map** section `umap` defines; a character carries its mesh, skeleton, skins and attachment points |
| `umap` | The **level map** — the simplified, room-partitioned model of a level that the in-game map screen draws, and the rule for which room a position falls in. Built at bake time from the level's own zones, read at runtime. Owns the model and the lookup; `ubundle` owns its bytes, and `uui` owns how it is drawn |
| `unav` | The two graphs a bot reasons over — *where you can go*, and *what opens what*. Owns their **types and their queries**; `ubundle` owns how they are written to a file. Built at bake time, read at runtime |
| `urecipe` | The recipe format, read and write — per-map material assignments, atmosphere, friendly name, bot hints, rule defaults, and the **class overrides** ADR-0004 requires so a badly-resolved custom actor can be given a better answer once rather than rediscovered by every player. Read by the baker as an input and by `ugame` for its rule defaults and its class overrides, so it crosses the seam like the two above. The one thing this project distributes that describes somebody else's map (ADR-0003) |

### Runtime

| Part | Responsible for |
|---|---|
| `core` | Types, math, memory, error type, logging, filesystem, job system. Depends on nothing |
| `uinput` | Devices to actions. Keyboard, mouse and gamepad are three sources of one action set, with per-device bindings, dead zones and response curves |
| `uworld` | The simulation. Entities, collision, movement, physics, the fixed tick, and which room of the level map each body currently occupies. Knows how a body moves; knows nothing about scoring |
| `urender` | Vulkan. Draws a bundle: dynamic lights and shadows, PBR materials, volumetrics, light shafts, ambient occlusion, post-processing |
| `uaudio` | Sound playback, positional mixing, music |
| `unet` | Transport, replication, server discovery and query, and content transfer against a fingerprint manifest |
| `uai` | Bots. Navigation, combat, and the planner that gets them through door puzzles |
| `ugame` | The rules. Deathmatch, Team Deathmatch, Monster Hunt, weapons, monsters, pickups, mutators, chat, map voting. The only part that knows what a frag is |
| `uui` | Menus, HUD, scoreboard, settings, the weapon wheel, the between-match map browser, and the level-map screen |

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
both runtime targets**, because rule 16 has each of them run it. `ut-ed`
**ships to anyone authoring content**, which is what **S6** asks for.
`ut-dump` ships to developers, who build it from source — which is why
`versioning-overrides.md` still counts its command line a breaking
surface.

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
   them. **The same test asserts `ut-ants-server`'s closure contains no
   `urender`, `uaudio` or `uui`.** Rule 4 forbids the edge out of
   `uworld` and nothing forbids `ugame` reaching them, so without this a
   dedicated server links Vulkan transitively and fails to start on a
   machine with no graphics driver.
3. **`ubundle`, `unav` and `urecipe` are the content vocabularies
   shared across the seam, along with `umap`, and there are no
   others.** The baker writes
   bundles and reads recipes; the runtime reads both; `ued` reads and
   writes both, which is the whole of the `0.5.0` and `0.6.0`
   milestones. A change to what a bundle contains is a change to
   `ubundle`.
4. **`uworld` must not depend on `urender`, `uaudio` or `uui`.** The
   dedicated server links no Vulkan and opens no audio device. This is
   the rule that keeps the simulation pure. **It is checked as a
   forbidden module edge, not as a link closure** — the client links
   `urender` and must, per rule 5, so a closure test over `ut-ants`
   would fail the day it was written. The link-closure form belongs to
   rule 2 and to `ut-ants-server`.
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
   sources.** No `#ifdef CLIENT` inside either, and no build setting that
   changes a result — which across two toolchains means the numeric
   contract in § What every part does the same way, since MSVC and GCC
   have no flags in common.
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
    definitions, and the maps and characters `ued` **authors**, which
    **S6** requires be distributable. A bundle `ued` writes by editing a
    `derived` one is itself `derived` and is written under `content/`
    like any other bake. Configuration and logs are
    neither, and live where the platform puts them. This is what makes
    ADR-0003's quarantine true in code, and rule 2 does not do it: a
    program can link no package reader and still write a baked map into
    the repository.

    **Every bundle records an origin, and it has two values.**
    `authored` means nothing out of anybody's Unreal Tournament install
    contributed to it; `derived` means something did. **Whichever tool
    writes a bundle writes the field, inherited from the most
    restrictive input** — so a `ued` edit of a baked map is `derived`,
    and only a bundle built from our own material and models is
    `authored`.

    **Two values rather than three, because a bundle has many sources.**
    A community Monster Hunt map draws on Epic's stock texture packages,
    so a per-map judgement would stamp it not-Epic's while its materials
    are Epic's throughout. `derived` is the honest answer for anything
    that touched an install, and the only one a tool can compute without
    judgement.

    **The guard runs over tracked and staged paths only** — never the
    working tree, which holds the player's own install under
    `content/ut99/` and would block every push. Three checks: no tracked
    path under `content/`; no tracked path matching the Unreal asset
    extensions; no tracked `.utab` outside `content/` whose origin is
    not `authored`. **The extension list is written once and read by
    both `.gitignore` and the guard**, or the two drift and the guard
    passes what git was ignoring.

    **And bundles do not travel.** `unet` sends a map as the community's
    own package plus our recipe, and the joining player's machine bakes
    it against their own install — ADR-0003 § Consequences, a clause
    ADR-0006 did not supersede. Epic's content never moves because the
    player already has it. **An `authored` bundle is the one exception
    and is sent whole**, since there is no install to bake it against;
    that is the route a `ued` character and an original map take, and
    what **S6** needs.

    **A package on the wire is classified by the stock manifest, which
    is a second test and not the origin field.** Origin is a property of
    a bundle, and what travels for a map is a package — so the two
    questions need two answers, and one field answering both is what
    made the last attempt wrong. **The game ships a stock manifest**:
    the names and hashes of the packages Epic shipped, our own factual
    data, game data rather than a bake input. **`unet` withholds any
    package whose *name* the manifest lists, whatever its hash; withholds
    any whose name and hash it cannot read; and sends the rest.** Failing closed is deliberate — an unrecognised
    package withheld costs a player a map, and an unrecognised package
    sent is the breach ADR-0006 § Decision forbids. This is the
    per-package test ADR-0006 § Consequences asks for and the origin
    field cannot give.

    **A `derived` bundle is never published, and `ued` does not try.**
    Editing somebody else's map produces a **recipe** — materials,
    atmosphere, bot hints, class overrides — which is precisely *our
    changes on top of their map*, is small, and is already the thing
    that travels. **Origin is inherited from the input bundle, never from
    what was edited**: a map started empty is `authored` however much
    geometry it gains, and a `derived` one stays `derived` however much is
    changed in it — so editing a baked map's geometry gives a `derived`
    bundle, which is never published. So `0.5.0`'s *built, hosted,
    downloaded and played by someone else* is a recipe for their map or
    a bundle of your own, and never a re-publication of Epic's. **ADR-0006's § Consequences reads the
    Epic-versus-community test as *what the baker read out of the
    install*, which cannot hold here — the community's own maps sit
    inside that install alongside Epic's, and this rule is the
    correction.**
16. **The runtime never reads the player's Unreal Tournament install
    directly. It shells out to `ut-bake`, which already links the
    package reader.** **Both runtime targets do this, not just the
    client.** Each validates an install by running `ut-bake --check
    <path>` and reports what it says, and each triggers a bake the same
    way — a dedicated server bakes its own rotation, so **S8**'s host
    needs an install exactly as a player does. Neither act happens
    inside a runtime target, so rule 2 survives both, and there is no
    separate manifest format for the two programs to disagree about.
    This is what ADR-0003's *refuse to start and say plainly why*
    runs.
17. **`unav` and `umap` own their model types; `ubundle` owns those
    types' bytes on disk.** So `ubundle` depends on both, never the
    reverse. Adding a graph edge type or a room attribute is a change to
    `unav` or `umap` and a bundle-format version bump; changing how a
    section is framed is `ubundle` alone.
18. **Whose exploration you may see is `ugame`'s, and the answer is
    your team's.** A room another player has entered is intelligence
    about that player, so the server sends a client only its own team's
    exploration — which in Monster Hunt is everyone, and in Deathmatch
    is nobody but you. **`uui` draws what it is given and never asks for
    more**, so a client cannot reveal what the server declined to send.
    Without this the level map is a wallhack on any competitive server,
    and **S12** is written to fail if it is.
19. **Any dependency these rules do not prohibit is allowed.** The rules
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
  server's rotation before anyone connects — which is enough to pre-bake
  any map the client already holds, though a map it lacks cannot be
  fetched until it connects. **The cold case is real and
  is not hidden**: a player joining a map nobody has baked waits, is
  told what is happening and how far along it is. **S5** carries that
  qualifier itself; this document does not add one.
- **Exploration is per-room, recorded by the server, and shown three
  ways.** `umap` partitions a level into rooms from the level's own
  zones; `uworld` reports which room each body is in; the server keeps a
  bit per player per room. The map screen then draws a room as **yours**
  (you have been in it), **your team's** (a teammate has, subject to
  rule 18), or **unreached** (nobody on your team has). **The whole
  layout is drawn from the first moment** — the level is somebody else's
  1999 map, not a secret we are keeping — so an unreached room is
  visibly there and visibly unreached, which is what **S12** asks for
  and what makes the map worth opening in Monster Hunt.
- **A movement assist is either visual or physical, and the two are
  built differently.** First-person platforming fails because the player
  cannot see their feet, and Metroid Prime's answer is mostly *showing*
  rather than *changing*: a marker on the ground where the current arc
  lands, and a small automatic downward pitch on take-off so the landing
  is in frame. Those are **visual** and live in `uui` and `urender`,
  which draw an arc `uworld` projects and expose: gravity, jump impulse
  and air control are its fidelity constants, so a second integrator
  elsewhere would drift from them the first time one is tuned.
  **The pitch offsets the render camera only — the view angles in the
  input command are untouched, and the crosshair stays on the aim ray**,
  so it rises up the screen as the camera tips down, while the shot goes
  exactly where the player pointed. That is what makes the claim of touching the
  simulation nowhere true rather than merely stated, and it is why these
  are the player's own setting and on by default.

  **UT99's own step-up height is not an assist.** It is one of the
  constants ADR-0001 names as carrying the feel, so it is an always-on
  `uworld` fidelity value, measured like run speed and dodge impulse.
  Only step-up *beyond* that height is optional.

  Assists that change what the body does — a grace window after leaving
  a ledge, mantling onto one, step-up above UT99's own height — are
  **physical**: they live in `uworld` and are a server setting
  replicated on join like every other rule (rule 14). **They default off
  everywhere, including Monster Hunt.** **S11** asks a UT99 player to
  check **S2** on the same server the platforming is happening on, so a
  physical assist on by default would fail **S11**'s second half by
  construction. Turning one on is a server operator's deliberate trade,
  and **S11** is measured at the defaults.
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
- **Configuration.** One TOML file per user, one per server. **Any
  setting that changes gameplay is replicated on join, and re-resolved
  and re-replicated on every map change**, so both sides always simulate
  under the same numbers. Both events are required: a per-map setting
  sent only at join never reaches a player who was already connected
  when the rotation advanced, which is **S10** failing for everyone who
  did not reconnect. Rule 14 and the movement-assist bullet both defer
  to this sentence.
- **Content addressing.** A bundle is named by the hash of its source
  map, its recipe and the baker version. Those three are what let two
  players confirm they are on the same level (ADR-0002), and they are
  why a baker change invalidates caches rather than silently producing a
  different world. **Every other bake input must be covered by one of
  the three, or the name is a lie** — which is why `umat`'s curated
  library ships with the baker and is versioned with it. **The recipe
  enters the hash by its bake-relevant fields only** — material
  assignments, atmosphere, and anything else `ubake` reads. Its rule
  defaults, friendly name, bot hints and class overrides are read at
  runtime and change
  no pixel, so a server operator switching a weapon set must not
  invalidate every client's cached bake of that map.

  **ADR-0004's global class-override list is NOT a bake input**, and
  bumping the baker for it would invalidate every cached bake on a large
  rotation to fix one monster. A bundle stores each placed actor's class
  name, its ancestry and its defaults; `ugame` resolves that to one of
  ours when it spawns. So the list is game data, shipped with the game,
  changed without rebaking anything — and a per-map override in
  `urecipe` beats it, because the map's author knows the map. A library that
  could change independently would let two players compute one name for
  two different worlds. **A `.utab` that any tool other than `ubake`
  wrote — a `ued` map, a character, an edited bundle — is named by the
  hash of its own contents and the content-tool version, which `ubake`
  and `ued` share.** Naming an edited bundle by its source map would
  give two different worlds one name, which is the harm this bullet
  exists to prevent. **The `ubundle` format version is
  one of the baker's own inputs**, so a framing change bumps the baker
  version and therefore every name — which is what
  `versioning-overrides.md` means when it says a bundle-format change
  invalidates every cached bake.
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
  of unordered containers. **And a numeric contract, because two
  first-class toolchains do not agree by default:** floating-point
  contraction and fast-math off, and no platform maths library in the
  simulation or the baker. `ubake` is bound by it too — ADR-0002 needs
  one map, recipe and baker version to hash to one bundle on any machine,
  and that now spans two compilers.
- **Threading.** One job system, in `core`. Simulation is single-threaded
  and deterministic. Rendering, asset loading and baking use jobs.
- **Tests.** Catch2 v3, fetched by the build rather than installed.
  `upkg`'s tests build **synthetic** packages in the test itself, so the
  suite passes with no Unreal Tournament present — that is **S7**. A
  second tier, off by default and enabled by a CMake option pointing at a
  real install, runs the same readers against real files locally.

## The stack, and what it rules out

| Choice | How it is acquired | Why | Runner-up |
|---|---|---|---|
| **C++23** | — | `std::expected` is the error model above, not a nicety | C++20, which would need a hand-rolled equivalent |
| **CMake + Ninja** | — | What the machine already runs; hooks and CI are trivial. Ninja on Linux; on Windows the Visual Studio generator, which finds MSVC without a developer command prompt | Meson |
| **Vulkan 1.3** | Installed SDK | Explicit control of the exact features the renderer needs; already proven on this GPU by `DOOM_Ants` | `wgpu`, rejected for putting a layer between us and those features |
| **SDL3** | Fetched | Window, input and gamepads in one dependency — its controller database already knows a DualShock 4 (**S9**) | GLFW, which has no gamepad database |
| **glm** | Fetched | Well understood, header-only, matches the maths in every reference | Our own, later, if it earns it |
| **shaderc** | From the Vulkan SDK | Compile GLSL to SPIR-V at build time | Hand-run `glslangValidator` |
| **Dear ImGui** | Vendored | Editor and developer overlays, vendored | Nothing else is close for this job |
| **Assimp** | Fetched, `ut-ed` only | Model import for character authoring — linked by `ut-ed` only, and **never by a runtime target** | Writing a glTF reader |
| **Catch2 v3** | Fetched | Fetched, not installed, so a stranger's clone builds (**S7**) | GoogleTest |
| **SDL3 audio** | With SDL3 | `uaudio` mixes and spatialises on SDL3's device, which is already a dependency — no second audio stack, and nothing new to check against GPL-3.0 | OpenAL Soft |
| **In-house in-game UI** | Ours | `uui` draws the HUD, menus, map browser and weapon wheel through `urender`. Dear ImGui is for the editor and developer overlays and is **never** in a shipped game's UI | Dear ImGui everywhere |

**`ADR-0007` owns the acquisition rule and the reasons**, including the
question a dependency that is not in this table is asked. The column above
is an index into it, not a second statement of it. In short: a library the compiler can build
from source, whose version need not match anything already on the machine, is
fetched at an exact tag; the Vulkan headers, loader, validation
layers and `glslc` are installed and found rather than fetched, at Vulkan 1.3
or newer — the LunarG SDK on Windows, either that or the distribution's
packages on Linux; Dear ImGui is vendored because its build system produces
nothing anyone would link -- it ships none at all. **Installing that SDK is a prerequisite on both platforms**, which
is the one step **S7** does not cover and the README has to state.

**Linux and Windows are both first-class targets.** Neither is the
primary one. Every release is built and its tests run on both, and a
change that breaks either is a broken change — so a platform-specific
API needs its counterpart written at the same time rather than a stated
port path to be walked later, and rule 1 still holds, so `core` is not
where one goes. **Windows builds with MSVC**, so the floor below names it
alongside GCC and Clang, and the gate builds all three: GCC and Clang on
Linux, MSVC on Windows. A floor no job exercises is not a floor. macOS is
neither supported nor ruled out, and is not built.

**What this rules out.** No scripting virtual machine of any kind
(ADR-0004). No managed runtime. No OpenGL fallback path — a machine
without Vulkan 1.3 does not run this. No compiler older than GCC 14,
Clang 19 or MSVC 19.40 (Visual Studio 2022 17.10) -- Clang 18 reports
`__cpp_concepts` as 201907L, and libstdc++ gates `std::expected` on
202002L, so it compiles the header away and this project cannot build
under it at all. And ray tracing is
neither required nor planned — the
visual target is reached with shadow maps, baked indirect light and
volumetrics, so no feature may be designed on the assumption that rays
are available.

## Decided later, deliberately

Named here so they are not mistaken for oversights. Each becomes an ADR
when it is settled — at the release shown, using the version labels
`docs/standards/versioning-overrides.md` defines. **Each is settled at the
START of the release that names it, against a prototype, and before the
work it constrains is written** — not after that release, because each
changes something the release itself builds: the fixed tick's own inputs
for the weapon wheel, `ugame`'s difficulty constants for the bot model,
and the client's aim path for the assist. Settling them *before* the
release is not possible for two, whose whole reason for waiting is that
the thing to judge does not exist yet.

**Aim assist is not a tick input**, which is why it can wait for
`0.3.0` while the wheel cannot: it transforms the view angles the input
command already carries, before that command is sent, so the struct
rules 9 and 12 turn on does not change and the server's simulation is
untouched.

- **Aim assist for gamepads (`0.3.0`).** An arena shooter played on a stick is
  at a real disadvantage against a mouse, and the three answers —
  assist for everyone, assist only in Monster Hunt and against bots, or
  no assist and accept it — have different consequences for competitive
  play. Settling it now would be guessing, and `0.2.0`'s own criterion
  — that it plays as well on a gamepad as on a mouse — is met by
  bindings, dead zones and response curves, without an assist. `0.3.0`
  is where there is first something to aim at.
- **Bot difficulty model (`0.3.0`).** Whether bots are made harder by
  better decisions or by tighter aim, and where the honest ceiling sits.
  `0.3.0` is where bots first ship.
- **Whether the weapon wheel pauses or slows time (`0.2.0`).** UT99 has
  no precedent and the answer changes how the game plays, not just how
  it looks. It is `0.2.0` because a time scale the server must agree on
  is an input to the fixed tick, and adding one later would cross rules
  9 and 12. **Not because of S9** — `versioning-overrides.md` cuts
  **S9** at `0.4.0`, where the Monster Hunt round it asks for exists,
  and that table owns what a release contains.

## Close calls

| | |
|---|---|
| [ADR-0001](decisions/ADR-0001-from-scratch-engine.md) | Build a new engine rather than host in Godot or Unreal 5 |
| [ADR-0002](decisions/ADR-0002-bake-maps-offline.md) | Convert maps offline rather than read `.unr` at runtime |
| [ADR-0003](decisions/ADR-0003-ship-the-recipe-not-the-content.md) | Distribute recipes and code; never Epic's content |
| [ADR-0004](decisions/ADR-0004-resolve-classes-by-ancestry.md) | Understand custom actors by ancestry, not by running UnrealScript |
| [ADR-0005](decisions/ADR-0005-own-network-protocol.md) | Our own protocol, not UT99 wire compatibility |
| [ADR-0006](decisions/ADR-0006-community-content-may-be-served.md) | The quarantine restricts Epic-derived content; community work may be served |

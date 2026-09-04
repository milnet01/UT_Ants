<!-- ants-roadmap-format: 1 -->
# UT_Ants — Roadmap

> What is planned, in progress and shipped. [CHANGELOG.md](CHANGELOG.md)
> is the user-facing record of what shipped; released items stay here and
> flip to ✅.
>
> **Format:** `~/.claude/standards/roadmap-format.md`. Theme emojis
> (§ 3.4), priority bands (§ 3.12) and the full bullet field set (§ 3.5)
> are defined there and deliberately not restated here.

**Legend**

- ✅ Done · 🚧 In progress · 📋 Planned · 💭 Considered

## P01 — (first block)

> Pre-1.0 projects use phase blocks (`## P01 — …`); these promote into
> `## 1.0.0 — initial release` at 1.0. Such a roadmap does not rotate
> into archives — see § 3.9.
>
> **Nothing goes here until design is agreed.** Items are broken out of
> the design, and the gate on doing so is that every sign of success in
> `docs/discovery.md` — each carrying an `S<n>` id — is claimed by at
> least one item, and every item
> names what must close before it can start, in `Blocked-by:`
> (`~/.claude/workflow.md` § 5, `roadmap-format.md` § 3.5).

## 0.1.0 — Bake and render

Read a UT package, bake a level into a .utab bundle, and walk through it with
modern lighting. Closes S1 and S7. Nothing here plays: there is no movement
model, no weapon and no opponent until 0.2.0.

- ✅ [UTA-0001] **Build system, test harness and the synthetic-package fixtures.**
  CMake + Ninja, C++23, Catch2 v3 fetched by the build rather than installed.
  The fixtures are the load-bearing part: upkg's tests construct valid UE1
  packages byte by byte in the test itself, so the suite passes on a clone with
  no Unreal Tournament present. A second tier, off by default behind a CMake
  option pointing at a real install, runs the same readers against real files.
  This is what S7 is measured on.
  Started 2026-09-03. No spec: spec-format.md 1's test says no -- one
  subsystem, obvious shape, cheap to redo, and nothing else binds to it.
  Resolved (2026-09-03): CMake + Ninja, C++23, Catch2 v3.16.0 fetched by
  the build. 11 tests green on a clone with no Unreal Tournament
  present, which is S7; 13 with UTA_REAL_ASSET_TESTS pointed at a real
  install, and that option refuses to configure without a path. Fixtures
  ship an encoder only, so UTA-0003's decoder is an independent
  implementation. Assertions proved able to fail: a six-to-seven bit
  shift reddened three tests with 64 encoding as {0}. Two of the
  hand-computed vectors were wrong and the run caught both.
  **Layman:** The scaffolding: how the project compiles, how tests run, and fake UT files the tests can use so nobody needs the real game to check our work.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: core.

- ✅ [UTA-0002] **core: error type, logging, filesystem and the job system.**
  std::expected<T, Error> across every module boundary; exceptions may be used
  inside a part and never escape one. One logger with a category per part. A job
  system for rendering, asset loading and baking; the simulation stays
  single-threaded and deterministic.
  Depends on nothing beyond the standard library (rule 1).
  Picked 2026-09-04. Spec required per spec-format.md § 1: the error
  type is a contract every other module binds to, the job system is the
  concurrency case, and how much an Error carries is a real design
  choice.
  Resolved (2026-09-04): src/core/ builds as uta_core, linking nothing
  but Threads::Threads, which a configure-time assertion checks
  (INV-14). 52 unit tests green under GCC, Clang and MSVC, and clean
  under ThreadSanitizer -- a leg proven able to REPORT, using a
  throwaway racy test that was not committed. The spec was gated to its
  cap before any code was written, then check-code and a four-lane
  review-code sweep ran over the result; findings not fixed here are
  UTA-0046, UTA-0047 and UTA-0048. Three defects only the pipeline could
  find: the project's Clang floor was wrong (18 cannot compile
  std::expected), .gitignore was silently swallowing src/core, and the
  Windows Clang leg lacked the ThreadSanitizer runtime.
  **Layman:** The shared foundations every other part uses -- how errors are reported, how things get logged, and how work is spread across processor cores.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: core.

- 📋 [UTA-0003] **upkg: read the Unreal Engine 1 package container.**
  Header, name table, import and export tables, the compact index encoding, and
  object serialisation. Data in, structures out -- no graphics, no game.
  Build-time only: no runtime target may link it (rule 2).
  Verified against synthetic packages the tests build themselves.
  Progress (2026-09-04): picked up. spec-format.md §1 triggers 1, 4 and
  5 all fire -- a file format other code binds to, a new on-disk read
  path, and malformed-input edge cases -- so this gets a spec before
  code.
  Progress (2026-09-04): parked, not abandoned. The spec is drafted at
  docs/specs/UTA-0003-package-container.md and has NOT been through
  review-contract, so it must not be built from yet. Its format
  description was verified against real bytes (a v68 map parses: tables,
  serial ranges, property lists) and against three community sources
  plus one working implementation; scripts/package-census.py records the
  install census three of its design decisions rest on. Paused under the
  user's stated priority order of 2026-09-04: outstanding review
  findings come before new roadmap items.
  **Layman:** Open a UT file and work out what is inside it -- the index of names and objects. Nothing is drawn yet; this is learning to read the format.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: upkg.

- 📋 [UTA-0004] **upkg: read level geometry, textures, sounds and actor placements.**
  The Model (BSP) geometry, palettised textures with their PolyFlags (masked,
  translucent, unlit, environment -- free information about glass, water, sky and
  lava), sounds, and the actor list with each actor's class, position and
  properties.
  Blocked-by: the container reader.
  **Layman:** Pull the actual level out of the file -- its walls, its textures, and the list of everything the designer placed in it.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: upkg.

- 📋 [UTA-0005] **upkg: read class tables, default properties and ancestry across packages.**
  The class table and defaultproperties, with the parent chain resolved across
  imports so a class in one package can be walked to a base class in another.
  No bytecode is read and no interpreter is written (ADR-0004). This item only
  EXTRACTS; resolving an unknown class onto one of ours is 0.3.0's.
  Blocked-by: the container reader.
  **Layman:** Work out what a custom monster IS -- what it descends from and what its numbers are -- without running any of its code. This is what makes the Monster Hunt maps work later.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: upkg.

- 📋 [UTA-0006] **unav: extract the navigation graph and the event-wiring graph.**
  The navigation graph comes from the PathNodes the level designer placed. The
  wiring graph comes from Tag/Event links between actors -- a button stores the
  tag of the door it fires -- which is what lets 0.4.0's bots solve door puzzles.
  Owns the graph TYPES and the queries over them; ubundle owns their bytes
  (rule 17).
  Blocked-by: reading actor placements.
  **Layman:** Two invisible maps the level already contains: where a player can walk, and which switch opens which door. UT99's own bots never used the second one.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: unav.

- 📋 [UTA-0007] **umap: partition a level into rooms and answer which room a point is in.**
  Built from the level's own BSP zones, so no map needs hand-authoring. Owns the
  simplified room model and the point-in-room lookup; ubundle owns its bytes
  (rule 17), uui draws it (rule 18), and the exploration state that fills it in is
  0.4.0's.
  Blocked-by: reading level geometry.
  **Layman:** Chop the level into rooms so the in-game map has something to draw, using the room divisions the original level already has.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: umap.

- 📋 [UTA-0008] **ubundle: define the .utab container, with its origin field and version.**
  The container for everything shipped as content: a baked map or an authored
  character. Owns the file layout and its version, never the meaning of a
  section's contents.
  Carries the origin field rule 15 requires -- authored when nothing out of
  anybody's UT install contributed, derived when something did, inherited from
  the most restrictive input. Two values, because a bundle has many sources: a
  community map draws on Epic's stock textures, so a per-map judgement would
  call it not-Epic's while its materials are Epic's throughout. Depends on unav and umap for
  their model types, never the reverse.
  **Layman:** Our own file format for a finished level -- and the field that records where its content came from, which is what keeps Epic's material off the network.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ubundle.

- 📋 [UTA-0009] **umat: generate a PBR material from a 1999 texture.**
  Upscale, then derive normal, roughness, height and emissive. The original
  texture's PolyFlags say which surfaces are glass, water, sky or self-lit, so
  those are read rather than guessed.
  Runs at bake time, so slow and high quality is affordable (ADR-0002).
  **Layman:** Turn a flat 1999 texture into a modern one with depth and shine, worked out automatically from the original image.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: umat.

- 📋 [UTA-0010] **umat: the curated material library, shipped with the baker.**
  Keyed by texture name, resolved before the generated fallback. Holds our own
  material definitions and any art we have the right to distribute; lives in the
  repository, not under content/.
  Ships WITH the baker and is versioned with it -- a library that could change
  independently would let two players compute one bundle name for two different
  worlds.
  **Layman:** Hand-made materials for the surfaces you look at most, used in preference to the automatic ones.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: umat.

- 📋 [UTA-0011] **ubake and the ut-bake CLI, including --check.**
  Drives upkg, umat, unav and umap and writes one bundle, content-addressed by
  its source map, its recipe and the baker version.
  --check validates an install, which is what both runtime targets run at startup
  rather than linking the package reader themselves (rule 16). Ships with both.
  Blocked-by: upkg, umat, unav, umap, ubundle.
  **Layman:** The tool that turns an old UT level into one of ours -- and the same tool the game runs to check you actually own Unreal Tournament.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ubake.

- 📋 [UTA-0012] **ut-dump: inspect a package from the command line.**
  Ships to developers, who build it from source. Its command line is a breaking
  surface even so.
  Blocked-by: the container reader.
  **Layman:** A developer tool that prints what is inside a UT file. Unglamorous, and the fastest way to find out why a bake went wrong.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: upkg.

- 📋 [UTA-0013] **The quarantine guard, in .githooks/pre-push and in CI.**
  Runs over TRACKED AND STAGED paths only, never the working tree -- the working
  tree holds the player's own install under content/ut99/, and a guard scanning
  it would block every push.
  Three checks: no tracked path under content/; no tracked path matching the
  Unreal asset extensions; no tracked .utab outside content/ whose origin is not
  authored. The extension list is written ONCE and read by both .gitignore and
  the guard, or the two drift and the guard passes what git was ignoring.
  This is what makes ADR-0003 true in code (rule 15).
  Blocked-by: the .utab origin field.
  Progress (2026-09-04): two of the three checks are live in
  scripts/quarantine-guard.sh, brought forward by UTA-0041 because the
  gate is what has to carry them -- no tracked path under content/, and
  no tracked path matching the quarantined extensions. It reads the
  INDEX, so under the pre-push hook it inspects the commits being pushed
  rather than the working tree, which on this machine holds the player's
  own install. The single extension list is honoured: the guard parses
  it out of .gitignore between two markers, and refuses loudly rather
  than passing if the markers are gone. Still open, and the reason this
  stays planned: the third check -- no tracked .utab outside content/
  whose origin is not authored -- needs the .utab origin field, which
  does not exist yet. Also still open: the guard runs over the
  repository, and UTA-0042's release path needs its own check.
  **Layman:** An automatic check that stops anything of Epic's being committed to the public repository. One careless commit is permanent in a public history.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ci.

- 📋 [UTA-0014] **urender: Vulkan device bring-up and the bundle draw path.**
  Vulkan 1.3. Device, swapchain, and the first draw of a bundle's geometry with
  its PBR materials.
  The level's own light actors -- position, colour, brightness, radius, flicker --
  become real dynamic lights with shadow maps, which is most of the visual upgrade
  and costs nothing per map. Clustered light culling, because a UT map carries
  hundreds.
  Reads uworld and never writes to it (rule 5).
  Blocked-by: ubundle.
  **Layman:** Get a picture on the screen: start the graphics card up and draw a baked level with its lights casting real shadows.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: urender.

- 📋 [UTA-0015] **urender: volumetric fog, light shafts, ambient occlusion and the flashlight.**
  None of these exist in the source map, so they are added at bake time by the
  recipe and drawn here. The flashlight is a spotlight attached to the camera.
  No ray tracing: the target is reached with shadow maps, baked indirect light
  and volumetrics.
  Blocked-by: the bundle draw path.
  **Layman:** The atmosphere -- fog you can see light beams through, soft shadowing in corners, and a torch for the dark parts.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: urender.

- 📋 [UTA-0016] **ut-ants: load a bundle and walk through it.**
  A minimal client: validate the install by running ut-bake --check, load a
  bundle, and fly or walk through it with the renderer's full lighting.
  No movement model yet -- matching UT99's feel is 0.2.0's, and this deliberately
  does not pre-empt it.
  This is what S1 is measured on.
  Blocked-by: the renderer and ut-bake.
  **Layman:** The first thing you can actually run -- open one of your maps and move through it. No guns, no bots, no rules yet.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ugame, uui.

- 📋 [UTA-0040] **urender: parallax occlusion mapping on baked surfaces.**
  POM steps a ray through the height map umat already generates (UTA-0009), so
  the input exists and this is the renderer half.
  Three decisions it must make rather than leave open, because each changes what
  ships: how many steps at what distance (POM is a per-pixel loop and a UT map's
  wall area is large), whether silhouettes are corrected at edges or left flat,
  and whether surfaces self-shadow under the dynamic lights.
  It is a per-surface material property, not a global switch: a 1999 texture with
  no real depth reads worse with POM than without, so the curated library
  (UTA-0010) decides per material and the generated fallback is conservative.
  Blocked-by: the bundle draw path, PBR generation.
  **Layman:** Make flat walls actually look deep. A brick wall stops being a picture of bricks and gains real recesses you can see into as you move past it.
  Kind: implement.
  Source: user-request-2026-09-03.
  Lanes: urender, umat.

- ✅ [UTA-0041] **The CI pipeline, and the local gate that shares its steps.**
  scripts/ci.sh owns the step list; .github/workflows/ci.yml CALLS it and
  duplicates nothing. local-gate.md 3 is explicit that a hand-written mirror the
  workflow does not call is what drifts, and a drifted mirror returns green for a
  pipeline that will fail.
  .githooks/pre-push already delegates to the machine-wide gate; this wires that
  gate to scripts/ci.sh and sets git config ants.gate.docsGlob, which local-gate
  6.1 requires and which an untold hook falls back from into an extension list
  that file forbids.
  Steps: configure, build, run the default test tier -- and the quarantine guard
  (UTA-0013), which is the one check that must run on a public repository before
  every push rather than after it.
  It runs before EVERY push, via the hook, not as a habit somebody remembers.
  A docs-only push runs the DOCUMENTATION checks, not nothing and not a subset
  the gate does not offer -- local-gate 6 calls a blanket skip and a full
  ninety-second run wrong in the same way, the second because that is how a
  person learns to reach for --no-verify. So scripts/ci.sh takes a --docs mode
  and the hook selects it by the paths in the push.
  Three git config keys, and each costs something different if unset:
  ants.gate.command says where the script is, or the repository is reported as
  having no gate at all; ants.gate.docsGlob says which paths count as
  documentation, and untold the hook falls back to an extension list that 6.1
  forbids by name -- so a repository relying on the shared hook without it has
  NOT satisfied the standard; ants.gate.docsMode names our --docs flag.
  Deciding what counts as documentation is by what the PIPELINE READS, never by
  the extension: a doc the suite asserts against is a pipeline input whatever it
  is called, and every uncertain case runs the full gate.
  Blocked-by: the build system.
  Resolved (2026-09-04): scripts/ci.sh owns the steps;
  .github/workflows/ci.yml calls it and duplicates none. The three
  ants.gate.* keys are set, docsGlob among them. Proved rather than
  assumed: a push to a local bare repo ran the gate in a detached
  worktree over the pushed commits and passed; a docs-only push selected
  --docs; a push carrying a broken documentation link was ABORTED and
  the remote tip did not move. Full run about seventeen seconds cold,
  docs mode under a tenth of a second. The gate found a real defect on
  its first run -- a dead case pattern in .githooks/pre-commit -- which
  is fixed. Windows joins Linux as a first-class target at the user's
  direction, so the matrix builds and tests on both; the Windows half is
  unproven until a remote exists for Actions to run on, and that is
  stated rather than claimed.
  Progress (2026-09-04): the Windows leg ran for the first time, on the
  first push to the new public remote, and failed in 12 seconds.
  GitHub's Windows runners check out with core.autocrlf set, so
  yamllint's new-lines rule rejected every workflow file and
  scripts/ci.sh exited before configuring a build. Both Linux legs
  passed. Fixed by adding .gitattributes with `* text=auto eol=lf`;
  renormalising rewrote nothing, so the repository was always LF and
  only the checkout was wrong. This is the first evidence the Windows
  leg works at all -- until this run it was written, lint-clean and
  unexecuted.
  **Layman:** One script that checks the project. GitHub runs it on every push, and the same script runs on your machine before a push -- so a green run here means a green run there.
  Kind: chore.
  Source: user-request-2026-09-03.
  Lanes: ci.

- 📋 [UTA-0043] **Decide how dependencies are acquired, on both platforms.**
  Raised by the design gate and deliberately NOT settled inside it, because
  picking the mechanism is a decision rather than a wording fix.

  The stack table settles acquisition for two entries only: Catch2 is fetched
  by the build, Dear ImGui is vendored. SDL3, glm, shaderc, Assimp and the
  Vulkan SDK have no stated answer. Under the old Linux-primary text a distro
  package was the unspoken default. Windows is now first-class and has no such
  default, so the gap became load-bearing rather than tidy.

  S7 is what makes it urgent: a stranger clones the repository and the build
  and the suite both pass. That is cut at 0.1.0 and it now has to be true on
  two platforms.

  Three candidate answers, and they are not equally good per dependency.
  FetchContent, as Catch2 already uses -- fine for a small header-mostly
  library, poor for the Vulkan SDK. A vcpkg manifest, which is the
  conventional Windows answer and adds a tool. find_package plus written
  install instructions, which is honest on Linux and weakest against S7 on
  Windows. The likely answer is a split by dependency, and the split is what
  needs deciding.

  Whatever is chosen has to hold in three places at once or it has not been
  decided: CMakeLists.txt, .github/workflows/ci.yml, and what a contributor
  does on a fresh clone.

  Blocked-by: nothing. This wants doing before the first dependency lands.
  **Layman:** Decide how the project gets the outside libraries it needs, in a way that works the same on Linux and Windows -- so a newcomer can clone it and build without a shopping list.
  Kind: investigate.
  Source: design-gate-2026-09-04.
  Lanes: core, ci.

- 📋 [UTA-0044] **urender and umat: subsurface scattering on curated materials.**
  Blocked by a design change: docs/design.md lists urender's
  responsibilities and subsurface scattering is not among them, so adding
  it changes what a conformer builds and the design edit runs the rule 14
  gate first.

  The constraint that shapes this: a 1999 texture carries no thickness
  data, and thickness is what SSS needs. It cannot be derived the way
  roughness and normals are (UTA-0009). So the parameters live in umat's
  CURATED library (UTA-0010), which ships with the baker and is versioned
  with it -- our own material definitions opt in, generated ones default
  to none. That also keeps the bake hash honest, since the curated
  library is already a baker input.

  Scope when it is written: a thickness/transmission parameter on a
  curated material, its slot in the bundle's material section, and one
  screen-space or wrap-lighting pass in urender. Note that adding a
  material parameter is a ubundle format change and therefore a baker
  version bump, which invalidates every cached bake (see
  versioning-overrides.md).

  Blocked-by: the design edit, and UTA-0010 for the library it lands in.
  **Layman:** Skin, wax, marble and leaves stop looking like painted plastic -- light passes a little way through them instead of stopping dead at the surface.
  Kind: feature.
  Source: user-request-2026-09-04.
  Lanes: urender, umat.

- 📋 [UTA-0045] **urender: screen-space reflections, weighted by material roughness.**
  Screen-space, per the user's direction: reflections are traced against
  the depth and colour buffers already on hand, not against the world.
  That is deliberate and its limits are known -- anything off-screen or
  behind another surface cannot reflect, and the usual fallback is a
  cubemap or simply nothing. ADR-0001's no-ray-tracing rule makes SSR the
  only option that fits the design rather than one of several.

  Roughness drives it. umat already generates a roughness map (UTA-0009
  lists base colour, normal, roughness, metallic, height and emissive),
  so unlike UTA-0044 this needs no new material channel and no bundle
  format change: a smooth surface gets a sharp ray, a rough one a widened
  cone or a blurred mip, and a fully rough one is not worth tracing.

  Blocked by a design change: docs/design.md lists urender's
  responsibilities -- dynamic lights and shadows, PBR materials,
  volumetrics, light shafts, ambient occlusion, post-processing -- and
  reflections are not among them, so adding them changes what a conformer
  builds and the edit runs the rule 14 gate first. The same gate covers
  UTA-0044, and one pass can carry both.

  Depends on UTA-0014 for the deferred buffers it reads.
  **Layman:** Wet floors, polished metal and glass pick up the room around them -- sharply where the surface is smooth, blurred where it is rough -- instead of being flatly lit.
  Kind: feature.
  Source: user-request-2026-09-04.
  Lanes: urender.

- ✅ [UTA-0046] **core: the Windows path hazards resolveUnder does not yet cover.**
  resolveUnder is the trust boundary unet and ubake bind to, and its
  Linux behaviour is now tested. Three Windows-specific shapes are not
  covered, and none can be checked from Linux:

  Reserved device names -- CON, NUL, AUX, PRN, COM1-COM9, LPT1-LPT9 and
  their extended forms such as CON.txt -- resolve to devices in ANY
  directory, so a remote-named COM1 passes containment and then reads
  from a serial port, which can block indefinitely.

  Win32 strips trailing dots and spaces, so "a." and "a" collide after
  the check passes. And "a.txt:s" writes an alternate data stream that a
  later extension check would not see -- which matters for the quarantine
  guard's extension list.

  Separately: the containment loop compares path elements, and a path
  ending in a separator yields a trailing empty element that
  lexically_normal may preserve. A root written with a trailing slash
  would then be refused. It fails closed, so it is availability rather
  than a hole, but a trust-boundary decision should not rest on behaviour
  that differs between standard library implementations.

  Needs the Windows test machine, which runs binaries but cannot build --
  so this wants either a CI job that runs the check or a binary built by
  CI and executed there.
  Progress (2026-09-04): picked up. Route settled: scripts/ci.sh runs
  ctest on the MSVC leg, so a Windows-only test runs in CI without
  needing the Windows box to build. Taken with UTA-0047 and UTA-0048 as
  one UTA-0002 spec amendment plus three fixes.
  Resolved (2026-09-04): resolveUnder gains a lexical pass over the
  relative path's components -- reserved device names with or without an
  extension, a trailing dot or space, a colon -- running BEFORE any
  filesystem call, and containment now skips an empty trailing element.
  Shipped WIDER than filed: the rules are enforced on every platform,
  not only Windows, because a rule holding on one and not the other
  means a Linux server and a Windows client disagree about which content
  is safe, and unet moves content between exactly those. That also makes
  it checkable on all three legs instead of one, so the Windows box was
  not needed. INV-15 in docs/specs/UTA-0002-core-foundations.md, with an
  order-distinguishing case (COM1/../safe.unr, which canonicalisation
  erases). Green on GCC 14, Clang 19 and MSVC, run e4fedc0.
  **Layman:** Close the Windows-only ways a downloaded file could be named so it lands somewhere it should not, or opens a device instead of a file.
  Kind: security.
  Source: review-code-2026-09-04 filesystem lane.
  Lanes: core.

- ✅ [UTA-0047] **core: make a job's failure observable to whoever waited on it.**
  A job body that throws is contained and logged, and its handle is then
  marked done exactly as a successful one is. So JobHandle::done() is
  true either way, wait() returns normally, and parallelFor reports
  completion for a batch in which every body threw. The only trace is a
  log line.

  That is what the spec says to do, so the code is not in breach -- the
  CONTRACT is what has no failure surface, and changing it is a spec
  amendment before it is a code change.

  Why it matters here rather than in general: ADR-0002 requires one map,
  recipe and baker version to hash to one bundle on any machine, and the
  design requires baking to use jobs. A silently half-failed parallelFor
  is a wrong bundle reported as a good one.

  Shape when it is written: an error count or a failure flag on the
  handle's shared state, and a parallelFor that reports how many bodies
  threw.
  Progress (2026-09-04): picked up alongside UTA-0046 and UTA-0048. The
  contract is what has no failure surface, so the UTA-0002 spec
  amendment comes before the code.
  Resolved (2026-09-04): JobHandle::failed() reports whether the body
  threw, published BEFORE done under the same lock so a waiter woken by
  done sees the outcome with it; parallelFor returns how many bodies
  threw and is nodiscard. The ordering has its own probabilistic test
  spinning on done() from another thread, with the ThreadSanitizer leg
  as the real check -- section 10 marks INV-16 partial for exactly that.
  INV-16. Green on all three legs, run e4fedc0.
  **Layman:** If a piece of background work fails, the code that asked for it should be able to find out, rather than being told everything went fine.
  Kind: enhancement.
  Source: review-code-2026-09-04 job-system lane.
  Lanes: core.

- ✅ [UTA-0048] **core: fileSink says why it could not open its file.**
  fileSink returns a sink that does nothing when the open fails, and the
  caller cannot tell. The failure path is ordinary rather than exotic:
  the log directory does not exist on a first run.

  docs/design.md requires std::expected across every module boundary, and
  this is one. The justification in the code -- that the logger has no
  channel to report its own failure through -- is write()'s reason and
  does not transfer: fileSink is a factory called once at startup by a
  caller that does have a channel.

  So: Result<LogSink>, carrying errno. The spec carries the current
  signature, so the amendment goes through review-contract first.

  Related spec drift found in the same lane, worth settling in one pass:
  Logger::write is declared noexcept in the code and without it in the
  spec, and clearSinks and errorCodeName are public surface the spec's
  class sketches do not mention.
  Progress (2026-09-04): picked up alongside UTA-0046 and UTA-0047.
  Includes the related spec drift the same review lane found --
  Logger::write's noexcept, and clearSinks and errorCodeName missing
  from the spec's class sketches.
  Resolved (2026-09-04): fileSink returns Result<LogSink>, with the code
  chosen by errno rather than one code standing for every cause. The
  errno mapping moved from FileSystem.cpp's anonymous namespace to
  Error.h as errorCodeFromErrno, because the spec requires both fopen
  sites to use one rule and two copies would drift. The related spec
  drift the same lane found is folded in too: errorCodeName, clearSinks
  and Logger::write's noexcept now appear in the class sketches. INV-17.
  Green on all three legs, run e4fedc0.
  **Layman:** If the game cannot open its log file it should say so at startup, instead of running with logging silently switched off.
  Kind: fix.
  Source: review-code-2026-09-04 logger lane.
  Lanes: core.

- 📋 [UTA-0049] **Prove the numeric contract holds across GCC, Clang and MSVC.**
  docs/design.md § What every part does the same way requires floating-point
  contraction and fast-math off with no platform maths library in the
  simulation or the baker, and ADR-0002 requires one map, recipe and baker
  version to hash to one bundle on any machine. Nothing tests either.

  A unit test that computes a fixed set of expressions -- fused-multiply-add
  bait, transcendentals, accumulation order -- and asserts an exact bit
  pattern, run on every matrix leg. Cheap now, because the matrix is green
  and there is almost no arithmetic to disagree about. Expensive at
  UTA-0011, where the whole bake pipeline sits on top of the assumption.

  Wanted before UTA-0011.
  **Layman:** Check that the same sum gives the exact same answer on all three compilers, so a map baked on Linux and on Windows produces one identical file rather than two that disagree.
  Kind: test.
  Source: user-decision-2026-09-04.
  Lanes: core, ci.

## 0.2.0 — Movement and weapons

UT99 movement reproduced by measurement, the core weapon set, gamepad parity and
the weapon wheel, and first-person platforming. Closes S2 and S11.

- 📋 [UTA-0017] **uworld: collision and the UT99 movement model, measured not guessed.**
  Cylinder collision against BSP and against other bodies, and the move semantics:
  walking and falling states, air control, dodge impulse, ground friction, jump
  height, step-up, ledge behaviour.
  Every constant is measured against the original and recorded with how it was
  measured, because S2 is a numbers-matching exercise and a recalled number is
  not a measurement.
  Unreal units, X forward, Y right, Z up -- inherited deliberately so a measured
  constant transfers with no conversion.
  Depends on neither the renderer, audio, the UI nor uinput (rules 4 and 12).
  This is what S2 is measured on.
  **Layman:** Make running, jumping and dodging feel exactly like UT99. This is the part that decides whether the game feels right, so the numbers are measured against the real game rather than estimated.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: uworld.

- 📋 [UTA-0018] **uinput: one action set from keyboard, mouse and gamepad.**
  Devices to actions, with per-device bindings, dead zones and response curves.
  SDL3's controller database already knows a DualShock 4.
  A tick consumes an input COMMAND and has no idea which device produced it
  (rule 12), which is what lets a gamepad, a keyboard and a replayed demo drive
  the same simulation.
  0.2.0's criterion is that it plays as well on a gamepad as on a mouse, and that
  is met by bindings and curves without an aim assist; the assist itself is
  settled at the start of 0.3.0.
  **Layman:** Make a PS4 controller a first-class way to play, not an afterthought -- with sensible dead zones and stick response.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: uinput.

- 📋 [UTA-0019] **The core weapon set, as data.**
  A weapon is a definition: damage, fire rate, projectile, spread, ammo, model,
  sounds. Behaviour that genuinely differs sits behind a small set of firing
  archetypes.
  This is the mechanism per-map weapon sets need at 0.4.0, and the landing place
  ADR-0004 needs for a custom weapon class read out of a package.
  Blocked-by: the movement model.
  **Layman:** The guns. Built as a table of numbers rather than as code, so a different set -- your super weapons -- is a different table rather than a second codebase.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ugame.

- 📋 [UTA-0020] **The weapon wheel, and whether it pauses or slows time.**
  ugame owns what is in the wheel; uui owns how it is drawn (rule 13).
  The pause-or-slow-time question is settled at the START of this release,
  against a prototype, and before the tick is finished -- a time scale the server
  must agree on is an input to the fixed tick, and adding one later would cross
  rules 9 and 12. It becomes an ADR when settled.
  Blocked-by: the core weapon set.
  **Layman:** Pick a gun from a wheel instead of scrolling through them one at a time.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ugame, uui.

- 📋 [UTA-0021] **First-person platforming: the landing marker and the take-off pitch.**
  A marker on the ground where the current jump arc lands, and a small automatic
  downward pitch on take-off so the landing is in frame.
  Both are VISUAL. The pitch offsets the render camera only; the view angles in
  the input command are untouched and the crosshair stays on the aim ray, rising
  up the screen as the camera tips down, so the shot goes where the player
  pointed. That is what makes them free against S2.
  The player's own setting, on by default.
  This is what S11's first half is measured on.
  **Layman:** The reason you fall off ledges in most shooters is that you cannot see your feet. Metroid Prime solved it by showing where you will land -- so we show it too.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: uui, urender.

- 📋 [UTA-0022] **uworld: the physical platforming assists, defaulting off.**
  Coyote time, mantling, and step-up ABOVE UT99's own step-up height. Physical,
  so they live in uworld and are a server setting replicated on join like every
  other rule (rule 14).
  UT99's own step-up height is not one of these. ADR-0001 names it among the
  constants that carry the feel, so it is an always-on fidelity value measured
  in UTA-0017 -- defaulting it off would stop a player at every staircase and
  fail S2 at the defaults S11 is measured at.
  They default OFF everywhere, Monster Hunt included. S11 asks a UT99 player to
  check S2 on the same server the platforming is happening on, so a physical
  assist on by default would fail S11's second half by construction.
  Turning one on is a server operator's deliberate trade, and S11 is measured at
  the defaults.
  Blocked-by: the movement model.
  **Layman:** A small grace period after you step off a ledge, pulling yourself up onto one, and stepping over low obstacles. These change how the body moves, so they are a server's choice and they are off unless someone turns them on.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: uworld.

## 0.3.0 — Monsters, bots and Deathmatch

Monsters resolved by ancestry, combat bots on the maps' own waypoints, and
Deathmatch and Team Deathmatch over a LAN with chat. Closes S3.

- 📋 [UTA-0023] **Resolve a custom actor class onto one of ours by ancestry and defaults.**
  Walk the ancestry read at 0.1.0 until a class we implement natively is reached,
  spawn that, apply the default properties. A hand-maintained global override list
  fixes notorious cases; a per-map recipe override beats it (ADR-0004).
  When resolution lands on a distant ancestor the game says so in its log, or the
  same disappointment is rediscovered by every player independently.
  The global list is NOT a bake input: resolution happens when an actor spawns,
  so the bundle stores each placed actor's class name, ancestry and defaults,
  and this list is game data shipped with the game. Bumping the baker for it
  would invalidate every cached bake on a large rotation to fix one monster.
  Blocked-by: UTA-0005.
  **Layman:** When a Monster Hunt map asks for a monster nobody here has ever heard of, work out what it descends from and what its numbers are, and spawn our version of it configured to match.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ugame.

- 📋 [UTA-0024] **The stock bestiary, as data.**
  Monsters are data on the same footing as weapons: stats, model, sounds, and a
  behaviour archetype. This is what ancestry resolution resolves ONTO.
  This is what S3 is measured on, together with the resolver.
  Blocked-by: the class resolver.
  **Layman:** The monsters themselves -- Skaarj, Titan, Krall, Brute, Warlord and the rest -- built as tables of numbers so a custom variant is a different table.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ugame.

- 📋 [UTA-0025] **uai: navigation and combat bots on the maps' own waypoints.**
  Navigation over the graph unav extracted, an honest aim model with reaction
  time and target leading, weapon choice by range, and combat movement -- dodging,
  strafing, using cover, retreating.
  Depends on uworld and unav, never on urender (rule 6), so a bot cannot know
  anything the server does not simulate.
  The difficulty model -- better decisions versus tighter aim -- is settled at the
  START of this release, against a prototype, and becomes an ADR.
  Aim assist for gamepads is settled here too: this is where there is first
  something to aim at.
  **Layman:** Opponents worth playing against. The levels already contain invisible waypoints the original designers placed, so the hardest part of shooter AI is inherited rather than built.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: uai.

- 📋 [UTA-0026] **unet: transport, prediction and the authoritative server.**
  UDP, authoritative server, client-side prediction and reconciliation for the
  local player, snapshot interpolation for everyone else.
  Prediction is only correct because client and server compile uworld and ugame
  from the same sources with no conditionals (rule 9) -- without it S2 is lost at
  the one moment it is most visible.
  unet includes no game headers (rule 7).
  Blocked-by: the movement model.
  **Layman:** Playing together over the network, without the lag making it feel wrong.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: unet.

- 📋 [UTA-0027] **Deathmatch and Team Deathmatch, with in-game chat and bot count.**
  DM and TDM rules, scoring, spawning, the scoreboard, say and team-say chat.
  A bot count per server, bots filling empty slots, a joining player taking a
  bot's slot and a bot returning when they leave.
  Blocked-by: bots, networking, the weapon set.
  **Layman:** The first real game modes. Set how many players you want, bots fill the empty slots, and a bot drops out when a real person joins -- the way UT does it.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ugame, uui.

## 0.4.0 — Monster Hunt

A real rotation: puzzle-solving bots, map voting with friendly names, mutators
and per-map weapon sets, content download from the host, and the level map.
Closes S4, S5, S9, S10 and S12. This is the release the live server could switch
to.

- 📋 [UTA-0028] **uai: the planner that gets bots through door puzzles.**
  A goal-directed planner over the wiring graph unav extracted at 0.1.0, not a
  fixed behaviour script: the bot holds a goal, finds what blocks it, and works
  backwards to the trigger.
  Team coordination through a shared blackboard, so one bot holds a plate while
  the others pass, and a switch already pressed is not pressed again.
  Stuck detection with a real recovery, and a follow-the-human fallback for
  puzzles the map data does not encode -- shoot-this-panel sequences no designer
  wired to anything.
  Per-map bot hints in the recipe cover what remains, and the server can record
  what it observed a human do.
  This is what S4 is measured on.
  Blocked-by: combat bots, the wiring graph.
  **Layman:** UT99's bots walk into a closed door and stay there. Ours read the level's own wiring, find the switch that opens it, press it, and carry on -- and where a plate must be held, one of them stays behind and holds it.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: uai.

- 📋 [UTA-0029] **Monster Hunt rules, mutators, and per-map weapon sets.**
  The MH ruleset, the mutator mechanism, and named weapon sets selectable per
  map.
  A recipe may say what a map WANTS, because its author knows the map; the
  server's rotation configuration overrides it and is authoritative, and the
  resolved answer is replicated on join (rule 14). A downloaded file must not be
  able to change how a server plays.
  This is what S10 is measured on.
  Blocked-by: the weapon set, the class resolver.
  **Layman:** The mode itself, plus the ability to switch the super weapons on for one map and off for the next without restarting the server or editing either map.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ugame.

- 📋 [UTA-0030] **Content download from the host, and baking ahead of need.**
  The server advertises a manifest of what a map needs with fingerprints; the
  client fetches what it lacks into a per-server cache and bakes it.
  Bundles do not travel. A map is sent as the community's own package plus our
  recipe, and the joining player's machine bakes it against their own install --
  ADR-0003's model, which ADR-0006 did not supersede. An authored bundle is the
  one exception and is sent whole, since there is no install to bake it against.
  A package is classified by the STOCK MANIFEST, not by the bundle origin field:
  origin describes a bundle and what travels for a map is a package, so the two
  questions take two answers. The game ships the names and hashes of the packages
  Epic shipped; unet sends only a package the manifest does not list, and refuses
  one it cannot identify. Failing closed is deliberate -- a package withheld
  costs a player a map, and a package wrongly sent is the ADR-0006 breach.
  Baking happens ahead of need wherever there is warning: the vote settles the
  next map before it starts, and the server browser names a rotation before anyone
  connects. A map nobody has prepared costs a wait, and the player is told so.
  This is what S5 is measured on.
  Blocked-by: networking, ubundle's origin field.
  **Layman:** Join a server and get whatever you are missing -- the map, its monsters, its skins -- automatically, and be playing within a minute.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: unet.

- 📋 [UTA-0031] **The map browser, map voting, and friendlier map names.**
  The between-match browser, the vote, and a name mapping so a map presents as
  what it is rather than as its filename. The mapping is our own data and travels
  with the recipe.
  Blocked-by: Monster Hunt rules.
  **Layman:** Between rounds, see what is coming and vote for it -- with names a human can read instead of MH-CanyonOfDoom][v2-final.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ugame, uui.

- 📋 [UTA-0032] **The level map: where you have been, where your team has been, what is unreached.**
  uworld reports which room of umap's partition each body occupies; the server
  keeps a bit per player per room; uui draws a room as yours, your team's, or
  unreached.
  The whole layout is drawn from the first moment -- the level is somebody else's
  1999 map, not a secret -- so an unreached room is visibly there and visibly
  unreached.
  The server sends a client only its own team's exploration, which in Monster Hunt
  is everyone and in Deathmatch is nobody but you. uui draws what it is given and
  never asks for more, so a modified client cannot reveal what the server withheld
  (rule 18). Without that the level map is a wallhack on any competitive server.
  This is what S12 is measured on.
  Blocked-by: umap, networking.
  **Layman:** An in-game map of the level that fills in as you explore, showing your team's progress and the parts nobody has reached -- and telling you nothing about an opponent on a Deathmatch server.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ugame, uui, unet.

- 📋 [UTA-0033] **A full Monster Hunt round playable on a gamepad.**
  Everything the mode needs reachable from a controller: the weapon wheel, chat,
  the vote, the level map, the scoreboard and the menus. Not just movement and
  firing.
  This is what S9 is measured on, and it is why S9 cuts here rather than at
  0.2.0 -- it asks for a Monster Hunt round, and Monster Hunt exists at this
  release.
  Blocked-by: Monster Hunt rules, the weapon wheel, the level map.
  **Layman:** Play a whole Monster Hunt round on a PS4 controller without touching keyboard or mouse.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: uinput, uui.

## 0.5.0 — Map editor

Edit a baked bundle, build a new level, and author enemies as data. A map built
here is hosted, downloaded and played by someone else.

- 📋 [UTA-0034] **ued: edit a baked bundle and build a new level.**
  Reads and writes bundles and recipes, which is the whole of this milestone and
  the next; ued is the one part allowed to depend on anything (rule 10) and
  nothing may depend on it (rule 11).
  It is a tool, not a runtime target, so rule 2 does not reach it -- but it ships
  to anyone authoring content, which S6 requires.
  Any bundle a tool other than ubake wrote is named by the hash of its own
  contents and the content-tool version ubake and ued share.
  Editing somebody else's map produces a RECIPE, not a bundle -- our changes on
  top of their map, which is small and is already the thing that travels.
  Editing geometry produces an authored bundle, which is a new map rather than an
  edit of theirs and may carry no geometry read out of an install. A derived
  bundle is a local artefact and is never published.
  Blocked-by: ubundle, ubake.
  **Layman:** The map editor. Open a converted level, change it, or build one from nothing -- and save it in our own format.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ued.

- 📋 [UTA-0035] **ued: author a new enemy as data.**
  Stats, model, sounds and a behaviour archetype, on the same footing as the
  stock bestiary. Not a scripting language: the archetypes are the vocabulary.
  Blocked-by: the stock bestiary, the editor.
  **Layman:** Make your own monster -- its stats, its model, how it behaves -- by filling in a form rather than by writing code.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ued.

## 0.6.0 — Character authoring

Import a model, attach it to the animation set, set up skins and attachment
points, and package it so other players download it automatically. Closes S6.

- 📋 [UTA-0036] **ued: import a player model and set it up.**
  Import through Assimp, which ued links and no runtime target does. Attach to
  the shared animation set so a new character walks, dodges and dies without
  animating anything by hand; set skin variants, team colours, portrait, voice and
  weapon attachment points; preview in the editor.
  We are not building a modelling tool. The model is made elsewhere; this is the
  setup and packaging half, which is the part that is actually missing.
  Blocked-by: the editor.
  **Layman:** Bring a character you made in Blender into the game -- attach it to our animations, set up its skins and team colours, and see it before you ship it.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ued.

- 📋 [UTA-0037] **Package a character and have other players download it automatically.**
  A character packages as a .utab like a map -- one container, one version, one
  download path -- with its origin field reading authored.
  This is what S6 is measured on, and S6 is half the 1.0 exit condition.
  Blocked-by: character import, content download.
  **Layman:** Someone other than you builds a character, hosts it, and other players see it correctly without installing anything by hand.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ued, unet.

## 1.0.0 — Good enough to replace the live server

Polish, performance and stability, against the exit condition in
docs/standards/versioning-overrides.md. Closes S8.

- 📋 [UTA-0038] **Run the live Monster Hunt rotation on UT_Ants and keep it running.**
  Not a feature -- a soak. The whole rotation, with bots and humans, over enough
  sessions to find what a short run does not: the map that fails to bake, the
  puzzle no bot solves, the leak that shows up at hour three, the maps whose
  monsters resolve to something disappointing.
  Each failure it finds is its own item; this one is the campaign that finds them.
  This is what S8 is measured on, and S8 with S6 is the 1.0 exit condition.
  Blocked-by: the 0.4.0 and 0.6.0 milestones.
  **Layman:** The real test: switch the actual server over, run the whole actual rotation with actual players, and see whether anyone wants to go back.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ugame, unet.

- 📋 [UTA-0039] **Hold a frame-rate floor across the map library.**
  A UT map carries hundreds of lights and the community maps are not built to a
  budget, so the interesting cases are the outliers rather than the average.
  Measured across a sample of the library on the development machine, with the
  worst maps named rather than averaged away.
  **Layman:** Make sure it runs smoothly on real hardware across the whole map collection, not just on the two maps we kept testing with.
  Kind: perf.
  Source: design-2026-09-03.
  Lanes: urender.

- 📋 [UTA-0042] **The game updates itself, signed and opt-in.**
  Modelled on finbreak, which ships this on Linux and Windows and has the
  post-mortems to prove where it goes wrong. Its spec is the reference:
  /mnt/Games/Scripts/Linux/finbreak/docs/specs/FIBR-0054.md.

  Signature before anything is swapped. An Ed25519 detached signature over
  the exact downloaded bytes, verified against a public key compiled into the
  binary. It fails CLOSED: an unparseable version, an asset the platform
  match does not resolve to exactly one file, or a missing signature is no
  offer at all rather than an unverified install.

  Opt-in, checked at launch, never polled and never silent. The player is
  asked. That is also what makes an outbound connection defensible in a game
  that otherwise makes none.

  The relaunch is the hard part, and it is where the reference project lost
  four separate releases. Three rules come straight off those post-mortems:
  spawn a detached waiter that blocks until the old process has exited rather
  than swapping in place; pass the binary path as an argv element and NEVER
  interpolate it into a shell script, because one apostrophe in an install
  directory bricked an update; and restore the loader environment the bundle
  overrode, or the helper you spawn loads the bundle's libraries and dies
  before it can relaunch. On Windows the running executable is locked, so the
  helper waits on the image PATH rather than a pid -- Windows recycles pids,
  and a onefile build is a parent/child pair -- and moves the file only once
  the process is gone.

  A truncated download is its own error and never a signature failure. Left
  alone, a flaky network reports as tampering, which teaches a player to
  dismiss the one alarm that matters.

  The quarantine still holds. The updater distributes the engine and never
  content: a release asset carrying a map is exactly the breach ADR-0003 and
  rule 15 exist to stop, and UTA-0013's guard runs over the repository rather
  than over a release, so the release path needs its own check.

  Version skew is ours to answer because the protocol is ours (ADR-0005). A
  client and a server on different versions must say so plainly and refuse,
  rather than desync.

  Deliberately NOT in the first cut, each an accepted risk rather than an
  oversight: rollback if the new build does not start, delta downloads, and
  key rotation. A signature proves the build is authentic, not that it runs.

  Two things this item does not settle. Whether the updater is a new part in
  docs/design.md, which would be a design amendment and its own gate. And
  whether 1.0.0 is the right milestone -- it is filed here because that is
  when players other than the author run it, and it moves earlier if a public
  build ships sooner.

  Blocked-by: the release pipeline, and a decision on where the updater lives
  in the design.
  **Layman:** The game can update itself: it notices a new version, asks you first, checks the download really came from us, and restarts into the new one.
  Kind: feature.
  Source: user-request-2026-09-04.
  Lanes: core, ci.

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

- ✅ [UTA-0003] **upkg: read the Unreal Engine 1 package container.**
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
  Progress (2026-09-04): the spec is GATED and accepted, superseding the
  parked note above. review-contract ran both loops of a spec's cap with
  three cold lanes each; sixteen verified findings, all fixed, and the cap
  was calm. Two were settled only by parsing real packages: a class export
  is recognised by a NULL class reference, not by one naming `Class`, and
  a Bool tag's size code is written as 5, so its trailing size byte must
  be consumed. The spec may now be built from.
  One question is open for the user and does NOT block the build: INV-1's
  out-of-span clause is checked by nothing, because the only sanitizer leg
  is ThreadSanitizer and CMakeLists.txt records a deliberate decision not
  to offer AddressSanitizer. Adding a memory-checker leg or a fuzzer is a
  build change a docs gate must not make.
  Progress (2026-09-04): build started. Rule 1 of the priority order was
  checked first and its queue is empty -- the three review-code-2026-09-04
  items and UTA-0049 are all shipped -- so this is picked up under rule 2.
  Tests are written before the code they lock, per languages/cpp.md
  § Tests.
  Resolved (2026-09-04): src/upkg/ builds as uta_upkg linking uta_core
  alone -- ByteReader, Package and the tagged property reader. Tests were
  written first and seen to fail against missing symbols; then each of the
  twelve testable invariants was broken on its own and its named test had
  to redden, with INV-13 proved by adding a second link entry and watching
  configure refuse. That pass found INV-2's test did not check what it
  claimed -- deleting the count check left it green -- so it now asserts
  the refusal names the check. The real-asset tier reads every package in
  the configured install: all but one opened, and Textures/M1.utx is
  genuinely truncated, so the tier makes a failing file prove itself
  truncated from its own header rather than tolerating failures at large.
  That run widens the census too: the UE2-era tail is versions 76, 79, 118
  and 128. Green on GCC 14, Clang 19 and MSVC, run 33895079138.
  INV-1's out-of-span clause is still checked by nothing -- the only
  sanitizer leg is ThreadSanitizer. That question stays open for the user
  and blocks nothing.
  **Layman:** Open a UT file and work out what is inside it -- the index of names and objects. Nothing is drawn yet; this is learning to read the format.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: upkg.

- ✅ [UTA-0004] **upkg: read level geometry, textures, sounds and actor placements.**
  The Model (BSP) geometry, palettised textures with their PolyFlags (masked,
  translucent, unlit, environment -- free information about glass, water, sky and
  lava), sounds, and the actor list with each actor's class, position and
  properties.
  Blocked-by: the container reader.
  Progress (2026-09-05): picked up. Blocker cleared -- the container
  reader shipped as UTA-0003.
  Resolved (2026-09-05): `upkg` reads `Polys` (textured polygons with
  their PolyFlags), `Palette`, the `Texture` family -- `Texture`,
  `WetTexture`, `IceTexture`, `ScriptedTexture` and `FireTexture`,
  including the second block-compressed mip chain -- and `Sound`. The
  actor-list half of `Level` is specified and verified but ships with
  UTA-0057, which carries the rest of that record.

  The design centres on one rule (spec SS 4.3): a reader that models a
  layout correctly ends EXACTLY at its export's end, and anything else is
  `MalformedData` with no partial result. That oracle is total and is
  checkable against content this project did not write, which is what
  UTA-0003 SS 2 says fixtures cannot supply. Against the configured
  install it passes over 552,989 `Polys`, 40,606 `Palette`, 50,386
  Texture-family and 8,898 `Sound` exports, with 39 refusals across two
  community files -- each proven per export, and capped in total, because
  a wrong offset term would push every texture into a named shape and each
  would still prove itself.

  Three things the published documentation does not carry, found by
  decoding the reference install rather than reading about it: stock
  content spans package versions 61 to 69 and two fields exist only from
  63; a texture may store a SECOND compressed mip chain, and missing it
  desynchronises the read rather than losing an extra; and the array order
  the community reference implies for `Model` is wrong.

  `upkg` also gained `readPropertyList`, which reports where the property
  list ends -- every typed reader starts there, and recomputing it per
  reader would have been a second decoder of the one format UTA-0003 owns.

  Green on GCC 14, Clang 19 and MSVC, run b6f8c48. Each new assertion was
  proven able to fail by mutating the rule it covers.

  Split (2026-09-05): `Model` and the post-actor-array half of `Level` move
  to UTA-0057 -- their layouts could not be derived, and the user chose to
  make that work visible rather than leave it inside a nearly-finished
  item.
  **Layman:** Pull the actual level out of the file -- its walls, its textures, and the list of everything the designer placed in it.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: upkg.

- ✅ [UTA-0005] **upkg: read class tables, default properties and ancestry across packages.**
  The class table and defaultproperties, with the parent chain resolved across
  imports so a class in one package can be walked to a base class in another.
  No bytecode is read and no interpreter is written (ADR-0004). This item only
  EXTRACTS; resolving an unknown class onto one of ours is 0.3.0's.
  Blocked-by: the container reader.
  Progress (2026-09-05): picked up by session ut-ants-f8, working in the
  main checkout on branch main. Nothing else was in flight at pick-up;
  the upkg lane is held by this session alone.
  Progress (2026-09-05): spec accepted at
  docs/specs/UTA-0005-class-tables-and-ancestry.md after two
  review-contract loops (22 findings, 22 fixed, cap reached; record in
  docs/reviews/). Implementation not started. Scope grew: reaching a
  class's default properties needs the compiled script walked
  instruction by instruction, because the only length the file stores is
  the size the script occupies in memory, not on disk. The user scoped
  that walker into this item rather than splitting it, since every
  monster inherits from classes that carry one. Nothing is executed.
  scripts/class-census.py re-derives the format findings over the
  reference install and exits non-zero if exact consumption stops
  holding.
  Progress (2026-09-05): session ut-ants-f8 ended here and is no longer
  live, so this marker is resumable by the next session under the
  two-session rule 2. It stays in progress rather than going back to
  planned because the spec is accepted and the item is genuinely
  part-built; nothing is half-written in the tree. Implementation has
  not started: no src/upkg/Script or src/upkg/Class, and none of the
  three new unit test files. Handoff at
  docs/session-handoff-2026-09-05.md.
  Progress (2026-09-06): resumed by session ut-ants-a3 in the main
  checkout on branch main, under two-session rule 2 -- the previous
  holder is not in ListAgents, so the marker was abandoned rather than
  live. Nothing else is in flight; the upkg lane is held by this
  session alone. Starting implementation from the accepted spec.
  Deferred (2026-09-06) to UTA-0043, on the user restating the priority
  order mid-session. UTA-0043's `Source:` records the design gate and
  the item is open, so rule 1 outranks this one -- and the 2026-09-04
  note on UTA-0003 declaring rule 1's queue empty had matched only
  review-code-* provenance, so a document review's item went unseen.
  It stays in progress rather than going back to planned: the spec is
  accepted and there is real work parked, so 📋 would tell another
  session a half-built item is free. Parked in `git stash` on main as
  "UTA-0005 WIP: Script.{h,cpp} walker + readPropertiesAt refactor, no
  tests yet" -- src/upkg/Script.{h,cpp} written whole and
  src/upkg/Properties.{h,cpp} refactored so the two existing entry
  points call a new readPropertiesAt, which is INV-12. Neither is wired
  into the build and neither has a test, which is why it is parked
  rather than committed. Clear this note when the item is resumed.
  Progress (2026-09-06): resumed by session ut-ants-a3. The deferral
  note above is cleared -- UTA-0043 is shipped, so rule 1's queue holds
  nothing this session can implement: UTA-0058 needs the user's answer
  on which map count is true, and UTA-0059 is scheduled for when the
  renderer lands the first dependency. This item is again the only one
  in flight. Restoring the parked work from git stash.
  Resolved (2026-09-06): src/upkg/Script.{h,cpp} walks a compiled script
  instruction by instruction; src/upkg/Class.{h,cpp} reads a class export,
  walks its ancestry across packages through an injected resolver, and
  merges the defaults up the chain. Nothing is executed.

  Properties gained TWO entry points rather than the one the spec
  predicted -- readPropertiesAt, and skipExecutionStackFrame, which a
  class needs too and whose only alternative was a second copy of one
  format shape. Both amendments are folded back into the specs.

  Verified on the reference install: 18,428 class exports across 889
  packages, every one consumed exactly (INV-1); 13,814 ancestry chains
  walked, 13,797 reaching a root and 17 ending incomplete, which is the
  same 17 the independent Python probe reports.

  That pass found a real UTA-0003 defect only class defaults reach: a
  Str's length counts characters and a negative one means 16-bit
  characters, where the reader had refused them as malformed. Fixed and
  amended into UTA-0003 SS 4.8.

  Flipped on the matrix, not a local leg: the run for ae58cf8 is
  completed success on all three legs -- Linux GCC 14, Linux Clang 19
  and Windows MSVC.
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
  Note (2026-09-04): UTA-0052 wants to land first. This item upscales and
  then derives five maps per texture, which multiplies video memory to the
  point where the 2 GB development card decides whether the result runs.
  Block compression and a per-material upscale cap are cheaper to build in
  than to retrofit, because retrofitting regenerates every material.
  Scope note (user, 2026-09-05): upgrading the original 1999 texture is
  the preferred route, and a replacement sourced from the Internet is
  allowed as well. So this item generates from the original where it can,
  and must not assume the original is the only possible input.

  Flagged, not decided: an Internet-sourced texture is third-party content,
  which ADR-0003 (ship the recipe, not the content) and design rule 15
  (everything derived from the player's install lives under content/ and is
  not published from this repository) both bear on. A replacement we do not
  hold the rights to cannot ship with the baker -- that is what UTA-0010's
  curated library is for -- so the likely shape is that a replacement is
  referenced by the recipe and fetched or supplied locally, never committed.
  Whoever picks this up settles that with the user before building it.
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
  Scope note (user, 2026-09-05): see the same note on UTA-0009. The user
  wants Internet-sourced texture replacements permitted alongside upgrades
  of the original. This item is the one that decides what may actually SHIP
  with the baker, because its own headline says the library holds "our own
  material definitions and any art we have the right to distribute" -- so
  the licensing line is this item's to draw, and a replacement without
  distribution rights belongs outside the library rather than in it.
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
  Note (2026-09-04): needs a tier assigned by UTA-0051 rather than a
  default chosen here. True volumetrics are among the most expensive
  things on this roadmap; cheap analytic height fog carries most of the
  atmosphere at a fraction of the cost, so the two probably sit at
  different tiers rather than one being cut.
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
  Note (2026-09-04): a throwaway spike to get crude geometry on screen
  before this item was PROPOSED and DECLINED by the user. Nothing is
  visible until this item lands, fourteen items after UTA-0003, and the
  suggestion was that an early spike would prove the design chain
  sooner. The user's call was to keep the order: the design has already
  been cold-reviewed, and a spike costs days it does not repay if the
  design holds. Do not re-propose it without new evidence that the chain
  is wrong.
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
  Note (2026-09-04): needs a tier assigned by UTA-0051 rather than a
  default chosen here. Parallax occlusion is a per-pixel loop over a UT
  map's large wall area, so it also wants a distance cutoff -- the step
  count question this item already names, answered against the tier rather
  than in isolation.
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

- ✅ [UTA-0043] **Decide how dependencies are acquired, on both platforms.**
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
  Progress (2026-09-06): picked up by session ut-ants-a3 in the main
  checkout on branch main, under rule 1 of the priority order -- this
  item's Source records the design gate, which is a document review,
  and it has been open since. It pre-empts UTA-0005, which this
  session had started and has parked; both markers are held by this
  one session, which is the two-item cap rather than two sessions.
  The core and ci lanes do not overlap the parked upkg work.
  Resolved (2026-09-06): decided and recorded as
  docs/decisions/ADR-0007-acquire-dependencies-by-route.md, with
  docs/design.md's stack table gaining an acquisition column indexing it.
  Fetched at a pinned tag: SDL3, glm, Catch2. Vendored: Dear ImGui.
  Installed and found: the Vulkan headers, loader and glslc at 1.3 or
  newer, with the validation layers a prerequisite the gate does not
  assert. Fetched for one target: Assimp, with ut-ed. The user chose
  this over vcpkg-for-everything, and chose the decision alone over
  decision-plus-wiring, since no dependency has landed and CMake for
  libraries nothing links would be untestable.

  Two of the three candidate mechanisms failed on measurement rather
  than on taste: SDL3 is absent from ubuntu-24.04 entirely, and
  shaderc's own build refuses without three sibling repositories synced
  beside it.

  Three review-contract loops, nineteen findings, nineteen fixed, none
  deferred; record in docs/reviews/. The cap was violent, and the split
  it routes to is filed as UTA-0059 rather than attempted at a cap.

  Flipped on the matrix, not a local leg: run for 499553e is completed
  success, and the pre-push gate ran ci.sh --docs naming the commit it
  gated.
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
  Note (2026-09-04): needs a tier assigned by UTA-0051 rather than a
  default chosen here. Curated materials only, so the cost is bounded by
  how many of them exist -- but on the target laptops it is still a tier
  decision rather than an always-on effect.
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
  Note (2026-09-04): needs a tier assigned by UTA-0051 rather than a
  default chosen here. Screen-space reflections are the most likely of the
  four expensive effects to be off at every tier the target hardware
  reaches, and on 1999 geometry they can read worse than no reflection at
  all -- which is a quality judgement to make when it is built, not now.
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

- ✅ [UTA-0049] **Prove the numeric contract holds across GCC, Clang and MSVC.**
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
  Progress (2026-09-04): picked up under the user's standing priority
  order (review findings before new roadmap items). Scope widened on
  measurement: the contract is not merely untested, it is unenforced --
  CMakeLists.txt sets no floating-point flags at all. Measured here, GCC
  and Clang both fold a*b+c into a single FMA once the target has one,
  and -ffp-contract=off suppresses it; the legs agree today only because
  nothing passes -march. So this item sets the flags and then locks them
  with a test. Transcendental bit patterns are deliberately NOT
  asserted: three libms are not bit-identical and design.md forbids
  depending on any of them, so such an assert would lock in something
  untrue.
  Resolved (2026-09-04): CMakeLists.txt now sets -ffp-contract=off
  -fno-fast-math on GCC and Clang and /fp:precise on MSVC, and refuses
  at configure time if a fast-math flag arrives through CMAKE_CXX_FLAGS.
  tests/unit/NumericContractTest.cpp locks INV-1..6 by exact bit
  pattern. Proven red before green: a worktree at the pre-fix commit
  plus -march=native failed exactly the contraction case, printing
  separate == fused == 0x3c3eb851eb851eb8; the fix returned 73/73. Green
  on all three legs of run 1748777 -- GCC 14, Clang 19 and MSVC. Three
  gaps stated rather than left to be found: INV-1 cannot discriminate at
  the default no-march target because there is no FMA to contract into;
  the configure-time guard has no automated test and was hand-verified
  four ways; and transcendentals are deliberately not asserted by bit
  pattern, since three libms are not bit-identical and design.md forbids
  depending on any of them.
  Note (2026-09-04): this item's Source reads user-decision, though it
  came out of the UTA-0046 code review. CLAUDE.md's priority order,
  written later the same day, defines rule 1's set as the items whose
  Source records a review -- so this item would have been invisible to
  its own priority rule. Left as written rather than corrected: a Source
  records what a past session believed, and rewriting shipped provenance
  to match a rule made afterwards is what makes records untrustworthy.
  Recorded because it is a live example of the trap that rule describes,
  and because the question of whether to correct it was raised with the
  user and not ruled on.
  **Layman:** Check that the same sum gives the exact same answer on all three compilers, so a map baked on Linux and on Windows produces one identical file rather than two that disagree.
  Kind: test.
  Source: user-decision-2026-09-04.
  Lanes: core, ci.

- ✅ [UTA-0050] **Make the build faster on a memory-limited machine.**
  Measured 2026-09-04 on the author's machine (12 cores, ~8 GB free of 31,
  GCC 16.2.0, Ninja); three review lanes were running, so wall times carry
  noise and the ranking is what matters rather than the absolute figures.

  Where the time goes: 108 of the 120 object files in a cold build are
  Catch2, which never changes. The four slowest translation units are all
  Catch2; the project's own slowest is CoreLogTest.cpp.

  What was measured, best payoff first.

  1. ccache with base_dir=/ and hash_dir=false. A second fresh build
     directory went from ~27 s to 1.01 s, 120 of 120 cache hits. At
     ccache's default settings the same experiment hit only 13.75%,
     because the compile line carries build-directory paths -- so the
     config is the whole benefit, not the tool being present. Wire it as
     CMAKE_CXX_COMPILER_LAUNCHER, guarded on ccache being found.

  2. Debug for the edit-test loop. One real source edit rebuilt in 2.43 s
     under Debug against 4.40 s under Release. The gate stays Release.

  3. mold. Relink dropped from 0.12 s to 0.06 s and a full incremental
     from 3.60 s to 3.50 s -- 60 ms, because this project links one small
     static library and one test binary. Worth wiring conditionally now so
     the benefit arrives on its own once urender, SDL3, Assimp and the
     Vulkan SDK land and the link step stops being trivial. Not worth
     claiming as a speed-up today.

  What was measured and does NOT help. ccache does nothing for a real edit
  (4.56 s against 4.62 s without it): the edited file has to be compiled,
  and Ninja rebuilds only what changed. Peak resident size of the largest
  compile is ~364 MB, so twelve parallel jobs sit near 4.4 GB and fit in
  the free memory -- RAM is not the binding constraint, and the job-count
  sweep came back non-monotonic, which is noise rather than a signal.

  Blocked-by: nothing. Independent of UTA-0003.
  Progress (2026-09-04): picked up on the user's decision to wire both
  ccache and mold, each guarded so a machine without them builds
  identically.
  Resolved (2026-09-04): CMakeLists.txt detects ccache and mold with
  find_program and uses each only if present, so a machine without
  either builds identically -- verified by configuring and building with
  both forced to NOTFOUND. CLAUDE.md names the two ccache settings
  (base_dir=/ and hash_dir=false) without which the cache mostly misses
  across build directories. Checked against the two specs governing this
  file: INV-7 of UTA-0049 still holds (the floating-point flags are on
  the real compile line and the refuse guard still fires) and INV-13 of
  UTA-0002 still holds (the ThreadSanitizer build works alongside mold).
  Green on all three legs. COVERAGE LIMIT: CI installs only the
  compilers with --no-install-recommends, so the runners exercise the
  tools-absent path; the ccache and mold paths are verified on this
  machine only. The Debug-for-iteration finding needs no code and is not
  implemented -- it is a choice at configure time.
  **Layman:** Cut the waiting time when rebuilding, especially after wiping the build folder, without needing a bigger machine.
  Kind: perf.
  Source: user-request-2026-09-04.

- 📋 [UTA-0051] **urender: quality tiers and dynamic resolution, so the engine scales to the hardware.**
  The development machine is a GTX 1050 with 2 GB, and the people this is
  built for play on old laptops. Every renderer item after the draw path
  adds cost, and with nowhere to declare that cost each one either ships
  on by default and breaks those machines, or ships off and is never seen.

  Named tiers, where every visual feature declares the tier it switches on
  at rather than owning a toggle of its own. That is the part other items
  bind to, so it wants settling BEFORE UTA-0015, UTA-0040, UTA-0044 and
  UTA-0045 rather than after -- those four are the expensive ones and each
  needs a tier assigned rather than a default invented locally.

  Dynamic resolution driven by a frame-time target, with the sharpening
  pass picking up the difference. Rendering at 70 to 80 per cent and
  sharpening buys more frames on a weak laptop than switching off any
  single effect, and costs less of the look.

  The default tier is detected rather than asked for, and always
  overridable.

  This is the mechanism. UTA-0039 is the measurement that proves it holds,
  and stays where it is.

  Likely needs a spec before code: it is a contract several later items
  bind to, which is spec-format.md section 1's first trigger.

  Blocked-by: the bundle draw path.
  **Layman:** One quality setting that actually works: the game picks a sensible level for your machine, leaves the expensive effects off on weak hardware, and quietly lowers resolution rather than stuttering.
  Kind: implement.
  Source: user-request-2026-09-04.
  Lanes: urender.

- 📋 [UTA-0052] **umat: a texture memory budget, with block compression and a per-material upscale cap.**
  UTA-0009 turns one 1999 texture into five -- albedo, normal, roughness,
  height and emissive -- and upscales before deriving them. Upscaling
  256x256 to 1024x1024 is sixteen times the pixels, so five maps at
  sixteen times is up to eighty times the memory the original texture
  used. The development card has 2 GB and the target laptops have less, so
  this is the constraint that decides whether the result runs at all.

  Two mechanisms, both at bake time and so costing no frames:

  - Block compression -- BC7 for colour, BC5 for two-channel normals.
    Roughly a quarter of the memory, decoded by the GPU for free.
  - An upscale factor decided per material rather than globally. A blurry
    wall texture gains nothing from four times; a hero surface might.

  The budget is per map and measured rather than assumed: the baker
  reports the working set it produced and refuses a bake that exceeds the
  tier's budget.

  Wanted BEFORE UTA-0009. Retrofitting compression means regenerating
  every material and changing what a bundle stores.

  Blocked-by: ubundle.
  **Layman:** Stop the improved textures from filling up the graphics card: squash them properly, and do not blow up a blurry old texture for no benefit.
  Kind: implement.
  Source: user-request-2026-09-04.
  Lanes: umat, ubundle.

- 📋 [UTA-0053] **urender: the cheap post-processing set -- bloom, colour grading, anti-aliasing and sharpening.**
  Four effects that together do most of the visual modernisation and cost
  under a millisecond between them on the development card.

  - Emissive-only bloom at quarter resolution. UT99 is full of glowing
    panels, lava, ammo and muzzle flashes, and the PolyFlags UTA-0009
    already reads say which surfaces are self-lit, so the mask is free.
  - A colour grading lookup table chosen per map at bake time. One texture
    fetch, and the largest single change in how modern the result reads.
  - Anti-aliasing. 1999 geometry is hard edges and thin railings, which
    alias badly. FXAA or SMAA rather than a temporal filter, which needs
    motion vectors and history the first draw path does not have.
  - A sharpening pass. This is what makes rendering below native
    resolution acceptable, so it is the enabler for the quality tiers
    rather than an effect in its own right.

  Blocked-by: the bundle draw path.
  **Layman:** The cheap finishing touches: glowing things glow, each map gets its own colour treatment, edges stop looking jagged, and the picture stays sharp.
  Kind: implement.
  Source: user-request-2026-09-04.
  Lanes: urender.

- 📋 [UTA-0054] **urender: cheap surface detail -- detail normals, dithered alpha, contact shadows and interior windows.**
  Four more sub-millisecond effects, each aimed at one way 1999 content
  reads as old.

  - A single shared tiling detail normal applied close to the camera, so a
    256x256 wall stops looking flat when you stand against it. One extra
    fetch, and negligible memory because every material shares the one
    texture.
  - Dithered alpha for masked surfaces, resolved so it does not shimmer.
    UT99 uses masked textures for every grate, fence and tree, and the
    shimmer is a large part of why they read as cheap.
  - Screen-space contact shadows: a short ray march that grounds an object
    where a shadow map's resolution runs out. Much cheaper than adding a
    cascade.
  - Interior cubemap parallax on windows, so a window is not a flat pane.

  Blocked-by: the bundle draw path.
  **Layman:** Walls look detailed close up, fences stop shimmering, objects stop looking like they float, and windows gain depth.
  Kind: implement.
  Source: user-request-2026-09-04.
  Lanes: urender, umat.

- 📋 [UTA-0055] **urender: vertex-animated banners, flags and water.**
  Movement costs nothing on the GPU and reads as life. UT99 hangs banners
  and flags throughout its maps and its water surfaces are static
  geometry.

  The work is not the vertex shader. It is deciding WHICH surfaces move,
  which is a bake-time tag on the material -- from the original texture
  name and PolyFlags where those say so, and from the curated library
  (UTA-0010) where they do not.

  Blocked-by: the bundle draw path.
  **Layman:** Make the flags and the water move instead of standing still.
  Kind: implement.
  Source: user-request-2026-09-04.
  Lanes: urender, umat.

- 📋 [UTA-0057] **upkg: derive the Model BSP tables and the rest of Level.**
  Split out of UTA-0004 (user, 2026-09-05), which shipped the four readers
  whose layouts could be established: `Polys`, `Palette`, the `Texture`
  family and `Sound`. These two could not.

  What is known, and it is not nothing. A `Model` begins with 41 bytes of
  `FBox` + `FSphere`, carries an object reference to its `Polys`, and ends
  with two `i32`. Between and after those sit runs of compact-index-prefixed
  arrays whose ORDER is what is unknown -- the member set is knowable, and
  the order the community documentation implies is measurably wrong: read
  that way, `DM-Deck16][.unr` yields five nodes and thirty-nine surfaces for
  a `Model` export of over 450 kB. A `Level` is an `i32` count, an `i32`
  capacity, that many actor references and then an `FURL` -- all verified --
  followed by tens of kilobytes this project has not described.

  The method is the one UTA-0004 was researched with, and the acceptance is
  already built: `docs/specs/UTA-0004-typed-level-content.md` SS 4.3 says a
  reader that models a layout correctly ends exactly at its export's end,
  and `tests/real/RealInstallTest.cpp` runs that over the whole install. So
  the layout is right when the real-asset tier consumes every `Model` and
  every `Level` export exactly, and is not right before then.

  Not on the critical path for the items that were blocked behind UTA-0004:
  `unav` needs the actor list and `umat` needs textures, and both shipped.
  `ubake` (UTA-0011) is what actually needs the BSP.

  Blocked-by: nothing. UTA-0004 shipped the container work this rests on.
  **Layman:** Work out the last two file layouts by experiment -- the level's shape, and the tail of the level record -- because nobody has written them down correctly.
  Kind: implement.
  Source: user-decision-2026-09-05.
  Lanes: upkg.

- 📋 [UTA-0058] **Settle the Monster Hunt map count the ADRs cite.**
  The design gate reported this on 2026-09-04 as out of scope for the
  document it was reviewing -- "the ADRs' to settle" -- and it was never
  filed, so it sat outside the priority order entirely. Filed 2026-09-06
  after a rule-1 sweep found it in the review record rather than on the
  roadmap.

  ADR-0002, ADR-0003 and ADR-0004 each cite 610 community Monster Hunt
  maps. docs/discovery.md says 515 community Monster Hunt maps in an
  install of 612. docs/design.md cites no count at all, which is why its
  own gate could not settle this.

  The number is not decoration: S3 and S8 are both measured against "the
  rotation", ADR-0003's quarantine reasoning counts what may be served,
  and UTA-0038 promises to run the live rotation. A figure wrong by
  ninety-five maps is a promise nobody can check.

  Two things need deciding, and only the server's owner can do the first.
  Which figure is true today -- and whether the roadmap's own citations
  should carry a date, since a live server's rotation grows and any bare
  number goes stale the same way this one did.

  An ADR is never edited after it is accepted (docs/decisions/README.md),
  so the repair is not an edit to the three. Either a superseding note, or
  -- more likely and cheaper -- correct docs/discovery.md if that is the
  stale one, and leave the ADRs recording what was believed when they were
  written, which is what they are for.
  **Layman:** Three decision documents say the server has 610 maps and the discovery notes say 515. Somebody has to say which is right, because a promise about "all of them" is measured against that number.
  Kind: doc-fix.
  Source: design-gate-2026-09-04.
  Lanes: docs.

- 📋 [UTA-0059] **Split ADR-0007's operating procedure out of the decision.**
  Routed here by ADR-0007's own review gate, which reached its cap of
  three loops for an ADR. The record is
  docs/reviews/ADR-0007-acquire-dependencies-by-route-loop-log.md.

  The cap was a violent one: the three loops found 5, 6 and 7 findings,
  five of loop 3's seven landed on text the run itself had written, and
  the document grew from 125 to 204 lines. Sibling ADRs run 43 to 64
  lines, so this is over three times the largest.

  The diagnosis is in what kept breaking. The decision half -- the four
  routes, why each, what was rejected -- was settled in loop 1 and never
  found again. Every later finding fell on the operating half: the
  routing question's steps, the observable the gate reads, the split
  between the workflow and the shared gate script, and the prerequisite
  list. That half is a specification wearing an ADR's clothes, and a
  cold read keeps catching it because it is the part with implementation
  detail to get wrong.

  The decision itself is sound and is accepted. Nothing here reopens it,
  and per the gate's own rule the review is not re-run on the document
  as it stands.

  Do this when the first dependency actually lands, which is the
  renderer (UTA-0014). The procedure then has code to attach to and can
  be checked by building rather than by reading, which is the reviewer
  an ADR does not get. Until then a split would move prose between two
  documents and change nothing anyone builds.
  **Layman:** The dependency-acquisition decision document grew three times the size of every other decision in the project, because it also carries the step-by-step procedure. Separate the two when the renderer lands and the procedure has real code to attach to.
  Kind: doc.
  Source: review-contract-2026-09-06 ADR-0007 cap.
  Lanes: docs.

- 📋 [UTA-0065] **Drive a player through any map unattended, and report what happened.**
  A harness that walks a player through a map with no human at the
  keyboard, captures screenshots, writes a machine-readable record of
  what it did and saw, and reports whether the map behaved.

  **It drives the real game, not a model of it.** That is the whole
  design constraint and the one thing that would quietly waste the
  work: a harness that simulates movement against the bundle validates
  something nobody plays, and would pass a map the actual engine falls
  through. It feeds the same input path a player uses -- which is why
  uinput is a lane here -- and reads the same world.

  **Automated, because the target is 610 maps.** A tool needing a person
  per map is a debugging aid, not a test system, and the rotation is the
  scale that matters. That points at two modes rather than one. A
  HEADLESS pass with no rendering, cheap enough to run over the whole
  library: is the exit reachable, does the navigation graph connect, did
  anything error, did the player fall out of the world. And a RENDERED
  pass that captures frames, necessarily slower and run over a sample or
  on demand, because nobody will run a gate that renders 610 maps.

  **Capture must be in-engine.** A desktop screen grab is not a fallback
  for it: under Wayland the X11 tools fail silently or hang against a
  native window, and a full-screen grab captures the user's own desktop
  rather than the game. The engine writes the frame itself, to a named
  path, at a moment the script asked for.

  **The consumer is a session like this one, so the output is for
  reading by machine first.** Structured records that grep and diff --
  position, room, what was reached, what was not, what errored, with
  timings -- alongside images. A GUI report would be the wrong artifact:
  the point is that a run can be compared against the previous run
  without a person in the loop, and that a failure names its own
  location.

  **The criteria are not all written yet, so the check set has to be
  open.** The frame-rate floor is UTA-0039's, the Monster Hunt
  progression rules are UTA-0029's, and neither exists. Building this
  against today's list would mean rebuilding it for each one that
  arrives; it wants a shape where a new check is added rather than the
  harness rewritten.

  **Three items already need this and none of them owns it.** UTA-0039
  holds a frame-rate floor across the map library, which is a
  measurement over every map. UTA-0038 runs the live rotation and keeps
  it running. UTA-0011's --check validates an install. All three assume
  something that can exercise a map unattended, and this is that thing --
  which is the argument for building it early rather than at the point
  the first of them needs it.

  **Reproducibility comes free and is worth protecting.** ADR-0002 and
  the numeric contract already require one machine's results to match
  another's, so a replayed input sequence that diverges across machines
  is a contract failure rather than harness flakiness -- which makes
  this a useful detector for that contract as well as a consumer of it.

  **Where it lives is a real decision, not a detail.** design.md rule 2
  constrains what a runtime target may link, and test scaffolding must
  not end up on a shipped path. A mode of the game binary, a separate
  target, and an external driver process are three different answers
  with different consequences for that rule.

  **What it cannot do, stated so a green run is not over-trusted.** It
  can say a map is traversable, does not error, holds a frame rate and
  opens its progression gates. It cannot say the map is any good.

  Blocked-by: something to drive -- the game loading a bundle and
  walking through it.
  User clarification (2026-09-06), two parts, both about how far this
  has to reach.

  **It must be able to put the game into a STATE, not only move a
  player through a level.** The example given was checking that the
  map-vote window appears when a match ends, which no amount of walking
  reaches. That is filed as UTA-0066, because it cannot exist before
  there are matches to drive and this item is wanted long before then.
  What lands HERE is the constraint: this harness's shape must not
  preclude it. A design that assumes the only input is movement will be
  rewritten rather than extended, which is the same reasoning that keeps
  the check set open.

  **A scripted route is a first-class way to drive the player**, not a
  lesser one, and the item should not assume autonomous navigation. The
  three ways differ in what they are good for and the harness wants more
  than one. An authored route is deterministic and reviewable, so a
  failure is reproducible and a diff against the last run means
  something. A recorded human run is realistic in a way an authored one
  is not, and catches what a straight line misses. Autonomous navigation
  is the only one that scales to maps nobody has scripted, which is most
  of 610. Hand-scripting the rotation is not on; scripting the maps that
  matter most and letting navigation cover the rest is.

  **The standing principle behind both, in the user's own framing:** the
  deliverable is that anything worth checking can be reached and
  observed, and the mechanism is chosen per case rather than fixed in
  advance. That is a reason to keep the driving surface and the check
  surface separate -- a new way to drive should not need a new way to
  report, and a new check should not care how the state was reached.
  **Layman:** A way to send a robot player through a level on its own, take pictures along the way, and write down what it found -- so a map can be checked without somebody playing it, and so the answer is something a machine can read rather than a person's impression.
  Kind: test.
  Source: user-request-2026-09-06.
  Lanes: ugame, ci, uinput.

- 📋 [UTA-0067] **A developer console that takes typed commands, and a script of them.**
  A command surface in the running game: type a command, it happens.
  UT99 has one on the tilde key, so this is an expected shape rather
  than an invention, and players already know to look for it.

  Proposed by the user as the way to reach a game state without playing
  to it -- start a match, then issue a command to finish the map. That
  use is UTA-0066's; this item is the surface those commands arrive on.

  **Filed separately from UTA-0066, and in an earlier release, because
  it pays before matches exist.** Walking a baked map is already the
  point of 0.1.0, and the first things wanted there are a console's
  ordinary fare: put me at that spot, show me the collision, tell me
  which room this is, take a screenshot. Tying the console to
  match-state control would hold it until 0.3.0 and leave the earlier
  work with no way to ask the engine anything.

  **The authority question is the real design constraint, not the
  parser.** This engine has an authoritative server. A console command
  that changes the world must be EXECUTED by the server, with the client
  console only a way to ask -- a console that mutates client state
  directly produces a client that disagrees with the server, which is
  the classic bug this shape invites and the hardest kind to
  diagnose later.

  **So the commands fall into two classes and they are not alike.**
  Local ones change only what this client shows -- a debug view, a
  screenshot, a stat readout -- and need nobody's permission. World ones
  change the game and must be asked of the server, refused by default,
  and gated behind something a player cannot present. UTA-0066 records
  why: "end the round now" on a server meant to run unattended is a
  grief vector, and the gating belongs with the command surface rather
  than with each command.

  **It has to be usable without a keyboard.** A session driving this
  reads and writes text, not a screen, so commands want to arrive from a
  file, from standard input or over a local socket as well as from the
  tilde key -- and each command should say what it did in a form that
  can be read back. That is what makes UTA-0065's harness able to use
  this rather than needing a second control path, and a `screenshot`
  command is the obvious trigger for the in-engine capture that item
  requires.

  **What ships is a decision, not an afterthought.** UT99 shipped its
  console and the game is better for it; the question is which commands
  survive into a release build and which are gated out, and it is
  cheaper to answer while the classes above are being drawn than to
  audit a command list later.

  Blocked-by: something to run the commands against -- the game loading
  a bundle.
  User decision (2026-09-06), corrected the same day: there are TWO
  consoles and only one of them is this item's. A NORMAL console, the
  kind UT99 ships, stays available to players. A DEV console -- the
  state-driving commands this bullet is about -- is for testing only and
  must not be available to players after 1.0.0. Until then testers use
  it, which is the whole reason it exists.

  That settles the "what ships is a decision" line above: the surface
  ships, the dev commands do not. An earlier version of this note said
  the answer was "none of it", which would have taken the player console
  with it; the distinction is recorded rather than quietly rewritten
  because it changes what somebody builds.

  **Compile it out rather than hide it.** A console behind an
  undocumented key, a hidden flag or a stripped menu entry is still in
  the binary, and this is a community that has been finding things in
  UT99 binaries for twenty-five years. "Not available to players" means
  the dev COMMANDS are not in the build they run -- the console itself
  stays, carrying the player commands. That also collapses the authority
  problem this bullet spends most of its length on for the shipped
  build: a command that does not exist cannot be gated wrongly. It does
  NOT collapse it before 1.0.0, or for the player commands, which still
  need the local-versus-world split above.

  **The cost is real and is worth stating rather than discovering.** If
  the console is absent from release builds, then UTA-0065 and UTA-0066
  drive a binary players do not run, and a test suite that only
  exercises the dev build is testing something nobody plays. Two things
  keep that honest. The difference between the two builds should be
  exactly this one thing, so nothing else diverges under cover of it.
  And whatever the harness can do through a path a player also has --
  scripted input, which is just keys -- should be run against the
  RELEASE build too, so the shipped artifact is exercised rather than
  assumed.

  **Before 1.0.0 it is available, which is the point.** Every release up
  to it is where this earns its keep, and the deadline is what stops
  "we'll strip it later" becoming "it shipped".

  **The server case does not go away.** A dedicated server may run a dev
  build during development, and UTA-0038's runs unattended, so the
  refuse-by-default gating on world commands is still needed for the
  period the console exists rather than being made moot by its eventual
  removal.

  The check that this actually happened is filed against 1.0.0, where it
  bites, rather than left in this bullet -- an obligation recorded only
  in a 0.1.0 item shipped long before the deadline is one nobody is
  reading on the day.
  **Layman:** The drop-down command box UT99 has on the tilde key. Type a command and something happens -- jump to a spot, show a debug view, take a screenshot, end the match. It is how a test reaches a situation directly instead of playing until it happens.
  Kind: implement.
  Source: user-request-2026-09-06.
  Lanes: uui, ugame, unet.

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

- 📋 [UTA-0056] **uaudio: the mixer, with a priority rule that keeps speech audible.**
  `uaudio` has no item until this one: the design names the part
  (`docs/design.md` § The parts -- "Sound playback, positional mixing,
  music") and nothing in the queue built it. Filed because a requirement
  arrived that needs a home.

  The requirement (user, 2026-09-05): UT99 lets a loud weapon drown out the
  announcer -- the super flak cannon is the case named -- and this engine
  must not. So the mixer is not a free-for-all where the loudest source
  wins: categories carry a priority, and a higher-priority category
  ducks the ones below it rather than competing with them. Speech --
  announcer, warnings, objectives -- is the category that must stay
  intelligible while anything else is playing.

  What is NOT decided here and is this item's to settle: which categories
  exist, whether ducking is per-category gain or a compressor keyed on the
  speech bus, how fast it recovers, and whether the player can turn it
  off. What IS decided is that "it got quieter because something loud
  happened" is a defect and not a mixing accident.

  Measurable, so it is not a matter of opinion: with the loudest weapon in
  the game firing continuously, an announcer line is still audible.
  Whoever builds this states the measurement -- a level difference on the
  speech bus is the obvious one -- so the rule can be regression-tested
  rather than re-argued.

  Blocked-by: the core weapon set, which is what produces a sound loud
  enough to test against.
  **Layman:** Make sure the important sounds -- the announcer, warnings -- are still audible when a loud weapon is firing, instead of being buried.
  Kind: implement.
  Source: user-request-2026-09-05.
  Lanes: uaudio.

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
  User request (2026-09-06): bots should have the intelligence to try
  and dodge enemy fire. Recorded here rather than filed separately
  because this item already names combat movement -- dodging, strafing,
  cover, retreating -- and two owners for one job is how the two
  disagree later.

  What the request sharpens is the trigger. "Dodging" as written could
  be read as movement that merely happens to be evasive: strafing while
  firing, which UT99 bots do and which is cheap. What was asked for is
  reactive -- seeing a projectile or a shot coming and getting out of
  its way. That is a different input, and on this engine it is available:
  the server simulates the projectile, so a bot can be given the fact
  without the renderer (rule 6) and without cheating any more than a
  human does by watching a rocket.

  Worth settling with the difficulty model this item already defers to a
  prototype, since perfect evasion is exactly the kind of tighter-aim
  substitute that makes a bot unpleasant rather than good.
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

- 📋 [UTA-0062] **Bots get out of the way when a player walks into them.**
  A bot that a player is pushing against should recognise it is blocking
  and move, rather than holding its ground while the player slides off
  its collision cylinder.

  Nothing else owns this. UTA-0025 is combat -- aim, weapon choice, and
  movement while fighting something. This is the opposite situation:
  nobody is shooting, the bot is doing something reasonable, and it
  happens to be standing where a teammate needs to be. It is a small
  behaviour with a loud failure mode, which is why it is filed rather
  than assumed.

  Where it bites hardest is doorways, lift platforms and the top of
  ladders -- anywhere the level is one body wide. A Monster Hunt team
  moving through a corridor is the case to test against, not an open
  arena, because in the open the player simply walks around.

  The first decision is which subsystem answers, and the two are not
  equivalent. It can be uai: the bot notices sustained contact from
  behind or from a teammate and steps aside deliberately, which reads as
  courtesy and can be made to look natural. Or it can be uworld: player
  and bot displace each other physically, which is simpler, needs no
  intelligence at all, and cannot distinguish a player asking to pass
  from a monster shoving. UT99 answers neither way, which is why its
  bots feel like furniture. Lanes are both until that is settled.

  What has to be true either way: a bot that yields must not yield off a
  ledge, into lava, or out of a fight it was holding a position in --
  and it has to stop yielding once the player has passed, or two bots in
  a corridor push each other along it indefinitely.

  Blocked-by: bots existing at all.
  **Layman:** In UT99 a bot standing in a doorway is a wall. You push, it does not move, and you go the long way round. Ours should notice it is in your way and step aside.
  Kind: implement.
  Source: user-request-2026-09-06.
  Lanes: uai, uworld.

- 📋 [UTA-0066] **Put a match into any state a test needs, without playing it out.**
  The harness in UTA-0065 drives a PLAYER. This drives the MATCH: set
  the score, wind the clock, remove the remaining monsters, end the
  round -- so a test can reach a state directly instead of playing its
  way there.

  The case that prompted it: checking the map-vote window appears when a
  match ends. Reaching that by play means a full match every time, which
  is slow enough that nobody runs it and flaky enough that a failure is
  not trusted. There are also states play cannot reach on demand at all
  -- a rare rule branch, a tie, a timeout with one player left.

  **The trap, and it decides the whole design. Force the INPUTS to the
  rules, never the outcome.** A control that simply shows the vote
  window tests the window; a control that sets the score to the limit
  and lets the real end-of-match path run tests the thing that actually
  breaks. The second finds a match that ends without ever offering a
  vote; the first passes while it does. So the surface should reach the
  same values the rules already read -- score, time remaining, players
  alive, monsters remaining -- and let the rules react, rather than
  letting a test assert a state the running game can never produce.

  **It must not be reachable on a live server.** An "end the round now"
  control is a grief vector on a server this project intends to run
  unattended, and the server is authoritative by design, so a client
  must not be able to ask for this at all. Build-gated, or gated behind
  something a player cannot present. That is a decision to make
  deliberately rather than a flag to add and tidy later.

  **A scenario should be describable and replayable**, so a failure can
  be handed to somebody else as the state it happened in rather than as
  a sequence of things to do. That pairs with UTA-0065's machine-first
  output: the record of a run says which scenario produced it.

  **The set of scenarios stays open**, for the same reason UTA-0065's
  check set does -- the rules it will be asked to drive belong to items
  that do not exist yet, so this wants a shape where a new scenario is
  added rather than the surface rewritten.

  Filed here rather than folded into UTA-0065 because the two are
  blocked by different things and land in different releases: that one
  needs only a bundle to walk and is wanted early, this one cannot exist
  before there are matches to put into a state. UTA-0065 carries a note
  saying its shape must not preclude this.

  Blocked-by: a match to drive -- the game rules and their lifecycle.
  User proposal (2026-09-06): expose this through a developer console --
  start a match, then type a command to finish the map, so no run
  through the level is needed to reach the end-of-match state.

  That is the right shape and it is now UTA-0067, filed in 0.1.0
  because a console pays before matches exist: walking a baked map
  wants one immediately for putting the camera somewhere, showing a
  debug view and triggering a screenshot. Holding it here would delay
  it to 0.3.0 and leave the earlier work with no way to ask the engine
  anything.

  What that leaves THIS item is the commands rather than the surface --
  which values the rules read, what each one does to a running match,
  and which of them a server must refuse. The console carries the
  gating, since a per-command answer to "may a player do this" is how
  one of them ends up ungated.

  It does not change this item's central rule. A console command that
  sets the score to the limit and lets the real end-of-match path run
  is the one worth having; a command that opens the vote window
  directly tests the window and would pass a match that ends without
  ever offering one.
  **Layman:** So a test can jump straight to the interesting moment -- the end of a match, a nearly-empty level, a team one point behind -- instead of playing for twenty minutes to get there. That is how you check something like the map-vote screen actually appears when a match finishes.
  Kind: test.
  Source: user-request-2026-09-06.
  Lanes: ugame, unet, ci.

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
  User request (2026-09-06): bots should navigate the map by using
  buttons to open doors, lifts and portals. Recorded here rather than
  filed separately because this item is that planner -- hold a goal,
  find what blocks it, work backwards to the trigger.

  Two of the three named are not spelled out in the text above, and both
  are worth naming because they behave differently from a door. A LIFT
  is not an obstacle to be removed but a vehicle to be ridden: the bot
  has to call it, wait, board it, and stay on it while it moves, and
  "the way is now open" is the wrong model for it. A PORTAL moves the
  bot somewhere the path network may not connect to where it started, so
  a route that treats it as an ordinary edge has to know its exit
  before committing to it.

  Both are things unav's wiring graph should be able to say, so the ask
  lands mostly on what UTA-0006 extracts rather than on this planner --
  worth checking when that item is specced, while it is still cheap.
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
  Scope addition (user, 2026-09-05): the vote is not only between
  matches. A player stuck on a map they dislike can open the voting window
  DURING a match, and that path needs a consent step the between-match
  vote does not: the other players are first asked whether to vote at all
  -- "agree, let's vote" against "stay on this map" -- and the map vote
  itself only opens if that carries. So there are two polls, not one, and
  the first exists so a single player cannot pull everyone else out of a
  round in progress. With no other players present the consent step has
  nobody to ask and the map vote opens directly. What carries the consent
  step, and whether it is a majority or a threshold, is undecided and is
  this item's to settle.
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

- 📋 [UTA-0060] **uai: route across the whole level, not just to the next waypoint.**
  A routing layer over the graph unav extracts: given a destination
  anywhere in the level, produce a route to it and follow it, rather than
  stepping to whichever waypoint comes next.

  This is NOT what UTA-0025 delivers, and the difference is the point.
  That item navigates the maps' own waypoints, which is the right
  foundation and is why shooter AI is largely inherited here rather than
  built. But author-placed waypoints are a local instruction -- go here
  next -- and a community map's coverage is whatever its author bothered
  with. A bot that only follows them cannot answer "how do I get from
  this room to that one", and on a large Monster Hunt level that is the
  question it needs answered most.

  What this has to add over UTA-0025. A route to an arbitrary point
  rather than to the next node. Knowing when the graph does not reach
  the destination, and saying so, rather than walking into geometry.
  Re-routing when the level changes under it -- a door closes, a lift
  moves, a bridge is destroyed. And some answer for the gaps, because a
  sparse or broken waypoint set is the normal case in community content
  rather than the exception.

  Open, and worth settling against a prototype rather than in advance:
  whether the route is computed over the waypoint graph alone, or over
  something coarser derived from the level's own rooms -- UTA-0007
  partitions a level into rooms and answers which room a point is in,
  which is close to the shape a route planner wants and may already be
  most of the answer.

  Blocked-by: the navigation graph, and combat bots to route for.
  **Layman:** Give bots something like satnav. Instead of only following the breadcrumb trail the 1999 designer laid down, a bot works out where it is, where it needs to be, and a route between them -- so it can still get somewhere when the breadcrumbs run out.
  Kind: implement.
  Source: user-request-2026-09-06.
  Lanes: uai.

- 📋 [UTA-0061] **uai: hunt down the last enemies when a level gates on killing them all.**
  On a level whose progression gate is "kill everything", a bot should
  actively seek the survivors rather than wait to be shot at.

  This is not combat AI and UTA-0025 does not cover it. That item makes a
  bot good at fighting something it can already see. This one is about
  the minutes AFTER the fight, when the gate has not opened and nobody
  knows why -- which is the single most common way a Monster Hunt round
  stops being fun, because the remaining monster is usually stuck on
  scenery, asleep in an unvisited corner, or somewhere the path network
  never went.

  What it needs. Knowing that this level HAS such a gate and that it is
  unmet, which is a question for the Monster Hunt rules rather than for
  the bot. Some memory of where the level has and has not been searched,
  which is close to what the level-map item already tracks for players.
  A search that prefers where a monster plausibly is over sweeping the
  whole level. And an honest end: when the sweep finds nothing, say so,
  because a monster the map has made unreachable is a map bug and
  pretending otherwise leaves a round that can never finish.

  Deliberately not scoped here: what the server does about an
  unreachable monster. Forgiving the gate, respawning the monster and
  letting the round stall are three different answers with different
  consequences for a rotation that has to run unattended, and that is a
  decision rather than an implementation detail.

  Blocked-by: the Monster Hunt rules, which own what the gate is.
  **Layman:** Some levels do not let you move on until every monster is dead. Right now that ends with everyone wandering the map for ten minutes looking for one monster stuck behind a crate. Bots should go and find it.
  Kind: implement.
  Source: user-request-2026-09-06.
  Lanes: uai.

- 📋 [UTA-0063] **Bake a hidden passage into a visible opening with a frame.**
  A 1999 map hides a passage by putting a surface over the opening that
  LOOKS like the wall around it and does not block movement. The baker
  should recognise those and emit a visible opening with a frame instead
  of a wall you have to know about.

  This is a deliberate departure from what the original author built,
  and the position is already the project's rather than new: design.md
  says of the level map that "the level is somebody else's 1999 map, not
  a secret we are keeping", and draws the whole layout from the first
  moment. This is that same stance applied to the geometry instead of
  to the map screen.

  **The decision to settle first, because it changes what gets built.**
  Not every hidden surface hides the way forward. Some hide a stash of
  ammo, which is a reward rather than an obstruction, and turning every
  one of them into a signposted doorway strips something worth keeping.
  The request was about the way FORWARD, so the two cases want telling
  apart -- and there is a computable test rather than a judgement:
  a hidden passage gates progression when removing it leaves the
  level's exit unreachable from its start. The navigation graph already
  answers reachability, so the baker can ask. Recommended default is to
  open the ones that gate and leave the rest, with the recipe able to
  override per map either way, because a heuristic run over hundreds of
  community maps will be wrong somewhere.

  **What must not be caught by it.** A non-blocking surface is not the
  signal on its own -- waterfalls, force fields, fog sheets and steam
  are all walk-through and all meant to be seen. The signal is a
  walk-through surface that is visually CONTINUOUS with the blocking
  wall around it: same or matching texture, aligned, coplanar. A
  detector built on "does not block" alone would delete every waterfall
  in the rotation.

  **The frame is invented geometry, and that is new for the baker.**
  Everything ubake does today converts what it read; this synthesises
  something the source does not contain, and it has to sit correctly in
  the surrounding wall and take a material that belongs there. A frame
  that reads as a mistake is worse than the fake wall, because the fake
  wall at least looked deliberate. Whether the opening is framed, or
  merely lit and unobstructed, is worth deciding against a prototype in
  a real map rather than in advance.

  Blocked-by: the baker and the geometry it emits, and the navigation
  graph for the reachability test.
  **Layman:** Some levels hide the way on behind a patch of wall you can simply walk through, with nothing to tell you it is there. We are not interested in making players guess, so the baker should find those and turn them into openings you can see, framed like a doorway.
  Kind: implement.
  Source: user-request-2026-09-06.
  Lanes: ubake, umat, unav.

- 📋 [UTA-0064] **An on-demand route indicator to the next thing that advances the map.**
  Press a key, and the game shows the route to whatever advances the
  level from where you are standing: the switch that opens the door
  ahead, the lift, the portal, the last enemy on a level that gates on
  killing them all, or the exit.

  **The engine already computes this answer for the bots, and it must
  not be computed twice.** UTA-0028's planner holds a goal, finds what
  blocks it and works backwards to the trigger. UTA-0060 turns a
  destination into a route. Between them that IS "what do I do next and
  how do I get there" -- so this item is that answer rendered for a
  human, and a second planner written for the player is the shape this
  project keeps refusing. What is genuinely new here is the presentation
  and the moment it appears, not the reasoning behind it.

  **The stance is already the project's.** design.md's movement-assist
  bullet holds that an assist is either visual or physical, that the two
  are built differently, and that the good answer is mostly SHOWING
  rather than CHANGING. This is a pure showing assist: it moves nothing,
  opens nothing and changes no simulation, which is what keeps it on the
  right side of that line. It sits with the level map, which already
  draws the whole layout on the same reasoning -- the level is somebody
  else's 1999 map, not a secret being kept.

  **On demand, and deliberately not a permanent HUD line.** The request
  was a button, and the constraint is worth keeping rather than
  softening later: a route always on screen removes the exploration the
  level map is built to make legible, and turns a Monster Hunt level
  into a corridor. A moment's help when someone is lost is a different
  thing from a rail.

  **It has to be honest when it has no answer.** A planner that cannot
  find a route -- a broken map, a monster the level has made
  unreachable, a trigger nothing wired up -- must say so rather than
  point at a wall, which is the same requirement UTA-0061 carries for
  the same reason: a confident wrong direction costs more than an
  admitted gap.

  **In co-op it follows the level map's visibility rule**, rather than
  inventing a second answer to the same question about what a team may
  share.

  Open, and for a prototype in a real level: whether the route is drawn
  on the floor as Dead Space does it, or as a marker on the objective, a
  compass, or a path on the level map. The floor line is the one that
  was asked for and is the one to try first; the others are cheap to
  compare once the routing exists.

  Blocked-by: the bot planner and the routing it uses, and uui to draw it.
  **Layman:** Dead Space has a button that draws a line on the floor to where you need to go next. Ours should do the same -- point you at the switch, the portal, the door or the last monster standing between you and the way on -- when you ask for it, and not before.
  Kind: implement.
  Source: user-request-2026-09-06.
  Lanes: uui, uai, unav.

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
  Constraint (user, 2026-09-05): the editor's interface must be
  user-friendly to the point of being idiot-proof, and the user is the
  reader it must work for -- someone who does not program. That is a
  requirement on this item, not a polish pass afterwards: it decides the
  defaults, how much is hidden, and whether a wrong action is possible at
  all rather than merely undoable.
  **Layman:** The map editor. Open a converted level, change it, or build one from nothing -- and save it in our own format.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ued.

- 📋 [UTA-0035] **ued: author a new enemy as data.**
  Stats, model, sounds and a behaviour archetype, on the same footing as the
  stock bestiary. Not a scripting language: the archetypes are the vocabulary.
  Blocked-by: the stock bestiary, the editor.
  Constraint (user, 2026-09-05): authoring an enemy must be usable by a
  non-programmer -- see the same note on UTA-0034. Data authoring is where
  a form of raw fields is most tempting and least usable.
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
  Constraint (user, 2026-09-05): model import and setup must be usable by a
  non-programmer -- see the same note on UTA-0034. Import is the step most
  likely to fail with a message only its author understands.
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

- 📋 [UTA-0068] **Prove the developer console is absent from the shipped build.**
  UTA-0067's console is a testing tool, and the user's decision is that
  it is not available to players after 1.0.0. This is the check that it
  is not, run before the release goes out.

  **Why it is an item rather than a line in that bullet.** UTA-0067 is a
  0.1.0 item and the obligation bites at 1.0.0, so recording it only
  there means it sits in a bullet nobody re-reads on the day. And an
  intention with no observable is indistinguishable from having
  forgotten: a build that still carries the console and a build that
  does not look identical from the outside, which is exactly the
  condition that ships one.

  **It checks the ARTIFACT, not the source.** A guard around the source
  is what is supposed to work; this is the check that it did. So it
  reads the built binary players receive and fails the release rather
  than reporting.

  **What it looks for is the DEV commands, not the console.** The normal
  console the player uses stays -- UT99 ships one and so do we, which is
  the user's correction of 2026-09-06. So the check cannot simply assert
  that no console exists, and a check written that way would fail a
  correct build while a build carrying every dev command but no player
  console would pass it. What must be absent is the state-driving set:
  the commands that end a match, set a score, move a body or spawn
  something, and any socket or stdin path that accepts them.

  **It also checks the dev build still HAS one**, which sounds redundant
  and is not: a check that only looks for absence passes just as well
  when the console was accidentally removed from both builds, and the
  first anyone would know is a test session with no way to drive
  anything.

  Deliberately not scoped here: whether other development surfaces
  follow the same rule. This is about the console the user named. A
  blanket sweep for dev tooling in release builds is a different and
  larger job, and inventing it under this item's name would be scope
  that nobody asked for.

  Blocked-by: the console existing, and a release build to check.
  **Layman:** A check that the testing console really is gone from what players download, rather than a note saying we meant to remove it.
  Kind: test.
  Source: user-decision-2026-09-06.
  Lanes: ci, ugame.

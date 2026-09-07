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

- ✅ [UTA-0006] **unav: extract the navigation graph and the event-wiring graph.**
  The navigation graph comes from the PathNodes the level designer placed. The
  wiring graph comes from Tag/Event links between actors -- a button stores the
  tag of the door it fires -- which is what lets 0.4.0's bots solve door puzzles.
  Owns the graph TYPES and the queries over them; ubundle owns their bytes
  (rule 17).
  The nodes are readable today; the edges are not. A `NavigationPoint`
  serialises `Paths[16]` as integers, and those are indices into the
  `Level` tail's ReachSpec array, which UTA-0057 derives. Without it this
  item extracts nodes with no connectivity, no collision radius and no
  height. Confirm that indexing before scheduling the work -- it is
  UTA-0057's to verify and this item's to depend on.

  Blocked-by: reading actor placements; UTA-0057 for the ReachSpec array.
  Unblocked (2026-09-06): UTA-0057 shipped the ReachSpec array, and the
  indexing this item was told to confirm before scheduling is confirmed.

  Measured over the whole reference install: 99.96% of non-empty Paths
  values are valid indices into the level's reach-spec array, and 99.54%
  name the listing node as the spec's start. Every entry answers both on
  816 of the 831 maps carrying one. The residue is fifteen maps whose
  path network disagrees with their own navigation points -- eight of
  them carry Paths values with no reach-spec array at all -- so this item
  must expect a node whose edges do not resolve and must not treat that
  as a reader fault. UTA-0057 SS 4.6a has the decomposition.

  What upkg hands over: reachSpecs in file order with file indexing, each
  carrying distance, start, end, collision radius, collision height,
  reach flags and a pruned byte. Start and end are object references to
  ACTORS, not node indices, so joining them to the node set needs the
  export table rather than a position in the actor array.

  Two things this item still owns. The graph types and the queries are
  this item's, not upkg's -- UTA-0057 INV-5 keeps a resolved graph out of
  the reader deliberately. And the collision radius and height are
  returned but graded by no invariant anywhere; UTA-0057 SS 10 records
  that finding an independent source for them is this item's.

  Blocked-by now reads: reading actor placements.
  Progress (2026-09-06): picked up by session ut-ants-64, in the main
  checkout. Taken as the Next: item now that UTA-0057 has unblocked it.
  First step is the spec-format SS 1 decision, not code.
  Progress (2026-09-06): spec accepted at
  docs/specs/UTA-0006-navigation-and-wiring-graphs.md. Two review-contract
  loops, three cold lanes each, fifteen verified findings all fixed, cap
  reached. Status stays in-progress: the spec is the contract, not the work.

  Two things the research settled that the bullet above had only believed.
  The wiring graph IS Event -> Tag: 94.9% of events in the reference
  install name a Tag some actor in the same map carries, it is many-to-many
  in both directions, and the remaining 5.1% dangle. And class-default Tags
  do NOT explain the dangle -- almost none match -- so the wiring half does
  not need UTA-0005's effectiveDefaults and reads an actor's own property
  list only. Exact matching was checked against case-insensitive: identical
  results, none recovered, so the spec's exact rule stands.

  Two measurements the spec now rests on, both new: 99.87% of reach-spec
  endpoints resolve to a NavigationPoint descendant, and 0.12% of endpoints
  are null with none an import.

  What the gate changed about the item, beyond the document. The subsystem
  ships TWO libraries, not one: docs/design.md rule 2 forbids either runtime
  target linking upkg while rule 6 makes uai depend on unav, so a single
  uta_unav linking uta_upkg would have pulled the package reader into the
  game binary through this item. A graph node therefore carries a bare
  export index rather than an ObjectReference.

  Known weakness, recorded rather than solved: two index spaces exist -- an
  export index and a node position -- both std::uint32_t, so a consumer
  passing the wrong one compiles and gets another actor's edges. nodeOf is
  the declared bridge; nothing here catches a consumer that skips it, and
  the consumers are UTA-0025's and UTA-0028's.

  The cap was violent: seven of loop 2's eight findings landed on text loop
  1 had written. Per the gate's own rule this document is not re-gated as it
  stands; the next reviewer is the build.
  Probe provenance (2026-09-06), recorded because the spec's SS 7 floors
  rest on these figures and the probes are scratchpad files on a
  session-scoped path that is already gone. Same trap UTA-0057's bullet
  records. Nothing in the repo produces these numbers until this item's
  tier 3 exists -- which is the point of building it.

  Three probes, each about 60-130 lines against libuta_upkg, all over the
  reference install's Maps directory:

  - wiring probe: for every non-class export, read the property list and
    collect Tag and Event as Name properties; build tag -> actors per map;
    count Events matching at least one Tag. Gave 94.9% resolve / 5.1%
    dangle, the many-to-many counts, and the dead class-default hypothesis
    (compare each dangling Event against the map's class names and object
    names).
  - case probe: the same, resolving each Event twice -- exact, then
    lower-cased both sides. Gave the identical-results answer that keeps
    SS 4.4's exact rule.
  - endpoint probe: readLevel per map, then for every reach spec tally
    start and end by ObjectReference::kind(), and for Export kinds whether
    the index is in range and whether it is a NavigationPoint descendant
    (ancestry via readAncestry, cached per class, package name FOLDED
    before the resolver -- the loop-2 finding). Gave 0.12% null, zero
    imports, zero out-of-range, 99.87% resolving to a node.

  Build line for any of them:
  g++ -std=c++23 -O1 -I src probe.cpp build/src/upkg/libuta_upkg.a build/src/core/libuta_core.a

  Rebuild cost is minutes; the ancestry half is the only fiddly part and
  tests/real/RealInstallTest.cpp already ships that shape.
  Progress (2026-09-06): resumed by session ut-ants-d4 in the main
  checkout on branch main, under two-session rule 2 -- the previous
  holder ut-ants-64 is no longer in ListAgents. This session is the only
  one in the project, so the unav lane is held by it alone. Rule 1 of the
  priority order was checked first: the only open review-sourced item is
  UTA-0059, whose own body schedules it behind UTA-0014, so nothing under
  rule 1 is implementable now. Starting implementation from the accepted
  spec.
  Resolved (2026-09-06): src/unav/ builds two libraries from one
  directory. uta_unav holds the graph types and the three queries and links
  uta_core alone; uta_unav_build holds buildNavGraph and buildWiringGraph and
  links uta_unav and uta_upkg. Both closures are asserted at configure time,
  which is INV-6's first half and the split's only mechanical guard --
  docs/design.md rule 2 keeps upkg out of every runtime target, and a single
  library would have breached it through this item.

  Flipped on the matrix, not a local leg: the run for f100280 is completed
  success on Linux GCC 14, Linux Clang 19 and Windows MSVC, and the pre-push
  gate ran ci.sh naming the commit it gated.

  Tier 1 is thirteen cases over INV-1 to INV-4. Each was proved able to
  fail: eight mutations of the builder were killed by an assertion rather
  than by a broken build, and the section 4.5 bound check needed a sanitizer
  to prove -- without it the out-of-range case is a heap-buffer-overflow
  rather than a wrong answer. The whole suite is clean under
  ASan+UBSan. The navigation fixture puts a node at export 0
  deliberately, because index() returns 0 for a null reference: with a class
  export there instead, the case passes with section 4.2's guard removed.

  Tier 3 builds both graphs for all 837 levels in the reference install and
  prints what the spec's section 2.1 asserts, so those figures are an output
  of the suite. It reproduces them: 99.87% of reach-spec endpoints resolve to
  a node and 94.85% of events resolve, against install-wide floors of 95% and
  90%.

  INV-5 and INV-6 are declared reading checks and both were performed.
  Graphs.h includes no upkg header at all and no member of any graph type is
  a view, a pointer or an upkg type -- the only spans are the three queries'
  return types, over the graph's own storage. Nothing under src/unav/
  constructs a ByteReader or calls serialBytes.

  The spec needed one amendment, recording what the build settled rather
  than changing direction: section 6 called an unparseable property list
  "inherited from readProperties", which left two readings. The builder
  propagates, and the real-asset tier measures that no non-class export in
  the reference install trips it.

  Left open, as the spec's section 14 already records: nothing checks a reach
  spec's collision radius and height, and whether the reverse wiring query
  earns its storage is undecided.
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
  Ships to developers, who build it from source. Its command line is a
  breaking surface even so.

  **It needs a machine-readable mode, not only a human one.** Requested
  2026-09-06 by the Monster Hunt server work on this machine, whose
  consumer is a script over a 740-map library rather than a person
  reading one file. Every analysis tool it has today works from a T3D
  export produced by driving the editor headlessly: 4.9 GB unpacked, slow
  to produce, and routinely deleted and rebuilt from a tarball. A `--json`
  or tab-separated mode over the name, import and export tables and an
  actor's properties retires that pipeline.

  Shape it against that consumer's three day-one queries: every package a
  map imports; every actor of a given class with its properties; and each
  `NavigationPoint` with its `Paths` entries. The first is UTA-0070's
  call. The third means nothing until UTA-0057 derives the ReachSpec
  array the `Paths` integers index into.

  That third query also validates this mode rather than merely using it.
  The consuming session's Paths evidence
  (/mnt/Games/Scripts/Linux/UT_MonsterHunt/ut-map-deps/paths-index-evidence.md) was
  gathered by grepping T3D exports, and its four results are reproducible
  from any reader that can list each NavigationPoint's used `Paths` and
  `upstreamPaths` slots. Running it both ways and getting the same
  answers checks `ut-dump`: a reader that consumes its bytes correctly
  but misreads `Paths` shows up as a divergence in the uniqueness result,
  which is the tightest of the four.

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

- ✅ [UTA-0057] **upkg: derive the rest of Level, including the ReachSpec path graph.**
  Split on 2026-09-06 at the request of the Monster Hunt server work on
  this machine, a consumer of upkg; UTA-0069 keeps the Model/BSP half.
  The two have different consumers and very different unblocking value.
  Originally split out of UTA-0004 (user, 2026-09-05).

  A `Level` is an `i32` count, an `i32` capacity, that many actor
  references and then an `FURL` -- all verified -- followed by tens of
  kilobytes this project has not described. `ReachSpecs` sits in that
  tail: the bot path graph, each entry carrying the two nodes it joins
  and the collision radius and height it was built for.

  **This is on unav's critical path, and this item's earlier claim that
  it was not is withdrawn.** Every `NavigationPoint` serialises
  `Paths[16]` as integers and the Properties reader gets those today, but
  they are indices into this array -- without it UTA-0006 reads the nodes
  and not the edges. That indexing claim is NOT established. The
  consuming session withdrew its strength on 2026-09-06: it has never
  read a ReachSpec, and the claim rested on a T3D export showing
  `Paths(0)=334` as a bare integer plus known engine structure.
  Verifying it is this item's first acceptance step, and nothing below
  removes that step.

  What it did produce is circumstantial evidence gathered without
  touching the array, over three maps from the live install, recorded at
  /mnt/Games/Scripts/Linux/UT_MonsterHunt/ut-map-deps/paths-index-evidence.md.
  Four
  results, and the third is the most valuable because it is negative:

  - Uniqueness holds perfectly. No index repeats within `Paths`, nor
    within `upstreamPaths`, anywhere in a level -- across 8,511 outgoing
    and 8,472 incoming entries. Little but an index into a level-wide
    array behaves that way.
  - The two sets match EXACTLY on one map of three (MH-Dust2-BP, 6,579
    distinct) and not on the other two (1202 vs 1274, 730 vs 619). That
    residue is unexplained and must not be assumed away.
  - **The 16-slot cap does not explain the residue, so do not spend time
    on it.** The map with the largest mismatch has zero nodes at the cap;
    the map that matches exactly is the only one where the cap bites at
    all. The hypothesis is already dead, and finding that out is what it
    cost them.
  - Indices are sparse, not 0..n. MH-Dust2-BP references 6,579 distinct
    with a maximum of 14,322, so the array is larger than the set
    referenced and a reader must not assume a dense range. Pruned specs
    still occupying slots is the obvious guess and is a guess.

  Results 1 and 2 give a cross-check to run beside the exact-consumption
  test, and it is the sharper of the two: the reader is wrong if any
  index resolves to a spec whose start is not the node that listed it,
  and wrong differently if that residue turns out to have a mundane cause
  the layout should have predicted.

  No other source of truth exists. Measured by that session 2026-09-06:
  `PATHS DEFINE` and `PATHS BUILD` both ignore blocking actors entirely
  -- a 400-unit `BlockPlayer` on the busiest node of
  MH-AncientCavesTorus changed 0 of 4453 links under DEFINE and 0 of 4449
  under BUILD. Their offline model from actor positions and brush
  geometry reached about 90% accuracy per sample point, which over a
  900-unit hop needing ~22 consecutive samples succeeds about a tenth of
  the time.

  The method is UTA-0004's and the acceptance is already built:
  `docs/specs/UTA-0004-typed-level-content.md` SS 4.3 says a reader that
  models a layout correctly ends exactly at its export's end, and
  `tests/real/RealInstallTest.cpp` runs that over the whole install. The
  layout is right when the real-asset tier consumes every `Level` export
  exactly, and is not right before then.

  In-engine ground truth is on offer if that is not enough -- surface
  solidity by trace and monster/geometry intersection, over a 740-map
  corpus. Two toolchain facts came with the offer: `ucc` silently skips a
  build when the package already exists in any of three directories, and
  the engine buffers its log and flushes only on a clean exit, so a
  SIGKILLed run leaves empty output files.

  Blocked-by: nothing. UTA-0004 shipped the container work this rests on.
  Progress (2026-09-06): held by session ut-ants-f0, in the main checkout.

  Picked up ahead of UTA-0006 because this item unblocks it -- see the
  body. spec-format.md SS 1 says a spec is required: the ReachSpec types
  are a contract UTA-0006, UTA-0012's third query and ubundle all bind
  to, there is a real design choice in how sparse indices and the
  unexplained Paths/upstreamPaths residue are represented, and an on-disk
  reading shape is hard to reverse once consumers exist. Siblings
  UTA-0004 and UTA-0005 both went through specs.

  First acceptance step stands as written: verify that Paths[16] are
  indices into the ReachSpec array, rather than building on the
  consuming session's withdrawn claim.
  Progress (2026-09-06): spec accepted at
  docs/specs/UTA-0057-level-tail-and-reachspecs.md. Two review-contract
  loops, three cold lanes each, nineteen verified findings all fixed, cap
  reached. Status stays in-progress: the spec is the contract, not the work.

  What the gate changed about the item, beyond the document. UTA-0005 is now
  a blocker for the acceptance test: no export of literal class
  NavigationPoint carries a Paths entry on any map measured, so the filter
  must walk ancestry and a name list cannot be complete while maps define
  their own subclasses. UTA-0004 owes three amendments when this lands --
  struct Level gains a member in the document that declares the type,
  SS 7's zero-refusal list gains Level, and SS 10's INV-1 and INV-2 rows go
  false. Two further UTA-0004 pointers were false already and were corrected
  in place (0e28168).

  Known weakness, recorded rather than solved: a ReachSpec's collision
  radius and height are returned and bound to by UTA-0006, and no invariant
  reaches them -- transposed, they consume the same bytes and name the same
  nodes. SS 10 grades them nothing.

  The cap was violent: six of loop 2's nine findings landed on text loop 1
  had written. Per the gate's own rule this document is not re-gated as it
  stands; the next reviewer is the build.
  Progress (2026-09-06): the ReachSpec array's layout is DERIVED and holds
  over the whole library. The item's largest risk is retired; what remains
  is a 21-byte trailer and the reader itself.

  After the FURL the Level tail is: an object reference to the level's
  Model, a compact-index count, then that many records of
  { i32 Distance, index Start, index End, i32 CollisionRadius,
  i32 CollisionHeight, i32 ReachFlags, u8 bPruned }. Start and End are
  object references to actors, not node indices.

  Evidence. A scratchpad probe against libuta_upkg parsed that array on
  837 of 837 maps in the reference install, zero failures, landing 22, 23
  or 24 bytes short of each export's end -- a spread of exactly two, which
  is one compact index at one, two or three bytes. On MH-Village1 the
  count is 1150 and the highest Paths value measured independently from
  T3D is 1149, so an array indexed 0..1149 is an exact fit. Spot values
  decode sanely: distance 100, radius and height 150, flags 32, 406 of
  1150 pruned.

  Not yet done, and the item is not accepted until it is: the 21 fixed
  trailer bytes are undescribed, so exact consumption (UTA-0004 SS 4.3)
  is NOT yet met. First bytes are a float that reads as a time in seconds
  (24.17 on Village1, 190.88 on DM-Deck16), then zeros, then the variable
  index, then zeros.

  The indexing claim SS 4.6 exists to settle is now testable rather than
  believed -- the array reads, so Paths values can be resolved against it.
  Not yet run.
  Progress (2026-09-06), the trailer measurements, so the next session does
  not re-derive them.

  Bytes remaining after the ReachSpec array, over all 837 maps: 22 on 640
  maps, 23 on 190, 24 on 7. Nothing else. So the trailer is 21 fixed bytes
  plus one compact index of one to three bytes.

  Hex, taken after the array ends:

    MH-Village1 (23)  a1 5c c1 41 00 00 00 00 00 00 00 78 2e 00 00 00
                      00 00 00 00 00 00 00
    DM-Deck16 (23)    0e e1 3e 43 00 00 00 00 00 00 00 61 11 00 00 00
                      00 00 00 00 00 00 00
    MH-AncientCavesTorus (22)  all 22 bytes zero
    MH-Dust2-BP (22)           all 22 bytes zero

  Reading: bytes 0-3 are a float -- 24.1702 on Village1, 190.879 on
  Deck16, 0.0 on the two all-zero maps, which reads as a level time in
  seconds. Bytes 4-10 are zero on every map seen. The variable compact
  index sits at byte 11 (Village1 `78 2e` = 3000; Deck16 `61 11` = 1121;
  zero elsewhere, one byte). The rest is zero. That accounts for 22/23/24
  exactly, and is a reading rather than a derivation -- an all-zero
  trailer cannot distinguish field boundaries, so the two non-zero maps
  are carrying the whole inference and more non-zero samples are wanted
  before this is written into the spec.

  The probe that produced all of this is a scratchpad file, not repo
  source, and its path is session-scoped:
  /tmp/claude-1000/-mnt-Games-Scripts-Linux-UT-Ants/ceedba0d-fc4e-4662-bb1b-a01f6b0dd9fc/scratchpad/level-tail-probe.cpp
  It is about 130 lines and rebuildable from the layout recorded above in
  minutes; build with
  g++ -std=c++23 -O1 -I src probe.cpp build/src/upkg/libuta_upkg.a build/src/core/libuta_core.a
  Progress (2026-09-06): readLevel is implemented and both open
  questions are answered. Commit 6ade64c, held by session ut-ants-64. (The earlier progress note
  above names ut-ants-f0, which was the previous session; this session
  copied that name by mistake and corrected it here.)

  The trailer is derived, not read from a narrow sample. After the array:
  a float, eighteen compact indices, then a run of zero bytes. The
  eighteen is fixed by the remainder spanning exactly two bytes -- one
  compact index at one to three -- and one of those indices resolves to a
  TextBuffer export on every one of the 200 maps where it is not null,
  which identifies the region rather than merely fitting its width. The
  earlier "two non-zero samples of four" weakness is retired: 540 of 837
  maps carry a non-zero trailer. One map, MH-SPNaliRescue, has one extra
  zero byte that nothing explains, so it is not modelled as a field --
  the reader requires the run to be zero and refuses a byte with a value.

  readLevel consumes all 837 Level exports exactly, no refusals, which is
  UTA-0004 SS 4.3's acceptance.

  SS 4.6 answers YES. Over 2.4M Paths entries on 837 maps: 99.96% in
  range, 99.54% with the listing node as the spec's start, and every
  entry clean on 816 of the 831 maps carrying one. The residue is fifteen
  maps whose network disagrees with their own nav points -- eight carry
  Paths values with NO reach-spec array at all. Pruning does not explain
  the start mismatches. So INV-2 and INV-3 became rates rather than being
  withdrawn; a wrong partition or a transposed node pair fails far below
  the floor. No negative Paths value anywhere, so no sentinel.

  Not yet done: the CI matrix. Local gate green including TSan, real tier
  green on all seven cases, but the item is not shipped on one leg.
  Resolved (2026-09-06): shipped at ac26f72, green on all three matrix
  legs -- Linux GCC 14, Linux Clang 19, Windows MSVC.

  Acceptance, each item checked rather than assumed. UTA-0004 SS 4.3:
  readLevel consumes all 837 Level exports in the reference install
  exactly, no refusals, and Level joined that spec's zero-refusal set.
  SS 4.6: answered yes, and SS 4.6a records the measurement. Tier 1: the
  fixture builder gained LevelExportWriter and the unit tier covers
  INV-1, INV-4 both ways, UTA-0004's INV-9 and both trailing-byte cases.
  Tier 2: INV-5's declared reading check performed -- Level.h returns
  actors, a raw slot count and the reach specs, with no resolved graph,
  no adjacency list and no name resolved from a reference. Tier 3: green
  on all seven cases.

  Two things a later reader should not re-derive. The conditional
  actorSlotOfIndex member was NOT built: a spec names its nodes by
  object reference, so that branch of SS 4.5 never fired. And INV-2 and
  INV-3 ship as RATES rather than as universal claims -- fifteen maps
  disagree with their own path networks, which is content rather than
  layout, and SS 4.6a owns the reasoning.

  UTA-0006 is unblocked by this.
  **Layman:** Work out the rest of the level record by experiment, because it holds the bot path graph -- which spot connects to which -- and nothing else can tell us.
  Kind: implement.
  Source: user-decision-2026-09-05.
  Lanes: upkg.

- ✅ [UTA-0058] **Settle the Monster Hunt map count the ADRs cite.**
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
  Progress (2026-09-06): held by session ut-ants-f0. The server owner
  settled it, and the answer moves the finding: the install at
  /mnt/Games/PC Games/UT/UnrealTournament-469 IS the live server, but
  counting MH-*.unr there does NOT give what the server serves. Measured
  2026-09-06: 740 MH-*.unr in Maps/, of which 183 are -BP rebuilds whose
  original also survives and is superseded, leaving 557 votable -- which
  matches NumFacts=557 in ~/.utpg/System/MHVoteData.ini exactly. Stock set
  97, unchanged. Maps-broken/ is a SIBLING of Maps/ holding 10 parked
  maps, so the scan must stay non-recursive.

  So the defect is not a stale number. It is that library size and served
  rotation are two different measurements and the docs carry only one,
  while S3, S8 and UTA-0038 are all measured against "the rotation".

  Repair, per the server owner's decision: leave ADR-0002, ADR-0003 and
  ADR-0004 alone -- they record what was believed when written, which is
  what an ADR is for -- and give docs/discovery.md both numbers with their
  derivations.
  Resolved (2026-09-06) in b819bcd, green on all three legs at 4fe951e
  (Linux GCC 14, Linux Clang 19, Windows MSVC).

  The finding was not a stale number. Library size and served rotation are
  two different measurements and the docs carried one, while S3 is
  measured against the library ("a map pulled from the existing library")
  and S8 and UTA-0038 against the rotation. Measured in the live server's
  own install: 740 MH-*.unr in Maps/, 183 of them -BP rebuilds whose
  original survives and is superseded, leaving 557 votable -- matching
  NumFacts=557 in ~/.utpg/System/MHVoteData.ini exactly. Stock set 97.

  docs/discovery.md now gives both numbers a derivation each, and records
  that Maps-broken/ is a sibling of Maps/ holding maps never served, so
  the scans stay non-recursive.

  ADR-0002, ADR-0003 and ADR-0004 keep their bare 610 by the server
  owner's decision: an accepted ADR records what was believed when it was
  written, and docs/decisions/README.md forbids editing one. The second
  question this item raised -- whether citations should carry a date -- is
  answered in practice rather than by rule: discovery.md dates its figures
  and ships the command to re-derive them, so the live answer has one home
  and no bare number in it can go stale silently.
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

- 🚧 [UTA-0069] **upkg: derive the Model BSP tables.**
  Split from UTA-0057 on 2026-09-06; that item keeps the `Level` tail.

  A `Model` begins with 41 bytes of `FBox` + `FSphere`, carries an object
  reference to its `Polys`, and ends with two `i32`. Between and after
  those sit runs of compact-index-prefixed arrays whose ORDER is unknown
  -- the member set is knowable, and the order the community
  documentation implies is measurably wrong: read that way,
  `DM-Deck16][.unr` yields five nodes and thirty-nine surfaces for a
  `Model` export of over 450 kB.

  `ubake` (UTA-0011) is what needs this. It also answers a question the
  Monster Hunt server work cannot answer offline today: real `PolyFlags`
  on BUILT surfaces rather than on brushes. The two drift apart when a
  surface is edited after the last build -- measured by that session
  2026-09-06, of 202 candidate surfaces flagged from brush data only 42
  were actually solid in the running game, and on MH-Village1 it was 6 of
  6 wrong.

  That half saves them a server boot. The `Level` tail unblocks work they
  cannot do at all, which is why it goes first.

  Acceptance is UTA-0004's: the real-asset tier consumes every `Model`
  export exactly.

  Blocked-by: nothing. UTA-0004 shipped the container work this rests on.
  Progress (2026-09-07): claimed by session ut-ants-b8, main checkout.
  Starting with the spec-need decision under spec-format.md § 1.
  Progress (2026-09-07): spec drafted and committed at a6053d2 as
  docs/specs/UTA-0069-model-bsp-tables.md. The top-level Model
  serialisation order is DERIVED and measured over the reference
  install; four element layouts are settled. See the spec SS 2.2, SS 4.4
  and SS 4.5. Still open: the element layouts of SS 4.6, and the review
  gate. Held by session ut-ants-b8, which was terminated by a deliberate
  terminal restart before review-contract loop 1 completed -- three
  lanes were dispatched and lost, and the loop log is correctly still
  empty. The gate is OWED and has not run. A later session may resume
  this claim: docs/session-handoff-2026-09-07.md carries the resume
  point.
  Progress (2026-09-07): claim resumed by session ut-ants-4e, main
  checkout, after the restart the note above describes. That note is
  superseded on one point: loop 1 DID complete, at 44d85f0. The review
  gate is now COMPLETE -- loops 1 and 2, the cap for a spec, logged in
  docs/reviews/UTA-0069-model-bsp-tables-loop-log.md -- and the spec is
  accepted (2026-09-07) at 85e51dc. Loop 2 fixed seven verified findings
  plus two corrections; re-running the payload probe showed the spec's
  all-empty histogram was a size-threshold selection presented as a
  partition, which no lane could have seen because the probe is not in
  the tree. Still open, and this item is NOT done until they close: the
  element layouts of spec SS 4.6 plus the UPrimitive prefix version
  branch, then readModel itself, then tier 1 fixtures and a tier-3 run
  with zero refusals, which is this item's stated acceptance.
  Derivation progress (2026-09-07), session ut-ants-4e. The probe was
  rebuilt and re-run, reproducing spec SS 2.2 exactly: 545,652 exact,
  1,155 wrong offset, 9,645 failing to walk. Three results worth not
  rediscovering.

  1. The 9,645 broken down by the table it stops at: Vectors 215, Points
  15, Nodes 3, Surfs 1, zone record 735, LightMap 32, LightBits 1,734,
  Bounds 4,924, LeafHulls 1,152, Leaves 580, Lights 253, trailing i32 1.
  This MEASURES what loop 2 of the review argued from the spec's own
  figures -- 266 of them stop at tables SS 4.5 calls settled, so closing
  SS 4.6's six layouts cannot reach zero refusals. Bounds is the largest
  single class by far and is the best next target after the zone record.

  2. The UPrimitive version branch is now SCOPED, where the spec could
  only call it a hypothesis. Every one of the 198 65-byte exports is in
  ONE package, MH-SPNaliRescue.unr, the corpus's only version-61 map; it
  holds 234 Model exports and ALL of them fail. No other version has a
  short payload class. So the branch is real, is version 61, and costs
  0.04% of the corpus -- and it is a Monster Hunt map, which is this
  item's stated consumer.

  3. Its layout is NOT simply four bytes shorter. A dumped v61 all-empty
  export reads FBox(25) then twelve zero bytes, then twelve bytes that
  decode as six compact indices with values in the hundreds to
  thousands, then NumSharedSides=0, NumZones=0, RootOutside=1,
  Linked=1. Dropping FSphere's W to make a 37-byte prefix was measured
  and changed nothing. Whatever v61 does differs by more than the prefix.

  Not attempted here: the zone record, LightBits, Bounds, LeafHulls,
  Leaves, Lights. The zone-count sanity guard in the probe was tested and
  is NOT the cause of the 735 -- relaxing it moved nothing.
  Derivation, second pass (2026-09-07), session ut-ants-4e. Five results
  that narrow SS 4.6 and are worth not re-deriving.

  1. LightMap's element layout is GENUINELY verified, which SS 4.5 could
  not claim on its own evidence: 10,361 of the exactly-consuming models
  have a non-empty LightMap. So it is exercised, not merely unexercised
  and therefore silent.

  2. Reading UClamp/VClamp as compact indices instead of raw i32 was
  measured and is REFUTED -- exact consumption falls to 96.20%. The
  spec's raw-i32 reading is right.

  3. The failures are CONTENT-dependent, not version-dependent, and this
  is the useful discriminator. Real-content models by package version,
  exact/failed/wrong-offset: v61 0/234/0, v63 22/18/3, v68 214/298/39,
  v69 11,731/9,095/1,113. So v61 is a total branch, while inside v69 --
  the bulk of the corpus -- 56% parse and 44% do not. No version rule
  explains that.

  4. At every stage from LightBits onward the dominant failure is a
  declared count that is large and NEGATIVE, so the cursor is already
  misaligned when it arrives; the wrong width is upstream of the table
  that reports the error, not at it. With LightMap verified by (1), the
  misalignment begins at or just after LightBits.

  5. Putting (3) and (4) together: the models that fail are the ones that
  POPULATE SS 4.6's tables, which the parsing models leave empty. That is
  where the remaining work is, and Bounds is the largest class.

  Still not attempted: the zone record, and the element layouts of
  LightBits, Bounds, LeafHulls, Leaves and Lights.
  OPEN QUESTION for the user, blocking readModel (2026-09-07,
  ut-ants-4e). The accepted spec cannot be implemented as written on one
  point. SS 4.1 requires the zone records be RETURNED, as
  std::vector&lt;ZoneProperties&gt; zones. SS 4.6 leaves the zone record
  underived, and SS 4.6's own rule forbids stating a layout that is not
  verified. A vector needs a complete element type, so the two cannot both
  be honoured. Review loop 2 narrowed SS 11's claim about this but did not
  resolve it, and the loop cap was reached. Three ways out, none of them
  mine to pick: declare ZoneProperties with no fields for now; omit the
  zones member until SS 4.6 closes; or derive the zone record before
  writing any of the reader. Do NOT resolve this silently in a diff.

  Two notes for whoever picks this up.

  The derivation probes are NOT in the tree and are NOT reconstructible
  in full from the spec -- SS 4.4 and SS 4.5 give the base walk, but the
  diagnostics that produced the recent findings (per-stage declared-count
  histograms, the per-version real-content breakdown, the payload
  cross-tab) were written this session. They were left in that session's
  scratchpad under /tmp/claude-1000/, which does not survive. Expect to
  rebuild them; the notes above say what each one measured so the results
  need not be re-derived, only the tooling.

  Considered and REJECTED, so it is not reopened: committing the probe to
  the tree so reviewers can check SS 2.2's install-scale figures. Review
  loop 2 showed the figures were unverifiable by any reader, which is a
  real gap -- but SS 7 already has tier 3 print those same figures, so it
  closes when readModel ships rather than needing a second mechanism.
  Open question ANSWERED by the user (2026-09-07): derive the zone
  record layout BEFORE writing any of readModel. So the SS 4.1 / SS 4.6
  conflict is resolved by closing SS 4.6's first table rather than by
  weakening SS 4.1's promise -- ZoneProperties gets real fields, and
  neither an empty placeholder struct nor omitting the zones member was
  taken. Both alternatives are closed; do not reopen them in a diff.
  This also matches SS 4.6's own file-order rule, and the zone record is
  already the next target the first derivation pass named.
  DERIVED (2026-09-07, session ut-ants-4e): the zone record, closing the
  first of SS 4.6's six layouts and the SS 4.1 conflict the user ruled on.

  FZoneProperties = compact index ZoneActor, i64 Connectivity, i64
  Visibility. 17 bytes when ZoneActor is null, wider as that index widens.

  Derived without assuming a layout, by anchor sweep: walk to NumZones,
  then sweep the byte span to the following Polys object reference,
  accepting a span only where the index there resolves to an export whose
  class is Polys and the LightMap count after it is sane. Self-checked on
  zero-zone models, where the span must be 0 and was, for every one
  sampled. Spans came out at ~17 per zone with occasional +1 -- the
  signature of a compact index inside an otherwise fixed record.

  Confirmed by CONTENT, not by byte count alone, which is what settles
  field ORDER: down the records of one model the middle eight bytes read
  1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80 -- a bitmask whose set bit tracks the
  record's own ordinal, so Connectivity is a zone mask and the leading
  field is what precedes it. Visibility read all-ones on every record
  dumped. The leading index widened to two bytes exactly where the span
  grew by one.

  Confirmed at install scale: implementing the layout took the zone-record
  failure class to ZERO, with 538,586 of 561,352 Model exports consuming
  exactly and 4 at a wrong offset. Every remaining failure is at a table
  SS 4.6 still leaves open: LightBits 17,366, Bounds 5,012, LightMap 99,
  LeafHulls 38, Lights 8, Leaves 5, plus the 234 early-table failures
  (Vectors 215, Points 15, Nodes 3, Surfs 1) that SS 4.6 assigns to the
  version-61 class.

  Two caveats on comparing these to SS 2.2. This probe sweeps more file
  extensions, so its corpus is 561,352 Model exports against SS 2.2's
  556,452 -- the totals are not the same population. And it requires
  SS 4.6's undrived tables to be EMPTY rather than carrying a partial
  layout, which moves failures out of "wrong offset" into "failed"; that
  is why wrong-offset reads 4 here against SS 2.2's 1,155. Neither figure
  supersedes SS 2.2; they answer different questions.

  The spec is NOT yet amended. Doing it once, after the remaining
  layouts are derived, so SS 4.6 takes one review gate rather than one per
  table -- SS 4.6's own file-order rule is what makes that safe.
  Next target: LightBits, the largest class and the earliest still open.
  Derivation, third pass (2026-09-07, ut-ants-4e). Four more of SS 4.6's
  six layouts measured, in the file order SS 4.6 requires. Each was
  confirmed by SS 4.5's own method -- the failure count at that table
  collapsed and no earlier table's count moved.

  LightBits -- one byte per element. Failures 17,366 -> 1,849.
  Bounds -- FBox, 25 bytes, the same shape as the prefix. 18,297 -> 5,237.
  LeafHulls -- four bytes per element. Exact consumption 543,062 against
    539,107 for a compact index, so the two candidates are separated.
  Lights -- one compact index per element (an object reference). Its own
    failures 244-338 against about 7,000 for a four-byte read.

  Bounds was additionally confirmed by a SECOND and independent method,
  because it carried the largest residue: sweeping its element width
  against the end of the payload, 11,690 models land exactly at width 25
  and 645 at the next best. Two methods, one answer.

  Leaves is NOT derived and must not be stated. It is barely exercised --
  685 models reach it with a non-empty count -- and NONE of them consume
  exactly, which is why a four-byte element and a compact index score
  identically and neither is evidence. Sweeping its width against the end
  of the payload gave no winner: the best candidate took 14 models of 685.
  By SS 4.6's cascade rule that means the misalignment is UPSTREAM of
  Leaves, in the residues at LightBits and Bounds, so Leaves cannot be
  settled until those close. Do not read the scatter as a narrow result.

  State after this pass, over 561,352 Model exports: 550,372 consume
  exactly, 1,466 end at the wrong offset, 9,514 fail -- Bounds 5,237,
  LightBits 1,849, LeafHulls 1,212, Leaves 542, Lights 338, LightMap 99,
  plus the 234 early-table failures SS 4.6 assigns to the version-61 class.

  One thing worth knowing before re-deriving. These residues are close to
  the per-table figures the FIRST pass recorded (LightBits 1,734, Bounds
  4,924, LeafHulls 1,152, Leaves 580, Lights 253), so that probe was
  evidently already WALKING these tables at these widths while SS 4.6
  declined to state them -- correctly, since nothing had verified them.
  What this pass adds is the verification, and the zone record, where the
  same comparison shows a real gain: 735 failures then, zero now.

  Searched and found nothing, so it is not re-searched: the user's Vestige
  engine (/mnt/Games/Scripts/Linux/Vestige) and asset library
  (/mnt/Games/3D Engine Assets). No UE1 format knowledge, no Unreal
  parsing code, no UE1 fixtures -- a zero match for every UE1 struct name
  across the whole indexed tree. Vestige delegates container parsing to
  tinygltf and has no binary cursor at all. The one item worth borrowing
  is the untrusted-count hardening in its engine/environment/terrain.cpp
  loadHeightmap: an absolute byte cap independent of the declared count,
  an exact expected-size check, and a short-read guard -- the shape these
  count-prefixed tables need.
  Leaves, run to ground (2026-09-07, ut-ants-4e). Three results, so the
  next session does not repeat the attempt.

  1. It is GENUINELY POPULATED, not a misalignment artefact. Of the 685
  models reaching a non-empty Leaves the declared counts are small and
  plausible -- 1, 2, 3, 6, 14 dominate and 539 of the 685 declare 16 or
  fewer. So the table carries real data and the layout is a real gap.

  2. A FIXED element width does not explain it, and neither does an
  index-bearing one. Sweeping (k compact indices + f fixed bytes) against
  the end of the payload for k up to 4 and f up to 32, the best shape took
  35 of 685. The apparent runners-up are ALIASES rather than independent
  evidence: an index whose value is zero occupies one byte, so "2 idx + 9
  bytes" and "3 idx + 8 bytes" are one shape counted twice, which is why
  they score identically. Do not read that pair as corroboration.

  3. So the misalignment is upstream, per SS 4.6's cascade rule, and
  Leaves cannot be settled until the residues at LightBits and Bounds
  close. Those residues are the version-61 branch and the
  content-dependent class the first pass measured -- open-ended work, not
  a next step of this derivation.

  Method note for whoever rebuilds the probe: the anchor sweep is what
  makes these answers measurements rather than guesses. Walk to the table,
  then sweep the byte span to a signature you can recognise -- the Polys
  object reference for the zone record, the exact end of the payload for
  Bounds and Leaves -- and accept a span only where the signature holds.
  It needs no hypothesis about the layout, and it self-checks wherever the
  answer is known in advance, as zero-zone models pin the zone span at 0.
  Second open question ANSWERED by the user (2026-09-07): `leaves` is
  LEFT OUT of what readModel returns until its layout is derived. So the
  reader is written now with every other table, zones included. The empty
  placeholder was offered again and rejected again, on the same grounds as
  for zones; do not reopen it. Chasing the upstream residues first was
  also declined as open-ended.

  The precedent is SS 4.1's own -- Level.h returns its reachSpecs and
  consumes the rest -- and UTA-0007, the item waiting on this, partitions
  a level by ZONES and does not name leaves, so nothing downstream is
  blocked by the omission.

  Next: amend SS 3.2, SS 4.1, SS 4.5, SS 4.6 and SS 11 for the five
  derived layouts and the omission, run the rule-14 gate on the amended
  spec, then write readModel.
  Review gate on the amendment: COMPLETE (2026-09-07). Two loops, the
  cap for a spec, logged as rows 3 and 4 of
  docs/reviews/UTA-0069-model-bsp-tables-loop-log.md. 24 verified
  findings, 24 fixed. The spec is accepted and ready to implement.

  The gate earned its cost twice, both times on claims no re-reading
  would have caught. Loop 1: SS 4.6 said all 9,346 walk-stops sit at a
  table SS 4.5 settles, when Leaves is exactly the one it does not, and
  the list summed to 9,343 -- I had read the probe's failure histogram
  through a line cap that hid its last row, the stage names sorting with
  lowercase last. Loop 2: every residue figure in the document described
  the derivation PROBE, which steps over Leaves at an unverified
  four-byte width, rather than the reader SS 4.1 specifies, which refuses
  a populated one. Re-measured under the specified reader the split is
  922 at the wrong offset and 9,878 failing, with Leaves 1,197; the exact
  count is unchanged at 545,652, because no export with a populated
  Leaves consumes exactly under either reading.

  That second one also shows why an empirical claim is re-run rather than
  argued: all three lanes concluded 545,652 was unreachable by a
  conforming reader, and the measurement refuted that while confirming
  the real defect underneath it.

  The cap was VIOLENT by the skill's measure -- four of loop 2's seven
  findings landed on text loop 1 wrote -- so the spec is NOT re-gated as
  it stands. It goes to implementation, which exercises the contract
  against real code.

  TAIL, filed rather than fixed, for whoever implements: SS 2.2's 533,685
  empty-only figure and SS 4.4's payload-bucket census do not reconcile.
  An all-empty Model cannot exceed 73 bytes, so the census should bound
  the empty-only run from above, and it is 33 short. Both figures predate
  this session and neither was re-measured; SS 4.4 already warns the two
  are not a partition, which may or may not be the whole answer. Also
  unverified here: SS 4.5's claim that 10,361 exactly-consuming exports
  carry a non-empty LightMap.

  Next: write readModel. The spec now states nine element layouts, gives
  Plane and Box as code, and settles that `leaves` is neither returned
  nor stepped over -- an empty one is consumed, a populated one refused.
  Evidence AGAINST the recorded rejection of committing the probe
  (2026-09-07). Not a reversal -- the rejection stands until someone
  decides otherwise -- but the ground has moved and the next session
  should know before it re-reads that note as settled.

  The probe has now been rebuilt from scratch TWICE, by two sessions, and
  each rebuild cost real time before any derivation could start. The
  rejection's reason was that tier 3 already prints SS 2.2's figures, so a
  second mechanism was unnecessary. That reason covers the FIGURES and
  does not cover the TOOLING: the diagnostics that produced this session's
  results -- the anchor sweep against a recognisable signature, the
  per-stage failure histogram, the element-width sweeps, the
  non-empty-count instrumentation -- are not what tier 3 prints and are
  not reconstructible from the spec.

  Concretely, what tier 3 gives you is a pass/fail count. What the probe
  gives you is WHERE the walk stopped and WHAT WIDTH would fix it, which
  is the only instrument that closes SS 4.6. Those are different tools
  answering different questions, and only one of them exists in the tree.

  Weigh that against the original objection when SS 4.6's residues are
  next picked up. The probe links only against the existing uta_upkg and
  uta_core static libraries and reads the install path from an argument,
  so it has no new dependency; the reason it is out of the tree is scope,
  not cost.
  Progress (2026-09-07, session ut-ants-84): held by this session, which
  resumed the item after the spec amendment gate closed. `readModel` is
  written in `src/upkg/Geometry.{h,cpp}` and builds clean — the nine
  element layouts of § 4.5, the § 4.4 order, and § 4.1's rule that
  `leaves` is neither returned nor stepped over (an empty table is
  consumed, a populated one is `MalformedData`). Tier 1 (§ 4.7's four
  fixture cases) is in authoring. Tier 3 has not been run, so INV-4 is
  unmeasured and the item stays 🚧.
  Finding (2026-09-07, session ut-ants-84): THE ACCEPTANCE METRIC IS
  NEARLY BLIND TO THE CONTENT THIS ITEM EXISTS FOR. Measured: of the 847
  packages holding a `Model`, only **4** have their LARGEST `Model` parse.
  A map carries hundreds of `Model` exports — one per editor brush — and
  its BSP geometry lives in the largest. So the 98% exact-consumption
  figure is carried almost entirely by tiny brush models whose tables are
  all empty, while the level geometry UTA-0007 and UTA-0011 need parses
  almost nowhere.

  That has a methodological consequence for § 4.5. Its later layouts were
  each chosen by maximising exact consumption over ALL exports — a
  population dominated by ~550,000 trivial models that never reach those
  tables at all. A wrong width in `LightMap`, `Bounds`, `LeafHulls` or
  `Lights` moves that figure by a fraction of a percent, so the metric
  barely constrains the very layouts it was used to settle. The failure
  concentration is consistent with this: nodes/surfs/verts fail 3/1/0
  times, and the mass sits at `LightBits` and after.

  PROPOSED next metric: packages whose largest `Model` parses, now printed
  by the tier-3 walk beside the export counts. It starts at 4 of 847 and
  has real signal, where exact-consumption does not.

  RECONCILED, and § 2.2 needs no correction. The per-table residue
  histogram now printed by tier 3 matches § 4.6 table for table —
  `Vectors` 215, `Points` 15, `Nodes` 3, `Surfs` 1, trailing `i32` 1 are
  identical, and the rest differ by +1 to +116, summing to exactly 180.
  This walk covers 4,900 more exports than § 2.2's probe, splitting 4,720
  exact and 180 refused. So the earlier note's unreconciled-figures
  warning is withdrawn: the reader reproduces the spec's derivation on the
  shared population, and the difference was scope alone.

  Also landed: every `Model` refusal now names the part it stopped in —
  the table, or the prefix, count or reference between tables — through
  `Error::withContext`. § 4.6 requires the residue be derived in file
  order, and that cannot be done while a short read reports a message
  naming no table. Unclassified refusals went from 3,751 to zero.
  Delivered (2026-09-07, session ut-ants-84) — re-recorded, because the
  note carrying this was discarded by a `roadmap_log` write that reported
  `discarded_external_edits`. The original text is in commit 15cd182.

  `readModel` shipped in `src/upkg/Geometry.{h,cpp}` at 15cd182, with tier
  1 and tier 3. Tier 1 is six cases across `PackageContentTest.cpp` and
  `PackageMalformedTest.cpp`: § 4.7's four, plus a bytes-left-over case
  and a populated-`Leaves` refusal. The fixtures populate every table
  § 4.5 settles, at distinct counts with a per-index label — all-empty
  tables left six of the nine derived layouts byte-identical under a swap.

  Tier 3 joins the install walk in `RealInstallTest.cpp`, tallying rather
  than asserting per export so the residue prints, with INV-4 asserted at
  zero refusals at the end.

  MEASURED: 550,372 `Model` exports consumed exactly, 10,980 refused.
  INV-5 is clean at zero violations — every one of the 550,372 `Polys`
  references resolved to a `Polys`-classed object, none null. INV-4 is
  RED, which § 6 states as the honest outcome while § 4.6 is open.

  Seven mutation routes probed against the tier-1 tests, all seven killed:
  `iLeaf`, `LeafHulls` and `LightMapIndex::uClamp` each read as a compact
  index (the three readings § 4.5 refuted by measurement), both INV-2
  count guards, INV-1's exact-consumption check, and § 4.1's `Leaves`
  refusal. One earlier probe reported the INV-2 oversize guard surviving;
  that was a faulty mutation — `count > remaining * 1000` still fires on a
  small export — and the corrected `if (false && ...)` mutation kills it.

  Gate green: 172 tests, ThreadSanitizer clean. The suite is also clean
  under AddressSanitizer + UndefinedBehaviorSanitizer, which is the leg
  that grades a bounds check rather than a wrong answer.
  Progress (2026-09-07, session ut-ants-b2): resumed the 🚧 left by
  ut-ants-b8/ut-ants-4e, neither of which is live in ListAgents. § 4.6's
  version-61 class is now EXPLAINED rather than scoped, and it is not a
  width difference.

  In version 61 a Model does not hold its BSP tables inline. It holds six
  OBJECT REFERENCES, to separate exports: Vectors, Points (a second
  Vectors), BspNodes, BspSurfs, Verts, Polys. Two independent proofs, both
  in MH-SPNaliRescue.unr, the corpus's only version-61 package. The class
  histogram: Model 234, BspNodes 234, BspSurfs 234, Verts 234, Polys 234,
  Vectors 468 -- one of each per Model, Vectors twice. And the six indices
  resolve by name to exactly those exports, on every export sampled
  (Vectors54/Vectors55/BspNodes27/BspSurfs27/Verts27/Polys23 for one).

  The prefix is 37 bytes, not 41: FBox then a 12-byte vector with no sphere
  radius. The references start at payload offset 37 and resolve there; at
  41 they do not. That is the handoff's unverified lead, now measured -- and
  it is why the earlier 37-byte experiment "changed nothing": the prefix was
  right and the whole second half was wrong.

  After the references sit EIGHT index-prefixed arrays, then RootOutside and
  Linked as i32. No NumSharedSides and no NumZones. Derived element widths
  in file order: 30, 1, 25 (an FBox), 4, never-populated, 1, undetermined,
  undetermined. Every array boundary in the smallest populated export lands
  exactly on the next count.

  Measured: 223 of the 234 version-61 Model exports consume exactly under
  that layout, against 0 today.

  The last two arrays are NOT determined by this corpus, and the evidence
  says so rather than being absent. Sweeping both widths to 48, the best
  pair improves on almost every other pair by ONE export. That is § 4.6's
  own alias warning for Leaves, so no width is stated for them.

  Consequence for § 4.6's file-order rule: the version-61 class is a
  DISJOINT population, one package, not upstream of anything. The cascade
  argument does not link it to the bounds/lightbits mass, which is version
  68/69. Closing either does not move the other.

  Current tier-3 baseline, this session: 550,372 consumed exactly, 10,980
  refused; vectors 215, bounds 5,237, lightmap bytes 1,849, leaf hulls
  1,212, leaves 1,030, wrong end offset 933. Packages whose LARGEST Model
  parses: 4 of 847.

  Not implemented. The scope question -- whether transcribing the six
  references belongs to this item, and whether reading those four export
  classes is its own item -- is with the user.
  Progress (2026-09-07, session ut-ants-b2): the user ruled that version 61
  is its own item. Filed as UTA-0072. readModel now refuses a Model below
  package version 62 by name, with ErrorCode::UnsupportedVersion -- the
  bytes are not malformed, they are a layout this reader does not describe,
  which is § 4.1's Leaves ruling applied to a whole export. The boundary is
  62 rather than 63 because that is the smallest claim the measurement
  supports: 61 is the only version below 62 the container accepts, and 62
  appears nowhere in the reference install.

  The refusal is locked by a tier-1 test that reads the SAME bytes at both
  versions -- consumed exactly at the builder's default, refused at 61 --
  so it measures the version and nothing else. Mutated to
  `if (false && ...)`: the test fails. 173 unit tests pass.

  And the walk says something § 4.6 does not. Refusals at vectors (215),
  points (15), nodes (3) and surfs (1) sum to exactly 234, the count of
  version-61 Model exports -- and all four buckets are now EMPTY across all
  847 packages. Every refusal at the first four tables in the whole install
  was this one class. § 4.6 lists them as residue positions and they are
  not residue at all.

  What is left, measured this session with the refusal in place: bounds
  5,237, lightmap bytes 1,849, leaf hulls 1,212, leaves 1,030, wrong end
  offset 933, lights 202, leaves count 183, lightmap entries 99, trailing
  fields 1, package version below 62 234. Consumed exactly 550,372,
  refused 10,980 -- unchanged, since the same exports refuse and only their
  classification moved. Packages whose LARGEST Model parses: 4 of 847.

  § 4.6 is now stale in two ways -- its version-61 paragraph, and its
  residue list naming four tables that carry none. Amending it re-arms the
  review gate under rule 14, so it is left for a deliberate decision rather
  than taken as a side effect. The ROADMAP carries the current numbers
  meanwhile.
  **Layman:** Work out the file layout of a level's shape, so the baker can read which surfaces are really solid instead of guessing from the brushes.
  Kind: implement.
  Source: consumer-request-2026-09-06 games-drive.
  Lanes: upkg.

- 📋 [UTA-0070] **upkg: answer which packages a package needs, as a supported call.**
  Requested 2026-09-06 by the Monster Hunt server work, which already
  reassembles this from `imports()` and `name()`. The import entries
  whose outer is null are the packages this one needs.

  It is load-bearing outside this project already. A ~60-line tool built
  against `libuta_upkg.a` walked all 837 maps in that install in 15
  seconds and named exactly what two of the ten parked maps are missing.
  The comparison is booting a dedicated server per map and reading the
  failure out of the log, at 45-60 seconds each -- and a missing package
  is the commonest single reason a community map is unplayable.

  That tool was handed over on 2026-09-06 and sits at
  /mnt/Games/Scripts/Linux/UT_MonsterHunt/ut-map-deps/ -- `ut-map-deps.cpp`
  plus a
  README with the build line and the measurements, verified to build and
  run from that copy rather than only from its author's scratch
  directory. Take it as the first consumer to shape the call against.

  Two things carry over with it. It needs `-std=c++23`, because
  `uta::Result` is `std::expected` and under 20 it fails with a
  misleading error. And its whole filter is a single judgement: an import
  whose outer is null names a PACKAGE, anything else names an object
  inside one. That is the line to argue with if a result ever looks
  wrong, so it belongs in this call's own contract rather than being
  re-decided by each caller.

  Blocked-by: nothing. The container reader shipped.
  **Layman:** Ask a map file which other files it needs, in one call. It is the fastest way to find out why a downloaded map will not load.
  Kind: implement.
  Source: consumer-request-2026-09-06 games-drive.
  Lanes: upkg.

- 📋 [UTA-0071] **CI: move the runner images to the current ones, or hold them on the record.**
  .github/workflows/ci.yml runs ubuntu-24.04 and windows-2022. Measured
  2026-09-07 against actions/runner-images: the win25 and ubuntu26 image
  families both exist and are actively rebuilt, most recently the same
  week. So both legs sit below the current image.

  dependencies.md SS 2 counts a CI runner image as a dependency and SS 1
  makes latest stable the default, framed as a SECURITY rule rather than a
  feature one -- an image nobody chose is an image nobody can defend.
  So this is a compliance gap, not a preference.

  Everything else on the version surface was checked at the same time and
  is already current, so it is not re-checked: Catch2 is pinned v3.16.0
  and that is the latest release; actions/checkout is pinned to the commit
  for v7.0.1, the latest, and the SHA pin is the form security.md wants.
  cmake_minimum_required 3.28 is a FLOOR, not a hold -- dependencies.md
  SS 4 says floors stay out of the ledger.

  Do not bump blind. The matrix pins clang-19 and g++-14 deliberately, and
  docs/design.md SS The stack owns the compiler floors -- a newer image
  changes the default toolchain under those pins, and the apt package
  names may not exist on it. Read the floors first, then move one leg at a
  time so a red leg names itself.

  Where a bump genuinely breaks, dependencies.md SS 3 is what to do
  instead: a ledger row carrying what is held, held-at, BROKE-AT, what
  breaks concretely, what would release it, and the dates. The broke-at
  version is the retest trigger and is the point of the row -- without it
  a hold becomes permanent by accident. This project has no hold ledger
  today because it has had no holds; SS 3 says where one goes if this
  produces the first.
  **Layman:** Move the machines GitHub builds on to the current versions, so we get their security fixes instead of staying on older ones by accident.
  Kind: chore.
  Source: user-request-2026-09-07 standing-dependency-rule.
  Lanes: ci.

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
  Blocked-by: combat bots, and UTA-0006 for the wiring graph, which shipped
  2026-09-06.
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

  Blocked-by: UTA-0006 for the navigation graph, which shipped 2026-09-06,
  and combat bots to route for.
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

- 📋 [UTA-0072] **upkg: read a version-61 Model, whose BSP tables are separate exports.**
  Split from UTA-0069 on 2026-09-07 by the user's ruling, after that item
  derived the layout and measured it. UTA-0069 refuses version 61 with a
  named reason instead; this item is what lifts the refusal.

  In version 61 a Model holds no inline BSP tables. It holds six object
  references -- Vectors, Points (a second Vectors), BspNodes, BspSurfs,
  Verts, Polys -- behind a 37-byte prefix (FBox, then a 12-byte vector with
  no sphere radius), ahead of eight index-prefixed arrays and the two
  trailing i32. There is no NumSharedSides and no NumZones. UTA-0069's
  ROADMAP bullet carries the derivation, its two proofs and the measured
  counts.

  Three pieces of work, and the first two are separable.

  1. Transcribe the Model. Add the six references behind a version branch.
  Measured 2026-09-07: 223 of the 234 version-61 Model exports consume
  exactly under the derived layout, against 0 before it.

  2. The last two of the eight arrays. Their element widths are NOT
  determined by the reference install: sweeping both to 48, the best pair
  beats almost every other pair by one export. That is UTA-0069 SS 4.6's
  alias warning for Leaves, so a width must not be stated on that evidence.
  Nine exports carry a non-empty seventh array and five a non-empty eighth.
  Without these, eleven exports still refuse.

  3. Readers for the four export classes -- Vectors, BspNodes, BspSurfs,
  Verts. Until these exist the geometry is not reachable, only referenced,
  so 1 alone gains a consumer nothing. Polys already has a reader and is
  the precedent for the shape.

  Worth it because the corpus's only version-61 package is a Monster Hunt
  map, MH-SPNaliRescue.unr, which is this milestone's own subject. It is
  also the whole population: 1 package of 847, 234 Model exports.

  Blocked-by: nothing. UTA-0069 shipped the container work and the
  derivation.
  **Layman:** Read the level shape out of very old maps, which store it in a different place inside the file.
  Kind: implement.
  Source: in-session-2026-09-07 split-from-UTA-0069.
  Lanes: upkg.

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

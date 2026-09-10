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

- ✅ [UTA-0007] **umap: partition a level into rooms and answer which room a point is in.**
  Built from the level's own BSP zones, so no map needs hand-authoring. Owns the
  simplified room model and the point-in-room lookup; ubundle owns its bytes
  (rule 17), uui draws it (rule 18), and the exploration state that fills it in is
  0.4.0's.
  Blocked-by: reading level geometry.
  Progress (2026-09-08): picked up by session ut-ants-c7. Unblocked by
  UTA-0069, which shipped the Model BSP tables this reads.
  Progress (2026-09-08): claimed then released by ut-ants-c7 without
  starting work. UTA-0074 taken ahead of it on the user's priority order,
  restated this session: outstanding fixes from any review come first,
  backlogged ones included. Nothing was built here; this item is
  untouched and still unblocked.
  Progress (2026-09-08): picked up by session ut-ants-c7, in the main
  checkout. Clears the release note above -- UTA-0074 was taken ahead of it
  and shipped at 290252c. Starting with the spec: spec-format.md SS 1 fires
  on three triggers here (a contract ubundle serialises and uui draws per
  design rule 17, an on-disk shape that is hard to reverse, and a real
  design choice about what a room IS).
  Progress (2026-09-08): spec accepted at 794f6da --
  docs/specs/UTA-0007-room-partition-and-lookup.md, two review loops, twenty
  verified findings all fixed. Item stays in-progress; implementation is next.

  The gate hit its cap of two and the cap was a VIOLENT one: seven of loop 2's
  nine findings landed on text loop 1 had written. Per review-contract SS At the
  cap that ends the review of the document as it stands, so do NOT re-run the
  gate on it. The build is the third reviewer.

  Deferred tail, filed rather than fixed, all from lane open questions:
  - Model::boundsValid is never handled. SS 4.4 samples between boundsMin and
    boundsMax unconditionally, and that flag can be false. Two lanes raised it.
    Settle it when the builder is written; it is a precondition, not a design
    fork.
  - Model::leafHulls appears in Geometry.h and nowhere in the spec. If it
    carries per-leaf hull planes it would weaken SS 4.4's argument for
    whole-level sampling. Semantics were not checkable from the packet.
  - The census counted 2759160 leaves over the largest parsing Model per map,
    where Geometry.h's Leaf comment says 2784273 over all populated exports.
    Plausibly different populations; unverified, and neither figure is
    reproducible from the tree.
  - SS 4.2 cites the Unreal wiki's "1=front, 0=back" for zone 0 being the null
    zone. That string is about ARRAY indexing, not zone numbering. The
    conclusion stands on the measurement -- no leaf of 2759160 names zone 0 --
    rather than on the citation.
  - INV-3 says "within nodes.size() plane tests" where SS 4.3 says "past
    nodes.size()". An off-by-one in prose; the named test asserts termination
    only, so nothing builds differently.
  Progress (2026-09-08): the build half landed by session ut-ants-c9 --
  src/umap/Build.{h,cpp}, uta_umap_build with its own configure-time
  closure assertion, tier-1 cases in tests/unit/RoomBuildTest.cpp, INV-7's
  grep wired into scripts/ci.sh, and SS 7's tier-3 case in
  tests/real/RealInstallTest.cpp. Item stays in-progress pending the gate
  and the matrix.

  Every tier-1 case was mutation-graded rather than trusted for passing.
  Twenty-six mutations of the builder, all killed; five survived at first
  and every one was a fixture that never reached the rule it named -- the
  zone-0 guard (no leaf named zone 0), the corner tie-break (no component
  touched itself), the refusedZones dedup (one refusal), the band
  half-open edge (no room stood on one), and the node's own iZone record
  (every fixture stopped at a leaf). Fixtures were added for each. Two
  bounds checks were separately graded under AddressSanitizer.

  The build was the third reviewer, as review-contract SS At the cap says.
  It found a defect in SHIPPED code -- UTA-0078, upkg reading a BSP node's
  front and back children the wrong way round -- and four things this spec
  had not settled, all now recorded in it: SS 4.4's cell extent, SS 4.5's
  missing band 0 when no room is sampled, INV-2's probe set needing two
  more exclusions, and SS 7's argument for the hard form being falsified by
  measurement (UTA-0079 carries what that leaves open).

  Of the deferred tail, Model::boundsValid is settled: an invalid box is
  not a measurement, so it yields no samples, which is the state SS 4.4
  step 3 already defines. The other four are untouched.
  Resolved (2026-09-08): shipped at 5e66335 on the matrix -- GCC 14, Clang
  19 and MSVC all green, checked by headSha. Both libraries, all three
  test tiers, and INV-7's grep in scripts/ci.sh.

  Every invariant has a test. INV-1, INV-3, INV-4, INV-6 and INV-8 in
  tests/unit/{RoomBuild,RoomMap}Test.cpp; INV-5 at configure time in
  src/umap/CMakeLists.txt, both closures; INV-7 in scripts/ci.sh; INV-2
  and INV-6 again on real geometry in tests/real/RealInstallTest.cpp.

  INV-2 does NOT hold in the hard form SS 7 asked for, and that is
  recorded rather than papered over: 30399 probes of 11126404 disagree and
  the case asserts under 1%. UTA-0079 is open on it, and the spec now
  carries why -- that section's argument against a rate assumed a swapped
  convention would score near 100%, where it measured zero.

  The build was the third reviewer and earned it. It found UTA-0078, a
  field-order defect in shipped upkg code that no unit test could have
  seen, and settled four things the spec had left open.

  Deferred tail: Model::boundsValid is settled here. The other four --
  Model::leafHulls, the leaf-census discrepancy, SS 4.2's wiki citation,
  and INV-3's off-by-one prose -- are untouched and stay filed.
  **Layman:** Chop the level into rooms so the in-game map has something to draw, using the room divisions the original level already has.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: umap.

- ✅ [UTA-0008] **ubundle: define the .utab container, with its origin field and version.**
  The container for everything shipped as content: a baked map or an authored
  character. Owns the file layout and its version, never the meaning of a
  section's contents.
  Carries the origin field rule 15 requires -- authored when nothing out of
  anybody's UT install contributed, derived when something did, inherited from
  the most restrictive input. Two values, because a bundle has many sources: a
  community map draws on Epic's stock textures, so a per-map judgement would
  call it not-Epic's while its materials are Epic's throughout. Depends on unav and umap for
  their model types, never the reverse.
  Progress (2026-09-08): held by session ut-ants-c8. Writing the spec
  first -- spec-format.md § 1 fires on this four ways (a contract other
  code binds to, a new on-disk shape, a real design choice in the origin
  field, and version/forward-compatibility edge cases), and its closing
  line settles the one-subsystem question outright. Lane is ubundle,
  which shares no directory with UTA-0012's upkg.
  Progress (2026-09-08): spec accepted at
  docs/specs/UTA-0008-bundle-container-and-origin.md. review-contract
  reached its cap of 2 loops for a spec, six cold lanes, 19 verified
  findings all fixed and 3 dismissed; the tail is empty and the loop log
  carries both rows. Scope was settled by the user: the envelope plus
  the sections for RoomMap, NavGraph and WiringGraph, with nothing
  compressed in version 1. Still held by session ut-ants-c8;
  implementation has not started, so the item stays in-progress rather
  than returning to planned.
  Progress (2026-09-08): resumed by session ut-ants-02. The prior
  holder ut-ants-c8 is not in ListAgents, so the 🚧 was abandoned per
  CLAUDE.md § Running two sessions rule 2. Item stays 🚧, now held by
  ut-ants-02. Implementing from the accepted spec: src/ubundle/,
  tests/unit/BundleFormatTest.cpp and BundleMalformedTest.cpp. Lane is
  still ubundle, sharing no directory with UTA-0012's upkg.
  Resolved (2026-09-08): shipped by session ut-ants-02 at e9c623e, green
  on the matrix -- GCC 14, Clang 19 and MSVC all success. src/ubundle/
  holds Bundle.h, Bundle.cpp and a CMakeLists.txt whose INV-10 link
  assertion was proved to fire by adding uta_upkg. Tests are
  tests/unit/BundleFormatTest.cpp and BundleMalformedTest.cpp.

  Mutation-probed before believed: 57 mutations, one per rule the spec
  names, 54 killed. That found FOUR fixtures grading a rule other than
  the one they named -- the UTA-0007 failure this project's CLAUDE.md
  records -- and each was rebuilt. The tiling rule needed a fixture
  built for it, two sections declaring the same offset, because a gap or
  a backwards overlap is caught by the trailing-bytes check first.

  Two survivors are redundancies in the spec rather than gaps, recorded
  at their own cases: the ascending-offset rule is subsumed by tiling,
  and the zone-zero rule by the index-0 rule plus table agreement. No
  fixture can isolate either; both checks stay, being the spec's.

  INV-1 and INV-2 are bounds properties a plain test cannot grade, so
  they were measured under AddressSanitizer in a build directory of
  their own -- removing readBytes' bound reports a heap-buffer-overflow,
  and removing the allocation count check reports a 275 GB allocation
  request. Both invisible in the Release leg.

  UTA-0011 (ubake) is now unblocked, as are UTA-0013's origin check and
  UTA-0016's bundle load.
  **Layman:** Our own file format for a finished level -- and the field that records where its content came from, which is what keeps Epic's material off the network.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ubundle.

- ✅ [UTA-0009] **umat: generate a PBR material from a 1999 texture.**
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
  Scope settled (user, 2026-09-09). The flagged-not-decided question above
  is now decided, and this item may be built without re-asking it.

  An Internet-sourced replacement is REFERENCED by the recipe and fetched
  or supplied locally at bake time. It is never committed. The bake it
  feeds is derived and lands under content/ like every other bake, which
  is what design rule 15 and ADR-0003 already require -- so this needs no
  override file.

  A replacement may ship inside the curated library only where a licence
  permitting redistribution can be pointed at. Drawing that line is
  UTA-0010's, and it carries the matching note.

  For this item the consequence is narrow: the input contract admits the
  original 1999 texture AND a locally-supplied replacement, and must not
  assume the original is the only input.
  Water and glass (user, 2026-09-10): water sections must look and behave
  like water, and glass must look like glass, with cheap or faked
  reflections preferred. This item's part is identifying those surfaces
  from PolyFlags, which its body already plans, and carrying that tag
  into the material so the renderer can act on it. The appearance is
  UTA-0089 and the behaviour is UTA-0090.
  Deferred (2026-09-10): still `Next:`, but rule 1 of the user's order
  takes the review-sourced items first -- UTA-0094 to UTA-0100, filed that
  day from the optimisation pass. UTA-0098 and UTA-0100 defer themselves.
  Clear this note when this item is picked up.
  Decided by the user (2026-09-10): how a texture is enlarged.

  The baker enlarges with a classic, non-AI method built into it, suited to
  low-resolution game art. AI upscaling is an optional SEPARATE tool whose
  output is a replacement texture that a map's recipe points at -- the
  locally-supplied replacement this item's input contract already admits.
  No AI runs inside the baker.

  Why: docs/design.md's Content addressing requires every bake input to be
  covered by the map, the recipe or the baker version, and its Determinism
  bullet forbids platform maths libraries in the baker. ADR-0002 requires
  one map, recipe and baker version to produce the same bundle on any
  machine. AI inference usually differs by graphics card and depends on
  exactly those libraries, so running it inside the baker would have meant
  changing a design rule, adding a large dependency, and tracking model
  licences. Offered as three options (classic plus optional AI, classic
  only, AI in the baker); this is the one chosen.

  For the spec: the enlarger is subject to the numeric contract and needs
  a golden-bytes test on all three compilers, as UTA-0052's INV-6 is for
  compression. A replacement texture's bytes must enter the bundle's name
  through the recipe, or the name is a lie.
  Claimed (2026-09-10) by session `ut-ants-b3` in the MAIN checkout
  `/mnt/Games/Scripts/Linux/UT_Ants`. The rule-1 deferral recorded above is
  cleared: every open review-sourced item now defers itself (UTA-0059,
  UTA-0098, UTA-0100), so this is `Next:` under rule 2. Starting with the
  spec, around the user's upscaler decision of the same day.
  Premise measured false (2026-09-10), before the spec is drafted. This
  body says "the original texture's PolyFlags say which surfaces are
  glass, water, sky or self-lit". Over the reference install:

  - Of 34,041 texture objects in Textures/*.utx, NONE carries an explicit
    `PolyFlags` property. The flags are on SURFACES (upkg's `BspSurf` and
    `Polygon`), not on textures.
  - Surfaces sharing one texture often disagree: across 136,927
    texture-in-map pairs, masked 19%, translucent 30%, modulated 33%,
    unlit 32%, portal 51%, wavy 54%, sky 72%.

  So a water, glass, sky or self-lit tag stored on a MATERIAL would be
  wrong for a large share of its surfaces. The bake must carry the flags
  per surface to the renderer; this item cannot read them into a
  material. The spec scopes that out and says whose it is.

  The bit meanings were grounded by measurement with our own reader over
  9,012,193 BSP surfaces, against the constants UT_MonsterHunt's
  analysis/wallcheck.py uses: e.g. 66% of glass-named surfaces carry 0x4,
  30% of sky-named carry 0x80, 53% of lamp-named carry 0x400000. Several
  heavily used bits (0x8000, 0x80000 among them) have no meaning in either
  source and are not relied on.
  Enlarger chosen by measurement (2026-09-10). The user asked for research
  rather than a visual pick -- their eyesight makes a side-by-side
  comparison a poor grader -- so the choice rests on sources and on an
  objective test, not on anyone's eye.

  The test: one square texture (128 px or larger) from each of the
  install's texture packages -- 870 in all -- box-shrunk to a quarter,
  enlarged back x4, and scored against the original by mean PSNR. PIL's
  filters stood in for each method; the engine carries its own
  deterministic implementation of the one chosen.

    Lanczos-3   27.07 dB  (flat 32.60, detailed 26.21)  best on 735 of 870
    bicubic     26.93     (32.49, 26.06)                best on  75
    bilinear    26.42                                   best on   1
    nearest     25.94                                   best on  52
    Scale2x x2  25.86     (31.87, 24.93)                best on   7

  "Flat" is 64 or fewer distinct colours (117 textures). Lanczos leads on
  both kinds; the pixel-art rule scores below plain nearest. That matches
  the sources: Wikipedia's Image scaling recommends bicubic and sinc/
  Lanczos for continuous-tone images, and its Pixel-art scaling
  algorithms article says those scalers are for hand-drawn pixel art and
  their changes "may be undesirable" where faithful reproduction is the
  goal. Its Lanczos resampling article gives a = 2 or 3 as usual, and a = 2
  as having ringing under 1%.

  Decision for the spec: Lanczos. Caveats it must carry: PSNR measures
  faithfulness, not appearance; ringing halos at sharp edges; and masked
  textures need their transparent texels handled separately, or colour
  bleeds into the holes. Whether a = 2 or a = 3 is measured on the
  engine's own implementation.
  Decided by the user (2026-09-10): metal is ONE SETTING PER MATERIAL, not
  a per-texel map. Offered against guessing metal pixel by pixel from the
  image, which costs graphics memory on every texture and has nothing to
  guess from, since a 1999 texture carries no metal information. The
  curated library (UTA-0010) and a map's recipe mark real metal.

  Consequence: the five maps UTA-0052's section 4.2 budgets stand -- base
  colour, normal, roughness, height, emissive. docs/design.md's umat row
  lists "metallic" among what umat generates; the spec reads that as a
  per-material value and says so in its cross-doc section rather than
  reinterpreting design.md silently.

  Also binding here, from UTA-0104's clarification the same day: a
  material is identified by the package its texture came from, never by a
  bare texture name, so two creators' textures sharing a name never
  collide.
  Three decisions by the user (2026-09-10) bearing on this item.
  Animated textures show as a still picture in the first version, with
  animation filed separately. Replacement images and the AI enlarger
  come after the first version, replacements as PNG. Same-named
  packages are told apart by a content fingerprint, owned by UTA-0104.
  Progress (2026-09-10, ut-ants-b3, main checkout): built to the
  accepted spec -- src/umat Resolve, Enlarge, Derive and Generate, the
  INV-12 link amendment, tests/unit/MaterialGenerateTest.cpp, and the
  INV-13 census in tests/real/RealInstallTest.cpp. Every hand mutation of
  the new guards is killed, INV-12 refuses configure when broken, and the
  census keeps Lanczos at or above bicubic and above nearest. Open until
  the CI matrix is green.
  Shipped (2026-09-10): green on the CI matrix (GCC 14, Clang 19,
  MSVC) at cfe1e66, run 34477101077. Spec accepted after two cold
  review loops; every hand mutation of the new guards killed; INV-12
  seen to refuse configure; INV-13's census holds the ranking on the
  reference install (local-only tier, not on the matrix by design).
  **Layman:** Turn a flat 1999 texture into a modern one with depth and shine, worked out automatically from the original image.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: umat.

- ✅ [UTA-0010] **umat: the curated material library, shipped with the baker.**
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
  Scope settled (user, 2026-09-09). This item owns the rights check, and
  the matching note is on UTA-0009.

  The library may hold a replacement texture only where a licence
  permitting redistribution can be pointed at. Everything else is
  referenced by the recipe and supplied locally at bake time, never
  committed.

  So the absence of a demonstrable licence is what puts an asset outside
  the library, rather than its provenance. Art we authored and art under a
  permissive licence are on the same footing here.
  Claimed (2026-09-10) by ut-ants-b3, working in the main checkout.
  Lanes umat; UTA-0012 (the other held item) is upkg and tools, so no
  directory is shared. The body's "keyed by texture name" predates
  UTA-0009, whose accepted spec keys a material by package and group
  path (materialId); the user's later decision, below, replaced that identity with a
  picture fingerprint.
  Decided by the user (2026-09-10), on a measurement over the reference
  install: of the textures embedded in maps, a large share are exact
  pixel-and-palette copies of a packaged texture, half of them renamed,
  spread over roughly a quarter of the maps. So a curated entry matches
  the PICTURE itself (a fingerprint of its pixels and palette), not a
  package or texture name -- replacing this body's "keyed by texture
  name". First entries: a small seed chosen from what the textures say
  about themselves (metal, lights and screens, lava and the like),
  checked against the image data rather than by eye, growing as surfaces
  are found wrong in play.
  Progress (2026-09-10, ut-ants-b3, main checkout): spec accepted after
  two cold review loops -- docs/specs/UTA-0010-curated-material-library.md,
  loop log in docs/reviews/. Next: build it with write-code. Suggested
  order: Fingerprint and Library first with MaterialLibraryTest; then
  the INV-8 census case in tests/real/RealInstallTest.cpp, written to
  print every missing seed entry as a C++ table row -- that output IS
  the seed, so CuratedMaterials.cpp is filled from it and no scratch
  generator is needed. A scratch run of the same rules gave roughly six
  hundred entries, the glow rule dropping about two in five candidates
  on the image check. Also owed by the build: src/umat/Derive.h's
  roughnessOf comment says the library and recipe override the
  heuristic, but they set only baseRoughness, which it adds to; and the
  design.md umat row and UTA-0009 SS 4.6 amendments the spec's SS 11
  lists.
  Resumed (2026-09-10) by ut-ants-0c, working in the main checkout;
  ut-ants-b3 is no longer in ListAgents. Building to the accepted spec
  in the suggested order above.
  Shipped (2026-09-10, ut-ants-0c): green on the CI matrix at 01341f1,
  run 34486670530 -- GCC 14, Clang 19 and MSVC. The census reproduces
  the seed exactly and finds no fingerprint collision. Every hand
  mutation of the new guards is killed except one, which is inert over
  the reference install: the word steel in the metal-group rule decides
  no entry there.
  **Layman:** Hand-made materials for the surfaces you look at most, used in preference to the automatic ones.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: umat.

- 🚧 [UTA-0011] **ubake and the ut-bake CLI, including --check.**
  Drives upkg, umat, unav and umap and writes one bundle, content-addressed by
  its source map, its recipe and the baker version.
  --check validates an install, which is what both runtime targets run at startup
  rather than linking the package reader themselves (rule 16). Ships with both.
  Blocked-by: upkg, umat, unav, umap, ubundle.
  Obligations from UTA-0010's accepted spec
  (docs/specs/UTA-0010-curated-material-library.md): fold
  umat::libraryDigest() into the baker version (its § 4.6); apply
  settings in the order generated defaults, then the curated library,
  then the recipe, each through umat::applied (§ 4.5), with a recipe's
  requested upscale set directly (§ 4.3); pass pictureFingerprint only a
  texture with no Format property (§ 4.2).
  Claimed 2026-09-10 by session ut-ants-ec, in the main checkout.
  Rule-1 items UTA-0059, UTA-0098 and UTA-0100 all defer themselves.
  **Layman:** The tool that turns an old UT level into one of ours -- and the same tool the game runs to check you actually own Unreal Tournament.
  Kind: implement.
  Source: design-2026-09-03.
  Lanes: ubake.

- 🚧 [UTA-0012] **ut-dump: inspect a package from the command line.**
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
  Progress (2026-09-08): the tool exists and answers all three day-one
  queries, in 0fcaae2. tools/ut-dump, linking uta_unav_build so no new
  path runs from a runtime target into upkg. JSON only; no human mode,
  having no user for one. The third query was unblocked by UTA-0057.

  Measured over the requesting consumer's own library: 837 packages read
  whole -- imports, actor classes, Level, nav graph, wiring graph -- in 67
  seconds cold and 14 warm. It replaces a server boot per map, measured at
  48 seconds each on that machine.

  A defect worth keeping: walking an import's outer ONE link yields the
  GROUP, not the package, because a texture reference is
  Package.Group.Texture. It reported 587 maps as needing a package called
  "Base". Walking to the outermost fixed it, and the check that settles it
  is that an install which loads must have its dependencies met -- the
  count over 740 installed maps went from 732 to zero.

  Two things are deliberately NOT done, and the item is not shipped.
  Nothing has run on the matrix, only the CMake default leg (173/173, TSan
  clean). And there is no test, because the output shape is a surface
  versioning-overrides.md names by name, and this item has never had a
  spec -- writing one before a consumer binds to the shape is the cheaper
  order. The project is pre-0.1.0 with no release cut, so nothing is
  broken by settling it later.
  Note (2026-09-08), recorded by ut-ants-c7 so a cross-session agreement
  does not die with a chat window. This item was built by the
  ut-monsterhunt-bc session, which committed it into this checkout directly;
  it is on origin and the matrix is green on GCC 14, Clang 19 and MSVC.

  Agreed with that session, and it still stands:

  - It stays 🚧 and does NOT flip shipped without a short spec settling the
    ut-dump JSON shape, plus a test pinning it. Reason:
    docs/standards/versioning-overrides.md makes the ut-dump CLI and its output
    a versioned surface, and their consumer is a script over 740 maps -- so
    spec-format.md SS 1 trigger 1 fires, a contract something else binds to.
  - They are drafting that spec from the CONSUMER side, because they know what
    their script binds to and this project does not, and will hand it here for
    the review gate rather than landing it.
  - The contract they intend to pin is deliberately narrow: importedPackages
    (outermost names, de-duplicated), classCounts over the export table,
    level {actors, rawSlots, reachSpecs}, and nav/wiring counts plus the
    dangling list with its actor class. Everything else in the current output
    is incidental and is NOT to be pinned.

  Verified here rather than taken on trust: the outer-chain defect they hit in
  their own tool is not latent anywhere else in this tree. src/upkg/Class.cpp,
  src/unav/Build.cpp and tests/real/RealInstallTest.cpp all walk an import's
  outer chain to the root already, with a termination guard.
  Correction (2026-09-09), from the consuming session. **This body tells you
  to validate against their paths-index-evidence.md and to reproduce its four
  results. Result 3 is WITHDRAWN — do not reproduce it.**

  It eliminated the 16-slot-cap hypothesis. Re-measured 2026-09-09 by
  re-exporting and re-running: Village1 and Dust2-BP reproduced their earlier
  numbers exactly, which establishes the method is unchanged.
  AncientCavesTorus did not — it previously read 1202 Paths / 1274
  upstreamPaths with no actors at either 16-slot cap, and now reads 4453/4453
  with 45 and 49 actors AT the caps. The map's own export changed, not the
  measurement. Reproducing result 3 would mean reproducing something nobody
  believes any more.

  Results 1, 2 and 4 are unretracted and remain the right target.

  Two further hazards they measured, both of which this reader will meet.
  `strings` is not evidence about a package: it reported 14,441 names in
  Textures/Wood.utx whose real name table holds 190, because compressed pixel
  data matches identifier patterns. Their analysis/pkgnames.py parses the
  header tables and was validated against that 190 — so where our reader
  disagrees with a `strings`-derived figure in any document, our reader is
  probably right. And two unrelated packages may share a name, with the
  `Paths=` search order deciding which wins: Textures/wonderland.utx shadowed
  Sounds/wonderland.uax for ten maps because Default.ini searches Textures
  first. Resolution is search-order dependent, not name dependent.

  The `--json` / TSV work this body records is unchanged. The bot-path half
  is now split out as UTA-0086, with the shape the consumer confirmed: edge
  list first, node list beside it.

  The 740-map figure in this body is stale — see UTA-0087.
  Request from UT_MonsterHunt (2026-09-10), its first priority: a
  per-actor dump -- class, location, Tag, Event, and zone and trigger
  damage properties -- for actors within a radius of a point in one map.
  This is the second day-one query in this item's body, and `ut-dump`
  today emits counts only. A one-off scratch dump was run for them the
  same day; the supported mode is still owed here.
  The per-actor request of 2026-09-10 is withdrawn: UT_MonsterHunt
  solved the drain from the weapon scripts themselves. The supported
  per-actor mode is still worth having, and the scratch dump handed over
  that day is the shape it was measured against.
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
  Constraint (2026-09-08): UTA-0075 requires this render path to carry a
  sub-pixel camera jitter and write a per-pixel motion-vector buffer, and
  to composite the UI after upscaling rather than into it. Those are the
  shared inputs of every temporal upscaler and of TAA. A velocity buffer
  is written by every draw that moves, so it is not a post-process that
  can be bolted on afterwards -- read UTA-0075 before settling the render
  graph, or adding it later touches every pass.
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
  Deferred out of 0.1.0 (2026-09-08, user decision). Not required by the
  release's cut condition: versioning-overrides.md cuts 0.1.0 on S1 and
  S7 alone, and S1 asks only that the map be recognisable, that shadows
  move as you move, that surfaces have real depth, and that light shafts
  cut through fog. Do not count this item when judging what is left for
  the release. It remains filed under 0.1.0 only because no roadmap verb
  moves an item between sections and the store reverts a hand edit to
  ROADMAP.md; a re-section op is requested in the Ants MCP feedback
  file. This item is additionally blocked by a design edit that has not
  been made -- design.md does not list subsurface scattering among
  urender's responsibilities.
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
  Deferred out of 0.1.0 (2026-09-08, user decision). Not required by the
  release's cut condition: versioning-overrides.md cuts 0.1.0 on S1 and
  S7 alone, and S1 asks only that the map be recognisable, that shadows
  move as you move, that surfaces have real depth, and that light shafts
  cut through fog. Do not count this item when judging what is left for
  the release. It remains filed under 0.1.0 only because no roadmap verb
  moves an item between sections and the store reverts a hand edit to
  ROADMAP.md; a re-section op is requested in the Ants MCP feedback
  file. Blocked behind UTA-0014 regardless, so it is not selectable
  before the renderer exists.
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
  Note (2026-09-09), from UTA-0052's scope decision: **the texture memory
  budget binds to this item, and this item does not block it.**

  UTA-0052's body said the baker refuses a bake exceeding "the tier's
  budget", which read as though it waited on the tiers defined here. It does
  not, and waiting would have been wrong — this item is blocked behind the
  bundle draw path, so the texture budget would have been pushed past the
  whole renderer, with UTA-0009 stalled behind it.

  Settled the other way by the user: UTA-0052 defines the budget as an
  absolute megabyte figure for a map's texture working set, starting from the
  development card's 2 GB. **When this item lands, it declares which tier
  maps to which figure.** That is a value this item supplies, not a contract
  it imposes.

  Nothing here changes. Recorded so a session building the tiers knows the
  budget number already exists and does not invent a second one.
  **Layman:** One quality setting that actually works: the game picks a sensible level for your machine, leaves the expensive effects off on weak hardware, and quietly lowers resolution rather than stuttering.
  Kind: implement.
  Source: user-request-2026-09-04.
  Lanes: urender.

- ✅ [UTA-0052] **umat: a texture memory budget, with block compression and a per-material upscale cap.**
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
  Progress (2026-09-09): held by session `ut-ants-17`, working in the MAIN
  checkout at `/mnt/Games/Scripts/Linux/UT_Ants`. Sole live session on this
  project — `ListAgents` shows no peer that could be working it, which is the
  condition rule 3 requires before keeping the main checkout.

  Taken as `Next:` on the user's call (2026-09-09), ahead of `UTA-0009`.
  Both items' bodies already recorded that this one wants to land first, and
  its `Blocked-by: ubundle` cleared when `UTA-0008` shipped on 2026-09-08.

  **Starting position, measured rather than assumed.** There is no
  `src/umat/` yet, so this is greenfield. `src/ubundle/Bundle.h` emits
  sections in the fixed order ROOM, NAVG, WIRG — a material or texture
  section does not exist, so this item decides what a bundle stores as well
  as how a texture is compressed. That is why `UTA-0052` is wanted before
  `UTA-0009`: retrofitting would regenerate every material and change the
  container.

  **A spec is owed and will be written first.** `spec-format.md` § 1 fires
  several ways — a contract other code binds to (the bundle's own layout),
  more than one subsystem (`umat` and `ubundle`, per this item's own
  `Lanes:`), and a real design choice in the budget's shape. Not a case where
  the roadmap bullet plus `write-code` is the whole contract.

  Next action: `write-spec` for `UTA-0052`, then its `review-contract` gate
  before any code.
  Scope settled (user, 2026-09-09), before the spec was drafted. Two
  questions, both raised because this body referred to something that does
  not exist.

  **1. The budget is ABSOLUTE MEGABYTES of texture working set per baked
  map.** This body said the baker "refuses a bake that exceeds the tier's
  budget", and tiers are `UTA-0051` — which is 📋 and itself blocked behind
  the renderer, two items away. So the budget was defined against something
  unbuilt and this item's `Blocked-by:` never said so.

  **The dependency runs the other way and this item does not wait.**
  `UTA-0051` later declares which tier maps to which megabyte figure; it
  binds to this number, not the reverse. The first value is the development
  card's 2 GB. The mechanism — compress, cap the upscale, measure, refuse —
  is unchanged whatever the number becomes, which is why it can be settled
  now.

  **2. Over budget, the bake REFUSES and reports what it measured** — the
  working set it produced and the budget it exceeded. Not automatic
  degradation. Two people baking one map must not silently get different
  quality, and a reduction nobody was told about is the failure that surfaces
  months later on somebody else's laptop. An opt-in `--fit-budget` was
  offered and NOT taken; do not add one without asking again.

  **Also settled by reading the code rather than assuming.** There is no
  `src/umat/` yet and `Bundle.h` emits sections in the fixed order ROOM,
  NAVG, WIRG with no material or texture section. `FORMAT_VERSION` is 1 and
  is checked for EQUALITY, and that header's own comment records that adding
  a byte later bumps the version at a cost `versioning-overrides.md` owns. So
  where the texture data lives is a breaking-surface decision this item's
  spec must make deliberately, not an implementation detail.
  Constraints found by reading UTA-0008's shipped invariants (2026-09-09),
  before drafting. `invariant_check` named that spec as governing
  `src/ubundle/Bundle.h`; these are its clauses, not new decisions.

  **The container already reserves a `compression` byte, and v1 requires it
  to be ZERO.** UTA-0008's INV-12 refuses a section whose `compression` byte
  is non-zero. So this item does not add the field — it defines what a
  non-zero value means, which is a smaller and better-shaped job than the
  body assumed.

  **A texture section forces a format version bump.** INV-4 checks
  `formatVersion` for EQUALITY against 1, before the section table is read,
  and INV-11 makes an undefined section id `MalformedData`. So a v1 reader
  refuses a bundle carrying textures, by design. The bump is a breaking
  surface `docs/standards/versioning-overrides.md` governs, and UTA-0008's
  INV-4 needs amending in the same change rather than left contradicting it.

  **The block compressor MUST be deterministic and identical across
  compilers, and this is the constraint most likely to be missed.** INV-8
  requires `write` to be byte-identical for equal input. INV-7 pins `write`
  against a golden byte array fixed in the source, which the CI matrix runs
  on GCC, Clang and MSVC alike — any leg whose bytes differ goes red on its
  own. `docs/design.md` § Close calls then names a bundle by the hash of its
  contents, so a non-deterministic encoder breaks content addressing as well
  as the tests.

  Many BC7 encoders are neither: they are multithreaded, heuristic, and
  tuned per platform. **So the encoder is a spec-level decision, not an
  implementation detail** — it must be selected or written for reproducible
  output, and the spec has to say how that is verified rather than assumed.

  **`ubundle` may not link `umat`.** INV-10 pins its link entries to
  `uta_core`, `uta_umap` and `uta_unav`, asserted at configure time. The
  texture payload therefore crosses as bytes `ubundle` does not interpret,
  which matches its stated scope — the file layout, never the meaning of a
  section's contents.
  Progress (2026-09-09): resumed by session `ut-ants-36`, working in the
  MAIN checkout at `/mnt/Games/Scripts/Linux/UT_Ants`. The previous holder
  `ut-ants-17` is absent from `ListAgents`, so its claim was abandoned and
  rule 2 makes the item resumable. `ListAgents` shows two live peers,
  `ants-terminal-ff` and `ut-monsterhunt-b9`, neither of which is a
  UT_Ants session — so no live peer holds the main checkout and rule 3
  allows keeping it.

  Next action unchanged: `write-spec` for UTA-0052, then its
  `review-contract` gate at genre `spec`.
  Three more decisions settled (user, 2026-09-09), before §4 was drafted. Each
  was raised because the body left it open and it changes what gets built.

  **3. The block encoder is VENDORED `bc7enc`** — `bc7enc.c`/`.h` for BC7 and
  `rgbcx.h` for BC1–BC5, from `richgel999/bc7enc`, MIT or public domain, pinned
  at an exact commit. `ADR-0007` question 4 routes it to route 2 (vendored, like Dear ImGui): its build system builds a demo executable rather than a library anyone links, so nothing new enters the
  toolchain. Its BC7 encoder is scalar and not vectorized and it threads
  nothing itself, so parallelising over 4×4 blocks with `core`'s job system
  cannot change the bytes. Upstream claims NO determinism, so the spec proves it
  with a golden byte array on all three CI legs rather than assuming it —
  `UTA-0008`'s INV-7 pattern.

  Rejected: writing our own (BC7 mode selection and endpoint fitting are weeks
  of work and would stall `UTA-0009`); a split of our own BC4/BC5 plus a
  vendored BC7 (two deterministic code paths and two fixtures to save a few
  hundred lines); and ISPC (`bc7e`, `ispc_texcomp`), which needs Intel's ISPC
  compiler as a fourth compiler and whose own README says determinism across
  Intel and AMD needs the targets limited to SSE with fast math off — that makes
  determinism a build-configuration argument rather than a property.

  **4. The first budget figure is 1024 MB of texture working set per baked
  map** — half the GTX 1050's 2 GB, leaving the other half for the G-buffer and
  depth at 1080p, shadow maps, geometry, volumetric grids and the desktop
  compositor. The body's "the development card's 2 GB" is the CARD, not the
  texture budget: spending all of it on textures leaves the renderer nothing and
  the guard could never refuse a bake that will not run. `UTA-0051` re-declares
  the figure per tier when it lands.

  **5. `UTA-0084` gets no CHANGELOG entry.** It changed how sessions coordinate,
  not anything a player or a server operator can see. `UTA-0081` settles the bar
  for the other four shipped items separately.
  Spec written and gated (2026-09-09).
  `docs/specs/UTA-0052-texture-memory-budget.md`, `accepted (2026-09-09)`.
  Loop log: `docs/reviews/UTA-0052-texture-memory-budget-loop-log.md`.

  `review-contract` at genre `spec`, three cold lanes per loop, **cap of 2
  reached**. Nineteen verified, nineteen fixed, one dismissed. Loop 1 eleven
  (Q1 1 / Q2 5 / Q3 3 / Q4 2), loop 2 eight (Q1 3 / Q2 2 / Q3 2 / Q4 1).
  Deferred tail: empty.

  **The cap was oscillating rather than calm.** Five of loop 2's eight
  findings landed on text loop 1 wrote — including an INV-8 assertion that
  would have stopped configuration on every CI leg of a correct tree, found
  by all three lanes and reproduced with a throwaway CMake project. So this
  gate is not re-run on the document as it stands: a spec's cap routes to
  implementation, which exercises the contract against real code.

  **What the spec decides, beyond the five scope decisions above.** A `TEXS`
  section in the `.utab`, `FORMAT_VERSION` 2, with a per-texture format tag
  rather than the section `compression` byte. BC7 for colour, BC5 for
  two-channel normals, **BC4 for the single-channel roughness and height maps**
  — the roadmap named two formats and the third follows from the same
  argument. The honest ratio across the five maps is **one third**, not the
  quarter this body states: a quarter is BC7 against `RGBA8`, true per format
  and not true of the set.

  **Next action: implement it.** `src/umat/` is greenfield; the build order is
  the container half (`ubundle`'s `CompressedTexture`, `TEXS`, the version
  bump and UTA-0008's seven amendments) before the encoder half, because the
  second writes into the first. `UTA-0009` is unblocked once this lands.

  **Two things the spec leaves for whoever builds it**, both in its § 15:
  `bc7enc.h`'s licence coverage is unsettled — the upstream `LICENSE`
  enumerates `bc7enc.c` and not its header — and must be settled before the
  file is copied in; and whether the vendored encoder reaches a platform maths
  function, which `docs/design.md` rules out of the baker.
  Deferred (2026-09-09) behind `UTA-0088`, which the priority order's rule 1
  puts first: it is review-sourced and open. `CLAUDE.md`'s `Next:` still
  names this item and is left alone, per § Which item comes next. Clear
  this note when implementation starts. Nothing is half-built yet — the
  spec is written and gated, no code exists.
  Progress (2026-09-09): **the deferral above is cleared** — `UTA-0088`
  shipped, rule 1's set is empty apart from `UTA-0059`, which defers itself
  until the renderer lands. Implementation starts now.

  Held by session `ut-ants-2d` in the MAIN checkout
  `/mnt/Games/Scripts/Linux/UT_Ants`. No peer session is working this
  project; `UTA-0012` is 🚧 from an earlier session whose name is no longer
  live, so it is abandoned and resumable rather than a live claim.

  Build order per the spec's § 11: the container half first — `ubundle`'s
  `CompressedTexture` and `BlockFormat`, the `TEXS` section, `FORMAT_VERSION`
  1 to 2, and `UTA-0008`'s seven amendments — then the encoder half, because
  the encoder writes into the container.
  Progress (2026-09-09): **the container half is done.** `ubundle` carries
  the `TEXS` section at `FORMAT_VERSION` 2, with `BlockFormat`,
  `MAX_UPSCALE_FACTOR`, `bytesPerBlock`, `CompressedTexture` and
  `expectedBlockBytes`. INV-1 to INV-5 and INV-13 are covered by
  `tests/unit/BundleTextureTest.cpp`. UTA-0008's seven amendments have
  landed and the CHANGELOG entry is filed. Full gate green on the local
  leg, ThreadSanitizer included; 219 unit tests.

  **Every new guard was mutated and confirmed graded** — ten mutations, ten
  killed, no survivors. Two of the tests were mis-graded when first written
  and only mutation found them: the power-of-two case declared 32 block
  bytes where its own dimensions need 48, so INV-1 refused it first, and the
  `sourceWidth > width` case is caught by the axes-agree clause rather than
  the exact-multiple one.

  Remaining: the `umat` half — `third_party/bc7enc/`, `src/umat/`, and
  INV-6 to INV-12 and INV-14.

  **§ 15's licence question is now a blocker rather than a note.** The spec
  requires `bc7enc.h`'s coverage settled BEFORE the file is copied into this
  GPL-3.0 repository, and the vendoring is the next step.
  Progress (2026-09-09), superseding the note above: **the licence question
  is SETTLED and the vendoring is DONE.** That note called it a blocker; it
  is not one. `bc7enc.h` carries its own dual-licence notice on its first
  line and the pointer resolves to the full text at `bc7enc.c`'s tail, so
  the upstream `LICENSE`'s omission is shorthand. `third_party/bc7enc/` is
  in at commit `f66c2e48` with a `README.md` recording repository, commit,
  date and a sha256 per file.

  **§ 15's OTHER question is settled too, and the answer needs the user.**
  `bc7enc.c` includes `<math.h>`, so the LETTER of `docs/design.md`'s
  Determinism bullet — *no platform maths library in the simulation or the
  baker* — is breached by this dependency. What it calls is `sqrtf` twice,
  `floor`/`floorf` five times and `fabs`/`fabsf` four times, with **no**
  `pow`, `exp`, `log`, `sin`, `cos`, `tan`, `atan2`, `cbrt` or `hypot`;
  `rgbcx.h` calls `fabs` alone. IEEE-754 pins every one of those exactly,
  so `ADR-0002`'s cross-compiler hash equality — that rule's own stated
  ground — holds while its wording does not.

  Recorded in `docs/specs/UTA-0052-texture-memory-budget.md` § 15 and in
  `third_party/bc7enc/README.md`, not resolved silently. The alternatives
  are patching vendored source or reopening § 3 decision 4. INV-6 is the
  grader either way. **Proceeding on that reading; the user may overturn
  it.**

  **Next: `src/umat/` — `Material.h`/`.cpp`, `CMakeLists.txt` with INV-8's
  and INV-12's configure-time assertions, `add_subdirectory(umat)`, and
  `tests/unit/MaterialCompressTest.cpp` for INV-6 to INV-12 and INV-14.**
  Progress (2026-09-10): resumed by session `ut-ants-b3` in the MAIN
  checkout `/mnt/Games/Scripts/Linux/UT_Ants`. `ut-ants-2d` is no longer
  live. The only peer session is `ants-terminal-14`, another project.
  Starting the `umat` half: `src/umat/`, its two configure-time
  assertions, and `tests/unit/MaterialCompressTest.cpp`.
  Progress (2026-09-10): **the umat half is built** -- `src/umat/`, both
  configure-time assertions, and `tests/unit/MaterialCompressTest.cpp`.
  Each configure assertion was broken once and stopped configure: a
  forbidden link (INV-12), `-ffast-math` on the target and on `bc7enc.c`
  alone (INV-8). Twenty-five hand mutations of `Material.cpp`; the one
  survivor was a fixture graded by a neighbouring rule, now fixed.

  **The container half had the same defect, three times.** Mutating
  `Bundle.cpp`'s width ceiling, height power-of-two and height ceiling --
  none of them in the earlier ten mutations -- left every test green. The
  width-ceiling case was a 16384x1 texture from an 8192x1 source, which
  the axes-agree rule refuses first; the two height clauses had no case.
  `BundleTextureTest.cpp` now carries a both-axes-agree ceiling case and
  one case per height clause.

  Remaining: re-mutate those three, the Clang leg, the full gate, and the
  matrix.
  Shipped (2026-09-10), on the matrix: CI run 34461826976 at `10a42ad`
  is green on Linux GCC 14, Linux Clang 19 and Windows MSVC. INV-6's golden
  arrays were captured on GCC, so the two other legs passing is the
  cross-compiler check the invariant exists for. The design.md Determinism
  reading recorded in the spec's SS 15 stands: bc7enc's `sqrtf`, `floor`
  and `fabs` did not move a byte between compilers.

  Local leg: 226/226, ThreadSanitizer clean, full `./scripts/ci.sh` green
  through the pre-push gate.
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
  Deferred out of 0.1.0 (2026-09-08, user decision). Not required by the
  release's cut condition: versioning-overrides.md cuts 0.1.0 on S1 and
  S7 alone, and S1 asks only that the map be recognisable, that shadows
  move as you move, that surfaces have real depth, and that light shafts
  cut through fog. Do not count this item when judging what is left for
  the release. It remains filed under 0.1.0 only because no roadmap verb
  moves an item between sections and the store reverts a hand edit to
  ROADMAP.md; a re-section op is requested in the Ants MCP feedback
  file. Blocked behind UTA-0014 regardless, so it is not selectable
  before the renderer exists.
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
  Deferred out of 0.1.0 (2026-09-08, user decision). Not required by the
  release's cut condition: versioning-overrides.md cuts 0.1.0 on S1 and
  S7 alone, and S1 asks only that the map be recognisable, that shadows
  move as you move, that surfaces have real depth, and that light shafts
  cut through fog. Do not count this item when judging what is left for
  the release. It remains filed under 0.1.0 only because no roadmap verb
  moves an item between sections and the store reverts a hand edit to
  ROADMAP.md; a re-section op is requested in the Ants MCP feedback
  file. Blocked behind UTA-0014 regardless, so it is not selectable
  before the renderer exists.
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
  Deferred out of 0.1.0 (2026-09-08, user decision). Not required by the
  release's cut condition: versioning-overrides.md cuts 0.1.0 on S1 and
  S7 alone, and S1 asks only that the map be recognisable, that shadows
  move as you move, that surfaces have real depth, and that light shafts
  cut through fog. Do not count this item when judging what is left for
  the release. It remains filed under 0.1.0 only because no roadmap verb
  moves an item between sections and the store reverts a hand edit to
  ROADMAP.md; a re-section op is requested in the Ants MCP feedback
  file. Blocked behind UTA-0014 regardless, so it is not selectable
  before the renderer exists.
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

- ✅ [UTA-0069] **upkg: derive the Model BSP tables.**
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
  Progress (2026-09-08): resumed by session ut-ants-26. The previous
  holder ut-ants-b2 ended in a deliberate terminal restart and is not in
  ListAgents, so its claim was abandoned rather than damaged (CLAUDE.md
  rule 2). Nothing was left half-written. Next: the bounds residue, then
  lightmap bytes, per SS 4.6 file order.
  Progress (2026-09-08, ut-ants-26): the lightmap element is a fixed
  thirty bytes, and SS 4.5's layout for it is wrong in three ways at once.
  Measured order: DataOffset as a raw i32, Pan, UClamp and VClamp one byte
  each, UScale, VScale, iLightActors as a raw i32. SS 4.5 states two
  leading compact indices and four-byte clamps.

  Reading DataOffset as a compact index stranded the cursor for the whole
  second run of tables: a first byte of 0x85 decodes to -5 and consumes
  one byte where the field is four. So the walk reached the later tables
  misaligned and blamed whichever one it stopped in. bounds carried the
  largest bucket while being correct -- of the exports refusing there,
  2191 declared zero bounds and 2047 a negative count, and no element
  width explained more than six.

  Derivation: swept the element width against a signature requiring the
  rest of the export to land exactly on its final byte. Thirty is the sole
  fit for 9455 exports, against eighteen for the runner-up, so this is not
  the alias trap. Corroborated semantically as well -- DataOffset lands
  inside the export's own LightBits array and never decreases,
  iLightActors is -1 or indexes Lights, and both scales are finite and
  positive, on all 306706 entries in the reference install. Reading the
  scales two bytes earlier holds for 31 percent of them.

  Install-wide: consumed exactly 550372 to 559932, refused 10980 to 1420.
  The lightmap entries bucket is empty. Residue now, in file order --
  lightmap bytes 408, bounds 195, leaf hulls 156, leaves 174, leaves count
  19, lights 49, wrong end offset 185, package version below 62 234.
  Packages whose largest Model parses 4 to 6 of 847, so INV-4 stays red.

  Roughly 1.4 refusals per package now, across 842 of 847. That shape says
  the residue is the one large level Model per map rather than a spread.

  Shipped in c4783ca with three mutations killed. SS 4.5 and SS 4.6 now
  disagree with the code; the amendment is a deliberate decision, not
  folded into that commit.
  Progress (2026-09-08, ut-ants-26): SS 4.6's residue is closed. Every
  Model export at package version 62 or above in the reference install is
  consumed exactly. Tier 3 is green.

  Two more findings after the lightmap element's width and order.

  The clamps are compact, not fixed-width. UClamp and VClamp are compact
  indices: 0x40 is the continue bit, so a clamp of 64 or more takes two
  bytes where 16 or 32 takes one. Read at a fixed thirty the element
  consumed 9560 exports and then shifted every later element of any export
  holding a large clamp. Seen directly rather than swept, in one
  MH-TheBoxWorld2 Model whose elements drift a byte at a time and where
  the element starting the drift reads 04 40 01 against its neighbours' 10
  10. This does NOT reopen the earlier ruling that the clamps are not
  compact: that was measured against SS 4.5's layout, where the clamps
  trail two LEADING compact indices, and it is correct there. The section
  has the pair the wrong way round -- the offsets are the raw fields.

  Leaves is derived. Three compact indices, iZone, iPermeating and
  iVolumetric, then a 64-bit zone mask. Eleven bytes at its smallest. Sole
  fit for all 842 exports that populate the table, every permutation of
  the same four fields fits none of them, and iZone indexes the export's
  own zone table on all 2784273 leaves. SS 4.1 reserved the name against
  this, so adding the member is not a rename.

  Install-wide across the whole session: consumed exactly 550372 to
  561118, refused 10980 to 234. Packages whose largest Model parses 4 to
  846 of 847. The 234 are the version-61 class in one package, which the
  user scoped into UTA-0072 on 2026-09-07.

  INV-4's assertion now reads "every refusal is that scoped-out class and
  there is no other" rather than "refused equals zero". It tightens back
  to zero on its own when UTA-0072 lands, because the bucket empties.

  Shipped in c4783ca, 9dd7ff3 and 15f545a, ten mutations killed across the
  three. Not yet flipped: awaiting the GitHub matrix, per CLAUDE.md.
  Resolved (2026-09-08, ut-ants-26): acceptance met. The real-asset tier
  consumes every Model export exactly at every package version this reader
  claims -- 561118 of them, tier 3 green, all three matrix legs green at
  8c10009 (GCC 14, Clang 19, MSVC), checked by headSha.

  Packages whose LARGEST Model parses: 846 of 847, from 4 when the session
  started. That is the figure UTA-0007 and UTA-0011 actually need.

  The one package that does not is the corpus's only version-61 one, whose
  Models keep their BSP tables in separate exports. readModel refuses
  those by name and UTA-0072 reads them, by the user's ruling of
  2026-09-07. INV-4's assertion is scoped to exactly that class and
  tightens back to zero on its own when UTA-0072 lands.

  Spec amended to what was built (8cde872, 8c10009): SS 4.6 is the record
  of how the residue closed, SS 4.5 carries the corrected FLightMapIndex
  and the new FLeaf, SS 4.1 returns leaves, and INV-4, SS 6 and SS 8
  follow. No review gate re-armed -- CLAUDE.md rule 14's own instance for
  an amendment recording what was built.
  Corroboration (2026-09-08), REPORTED NOT VERIFIED HERE. The
  ut-monsterhunt-bc session ran readModel and readLevel over 1110 freshly
  downloaded maps -- content that has never been in the reference install and
  that neither this item's tests nor UTA-0057's have ever seen. Every one
  opened, every one yielded a readable Level, in 84 seconds, zero failures;
  1037 were judged clean enough to play. Earlier the same session put the whole
  reference install at 67s cold and 14s warm for both graphs plus the full
  read, ~1 GB peak RSS.

  Recorded because it is the strongest external evidence these readers have --
  the install is the corpus they were derived FROM, so agreement there is
  partly circular, and this is not. It is filed here rather than in the spec
  for two reasons: this session did not run it and cannot point at a command or
  an output that backs it, and UTA-0069's spec is accepted with its gate
  complete. Treat it as a lead worth re-measuring, not as a figure to cite.

  Same caveat applies to UTA-0057, whose readLevel tail the run exercised
  equally.
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

- ✅ [UTA-0074] **UTA-0004's spec cites a test file that does not exist, and one claim in it is stale.**
  docs/specs/UTA-0004-typed-level-content.md names
  tests/unit/PackageMalformedContentTest.cpp in four places. No such file
  exists; the real one is tests/unit/PackageMalformedTest.cpp.

  Two of the four are INV *Test:* clauses, which is what makes this more
  than cosmetic: an invariant whose test clause points at nothing cannot
  be verified from the document, and spec_lint cannot catch it on this
  project -- it reports surfaces_checked false here, because it resolves
  surfaces only in a tests/features/<name>/ layout and this project uses
  tests/unit/. The other two are the test inventory and INV-2's grade row.

  Separately, INV-2's grade row says the malformed tier "does not reach
  readModel at all, whose layout is withheld". That was true when it was
  written and is not now: UTA-0069 shipped on 2026-09-08, no table is
  withheld, and tier 1 reaches readModel through both content and
  malformed cases. Re-grade the row rather than only re-pointing the path.

  Found while closing UTA-0069, and filed rather than fixed there: it is
  UTA-0004's document, and a drive-by edit to another item's spec is the
  change nobody reviews. The earlier session recorded it in a session
  handoff, which is not a place work survives.

  Blocked-by: nothing.
  Progress (2026-09-08): picked up by session ut-ants-c7, ahead of
  UTA-0007, on the user's restated priority order.
  Resolved (2026-09-08) by ut-ants-c7 at 290252c, matrix green.

  The four citations now name tests/unit/PackageMalformedTest.cpp, and every
  tests/ and src/ path the spec cites resolves on disk.

  The stale claim was in seven passages, not one. "readModel's layout is
  withheld, so there is nothing to malform" was the shared premise of INV-2's
  and INV-4's test clauses, the INV-1, INV-2 and INV-4 grade rows, the test
  inventory's tier-1 entry, and SS 4.10's builder note. Fixing only the row
  this bullet named would have left the document asserting both that readModel
  has no fixture case and that it has three.

  Regraded against measurement. The content tier reads a Model's tables back at
  known counts, places its elements at the file's own indices, and refuses one
  with bytes left over; the malformed tier declares more nodes than the export
  holds, a negative node count, and more leaves than it holds, and asserts a
  valid fixture-built Model parses. SS 4.10's claim that the SHARED builder
  builds no Model is still true and was kept -- only its reason had died, the
  Model fixtures living in the unit-test files' own helpers.

  A third defect of the same class was found and fixed here: INV-3's clause said
  its grep returns nothing, and it now returns a Geometry.cpp comment. The
  invariant is untouched -- no line compares against 61 or 69 -- and the clause
  records that this is the second time it was written tighter than what it tests.

  The cold-eyes loop-log row carries the same dead premise and was deliberately
  left as written.

  No gate: every edit corrects a citation or records what UTA-0069 built.
  No CHANGELOG entry: internal document hygiene, not a notable change.
  **Layman:** A design document points at a test file by the wrong name, so nobody can check the promises it makes.
  Kind: doc-fix.
  Source: in-session-2026-09-08.

- 📋 [UTA-0075] **urender: jitter the camera and produce a motion-vector buffer.**
  The prerequisite every temporal upscaler shares, filed separately from the
  integration because it is a constraint on UTA-0014's render graph rather
  than a feature.

  FSR, XeSS and DLSS take the SAME three inputs: a sub-pixel camera jitter
  applied to the projection matrix each frame, a per-pixel motion-vector
  buffer, and depth. All three also want a negative texture mip bias to match
  the lower render resolution, and all three need the UI composited AFTER
  upscaling rather than drawn into the upscaled image. Verified against
  Intel's XeSS-SR developer guide and AMD's FSR documentation, which agree on
  the input set and on motion vectors excluding jitter-induced motion.

  So the choice of upscaler decides almost nothing here, and picking one
  later costs nothing extra. What costs is building the first draw path
  without provision for these: a velocity buffer is not a post-process bolted
  on at the end, it is written by every draw that moves, and adding it after
  the render graph is settled touches every pass.

  It pays even if no upscaler is ever integrated: these are also exactly the
  inputs temporal anti-aliasing needs, and TAA is wanted regardless.

  Blocked-by: UTA-0014.
  Note (2026-09-08): filed in 0.1.0 because it constrains the renderer built
  there. It may move to a later milestone freely PROVIDED it lands before the
  render graph has passes built on top of it -- the ordering is the point,
  not the milestone.
  Deferred out of 0.1.0 (2026-09-08, user decision). Not required by the
  release's cut condition: versioning-overrides.md cuts 0.1.0 on S1 and
  S7 alone, and S1 says nothing about resolution or frame rate. Do not
  count this item when judging what is left for the release. It remains
  filed under 0.1.0 only because no roadmap verb moves an item between
  sections and the store reverts a hand edit to ROADMAP.md; a re-section
  op is requested in the Ants MCP feedback file. Its own body argues it
  is cheap now and expensive to retrofit; that argument is worth
  revisiting when UTA-0014 lands, and it is an argument about ordering
  rather than about the release condition.
  **Layman:** Groundwork that lets the game render at a lower resolution and scale it up cleanly later. Cheap to build in now, expensive to retrofit.
  Kind: implement.
  Source: user-request-2026-09-08.
  Lanes: urender.

- 📋 [UTA-0076] **Temporal upscaling: FSR 3.1 first, XeSS second.**
  Integration, once the inputs item has landed. Researched 2026-09-08; the
  ordering below is forced by this project's own constraints, not by quality
  rankings.

  **FSR 4 is ruled out and this is the load-bearing finding.** It is
  DirectX 12 only and cannot be integrated into a Vulkan title. design.md
  pins Vulkan 1.3 with no OpenGL fallback, so the newest and best-looking
  AMD upscaler is simply unavailable to us. Do not spend time on it; check
  whether that has changed before acting on this item, since it is the one
  fact here most likely to move.

  **FSR 3.1 is the first choice.** It is the release that added Vulkan
  support, it is cross-vendor, and it is permissively licensed and shipped as
  source rather than a signed binary -- which matters for a project that
  builds on Linux and Windows from one tree.

  **XeSS is the second.** It has a Vulkan path, and from SDK 2.1 its
  networks run on non-Intel GPUs through DP4a, so it is not Arc-only. Its
  Vulkan path does not support the external-descriptor-heap flag; that is a
  note for whoever integrates, not an obstacle.

  **DLSS is optional and last.** Vulkan-capable but NVIDIA hardware only and
  proprietary, so it can never be the baseline -- it is an addition for the
  players who have the hardware, worth doing only once one cross-vendor path
  is working.

  **Honest caveat on the premise.** Upscaling buys frames by rendering fewer
  pixels, so it repays only where the GPU is the bottleneck. A UT99 map at
  1999 geometry densities may well not be, and the renderer does not exist
  yet to measure. What makes it plausible here is what UTA-0014 plans on top
  of that geometry -- dynamic lights with shadow maps, PBR, volumetrics,
  light shafts, ambient occlusion. Measure before integrating; the
  prerequisite item is worth doing either way, this one is not.

  Blocked-by: the motion-vector and jitter item, and UTA-0014.
  Deferred out of 0.1.0 (2026-09-08, user decision). Not required by the
  release's cut condition: versioning-overrides.md cuts 0.1.0 on S1 and
  S7 alone, and S1 says nothing about resolution or frame rate. Do not
  count this item when judging what is left for the release. It remains
  filed under 0.1.0 only because no roadmap verb moves an item between
  sections and the store reverts a hand edit to ROADMAP.md; a re-section
  op is requested in the Ants MCP feedback file. Blocked behind UTA-0075
  for its inputs.
  **Layman:** Render the game smaller and scale it up, so it runs faster without looking soft. AMD's version first because it is the only good one that works with our graphics setup.
  Kind: implement.
  Source: user-request-2026-09-08.
  Lanes: urender.

- 📋 [UTA-0077] **Run the real-asset tier on Windows, against a real install.**
  The real-asset tier is the ONLY check on upkg's readers against content this
  project did not write, and it has never run on Windows. Every Model and Level
  layout in UTA-0004, UTA-0057 and UTA-0069 was derived and verified on Linux.
  CI's MSVC leg builds and runs the UNIT suite; it has no Unreal Tournament
  install, so it cannot run this tier at all.

  Attempted 2026-09-08 and it did not land. What was learned is worth keeping,
  because each finding costs an hour to rediscover.

  The Windows machine has a real install at C:\UnrealTournament -- 96 maps and
  83 System packages, measured with `dir`. That is a DIFFERENT corpus from the
  Linux reference install's 837 maps, which makes it worth more than a copy
  would be.

  Two things block it, and neither is the code.

  1. There is no MSVC binary to run. `.github/workflows/ci.yml` uploads no
     artifacts, and the Windows machine has no compiler (`where cl.exe cmake.exe`
     finds nothing). A mingw cross-build from Linux DOES produce a working
     binary -- x86_64-w64-mingw32-g++ 16.2.0 clears the GCC 14 floor, and both
     test executables linked once mold was disabled. But it is NOT a faithful
     stand-in: the mingw run failed 3 of 169 unit cases, all in
     writeFileAtomically, because that function opens its temporary with the
     C11 exclusive-create mode "wbx" and mingw's msvcrt does not support the
     "x". MSVC's UCRT does, which is why CI is green on the same tests. So a
     mingw binary silently tests a different CRT.
  2. Windows Defender quarantines the copied binary. It ran once, then both
     .exe files were deleted from C:\Users\Public, and a re-copy was refused
     with "The system cannot execute the specified program". An exclusion needs
     admin on the user's own machine and was not taken unilaterally.

  So the shape of the fix is: upload the MSVC leg's test binaries as a CI
  artifact, download that, and run it on the Windows machine against its own
  install, with one Defender exclusion for the drop directory. That gets the
  same toolchain CI already tests rather than an approximation.

  Found while here, unrelated and minor: the mold block in CMakeLists.txt is
  guarded `if(NOT MSVC)`, which does not exclude a PE/COFF target generally, so
  any mingw cross-build trips `mold: fatal: unknown -m argument: i386pep`.
  Cross-compiling is not a supported path, so this is a note rather than a
  defect -- but the guard is about the LINKER's target, not about MSVC.

  Blocked-by: nothing.
  **Layman:** Our readers have only ever been checked against real game files on Linux. Half the players are on Windows. Check them there too.
  Kind: test.
  Source: in-session-2026-09-08.
  Lanes: ci.

- ✅ [UTA-0078] **upkg read a BSP node's front and back children the wrong way round.**
  UE1's FBspNode stores iBack BEFORE iFront. readBspNode read them in the
  opposite order.

  Why nothing caught it. UTA-0069 SS 4.5 settled each table by a
  parse-success walk -- the failure count at that table collapsing when the
  layout was corrected. Swapping two ADJACENT fields of the same wire type
  changes no byte count and no failure count, so that method is blind to it
  by construction. The order was taken from the field names, which is the
  one part of the layout the walk never tested. The same blindness applies
  to every other adjacent same-type pair in these layouts.

  What caught it was UTA-0007's INV-2, written today: the descent held
  against each node's OWN zone record, which is a ground truth no descent
  produced. On one map, 0 of 11451 probes agreed before the swap and 11406
  after. Across the reference install, 21803383 disagreements of 24304564
  became 30399 of 11126404.

  No unit test can detect this. tests/support/UnrealPackageBuilder writes
  what the reader reads, so a round trip through it agrees with itself
  whichever order both use. The real-asset tier is the only detector, it is
  off by default, and UTA-0007 SS 10 already said this defect is invisible
  to every check that runs by default.

  Nothing else in the tree reads these fields: iFront and iBack appear only
  in src/upkg/Geometry.{cpp,h}, src/umap/{Build,Rooms}.cpp, the tests and
  two specs. Corroborated independently by session ut-monsterhunt-08, which
  searched its own ut-dump corpus tooling and found the only hit to be an
  unrelated wiring-graph array.
  **Layman:** The code that reads a level's shape had two fields swapped, so anything asking "which room is this point in?" got the wrong answer almost every time. Found and fixed.
  Kind: fix.
  Source: in-session-2026-09-08.
  Lanes: upkg.

- 📋 [UTA-0079] **0.27% of INV-2 probes disagree for no reason yet found.**
  UTA-0007 SS 7 asks INV-2 for the HARD form -- zero disagreeing probes
  across the install. After the UTA-0078 field-order fix the count is 30399
  of 11126404, so the tier-3 case asserts under 1% instead and says so in
  place.

  That substitution is defensible on a measurement SS 7 did not have. Its
  argument against a rate was that a swapped convention would still score
  near 100%; measured, it scores ZERO. The defect is catastrophic rather
  than marginal, so a 1% ceiling catches it with a factor of three hundred
  to spare. What a rate does NOT catch is a small future regression, which
  is the cost being accepted here.

  Four hypotheses were tested and each is excluded -- the count moved by at
  most one across all of them:

  1. Nodes the descent cannot reach. Coplanar detail hung off the tree by
     iPlane is ~36% of a map's nodes, and so is any subtree whose own root
     is one of those. Excluded by a walk from node 0; residual unchanged.
  2. Probes not strictly inside their own cell. Verified against every
     ancestor plane for side and for a margin of one nudge. Residual
     unchanged. Measuring the path the probe TOOK is the wrong test and was
     the first attempt; the ancestry walk replaced it.
  3. A shared BSP subtree making the ancestry ambiguous. Measured: every
     node has at most one parent, so the graph is a tree and the ancestry
     is exact.
  4. Leaf or zone indices out of range. The census reports 0 of 5720281
     leaves out of range and 0 naming zone 0, and 0 builds refused.

  Worth trying next: whether the residual concentrates in particular maps
  or is spread evenly, which separates content from a systematic cause; and
  whether roomAt's float plane test and the probe's double one disagree at
  large coordinates, which the margin was sized to prevent but which was
  not measured directly.
  Current figure (2026-09-10): the real tier's INV-2 census over today's
  install made 11,945,148 probes, 36,016 disagreeing -- 0.30%. The test's
  comment records 30,399 of 11,126,404 (0.27%) from an earlier, smaller
  install. The rate moved as the library grew, so whatever explains the
  disagreements should be tested against a fresh figure, not the one in
  the comment. Still well under the test's 1% ceiling.
  **Layman:** Our check that the room lookup agrees with the level file is right 99.7% of the time. The last 0.3% is unexplained, so the check is set just below it rather than claiming perfection.
  Kind: investigate.
  Source: in-session-2026-09-08.
  Lanes: umap.

- 📋 [UTA-0081] **Four shipped items have no CHANGELOG entry.**
  Found 2026-09-08 by comparing the ids CHANGELOG.md cites against the ids
  ROADMAP.md marks shipped. Missing: UTA-0043, UTA-0058, UTA-0069 and
  UTA-0074. UTA-0007 and UTA-0078 shipped the same day and were written up
  in that session, so they are not in this list.

  Not written up here because they are not this session's work, and
  releases.md SS 7 calls drafting a changelog from a commit range an
  anti-pattern -- an entry says what a change MEANS to a reader, which the
  diff does not carry. Each wants the session that shipped it, or the user,
  to say what it delivered.

  UTA-0069 is the one to do first. It shipped the Model BSP tables that
  UTA-0007 and UTA-0078 both build on, and the release notes for this
  milestone read oddly without it.

  Worth a check rather than a habit: the comparison above is two greps and
  it caught four misses, so it belongs in the release pre-flight rather
  than in a person's memory. cut-release --check is where that would sit.
  **Layman:** Four finished pieces of work were never written up in the list of what changed, so anyone reading that list would think they had not happened.
  Kind: doc.
  Source: in-session-2026-09-08.
  Lanes: docs.

- 📋 [UTA-0082] **0.1.0's planned count includes seven items deferred out of its cut condition.**
  UTA-0044, UTA-0045, UTA-0053, UTA-0054, UTA-0055, UTA-0075 and UTA-0076
  each carry a "Deferred out of 0.1.0" annotation, made on the user's
  call. They still sit in this section, so anyone COUNTING its planned
  items gets a number seven too high. Anyone READING them sees the truth,
  which is why this is a presentation defect and not a lost decision.

  The agreed fix (user, 2026-09-08) was one line in this section's intro
  saying so. That is not writable: the intro is held in the roadmap store
  and no roadmap_log op amends it, and a hand edit is discarded by the
  next render of any kind -- measured, with `discarded_text` naming the
  inserted line, and the file verified byte-identical afterwards. Filed
  in the Ants MCP feedback file as a request for an amend_intro op.

  Moving the seven is the other route and is blocked on the same tooling:
  there is no op to move an item between sections. Re-filing them with
  new ids in a later section would fix the count and break the
  cross-references in their bodies, which is why the user was asked and
  has deliberately not chosen it.

  So this item is the note, standing in for the intro line until either
  op exists. Do it by writing that line and deleting this, not by
  re-filing seven items.
  **Layman:** The list of work for the first release counts seven things that were already decided not to be in it, so the release looks further away than it is.
  Kind: doc.
  Source: in-session-2026-09-08.
  Lanes: docs.

- 📋 [UTA-0083] **Generalise the mutation probe past its hand-written mutation list.**
  scripts/mutation-probe.py shipped 2026-09-08 with one subject, ubundle,
  and 57 mutations written out by hand. It earned its place immediately:
  it found four fixtures grading a rule OTHER than the one they named,
  all four passing, all four reading correctly. CLAUDE.md SS Build and
  test records the same failure from UTA-0007, so this is a defect class
  this project keeps hitting rather than a one-off.

  The problem is the list. Fifty-seven string literals matched against
  source text go stale the moment the source is reformatted, and the
  NOT-APPLIED and NOT-UNIQUE states exist only to say so out loud. A hand
  list also never grows: nobody writes 57 more for umat.

  What is already right and should not be redone: the three traps are
  paid for and encoded -- restore by rewriting and touching (an older
  mtime lets ninja skip the rebuild), baseline the FILTER and not just
  the suite (a Catch2 tag matching nothing exits non-zero, so every
  mutation reads as killed), and never a ulimit around a sanitizer binary
  (ASan's shadow map needs terabytes of address space; a cap kills it at
  startup, which also reads as killed). Also right: expected_survivors,
  which distinguishes a redundant rule from an ungraded one, and the
  non-zero exit on a NEW survivor, which is what makes it CI-able.

  Two directions, and the second is the cheaper one to try first.

  1. Generate the mutations from the source rather than listing them --
     swap adjacent same-width same-type field reads, negate a comparison,
     delete a guarded return. That is a small C++-aware rewriter, and
     getting it wrong produces mutations that do not compile rather than
     wrong answers, so the failure mode is cheap.

  2. Leave the lists hand-written and make writing one cheap: a spec's
     invariants already name their rules, and spec_query returns them.
     A probe keyed to invariant ids would report per-INV rather than per
     string, which is the number a reader wants anyway.

  Not for CI as it stands: the ubundle run is 57 rebuilds. Per-subsystem,
  on demand, before flipping an item to shipped, is the cadence it was
  used at and is affordable.
  **Layman:** We have a tool that deliberately breaks one rule at a time and checks a test notices. It works, but the list of things to break is written out by hand for one subsystem, so nobody will keep it up.
  Kind: test.
  Source: in-session-2026-09-08.
  Lanes: tests.

- ✅ [UTA-0084] **A third session cannot tell that two already hold this project.**
  `CLAUDE.md` § Running two sessions at once caps the project at two
  sessions, and rule 3 gives the start-up test as `git worktree list` and
  `ListAgents`. Neither answers the question the cap asks.

  **`ListAgents` is machine-wide, not project-scoped.** Measured
  2026-09-08 during the workflow-overrides gate: it returned
  `ut-monsterhunt-05`, `claude-39`, `pressless-dd` and `ants-terminal-82`.
  None works this project, and `claude-39` names no project at all. So a
  starting session cannot read a count of this project's sessions off it.

  `git worktree list` is project-scoped but answers a different question:
  a worktree outlives the session that made it, so its presence proves
  nothing about a live holder.

  **The route that does work is already in the file, spread across two
  rules.** Rule 1 has each session name itself in the 🚧 progress note;
  rule 2 has `roadmap_query status:"in-progress"` list the held items, and
  a holder not in `ListAgents` is abandoned. Together those identify live
  holders BY NAME. Rule 3 does not say so, and a session following rule 3
  alone learns nothing.

  This is why the item is `investigate` rather than `doc-fix`: rewriting
  rule 3 to name the 🚧-notes route is a change to the coordination
  protocol, not a correction of a false sentence, and it should be decided
  rather than patched. Worth settling first: whether a session name is
  required to encode its project — nothing enforces that today, and
  `claude-39` is the counter-example — because the whole route rests on
  matching names.

  **Not urgent, and the cap has held.** No third session has started, and
  this session correctly detected an abandoned `UTA-0008` holder by
  exactly the rules-1-and-2 route. The gap is that nothing DETECTS a
  breach; it is prevented by each session checking honestly.
  Progress (2026-09-09): held by session ut-ants-17, sole session on this
  project -- git worktree list shows one checkout and ListAgents shows no
  other UT_Ants session.

  Taken as a doc-fix on the user's call (2026-09-09), ahead of UTA-0052.
  The item's own diagnosis is accepted and is not being re-derived: the
  route that works is already in rules 1 and 2, and rule 3 does not name
  it. The fix is to make rule 3 name it.

  ListAgents being machine-wide was re-confirmed this session rather than
  taken from the item body: it returned ants-terminal-ff and
  ut-monsterhunt-b9, neither of which works this project.

  Being a change to a contract document, this runs CLAUDE.md rule 14's
  gate before it lands.
  Resolved 2026-09-09 by session ut-ants-17, main checkout.

  Rule 3's start-up test now names the route that answers it:
  `roadmap_query status:"in-progress"` for the held items, each carrying its
  holder's session name and checkout from rule 1, then `ListAgents` for which
  of those names is live.

  **The item asked for a coordination-protocol change and got one.** Rule 1
  now records the holder's CHECKOUT beside its session name, because nothing
  on this machine reports which checkout a session occupies — that note is
  the only place the fact exists.

  **What the gate added that this item did not anticipate**, each found by
  cold lanes over three loops:

  - An empty in-progress list is not evidence the main checkout is free. A
    session between items holds nothing, which is state 4 and this project's
    ordinary state, so the roadmap is silent while somebody sits in main.
  - Defaulting to a worktree cannot be unconditional. A sole session that
    relocates leaves nobody able to merge, commit `ROADMAP.md` (rule 7) or
    advance `Next:` (rule 6). The default now fires only when a live peer
    cannot be ruled out, and names the route back.
  - Rule 5's reason for keeping 🚧 across a session boundary contradicted
    rule 2's abandonment test. The marker records a state, not a claim.

  **The gap this item named is narrowed, not closed, and the document now
  says so.** No command answers "is the main checkout free?", so the cap
  still rests on rule 1 being followed honestly. What changed is that a
  session following rule 3 now learns who holds what, instead of learning
  nothing.

  **The gate hit its cap of three loops.** Rule 3 was repaired in every one
  and each repair produced the next finding — an oscillation confined to that
  rule, which is evidence about the rule rather than the review: it answers a
  question no command supports, and it now defers to a judgement instead of
  pretending to a test. Thirteen verified findings, thirteen fixed, one
  dismissed. Record: `docs/claude-md-review-2026-09-09.md`.

  **Also recorded there: a lane cannot be cold on this document.** The
  harness injects the live `CLAUDE.md` into every session, subagents
  included, and all three lanes disclosed it. The scrubbed copy still
  withholds the review history; it cannot withhold the document.
  **Layman:** Two sessions may work this project at once. Nothing reliably tells a third one that the two slots are taken, so the limit rests on each session checking honestly rather than on anything that can detect a breach.
  Kind: investigate.
  Source: review-contract-2026-09-08 workflow-overrides loop 3.
  Lanes: docs.

- 📋 [UTA-0085] **unav: decode and validate the ReachSpec reach flags.**
  UTA-0006 shipped `NavEdge` carrying `reachFlags`, and `Graphs.h` says of
  it: "Passed through ungraded. Nothing here or in UTA-0057 checks these
  against an independent source; UTA-0006 SS 14 keeps that open." So the
  number travels into the bundle and nothing can act on it.

  That is the gap between having the bot-path data and being able to use
  it. The flags are what separate a walk from a jump, a swim, a flight and
  a door that must be opened first -- and with `collisionRadius` and
  `collisionHeight`, they decide whether a given pawn may traverse an edge
  at all. A bot planner without them can only treat every edge as equal,
  which is what UT99 largely did.

  **Validate against a real install, not against the UT99 source's
  constants alone.** A constant list says what the flags were meant to
  mean; only a real map says what they are. The second test tier
  (`UTA_REAL_ASSET_TESTS`) is where that check belongs.

  **Do not validate against result 3 of the Monster Hunt session's
  paths-index-evidence.md.** It was WITHDRAWN on 2026-09-09, not repaired:
  AncientCavesTorus previously read 1202/1274 Paths with no actors at the
  16-slot cap and now reads 4453/4453 with 45 and 49 actors at it, while
  Village1 and Dust2-BP reproduced their earlier numbers exactly. The map's
  export changed, not the measurement. Results 1, 2 and 4 are unretracted
  and are the right target.

  Requested by the user 2026-09-09: bots in this engine are to be far more
  useful than UT99's. This item is the enabling step, not the bots
  themselves -- UTA-0025 is waypoint parity, and UTA-0073 and UTA-0028 are
  what beat it.
  Signal from the consuming session (2026-09-09), and it bears directly on
  what this item must decode.

  **Their route probe reports `ctrlhops=16` on some maps, and 16 is also the
  NavigationPoint path-array slot count.** They do not yet know whether that
  is a coincidence or one cap surfacing twice.

  That matters here for two reasons. The withdrawn result 3 of their
  paths-index-evidence.md was the measurement that ELIMINATED the
  16-slot-cap hypothesis — so with it withdrawn, that hypothesis is live
  again rather than settled. And an edge list with ungraded flags could not
  tell the two apart, which is an independent argument for decoding the
  flags before exposing the graph.

  So: when validating, treat 16 as a number to watch rather than an
  incidental. If a decoded traversal rule explains a 16-hop ceiling, that is
  a result; if the slot count explains it, that is a different result with
  different consequences for the baker.

  **An independent oracle exists for part of this.**
  `/mnt/Games/Scripts/Linux/UT_MonsterHunt/analysis/pkgnames.py` parses a
  package's name and export tables in pure Python with no dependencies, and
  was validated against Textures/Wood.utx's real 190-entry name table.
  Diffing our reader against it on those two tables is a cheap check that
  shares none of our code.
  **Layman:** Work out what each bot path actually allows -- walk, jump, swim, or a door that must be opened first. We already read the number; nothing yet knows what it means.
  Kind: implement.
  Source: user-request-2026-09-09.
  Lanes: unav, upkg.

- 📋 [UTA-0086] **ut-dump: emit the bot-path graph as an edge list and a node list.**
  UTA-0012's third day-one query, split out now that UTA-0057 has shipped
  the ReachSpec graph it was blocked on.

  **Shape confirmed by the consumer on 2026-09-09: both, and the EDGE LIST
  is the half that is actually needed.** Its question is reachability --
  can a bot get from PlayerStart to MonsterEnd -- which needs from-node,
  to-node and the ReachSpec's own fields, because the reach flags and the
  collision radius and height decide whether a given pawn may traverse an
  edge at all. Node rows are wanted alongside, because node CLASS matters:
  InventorySpot, PathNode, Spawnpoint and MonsterEnd are not
  interchangeable. "A node list without edges tells me nothing I cannot
  already get."

  So: edge list first, node list beside it, separate keys.

  This retires a 4.9 GB T3D-export intermediate that is repeatedly deleted
  and rebuilt, and replaces a route census currently derived by grepping
  those exports.

  **Two hazards the consumer measured and this reader will meet.** `strings`
  is not evidence about a package: it reported 14,441 names in
  Textures/Wood.utx whose real name table holds 190. And two unrelated
  packages may share a name, with the `Paths=` search order deciding which
  wins -- Textures/wonderland.utx shadowed Sounds/wonderland.uax for ten
  maps because Default.ini searches Textures first. Resolution is
  search-order dependent, not name dependent.

  Blocked-by: the reach-flag decode, which is what makes an edge's fields
  mean anything.
  A whole-library validation route exists (2026-09-09), which is a far
  stronger check than the four evidence results.

  `/mnt/Games/Scripts/Linux/UT_MonsterHunt/analysis/loadsweep.py` batches map
  loads through one editor process: 0.16s per map batched against 2.42s
  one-at-a-time, measured over the same 40 maps. Its module docstring is
  longer than its code and carries the reasoning — read that first.

  **What it answers is WEAKER than what this item needs.** It asks whether
  the engine can open a package. It says nothing about whether a map plays,
  and deliberately does not count a substituted missing texture as a failure.
  So it is a cross-check on our reader's ability to open the library, not on
  the graph we extract from it.

  The cautions its author gives, each paid for already:

  - **The exit code is a lie.** The engine exits 0 and logs
    "Success - 0 error(s)" after failing to load a map. Only the log is a
    witness — it judges on a "New File, Existing Package" trace for a load
    and a "Failed to load" trace for a failure.
  - **A map in a batch with NEITHER trace was never reached**; the engine
    died partway. Retry it alone rather than reporting it. Losing maps
    silently has bitten that project three times.
  - `--maps` takes a DIRECTORY and the path must be ABSOLUTE — the engine's
    cwd is inside the install, so a relative path reports every map missing.
    Symlinks work, which is how a subset is swept.
  - `-log=` silently ignores any path containing a space, so the log cannot
    live under `/mnt/Games/PC Games/...`. Not `/tmp` either: it is a tmpfs
    and a path builder wrote 1.1 GB there once.
  - `--batch` bounds MEMORY, not time. The engine does not collect garbage
    between loads.
  - Point `--out` somewhere new. Do not overwrite
    `work/loadsweep/results.jsonl` — verified 2026-09-09 to hold 2028 rows,
    and it is that project's current baseline.

  The consuming session is **NOT blocked** on this item. Its route census
  works today from T3D exports. This would remove a 4.9 GB intermediate,
  which is an improvement rather than a dependency — stated plainly by them
  so that it does not reorder this queue.
  Correction (2026-09-09), from the tool's author, and it reverses the advice
  above: **do NOT validate against `work/loadsweep/results.jsonl`.**

  That file describes a library of 2027 that no longer exists, against a disk
  now holding 2022. Since the run that produced it: two maps it calls BROKEN
  now load (a `U4eMedigun` substitution fixed `MH-()mG-FourGhostsV2` and
  `MH-()mG-monsterthatrockTest5`), five of its rows are maps since deleted,
  and 45 of its rows carry the old `Maps\...` names — those maps now load
  under stripped names and appear nowhere under them.

  Diffing our reader against it would surface disagreements that are the
  baseline's, not ours. Regenerating takes minutes. **Ask them to generate a
  fresh sweep when this item is close**, so the cross-check is current rather
  than a second stale artefact. They offered.

  **The row count is 2028 and the population is 2027.** The extra row is
  loadsweep's own completion sentinel, `{"batch": "done"}`, written only when
  a run finishes — so its presence is what distinguishes a completed sweep
  from one that died partway, the failure that has bitten that project three
  times. It is load-bearing: do not strip it, and **count the `map` key
  rather than lines.** That baseline reads 1972 loads and 55 broken.

  The earlier note on this item said the file held 2028 rows without knowing
  what the extra one was. It is not a defect.
  Correction (2026-09-09): **ping the PROJECT, not a session.**

  The note above says to ask them for a fresh sweep. The session that offered
  it has since ended, so a future session addressing `ut-monsterhunt-b9` by
  name would reach nothing and read the silence as a refusal.

  The commitment is written down instead, at **`GAME-0032`** in
  `/mnt/Games/Scripts/Linux/UT_MonsterHunt/ROADMAP.md` — verified present
  2026-09-09. It records what a session with no memory of the exchange would
  need: that the sweep is generated AT ping time rather than copied from
  `work/loadsweep/results.jsonl`, that this freshness is the whole substance
  of the promise, the command to run, and the instruction to write to a NEW
  `--out` rather than overwrite the baseline. It also records our build order
  and its reasoning, and that they are **not** blocked on `UTA-0085` or this
  item — so a later session there cannot cite a blocker to reorder our queue.

  **The general lesson is worth more than this instance.** A cross-session
  message is read when the other session next looks, and nothing makes it
  look. An arrangement that lives only in two sessions' contexts dies with
  whichever ends first. Put it in both projects' roadmaps and address the
  project.
  **Layman:** Print a map's bot paths from the command line, so they can be checked across the whole map library without loading the game.
  Kind: implement.
  Source: consumer-request-2026-09-09.
  Lanes: upkg.

- 📋 [UTA-0087] **The 740-map figure is stale everywhere it is cited.**
  UTA-0058 settled a Monster Hunt map count that the ADRs cite. That
  figure has been overtaken.

  **Measured directly on 2026-09-09** at
  `/mnt/Games/PC Games/UT/UnrealTournament-469/Maps`:

  - 2022 `.unr` files in total
  - 1923 matching `MH-*.unr`
  - 337 matching `MH-*BP*.unr`

  ROADMAP.md cites 740 in several bodies, and 183 as the `-BP` count.
  Both are wrong, and one derived figure -- "count over 740 installed maps
  went from 732 to zero" -- rests on the old denominator.

  The Monster Hunt session reports the library moved three times in one
  week: 153 path-repaired `-BP` maps promoted, 45 maps that were always
  present but unopenable because a literal `Maps\` prefix from a Windows
  path survived extraction, and 5 byte-identical duplicates deleted. None
  of those `Maps\`-prefixed files remain -- verified here.

  **Fix the ADRs as well as the roadmap**, since UTA-0058's own headline is
  about what the ADRs cite. And prefer a dated measurement with the command
  that produced it over a bare number, because this figure has now been
  wrong twice.
  Reconciliation (2026-09-09), and the trap is worth more than the numbers.

  The 45 renamed maps are a STATUS change with no count effect, and must not
  be added to any total. They were named `Maps\MH-Foo.unr` — still ending in
  `.unr`, so `find -name '*.unr'` counted every one of them before the rename
  as well as after. Renaming changed what the ENGINE can open, not what the
  filesystem holds. **"Became loadable" is not "was added",** and the same 45
  will look like an increase in any before/after run against an older sweep.

  This session got that wrong first: it added the 45 and reported a
  mismatch that did not exist. Recorded because the error is the natural one.

  The chain over all `.unr`, confirmed against the disk measurement above:

        1874  before the promotion
        +153  path-repaired -BP maps promoted 2026-09-09
        =2027  the population the load sweep swept, the 45 included
          -5  byte-identical duplicates deleted 2026-09-09
        =2022

  All 45 renamed and all 5 deleted maps carry the `MH-` prefix, so on the
  MH-only denominator this week is +153 and −5.

  **The deltas above do NOT bridge 740 → 1923.** That gap is earlier intake
  work predating this week. So 740 → 1923 is the right headline and this
  week's arithmetic is not its explanation — do not present them as one
  chain, which is the second way this figure can be got wrong.
  **Layman:** The roadmap and the decision documents say the map library holds 740 maps. It holds far more now, and several figures derived from that number are wrong.
  Kind: doc-fix.
  Source: in-session-2026-09-09.
  Lanes: docs.

- ✅ [UTA-0088] **ADR-0007 question 4 mis-routes a dependency whose build system builds something else.**
  Found by `UTA-0052`'s `review-contract` gate, loop 2, from a lane's open
  question. Not corrected there: it is that ADR's rule, not that spec's.

  `docs/decisions/ADR-0007-acquire-dependencies-by-route.md` § Decision asks,
  as question 4, **"Does it ship no build system of its own?"** and routes a
  yes to route 2 (vendored).

  `bc7enc` ships a `CMakeLists.txt` — its repository root holds one, read
  2026-09-09 via `gh api repos/richgel999/bc7enc/contents`. So question 4's
  literal answer is no, and the question set falls through to question 5,
  which routes it to route 4: fetched, guarded by the one target that needs
  it.

  **That is the wrong answer.** What that `CMakeLists.txt` builds is a demo
  executable from `test.cpp` and a bundled `lodepng`, not a library anyone
  links. Route 2's own stated ground is what fits — *"its sources are
  compiled into the target that uses it, so fetching would buy nothing a copy
  does not already give"* — and `UTA-0052` § 3 decision 4 takes route 2 on
  that ground, saying so in place.

  **The question is worded for a repository with no build system, and the
  test it means to apply is whether the build system produces something you
  would link.** Dear ImGui, the ADR's own route-2 example, happens to satisfy
  both readings, so the gap was invisible until a second candidate arrived.

  Fix is a rewording of question 4 and nothing else — no route changes, and
  `UTA-0052`'s decision stands either way. Gate the edit under `CLAUDE.md`
  rule 14 at genre `adr`: a conformer answering question 4 about the next
  library of this shape builds a different acquisition.
  Progress (2026-09-09): taken by session `ut-ants-2d`, working in the
  MAIN checkout `/mnt/Games/Scripts/Linux/UT_Ants`. Taken ahead of
  `UTA-0052` under the user's priority order rule 1 — it is the only
  open review-sourced item not standing deferred (`UTA-0059` defers
  itself until the renderer lands).
  Resolved (2026-09-09). Question 4 now asks whether a dependency's build
  system produces anything anyone would link, rather than whether it ships
  one. Dear ImGui ships none and still passes, so no existing route moved;
  route 2 names `bc7enc` beside it and pins the vendoring layout.

  Gated under `CLAUDE.md` rule 14 at genre `adr`: `review-contract`, three
  cold lanes per loop, cap of 3 reached. Thirteen verified, thirteen fixed,
  one dismissed. Loops 4, 5 and 6 of the document's log,
  `docs/reviews/ADR-0007-acquire-dependencies-by-route-loop-log.md`.

  **A CALM cap**, unlike `UTA-0052`'s: one of the final loop's three
  findings landed on text this run wrote, against two of two the loop
  before. Two of the thirteen fall inside the arming span, so the run was
  overwhelmingly the AUDIT half rather than the gate half — a fifteen-line
  trigger returned eleven defects in text it never touched.

  The most valuable was not the routing defect this item filed. The ADR
  said `find_package(Vulkan)` locates `glslc` and that the gate fails when
  either is absent. It does not: `FindVulkan` appends `glslc` to the
  component list itself, so `find_package_handle_standard_args` never
  treats it as required. Measured both directions on a stand-in module —
  appended, the package is FOUND with the component FALSE; named in
  `COMPONENTS`, configuration stops. A gate built as described would have
  been green on a machine with no shader compiler.

  Collateral corrected outside this document: `UTA-0052`'s spec and
  `docs/design.md` each still stated question 4's old wording, and the
  spec's § 4.1 pinned a `README.md` recording the commit but not the
  repository, which did not satisfy the general rule this run wrote.

  No CHANGELOG entry: an internal decision record, invisible to a player or
  a server operator.
  **Layman:** A rule for deciding how we obtain outside code asks the wrong question, so it gives the wrong answer for a library that ships a demo program.
  Kind: doc-fix.
  Source: review-contract-2026-09-09.

- 📋 [UTA-0089] **urender: water and glass that look the part, with cheap faked reflections.**
  The user's requirement (2026-09-10): water sections must actually look
  like water, and glass sections must look like glass, with reflections
  where they fit. Faked or cheap reflections are acceptable, and cheap is
  preferred.

  The cheap route is a reflection probe: an environment image captured
  once at bake time per room, or the sky where a surface sees it, blended
  in by a Fresnel term so a surface reflects more at a grazing angle.
  Glass adds transparency and a tint. Water adds scrolling detail normals
  and a depth tint. None of it traces a ray, so ADR-0001's no-ray-tracing
  rule is not in question.

  This is NOT UTA-0045. That item is screen-space reflections, the
  expensive route, and the user deferred it out of 0.1.0 on 2026-09-08.
  Surface motion is UTA-0055, deferred the same day. This item is what
  makes water and glass read correctly without either.

  Which surfaces are water and which are glass comes from UTA-0009, which
  already reads the original texture's PolyFlags rather than guessing.

  Blocked-by: UTA-0014.

  Two open points for whoever picks this up. Reflections are not among the
  responsibilities docs/design.md lists for urender, so adding them runs
  the rule 14 gate on that document first, as UTA-0045's body already
  records. And whether this is part of 0.1.0's cut condition is the
  user's call: S1 as written does not require it.
  Where the water and glass tag comes from (measured 2026-09-10, recorded
  in full on UTA-0009): from each SURFACE's PolyFlags, not from the
  material. Textures carry no PolyFlags of their own in the install, and
  surfaces sharing a texture disagree often -- translucent 30%, wavy 54%,
  portal 51% of the time. So the bake carries the flags per surface into
  the bundle, and this item reads them there. UTA-0009 does not supply a
  per-material water or glass tag.
  The user's requirement (2026-09-10), in their words: "For water,
  please use modernised graphics techniques to show waters / liquids.
  But please do it as cheaply as possible."

  Candidates for this item's spec to settle by research and
  measurement, cheapest first: two scrolling normal maps for moving
  ripples; a Fresnel blend, more reflective at glancing angles;
  reflection from a pre-baked environment map rather than an extra
  scene render; refraction by sampling the already-rendered scene with
  a small ripple offset; depth-based tint so deep water reads darker;
  a soft fade where water meets geometry. The user cannot judge looks
  by eye, so the choice rests on sources and numbers.
  **Layman:** Water should look like water and glass like glass, with reflections that are cheap tricks rather than expensive real ones.
  Kind: feature.
  Source: user-request-2026-09-10.
  Lanes: urender, umat.

- 📋 [UTA-0090] **uworld: water volumes that behave like water.**
  The user's requirement (2026-09-10): water sections must behave like
  water, not only look like it. Nothing on the roadmap covered the
  behaviour: UTA-0017's movement model does not mention water, and the
  render item filed beside this one covers only appearance.

  UT99 marks water with a zone. Inside one a player swims rather than
  walks, moves more slowly, can rise and sink, and can climb out at the
  surface. A zone may also set a current or cause damage. The behaviour is
  measured from the original, not guessed, on the same terms as UTA-0017.

  Reading which zones are water is upkg's side of this. It may already be
  covered by UTA-0004's actor placements, and whoever picks this up checks
  that first.

  Blocked-by: UTA-0017.
  Required, not optional (user, 2026-09-10): this project has to play the
  maps UT99 already plays, and water maps cannot be played without
  swimming.

  Filed under 0.1.0 by mistake. It belongs with the movement release,
  0.2.0, beside UTA-0017, which it is blocked by. The batch call that
  filed it took one section for both items, and no roadmap verb moves an
  item between sections. Do not count it when judging what is left for
  0.1.0.
  Asked UT_MonsterHunt (2026-09-10) to measure UT99's real water-zone
  behaviour with its in-game probe pattern: swim speed, climbing out,
  zone velocity. That is how this item gets measured, not guessed.
  Linked (2026-09-10): UT_MonsterHunt GAME-0078 carries the six water
  numbers asked for and will measure them in the running game, reporting
  which its probe pattern cannot reach reliably rather than guessing.
  **Layman:** Jumping into water should mean swimming, slower movement and a way to climb back out, the way the original game does it.
  Kind: feature.
  Source: user-request-2026-09-10.
  Lanes: uworld.

- ✅ [UTA-0091] **ubundle: split Bundle.cpp by section, so work on one section does not share a file with another.**
  The user's standing request (2026-09-10): refactor files at every
  opportunity, because Ants Terminal is building a way for several sessions
  to work one project at once, and two sessions editing one file collide.

  src/ubundle/Bundle.cpp holds three reasons to change in one file: the
  container framing (header, section table, read and write), the ROOM,
  NAVG and WIRG section codecs (which move when umap or unav do --
  UTA-0085 will change NAVG), and the TEXS codec (which moves with umat --
  UTA-0052, then UTA-0009). Its history already shows two items editing
  it. That is coding.md section 1.8's seam, not a line count.

  Behaviour must not move. UTA-0008's and UTA-0052's golden byte arrays
  grade every section's bytes, so the whole existing suite is the check.
  Shipped (2026-09-10) at `357bdc6`, on the matrix: CI run 34463029760
  is green on Linux GCC 14, Linux Clang 19 and Windows MSVC. The code moved
  verbatim and no byte of the format changed -- UTA-0008's and UTA-0052's
  golden arrays pass unchanged. The ubundle mutation probe, pointed at the
  new files, kills 55 of 57, and its two survivors are the declared ones.
  **Layman:** Break the bundle file-format code into one file per part of the format, so two people or sessions working on different parts do not trip over each other.
  Kind: refactor.
  Source: user-request-2026-09-10.
  Lanes: ubundle.

- ✅ [UTA-0094] **ut-dump reads each file one character at a time; read it in one call.**
  Found by the optimisation pass the user asked for on 2026-09-10, and
  checked against the source before filing.

  `readWhole` in tools/ut-dump/main.cpp builds its vector from an
  `istreambuf_iterator`, one character at a time with repeated growth.
  Measured by the pass: 55% of ut-dump's CPU; a warm whole-library run
  fell from 34.2 s to 21.1 s with only that swapped for one read, and the
  JSON was byte-identical. `uta::fs::readFile` already does one read and
  is already linked.

  The same resolver never remembers a System package that failed to
  open, so it re-reads that file on every lookup. Remember the failure.

  Verify: time a warm run over Maps/ before and after, and `cmp` the two
  JSON outputs. UT_MonsterHunt runs this tool over its whole library, so
  the gain is theirs as much as ours.

  Not part of 0.1.0's cut condition.
  Claimed (2026-09-10) by session `ut-ants-b3` in the MAIN checkout
  `/mnt/Games/Scripts/Linux/UT_Ants`. Taken ahead of `Next:` (UTA-0009)
  because it is review-sourced -- rule 1 of the user's order.
  Shipped (2026-09-10) at `0858197`, on the matrix: CI run 34464098637 is
  green on Linux GCC 14, Linux Clang 19 and Windows MSVC. A warm library
  run went from about 28 s to 11.4 s, and the JSON is byte-identical over
  all 2,023 packages.
  **Layman:** Make the map-inspection tool read files in one go instead of letter by letter, so a whole-library check takes about a third less time.
  Kind: perf.
  Source: review-code-2026-09-10 optimisation pass.
  Lanes: upkg.

- ✅ [UTA-0095] **The real-asset tests read files one character at a time; read them in one call.**
  Found by the optimisation pass of 2026-09-10 and checked against the
  source: tests/real/RealInstallTest.cpp has four readers built on
  `istreambuf_iterator`.

  Measured by the pass: a cold run of the tier took 582 s wall for 220 s
  of CPU, and the read was 83.5% of CPU in one test and 54% in another.
  Replace all four with `uta::fs::readFile`. Wall time improves mostly
  with a warm page cache, because the Maps drive is a spinning disk.

  Not part of 0.1.0's cut condition.
  Claimed (2026-09-10) by session `ut-ants-b3` in the MAIN checkout
  `/mnt/Games/Scripts/Linux/UT_Ants`, after UTA-0096 closed. Rule 1 of the
  user's order: review-sourced. A "before" run of the real tier was taken
  in build-real at d3360e8 for the comparison.
  Shipped (2026-09-10) at `6e1fe75`. The tier passes 9/9 locally, and
  warm, back to back, the test binary went from 177.9 s wall and 161.9 s of
  CPU to 53.3 s and 43.0 s.

  **The matrix cannot grade this, and it is said plainly rather than
  implied.** CI run 34466927875 is green on GCC 14, Clang 19 and MSVC, but
  the real-asset tier is local-only by design (S7), so no CI leg compiles
  tests/real/RealInstallTest.cpp at all. The only evidence is this
  machine's GCC leg. Running the tier on Windows is UTA-0077's.
  **Layman:** Speed up the slow test tier that checks real game files by reading each file in one go.
  Kind: chore.
  Source: review-code-2026-09-10 optimisation pass.
  Lanes: tests.

- ✅ [UTA-0096] **upkg reads the lightmap byte table one byte at a time; read it as one run.**
  Found by the optimisation pass of 2026-09-10 and checked against the
  source: src/upkg/Geometry.cpp reads `lightBits` through
  `readTable<std::uint8_t>` with a per-byte lambda.

  Measured by the pass: the table is 100 to 350 KB per map, and it plus
  `readU8` is about 9% of CPU in the zone-descent profile. Read the count,
  keep the size check, then take the run with one `readBytes`. That is
  exactly what ubundle's TextureSection.cpp does for block data, and its
  comment says why.

  The bounds check is kept, because `readBytes` performs it.

  Not part of 0.1.0's cut condition.
  Claimed (2026-09-10) by session `ut-ants-b3` in the MAIN checkout
  `/mnt/Games/Scripts/Linux/UT_Ants`, after UTA-0094 closed. Rule 1 of the
  user's order: review-sourced.
  Shipped (2026-09-10) at `d3360e8`, on the matrix: CI run 34465285272 is
  green on Linux GCC 14, Linux Clang 19 and Windows MSVC. readModel's total
  over the reference library fell from 11.48 s to 9.33 s with the lightBits
  checksum identical. Mutation found the table's count check ungraded; two
  refusal cases were added in d3360e8, and deleting the check now reddens
  both.
  **Layman:** Read one big block of lighting data from a map in a single step instead of byte by byte.
  Kind: optimize.
  Source: review-code-2026-09-10 optimisation pass.
  Lanes: upkg.

- ✅ [UTA-0097] **CI rebuilds Catch2 from scratch on every Linux leg; cache compiler output between runs.**
  Found by the optimisation pass of 2026-09-10 and checked against the
  source: .github/workflows/ci.yml provisions no compiler cache, and
  scripts/ci.sh builds a second, ThreadSanitizer tree.

  Measured locally by the pass, with ccache off: 108 of 155 objects are
  Catch2, and each of the two Linux builds costs about 350 CPU-seconds.
  The CMake launcher is already wired, so this is provisioning only:
  ccache on the apt line, and `actions/cache` keyed on compiler and
  Catch2 tag, with `CCACHE_BASEDIR` and `CCACHE_NOHASHDIR`.

  Pin `actions/cache` by SHA and resolve its current release when this is
  built, per dependencies.md. The compile flags are part of the cache key,
  so the numeric contract is safe. The MSVC leg is unchanged.

  Not part of 0.1.0's cut condition.
  Claimed (2026-09-10) by session `ut-ants-b3` in the MAIN checkout
  `/mnt/Games/Scripts/Linux/UT_Ants`, after UTA-0095 closed. Rule 1 of the
  user's order: review-sourced.
  Shipped (2026-09-10) at `575246c`, measured over two CI runs of the same
  commit, all three legs green in both:

    run 1 (34467774999, fills the cache): GCC 239 s, Clang 241 s, MSVC 216 s
    run 2 (34468178273, restores it):     GCC  38 s, Clang  65 s, MSVC 257 s

  Run 2 hit on every compile: of 312 calls per Linux leg, 312 hits, all
  direct. Every call was cacheable, so the base-dir and hash-dir settings
  work. The cache is small -- about 38 MB for GCC and 22 MB for Clang against
  the 1 GB ceiling -- so that ceiling is generous rather than binding. MSVC
  has no cache by design, and its difference is runner variation.
  **Layman:** Let the online build reuse work from its last run, so each push goes green sooner.
  Kind: chore.
  Source: review-code-2026-09-10 optimisation pass.
  Lanes: ci.

- 📋 [UTA-0098] **umap room sampling grows with the level box and runs on one thread; dormant until the box is real.**
  Found by the optimisation pass of 2026-09-10. The mechanism is checked
  against the source: src/umap/Build.cpp samples a lattice over the
  level's box, calls `roomAt` per point on one thread, and samples nothing
  when `boundsValid` is false.

  Measured by the pass on copies given a real box: AS-Frigate 2.72 s at
  the default 32-unit spacing, and CTF-Face refused outright at 3.3e9
  samples, its box being mostly sky. Its census found 1,258 maps would
  need over 1e8 samples.

  Decide WHICH box to sample first -- the playable area, not the sky --
  because that matters more than any code change. Then split the loop into
  y-row bands with `JobSystem::parallelFor`, merged in band order, one job
  per band. Determinism must hold byte for byte at 1, 2 and N workers.

  Defers itself: nothing to do until a real box reaches this code. Not
  part of 0.1.0's cut condition.
  Also arms a test (2026-09-10): measured, the real-asset tier's INV-6
  ring checks examine zero footprints on every map, because the lattice
  is empty without a valid box. Whatever box this item settles on is what
  turns those checks from vacuous into real. UTA-0099 makes the count
  visible in the meantime.
  **Layman:** Working out the rooms of a big map could take seconds once real map sizes are used; decide which area to sample before speeding it up.
  Kind: investigate.
  Source: review-code-2026-09-10 optimisation pass.
  Lanes: umap.

- ✅ [UTA-0099] **A real-asset umap test has ring checks that examine nothing, on every map, because no sample is ever taken.**
  Found by the optimisation pass of 2026-09-10. The mechanism is checked
  against the source: src/umap/Build.cpp returns an empty sample lattice
  when `Model::boundsValid` is false, and tests/real/RealInstallTest.cpp's
  "every node's own zone record agrees with the descent" relies on that
  lattice for its ring checks.

  The pass measured only 3 of 2,021 maps in the reference library with a
  valid box. So on nearly every map the ring checks run over nothing and
  pass. A test that cannot fail is a defect in the test. The number is the
  pass's measurement, taken with a scratch tool that did not survive the
  session; re-measure it before relying on it.

  The fix is the test's: find a box source that real maps carry, or have
  the test count and assert how many maps it actually checked. Its
  comment about choosing a 512-unit spacing assumes a box that is empty.
  Worth reading beside UTA-0079.

  Not part of 0.1.0's cut condition, but it is S7-adjacent: the real tier
  reports green here without evidence.
  Re-measured (2026-09-10), as this body asked, with the test's own map
  selection, Model choice and 512-unit spacing. 2,021 maps have a parsing
  Model; exactly 3 have `boundsValid` on their largest one (MH-Spacemars,
  MH-mG-Spacemarsbeta-fix6, MH-(_@_)_Nevada_Fallout_V3), confirming the
  pass's figure. But even those 3 build no footprint: of 32,907 rooms,
  none has one, so the INV-6 ring REQUIREs run ZERO times across the
  install. The headline is corrected from "almost every map" to every map.

  The test's OTHER half is real and must not be confused with this: its
  INV-2 probes use the descent tables, not the lattice, and its own
  comment records 11,126,404 of them.

  RoomBuildOptions has no box override, so a test cannot supply one.
  Requiring the ring count to be above zero would go red on main today;
  supplying a real box is UTA-0098's open design decision, not this
  item's. So the fix here is to count the rings examined, report the
  count every run, and correct the comment that claims INV-6 is checked on
  real geometry. The REQUIREs go live by themselves once a box does.
  Claimed (2026-09-10) by session `ut-ants-b3` in the MAIN checkout
  `/mnt/Games/Scripts/Linux/UT_Ants`, after UTA-0097 closed. Rule 1 of the
  user's order: review-sourced. The fix is the one this body settles on:
  count the rings examined, report it every run, correct the comment.
  Shipped (2026-09-10) at `9f6d576`. The zone-record test now counts the
  rings its real-geometry INV-6 checks examine and WARNs the count every
  run -- "outer rings examined 0" today -- and its comment says plainly
  that the ring half examines nothing until the lattice has a box
  (UTA-0098). Passes 1/1 locally.

  CI run 34468884284 is green on GCC 14, Clang 19 and MSVC, but as with
  UTA-0095 the real-asset tier is local-only by design (S7), so no CI leg
  compiles this file; the local run is the only evidence.
  **Layman:** One of the real-map tests has been passing without actually checking anything; make it check something, or say plainly what it cannot check.
  Kind: test.
  Source: review-code-2026-09-10 optimisation pass.
  Lanes: umap, tests.

- 📋 [UTA-0101] **ut-dump: every map's Title, Author and total monster capacity.**
  Requested by UT_MonsterHunt on 2026-09-10, two asks folded into one
  item because both are additions to ut-dump's per-package JSON.

  Title and Author come from the map's LevelInfo (or its LevelSummary).
  They feed that project's map-creators registry and name normalisation:
  the user wants map names made consistent AND every creator kept on
  record, so a rename never loses the credit.

  Monster capacity is the sum of `capacity` over every ThingFactory
  descendant (CreatureFactory and its kin), plus the ScriptedPawns placed
  directly in the map. A factory's `capacity` may come from its class's
  defaults rather than from the map, so it is read through the class
  family, as the scratch actor dump of the same day did. The HUD counts
  only monsters alive now; this is the whole-map total, and it also feeds
  the vote window's per-map facts.

  Additive keys, so the schema number stays. Verify against a handful of
  maps opened in the editor, and say which maps' factories carry an
  unlimited or unset capacity rather than folding them into a sum.

  Not part of 0.1.0's cut condition.
  Encoding requirement, measured 2026-09-10 before anyone builds this.
  UT99 map text is 8-bit Windows text, not UTF-8, and upkg passes string
  bytes through untouched. ut-dump's `writeJsonString` copies bytes above
  0x7F verbatim, so emitting free-text Title and Author as-is produces
  invalid JSON. Today's output survives only because every field it prints
  is an ASCII identifier.

  A scratch scan of Title, Author, LevelEnterText and event messages over
  2,022 maps found 16,054 ASCII fields, 41 already valid UTF-8, and 81
  Windows-1252 (e.g. a middle dot, a registered sign), with none needing a
  Latin-1 fallback. The rule that handled all of them: keep a field if it
  is valid UTF-8, else decode it as Windows-1252, falling back to Latin-1.
  Emit UTF-8 JSON by that rule, and test it with a Windows-1252 byte.

  The same scan, handed to UT_MonsterHunt as a TSV, found LevelSummary's
  Title set on 2,005 maps against LevelInfo's 1,812, so report both.
  Linked (2026-09-10): UT_MonsterHunt GAME-0076, the whole-map
  monsters-left HUD this total feeds, and GAME-0070, the map-creators
  table built from the Title/Author scan handed over that day.
  **Layman:** Let the map-inspection tool report each map's name, who made it, and how many monsters it can hold in total.
  Kind: feature.
  Source: consumer-request-2026-09-10 UT_MonsterHunt.
  Lanes: upkg.

- 📋 [UTA-0103] **Split the real-asset test file by subject, and share its System-package resolver.**
  The user's standing request (2026-09-10): refactor files at every
  opportunity, because several sessions will soon work one project at once.

  tests/real/RealInstallTest.cpp holds the real-asset tier for three
  subsystems in one file: the package reader (UTA-0003, UTA-0004, UTA-0005,
  UTA-0057, UTA-0069), the nav and wiring graphs (UTA-0006) and the room map
  (UTA-0007). `git log` shows eight different items editing it -- separate
  histories and separate callers, which is coding.md section 1.8's seam,
  not a line count.

  It also carries three copies of one System-package resolver (a map of
  opened packages plus their bytes), which is past the Rule of Three.
  Move that into a shared test-support helper, then split the cases by
  subject.

  Do this AFTER UTA-0095, which changes the same reader code, so the two
  do not fight over one file. Behaviour must not move: the tier's case
  names and results stay identical before and after.

  Not part of 0.1.0's cut condition.
  **Layman:** Break one very large test file into a few smaller ones by topic, so two people working on different parts do not edit the same file.
  Kind: refactor.
  Source: user-request-2026-09-10 standing refactor rule.
  Lanes: tests.

- 📋 [UTA-0104] **One map standard: every baked map stores and applies its textures, surfaces and everything else the same defined way, so maps from different creators never conflict.**
  The user's requirement (2026-09-10), given in response to the PolyFlags
  finding recorded on UTA-0009: the maps we create from the import must all
  follow a standard, so that updating and fixing them is seamless.

  What the finding showed. UT99 carries a surface's kind only as a raw
  32-bit PolyFlags word on each surface. Over the reference install,
  surfaces sharing one texture disagree often (translucent 30%, portal
  51%, wavy 54%, sky 72%), texture objects carry no PolyFlags of their
  own, and several heavily used bits have no meaning in any source this
  project holds. Passing that word through to the bundle would make every
  baked map a different dialect.

  The reading of the requirement, stated so the user can correct it:

  - Every baked map stores each surface in ONE documented form of this
    project's own: a fixed set of surface kinds, each with a written
    meaning, rather than UT99's raw bits.
  - One conversion rule set, applied identically to every map at bake
    time, so two maps with the same UT99 surface get the same result.
  - The original raw PolyFlags kept beside it, so a conversion can be
    audited and re-derived.
  - One sanctioned way to FIX a surface after the fact -- through the
    map's recipe, never by editing a bake -- which is what makes a fix
    survive a re-bake.
  - A check that fails any baked map not conforming, so the standard is
    enforced rather than hoped for.

  Needs a spec: it is an on-disk contract in the bundle that the renderer
  (UTA-0089's water and glass), the editor (UTA-0034) and every future
  fix bind to. The surface kinds are grounded in the bit meanings measured
  for UTA-0009, and only in bits with a known meaning.

  Belongs with the baker (UTA-0011), which writes the geometry into the
  bundle.
  Scope clarified by the user (2026-09-10), in their words: "What I meant
  is that the way we store and apply textures and anything else in the
  map is following some sort of standard that we have defined for maps so
  that two maps from different creators don't have conflicts in how it is
  interpreted."

  So this is a MAP STANDARD, wider than the surface-flag reading above,
  which it absorbs: surfaces are one part of it, not the whole. It covers
  how every baked map stores and applies its materials and textures, its
  surfaces, and everything else it carries, defined once by this project,
  so no creator's private convention changes how another creator's map
  is read.

  A conflict of exactly this kind is already measured. UT_MonsterHunt
  found Textures/wonderland.utx shadowing Sounds/wonderland.uax on ten
  maps, because two unrelated packages share a name and UT99 resolves by
  search order. So identity is part of the standard: anything a map
  refers to is identified by the package it came from, never by a bare
  name.

  Needs a spec, and probably a project standard beside it, since
  everything the baker writes is built under it. UTA-0009's materials are
  the first thing bound by it.
  Decided by the user (2026-09-10): two packages sharing a name are
  told apart by a short fingerprint of each package's contents, so two
  different files get different names automatically and identical
  copies are recognised as one. The fingerprint's form is this item's
  to fix; UTA-0009's material names carry it in their package segment.

  Open question for this item's spec, raised by the UTA-0009 contract
  review: textures carry bool flags such as bMasked, and whether the
  engine combines those with each surface's own PolyFlags is
  unmeasured here. Settle it before fixing how a surface's kind is
  derived.
  **Layman:** Every imported map follows the same rules for how its textures, surfaces and everything else are stored and used, so two maps by different creators are never read differently or clash.
  Kind: feature.
  Source: user-request-2026-09-10.
  Lanes: ubake, ubundle, urecipe.

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
  Widened (user, 2026-09-08): the same complaint, raised again with the
  same weapon, but "it overwhelms the sound" has a second reading this
  item did not cover and both are wanted.

  The requirement as filed is about PRIORITY -- speech ducking a loud
  weapon so the announcer stays audible. That is one half. The other is
  VOICE COUNT: the flak cannon's shrapnel spawns many near-simultaneous
  impact sounds, and dozens of voices summing at once saturate the master
  bus. That clips and muddies the mix EVEN IF the ducking works
  perfectly, because nothing is competing for priority -- it is one
  category drowning itself.

  So this item owns both, and they need different mechanisms. Priority is
  category gain or a compressor keyed on the speech bus. Voice count is a
  polyphony cap per sound cue with priority-based stealing, plus
  near-coincident identical impacts collapsing to one voice rather than
  N. A mixer that solves one and not the other still has the defect the
  user reported.

  Measurable in the same spirit as the existing rule, and worth stating
  separately: firing the loudest weapon continuously, the master bus does
  not clip and the shrapnel reads as one dense impact rather than a
  wall. Whoever builds this states both measurements.

  Still 0.2.0 and still blocked by the core weapon set -- there is no
  weapon to overwhelm anything with yet.
  **Layman:** Make sure the important sounds -- the announcer, warnings -- are still audible when a loud weapon is firing, instead of being buried.
  Kind: implement.
  Source: user-request-2026-09-05.
  Lanes: uaudio.

- 📋 [UTA-0105] **Animated textures: fire, rippling water, wet and ice textures move again.**
  Decided by the user 2026-09-10: the first version shows these
  as a still picture where one exists, and real animation is its own
  item, linked to the water-and-glass rendering work (UTA-0089).

  upkg reads FireTexture, WetTexture and IceTexture today and refuses
  WaveTexture, whose extra data it does not describe. So this item
  starts by reading WaveTexture, then decides per class whether the
  motion is recomputed at run time or baked into frames. UTA-0009's
  spec defers these classes to here.
  **Layman:** Fire, rippling water and other textures that moved by themselves in the original move again, instead of showing as still pictures.
  Kind: feature.
  Source: user-request-2026-09-10.
  Lanes: umat, upkg, render.

- 📋 [UTA-0106] **Replacement images: a locally supplied PNG stands in for a texture.**
  Decided by the user 2026-09-10: after the first version, PNG
  files. The recipe references the replacement and the file is
  supplied locally, never committed. UTA-0009's spec already takes a
  replacement as an RGBA image and keeps the replaced texture's
  identity; this item adds decoding the file and the recipe field, and
  the replacement's bytes must reach the bundle's name through the
  recipe (design.md, Content addressing).
  **Layman:** A better picture for a texture, supplied on your own machine as a PNG file, is used in place of the original when a map is imported.
  Kind: feature.
  Source: user-request-2026-09-10.
  Lanes: umat, urecipe.

- 📋 [UTA-0107] **Optional AI enlarging tool that produces replacement images.**
  Decided by the user 2026-09-10: the baker keeps its built-in
  classic enlarger, and AI enlarging is an optional separate tool
  whose output is a replacement image. It runs after the replacement
  image item, which it depends on. It is a tool, not part of the
  baker, so bake determinism is unaffected.
  **Layman:** An optional tool uses AI to make sharper versions of the original textures, saved as replacement pictures the importer can use.
  Kind: feature.
  Source: user-request-2026-09-10.
  Lanes: tools.

- 📋 [UTA-0108] **A rule setting that turns off landing damage, while leaving the world still kills.**
  The user's requirement (2026-09-10): a setting that turns off landing
  damage, the harm from hitting the ground too fast. Falling off the edge
  of the world must still kill.

  Decided with the user (2026-09-10):
  - It spares players and bots. Monsters keep landing damage.
  - Whoever runs the game sets it, on the footing of the other per-map
    rule settings (S10). The default is UT99's: landing damage on.

  The robustness requirement is a separation. Landing damage and leaving
  the world are two different ways to die, and the setting reaches only
  the first. In UT99 the second is a kill zone or falling outside every
  zone. Our engine keeps that split: a pawn in a kill zone, or below the
  world, dies whatever this setting says. Landing damage belongs to the
  movement model (UTA-0017), which measures UT99's threshold rather than
  recalling it.

  Twin in UT_MonsterHunt: asked for by message from ut-ants-0c on
  2026-09-10, for the live UT99 server first.
  Twin filed (2026-09-10): UT_MonsterHunt GAME-0079,
  MHMonsterHealth.bNoLandingDamage, default off. Its survey of the UT99
  sources, for our design:
  - UT99 gives landing damage and leaving the world one damage name,
    'Fell'. A landing arrives through TakeDamage with no instigator (a
    monster's landing names itself). Pawn.FellOutOfWorld and a map's
    TriggeredDeath call Died directly, so no damage hook sees them. Its
    filter is therefore 'Fell' with no instigator, on players and bots.
  - One landing slips past that filter: SkaarjBot's lethal dodge landing
    calls Died directly. Left as a documented gap there.
  - Kill zones are native code, not yet confirmed; that project is
    probing one with the setting on.
  Design lesson: give landing damage its own kind here, never a name
  shared with leaving the world, so this setting can target it without
  inferring it from who caused it.
  From UT_MonsterHunt (ut-monsterhunt-71, 2026-09-10): its GAME-0077 is
  withdrawn. The "zombie kills without damage" were landing damage:
  Pawn.TakeFallingDamage calls TakeDamage(1000, None, 'Fell'), which a
  damage hook sees. One landing is unexplained: a bot landed at
  vz=-45261 with no logged hit that accounts for it, so something no
  damage hook sees set that velocity. It still arrived as 'Fell', so a
  landing-damage setting absorbs it. One more death that bypasses every
  damage hook: bpak.BPulseGun kills its own holder via
  Died(None, 'Fell') at 1000 health (their GAME-0080).
  Kill-zone result from UT_MonsterHunt (2026-09-10, fell6.log on
  MH-Crimson-BP, bots placed by SetLocation), with bNoLandingDamage on:
  - A bot placed in a kill zone (LavaZone, bKillZone=True) died in about
    2 s of the lava's own burn damage, type 'Burned', through TakeDamage.
    An instant native kill-zone kill was not observed. 'Burned' is not
    'Fell', so the setting never touched it.
  - A bot placed below the world died 5 s later, type 'Fell', with no
    TakeDamage logged: Pawn.FellOutOfWorld, which reaches no damage hook.
    SetLocation also accepted a point outside the level.
  So kill zones and leaving the world still kill with the setting on.
  Not yet shown: a landing actually absorbed. Their drop probe is next.
  **Layman:** Whoever runs the game can switch off fall damage, but falling out of the map still kills you.
  Kind: feature.
  Source: user-request-2026-09-10.
  Lanes: uworld, ugame.

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
  Same feature as UT_MonsterHunt's GAME-0015 (a bot steps out of a
  player's way). Share what works rather than solve it twice.
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

- 📋 [UTA-0073] **uai: bots read the ground in front of them and jump or go around.**
  User request (2026-09-08): a bot should interpret what is in front of
  it -- a gap it must jump, an obstacle it must jump over or walk around
  -- rather than only following a route.

  Filed separately from UTA-0025 because that item is navigation over the
  waypoint GRAPH plus combat. This is what happens between two waypoints,
  where the graph says "go there" and says nothing about the ground. A bot
  that only follows the graph walks into the crate somebody added after
  the map was last pathed.

  The user's constraint, given in the same breath: as cheap as possible.
  That is a design constraint, not a preference, and it decides the shape.
  Three things follow.

  Prefer what the map already states. UTA-0057 extracted the ReachSpec
  path graph, and a ReachSpec already carries the traversal it needs --
  its own flags and its collision radius and height. Where the graph
  answers, that costs a lookup and no geometry work at all, and it is also
  what the original game did.

  Probe geometry only where the graph is silent, and only ahead. A short
  downward probe for a gap and a short forward probe for an obstacle,
  along the direction of travel, is the whole of it. Not a visibility
  model, not a mesh, and nothing that walks the BSP per frame.

  Budget it explicitly. A probe per bot per tick, amortised across bots
  rather than every bot probing every frame, with a stated ceiling; and
  the answer cached against the bot's current path edge, since it does not
  change while the geometry does not.

  Decide against a measurement, not by argument: the cost per bot per tick
  at a realistic bot count, on a map with the obstacles this is for.

  Depends on uworld and unav, never on urender (rule 6) -- a bot cannot
  know anything the server does not simulate, which rules out anything
  resembling a rendered depth probe.

  Blocked-by: nothing filed. It wants UTA-0025's bot to exist first, in
  practice, since there is nothing to steer until then.
  **Layman:** Bots notice a gap or a crate in front of them and jump it, jump onto it, or walk around it, instead of running into it.
  Kind: feature.
  Source: user-request-2026-09-08.

- 📋 [UTA-0092] **Give every baked map more than one player spawn point.**
  The user's requirement (2026-09-10): when a map is imported, check
  that it has more than one spawn point. Where it has only one, add more,
  even if the new ones sit right next to the original.

  In UT99 a spawn point is a PlayerStart actor. The bake counts them.
  Where it finds one, it places extra starts beside it, each tested
  against the level's collision so nobody spawns inside a wall. The added
  starts go in the bundle, never into the player's own map file.

  Open for whoever picks this up: how many to add, and whether a map with
  no PlayerStart at all is refused or given one.

  Filed under 0.3.0 because that is where several players first spawn
  together (UTA-0027). The bake side is UTA-0011's, and placing a start
  clear of walls needs UTA-0017's collision.

  Blocked-by: UTA-0011.
  Evidence (2026-09-10): a ut-dump sweep of the reference library found
  23 Monster Hunt maps with one PlayerStart or none, one of them with
  none. A custom PlayerStart subclass is counted under its own class, so
  the list is candidates, not verdicts. Shared with UT_MonsterHunt, which
  can fix the same maps on the live server.
  Confirmed in play (UT_MonsterHunt, 2026-09-10). Its own reader, a
  parse of each map's export table, found the same 23 one-start maps,
  name for name. On 2026-09-09 the user played MH-AirForce-BaseRemidas,
  which has one PlayerStart: in about 13 minutes bots respawned 40, 38
  and 27 times, each spawn killing whoever stood on the start.
  Linked (2026-09-10): UT_MonsterHunt GAME-0075, the 23 one-start maps and
  their telefrag loops on the live server.
  **Layman:** If a map only has one place for players to appear, add a few more beside it, so several players joining together are not all dropped on the same spot.
  Kind: feature.
  Source: user-request-2026-09-10.
  Lanes: ubake, ugame.

- 📋 [UTA-0093] **Spawn protection: a few seconds of safety after a player appears.**
  The user's requirement (2026-09-10): a spawn protection timer of about
  three seconds, so a player gets their bearings even when monsters have
  chased the players or bots back into the spawn room.

  Three seconds is the user's starting figure, not a measured one. It is
  a rule default a server operator can change, on the same footing as the
  other per-map rule settings (S10).

  Open for whoever picks this up: whether protection ends early when the
  player fires or picks something up, as it does in many shooters, and
  how a protected player looks to others.

  Filed under 0.3.0 beside Deathmatch (UTA-0027), where it first matters.
  Monster Hunt (UTA-0029) uses the same rule.
  Twin in UT_MonsterHunt (2026-09-10): GAME-0066, "Nothing protects a
  respawn". That project can fix the live UT99 server with a mutator
  first; what it learns in real play -- whether three seconds is right,
  whether firing should end protection early -- is the evidence this item
  should build on rather than guess. Asked for by message from session
  ut-ants-b3.
  Evidence from UT_MonsterHunt's live server (2026-09-10), for whoever
  designs this:
  - Its default is 5 s; the user told us about 3 s. It is collecting
    real-play numbers on the window and will send them.
  - Protection deliberately does NOT end when the player fires: this is
    co-op against monsters, so nobody is spawn-camped.
  - In UT99, health is also lost WITHOUT passing through TakeDamage
    (MH-KillThemAllEG-BP), so a damage hook alone cannot protect
    everything. Our protection must cover every path health leaves by.
  - Bots take self-damage and players do not; they zero it for bots.
  - The user wants team awards (Invulnerability, Damage Amplifier, 30 s)
    to survive a death while the award is still running.
  Design constraint (UT_MonsterHunt, 2026-09-10): the unseen health loss
  on MH-KillThemAllEG-BP was BPak's BSniperRifle and BSaw, whose
  `ProcessTraceHit` does `Pawn(Owner).Health -= 100` on every shot -- a
  direct write that never reaches TakeDamage, so every protection and
  accounting hook missed it. In our engine, make Health writable through
  one function only, and route a weapon's cost-of-use charge through it,
  so spawn protection and the kill feed cannot be bypassed.
  A second way around a damage hook (UT_MonsterHunt GAME-0077,
  2026-09-10): a zombie, qZombie, kills instantly without any damage call,
  through Died() or a direct Health write. With BPak's direct write
  (GAME-0073, which that project reduced to 10 HP a shot in its own
  subclass) that is two paths. Protection here must hold on every path by
  which a pawn loses health or dies, not only on damage.
  Correction (UT_MonsterHunt, 2026-09-10, by message to ut-ants-0c):
  the GAME-0077 paragraph above is withdrawn. Those deaths were engine
  landing damage -- Pawn.TakeFallingDamage calls TakeDamage(1000,
  'Fell') -- which a damage hook DOES see. qZombie writes only its own
  Health. The pawns were bots launched by their own blasts: the mutator
  zeroed bot self-damage but not self-momentum, so point-blank self-hits
  stacked into a launch. Design lesson: cancelling damage without
  cancelling its momentum turns a self-kill into a falling death. BPak's
  direct Health write (GAME-0073) remains the one known path around a
  damage hook.
  More paths around a damage hook (UT_MonsterHunt source survey,
  2026-09-10): bpak's BPulseGun calls Died on its own holder once the
  holder's health reaches 1000, and Pawn.FellOutOfWorld and UnrealShare
  TriggeredDeath call Died directly. None passes through TakeDamage.
  Refined (UT_MonsterHunt, 2026-09-10): the launches were not only a
  bot's own blasts. Teammates' blasts launched bots too -- bpak's
  BFlakCannon fireballs call HurtRadius with a large momentum many times
  a second -- because friendly fire set to zero still pushes, and the
  players' anti-boost rule (BarbiesWorld KHMBase bUseAntiBoost) zeroes
  momentum only when both sides are PlayerPawn, so bots fall outside it.
  Lesson: friendly fire off still pushes, and an anti-boost that tests
  for human players misses bots. That project's fix extends anti-boost
  to every pair of players and bots; monsters still push everyone.
  From UT_MonsterHunt (ut-monsterhunt-71, 2026-09-10): the push that
  throws bots into lethal landings is mostly bpak.BFlakCannon's BBelch
  fireballs, each running HurtRadius(400, 150, None, 60000) every 0.02 s.
  The bot's own fireballs and its teammates' both do it. A team hit under
  FriendlyFireScale=0 does no damage but still pushes. Their fix,
  MHMonsterHealth.bBotAntiBoost (default True), zeroes the momentum
  between every pair of players and bots; in test on their side.
  For our design: decide whether friendly fire off also removes the push.
  UT99's answer is no, and that is what throws bots off ledges.
  **Layman:** For about three seconds after you appear, monsters and other players cannot hurt you, so you have time to see where you are.
  Kind: feature.
  Source: user-request-2026-09-10.
  Lanes: ugame.

- 📋 [UTA-0100] **upkg's effectiveDefaults merges in quadratic time; index it if UTA-0023 calls it per actor.**
  Found by the optimisation pass of 2026-09-10 and checked against the
  source: src/upkg/Class.cpp's `effectiveDefaults` searches the merged
  vector with `find_if`, folding every candidate name.

  Measured by the pass: about 1 ms per class on average, worst 5.9 ms, and
  merges reach 4,282 properties. All 506 BotPack classes took 555 ms.
  Nothing calls it outside tests today.

  If UTA-0023 calls it per class it costs about 50 ms a bake and is not
  worth changing. If it calls it per actor, add a side index keyed on
  (folded name, array index), and never build the output by iterating an
  unordered index, or the output order changes.

  Defers itself until UTA-0023 is designed.
  **Layman:** Working out a game object's settings slows down on big class trees; only worth fixing once the code that calls it often exists.
  Kind: investigate.
  Source: review-code-2026-09-10 optimisation pass.
  Lanes: upkg.

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
  Data source (2026-09-10): UT_MonsterHunt's docs/fakewalls-report.txt
  and GAME-0014 hold the walls a player is MEANT to walk through,
  confirmed in the running game -- exactly the set this item bakes into
  visible openings.
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

- 📋 [UTA-0102] **Monster Hunt HUD: show the monsters left in the whole map, beside the ones alive now.**
  The user's requirement (2026-09-10): UT99's Monster Hunt counter of
  enemies left counts only the monsters alive right now -- the number the
  engine can hold at once -- so it says little about how much of the map
  is left. Keep that number, because it is useful, and add the total
  still to come for the whole map.

  The total is the map's full monster capacity (every monster factory's
  capacity plus the monsters placed directly) minus those already killed.
  The static half is read at bake time; UTA-0101 does it for ut-dump. A
  factory can spawn without limit, and the HUD must then say so rather
  than show a large number as if it were real.

  The live UT99 server gets the same counter from UT_MonsterHunt, which
  owns that runtime side; share how players read it.

  Belongs with Monster Hunt's rules (UTA-0029) and the HUD (uui).
  Linked (2026-09-10): UT_MonsterHunt GAME-0076, the same counter on the
  live UT99 server.
  **Layman:** Show two monster counts in Monster Hunt: how many are around right now, and how many are left to beat in the whole map.
  Kind: feature.
  Source: user-request-2026-09-10.
  Lanes: uui, ugame.

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

- 📋 [UTA-0080] **Players' machines fetch any content they lack from each other -- maps, characters, monster models and the rest.**
  Asked for by the user on 2026-09-08, with both decisions taken then.

  BOTH routes, redirect preferred and the in-game channel as the fallback.
  An HTTP redirect is what nearly every live UT99 server actually uses: the
  server hands the client a web address and the file comes from there, which
  is fast and keeps file serving out of the game protocol. The native
  channel carries the file down the game connection itself and needs no web
  host, which is what makes it the fallback rather than the omission.

  1.0.0, alongside UTA-0038. A player joining the live Monster Hunt
  rotation without the map is exactly the problem that milestone exists to
  solve, and it needs client/server networking that no earlier milestone
  builds.

  THE SECURITY PROBLEM IS THE DESIGN PROBLEM, and it is why this wants a
  spec before any code. UT99's own version of this feature is a known
  malware route: a server hands a client arbitrary package files, and a
  UT99 `.u` package carries executable UnrealScript. A client that accepts
  whatever a server sends has given that server code execution. Three
  things follow and none is optional:

  - Accept only the file the server actually named, to a path the client
    chose, never a path from the wire.
  - Decide what a downloaded package is ALLOWED to be. A map is content; a
    code package is not the same risk and probably is not accepted at all.
  - Design rule 18's shape applies here too -- a client cannot be made to
    trust what the server sends just because the server sent it.

  Blocked-by: client/server networking, which no item builds yet.
  Timing, from the user 2026-09-08: in UT99 the download happens AT MAP
  CHANGE, during a session -- the server switches to the next map in the
  rotation and the client fetches it then, behind the loading screen. It is
  not a pre-join or lobby step.

  That places this on the map-change path rather than beside the connect
  handshake, and it sets what the player sees: a wait between maps, with
  progress, and a way for the fetch to fail without dropping the player
  from the server. A client that cannot get the map is the case to design
  for, not the exception -- UT99's own answer is that the player is
  disconnected, which is the behaviour worth improving on.

  It also means the download budget is a map change, not a first join: the
  rotation keeps moving, so a slow fetch holds up one player rather than
  the server.
  Scope widened by the user (2026-09-10): not maps alone. Anything one
  player's machine has and another's lacks travels -- maps, characters,
  monster models, and every other kind of content -- and in BOTH
  directions: from the host to a joining player, and from a player to the
  host and on to the others (a player's own character, say).

  The limit that does not move: ADR-0003 and ADR-0006. Nothing Epic made is
  ever sent. Every player owns Unreal Tournament, so Epic's content is
  already on every machine; "anything the other lacks" is community content
  in practice, and UTA-0030's stock manifest is what enforces it -- a
  package on the manifest is withheld whatever its hash, one that cannot be
  identified is refused, and an authored bundle (a ued character) is sent
  whole.

  Two points the spec must settle, both raised by the widening:

  - Custom monsters live in `.u` packages, and the body above says a code
    package is "probably not accepted at all". This engine never executes
    UnrealScript (ADR-0004): a `.u` is data to it, read by upkg, whose
    readers are total and never read outside their input. So the risk of
    accepting one is an attack on the reader, not code execution. Decide
    that deliberately; do not inherit UT99's refusal by reflex.
  - The reverse direction is new: a host receiving a player's content and
    serving it onward. The same rules apply the other way -- the host
    chooses the path, never the wire; it accepts only what it asked for;
    and nothing a client sends is trusted because the client sent it.

  Related: UTA-0030 builds the manifest-and-bake mechanism for host to
  player (0.4.0); UTA-0037 packages a character for download (0.6.0). This
  item is the whole of it, in both directions, at 1.0.0.
  Timing decided by the user (2026-09-10). This SUPERSEDES the "Timing,
  from the user 2026-09-08" paragraph above, which placed the download at
  map change and said it was not a pre-join step. Build to this instead:

  - At join, the two machines compare what each has, and the player sees
    a short list of what will be downloaded.
  - The map being played downloads first, straight away, with a progress
    bar and no prompt -- the player is told nothing else about it.
  - The rest of the rotation downloads quietly in the background, so a
    later map change does not wait. Map change is only the fallback, for
    anything the background fetch has not reached yet.
  - Characters and monster models download automatically as well, exactly
    like maps: no prompt, just the list and the progress. Offered as an
    option to ask first; declined.

  The old paragraph's design points still hold wherever the fallback is
  used: a fetch that fails must not drop the player from the server, and
  a slow fetch holds up one player, never the server.
  **Layman:** If a server has a map, a character or a monster you have never seen, the game fetches it for you instead of turning you away -- and anything of yours the others lack reaches them too.
  Kind: feature.
  Source: user-request-2026-09-08.
  Lanes: unet, ubundle.

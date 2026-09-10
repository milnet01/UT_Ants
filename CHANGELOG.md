# Changelog

All notable changes to UT_Ants are documented in this file.

The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). The format
contract is `~/.claude/standards/changelog-format.md` § 4.

The `[Unreleased]` block stays at the top, always, even when empty.

## [Unreleased]

(Nothing yet. Scaffolding is not a release — the first dated section
appears once something has actually shipped.)

### Added

- **umat: a curated material library, found by a fingerprint of each texture's picture** (UTA-0010)
  An entry marks a texture metal or glowing, and reaches every copy of
  its picture, renamed or not. It ships with a seed derived from what
  the textures say about themselves. Its digest is the baker version's
  share (UTA-0011).

- **umat turns a 1999 texture into a modern material** (UTA-0009)
  Each texture is enlarged with a Lanczos filter and gains normal,
  roughness and height maps derived from it, plus a glow map where marked,
  all with full mip chains and block-compressed. The same input gives the
  same bytes on every compiler and thread count. Masked textures get a
  separate see-through variant, and materials are named by package and
  group so two creators' textures never clash.

- **Textures are block-compressed, and a map's texture memory is measured against a budget.** (UTA-0052)
  The baker's texture step can squash a texture into the compressed
  formats a graphics card reads directly (BC4, BC5 and BC7), limit how far a
  small texture is enlarged, and add up the video memory a map's textures
  need. A map over the budget is refused with its textures listed largest
  first; nothing is quietly shrunk to make it fit. Nothing calls this yet:
  the baker that will is UTA-0011.

- **`ubundle` defines the `.utab` container and the origin field that travels with it** (UTA-0008)
  Our own file format for a finished level -- and the one field that
  records whether any of its content came out of somebody's copy of
  Unreal Tournament. A tool that must not link the engine can read the
  first sixteen bytes and answer that question alone.

- **`umap` partitions a level into rooms and answers which room a point is in** (UTA-0007)
  A level's own zones become rooms, each with a traced 2D outline and a
  floor band, plus a lookup that answers which room any point falls in.
  The map screen has something to draw and the server has something to
  record exploration against; neither needs the package reader.

  Outlines are traced from the level's own geometry rather than drawn as
  rectangles, and rooms are split into floor bands, so a tower reads as
  floors rather than as one flattened blur. A zone occupying two separate
  volumes is one room with two parts. A room too thin to catch a sample is
  kept and reported rather than dropped silently.

  Two libraries from one directory: the runtime links the room types and
  the lookup alone, and the builder that reads packages is bake-side only.
  Both link closures are asserted when the build is configured, because a
  convenience dependency added later is how that boundary stops being
  checkable with nothing else failing.

- **`unav` extracts a level's navigation graph and its event-wiring graph** (UTA-0006)
  Two graphs every UT99 level already contains, pulled out as data: where
  a player can walk, and which switch opens which door. The first joins
  the level's reach specs onto the navigation points a designer placed;
  the second matches an actor's `Event` onto every actor carrying that
  `Tag`. Neither needs the original engine running.

  An event reaches EVERY actor carrying the tag, not the first -- one
  switch commonly opens a bank of movers, so a single target would be
  wrong about most of the map library. An event naming a tag no actor
  carries is kept and reported rather than dropped, because it is the only
  record that an author wired something and the target went away.

  It ships as two libraries. The graph types and their queries link
  nothing but the core, so the game can hold a graph without the package
  reader; the builders link the reader and run at bake time only. Both
  link closures are checked when the build is configured, which is what
  keeps that boundary honest rather than aspirational.

- **`upkg` reads a level's bot path graph** (UTA-0057)
  `readLevel` returns a map's actors and its reach specs -- the directed
  connections between navigation points, each carrying the collision size
  it was built for. A navigation point's `Paths` entries index that array,
  which is what lets the connections between waypoints be read without
  running the game.

- **upkg reads class tables, default properties and ancestry across packages** (UTA-0005)
  Works out what a custom actor IS -- what it descends from, and the
  values its author set on it -- by reading the class table rather than
  by running any of its code. The parent chain is followed across
  package boundaries, so a monster defined in one file can be walked to
  a base class in another, and the defaults are merged down that chain
  the way the engine itself resolves them.

  Reaching those defaults meant walking each class's compiled script
  instruction by instruction, because the only length the file records
  is the size the script occupies in memory rather than on disk. Nothing
  is executed: each instruction's operands are read only to learn how
  wide it is.

  Proven against a full Unreal Tournament install: 18,428 class exports
  across 889 packages, every one consumed exactly, and 13,814 ancestry
  chains walked. A package whose content is missing ends the walk
  saying which file or class it wanted, rather than failing.

- **upkg reads typed level content: polygons, palettes, textures and sounds** (UTA-0004)
  Reads a package's brush polygons with their textures and surface flags,
  its palettes, its textures -- including the second block-compressed image
  set later packages carry -- and its sounds. Every reader either consumes
  its object's bytes exactly or reports the file as malformed; it never
  returns half an answer. Proven against a full Unreal Tournament install
  on Linux and Windows.

- **upkg: read the Unreal Engine 1 package container.** (UTA-0003)
  Open a UT file and work out what is inside it -- the index of names and objects. Nothing is drawn yet; this is learning to read the format.

- **Prove the numeric contract holds across GCC, Clang and MSVC.** (UTA-0049)
  The build now sets floating-point contraction and fast-math off on every
  compiler, and refuses to configure if a conflicting flag is passed in. A
  test locks the result by exact bit pattern on GCC, Clang and MSVC, so a
  map baked on one machine cannot quietly differ from the same map baked on
  another.

- **core: the error type, the logger, the filesystem and the job system** (UTA-0002)
  The shared foundation every other part is built on. Failures cross a
  module boundary as std::expected<T, Error>; one logger carries a
  category per part; configuration, cache and logs resolve to where the
  platform puts them; and one job system spreads work across cores while
  the simulation stays single-threaded. The build gains a ThreadSanitizer
  step, which is how the job system's thread-safety is measured.

- **Queued: the game updates itself, signed and opt-in** (UTA-0042)
  Filed as planned work, not shipped. Modelled on finbreak, whose post-mortems record four releases lost to the relaunch step alone.

- **A quarantine guard, so nothing of Epic's can be committed**
  Refuses any tracked path under content/ or matching a quarantined Unreal format. Its extension list is parsed out of .gitignore rather than copied, so the guard cannot pass what git was quietly ignoring.

- **A CI pipeline and a local gate that share one list of steps** (UTA-0041)
  scripts/ci.sh owns the steps; the GitHub workflow calls it and duplicates none of them, so a green run locally means a green run there. It runs before every push via the hook, over the commits being pushed rather than the working tree. A documentation-only push runs the documentation checks -- not nothing, and not the full build.

- **Build system, test harness and the synthetic-package fixtures.** (UTA-0001)
  CMake + Ninja, C++23, Catch2 v3.16.0 fetched by the build rather than installed. The suite passes on a clone with no Unreal Tournament present, which is what S7 is measured on; a second tier behind UTA_REAL_ASSET_TESTS runs against a real install and refuses to configure without a path.

### Changed

- **Every cached bake is invalidated: the map bundle format moves to version 2.** (UTA-0052)
  A `.utab` can now carry block-compressed textures, in a new `TEXS`
  section, so the format version moves from 1 to 2. A reader accepts
  version 2 and nothing else, which is deliberate — so any bundle baked
  before this must be baked again. Nothing is lost but the time: a bundle
  is a bake output, and the format version is one of the baker's own
  inputs, so a version change renames every bundle anyway.

  No released bundle is orphaned. `0.1.0` has not been cut.

- **Make the build faster on a memory-limited machine.** (UTA-0050)
  The build now uses ccache and the mold linker when they are installed,
  and ignores them when they are not, so nothing is required to build. With
  ccache configured as the build docs describe, rebuilding after wiping the
  build folder is close to instant instead of recompiling the test
  framework from scratch.

- **A job that fails can be seen to have failed** (UTA-0047)
  JobHandle::failed() reports whether the work threw, and parallelFor
  returns how many of its bodies did. Previously a job whose body threw was
  caught, logged and then marked complete exactly like a successful one, so
  a batch in which everything failed reported success. That matters for the
  map baker: a half-failed bake reported as good is a wrong map file
  presented as a correct one.

- **Windows is now a first-class target alongside Linux**
  Both are built and tested on every run, Windows with MSVC. The design previously said Windows would not be tested before 1.0; it now says the opposite, and the compiler floor gains MSVC.

### Fixed

- **A BSP node's front and back children were read the wrong way round** (UTA-0078)
  The package reader took a level's two branch links in the opposite order
  to the one the file stores them in, so every "which room is this point
  in?" answer was wrong. Shipped since the BSP tables landed and invisible
  until now.

  Nothing could have caught it earlier. That layout was verified by
  checking the file parses cleanly, and swapping two neighbouring fields of
  the same width changes nothing about parsing -- the order came from the
  field names, which is the one thing that check never tests. No unit test
  can see it either, because the test package writer writes what the reader
  reads and a round trip agrees with itself whichever order both use.

  What found it is UTA-0007's new check against the real game install,
  which holds the lookup against what each node of the level file itself
  records. Before the fix, 0 of 11451 probes on one map agreed; after,
  11406. Across the reference install, 21803383 disagreements of 24304564
  became 30399 of 11126404.

- **fileSink says why it could not open its log file** (UTA-0048)
  It returned a sink that silently did nothing, so a first run with no log
  directory left the game running with logging switched off and no way to
  find out. It now reports the reason, with the error code chosen for the
  cause rather than one code standing for every failure.

- **A dead pattern in the commit hook's link check**
  Found by the new gate on its first run.

### Security

- **resolveUnder refuses names that are unsafe on Windows, on every platform** (UTA-0046)
  A downloaded file called COM1 opens a serial port rather than a file, and
  can block with no timeout; "a." and "a" are one file on Windows after a
  check has passed on two names; and "a.txt:s" writes a hidden data stream
  an extension check cannot see. All three are now refused by name, before
  any filesystem call. The rule is enforced on Linux too, deliberately: a
  rule that holds on one platform and not the other means a server and a
  client disagree about which content is safe.

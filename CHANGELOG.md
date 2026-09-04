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

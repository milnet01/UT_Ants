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

- **Windows is now a first-class target alongside Linux**
  Both are built and tested on every run, Windows with MSVC. The design previously said Windows would not be tested before 1.0; it now says the opposite, and the compiler floor gains MSVC.

### Fixed

- **A dead pattern in the commit hook's link check**
  Found by the new gate on its first run.

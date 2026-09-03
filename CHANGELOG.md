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

- **Build system, test harness and the synthetic-package fixtures.** (UTA-0001)
  CMake + Ninja, C++23, Catch2 v3.16.0 fetched by the build rather than installed. The suite passes on a clone with no Unreal Tournament present, which is what S7 is measured on; a second tier behind UTA_REAL_ASSET_TESTS runs against a real install and refuses to configure without a path.

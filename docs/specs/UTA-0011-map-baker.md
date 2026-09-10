# UTA-0011 — `ubake` and `ut-bake`: bake a map, and check an install

**Status:** accepted (2026-09-10).
**Kind:** implement.
**Source:** ROADMAP UTA-0011 (design-2026-09-03; scope narrowed with the
user 2026-09-10).

**Blocker for:** UTA-0109, UTA-0110, UTA-0111, UTA-0112 and UTA-0113, which
each add a part to this baker; UTA-0016, which runs `--check` and loads a
bake.
**Pairs with:** UTA-0104 (the map standard), UTA-0070 (which packages a
package needs), UTA-0098 (which box the room sampler samples).

**Layman:** The tool that turns one of your Unreal Tournament maps into our
own baked format, and checks that a folder really holds Unreal Tournament.

## 1. Goal

`ut-bake` bakes a map from a player's install into one `.utab` bundle. The
bundle holds the parts that exist today: the level map, the two bot graphs
and the materials. The same map, recipe and baker version give the same file
name and the same bytes on any machine. `ut-bake --check` says whether a
directory is a usable Unreal Tournament install, and why not. Each later part
of a bundle plugs into this baker rather than starting a second one.

## 2. Problem

1. **Nothing turns a map into a bundle.** `ubundle::write` encodes a
   `ubundle::Bundle`. `umap::buildRoomMap`, `unav::buildNavGraph`,
   `unav::buildWiringGraph` and `umat::generate` each build one part. Nothing
   drives them from a `.unr` file.
2. **Both runtime targets depend on a program that does not exist.**
   `docs/design.md` rule 16: *"Each validates an install by running `ut-bake
   --check <path>` and reports what it says, and each triggers a bake the
   same way."*
3. **Accepted specs leave obligations here.**
   - UTA-0010 § 4.5 fixes the order settings are applied in, § 4.6 has this
     item fold `umat::libraryDigest()` into the baker version, and § 4.2 has
     the caller pass `umat::pictureFingerprint` only a texture with no
     `Format` property.
   - UTA-0052: *"Printing the report is `ubake`'s (UTA-0011)."*
   - UTA-0009 § 4.6: *"UTA-0011 builds `<path>` from the export's `outer`
     chain"*. Its § 6 leaves which image stands in for a procedural
     texture to this item.
   - UTA-0008 § 9 gives naming a bundle file to `ubake`. Its § 10 records
     *"The format version is one of the baker's inputs"* as caught by
     nothing until this item.
4. **Nothing says which `Model` is the level's world.** `upkg::readLevel`
   reads the level's `Model` reference and discards it (`src/upkg/Level.cpp`,
   *"Consumed and not returned"*). UTA-0007 § 4.2's measurements took *"the
   largest parsing `Model` in each `.unr`"* instead, which is a guess the
   file does not need.
5. **A material's `metallic` value has nowhere to go.** UTA-0009 § 4.4:
   *"`Material::metallic` carries it"*. `ubundle::Bundle` holds maps only,
   and no map records it. UTA-0009 § 9 leaves *"Writing materials into the
   bundle"* to this item.

## 3. Scope decisions (agreed with the user)

1. **This item is the working baker, and five parts are split out.**
   *User, 2026-09-10*, offered against one large item and against keeping the
   recipe format in. This item holds the command line, `--check`, the baker
   version, naming and caching a bake, and the four sections that exist
   today: `ROOM`, `NAVG`, `WIRG` and `TEXS`. Geometry is UTA-0109; lights and
   actor placements UTA-0110; collision UTA-0111; baked indirect light
   UTA-0112; the recipe format UTA-0113.
2. **Every bake has no recipe until UTA-0113.** Follows from decision 1. The
   name records the absence (§ 4.4), so a recipe arriving later renames the
   bake rather than colliding with it.
3. **A bake is named by SHA-256, not by `umat`'s FNV-1a.** Mine. § 8 carries
   the reason: a hostile package could otherwise take a legitimate map's
   name in a player's cache.
4. **The caller names the output directory.** Mine. § 4.7 and § 15 carry
   why: two documents disagree about where bakes live.
5. **A procedural texture's still picture is its own stored base level.**
   Mine, following the user's decision of 2026-09-10 in UTA-0009 § 6, that
   *"the first version shows each procedural texture as a still picture where
   one exists"*. A stored base level is that picture, where `upkg` reads one.
6. **Packages are searched in UT99's own order.** Mine, measured from the
   reference install's `System/Default.ini`, whose `[Core.System]` section
   lists `Paths` as `../System/*.u`, `../Maps/*.unr`, `../Textures/*.utx`,
   `../Sounds/*.uax`, `../Music/*.umx`, in that order.
7. **A material's `metallic` value is stored in a new `MATS` section.**
   Mine. UTA-0009 § 3 decision 3 keeps metal out of the maps, and § 2 item
   5 shows nothing else can hold it. A section of its own keeps one value per
   material (§ 4.10).

## 4. Design

### 4.1 The files

| File | Holds |
|---|---|
| `src/core/Sha256.h/.cpp` | SHA-256 (§ 4.4) |
| `src/upkg/Level.h/.cpp` | `Level::model`, returned rather than discarded (§ 4.5) |
| `src/ubundle/Bundle.h`, `Sections.h`, `MaterialSection.cpp` | the `MATS` section (§ 4.10) |
| `src/ubake/Install.h/.cpp` | an install, its resolver (§ 4.2) and `checkInstall` (§ 4.9) |
| `src/ubake/Name.h/.cpp` | the baker version (§ 4.3) and the bake name (§ 4.4) |
| `src/ubake/Bake.h/.cpp` | one bake (§ 4.5, § 4.6) and writing it (§ 4.7) |
| `tools/ut-bake/Cli.h/.cpp` | the command line as a function (§ 4.8) |
| `tools/ut-bake/main.cpp` | calls it |

`uta_ubake` links `uta_core`, `uta_upkg`, `uta_umat`, `uta_unav_build`,
`uta_umap_build` and `uta_ubundle`. The two `_build` libraries hold the
graph and room builders. `docs/design.md` rule 2 keeps it out of both
runtime targets. The link-closure test that asserts so belongs to the first
item that builds a runtime target (§ 9).

`tools/ut-bake/Cli.cpp` is compiled into the `ut-bake` binary and into the
unit tests, so the command line is tested without starting a process.

### 4.2 The install and its resolver

```cpp
namespace uta::ubake {

/// An Unreal Tournament install, indexed once by `open`.
class Install {
public:
    /// NotFound when `root` is not a directory.
    [[nodiscard]] static Result<Install> open(const std::filesystem::path& root);

    /// Folds its input as § 4.4 defines, then looks it up in § 4.2's order.
    /// A name that is absent, or whose file does not open as a package, gives
    /// nullptr and no error. Each package is read and opened at most once.
    /// Valid for the lifetime of the Install, across a move of it.
    [[nodiscard]] upkg::PackageResolver resolver();

    /// The file § 4.2's order finds for a package name, whether or not it
    /// opens, or empty when there is none.
    [[nodiscard]] std::filesystem::path pathOf(std::string_view packageName) const;

    /// The bytes of a package the resolver has opened, or an empty span.
    [[nodiscard]] std::span<const std::byte> bytesOf(std::string_view packageName) const;

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
};

}  // namespace uta::ubake
```

**The search order is fixed**: `System/*.u`, `Maps/*.unr`, `Textures/*.utx`,
`Sounds/*.uax`, `Music/*.umx` (§ 3 decision 6). Each directory is matched and
each extension compared case-insensitively, as `ut-dump`'s `SystemPackages`
does. A missing directory contributes nothing.

**An earlier directory shadows a later one.** With `Textures/wonderland.utx`
and `Sounds/wonderland.uax` both present, `wonderland` resolves to the
texture package. That is what UT99 loads, and it is the conflict UTA-0104
measured. Telling the two apart is UTA-0104's; this item reproduces UT99.

**Within one directory**, two files whose names fold to one package name
resolve to the first by bytewise comparison of their file names.

The order is not read from the player's own `.ini`: two players with
different settings would then name one map differently (§ 8).

### 4.3 The baker version

```cpp
namespace uta::ubake {

/// Bumped by hand whenever any code a bake runs -- `ubake`, `umat`, `umap`,
/// `unav`, `upkg` or `ubundle` -- changes what a bake writes.
inline constexpr std::uint32_t BAKER_REVISION = 1;

/// "r<BAKER_REVISION>-f<ubundle::FORMAT_VERSION>-l<umat::libraryDigest()>",
/// the revision and format in decimal, the digest as sixteen lower-case hex
/// digits.
[[nodiscard]] std::string bakerVersion();

}  // namespace uta::ubake
```

It covers the three bake inputs that are neither the map nor the recipe:
the code a bake runs, the framing (`docs/design.md` § Content addressing, *"The
`ubundle` format version is one of the baker's own inputs"*), and the
curated library (UTA-0010 § 4.6).

**A golden bake keeps `BAKER_REVISION` honest** (INV-5). A synthetic map is
baked, and the SHA-256 of the written bundle is compared with a value the
test records beside the revision it was recorded under. A change to what the
baker writes, with no bump, fails it. So does a bump with no re-recording.
It reaches `umat`, `umap` and `unav` only as far as the fixture exercises
them, which § 10 records.

### 4.4 The bake name

```cpp
namespace uta {

/// FIPS 180-4 SHA-256.
class Sha256 {
public:
    void add(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] std::array<std::byte, 32> finish() noexcept;
};

[[nodiscard]] std::array<std::byte, 32> sha256(std::span<const std::byte> bytes) noexcept;

}  // namespace uta

namespace uta::ubake {

/// One package of the map's import closure: its folded name, and the digest of
/// the file it resolves to -- empty when the install's resolver returns no
/// package for it: no file, or a file that does not open.
struct ClosureEntry {
    std::string name;
    std::optional<std::array<std::byte, 32>> digest;
};

struct NameInputs {
    std::string bakerVersion;
    std::string mapName;                     ///< the map file's folded stem
    std::array<std::byte, 32> mapDigest;
    std::vector<ClosureEntry> closure;       ///< any order; the name sorts it
};

/// 64 lower-case hex digits.
[[nodiscard]] Result<std::string> bakeName(const std::filesystem::path& map, Install& install);

namespace detail {
[[nodiscard]] std::string nameOf(const NameInputs& inputs);

/// The folded names of the map's import closure, ascending. Every lookup goes
/// through `resolver`.
[[nodiscard]] Result<std::vector<std::string>> closure(const upkg::Package& map,
                                                     const upkg::PackageResolver& resolver);
}

}  // namespace uta::ubake
```

**The name is the lower-case hex SHA-256 of this byte string**, where `LF`
is the byte `0x0A`:

1. The ASCII text `uta-bake-name-1`, then `LF`. It separates this layout from
   any other hashed string.
2. `bakerVersion()`, then `LF`.
3. The recipe: the one byte `0x00`, meaning no recipe. UTA-0113 defines what
   follows a `0x01`.
4. The map's folded file stem, `LF`, then the 32 bytes of the SHA-256 of the
   map file.
5. For each closure entry in ascending bytewise order of name: the name,
   `LF`, then either `0x01` and the 32-byte digest, or `0x00` where the
   resolver returns no package for it: no file, or one that does not open.

**The import closure** is every package the map's imports name, then every
package those name, until no new name appears. An import names a package
where its outermost outer is null — `ut-dump`'s `importedPackages` rule,
which walks the outer chain rather than stopping at the immediate outer.
Names are folded: ASCII `A`–`Z` become `a`–`z`, every other byte is
kept, and a name is hashed as its UTF-8 bytes. The map's stem in item 4 is
folded the same way. The map itself is left out. A package the resolver returns none for contributes its name and `0x00`,
so installing or repairing it later renames the bake.

**Why the closure, and why before the bake.** *"Every other bake input must
be covered by one of the three, or the name is a lie"* (`docs/design.md`
§ Content addressing). A map's textures and its actors' ancestry live in
other packages. And the name must exist before the bake, to find a cached
one, so it cannot be built from what the bake happened to open. INV-2 checks
that the bake opens nothing outside the closure.

**SHA-256 lives in `core`**, because `unet`'s fingerprint manifest and the
stock manifest's hashes (`docs/design.md` rule 15) are runtime work that
cannot link `ubake`.
Source: <https://csrc.nist.gov/pubs/fips/180-4/upd1/final>.

### 4.5 One bake

```cpp
namespace uta::ubake {

struct SkippedTexture {
    std::string material;   ///< umat::materialId of the variant not made
    std::string reason;
};

struct BakeResult {
    ubundle::Bundle bundle;             ///< origin Derived, kind Map
    umap::RoomBuildReport rooms;
    umat::BudgetReport budget;
    std::vector<SkippedTexture> skipped;
};

/// Build a bundle from one map. Writes nothing and enforces no budget.
[[nodiscard]] Result<BakeResult> bake(const upkg::Package& map, std::string_view mapName,
                                      Install& install, JobSystem& jobs);

namespace detail {

/// The curated library's lookup -- `umat::curated` outside tests.
using CuratedLookup = std::function<const umat::CuratedOverride*(std::uint64_t fingerprint)>;

/// One bake with its dependencies given -- a test seam. Every package lookup
/// goes through `resolver`. `bake` passes `install.resolver()`, `umat::curated`
/// and `umat::TEXTURE_BUDGET_BYTES`; `bakeToDirectory` passes its request's
/// `budgetBytes`.
[[nodiscard]] Result<BakeResult> bake(const upkg::Package& map, std::string_view mapName,
                                      const upkg::PackageResolver& resolver, JobSystem& jobs,
                                      const CuratedLookup& curated, std::uint64_t budgetBytes);

}  // namespace detail

}  // namespace uta::ubake
```

`upkg::Level` gains one member, the reference `readLevel` already reads:

```cpp
struct Level {
    std::vector<ObjectReference> actors;
    std::uint32_t rawSlotCount = 0;
    ObjectReference model;              ///< the level's world BSP -- added here
    std::vector<ReachSpec> reachSpecs;
};
```

In order:

1. **Find the level.** The map's export whose class is `Level`. None, or
   more than one, refuses the bake with `MalformedData` naming the map.
2. **Read it and its world.** `readLevel`, then the export `Level::model`
   names. It must be an export of this package whose class is `Model`, or the
   bake refuses with `MalformedData`. `readModel`'s refusal refuses the bake
   and names the map — UTA-0007 § 6's row for a version-61 package.
3. **`ROOM`** is `umap::buildRoomMap` over that `Model`, with default
   options. Its refusal refuses the bake; its report is kept.
4. **`NAVG`** is `unav::buildNavGraph` with the install's resolver, and
   **`WIRG`** is `unav::buildWiringGraph`. A refusal of either refuses the
   bake.
5. **`TEXS`** and **`MATS`** are § 4.6's materials.
6. **`budget`** is `umat::measure` over every map of every material, against
   the budget given.

**Every section in the steps above is written, and empty where the level
has none**, so a present but empty section says the level was examined
(UTA-0008 § 4.4).

The header's origin is `Origin::Derived`, since a bake read an install
(`docs/design.md` rule 15), and its kind is `BundleKind::Map`.

### 4.6 The materials

**Which textures.** Every non-null `texture` of the `Model`'s `surfs`. Each
reference resolves to one export:

- an export reference is that export of the map;
- an import reference resolves its outermost outer through the install's
  resolver, then takes the export of that package whose name and whose chain
  of outer names match the import's, compared case-insensitively.

**Which variants.** A texture gets the masked variant where any surface
naming it has `PF_MASKED`, and the opaque variant where any surface naming it
does not. `PF_MASKED` is `0x00000002` of `BspSurf::polyFlags`.
Source: <https://wiki.beyondunreal.com/Legacy:PolyFlags>.

**Each variant, in ascending bytewise order of its material id:**

1. The texture's properties, by `upkg::readProperties`. **A `Format`
   property skips the texture** — UTA-0010 § 4.2's rule, and nothing here
   decodes a format other than palettised.
2. Its `Palette` property must name an export of the same package, read by
   `upkg::readPalette`.
3. `upkg::readTexture`. Its base level is `mips[0]`. For a procedural texture
   that is its stored still picture (§ 3 decision 5).
4. `settings` is `MaterialSettings{}`, then `umat::applied` with the entry
   the curated lookup returns for `umat::pictureFingerprint(base, palette)`, where it
   returns one. That is UTA-0010 § 4.5's order with no recipe.
5. `umat::resolve(base, palette, masked)`, then `umat::generate` under the id
   `umat::materialId(package, path, masked)`. `package` is the map's folded
   stem for an export of the map, and the outermost import's name for an
   import. `path` is the names of the export's outers, outermost first, then the
   export's own name, joined by `.` (UTA-0009 § 4.6).

**A texture that cannot be made is skipped, and the bake goes on.** An
unresolved import, a texture of an absent package, a `Format` property, a
missing or imported palette, and a refusal from any `upkg` or `umat` call
above each add one `SkippedTexture` naming the variant and the reason.

**`TEXS` holds each material's maps**: materials in ascending material id,
each one's maps in `MapKind` order as `umat::Material::maps` holds them. Two
resolved textures cannot share an id: the resolver gives one package per
name, and one package gives one export per path.

**`MATS` holds one `ubundle::MaterialRecord` per material**, in the same
order, its `metallic` the value the material was generated with. A skipped
variant has neither maps nor a record.

### 4.7 Caching and writing

```cpp
namespace uta::ubake {

enum class Verdict { Written, Cached, OverBudget };

struct BakeRequest {
    std::filesystem::path install;
    std::filesystem::path map;
    std::filesystem::path outDir;
    bool force = false;
    std::uint64_t budgetBytes = umat::TEXTURE_BUDGET_BYTES;
};

struct BakeOutcome {
    Verdict verdict = Verdict::Written;
    std::string name;
    std::filesystem::path path;             ///< outDir / (name + ".utab")
    std::optional<BakeResult> result;       ///< absent when Cached
};

/// Name, look in the cache, bake, check the budget, write.
[[nodiscard]] Result<BakeOutcome> bakeToDirectory(const BakeRequest& request, JobSystem& jobs);

}  // namespace uta::ubake
```

1. `bakeName`. The file is `outDir / (name + ".utab")`. `outDir` is created
   if absent.
2. **A cached bake** is a file at that path whose first sixteen bytes
   `ubundle::readHeader` accepts. Without `force` it is the outcome, and
   nothing is baked or written. A file there that `readHeader` refuses is
   baked over.
3. `detail::bake` with the request's `budgetBytes`, then
   `umat::enforceBudget` on its report. Over budget, the outcome
   is `OverBudget` and nothing is written — UTA-0052's never-degrade rule.
4. `ubundle::write`, then `fs::writeFileAtomically`.

**No part of the output path comes from the map's contents.** The name is
hex digits and the directory is the caller's.

**The caller picks `outDir`.** `docs/design.md` rule 15 puts *"every `.utab`
baked from"* an install under `content/`. `fs::cacheDirectory` in
`src/core/FileSystem.h` says it holds *"baked bundles"*. This item does not
choose between them (§ 15).

### 4.8 The command line

```text
ut-bake --check <install>
ut-bake --install <install> --out <dir> [--force] <map>
ut-bake --help
```

**Standard output is exactly one JSON object**, and standard error carries
any lines meant for a person, as `ut-dump` does. String escapes are
`ut-dump`'s. The exit code:

| Code | Means |
|---|---|
| `0` | the install checked out; or the bake was written or found cached |
| `1` | the install did not check out; or the bake was refused or over budget |
| `2` | the arguments were wrong |

`--check` prints:

```json
{"schema": 1, "install": "<as given>", "ok": true,
 "problems": [{"what": "<file or directory>", "why": "<a sentence>"}]}
```

A bake prints:

```json
{"schema": 1, "map": "<as given>", "bakerVersion": "<§ 4.3>",
 "verdict": "written | cached | over-budget | refused",
 "name": "<64 hex digits>", "path": "<the file>",
 "error": "<a sentence>",
 "rooms": {"withoutFootprint": [0], "refusedZones": [0]},
 "budget": {"workingSetBytes": 0, "budgetBytes": 0,
            "byTexture": [{"name": "<map name>", "bytes": 0}]},
 "skipped": [{"material": "<id>", "why": "<a sentence>"}]}
```

- `error` appears only on `refused`.
- `name` and `path` appear on `written`, `cached` and `over-budget`.
- `rooms`, `budget` and `skipped` appear on `written` and `over-budget`.

`verdict`, `path`, `ok`, `problems` and the exit code are what UTA-0016
binds to. Every field is part of the command line's output shape, which
`docs/standards/versioning-overrides.md` § Breaking surfaces lists.

```cpp
namespace uta::ubake {
/// `args` excludes the program name. Returns the exit code.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err);

namespace detail {
/// `runCli` with the bake's budget given -- a test seam.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err, std::uint64_t budgetBytes);
}
}
```

### 4.9 Checking an install

```cpp
namespace uta::ubake {

struct Problem {
    std::string what;
    std::string why;
};

struct CheckReport {
    bool ok = false;
    std::vector<Problem> problems;
};

[[nodiscard]] CheckReport checkInstall(const std::filesystem::path& root);

}  // namespace uta::ubake
```

`ok` is true when there are no problems. The problems:

- `root` is not a directory.
- Each of `Core`, `Engine` and `Botpack` for which `Install::pathOf`
  returns empty: *"System/Botpack.u is missing, so this is not an Unreal
  Tournament install."*
- Each of those whose file, read and handed to `upkg::Package::open`, is
  refused, with that refusal's message.

`Core` and `Engine` hold the classes a level's actors descend from; `ut-dump`
records a map's `Teleporter` descending from `NavigationPoint` in
`Engine.u`. `Botpack` is Unreal Tournament's own game package, which is what
separates its install from any other Unreal Engine 1 game's.

### 4.10 The `MATS` section

```cpp
namespace uta::ubundle {

inline constexpr std::uint32_t FORMAT_VERSION = 3;  // 3 since UTA-0011

/// One material's own values. Its maps are the TEXS entries named `<id>:<map>`.
struct MaterialRecord {
    std::string id;
    bool metallic = false;
};

struct Bundle {
    // ... the existing members, then:
    std::optional<std::vector<MaterialRecord>> materials;
};

}  // namespace uta::ubundle
```

The section id is the bytes `M`, `A`, `T`, `S`. Its payload is one
`vector<MaterialRecord>` in UTA-0008 § 4.2's encoding. Each element:

| Field | Encoding | Meaning |
|---|---|---|
| `id` | `string` | `umat::materialId`'s result. Opaque here |
| `metallic` | `u8` | `0` or `1`. No other value is defined |

**The minimum encoded size of one element is 5 bytes**: a `u32` length for
an empty `id`, then one `u8`. It joins UTA-0008 § 4.2's minimum-size table.

**Validation, in UTA-0008 § 4.9's manner.** A `metallic` byte other than `0`
or `1` is `MalformedData`, never defaulted. The `id`s are in strictly
ascending bytewise order, which also makes them unique. `write` refuses the
same two with `InvalidArgument`.

**`write` emits `MATS` last: `ROOM`, `NAVG`, `WIRG`, `TEXS`, `MATS`.** It is
appended, as UTA-0052 appended `TEXS`, so UTA-0008 § 4.10's order clause is
extended rather than contradicted.

**`FORMAT_VERSION` becomes `3`.** Nothing else about the framing moves. § 14
carries compatibility.

**`ubundle` does not check that each record has maps.** A section never
reads another's meaning; the baker guarantees the pairing (INV-17).

## 5. Invariants

- **INV-1** — Baking one synthetic map twice, on a `JobSystem` of one worker
  and on one of four, gives byte-identical `ubundle::write` output and one
  name.
  *Test:* `tests/unit/BakeTest.cpp`, *the same map bakes to the same bytes
  on any worker count*.
  *Breaks when:* a stage depends on the order jobs finish in, or emits
  `TEXS` in the iteration order of an unordered container.

- **INV-2** — The bake asks the resolver only for packages in the closure the
  name was computed over.
  *Test:* `tests/unit/BakeTest.cpp` wraps the install's resolver, passes the
  wrapper to `detail::bake`, records every name asked for, and checks each is
  in `detail::closure`'s result for the map.
  The fixture's texture lives in a second package, and its level's actor
  descends from a class in a third, so both routes are exercised.
  *Breaks when:* the bake reaches a package by any route but an import chain
  — a hard-coded lookup, say — so a change to that package leaves the name
  unchanged.

- **INV-3** — The name changes when any one input changes: a byte of the map,
  a byte of a closure package, `bakerVersion()`, or a closure package going
  from absent, or from a file that does not open, to one that does.
  *Test:* `tests/unit/BakeTest.cpp`, one case per input, each through
  `detail::nameOf` or a synthetic install on disk.
  *Breaks when:* a closure entry contributes its name without its digest, or
  an absent package is left out of the string, so installing it renames
  nothing.

- **INV-4** — `detail::nameOf` is the SHA-256 of § 4.4's byte string.
  *Test:* `tests/unit/BakeTest.cpp` assembles the string literally for a
  closure of names `ab` and `c` given out of order, one absent, and compares
  `sha256` of it with `nameOf`. A second case with names `a` and `bc`, same
  digests, must give a different name.
  *Breaks when:* the entries are not sorted, or a separator is dropped so
  that closures `{ab, c}` and `{a, bc}` hash the same string.

- **INV-5** — The golden bake's bundle hashes to the value recorded for the
  current `BAKER_REVISION`.
  *Test:* `tests/unit/BakeGoldenTest.cpp`. It runs on every CI leg, so a
  compiler that bakes different bytes fails it too.
  *Breaks when:* the baker, or a generator the fixture exercises, changes
  what is written while `BAKER_REVISION` stays the same.

- **INV-6** — `sha256` gives FIPS 180-4's example digests, and `Sha256` fed in
  pieces gives the one-shot digest.
  *Test:* `tests/unit/CoreSha256Test.cpp`: `""` gives
  `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`;
  `"abc"` gives
  `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`; the
  56-byte `"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"` gives
  `248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1`.
  Then the same three fed one byte at a time.
  *Breaks when:* padding is wrong where the length field no longer fits in
  the last block, which the 56-byte message is chosen to reach.

- **INV-7** — A material is generated with `MaterialSettings{}` and the
  curated entry for its picture applied, or with `MaterialSettings{}` where
  the library holds none.
  *Test:* `tests/unit/BakeTest.cpp` bakes one fixture texture through
  `detail::bake` twice: with a lookup returning an entry that sets
  `metallic` for the fixture's own `umat::pictureFingerprint` and null for
  any other, then with one returning null for all. Its `MATS` record reads
  `true`, then `false`.
  *Breaks when:* the lookup is asked for the wrong key, its entry is not
  applied, or `MATS` records a value other than the one the material was
  generated with.

- **INV-8** — A texture named by a surface with `PF_MASKED` and by one without
  gives two materials, `<id>` and `<id>#masked`. Named only by unmasked
  surfaces, it gives `<id>` alone.
  *Test:* `tests/unit/BakeTest.cpp`, a fixture `Model` with three surfaces
  over two textures.
  *Breaks when:* the variant follows the texture's own `bMasked` property
  rather than the surface, or tests the wrong bit.

- **INV-9** — A texture carrying a `Format` property gives no material and one
  `SkippedTexture` naming it.
  *Test:* `tests/unit/BakeTest.cpp`, one fixture texture baked twice: without
  a `Format` property it makes a material, and with one it is skipped, the
  skip's reason naming `Format`.
  *Breaks when:* the check is skipped, so a block format storing one byte a
  texel is read as palette indices.

- **INV-10** — A cached bake is returned without baking or writing; `force`
  bakes over it; a file there that `readHeader` refuses is baked over.
  *Test:* `tests/unit/BakeTest.cpp` writes a file holding a valid sixteen-byte
  header and nothing else, and checks its bytes survive a run without `force`
  and are replaced by a run with it. A second file with a bad magic is
  replaced by a run without `force`.
  *Breaks when:* the cache check tests only that the file exists, so a file
  that cannot be a bundle is served for ever.

- **INV-11** — `checkInstall` passes a synthetic install holding `Core`,
  `Engine` and `Botpack`. Each one removed gives one problem naming it; one
  that does not open gives a problem carrying `upkg`'s message.
  *Test:* `tests/unit/BakeInstallTest.cpp`.
  *Breaks when:* the check tests only that directories exist.

- **INV-12** — `wonderland` resolves to `Textures/wonderland.utx` when
  `Sounds/wonderland.uax` is also present, and to the `.uax` when it is alone.
  *Test:* `tests/unit/BakeInstallTest.cpp`, both files synthetic packages
  told apart by an export name.
  *Breaks when:* the directories are searched in another order, or files are
  keyed by extension as well as name.

- **INV-13** — `readLevel` returns the `Model` reference the level holds, and
  the bake builds `ROOM` from that export.
  *Test:* `tests/unit/PackageContentTest.cpp`, through
  `LevelExportWriter::setModel`; and `tests/unit/BakeTest.cpp` with two
  `Model` exports where the level names the smaller.
  *Breaks when:* the reference is still discarded, or the baker picks the
  largest `Model`.

- **INV-14** — A map with no `Level` export, or whose level names no `Model`
  export, is refused with `MalformedData`, and the message names the map.
  *Test:* `tests/unit/BakeTest.cpp`.
  *Breaks when:* the baker writes a bundle with no sections instead.

- **INV-15** — `runCli` prints one JSON object with § 4.8's keys for each
  verdict and for `--check`, and returns § 4.8's exit code. An `over-budget`
  run leaves no file at `path`.
  *Test:* `tests/unit/BakeCliTest.cpp`, over a synthetic install on disk;
  `over-budget` through `detail::runCli` with a budget of one byte.
  *Breaks when:* a path prints a second object, a key is renamed, or a
  refusal exits `0`.

- **INV-16** — The written file is `outDir / (name + ".utab")`, and `name` is
  exactly 64 lower-case hex digits.
  *Test:* `tests/unit/BakeTest.cpp`.
  *Breaks when:* the file is named from anything the map holds, which would
  let a crafted map write outside `outDir`.

- **INV-17** — Every `MATS` id has a `TEXS` map named `<id>:base`, every
  `TEXS` map's name before its colon is a `MATS` id, and `TEXS` holds its
  materials in ascending id, each one's maps in `MapKind` order (§ 4.6).
  *Test:* `tests/unit/BakeTest.cpp`, over INV-8's fixture with its surfaces
  naming its textures in descending id order, plus one texture that is
  skipped.
  *Breaks when:* a skipped variant leaves a record behind, a generated
  material's record is dropped, or `TEXS` follows surface order.

- **INV-18** — `MATS` round-trips through `ubundle::write` and
  `ubundle::read`, and `read` refuses a `metallic` byte of `2`, and two
  records out of order, with `MalformedData`.
  *Test:* `tests/unit/BundleMaterialTest.cpp`.
  *Breaks when:* an unknown byte is read as `true` or `false` rather than
  refused, or order is left to the writer's good behaviour.

## 6. Failure modes

| When | What happens |
|---|---|
| A closure package fails to open | It counts as absent: its name and `0x00` enter the name. Ancestry walks end `PackageMissing`, and its textures are skipped with that reason |
| `readModel` refuses — a version-61 package (UTA-0072) | The bake is refused and names the map |
| `buildRoomMap` refuses — a zone table past `ZONE_CEILING` | The bake is refused |
| The level's box is not valid | Every room is in `rooms.withoutFootprint`. Real maps do this until UTA-0098 settles which box to sample |
| Over budget | `over-budget`, nothing written, the report printed |
| Two runs bake one map at once | Both write the same bytes atomically; the second rename replaces identical content |
| `outDir` cannot be created or written | Refused, with `uta::fs`'s error |
| A crash mid-write | `fs::writeFileAtomically` leaves the old file or none, never half a new one |

## 7. Tests

**Unit, on every CI leg and with no Unreal Tournament present (S7):**
`tests/unit/BakeTest.cpp` for INV-1, INV-2, INV-3, INV-4, INV-7, INV-8,
INV-9, INV-10, INV-13, INV-14, INV-16 and INV-17. `BakeInstallTest.cpp` for
INV-11 and INV-12, `BakeCliTest.cpp` for INV-15, `BakeGoldenTest.cpp` for
INV-5, `CoreSha256Test.cpp` for INV-6 and `BundleMaterialTest.cpp` for
INV-18. `PackageContentTest.cpp` takes INV-13's reader half. Fixtures are synthetic packages built with
`tests/support/UnrealPackageBuilder.h`. The `Model` body encoder
`PackageMalformedTest.cpp` carries moves into that support file for these
fixtures. Each test is seen failing before the code it locks exists.

**Real-asset tier, local only:** a case in `tests/real/` that computes the
name of every map in the install's `Maps`, printing how many have more than
one `Level` export and how many level `Model` references differ from the
largest `Model`. It bakes one stock map twice and compares the bytes.

**Mutation, by hand** (`CLAUDE.md` § Build and test): drop the `PF_MASKED`
test, drop a closure entry's digest, skip the `Format` check, emit `TEXS`
in resolution order, and read a `MATS` `metallic` byte of `2` as `true`. Each must be killed by the invariant that names it.

## 8. Alternatives considered (and rejected)

- **FNV-1a 64, as `umat` uses.** A bake's name is also the key of a player's
  cache. FNV-1a is not collision resistant, so a hostile server could hand
  over a package whose bake takes a legitimate map's name. The player's cache
  would then serve the wrong bake on another server.
- **Naming a bake by what it opened.** The name has to exist before the bake,
  to find a cached one.
- **Reading the search order from the player's `.ini`.** Two players with
  different settings would name one map differently.
- **The largest `Model` as the level's world.** The level names its own
  `Model`, and a guess is a second answer (§ 2 item 4).
- **The baker's binary as its version.** It differs between compilers, so
  ADR-0002's *"the same bundle on any machine"* would not give the same name.
- **Validating a cached bake in full with `ubundle::read`.** The loader
  reads it anyway, and `--force` is the recovery when that fails.
- **The baker choosing its own directory.** § 15.
- **`metallic` as a field on each `CompressedTexture`.** One material's value
  would be copied onto each of its maps, and the copies could disagree.

## 9. Out of scope

- Geometry, and binding each surface to its material — tracked by UTA-0109.
- Lights and actor placements — tracked by UTA-0110.
- Collision — tracked by UTA-0111.
- Baked indirect light — tracked by UTA-0112.
- The recipe format, and applying a recipe — tracked by UTA-0113.
- Which box the room sampler samples — tracked by UTA-0098.
- A supported call for which packages a package needs, to replace this
  item's closure walk and `ut-dump`'s `importedPackages` — tracked by
  UTA-0070.
- Telling apart two packages that share a name — tracked by UTA-0104.
- Moving a procedural texture — tracked by UTA-0105.
- The link-closure test keeping `ubake` out of the runtime targets — tracked
  by UTA-0016, the first item to build one.
- Decoding a texture that carries a `Format` property — deferred; not yet
  queued.
- Which Unreal Tournament versions an install may be — deferred; not yet
  queued.

## 10. What checks this

| Rule | What catches a breach |
|---|---|
| INV-1, INV-2, INV-3, INV-4, INV-7, INV-8, INV-9, INV-10, INV-14, INV-16, INV-17 | `tests/unit/BakeTest.cpp` |
| INV-5 | `tests/unit/BakeGoldenTest.cpp` |
| INV-6 | `tests/unit/CoreSha256Test.cpp` |
| INV-11, INV-12 | `tests/unit/BakeInstallTest.cpp` |
| INV-13 | `tests/unit/PackageContentTest.cpp` and `tests/unit/BakeTest.cpp` |
| INV-15 | `tests/unit/BakeCliTest.cpp` |
| INV-18 | `tests/unit/BundleMaterialTest.cpp` |
| `BAKER_REVISION` covering the generators | **Partial:** INV-5 catches what its fixture exercises; a change reached only by real content passes |
| `PF_MASKED` meaning masked in UT99 | **nothing** — INV-8 tests the bit the code uses, and the value rests on the cited source |
| The search order matching UT99 | **Partial:** INV-12 locks the order; that it is UT99's rests on one reading of `Default.ini` |
| A map holding one `Level` export | **Partial:** the real-asset tier prints how many do not; no CI leg runs it |

## 11. Cross-doc impact

- `docs/specs/UTA-0057-level-tail-and-reachspecs.md` and the comment in
  `src/upkg/Level.cpp` — the `Model` reference is returned now, not
  consumed. Recorded when built.
- `docs/specs/UTA-0002-core-foundations.md` — `core` gains `Sha256`. A
  pointer to this spec.
- `docs/specs/UTA-0008-bundle-container-and-origin.md` § 10, UTA-0010 § 10 and
  UTA-0052 § 10 — each has a row this item now catches. Updated when built.
- `docs/specs/UTA-0008-bundle-container-and-origin.md` — in the same change
  as the code, as UTA-0052 did for `TEXS`: § 4.10's API and section order
  gain `MATS`, § 4.2's minimum-size table gains `MaterialRecord`, and INV-4
  is annotated in place with version `3`.
- `docs/specs/UTA-0008-bundle-container-and-origin.md` § 4.4 — its example,
  that a level with no navigation points carries no `NAVG`, becomes an empty
  one, matching the same paragraph's own distinction. Corrected in the same
  change as the code.
- `CHANGELOG.md` — an `### Added` entry for `ut-bake`, and a `### Changed`
  entry for the bundle format's version `3`, as
  `docs/standards/versioning-overrides.md` § Override requires of a breaking
  change.
- `README.md` — how to run `ut-bake`, once it exists.
- `docs/design.md` — none. This item implements rule 16 and § Content
  addressing.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0011-map-baker-loop-log.md`.

## 13. Resource cost

- Two new build targets, `uta_ubake` and `ut-bake`. No new external
  dependency: SHA-256 is written here.
- An `Install` keeps every package it opened for its own lifetime, which is
  bounded by the install.
- Every run hashes the map and its closure, a cached one included. Not
  measured; the real-asset case prints it.

## 14. Migration / compatibility

**No `.utab` exists that version `3` orphans.** `0.1.0` has not been cut,
which is UTA-0052 § 4.7's argument for version `2`, unchanged. The reader
still accepts one version only (UTA-0008 INV-4), so a stray version-`2` file
is refused rather than misread, and § 4.7's cache check bakes over it.

## 15. Open questions

- **Where bakes live.** `docs/design.md` rule 15 says under `content/`;
  `fs::cacheDirectory` says the per-user cache. This item takes the directory
  from its caller, so the question lands on UTA-0016, which picks it.

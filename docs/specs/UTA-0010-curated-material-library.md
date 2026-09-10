# UTA-0010 — `umat`: the curated material library

**Status:** accepted (2026-09-10).
**Kind:** implement.
**Source:** ROADMAP UTA-0010 (design-2026-09-03; scope settled with the
user 2026-09-05, 2026-09-09 and 2026-09-10).

**Pairs with:** UTA-0009 (generates the material this library adjusts),
UTA-0104 (the map standard), UTA-0106 (locally supplied replacement
images).
**Blocker for:** UTA-0011 (`ubake` looks every texture up here, and folds
the library into its baker version).

**Layman:** A list of hand-set materials — this texture is metal, that one
glows — found by the picture itself, so it reaches every copy of that
picture in every map.

## 1. Goal

After this ships, `umat` carries a **curated library**: entries that
adjust a generated material's settings, each keyed by a **fingerprint of
a texture's picture**. The baker computes a texture's fingerprint, asks
the library, and applies any entry before generating the material. The
library is compiled into the baker, and a digest of it enters the baker
version, so changing an entry changes every bundle name it could affect.
The first version ships a small seed: metal and glowing textures chosen
by rules over what the textures say about themselves.

## 2. Problem

1. **A generated material is never metal and never glows by default.**
   `umat::MaterialSettings` in `src/umat/Generate.h` defaults `metallic`
   and `emissive` to false. UTA-0009's § 3 decision 3 says: *"The curated
   library (UTA-0010) and a map's recipe mark real metal."*
2. **A texture's name does not identify its picture.** Measured
   2026-09-10 over the reference install in a scratch run: many textures
   embedded in maps are exact pixel-and-palette copies of a packaged
   texture, about half of them renamed, spread over a large share of the
   maps. A copy embedded in a map lives in the map's own package, so a key
   of package and name reaches none of them. § 7's census case prints the
   figures.
3. **The library is a bake input.** `docs/design.md` § What every part does
   the same way, its Content addressing bullet: every other bake input
   must be covered by the map, the recipe or the baker version, *"which is
   why `umat`'s curated library ships with the baker and is versioned with
   it"*.
4. **The roadmap body keys the library "by texture name".** That predates
   the measurement in item 2 and is replaced by § 3 decision 1.

## 3. Scope decisions (agreed with the user)

1. **An entry matches the picture itself.** *Decided by the user,
   2026-09-10*, offered against *package and name* and *picture first,
   name as fallback*. An entry reaches every copy of its picture, renamed
   or not, and never a different picture that shares a name.

2. **The first version ships a small seed, grown in play.** *Decided by
   the user, 2026-09-10*, offered against *empty* and *a big pass before
   release*. The seed comes from what textures say about themselves,
   chosen by machine rather than by eye, with the glow rule checked
   against the image data (§ 4.7). Entries
   are added when a surface is found wrong in play.

3. **The library holds settings, not art, in this version.** *User,
   2026-09-10*: replacement images come after the first version; locally
   supplied ones are UTA-0106. *User, 2026-09-09*, recorded on this item:
   art shipped IN the library is this item's own later extension, and
   holds a replacement only where a licence permitting redistribution can
   be pointed at.

4. **Entries are C++ data compiled into the baker.** A data file would
   need a parser — `src` has none — or a new dependency, which ADR-0007's
   questions would have to admit. Compiled data needs neither, and ships
   with the baker by construction. *Chosen here, not by the user*: it
   changes nothing the user sees.

## 4. Design

### 4.1 The files

| File | Holds |
|---|---|
| `src/umat/Fingerprint.h/.cpp` | `pictureFingerprint` (§ 4.2) |
| `src/umat/Library.h/.cpp` | the entry types, lookup, `applied` and the digest (§ 4.3–4.6) |
| `src/umat/CuratedMaterials.cpp` | the table itself, and nothing else |

The table has a file of its own so that adding an entry touches no logic.
Nothing here adds a link: the fingerprint reads `upkg` types, which
`uta_umat` already links (UTA-0009's INV-12).

### 4.2 The picture fingerprint

```cpp
namespace uta::umat {
/// Nothing when `base` is not one index byte per texel.
[[nodiscard]] std::optional<std::uint64_t> pictureFingerprint(
    const upkg::Mip& base, const upkg::Palette& palette) noexcept;
}
```

FNV-1a 64 — offset basis `0xcbf29ce484222325`, prime `0x100000001b3` —
over these bytes, in this order:

1. `base.width`, then `base.height`, each as 4 bytes little-endian;
2. every index byte of `base`, row by row;
3. every palette entry's `r`, `g` and `b`, in entry order.

**These are the bytes `umat::resolve` reads, plus any palette entries no
index names.** Palette alpha is left out because `resolve` ignores it
(`src/umat/Resolve.h`). So two textures sharing a fingerprint resolve to
the same picture, collisions aside — INV-9 grades those. Two copies that
differ only in an unused palette entry do not match, which the user's
decision — pixels and palette — accepts.

**The caller passes only a texture carrying no `Format` property.** The
byte-count check is a backstop: a block format storing one byte per texel
would pass it. § 7's census prints how many textures carry a `Format`
property and how many of those pass the check. `core` has no hash
function, so this one is `umat`'s. The source gives the two constants in
decimal, as 14695981039346656037 and 1099511628211.
Source: <https://www.isthe.com/chongo/tech/comp/fnv/index.html>.

### 4.3 An entry, and the table

```cpp
namespace uta::umat {
/// Settings an entry replaces. An empty field keeps the value it had.
struct CuratedOverride {
    std::optional<bool> metallic;
    std::optional<std::uint8_t> baseRoughness;
    std::optional<bool> emissive;
    std::optional<std::uint8_t> emissiveThreshold;
};

/// Why an entry exists (§ 4.7). Audit only: it reaches no bundle.
enum class CurationSource : std::uint8_t { MetalSound, GroupName, Play };

struct CuratedEntry {
    std::uint64_t fingerprint;
    /// The picture's usual `<package>.<path>`, for a human. Audit only.
    std::string_view note;
    CurationSource source;
    CuratedOverride settings;
};

/// Sorted by fingerprint ascending, no fingerprint twice (INV-4).
[[nodiscard]] std::span<const CuratedEntry> curatedLibrary() noexcept;
}
```

`src/umat/CuratedMaterials.cpp` defines the table as a `constexpr
std::array<CuratedEntry, N>` and `static_assert`s that it is sorted and
unique. `requestedUpscale` has no field in this version. UTA-0052's § 4.5
gives the library its own figure "for the materials it ships", which
arrives with library art (§ 9). A recipe's requested factor sets
`MaterialSettings::requestedUpscale` directly, not through `applied`.

### 4.4 Lookup and applying an entry

```cpp
/// The entry for this fingerprint, or null. A binary search.
[[nodiscard]] const CuratedOverride* curated(std::uint64_t fingerprint) noexcept;

/// `settings` with every field `entry` sets replaced, and no other.
[[nodiscard]] MaterialSettings applied(MaterialSettings settings,
                                       const CuratedOverride& entry) noexcept;
```

An entry applies to a texture's opaque and masked variants alike. It keys
on the ORIGINAL texture's picture, so it still applies when UTA-0106
replaces that picture.

### 4.5 Precedence — the order UTA-0011 binds to

**Generated defaults, then the library, then the recipe.** The baker
starts from `MaterialSettings{}`, applies the library's entry, then the
map recipe's own material assignment. The recipe's assignment is applied
the way an entry is, through `applied`: only the fields it sets replace
anything, so a recipe setting only `metallic` keeps a library `emissive`.
Its requested upscale factor is the exception, set directly (§ 4.3).
The recipe wins because the map's
author knows the map — the reason `docs/design.md` gives for a recipe's
class override beating the global list.

### 4.6 The digest — the baker version's share

```cpp
/// Changes whenever an entry's fingerprint or any override changes.
[[nodiscard]] std::uint64_t libraryDigest() noexcept;
namespace detail {
[[nodiscard]] std::uint64_t digestOf(std::span<const CuratedEntry> entries) noexcept;
}
```

FNV-1a 64, as in § 4.2, over each entry in table order: its fingerprint as
8 bytes little-endian, then each override field in declaration order as a
presence byte (0 or 1) followed, when present, by its value as one byte.
**`note` and `source` are left out**: neither changes a pixel, so neither
may invalidate a cached bake. `libraryDigest()` is `digestOf` over
`curatedLibrary()`. **UTA-0011 folds it into the baker version**, which is
how this library is "versioned with" the baker.

### 4.7 The seed

The seed's population is every export whose class is exactly `Texture` —
not the procedural classes `upkg::isModelledTextureClass` also accepts —
in the reference install's `Textures/*.utx`, carrying no `Format`
property, whose base level has a fingerprint. Copies
collapse to one entry per fingerprint, and a picture qualifies when any
of its copies does.

- **Metal (`CurationSource::MetalSound`)**: the texture's `FootstepSound`
  or `HitSound` property names a sound whose object name, lower-cased,
  contains `stepmetal`, `hitmetal`, `fs_metal`, `metalstep` or `metwalk`.
  Sets `metallic`.
- **Metal (`CurationSource::GroupName`)**: the texture's own group — the
  export its `outer` names — is named `metal`, `metals` or `steel`,
  lower-cased. Sets `metallic`.
- **Glow (`CurationSource::GroupName`)**: the group is named `light`,
  `lights`, `lamp`, `lamps`, `lightbox`, `baselight`, `carlights`,
  `gothiclight`, `glowpanels`, `lava`, `neon`, `screen`, `screens`,
  `panelscreen` or `monitors`, lower-cased — **and** the image check
  passes. Sets `emissive`.
- **The image check**: at least `SEED_BRIGHT_SHARE`, one texel in a
  hundred, of the base level resolved as the opaque variant has a Rec. 709
  luma at or above
  `MaterialSettings{}.emissiveThreshold`, computed as UTA-0009's
  `heightOf` computes it. A group named like a light whose picture has no
  bright texels would give an all-black emissive map.

**The metal rules carry no image check.** A picture's pixels do not show
whether it is metal — UTA-0009's § 3 decision 3 declined to guess metal
texel by texel — so the check belongs to the glow rule alone.

A picture meeting several rules, through one copy or several, gets every
setting they give. Its source is `MetalSound` if that rule met, else
`GroupName`. Everything else stays with the generated defaults.

**A `Play` entry is added by hand, and replaces any seed entry at its
fingerprint** — INV-4 allows one entry per fingerprint.

### 4.8 As built (2026-09-10)

Recorded after the build. None of it changes a contract above.

- `CuratedOverride` also declares a defaulted `==`. The census and the
  tests compare with it.
- FNV-1a is `detail::Fnv1a` in `src/umat/Fingerprint.h`, shared by the
  fingerprint and the digest.
- The table's rows name one of three settings, `METAL`, `GLOW` and
  `METAL_GLOW`, defined beside it in `src/umat/CuratedMaterials.cpp`.
- § 4.7's rules and `SEED_BRIGHT_SHARE` live in the census case *the
  curated seed is what its rules derive over the install*, in
  `tests/real/RealInstallTest.cpp`. It prints each missing seed entry as
  a row the table takes as it stands.
- An entry's `note` names its picture's first copy in sorted file order.
  Some install files are named with a Windows path, `Textures\X.utx`,
  and that backslash reaches the note.
- INV-9's census covers `Textures/*.utx` and `Maps/*.unr`. Its second
  hash is `std::hash<std::string>` over the fingerprint's input, written
  out again.
- § 4.2's `Format` backstop: the census prints how many textures carry a
  `Format` property and how many of those are one byte a texel.

## 5. Invariants

- **INV-1** — `pictureFingerprint` on a fixed synthetic level and palette
  produces a fixed golden value.
  *Test:* `tests/unit/MaterialLibraryTest.cpp`, the value literal in the
  source, compared on every CI leg. No arrow: the surface does not exist
  yet.
  *Breaks when:* the byte order, the width or height encoding, or the
  hash constants change — any of which silently detaches every entry from
  its picture.

- **INV-2** — the fingerprint changes when one index, one palette colour
  channel, or the dimensions change, and does not change when a palette
  alpha changes.
  *Test:* `tests/unit/MaterialLibraryTest.cpp`: a 4×2 level against the
  same bytes as 2×4, one index changed, one entry's `g` changed, one
  entry's `a` changed. No arrow: the surface does not exist yet.
  *Breaks when:* the dimensions are left out, so a 4×2 and a 2×4 picture
  collide, or palette alpha is included, so copies differing only in a
  field `resolve` ignores stop matching.

- **INV-3** — a level that is not one index byte per texel has no
  fingerprint.
  *Test:* `tests/unit/MaterialLibraryTest.cpp`, a 2×2 level holding 16
  bytes. No arrow: the surface does not exist yet.
  *Breaks when:* a compressed or high-colour level is hashed as if
  palettised, giving it an entry nothing could have meant for it.

- **INV-4** — `curatedLibrary()` is sorted by fingerprint ascending with
  no fingerprint twice.
  *Test:* the `static_assert` in `src/umat/CuratedMaterials.cpp`, and a
  check of the same in `tests/unit/MaterialLibraryTest.cpp`. No arrow:
  the surface does not exist yet.
  *Breaks when:* an entry is added out of order, which `curated`'s binary
  search then silently misses.

- **INV-5** — `curated` returns the entry for every fingerprint in the
  table, and null for a fingerprint one above or one below each.
  *Test:* `tests/unit/MaterialLibraryTest.cpp`, over every entry. No
  arrow: the surface does not exist yet.
  *Breaks when:* the search is off by one at either end.

- **INV-6** — `applied` replaces exactly the fields an entry sets.
  *Test:* `tests/unit/MaterialLibraryTest.cpp`: an override per field
  setting it alone, and one setting all four, each applied to settings that
  differ from it in every field. No arrow: the surface does not exist yet.
  *Breaks when:* an empty field resets its setting to the default instead
  of keeping it.

- **INV-7** — `detail::digestOf` changes when a fingerprint or any
  override field changes, including a field going from empty to present,
  and does not change when `note` or `source` changes.
  *Test:* `tests/unit/MaterialLibraryTest.cpp`, a test-local table and
  one mutated copy per field. No arrow: the surface does not exist yet.
  *Breaks when:* a field is left out of the digest, so an entry change
  reaches bundles without changing their names — the lie `docs/design.md`'s
  Content addressing bullet names.

- **INV-8** — over the reference install, § 4.7's rules produce exactly
  the table's `MetalSound` and `GroupName` entries — fingerprint, settings
  and source — once every fingerprint a `Play` entry holds is removed from
  both sides.
  *Test:* a census case in `tests/real/RealInstallTest.cpp` that
  re-derives the seed and prints every missing or extra entry as a table
  row. Real-asset tier only. No arrow: the case does not exist yet.
  *Breaks when:* a seed entry is edited by hand instead of replaced by a
  `Play` entry, or a rule changes without the table following — either way the seed stops being what § 4.7 says
  it is.

- **INV-9** — no two different pictures in the reference install share a
  fingerprint.
  *Test:* the same census pass, keeping a second, independent hash of each
  picture's bytes beside its fingerprint and failing where one fingerprint
  carries two. Real-asset tier only. No arrow: the case does not exist
  yet.
  *Breaks when:* the fingerprint's inputs are narrowed until distinct
  pictures meet, and an entry lands on a picture it was never meant for.

## 6. Failure modes

- **A texture with no fingerprint** — a non-palettised level. It gets no
  entry and the generated defaults stand.
- **An install that differs from the reference one.** An entry whose
  picture is absent matches nothing and costs nothing. A picture the seed
  never saw gets the generated defaults.
- **A fingerprint collision.** INV-9 catches one within the reference
  install. Outside it, a collision applies one picture's settings to
  another, and nothing detects it.
- **A seed rule that is wrong for a picture** — a metal-sounding texture
  that is painted wood. A `Play` entry with the right settings replaces
  the seed entry, or the recipe overrides it.

## 7. Tests

| File | Locks |
|---|---|
| `tests/unit/MaterialLibraryTest.cpp` | INV-1, INV-2, INV-3, INV-4, INV-5, INV-6, INV-7 |
| `src/umat/CuratedMaterials.cpp` | INV-4, at compile time |
| `tests/real/RealInstallTest.cpp` | INV-8, INV-9, real-asset tier |

Each case is seen to fail against pre-change code; for most, that is a
compile failure. Every guard is mutated by hand, per `CLAUDE.md` § Build
and test.

**The census case also prints § 2 item 2's figures** — how many textures
embedded in maps copy a packaged picture, how many were renamed, and how
many maps hold one — so the measurement behind § 3 decision 1 is an
output of the tree, not a scratch run. It prints § 4.2's `Format` figures
too.

## 8. Alternatives considered (and rejected)

- **Key by package and name** — rejected by the user (§ 3 decision 1): it
  reaches no copy embedded in a map.
- **Picture first, name as fallback** — rejected by the user: a name match
  can land on another creator's different picture.
- **Key by UTA-0104's package fingerprint** — misses every copy embedded
  in a map, for the same reason as a name.
- **A data file read at bake time** — needs a parser or a new dependency,
  and becomes a second format to version (§ 3 decision 4).
- **SHA-256** — `core` has no hash library, and a 64-bit fingerprint is
  graded by INV-9 over the pictures that matter.
- **An empty library, or a big pass before release** — rejected by the
  user (§ 3 decision 2).

## 9. Out of scope

- Locally supplied replacement images — tracked by UTA-0106.
- Licensed art shipped in the library, and the upscale figure it brings —
  a later extension of this item; not yet queued.
- The recipe format and its material assignments — deferred; not yet
  queued.
- The baker version itself, and folding the digest into it — tracked by
  UTA-0011.
- Per-surface flags — tracked by UTA-0104.
- The meaning of the `TextureMaterial` byte some textures carry —
  deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2, INV-3 | `tests/unit/MaterialLibraryTest.cpp` |
| INV-4 | `src/umat/CuratedMaterials.cpp`'s `static_assert`, and `tests/unit/MaterialLibraryTest.cpp` |
| INV-5, INV-6, INV-7 | `tests/unit/MaterialLibraryTest.cpp` |
| INV-8, INV-9 | `Partial:` `tests/real/RealInstallTest.cpp` — local-only by design (S7), so no CI leg runs it |
| UTA-0011 folds the digest into the baker version | **nothing yet** — UTA-0011's own contract |
| A seed rule suits a picture | **nothing** — the rules are heuristics, and `Play` entries correct them |

## 11. Cross-doc impact

- ROADMAP UTA-0010 — its body's "keyed by texture name" is replaced by
  § 3 decision 1, already recorded on the item.
- ROADMAP UTA-0011 — gains the obligation to fold `libraryDigest()` into
  the baker version, § 4.5's order, and § 4.2's `Format` rule.
- `docs/design.md` § The parts — the `umat` row says the library is
  resolved first and generation runs otherwise, and that changing it is a
  baker version change. Under this spec an entry adjusts the settings
  generation runs with, and only a change the digest covers is a baker
  version change. The row is reworded to say so.
- `docs/specs/UTA-0009-material-from-texture.md` § 4.6 — says UTA-0010's
  library writes ids in `materialId`'s form. The library is keyed by
  picture fingerprint, and only an entry's audit-only `note` uses that
  form. The sentence is amended to say so.
- `CHANGELOG.md` — an Added entry when this ships.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0010-curated-material-library-loop-log.md`.

## 13. Resource cost

No new dependency and no new link. The table is compiled data, and a
lookup is a binary search over it.

## 15. Open questions

- **`SEED_BRIGHT_SHARE`.** One texel in a hundred is a starting value.
- **Whether the seed should also read `TextureMaterial`**, once what its
  values mean is measured.

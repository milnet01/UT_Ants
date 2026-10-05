<!-- ants-spec-format: 1 -->
# UTA-0113 — the recipe format, read and write

**Status:** built 2026-10-05; § 14 answered by the user 2026-10-03. The build
settled four details, recorded in place: § 4.1's own fields, § 4.2's version
refusal code, § 4.3's named file and shipped directory.
**Kind:** implement.
**Source:** ROADMAP UTA-0113 (user request 2026-09-10, split from UTA-0011).

**Blocker for:** UTA-0181, UTA-0106.

**Layman:** A small text file, written by people rather than by the game,
that holds our own changes to somebody else's map: which surfaces are
metal or glow, and a friendly name. Players share this file instead of the
map's own content.

## 1. Goal

A map can carry a recipe: a hand-editable text file of our own changes to
it. The baker reads it and applies its material assignments after the
curated library, and the recipe's bake-relevant contents enter the
bundle's name, so two players with different recipes never share one name
for two different worlds.

## 2. Problem

1. **Every bake has no recipe.** `docs/specs/UTA-0011-map-baker.md` § 4.4
   item 3 hashes the one byte `0x00`, "meaning no recipe", and says
   "UTA-0113 defines what follows a `0x01`".
2. **The precedence UTA-0010 fixed has no third step.**
   `docs/specs/UTA-0010-curated-material-library.md` § 4.5 orders
   *"generated defaults, then the library, then the recipe"*, applied
   through `umat::applied`; with no recipe the baker stops after the library.
   A material the library gets wrong can only be fixed by changing the
   library, which renames every bake (§ 4.6 folds `libraryDigest()` into
   the baker version).
3. **Replacement textures have nowhere to enter the name.** The user decided
   (2026-09-05 and 2026-09-09, UTA-0009 § 3 decision 4) that a replacement
   is "referenced by the recipe and supplied locally", and `docs/design.md`'s
   Content addressing bullet requires every bake input to be covered by the
   map, the recipe or the baker version. UTA-0106 and UTA-0181 wait on this.
4. **The recipe is a breaking surface.** `docs/standards/versioning-overrides.md`
   names it: *"Recipes are authored by hand and by other people; a recipe
   that stops loading is their work broken."* So its first version must be
   one later versions extend rather than replace.

## 3. Scope decisions (agreed with the user)

- **A recipe is what travels instead of Epic's content** — `ADR-0003`.
- **A replacement image is referenced by the recipe and supplied locally**
  — the user, 2026-09-05 and 2026-09-09 (UTA-0009 § 3 decision 4).
- **A recipe expresses defaults; the server decides** — `docs/design.md`
  rule 14. Nothing in version 1 sets a rule.
- **§ 14's three questions** — the user took the recommended answer to
  each, 2026-10-03, so the design below stands as written.

## 4. Design

### 4.1 Version 1's contents

Version 1 holds only what the baker reads, plus the friendly name every
later part will want. ADR-0003 also lists fog volumes, light-shaft
placement and bot hints, and `docs/design.md` lists rule defaults and class
overrides; each arrives in a later version (§ 9).

```cpp
namespace uta::urecipe {

inline constexpr std::uint32_t RECIPE_VERSION = 1;

/// One texture's assignment: UTA-0010's override fields, any subset, and
/// the requested upscale factor UTA-0010 § 4.5 sets directly. In § 4.2's
/// key order.
struct MaterialAssignment {
    std::string texture;                     ///< `<package>.<path>`, folded, no `#masked`
    std::optional<bool> metallic;
    std::optional<bool> emissive;
    std::optional<std::uint8_t> baseRoughness;
    std::optional<std::uint8_t> emissiveThreshold;
    std::optional<std::uint8_t> parallaxDepth;
    std::optional<std::uint32_t> upscale;    ///< 1, 2 or 4
};

struct Recipe {
    std::string map;                          ///< the map file's folded stem it is for
    std::optional<std::array<std::byte, 32>> mapDigest; ///< when set, the SHA-256 the map must have
    std::string friendlyName;                 ///< runtime only; never hashed
    std::vector<MaterialAssignment> materials; ///< ascending by `texture`, each once
};

[[nodiscard]] Result<Recipe> parse(std::string_view text);
[[nodiscard]] std::string write(const Recipe& recipe);

/// SHA-256 of § 4.4's canonical bytes: the bake-relevant fields only.
[[nodiscard]] std::array<std::byte, 32> bakeDigest(const Recipe& recipe);

}  // namespace uta::urecipe
```

`texture` names a texture the way `umat::materialId` does, without its
masked suffix: a recipe assigns to a texture, and both of its variants
take the assignment.

**`urecipe` links `uta_core` and nothing else**, asserted in its
`CMakeLists.txt`. `docs/design.md` rule 3 makes it a vocabulary the runtime
reads, and `umat` links the package reader, so the assignment mirrors
`umat::CuratedOverride`'s five fields rather than holding one; `ubake`
converts it.

### 4.2 The text format

UTF-8, lines ending `LF` or `CR LF`. A `#` starts a comment to the end of
its line, outside a quoted value. Blank lines are ignored. The first
non-blank line is the header; then `key = value` lines, grouped by
`[section]` headings, in the shape Unreal Tournament's own `.ini` files
use, which its players already edit (§ 14 question 1).

```ini
ut-ants recipe 1

[map]
file = DM-Deck16][
sha256 = 2f8a...e1          # optional: refuse the recipe for a different file
name = "Deck 16, the original"

[material uttech1.wall.bmdirtyt]
metallic = false
emissive = true
emissive-threshold = 200

[material uttech1.floor.bmfloor4]
upscale = 2
```

- **Header:** `ut-ants recipe <version>`. A version above `RECIPE_VERSION`
  is refused with `UnsupportedVersion`, naming the line and both versions, so
  an old game says it is old rather than misread a newer recipe. A UTF-8
  byte-order mark before it is skipped, since Windows editors write one.
- **`[map]`:** `file` required, `sha256` and `name` optional. A quoted
  value may hold `#` and `=`; `\"` and `\\` are its only escapes.
- **`[material <texture>]`:** `<texture>` is `<package>.<path>`, with no
  space and no `#`, and is lower-cased as `file` is. The keys `metallic`, `emissive` (`true` or
  `false`), `base-roughness`, `emissive-threshold`, `parallax-depth` (0 to
  255) and `upscale` (`1`, `2` or `4`), each at most once.
- **Strict.** An unknown section, an unknown key, a repeated key, a repeated
  material, a value out of range or a malformed line is refused with
  `MalformedData` naming the line number. A hand-written file that silently
  drops a misspelt key is worse than one that refuses: the author sees
  nothing change and does not know why.

`write` emits the canonical form: the header, `[map]`, then each material
in ascending `texture` order, keys in the order above, values unquoted
except `name`, and `file` where it holds a `#` or a quote. `parse(write(r))`
equals `r`.

### 4.3 Finding a map's recipe (§ 14 question 2)

The bake takes at most one recipe. In order, the first that exists:

1. `ut-bake --recipe <file>`, when given.
2. The player's own: `<data directory>/recipes/<folded map stem>.recipe`,
   a new `core::dataDirectory()` beside `configDirectory()` —
   `$XDG_DATA_HOME`, else `~/.local/share`, under `ut-ants`; on Windows
   `%APPDATA%\UT_Ants\data`.
3. The shipped one: `recipes/<folded map stem>.recipe` in this repository,
   installed beside the game.

**A `--recipe` file that does not exist is refused**, not passed over: the
player named it, and a bake that ignored it would change nothing in silence
(§ 4.2's reason). Steps 2 and 3 are skipped where the file is absent.

**Step 3's directory is the repository's own `recipes/`**, compiled into
`urecipe::shippedDirectory()`, because no install step exists yet;
packaging moves it beside the game.

None is "no recipe". A recipe whose `file` is not the map's folded stem, or
whose `sha256` is set and differs from the map's, is refused, so a recipe
never applies to a map it was not written for. A server sending its
recipe to a joining player is a later item (§ 9); until then a player's
own recipe can rename their bake away from a server's, and that is stated
in § 6.

### 4.4 The bake name

UTA-0011 § 4.4 item 3 becomes: the byte `0x00` when the bake has no
recipe, else the byte `0x01` followed by the 32 bytes of
`bakeDigest(recipe)`. A map baked with no recipe keeps the name it has
today.

`bakeDigest` is the SHA-256 of:

1. The ASCII text `uta-recipe-bake-1`, then `LF`.
2. For each material in ascending `texture` order: `texture`, `LF`, then each
   of the six fields in § 4.2's key order as a presence byte (`0` or `1`)
   followed, when present, by its value as one byte.

`map`, `mapDigest` and `friendlyName` are left out: the map and its digest
are already item 4 of the name, and a friendly name changes no pixel, so
renaming a map must not rename its bake. This is UTA-0010 § 4.6's rule for
`note` and `source`.

### 4.5 Applying it

`ubake` applies each assignment through `umat::applied` after the library's
entry, as UTA-0010 § 4.5 orders, to both of the texture's variants, and sets
`requestedUpscale` from `upscale` when present. An assignment naming a
texture the map never uses is reported on the bake report's
`recipeUnused` list and changes nothing; it is not refused, because a
recipe may be shared between versions of a map.

## 5. Invariants

- **INV-1** — `parse(write(r))` equals `r` for every field, and `write` of a
  parsed file is § 4.2's canonical form.
  *Test:* `tests/unit/RecipeFormatTest.cpp`, new.
  *Breaks when:* a field is not written, or written in another order.

- **INV-2** — `parse` refuses, naming the line, each of: no header, a
  version above `RECIPE_VERSION`, an unknown section, an unknown key, a
  repeated key, a repeated material, a value out of range, a line with no
  `=`, an unclosed quote, and a `[map]` with no `file`.
  *Test:* `tests/unit/RecipeFormatTest.cpp`, one case per refusal.
  *Breaks when:* any of them is accepted, or refused without its line.

- **INV-3** — `bakeDigest` covers the bake-relevant fields only: two recipes
  differing only in `name`, or only in `sha256`, have one digest; two
  differing in any material field, or in a material's presence, do not.
  *Test:* `tests/unit/RecipeFormatTest.cpp`.
  *Breaks when:* the friendly name is hashed, or a field is left out.

- **INV-4** — the bake name: with no recipe it is unchanged from UTA-0011's;
  with a recipe it differs, and two recipes of one digest give one name.
  *Test:* `tests/unit/BakeTest.cpp`, extended, beside its name cases.
  *Breaks when:* item 3 is not `0x00` with no recipe, or a recipe changes
  nothing.

- **INV-5** — a recipe's assignment is applied after the library, field by
  field: a recipe setting only `metallic` keeps a library `emissive`, and
  `upscale` sets the factor directly.
  *Test:* `tests/unit/BakeTest.cpp`, extended.
  *Breaks when:* the recipe is applied before the library, or replaces the
  whole override.

- **INV-6** — the lookup: `--recipe` beats the player's own, which beats the
  shipped one; a recipe for another map stem, or with a `sha256` the map
  does not have, or a `--recipe` file that does not exist, is refused.
  *Test:* `tests/unit/RecipeLookupTest.cpp`, new, over a scratch data
  directory.
  *Breaks when:* the order differs, or a recipe applies to the wrong map.

**The trust boundary.** A recipe comes from other people and is read from
disk. `parse` reads it as text only: it opens no path a recipe names, runs
nothing, and bounds every value (§ 4.2). A recipe cannot reach any field
§ 4.1 does not list.

## 6. Failure modes

- **A recipe the player edited renames their bake.** Expected: it is a
  different world. Against a server, the names disagree until the server
  sends its recipe (§ 9).
- **A misspelt key refuses the whole recipe.** Deliberate (§ 4.2); the bake
  stops and says which line, rather than baking without the author's change.
- **A newer recipe on an older game** is refused naming both versions.
- **A recipe for a map the player does not have** is never read: lookup is
  by the map being baked.
- **A texture the map does not use** is reported, not refused (§ 4.5).
- **A recipe added, edited or removed after a bake**: the launcher records
  which recipe file a bake took and its stamp, and offers the bake again
  when either moves (UTA-0287).

## 7. Tests

Unit tier, every CI leg, no Unreal Tournament needed:

- INV-1, INV-2, INV-3 — `tests/unit/RecipeFormatTest.cpp`, new.
- INV-4 — `tests/unit/BakeTest.cpp`, extended.
- INV-5 — `tests/unit/BakeTest.cpp`, extended.
- INV-6 — `tests/unit/RecipeLookupTest.cpp`, new.
- `--recipe` and the report's `recipe` and `recipeUnused` —
  `tests/unit/BakeCliTest.cpp`, extended.

Each is seen failing before the code it locks exists.

## 8. Alternatives considered (and rejected)

- **JSON.** Familiar to programmers, and no reader exists in the tree
  either; but it has no comments, and the people writing recipes are map
  players, who know `.ini` files from Unreal Tournament itself.
- **TOML.** A good fit, but it needs a fetched parser, a dependency
  `docs/standards/dependency-acquisition.md` would route, for a format this
  small.
- **A binary format.** Not hand-editable, which the breaking-surface entry
  says recipes are.
- **Lenient parsing that skips unknown keys.** Forward compatible, but a
  misspelt key then does nothing in silence (§ 4.2).

## 9. Out of scope

- Fog volumes, light-shaft placement, bot hints, rule defaults and class
  overrides — deferred; not yet queued. Each is a later `RECIPE_VERSION`.
- Per-map atmosphere such as the haze scale — deferred; not yet queued
  (§ 14 question 3).
- Replacement textures — tracked by UTA-0106 and UTA-0181.
- A server sending its recipe to joining players — deferred; not yet queued.
- An editor for recipes — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2, INV-3 | `tests/unit/RecipeFormatTest.cpp` |
| INV-4 | `tests/unit/BakeTest.cpp` |
| INV-5 | `tests/unit/BakeTest.cpp` |
| INV-6 | `tests/unit/RecipeLookupTest.cpp` |
| A recipe file that worked still loading after an upgrade | **nothing** automated: the breaking-surface rule in `docs/standards/versioning-overrides.md`, applied by hand at each `RECIPE_VERSION` change |

## 11. Cross-doc impact

- `docs/specs/UTA-0011-map-baker.md` § 4.4 item 3 — the recipe's bytes.
- `docs/specs/UTA-0010-curated-material-library.md` § 4.5 — its third step
  becomes live.
- `docs/design.md` § The parts — `urecipe`'s version-1 subset.
- `README.md` — where a player puts a recipe.
- `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0113-recipe-format-loop-log.md`.

## 13. Migration / compatibility

New format, version 1. Every existing bake keeps its name: with no recipe,
§ 4.4's item 3 is the `0x00` it already hashes. A later version adds
sections or keys; a version-1 game refuses a newer file by its header
rather than misreading it.

## 14. Open questions

Answered by the user on 2026-10-03: the recommended answer to each, so
the design above stands. Kept for the reasoning behind each choice.

1. **What a recipe looks like when someone writes one.** Recommended: the
   `.ini` shape of § 4.2, because Unreal Tournament's players already edit
   its `.ini` files and it allows comments. Alternative: JSON, more familiar
   to programmers, no comments.
2. **Where a recipe lives, and which wins.** Recommended: § 4.3's order — a
   file named on the command line, then the player's own folder, then the
   ones shipped with the game — so a player can always override what we
   ship for their own machine. Alternative: only shipped recipes in the
   first release, the player's folder later.
3. **Whether the first version carries the fog and haze too.** Recommended:
   no; materials and the name first, since the baker reads only those, and
   atmosphere in version 2 once fog volumes exist to carry. Alternative:
   include the haze scale now, read at run time and so never in the name.

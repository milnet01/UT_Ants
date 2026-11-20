<!-- ants-spec-format: 1 -->
# UTA-0478 — bake a map's sounds and the weapon set's into bundles

**Status:** shipped (2026-11-19), green on the matrix. No review: contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it was the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0478 (in-session-2026-11-17, found writing UTA-0056's spec).
**Blocker for:** UTA-0467, UTA-0480.  **Pairs with:** UTA-0056.

**Layman:** The sounds each map needs are copied into that map's baked file, and the weapon sounds into one small shared file, so the game can load them without reading Unreal Tournament's own files. Nothing is heard yet: playing them is the next items' work.

## 1. Goal

A baked map carries every sound its actors name, and one more bundle
carries the weapon set's sounds. The client loads both into a sound bank
keyed by each sound's path, and decodes them with `uaudio::decodeWav`.
UTA-0467 and UTA-0480 then play from that bank.

## 2. Problem

1. The client cannot read a sound. `docs/design.md` rule 2 keeps `upkg` out
   of `ut-ants`, and `apps/ut-ants/Audio.h` says "Nothing plays yet: the
   runtime has no sounds until UTA-0478 bakes them into the bundle."
2. The bundle has nowhere to put one. `KNOWN_IDS` in
   `src/ubundle/Bundle.cpp` names no sound section.
3. Nothing reads an actor's sound properties. The paths are already in the
   bundle: `PLAC` stores every property of every placed actor and each
   class's effective defaults as `ubundle::PropertyRecord`, and an object
   reference is its folded path (`src/ubundle/Bundle.h`).
4. The weapon set's sounds belong to no map. `weapons/stock.weapons` names
   each weapon's class and no sound. A map's bake may open only its import
   closure (`docs/specs/UTA-0011-map-baker.md` INV-2), and a map need not
   import the weapons' package.

The two halves already exist. `upkg::readSound` (`src/upkg/Sound.h`)
returns a `Sound` export's payload untouched, a complete RIFF WAV in stock
content. `uaudio::decodeWav` (`src/uaudio/Wav.h`) takes those bytes.

## 3. Scope decisions (agreed with the user)

1. **The weapon set's sounds go in one shared bundle, not in every map's**
   (user, 2026-11-19). A map's bundle keeps depending on the map, its
   recipe and the baker alone. Changing the weapon set re-bakes one small
   file. Measured the same day with a scratch script over the export table:
   `Botpack.u` holds 113 `Sound` exports, 4.8 MB in all. § 13 has what the
   stock set came to once built.
2. **The bundle carries each sound's bytes as the package holds them**
   (session, from the roadmap body). The baker does not decode or convert.
   `ubake` links no `uaudio`, and `decodeWav` already refuses what it
   cannot play.
3. **A map's sounds are found by what a property points at, not by the
   property's name** (session). Any object reference that resolves to a
   `Sound` export counts. A list of property names would miss the ones a
   mod's class adds.
4. **The weapon set's sounds are its classes' defaults, one class further**
   (session). A weapon's class names its projectile and ammo classes in
   class-valued defaults; their sounds are gathered too, and no further. A
   sound a script names in its code is out of reach (§ 9).
5. **`shapeOf` does not sample the new section** (session). The roadmap
   body said it would. `shapeOf` in `src/urender/Frame.cpp` decides whether
   the renderer uploads again, and the renderer uploads no sound.
6. **Nothing plays in this item** (session). The roadmap splits playing
   into UTA-0467 for weapons and UTA-0480 for a map's own sounds.

## 4. Design

### 4.1 The `SNDS` section

```cpp
namespace uta::ubundle {

/// One baked sound -- UTA-0478 SS 4.1.
struct BakedSound {
    std::string path;            ///< folded "package.group.name", as PLAC spells a reference
    std::vector<std::byte> data; ///< the Sound export's payload, verbatim; never empty
};

inline constexpr std::size_t SOUND_LIMIT = 4096;

struct Bundle {
    // ...
    std::optional<std::vector<BakedSound>> sounds; // SNDS
};

}
```

On disk, with `src/ubundle/Codec.h`'s framing:

```text
SNDS    := vector<Sound>
Sound   := string path, u32 byteCount, byteCount bytes
```

`ID_SNDS` is `{'S','N','D','S'}`. `write` emits it last, after `PROP`. A
sound's smallest encoding is eight bytes, which sizes `readVector`'s
division.

`validateSounds` holds, for `read` (`MalformedData`) and `write`
(`InvalidArgument`):

1. at most `SOUND_LIMIT` sounds;
2. no empty `path` and no empty `data`;
3. paths strictly ascending by byte value, so no path appears twice and
   the same sounds always encode to the same bytes.

The section's codec lives in `src/ubundle/SoundSection.cpp`, declared in
`src/ubundle/Sections.h`, as `GrassSection.cpp` is.

### 4.2 The `Sounds` bundle kind

```cpp
enum class BundleKind : std::uint8_t { Map = 0, Character = 1, Sounds = 2 };
```

A `Sounds` bundle holds `BAKR` and `SNDS` and no other section. `read`
and `write` refuse any other section in one. A `Map` bundle may hold
`SNDS`. The header's kind byte is refused above 2.

`FORMAT_VERSION` becomes 37.

### 4.3 A map's sounds

A step of `detail::bake` in `src/ubake/Bake.cpp`, in a new
`src/ubake/Sounds.cpp`, after the placements are built:

1. Collect every `PropertyRecord` of kind `Object` with a non-empty value,
   from each `ActorPlacement::properties` and each `ActorClass::defaults`
   in the bundle's placements.
2. Resolve each distinct path through the bake's resolver, as
   `siteOfPath` resolves a prop's skin. Keep it where it leads to an
   export whose class is `Sound`. A path that leads elsewhere, or nowhere,
   is not a sound and is dropped without a report: most object references
   name textures, meshes and classes.
3. `upkg::readSound` the export. A sound that fails to read, or whose
   payload is empty, is left out and listed in
   `BakeResult::skippedSounds` with its path and the reason. So is each
   sound past `SOUND_LIMIT`, in path order.
4. The rest, sorted by path, are the bundle's `sounds`. A map with none
   carries an empty `SNDS`.

Every package this opens is in the map's import closure: an actor's own
reference is an import of the map, and a class default's is an import of
that class's package. UTA-0011's INV-2 stands unchanged.

`BAKER_REVISION` becomes 79. `ut-bake`'s report gains, on `written` and
`over-budget`,
`"sounds": {"count": N, "bytes": N, "skipped": [{"path": "...", "why": "..."}]}`,
shaped as its `props` member is.

### 4.4 The weapon set's sound bundle

```cpp
namespace uta::ubake {

struct SoundSetRequest {
    std::filesystem::path install;
    std::vector<std::string> classes; ///< folded "package.class", any order
    std::filesystem::path outDir;
    bool force = false;
};

struct SoundSetOutcome {
    Verdict verdict = Verdict::Written; ///< Written or Cached
    std::string name;
    std::filesystem::path path;         ///< outDir / (name + ".utab")
    std::size_t soundCount = 0;         ///< zero when Cached
    std::uint64_t soundBytes = 0;
    std::vector<std::string> unresolved;      ///< classes not found
    std::vector<SkippedSound> skippedSounds;
};

[[nodiscard]] Result<SoundSetOutcome> bakeSoundSet(const SoundSetRequest& request);

}
```

**Which sounds.** For each class in `classes`: its
`upkg::effectiveDefaults`, every object-valued one that leads to a `Sound`
export, as § 4.3 items 2 and 3. Then the same for every class one of those
defaults names by a class-valued property. Not for the classes *those*
name. A class that does not resolve goes in `unresolved` and the bake
goes on.

**The name** is the lower-case hex SHA-256 of:

```text
"uta-sound-set-name-2" LF
bakerVersion() LF
each class of the request, sorted, each once: the path, LF
"sounds" LF
each sound path the request names, sorted, each once: the path, LF
each package of the closure, sorted: the name, LF, then 0x01 and the
  file's SHA-256, or 0x00 where the resolver returns none
```

The closure is the packages the request's classes name, and everything
`detail::closure` reaches from each. It is `bakeNameOf`'s closure rule
(`src/ubake/Name.cpp`) with the weapon classes' packages as the roots, and
it covers every package the bake opens.

**Caching and writing** are `bakeToDirectory`'s rules
(UTA-0011 § 4.7 items 1, 2 and 4): a file at the path whose header reads
is the outcome unless `force`. The bundle's kind is `Sounds` and its
origin `Derived`.

**The command line** gains one form:

```text
ut-bake --install <install> --out <dir> [--force] --weapon-sounds <weapons file>
```

`ut-bake` reads the file with `ugame::load` and passes each
`Weapon::className`. `tools/ut-bake` links `uta_ugame` for it; `uta_ubake`
does not. The report is one JSON object with `schema`, `verdict`, `name`,
`path` and `bakerVersion` as a map bake's report spells them, and
`"weapons"`, the file as given. A `written` report adds `"sounds"`, as
§ 4.3's, and `"unresolved"`. A weapons file that does not read is
`refused`, exit 1.

### 4.5 The sound bank

```cpp
namespace uta::uaudio {

/// Decoded sounds by path -- UTA-0478 SS 4.5.
class SoundBank {
public:
    /// Decodes `wav` and keeps the clip under `path`. A path already held
    /// keeps its first clip. A payload decodeWav refuses is not kept, and
    /// its error is returned.
    Result<void> add(std::string path, std::span<const std::byte> wav);

    /// Null where `path` is not held. The pointer stays good until the
    /// bank is destroyed, whatever is added after.
    [[nodiscard]] const Clip* find(std::string_view path) const;

    [[nodiscard]] std::size_t size() const;
};

}
```

`uaudio` still links `uta_core` alone (UTA-0056 INV-11): the bank takes
spans, not a `Bundle`.

### 4.6 The client

```text
ut-ants ... [--sounds <bundle>] <install> <bundle>
```

1. `ut-ants` adds the map bundle's `sounds` to a `SoundBank`, then the
   `--sounds` bundle's, and drops the stored bytes. It refuses to start
   when `--sounds` names a file that does not read or whose kind is not
   `Sounds`. A bundle from an older format is refused by `read`.
2. It prints one line to standard error: how many sounds are ready, and
   the path of each one refused.
3. The bank is destroyed after the `Audio` that plays from it, since a
   voice holds a pointer to its clip (`src/uaudio/Mixer.h`).
4. The launcher runs `ut-bake --weapon-sounds` on
   `ugame::shippedDirectory() / "stock.weapons"` once, as it starts, into
   the same directory as the map bakes, and passes the result as
   `--sounds` to each map it opens. A failure there is said on standard
   error, and maps still open, without the weapon sounds. The bundle
   carries `BAKR`, so the launcher's sweep of older bakers' files
   (`unreachableBakes`) keeps today's and removes the rest.

## 5. Invariants

- **INV-1** — `read(write(bundle))` returns `bundle.sounds` unchanged, path and bytes, for a map bundle and for a `Sounds` bundle. *Test:* `tests/unit/SoundSectionTest.cpp`, three sounds, one of a single byte.
  *Breaks when:* the codec writes the byte count as the vector's count, or drops a sound's last byte.
- **INV-2** — `read` refuses with `MalformedData`, and `write` with `InvalidArgument`, a `SNDS` that breaks any of § 4.1's three rules, and `read` refuses a sound whose byte count passes its section. *Test:* `tests/unit/SoundSectionTest.cpp`, one fixture per rule, each valid but for that rule: two equal paths; two paths descending; an empty path; an empty payload; one sound past `SOUND_LIMIT`; a byte count one past the section's end.
  *Breaks when:* the order check is `<=` in place of `<`, or the limit check is dropped.
- **INV-3** — A bundle of kind `Sounds` with any section but `BAKR` and `SNDS` is refused by `read` and by `write`, and a kind byte of 3 is refused by `read`. *Test:* `tests/unit/SoundSectionTest.cpp`, a `Sounds` bundle with an empty `LITE`, and a valid bundle with its kind byte patched.
  *Breaks when:* the kind check still stops at `Character`, or `write` checks the kind and `read` does not.
- **INV-4** — A map's `SNDS` holds exactly the sounds § 4.3 names, each once, with `readSound`'s bytes. *Test:* `tests/unit/BakeSoundsTest.cpp`, on a fixture map whose actors name: one sound of another package in an own property; the same sound again in a second actor; a sound only a class default names; a null reference; a texture. Expects the two sounds, ascending.
  *Breaks when:* class defaults are not read, a texture is taken for a sound, or a sound named twice is stored twice. The texture isolates the class test and the second actor isolates the dedup.
- **INV-5** — A sound whose export does not read, or reads empty, is left out and listed in `skippedSounds`, and the bake succeeds. *Test:* `tests/unit/BakeSoundsTest.cpp`, a fixture with one good sound and one whose size field passes its export.
  *Breaks when:* `readSound`'s error fails the bake.
- **INV-6** — `bakeSoundSet` stores the sounds of each listed class's effective defaults and of the classes those name, and of no class further. A class that does not resolve is listed and the rest are baked. *Test:* `tests/unit/BakeSoundsTest.cpp`, a weapon class whose parent sets one sound, naming a projectile class with one sound, which names a third class with one sound; and a class name that is not there. Expects the first two sounds and the name in `unresolved`.
  *Breaks when:* own defaults are read in place of effective ones, the walk does not follow a class-valued default, or it follows two.
- **INV-7** — The sound set's name changes when a class is added to the request and when any closure package's bytes change, and not when the request's classes are reordered or repeated. The sounds a request names move it too: UTA-0467 INV-8. *Test:* `tests/unit/BakeSoundsTest.cpp`, five names from one fixture install.
  *Breaks when:* the classes are hashed in request order, or a package the weapon's package imports is left out of the closure.
- **INV-8** — `SoundBank::find` returns the clip `decodeWav` gives for the bytes added under that path, and null for a path not added. A second `add` under a held path changes nothing. A payload `decodeWav` refuses is not held and `add` returns its error. A pointer `find` returned still reads the same clip after a hundred more sounds are added. *Test:* `tests/unit/SoundBankTest.cpp`, on WAV payloads built in the test.
  *Breaks when:* the clips live in a `std::vector` that grows, or a refused payload leaves an empty clip behind.
- **INV-9** — `ut-ants` still links none of `uta_upkg`, `uta_umat` and `uta_ubake`, `uta_uaudio` still links `uta_core` alone, and `uta_ubake` links no `uta_ugame`. *Test:* configure time: `apps/ut-ants/CMakeLists.txt`, `src/uaudio/CMakeLists.txt` and `src/ubake/CMakeLists.txt`'s link-list check.
  *Breaks when:* the bank takes a `Bundle`, or `ubake` parses the weapons file itself.
- **INV-10** — Baked from the real install, DM-Deck16][ carries at least one sound, the stock weapon set at least one per weapon class that resolves, and every stored sound either decodes or is one `decodeWav` refuses by its format. The test prints each bundle's sound count, stored bytes and decoded bytes. *Test:* `tests/real/RealSoundsTest.cpp`, on the real-asset tier.
  *Breaks when:* the gather resolves no path on real packages, or stores bytes that are not the export's payload.
- **INV-11** — The bundle is a file from outside, and a sound's bytes are never trusted: `read` bounds every count by its section (INV-2), and the bank decodes only through `decodeWav`, which refuses a chunk that runs past its bytes (UTA-0056 INV-1). *Test:* `tests/unit/SoundSectionTest.cpp` for the first half; `WavTest` for the second.
  *Breaks when:* the client reads a sound's header itself to size a buffer.

## 6. Failure modes

- **A reference's package is missing.** The path leads nowhere and is
  dropped (§ 4.3 item 2). `ut-bake --check` already reports a missing
  package.
- **A sound's export is damaged or empty.** Left out and reported
  (INV-5).
- **A stored sound is not PCM.** It is stored. The bank refuses it, the
  client names it on standard error, and `find` returns null, so it plays
  as silence, as UTA-0056 § 4.2 decided.
- **A weapon class is missing from the install.** Listed in `unresolved`;
  the other classes' sounds are baked.
- **No `--sounds` bundle, or the launcher's bake of it fails.** The client
  runs with the map's sounds alone.
- **A map and the sound set both hold one path.** The bank keeps the
  map's, added first. In one install both come from one export.
- **An old bundle.** Format 36 is refused by `read` as every older format
  is (§ 14).

## 7. Tests

| Test | Locks | Label |
|---|---|---|
| `tests/unit/SoundSectionTest.cpp` | INV-1, INV-2, INV-3, INV-11 | `unit` |
| `tests/unit/BakeSoundsTest.cpp` | INV-4, INV-5, INV-6, INV-7 | `unit` |
| `tests/unit/SoundBankTest.cpp` | INV-8 | `unit` |
| `tests/real/RealSoundsTest.cpp` | INV-10 | real-asset tier, hidden |
| The three `CMakeLists.txt` link checks INV-9 names | INV-9 | configure time |

Each unit test is seen to fail before the code it locks exists, and a
hand mutation of each `Breaks when` is seen to fail it
(`.claude/rules/testing.md`). `tests/unit/BakeGoldenTest.cpp`'s golden
hash is re-recorded with the revision. The tests that pin
`FORMAT_VERSION` and `BAKER_REVISION` move with them.

## 8. Alternatives considered (and rejected)

- **The weapon sounds in every map's bundle.** One file per map. It lost
  on the user's call (§ 3 decision 1): a change to the weapon set would
  re-bake every map, and a map's bake would open a package outside its
  import closure, against UTA-0011 INV-2.
- **Finding a map's sounds by property name** (`AmbientSound`,
  `OpeningSound` and the rest). It lost because the list is never
  complete: a mod's class adds its own names.
- **Decoding at bake time and storing samples.** It lost because `ubake`
  would link `uaudio`, the stored form would be two to four times the
  size, and the bytes as held are already what `decodeWav` takes.
- **Resampling everything to 48000 Hz at bake time.** The mixer already
  steps each clip by its own rate (UTA-0056 INV-2).
- **A `Bundle` argument on the bank.** It lost to UTA-0056 INV-11.
- **Following class-valued defaults to any depth.** It lost because a
  weapon reaches the player's and the game's classes within a few steps,
  and their sounds are not the weapon set's.

## 9. Out of scope

- Playing weapon sounds, and naming which sound a weapon event plays —
  `docs/specs/UTA-0467-weapon-sounds.md`. Its § 4.4 adds the sounds the
  weapons file names to the request, the name and the report here.
- Playing a map's own sounds, and positional sound — tracked by UTA-0480.
- Player, monster, announcer and music sounds — deferred; not yet queued.
- Compressing the stored sounds — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2, INV-3 | `tests/unit/SoundSectionTest.cpp` |
| INV-4, INV-5, INV-6, INV-7 | `tests/unit/BakeSoundsTest.cpp` |
| INV-8 | `tests/unit/SoundBankTest.cpp` |
| INV-9 | Configure time, every build |
| INV-10 | `tests/real/RealSoundsTest.cpp`, run by hand: nothing runs the real tier automatically |
| INV-11 | Partial: the two tests it names; **nothing** checks that the client decodes only through `decodeWav` |
| § 4.3, the bake opens nothing outside the closure | UTA-0011 INV-2's test in `tests/unit/BakeTest.cpp` |
| § 4.4, the command line | `tests/unit/BakeCliTest.cpp`, the `--weapon-sounds` case |
| § 4.6, `--sounds` is parsed | `tests/unit/ClientCliTest.cpp`, the `--sounds` case |
| § 4.6, the client's bank and its one line | **nothing** automatic; run by hand on a baked map |
| § 4.6 item 4, the launcher bakes and passes the sound set | **nothing** automatic; the bake was run by hand, the pass to the viewer was not |

## 11. Cross-doc impact

- `docs/specs/UTA-0008-bundle-container-and-origin.md`: the kind byte's
  table and its refusal row gain `2`, the sounds bundle.
- `docs/specs/UTA-0011-map-baker.md` § 4.8: the `--weapon-sounds` form
  and the report's new members.
- `CLAUDE.md` § Standing facts: the format and the baker revision.
- `CHANGELOG.md`.
- `apps/ut-ants/Audio.h`'s comment that nothing plays until this item.
- `apps/ut-ants/Cli.cpp`'s help: `--sounds`. `tools/ut-bake`'s help:
  `--weapon-sounds`.
- ROADMAP UTA-0478's body names this file and drops the `shapeOf` clause
  (§ 3 decision 5).

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0478-baked-sounds-loop-log.md`.

## 13. Resource cost

A map's bundle grows by its sounds' stored bytes, and the client holds
each decoded as `float` samples, four bytes a sample: four times an
8-bit sound's stored size and twice a 16-bit one's. `SOUND_LIMIT` caps
the count.

Measured 2026-11-19 by INV-10's test, which prints these:

| Bundle | Sounds | Stored bytes | Decoded bytes | Refused |
|---|---|---|---|---|
| DM-Deck16][ | 51 | 1,842,810 | 4,966,120 | 0 |
| The stock weapon set | 40 | 1,508,648 | 4,315,336 | 0 |

The client run on both held 62 sounds: the map and the weapon set share
the rest, since the map places weapons.

No new dependency. One new build target per test file.

## 14. Migration / compatibility

`FORMAT_VERSION` 37 and `BAKER_REVISION` 79 rename every bake, and `read`
refuses a format 36 file, as on every earlier bump. Every map is re-baked
on its next use. `ut-ants` without `--sounds` behaves as before but for
the bank it builds from the map.

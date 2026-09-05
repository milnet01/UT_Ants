# UTA-0004 — `upkg`: typed level content

**Status:** spec draft (2026-09-05).
**Kind:** implement.
**Source:** ROADMAP UTA-0004 (design-2026-09-03).
**Blocked by:** UTA-0003 — every reader here reads through that container.
**Blocker for:** UTA-0006, UTA-0007, UTA-0009, UTA-0011 — the graphs, the
room partition, the material generator and the baker all consume what this
item returns.

Turn the objects inside an opened package into things this project can use:
the shape of a level, its pictures, its sounds, and the list of what the
designer placed in it.

## 1. Goal

Given a `Package` opened by UTA-0003, `upkg` returns typed content for the
object classes a map and its dependencies are made of — `Polys` (textured
polygons with their flags), `Model` (the BSP tables), `Texture` and its
palettised mip chains, `Palette`, `Sound`, and `Level`'s actor list. Every
reader either consumes its export's bytes exactly or returns an `Error`;
none throws, and none reads outside the span. When this was written `upkg`
could reach an object's bytes and its tagged property list and could
interpret neither: `src/upkg/` held `ByteReader`, `Package` and
`Properties` alone.

## 2. Problem

`docs/design.md` § The parts gives `upkg` "Reading Unreal Engine 1
packages … Data in, structures out". UTA-0003 built the container half:
`uta::upkg::Package::open` returns the header and the three tables,
`Package::serialBytes` returns an export's byte range, and
`uta::upkg::readProperties` returns the tagged property list that range
begins with. Nothing interprets what follows that list, which is where all
the level content lives.

Four items are blocked behind that. `unav` (UTA-0006) needs the actor list
to find navigation points. `umap` (UTA-0007) needs geometry to partition.
`umat` (UTA-0009) needs texture pixels and palettes. `ubake` (UTA-0011)
needs all of it.

Three properties of the input shape this contract.

1. **It is untrusted.** UTA-0003 § 2 item 2 owns the argument and it
   carries over unchanged: `unet` is responsible for content transfer, so
   a package can arrive from a server. Every count and every offset here
   is attacker-controlled, and each one sizes a loop or a view.
2. **The published documentation is incomplete and partly wrong.** The
   community reference reproduced by the BeyondUnreal and Unreal Archive
   wiki pages documents `Texture`, calls itself a brief introduction
   rather than a full documentation of the format, and covers none of
   `Model`, `Sound` or `Level`. Where a layout could be tested against
   real files it was, and § 2.1 records what that found — including one
   case where the obvious reading of the wiki is wrong.
3. **`upkg`'s own fixtures cannot be the only evidence.** UTA-0003 § 2
   argues that a fixture writer and a reader written by one author from
   one reading of the format agree with each other rather than with the
   format. That argument applies with more force here, because these
   layouts are less documented than the container's.

### 2.1 What the reference install actually contains

Every layout claim in § 4 was checked by decoding the reference install
with a throwaway probe written independently of `upkg` — a different
language and a different author's reading — rather than from the wiki.
The install is the one `UTA_UT_INSTALL_DIR` points at.

Three findings changed the design.

**Package versions are not uniform.** Re-derive with:

```sh
for f in "$UT"/{Maps,Textures,Sounds,Music,System}/*; do
  od -An -tu2 -j4 -N2 "$f"; done | sort -n | uniq -c
```

UT99's own content runs from version 61 to 69, not the 68 and 69 a
map-only sample suggests: dozens of the stock texture packages are below
68, and the oldest are at 61. That matters because two fields in § 4.6 and
§ 4.7 exist only from version 63, so the older branch is exercised by
stock content rather than being an edge case. Packages at versions 76, 79, 118 and 128 are also
present — later-engine content that has been dropped into the install and
that these readers must refuse rather than misread.

**A texture may carry two mip chains.** Texture packages carrying
`bHasComp` and `CompFormat` properties store a second, block-compressed
copy of every mip after the first chain. Reading one chain and stopping
leaves the export short by the whole second chain, so it is not an
optional extra a reader can ignore — it desynchronises the read. This is
not in the wiki.

**The wiki's implied `Model` layout does not hold.** Reading the BSP
arrays in the order the community documentation implies yields five nodes
and thirty-nine surfaces for `DM-Deck16][.unr`, whose `Model` export is
over 450 kB. Those two cannot both be true. § 4.5 says what follows.

## 3. Scope decisions (agreed with the user)

### 3.1 Geometry comes out as the file's own tables — user, 2026-09-05

`upkg` returns `Polys` and the `Model` tables as they are stored and does
no geometry work: no BSP walk, no triangulation, no winding repair. Asked
to choose between faithful tables, ready-made triangles, or both, the user
chose faithful tables.

The reasons for it, recorded because the alternative will look attractive
again: it is what `docs/design.md` § The parts already says `upkg` is, the
walk is a real design decision that belongs where the bundle's shape is
decided (`ubake`, UTA-0011), and a reader that only transcribes can be
checked against the file byte for byte, which § 4.3 turns into this item's
whole acceptance test. The cost is accepted: nothing is drawable when this
ships.

### 3.2 Both mip chains are kept — user, 2026-09-05

Where a texture carries a compressed second chain, both chains are
returned and `umat` (UTA-0009) chooses. Asked to choose between keeping
both, keeping the original, and preferring the compressed copy, the user
chose both.

The reason recorded: the compressed copy is shipped art, and discarding it
here would make recovering it later a change to this contract rather than
a change to a caller.

### 3.3 The remaining calls are mine, with reasons

1. **The typed readers gate on package version; the container does not.**
   `Package::open` stays version-tolerant, because `ut-dump` (UTA-0012)
   has a legitimate reason to list the tables of a package this project
   cannot otherwise read. The gate sits on the typed readers, which are
   the things that would misread. § 4.2 sets the admitted range; § 2.1 is
   where it was measured.
2. **A class this item does not model is refused by name, not partly
   read.** `WaveTexture` is the measured case: it shares `Texture`'s mip
   chain and then stores something further that this item does not
   describe. Returning its mips and silently leaving bytes unread would
   defeat § 4.3 for every caller at once, so an unmodelled class is an
   `Error` naming the class.
3. **Bulk payload is returned as a view, never copied.** `Package` already
   holds a view of the caller's bytes and copies no package data
   (`src/upkg/Package.h`, its LIFETIME note); mip pixels and sound
   payloads are the largest things in a package and copying them would
   reverse that decision by the back door.
4. **Texture subclasses are recognised by class NAME, not by ancestry.**
   Resolving "is this class a `Texture`?" properly needs the class table,
   which is UTA-0005 and is not built. A named list is the honest interim,
   and § 9 records that UTA-0005 replaces it.
5. **`Palette` is in scope though the roadmap bullet does not name it.**
   The bullet asks for palettised textures; a palettised texture without
   its palette is indices with no colours. It is one small reader.

## 4. Design

### 4.1 Layout and the build

Four new pairs in `src/upkg/`, added to the existing `uta_upkg` target:

```
src/upkg/Geometry.h  .cpp   Polys, Model
src/upkg/Texture.h   .cpp   Texture family, Palette
src/upkg/Sound.h     .cpp   Sound
src/upkg/Level.h     .cpp   Level and the actor list
```

The link closure does not move: `uta_upkg` still links `uta_core` and
nothing else, which `src/upkg/CMakeLists.txt` already asserts at configure
time for UTA-0003's INV-13. Nothing here needs a new dependency.

Every entry point takes the opened `Package` and one `ExportEntry`, and
returns `Result<T>` — the same shape as `readProperties`:

```cpp
[[nodiscard]] Result<Polys>   readPolys  (const Package&, const ExportEntry&);
[[nodiscard]] Result<Model>   readModel  (const Package&, const ExportEntry&);
[[nodiscard]] Result<Texture> readTexture(const Package&, const ExportEntry&);
[[nodiscard]] Result<Palette> readPalette(const Package&, const ExportEntry&);
[[nodiscard]] Result<Sound>   readSound  (const Package&, const ExportEntry&);
[[nodiscard]] Result<Level>   readLevel  (const Package&, const ExportEntry&);
```

### 4.2 The version gate

Each entry point above refuses before reading anything when
`package.header().packageVersion` is outside 61 to 69 inclusive, with
`ErrorCode::UnsupportedVersion` and a message naming the version. § 2.1 measured
the range; § 3.3 item 1 says why the gate is here rather than in
`Package::open`.

### 4.3 Every reader ends exactly where the export ends

This is the item's central rule and its acceptance test.

An export declares its own byte range — `ExportEntry::serialOffset` and
`serialSize`, which UTA-0003 validated against the file when the package
opened. A reader that models a layout correctly finishes at exactly
`serialOffset + serialSize`. One that has a field's width wrong, or has
missed a version branch, or has missed a second mip chain, finishes
somewhere else. So:

> A typed reader that does not end exactly at the end of its export
> returns `MalformedData`. It never returns a partial result.

Two things make this worth building the item around. It is **total** —
it fires on every field of every object, without anyone having to predict
which field will be wrong — and it is **checkable against content this
project did not write**, over the whole reference install, which is the
evidence UTA-0003 § 2 says fixtures cannot supply. § 7 spends it.

It is not a proof of correctness: a reader could consume the right number
of bytes and assign them to the wrong fields. § 10 grades it on that.

### 4.4 `Polys` — the textured polygons

Verified byte-exact over every `Polys` export in the reference install's
maps. After the property list:

```
i32           Num              polygon count
i32           Max              allocated size; not used
  per polygon:
    index     NumVertices
    FVector   Base, Normal, TextureU, TextureV      (12 bytes each)
    FVector   Vertex[NumVertices]
    u32       PolyFlags
    index     Actor, Texture, ItemName, iLink, iBrushPoly
    i16       PanU, PanV
```

`Texture` is an object reference resolved through
`Package::objectName`; `ItemName` is a name index. `PolyFlags` is the
field ROADMAP UTA-0004 calls "free information about glass, water, sky and
lava", and it arrives here per polygon, already paired with the texture it
applies to.

This is the surface most callers want, and it is the one whose layout is
certain.

### 4.5 `Model` — the BSP tables

`Model` is the one reader whose full layout this spec does not state,
because it could not be verified, and stating an unverified layout is what
`~/.claude/skills/write-spec/references/drafting-rules.md` forbids.

What is verified:

- The data begins with the `UPrimitive` prefix — an `FBox` (two
  `FVector` and a validity byte) followed by an `FSphere` (an `FVector`
  and a float): 41 bytes, decoded to real bounds on a brush model.
- A run of compact-index-prefixed arrays follows, all empty on a brush
  model.
- An object reference to the brush's `Polys` export sits after them,
  confirmed by resolving it to a `Polys`-classed export.
- Two trailing `i32` fields follow a second, shorter run of arrays.
- The array order implied by the community documentation is **wrong**
  (§ 2.1).

So the implementer derives the order against real packages, by the method
this spec was researched with, and § 4.3 is the acceptance: the order is
right when `readModel` consumes every `Model` export in the reference
install exactly. Deriving it is the largest single risk in this item.

**If it cannot be derived, that is a decision to bring back, not to
absorb.** The other five readers are independent of it and ship on their
own; whether `Model` is deferred to its own item is the user's call and
not the implementer's.

### 4.6 `Texture`, and the family that shares its layout

Verified byte-exact at versions 61 and 68, and across the reference
install's texture packages. Most of a texture's metadata is in the tagged
property list `readProperties` already returns — `Palette`, `USize`,
`VSize`, `UBits`, `VBits`, `UClamp`, `VClamp`, and `bHasComp` /
`CompFormat` where present. What follows the list is the mip chain:

```
u8            MipCount
  per mip:
    u32       WidthOffset        only when packageVersion >= 63
    index     Size
    u8        Data[Size]
    u32       Width, Height
    u8        BitsWidth, BitsHeight
```

Then, **only when the `bHasComp` property is true**, a second chain in the
same shape, holding the block-compressed copy (§ 3.2). `CompFormat` says
which compression; it is carried through as read and not interpreted here.

`WidthOffset`, where present, is the absolute file offset of the byte just
past that mip's data — so it equals the mip's data start plus `Size`. It
is redundant, which makes it a free cross-check: § 5's INV-5 spends it.

Classes read by this reader: `Texture`, `WetTexture`, `IceTexture` and
`ScriptedTexture` share the layout exactly. `FireTexture` shares it and
then stores a compact-index count and that many 8-byte spark records —
note that this count is the array's own and is *not* the `NumSparks`
property, which differs. Every other class, `WaveTexture` included, is
refused by name per § 3.3 item 2.

### 4.7 `Palette`

Verified byte-exact over every `Palette` export in the reference install's
texture packages, always with 256 entries:

```
index         Count
u8            Entry[Count][4]     R, G, B, A
```

The count is read rather than assumed to be 256, because it is a value
from the file and § 2 item 1 governs it.

### 4.8 `Sound`

Verified byte-exact at versions 61 and 68, and across the reference
install's sound packages. After the property list:

```
index         Format             a name index -- "WAV" in stock content
u32           NextOffset         only when packageVersion >= 63
index         Size
u8            Data[Size]
```

`NextOffset`, where present, is the offset just past `Data`, mirroring
§ 4.6's `WidthOffset`, and INV-5 checks it the same way. `Data` is a
complete RIFF WAV in stock content and is returned as an unmodified view:
decoding audio is `uaudio`'s (`docs/design.md` § The parts), and `Format`
is carried through so a caller can tell what it has rather than guessing
from the bytes.

### 4.9 `Level` and the actor list

Verified against the reference install's maps. After the property list:

```
i32           Num
i32           Max
index         Actor[Num]         object references
FURL          the level's URL
```

Two properties of this array shape the contract.

**It is sparse.** A large share of the slots in a stock map are null —
deleted actors leave holes rather than compacting the array. `Level`
therefore returns only the non-null references, and reports the raw slot
count separately so a caller can tell a sparse array from a short one.

**An actor's own data is already readable.** Each reference resolves to an
export whose class name is the actor's class and whose tagged property
list carries its settings, including `Location` and `Rotation`.
`readProperties` already decodes those two: `src/upkg/Properties.cpp`
maps a `Struct` property whose struct name is `Vector` or `Rotator` onto
`Vector3` and `Rotator`. So this reader enumerates and resolves; it does
not re-implement property reading, and it does not interpret any actor
class — resolving what a custom class *means* is UTA-0023, and its
ancestry is UTA-0005.

The `FURL` that follows the array is read only far enough to satisfy
§ 4.3, and is not returned: nothing in the roadmap consumes a level's URL.

### 4.10 The fixture builder grows

`tests/support/UnrealPackageBuilder` gains writers for the shapes above,
so the unit tier can build a package containing a `Polys`, a `Texture`
with and without a second chain, a `Palette`, a `Sound` and a `Level`
with a sparse actor array — and malformed variants of each. It ships an
encoder and no decoder, for the reason `tests/support/UnrealPackageBuilder.h`
states, so the readers here remain an independent implementation of the
same format.

## 5. Invariants

- **INV-1** — Every typed reader in § 4.1 either consumes its export's
  bytes exactly, from `serialOffset` to `serialOffset + serialSize`, or
  returns an `Error`. No reader returns a value having read a different
  number of bytes.
  *Test:* `tests/unit/PackageContentTest.cpp` for the fixture cases, and
  `tests/real/RealInstallTest.cpp` for the reference install. Cannot be
  run until the readers exist.
  *Breaks when:* a version branch is missed, a field's width is wrong, or
  a second mip chain is skipped — each of which leaves the cursor
  somewhere other than the end.
  *Isolates:* a fixture whose only defect is a trailing unread byte fails
  this and no other invariant here; nothing else in this spec inspects the
  final cursor position.

- **INV-2** — No typed reader throws, terminates, or reads outside the
  export's span, for any input bytes.
  *Test:* `tests/unit/PackageMalformedContentTest.cpp` drives truncations
  and lying counts built by the fixture builder. Nothing else checks it:
  `scripts/ci.sh`'s only sanitizer leg is ThreadSanitizer, which finds
  races rather than out-of-span reads. § 10 grades this on that.
  *Breaks when:* a count or size from the file is used to advance or to
  size a view before it is checked against the bytes present.

- **INV-3** — A package whose `packageVersion` is outside 61 to 69 is
  refused by every entry point in § 4.1, with
  `ErrorCode::UnsupportedVersion`,
  before any byte of the export is interpreted.
  *Test:* `tests/unit/PackageContentTest.cpp` builds a version 128 package
  and asserts each entry point refuses.
  *Breaks when:* a reader is added without the gate, or the gate is placed
  after the first field read — at which point a later-engine package is
  parsed as a UT99 one.
  *Isolates:* the fixture is well-formed for its own version, so nothing
  but the gate can reject it.

- **INV-4** — No allocation and no span is sized by a count or length read
  from the file before that value has been checked against the bytes
  remaining in the export.
  *Test:* `tests/unit/PackageMalformedContentTest.cpp` builds a `Palette`
  declaring a count far larger than the file and asserts an `Error` whose
  message names the count check. That the refusal precedes the allocation
  is not observable from outside; § 10 grades it on that.
  *Breaks when:* a polygon or mip loop reserves from the file's count
  first — a four-byte edit then asks for gigabytes.

- **INV-5** — Where `packageVersion >= 63`, a mip's `WidthOffset` and a
  sound's `NextOffset` equal the offset just past their payload; a
  disagreement is `MalformedData`.
  *Test:* `tests/unit/PackageContentTest.cpp` builds a texture whose
  `WidthOffset` is off by one and asserts the refusal.
  *Breaks when:* the redundant field is read and discarded rather than
  checked, which is the natural implementation and the reason this is an
  invariant rather than a note.
  *Isolates:* the fixture is otherwise well-formed and consumes exactly,
  so INV-1 passes on it and only this check can reject it.

- **INV-6** — A texture's second mip chain is read if and only if its
  `bHasComp` property is true, and `compressedMips` is non-empty if and
  only if it was read.
  *Test:* `tests/unit/PackageContentTest.cpp` builds one texture with the
  property and a second chain and one without either, and asserts both
  consume exactly and that only the first reports compressed mips.
  *Breaks when:* the reader looks for a second chain by trying to read one
  and seeing whether bytes remain, which succeeds by accident on a texture
  that has none.

- **INV-7** — A `Sound`'s payload and a mip's pixels are returned as views
  into the caller's bytes, byte-for-byte identical to the export's range,
  and are not copied or transformed.
  *Test:* `tests/unit/PackageContentTest.cpp` compares the returned span's
  `data()` against the address of the corresponding offset in the input
  buffer.
  *Breaks when:* a reader copies into a `std::vector` for convenience,
  which is invisible in behaviour and reverses § 3.3 item 3.

- **INV-8** — An object class this item does not model is refused with an
  `Error` naming the class, rather than read as its nearest relative.
  *Test:* `tests/unit/PackageContentTest.cpp` builds a `WaveTexture`
  export and asserts `readTexture` refuses and names it.
  *Breaks when:* the class check is a "starts with" or a fall-through
  default, at which point `WaveTexture` is read as a `Texture` and INV-1
  fires far from the cause.

- **INV-9** — `readLevel` returns every non-null actor reference in the
  file's order, and none of the null slots, and reports the raw slot count
  alongside.
  *Test:* `tests/unit/PackageContentTest.cpp` builds a level whose actor
  array interleaves null and non-null slots, and asserts both the returned
  sequence and the raw count.
  *Breaks when:* the reader compacts silently, at which point a caller
  counting actors and a caller reading the array disagree, or it returns
  the nulls, at which point every caller must filter them.

## 6. Failure modes

Every failure is an `Error` from `uta::core`; none is an exception, and
none is a partial result.

| Cause | Code |
|-------|------|
| Package version outside 61–69 | `UnsupportedVersion` |
| Object class not modelled by this item | `InvalidArgument` |
| Count or length exceeds the bytes present | `MalformedData` |
| Reader did not end at the export's end (§ 4.3) | `MalformedData` |
| `WidthOffset` / `NextOffset` disagrees with the payload (INV-5) | `MalformedData` |
| Export has no serialised data | empty result, not an error |

The last row follows UTA-0003's INV-7: a sizeless export is ordinary and
iterating a package must not error on one.

A short read is `MalformedData` and never `IoFailure`, because these
readers hold bytes rather than a file — the same rule, and the same
reasoning, as UTA-0003 § 4.5.

## 7. Tests

Three tiers, and only the third reads bytes this project did not write.

1. **`tests/unit/PackageContentTest.cpp`** — the well-formed cases, built
   by the fixture builder: each reader over each shape in § 4, the version
   gate, the two mip chains, the sparse actor array, the redundant-offset
   check, and the view-not-copy check. Covers **INV-1** (fixture half),
   **INV-3**, **INV-5**, **INV-6**, **INV-7**, **INV-8** and **INV-9**.
   Label `unit;fast`, in the existing Catch2 executable.

2. **`tests/unit/PackageMalformedContentTest.cpp`** — truncations, lying
   counts, and a payload whose declared size runs past the export. Every
   case asserts an `Error` rather than a crash. Covers **INV-2** and
   **INV-4**.

3. **`tests/real/RealInstallTest.cpp`** — covers **INV-1** over content
   this project did not write. Extended to walk every package
   under `UTA_UT_INSTALL_DIR`, run each typed reader over every export of
   a modelled class, and assert § 4.3. It **prints its own totals per
   class**, so the figures this spec's § 2.1 rests on are an output of the
   suite rather than a transcription in prose that nobody re-derives. Off
   by default behind `UTA_REAL_ASSET_TESTS`, which is what keeps a clone
   with no Unreal Tournament building and testing clean — the separation
   **S7** is measured on.

This tier is the acceptance for § 4.5: `Model`'s layout is right when it
consumes every `Model` export in the install exactly, and is not right
before then.

## 8. Alternatives considered (and rejected)

- **Triangulate in `upkg`.** Rejected by the user, § 3.1. It would make
  `upkg` own a design decision that belongs in the baker, and it would
  destroy § 4.3's oracle for geometry, because a triangulated output can
  no longer be checked against the file byte for byte.

- **Transcribe `Model`'s field order from the community documentation.**
  Rejected on evidence: § 2.1 measured what that order produces and it is
  wrong. A spec asserting it would have read as verified.

- **Convert palettised pixels to RGBA on read.** Rejected: `docs/design.md`
  § The parts gives texture-to-material work to `umat`, and converting
  here would force every caller to accept `umat`'s colour decisions,
  including `ut-dump`, which wants to show what the file holds.

- **Read the compressed chain only, where present.** Rejected by the user,
  § 3.2.

- **Resolve texture subclasses by ancestry now.** Rejected as out of
  order: ancestry needs the class table, which is UTA-0005. A named list
  is wrong for a custom subclass nobody has seen, and INV-8 makes that
  wrongness a refusal rather than a misread.

- **Skip the exact-consumption rule and check fields individually.**
  Rejected: field checks only catch the fields somebody thought to check,
  and § 2 item 3 is precisely that a single author's expectations are the
  thing that cannot be trusted here.

## 9. Out of scope

- **Class tables, default properties and ancestry.** UTA-0005. It is what
  replaces § 3.3 item 4's named subclass list.
- **Turning the tables into triangles.** UTA-0011, per § 3.1.
- **The navigation and event graphs.** UTA-0006, which consumes the actor
  list this item returns.
- **Room partition.** UTA-0007.
- **Meshes, skeletal or vertex-animated, and music.** No roadmap item
  reads them yet; `.umx` is out of this item entirely.
- **Interpreting `CompFormat`, or decompressing a compressed mip.**
  UTA-0009 (`umat`) — this item carries the bytes and the format tag.
- **Decoding audio.** `uaudio`; this item returns the payload as read.
- **Resolving a custom actor class onto one of ours.** UTA-0023.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/PackageContentTest.cpp` for fixtures, and `tests/real/RealInstallTest.cpp` over the reference install — the only check here that reads bytes this project did not write. Off by default, so an ordinary run proves agreement with our own fixtures only |
| INV-2 | **Partial:** `tests/unit/PackageMalformedContentTest.cpp` and its assertions alone. No memory checker runs it — the only sanitizer leg is ThreadSanitizer — so an out-of-span read the corpus does not provoke is caught by nothing until an AddressSanitizer leg or a fuzzer exists. Same grade, and the same reason, as UTA-0003's INV-1 |
| INV-3 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| INV-4 | **Partial:** the test asserts the refusal *names the count check*, so deleting that check is detectable. That bounds which check refuses, not the ordering: a reader that reserved first and still produced this message would pass. Bounding the allocation needs a counting allocator no harness here has — the grade UTA-0003's INV-2 carries, for the same reason |
| INV-5 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| INV-6 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| INV-7 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| INV-8 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| INV-9 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| § 4.3 "consuming exactly is not a proof of correctness" | **nothing** — a reader that consumed the right byte count and assigned the fields wrongly passes every check in this spec. What would catch it is comparing a decoded value against the same value obtained another way, and nothing here does that for any field |
| § 4.5 "`Model`'s order is derived, not transcribed" | **Partial:** the real-asset tier proves the order consumes exactly across the install, which is strong evidence and not a proof, by the row above |
| § 4.9 "`Location` and `Rotation` are already decoded" | `tests/unit/PackagePropertiesTest.cpp`, which UTA-0003 ships — this item adds no check of its own |
| § 3.3 item 3 "bulk payload is a view" | INV-7 covers the sound payload and mip pixels. Nothing checks that `Polys` vertices are not copied, and they are the one bulk field this spec does not require a view for |
| § 4.10 "the readers stay an independent implementation" | **nothing** — no check enforces that a reader and the fixture writer were not written from one reading. UTA-0003 § 2's argument, and the real-asset tier, are what stand in for it |

## 11. Cross-doc impact

- `CHANGELOG.md` — an `Added` entry under `[Unreleased]`.
- `CLAUDE.md` — the position lines only.
- `docs/design.md` — no change; this implements § The parts' `upkg`.
- `docs/specs/UTA-0003-package-container.md` — no change expected. Its
  § 9 already names this item as the owner of typed level content.
- `README.md` — no change; the build invocation does not move.

## 12. Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|

## 13. Resource cost

No new library and no new dependency; six readers added to `uta_upkg`.

- **Every reader is called per export and holds nothing between calls.**
  There is no cache and no global state.
- **The returned structures own their small fields and view their large
  ones** (§ 3.3 item 3), so reading a texture allocates its mip
  descriptors and not its pixels. The largest allocation in this item is
  a `Polys`' vertex array, which is bounded by the export's own size.
- **The real-asset tier is the expensive part**, running six readers over
  every export of every package in an install. It is off by default and
  is not on the ordinary gate.

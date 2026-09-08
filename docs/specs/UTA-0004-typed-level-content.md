# UTA-0004 — `upkg`: typed level content

**Status:** accepted (2026-09-05).
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

UT99's own content runs from version 61 to 69: dozens of the stock
texture packages are below 68, and the oldest are at 61. That matters because two fields in § 4.6 and
§ 4.8 exist only from version 63, so the older branch is exercised by
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

1. **This item adds no version gate, because UTA-0003 already built
   one.** `Package::open` refuses anything outside 61 to 69 before it
   reads a table, and UTA-0003 § 4.4 and its INV-9 own that rule. So a
   `Package` outside the range cannot be constructed, and a gate on the
   typed readers could never fire. § 4.2 says what this item inherits.
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

The returned types are declared here rather than left to the
implementation, because § 2 item 1's argument is UTA-0003's: four later
items bind to them, and a vocabulary invented twice is two vocabularies.
Bulk payload is a view, per § 3.3 item 3.

```cpp
struct Polygon {
    std::vector<Vector3> vertices;      // NumVertices of them
    Vector3         base, normal, textureU, textureV;
    std::uint32_t   polyFlags = 0;
    ObjectReference actor, texture;     // unresolved -- see below
    std::uint32_t   itemName = 0;       // name index
    std::int32_t    link = 0, brushPoly = 0;
    std::int16_t    panU = 0, panV = 0;
};
struct Polys { std::vector<Polygon> polygons; };

struct Mip {
    std::span<const std::byte> pixels;  // a view, never a copy
    std::uint32_t width = 0, height = 0;
    std::uint8_t  bitsWidth = 0, bitsHeight = 0;
};
struct Texture {
    std::vector<Mip> mips;              // empty only for a sizeless export
    std::vector<Mip> compressedMips;    // empty unless bHasComp (INV-6)
};

struct PaletteEntry { std::uint8_t r = 0, g = 0, b = 0, a = 0; };
struct Palette { std::vector<PaletteEntry> entries; };

struct Sound {
    std::uint32_t formatName = 0;       // name index -- "WAV" in stock content
    std::span<const std::byte> data;    // a view; a RIFF WAV in stock content
};

struct Level {
    std::vector<ObjectReference> actors;   // non-null only, in file order
    std::uint32_t rawSlotCount = 0;        // including the nulls (INV-9)
    std::vector<ReachSpec> reachSpecs;     // UTA-0057, in file order
};
```

**`ReachSpec` is UTA-0057's and is declared there**, not here — that item
derived the level's tail and owns the record's field order. `Level` carries
the array because four later items bind to this type and would otherwise read
a superseded declaration.

**An object reference is returned unresolved**, as `ObjectReference` rather
than as a name. `Package::objectName` returns a `std::string_view` whose
lifetime is the `Package`'s, so resolving here would hand every caller a
view it did not ask for and cannot outlive; the caller resolves what it
needs.

`Model` is the exception, and § 4.5 says why: its verified fields are

```cpp
struct Model {
    Vector3 boundsMin, boundsMax;       // FBox
    bool    boundsValid = false;
    Vector3 sphereCentre; float sphereRadius = 0;
    ObjectReference polys;
    std::int32_t rootOutside = 0, linked = 0;
};
```

Its BSP index tables are `std::vector` members added by the
implementation that derives their order — the member *set* is knowable
from the file, their serialisation order is what § 4.5 says is not. **The
implementation names those members, and UTA-0007 binds to them**: `umap`
partitions on the level's zones, which live in the BSP tables and not in
`Polys`. UTA-0009 consumes only `Polys`. **UTA-0006 consumes the actor list
AND the reach-spec array in § 4.9's tail**, which UTA-0057 derives: a node's
`Paths` entries index into it, so the actor list alone gives `unav` nodes and
no edges. Corrected 2026-09-06; this sentence had said both items needed only
`Polys` and the actor list.

### 4.2 The version gate is inherited, not rebuilt

Every reader here takes an already-opened `Package`, and
`Package::open` refuses a `packageVersion` outside 61 to 69 with
`ErrorCode::UnsupportedVersion` before reading any table — the constants
are `MIN_VERSION` and `MAX_VERSION` in `src/upkg/Package.cpp`. So the
later-engine packages § 2.1 found are already unreachable from here, and
**no reader in § 4.1 repeats the check**.

Stating this is not redundant: the range must exist in exactly one place
or the two copies drift, and INV-3 is what holds that.

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

`Texture` is an object reference, returned unresolved per § 4.1 — a
caller that wants its name calls `Package::objectName`. `ItemName` is a
name index. `PolyFlags` is the
field ROADMAP UTA-0004 calls "free information about glass, water, sky and
lava", and it arrives here per polygon, already paired with the texture it
applies to.

This is the surface most callers want, and it is the one whose layout is
certain.

### 4.5 `Model` — the BSP tables

`Model` is the reader whose layout this spec states least, because it
could not be verified, and stating an unverified layout is what
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

**It could not be derived, and the decision came back.** The other five
readers were independent of it and shipped on their own; on 2026-09-05
the user split `Model` out, and on 2026-09-06 that item was itself split:
the `Model` half is **UTA-0069**, which carries this section as its brief,
and **UTA-0057** keeps the `Level` tail of § 4.9. § 14 records what shipped here.

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

`WidthOffset`, where present, is an offset into the **whole package file**,
not into the export — it is the byte just past that mip's data. A reader
holding a span of the export therefore compares it against
`entry.serialOffset` plus its own cursor, and a reader that forgets the
`serialOffset` term refuses every real texture. It is redundant, which
makes it a free cross-check: § 5's INV-5 spends it.

Classes read by this reader: `Texture`, `WetTexture`, `IceTexture` and
`ScriptedTexture` share the layout, measured consuming exactly across the
reference install's texture packages and maps. It does **not** extend to
every file in the install: a small number of exports across two community
files are refused, all of them by one of the two shapes § 7 tier 3 names.
The tier reports its own totals, so the figure is an output of the suite
rather than prose here that nobody re-derives. `FireTexture` shares it and
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

**What follows the actor array is NOT fully derived, and `Model` is not
the only reader in that position.** An `FURL` comes next and decodes
cleanly across the install's maps — four compact-index-prefixed strings
(protocol, host, map, portal), a string array of options, then `i32` port
and `i32` valid — but a substantial remainder follows *it*, tens of
kilobytes on a stock map, which this spec does not describe. Neither the
`FURL` nor that remainder is returned; nothing in the roadmap consumes a
level's URL, and the actor list is what the blocked items want.

They must still be *consumed*, because § 4.3 admits no partial read. So
the remainder is derived exactly as § 4.5's tables are, with § 4.3 as its
acceptance, and § 4.10 does not build a `Level` fixture beyond the actor
array until it is. **That derivation is UTA-0057's**, split out with
§ 4.5's on 2026-09-05; `readLevel` is not among the readers § 14 records
as shipped.

### 4.10 The fixture builder grows

`tests/support/UnrealPackageBuilder` gains writers for the shapes above,
so the unit tier can build a package containing a `Polys`, a `Texture`
with and without a second chain, a `Palette`, a `Sound` and a `Level`
with a sparse actor array — and malformed variants of each.

**It must build a `Texture` and a `Sound` at a version below 63 as well as
at 68.** § 4.6's `WidthOffset` and § 4.8's `NextOffset` exist only from 63,
so without a pre-63 fixture the older branch is exercised by nothing on the
default gate — and INV-1 names a missed version branch as its first break
mode. § 2.1 measured that stock content takes that branch.

**It does not build a `Model`, and it builds a `Level` only as far as the
actor array.** Both layouts are derived now — `Model` by UTA-0069, the
`Level` tail by UTA-0057 — so the reason is no longer that there is
nothing to encode against. The `Model` fixtures live in the unit-test
files' own helpers instead; § 7 and § 10 say what covers each. It ships an
encoder and no decoder, for the reason `tests/support/UnrealPackageBuilder.h`
states, so the readers here remain an independent implementation of the
same format.

## 5. Invariants

- **INV-1** — Every typed reader in § 4.1 either consumes its export's
  bytes exactly, from `serialOffset` to `serialOffset + serialSize`, or
  returns an `Error`. No reader returns a value having read a different
  number of bytes.
  *Test:* `tests/unit/PackageContentTest.cpp` for the fixture cases —
  every reader but `readModel`, which § 7 tier 1 explains — and
  `tests/real/RealInstallTest.cpp` for the reference install, which is
  `readModel`'s only check. Cannot be run until the readers exist.
  *Breaks when:* a version branch is missed, a field's width is wrong, or
  a second mip chain is skipped — each of which leaves the cursor
  somewhere other than the end.
  *Isolates:* a fixture whose only defect is a trailing unread byte fails
  this and no other invariant here; nothing else in this spec inspects the
  final cursor position.

- **INV-2** — No typed reader throws, terminates, or reads outside the
  export's span, for any input bytes.
  *Test:* `tests/unit/PackageMalformedTest.cpp` drives truncations
  and lying counts — for every reader, `readModel` now included. Its cases
  declare more nodes than the export can hold, a negative node count, and
  more leaves than it holds. Both exceptions this clause carried are gone:
  UTA-0069 derived `Model`'s layout on 2026-09-08, and `readLevel`'s
  remainder left the same category when UTA-0057 derived it.
  Nothing else checks it:
  `scripts/ci.sh`'s only sanitizer leg is ThreadSanitizer, which finds
  races rather than out-of-span reads. § 10 grades this on that.
  *Breaks when:* a count or size from the file is used to advance or to
  size a view before it is checked against the bytes present.

- **INV-3** — The supported version range is stated in exactly one
  place. No file added by this item names a version bound or compares
  `packageVersion` against one.
  *Test:* `grep -nE '\b(61|69)\b' src/upkg/{Geometry,Texture,Sound}.{h,cpp}`
  returns no line that compares against 61 or 69. **It is not empty**, and
  was not when this clause said it should be: `Geometry.cpp` carries a
  comment explaining why `readModel`'s version boundary is 62, and prose
  naming a version is not a bound. `tests/unit/PackageReaderTest.cpp`
  already asserts the gate itself, so this invariant is about a duplicated
  BOUND rather than about the gate.
  **Reading `packageVersion` is expected and is not a breach**: § 4.6 and
  § 4.8 branch on version 63 for a field's presence, which is a different
  fact from the supported range. An earlier wording of this clause grepped
  for `packageVersion` and was falsified by the first conforming
  implementation, which reads it four times legitimately. The wording that
  replaced it was falsified in turn, on 2026-09-08, by the comment above --
  twice now this clause has been written tighter than the invariant it
  tests.
  *Breaks when:* a reader re-checks the version "to be safe", and the two
  bounds then drift the first time either moves — the failure being that
  the copy is invisible, since both agree on the day it is written.
  *Isolates:* the gate is UTA-0003's and passes with or without this
  invariant, so only a duplicated bound fails it.

- **INV-4** — No allocation and no span is sized by a count or length read
  from the file before that value has been checked against the bytes
  remaining in the export.
  *Test:* `tests/unit/PackageMalformedTest.cpp` builds a `Palette`
  declaring a count far larger than the file and asserts an `Error` whose
  message names the count check. `Model` is reached here too, by the
  lying-count cases INV-2 names. That the refusal precedes the allocation
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
| Object class not modelled by this item | `InvalidArgument` |
| Count or length exceeds the bytes present | `MalformedData` |
| Reader did not end at the export's end (§ 4.3) | `MalformedData` |
| `WidthOffset` / `NextOffset` disagrees with the payload (INV-5) | `MalformedData` |
| Export has no serialised data | empty result, not an error |

The last row follows UTA-0003's INV-7: a sizeless export is ordinary and
iterating a package must not error on one. For a modelled class that
means a default-constructed result — an empty `mips`, no polygons — and
never a refusal; § 4.1's struct comments say the same.

**A package version outside 61–69 is not in this table**, because it
never reaches a reader here: `Package::open` refuses it first (§ 4.2).

A short read is `MalformedData` and never `IoFailure`, because these
readers hold bytes rather than a file — the same rule, and the same
reasoning, as UTA-0003 § 4.5.

## 7. Tests

Three tiers, and only the third reads bytes this project did not write.

1. **`tests/unit/PackageContentTest.cpp`** — the well-formed cases, built
   by the fixture builder: each reader over each shape in § 4, the version
   gate, the two mip chains, the sparse actor array, the redundant-offset
   check, and the view-not-copy check — including the pre-63 `Texture` and
   `Sound` pair § 4.10 requires. Covers **INV-1** (fixture half, for every
   reader except `readModel`), **INV-5**, **INV-6**, **INV-7**, **INV-8**
   and **INV-9**. INV-3 is a grep rather than a case here. Label `unit;fast`, in the existing Catch2
   executable.

   **`readModel` has fixture cases here from 2026-09-08**, when UTA-0069
   derived its layout (§ 4.5): one reading its tables back at known counts,
   one placing its elements at the file's own indices, and one refused for
   bytes left over. They are built by helpers local to this file rather
   than by the shared builder, which still has no `Model` writer (§ 4.10).
   Tier 3 is no longer its only check.

2. **`tests/unit/PackageMalformedTest.cpp`** — truncations, lying
   counts, and a payload whose declared size runs past the export. Every
   case asserts an `Error` rather than a crash. Covers **INV-2** and
   **INV-4**.

3. **`tests/real/RealInstallTest.cpp`** — covers **INV-1** over content
   this project did not write. Extended to walk every package under
   `UTA_UT_INSTALL_DIR`. A package outside 61–69 does not open at all
   (§ 4.2), so the walk asserts `Package::open` refuses it with
   `UnsupportedVersion` — that is where the install's version 76, 79, 118
   and 128 packages land — and reads every export of a modelled class in
   the rest, asserting § 4.3 on each.

   **Refusals are bounded, not merely recorded.** An earlier draft of this
   section allowed any in-range refusal to be recorded rather than failed,
   which would have made the tier incapable of failing — and since tier 3
   is `readModel`'s only check, a `readModel` with a wrong field order
   would have refused every `Model` export and still gone green. So:

   - **Zero refusals are permitted** for `Polys`, `Model`, `Palette` and —
     from 2026-09-06, per UTA-0057 § 7 — `Level`. A refusal there fails the
     tier.
   - **Two shapes are permitted to be recorded rather than failed**, each
     proven per export. The property list itself does not parse, so the
     typed reader never reached its own layout — that layer is UTA-0003's.
     Or a redundant offset contradicts its own payload, which is INV-5
     doing its job on an export that disagrees with itself.
   - Anything else recorded is a **new** shape, and the tier fails until
     somebody decides which of the two above it is.
   - **And the recorded total is capped**, because a per-export exemption
     alone would hide the failure that matters: a systematic reader error —
     a wrong `serialOffset` term, a missed version branch — pushes *every*
     export into one of those shapes, and each still "proves" itself. The
     install is overwhelmingly readable, so a recorded total near the read
     total is a defect here rather than in 1999's content.

   A third shape was named before this item was built — an export whose
   property list consumes the whole export — and the conforming reader
   produced **none**. It was measured with a throwaway probe whose class
   resolution on the one file concerned turned out to be unsound, so it is
   withdrawn rather than kept as an unexercised guard. It **prints its own totals per
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
| INV-1 | `tests/unit/PackageContentTest.cpp` for fixtures, and `tests/real/RealInstallTest.cpp` over the reference install — the only check here that reads bytes this project did not write. Off by default, so an ordinary run proves agreement with our own fixtures only. `readModel` was in that position until UTA-0069 derived its layout on 2026-09-08; it now has fixture cases in both unit-tier files and is exercised on every ordinary run. `readLevel` left the same position when UTA-0057 derived its tail on 2026-09-06 |
| INV-2 | **Partial:** `tests/unit/PackageMalformedTest.cpp` and its assertions alone. No memory checker runs it — the only sanitizer leg is ThreadSanitizer — so an out-of-span read the corpus does not provoke is caught by nothing until an AddressSanitizer leg or a fuzzer exists. Same grade, and the same reason, as UTA-0003's INV-1. **It reaches every reader named here**: `readModel` joined when UTA-0069 derived its layout on 2026-09-08, and `readLevel`'s remainder left that category when UTA-0057 derived it |
| INV-3 | The grep in its *Test:* clause. The gate itself is UTA-0003's and `tests/unit/PackageReaderTest.cpp` covers it; this row is about a duplicated bound and nothing else. **Nothing runs that grep automatically** — it is not wired into `scripts/ci.sh`, so it is a check somebody performs rather than one the gate enforces |
| INV-4 | **Partial:** the test asserts the refusal *names the count check*, so deleting that check is detectable. That bounds which check refuses, not the ordering: a reader that reserved first and still produced this message would pass. Bounding the allocation needs a counting allocator no harness here has — the grade UTA-0003's INV-2 carries, for the same reason. Reaches `readModel` too, by INV-2's route |
| INV-5 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| INV-6 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| INV-7 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| INV-8 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| INV-9 | `tests/unit/PackageContentTest.cpp`, a Catch2 unit test |
| § 4.3 "consuming exactly is not a proof of correctness" | **nothing** — a reader that consumed the right byte count and assigned the fields wrongly passes every check in this spec. What would catch it is comparing a decoded value against the same value obtained another way, and nothing here does that for any field |
| § 4.5 "`Model`'s order is derived, not transcribed" | **Partial:** the real-asset tier proves the order consumes exactly across the install, which is strong evidence and not a proof, by the row above |
| § 4.9 "the post-`FURL` remainder is derived, not transcribed" | **Partial:** the real-asset tier proves it consumes exactly across the install, on the same footing and with the same limit as § 4.5's row above |
| § 4.6 "verified with known exceptions" | **Partial:** § 7 tier 3 fails on any refusal outside its two named shapes, and its cap catches a systematic error — but an individual new refusal matching a named shape is absorbed, and nothing distinguishes it from a regression |
| § 4.9 "`Location` and `Rotation` are already decoded" | `tests/unit/PackagePropertiesTest.cpp`, which UTA-0003 ships — this item adds no check of its own |
| § 3.3 item 3 "bulk payload is a view" | INV-7 covers the sound payload and mip pixels. Nothing checks that `Polys` vertices are not copied, and they are the one bulk field this spec does not require a view for |
| § 4.10 "the readers stay an independent implementation" | **nothing** — no check enforces that a reader and the fixture writer were not written from one reading. UTA-0003 § 2's argument, and the real-asset tier, are what stand in for it |

## 11. Cross-doc impact

- `CHANGELOG.md` — an `Added` entry under `[Unreleased]`.
- `CLAUDE.md` — the position lines only.
- `docs/design.md` — no change; this implements § The parts' `upkg`.
- `docs/specs/UTA-0003-package-container.md` — **changed after all.** Its
  § 9 already names this item as the owner of typed level content, and
  that stands; what did not was the expectation of no code change. Every
  reader here starts where the property list ends, and `readProperties`
  reported only the properties — so `upkg` gained `readPropertyList`,
  returning the list *and* the offset at which native data begins, with
  `readProperties` kept as a wrapper. Recomputing that offset per reader
  would have been a second decoder of the one format UTA-0003 owns.
- `README.md` — no change; the build invocation does not move.

## 12. Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-05 | 3, cold — genre pinned `spec`; packet carried the install census, the six code windows and the UTA-0003 passages this spec leans on | 2 | 2 | 2 | 2 | **Eight verified, eight fixed, none dismissed.** **All three lanes independently found the same three defects**, the strongest agreement available in a three-lane loop, and the first is the run's most consequential: § 4.1 named six return types and **declared none of them**, while the header names four items that bind to them and § 2 item 1 carries UTA-0003's argument that a vocabulary invented twice is two vocabularies. One lane worked the consequence out concretely — a `Polys` holding a resolved `std::string_view` would either fail to compile against a caller written to `ObjectReference`, or compile and hold a view outliving its `Package`. The six structs are now declared, and references are returned unresolved for exactly that lifetime reason. **The second was a contradiction that would have destroyed this item's only real oracle:** § 7's tier 3 said to walk *every* package in the install and assert § 4.3, while § 4.2 refuses anything outside 61–69 and § 2.1 records that versions 76, 79, 118 and 128 are present — so the tier goes red on a clean install, and the cheap repair (skip whatever errors) makes the real-asset half of INV-1 pass vacuously. Tier 3 now splits by the gate. **The third was mine and mechanical:** § 2.1 cited "§ 4.6 and § 4.7" for the two version-63 fields; § 4.7 is `Palette` and has no version branch at all, the second field being § 4.8's `NextOffset`. **Both Q4s were fixture gaps that left an invariant reading as covered:** § 4.10's builder list held no package below version 63, so INV-1's first named break mode — a missed version branch — was falsifiable only by a tier that is off by default; and it holds no `Model`, which § 4.5 withholds the layout for, so `readModel` had no unit-tier coverage while INV-1 claimed every reader. **A lane open question found the run's sharpest Q1, and it was concealed by my own packet:** the packet summarised the subclass measurements without the word *exact*, three lanes queried it, and re-running the probe showed `IceTexture` mismatching on a community map. The claim that the four classes "share the layout exactly" was broader than any measurement supports and is narrowed to the stock packages. **A second open question, raised by two lanes, became a Q3:** § 4.6 called `WidthOffset` an absolute file offset, which is true, and never said how a reader holding a span of the export compares against it — a reader that forgets the `serialOffset` term refuses every real texture. **Resolved clean, not a finding:** none of the six type names collides with anything already in `uta::upkg`. |
| 2 | 2026-09-05 | 3, cold — identical brief, packet rebuilt from disk and its subclass measurements corrected | 2 | 3 | 3 | 1 | **Nine verified, nine fixed, none dismissed. Cap reached (2 for a spec); the run ships.** **The run's most consequential finding was pre-existing and is a claim I made about code without opening it:** § 3.3 item 1 and § 4.2 had this item adding a package-version gate because `Package::open` "stays version-tolerant". It does not — `src/upkg/Package.cpp` refuses anything outside 61–69 before it reads a table, and UTA-0003's spec says so in four places, its own § 2.1 carrying the same version census this document re-derived. So the gate could never fire, INV-3 was unfalsifiable (no out-of-range `Package` can be constructed to hand a reader), and § 7's tier-3 split was built on it. The section is now an inheritance note, INV-3 is re-aimed at the range being stated once, and § 6 loses a row. **The second was a lane's open question in loop 1 that became a finding here, and it widens the item:** § 4.9 said the `FURL` is "read only far enough to satisfy § 4.3", and `Level` does not end at the `FURL` — tens of kilobytes follow it on a stock map. So `Model` is not the only reader with an underived layout, and § 4.9 now says so with § 4.3 as its acceptance. **Five of the nine landed on text loop 1 wrote, which is a high share and is recorded as such.** The worst was loop 1's own repair of the tier-3 walk: it allowed any in-range refusal to be *recorded rather than failed*, which made the tier incapable of failing — and since tier 3 is `readModel`'s only check, a wrong field order would have refused every `Model` export and still gone green. Two lanes found it independently. The allowance is now bounded to one named shape, with zero refusals permitted for `Polys`, `Model`, `Palette` and stock packages. All three lanes found loop 1's `mips` comment ("never empty on a modelled class") contradicting § 6's sizeless-export row. **Open questions settled by measurement, not argument:** the three mismatching `IceTexture` exports are in a community map rather than a stock package, so § 4.6's sentence held and gained the exception it was missing; and § 4.4's "every `Polys` export" was re-run across all 790 maps — 538388 of 538388 exact — so the claim is now true as written rather than measured over forty. **Shipping at the cap:** every finding is fixed and the tail is empty, but two of six readers have underived layouts and the collateral rate is high, so the next reviewer should be the build rather than a third cold read. |
| 2-impl | 2026-09-05 | **none — no reviewer was dispatched.** An implementation row, written by `write-spec` Step 8 rather than by a review loop | 1 | 1 | 1 | 1 | **Four clauses implementation proved false or incomplete; all four amended.** **[Q3] The readers had nowhere to start.** Every one begins where the property list ends, and `readProperties` returned only the properties — so `upkg` gained `readPropertyList`, reporting the list and the offset at which native data begins. § 11 had said UTA-0003 needed no change, and it did. **[Q4] INV-3's own test clause was falsified by the first conforming implementation.** It grepped the new sources for `packageVersion` and expected nothing; the reader reads it four times legitimately, because § 4.6 and § 4.8 branch on version 63 for a field's PRESENCE, which is a different fact from the supported range. The clause now greps for the range bounds. **[Q2] § 7 tier 3 named a refusal shape the reader never produced** — an export whose property list consumes the whole export. It came from a throwaway probe whose class resolution on the one file concerned proved unsound; the two shapes that DO occur are an unparseable property list and a redundant offset contradicting its payload. The unexercised guard was withdrawn rather than kept. **[Q1] The tier needed a cap, not only per-export exemptions.** A wrong `serialOffset` term would push every texture into a named shape and each would still prove itself; the cap is what separates broken content from a broken reader. **What the build confirmed rather than changed:** the four verified layouts read the reference install exactly — Polys 552,989, Palette 40,606, Texture family 50,386 and Sound 8,898 exports consumed to the byte, with 39 refusals across two community files. Each new assertion was proven able to fail by mutating the rule it covers. `Model` and `Level` past the actor array remain underived and unbuilt, exactly as § 4.5 and § 4.9 say. |

## 13. Resource cost

No new library and no new dependency; six readers added to `uta_upkg`.

- **Every reader is called per export and holds nothing between calls.**
  There is no cache and no global state.
- **The returned structures own their small fields and view their large
  ones** (§ 3.3 item 3), so reading a texture allocates its `Mip`
  descriptors and not its pixels, and a `Sound` allocates nothing at all.
  The largest allocation in this item is a `Polys`' per-polygon vertex
  vector, which § 4.1 makes a copy rather than a view and which is bounded
  by the export's own size.
- **The real-asset tier is the expensive part**, running six readers over
  every export of every package in an install. It is off by default and
  is not on the ordinary gate.

## 14. What was built (2026-09-05)

Four of the six readers § 4.1 names shipped: `readPolys`, `readPalette`,
`readTexture` and `readSound`, in `src/upkg/Geometry.*`, `Texture.*` and
`Sound.*`. `readModel` and `readLevel` did not — § 4.5 and § 4.9 explain
why, and **UTA-0057** carries them.

`upkg` gained one thing this spec did not anticipate: `readPropertyList`,
reporting the property list *and* the offset at which native data begins.
Every reader here starts there, and § 11 records that this made UTA-0003
change after all.

What the build proved that no reading had: the four layouts hold across
the whole reference install under § 4.3's rule, the version-63 branch is
taken by stock content rather than being an edge case, and INV-3's
original test clause was false. § 12's implementation row has the detail.

**What is checked, honestly.** The real-asset tier is off by default, so
an ordinary run proves the four readers against this project's own
fixtures and nothing else; the install run is what proves them against
1999. § 10 grades every invariant on that split, and three rows there say
`nothing`.

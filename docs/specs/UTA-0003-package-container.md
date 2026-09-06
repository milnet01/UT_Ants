# UTA-0003 — `upkg`: read the Unreal Engine 1 package container

**Status:** accepted (2026-09-04).
**Kind:** implement.
**Source:** ROADMAP UTA-0003 (design-2026-09-03).
**Blocker for:** UTA-0004, UTA-0005 — both read through this reader.

Unreal Tournament keeps its maps, textures, sounds and music in one kind
of file. This teaches the project to open one and work out what is inside
it — the list of names, the list of objects, and the settings each object
carries. Nothing is drawn yet.

## 1. Goal

`src/upkg/` exists and builds as `uta_upkg`, linking `uta_core` and
nothing else. Given the bytes of a `.unr`, `.utx`, `.uax`, `.umx` or `.u`
package it returns the header, the name, import and export tables, the
byte range of any export's serialised data, and the tagged property list
that data begins with — reporting every malformed input as an `Error`
rather than a crash. When this was written nothing in the repository could
read a package: `src/` held `core` alone. Section 14 records the build.

## 2. Problem

`docs/design.md` § The parts makes `upkg` responsible for "Reading Unreal
Engine 1 packages … Names, imports, exports, object serialisation, class
tables and default properties. Data in, structures out". None of it
exists, and four later items are blocked behind it — UTA-0004 (level
geometry, textures, sounds, actor placements), UTA-0005 (class tables and
ancestry), UTA-0011 (`ubake`) and UTA-0012 (`ut-dump`).

Three things make this worth a contract rather than a direct build.

1. **It is a format other code binds to.** The types this item names —
   an object reference, a name index, a property value — are what
   UTA-0004 and UTA-0005 are written against. Invented twice, they are
   two vocabularies.
2. **Its input is untrusted.** Design rule 15 quarantines everything
   derived from the player's install under `content/`, and `unet` is
   responsible for "content transfer against a fingerprint manifest", so
   a package can arrive from a server. Every count, offset and length in
   the file is attacker-controlled, and each one indexes or sizes
   something.
3. **The format's edges are not guessable.** An export's serial offset is
   absent when its size is zero; a boolean property's value lives in a
   tag bit and occupies no bytes; an object carrying an execution stack
   prefixes its property list with one. A reader that misses any of them
   silently mis-parses rather than failing.

`tests/support/UnrealPackageBuilder.h` already builds synthetic packages
and says why it ships an encoder and no decoder: the reader "writes its
own decode path, so the two are independent implementations of one format
and a misreading on either side shows up as a disagreement rather than
cancelling out". That property is preserved here.

### 2.1 What the reference install actually contains

The design's assumptions were checked against the Unreal Tournament
install on this machine rather than assumed. Two of them were wrong.

```sh
scripts/package-census.py "<unreal-install-directory>"
```

It reads the first eight bytes of each package — signature, package
version, licensee version — and copies nothing. Measured 2026-09-04 over
the install on this machine:

| Finding | Consequence |
|---|---|
| Versions 61–69 cover almost the whole library; a small tail sits at 76, 79, 118 and 128 | The version gate is real work, not a formality — the tail is UE2-era content that must be refused rather than mis-read |
| Packages below version 64 are a minority but not rare, and are mostly textures, music and sounds rather than maps | The pre-64 null-terminated name table is supported here (§4.5), not deferred |
| A few packages are very much larger than the median, and how much larger depends on which content is installed | Whole-file reading cannot be forced on the caller (§3.2) |

The figures themselves are deliberately not restated here: they are a
property of one install and differ on another. The script is what
reproduces them, which is why it is committed rather than quoted.

## 3. Scope decisions (agreed with the user)

### 3.1 The generic property reader is in scope — user, 2026-09-04

The roadmap bullet ends "and object serialisation", which reads two ways.
The user chose the wider one: this item ships the reader for the tagged
property list every Unreal object stores its settings in. UTA-0004 and
UTA-0005 both need it, and built inside either it would be built for one
caller.

What that does **not** include is interpreting a particular class's
properties, or resolving a struct's members — both need the class table,
which is UTA-0005. §4.7 draws the line and §9 records it.

### 3.2 The remaining calls are mine, with reasons

`spec-format.md` §3 asks who made each preference call. These were not put
to the user; each is reversible behind the API.

1. **`upkg` opens no files.** Every entry point takes a
   `std::span<const std::byte>`. The caller decides how the bytes arrive
   — `uta::fs::readFile` today, a memory map later. §2.1 measured how
   far package sizes spread, so a reader that forces whole-file reading
   makes that decision for every future caller; with a span it makes it
   for none. It also removes file I/O from every test.
2. **Versions 61–69 are accepted, including the pre-64 name table.** The
   branch is small and §2.1 measured the packages that need it. Refusing
   them would leave a known hole with a known size.
3. **The reader validates eagerly and decodes lazily.** The header and
   all three tables are read and checked when the package is opened; an
   export's property list is read only when asked for. Opening a package
   to list its contents is the common case, including `ut-dump`'s.
4. **A struct property this reader cannot name is returned as bytes, not
   refused.** Its layout needs UTA-0005, and the tag's size field is
   always sufficient to skip it. Refusing would make the reader useless
   until UTA-0005 lands.

## 4. Design

Casing follows `languages/cpp.md`, as `core` and
`tests/support/UnrealPackageBuilder.h` already do. Everything lives in
namespace `uta::upkg`. Failures are `uta::Result<T>` per
`docs/design.md` § What every part does the same way; `UTA_TRY` and
`UTA_CHECK` from `src/core/Error.h` are the propagation shape.

### 4.1 Layout and the build

```
src/upkg/CMakeLists.txt         the uta_upkg target
src/upkg/ByteReader.h  .cpp     the bounds-checked cursor
src/upkg/Package.h     .cpp     header, tables, lookups
src/upkg/Properties.h  .cpp     the tagged property list
```

`src/CMakeLists.txt` gains `add_subdirectory(upkg)`. `uta_upkg` is a
static library whose only link entry is `uta_core`; `src/core`'s
configure-time link assertion is copied for it, which is INV-13 — §1's
"linking `uta_core` and nothing else" — expressed where the build can
see it. It says nothing about design rule 2, which constrains what the
*runtime targets* link. `ut-ants` and `ut-ants-server` do not exist, so
rule 2 is unexpressed in the build until one does, and the link-closure
test it requires belongs to whichever item first builds one (§9).

### 4.2 The cursor — `ByteReader`

One type owns every bounds check. The alternative — checking at each call
site — is what the sibling Vestige engine's glTF loader did, and its
history records bounds-check fixes landing one call site at a time
afterwards.

```cpp
class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> bytes,
                        std::size_t position = 0) noexcept;

    [[nodiscard]] Result<std::uint8_t>  readU8();
    [[nodiscard]] Result<std::uint16_t> readU16();
    [[nodiscard]] Result<std::uint32_t> readU32();
    [[nodiscard]] Result<std::int32_t>  readI32();
    [[nodiscard]] Result<std::int64_t>  readI64();
    [[nodiscard]] Result<float>         readFloat();

    /// Unreal's compact index: a signed 32-bit value in one to five bytes.
    [[nodiscard]] Result<std::int32_t> readIndex();

    /// A view into the underlying span -- no copy, no allocation.
    [[nodiscard]] Result<std::span<const std::byte>> readBytes(std::size_t count);

    [[nodiscard]] Result<void> seek(std::size_t position);
    [[nodiscard]] Result<void> skip(std::size_t count);

    [[nodiscard]] std::size_t position() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;
};
```

Every reader returns `MalformedData` when fewer bytes remain than the
read needs, and never moves the cursor on failure — the cursor holds
bytes rather than a file (§3.2 item 1), so a short read is a malformed
package and not an I/O failure. That is the one code for a short read
anywhere in `upkg`, which §4.5, §6 and INV-9 all rely on. Multi-byte integers are
little-endian and are assembled byte by byte rather than by casting a
pointer, so an unaligned offset — which the format allows everywhere — is
not undefined behaviour. Floats are assembled the same way and
`std::bit_cast` to `float`.

### 4.3 The compact index

The first byte carries the sign in bit 7, a continuation flag in bit 6
and six value bits; each later byte carries a continuation flag in bit 7
and seven value bits. `tests/support/UnrealPackageBuilder.cpp`'s
`encodeCompactIndex` is the encoder for exactly this, and this decoder is
written from the format rather than from that function.

Two rules the encoder does not imply:

- **At most five bytes.** Six bits plus four times seven exceeds 32, so a
  fifth continuation byte cannot contribute. A sixth byte is
  `MalformedData`, never a longer value. Without the cap, a run of `0xFF`
  is an unbounded read.
- **Accumulate unsigned.** The final shift places bits at position 27, so
  accumulating into a signed 32-bit value overflows on a hostile input.
  The value is built in `std::uint32_t` and converted once at the end.

### 4.4 The header

```cpp
struct PackageHeader {
    std::uint16_t packageVersion = 0;
    std::uint16_t licenseeVersion = 0;
    std::uint32_t packageFlags = 0;
    std::array<std::byte, 16> guid{};   // zeroed below version 68
};
```

Read in order: a `std::uint32_t` signature that must equal `0x9E2A83C1`
(`MalformedData` otherwise), the two version words, the flags, then a
`std::uint32_t` count and a `std::uint32_t` offset for each of the name,
export and import tables, in that order — six words in all. Below version 68 a heritage count and offset follow and are
skipped; at 68 and above a 16-byte GUID and a generation list follow, and
the generation list is skipped after its count is validated.

`packageVersion` outside 61–69 inclusive is `UnsupportedVersion`, with
the version in the message. §2.1 measured why the upper bound matters.

Every count and offset is checked against the span before any table is
read: an offset must lie inside the span, and a count must not exceed the
bytes remaining after its offset divided by that entry's smallest
possible encoding. That second check is what stops a header claiming
millions of entries from causing a large allocation before a single byte
of table is read.

### 4.5 The three tables

```cpp
struct NameEntry  { std::string name; std::uint32_t flags = 0; };

struct ImportEntry {
    std::uint32_t classPackage = 0;   // name index
    std::uint32_t className = 0;      // name index
    ObjectReference outer;
    std::uint32_t objectName = 0;     // name index
};

struct ExportEntry {
    ObjectReference objectClass;
    ObjectReference super;
    ObjectReference outer;
    std::uint32_t objectName = 0;     // name index
    std::uint32_t objectFlags = 0;
    std::size_t serialOffset = 0;     // both zero when the object has no data
    std::size_t serialSize = 0;
};
```

**A name entry** is its length as a compact index, that many bytes
including a terminating null, then a `std::uint32_t` of flags. Below
version 64 the length prefix is absent and the string is null-terminated
instead. A declared length of zero, or one running past the span, is
`MalformedData`.

**An import entry** is three compact indices with a `std::int32_t` outer
reference third: class package, class name, outer, object name.

**An export entry** is: class (compact index), super (compact index),
outer (`std::int32_t`), object name (compact index), object flags
(`std::uint32_t`), serial size (compact index), and — **only when serial
size is greater than zero** — serial offset (compact index). Reading the
offset unconditionally desynchronises the whole table from the first
sizeless export onward.

Every name index in every entry is validated against the name count as
the table is read, so a `Package` that opened successfully cannot later
return an out-of-range index. Each export's serial range is validated the
same way: `serialOffset + serialSize` must lie within the span.

### 4.6 Object references

```cpp
enum class ObjectReferenceKind { Null, Export, Import };

class ObjectReference {
public:
    explicit ObjectReference(std::int32_t raw = 0) noexcept;
    [[nodiscard]] ObjectReferenceKind kind() const noexcept;
    /// Zero-based index into the relevant table. Meaningless when kind() is Null.
    [[nodiscard]] std::uint32_t index() const noexcept;
    [[nodiscard]] std::int32_t raw() const noexcept;
};
```

A positive value is the export at `value - 1`; a negative value is the
import at `-value - 1`; zero is null. Negate through a wider type —
`-static_cast<std::int64_t>(raw) - 1`, narrowed once — because `raw` can
be `INT32_MIN`, whose magnitude no `std::int32_t` holds; §4.3's
accumulator is widened for the same reason, and INV-1 covers any input
bytes. The off-by-one is the format's, and
it is why this is a type rather than a bare `std::int32_t`: subtracting
one at each call site is subtracting it in some of them.

`Package::open` validates every reference in both tables against the
table it names. `ObjectReference::index()` is therefore total for any
reference reachable from an opened package.

### 4.7 The package, and an object's bytes

```cpp
class Package {
public:
    [[nodiscard]] static Result<Package> open(std::span<const std::byte> bytes);

    [[nodiscard]] const PackageHeader& header() const noexcept;
    [[nodiscard]] std::span<const NameEntry>   names()   const noexcept;
    [[nodiscard]] std::span<const ImportEntry> imports() const noexcept;
    [[nodiscard]] std::span<const ExportEntry> exports() const noexcept;

    [[nodiscard]] Result<std::string_view> name(std::uint32_t index) const;
    /// "None" for a null reference; otherwise the referenced entry's name.
    [[nodiscard]] Result<std::string_view> objectName(ObjectReference ref) const;
    /// An empty span when the export has no serialised data.
    [[nodiscard]] Result<std::span<const std::byte>> serialBytes(const ExportEntry&) const;
};
```

`Package` holds the tables and a view of the caller's bytes; it copies no
package data. The caller must keep those bytes alive for the package's
lifetime, which the header states.

### 4.8 The tagged property list

An object's serialised data begins with a list of tagged properties,
terminated by the name `None`. Objects whose flags include
`HasStack` (`0x02000000`) prefix it with an execution-stack frame: two
object references written as compact indices, a `std::int64_t` probe
mask, a `std::int32_t` latent action, and — only when the first
reference is non-null — a compact-index offset. That frame is read and discarded; skipping it is
not optional, and it is common in real maps.

A tag is read in this order, and the order is not rearrangeable:

1. **Name index** (compact index). When it names `None`, the list ends.
2. **Info byte.** Bits 0–3 are the type; bits 4–6 are the size code; bit
   7 is the array flag, except for `Bool`, where it is the value.
3. **Struct name** (compact index), only when the type is `Struct`.
4. **Size**, from the size code: 0→1, 1→2, 2→4, 3→12, 4→16 bytes, and
   5, 6, 7 → a following `std::uint8_t`, `std::uint16_t` or
   `std::uint32_t`.
5. **Array index**, only when bit 7 is set and the type is not `Bool`.
   This is *not* a compact index: a first byte below `0x80` is the whole
   value; `(byte & 0xC0) == 0x80` means two bytes carrying 15 bits; and
   `(byte & 0xC0) == 0xC0` means four bytes carrying 30 bits. The marker
   bits occupy the leading byte and the value's high bits follow them, so
   these read most-significant byte first — the one place §4.2's
   little-endian rule does not apply.

```cpp
enum class PropertyType : std::uint8_t {
    Byte = 1, Int, Bool, Float, Object, Name, String, Class,
    Array, Struct, Vector, Rotator, Str, Map, FixedArray,
};

struct NameRef  { std::uint32_t index = 0; };
struct Vector3  { float x = 0, y = 0, z = 0; };
struct Rotator  { std::int32_t pitch = 0, yaw = 0, roll = 0; };

using PropertyValue = std::variant<std::monostate,       // the default; nothing read yet
                                   std::uint8_t,         // Byte
                                   std::int32_t,         // Int
                                   bool,                 // Bool
                                   float,                // Float
                                   ObjectReference,      // Object, Class
                                   NameRef,              // Name
                                   std::string,          // String, Str
                                   Vector3, Rotator,
                                   std::span<const std::byte>>;  // not decoded

struct Property {
    std::uint32_t nameIndex = 0;
    PropertyType type = PropertyType::Byte;
    std::uint32_t structNameIndex = 0;   // meaningful only when type is Struct
    std::uint32_t arrayIndex = 0;
    PropertyValue value;
};

/// Read the tagged property list an export's serialised data begins with,
/// skipping the execution-stack frame where the object carries one.
[[nodiscard]] Result<std::vector<Property>> readProperties(const Package&,
                                                           const ExportEntry&);
```

**A `Bool`'s value comes from the info byte and consumes no value
bytes.** It is still a value: a `Bool` property holds the `bool`
alternative, never `std::monostate`.

**Step 4 still runs for a `Bool`.** Only the *value* bytes are skipped,
never the size field itself: every `Bool` tag measured in real content
carries size code 5, so a trailing `std::uint8_t` is present and must be
consumed. Skipping it desynchronises every property after the first
`Bool`, in a list that still parses.

**Values decoded here:** `Byte`, `Int`, `Bool`, `Float`, `Object`,
`Class`, `Name`, `Str`, `String` (exactly `size` bytes), and the two
structs whose layout is fixed by the format — `Vector` and `Rotator`.

**Amended 2026-09-06, by measurement: a `Str`'s compact-index length counts
CHARACTERS, and a NEGATIVE length means those characters are 16 bits wide.**
This section previously said "that many bytes including the terminator",
which is right only for the positive case. UTA-0005's real-asset pass found
the other one: a map-vote mod's class defaults store their entries as UTF-16,
and every such value satisfies `declared size == 1 + 2 * magnitude`, which is
what confirms the reading rather than the shape merely looking plausible. The
reader had been refusing them as malformed, so an export carrying one could
not be read at all. Class defaults are where this form appears — UTA-0004 read
level content and never met it.

**Those two arrive spelled either way, and both decode identically.** A
tag may carry `Vector` or `Rotator` in its type field, or `Struct` with
that struct name. `structNameIndex` is meaningful only in the second
spelling, and is zero in the first.

**Amended 2026-09-06: the class table does not supply a struct's layout,
and UTA-0005 does not decode struct-typed values.** That item measured the
question and scoped it out — struct-typed defaults are a small share of all
defaults, and once `Vector` and `Rotator` are set aside what remains is
dominated by mod bookkeeping nothing in the 0.1.0 or 0.3.0 line consumes.
Recovering a struct's member layout means reading each class's `Children`
chain, which is a second traversal and belongs to whichever item first needs
those values. Until then the carry-through below is the whole answer, not a
placeholder for UTA-0005.

**Everything else is returned as its raw bytes**, with the type and
struct name intact: any other struct, and `Array`, `Map` and
`FixedArray`. This is not a gap to be filled later by guessing — a
struct's members are serialised without tags, so its layout is only
knowable from the class table (UTA-0005). The tag's size field is what
makes the undecoded case safe, and this is the whole reason the size is
read even for types whose width is fixed.

A class object does not begin with a property list at all, and its
export is recognised by a **null** class reference — not by one naming
`Class`, which no package writes. `readProperties` refuses such an
export with `InvalidArgument` rather than returning nonsense. Class
objects are UTA-0005.

An export with no serialised data has no list either: `readProperties`
returns an empty vector and succeeds.

### 4.9 The fixture builder grows

`tests/support/UnrealPackageBuilder` gains: import and export entries,
per-export serialised data, a tagged-property writer, an
execution-stack frame writer for the `HasStack` exports INV-12 needs,
pre-68 headers and pre-64 null-terminated name tables — it writes the
68-and-up shape unconditionally today — and the ability to write a header whose
counts and offsets deliberately disagree with the body — which is what
the malformed-input tests need and what a self-consistent builder cannot
produce. It remains an encoder with no decode path.

## 5. Invariants

- **INV-1** — No `upkg` entry point throws, terminates or reads outside
  its span, for any input bytes.
  *Test:* `tests/unit/PackageMalformedTest.cpp` drives a corpus of
  truncations and lying headers built by the fixture builder. Nothing
  else checks it. `scripts/ci.sh`'s one sanitizer leg is
  ThreadSanitizer, which finds races rather than out-of-span reads, and
  `CMakeLists.txt` records why AddressSanitizer is not offered. §10
  grades this invariant on that.
  *Breaks when:* a length or offset from the file is used before it is
  checked — the class §2 names as attacker-controlled.

- **INV-2** — No allocation is sized by a value read from the file
  without that value first being checked against the bytes actually
  present.
  *Test:* `tests/unit/PackageMalformedTest.cpp` opens a package whose
  header declares counts far larger than the file, and asserts an
  `Error`. That the error arrives *before* the allocation is not
  observable from outside the reader, so the test bounds the failure and
  not the ordering; §10 grades it on that.
  *Breaks when:* a table vector is reserved from the header's count
  before the count is validated — a four-byte edit then asks for
  gigabytes.

- **INV-3** — `ByteReader::readIndex` is the exact inverse of
  `uta::test::encodeCompactIndex` for every `std::int32_t`, including
  both extremes.
  *Test:* `tests/unit/CompactIndexTest.cpp`, over boundary values and a
  deterministic sample. The two implementations are independent, which
  is what makes agreement evidence.
  *Breaks when:* the decoder treats bit 7 of the first byte as
  continuation rather than sign, or accumulates into a signed value and
  overflows at the last shift.

- **INV-4** — A compact index is at most five bytes; a sixth
  continuation byte is `MalformedData`.
  *Test:* `tests/unit/CompactIndexTest.cpp` decodes a hand-written run of
  `0xFF` bytes and asserts the error.
  *Breaks when:* the decode loop terminates only on a clear continuation
  bit, so a run of `0xFF` reads to the end of the span.

- **INV-5** — A package whose signature is not `0x9E2A83C1` is
  `MalformedData`; one whose package version is outside 61–69 is
  `UnsupportedVersion`. Neither is read further.
  *Test:* `tests/unit/PackageReaderTest.cpp`, including a version-76
  header, which §2.1 found in the reference install.
  *Breaks when:* the version is checked after the tables are read, or
  only a lower bound is checked — the UE2-era tail then parses as
  garbage rather than being refused.

- **INV-6** — Every name index held by an opened `Package` is within its
  name table, and every object reference is within the table it names.
  Neither can be out of range after `open` succeeds.
  *Test:* `tests/unit/PackageReaderTest.cpp` builds a package whose
  export names an index past the end of the name table, and asserts
  `open` fails rather than a later lookup.
  *Breaks when:* validation is deferred to the accessors, so a caller
  that indexes directly into `exports()` gets an unchecked value.

- **INV-7** — An export's serial offset is read only when its serial size
  is greater than zero, and a sizeless export reports an empty span.
  *Test:* `tests/unit/PackageReaderTest.cpp` builds a package whose
  second export has zero size and whose third has data, and asserts the
  third's bytes are the ones written.
  *Breaks when:* the offset is read unconditionally — the table then
  desynchronises from the first sizeless export onward, and every entry
  after it is wrong while the file parses without error.

- **INV-8** — `serialBytes` returns a span inside the caller's bytes for
  every export of an opened package, and never a span that extends past
  them.
  *Test:* `tests/unit/PackageMalformedTest.cpp` builds an export whose
  offset and size overrun the file, and asserts `open` fails.
  *Breaks when:* the range is validated against the offset alone, so
  `offset + size` overflows or overruns.

- **INV-9** — A property list ends at the name `None` and at no other
  condition. Reaching the end of the export's serial bytes without one is
  `MalformedData`. An export with no serialised data has no list to end
  and is outside this invariant (§4.8).
  *Test:* `tests/unit/PackagePropertiesTest.cpp` writes a list with its
  terminator removed and asserts the error.
  *Breaks when:* the loop stops at the end of the buffer and returns what
  it has, so a truncated object reads as a complete one.

- **INV-10** — A `Bool` property takes its value from bit 7 of the info
  byte and consumes no value bytes; every other type consumes exactly the
  bytes its size field declares.
  *Test:* `tests/unit/PackagePropertiesTest.cpp` reads a list with a
  `Bool` between two known properties and asserts all three, which is
  what makes the cursor position observable.
  *Breaks when:* the `Bool`'s size is consumed — the property after it
  then reads the wrong bytes, and a list ending in `Bool` still parses.

- **INV-11** — A property whose type or struct this reader does not
  decode is returned with its type, struct name and raw bytes, and
  parsing continues.
  *Test:* `tests/unit/PackagePropertiesTest.cpp` writes a struct with an
  invented name between two decodable properties and asserts the third is
  read correctly.
  *Breaks when:* an unknown type aborts the list, or is skipped by a
  guessed width rather than its declared size.

- **INV-12** — An object carrying the `HasStack` flag has its execution
  stack frame skipped before its property list is read.
  *Test:* `tests/unit/PackagePropertiesTest.cpp` builds one export with
  the flag and one without, carrying identical property lists, and
  asserts both read the same.
  *Breaks when:* the frame is ignored — the first property name is then
  read out of the frame's bytes, which usually decodes as some other
  name rather than failing.

- **INV-13** — `uta_upkg`'s only link entry is `uta_core`.
  *Test:* `src/upkg/CMakeLists.txt`, a configure-time property assertion,
  as `src/core/CMakeLists.txt` already does for its own.
  *Breaks when:* a convenience dependency is added — which is how
  design rule 2's boundary stops being checkable.

## 6. Failure modes

- **The file is not a package.** Wrong signature, or shorter than a
  header: `MalformedData`, before anything is allocated.
- **The version is one we do not read.** `UnsupportedVersion` naming the
  version. §2.1 shows this fires on real content, not only on corruption.
- **A count or offset is a lie.** Caught at `open`, so no partially-built
  `Package` escapes. The design accepts that a package with one bad entry
  is refused whole; a partial package would push the same decision onto
  every caller.
- **A name is not valid UTF-8.** Names are stored as bytes and exposed as
  `std::string`; no transcoding is attempted. UT99 names are ASCII in
  practice, and a reader that rejected a stray high byte would refuse
  files the original game loads.
- **An export's data is truncated inside a property.** `MalformedData`
  from the cursor, which is the same path INV-9 covers.
- **A struct we cannot decode.** Not a failure: §4.8 returns its bytes.
  The failure would be inventing a layout for it.
- **The caller frees the bytes while a `Package` lives.** Undefined, and
  not defended against — the alternative is copying every package,
  including the largest. Stated in the header, and the reason §3.2
  item 1 keeps the choice with the caller.

## 7. Tests

New files join the existing `uta_unit_tests` executable in
`tests/CMakeLists.txt`, which discovers with
`catch_discover_tests(uta_unit_tests PROPERTIES LABELS "unit;fast" TIMEOUT 30)`.
The executable gains `uta_upkg` as a link library.

| File | Locks |
|---|---|
| `tests/unit/CompactIndexTest.cpp` (existing, extended) | INV-3, INV-4 |
| `tests/unit/PackageReaderTest.cpp` | INV-5, INV-6, INV-7 |
| `tests/unit/PackagePropertiesTest.cpp` | INV-9, INV-10, INV-11, INV-12 |
| `tests/unit/PackageMalformedTest.cpp` | INV-1, INV-2, INV-8 |

INV-13's surface is `src/upkg/CMakeLists.txt` rather than a test file;
§10 carries it.

Every one builds its own package bytes with
`uta::test::UnrealPackageBuilder`, so **S7** holds: the suite passes with
no Unreal Tournament present.

`tests/real/RealInstallTest.cpp` gains a second-tier case that opens
every package under `UTA_UT_INSTALL_DIR` and asserts each either opens or
returns `UnsupportedVersion`. It asserts no counts — §2.1's figures are
one install's, and a census here would go stale. This is the only test
that can catch the class §2.1 exists to name: an assumption that holds
against the fixtures and not against what shipped in 1999.

Each test is written before the code it locks and seen to fail — against
a missing symbol first, then against the failure it names, per
`languages/cpp.md` § Tests.

## 8. Alternatives considered (and rejected)

- **Take a file path and read the file.** Rejected: it forces whole-file
  reading on every caller, and §2.1 measured how large packages get. A
  span defers the choice at no cost.
- **Memory-map the file inside `upkg`.** Rejected for now, not on merit:
  it is platform code with no measurement behind it yet, and the span
  API is what lets it be added later as a caller's choice rather than a
  rewrite.
- **Refuse packages below version 64.** Rejected: §2.1 found real content
  there, mostly textures and music, and the branch is one alternative
  name-table read.
- **Decode struct properties by guessing common layouts.** Rejected: a
  guess that is wrong reads plausible numbers, so the failure is silent.
  UTA-0005 has the class table, which is the only thing that actually
  knows.
- **Throw on malformed input and catch at the boundary.** Rejected by
  `docs/design.md`: `std::expected` across every module boundary, and
  malformed input is the expected case here rather than an exception.
- **Give the fixture builder a decoder and round-trip it.** Rejected, and
  `UnrealPackageBuilder.h` already records why: two implementations that
  share assumptions cancel out each other's mistakes.
- **A third-party package library.** Rejected by ADR-0001 and by design
  rule 1's dependency posture; reading this format is the project's own
  competence.

## 9. Out of scope

- **Typed level content** — geometry, textures, sounds, actor
  placements. UTA-0004.
- **Class tables, default properties, struct layouts and ancestry across
  packages.** UTA-0005, which is what makes §4.8's undecoded cases
  decodable.
- **UnrealScript bytecode** in `.u` packages. No roadmap item; nothing
  needs it yet, and ADR-0004 resolves classes by ancestry rather than by
  running their code.
- **The link-closure test** of design rule 2, which needs `ut-ants` and
  `ut-ants-server` to exist. INV-13 covers `uta_upkg`'s own edge.
- **`ut-dump`**, the command-line inspector this reader makes possible.
  UTA-0012.
- **Writing packages.** Nothing in this project writes one; content goes
  out as `.utab` bundles (UTA-0008).

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | **Partial:** `tests/unit/PackageMalformedTest.cpp` and its assertions alone. No memory checker runs it — the only sanitizer leg is ThreadSanitizer — so an out-of-span read the corpus does not provoke is caught by nothing until an AddressSanitizer leg or a fuzzer exists |
| INV-2 | **Partial:** `tests/unit/PackageMalformedTest.cpp` asserts the refusal *names the count check*, so deleting that check is detectable — the test then fails on the message. That bounds which check refuses, not the allocation: a reader that reserved first and still produced this message would pass. Bounding the allocation needs a counting allocator no harness here has |
| INV-3 | `tests/unit/CompactIndexTest.cpp`, a Catch2 unit test |
| INV-4 | `tests/unit/CompactIndexTest.cpp`, a Catch2 unit test |
| INV-5 | `tests/unit/PackageReaderTest.cpp`, a Catch2 unit test |
| INV-6 | `tests/unit/PackageReaderTest.cpp`, a Catch2 unit test |
| INV-7 | `tests/unit/PackageReaderTest.cpp`, a Catch2 unit test |
| INV-8 | `tests/unit/PackageMalformedTest.cpp`, a Catch2 unit test |
| INV-9 | `tests/unit/PackagePropertiesTest.cpp`, a Catch2 unit test |
| INV-10 | `tests/unit/PackagePropertiesTest.cpp`, a Catch2 unit test |
| INV-11 | `tests/unit/PackagePropertiesTest.cpp`, a Catch2 unit test |
| INV-12 | `tests/unit/PackagePropertiesTest.cpp`, a Catch2 unit test |
| INV-13 | `src/upkg/CMakeLists.txt`, a configure-time property assertion |
| §4.8 "the two independent implementations must disagree visibly" | **Partial:** the real-asset tier is the only thing that reads bytes this project did not write. It now points `upkg` at every package in the configured install, and was run clean on 2026-09-04 — but it is off by default, so an ordinary run still proves agreement with ourselves |
| §4.5 "a name is exposed as bytes, not transcoded" | **nothing** — no test asserts a non-ASCII name survives; UT99 content is ASCII in practice and no fixture carries a counter-example |
| §6 "the caller must keep the bytes alive" | **nothing** — a lifetime rule a header states and no check enforces, and there is no AddressSanitizer leg that would catch a use-after-free |
| §3.2 item 3 "decode lazily" | **nothing** — nothing measures that opening a package does not read every export's data; it is visible in the code and not in a test |
| §4.4 pre-68 heritage header, §4.5 pre-64 name table | **Partial:** the builder now writes both older shapes and `tests/unit/PackageReaderTest.cpp` opens a version 62 package; the real-asset tier reads real content below 64. Still no invariant names either branch, so neither is gated by one |

## 11. Cross-doc impact

- `CHANGELOG.md` — an `Added` entry for `upkg` under `[Unreleased]`.
- `CLAUDE.md` — the position lines only.
- `docs/design.md` — no change. This spec implements § The parts'
  description of `upkg`.
- `README.md` — no change; the build invocation does not move.

## 12. Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-04 | 3, cold — genre pinned `spec`; packet carried a re-run install census and the build's sanitizer configuration | 2 | 2 | 4 | 0 | **Eight verified, eight fixed, plus one mechanical (`spec_lint` `missing_section`); two dismissed.** **All three lanes independently found the same Q1, and it is the run's most consequential:** INV-1's *Test:* clause named an address-sanitizer leg of `scripts/ci.sh`. There is none — `UTA_SANITIZE` accepts `""` or `"thread"`, and `CMakeLists.txt` records why AddressSanitizer was declined — and ThreadSanitizer finds races rather than out-of-span reads. So the bounds clause of the invariant this spec's untrusted-input argument rests on was checked by nothing while reading as gated. §10 now says so; whether CI should gain a memory checker is surfaced, not decided, being a build change. **All three also found the `PropertyValue` variant annotating both `std::monostate` and `bool` as the `Bool` case**, against INV-10 — a caller would have read every boolean as absent, silently. **Two lanes found the reason-code split:** §4.2 made a short read `IoFailure` where §4.5, §6 and INV-9 make the same event `MalformedData`, on the API UTA-0004, UTA-0005 and `ut-dump` branch on. **Two found `Vector` and `Rotator` falling between the decoded and undecoded lists** — declared as tag types, described only as `Struct`-named — which is what UTA-0004 reads actor placements off. **Two found "six `std::uint32_t` count/offset pairs" for three tables**, whose literal reading over-consumes the header and desynchronises everything after it. Single-lane: the execution-stack frame's two object references had no stated encoding in a document that uses both, and §4.9's builder list omitted the frame writer INV-12's test needs. **The stale-figure class cost three sections** — §2.1 declared its figures deliberately not restated and then restated them, and "roughly 400 MB" was measured this day at 95.7 MiB for the base install, the ~385 MiB belonging to a separate content pack. Figures removed rather than corrected. **Collateral, repaired in the neighbouring document:** `UTA-0002` carried the same 400 MB claim, which is where this one came from. **Dismissed as true but immaterial:** §10 attributes two quoted rules to sections not containing them, and §7 leaves the real-asset executable's `uta_upkg` link unstated — a link error settled on sight. |
| 2 | 2026-09-04 | 3, cold — identical brief, packet rebuilt from disk | 3 | 1 | 2 | 2 | **Eight verified, eight fixed. Cap reached (2 for a spec); the run files its tail and exits. A CALM cap** — one of the eight landed on text this run wrote, the rest were pre-existing, so the document held more defects than the cap held loops and shipping is right. The gate was armed by the commit that drafted this document whole, so all findings of both loops fall inside the gated span: this run was a gate, with no audit half to separate. **Two findings were settled by RUNNING a parser over real packages, and neither was decidable by reading.** §4.8 refused a class object by "an export whose class is `Class`" — no export in four packages across two versions names `Class`, because a class export carries a **null** class reference, so the guard never fired and `readProperties` would have parsed a class body as a property list, which is the nonsense the sentence existed to prevent. And §4.8 said a `Bool`'s size code "is ignored", leaving open whether step 4 runs: every `Bool` tag measured in real content carries size code 5, so a trailing size byte is present and skipping it desynchronises every property after the first `Bool` — invisibly, since the fixture writer would share the same wrong assumption, which is the cancellation §2 exists to prevent. **All three lanes found INV-2's test clause unfalsifiable:** "asserts an `Error` returns promptly" has no observable, a reserve-then-validate reader passes it, and §10 graded INV-2 unqualified beside a Partial INV-1. Now graded Partial with the reason. **Two lanes found INV-9 making every sizeless export an error**, against §4.7's empty span and INV-7 — ordinary iteration by UTA-0004 would have errored. Also fixed: §4.1 called the copied link assertion "design rule 2 expressed where the build can see it" while the same paragraph disclaimed it (it is INV-13; rule 2 constrains the runtime targets); the array index's byte order was unstated where §4.2 sets little-endian as the default, though only a most-significant-first read yields the 15- and 30-bit widths §4.8 itself states; `ObjectReference` negated a raw `INT32_MIN` in `std::int32_t`, undefined against INV-1's "any input bytes"; and the pre-68 header and pre-64 name-table branches had no invariant, no test and no §10 row. **Open questions resolved clean, none a finding:** `Result<void>` is documented in `Error.h` and already used; the fixture builder does emit the v68+ GUID and generation list; and both non-zero-licensee packages parse under the stated layout. |

## 13. Resource cost

`uta_upkg` is a new static library with no global state.

- **A `Package`** holds three vectors — names, imports, exports — and a
  view of the caller's bytes. It copies no package data. The name table
  is the only allocation proportional to the file, and it is bounded by
  the validated name count.
- **`readProperties`** returns one vector per call and retains nothing.
  Undecoded values are spans into the caller's bytes, so an object full
  of unknown structs costs no more than one full of known ones.
- **The caller owns the bytes**, so the peak cost of reading a package is
  the caller's choice, not this library's. §2.1 is why that matters.
- **No new external dependency.** Design rule 1 and INV-13.

## 14. What was built (2026-09-04)

An amendment recording the build, not a change of direction: nothing a
conformer would do differently, and the gate is not re-armed.

`src/upkg/` landed as specified — `ByteReader`, `Package`, `Properties`
and the `uta_upkg` target linking `uta_core` alone. The four test files
of §7 were written before the code they lock and seen to fail against
missing symbols first.

**Every invariant was then shown to fail for the reason it names.** Each
was broken on its own, one at a time, and the named test had to redden
and the suite return green once the break was undone; INV-13 was proved
by adding a second link entry and watching configure refuse. Breaking
the whole feature at once was avoided deliberately — identical failures
everywhere read as coverage while proving nothing about any part.

Two things that pass changed as a result.

- **INV-2's test did not check what it claimed.** Deleting the count
  check left it green: the read failed later anyway, one oversized
  reserve further on. The test now asserts the refusal names the count
  check, which makes the ordering observable from outside — §10's row is
  updated to say what that does and does not bound.
- **The real-asset tier found a damaged package.** Pointed at the
  reference install, `upkg` read every package but one; `Textures/M1.utx`
  is truncated, its header naming tables far beyond its own end. The
  tier does not tolerate failures in general — it re-reads the header
  itself, without going through `upkg`, and requires a file that failed
  to prove itself truncated.

That run also widens §2.1: the UE2-era tail is versions 76, 79, 118 and
128, and the pre-64 branch is exercised by real content rather than by
fixtures alone.

**Still unchecked, and unchanged by this build:** INV-1's out-of-span
clause. The only sanitizer leg is ThreadSanitizer. Whether `scripts/ci.sh`
gains a memory-checker leg or a fuzzer is a build decision open for the
user; it does not block anything already shipped.

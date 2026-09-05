# UTA-0005 — `upkg`: class tables, default properties and ancestry

**Status:** accepted (2026-09-05).
**Kind:** implement.
**Source:** ROADMAP UTA-0005 (design-2026-09-03).
**Blocked by:** UTA-0003 — every reader here reads through that container,
and reuses its property-list reader.
**Blocker for:** UTA-0023, UTA-0024, UTA-0029 — resolving a custom actor
onto one of ours, the stock bestiary, and the Monster Hunt rules all read
what this item returns.

Read a class out of a package: what it descends from, and the values its
author set on it. Walk that ancestry across packages, so a monster defined
in one file can be followed to a base class in another. No compiled code is
executed.

## 1. Goal

`ADR-0004` decided that this engine understands a custom actor by its
ancestry and its default properties rather than by running its
UnrealScript. This item is the half that reads. It returns, for any class
export: its parent, its own default properties, and — given a way to open
other packages — the whole chain up to `Object` with the defaults merged
along it.

It resolves nothing onto this engine's own classes. That is UTA-0023.

## 2. Problem

A `.u` package holds compiled bytecode and a class table. `ADR-0004` rests
on the class table being readable without a virtual machine, and it is.
But the naive reading of it is wrong in ways that mostly do not announce
themselves: they produce a plausible answer rather than a failure.

Every layout claim in § 4 was checked by decoding the reference install
with a throwaway probe written independently of `upkg` — a different
language, from the format rather than from this project's code — as
UTA-0004 § 2.1 did. The install is the one `UTA_UT_INSTALL_DIR` points at.

### 2.1 What the reference install actually contains

The probe read every class export in the install: the `System` tree's
packages, and the class exports embedded in maps, texture and sound
packages. Three findings changed the design, and one correction is
recorded below them because the method is the item's only oracle.

**A class's parent is recorded twice, and the container has already
validated one of them.** Every export entry carries a `super` column, and
each class also serialises a `SuperField` of its own. They agree on every
class export in the install. `Package::open` validates the table column
(UTA-0003 § 4.6), so a reader taking the parent from there inherits that
guarantee and validates nothing itself. § 4.5.

**`ScriptSize` is a memory size, not a file size.** A class's compiled
script is serialised expression by expression, and an object reference or
a name inside it occupies a compact index on disk but a wider fixed field
in memory. `ScriptSize` counts the memory form, so a reader that skips
that many bytes lands in the wrong place — usually inside the default
properties, which then parse as nonsense. There is no stored on-disk
length: if there were, the classes that carry no script would not consume
exactly, and they do. § 4.4.

**Most classes carry no script, and the ones that do are the ones that
matter.** Only about a twentieth of class exports have a non-empty script
of their own — a `replication` block. But they include `Actor`, `Pawn`,
`PlayerPawn`, `Inventory`, `Weapon`, `LevelInfo` and `ZoneInfo`: the base
classes every monster inherits from, and where the inherited numbers live.
Re-derive the split with `scripts/class-census.py`.

**The exact-consumption rule cannot recover the script length on its
own.** UTA-0004 § 4.3 made "a reader ends exactly where its export ends"
the acceptance test, and it is still the acceptance test here. It is not
sufficient to *find* the script's end by search: for `Actor`, dozens of
different assumed script lengths each leave a property list that parses
and terminates exactly at the export end. The script has to be walked.
§ 8 records that as the rejected alternative it is.

**A probe that disagrees with the shipped reader is evidence about the
probe.** An earlier draft of this section claimed the export table's
`super` column was out of range on some community packages and that the
object's own field had to be preferred. It was the probe: it accumulated
the format's compact index without narrowing to 32 bits, where
`ByteReader::readIndex` narrows once at the end (UTA-0003 § 4.3), so a
five-byte encoding with bit 31 set read as a large positive rather than a
small negative. Re-run with the narrowing, no class export in the install
has an out-of-range parent in either place. The claim reached a draft and
was caught by this document's review gate; it is recorded because the
probe is what every other claim in this section rests on.

## 3. Scope decisions (agreed with the user)

### 3.1 The script skipper is in scope — user, 2026-09-05

Reaching the default properties requires stepping over the compiled
script, which requires knowing the operand shape of every instruction. The
user was given the choice of building that inside this item, splitting it
into its own roadmap item, or shipping without it and reading only the
classes that carry no script. The decision was to build it here, on the
ground that this item cannot return correct numbers for any monster
without it: a class's defaults are stored as a difference from its
parent's, and the parents all carry scripts.

**Nothing is executed.** The walker reads each instruction's operands to
learn its length and discards them. It does not evaluate, branch, or call.
`ADR-0004`'s "no bytecode is executed, and no UnrealScript VM is written"
holds: this is a length calculation, and the item ships no interpreter,
no stack and no native function surface.

### 3.2 Struct-typed default values stay undecoded — mine, with reasons

UTA-0003 § 4.8 carries any struct it does not name through as raw bytes
with its type and struct name intact, and says a struct's layout is
knowable only from the class table, which is this item. This item does
**not** decode them, and § 11 records the correction that sentence needs.

The reason is measured. Struct-typed values are a small share of all
default properties, and once `Vector` and `Rotator` — which UTA-0003
already decodes, in both spellings — are set aside, what remains is
dominated by mod bookkeeping: map-vote lists, mute lists, admin stacks.
None of it is what `ADR-0004` reads. The properties that decide how a
monster looks and fights are objects, floats, bytes, bools, ints and
names, and all six are already decoded.

Decoding the rest means reading each class's `Children` chain to recover
member layouts — a second traversal, for values nothing in the 0.1.0 or
0.3.0 line consumes. It is filed rather than built.

### 3.3 The remaining calls are mine, with reasons

1. **Opening other packages is the caller's job, injected.** This item
   takes a resolver rather than reaching for the filesystem. A search path
   is a property of an installation, not of a package format; `ut-dump`
   (UTA-0012) and the baker (UTA-0011) will want different ones, and the
   tests want one backed by fixtures and no disk at all.
2. **A merged default carries its name as text.** Name and object indices
   are per-package, so a value read from `Engine.u` and a value read from
   a mod package cannot be compared by index. § 4.7.
3. **Content the walk cannot reach is not an error.** The walk stops and
   says what it wanted, distinguishing a package the resolver has not got
   from a class absent within one it has. `ADR-0004` requires the fallback
   to be legible, and an install that lacks a mod is the ordinary case,
   not a malformed one.

## 4. Design

### 4.1 Layout and the build

Two new pairs in `src/upkg/`, added to the existing `uta_upkg` target:

```
src/upkg/Script.h  .cpp   the compiled-script walker
src/upkg/Class.h   .cpp   class reading, ancestry, merged defaults
```

**`Properties` gains one entry point, and § 4.3 step 11 says why it must.**
That is a change to UTA-0003's surface, recorded in § 11.

The link closure does not move: `uta_upkg` still links `uta_core` and
nothing else, which `src/upkg/CMakeLists.txt` already asserts at configure
time for UTA-0003's INV-13.

Entry points take the opened `Package` and one `ExportEntry` and return
`Result<T>`, the shape UTA-0003 and UTA-0004 use:

```cpp
struct ClassInfo {
    ObjectReference super;                  // the parent; null on Object
    std::uint32_t   friendlyName = 0;       // name index
    std::uint32_t   classFlags   = 0;
    std::array<std::byte, 16> classGuid{};
    ObjectReference within;                 // version 62 and above
    std::uint32_t   configName   = 0;       // name index, version 62 and above
    std::vector<Property> defaults;         // this class's OWN defaults
};

[[nodiscard]] Result<ClassInfo> readClass(const Package&, const ExportEntry&);
```

`defaults` is a difference against the parent, not the effective set —
that is what the file holds, and § 4.7 is what combines them.

### 4.2 A class export is recognised by a null class reference

UTA-0003 § 4.8 established this and `readProperties` already refuses such
an export. `readClass` is the mirror: given an export whose class
reference is **not** null, it returns `InvalidArgument`. Given one with no
serialised data it returns `InvalidArgument` too — a class with no bytes
has no parent to report, and returning an empty `ClassInfo` would hand the
caller a null parent indistinguishable from `Object`'s.

### 4.3 The class layout

Read in this order from the start of the export's serialised bytes:

1. **The execution-stack frame**, only when the export's flags include
   `HasStack` (`0x02000000`), read and discarded exactly as UTA-0003
   § 4.8 describes it.
2. **`SuperField`** and **`Next`**, object references as compact indices.
3. **`ScriptText`** and **`Children`**, object references as compact
   indices.
4. **`FriendlyName`** (name index), then `Line` and `TextPos`, two
   `std::int32_t`.
5. **`ScriptSize`**, a `std::int32_t`, then the script — § 4.4.
6. **`ProbeMask`** and **`IgnoreMask`**, two `std::int64_t`; then
   `LabelTableOffset`, a 16-bit field; then `StateFlags`, a
   `std::int32_t`. All four are consumed and none is interpreted, so the
   16-bit field's signedness does not reach the reader and `ByteReader`
   needs no new accessor for it.
7. **`ClassFlags`**, a `std::uint32_t`, then a 16-byte GUID.
8. **The dependency list**: a compact-index count, then that many entries
   of an object reference **as a compact index**, a `std::int32_t` and a
   `std::uint32_t`.
9. **The package-import list**: a compact-index count, then that many name
   indices, each a compact index.
10. **`ClassWithin`**, an object reference as a compact index, and
    **`ClassConfigName`**, a name index as a compact index — both only at
    package version 62 and above.
11. **The default properties**, a tagged property list in exactly the form
    UTA-0003 § 4.8 reads, terminated by `None`.

Steps 6 and 7 are the state and class fields the format inherits through
its own class hierarchy; they are listed flat here because that is the
order the bytes arrive in, and a reader has no use for the hierarchy.

**Step 11 cannot use either property entry point `upkg` already has.**
`readProperties` and `readPropertyList` both take an `ExportEntry`, start
at the beginning of its serialised bytes and skip an execution-stack
frame — because for every object UTA-0003 and UTA-0004 read, the list is
the *first* thing in the export. A class's list is the *last* thing, and
by then the cursor is ten fields and a compiled script downstream.

`Properties` therefore gains an entry point that reads a list from a
cursor the caller already holds:

```cpp
/// Read a tagged property list at the cursor's current position.
/// The list's extent is bounded by the reader's own span, so a caller
/// that built one over a single export cannot read beyond it.
[[nodiscard]] Result<std::vector<Property>> readPropertiesAt(const Package&,
                                                             ByteReader&);
```

`readProperties` and `readPropertyList` become callers of it. That is the
point: `Properties.h` already warns, in its own comment on `PropertyList`, that a
second path would mean a second decoder of the one format that file owns.
This item must not become one. INV-12 is that rule.

### 4.4 The compiled script is walked, never skipped by `ScriptSize`

The walker reads one instruction at a time. Each instruction contributes a
number of **memory** bytes, and the walk continues until that running
total reaches `ScriptSize`. The file position advances by the disk
encoding, which is shorter.

**The total must land on `ScriptSize` exactly.** A walk whose running total
steps *past* it has mis-read an instruction's width and stopped
mid-instruction, so it returns `MalformedData`. Nothing downstream catches
this: § 2.1's fourth finding is that a wrong script end still leaves a
property list that parses and ends where it should, so the acceptance rule
in § 4.8 cannot see it.

**`ScriptSize` stays signed all the way in, and a negative value is
`MalformedData`** — refused by `skipScript` as its first act, before the
walk begins. Narrowing it to an unsigned type at the call site is what this
forbids: `-1` then arrives as four billion and is caught, if at all, by the
export bound rather than by the rule that means to catch it.

```cpp
/// Advance the cursor past a compiled script. Executes nothing: each
/// instruction's operands are read only to learn its length.
[[nodiscard]] Result<void> skipScript(const Package&, ByteReader&,
                                      std::int32_t scriptSize);
```

Three rules make it safe.

**An instruction's memory cost is one byte for the opcode itself, plus
the memory widths of its operands.** The opcode byte is easy to leave out
and nothing else in the walk restores it: an instruction carrying one
object reference costs five, not four, so a walker that charges operands
alone reaches 12 where the file says 15.

**Those widths are fixed constants, not `sizeof`.** An object reference
counts as four memory bytes and so does a name, because `ScriptSize` was
computed by a 32-bit compiler in 1999. Writing `sizeof(void*)` gives four
on a 32-bit host and eight on every host this project actually builds on.
The constants are named and commented where they are defined.

**An unrecognised instruction is `MalformedData`.** The walker never
guesses a length, and never resynchronises by scanning.

**The walk is bounded by the export.** A walk that would read past the
export's end returns `MalformedData` before reading, so a corrupt
`ScriptSize` cannot drive an unbounded read.

Instructions divide into three groups: a fixed table of primary opcodes
with known operands; extended-native calls, whose second byte completes an
index and whose parameters run to an end-of-parameters marker; and native
calls, whose parameters run to the same marker. Nested expressions recurse,
so the walker is bounded in depth as well — a depth cap returns
`MalformedData` rather than exhausting the stack on hostile input.

### 4.5 The parent comes from the export table, which is already validated

`readClass` reports the parent from `ExportEntry::super`, not from the
`SuperField` inside the object's bytes.

The reason is that the container has already done the work. UTA-0003
§ 4.6 has `Package::open` validate every reference in both tables against
the table it names, so for any package that opened, that column is in
range — and `readClass` performs no reference validation of its own. The
in-data `SuperField` carries no such guarantee: nothing has read it, so a
reader taking the parent from there owes a bounds check the container
already paid for.

The `SuperField` is still **consumed** — § 4.3 step 2 has to read past it
to reach the fields after it — and is not used. § 2.1 records that the two
agree on every class export in the reference install, and how a first
probe made it look otherwise.

### 4.6 Resolving a parent in another package

A parent reference that names an import is a class in another package. The
import entry gives the class's name; the package it lives in is found by
following the import's outer chain to its root.

```cpp
/// Open a package by name, or report that it is not available.
/// Returns nullptr, with no error, when the package simply is not present.
using PackageResolver =
    std::function<Result<const Package*>(std::string_view packageName)>;

struct ResolvedClass {
    const Package*     package = nullptr;
    const ExportEntry* entry   = nullptr;
};

enum class AncestryEnd { Root, PackageMissing, ClassMissing };

struct Ancestry {
    std::vector<ResolvedClass> chain;   // the class itself first, root last
    AncestryEnd end = AncestryEnd::Root;
    std::string missingPackage;         // set on PackageMissing only
    std::string missingClass;           // set on PackageMissing and ClassMissing
};

[[nodiscard]] Result<Ancestry> readAncestry(const Package&, const ExportEntry&,
                                            const PackageResolver&);
```

**`readAncestry` folds the name before it calls the resolver, and a
resolver may assume folded input.** The engine's own package and class
names are case-insensitive, and the resolvers are written by other items —
§ 3.3 item 1 names two, plus the tests' own. Leaving the obligation
unstated is how one side folds and the other does an exact filesystem
lookup, so `botpack` misses `BotPack.u` on Linux and the walk ends
`PackageMissing` — reporting a mod as absent when it is installed, which is
the failure INV-7 exists to prevent.

**The resolver owns the lifetime of what it returns.** `Ancestry` holds
pointers into packages the resolver keeps alive, the same bargain
`Package` already makes with the caller's bytes.

**A cycle is `MalformedData`.** A well-formed chain terminates at a class
with no parent. The walk carries a depth cap and refuses beyond it. The
number is the implementation's, but it has a floor: the deepest chain in
the reference install is 12, so any cap comfortably above that makes a cap
that fires evidence of a malformed package rather than of an unusually deep
hierarchy. `scripts/class-census.py` re-derives the depth.

**Two ends are successful, and they are different facts.**
`PackageMissing` is the resolver declining to supply a package: both
`missingPackage` and `missingClass` are set, and a caller can say which
content the install lacks. `ClassMissing` is a package that opened and does
not contain the class: `missingClass` is set and `missingPackage` is
**empty**, because naming a package that is present would report the
opposite of what happened. `ADR-0004` requires exactly that legibility, and
one state covering both cases would defeat it.

### 4.7 Effective defaults are a merge up the chain

A class's stored defaults are a difference against its parent's. The
effective set is built by walking the chain from the root down, each class
overriding what it names.

```cpp
struct EffectiveProperty {
    std::string    name;        // resolved text, so it compares across packages
    const Package* origin;      // the package whose name and object
                                // indices this value is relative to
    Property       property;
};

[[nodiscard]] Result<std::vector<EffectiveProperty>>
effectiveDefaults(const Ancestry&);
```

**The merge key is the property's name as text, compared
case-insensitively, together with its array index** — never the name
index. A name index is a position in one package's name table; the same
property is a different number in every package, so merging by index
silently fails to override anything across a package boundary, and the
caller gets the base class's value. The case rule is the same one § 4.6
applies to package and class names, and for the same reason: the engine's
own names are case-insensitive, so a child spelling a property differently
from its parent must still override it rather than adding a second entry.

**`name` carries the spelling of the class nearest the leaf that set the
value**, since that is the one an author last wrote. Callers compare it
case-insensitively, as the merge does; the field is not normalised, because
a normalised name is not a name anyone would recognise in a log.

**`origin` is not optional bookkeeping.** A `Property` whose value is an
object reference is meaningful only against the package it was read from.
Dropping `origin` would produce a merged set whose object-valued entries
cannot be resolved at all.

An ancestry that ended `PackageMissing` or `ClassMissing` still merges,
over the part of the chain that resolved. The result is honest but incomplete, and the caller
knows which case it is from `Ancestry::end`.

### 4.8 Every reader here ends exactly where its export ends

UTA-0004 § 4.3's rule is inherited unchanged and is again this item's
acceptance test:

> A typed reader that does not end exactly at the end of its export
> returns `MalformedData`. It never returns a partial result.

It is what makes § 4.3's layout and § 4.4's walker checkable against
content this project did not write. It is not a proof of correctness — a
reader can consume the right bytes and assign them to the wrong fields —
and § 10 grades it on that.

### 4.9 The fixture builder grows

`tests/support/UnrealPackageBuilder.h` gains the ability to write a class
export: the § 4.3 field order, a caller-supplied script body, and a
default property list. It must be able to write a class whose parent is an
import, so cross-package resolution is testable without the reference
install.

**The invariants need it to write things a self-consistent package would
not contain**, and each is named because a builder that derives them from
one another cannot produce the fixture at all:

- the in-data `SuperField` **independently of** the export-table `super`
  column, so INV-2's fixture can make the two differ;
- a negative `ScriptSize`, an unknown opcode, and a script whose
  instructions cannot sum to `ScriptSize` exactly — INV-3's three fixtures;
- a property name at a **chosen** index in the name table, and in a chosen
  case, and property tags carrying a chosen array index — INV-8's three;
- an ancestry cycle and a chain past the depth cap — INV-6's.

## 5. Invariants

**INV-1.** Every class export in the reference install is consumed
exactly: the reader finishes at `serialOffset + serialSize`, for every
class export in every readable package.
*Test:* `tests/real/RealInstallTest.cpp`, the real-asset tier.

**INV-2.** `readClass` reports the parent from `ExportEntry::super`, and
performs no reference validation of its own.
*Test:* `tests/unit/PackageClassTest.cpp` — a fixture whose in-data
`SuperField` is out of range while its export-table `super` column is
valid. The package opens, because UTA-0003 validates the tables and not an
object's bytes; `readClass` returns the table's parent and does not fail.
A reader taking the in-data field fails or returns garbage, so the fixture
isolates this rule and no other.

**INV-3.** `skipScript` returns `MalformedData` on an unrecognised opcode,
on a walk that overshoots `ScriptSize`, and on a negative `ScriptSize`.
*Test:* `tests/unit/PackageScriptTest.cpp` — three fixtures, one per clause:
an opcode outside the table; a script whose instructions cannot sum to
`ScriptSize` exactly; and a negative `ScriptSize`.

**INV-3a.** `skipScript` executes nothing.
*Test:* a reading check over `src/upkg/Script.cpp` — the walker takes no
interpreter state, holds no stack, and calls nothing outside `ByteReader`
and the name table. There is no output to paste, and it is declared as a
reading check for INV-12's reason: this is `ADR-0004`'s load-bearing
promise, and no fixture can demonstrate the absence of an interpreter.

**INV-4.** `skipScript` computes memory widths from fixed constants, so
its result does not depend on the host's pointer size.
*Test:* `tests/unit/PackageScriptTest.cpp` — one instruction carrying an
object reference, with `ScriptSize` 5, walks to a literal disk position and
succeeds, on every platform in the matrix. A walker using `sizeof(void*)`
charges nine on the 64-bit hosts this project builds on, steps past 5, and
is refused by § 4.4's exactness rule — so the fixture fails on the defect
rather than landing somewhere else. Asserting the literal position as well
as success is what catches a wrong width that happens to sum to
`ScriptSize` anyway.

**INV-5.** No reader reads outside its export's byte range, for any input
bytes.
*Test:* `tests/unit/PackageMalformedTest.cpp` — a class whose `ScriptSize`
exceeds its export returns `MalformedData`.

**INV-6.** `readAncestry` terminates on any input: a cycle returns
`MalformedData`, and a chain beyond the depth cap returns `MalformedData`.
*Test:* `tests/unit/PackageAncestryTest.cpp` — a two-class cycle, and a
chain past the cap.

**INV-7.** A parent the walk cannot reach ends it successfully, in the
state that says which fact was true: `PackageMissing` when the resolver
declines to supply the package, `ClassMissing` when a package opened and
does not hold the class. Neither is an error, and `missingPackage` is
empty in the second.
*Test:* `tests/unit/PackageAncestryTest.cpp`, three fixtures — a resolver
that supplies nothing; a resolver that supplies a package from which the
parent class is absent; and an import naming its package in a different
case from the resolver's own key, which must resolve rather than end
`PackageMissing`. The first two cannot be collapsed into one, because a
reader that treats both ends alike passes whichever is written alone; the
third is the only surface the folding rule has.

**INV-8.** `effectiveDefaults` merges on the property's name as text,
case-insensitively, and on its array index — so a child in one package
overrides a parent in another, and two elements of one property do not
collapse into each other.
*Test:* `tests/unit/PackageAncestryTest.cpp`, three fixtures, because the
invariant states three rules and one fixture isolates one of them: two
packages placing the same property name at different name-table indices;
a child spelling the name in a different case from its parent; and a parent
and child setting *different array indices* of one property, where both
must survive the merge.

**INV-9.** Every `EffectiveProperty` carries the package its value's
indices are relative to.
*Test:* `tests/unit/PackageAncestryTest.cpp` — a merged set whose entries
come from two packages; each `origin` is the package that supplied it.

**INV-10.** `readClass` returns `InvalidArgument` for an export whose
class reference is not null, and for one with no serialised data.
*Test:* `tests/unit/PackageClassTest.cpp`.

**INV-11.** `uta_upkg` gains no link dependency.
*Test:* the configure-time assertion already in `src/upkg/CMakeLists.txt`.

**INV-12.** `upkg` holds one tagged-property-list decoder, not two:
`readProperties` and `readPropertyList` are implemented in terms of
`readPropertiesAt`.
*Test:* a reading check over `src/upkg/Properties.cpp` — the tag-decoding
loop appears once, and the two older entry points call the new one. There
is no output to paste, and an equivalence test between the entry points is
deliberately not the surface: two decoders that agree would pass it.

## 6. Failure modes

| What happens | What the reader does |
|---|---|
| Export's class reference is not null | `InvalidArgument` (INV-10) |
| Unknown opcode in a script | `MalformedData` (INV-3) |
| `ScriptSize` runs past the export | `MalformedData` (INV-5) |
| Script walk ends past the export | `MalformedData` (§ 4.8) |
| Property list does not end at the export end | `MalformedData` (§ 4.8) |
| Parent's package not available | `PackageMissing`, walk ends, no error (INV-7) |
| Parent's class absent from a package that opened | `ClassMissing`, walk ends, no error (INV-7) |
| Script walk overshoots `ScriptSize` | `MalformedData` (INV-3) |
| `ScriptSize` is negative | `MalformedData` (INV-3) |
| Ancestry cycle, or chain past the depth cap | `MalformedData` (INV-6) |
| Resolver itself fails | that error, propagated unchanged |

## 7. Tests

**Unit, on fixtures, no install required.** Four new files, each built
into the existing Catch2 unit executable with the `unit;fast` label:
`PackageClassTest.cpp`, `PackageScriptTest.cpp`, `PackageAncestryTest.cpp`,
and additions to `PackageMalformedTest.cpp`. Between them they cover
INV-2, INV-3, INV-4, INV-5, INV-6, INV-7, INV-8, INV-9 and INV-10. Every
fixture is written by the § 4.9 builder, so no Epic content enters the
repository — `ADR-0003` and UTA-0013's guard.

**INV-3a and INV-12 are reading checks rather than tests**, each for the
reason stated with it: no fixture can show that an interpreter is absent,
and none can show that a decoder exists only once.

**INV-11 has no test file.** Its surface is the configure-time assertion
already in `src/upkg/CMakeLists.txt`, which fails the build rather than a
test, and adding a test beside it would check the assertion rather than the
link closure.

**`PackagePropertiesTest.cpp` gains the § 4.3 step 11 entry point**, so the
existing property tests and the new one exercise the same decoder.

**One branch of § 4.3 is fixture-only, and it is worth saying so.** Every
class export in the reference install sits at package version 68 or 69 —
counted across its `.u`, `.unr`, `.utx`, `.uax` and `.umx` packages alike,
not the `.u` files alone, and re-derivable with `scripts/class-census.py`.
So step 10's *version 62 and above* condition is true for all of them and
its false arm is never taken. INV-1 therefore says nothing about it. A fixture
at a version below 62 is what covers that arm, and it is named here because
the real-asset tier's breadth otherwise reads as covering everything.

**Real-asset tier, off by default.** `RealInstallTest.cpp` gains a class
pass: read every class export in every package under
`UTA_UT_INSTALL_DIR`, assert exact consumption, and walk each class's
ancestry with a resolver backed by the install. It asserts that the number
consumed exactly equals the number attempted, so a reader that starts
refusing individual exports fails rather than quietly reporting fewer
successes — **and it asserts the number of packages opened as well**,
because a regression that refuses a whole package lowers both export counts
together and would otherwise pass. This is INV-1, and it is the only test
that reads content this project did not write.

**The census script.** `scripts/class-census.py` reports, over an install,
how many class exports there are, how many carry a script, and how many
walk to a root — the numbers § 2.1 rests on, so they are re-derived rather
than trusted. It reads packages and writes nothing, like
`scripts/package-census.py`.

**It exits non-zero on exactly one of those numbers**: when any class
export is not consumed exactly, which is § 4.8's rule and the only one with
a right answer that does not move. The rest are reported for a reader to
compare; a script asserting them would go red every time the install gains
a mod.

## 8. Alternatives considered (and rejected)

**Find the script's end by searching for a property list that terminates
exactly at the export end.** This needs no opcode table at all, and it is
the reason § 2.1's fourth finding was measured rather than assumed: for
`Actor`, dozens of candidate script lengths each yield a property list
that parses and ends exactly where it should. The exact-consumption rule
is a strong check on a layout that is otherwise determined; it is not
strong enough to determine one. Rejected as unsound, not as slow.

**Read only the classes that carry no script.** This is most of them, and
it was offered to the user as an option. Rejected because the classes it
skips are the base classes every monster inherits from, so the numbers it
did return would be silently incomplete.

**Take `ScriptSize` as the on-disk length.** This is what the community
documentation implies. Rejected on measurement: it desynchronises every
class that carries a script, and it is the failure that produced this
item's first probe result.

**Decode struct-typed default values.** Deferred, with the measurement, in
§ 3.2.

**Have this item open packages itself.** Rejected in § 3.3: the search
path belongs to an installation, and injecting it is what lets the unit
tier run with no disk.

## 9. Out of scope

- Executing bytecode, and any part of an UnrealScript VM (`ADR-0004`).
- Resolving a custom class onto one of this engine's own — UTA-0023.
- Reading the `Children` chain, and with it struct member layouts and
  function signatures (§ 3.2).
- Any use of what is read: the bestiary, spawning, mutators.
- The override list `ADR-0004` mentions for notorious classes. That is a
  resolution policy, so it belongs with UTA-0023.

## 10. What checks this

| Claim | Invariant | What checks it |
|---|---|---|
| § 4.3's field order is right | INV-1 | the real-asset tier, over every class export in the install |
| § 4.4's walker is right | INV-1 | the same; a wrong walk lands mid-property-list and fails |
| § 4.5's choice of parent source | INV-2 | a fixture whose in-data field is out of range while the table column is valid |
| The walker refuses what it cannot read | INV-3 | an unknown opcode, an overshoot, and a negative `ScriptSize` |
| Nothing is executed | INV-3a | a reading check — no fixture can show an interpreter is absent |
| The walk does not depend on the host | INV-4 | asserted against a literal, on GCC, Clang and MSVC |
| No reader leaves its export | INV-5 | `ByteReader` is built over the export's span; a malformed `ScriptSize` refuses |
| The ancestry walk terminates | INV-6 | a cycle fixture and an over-deep chain |
| Unreachable content is legible, not an error | INV-7 | three fixtures: nothing supplied, the class absent from a package that opened, and a package named in another case |
| Merging works across packages | INV-8 | three fixtures: name tables disagreeing on an index, a name spelled in another case, and two array indices of one property |
| A merged value stays resolvable | INV-9 | each entry's `origin` |
| A non-class export is refused | INV-10 | `readClass` on an ordinary export, and on a sizeless one |
| The link closure does not move | INV-11 | the configure-time assertion |
| One property decoder, not two | INV-12 | a reading check — see the invariant for why not a test |
| § 2.1's exact-consumption claim | none | `scripts/class-census.py`, which exits non-zero when any class export is not consumed exactly |
| § 2.1's other numbers | none | reported by `scripts/class-census.py`; compared by a reader, asserted by nothing |

**What none of this checks.** Exact consumption proves a reader agrees
with the file about where fields end, not that it assigned them to the
right names. A field pair of the same width transposed — `Line` and
`TextPos`, `ProbeMask` and `IgnoreMask` — consumes identically and is
wrong. Nothing here catches that, and the first thing that would is a
caller using the values: UTA-0023 reading a monster's numbers and getting
a monster that behaves like the one in the original game.

## 11. Cross-doc impact

**UTA-0003's `Properties` surface grows by one entry point.** § 4.3 step 11
says why: both existing entry points begin at an export's start, and a
class's default properties are at its end. `readPropertiesAt` is additive —
no existing signature changes and no existing behaviour does — but it is a
change to a shipped item's surface, and UTA-0003 § 4.8 should name it when
this item lands.

**UTA-0003 § 4.8 needs a correction.** It says a struct's layout "is only
knowable from the class table (UTA-0005)", which reads as a promise that
this item supplies it. § 3.2 records that it does not, with the
measurement. That sentence should name the item that will, once one
exists, rather than this one. The correction is not made here: this spec
is a draft until its gate passes, and amending a shipped spec on the
strength of a draft is backwards.

**`ADR-0004` is unchanged.** § 3.1 records why the script walker does not
breach its "no bytecode is executed" line, and the ADR's requirement that
a fallback be legible is met by INV-7.

**`CLAUDE.md` is unchanged.** Nothing here changes how the project is
built, tested or gated.

## 12. Cold-eyes loop log

`docs/reviews/UTA-0005-class-tables-and-ancestry-loop-log.md`.

The rows live outside this document, per `spec-format.md` § 6. The sibling
specs keep theirs inline; that predates the rule rather than overriding it,
and `docs/standards/` carries no spec-format override.

## 13. Resource cost

The walker is a table of instruction shapes and a recursive descent over
it — the largest single piece of this item, and bounded: the instruction
set is fixed and 1999-vintage, and the exact-consumption rule over the
whole install grades it in one run.

Reading a class allocates its default property list and nothing else; the
script is walked without being stored. An ancestry walk holds pointers
into packages the resolver owns, so its cost is the resolver's caching
policy rather than this item's. The merged default set is the only place
this item copies a string per property, and it copies names only.

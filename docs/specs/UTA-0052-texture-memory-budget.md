# UTA-0052 — `umat`: a texture memory budget, block compression and a per-material upscale cap

**Status:** accepted (2026-09-09).
**Kind:** implement.
**Source:** ROADMAP UTA-0052 (user-request-2026-09-04).

**Blocker for:** UTA-0009 (`umat` derives five maps per texture and must
compress them as it goes), UTA-0010 (the curated library ships compressed
material data), UTA-0011 (`ubake` is what calls the budget check and prints
its report).
**Pairs with:** UTA-0008 and UTA-0051. This raises UTA-0008's `.utab` format
version, adds a section to its table and amends its INV-4 — § 11 carries the
edits. UTA-0051's quality tiers later map a tier onto the megabyte figure
below; that binds to this number, and this item does not wait for it.

Stop the improved textures from filling up the graphics card: squash them
properly, do not blow up a blurry old texture for no benefit, and refuse a
map that would not fit rather than quietly making it worse.

## 1. Goal

A baked map's textures are stored block-compressed, at a resolution capped
per material rather than globally, in a `.utab` section that records enough
about each one to add up what it will occupy on the card. The baker can then
measure a map's texture working set before anything is written, and refuse a
bake that exceeds a stated budget — naming the figure it measured and every
texture that contributed to it.

## 2. Problem

UTA-0009 turns one 1999 texture into five maps — base colour, normal,
roughness, height and emissive — and upscales before deriving them. Three
consequences, and the third is what this item exists for.

1. **The multiplication is large.** Upscaling 256×256 to 1024×1024 is
   sixteen times the pixels. Five maps at sixteen times is up to eighty times
   the memory one original texture occupied. The development machine is a GTX
   1050 with 2 GB and the target laptops have less, so this is the constraint
   that decides whether the result runs at all rather than a tuning question.

2. **Nothing in the container can hold a compressed texture, and nothing can
   measure one.** `src/ubundle/Bundle.h` defines `Bundle` with three optional
   members — `rooms`, `nav` and `wiring` — and `write` emits sections in the
   fixed order `ROOM`, `NAVG`, `WIRG`. There is no material or texture
   section, and `src/umat/` does not exist. So this item decides what a
   bundle stores as well as how a texture is compressed, which is why the
   roadmap wants it ahead of UTA-0009: retrofitting compression regenerates
   every material and changes the container under work already done.

3. **The container's version is checked for equality, so adding a section is
   a breaking change that has to be made deliberately.**
   `ubundle::FORMAT_VERSION` is `1`, UTA-0008's INV-4 refuses any header
   whose `formatVersion` is not `1`, and its INV-11 makes an undefined
   section id `MalformedData`. `docs/standards/versioning-overrides.md`
   § Breaking surfaces puts the map bundle format first on its list, and
   `docs/design.md` § What every part does the same way makes the format
   version one of the baker's own inputs — so a bump renames every bundle
   and invalidates every cached bake.

**And the compressor is where this can go wrong invisibly.** UTA-0008's
INV-8 requires `write` to be byte-identical for equal input, and its INV-7
pins `write` against a golden byte array that the CI matrix compares on GCC,
Clang and MSVC alike. `docs/design.md` § What every part does the same way
then names a bundle by the hash of its contents. A block encoder that is
multithreaded with a data-dependent partition, or that leans on an
approximate reciprocal, produces different bytes on different machines — and
that breaks content addressing before it breaks a test.

## 3. Scope decisions (agreed with the user)

1. **The budget is absolute megabytes of texture working set per baked map.**
   **User, 2026-09-09.** The roadmap body said the baker refuses a bake that
   exceeds "the tier's budget", and tiers are UTA-0051, which is `📋` and
   itself blocked behind the bundle draw path. Defining the budget against
   that would have pushed this item past the whole renderer with UTA-0009
   stalled behind it. The dependency runs the other way: UTA-0051 later
   declares which tier maps to which figure, and it binds to this number.

2. **The first figure is 1024 MiB.** **User, 2026-09-09.** Half the GTX 1050's
   2 GB for textures, half for everything else — the G-buffer and depth
   targets at 1080p, shadow maps, geometry, the volumetric grids UTA-0015
   adds, and the desktop compositor. The roadmap body's "the development
   card's 2 GB" is the card, not the texture budget: spending all of it on
   textures leaves the renderer nothing, and a guard that can never refuse a
   bake that will not run is not a guard. **The mechanism is unchanged
   whatever the number becomes**, which is why it can be settled before any
   measurement exists — § 15 records that it has none.

3. **Over budget, the bake refuses and reports what it measured.** **User,
   2026-09-09.** Not automatic degradation. Two people baking one map must
   not silently get different quality, and a reduction nobody was told about
   is the failure that surfaces months later on somebody else's laptop. An
   opt-in `--fit-budget` was offered and **not** taken; do not add one
   without asking again.

4. **The block encoder is `bc7enc`, vendored.** **User, 2026-09-09.**
   `bc7enc.c`/`.h` for BC7 and `rgbcx.h` for BC1–BC5, from
   `github.com/richgel999/bc7enc`. Its `LICENSE` offers each of `rgbcx.h`,
   `bc7decomp.cpp/h` and `bc7enc.c` under a choice of MIT or the Unlicense
   (read 2026-09-09 via `gh api repos/richgel999/bc7enc/contents/LICENSE`),
   either of which this project's GPL-3.0 `LICENSE` can absorb. Its BC7
   encoder is scalar and not vectorized and it threads nothing itself, so
   § 4.4's parallel-over-blocks arrangement cannot change the bytes.
   **Upstream claims no determinism**, so § 5 proves it rather than assuming
   it. § 8 records what lost.

   **Route 2, and ADR-0007's question 4 reaches this case.** That question
   asks whether a dependency's build system produces anything anyone would
   link. `bc7enc` ships a `CMakeLists.txt` (its repository root holds one,
   read 2026-09-09 via `gh api repos/richgel999/bc7enc/contents`), and what
   that file builds is a **demo executable** from `test.cpp` and a bundled
   `lodepng`, not a library anyone links. So question 4 routes it to route 2.
   `docs/decisions/ADR-0007-acquire-dependencies-by-route.md`'s route 2 states
   the same ground:
   *"its sources are compiled into the target that uses it, so fetching would
   buy nothing a copy does not already give"* — exactly true here: the
   encoder is two headers and a
   `.c` file compiled into `uta_umat`. **When this document was gated,
   question 4 asked whether a dependency shipped no build system of its
   own**, which answered no for `bc7enc` and routed it away from route 2.
   That was a defect in the ADR rather than in this document; it was filed as
   UTA-0088 and fixed there on 2026-09-09. The route this document takes
   never changed.

5. **The section descriptor's `compression` byte stays zero, and this item
   does not use it.** Mine, and it **contradicts the roadmap body**, which
   recorded that this item "defines a non-zero value" for that byte. Three
   reasons it must not. UTA-0008's § 3 decision 2 gives the byte its purpose
   — making compression "a per-section property, so the choice to compress
   large geometry and leave a small graph raw is already expressible when
   compression arrives" — which is a stream codec over a whole payload, not
   the encoding of individual texels. One `TEXS` section holds BC7, BC5 and
   BC4 textures together, so one byte for the whole section cannot describe
   it and a per-texture `format` field is needed regardless; using both would
   be two sources of truth for one fact. And a non-zero `compression` has to
   mean *inflate this payload before parsing it* for `ubundle` to stay
   general — if it meant *these are BC blocks*, `ubundle` would have to know
   what a texture is, which is exactly what its own scope and UTA-0008's
   INV-10 rule out. **The good news is that this shrinks the change:**
   UTA-0008's INV-12 needs no amendment, and INV-13 below locks the byte at
   zero so the roadmap's reading cannot be re-adopted by accident.

6. **`ubundle` gains no link, so UTA-0008's INV-10 is untouched.** Mine,
   following the roadmap body's own reading. The types `TEXS` decodes into
   are *layout* — a format tag, dimensions, a mip count and a payload extent
   — so they live in `src/ubundle/Bundle.h` beside the header and the section
   descriptor. `ubundle` validates the framing and never reads a block's
   contents. `uta_umat` is what produces those bytes and it links `ubundle`,
   not the reverse.

## 4. Design

### 4.1 What `umat` is, and what it may link

A new directory `src/umat/` holding `Material.h`/`Material.cpp`, producing
**one** static library `uta_umat` that links `uta_core` and `uta_ubundle` and
nothing else.

**It does not link `uta_upkg`, and that is deliberate rather than an
oversight.** Nothing in § 4.9's API takes a package: `compress` is handed
`Image` levels somebody else decoded, and § 7 says in its own words that
nothing here reads a package byte. Reading a source texture out of a `.utx`
is UTA-0009's, and **UTA-0009 adds the link and amends INV-12 in the same
change**. Listing it now would make INV-12 refuse to configure the correct
minimal implementation of this document.

**One library rather than two, unlike `umap` and `unav`.**
`src/ubundle/CMakeLists.txt` already carries the reasoning for the
single-library case: those two split because a builder half must reach
`uta_upkg` while a runtime half must not. `umat` has no runtime half at all.
`docs/design.md` § The parts lists it under *Build-time only — never linked
into a runtime target*, and rule 2 names it: "Neither runtime target links
`upkg`, `umat` or `ubake`." So the whole library is bake-side and there is
nothing to separate.

`src/CMakeLists.txt` gains `add_subdirectory(umat)` after `ubundle`, matching
that file's stated rule that the order is the order items land in.

`src/umat/CMakeLists.txt` asserts **two** properties at configure time, both
in the form `src/ubundle/CMakeLists.txt` uses for UTA-0008's INV-10 — naming
what is permitted rather than forbidding all, so neither fails the moment a
line above is legitimately needed. The first is the link list (INV-12). The
second is `uta_umat`'s `COMPILE_OPTIONS`, which must carry no entry matching
the root guard's own pattern, `(-ffast-math|-Ofast|/fp:fast|-ffp-contract=fast)`
(INV-8).

**That pattern is the root guard's verbatim, and matching a wider one would
refuse the correct build.** A target's `COMPILE_OPTIONS` property is
initialised from the directory property `add_compile_options` sets, so
`uta_umat`'s property *already carries* `-ffp-contract=off` and
`-fno-fast-math` on GCC and Clang, and `/fp:precise` on MSVC — the flags that
enforce the contract. Measured 2026-09-09 with a throwaway CMake project:
`add_compile_options(-ffp-contract=off -fno-fast-math)` at the top level,
`get_target_property` on a library in a subdirectory, and the property read
back `-ffp-contract=off;-fno-fast-math`. A pattern matching bare
`-ffp-contract` or `/fp:` therefore stops configuration on every leg of a
correct tree. **The assertion forbids the *enabling* spellings only**, and
the inherited entries above are expected to be present.

**The second assertion is needed because the root guard cannot see a target.**
The root `CMakeLists.txt` refuses a fast-math flag arriving through
`CMAKE_CXX_FLAGS` and its four per-config variants, and those are cache
variables: a `target_compile_options(uta_umat PRIVATE -ffast-math)` is
invisible to it and configures green. That is INV-8's own breaking case, so
without a target-level assertion the invariant would be graded by nothing.

**The vendored encoder lives in `third_party/bc7enc/`** with its upstream
`LICENSE` copied beside it and a `README.md` recording the exact upstream
commit, since a vendored copy *is* its own pin. Its sources are compiled
into `uta_umat` rather than into a library of their own, so nothing else in
the build can reach them.

**`bc7enc.c` is compiled as C++, and the root project stays `LANGUAGES CXX`.**
`src/umat/CMakeLists.txt` sets `LANGUAGE CXX` on it with
`set_source_files_properties`. Enabling `C` in the root `project()` call is
the alternative and it is worse: the numeric-contract guard scans the
`CMAKE_CXX_FLAGS` family only, so a C translation unit would sit outside the
one check that holds INV-6's determinism, and `add_compile_options`' own flags
would be the only thing reaching it. One language, one guard, one set of
flags.

### 4.2 The format matrix

Which map gets which block format, and what each costs per pixel. A BC block
covers 4×4 texels; BC4 spends 8 bytes on one, BC5 and BC7 spend 16.

| Map | Channels | Format | Uncompressed | Compressed | Ratio |
|---|---|---|---|---|---|
| Base colour | RGB + A | BC7 | 4 B/px (`RGBA8`) | 1 B/px | 1/4 |
| Normal | RG, Z reconstructed | BC5 | 2 B/px (`RG8`) | 1 B/px | 1/2 |
| Roughness | R | BC4 | 1 B/px (`R8`) | 0.5 B/px | 1/2 |
| Height | R | BC4 | 1 B/px (`R8`) | 0.5 B/px | 1/2 |
| Emissive | RGB | BC7 | 4 B/px (`RGBA8`) | 1 B/px | 1/4 |

**Twelve bytes per pixel become four, so the five maps together cost one
third of what they would uncompressed — not one quarter.** The roadmap body's
"Roughly a quarter of the memory" is true of BC7 against `RGBA8` and is not
true of the set, because BC5 and BC4 are halvings rather than quarterings and
their sources are already narrow. Stated because a budget argued from the
wrong ratio is a budget that is wrong by a third.

**BC4 is this document's addition and the roadmap named only two formats.**
Roughness and height are single-channel, and putting a single channel through
BC7 would spend 1 B/px on it instead of 0.5. The alternative — packing both
into one BC5 — costs exactly the same 1 B/px for the pair, because BC5 *is*
two BC4 blocks; two independent BC4 textures are the same bytes and keep the
maps separately addressable. § 8 records that comparison.

**Normals are two-channel and Z is reconstructed** as
`z = sqrt(1 - x² - y²)`, which is why BC5 rather than BC7: BC5 stores two
channels at full block precision instead of three at a shared one, and a
normal map compressed as colour shows banding on smooth curvature.

### 4.3 The `TEXS` section

A new section id, the bytes `T`, `E`, `X`, `S`. Its framing follows
UTA-0008 § 4.2's primitive encoding unchanged — little-endian throughout,
`vector<T>` as a `u32` count then that many encodings of `T`, `string` as a
`u32` byte length then exactly that many opaque bytes.

The section payload is one `vector<CompressedTexture>`. Each element:

| Field | Encoding | Meaning |
|---|---|---|
| `name` | `string` | The producer's own key for this texture. Opaque here |
| `format` | `u8` | `0` = BC4, `1` = BC5, `2` = BC7. No other value is defined |
| `width` | `u16` | Base level width, a power of two in `[1, 8192]` |
| `height` | `u16` | Base level height, same constraint |
| `sourceWidth` | `u16` | The width of the image handed to `compress` — after any resample, before any upscale |
| `sourceHeight` | `u16` | The height of the same image |
| `mipCount` | `u8` | Levels stored, `1` upward. `1` means base level only |
| `blocks` | `vector<u8>` | Every stored level's block bytes, base level first |

**Minimum encoded size of one element is 18 bytes** — a `u32` length for an
empty `name`, one `u8`, four `u16`, one `u8`, and a `u32` count for an empty
`blocks`. That is the figure UTA-0008 § 4.2 rule 1's division check needs,
and it joins the minimum-size table there.

**The applied upscale factor is derived, never stored.** It is
`width / sourceWidth`, and storing it as well would be a field that can
disagree with the two it is computed from — UTA-0008 § 4.3's own reason for
having no table-offset field. Source dimensions are stored because nothing
else in the bundle records them, so without them the cap in § 4.5 would not
be auditable after a bake.

**They are the dimensions entering compression, not the dimensions of the
1999 original.** UTA-0009 admits a locally-supplied replacement and resamples
a non-power-of-two one before compression, so a 1000×1000 replacement reaches
`compress` at 1024×1024 and stores `sourceWidth` 1024. Recording 1000 instead
would make the ratio `1024/1000`, which is neither exact nor a power of two,
so INV-2 would refuse a texture the resampling path is supposed to produce —
the whole route § 9 defers to UTA-0009 would be unbakeable. **What this
loses is the resample factor**, which the bundle does not record and § 9
names as out of scope; what it keeps is the upscale factor, which is the one
§ 4.5 caps and this field exists to audit.

**So the source dimensions are constrained too, and INV-2 refuses a bundle
that breaks the derivation.** `sourceWidth` and `sourceHeight` are non-zero,
and `width / sourceWidth` and `height / sourceHeight` are equal, exact, and a
power of two in `[1, MAX_UPSCALE_FACTOR]`. Without those clauses a
`sourceWidth` of `0` divides by zero and one larger than `width` yields a
factor of `0` under integer division — in both cases the fields exist and
record nothing, which is the one thing they are stored for. `umat` never
downscales (§ 4.5), so a factor below 1 is not a state this format has.

**Levels are consecutive halvings, floored at one.** Level *l* measures
`max(1, width >> l)` by `max(1, height >> l)`. So the bytes one element's
`blocks` must hold are exactly

```
Σ over l in [0, mipCount)  ceil(w_l / 4) * ceil(h_l / 4) * bytesPerBlock(format)
```

and a payload of any other length is `MalformedData` (INV-1). **This is the
one rule that makes the section self-describing**: without it a reader takes
the declared length on trust and starts the next element wherever the last
one happened to stop.

**Blocks within a level are row-major** — left to right along a row of blocks,
then top to bottom — and a level whose width or height is not a multiple of
four has its right and bottom edge blocks padded by repeating the last real
texel. **This is wire format and it is stated because nothing validates it.**
`ubundle` treats `blocks` as opaque bytes (§ 4.8), so a producer writing
column-major and a consumer reading row-major satisfy INV-1, INV-2 and each
other's byte counts exactly, and the disagreement first appears as a scrambled
texture in a renderer nobody has built yet. UTA-0009 writes these bytes and
UTA-0016 uploads them; both bind to this sentence.

**Power-of-two dimensions are required, and this is a real constraint rather
than a convention.** They make `mipCount`'s upper bound exactly
`log2(max(width, height)) + 1`, so a mip count can be refused before any
payload byte is read (INV-2). UE1 textures are power-of-two already;
UTA-0009's scope note admits a locally-supplied replacement, and resampling
one to a power of two before compression is that item's step, constrained
here.

**Sections stay optional and empty stays distinct from absent**, as
UTA-0008 § 4.4 requires. No `TEXS` says nothing about the map's materials; a
present but empty `TEXS` says the map was examined and produced none.

### 4.4 Compressing a block, and why the job system cannot change the bytes

BC formats are **block-independent**: one 4×4 block's 16 bytes are a function
of its own 16 texels and the encoder's options, and of nothing else. So the
encode is parallelised over blocks with `core`'s job system —
`uta::core::JobSystem::parallelFor(count, body)`, one index per block row,
each writing only into its own output range — and the output is identical for
any worker count, including one. **What would break that is not
parallelism**: it is an encoder that threads internally with a work-stealing
partition, a data-dependent block order, or a scratch buffer shared between
blocks. The vendored encoder does none of those, being scalar and threadless
(§ 3 decision 4), and INV-7 is what holds it.

**The vendored sources are not exempt from the project's numeric contract.**
The root `CMakeLists.txt` applies `-ffp-contract=off -fno-fast-math` on GCC
and Clang and `/fp:precise` on MSVC to every target, and refuses to configure
when `CMAKE_CXX_FLAGS` carries a flag that would undo it —
`docs/specs/UTA-0049-numeric-contract.md` is the contract and
`tests/unit/NumericContractTest.cpp` proves it holds. `uta_umat` adds no
per-target flag that would re-enable either (INV-8). Without this a vendored
C file compiled under contraction folds a multiply-add differently on one
compiler and the golden bytes of INV-6 diverge on that leg alone.

**The perceptual metric is off.** `bc7enc`'s optional perceptual mode computes
error in a weighted YCbCr space, which is a colour-space transform this
document has not established to be free of `<cmath>`. Linear RGB error is
what is used. § 15 records the check that is still owed on the vendored source.

**What `docs/design.md` rules out is "no platform maths library in the
simulation or the baker", and that is narrower than banning a transcendental.**
A `pow` computed from our own table satisfies that rule; a `powf` from the
platform's libm does not, because two machines' libm may differ in the last
bit. So the constraint this item takes is design.md's, and § 15's open
question is about which of the two the vendored source falls under — not
about a transcendental being forbidden outright.

### 4.5 The per-material upscale cap

One constant and one pure function, plus one constant `ubundle` owns.

```cpp
/// The largest edge `umat` will UPSCALE TO. Not a ceiling on the SOURCE:
/// see the over-size case below.
inline constexpr std::uint32_t MAX_OUTPUT_EDGE = 1024;

// MAX_UPSCALE_FACTOR is NOT declared here -- it lives in
// src/ubundle/Bundle.h beside CompressedTexture, and § 4.8 says why.

/// The factor actually applied to a source of these dimensions, given the
/// factor the caller asks for. Pure, and the same on every machine.
[[nodiscard]] std::uint32_t upscaleFactor(std::uint32_t sourceWidth,
                                         std::uint32_t sourceHeight,
                                         std::uint32_t requested) noexcept;
```

It returns the largest power of two that is at most `requested`, at most
`ubundle::MAX_UPSCALE_FACTOR`, and leaves both output edges at or below
`MAX_OUTPUT_EDGE`. Where no factor qualifies it returns `1`, and it never
returns `0` (INV-11).

**A source already larger than `MAX_OUTPUT_EDGE` is passed through at factor
1, and `umat` never downscales.** § 4.3 admits a base level up to 8192 and
UTA-0009's scope note admits a locally-supplied replacement, so this case is
reachable rather than theoretical — a 2048×2048 replacement gets factor 1 and
is stored at 2048. **Two reasons the cap does not clamp it.** Downscaling
somebody's higher-resolution replacement is a quality decision this item has
no basis for taking silently, and it is the same silent degradation § 3
decision 3 rules out one layer along. And the memory guard is the budget,
not the cap: a 2048 texture that does not fit is refused by § 4.6 with a
report naming it, which is a decision somebody sees. So `MAX_OUTPUT_EDGE`
bounds *upscaling* and nothing else, which is what the constant's comment
says.

**Per material means the caller supplies `requested` per texture**, which is
what makes the cap per-material from the first line of code rather than
global. A blurry wall texture gains nothing from four times and a hero
surface might, and the caller is what knows which is which.

**Where `requested` comes from is deferred, and the default is the cap.**
Called with `ubundle::MAX_UPSCALE_FACTOR`, the function yields the largest factor the
edge limit allows — so a bake that asks for nothing in particular gets a
uniform, predictable answer. The per-material figure belongs in the recipe,
whose format is not yet queued as a roadmap item (§ 9), and UTA-0010's
curated library will carry its own for the materials it ships.

**An automatic decision from the image's own detail was considered and
rejected** (§ 8). It is a heuristic over pixel data, which is where a
cross-compiler difference would hide, and there is no measurement yet that
says which textures would benefit.

### 4.6 Measuring the working set, and refusing

```cpp
/// Bytes of texture working set one baked map may occupy.
inline constexpr std::uint64_t TEXTURE_BUDGET_BYTES = 1024ull * 1024ull * 1024ull;

/// What one texture contributes.
struct TextureCost {
    std::string   name;
    std::uint64_t bytes = 0;
};

struct BudgetReport {
    std::uint64_t            workingSetBytes = 0;
    std::uint64_t            budgetBytes = 0;
    /// Every texture, largest first, and never a top-N: a truncated list
    /// cannot say where an overspend came from.
    std::vector<TextureCost> byTexture;
};

/// The sum of every stored level of every texture. Mips included.
[[nodiscard]] std::uint64_t workingSet(
    std::span<const ubundle::CompressedTexture> textures) noexcept;

[[nodiscard]] BudgetReport measure(
    std::span<const ubundle::CompressedTexture> textures,
    std::uint64_t budgetBytes = TEXTURE_BUDGET_BYTES);

/// `InvalidArgument` when the working set exceeds the budget, its message
/// naming both figures. Nothing is dropped, resized or re-compressed.
[[nodiscard]] Result<void> enforceBudget(const BudgetReport& report);
```

**The working set is the sum of stored payload bytes, because that is what a
block-compressed texture occupies.** BC data is uploaded to the card as-is;
there is no decode step and no second copy. So the measurement is exact
rather than an estimate, which is what lets the refusal be a hard one.

**It counts every stored level.** A full mip chain is four thirds of its base
level, so a measurement of the base alone reports three quarters of the truth
— enough to pass a bake at the limit and fail it on the card (INV-9).

**`enforceBudget` takes the report and not the textures, and that is the
no-degradation guarantee rather than an accident of convenience.** § 3
decision 3 rules out silent degradation, and a function that cannot reach a
texture cannot degrade one — the guarantee is in the signature, where no
implementation can breach it. **That is also why it is not an invariant:** a
test asserting the texture set is unchanged across a call that never receives
it passes for every possible implementation, so INV-10 states the refusal
alone and § 10 records the rule as unchecked. What is left uncovered is a
*caller* that degrades before calling in, and nothing here can see that.

**`InvalidArgument` rather than `OutOfMemory`.** Nothing failed to allocate:
the refusal is a policy decision taken at bake time, on a machine that may
have no GPU at all. `src/core/Error.h`'s `ErrorCode` offers no
resource-exhausted value, and adding one belongs to UTA-0002's contract
rather than to this item — § 15 records it as reopenable.

**Sorting `byTexture` is part of the contract, not presentation.** The first
question anyone asks of a refused bake is which texture to cap, and an
unordered list of several hundred entries does not answer it. The order is by
`bytes` descending, then by `name` ascending so the result is total and does
not depend on the input order. **INV-14 grades it, and not INV-10** — the
order is produced by `measure`, so an invariant whose test only calls
`enforceBudget` could never fail on it.

**Printing the report is `ubake`'s (UTA-0011).** This item returns it. So the
tests below grade the measurement and the refusal, and not what a user sees —
§ 10 carries that as a `Partial:` row rather than claiming coverage.

### 4.7 The format version bump

`ubundle::FORMAT_VERSION` becomes `2`, and `TEXS` joins the section ids the
reader defines. Nothing else about UTA-0008's framing changes: the header
stays sixteen bytes, the descriptor stays twenty-four, `compression` stays
zero (§ 3 decision 5), and the table rules — ascending offset, no duplicate
id, every extent inside the file, exact tiling — apply to the new section
unchanged.

**`write` emits `TEXS` last: `ROOM`, `NAVG`, `WIRG`, `TEXS`.** Appending
leaves UTA-0008 § 4.10's existing order clause literally intact, so a reader
of that document is extended rather than contradicted. It also keeps the
small graph sections near the front of a file whose largest section is by far
this one.

**UTA-0008's INV-4 is amended in the same change or the corpus states two
versions.** That invariant refuses any header whose `formatVersion` is not
`1`, and it is annotated in place rather than renumbered, per
`spec-format.md` § 3.7. § 11 lists every edit.

**No `.utab` exists that this orphans.** `0.1.0` has not been cut, so there
is no version-1 bundle anywhere. § 14 carries the compatibility argument.

### 4.8 The API added to `ubundle`

```cpp
namespace uta::ubundle {

/// Which block format a stored texture uses -- § 4.3. A byte outside this
/// set is MalformedData and is never defaulted (INV-3).
enum class BlockFormat : std::uint8_t { BC4 = 0, BC5 = 1, BC7 = 2 };

/// The largest upscale a stored texture may record -- § 4.3, INV-2.
///
/// HERE rather than in `umat`, because INV-2 is a DECODE-TIME rule: `read`
/// refuses a texture whose stored ratio exceeds it, and `ubundle` may not
/// depend on `umat` (INV-12 runs `umat` -> `ubundle` and never back). `umat`
/// re-uses this constant rather than declaring a second one, or the writer
/// and the reader drift and `umat` produces bundles its own reader refuses.
inline constexpr std::uint32_t MAX_UPSCALE_FACTOR = 4;

/// Bytes one 4x4 block occupies in `format`.
[[nodiscard]] constexpr std::size_t bytesPerBlock(BlockFormat format) noexcept {
    return format == BlockFormat::BC4 ? 8u : 16u;
}

/// One block-compressed texture and its mip chain.
///
/// SCOPE: every field here is LAYOUT. `blocks` is opaque to this library --
/// it is validated for LENGTH against the fields above it (INV-1) and never
/// read. What a texel means is `umat`'s, and `umat` is build-time only, so
/// nothing here may reach it (UTA-0008's INV-10).
struct CompressedTexture {
    std::string            name;
    BlockFormat            format = BlockFormat::BC7;
    std::uint16_t          width = 0;
    std::uint16_t          height = 0;
    std::uint16_t          sourceWidth = 0;
    std::uint16_t          sourceHeight = 0;
    std::uint8_t           mipCount = 1;
    std::vector<std::byte> blocks;
};

/// The bytes `blocks` must hold for the fields beside it -- § 4.3. Zero for
/// a combination the format does not permit, which INV-2 refuses first.
[[nodiscard]] std::uint64_t expectedBlockBytes(const CompressedTexture& texture) noexcept;

}  // namespace uta::ubundle
```

`Bundle` gains one member, in `TEXS`'s write position:

```cpp
struct Bundle {
    BundleHeader                                    header;
    std::optional<umap::RoomMap>                    rooms;
    std::optional<unav::NavGraph>                   nav;
    std::optional<unav::WiringGraph>                wiring;
    std::optional<std::vector<CompressedTexture>>   textures;
};
```

### 4.9 The API added by `umat`

```cpp
namespace uta::umat {

/// One level of a source image, 8-bit, row-major, no row padding. `channels`
/// is 1, 2 or 4; `pixels` holds width * height * channels bytes.
struct Image {
    std::uint32_t          width = 0;
    std::uint32_t          height = 0;
    std::uint8_t           channels = 0;
    std::vector<std::byte> pixels;
};

/// Compress one texture's mip chain. `levels[0]` is the base level and each
/// one after it is the previous halved in both axes, floored at 1 -- the
/// same relation § 4.3 encodes, checked here so a bad chain is refused
/// before it reaches the container.
///
/// `sourceWidth`/`sourceHeight` are recorded verbatim into the result so the
/// applied upscale factor stays derivable (§ 4.3). Generating the levels and
/// deriving the five maps are UTA-0009's.
[[nodiscard]] Result<ubundle::CompressedTexture> compress(
    std::string name,
    std::span<const Image> levels,
    ubundle::BlockFormat format,
    std::uint16_t sourceWidth,
    std::uint16_t sourceHeight,
    core::JobSystem& jobs);

}  // namespace uta::umat
```

`compress` refuses with `InvalidArgument` for an empty `levels`, a level
whose `pixels` size disagrees with its own dimensions, a chain that is not
consecutive halvings, a base level that is not a power of two in
`[1, 8192]`, more levels than `log2(max(width, height)) + 1`, a
`channels` count the requested format cannot carry — BC4 needs 1, BC5 needs
at least 2, BC7 needs at least 3 — **or a source pair INV-2 would refuse**: a
zero `sourceWidth` or `sourceHeight`, or a ratio against the base level that
is not one exact power of two in `[1, MAX_UPSCALE_FACTOR]` in both axes.

**That last one is checked here as well as at `read`, deliberately.** INV-2
stops such a bundle being *decoded*; this stops one being *written*, so a bad
texture cannot be produced by the only writer there is and then blamed on the
reader — the same argument `src/ubundle/Bundle.h` already gives for `write`
refusing a structure that violates UTA-0008 § 4.9.

## 5. Invariants

- **INV-1** — a `CompressedTexture` whose `blocks` length is not exactly
  `expectedBlockBytes` for its `format`, `width`, `height` and `mipCount` is
  `MalformedData`.
  *Test:* `tests/unit/BundleTextureTest.cpp`, one case per format with the
  payload one byte short and one byte long, in a `TEXS` section whose own
  extent is exactly consistent with the file. No arrow: the surface does not
  exist yet.
  *Breaks when:* the declared payload length is taken on trust and the next
  element is parsed from wherever the last one ended.
  **The fixture must be consistent at the section level to isolate this
  rule.** UTA-0008 § 4.2 rule 1's division check rejects a payload larger
  than the bytes remaining in its section, and § 4.4's extent rules reject a
  section that overruns the file — so a fixture built by truncating a valid
  bundle is refused by one of those before this check is reached, and deleting
  this rule would leave it red anyway. Only a table and section extent that
  are entirely correct, with a per-element length that disagrees with the
  element's own declared dimensions, can reach it.

- **INV-2** — `width` and `height` are powers of two in `[1, 8192]`;
  `mipCount` is in `[1, log2(max(width, height)) + 1]`; `sourceWidth` and
  `sourceHeight` are non-zero; and `width / sourceWidth` equals
  `height / sourceHeight` exactly and is a power of two in
  `[1, MAX_UPSCALE_FACTOR]`. Any other value is `MalformedData`, refused
  before a payload byte is read.
  *Test:* `tests/unit/BundleTextureTest.cpp`, one case each for a zero
  dimension, a non-power-of-two dimension, a dimension above 8192, a
  `mipCount` of zero, a `mipCount` one past the chain's length, a
  `sourceWidth` of zero, a `sourceWidth` larger than `width`, a source pair
  whose two ratios disagree, and a ratio of 8. No arrow: the surface does not
  exist yet.
  *Breaks when:* `mipCount` is trusted and the level loop walks past the
  payload; or a zero dimension is admitted, which makes INV-1's product zero
  and therefore satisfied by a zero-length payload; or the source dimensions
  are stored without being checked, after which § 4.3's derived factor is a
  division by zero or an integer `0`.
  **The zero-dimension case is why this cannot be folded into INV-1.** With
  `width` zero the expected byte count is zero, so INV-1 passes on it and
  only a dimension check can refuse it.
  **The source clauses have no other grader anywhere.** `sourceWidth` and
  `sourceHeight` are read by nothing in `ubundle` — they exist so the cap
  stays auditable — so INV-1's length arithmetic never touches them and a
  bundle carrying nonsense there decodes cleanly under every other rule.

- **INV-3** — a `format` byte outside `{0, 1, 2}` is `MalformedData` and is
  never defaulted to a value.
  *Test:* `tests/unit/BundleTextureTest.cpp` reads sections carrying `3` and
  `0xFF`. No arrow: the surface does not exist yet.
  *Breaks when:* the byte is cast to `BlockFormat` without a range check,
  after which `bytesPerBlock` returns 16 for an unreadable format and INV-1's
  length check silently grades the wrong arithmetic.
  **The fixture's payload must be the length 16 bytes per block implies**, or
  INV-1 refuses it first and this case is vacuous. `bytesPerBlock` as § 4.8
  writes it returns 8 only for `BC4` and 16 for everything else, so an
  undefined byte reaching it yields 16 — a payload sized for that passes
  INV-1, and only the range check can refuse it.

- **INV-4** — a hand-authored golden byte array for a **whole `.utab` file** —
  the sixteen-byte header at `formatVersion` 2, a one-entry section table, and
  a `TEXS` section carrying **at least one texture of each of BC4, BC5 and
  BC7** — decodes to exactly the values it encodes, field by field.
  **A whole file rather than the payload alone**, for two reasons: INV-13
  asserts the `compression` byte, which lives in the section *descriptor* and
  so is absent from a payload-only fixture; and INV-5 compares `write`'s
  output against these bytes, and `write` emits a file.
  *Test:* `tests/unit/BundleTextureTest.cpp`, asserting each field against
  its own literal. No arrow: the surface does not exist yet.
  *Breaks when:* two adjacent fields of the same width and type are
  transposed in the reader.
  **Two constraints on the fixture, both forced by the layout.**
  *Multiplicity:* at least three textures, at least one with `mipCount`
  greater than 1, because at one level the sum in § 4.3 collapses to a single
  term and a level loop that runs once cannot be told from one that ignores
  `mipCount`. *Distinctness:* every field the fixture is free to choose holds
  a value distinct from every other's, so that any transposition within a run
  of same-width same-type fields changes what decodes. The runs that rule
  produces here are `width`/`height`/`sourceWidth`/`sourceHeight` — four
  consecutive `u16` — and `format`/`mipCount`, two `u8` separated by those
  four. **A round-trip test cannot break this**, which is why the bytes are
  written from § 4.3 by hand rather than produced by `write`: a swap present
  in both reader and writer round-trips perfectly. Same class as UTA-0008's
  INV-6.

- **INV-5** — `write` applied to the structure INV-4 decodes produces exactly
  INV-4's golden bytes.
  *Test:* `tests/unit/BundleTextureTest.cpp`. **This is the cross-compiler
  check for the container half** — the array is a literal fixed in source, so
  the CI matrix runs one comparison against one constant on GCC, Clang and
  MSVC alike and any leg whose bytes differ goes red on its own. No arrow:
  the surface does not exist yet.
  *Breaks when:* the same transposition exists in the writer. **The pair
  isolates each side:** INV-4 grades the reader against the golden bytes and
  this grades the writer against them, so neither can be satisfied by a
  compensating error in the other.

- **INV-6** — `umat::compress` applied to a fixed `Image` chain, once per
  format, produces a fixed golden array of block bytes.
  *Test:* `tests/unit/MaterialCompressTest.cpp`, comparing against arrays
  literal in the source. **This is the cross-compiler check for the encoder**,
  for INV-5's reason: one constant, three legs. No arrow: the surface does not
  exist yet.
  *Breaks when:* the vendored encoder is built with contraction or fast-math
  on, on one compiler and not another; or an approximate reciprocal or
  reciprocal-square-root path is enabled, which is what
  `github.com/BinomialLLC/bc7e`'s own README warns of in `ispc_texcomp`; or
  the vendored copy is updated without regenerating this array.
  **What this cannot see, and INV-7 can:** it compares each compiler's output
  against a constant, so it catches drift between legs. It runs with whatever
  worker count the test hands it, so it says nothing about a second count.

- **INV-7** — `compress` output does not depend on the worker count of the
  `JobSystem` it is given: worker counts 1, 2 and `hardware_concurrency()`
  produce byte-identical results.
  *Test:* `tests/unit/MaterialCompressTest.cpp`, encoding the same chain
  under three separately constructed `JobSystem` instances and comparing the
  bytes. No arrow: the surface does not exist yet.
  *Breaks when:* the encoder is replaced by one that threads internally with
  a work-stealing partition; the block partition is made data-dependent; or a
  scratch buffer is shared between blocks so one block's encode reads
  another's leftovers.
  **Three counts including 1, because 1 is the degenerate case.** A pool of
  one worker runs every block on the submitting thread, so a shared scratch
  buffer produces a *consistent* wrong answer there and a comparison of two
  multi-worker runs would agree with each other and with nothing else.
  **The fixture is small on purpose.** `tests/CMakeLists.txt` puts
  `TIMEOUT 30` on every discovered unit test, and a large BC7 encode run
  three times does not fit inside it; 64×64 is enough to produce many blocks.

- **INV-8** — `uta_umat` adds no compile option that re-enables
  floating-point contraction or fast-math for any translation unit it
  compiles, the vendored sources included.
  *Test:* `src/umat/CMakeLists.txt` reads `uta_umat`'s `COMPILE_OPTIONS`
  property and stops with a `FATAL_ERROR` where any entry matches the root
  guard's own pattern, `(-ffast-math|-Ofast|/fp:fast|-ffp-contract=fast)` —
  the assertion form `src/ubundle/CMakeLists.txt` uses for UTA-0008's INV-10.
  **Prove it by breaking it once**: add `target_compile_options(uta_umat
  PRIVATE -ffast-math)` and configure, which must stop. No arrow: the file
  does not exist yet.
  **The pattern must be the enabling spellings only.** The property is
  initialised from the directory property `add_compile_options` sets, so it
  already carries `-ffp-contract=off`, `-fno-fast-math` or `/fp:precise` on a
  correct tree (§ 4.1 carries the measurement). A pattern matching bare
  `-ffp-contract` or `/fp:` refuses that tree on every leg, and the
  break-it-once step above cannot tell that from the assertion working,
  because configuration has already stopped before the breaking change is
  made.
  *Breaks when:* a per-target flag is added to make the vendored C compile
  faster, after which a multiply-add folds on one compiler and not another
  and INV-6 fails on one leg with nothing saying why.
  **The root guard cannot grade this and the assertion is not redundant with
  it.** That guard scans `CMAKE_CXX_FLAGS` and its four per-config variants,
  which are cache variables — a target property is invisible to it, so the
  breaking case above configures green under the root guard alone. The two
  cover different arrival routes for one flag.
  **This is defence in depth and INV-6 is the contract.** A determinism
  failure shows up in INV-6's golden array however it was caused; this
  invariant removes the commonest cause, and does so at configure time where
  the diagnosis is a filename rather than a byte diff.

- **INV-9** — `workingSet` equals the sum of `blocks.size()` over every
  texture — every stored mip level, not the base level alone.
  *Test:* `tests/unit/MaterialCompressTest.cpp`, over a set containing one
  texture with a full chain and one with `mipCount` 1, asserted against the
  sum computed from § 4.3's formula in the test itself. No arrow: the surface
  does not exist yet.
  *Breaks when:* the sum counts level 0 only. A full chain is four thirds of
  its base level, so that reports three quarters of the truth — enough to
  pass a bake sitting at the budget and have it fail on the card.

- **INV-10** — `enforceBudget` returns `InvalidArgument` when
  `workingSetBytes` exceeds `budgetBytes`, and succeeds otherwise.
  *Test:* `tests/unit/MaterialCompressTest.cpp`, three cases — one byte under
  the budget, exactly at it, one byte over — asserting the outcome. No arrow:
  the surface does not exist yet.
  *Breaks when:* the comparison is written `>=`, refusing a map that fits
  exactly — which the at-budget case is there to catch. Or the sense is
  inverted, which the two one-byte cases catch between them.
  **The no-degradation half of § 3 decision 3 is deliberately NOT an
  invariant here, because this signature cannot express its breach.**
  `enforceBudget` takes a `BudgetReport` and never sees a texture, so no
  implementation of it can drop, resize or re-compress one — an assertion
  that the texture set is unchanged across the call passes for every possible
  implementation and could never go red. § 4.6 states the guarantee where it
  actually lives, in the shape of the signature, and § 10 records the rule as
  unchecked rather than claiming it.

- **INV-11** — `upscaleFactor` returns the largest power of two that is at
  most `requested`, at most `MAX_UPSCALE_FACTOR`, and leaves both output
  edges at or below `MAX_OUTPUT_EDGE` — § 4.5's rule, word for word. Where no
  factor qualifies it returns `1`, and it never returns `0`. **`1` is the
  floor, so a source edge already above `MAX_OUTPUT_EDGE` yields `1` and is
  stored unreduced**: the function never downscales, and the budget rather
  than the cap is what refuses an over-size texture.
  *Test:* `tests/unit/MaterialCompressTest.cpp`, including a 512×512 source
  asking for 4 (answer 2, the edge limit binding), a 1024×1024 source asking
  for 4 (answer 1), a **2048×2048 source asking for 4 (answer 1, and 2048 is
  stored unchanged)**, a 64×64 asking for 8 (answer 4, `MAX_UPSCALE_FACTOR`
  binding), a 64×64 asking for 3 (answer 2, rounding down to a power of two),
  and a 64×64 asking for 0 (answer 1). No arrow: the surface does not exist
  yet.
  *Breaks when:* the factor cap is applied before the edge test, so a 512×512
  source asking for 4 yields 2048 and blows `MAX_OUTPUT_EDGE`. Or `requested`
  of 0 propagates, producing a zero-sized output that INV-2 then refuses at
  the container with no explanation of where it came from. Or the edge rule
  is written as a clamp on the RESULT rather than as a bound on the upscale,
  which makes a 2048 source return a factor below 1 — a value the return type
  cannot carry and § 4.3's stored ratio cannot express.
  **The 2048 case is what separates those two readings**, and every other
  case in the list passes under both. Without it an implementer may write the
  clamp and only discover it when `width / sourceWidth` truncates to `0`.
  **This clause restates § 4.5 rather than paraphrasing it, deliberately.** A
  reworded version of the same rule is a second rule, and the two disagree on
  the case neither author had in mind — here, whether a bound that was never
  crossed still binds.

- **INV-12** — `uta_umat`'s link entries are exactly `uta_core` and
  `uta_ubundle`. **Not `uta_upkg`** — nothing in § 4.9's API takes a package,
  so listing it would make this assertion refuse the correct minimal
  implementation of this document. UTA-0009 adds it and amends this invariant
  in the same change.
  *Test:* `src/umat/CMakeLists.txt`, a configure-time property assertion in
  the form `src/ubundle/CMakeLists.txt` uses for UTA-0008's INV-10. No arrow:
  the file does not exist yet.
  *Breaks when:* a convenience dependency on `uta_umap_build` or
  `uta_unav_build` is added. **This is half of `docs/design.md` rule 2 and
  says so:** the other half is a link-closure test over `ut-ants` and
  `ut-ants-server` asserting neither reaches `uta_umat`, and neither program
  exists until UTA-0016. § 10 records that as `Partial:` rather than claiming
  it.
  **Prove it by breaking it once**: add `uta_umap_build` to the
  `target_link_libraries` line and configure, which must stop with the
  `FATAL_ERROR` naming the permitted entries. An assertion nobody has seen
  fire is an assertion that may be comparing the wrong property.

- **INV-13** — `write` emits `0` in the `TEXS` descriptor's `compression`
  byte, and the block format is carried only by each texture's own `format`
  field.
  *Test:* `tests/unit/BundleTextureTest.cpp` asserts the byte's position in
  INV-4's golden array, and asserts that two textures of different formats in
  one `TEXS` section round-trip — which no section-level byte could express.
  No arrow: the surface does not exist yet.
  *Breaks when:* the byte is repurposed to carry the block format, per the
  reading § 3 decision 5 overturns — after which a section holding BC7 and
  BC4 together cannot be described at all, and `ubundle` has to know what a
  texture is to parse its own table.
  **The refusal half is UTA-0008's INV-12 and is not restated here.** That
  invariant already refuses any section whose `compression` byte is non-zero,
  so a fixture carrying `1` is killed by a rule this document did not write;
  asserting it here would grade UTA-0008's reader, not this item's writer.
  What is new is the two-format round-trip, which is the only case that
  distinguishes a per-texture `format` field from a per-section byte.

- **INV-14** — `measure` returns a report whose `workingSetBytes` and
  `budgetBytes` are the figures it was given, and whose `byTexture` names
  **every** texture, ordered by `bytes` descending and, where two are equal,
  by `name` ascending.
  *Test:* `tests/unit/MaterialCompressTest.cpp`, over a set with textures of
  differing sizes and one tied pair, asserting the full sequence. No arrow:
  the surface does not exist yet.
  *Breaks when:* `byTexture` is left in input order, or truncated to the
  largest few, or the tie is broken by input order — after which the order is
  stable on the author's fixture and arbitrary on a real map.
  **Split out of INV-10 rather than stated there, because `enforceBudget` does
  not produce the order — `measure` does.** An invariant whose test only ever
  calls `enforceBudget` cannot fail on it, and § 4.6 calls the ordering part
  of the contract rather than presentation: the first question anyone asks of
  a refused bake is which texture to cap.

## 6. Failure modes

- **The vendored encoder turns out to use a transcendental** — `pow` for a
  gamma curve, say. `docs/design.md` § What every part does the same way
  rules a platform maths library out of the baker, so two machines' libm
  could disagree in the last bit. INV-6 catches it as a red leg rather than
  as silent drift, and the remedy is to disable the path that uses it or to
  replace it with a table. § 15 carries the check that is still owed.

- **A texture legitimately needs more than 1024 MiB of company.** The bake
  refuses and the report names the largest contributors. There is no
  automatic escape (§ 3 decision 3): the operator lowers a material's
  requested factor, or raises the budget deliberately by passing
  `budgetBytes`. A map that cannot fit at factor 1 cannot be baked, and the
  report is what says so.

- **`mipCount` is 1 everywhere because UTA-0009 has not generated mips yet.**
  The working set is then the base levels alone and the budget is measured
  correctly against what is stored — but a later item adding mips raises every
  map by a third, and a rotation that fitted stops fitting. That is a real
  cliff and it is announced by the same `### Changed` entry § 14 requires,
  since adding mips changes what a bundle holds.

- **A block-compressed texture is uploaded to a GPU that does not support
  the format.** BC4, BC5 and BC7 are core in Vulkan 1.0 as the
  `textureCompressionBC` feature, and `docs/design.md` already rules out any
  machine below Vulkan 1.3. Nothing in this item checks the feature bit; that
  is the renderer's business and arrives with UTA-0016.

- **`JobSystem`'s job bodies throw.** `parallelFor` returns the count that
  did, and `uta::core::JobHandle` was given an observable failure by
  UTA-0047. `compress` propagates a non-zero count as an error rather than
  returning a partially written texture, because a bundle written from one is
  a wrong bundle presented as a good one.

- **Two textures share a `name`.** `ubundle` does not care — the field is
  opaque to it — and neither does the working set. Whoever resolves a name to
  a material slot has to (UTA-0009), and this item deliberately imposes no
  uniqueness rule it would not itself enforce.

## 7. Tests

Two new files, both in the existing `uta_unit_tests` target in
`tests/CMakeLists.txt`, which `catch_discover_tests` labels `unit;fast` with
`TIMEOUT 30`. That target's `target_link_libraries` gains `uta_umat`.

| File | Locks |
|---|---|
| `tests/unit/BundleTextureTest.cpp` | INV-1, INV-2, INV-3, INV-4, INV-5, INV-13 — the container half |
| `tests/unit/MaterialCompressTest.cpp` | INV-6, INV-7, INV-9, INV-10, INV-11, INV-14 — the encoder and budget half |

INV-8 and INV-12 are configure-time assertions in `src/umat/CMakeLists.txt`
and are graded by the build failing, not by a Catch2 case. **Both are real
assertions over a target property** — INV-12 over `LINK_LIBRARIES`, INV-8
over `COMPILE_OPTIONS` — and each must be seen to fire once, by making the
breaking change its own clause names and confirming configure stops.

**Every case must be seen to fail against pre-change code**, per
`~/.claude/standards/testing.md`. For most of these the pre-change state is
that the symbol does not exist, so the honest red run is a compile failure;
the cases that need a genuine behavioural red are INV-1, INV-2, INV-9,
INV-10 and INV-11, each of which is run once with its check deleted from the
implementation before the check is restored.

**The mutation probe does not cover these.** `./scripts/mutation-probe.py`
takes `ubundle` as its only subject and its mutations are written out by
hand, so `CLAUDE.md` § Build and test's rule applies: this lane is mutated by
hand. The mutations worth making by hand are the ones each *Breaks when*
above names.

**No real-asset tier case.** Nothing here reads a package, so
`UTA_REAL_ASSET_TESTS` gains nothing and **S7** is unaffected: the whole of
this item tests from synthetic images built in the test itself.

## 8. Alternatives considered (and rejected)

- **Write our own BC encoders.** BC4 and BC5 are genuinely small — endpoint
  min/max and interpolated values. BC7 is not: mode selection, partition
  tables and endpoint fitting are weeks of work and the quality bar is a
  research problem. It would stall UTA-0009 behind an encoder nobody asked
  for, and it buys determinism that § 5 obtains anyway from a golden array.

- **Our own BC4/BC5 plus a vendored BC7.** Less third-party surface, at the
  cost of two deterministic code paths and two sets of golden arrays instead
  of one, to save a few hundred lines. Rejected on the reuse-before-rewriting
  ground `coding.md` § 1.3 states.

- **ISPC — `bc7e` or `ispc_texcomp`.** Two to three times faster than
  `ispc_texcomp` at equal quality by its own README, and the highest quality
  available. It needs Intel's ISPC compiler, a fourth compiler in a build
  whose matrix already carries three. And determinism becomes a build
  argument rather than a property: `bc7e`'s README says *"If determinism
  across Intel/AMD CPU's is important in your usage case, you'll want to
  probably limit the targets to only SSE and disable fast math"*, and
  separately that
  *"BC7E doesn't use approximate (and non-deterministic/ill-defined) SSE
  rsqrt() or rcp() instructions, unlike ispc_texcomp"* — which rules
  `ispc_texcomp` out on its own terms.

- **A GPU encoder.** Fastest of all, and it makes the baker require a
  graphics driver and produce bytes that depend on the card. `ADR-0002`
  requires one map, recipe and baker version to hash to one bundle on any
  machine; this cannot.

- **Pack roughness and height into one BC5 instead of two BC4s.** Identical
  memory — BC5 is two BC4 blocks — and it couples two maps that are
  separately addressable, so a material wanting height and no roughness pays
  for both. No saving, a real cost.

- **BC1 for base colour.** Half of BC7's bytes at 0.5 B/px. Rejected: BC1
  has 5:6:5 endpoints and four colours per block, which shows visible
  blocking on the gradients an upscaled 1999 texture is mostly made of, and
  the point of UTA-0009 is that those textures look better rather than
  smaller.

- **Decide the upscale factor automatically from the image's own detail.**
  The roadmap's "A blurry wall texture gains nothing from four times"
  suggests it. It is a heuristic over pixel data, which is exactly where a
  cross-compiler difference hides, and there is no measurement yet that says
  which textures would benefit. The cap plus a per-material `requested` gets
  the same outcome with a decision somebody can point at.

- **Use the section descriptor's `compression` byte for the block format.**
  What the roadmap body assumed. § 3 decision 5 carries the three reasons it
  cannot work.

- **Store the applied upscale factor rather than the source dimensions.**
  One byte instead of four, and a field that can disagree with the `width` it
  was derived from. UTA-0008 § 4.3's no-table-offset reasoning applies
  unchanged.

## 9. Out of scope

- Deriving the five maps from a 1999 texture, and generating mip chains —
  tracked by UTA-0009.
- Resampling a locally-supplied replacement texture to a power of two before
  compression — tracked by UTA-0009, constrained by § 4.3. **The resample
  factor is not recorded anywhere**: `sourceWidth` is the post-resample
  figure, so a bundle cannot say what the file on disk originally measured.
  Deferred; not yet queued.
- The curated material library and the licence line for a redistributable
  replacement — tracked by UTA-0010.
- Calling the budget check, and printing its report — tracked by UTA-0011.
- Mapping a quality tier onto a megabyte figure — tracked by UTA-0051.
- Uploading a block-compressed texture and checking
  `textureCompressionBC` — tracked by UTA-0016.
- The recipe field that carries a per-material `requested` factor —
  deferred; not yet queued. `docs/design.md` § The parts gives `urecipe`
  per-map material assignments, and no roadmap item covers the format yet.
- A semantic table saying which texture serves which slot of which material —
  deferred; not yet queued. `TEXS` stores textures keyed by an opaque name;
  what a name means is above this layer.
- BC6H for a high-dynamic-range emissive map — deferred; not yet queued.
  Emissive is 8-bit here, which is what a 1999 source provides.
- Compressing any other section's payload with a stream codec, which is what
  UTA-0008's `compression` byte is reserved for — deferred; not yet queued.
- Refusing a bake at the first texture that crosses the budget, rather than
  after the whole set is compressed and resident — deferred; not yet queued.
  § 13 carries what it would cost and why it is not taken here.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/unit/BundleTextureTest.cpp` |
| INV-2 | `tests/unit/BundleTextureTest.cpp` |
| INV-3 | `tests/unit/BundleTextureTest.cpp` |
| INV-4 | `tests/unit/BundleTextureTest.cpp` |
| INV-5 | `tests/unit/BundleTextureTest.cpp`, on all three CI legs |
| INV-6 | `tests/unit/MaterialCompressTest.cpp`, on all three CI legs |
| INV-7 | `tests/unit/MaterialCompressTest.cpp` |
| INV-8 | `src/umat/CMakeLists.txt` and the root `CMakeLists.txt` guard — configure fails |
| INV-9 | `tests/unit/MaterialCompressTest.cpp` |
| INV-10 | `tests/unit/MaterialCompressTest.cpp` |
| INV-11 | `tests/unit/MaterialCompressTest.cpp` |
| INV-12 | **Partial:** `src/umat/CMakeLists.txt` asserts the link list; nothing asserts `ut-ants` and `ut-ants-server` exclude it, both being absent until UTA-0016 |
| INV-13 | `tests/unit/BundleTextureTest.cpp` |
| INV-14 | `tests/unit/MaterialCompressTest.cpp` |
| § 4.2's ratio arithmetic | **nothing** — the table is derivable from the BC block sizes but no test computes it; a wrong cell misleads a budget argument and nothing fails |
| § 4.6's 1024 MiB figure being right for the hardware | **nothing** — no measurement exists on a real card; UTA-0039 is where a frame-rate floor is held across the library, and UTA-0051 is where a tier declares a figure |
| § 4.6's report reaching a user | **Partial:** the report's contents are asserted by INV-10; nothing prints it until UTA-0011 |
| § 4.7's version bump reaching UTA-0008 | **Partial:** `check-doc-facts` `quotes`, a quoted-fragment check, over both specs catches a quotation of UTA-0008 that this bump falsified, and `mcp__ants__spec_query`, an invariant-parse verb, run on UTA-0008 shows whether INV-4 names version 2. Neither reads § 4.3's header table, § 4.4's `id` row, § 4.2's minimum-size table, § 4.10 or § 14 — every edit § 11 lists except INV-4 and the INV-6/INV-7 golden array — so those are checked by a reader alone. A stale one is a corpus stating two versions |
| § 4.1's vendored `LICENSE` being shipped | **nothing** — no gate reads `third_party/`; the MIT notice is a redistribution obligation nobody checks |
| § 3 decision 3's no-degradation rule | **nothing** — it is guaranteed structurally rather than checked: `enforceBudget` takes a `BudgetReport` and never sees a texture, so no implementation of it can degrade one (§ 4.6). Nothing catches a *caller* that degrades before calling in, and nothing catches a later signature change handing it the textures |

Count with:

```sh
awk '/^\| INV-|^\| § /' docs/specs/UTA-0052-texture-memory-budget.md | wc -l
awk '/^\| INV-|^\| § /' docs/specs/UTA-0052-texture-memory-budget.md \
  | grep -c 'nothing\|Partial:'
```

## 11. Cross-doc impact

- **`docs/specs/UTA-0008-bundle-container-and-origin.md`** — amended in the
  same change, never renumbered:
  - **INV-4** annotated `amended by UTA-0052` and its version raised from `1`
    to `2`. Its `*Breaks when:*` is unchanged, the equality check being the
    thing it protects.
  - **INV-6 and INV-7's golden byte array** regenerated for the new
    `formatVersion` byte. Their claims do not change; the constant does.
  - **§ 4.3's header table** — `formatVersion` value `1` becomes `2`.
  - **§ 4.4** — `id`'s "four bytes, § 4.6–4.8" extended to name this
    document's § 4.3 as well, so *a section id this version defines* still
    resolves to a complete list.
  - **§ 4.2's minimum-encoded-size table** gains `CompressedTexture` at 18
    bytes, the figure § 4.3 derives.
  - **§ 4.10** — `Bundle` gains `textures` and the `write` order becomes
    `ROOM`, `NAVG`, `WIRG`, `TEXS`.
  - **§ 14** — its opening sentences read *"**A reader accepts
    `formatVersion == 1` and nothing else.** It does not accept a range."*
    That is UTA-0008's whole compatibility argument stated in version terms,
    and left alone it makes the corpus state two versions — the harm § 4.7
    names. The rule is unchanged and only the number moves: the reader accepts
    `formatVersion == 2` and nothing else.
  - **INV-10, INV-11, INV-12, § 4.9 and INV-3 are unchanged**, and that is a
    finding rather than an omission. `ubundle` gains no link (§ 3 decision 6);
    the unknown-id rule needs no edit because only the defined set grew; the
    `compression` byte stays zero (§ 3 decision 5); and **§ 4.9 gains no
    texture rule.** That section lists *post-decode* structural relations
    between decoded tables — a node run reaching past its edge vector, a zone
    naming a room that does not exist — and INV-3 is scoped to what it lists.
    This document's INV-1 and INV-2 are *decode-time* refusals in § 4.4's
    class, checked before a `Bundle` is returned at all. Copying them into
    § 4.9 would make two documents own one rule, which is the drift
    `documentation.md` § 2.1 forbids.
- **`CHANGELOG.md`** — a `### Changed` entry leading with what stops working:
  the bundle format version moves to 2 and every cached bake is invalidated.
  `docs/standards/versioning-overrides.md` § Override requires the entry and
  requires a release whose section contains one to lead with it.
- **`docs/design.md`** — § The stack gains a row for `bc7enc` (vendored,
  route 2), with `ispc_texcomp`/`bc7e` as the runner-up. No rule changes:
  rule 2 already names `umat` build-time only and rule 17's version-bump
  clause is what § 4.7 obeys.
- **`docs/decisions/ADR-0007-acquire-dependencies-by-route.md`** — changed by
  UTA-0088 on 2026-09-09, not by this item. Question 4 now asks what a
  dependency's build system produces rather than whether it ships one, and
  route 2 names `bc7enc` beside Dear ImGui. No route changed, and no rule
  here follows from the edit.
- **`README.md`** — no change. Nothing new is a prerequisite: the vendored
  sources are in the repository and need no installed package.
- **`CLAUDE.md`** — no change. § Build and test's commands and options are
  unaffected.

## 12. Cold-eyes loop log

Rows live in `../reviews/UTA-0052-texture-memory-budget-loop-log.md`.

## 13. Resource cost

**`umat` holds one texture's chain at a time.** `compress` takes the source
levels the caller owns and returns one `CompressedTexture`; there is no
cache, no eviction and no state between calls. Peak inside one call is the
caller's levels plus the output — for a 1024×1024 `RGBA8` base with a full
chain, about 5.3 MiB in and 1.3 MiB out.

**The accumulation is the caller's, and the budget does NOT bound it.**
`measure` and `enforceBudget` take a span of every texture, so a whole map's
compressed set is resident before either runs. The peak is therefore the
working set the map actually produced, and on the case this item exists for —
a map several times over budget — that peak *exceeds* `TEXTURE_BUDGET_BYTES`
by construction. **The refusal is post-hoc**: it reports what was produced,
it does not stop it being produced.

**So the honest statement is that this item names a cap it does not enforce
at allocation time**, and an implementer who builds accumulate-then-measure
believing memory is capped will exhaust a 2 GB laptop on exactly the bake
that should have printed the report naming the largest contributors. The
remedy — a running total checked against `budgetBytes` as each texture is
compressed, refusing at the first texture that crosses it — is a real design
and is **deferred, not yet queued** (§ 9). It is deferred rather than taken
because it changes what the report can say: an early refusal names the
texture that crossed the line and cannot name the ones after it, which is
less useful than the full ordered list § 4.6 promises.

**`ubundle`'s side grows by the section it decodes.** `read` already holds
the caller's bytes and the structures decoded from them; `TEXS` adds a copy
of the block bytes, since `CompressedTexture::blocks` owns its payload rather
than viewing it — consistent with UTA-0008's other sections, and required
because a `Bundle` outlives the span it was read from.

**One new external dependency, vendored: `bc7enc`.** No fetch, no network,
no toolchain addition. **One new build target: `uta_umat`.** Two new test
files, in the existing test executable.

## 14. Migration / compatibility

**There is no version-1 data.** `0.1.0` has not been cut and no `.utab` has
been produced outside this repository's own tests, so the bump orphans
nothing that exists.

**A version-1 reader refuses a version-2 bundle, by design.** UTA-0008's
INV-4 checks `formatVersion` for equality and its § 14 gives the reasoning:
a bundle is a bake output, `docs/standards/versioning-overrides.md`
§ Breaking surfaces already treats a format change as invalidating every
cached bake, and `docs/design.md` § What every part does the same way makes
the format version one of the baker's own inputs — so a version change
renames every bundle anyway and there is nothing a tolerant reader would
save.

**What this does cost, once bakes exist:** every cached bake on a rotation is
re-baked. `versioning-overrides.md` says that must be announced "because on
a big rotation it is a long wait", which is what § 11's `### Changed` entry
is for.

**The authored-bundle problem is not made worse and is not solved here.**
UTA-0008 § 15 holds the question of how a distributed authored bundle
survives a format bump, and dates it before `0.6.0` ships **S6**. This item
adds one more bump before that answer exists, which is the state that
document already describes.

## 15. Open questions

- **The 1024 MiB figure has no measurement behind it.** It is a division of
  the development card's 2 GB by reasoning about what else lives there, not a
  reading from a profiler. UTA-0051 declares the per-tier figures and
  UTA-0039 is where a frame-rate floor is held across the map library; either
  could move this number. The mechanism does not change when it does.

- **Whether the vendored encoder uses a transcendental from `<cmath>`.**
  `docs/design.md` rules a platform maths library out of the baker, and this
  document has not read `bc7enc.c` to confirm it uses none. § 4.4 disables
  the perceptual path, which is the likely user of one. The check is owed at
  vendoring time; INV-6 is what would catch the consequence either way.

- **Whether `core` should gain a resource-exhausted `ErrorCode`.** § 4.6 uses
  `InvalidArgument` because `src/core/Error.h` offers nothing closer and
  `OutOfMemory` would be a lie. Adding a value is UTA-0002's contract to
  amend, and one caller is thin justification. Reopen if a second refusal of
  this shape appears.

- **Whether `bc7enc.h` is covered by the upstream dual licence.** That file's
  `LICENSE` enumerates `rgbcx.h`, `bc7decomp.cpp/h` and `bc7enc.c` by name
  and does not name `bc7enc.h`, which § 4.1 vendors alongside them. Almost
  certainly an omission rather than a reservation — a `.c` file's own header
  — but it is a licence question on a GPL-3.0 repository, so settle it with
  upstream or by inspection before the file is copied in, not after.

- **Whether `mipCount` should be required rather than permitted.** A texture
  with one level shimmers at distance, so every production texture will carry
  a chain — but requiring it here would make this item depend on UTA-0009's
  mip generation, which § 9 defers. The container permits `1` and the budget
  measures what is stored.

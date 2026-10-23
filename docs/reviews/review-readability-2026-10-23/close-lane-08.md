# close-findings: lane 08, umat and urecipe

Closes every finding in [`lane-08-umat-urecipe.md`](lane-08-umat-urecipe.md)
under `UTA-0419`. Run 2026-10-23. Roadmap lookup: `UTA-0419` is the item;
the lane's two correctness leads are `UTA-0406`, closed separately in
`4e56d97`.

The fixes change no baked byte. Proof: `ut-bake` built at `fa71105` and
after both commits baked DM-ArcaneTemple, AS-Frigate, DM-Fetid and
CTF-Face (baker `r68-f32`); each wrote one bundle, and each pair is
byte-identical.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| 1 | `bakeDigest`'s comment buries the byte order under history; three `.cpp` comments say "last" or "version-6 digest" falsely | Q4 | MEDIUM | yes; the hidden brushes are hashed after the sky | fixed | `Recipe.h`, `Recipe.cpp` | → one numbered list of the byte order at `bakeDigest` in `Recipe.h`; the `.cpp` comments drop "last" and the version number | `bakeDigest` body: agrees, section by section |
| 2 | `stretchAxis` comment names `stride` and `step` parameters it lacks; `(void)lines;` on a used parameter (also the lane's tool-gap row) | Q4 | MEDIUM | yes | fixed | `Enlarge.cpp` | → the comment describes `alongRows`; the dead statement is gone | n/a |
| 3 | `parse` handles each section kind in five places, nothing lists them; the map-digest refusals are five near-copies | Q2 | MEDIUM | yes | fixed in part, rest queued as `UTA-0428` | `Recipe.cpp` | → a comment at the `In` enum lists every place a new kind touches; the five refusals are one list, messages byte-identical. The table-driven parser is `UTA-0428`: it rewrites the parser | RecipeFormatTest digest refusals by line: agree |
| 4 | the 8192 edge ceiling written in five places, messages included | Q1 | MEDIUM | yes; no spec names a constant for it, UTA-0052 SS 4.3 states the number | fixed | `ubundle/Bundle.h`, `TextureSection.cpp`, `Material.cpp`, `Generate.cpp`, `Generate.h`, `Enlarge.cpp`, `Enlarge.h`, `ubake/Bake.cpp` | → `ubundle::MAX_TEXTURE_EDGE` beside `MAX_UPSCALE_FACTOR`; every check and message uses it; Bake.cpp's `MADE_EDGE_LIMIT` is set from it | UTA-0052 SS 4.3 `[1, 8192]`: agrees |
| 5 | ASCII lower-casing written three times | Q1 | MEDIUM | yes, at the review's commit | fixed | — | already closed by lane 07's `ca83695`: `Recipe.cpp`, `Lookup.cpp` and `Generate.cpp` call `core/Ascii.h` | n/a |
| 6 | `Recipe.h` says CuratedOverride has six fields | Q4 | LOW | yes; five, plus `upscale` | fixed | `Recipe.h` | → "a MaterialAssignment's fields mirror, `upscale` aside" | `Recipe.h` `upscale` doc: agrees |
| 7 | `[hide]` names checked by `lampNameOf` | Q3 | LOW | yes | fixed | `Recipe.cpp` | → `sectionNameOf`, which names the section; landed with `UTA-0406` in `4e56d97` | UTA-0109 SS 4.6 "the name as a lamp's": agrees |
| 8 | bare indices 8 and 4 are keyword lengths | Q5 | LOW | yes | fixed | `Recipe.cpp` | → `nameAfter(heading, keyword)` takes the length from the keyword; a hand mutation dropping its blank check survived, so a new RecipeFormatTest case refuses `[lamps]` and kin, and kills it | n/a |
| 9 | `chains` pairs with `MAPS` by position only | Q4 | LOW | yes | fixed | `Generate.cpp` | → a `static_assert` pins `MAPS` to `MapKind` order, and both say they pair | n/a |
| 10 | "(INV-8)" reads as UTA-0052's | Q4 | LOW | yes; UTA-0009 INV-8 is the map set and formats | fixed | `Generate.cpp` | → "UTA-0009 INV-8" | UTA-0009 INV-8: agrees |
| 11 | `sortedAndUnique` copied in three tables | Q1 | LOW | yes | fixed | `Fingerprint.h`, `CuratedMaterials.cpp`, `CuratedCloth.cpp`, `CuratedFlames.cpp` | → `detail::sortedByFingerprint` in `Fingerprint.h`; each table keeps its own `static_assert` and message. The cloth and flame lists stay apart: UTA-0055 and UTA-0286 name their files and lookups | UTA-0010 INV-4 test clause, "the `static_assert` in CuratedMaterials.cpp": agrees |
| 12 | `stretchAxis` shadows `wrap` with a local lambda | Q1 | LOW | yes | fixed; the optional cross-file helper queued as `UTA-0429` | `Enlarge.cpp` | → calls the file-level `wrap` | n/a |

## Run-level

- cited_by: tree search for `lampNameOf`, `sortedAndUnique`,
  `MAX_BASE_EDGE`, `MAX_EDGE`, `MADE_EDGE_LIMIT` and `stretchAxis`
  outside `docs/reviews/`: no document names any of them.
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
- collateral: none.
- surfaced: none.
- out_of_scope: none.
- falsified: none.

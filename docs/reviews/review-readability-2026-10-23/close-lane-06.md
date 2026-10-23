# close-findings: lane 06, ubundle

Closes every finding in [`lane-06-ubundle.md`](lane-06-ubundle.md) under
`UTA-0417`. Run 2026-10-23. Roadmap lookup: `roadmap_query` on
`KNOWN_SECTIONS` found no bullet; `UTA-0417` is the item.

The fixes change no baked byte. Proof: `ut-bake` built at `5753e43` and
after the batch baked DM-ArcaneTemple, AS-Frigate, DM-Fetid and CTF-Face
(revision 68, format 32); each bundle and each material light file is
byte-identical.

| # | finding | dim | sev | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|---|---|
| 1 | LITE and FLAM record sizes hand-copied | Q1 | HIGH | yes | fixed | `Sections.h`, `LightSection.cpp`, `FlameSection.cpp`, `LampSection.cpp`, `map-analyser.py`, UTA-0162 | 73 and 32 copied into `LampSection.cpp` → `LIGHT_SIZE` and `FLAME_RECORD` in `Sections.h`; `MIN_LAMP` built from `LIGHT_SIZE`; the Python copy named both ways | UTA-0162 § 4.1 names `LIGHT_SIZE`'s file: amended |
| 2 | `KNOWN_SECTIONS` kept by hand | Q1 | MEDIUM | yes | fixed | `Bundle.cpp` | `KNOWN_SECTIONS = 19` by hand → `KNOWN_IDS`, which `knownId` searches and `KNOWN_SECTIONS` counts | `ubundle.py`'s `if (!knownId(descriptor.id))` mutant: line kept, agrees |
| 3 | `validateShadowMaskVertices` does not exist | Q4 | LOW | yes | fixed | `ShadowMaskSection.cpp` | → `validateShadowMaskAcross` | n/a |
| 4 | TEXS's "unlike the three above" | Q4 | LOW | yes | fixed | `Bundle.cpp` | "unlike the three above" dropped; FLAM, LAMP and SUN arms say they are checked across sections | n/a |
| 5 | unreachable arm names WIRG | Q4 | LOW | yes | fixed | `Bundle.cpp` | WIRG → the last arm, BAKR | n/a |
| 6 | vertex-zone rule omits LAMP | Q4 | LOW | yes | fixed | `Bundle.cpp`, `Sections.h` | LAMP added to both | `ZoneSection.cpp` `validateVertexZones`: agrees |
| 7 | `Bundle.cpp` header's file list | Q4 | LOW | yes | fixed | `Bundle.cpp` | list → one file per section, declared in `Sections.h` | n/a |
| 8 | format version 30 missing | Q4 | LOW | yes | fixed | `Bundle.h` | 30 added (UTA-0387, LPRB's sky visibility) | UTA-0387 § 4: "becomes 30", agrees |
| 9 | `swayCell` "in 16 units" | Q4 | LOW | yes | fixed | `Bundle.h` | "in 16 units" → "divided by 16" | UTA-0055 `S / 16`: agrees |
| 10 | a lamp flame's bare 0 | Q5 | LOW | yes | fixed | `LampSection.cpp`, `Sections.h` | call commented; `lights` note moved onto `validateFlame` | `applyAddedLamps` sets each flame's light: agrees |
| 11 | `Sky.cpp` lambda named `read` | Q3 | LOW | yes | fixed | `Sky.cpp` | `read` → `takeProperties` | n/a |
| 12 | `TileAnswers.cpp` `hash` is a position | Q3 | LOW | yes | fixed | `TileAnswers.cpp` | `hash` → `commentStart` | `tilekind.py` pins the other `hash`: untouched |
| 13 | `AddedLamps.cpp` `sun` and `lite` | Q3 | LOW | yes | fixed | `AddedLamps.cpp` | `lite` → `liteSize`, `sun` → `oldSunIndex` | n/a |
| 14 | `putEncoded` reports any refusal as overflow | Q3 | LOW | yes | fixed | `Codec.h` | a nested refusal set `overflowed_` → kept as `nestedRefusal_`, returned by `finish` under `what`. No red run: the symptom is the harm (a count past 4 Gi) | `encodeGeometry`, the only nested encoder, refuses only on overflow: its message now reaches the caller, prefixed |

## Run-level

- cited_by: tree search for `LIGHT_SIZE`, `FLAME_RECORD`, `MIN_LAMP`,
  `KNOWN_SECTIONS`, `knownId`, `overflowed_` and
  `validateShadowMaskVertices`: UTA-0162 (amended), `map-analyser.py`
  (comment points both ways), `scripts/mutations/ubundle.py` (agrees), and
  `docs/reviews/` (frozen).
- swept: every `must_agree` cell above holds its verdict. Code-pair list:
  absent in this project. Asked of each fix: contradicts, breaks, stales.
- collateral: none.
- surfaced: none.
- out_of_scope: the section order is still written out by hand in
  `Bundle.h`'s and `Bundle.cpp`'s write-order comments, beside the order
  the code emits; not part of finding 2's remedy.
- falsified: none.

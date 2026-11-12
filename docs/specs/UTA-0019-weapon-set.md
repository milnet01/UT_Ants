<!-- ants-spec-format: 1 -->
# UTA-0019 — the core weapon set, as data

**Status:** accepted (2026-11-11), unreviewed. Contract reviews are cancelled for this project (`CLAUDE.md` § Contract reviews); building it is the reviewer.
**Kind:** implement.
**Source:** ROADMAP UTA-0019 (design-2026-09-03; § 3 answered by the user 2026-11-11).
**Blocker for:** UTA-0020, UTA-0029.

**Layman:** The guns. Each one is a block of numbers in a text file, so a different set, such as your super weapons, is a different file rather than new code. You can fire all of them in the map launcher, drawn as simple stand-in shapes until the real models and sounds arrive.

## 1. Goal

A new part, `ugame`, reads a **weapon set**: a hand-editable text file
of weapon definitions. A definition is numbers — damage, refire time,
projectile, spread, ammo — plus one of a few **firing archetypes** for
behaviour that genuinely differs (`docs/design.md` § Weapons, monsters
and pickups are data, not code). `ugame`'s tick fires them on UT99's
rules, measured from the original as UTA-0017 measured movement. The
game ships the stock set. `ut-ants`' walking mode carries every stock
weapon, draws shots and projectiles as stand-in shapes, and can drop
target dummies to shoot.

## 2. Problem

1. **Nothing can be fired or hurt.** There is no health, damage or
   inventory anywhere in `src/`. `uworld::MoveCommand` carries no fire
   button and no pitch. `uinput::Action` has no Fire, AltFire or weapon
   choice.
2. **There is no `ugame`.** `docs/design.md` § The parts gives it the
   rules, weapons included, and design rule 8 makes it their only home.
3. **The renderer draws only the bake.** `urender::Renderer::draw` takes
   a bundle and a camera; the only extra is `setOverlayText`. A
   projectile or a dummy has nowhere to be drawn.
4. **Much of a UT99 weapon is script, not defaults.** `upkg` decodes a
   class's default properties (UTA-0005), so a projectile's `Speed` and
   `Damage` can be read. Refire times come from animation rates and
   instant-hit damage from function bodies, which ADR-0004 never runs.
   Those must be measured.
5. **The 0.2.0 criterion asks for it.** `docs/standards/versioning-overrides.md`:
   "Movement and core weapons measure within tolerance of UT99 (**S2**)".
   **S2** names hammer jumps and shock combos.

## 3. Scope decisions (agreed with the user)

1. **Playable, with a simple look** (user, 2026-11-11). Every weapon
   fires in `ut-ants`. Shots, projectiles, explosions and dummies are
   stand-in shapes, with a crosshair and a text readout. No sounds, no
   weapon models: those wait for `uaudio` (UTA-0056) and animated
   meshes, as their own items.
2. **The stock weapons except two** (user, 2026-11-11), by their
   Botpack class: `ImpactHammer`, `ChainSaw`, `Enforcer`,
   `UT_BioRifle`, `ShockRifle`, `PulseGun`, `ripper`, `Minigun2`,
   `UT_FlakCannon`, `UT_Eightball`, `SniperRifle`. The Redeemer
   (`WarheadLauncher`) and the Translocator each get their own item:
   neither works like a gun. Each name was checked present in the 469
   install's `System/Botpack.u` (`strings -n4 Botpack.u | grep -iw`).
3. **A weapon set is an editable file** (user, 2026-11-11), so a new set
   needs no rebuild. It uses the recipe's `.ini`-style text (UTA-0113
   § 4.2), which UT99 players already edit. No new library.
4. **The numbers are ours, measured once** (user, 2026-11-11). They are
   measured from the original and written into the shipped stock file.
   The game never reads them from a player's install, so every player
   and server plays the same set.
5. **The tolerance is UT99's** (`versioning-overrides.md`'s 0.2.0 row,
   not a choice made here): each stock mode is checked against
   recordings of the original (INV-6).
6. **Every fixed rule is copied; random parts keep the original's
   ranges** (user, 2026-11-12). Where a shot starts, a rocket's speeding
   up, a toss's lift, damage and timing all follow § 4.9. A random part,
   such as the Minigun's damage or the flak's scatter, draws from the
   original's range with the game's own generator (§ 4.5). Each
   archetype gains the keys this needs, so a new weapon is still a file.
   Behaviour no key can express, such as the rocket launcher's circle of
   rockets, is a named rule of its archetype.
7. **The sniper rifle's alt fire zooms** (user, 2026-11-12), as in the
   original: it fires nothing. The zoom is the client's (§ 4.8).

## 4. Design

### 4.1 Files

| File | Holds |
|---|---|
| `src/core/IniText.h`, `.cpp` | § 4.3's line reader, moved out of `urecipe/Recipe.cpp` |
| `src/urecipe/Recipe.cpp` | reads through `IniText`; behaviour unchanged |
| `src/ugame/WeaponSet.h`, `.cpp` | § 4.2's definitions; `parse`, `write`, `shippedDirectory()` |
| `src/ugame/Game.h`, `.cpp` | § 4.5's state and tick |
| `src/ugame/Fire.cpp` | § 4.4's archetypes |
| `src/uworld/Movement.h`, `.cpp` | § 4.6: the command gains aim and buttons; a body takes a push |
| `src/uworld/BodyCollision.h`, `.cpp` | a segment against the level and movers, if `sweep` at zero size cannot serve |
| `src/uinput/Input.h`, `.cpp` | Fire, AltFire, NextWeapon, PrevWeapon, Weapon1 to Weapon0 |
| `src/urender/Renderer.h`, stand-in shapes | § 4.8 |
| `apps/ut-ants/Walker.cpp` | § 4.8: carries the set, drops dummies |
| `weapons/stock.weapons` | the stock set, § 4.7's measured numbers |
| `tools/ut-record/` | § 4.7's weapon runs |
| `tests/unit/IniTextTest.cpp`, `WeaponSetTest.cpp`, `GameTest.cpp` | § 7 |
| `tests/real/RealWeaponTest.cpp` | INV-6 |

### 4.2 The weapon-set file

UTA-0113 § 4.2's rules hold: UTF-8, `#` comments, `[section]` headings,
`key = value`, a byte-order mark skipped, and **strict**: an unknown
section or key, a repeated one, a value out of range or a missing
required key is refused with `MalformedData` naming the line. A header
version above `WEAPON_SET_VERSION` is refused with `UnsupportedVersion`.
`write` emits a canonical form, and `parse(write(s))` equals `s`.

The shape, with values left out (§ 4.7 measures them):

```ini
ut-ants weapons 1

[set]
name = "Stock"

[weapon shock-rifle]
class = botpack.shockrifle   # the UT99 class it stands for (ADR-0004)
slot = 4                     # the number key; ties order by file order
ammo-max = …
ammo-start = …
switch-time = …              # seconds to bring it up

[fire shock-rifle primary]
archetype = instant-hit
refire = …                   # game seconds between shots while held
ammo = 1
damage = …
momentum = …
spread = 0                   # UT99 angle units, 65536 to a turn

[fire shock-rifle alt]
archetype = projectile
projectile = shock-ball

[projectile shock-ball]
speed = …
radius = …
damage = …
splash-radius = …
combo = shock-combo          # an instant hit on it sets this off
```

- **Names** are lower-case kebab words, unique per kind. A `fire`,
  `projectile` or `combo` reference to a missing name is refused.
- **Units** are UT99's: unrealunits, game seconds, angle units. Decimals
  are read with `std::from_chars`.
- **`class`** maps a placed or custom UT99 weapon to a definition. A
  custom class resolves by ancestry to the nearest class a set names,
  as ADR-0004 does for monsters.
- The game reads `weapons/` from `ugame::shippedDirectory()`, compiled in
  as `urecipe::shippedDirectory()` is (UTA-0113 § 4.3). `ut-ants
  --weapons <file>` loads another set; a missing file is refused, not
  passed over. Choosing a set per map is UTA-0029's.

### 4.3 The shared line reader

`Recipe.cpp`'s line helpers (`trimmed`, `valueOf`, `headingOf`,
`boolOf`, `numberOf` and the header check) move to `core::IniText`
unchanged, with a decimal reader added. Both formats then refuse the
same malformed line in the same words. The recipe's tests pass
unedited: the move changes no recipe's reading, so no bake name moves.

### 4.4 Firing archetypes

Each `fire` section names one archetype. Its keys are the archetype's;
another archetype's key is refused.

| Archetype | Does | Stock modes |
|---|---|---|
| `instant-hit` | a segment from the eye along the aim, plus spread; the first body or surface hit takes damage and momentum. `pellets`, `damage-random`, `push-chance`, `push-scale`, `head-damage`, `head-momentum` | Enforcer, Shock primary, Minigun, Sniper |
| `projectile` | spawns `count` projectiles of a named kind, with spread | Bio, Shock alt, Pulse primary, Ripper, Flak, Rocket |
| `beam` | a segment of `range` every tick while held, damage per second; `hit-every` stores the hold and hurts once past it | Pulse alt |
| `melee` | a short segment; on a surface, pushes the firer back | Hammer, Chainsaw |

Two modifiers apply to any archetype: **`charge`**, held to build up and
fired on release (the hammer, Bio alt, the rocket launcher's loading),
and a refire held per mode. The press spends the mode's `ammo`. A
**`charge-step`** charge grows in steps: each time the hold since the
last step passes it, one more `ammo` is spent and the charge grows by one
projectile (`charge-scales = count`) or by that hold (`damage`). A count
charge fires itself once its count is loaded or its ammo is out; a damage
charge waits for release.

A fire mode's **`rule`** names behaviour no key can say (§ 3 item 6). Each
rule belongs to one archetype, and `Fire.cpp` holds what it does; another
archetype's rule, or an unknown one, is refused. Projectile rules:
`rocket-fan` and `grenade-cluster` (§ 4.9, Rockets). Melee rule:
`hammer` (§ 4.9, Hammer).

A melee mode may carry `self-damage`, what a surface hit costs its firer;
`reach`, past which from the firer no body is hurt; and `distance-scale`,
which scales the effect by the hit's distance over it. Every mode has an **`offset`**: where its
shot starts from the eye, forward, right and up, turned by the aim
(§ 4.9).

A projectile kind carries `speed`, `radius`, `gravity`, `gravity-after`,
`lifetime`, `damage`, `momentum`, `splash-radius`, and at most one
behaviour on
striking: **explode**, **bounce** (`bounces`; `bounce-keep` of its speed
along the surface; `bounce-off` of its speed off it, give or take
`bounce-off-random`; `bounce-gravity`, the gravity it falls at from its
first bounce), **stick**
(Bio's glob, exploding after `fuse`), or **split** into another kind
(the flak shell). `combo` names what an instant hit on it sets off.
`expire` says whether it explodes or vanishes when its `lifetime` ends
(the grenade explodes). Striking a body, `splash-from = moved` centres
the blast where the shot's whole move that tick ends, the script's
`Location` (the shock ball, Bio); the default `contact` is where it met
the body. `body-hit = direct` hits the struck body alone, along the
flight, keeping the blast for the level (the Ripper's alt).
`splash-up-max` caps the upward part of each push's direction, left
unnormalised (`Razor2Alt.BlowUp`'s 0.45).

§ 3 item 6's launch keys: `acceleration` along the launch direction, up
to `max-speed`, which also holds a kind launched faster than it from its
first move (the ripper's blade: 1300, then 1200, recorded); `lift`, added upward; `inherit`, a share of the firer's
velocity along the aim. Each `-random` key draws a uniform value within
± its value: `speed-random` and `lifetime-random` about their key, and
`turn-random` for both the yaw and the pitch.

A new archetype is a code change; a new weapon built from existing
archetypes is not. That is the line § 3 item 3 draws.

### 4.5 The game state and its tick

`ugame::Game` holds the bodies, each a `uworld::MoveState`, a
`BodySize`, health and an inventory (weapon, ammo, refire and charge
timers); the live projectiles; a tick count; and one random generator,
a seeded integer generator whose state is part of the game state.

`ugame::tick(world, set, rules, game, commands, dt)` runs, per tick:
each body switches weapon and moves (`uworld::tick`); then each body
fires; then each projectile moves, sweeping its radius through the
`BodyWorld` and against body cylinders, and strikes. A projectile of
radius 0 is traced as a segment, by `BodyWorld::trace`. Damage and
momentum land at the end of the tick. Every body moves before any
fires, so no body's place in the order changes what a shot hits.

- **Bodies are UT99's upright cylinders** (`BodySize`). A segment or a
  projectile hits one where it enters the cylinder. A hit in its upper
  part counts as a head where the mode has `head-damage`; that
  height is `[set]`'s `head-height`, a share of the half-height above
  the centre, measured (§ 4.7).
- **Damage lands as whole numbers**, as the original's integers do:
  truncated, scaled by `[set]`'s `damage-scale` (hardcore's 1.5), and
  truncated again (§ 4.9).
- **Spread** draws from the game's generator, as angle units, turned
  into a direction through `core/Angles.h`. No platform maths: design
  rule 9 and UTA-0049's numeric contract hold for `ugame` as for
  `uworld`.
- **Self-damage and self-push** follow the original: a rocket at your
  feet and a hammer against the floor both throw you (**S2**). How
  much damage the firer takes is `[set]`'s `self-damage`, measured.
- **A body at zero health** is dead. In `ut-ants` the player respawns at
  a `PlayerStart` with the full set, and a dummy is removed.

### 4.6 The command and the push (amends UTA-0017 § 4.2)

`MoveCommand` gains `pitch` (65536 to a turn) and button bits `fire`,
`altFire`, `weaponSlot` (a number key pressed this tick, or none) and
`weaponStep` (next or previous weapon). One
command per tick still carries all input (design rule 12).
`uworld` gains a push: momentum added to a body's velocity, lifting a
walking body into `Falling` where the original does. Movement with no
push is unchanged, so UTA-0017's INV-2 replays as before.

### 4.7 Measuring the original

`ut-record` (UTA-0456) gains weapon runs: its mutator gives the player
one weapon, fires a scripted pattern at 85 fps, and logs per tick each
shot fired, each projectile's position and velocity, and each damage
taken with its momentum. A target is a stand-in pawn with health enough
never to die, at known distances. The runs cover every mode in § 3 item
2, a hammer jump, a rocket jump and a shock combo.

Each number in `stock.weapons` comes from a run, or from the class's
defaults where `upkg::effectiveDefaults` reads it. A comment on each
line says which. A number the original decides by frame rate is
recorded at more than one rate before it is trusted.

The runs are `tools/ut-record/weapons.txt`. The target is
`UTARecTarget`: a pawn the player's size, counted as a player and
carrying an item, so its damage passes through hardcore's scaling as an
armed opponent's does. A run at a missed frame rate (UTA-0456 INV-7)
measures damage, momentum and flight, never timing.

**The rules of § 4.9 come from the original's script.** Export it from
a copy of the install, never into this repository (ADR-0003):
`ucc-bin-amd64 batchexport ../System/Botpack.u class .uc <dir>`, run
from the copy's `System64`, and the same for `Engine.u`. A rule read
there is trusted once a recording agrees.

### 4.8 What `ut-ants` shows

- **Stand-in shapes.** The renderer gains a per-frame list of simple
  shapes in world space: spheres, lines and upright cylinders, each with
  a flat colour, depth-tested against the level. Projectiles are
  spheres, beams and instant hits short-lived lines, explosions a sphere
  that grows and fades, dummies cylinders. Modern explosion effects
  replace that sphere in UTA-0470 (user, 2026-11-12).
- **A crosshair** at the screen's centre, and the overlay text gains the
  weapon's name, its ammo and the player's health.
- **The walking mode carries the whole set** with full ammo. Placed
  weapon pickups wait for the game modes (UTA-0027).
- **A key drops a dummy** a few metres ahead, standing on the floor,
  with the player's size and health.
- **The sniper's zoom** narrows the view; it is the client's, not a tick
  input.

### 4.9 The original's rules

Read from the exported script (§ 4.7), 2026-11-12. "Recorded" marks a
rule a weapon run has confirmed. Class names are Botpack's unless marked
Engine.

**Damage and push** (Engine `Pawn.TakeDamage`, `Pawn.AddVelocity`;
Botpack `DeathMatchPlus.ReduceDamage`):

- Hardcore mode multiplies damage by 1.5, truncated to a whole number,
  for a victim carrying any item. Recorded: 40 takes 60, 45 takes 67.
- A push is momentum ÷ mass (100). On a walking body its upward part is
  raised to at least 0.4 of its length. Recorded.
- A push the body gave itself is scaled by 0.6.
- A push lifts a walking body into falling. Where the body already rises
  faster than 380, a push's upward part is halved.
- A falling projectile (gel, glob, flak slug, grenade) gains speed at half
  the zone's gravity before each tick's move. Recorded 2026-11-12: 475 a
  second², not 950. One that does not bounce (the flak slug) gains the
  other half after the move (`gravity-after`): recorded 950.
- A shot stops 2.5 short of a surface it strikes, along its path.
  Recorded 2026-11-12: flak chunks at 30, 85 and 120 frames a second,
  and the blast of a rocket fired at the floor.
- A bounce ends one move and starts another in the same tick: the shot
  falls and moves for the time left. Recorded.
- A shot's hit on a body is moved 0.4 of the body's radius on along the
  shot (Engine `Pawn.AdjustHitLocation`). Recorded: the flak slug's push.
- A flak slug's chunks start 10 off what it struck, from where its move
  ended, and fly along the slug's own aim, each turned by its own draws
  (Botpack `flakslug.Explode`). Off a body, the direction is from the
  body's centre to the hit. Recorded.

**Where a shot starts** (Engine `Weapon.ProjectileFire`,
`Weapon.TraceFire`, `Inventory.CalcDrawOffset`):

- From the body's centre, plus the eye height, plus the weapon's view
  offset and `FireOffset`, both turned by the aim. Each weapon's
  numbers are its defaults. Recorded for the rocket: 16 above the
  centre, not 27.
- An instant hit leaves out `FireOffset`'s forward part (Shock, Minigun,
  hammer, chainsaw); the Enforcer scales `FireOffset` by 0.35; the sniper
  starts at the eye.

**Per weapon** (each class's fire functions and states):

- **Refire** ends with the firing animation, at the rate each class
  passes. The script does not hold animation lengths, so the runs
  measure refire. Recorded at 30 and 85 fps, 2026-11-12: each interval
  agrees within one tick, so refire does not move with frame rate. The
  exception is the Minigun's spun-up alt: 0.074 s at 30, 0.078 s at 85.
  A hold's first interval can be longer, by `refire-lead`. Recorded
  2026-11-12 at 30, 85 and 120 fps: the Ripper's primary, 0.404 to
  0.409 s against 0.384 s. The script does not say why.
- **Wind-up:** a press at idle on a mode with `wind-up` fires that long
  after, held or not, and spends `press-ammo` at once with no shot. The
  Enforcer's alt turns the gun first (`Enforcer.AltFiring`; recorded
  0.185 s at 30, 85 and 120 fps, a tap too). The Minigun's press uses a
  round and its first bullet follows a 0.13 s sleep (`Minigun2.Fire`,
  `NormalFire`, `AltFiring`; recorded).
- **Hammer:** charge grows 0.75 a second, capped at 1.5. On release it
  traces 120: a body within 90 takes 60 × charge and 66000 × charge;
  the level costs the firer 36 and pushes back 69000 × charge, with
  charge at least 1. Held past a charge of 1, a body 25 ahead fires it;
  the level never does. Recorded. Alt traces 180, scaled by distance ÷ 180: 20 and
  30000 to a body, 24 and 40000 to the firer off the level. Recorded:
  1.01 s of charge gave 45.
- **Chainsaw:** one trace every 0.30 s while held, 20 damage, momentum
  15000 towards the firer. Alt is one slash, 110 damage and 10000,
  landing 0.3 s after the press. Recorded.
- **Enforcer:** 17 damage, momentum 3000; one hit in five, momentum
  15000. Alt fires repeatedly; its accuracy is 0.4 on the first shot
  and grows 0.5 a shot to 3.4.
- **Shock:** 40 damage, 60000. A hit on a ball spends 2 ammo and sets off
  the combo: 165 damage, radius 250, momentum 140000. Recorded: 40.
- **Pulse:** primary every 0.18 s, its start circling the muzzle. Alt
  spends 1 ammo every 0.24 s; the beam reaches 810 in 81-unit pieces.
- **Minigun:** damage 9 to 14 at random; momentum zero, or one hit in
  five a push. Recorded: 9 to 14. Both modes wind up 0.13 s. A held
  primary fires every 0.13 s and presses again every 0.424 s
  (`repress`): a round with no bullet, and a new wind-up
  (`Minigun2.NormalFire`'s `AnimEnd`). A held alt fires every 0.13 s
  (`spin-refire`) until it has been held 0.5 s (`spin-up`), and every
  0.08 s from the shot after. Recorded at 30, 85 and 120 frames a
  second. The original's `Sleep` ends at the tick nearest its time, so
  its cadence moves with the frame rate: 0.147 and 0.073 s at 30. Ours
  does not (INV-5); INV-6's replay rounds these times to each run's
  ticks.
- **Flak:** eight chunks at fixed offsets, each turned up to ±1000 and
  its speed ±100 at random. Alt tosses a slug with lift 200. The slug's
  five chunks keep its aim, not the surface's normal. A chunk bounces
  without limit, keeping 0.8 of its speed along the surface and a random
  0 to 0.64 off it. It flies straight until its first bounce and falls
  at half the zone's gravity after (`bounce-gravity`). In that first
  bounce's tick it falls for the time left and moves for that and a
  whole tick more. Recorded at 30, 85 and 120 frames a second. At most
  three bounces a tick: recorded, a chunk jammed in a corner.
- **Rockets:** the press loads one and spends a round; each 0.88 s held
  loads one more and spends a round, up to six, and the sixth fires them.
  Recorded. Several leave from a circle (radius 4; grenades 7) shifted
  7.5 right and 7 down, at angles 1.0484 rad apart, each up to 4 back at
  random. Rockets fan in yaw: the second and third 600 a radian of their
  angle to the left, the fifth and sixth `(angle − 3) × 600` to the right,
  the fourth along the aim; each after the first × 0.9 to 1.1 in speed.
  Several grenades are each turned up to ±750 in pitch, yaw and roll. A
  rocket gains speed by 50 a second²; a grenade's speed, fuse and bounce
  are part random.
- **Bio:** a gel is tossed with lift 120 and bursts after its fuse or on
  a body. Its fuse is 3 s in flight and 3.8 s once landed. The alt
  charge steps once its hold passes 0.5 s, spending a round and adding
  that hold, until it reaches 4.1. Recorded. The glob's scale is
  1 + 0.8 × charge, and a gel's is 2. The scale multiplies damage,
  momentum and splash radius (75 × scale, at most 250).
- **Sniper:** a hit above 0.62 of the body's half-height is a head: 100
  and 35000; elsewhere 45 and 30000. Alt zooms (§ 3 item 7).

**Explosions** (Engine `Actor.HurtRadius`): damage and momentum fall off
linearly from the victim's edge, not its centre, as `Fire.cpp`'s
`splash` already does.

## 5. Invariants

- **INV-1** — A set file is strict. An unknown key, an unknown section,
  a repeated key, a missing required key, an out-of-range value, a key
  of another archetype, and a reference to a missing name are each
  refused with `MalformedData` naming the line.
  *Test:* `WeaponSetTest.cpp`, one case per refusal.
  *Breaks when:* the parser skips what it does not know.

- **INV-2** — `parse(write(s))` equals `s` for every parsed set,
  the shipped stock set included.
  *Test:* `WeaponSetTest.cpp`.
  *Breaks when:* `write` drops or reorders a key, or rounds a decimal.

- **INV-3** — The shipped stock set parses, and each of § 3 item 2's
  eleven classes maps to exactly one weapon.
  *Test:* `WeaponSetTest.cpp`, reading `ugame::shippedDirectory()`.
  *Breaks when:* a weapon is missing, doubled, or names a missing
  projectile.

- **INV-4** — Behaviour comes from the file. A set differing from the
  stock set in one mode's damage, refire or projectile speed changes
  that result, and only that, in a scripted tick.
  *Test:* `GameTest.cpp`, the stock set against an edited copy.
  *Breaks when:* a number is fixed in code rather than read.

- **INV-5** — The tick is deterministic. A fixed script firing every
  mode, with spread, gives bit-identical game states across runs, across
  the frame mixes of UTA-0017's INV-1, and across GCC, Clang and MSVC.
  The cross-compiler leg hashes the state's integers and the bits of
  its doubles, with no `std::sin` among the inputs.
  *Test:* `GameTest.cpp` on GitHub's matrix.
  *Breaks when:* a draw uses an unseeded or global generator, or the
  platform maths library.

- **INV-6** — The stock set matches the original. Replaying each
  weapon run of § 4.7, each from its recorded start: shots land on the
  same ticks, give or take one; projectiles stay within 1 unit of the
  recording; damage taken is equal; and a body's velocity after a push,
  the hammer and rocket jumps included, is within 1 unit/s. Where the
  original draws at random (§ 3 item 6), the recorded value lies within
  the range our rule draws from, and the rest of the shot matches as
  above.
  *Test:* `RealWeaponTest.cpp`, label `real`, which prints each run's
  worst error.
  *Breaks when:* a measured number or an archetype's rule is wrong.

- **INV-7** — Ammo is never spent below zero. A mode with less ammo than
  its `ammo` fires nothing and changes nothing.
  *Test:* `GameTest.cpp`.
  *Breaks when:* the check follows the spend.

- **INV-8** — A combo needs a hit. An instant hit that enters a shock
  ball's radius sets off its combo and removes the ball; one passing
  outside it, by any margin, does not.
  *Test:* `GameTest.cpp`, a ball and two shots on a hand-built level.
  *Breaks when:* the test uses the ball's centre or ignores its radius.

- **INV-9** — A body is hit where its cylinder is. A segment passing
  at `radius − 0.01` from the axis hits; at `radius + 0.01` it misses;
  above or below the cylinder it misses.
  *Test:* `GameTest.cpp`.
  *Breaks when:* bodies are tested as spheres or boxes.

## 6. Failure modes

- No stock set at `shippedDirectory()`, or a `--weapons` file that
  does not exist or does not parse: `ut-ants` refuses to start the
  walking mode and says which file and line.
- The install or the recordings missing: `RealWeaponTest` fails, naming
  what it lacks; it never skips.
- A projectile spawning inside solid strikes at once, where it is.
- A body inside solid when pushed keeps UTA-0017's rule: it does not
  move that tick.

## 7. Tests

`IniTextTest.cpp` for the moved reader; the recipe's existing tests
unchanged. INV-1, INV-2 and INV-3 in `WeaponSetTest.cpp`. INV-4,
INV-5, INV-7, INV-8 and INV-9 in `GameTest.cpp`, on hand-built levels. INV-6 in
`RealWeaponTest.cpp`, label `real`, reading the recordings
`UTA_WEAPON_RECORDINGS` names, as `RealMovementTest.cpp` does. A
`scripts/mutations/` file for `ugame` covers INV-4, INV-7, INV-8 and
INV-9.

## 8. Alternatives considered (and rejected)

- **Weapons as code, one class each.** A super-weapon set would then
  be a second codebase, which `docs/design.md` rules out.
- **A TOML or JSON file with a library.** A new dependency, and a
  second format beside the recipe's for the same audience.
- **Reading numbers from each player's `Botpack.u`.** The user chose
  one shared table (§ 3 item 4). A modified install would change play,
  and much of a weapon is script that is never read.
- **Numbers compiled in for now.** Rejected by the user: a set would
  need a rebuild, and the format would still be owed to UTA-0029.
- **Hitboxes rather than cylinders.** Not how the original hits, and
  **S2** asks for the original's feel.

## 9. Out of scope

The Redeemer and the Translocator (own items). Weapon models, hand
animation, muzzle flashes and sounds (own item, after UTA-0056).
Modern explosion effects (UTA-0470).
Placed weapon and ammo pickups (UTA-0027). Rocket lock-on, double
Enforcers and the hammer's alt-fire turning projectiles away (own item).
Choosing a set per map (UTA-0029). The weapon wheel (UTA-0020).
Sending a set to a joining player (with networking). Armour.

## 10. What checks this

| Invariant | Checked by |
|---|---|
| INV-1, INV-2, INV-3 | `WeaponSetTest` |
| INV-4, INV-7, INV-8, INV-9 | `GameTest`, and the `ugame` mutation file |
| INV-5 | `GameTest` on GitHub's matrix |
| INV-6 | `RealWeaponTest`, run by hand: nothing runs the real tier automatically |
| § 4.3, the recipe reads as before | the recipe's existing tests |
| § 4.8, firing feels like UT99 | The user, playing it |

## 11. Cross-doc impact

- UTA-0017's spec § 4.2: `MoveCommand` gains aim and buttons (§ 4.6).
- UTA-0113's spec § 4.2: its line reader lives in `core::IniText`.
- `docs/design.md` § The parts: `ugame` exists; `weapons/` ships beside
  `recipes/`.
- UTA-0020: the wheel shows the inventory § 4.5 holds.
- UTA-0029: a per-map weapon set is a file in § 4.2's format.
- ADR-0004: a weapon class resolves through `class` (§ 4.2).

## 12. Cold-eyes loop log

No review: the gate is cancelled here (`CLAUDE.md` § Contract
reviews). Building it is the reviewer. Record:
`docs/reviews/UTA-0019-weapon-set-loop-log.md`.

## 13. Resource cost

A shot is one segment test, or a few projectiles each swept once a
tick. Unmeasured until built; `GameTest` prints a busy tick's time.

## 14. Open questions

None. Whether refire moves with frame rate is answered in § 4.9,
**Refire**.

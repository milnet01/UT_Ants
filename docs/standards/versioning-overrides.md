# Versioning overrides — UT_Ants

Deltas only. Everything not named here follows
`~/.claude/standards/versioning.md` unmodified.

## The `1.0` exit condition

> **MAJOR stays `0` until the author's live Monster Hunt server runs on
> UT_Ants instead of UT99 with nobody wanting to switch back (S8), and
> someone other than the author has authored a map and a player character
> that other players downloaded and used (S6).**

Both halves are observable by someone else, which is what § 4 asks for.

**Every sign of success in `docs/discovery.md` is claimed by a release
below.** **S7** sits at `0.1.0` because a clone that builds without
Unreal Tournament is true from the first tag or it is never true after
it.

## Override — what moves the MINOR inside `0.x`

**Global rule.** `versioning.md` § 4: inside `0.x` a breaking change
bumps the MINOR; everything else, a new capability included, bumps the
PATCH.

**This project.** Inside `0.x` the MINOR is the **milestone**, and the
PATCH is everything else — fixes, and capabilities that do not complete a
milestone.

| Version | Milestone | The release is cut when |
|---|---|---|
| `0.1.0` | Bake and render | A UT map bakes and can be walked through with dynamic lights, shadows, PBR, volumetrics, light shafts and the flashlight (**S1**), from a clone that builds and tests with no Unreal Tournament present (**S7**) |
| `0.2.0` | Movement and weapons | Movement and core weapons measure within tolerance of UT99 (**S2**); a run of platforms can be crossed without losing that (**S11**); and it plays as well on a gamepad as on a mouse. **S9** is not cut here — it asks for a full Monster Hunt round, which arrives at `0.4.0` |
| `0.3.0` | Monsters, bots, Deathmatch | An MH map's monsters spawn (**S3**); DM and TDM play against bots and over a LAN with chat |
| `0.4.0` | Monster Hunt | A real rotation runs — puzzle-solving bots (**S4**), map voting, friendly names, mutators, content download from the host (**S5**), per-map weapon sets a server can toggle (**S10**), the level map showing where the team has and has not been (**S12**), and a full round played on a gamepad (**S9**) |
| `0.5.0` | Map editor | A map is built, hosted, downloaded and played by someone else |
| `0.6.0` | Character authoring | A player character is imported, packaged, downloaded and seen correctly (**S6**) |
| `1.0.0` | — | The exit condition above |

**Why.** The global rule makes the MINOR a fact about breakage, which is
the right answer for something other code imports. Nothing imports a
game. Applied here it would produce a run of patch releases —
`0.1.0`, `0.1.1`, `0.1.2` — through work that changes what the thing *is*,
and arrive at `1.0` from a number that told nobody anything on the way.
The milestones are already the plan, already in `ROADMAP.md`, and are
what a player would use to place a release.

**What this costs.** A breaking change inside `0.x` no longer announces
itself in the version number. Two things carry that instead, and both are
required rather than encouraged:

- Every breaking change is a `### Changed` or `### Removed` entry in
  `CHANGELOG.md` that says, in its first clause, what stops working.
- A release whose section contains one leads with it, per
  `releases.md` § 2.

## Breaking surfaces

`versioning.md` § 3 asks each project to name its own, rather than borrow
a list. Something breaks if a user or a server operator who upgrades has
something that used to work stop working. For this project that is:

- **The map bundle format**, and the inputs that name a bundle. A
  change here invalidates every cached bake — expected, and it must be
  announced, because on a big rotation it is a long wait.
- **The recipe format.** Recipes are authored by hand and by other
  people; a recipe that stops loading is their work broken.
- **The network protocol version.** A client and server that can no
  longer talk is the most visible break available.
- **The content-download manifest**, for the same reason.
- **The stock manifest** of packages Epic shipped, and the withhold test
  `docs/design.md` states over it. A wrong entry either withholds a map
  from every joining player or serves content this project must not serve.
- **Server configuration keys**, and the defaults of any key that changes
  gameplay.
- **Key and gamepad bindings**, and the action names they bind to. A
  binding people have in their hands is a surface whether or not it is
  written down.
- **The command-line interface** of `ut-bake`, `ut-dump` and
  `ut-ants-server` — flags and output shape both.

The editor adds no format of its own: it reads and writes bundles and
recipes, and a character it authors is a `.utab` bundle like a map. Both
are named above.

Not surfaces: internal C++ APIs between the parts in `docs/design.md`,
and the shape of a bundle's *contents* where the format version already
covers it.

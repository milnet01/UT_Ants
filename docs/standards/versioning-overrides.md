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
- A release whose section contains one leads with it. **This project's own
  addition**: `releases.md` § 2 makes the changelog section the single
  description of a release and says nothing about the order of the entries
  inside it, so the ordering is stated here rather than cited to there.

## Which release an item belongs to

**An item lands in the release whose FORMAT must carry it, not the release
whose feature uses it.** `umap` (UTA-0007) and `unav` (UTA-0006) are both
`0.1.0` work and neither is used by `0.1.0`: the level map is **S12**, which
the table above cuts at `0.4.0`, and the wiring graph is what lets `0.4.0`'s
bots solve door puzzles. They land at `0.1.0` because `ubundle` serialises
both, and the map bundle format is the first breaking surface below — a change
to it invalidates every cached bake. Deferring them to the release that uses
them would mean re-baking every map at that point.

**The test is narrow, and it is not licence to pull work forward.** Ask
whether adding the work later would invalidate content already baked or
published under one of this release's own formats. If it would, the work lands
here whatever uses it later. Work that merely relates to a later milestone, or
that is simply convenient to do now, belongs to the release that names it.
Without the narrowing this rule justifies anything, since almost any work can
be argued to be cheaper now than later.

**The test is that cost, and deliberately not the word *frozen*.** Nothing in
this document says which release freezes which format, and § Override's own
cost clause allows a breaking format change later in `0.x` so long as the
changelog announces it — so a rule keyed on freezing would have no answer here.
The cost is checkable against § Breaking surfaces, which is the list of things
whose change breaks something a user already has.

**An item filed in a release's section is not evidence that the release needs
it.** A release is cut on the cut condition its row states and on nothing else
— the signs of success that row names, **and any further condition in the same
cell**. Three rows carry such a condition and `0.5.0`'s names no sign at all,
so a rule reading only the signs would leave that release gated by nothing.

**Two things hold a release, and only these two: its row's cut condition, and
work one of this release's formats has to carry** — the class the first
paragraph describes. Without that second one a conformer cuts `0.1.0` on
**S1** and **S7** alone while the bundle format is still missing the sections
`umap` and `unav` fill, which is the re-baking cost this whole section exists
to prevent.

**Whoever defers an item out of a release's cut condition records that in the
item's own body, in the roadmap store.** An obligation, not a description:
without the note the next session counts the item as work the release is
waiting on, which is the harm above. So read the bodies before counting a
section's open items.

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

**That second exemption is from being listed separately here, never from
being announced.** `docs/design.md` rule 17 makes any new room attribute or
graph edge type a bundle-format version bump — so it is a change to the first
surface above, and it is announced under that one. Only a contents change that
bumps no version is outside this list entirely. Without the distinction a
conformer adds a room attribute, reads *contents are not a surface*, ships no
`### Changed` entry, and every server operator re-bakes a rotation with no
warning.

## Cold-eyes loop log

Rows live in `../reviews/versioning-overrides-loop-log.md`.

# UT_MonsterHunt's gameplay adjustments (2026-11-24)

The user asked on 2026-11-24 that every adjustment UT_MonsterHunt makes to
the live UT99 server can be switched on and off in UT_Ants too. Below is
that project's list of them, copied as its session sent it on 2026-11-24.
It describes UT_MonsterHunt's code, not ours; `UTA-0507` tracks the work here.

Compiled from the code by a helper of the UT_MonsterHunt session. Spot-checked: the
mutator chain (start-mh-server.sh, the MUTATORS= line and its appends), the
--no-super/--team-shouts/--flashlight flags, and settings_catalog.py defaults.
Rows marked "unverified" were not read in code.

**How the chain is switched.** Every adjustment is a mutator in one chain in
`/mnt/Games/PC Games/UT/UnrealTournament-469/start-mh-server.sh`. The server URL is
`<map>?game=BarbiesWorld.MonsterHuntSB?Mutator=$MUTATORS?MaxPlayers=16`. Launcher
settings are per-key ini edits listed in
`/mnt/Games/Scripts/Linux/UT_MonsterHunt/launcher/settings_catalog.py` (SETTINGS),
which holds each default. Source: `/mnt/Games/Scripts/Linux/UT_MonsterHunt/unrealscript/<Pkg>/Classes/`.
"(in-progress mark)" = code exists, roadmap item awaits a play check.

## Rules

| Adjustment | What a player sees | Switch and default | Where |
|---|---|---|---|
| Monster toughness | Monsters 2.5x health, bosses 2.0x, cap 25000 | MHMonsterHealth.ini `HealthMultiplier`, `BossMultiplier`, `HealthCap`; also `mutate monsterhealth` | MHMonsterHealth.uc |
| Monster armour | Monsters spawn with 150 armour and regain 5 a second, up to 150 | MHMonsterHealth.ini `ArmourAtSpawn=150`, `bRegenArmour=True`, `ArmourRegenPerSec=5`, `MaxRegenArmour=150` (live 2026-11-25) | GAME-0297 |
| Spawn protection | 5 s unhurtable after spawn; no telefrag on a start | MHMonsterHealth.ini `bSpawnProtect=True`, `SpawnProtectSeconds=5.0`; `mutate spawnprot` | MHMonsterHealth.uc; GAME-0066 |
| Team awards survive death | Team protection/amp kept through a death | MHMonsterHealth.ini `bKeepTeamAwards=True` | GAME-0068 |
| No landing damage | Hard landings don't hurt | MHMonsterHealth.ini `bNoLandingDamage=False` | GAME-0079 |
| Bots immune to own weapons | Bots don't hurt themselves | MHMonsterHealth.ini `bNoBotSelfDamage=True` | GAME-0074 |
| No player/bot knockback | Players and bots don't push each other | MHMonsterHealth.ini `bBotAntiBoost=True` | settings_catalog.py |
| Barbie's World player rules | Spawn health 500, regen, friendly fire, collision, 75% score kept on death, ammo x10, weapon drops | BarbiesWorld.ini `[BarbiesWorld.KHMBase]` `DefaultHealth`, `bUseRegenHealth`, `bUseAntiBoost`, `FriendlyFireSelfDamageRatio`, `bPlayerCollision`, `retainScoreOnDeathPercent`, `MaxAmmoScale`, `bUseRegenAmmo`, `bDropWeaponsOnDeath`. The other game's own mutator, not ours | settings_catalog.py |
| Awards | Timed health, team amp, team protection, Redeemer | KHMBase `AwardInterval_*` (0 = off), `AwardAmount_*` | settings_catalog.py |
| Lives and double jump | Unlimited lives (-1), double jump on, 2 jumps, height 2.0 | MHEndGate.ini `Lives`, `DJ_Active`, `DJ_maxJumps`, `DJ_Height` (re-applied after MonsterHuntSB.InitGame resets them) | GAME-0177 |
| Random gun per map | Spawn with one random gun per map | MHEndGate.ini `bRandomWeaponForPlayer=True` | settings_catalog.py |
| Super guns | Stock guns/ammo become BPak super versions; spawn holding the set; pulse alt-fire no longer kills its holder; sniper health cost tunable | On (`SUPER=1`); off with `--no-super` / launcher "Super guns on every map". MHSuperGuns.ini `bSwapMapPickups`, `bSwapHeldWeapons`, `bSwapClassicUnreal`, `SniperHealthCost` (10; GAME-0073 wants 5) | MHSuperGuns.uc, MHPulseGun.uc, MHSniperRifle.uc; GAME-0185, GAME-0087 |
| Co-op translocator | Translocate to teammates | Always on (CoopTranslocator12, third-party) | start-mh-server.sh; GAME-0115 |
| FatBoy | Stock Botpack.FatBoy | Always on; effect unverified | start-mh-server.sh |
| Bot skill | Skill 7, accuracy/alertness 1.0, aggression 0.7, orders Attack, jumpy, no camping | MHBotSkill.ini `BotSkill`, `Accuracy`, `Alertness`, `StrafingAbility`, `CombatStyle`, `CampingRate`, `bJumpy`, `BotOrders` | MHBotSkill.uc |
| Bots step aside, idle, unstick | Bots yield to players, idle-animate, stuck bots get fallback orders | MHBotSkill.ini `bYieldToPlayers`, `bIdleWhenStill`, `bFreeStuckBots` (all True), `StuckDistance`, `StuckChecksToFree`, `FallbackOrders` | GAME-0015, 0016, 0065, 0111 |
| Bots hunt and escort | The bot nearest each human escorts them; the others hunt the nearest reachable monster | MHBotSkill.ini `bHuntMonsters=True` (live 2026-11-25) | GAME-0294 |
| Bots open switch doors | A bot firing into a shut switch door goes to its switch, giving up after 60 s; on the way it fights only monsters within 800 uu, unless a farther one hurt it in the last 3 s | MHBotSkill.ini `bDoorSwitch=True`, `DoorGiveUpSeconds=60`, `DoorFightRadius=800`, `DoorPainSeconds=3` (live 2026-11-26) | GAME-0295 |
| Bots catch up | A bot left far behind is moved to a navigation point near its human, else to one of the human's last 8 floor spots (150-1200 uu away, within 256 uu height, in sight, unoccupied) | MHBotSkill.ini `bTrailFallback=True`, `TrailGap=200` (live 2026-11-26) | GAME-0180 |
| Bots leave UltimaProtos to humans | — | Unverified, GAME-0229 in progress | ROADMAP |
| Stuck-monster rescue | Monsters inside walls lifted or moved to a path node | Always on; no ini keys found | MHStuckMonsters.uc; GAME-0011 |
| Exit gate | Exit shut until map mostly cleared (`Percent=2`, `MinRemaining=3`); opens after 180 s stalled or 2700 s | MHEndGate.ini `bEnabled=True`, `StallSeconds`, `MaxSeconds` | MHEndGate.uc; GAME-0101, 0149 (fixes in progress: 0155, 0179, 0280, 0281) |
| End placement | Exit at the world corner moved to the far end of the path network | MHEndPlace.ini `bEnabled=True`, `bLogOnly=False`, `CornerLimit=32000` | MHEndPlace.uc; GAME-0097 |
| Mover fixes | One-pull levers stay; untagged doors/buttons don't reverse together | Always on, no ini | MHMoverFix.uc; GAME-0191 |
| Bot hop | A bot moved past a translocator-only gap, per map (one map so far); never a person | MHBotHop.ini `Hops[]`, `HoldSeconds`, `TargetRadius`; always in chain | MHBotHop.uc; GAME-0271 |
| Per-map class fixes | Fixes for UltimaProtos, MultiMissile, CybotLauncher, TheMiner (NaliFix), JumpDetectorVX (RocketXFix), SmokeLightning (SmokeFix), SteelBox (DecoFix), DM_Invis (DoomFix), only on maps placing them | MHWaypointFix.ini `ClassFixes[0..7]`; clear an entry to switch it off | MHWaypointFix.uc AddClassFixes; GAME-0088, 0181, 0048, 0218, 0254 |
| Doom map tuning | On MH-[LineARC]-Doom2025 only: Doom guns 4x damage, 2x ammo, other fixes | MHDoomFix.ini `DoomGunDamageScale=4.0`, `DoomAmmoScale=2.0`, `BoostMaps[]` | unrealscript/MHDoomFix, MHDoomGuns; GAME-0256..0260, 0264 |
| Quiet team shouts | Bots don't shout "I'm on your team!" | On; off with `--team-shouts` / launcher box | MHQuietTeam.uc; GAME-0183, 0184 |
| Flashlight stays on | Light returns after death; optionally everyone starts lit | Off; `--flashlight` adds MHLight; MHLight.ini `bStartOn` | MHLight.uc; GAME-0189, 0270, 0282 |
| View shake cap | A hard hit doesn't blind | MHNoShake.ini `bLimitShake`, `MaxRollDegrees`, `MaxVertUnits`, `MaxShakeSeconds`; defaults unverified | MHNoShake.uc; GAME-0108 |
| Shoot-switch bots | Bots shoot needed switches | Not in the live chain (probe-only) | MHShootSwitch.uc; GAME-0053 |
| Perf log | Log only | Always on | MHPerfLog; GAME-0230 |

## Interface

| Adjustment | What a player sees | Switch and default | Where |
|---|---|---|---|
| Monsters still to come | HUD adds ", still to come: up to N" | Always on | MHMonsterCount.uc; GAME-0076 |
| Bot kills on scoreboard | Bot kills and time instead of n/a | Always on (in-progress mark) | MHBotKills.uc; GAME-0147 |
| Map-vote window | Opens at map end; maps coloured finished/started/never; ratings; clean names | MHVoteGUI.ini `bOpenAtMapEnd=True`, `OpenDelay=3.0`, `MaxResults=60`, `StartedAfterSeconds=60` | unrealscript/MHVoteGUI; GAME-0055, 0067, 0081, 0198 |
| Console vote/admin | `mutate vote`, `votemap`, `skipmap`, `map`, `thismap`; playlists | MHAdmin.ini `bVoteAtMapEnd=True`, `VoteEndOfMapSeconds=35`, `VoteDurationSeconds=30`, `VoteMapChoices=16`, `bOfferNextInRotation=True`, `bRequireAdmin=False` | MHAdmin.uc, MHPlaylist.uc; GAME-0190 |
| Hints | `mutate hint` names the next step; glowing trail; nudge after 45 s still | MHHint.ini `bAutoHint=True`, `StuckSeconds=45`, `AutoHintCooldown=120`, `bTrail`, `TrailSeconds`, ... (trail defaults unverified) | MHHint.uc; GAME-0013, 0017, 0145, 0151, 0249, 0282 |
| Last monsters | With 5 or fewer monsters left, `mutate hint` lists them and the trail leads to the nearest | MHHint.ini `LastMonsters=5` (0 off; live 2026-11-25) | GAME-0296 |
| Weapon wheel | Hold a pad button, pick a gun with the right stick | Always on; client User.ini binds "Button bExtra0" | unrealscript/MHWeaponWheel; GAME-0261 |
| Controller camera/dodge | Stick speed; double-tap dodge | Client User.ini `JoyR`, `JoyU`, `DodgeClickTime` | settings_catalog.py; GAME-0182, 0184 |

## Maps (load-time repairs; offline tools have no live switch)

| Adjustment | What it does | Switch and default | Where |
|---|---|---|---|
| Waypoint renumbering | Broken MonsterWaypoint order fixed at level start | MHWaypointFix.ini `bEnabled=True`, `bLogOnly=False`, `DelaySeconds=6` | MHWaypointFix.uc |
| Extra player starts | Maps with 0-1 starts get up to 8 | MHWaypointFix.ini `bAddStarts=True`, `StartsWanted=8`, `StartSearchRadius=1024`, `StartSeparation=96` | GAME-0075 |
| Path back-links | Half-written bot paths finished at load | Always on | MHBackLink.uc; GAME-0096 |
| Wall blockers | Walk-through walls closed, bot links through them cut | MHWallFix.ini `bEnabled=True`, `Blockers[]` | MHWallFix.uc; GAME-0006, 0007 |
| Offline map repair | Fixed map copies (mapfix, autofix, wallfix); promotion is by hand | On demand: `python3 analysis/mapcheck fix`, `analysis/autofix.py` | GAME-0124, 0268, 0284 |
| Library curation | Duplicates, unplayable and voted-off maps removed; new maps staged | By hand: analysis/ua_*.py, content_dupes.py, votedoff.py | GAME-0071, 0175, 0199, 0222, 0224, 0277 |

## Gaps

No single list existed before this one. Closest: `launcher/settings_catalog.py`,
`docs/map-fault-checks.md`, README "The UnrealScript, and where it really lives".
Not read in code: GAME-0229, GAME-0154, MHNoShake and MHHint trail defaults,
MHStuckMonsters keys, per-map fix sites (GAME-0150, 0250, 0262).

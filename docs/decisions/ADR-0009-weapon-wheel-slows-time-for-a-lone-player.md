# ADR-0009: The open weapon wheel slows time to 20%, only for a lone human player

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

UTA-0020's weapon wheel needs an answer to whether it pauses or slows
time. UT99 has no wheel, so there is no precedent. `docs/design.md`
§ Decided later, deliberately sets the deadline: the start of `0.2.0`,
against a prototype. The reason is that a time scale the server must
agree on is an input to the fixed tick (rules 9 and 12).

In a match with two or more human players, one player cannot slow the
world for everyone else. So the choice only exists when a single human
is playing, alone or against bots.

The user tried a prototype on a controller on 2026-11-17. It had four
modes: time keeps running, slows to 50%, slows to 20%, and stops. The
user chose 20%. The session had recommended keeping time running, so
that the game plays the same alone and online.

## Decision

While the wheel is open, the game runs at 20% speed, but only while
exactly one human player is in the game. With two or more humans, the
wheel leaves time alone.

- **Whose rule:** `ugame`'s (rule 8). The server decides the time scale.
  The client predicts it with the same rule, from the same sources
  (rule 9).
- **How it reaches the tick:** each input command carries a "wheel open"
  bit (rule 12). The scale slows how fast game time follows the wall
  clock. A tick's length in game seconds does not change, so movement
  and weapons give the same results tick for tick.
- **When a second human joins** during a slow-down, time returns to full
  speed at once.

## Consequences

- `MoveCommand` gains a field. `uworld`'s tick clock takes a time scale.
  `ugame` owns the rule that sets it. This is the change rules 9 and 12
  required to land in `0.2.0`, not later.
- A replayed command stream reproduces each slow-down, since the bit is
  in the commands.
- Bots, projectiles and effects slow with everything else, since they
  share one world clock. How sound and the drawn effects follow the
  slowed clock is UTA-0020's spec to settle.
- **The cost the user accepted:** a player who learns to choose weapons
  under slowed time alone gets no slow-down online. The skill does not
  fully carry over.
- 20% applies to `0.2.0`. Changing the figure is a constant in `ugame`.
  Changing who gets the slow-down, or making the wheel pause, needs a
  new ADR.

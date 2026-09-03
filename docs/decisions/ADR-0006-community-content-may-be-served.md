# ADR-0006: The quarantine restricts Epic-derived content; community-authored content may be served onward

- **Status:** Accepted
- **Date:** 2026-09-03
- **Supersedes:** the *"served to another player"* clause of
  [ADR-0003](ADR-0003-ship-the-recipe-not-the-content.md) § Decision. The
  rest of that ADR stands.

## Context

ADR-0003 states the quarantine as *"Nothing under `content/` is
committed, published, or served to another player"*, while its own table
puts community-made maps in the column that travels freely, and the
project's whole purpose is a Monster Hunt rotation of community maps that
a host sends to joining players.

Both cannot be true. `content/` holds the host-download cache, so a
server passing a community map to the next player is serving something
under `content/` — which the rule forbids and the design requires.

The cause is that one sentence tried to carry two different rules: what
this **repository** may publish, and what a **running server** may send.
They have different subjects and different reasons.

## Decision

Split them.

- **The repository publishes nothing under `content/`.** Unchanged, and
  it is what the pre-push and CI guard checks.
- **A running server may send a player any content whose author meant it
  to be shared** — community maps, characters, recipes — exactly as
  Unreal Tournament servers have always done. Where it is cached does
  not decide this; who wrote it does.
- **A server never sends Epic's content.** Stock maps, textures and
  sounds come from the joining player's own installation, which the game
  has already required (ADR-0003) before it will start.

## Consequences

The Monster Hunt rotation works, which ADR-0003 as written forbade.
Community authors keep the distribution their work has always had, and
the repository's guarantee is untouched.

The cost is that one rule became two, and the second cannot be checked
mechanically: whether an author meant their map to be shared is a fact
about the author, not about the file. A server operator can breach it,
and nothing here will catch them.

What has to be true: a server's content list must distinguish what came
from the player's own Unreal Tournament install from what the community
wrote, or the second rule has nothing to act on. That distinction is
already available — anything the baker read out of the player's install
is Epic's, and anything downloaded or authored here is not.

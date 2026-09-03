# UT_Ants — Design

> **Purpose — so the shape is decided once, and anyone can tell where a
> new piece of work belongs and what it is allowed to touch.**

**This document is a gate.** Work is not broken into items until it is
agreed — `~/.claude/workflow.md` § 2. It passes when someone can take any
item off the queue and say which part it belongs in and what it may
touch.

**Status:** not started.

## The parts

> One line each: what this part is responsible for. You cannot place work
> without knowing the parts.

## What may depend on what

> The load-bearing section — this is what stops the shape rotting, and it
> is what the pick-an-item gate reads. State the rules, not just the
> arrows: which parts may call which, and which must never.
>
> A diagram is welcome and is not a substitute. It renders this section;
> it does not replace it.

## What every part does the same way

> Errors, state, persistence, logging. Decided once here, or every item
> invents its own.

## The stack, and what it rules out

> What was chosen, one sentence of why each, and the runner-up. The
> "rules out" half matters more than it looks: it is what stops a later
> item assuming something the stack cannot do.

## Close calls

> Each gets an ADR in `docs/decisions/`, so it is not re-argued later.
> List them here with a link; the reasoning lives there.

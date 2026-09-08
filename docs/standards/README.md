# Standards for UT_Ants

**The standards are global and are read in place**, at
`~/.claude/standards/`. There is exactly one copy of each, shared by
every project on this machine. Nothing copies them here.

`~/.claude/standards/README.md` is the index and owns the three cases: a
project follows the global set, overrides one with a deltas-only file, or
owns a standard outright.

## This directory

Two things belong here, and nothing else:

1. **Overrides** — a deltas-only file naming where this project departs
   from a global standard, or from the machine-wide `workflow.md`, and
   why. `CLAUDE.md` § Overrides routes both here. `workflow.md` is a
   foundation document rather than a standard, so without naming it this
   list said "two things and nothing else" while admitting neither the
   workflow override sitting beside this file nor the rule that put it
   there.
2. **Standards this project owns outright** — a rule that is genuinely
   about this project and has no global equivalent.

If this directory is empty, this project follows the global set
unmodified. That is the common case and needs no file to say so.

**A copy of a global standard does not belong here.** Two copies are two
standards that will disagree, and the one nobody is looking at will be
the one being followed.

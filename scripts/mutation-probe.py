#!/usr/bin/env python3
"""Ask, of each rule a spec names, whether any test actually grades it.

    scripts/mutation-probe.py ubundle
    scripts/mutation-probe.py ubundle --asan
    scripts/mutation-probe.py ut-paths --coverage

A subject is one file in scripts/mutations/, its mutations each tagged with
the spec invariant they break. --coverage lists the spec's invariants and the
mutations each has, building nothing: an invariant with none is where to
write the next one.

WHY THIS EXISTS. A green test proves nothing about the rule it names. On
UTA-0007 three of four tier-1 cases passed on first writing and each was
decided by something OTHER than the rule in its title -- a table entry that
already returned the refusal, a range check that subsumed the guard, and a
loop bound that rejected the fixture early. None of that is visible from
reading the test. CLAUDE.md SS Build and test records it, and this script is
how the same question gets asked mechanically.

On UTA-0008 it asked 57 times and found FOUR more: a string-overrun case in a
section too short for the node count that came before it, an unknown-id case
whose payload was too short to decode either way, a table-bound case whose bad
entry sat where a different rule caught it first, and a graph case that broke
two rules at once. Every one passed. Every one read correctly.

HOW TO READ THE RESULT.

  KILLED       removing the rule turned some test red. The rule is graded.
  SURVIVED     removing the rule changed nothing. Either the fixture grades
               something else -- suspect this FIRST, per CLAUDE.md -- or the
               rule is genuinely redundant, in which case declare it in
               EXPECTED_SURVIVORS with the reason.
  NOT-APPLIED  the search text no longer matches. The mutation is stale and
               is telling you nothing; fix it or drop it.
  COMPILE-FAIL the mutation does not build. Same -- it is not a result.

Exit status is 0 when the survivors are exactly the declared ones. A NEW
survivor exits 1: it means an edit made some rule untested and no other check
in this repository would have said so.

THREE TRAPS, ALL PAID FOR ONCE ALREADY.

  1. Restore by rewriting and touching. `mv` restores an older mtime, ninja
     skips the rebuild, and the NEXT mutation is graded against the previous
     one's binary.
  2. Baseline the filter, not just the suite. A Catch2 tag that matches no
     tests exits non-zero, under which every mutation reads as KILLED. The
     baseline below asserts a non-zero case count.
  3. Never put `ulimit -v` around a sanitizer binary. ASan reserves tens of
     terabytes of address space for its shadow map; a cap makes it die at
     startup, before any test runs, and that also reads as KILLED. Under
     --asan the bound is ASAN_OPTIONS and a timeout instead.
"""

import argparse
import importlib.util
import os
import pathlib
import re
import signal
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# One file per subject in scripts/mutations/, each defining SUBJECT (UTA-0083).
# A subject names the spec whose invariants its mutations break, so the report
# is per invariant -- and an invariant with no mutation at all is named rather
# than passed over.
MUTATIONS = ROOT / "scripts" / "mutations"


def load_subjects():
    # No __pycache__ beside the lists: they are data, and a cache there is an
    # untracked directory in every checkout that runs this.
    sys.dont_write_bytecode = True
    subjects = {}
    for path in sorted(MUTATIONS.glob("*.py")):
        spec = importlib.util.spec_from_file_location(f"mutations_{path.stem}", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        subjects[path.stem] = module.SUBJECT
    return subjects


SUBJECTS = load_subjects()

# A spec's invariants in spec-format.md SS 3.7's bullet form. A paragraph form
# parses to none, which the report says rather than reading as full coverage.
INVARIANT = re.compile(r"^- \*\*(INV-\d+)\*\*", re.MULTILINE)


def invariants(subject):
    return INVARIANT.findall((ROOT / subject["spec"]).read_text(encoding="utf-8"))


def per_invariant(subject, results):
    """Print each invariant of the subject's spec with what its mutations did.

    `results` maps a label to its state; None when nothing ran (--coverage).
    """
    named = invariants(subject)
    if not named:
        print(f"  {subject['spec']} parses to NO invariants -- SS 3.7's bullet form?")
    rows = {}
    for inv, label, *_ in subject["mutations"]:
        rows.setdefault(inv or "(none)", []).append(label)
    print(f"\n=== per invariant, {subject['spec']} ===")
    for inv in named + sorted(k for k in rows if k not in named):
        labels = rows.get(inv, [])
        if not labels:
            print(f"  {inv:8} no mutation")
            continue
        if results is None:
            print(f"  {inv:8} {len(labels)} mutation(s)")
            continue
        states = [results.get(label) for label in labels]
        ran = [x for x in states if x is not None]
        print(f"  {inv:8} {len(labels)} mutation(s): killed {ran.count('KILLED')}, "
              f"survived {ran.count('SURVIVED')}, other {len(ran) - ran.count('KILLED') - ran.count('SURVIVED')}"
              + ("" if len(ran) == len(labels) else f", not run {len(labels) - len(ran)}"))


def run(cmd, timeout=900, env=None):
    # An argv list, never a shell string (python.md SS Idioms).
    return subprocess.run(cmd, cwd=ROOT, capture_output=True,
                          text=True, errors="replace", timeout=timeout, env=env)


def probe(build_dir, subject, label, rel, old, new, test_cmd, env):
    path = ROOT / rel
    # Bytes, not text: read_text/write_text translate line endings and
    # re-encode, so the "restored" file need not be the original (UTA-0222).
    original = path.read_bytes()
    old_bytes, new_bytes = old.encode(), new.encode()
    if old_bytes not in original:
        return "NOT-APPLIED"
    if original.count(old_bytes) != 1:
        return "NOT-UNIQUE"
    path.write_bytes(original.replace(old_bytes, new_bytes, 1))
    # Rewrite and touch. A restore that leaves an older mtime lets ninja skip
    # the rebuild, and the next mutation is graded against this one's binary.
    os.utime(path, None)
    try:
        if run(["cmake", "--build", build_dir, "--target", subject["target"]]).returncode != 0:
            return "COMPILE-FAIL"
        try:
            return "KILLED" if run(test_cmd, timeout=300, env=env).returncode != 0 else "SURVIVED"
        except subprocess.TimeoutExpired:
            return "KILLED"
    finally:
        path.write_bytes(original)
        os.utime(path, None)


def main():
    # A SIGTERM (a killed push, a timeout) must still run probe's finally, or
    # the mutated source is left in the tree (UTA-0222). SystemExit unwinds
    # through it the way Ctrl-C's KeyboardInterrupt already did.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("subject", choices=sorted(SUBJECTS))
    parser.add_argument("--asan", action="store_true",
                        help="probe in build-asan/ instead of build/, for rules whose "
                             "removal is undefined behaviour rather than a wrong answer")
    parser.add_argument("--only", help="probe only mutations whose label contains this")
    parser.add_argument("--coverage", action="store_true",
                        help="list the spec's invariants and the mutations each has, building "
                             "nothing -- where to write the next mutation")
    args = parser.parse_args()

    subject = SUBJECTS[args.subject]
    if args.coverage:
        per_invariant(subject, None)
        return
    build_dir = "build-asan" if args.asan else "build"
    if not (ROOT / build_dir).is_dir():
        sys.exit(f"no {build_dir}/ -- CLAUDE.md SS Build and test says how to configure it")

    env = dict(os.environ)
    if args.asan:
        # NOT a ulimit. ASan reserves tens of terabytes of address space for
        # its shadow map, so a virtual-memory cap kills it at startup, before
        # any test runs -- and that reads as a mutation KILLED.
        env["ASAN_OPTIONS"] = "allocator_may_return_null=0:max_allocation_size_mb=2048"
    test_cmd = [f"./{build_dir}/{subject['binary']}", subject["filter"]]

    if run(["cmake", "--build", build_dir, "--target", subject["target"]]).returncode != 0:
        sys.exit("the unmutated tree does not build")
    base = run(test_cmd, env=env)
    if base.returncode != 0:
        sys.exit("the unmutated tree is already red -- fix that before probing")
    # A filter matching NO tests exits non-zero, under which every mutation
    # reads as KILLED. Assert it matched some. Not an exact count: that went
    # stale with every test added, and refused to run (UTA-0083).
    matched = re.search(r"\b(\d+) test cases?\b", base.stdout)
    if not matched or int(matched.group(1)) == 0:
        sys.exit(f"filter {subject['filter']} matched no test cases -- "
                 "a tag typo would make every mutation read as killed")
    print(f"baseline green in {build_dir}/, {matched.group(1)} cases matched\n")

    results = []
    try:
        for _, label, rel, old, new in subject["mutations"]:
            if args.only and args.only not in label:
                continue
            state = probe(build_dir, subject, label, rel, old, new, test_cmd, env)
            results.append((state, label))
            print(f"{state:>13}  {label}", flush=True)
    finally:
        # The sources are restored after each mutation, the binary is not: left
        # alone, the next test run grades the LAST mutant (UTA-0083).
        if run(["cmake", "--build", build_dir, "--target", subject["target"]]).returncode != 0:
            print(f"WARNING: rebuilding the restored tree failed; {build_dir}/ holds a mutant")

    expected = subject["expected_survivors"]
    if args.asan:
        expected = {k: v for k, v in expected.items() if k not in subject.get("killed_under_asan", ())}
    survived = [label for s, label in results if s == "SURVIVED"]
    broken = [(s, label) for s, label in results if s in ("NOT-APPLIED", "NOT-UNIQUE", "COMPILE-FAIL")]
    unexplained = [label for label in survived if label not in expected]
    # A declared survivor this run KILLED: the declaration is stale, and "the
    # survivors are exactly the declared ones" is false (UTA-0222).
    probed = {label for _, label in results}
    # Except, outside --asan, the rules only ASan can grade: their removal is
    # undefined behaviour, which kills some runs and not others (measured on
    # readBytes' bound: survived, killed, survived).
    undefined = () if args.asan else subject.get("killed_under_asan", ())
    stale = [label for label in expected
             if label in probed and label not in survived and label not in undefined]

    per_invariant(subject, dict((label, state) for state, label in results))
    print(f"\n=== {args.subject} in {build_dir}/ ===")
    print(f"killed {sum(1 for s, _ in results if s == 'KILLED')} of {len(results)}")
    for label in survived:
        note = expected.get(label, "NOT DECLARED -- no test grades this rule")
        print(f"  survived: {label}\n            {note}")
    for state, label in broken:
        print(f"  {state}: {label}")
    for label in stale:
        print(f"  declared a survivor, but KILLED: {label}\n"
              "            a test now grades it -- drop it from expected_survivors")

    if unexplained or broken or stale:
        print("\nA new survivor means some rule is no longer graded. Suspect the FIXTURE "
              "first: ask which rule makes it fail, and whether that is the rule it names.")
        sys.exit(1)
    print("\nsurvivors are exactly the declared ones")


if __name__ == "__main__":
    main()

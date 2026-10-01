#!/usr/bin/env python3
"""Join direct_sweep's TSV with per-module facts, print the table, and GATE.

WHY THIS IS NOT DONE IN BASH.  The obvious

    while IFS=$'\t' read -r module verdict reason ...

is wrong for this file: bash treats tab as IFS *whitespace*, so a run of
delimiters collapses and an EMPTY field vanishes.  `reason` is empty on every
successful row, so every later column shifted left and the table printed
step1_ms as the checkDesign time and wall_s as the cycle count --
`accepts(94ms) ran-6.9cyc` for a row whose real values were `check_ms=48
cycles=4`.  The evidence was right; the join misreported it.  `csv.DictReader`
reads by NAME and preserves empties.

FAIL CLOSED.  Printing a table is not the job; deciding is.  Exit 0 requires
EVERY REQUESTED module to satisfy all of:

    emit=0, gates=0, typecheck=0, compiles theorem elaborated,
    a direct-sweep row exists, verdict=ACCEPTED, cycles=<requested>,

plus direct_sweep rc=0 and the swept row set being exactly the requested set.
An earlier version checked only the sweep's rc and row set, so a module that
failed Phase A was simply absent from `--expect` and the validator still
exited 0 -- and if EVERY module failed Phase A, nothing was swept, the expected
set was empty, and the run "succeeded" having validated nothing.

The differential-simulation column is reported but NOT gated on: it is finite
randomized+directed simulation, never a proof, and its artifact may predate the
certificate in front of it (then it reads `prior-smoke`).
"""
import argparse
import csv
import json
import sys

COLS = ("MODULE", "EMIT", "GATES", "TYPECHK", "COMPILES_THM",
        "CHECKDESIGN", "DIRECT_SIM", "RTL_DIFFSIM", "AXIOMS")
FMT = "%-14s %-6s %-6s %-8s %-15s %-15s %-13s %-12s %s"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tsv', default='')
    ap.add_argument('--facts', required=True, help='JSON: {module: {...}}')
    ap.add_argument('--requested', required=True,
                    help='comma-separated modules the caller asked for; ALL must pass')
    ap.add_argument('--generated', default='',
                    help='comma-separated modules that survived phase A')
    ap.add_argument('--sweep-rc', type=int, default=0)
    ap.add_argument('--cycles', type=int, default=4)
    a = ap.parse_args()

    requested = [m for m in a.requested.split(',') if m]
    if len(set(requested)) != len(requested):
        dup = sorted({m for m in requested if requested.count(m) > 1})
        print(f"FATAL: duplicate module(s) requested: {','.join(dup)}", file=sys.stderr)
        print("       the facts map is keyed by name, so a duplicate silently "
              "overwrites its twin", file=sys.stderr)
        return 2
    if not requested:
        print("FATAL: no modules requested", file=sys.stderr)
        return 2

    generated = [m for m in a.generated.split(',') if m]
    facts = json.load(open(a.facts))

    rows, problems = {}, []
    if a.tsv:
        try:
            with open(a.tsv, newline='') as fh:
                for r in csv.DictReader(fh, delimiter='\t'):
                    m = r.get('module', '')
                    if m in rows:
                        problems.append(f"duplicate sweep row for {m}")
                    rows[m] = r
        except OSError as e:
            problems.append(f"cannot read {a.tsv}: {e}")
    elif generated:
        problems.append("no sweep TSV, but phase A produced certificates to sweep")

    if a.sweep_rc != 0:
        problems.append(f"direct_sweep exited {a.sweep_rc}")

    missing_a = [m for m in requested if m not in generated]
    if missing_a:
        problems.append("failed phase A (generate/typecheck): " + ",".join(missing_a))
    if set(rows) != set(requested):
        if set(requested) - set(rows):
            problems.append("no sweep row: " + ",".join(sorted(set(requested) - set(rows))))
        if set(rows) - set(requested):
            problems.append("unexpected sweep row: " + ",".join(sorted(set(rows) - set(requested))))

    trust_sweep = not any(p.startswith(("direct_sweep exited", "cannot read",
                                        "duplicate sweep row")) for p in problems)

    print(FMT % COLS)
    for m in requested:
        f = facts.get(m, {})
        chk = sim = "not-run"
        r = rows.get(m)
        if r is not None:
            v = r.get('verdict', '') or '?'
            if v != 'ACCEPTED':
                chk, sim = v, "-"
                problems.append(f"{m}: direct_sweep verdict {v}"
                                + (f" ({r.get('reason','')[:80]})" if r.get('reason') else ""))
            elif not trust_sweep:
                chk = sim = "UNTRUSTED"
            else:
                chk = f"accepts({r.get('check_ms','?')}ms)"
                cyc = r.get('cycles', '')
                sim = f"ran-{cyc}cyc" if cyc else "no-cycles"
                if str(cyc) != str(a.cycles):
                    problems.append(f"{m}: ran {cyc or 0} cycle(s), expected {a.cycles}")
        for key, want, label in (("emit", "0", "lean emit"), ("gates", "0", "static gates"),
                                 ("tc", "0", "typecheck")):
            if str(f.get(key, "?")) != want:
                problems.append(f"{m}: {label} = {f.get(key, '?')} (want {want})")
        if f.get("thm") != "elaborated":
            problems.append(f"{m}: compiles/step theorem {f.get('thm', '?')}")
        print(FMT % (m, f.get('emit', '?'), f.get('gates', '?'), f.get('tc', '?'),
                     f.get('thm', '?'), chk, sim, f.get('diffsim', '?'), f.get('ax', '?')))

    if problems:
        print("\nNOT ACCEPTED:", file=sys.stderr)
        for p in dict.fromkeys(problems):
            print(f"  - {p}", file=sys.stderr)
        return 1
    print(f"\nall {len(requested)} requested module(s) accepted "
          f"(emit/gates/typecheck 0, theorem elaborated, checkDesign ACCEPTED, "
          f"{a.cycles} simulator cycles). Differential smoke is reported, not gated.")
    return 0


if __name__ == '__main__':
    sys.exit(main())

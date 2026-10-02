#!/usr/bin/env python3
"""The current coverage count, derived rather than asserted.

A coverage number copied forward from a document is the one thing in this
project that has been wrong most often: the 2026-08 CORE-ET census over-reported
gating by 16 modules, and a "105 of 122" claim once sat beside a 102-row table.
So this reads the primary artifacts every time and recomputes:

  BASE      pass/lean/CENSUS_D2_122.tsv -- one full run over the whole corpus.
  LATER     every generated/census_d2/<run>/ with sweep evidence.  A run's
            direct_sweep.tsv is authoritative; when a run was stopped before its
            table was written, logs/direct_sweep.log still carries the verdict
            line, and that is recorded as WEAKER evidence, never as equal.
  CORPUS    pass/lean/COREET_MODULES.txt -- the set a count is a fraction OF.

It FAILS CLOSED.  A module accepted by a later run that is not in the corpus, a
module whose runs disagree, a duplicate -- each is an error, because every one
of them silently changes the number.

CVA6 is audited SEPARATELY and never merged in.  Its blocks are not in the
CORE-ET corpus, and its two tables answer different questions:
  pass/lean/SWEEP_cva6.tsv        legacy route: a PROVEN theorem with
                                  `#print axioms` checked (`axioms 1/1`).
                                  It has NO cycles/check_ms column, so it says
                                  NOTHING about direct-simulator acceptance.
  pass/lean/SWEEP_direction2.tsv  the direct simulator: checkDesign plus
                                  executed cycles. CVA6 blocks appear here under
                                  their generated `<name>_gate` wrapper names.
"""
import argparse
import collections
import csv
import glob
import json
import os
import re
import sys

ACCEPTED = "ACCEPTED"


def read_tsv(path):
    with open(path, newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh, delimiter="\t"))


# A sweep progress line: "[1/3] module   VERDICT   nodes=N wall=Ws"
LOG_RE = re.compile(r"^\[\d+/\d+\]\s+(\S+)\s+(\w+)\s+nodes=\s*(\d*)\s+wall=\s*([\d.]*)s")


def scan_runs(root):
    """-> {module: [evidence dict]} over every run directory under `root`."""
    found = collections.defaultdict(list)
    for run in sorted(glob.glob(os.path.join(root, "*/"))):
        run_id = os.path.basename(run.rstrip("/"))
        if run_id == "runtime_locks":
            continue
        tsv = os.path.join(run, "direct_sweep.tsv")
        if os.path.exists(tsv):
            for r in read_tsv(tsv):
                found[r["module"]].append(
                    {"run": run_id, "verdict": r["verdict"], "kind": "tsv-row",
                     "cycles": r.get("cycles", ""), "nodes": r.get("nodes", ""),
                     "wall_s": r.get("wall_s", ""), "rss_kb": r.get("rss_kb", ""),
                     "reason": r.get("reason", ""), "evidence": tsv})
            continue
        log = os.path.join(run, "logs", "direct_sweep.log")
        if os.path.exists(log):
            for line in open(log, encoding="utf-8", errors="replace"):
                m = LOG_RE.match(line.strip())
                if m:
                    found[m.group(1)].append(
                        {"run": run_id, "verdict": m.group(2), "kind": "progress-log",
                         "cycles": "", "nodes": m.group(3), "wall_s": m.group(4),
                         "rss_kb": "", "reason": "", "evidence": log})
    return found


def coreet(args, errors):
    corpus = [l.strip() for l in open(args.corpus, encoding="utf-8") if l.strip()
              and not l.startswith("#")]
    corpus_set = set(corpus)
    if len(corpus_set) != len(corpus):
        errors.append(f"{args.corpus} lists a module more than once")

    base = read_tsv(args.base)
    if {r["module"] for r in base} != corpus_set:
        errors.append(f"{args.base} does not cover exactly the corpus")
    base_status = {r["module"]: r for r in base}
    base_accepted = {m for m, r in base_status.items() if r["stage"] == "accepted"}

    runs = scan_runs(args.runs)
    # A module the base already accepts needs no later evidence; a module it does
    # not is UPGRADED only by an ACCEPTED verdict from a later run.
    upgrades, conflicts, strays = {}, [], []
    for mod, evs in runs.items():
        if mod not in corpus_set:
            strays.append(mod)
            continue
        verdicts = {e["verdict"] for e in evs}
        acc = [e for e in evs if e["verdict"] == ACCEPTED]
        if len(verdicts) > 1:
            conflicts.append((mod, sorted(verdicts),
                              "; ".join(f"{e['run']}={e['verdict']}" for e in evs)))
        if mod in base_accepted or not acc:
            continue
        # Prefer a full TSV row over a progress line, and the latest run.
        acc.sort(key=lambda e: (e["kind"] != "tsv-row", e["run"]))
        upgrades[mod] = acc[0]

    if strays:
        errors.append(f"{len(strays)} swept module(s) are not in the corpus: "
                      + ", ".join(sorted(strays)[:6]))
    overlap = set(upgrades) & base_accepted
    if overlap:
        errors.append(f"double count: {sorted(overlap)} are accepted in BOTH the base "
                      f"and a later run")

    accepted = base_accepted | set(upgrades)
    remaining = sorted(corpus_set - accepted)
    return {"corpus": corpus, "corpus_n": len(corpus_set), "base_accepted": base_accepted,
            "upgrades": upgrades, "accepted": accepted, "remaining": remaining,
            "base_status": base_status, "conflicts": conflicts}


def cva6(args, errors, corpus_modules):
    legacy = read_tsv(args.cva6_legacy) if os.path.exists(args.cva6_legacy) else []
    d2_rows = read_tsv(args.d2_sweep) if os.path.exists(args.d2_sweep) else []
    if d2_rows and "cycles" not in d2_rows[0]:
        errors.append(f"{args.d2_sweep} has no `cycles` column; it is not a "
                      f"direct-simulator table")
    if legacy and "cycles" in legacy[0]:
        errors.append(f"{args.cva6_legacy} unexpectedly HAS a cycles column; the "
                      f"legacy/direct distinction below would be wrong")

    # The D2 table accumulates across runs, so one module can appear several
    # times -- including with different node counts. Collapse per module and keep
    # the disagreement visible rather than picking one silently.
    by_mod = collections.defaultdict(list)
    for r in d2_rows:
        by_mod[r["module"]].append(r)

    blocks = {}
    for r in legacy:
        blocks[r["module"]] = {"legacy": r["verdict"], "legacy_axioms": r.get("axioms", ""),
                               "d2": "", "d2_cycles": "", "d2_runs": 0, "gate": ""}
    # CVA6 blocks reach the D2 sweep under their generated `<name>_gate` wrapper.
    for mod in list(blocks):
        for cand in (mod + "_gate", "cva6_" + mod + "_gate"):
            if cand in by_mod:
                rs = by_mod[cand]
                vs = {x["verdict"] for x in rs}
                blocks[mod]["gate"] = cand
                blocks[mod]["d2"] = "/".join(sorted(vs))
                blocks[mod]["d2_cycles"] = "/".join(sorted({x.get("cycles", "") for x in rs}))
                blocks[mod]["d2_runs"] = len(rs)
                break
    # ...and D2-only CVA6 wrappers with no legacy row at all.
    #
    # The RULE is the `_gate` suffix, not a name prefix. `scripts/gen_cva6_wrappers.py`
    # emits `<module>_gate` for every CVA6 block it wraps, and NO CORE-ET module
    # ends in `_gate` (checked below), so the suffix identifies a CVA6 wrapper
    # unambiguously. A prefix heuristic (`cva6_|cvxif_|ariane_`) missed 18 of
    # them -- bht, decoder, scoreboard, frontend and the rest of the core
    # pipeline -- and under-reported CVA6 coverage by more than half.
    named = {b["gate"] for b in blocks.values() if b["gate"]}
    corpus = set(corpus_modules)
    gated = {m for m in corpus if m.endswith("_gate")}
    if gated:
        errors.append(f"CORE-ET modules end in `_gate` ({sorted(gated)[:3]}), so the "
                      f"suffix no longer identifies a CVA6 wrapper")
    for mod, rs in by_mod.items():
        if mod in named or mod in corpus or not mod.endswith("_gate"):
            continue
        vs = {x["verdict"] for x in rs}
        blocks[mod] = {"legacy": "", "legacy_axioms": "", "d2": "/".join(sorted(vs)),
                       "d2_cycles": "/".join(sorted({x.get("cycles", "") for x in rs})),
                       "d2_runs": len(rs), "gate": mod}
    return blocks


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default="pass/lean/CENSUS_D2_122.tsv")
    ap.add_argument("--corpus", default="pass/lean/COREET_MODULES.txt")
    ap.add_argument("--runs", default="generated/census_d2")
    ap.add_argument("--cva6-legacy", default="pass/lean/SWEEP_cva6.tsv")
    ap.add_argument("--d2-sweep", default="pass/lean/SWEEP_direction2.tsv")
    ap.add_argument("--overrides", default="pass/lean/D2_EVIDENCE_OVERRIDES.tsv")
    ap.add_argument("--out", default="", help="write the ledger here (markdown)")
    args = ap.parse_args()

    errors = []
    ce = coreet(args, errors)
    cb = cva6(args, errors, ce["corpus"])

    # Results obtained with a knob changed: real, differently-qualified, and
    # never a replacement for the canonical row.
    ov = {}
    if os.path.exists(args.overrides):
        with open(args.overrides, encoding="utf-8") as fh:
            for line in fh:
                line = line.rstrip("\n")
                if not line.strip() or line.startswith("#"):
                    continue
                f = line.split("\t")
                if len(f) != 5:
                    errors.append(f"{args.overrides}: a row has {len(f)} fields, not 5")
                    continue
                ov[f[0]] = {"status": f[1], "override": f[2], "detail": f[3], "source": f[4]}
    for m in ov:
        if m not in cb:
            errors.append(f"{args.overrides} names `{m}`, which has no canonical sweep row; "
                          f"an override qualifies a measured result, it does not replace one")

    out = []
    w = out.append
    w("# D2 coverage ledger")
    w("")
    w("GENERATED by `scripts/coverage_ledger.py` -- do not hand-edit. Every number")
    w("below is recomputed from the primary artifacts named in each section; a count")
    w("copied forward is the thing this file exists to stop.")
    w("")
    w("## CORE-ET")
    w("")
    w(f"**{len(ce['accepted'])} of {ce['corpus_n']} accepted** "
      f"(base {len(ce['base_accepted'])}, upgraded by later targeted runs "
      f"{len(ce['upgrades'])}).")
    w("")
    w(f"* base: `{args.base}` -- one run over the whole corpus")
    w(f"* corpus: `{args.corpus}`")
    w(f"* later runs scanned: `{args.runs}/*/`")
    w("")
    if ce["upgrades"]:
        w("### Upgraded since the base census")
        w("")
        w("| module | verdict | cycles | nodes | wall_s | rss_kb | evidence | strength |")
        w("|---|---|---|---|---|---|---|---|")
        for m, e in sorted(ce["upgrades"].items()):
            w(f"| `{m}` | {e['verdict']} | {e['cycles'] or '-'} | {e['nodes'] or '-'} | "
              f"{e['wall_s'] or '-'} | {e['rss_kb'] or '-'} | `{e['evidence']}` | "
              f"{e['kind']} |")
        w("")
        weak = [m for m, e in ce["upgrades"].items() if e["kind"] != "tsv-row"]
        if weak:
            w(f"`progress-log` strength means the run was stopped before its table was")
            w(f"written, so the verdict is real but the timing columns do not exist: "
              f"{', '.join('`' + m + '`' for m in sorted(weak))}.")
            w("")
    w("### Remaining")
    w("")
    w("| module | stage (base census) | reason |")
    w("|---|---|---|")
    for m in ce["remaining"]:
        r = ce["base_status"].get(m, {})
        w(f"| `{m}` | {r.get('stage','?')} | {r.get('reason','')[:110]} |")
    w("")
    w("## CVA6 -- audited separately, never merged into the CORE-ET count")
    w("")
    w("There is **no tracked CVA6 module manifest** in this repo, so a CVA6 coverage")
    w("FRACTION has no denominator here. `pass/lean/CVA6_COVERAGE_PLAN.md` names 78")
    w("blocks reachable from module `cva6` and points at a list outside the repo.")
    w("What can be stated is per block, from two tables that answer different")
    w("questions:")
    w("")
    w(f"* `{args.cva6_legacy}` -- the LEGACY route: a proven theorem with")
    w("  `#print axioms` checked. It has no `cycles`/`check_ms` column, so a")
    w("  `PROVEN` there is **not** direct-simulator acceptance.")
    w(f"* `{args.d2_sweep}` -- the DIRECT simulator: checkDesign plus executed")
    w("  cycles. CVA6 blocks appear under their generated `<name>_gate` wrapper,")
    w("  which is how they are identified here -- no CORE-ET module uses that suffix.")
    w("  The three DINO CPUs in that table are neither CORE-ET nor CVA6 and are")
    w("  deliberately not counted in either section.")
    w("")
    n_legacy = sum(1 for b in cb.values() if b["legacy"] == "PROVEN")
    n_d2 = sum(1 for b in cb.values() if b["d2"] == ACCEPTED)
    n_both = sum(1 for b in cb.values() if b["legacy"] == "PROVEN" and b["d2"] == ACCEPTED)
    n_ov = sum(1 for m in ov if m in cb and cb[m]["d2"] != ACCEPTED)
    w(f"**{len(cb)} blocks with any evidence: {n_legacy} legacy PROVEN, "
      f"{n_d2} direct-simulator ACCEPTED with cycles run in the canonical table, "
      f"{n_both} both, {n_ov} accepted only under an explicit override.**")
    w("")
    if ov:
        w("### Accepted under an override -- NOT in the canonical table")
        w("")
        w("These ran, and they ran with a knob changed. The canonical row records what")
        w("the DEFAULT invocation did and is left alone; collapsing the two is how")
        w("\"accepted\" quietly stops meaning one thing.")
        w("")
        w("| block | canonical | status | override | detail | source |")
        w("|---|---|---|---|---|---|")
        for m, o in sorted(ov.items()):
            w(f"| `{m}` | {cb.get(m, {}).get('d2', '-')} | {o['status']} | `{o['override']}` | "
              f"{o['detail']} | {o['source']} |")
        w("")
    w("| block | legacy | axioms | gate name | direct-sim | cycles | d2 rows |")
    w("|---|---|---|---|---|---|---|")
    for m, b in sorted(cb.items()):
        d2 = b["d2"] or "-"
        if m in ov and d2 != ACCEPTED:
            d2 += " (+override, see below)"
        w(f"| `{m}` | {b['legacy'] or '-'} | {b['legacy_axioms'] or '-'} | "
          f"`{b['gate'] or '-'}` | {d2} | {b['d2_cycles'] or '-'} | "
          f"{b['d2_runs']} |")
    w("")
    if ce["conflicts"]:
        w("## Runs that disagree")
        w("")
        for m, vs, detail in ce["conflicts"]:
            w(f"* `{m}`: {', '.join(vs)} -- {detail}")
        w("")
    text = "\n".join(out) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text)
        print(f"wrote {args.out}")
    else:
        print(text)

    for e in errors:
        print(f"FAIL: {e}", file=sys.stderr)
    print(f"CORE-ET {len(ce['accepted'])}/{ce['corpus_n']}  "
          f"(base {len(ce['base_accepted'])} + {len(ce['upgrades'])} upgraded); "
          f"remaining {len(ce['remaining'])}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Build the Direction 2 census TSV from one census run's artifacts.

Separate from the driver so the join is testable and so the TSV is parsed by
NAME: `while IFS=$'\t' read` collapses empty fields in bash (tab is IFS
whitespace), which silently shifted every column after the empty `reason` in an
earlier table and reported step1_ms as the checkDesign time.

Every requested module gets a row, including blocked ones and ones that never
produced a certificate. A module that disappears from a census is
indistinguishable from one that passed.
"""
import argparse
import csv
import json
import os
import re
import sys

COLS = ["module", "lowering", "compile", "single_edge", "emit", "static_gates",
        "checkdesign", "direct_cycles", "rtl_evidence", "wall_s", "rss_kb",
        "stage", "reason", "artifact"]

# The six whose plain-proc lowering was validated end to end; their RTL
# differential artifact predates this run, hence `prior-smoke` not `smoke-pass`.
PRIOR_SMOKE = {"txfma_f0", "txfma_f2", "txfma_f3", "txfma_f5", "txfma_e5", "txfma_f6"}


def grep1(path, pat, default=""):
    try:
        with open(path, errors="replace") as fh:
            for line in fh:
                m = re.search(pat, line)
                if m:
                    return m.group(1)
    except OSError:
        pass
    return default


def first_diag(path):
    """The first error diagnostic, so a failed row says WHY."""
    try:
        with open(path, errors="replace") as fh:
            body = fh.read()
    except OSError:
        return ""
    m = re.search(r'"severity":"error".*?"message":"([^"]{0,220})"', body, re.S)
    if m:
        return m.group(1)
    m = re.search(r'^\[ERROR\][^\n]{0,220}', body, re.M)
    return m.group(0) if m else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--modules", required=True)
    ap.add_argument("--sweep-tsv", default="")
    ap.add_argument("--sweep-rc", type=int, default=0)
    ap.add_argument("--cycles", type=int, default=4)
    ap.add_argument("--started", default="")
    ap.add_argument("--ended", default="")
    a = ap.parse_args()

    modules = [m for m in a.modules.split(",") if m]
    sweep = {}
    if a.sweep_tsv and os.path.exists(a.sweep_tsv):
        with open(a.sweep_tsv, newline="") as fh:
            for r in csv.DictReader(fh, delimiter="\t"):
                sweep[r.get("module", "")] = r

    rows, counts = [], {}
    for m in modules:
        mo = os.path.join(a.out_dir, "mod", m)
        log = os.path.join(a.out_dir, "logs", f"{m}.log")
        cert = os.path.join(mo, "lean", f"{m}_Lgraph.lean")
        status = os.path.join(mo, "status")
        low = rc = ""
        if os.path.exists(status):
            parts = open(status).read().rstrip("\n").split("\t")
            low, rc = (parts + ["", ""])[:2]

        row = dict.fromkeys(COLS, "")
        row["module"] = m
        row["lowering"] = low or "-"
        row["artifact"] = cert if os.path.exists(cert) else ""

        if low == "BLOCKED":
            row.update(stage="blocked", reason=rc, lowering="-",
                       rtl_evidence="diffsim-mismatch")
            counts["blocked"] = counts.get("blocked", 0) + 1
            rows.append(row)
            continue

        row["compile"] = grep1(log, r"compile exit=(\d+)", "-")
        row["single_edge"] = grep1(log, r"single_edge exit=(\d+)", "-")
        row["emit"] = grep1(log, r"lean emit exit=(\d+)", "-")
        row["static_gates"] = grep1(log, r"static gates: gate_status=(\d+)", "-")
        row["rtl_evidence"] = "prior-smoke" if m in PRIOR_SMOKE else "not-measured"

        s = sweep.get(m)
        if s is not None and a.sweep_rc == 0:
            v = s.get("verdict", "")
            row["checkdesign"] = "ACCEPTED" if v == "ACCEPTED" else v
            row["direct_cycles"] = s.get("cycles", "")
            row["wall_s"] = s.get("wall_s", "")
            row["rss_kb"] = s.get("rss_kb", "")
            if v == "ACCEPTED" and str(s.get("cycles", "")) == str(a.cycles):
                row["stage"] = "accepted"
            else:
                row["stage"] = "sim-refused"
                row["reason"] = s.get("reason", "") or v
        elif s is not None:
            row["checkdesign"] = "UNTRUSTED(sweep rc!=0)"
            row["stage"] = "sim-untrusted"
        else:
            # No sweep row: find the earliest stage that failed, so the reason
            # names a stage rather than just "no certificate".
            for col, name in (("compile", "compile"), ("single_edge", "single_edge"),
                              ("emit", "lean-emit"), ("static_gates", "static-gates")):
                if row[col] not in ("0", "-", ""):
                    row["stage"] = f"{name}-failed"
                    break
            else:
                row["stage"] = "no-certificate" if rc in ("", "0") else f"runner-exit-{rc}"
                if rc == "124":
                    row["stage"] = "timeout"
            row["reason"] = first_diag(log)
        counts[row["stage"]] = counts.get(row["stage"], 0) + 1
        rows.append(row)

    tsv = os.path.join(a.out_dir, "census.tsv")
    with open(tsv, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=COLS, delimiter="\t",
                           lineterminator="\n", extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    summary = {"modules": len(rows), "counts": counts, "cycles": a.cycles,
               "sweep_rc": a.sweep_rc, "started": a.started, "ended": a.ended}
    mani = os.path.join(a.out_dir, "manifest.json")
    try:
        d = json.load(open(mani))
    except Exception:
        d = {}
    d["summary"] = summary
    d["ended"] = a.ended
    json.dump(d, open(mani, "w"), indent=2)

    print(f"\n{len(rows)} row(s) -> {tsv}")
    for k in sorted(counts, key=lambda k: (-counts[k], k)):
        print(f"  {counts[k]:4d}  {k}")
    accepted = counts.get("accepted", 0)
    print(f"\nACCEPTED (checkDesign + {a.cycles} executed cycles): {accepted}/{len(rows)}")
    # Nonzero while blockers remain, but the census itself has completed and
    # every row is preserved.
    return 0 if accepted == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())

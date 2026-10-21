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


SIGNALS = {124: "timed out (SIGTERM from `timeout`)",
           130: "interrupted (SIGINT)",
           134: "aborted (SIGABRT -- an uncaught C++ exception)",
           137: "killed (SIGKILL -- often the OOM killer)",
           143: "terminated (SIGTERM -- an external kill, NOT a design failure)"}


def first_diag(path, rc=""):
    """The most informative reason available, never blank when a log exists.

    The bound on the captured message used to be written `([^"]*{0,220})"`,
    which requires the closing quote to fall within 220 characters -- so every
    message LONGER than that failed to match and the row's reason came out
    empty, which reads as "no diagnostic" rather than "a long one". Capture
    unbounded and truncate afterwards.
    """
    out = []
    try:
        with open(path, errors="replace") as fh:
            body = fh.read()
    except OSError:
        body = ""
    if body:
        m = re.search(r'"severity":"error".*?"message":"([^"]*)"', body, re.S)
        if m:
            out.append(m.group(1)[:220])
        if not out:
            m = re.search(r"^\[ERROR\][^\n]*", body, re.M)
            if m:
                out.append(m.group(0)[:220])
        if not out:
            # static gates print plain FAIL lines, not structured diagnostics
            m = re.search(r"^FAIL: [^\n]*", body, re.M)
            if m:
                out.append(m.group(0)[:220])
        if not out:
            m = re.search(r"^(?:ERROR|FATAL|terminate called)[^\n]*", body, re.M)
            if m:
                out.append(m.group(0)[:220])
    # An exit status is always something, and for a signal it is the whole story.
    try:
        n = int(rc)
    except (TypeError, ValueError):
        n = None
    if n is not None and n in SIGNALS:
        out.append(SIGNALS[n])
    elif n not in (None, 0) and not out:
        out.append(f"runner exited {n} with no structured diagnostic")
    return " | ".join(dict.fromkeys(x for x in out if x)) or ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--modules", required=True)
    ap.add_argument("--sweep-tsv", default="")
    ap.add_argument("--sweep-rc", type=int, default=0)
    ap.add_argument("--present", default="",
                    help="comma-separated modules that cleared EVERY generation "
                         "gate and were handed to the sweep")
    ap.add_argument("--cycles", type=int, default=4)
    ap.add_argument("--started", default="")
    ap.add_argument("--ended", default="")
    a = ap.parse_args()

    modules = [m for m in a.modules.split(",") if m]
    sweep, dup_rows = {}, set()
    if a.sweep_tsv and os.path.exists(a.sweep_tsv):
        with open(a.sweep_tsv, newline="") as fh:
            for r in csv.DictReader(fh, delimiter="\t"):
                m = r.get("module", "")
                if m in sweep:
                    dup_rows.add(m)
                sweep[m] = r

    # SWEEP INTEGRITY, separated from sweep OUTCOMES.
    #
    # direct_sweep exits 1 when it ran to completion but some certificate was
    # REFUSED. Over 122 modules that is near-certain, and treating it as
    # infrastructure failure would mark every good row UNTRUSTED because one
    # row refused. So rc=1 is accepted as "complete, some refused" ONLY when
    # the row set is exactly the set handed to it; rc>=2, or any missing,
    # extra or duplicate row, is infrastructure failure and taints everything.
    present = [m for m in a.present.split(",") if m]
    infra = []
    if a.sweep_rc >= 2:
        infra.append(f"direct_sweep exited {a.sweep_rc} (infrastructure failure)")
    if present or sweep:
        got, want = set(sweep), set(present)
        if want - got:
            infra.append("swept no row for: " + ",".join(sorted(want - got)))
        if got - want:
            infra.append("sweep returned rows not handed to it: " + ",".join(sorted(got - want)))
        if dup_rows:
            infra.append("duplicate sweep rows: " + ",".join(sorted(dup_rows)))
    sweep_trustworthy = not infra
    if infra:
        print("SWEEP INTEGRITY FAILURE -- every checkDesign/cycle cell is UNTRUSTED:",
              file=sys.stderr)
        for i in infra:
            print(f"  - {i}", file=sys.stderr)
    elif a.sweep_rc == 1:
        print("note: direct_sweep exited 1 = completed with refusals; row set verified "
              "exact, so each row's own verdict is used.")

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

        # A row is `accepted` only if EVERY generation gate was 0 as well.
        # Sweeping on certificate existence alone once let a module whose
        # static gate had failed come back ACCEPTED.
        stages_ok = all(row[c] == "0" for c in
                        ("compile", "single_edge", "emit", "static_gates"))
        s = sweep.get(m)
        if s is not None and sweep_trustworthy:
            v = s.get("verdict", "")
            row["checkdesign"] = "ACCEPTED" if v == "ACCEPTED" else v
            row["direct_cycles"] = s.get("cycles", "")
            row["wall_s"] = s.get("wall_s", "")
            row["rss_kb"] = s.get("rss_kb", "")
            if not stages_ok:
                row["stage"] = "stage-gate-failed"
                row["reason"] = ("swept despite a failed generation gate: "
                                 + ",".join(f"{c}={row[c]}" for c in
                                            ("compile", "single_edge", "emit", "static_gates")
                                            if row[c] != "0"))
            elif v == "ACCEPTED" and str(s.get("cycles", "")) == str(a.cycles):
                row["stage"] = "accepted"
            elif v == "ACCEPTED":
                row["stage"] = "sim-incomplete"
                row["reason"] = f"ran {s.get('cycles','') or 0} cycle(s), expected {a.cycles}"
            else:
                row["stage"] = "sim-refused"
                row["reason"] = s.get("reason", "") or v
        elif s is not None:
            row["checkdesign"] = "UNTRUSTED(sweep integrity)"
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
            row["reason"] = first_diag(log, rc)
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
    print(f"\nACCEPTED (all generation gates 0, checkDesign ACCEPTED, "
          f"{a.cycles} executed cycles): {accepted}/{len(rows)}")
    if infra:
        print("THE SWEEP COLUMNS ARE NOT TRUSTWORTHY -- see the integrity failure above.")
        return 2
    # Nonzero while blockers remain, but the census itself has completed and
    # every row is preserved.
    return 0 if accepted == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())

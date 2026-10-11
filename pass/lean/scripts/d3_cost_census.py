#!/usr/bin/env python3
"""Join D3 sweep timings to the SHAPE of the certificate that produced them.

Why: the sweep sizes a target by node count, and node count is the wrong
predictor of what this harness costs.  `btb_gate` has a third the nodes of
`decoder_gate` and four times the wall time, because 346 of its operations are
2,080 bits wide.  `scoreboard_gate` is mid-sized by nodes and eleven times the
next design by TOTAL DECLARED BIT WIDTH, and it is the only target in either
corpus that does not finish.

So this writes one row per measured target carrying both: what the sweep
recorded (wall, peak RSS, verdict, run status) and what the certificate
actually contains (width distribution, operation mix, async-reset flops).  It
is the dataset the Lean-construction phase is stated over (PORT_LEDGER
section 32), and it is evidence, not scheduling input -- nothing reads it back
into a run.

Usage:
  d3_cost_census.py --results CORPUS=RESULTS.tsv:CERTDIR [...] --out CENSUS.tsv
"""

import argparse
import csv
import pathlib
import re
import sys

WIDTH = re.compile(r"width := (\d+)")
OPS = ("Op_SHL", "Op_Sext", "Op_Mux", "Op_MuxBool", "Op_Sum", "Op_Mult",
       "Op_And", "Op_Or", "Op_Xor", "Op_EQ", "Op_LT", "Op_GT", "Op_SRA",
       "Op_Not", "Op_Concat", "Op_Get_mask", "Op_Set_mask", "Op_RedOr")

COLS = ["corpus", "target", "module", "verdict", "run_status", "wall_s",
        "max_rss_kb", "nodes", "sources", "bindings", "flops", "async_flops",
        "width_decls", "max_width", "total_width", "median_width",
        "ops_ge_256b", "ops_ge_1000b", "top_op", "top_op_n"]


def shape(cert: pathlib.Path) -> dict:
    t = cert.read_text(encoding="utf-8", errors="replace")
    w = sorted((int(x) for x in WIDTH.findall(t)), reverse=True)
    counts = {o: t.count(f"LGraphOp.{o}") for o in OPS}
    top = max(counts.items(), key=lambda kv: kv[1]) if counts else ("", 0)
    return {
        "async_flops": t.count("asyncReset := true"),
        "width_decls": len(w),
        "max_width": w[0] if w else 0,
        "total_width": sum(w),
        "median_width": w[len(w) // 2] if w else 0,
        "ops_ge_256b": sum(1 for x in w if x >= 256),
        "ops_ge_1000b": sum(1 for x in w if x >= 1000),
        "top_op": top[0], "top_op_n": top[1],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", action="append", required=True,
                    help="CORPUS=RESULTS.tsv:CERTDIR")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    rows, missing = [], []
    for spec in a.results:
        corpus, _, rest = spec.partition("=")
        res, _, certdir = rest.rpartition(":")
        res, certdir = pathlib.Path(res), pathlib.Path(certdir)
        if not res.is_file():
            print(f"REFUSING: no results table at {res}", file=sys.stderr)
            return 2
        with res.open(encoding="utf-8") as fh:
            for r in csv.DictReader(fh, delimiter="\t"):
                m = (r.get("module") or "").strip()
                if not m:
                    continue
                cert = certdir / f"{m}_Lgraph.lean"
                if not cert.is_file():
                    # A row whose certificate is gone cannot be given a shape.
                    # It is listed, not silently dropped: a census with holes in
                    # it should say where they are.
                    missing.append(f"{corpus}/{m}")
                    continue
                row = {"corpus": corpus, "target": r.get("target_key") or m,
                       "module": m, "verdict": r.get("verdict", ""),
                       "run_status": r.get("run_status", ""),
                       "wall_s": r.get("wall_s", ""),
                       "max_rss_kb": r.get("max_rss_kb", "")}
                for k in ("nodes", "sources", "bindings", "flops"):
                    row[k] = r.get(k, "")
                row.update(shape(cert))
                rows.append(row)

    rows.sort(key=lambda r: -int(r["total_width"] or 0))
    out = pathlib.Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8") as fh:
        fh.write("# D3 cost census: measured wall/RSS against certificate SHAPE.\n")
        fh.write("# Sorted by total_width, which predicts this harness's cost;\n")
        fh.write("# `nodes`, which the sweep schedules by, does not. Evidence only:\n")
        fh.write("# nothing reads this back into a run.\n")
        if missing:
            fh.write(f"# rows without a certificate on disk ({len(missing)}): "
                     f"{', '.join(sorted(missing))}\n")
        fh.write("\t".join(COLS) + "\n")
        for r in rows:
            fh.write("\t".join(str(r.get(c, "")) for c in COLS) + "\n")
    print(f"{out}: {len(rows)} row(s)"
          + (f", {len(missing)} without a certificate" if missing else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Adjudicate several census runs into one 122-row CORE-ET result.

WHY THIS EXISTS. The headline figure is COMBINED CURRENT EVIDENCE, not one
uninterrupted run: a provisional baseline plus targeted follow-up runs that
supersede specific rows. Publishing the baseline TSV under the combined number
would contradict itself -- it still records `txfma_f1` as `compile=143` (a
SIGTERM from a mis-targeted kill, not a design failure) and `null_vpu` /
`minion_dcache_texsend` as `static-gates-failed` (a gate defect, since both are
legitimate zero-node designs).

So the baseline is preserved verbatim for audit, and this emits a separate
adjudicated table in which every row says WHICH RUN it came from, at which
generation commit, and whether that run's provenance is provisional.

A later run wins only for the modules it actually covers; nothing else moves.

Usage:
  census_merge.py --base <run-dir> --override <run-dir> [--override ...]
                  --out <merged.tsv> [--root <repo>]
"""
import argparse
import csv
import json
import os
import sys

PROV = ["evidence_run", "generation_commit", "aggregation_commit", "provisional"]


def load(run_dir, root):
    tsv = os.path.join(run_dir, "census.tsv")
    man = os.path.join(run_dir, "manifest.json")
    if not os.path.exists(tsv):
        sys.exit(f"FATAL: no census.tsv in {run_dir}")
    try:
        m = json.load(open(man))
    except Exception:
        m = {}
    resumed = m.get("resumed") or []
    prov = {
        "evidence_run": os.path.basename(os.path.normpath(run_dir)),
        "generation_commit": (m.get("commit") or "")[:12],
        # A resumed run aggregated later, possibly at another commit; that is
        # exactly what makes its provenance provisional.
        "aggregation_commit": ((resumed[-1].get("aggregated_at_commit") or "")[:12]
                               if resumed else (m.get("commit") or "")[:12]),
        "provisional": "yes" if resumed else "no",
    }
    rows = {}
    with open(tsv, newline="") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            # Machine-absolute paths are not portable evidence -- and they
            # appear in CAPTURED DIAGNOSTICS too, not just the artifact column
            # (a yosys refusal quotes the full read_slang command line). Strip
            # the repository prefix wherever it occurs so the committed table
            # diffs cleanly between checkouts.
            for k, v in list(r.items()):
                if isinstance(v, str) and root in v:
                    r[k] = v.replace(root, "")
            r.update(prov)
            rows[r["module"]] = r
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--override", action="append", default=[])
    ap.add_argument("--out", required=True)
    ap.add_argument("--root", default=os.getcwd())
    ap.add_argument("--expect-modules", type=int, default=122)
    a = ap.parse_args()
    root = os.path.abspath(a.root).rstrip("/") + "/"

    merged = load(a.base, root)
    order = list(merged)
    replaced = []
    for d in a.override:
        for mod, row in load(d, root).items():
            if mod not in merged:
                sys.exit(f"FATAL: override run {d} covers '{mod}', which is not in the base census")
            before = merged[mod].get("stage", "")
            merged[mod] = row
            replaced.append((mod, before, row.get("stage", ""), row["evidence_run"]))

    if len(merged) != a.expect_modules:
        sys.exit(f"FATAL: merged census has {len(merged)} modules, expected {a.expect_modules}")

    cols = [c for c in csv.DictReader(open(os.path.join(a.base, "census.tsv"),
                                           newline=""), delimiter="\t").fieldnames
            if c not in PROV] + PROV
    with open(a.out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=cols, delimiter="\t",
                           lineterminator="\n", extrasaction="ignore")
        w.writeheader()
        for mod in order:
            row = dict(merged[mod])
            # Trailing whitespace makes `git show --check` complain and is
            # indistinguishable from data in a TSV.
            for k, v in row.items():
                v = (v or "").strip()
                # An empty TRAILING field leaves the line ending in a tab,
                # which `git show --check` flags and which reads the same as a
                # blank value. `-` is the file's convention for "not reached".
                row[k] = v if v else "-"
            w.writerow(row)

    acc = sum(1 for r in merged.values() if r.get("stage") == "accepted")
    print(f"{len(merged)} modules -> {a.out}")
    print(f"accepted: {acc}")
    for mod, before, after, run in replaced:
        print(f"  superseded  {mod:<26} {before or '-':<20} -> {after:<12} ({run})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

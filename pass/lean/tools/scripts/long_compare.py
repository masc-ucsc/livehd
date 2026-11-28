#!/usr/bin/env python3
"""The long tier's tables (pass/lean/tools/tests/long_tier.sh): this week's numbers, and what changed
since last week's.

    long_compare.py best <runs.tsv> <out.tsv> --keys K1,K2,.. [--ignore round]
        one row per key from every run of it: the least wall_s and cpu_s, the largest peak_rss_mb, the
        number of runs; every other column but the ignored ones (dropped) must agree across the runs (a
        trace hash that differs between two runs of one key is printed as `DIFFERS`, exit code 1).

    long_compare.py table <new.tsv> <prev.tsv|-> <out.tsv> --keys K1,.. --metrics M1,..
                    [--trace COL] [--same COL] [--limit 10]
        one row per key of the new table: each metric's previous value, new value and change in percent,
        and `REPORT` where a change is above the limit (slower or faster: reported, never failed).
        --trace COL is a trace hash: a key whose hash changed while --same COL (the certificate's hash)
        did not is a different trace of the same design, printed as `TRACE CHANGED` (exit code 1).
        Prints one summary line, `<n> compared, <k> changes above <limit>% (...)`.

    long_compare.py survey <new survey.tsv> <prev survey.tsv|-> <out.tsv>
        sym_survey.py's table against last week's, per family, block and route: a status that was OK and
        is now anything else (REFUSED, DRIFT, TIMEOUT, KILLED, MISSING) is WORSE (exit code 1); every
        other change of status or refusal class is listed; a seconds or peak_mb change above 10% on a row
        OK both weeks is reported, when it is also at least 1 s or 32 MB (the survey's times are
        tenths of a second: a probe of 0.1 s that takes 0.2 s is not a change worth a line).

A previous table of `-` (or a missing file) is the first week: nothing to compare, exit 0.
"""
import argparse
import csv
import os
import sys


def read(path):
    if not path or path == "-" or not os.path.exists(path):
        return None
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh, delimiter="\t"))


def write(path, hdr, rows):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh, delimiter="\t", lineterminator="\n")
        w.writerow(hdr)
        w.writerows(rows)


def num(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def best(args):
    runs = read(args.runs) or []
    keys = args.keys.split(",")
    drop = set(args.ignore.split(",")) if args.ignore else set()
    groups = {}
    for r in runs:
        groups.setdefault(tuple(r[k] for k in keys), []).append({c: v for c, v in r.items() if c not in drop})
    hdr = [c for c in runs[0].keys() if c not in drop] + ["runs"] if runs else keys
    out, bad = [], 0
    for k, rs in groups.items():
        row = dict(rs[0])
        for m in ("wall_s", "cpu_s"):
            if m in row:
                vs = [num(r[m]) for r in rs if num(r[m]) is not None]
                row[m] = f"{min(vs):.2f}" if vs else "-"
        if "peak_rss_mb" in row:
            vs = [num(r["peak_rss_mb"]) for r in rs if num(r["peak_rss_mb"]) is not None]
            row["peak_rss_mb"] = f"{max(vs):.0f}" if vs else "-"
        for c in row:
            if c in keys or c in ("wall_s", "cpu_s", "peak_rss_mb"):
                continue
            if len({r[c] for r in rs}) > 1:
                row[c] = "DIFFERS"
                bad += 1
        row["runs"] = str(len(rs))
        out.append([row.get(h, "") for h in hdr])
    write(args.out, hdr, out)
    print(f"best: {len(out)} keys from {len(runs)} runs, {bad} columns differ between runs of one key")
    return 1 if bad else 0


def table(args):
    new = read(args.new) or []
    prev = read(args.prev)
    keys, metrics = args.keys.split(","), args.metrics.split(",")
    hdr = keys + [f"{m}_{w}" for m in metrics for w in ("prev", "new", "change_pct")] + ["report"]
    if args.trace:
        hdr += [f"{args.trace}_prev", f"{args.trace}_new"]
    if prev is None:
        rows = [[r[k] for k in keys] + [x for m in metrics for x in ("-", r.get(m, "-"), "-")] + ["first week"]
                + ([("-"), r.get(args.trace, "-")] if args.trace else []) for r in new]
        write(args.out, hdr, rows)
        print(f"{len(new)} rows, no previous table: nothing compared")
        return 0
    old = {tuple(r[k] for k in keys): r for r in prev}
    rows, nrep, slower, faster, changed, unmatched = [], 0, 0, 0, 0, 0
    for r in new:
        key = tuple(r[k] for k in keys)
        o = old.get(key)
        cells, notes = [], []
        if o is None:
            unmatched += 1
            notes.append("new key")
        for m in metrics:
            a, b = (num(o.get(m)) if o else None), num(r.get(m))
            if a is None or b is None or a == 0:
                cells += [o.get(m, "-") if o else "-", r.get(m, "-"), "-"]
                continue
            pct = 100.0 * (b - a) / a
            cells += [o[m], r[m], f"{pct:+.1f}"]
            if abs(pct) > args.limit:
                notes.append(f"{m} {pct:+.0f}%")
                if pct > 0:
                    slower += 1
                else:
                    faster += 1
        if args.trace and o is not None:
            ta, tb = o.get(args.trace, "-"), r.get(args.trace, "-")
            same_input = not args.same or o.get(args.same) == r.get(args.same)
            if ta not in ("-", "") and tb not in ("-", "") and ta != tb and same_input:
                notes.append("TRACE CHANGED")
                changed += 1
        if any(n.endswith("%") for n in notes):
            nrep += 1
        rep = "; ".join(notes)
        if any(n.endswith("%") for n in notes):
            rep = "REPORT " + rep
        rows.append([r[k] for k in keys] + cells + [rep]
                    + ([o.get(args.trace, "-") if o else "-", r.get(args.trace, "-")] if args.trace else []))
    write(args.out, hdr, rows)
    print(f"{len(new)} compared with last week's ({unmatched} new), {nrep} with a change above "
          f"{args.limit:g}% ({slower} slower or larger, {faster} faster or smaller), "
          f"{changed} traces changed")
    return 1 if changed else 0


SURVEY_OK = "OK"


def survey(args):
    new = read(args.new) or []
    prev = read(args.prev)
    hdr = ["family", "block", "route", "status_prev", "status_new", "class_prev", "class_new",
           "seconds_prev", "seconds_new", "peak_mb_prev", "peak_mb_new", "change"]
    if prev is None:
        write(args.out, hdr, [[r["family"], r["block"], r["route"], "-", r["status"], "-", r["class"], "-",
                               r["seconds"], "-", r["peak_mb"], "first week"] for r in new])
        print(f"{len(new)} rows, no previous survey: nothing compared")
        return 0
    old = {(r["family"], r["block"], r["route"]): r for r in prev}
    rows, worse, better, other, numrep, unmatched = [], 0, 0, 0, 0, 0
    for r in new:
        key = (r["family"], r["block"], r["route"])
        o = old.get(key)
        if o is None:
            unmatched += 1
            rows.append([*key, "-", r["status"], "-", r["class"], "-", r["seconds"], "-", r["peak_mb"], "new row"])
            continue
        a, b = o["status"], r["status"]
        notes = []
        if a == SURVEY_OK and b != SURVEY_OK and b != "SKIPPED":
            notes.append(f"WORSE: {a} -> {b}")
            worse += 1
        elif a != SURVEY_OK and b == SURVEY_OK:
            notes.append(f"better: {a} -> {b}")
            better += 1
        elif a != b or o["class"] != r["class"]:
            notes.append(f"changed: {a} {o['class']} -> {b} {r['class']}".replace("  ", " "))
            other += 1
        if a == b == SURVEY_OK:
            for m, floor in (("seconds", 1.0), ("peak_mb", 32.0)):
                x, y = num(o[m]), num(r[m])
                if x and y is not None and abs(100.0 * (y - x) / x) > 10 and abs(y - x) >= floor:
                    notes.append(f"{m} {100.0 * (y - x) / x:+.0f}%")
            if len(notes):
                numrep += 1
        rows.append([*key, a, b, o["class"], r["class"], o["seconds"], r["seconds"], o["peak_mb"], r["peak_mb"],
                     "; ".join(notes)])
    gone = set(old) - {(r["family"], r["block"], r["route"]) for r in new}
    write(args.out, hdr, rows)
    print(f"survey: {len(new)} rows compared with last week's ({unmatched} new, {len(gone)} not run this week): "
          f"{worse} worse, {better} better, {other} other status changes, {numrep} OK rows with a time or "
          f"memory change above 10%")
    return 1 if worse else 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("best")
    b.add_argument("runs")
    b.add_argument("out")
    b.add_argument("--keys", required=True)
    b.add_argument("--ignore", default="round")
    t = sub.add_parser("table")
    t.add_argument("new")
    t.add_argument("prev")
    t.add_argument("out")
    t.add_argument("--keys", required=True)
    t.add_argument("--metrics", required=True)
    t.add_argument("--trace", default="")
    t.add_argument("--same", default="")
    t.add_argument("--limit", type=float, default=10.0)
    s = sub.add_parser("survey")
    s.add_argument("new")
    s.add_argument("prev")
    s.add_argument("out")
    args = ap.parse_args()
    return {"best": best, "table": table, "survey": survey}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())

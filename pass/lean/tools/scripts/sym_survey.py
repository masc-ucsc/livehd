#!/usr/bin/env python3
"""Refusal survey: build one symbolic step of many certificates on each symbolic route.

    sym_survey.py --probe <nat>/sym_probe --inputs census_inputs.txt --out <dir>
                  [--routes fp,ir,vc] [--fp-max-nodes 150000] [--timeout 1800] [--mem 20G]
                  [--only <regex>] [--sizes <census.tsv>]

`census_inputs.txt` lists `family=<path>/<Top>_Lgraph.lean` lines (the census list); the
certificate is the `<Top>.dcert` beside the (symlink-resolved) `_Lgraph.lean`. Each probe runs
under a cgroup memory cap and a timeout, strictly one at a time, and appends one row to
`<out>/survey.tsv` (rows already there are skipped, so a stopped survey resumes):

    family block nodes route status class seconds peak_mb defs diag message

`status` is OK, REFUSED, TIMEOUT or KILLED (memory cap); `class` the refusal class
(`Sym.Diag.refusalClass`). `diag` (fp only) lists, for the primitives whose symbolic support is
tracked, how often each was taken symbolically: `mulI:sym=12,...`. The raw probe output of each
row is kept in `<out>/logs/`.

`--sizes` is a census TSV with `family`, `block` and `nodes` columns: certificates run smallest
first, and fp skips any above `--fp-max-nodes`. Without it every certificate counts as 0 nodes.
"""
import argparse, csv, os, re, subprocess, time

TRACKED = ("mulI", "divI", "modI", "eqV", "mkCtorP", "ctorTagP", "ctorFieldsP", "bvBit", "bvGetMask")
HDR = ["family", "block", "nodes", "route", "status", "class", "seconds", "peak_mb", "defs", "diag", "message"]


def certs(inputs):
    seen = {}
    for line in open(inputs):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        fam, path = line.split("=", 1)
        real = os.path.realpath(path)
        top = os.path.basename(real).replace("_Lgraph.lean", "")
        dcert = os.path.join(os.path.dirname(real), top + ".dcert")
        seen[(fam, top)] = dcert          # a repeated family/block keeps its last entry
    return seen


def sizes(path):
    out = {}
    if path and os.path.exists(path):
        for r in csv.DictReader(open(path), delimiter="\t"):
            out[(r["family"], r["block"])] = int(r["nodes"])
    return out


def probe(args, dcert, route, log):
    cmd = ["systemd-run", "--user", "--scope", "-q", "-p", f"MemoryMax={args.mem}", "-p", "MemorySwapMax=0",
           "/usr/bin/time", "-f", "TIME %e %M", "timeout", str(args.timeout), "nice", "-n19",
           args.probe, dcert, f"sim:{route}", "sf:20000000", "wf:500000"]
    if route == "fp":
        cmd += ["diag", "census"]
    t0 = time.time()
    with open(log, "w") as fh:
        rc = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT).returncode
    text = open(log, errors="replace").read()
    m = re.search(r"TIME ([\d.]+) (\d+)", text)
    secs, peak = (float(m.group(1)), int(m.group(2)) // 1024) if m else (time.time() - t0, 0)
    pl = re.search(rf"^PROBE {route} (OK|REFUSED)(.*)$", text, re.M)
    if pl and pl.group(1) == "OK":
        d = re.search(r"defs=(\d+)", pl.group(2))
        status, cls, msg, defs = "OK", "", "", d.group(1) if d else ""
    elif pl:
        rest = pl.group(2).strip()
        cls, _, msg = rest.partition(":")
        status, defs = "REFUSED", ""
        cls, msg = cls.strip(), msg.strip()
    elif re.search(r"^PROBE all REFUSED", text, re.M):
        status, cls, msg, defs = "REFUSED", "setup", "the memory route does not admit this certificate", ""
    elif rc == 124 or "Command exited with non-zero status 124" in text:
        status, cls, msg, defs = "TIMEOUT", "", f"over {args.timeout} s", ""
    else:
        status, cls, msg, defs = "KILLED", "", f"rc={rc} (memory cap {args.mem} or crash)", ""
    diag = {}
    for k, v in re.findall(r"^DIAG \S+ prim (\S+) sym ok (\d+)$", text, re.M):
        if k in TRACKED:
            diag[k] = diag.get(k, 0) + int(v)
    refused = re.findall(r"^DIAG \S+ prim (\S+) sym REFUSED (\d+)$", text, re.M)
    dtxt = ",".join(f"{k}:sym={v}" for k, v in sorted(diag.items()))
    if refused:
        dtxt += (";" if dtxt else "") + ",".join(f"{k}:REFUSED={v}" for k, v in refused)
    return status, cls, f"{secs:.1f}", str(peak), defs, dtxt, msg[:200]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", required=True)
    ap.add_argument("--inputs", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--routes", default="fp,ir,vc")
    ap.add_argument("--fp-max-nodes", type=int, default=150000)
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--mem", default="20G")
    ap.add_argument("--only", default="")
    ap.add_argument("--sizes", default="")
    args = ap.parse_args()
    os.makedirs(os.path.join(args.out, "logs"), exist_ok=True)
    tsv = os.path.join(args.out, "survey.tsv")
    done = set()
    if os.path.exists(tsv):
        for r in csv.DictReader(open(tsv), delimiter="\t"):
            done.add((r["family"], r["block"], r["route"]))
    else:
        open(tsv, "w").write("\t".join(HDR) + "\n")
    size = sizes(args.sizes)
    rows = sorted(certs(args.inputs).items(), key=lambda kv: size.get(kv[0], 0))
    for (fam, top), dcert in rows:
        if args.only and not re.search(args.only, f"{fam}/{top}"):
            continue
        n = size.get((fam, top), 0)
        for route in args.routes.split(","):
            if (fam, top, route) in done:
                continue
            if not os.path.exists(dcert):
                res = ("MISSING", "", "0", "0", "", "", f"no {dcert}")
            elif route == "fp" and n > args.fp_max_nodes:
                res = ("SKIPPED", "", "0", "0", "", "", f"{n} nodes > fp limit {args.fp_max_nodes}")
            else:
                log = os.path.join(args.out, "logs", f"{fam.replace(':', '_')}__{top}__{route}.log")
                res = probe(args, dcert, route, log)
            with open(tsv, "a") as fh:
                fh.write("\t".join([fam, top, str(n), route, *res]) + "\n")
            print(f"{fam}/{top} {route}: {res[0]} {res[1]} {res[2]} s {res[3]} MB {res[5]}", flush=True)


if __name__ == "__main__":
    main()

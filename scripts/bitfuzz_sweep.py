#!/usr/bin/env python3
"""pass.bitfuzz corpus sweep.

For every design in the equiv corpus: compile it twice -- once normally, once
with `--set compile.bitfuzz.mode=<mode>` -- then LEC the two emitted netlists
against each other. Stripping the per-pin width/sign annotations must not
change what the design MEANS, so any REFUTED is a place where some stage gave
those attributes semantic weight.

Baseline-compile failures are reported separately and never counted as fuzz
findings: the corpus has pre-existing breakage.
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# ./bazel-bin is a symlink that flips to whichever -c config was built LAST, so
# an explicit $LHD is the reliable way to pin one binary across a long sweep.
LHD = os.environ.get("LHD", os.path.join(REPO, "bazel-bin", "lhd", "lhd"))
CORPUS = os.environ.get("BITFUZZ_CORPUS", os.path.join(REPO, "inou", "prp", "tests", "equiv"))
MODULE_RE = re.compile(r"^\s*module\s+([\\A-Za-z_][^\s(;]*)", re.M)


def run(cmd, cwd, timeout):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired:
        return 124, "TIMEOUT"


def bitfuzz_stats(out):
    """Pull the one bitfuzz-summary record out of the JSONL diagnostics."""
    agg = {}
    for line in out.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            d = json.loads(line)
        except Exception:
            continue
        if d.get("code") != "bitfuzz-summary":
            continue
        m = d.get("message", "")
        for key, pat in (
            ("cleared", r"cleared (\d+) pin"),
            ("same", r"(\d+) identical"),
            ("narrower", r"(\d+) narrower"),
            ("wider", r"(\d+) wider"),
            ("sign", r"(\d+) sign-changed"),
            ("unrec", r"(\d+) unrecovered"),
            ("norule", r"\((\d+) with no inference rule\)"),
        ):
            g = re.search(pat, m)
            if g:
                agg[key] = agg.get(key, 0) + int(g.group(1))
    return agg


def one(vfile, mode, workroot, timeout):
    name = os.path.basename(vfile)[:-2]
    wd = os.path.abspath(os.path.join(workroot, re.sub(r"\W+", "_", name)))
    shutil.rmtree(wd, ignore_errors=True)
    os.makedirs(wd, exist_ok=True)
    res = {"name": name, "verdict": "?", "stats": {}, "detail": ""}

    base = [LHD, "compile", vfile, "--recipe", "O2", "--workdir"]
    rc, out = run(base + [f"{wd}/wref", "--emit-dir", f"verilog:{wd}/ref/"], wd, timeout)
    if rc != 0:
        res["verdict"] = "BASE_COMPILE_FAIL"
        res["detail"] = out.strip().splitlines()[-1][:200] if out.strip() else ""
        return res

    rc, out = run(
        base + [f"{wd}/wbf", "--emit-dir", f"verilog:{wd}/bf/", "--set", f"compile.bitfuzz.mode={mode}"],
        wd,
        timeout,
    )
    res["stats"] = bitfuzz_stats(out)
    if rc != 0:
        res["verdict"] = "FUZZ_COMPILE_FAIL"
        res["detail"] = out.strip().splitlines()[-1][:300] if out.strip() else ""
        return res

    try:
        refs = sorted(f for f in os.listdir(f"{wd}/ref") if f.endswith(".v"))
    except FileNotFoundError:
        refs = []
    if not refs:
        res["verdict"] = "NO_OUTPUT"
        return res

    verdicts = []
    for f in refs:
        rp, bp = f"{wd}/ref/{f}", f"{wd}/bf/{f}"
        if not os.path.exists(bp):
            verdicts.append(("MISSING", f, ""))
            continue
        try:
            src = open(rp).read()
        except Exception:
            continue
        m = MODULE_RE.search(src)
        if not m:
            continue
        top = m.group(1).lstrip("\\")
        rc, out = run(
            [LHD, "lec", "--impl", f"verilog:{bp}", "--ref", f"verilog:{rp}", "--top", top,
             "--workdir", f"{wd}/lec_{top}"],
            wd,
            timeout,
        )
        if rc == 0:
            verdicts.append(("PROVEN", top, ""))
        elif rc == 124:
            verdicts.append(("TIMEOUT", top, ""))
        else:
            cex = ""
            g = re.search(r"counterexample: (.*)", out)
            if g:
                cex = g.group(1)[:160]
            kind = "REFUTED" if "REFUTED" in out else "LEC_OTHER"
            verdicts.append((kind, top, cex))

    order = ["REFUTED", "LEC_OTHER", "TIMEOUT", "MISSING", "PROVEN"]
    verdicts.sort(key=lambda v: order.index(v[0]) if v[0] in order else 0)
    if verdicts:
        res["verdict"] = verdicts[0][0]
        res["detail"] = f"{verdicts[0][1]}: {verdicts[0][2]}"
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", default="wires")
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--workroot", default="sweep")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--filter", default="")
    a = ap.parse_args()

    files = sorted(
        os.path.join(CORPUS, f)
        for f in os.listdir(CORPUS)
        if f.endswith(".v") and not f.endswith("_tb.v")
    )
    if a.filter:
        files = [f for f in files if a.filter in os.path.basename(f)]
    if a.limit:
        files = files[: a.limit]

    os.makedirs(a.workroot, exist_ok=True)
    results = []
    with cf.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {ex.submit(one, f, a.mode, a.workroot, a.timeout): f for f in files}
        for i, fu in enumerate(cf.as_completed(futs), 1):
            r = fu.result()
            results.append(r)
            print(f"[{i}/{len(files)}] {r['verdict']:18s} {r['name']}", flush=True)

    buckets = {}
    for r in results:
        buckets.setdefault(r["verdict"], []).append(r)

    tot = {}
    for r in results:
        for k, v in r["stats"].items():
            tot[k] = tot.get(k, 0) + v

    print("\n=== bitfuzz sweep (mode=%s, %d designs) ===" % (a.mode, len(results)))
    for k in sorted(buckets, key=lambda k: -len(buckets[k])):
        print(f"  {k:20s} {len(buckets[k])}")
    print("\n  pin totals:", json.dumps(tot, sort_keys=True))
    for k in ("REFUTED", "LEC_OTHER", "FUZZ_COMPILE_FAIL", "MISSING", "TIMEOUT"):
        for r in buckets.get(k, []):
            print(f"  {k}: {r['name']} -- {r['detail']}")
    with open(os.path.join(a.workroot, "results.json"), "w") as fh:
        json.dump(results, fh, indent=1)
    return 1 if any(k in buckets for k in ("REFUTED", "FUZZ_COMPILE_FAIL")) else 0


if __name__ == "__main__":
    sys.exit(main())

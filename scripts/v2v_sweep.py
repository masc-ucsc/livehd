#!/usr/bin/env python3
#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# v2v_sweep — run v2v_verilator_diff.py over every module a design and its
# LiveHD regeneration BOTH declare, and summarize.
#
#   scripts/v2v_sweep.py --gen /tmp/minion_gen.v --orig-flags "-F filelist.f -DSYNTHESIS" \
#                        --sources-from filelist.f [--cycles 60] [--jobs 4]
#
# WHY THE INTERSECTION MATTERS. LiveHD\'s emission also contains its own memory
# library (ware/rtl/cgen_memory_*.v), which the original design never had.
# Verilating one of those against the original resolves it to a MISSING-MODULE
# stub whose outputs are all zero, so it "mismatches" and means nothing —
# measured: 28 such rows before this filter, every one noise. Sweeping only the
# modules both sides declare is the difference between a finding and a false
# alarm.
import argparse, concurrent.futures, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def modules_in(path):
    try:
        with open(path, errors="ignore") as f:
            return set(re.findall(r"^\s*module\s+\\?([A-Za-z_][\w$]*)", f.read(), re.M))
    except OSError:
        return set()


def expand_filelist(fl):
    """Every source path a `-f/-F` filelist names, resolved against its dir."""
    out, base = [], os.path.dirname(os.path.abspath(fl))
    try:
        with open(fl) as f:
            for line in f:
                line = line.split("//")[0].strip()
                if not line or line.startswith(("-", "+")):
                    continue
                out.append(line if os.path.isabs(line) else os.path.join(base, line))
    except OSError:
        pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", required=True)
    ap.add_argument("--orig-flags", default="")
    ap.add_argument("--sources-from", action="append", default=[],
                    help="filelist (.f) or source file declaring the ORIGINAL modules (repeatable)")
    ap.add_argument("--cycles", type=int, default=60)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) // 2))
    ap.add_argument("--work", default="/tmp/v2v_sweep")
    ap.add_argument("--only", default="", help="regex: sweep only matching module names")
    args = ap.parse_args()

    orig_sources = []
    for s in args.sources_from:
        orig_sources += expand_filelist(s) if s.endswith((".f", ".F")) else [s]
    orig_mods = set()
    for s in orig_sources:
        orig_mods |= modules_in(s)
    gen_mods = modules_in(args.gen)
    mods = sorted(gen_mods & orig_mods)
    if args.only:
        rx = re.compile(args.only)
        mods = [m for m in mods if rx.search(m)]
    print("sweeping {} module(s) declared by BOTH (generated {}, original {})"
          .format(len(mods), len(gen_mods), len(orig_mods)))
    if not mods:
        print("FAIL: no shared modules — check --sources-from")
        return 2

    def run(m):
        cmd = [sys.executable, os.path.join(HERE, "v2v_verilator_diff.py"), "--top", m,
               "--orig-flags", args.orig_flags, "--gen", os.path.abspath(args.gen),
               "--cycles", str(args.cycles), "--work", os.path.join(args.work, m)]
        cp = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return m, cp.returncode, cp.stdout.decode("utf-8", "ignore")

    tally = {0: 0, 1: 0, 77: 0, "err": 0}
    mismatches = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for m, rc, out in ex.map(run, mods):
            if rc == 0:
                tally[0] += 1
                print("MATCH    " + m, flush=True)
            elif rc == 1:
                tally[1] += 1
                mismatches.append((m, out))
                print("MISMATCH " + m, flush=True)
                for line in out.splitlines():
                    if line.startswith(("MISMATCH", "  orig:", "  gen :", "  first differing", "FAIL:")):
                        print("    " + line[:220], flush=True)
            elif rc in (77, 78, 79, 80):
                tally[77] += 1
                why = {77: "struct-leaf ports", 78: "generated model would not build",
                       79: "original not buildable standalone (parameterized?)",
                       80: "port widths differ (parameterized: pre- vs post-elaboration)"}[rc]
                print("skip     {}  ({})".format(m, why), flush=True)
            else:
                tally["err"] += 1
                print("ERROR    {}  rc={}".format(m, rc), flush=True)
                print("    " + out.strip().splitlines()[-1][:220] if out.strip() else "", flush=True)

    print("\nv2v sweep: {} match, {} MISMATCH, {} skipped, {} error"
          .format(tally[0], tally[1], tally[77], tally["err"]))
    return 1 if tally[1] else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""2f-v2prp corpus census.

Sweeps every inou/prp/tests/equiv/*.v through the Verilog -> Pyrope round-trip:

    slang reader   :  foo.v        -> foo.prp     (upass/prp_writer)
    re-compile     :  foo.prp      -> foo_impl.v  (inou.prp -> tolg -> cgen)
    equivalence    :  foo_impl.v   == foo.v       (inou/yosys/lgcheck)

and prints a per-file pass / fail / inconclusive / skip table plus a summary.
This is the manual census (the gated bazel subset lives in pyrope_test.py's
`v2prp` mode); run it from the repo root after `bazel build //lhd:lhd`.

  python3 inou/prp/tests/v2prp_census.py            # whole corpus
  python3 inou/prp/tests/v2prp_census.py trivial_if mem_basic   # named files
  python3 inou/prp/tests/v2prp_census.py -v <name>  # verbose (dump logs)

Exit code is 0 iff there are no hard FAILs (inconclusive/skip do not fail).
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

EQUIV_DIR = "inou/prp/tests/equiv"
LHD = "./bazel-bin/lhd/lhd"
LGCHECK = "./inou/yosys/lgcheck"
TIMEOUT = 60  # per lgcheck; a timeout is inconclusive, not a fail


def _modules(vpath):
    try:
        with open(vpath) as f:
            return re.findall(r"\bmodule\s+\\?([^\s(]+)", f.read())
    except OSError:
        return []


def _run(cmd, timeout=None):
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        return p.returncode, p.stdout.decode("utf-8", "ignore")
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode("utf-8", "ignore") if e.stdout else ""
        return None, out  # None rc == timeout


def run_one(name, work, verbose=False):
    v = os.path.join(EQUIV_DIR, name + ".v")
    if not os.path.exists(v):
        return "SKIP", "no golden .v"

    prp_dir = os.path.join(work, "prp")
    v_dir = os.path.join(work, "impl")
    for d in (prp_dir, v_dir):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d, exist_ok=True)

    # 1. slang -> pyrope
    rc, log = _run([LHD, "compile", "--reader", "slang", v, "--emit-dir", "pyrope:" + prp_dir + "/",
                    "--workdir", os.path.join(work, "w1")])
    if rc != 0:
        return "FAIL", "slang->pyrope rc={}\n{}".format(rc, log if verbose else "")
    prps = glob.glob(os.path.join(prp_dir, "*.prp"))
    if not prps:
        return "FAIL", "no .prp emitted"
    prp = prps[0]
    with open(prp) as f:
        emitted = f.read()
    if "/* TODO" in emitted or "unhandled" in emitted:
        return "FAIL", "writer TODO/unhandled in output:\n{}".format(emitted if verbose else "")

    # 2. pyrope -> verilog
    rc, log = _run([LHD, "compile", prp, "--recipe", "O0", "--emit-dir", "verilog:" + v_dir + "/",
                    "--workdir", os.path.join(work, "w2")])
    if rc != 0:
        return "FAIL", "pyrope->verilog rc={}\n{}\n--- emitted prp ---\n{}".format(
            rc, log if verbose else "", emitted)
    gen = sorted(glob.glob(os.path.join(v_dir, "*.v")))
    if not gen:
        return "FAIL", "no verilog generated"

    impl = os.path.join(v_dir, "all_impl.v")
    with open(impl, "w") as out:
        for g in gen:
            with open(g) as f:
                out.write(f.read() + "\n")

    # 3. equivalence: pick the impl top that matches the reference top
    ref_top = _modules(v)[0] if _modules(v) else None
    impl_mods = _modules(impl)
    # Prefer an impl module whose dotted tail matches the ref top's tail.
    tail = ref_top.split(".")[-1] if ref_top else None
    impl_top = next((m for m in impl_mods if m == ref_top), None)
    if impl_top is None and tail:
        impl_top = next((m for m in impl_mods if m.split(".")[-1] == tail), None)
    if impl_top is None and len(impl_mods) == 1:
        impl_top = impl_mods[0]
    if not ref_top or not impl_top:
        return "FAIL", "top detect: ref={} impl_mods={}".format(ref_top, impl_mods)

    rc, log = _run([LGCHECK, "--reference", v, "--implementation", impl,
                    "--reference_top", ref_top, "--implementation_top", impl_top], timeout=TIMEOUT)
    if rc is None:
        return "INCONCLUSIVE", "lgcheck timeout (>{}s)".format(TIMEOUT)
    if rc == 0:
        return "PASS", "ref={} impl={}".format(ref_top, impl_top)
    return "FAIL", "lgcheck not equivalent (ref={} impl={})\n{}".format(ref_top, impl_top, log if verbose else "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*", help="bare test names (default: all)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(LHD):
        print("missing {} — run `bazel build //lhd:lhd` first".format(LHD))
        return 2

    names = args.names or sorted(os.path.splitext(os.path.basename(p))[0]
                                 for p in glob.glob(os.path.join(EQUIV_DIR, "*.v")))
    work_root = "tmp_v2prp_census"
    shutil.rmtree(work_root, ignore_errors=True)
    os.makedirs(work_root, exist_ok=True)

    tally = {}
    rows = []
    for n in names:
        status, detail = run_one(n, os.path.join(work_root, n), args.verbose)
        tally[status] = tally.get(status, 0) + 1
        rows.append((n, status, detail))
        marker = {"PASS": "ok ", "FAIL": "XXX", "INCONCLUSIVE": "?? ", "SKIP": "-- "}.get(status, "?")
        print("[{}] {:30s} {}".format(marker, n, status if status != "PASS" else detail))
        if args.verbose and status not in ("PASS", "SKIP"):
            print("    " + detail.replace("\n", "\n    "))

    print("\n=== summary ({} files) ===".format(len(names)))
    for k in ("PASS", "FAIL", "INCONCLUSIVE", "SKIP"):
        if k in tally:
            print("  {:13s} {}".format(k, tally[k]))
    if tally.get("FAIL"):
        print("\nFAILs:")
        for n, s, _ in rows:
            if s == "FAIL":
                print("  " + n)
    return 1 if tally.get("FAIL") else 0


if __name__ == "__main__":
    sys.exit(main())

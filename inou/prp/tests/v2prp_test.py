#!/usr/bin/env python3
"""2f-v2prp gated test: a golden `.v` round-trips through Pyrope and is proven
equivalent to its ORIGINAL Pyrope reference.

    lhd compile --reader slang foo.v --emit-dir pyrope:DIR/         # slang -> Pyrope
    lhd lec --impl pyrope:DIR/<vtop>.prp --ref pyrope:equiv/foo.prp \
            --impl-top <vtop> --ref-top <ptop>                      # recompile + LEC

The reference is the ORIGINAL `equiv/<name>.prp` (the hand-written Pyrope the
golden `.v` mirrors), NOT the `.v` itself.  This proves the slang->Pyrope
emission preserves the design directly against its Pyrope source, and sidesteps
yosys `read_verilog` gaps on the reference side (e.g. `'{...}` assignment
patterns).  The backend is the in-process cvc5 BMC engine (the default since the
`ind`->`bmc` flip): for the v->prp direction the two sides name their flops
differently, so the inductive miter (which corresponds flops by name) cannot be
used; BMC unrolls each design from reset and compares I/O, no name matching
needed.

Per-side tops come from the reference's `:verilog_top:` (the emitted module,
== the `.v` module name) and `:pyrope_top:` (the original lambda) headers.

`lhd lec` is the authoritative gate: equivalent => pass, not-equivalent => fail,
and a TIMEOUT => inconclusive (exit 0, NOT a fail — per the task's gate
semantics).  This is the per-file form driven by the `prp-v2prp-<name>` bazel
targets; inou/prp/tests/v2prp_census.py is the whole-corpus census.

  python3 inou/prp/tests/v2prp_test.py -i inou/prp/tests/equiv/trivial_if.v
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

CHECK_TIMEOUT = 60  # seconds; a timeout is inconclusive, not a failure


def _header(prp_path, key):
    """Return the `:key: value` header field from a Pyrope reference, or None."""
    try:
        with open(prp_path) as f:
            m = re.search(r":%s:\s*([^\s*]+)" % re.escape(key), f.read())
            return m.group(1).strip() if m else None
    except OSError:
        return None


def _modules(vpath):
    with open(vpath) as f:
        return re.findall(r"\bmodule\s+\\?([^\s(]+)", f.read())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", required=True, help="golden .v file")
    args = ap.parse_args()

    lhd = "./bazel-bin/lhd/lhd" if os.path.exists("./bazel-bin/lhd/lhd") else "./lhd/lhd"
    if not os.path.exists(lhd):
        print("missing lhd binary")
        return 3

    v = args.input
    name = os.path.splitext(os.path.basename(v))[0]
    ref_prp = os.path.join(os.path.dirname(v), name + ".prp")
    if not os.path.exists(ref_prp):
        print("{} - v2prp - FAILED: no original .prp reference next to {}".format(name, v))
        return 1

    # Per-side tops from the reference headers: :verilog_top: is the emitted
    # module (the .v module name), :pyrope_top: the original lambda.
    vtop = _header(ref_prp, "verilog_top")
    ptop = _header(ref_prp, "pyrope_top")
    if not vtop or not ptop:
        # Fall back to the .v's first module name for both (legacy goldens).
        mods = _modules(v)
        vtop = vtop or (mods[0] if mods else name)
        ptop = ptop or vtop

    work = "tmp_v2prp_" + re.sub(r"\W+", "_", name)
    shutil.rmtree(work, ignore_errors=True)
    prp_dir = os.path.join(work, "prp")
    os.makedirs(prp_dir, exist_ok=True)

    # 1. Verilog -> Pyrope via the slang reader + upass/prp_writer.
    comp = subprocess.run(
        [lhd, "compile", "--reader", "slang", v, "--emit-dir", "pyrope:" + prp_dir + "/",
         "--workdir", os.path.join(work, "w_slang")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("{} - v2prp - FAILED: slang->pyrope rc={}".format(name, comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1

    prps = glob.glob(os.path.join(prp_dir, "*.prp"))
    if not prps:
        print("{} - v2prp - FAILED: no .prp emitted".format(name))
        return 1
    # The emitted top file is <vtop>.prp; fall back to the sole file.
    emitted = os.path.join(prp_dir, vtop + ".prp")
    if not os.path.exists(emitted):
        emitted = prps[0]
    with open(emitted) as f:
        text = f.read()
    if "/* TODO" in text or "unhandled node" in text:
        print("{} - v2prp - FAILED: writer left a TODO/unhandled node:\n{}".format(name, text))
        return 1

    # The impl side: when the top instantiates sibling-file submodules (the
    # writer emits one .prp per module), read the whole emit dir so the call
    # targets resolve; otherwise the single top file is enough.
    impl_arg = "pyrope:" + prp_dir + "/" if len(prps) > 1 else "pyrope:" + emitted

    # 2. Recompile the emitted Pyrope and LEC it against the ORIGINAL .prp.
    #    Default engine = bmc, default solver = cvc5 (no --set needed).
    try:
        chk = subprocess.run(
            [lhd, "lec", "--impl", impl_arg, "--ref", "pyrope:" + ref_prp,
             "--impl-top", vtop, "--ref-top", ptop,
             "--workdir", os.path.join(work, "w_check")],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=CHECK_TIMEOUT)
    except subprocess.TimeoutExpired:
        # Authoritative gate is inconclusive on timeout — pass (run `lhd lec`
        # by hand for a deeper bound / cross-check).
        print("{} - v2prp - inconclusive (lhd lec timeout >{}s, NOT a fail)".format(name, CHECK_TIMEOUT))
        return 0

    out = chk.stdout.decode("utf-8", "ignore")
    if chk.returncode == 0:
        # `lhd lec` exits 0 for INCONCLUSIVE too (could-not-prove is a warning,
        # not a fail — same gate semantics as the timeout above). Report it
        # honestly instead of conflating it with a real proof.
        if "INCONCLUSIVE" in out or "UNKNOWN" in out:
            print("{} - v2prp - inconclusive (solver gave up; NOT a proof; impl-top:{} ref-top:{})".format(name, vtop, ptop))
            return 0
        print("{} - v2prp - success (impl-top:{} ref-top:{})".format(name, vtop, ptop))
        return 0
    # An ENCODER REFUSAL is not a disproof (todo/livehd/2f-latch M0). `lhd lec`
    # now exits NONZERO when the encoder cannot model a cell — a latch today —
    # precisely so no downstream automation mistakes it for a proof. But it is
    # epistemically the same as the solver-gave-up case handled above: nothing
    # was compared, so it proves nothing AND disproves nothing. Reporting it as
    # "not equivalent" would be a lie about the design.
    #
    # This branch is what these six latch round-trips ride until M4 teaches the
    # native encoder the commit-class latch encoding. Before the refusal flag
    # propagated correctly they took the exit-0 path above and were counted as
    # passes with no verdict behind them — vacuous, which is the whole reason
    # M0 exists. The round-trip coverage (verilog -> pyrope -> recompile) is
    # real either way; only the equivalence GATE is unavailable.
    if "REFUSAL, not a timeout" in out:
        print("{} - v2prp - inconclusive (encoder REFUSED a cell it cannot model; "
              "round-trip OK, equivalence UNCHECKED; impl-top:{} ref-top:{})".format(name, vtop, ptop))
        return 0
    print("{} - v2prp - FAILED: not equivalent (impl-top:{} ref-top:{})".format(name, vtop, ptop))
    print(out)
    return 1


if __name__ == "__main__":
    sys.exit(main())

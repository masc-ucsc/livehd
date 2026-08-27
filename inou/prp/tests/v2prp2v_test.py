#!/usr/bin/env python3
"""Verilog -> Pyrope -> Verilog gated test with two equivalence checks.

A golden `.v` goes all the way THROUGH Pyrope and back. The emitted Pyrope is
first checked against the hand-written Pyrope twin, then inou/yosys/lgcheck
checks the generated Verilog against the ORIGINAL Verilog:

    lhd compile foo.v      --emit-dir pyrope:P/   --workdir W1   # slang -> lg -> prp_writer
    lhd compile P/*.prp    --emit-dir verilog:V/  --workdir W2   # inou.prp -> tolg -> cgen
    lhd lec --impl P/<top>.prp --ref equiv/foo.prp ...
    lgcheck --reference foo.v --implementation V/all.v \
            --reference_top <vtop> --implementation_top <flat vtop>

The Pyrope leg is deliberate. The old harness went slang -> lg -> cgen DIRECTLY,
which skipped upass/prp_writer and the Pyrope re-read entirely, so a writer that
emitted un-re-parseable or lossy Pyrope was invisible here (three such bugs --
a dropped array type, an undeclared `_mux_N`, an unescaped module name -- passed
this test while failing everywhere else). Routing through the writer makes ONE
run cover the whole `v -> prp -> v` path, which is the path the corpus fuzzing
actually exercises.

The native LEC check used to be a separate `prp-v2prp-*` target which repeated
the Verilog -> Pyrope conversion. Keeping it here preserves that check without
running the conversion twice. The original-Verilog check remains independent:
yosys read_verilog (or yosys-slang via the sibling .prp's `:gold_reader: slang`
header) reads the reference, so a slang misread or a lowering miscompile that
reaches the emitted netlist refutes the miter.

Per-side tops: the reference top is the sibling .prp's `:verilog_top:` header
(the golden module name) or the first module declared in the .v. The
implementation top is the same name FLATTENED to its entity (cgen emits flat
module names: `file.entity` -> `entity`).

Gate semantics:
  native LEC equivalent -> pass
  native LEC not-equivalent -> FAIL
  native LEC refusal/unknown/timeout -> INCONCLUSIVE
  lgcheck exit 0 -> pass
  lgcheck exit 1 -> FAIL (bounded counterexample or a hard yosys error: the
                    designs are not equivalent, or cannot even be compared)
  lgcheck exit 2 -> INCONCLUSIVE: pass with an honest note (no proof, but no
                    counterexample either)
  wall-clock timeout -> inconclusive, NOT a fail

  python3 inou/prp/tests/v2prp2v_test.py -i inou/prp/tests/equiv/trivial_if.v
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

NATIVE_CHECK_TIMEOUT = 60
VERILOG_CHECK_TIMEOUT = 240


def _header(prp_path, key):
    """Return the `:key: value` header field from the sibling Pyrope, or None."""
    try:
        with open(prp_path) as f:
            m = re.search(r"^:%s:\s*([^\s*]+)" % re.escape(key), f.read(), re.M)
            return m.group(1).strip() if m else None
    except OSError:
        return None


def _modules(vpath):
    try:
        with open(vpath) as f:
            # Anchored at the START OF A LINE: an unanchored `\bmodule\s+` also matches the
            # word inside a golden's own prose comment. See prplib.PrpRunner._verilog_modules.
            return re.findall(r"^\s*module\s+\\?([^\s(]+)", f.read(), re.M)
    except OSError:
        return []


def _slang_plugin():
    # yosys-slang plugin for `:gold_reader: slang` goldens (same probes as
    # prplib._yosys_slang_plugin; cwd is the runfiles _main dir under bazel,
    # the repo root on manual runs).
    for cand in ("../+http_archive+yosys_slang/slang.so",
                 "../+_repo_rules+yosys_slang/slang.so",
                 "bazel-bin/external/+http_archive+yosys_slang/slang.so",
                 "bazel-bin/external/+_repo_rules+yosys_slang/slang.so"):
        path = os.path.normpath(cand)
        if os.path.exists(path):
            return path
    return None


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
        print("{} - v2prp2v - FAILED: no original .prp reference next to {}".format(name, v))
        return 1

    vtop = _header(ref_prp, "verilog_top")
    ptop = _header(ref_prp, "pyrope_top")
    if not vtop:
        mods = _modules(v)
        if not mods:
            print("{} - v2prp2v - FAILED: no module declared in {}".format(name, v))
            return 1
        vtop = mods[0]
    ptop = ptop or vtop
    # cgen emits FLAT module names (`file.entity` graph -> `entity` module).
    impl_top = vtop.rsplit(".", 1)[-1]

    work = "tmp_v2prp2v_" + re.sub(r"\W+", "_", name)
    shutil.rmtree(work, ignore_errors=True)
    odir = os.path.join(work, "v")
    os.makedirs(odir, exist_ok=True)

    # 1a. Verilog -> LGraph -> PYROPE (native slang reader, default recipe).
    # `flat_top_io`: a struct is a BUNDLE everywhere inside LiveHD, but this
    # check miters the emitted netlist against the ORIGINAL .v, so the emitted
    # TOP interface must be the source module's packed port list. Only the top
    # needs it — yosys' miter compares the top, and submodule interfaces are
    # internal to each netlist (measured: a pair whose internal child differs
    # bus-vs-leaf still proves). It rides the Pyrope leg: the writer emits the
    # flattened signature, and the recompile keeps it.
    prpdir = os.path.join(work, "prp")
    os.makedirs(prpdir, exist_ok=True)
    comp = subprocess.run(
        [lhd, "compile", v, "--emit-dir", "pyrope:" + prpdir + "/",
         "--set", "compile.slang.flat_top_io=true",
         "--workdir", os.path.join(work, "w1")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("{} - v2prp2v - FAILED: slang->pyrope rc={}".format(name, comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1
    prps = sorted(glob.glob(os.path.join(prpdir, "*.prp")))
    if not prps:
        print("{} - v2prp2v - FAILED: no pyrope emitted in {}".format(name, prpdir))
        return 1

    emitted = os.path.join(prpdir, vtop + ".prp")
    if not os.path.exists(emitted):
        emitted = prps[0]
    with open(emitted) as f:
        emitted_text = f.read()
    if "/* TODO" in emitted_text or "unhandled node" in emitted_text:
        print("{} - v2prp2v - FAILED: writer left a TODO/unhandled node:\n{}".format(
            name, emitted_text))
        return 1

    # 1b. Check the emitted Pyrope against the hand-written Pyrope twin. This
    # is the unique assertion formerly made by every prp-v2prp-* target.
    impl_arg = "pyrope:" + prpdir + "/" if len(prps) > 1 else "pyrope:" + emitted
    native_failed = False
    try:
        native = subprocess.run(
            [lhd, "lec", "--impl", impl_arg, "--ref", "pyrope:" + ref_prp,
             "--impl-top", vtop, "--ref-top", ptop,
             "--workdir", os.path.join(work, "w_native_check")],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=NATIVE_CHECK_TIMEOUT)
    # OUR ENGINE MUST DECIDE (user ruling 2026-08-02). The two legs have
    # DIFFERENT contracts on purpose:
    #
    #   lgcheck  an INDEPENDENT oracle. A definitive FAIL is always an error --
    #            it caught a real difference. Its INCONCLUSIVE is NOT an error:
    #            lgcheck simply could not decide, which says nothing about us.
    #
    #   lhd lec  OUR engine, on OUR corpus. It must PROVE. Anything else --
    #            UNKNOWN, an encoder refusal, a timeout -- is a gap in the tool
    #            that this corpus exists to surface, so it FAILS the test rather
    #            than printing a note nobody reads. PASS(n) counts as proven (a
    #            complete BMC is a real result, just annotated with its depth).
    except subprocess.TimeoutExpired:
        print("{} - v2prp2v - FAILED: lhd lec TIMED OUT >{}s on our own corpus "
              "(our engine must decide these)".format(name, NATIVE_CHECK_TIMEOUT))
        native_failed = True
    else:
        native_out = native.stdout.decode("utf-8", "ignore")
        # Hierarchical LEC may report an intermediate collapsed-box UNKNOWN and
        # then prove the required flat retry.  Judge the final top-level verdict,
        # not diagnostic words retained in that successful retry's detail.
        native_proven = native.returncode == 0 and re.search(
            r"(?m)^lec: .* (?:PROVEN|PASS\(\d+\)) equivalent", native_out)
        if native_proven:
            print("{} - v2prp2v - native Pyrope check success "
                  "(impl-top:{} ref-top:{})".format(name, vtop, ptop))
        elif native.returncode == 0:
            print("{} - v2prp2v - FAILED: lhd lec did not PROVE (inconclusive on our own "
                  "corpus; impl-top:{} ref-top:{})".format(name, vtop, ptop))
            print(native_out)
            native_failed = True
        elif "REFUSAL, not a timeout" in native_out:
            print("{} - v2prp2v - FAILED: lhd lec REFUSED to encode (unmodelled cell; "
                  "impl-top:{} ref-top:{})".format(name, vtop, ptop))
            print(native_out)
            native_failed = True
        else:
            native_failed = True
            print("{} - v2prp2v - FAILED: emitted Pyrope not equivalent to reference "
                  "(impl-top:{} ref-top:{})".format(name, vtop, ptop))
            print(native_out)

    # 1c. PYROPE -> LGraph -> Verilog. Emitting every unit at once is the normal
    # case; a design whose units import each other rejects the duplicates, so
    # fall back to the single unit that carries the top.
    comp = subprocess.run(
        [lhd, "compile"] + prps + ["--emit-dir", "verilog:" + odir + "/",
                                   "--workdir", os.path.join(work, "w2")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0 and len(prps) > 1:
        solo = [p for p in prps
                if os.path.splitext(os.path.basename(p))[0] == impl_top]
        if solo:
            shutil.rmtree(odir, ignore_errors=True)
            os.makedirs(odir, exist_ok=True)
            comp = subprocess.run(
                [lhd, "compile", solo[0], "--emit-dir", "verilog:" + odir + "/",
                 "--workdir", os.path.join(work, "w2b")],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("{} - v2prp2v - FAILED: pyrope->verilog rc={}".format(name, comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1

    gen_vs = sorted(glob.glob(os.path.join(odir, "*.v")))
    if not gen_vs:
        print("{} - v2prp2v - FAILED: no verilog emitted in {}".format(name, odir))
        return 1
    impl = os.path.join(work, "all_impl.v")
    with open(impl, "w") as out:
        for g in gen_vs:
            with open(g) as f:
                out.write(f.read())
                out.write("\n")

    # 2. lgcheck: reference = the ORIGINAL .v (independent yosys read).
    # Like the direct equiv harness, skip this posedge-only oracle when the
    # fixture explicitly selects cvc5 for latch/opposite-edge structure.
    # lgcheck cannot represent the intervening close phase and otherwise burns
    # through its SAT cascade before returning inconclusive.
    if (_header(ref_prp, "equiv_engine") or "").strip() == "cvc5":
        print("{} - v2prp2v - original Verilog check skipped "
              "(:equiv_engine: cvc5; latch/edge structure)".format(name))
        return 1 if native_failed else 0

    cmd = ["./inou/yosys/lgcheck", "--reference", v, "--implementation", impl,
           "--reference_top", vtop, "--implementation_top", impl_top]
    if (_header(ref_prp, "gold_reader") or "") == "slang":
        plugin = _slang_plugin()
        if not plugin:
            print("{} - v2prp2v - FAILED: :gold_reader: slang but yosys-slang plugin not found".format(name))
            return 1
        cmd += ["--gold_reader", "slang", "--slang_plugin", plugin]
    try:
        chk = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             timeout=VERILOG_CHECK_TIMEOUT)
    except subprocess.TimeoutExpired:
        print("{} - v2prp2v - original Verilog check inconclusive "
              "(lgcheck timeout >{}s, NOT a fail)".format(name, VERILOG_CHECK_TIMEOUT))
        return 1 if native_failed else 0

    out = chk.stdout.decode("utf-8", "ignore")
    if chk.returncode == 0:
        print("{} - v2prp2v - original Verilog check success "
              "(ref_top:{} impl_top:{})".format(name, vtop, impl_top))
        return 1 if native_failed else 0
    if chk.returncode == 2:
        # lgcheck's explicit INCONCLUSIVE: no proof but NO counterexample.
        print("{} - v2prp2v - original Verilog check inconclusive "
              "(no proof, no counterexample; ref_top:{} impl_top:{})".format(
            name, vtop, impl_top))
        return 1 if native_failed else 0
    print("{} - v2prp2v - FAILED: round-trip not equivalent "
          "(ref_top:{} impl_top:{})".format(
        name, vtop, impl_top))
    print(out)
    return 1


if __name__ == "__main__":
    sys.exit(main())

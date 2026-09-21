#!/usr/bin/env python3
"""Verilog -> Pyrope -> Verilog regression using the default LEC solver.

Both emitted Pyrope versus its handwritten twin and emitted Verilog versus
its original source must pass. Source Verilog is always read by native Slang;
unknown results, refusals, and timeouts fail the regression.
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

from lec import run_lec, verdict

NATIVE_CHECK_TIMEOUT = 20
# Budget for comparing emitted Verilog with its source using default LEC.
# A fixture can override it with :verilog_check_timeout:; timeouts fail.
VERILOG_CHECK_TIMEOUT = 20


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", required=True, help="golden .v file")
    ap.add_argument("--native-only", action="store_true", help="check the emitted Pyrope against its handwritten twin")
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
    native = run_lec(
        [lhd, "lec", "--impl", impl_arg, "--ref", "pyrope:" + ref_prp,
         "--impl-top", vtop, "--ref-top", ptop,
         "--workdir", os.path.join(work, "w_native_check")], timeout=NATIVE_CHECK_TIMEOUT)
    native_failed = verdict(native) != "proven"
    if native_failed:
        print("{} - v2prp2v - FAILED: native Pyrope proof".format(name))
        print(native.stdout.decode("utf-8", "replace"))
    else:
        print("{} - v2prp2v - native Pyrope proof passed".format(name))
    if args.native_only:
        return int(native_failed)

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

    # 2. Compare the round-trip Verilog with its source using native Slang LEC.
    verilog_timeout = VERILOG_CHECK_TIMEOUT
    hdr_timeout = _header(ref_prp, "verilog_check_timeout")
    if hdr_timeout:
        try:
            verilog_timeout = int(hdr_timeout)
        except ValueError:
            print("{} - v2prp2v - FAILED: :verilog_check_timeout: must be an "
                  "integer (got '{}')".format(name, hdr_timeout))
            return 1

    cmd = [lhd, "lec", "--ref", v, "--impl", impl,
           "--ref-top", vtop, "--impl-top", impl_top,
           "--workdir", os.path.join(work, "w_verilog_check")]
    chk = run_lec(cmd, timeout=verilog_timeout)

    out = chk.stdout.decode("utf-8", "ignore")
    if verdict(chk) == "proven":
        print("{} - v2prp2v - original Verilog check success "
              "(ref_top:{} impl_top:{})".format(name, vtop, impl_top))
        return 1 if native_failed else 0
    print("{} - v2prp2v - FAILED: round-trip did not prove equivalent "
          "(ref_top:{} impl_top:{})".format(name, vtop, impl_top))
    print(out)
    return 1


if __name__ == "__main__":
    sys.exit(main())

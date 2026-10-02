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

from lec import final_verdict, run_lec, verdict

NATIVE_CHECK_TIMEOUT = 20
# Budget for comparing emitted Verilog with its source using default LEC.
# A fixture can override it with :verilog_check_timeout:; timeouts fail.
VERILOG_CHECK_TIMEOUT = 20
# Budget for the graphs-flow (lg+pyrope, DCE on) Pyrope vs the source Verilog.
# Unlike the checks above, an UNKNOWN/timeout here passes; only REFUTED fails.
GRAPHS_CHECK_TIMEOUT = 5


def _same_emission(prps, gprps):
    """True when both emission dirs hold the same file names with identical text."""
    if [os.path.basename(p) for p in prps] != [os.path.basename(p) for p in gprps]:
        return False
    for a, b in zip(prps, gprps):
        with open(a, "rb") as fa, open(b, "rb") as fb:
            if fa.read() != fb.read():
                return False
    return True


def _lenient_verdict(result):
    """proven / inconclusive (UNKNOWN or timeout: pass) / failed (REFUTED, crash, refusal)."""
    if verdict(result) == "proven":
        return "proven"
    text = result.stdout.decode("utf-8", "replace")
    last = final_verdict(text)
    if result.returncode == 10 or " REFUTED " in last:
        return "failed"
    if result.returncode == 124 and "LEC outer watchdog exceeded" in text:
        return "inconclusive"
    # An encoder REFUSAL (a cell the encoder does not model, or a top with no
    # observable output) also exits 7 with a final UNKNOWN line, but it compared
    # nothing: lhd lec reports it as a `lec REFUSED` error. That is a failure,
    # not a solver give-up, so it must not pass as inconclusive.
    if "lec REFUSED " in text:
        return "failed"
    if result.returncode == 7 and " UNKNOWN " in last:
        return "inconclusive"
    return "failed"


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

    # 1a'. The same emission through the GRAPHS flow: an `lg:` sink next to the
    # `pyrope:` one (the `lhd compile verilog --emit-dir lg: --emit-dir pyrope:`
    # line lhdtrack seeds its trees with). Pyrope-only emission keeps parameter
    # provenance, which turns off upass's named-store DCE, so 1a alone never
    # exercises it. With the DCE on, an unread `<port>__bpr.<f>` readback wire
    # (inou.slang's bundle-output reader view) used to lose its driver but keep
    # its `wire` declaration, and the writer's self-check rejected the undriven
    # wire (lhdtrack suggestions7 item 4: 44 of 119 designs). The compile must
    # succeed, and -- since this DCE-on Pyrope is what lhdtrack seeds from --
    # it must also stay equivalent to the source Verilog (step 1a'' below).
    gdir = os.path.join(work, "prp_graphs")
    comp = subprocess.run(
        [lhd, "compile", v, "--emit-dir", "lg:" + os.path.join(work, "lg_graphs"),
         "--emit-dir", "pyrope:" + gdir + "/",
         "--set", "compile.slang.flat_top_io=true",
         "--workdir", os.path.join(work, "w1g")],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if comp.returncode != 0:
        print("{} - v2prp2v - FAILED: slang->lg+pyrope rc={}".format(name, comp.returncode))
        print(comp.stdout.decode("utf-8", "ignore"))
        return 1

    # 1a''. LEC the graphs-flow Pyrope against the source Verilog. When the
    # emission is byte-identical to 1a's, steps 1b/1c/2 already check that
    # very text, so the extra LEC is spent only on a differing emission (the
    # DCE actually removed something). Short budget: UNKNOWN or a timeout
    # passes; only a REFUTED verdict or a crash/refusal fails.
    gprps = sorted(glob.glob(os.path.join(gdir, "*.prp")))
    if not gprps:
        print("{} - v2prp2v - FAILED: no pyrope emitted in {}".format(name, gdir))
        return 1
    if not _same_emission(prps, gprps):
        gemitted = os.path.join(gdir, vtop + ".prp")
        if not os.path.exists(gemitted):
            gemitted = gprps[0]
        gimpl = "pyrope:" + gdir + "/" if len(gprps) > 1 else "pyrope:" + gemitted
        graphs = run_lec(
            [lhd, "lec", "--impl", gimpl, "--ref", "verilog:" + v,
             "--impl-top", vtop, "--ref-top", vtop,
             "--workdir", os.path.join(work, "w_graphs_check")],
            timeout=GRAPHS_CHECK_TIMEOUT)
        gout = graphs.stdout.decode("utf-8", "replace")
        gstatus = _lenient_verdict(graphs)
        if gstatus == "failed":
            print("{} - v2prp2v - FAILED: graphs-flow Pyrope (lg+pyrope emission) "
                  "is not equivalent to the source Verilog".format(name))
            print(gout)
            return 1
        print("{} - v2prp2v - graphs-flow Pyrope check {}".format(name, gstatus))

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
    # A `:lec_expect: refuted` pair is a MUTATED golden: its round trip must
    # still differ from the handwritten Pyrope. The Verilog leg below compares
    # the golden with its OWN round trip, so it must prove either way.
    expect = (_header(ref_prp, "lec_expect") or "proven").lower()
    if expect == "refuted":
        native_failed = native.returncode != 10 or " REFUTED " not in final_verdict(
            native.stdout.decode("utf-8", "replace"))
    else:
        native_failed = verdict(native) != "proven"
    if native_failed:
        print("{} - v2prp2v - FAILED: native Pyrope check did not report {}".format(name, expect.upper()))
        print(native.stdout.decode("utf-8", "replace"))
    else:
        print("{} - v2prp2v - native Pyrope check {}".format(name, "refuted as expected" if expect == "refuted" else "passed"))
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

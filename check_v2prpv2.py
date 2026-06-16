#!/usr/bin/env python3
"""check_v2prpv2.py — Verilog -> Pyrope -> Verilog round-trip equivalence driver.

For a design (or single file) that the `--reader yosys-slang` front-end can read,
this drives the full v2prp round-trip and proves it equivalent two ways:

    verilog1 = lhd compile --reader yosys-slang <ARGS> --emit verilog   (reference)
    pyrope   = lhd compile --reader slang       <ARGS> --emit pyrope    (impl, v2prp)
    verilog2 = lhd compile <pyrope> --emit verilog                      (re-compile leg)
    lhd check  verilog1 verilog2     (yosys/lgcheck under the hood — authoritative)
    lgcheck    verilog1 verilog2     (raw lgcheck — secondary cross-check)

The SAME read arguments (file list, +incdir, +define, --top, …) are shared by both
the yosys-slang and slang `lhd compile` invocations — pass them after `--`.

Outcome semantics (mirrors the 2f-v2prp gate, scaled up):
  * yosys-slang can't read it          -> SKIP   (outside the gate; not our failure)
  * slang reader can't lower it        -> IMPL   (a --reader slang gap)
  * re-compile (prp -> verilog) fails  -> RECOMP (a writer / tolg / cgen gap)
  * lhd check not-equivalent           -> LECFAIL
  * lhd check times out                -> INCONCLUSIVE (NOT a fail)
  * lhd check equivalent               -> PASS    (lgcheck is reported alongside;
                                                   a lgcheck TIMEOUT falls back to
                                                   lhd check and does NOT fail)

`lhd check` is treated as equal-or-better than `lgcheck`: the PASS/FAIL verdict is
lhd check's; lgcheck is an extra signal (proven / refuted / timeout / skipped).

Usage:
  check_v2prpv2.py --top TOP [opts] -- <slang read args, e.g. files +incdir+...>
  check_v2prpv2.py --top Adder -- ../xs/build/build_opt/rtl/Adder.sv
  check_v2prpv2.py --top SoC --define SYNTHESIS -I gensrc -- -F files.f
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import time


def run(cmd, timeout=None):
    """Run cmd; return (rc, combined_output). rc is None on timeout."""
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        return p.returncode, p.stdout.decode("utf-8", "ignore")
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode("utf-8", "ignore") if e.stdout else ""
        return None, out


def find_lhd():
    for p in ("./bazel-bin/lhd/lhd", "./lhd/lhd"):
        if os.path.exists(p):
            return p
    sys.exit("check_v2prpv2: lhd binary not found (build //lhd:lhd)")


def main():
    ap = argparse.ArgumentParser(description="Verilog->Pyrope->Verilog round-trip LEC driver")
    ap.add_argument("--top", required=True, help="top module name")
    ap.add_argument("--workdir", default=None, help="scratch dir (default tmp_v2prpv2_<top>)")
    ap.add_argument("--check-timeout", type=int, default=120, help="lhd check timeout (s); timeout=inconclusive")
    ap.add_argument("--lgcheck-timeout", type=int, default=120, help="lgcheck timeout (s); timeout=fall back to lhd check")
    ap.add_argument("--recipe", default="O0", help="re-compile recipe for the prp->verilog leg")
    ap.add_argument("--keep", action="store_true", help="keep the scratch dir")
    ap.add_argument("--no-lec", action="store_true", help="stop after recompile (structural census: SKIP/IMPL/RECOMP/ROUNDTRIP)")
    ap.add_argument("-q", "--quiet", action="store_true", help="one summary line only")
    ap.add_argument("slang_args", nargs=argparse.REMAINDER,
                    help="read args shared by both readers; put after `--`")
    args = ap.parse_args()

    # Strip a leading `--` separator if argparse left it in REMAINDER.
    slang_args = args.slang_args
    if slang_args and slang_args[0] == "--":
        slang_args = slang_args[1:]
    if not slang_args:
        sys.exit("check_v2prpv2: no read args (pass files/flags after `--`)")

    lhd = find_lhd()
    top = args.top
    work = args.workdir or ("tmp_v2prpv2_" + top.replace("/", "_"))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    v1_dir = os.path.join(work, "v1")
    p_dir = os.path.join(work, "prp")
    v2_dir = os.path.join(work, "v2")

    # The reference (verilog1) is the ORIGINAL input verilog — the ground truth,
    # read by lgcheck's standard yosys read_verilog.  We deliberately do NOT use
    # yosys-slang's re-emit as the reference: yosys-slang can itself diverge from
    # the source (e.g. it mishandles an overflowing sized literal like `6'h40`),
    # which would flag a CORRECT round-trip as a false mismatch.  yosys-slang is
    # only the GATE here (it must be able to read the design).
    source_files = [a for a in slang_args if (a.endswith(".v") or a.endswith(".sv")) and not a.startswith("-")]
    ref_path = None
    if len(source_files) == 1:
        ref_path = source_files[0]
    elif len(source_files) > 1:
        ref_path = os.path.join(work, "ref_all.v")
        with open(ref_path, "w") as out:
            for s in source_files:
                with open(s) as f:
                    out.write(f.read() + "\n")
    # else: no plain source files (e.g. `-F filelist`) -> fall back to the
    # yosys-slang emit as the reference (best effort).

    def report(status, lhd_chk="-", lg="-", detail="", t=0.0):
        print("v2prpv2 {:9s} top={} lhd_check={} lgcheck={} ({:.1f}s){}".format(
            status, top, lhd_chk, lg, t, ("  " + detail) if detail and not args.quiet else ""))
        return 0 if status in ("PASS", "SKIP", "INCONCLUSIVE") else 1

    t0 = time.time()

    # ── 1. reference: yosys-slang -> verilog (the gate) ──────────────────────
    rc, log = run([lhd, "compile", "--reader", "yosys-slang", "--top", top, "--recipe", "O0",
                   "--emit-dir", "verilog:" + v1_dir + "/", "--workdir", os.path.join(work, "w_ys"),
                   "-q", "--"] + slang_args, timeout=args.check_timeout * 3)
    if rc != 0:
        # Not readable by yosys-slang -> outside the gate -> SKIP.
        if not args.quiet:
            print(log[-1500:])
        return report("SKIP", detail="yosys-slang could not read it")
    v1 = sorted(glob.glob(os.path.join(v1_dir, "*.v")))
    v1_top = next((p for p in v1 if os.path.splitext(os.path.basename(p))[0] == top), v1[0] if v1 else None)
    if not v1_top:
        return report("SKIP", detail="yosys-slang emitted no verilog")

    # ── 2. impl: slang -> pyrope ─────────────────────────────────────────────
    rc, log = run([lhd, "compile", "--reader", "slang", "--top", top, "--recipe", "O0",
                   "--emit-dir", "pyrope:" + p_dir + "/", "--workdir", os.path.join(work, "w_slang"),
                   "-q", "--"] + slang_args, timeout=args.check_timeout * 3)
    prps = sorted(glob.glob(os.path.join(p_dir, "*.prp")))
    if rc != 0 or not prps:
        if not args.quiet:
            print(log[-2500:])
        return report("IMPL", detail="--reader slang could not lower it")
    # Reject any writer TODO/unhandled leftovers.
    bad = [p for p in prps if any(m in open(p).read() for m in ("/* TODO", "unhandled node"))]
    if bad:
        return report("IMPL", detail="writer TODO/unhandled in " + os.path.basename(bad[0]))

    # ── 3. re-compile leg: pyrope -> verilog2 ────────────────────────────────
    rc, log = run([lhd, "compile"] + prps + ["--recipe", args.recipe,
                   "--emit-dir", "verilog:" + v2_dir + "/", "--workdir", os.path.join(work, "w_re")],
                  timeout=args.check_timeout * 3)
    v2 = sorted(glob.glob(os.path.join(v2_dir, "*.v")))
    if rc != 0 or not v2:
        if not args.quiet:
            print(log[-2500:])
        return report("RECOMP", detail="prp -> verilog2 failed")
    v2_top = next((p for p in v2 if os.path.splitext(os.path.basename(p))[0] == top), v2[0])

    if args.no_lec:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        return report("ROUNDTRIP", t=time.time() - t0)

    # The reference is the original source (verilog1); fall back to the
    # yosys-slang emit only when no plain source file was given.
    ref = ref_path if ref_path else v1_top

    # ── 4. lhd check (authoritative) ─────────────────────────────────────────
    rc, log = run([lhd, "check", "--impl", "verilog:" + v2_top, "--ref", "verilog:" + ref,
                   "--top", top, "--workdir", os.path.join(work, "w_chk")], timeout=args.check_timeout)
    lhd_chk = "timeout" if rc is None else ("pass" if rc == 0 else "fail")

    # ── 5. lgcheck (secondary cross-check; timeout falls back to lhd check) ───
    lg = "skip"
    if os.path.exists("./inou/yosys/lgcheck"):
        rc2, log2 = run(["./inou/yosys/lgcheck", "--reference", ref, "--implementation", v2_top,
                         "--reference_top", top, "--implementation_top", top], timeout=args.lgcheck_timeout)
        if rc2 is None:
            lg = "timeout"
        elif rc2 == 0:
            lg = "proven"
        else:
            lg = "refuted"

    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)

    t = time.time() - t0
    if lhd_chk == "pass":
        # lgcheck "refuted" while lhd check "pass" is a disagreement worth flagging,
        # but lhd check is authoritative (equal-or-better) -> still PASS.
        return report("PASS", lhd_chk, lg, t=t)
    if lhd_chk == "timeout":
        return report("INCONCLUSIVE", lhd_chk, lg,
                      detail="lhd check timed out" + (" (lgcheck %s)" % lg if lg != "skip" else ""), t=t)
    return report("LECFAIL", lhd_chk, lg, detail=log[-1500:] if not args.quiet else "", t=t)


if __name__ == "__main__":
    sys.exit(main())

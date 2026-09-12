#!/usr/bin/env python3
"""Direction 2 sweep: classify every generated `DesignCert` with the DIRECT
simulator's own admission test, and run it for a few cycles.

Why this exists.  `checkDesign` is an INDEPENDENT, source-level acceptance
predicate (`pass/lean/../formal/lean/LeanSemanticPrimitives/Compiler/DirectCheck.lean`)
whose accepted-operator table was derived from the exporter's `cert_node_expr`,
not copied from `compileOp`'s refusal list.  A table derived that way is only
worth anything if it is checked against the certificates real designs actually
produce -- that is this sweep.  Every design is classified ACCEPTED or REFUSED
WITH A REASON; a refusal that is not explainable is a bug in the table.

The probe it generates imports `DirectTrace`, NOT `CompileDesign`, so the direct
path elaborates without Mathlib.  That is not an optimisation detail: it is the
point.  The direct simulator does not need the verified compiler to exist.

Usage:
  direct_sweep.py --out SWEEP.tsv [--jobs 4] [--cycles 4] [--limit N]
                  [--filter REGEX] ROOT [ROOT...]
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

LEAN_DIR = os.path.realpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..", "formal", "lean"))

CERT_RE = re.compile(r"^def\s+(\w+)_designCert\s*:\s*DesignCert", re.M)
TAIL_MARKERS = ("/-- Compile-and-run.", "\ndef ")  # cut before the residual half

PROBE_TAIL = r"""
open Compiler Compiler.Direct in
#eval show IO Unit from do
  let D := @BASE@_designCert
  IO.println s!"SHAPE sources={D.sources.size} nodes={D.nodes.size} outputs={D.outputs.size} flops={D.flops.size} mems={D.memories.size} inputs={inputArity D}"
  let t0 ← IO.monoMsNow
  let verdict := match designErrors D with
    | none   => "ACCEPTED"
    | some e => "REFUSED\t" ++ e.tag ++ "\t" ++ e.render
  let t1 ← IO.monoMsNow
  IO.println s!"VERDICT\t{verdict}"
  IO.println s!"CHECK_MS {t1 - t0}"
  if (designErrors D).isNone then
    let s0 := zeroState D
    let i0 := zeroInput D
    let t2 ← IO.monoMsNow
    let r1 := directStepRaw D i0 s0
    let h1 := r1.outputs.foldl (fun a b => a + (bv_uint b).toNat) 0
    let t3 ← IO.monoMsNow
    IO.println s!"STEP1_MS {t3 - t2} outsum {h1}"
    let t4 ← IO.monoMsNow
    let mut st := s0
    let mut acc := 0
    for _ in [0:@CYCLES@] do
      let r := directStepRaw D i0 st
      acc := acc + r.outputs.foldl (fun a b => a + (bv_uint b).toNat) 0
      st := r.nextState
    let t5 ← IO.monoMsNow
    IO.println s!"RUN_MS {t5 - t4} cycles @CYCLES@ outsum {acc}"
    -- the CHECKED public entry point must agree that this is runnable
    match runDirect D s0 (List.replicate @CYCLES@ i0) with
    | .error e => IO.println s!"RUNDIRECT\tREFUSED\t{e.tag}"
    | .ok t    => IO.println s!"RUNDIRECT\tOK\t{t.steps.length}"
"""


def build_probe(src_text, base, cycles, max_rec_depth=0):
    """Swap the import for the Mathlib-free one, drop the residual half, append the probe.

    `max_rec_depth` overrides the `set_option maxRecDepth` the exporter emits.
    The emitted 1,000,000 is not enough for the two 14 MB CVA6 hpdcache
    certificates: Lean's front end exhausts it while ELABORATING the
    `sources := #[..]` literal, the definition becomes noncomputable, and the
    probe fails before `checkDesign` is ever called.  20,000,000 clears it (at
    ~4 h and 16.6 GB for a 107,213-node design, essentially all of it
    elaboration -- the semantic check itself is under two seconds).
    """
    text = src_text.replace(
        "import LeanSemanticPrimitives.Compiler.CompileDesign",
        "import LeanSemanticPrimitives.Compiler.DirectTrace")
    if max_rec_depth:
        text = re.sub(r"^set_option maxRecDepth \d+$",
                      f"set_option maxRecDepth {max_rec_depth}", text, count=1, flags=re.M)
    cut = text.find("/-- Compile-and-run.")
    if cut < 0:
        # older/newer emitter wording: cut at the first decl after the certificate
        m = re.search(r"^def\s+\w+_step\s*:", text, re.M)
        cut = m.start() if m else len(text)
    return text[:cut] + PROBE_TAIL.replace("@BASE@", base).replace("@CYCLES@", str(cycles))


def run_one(path, workdir, cycles, timeout, lean_env, max_rec_depth=0):
    base_m = CERT_RE.search(open(path, encoding="utf-8", errors="replace").read(1 << 22))
    name = os.path.basename(path).replace("_Lgraph.lean", "")
    row = {"module": name, "path": path, "verdict": "NO_CERT", "reason": "",
           "sources": "", "nodes": "", "outputs": "", "flops": "", "mems": "",
           "inputs": "", "check_ms": "", "step1_ms": "", "run_ms": "",
           "cycles": cycles, "wall_s": "", "rss_kb": ""}
    if not base_m:
        return row
    base = base_m.group(1)
    src = open(path, encoding="utf-8", errors="replace").read()
    probe = os.path.join(workdir, f"{name}_probe.lean")
    with open(probe, "w", encoding="utf-8") as f:
        f.write(build_probe(src, base, cycles, max_rec_depth))
    cmd = ["/usr/bin/time", "-f", "%e %M", "nice", "-n", "19", "ionice", "-c", "3",
           "lean", probe]
    t0 = time.time()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                           cwd=LEAN_DIR, env=lean_env)
        out, err = p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        row["verdict"] = "TIMEOUT"
        row["wall_s"] = f"{time.time() - t0:.1f}"
        return row
    row["wall_s"] = f"{time.time() - t0:.1f}"
    tail = err.strip().splitlines()
    if tail:
        m = re.match(r"^([\d.]+)\s+(\d+)$", tail[-1])
        if m:
            row["rss_kb"] = m.group(2)
            tail = tail[:-1]
    for line in out.splitlines():
        if line.startswith("SHAPE "):
            for kv in line[6:].split():
                k, _, v = kv.partition("=")
                if k in row:
                    row[k] = v
        elif line.startswith("VERDICT\t"):
            parts = line.split("\t")
            row["verdict"] = parts[1].strip('"')
            if len(parts) > 2:
                row["reason"] = parts[2] + (": " + parts[3].rstrip('"') if len(parts) > 3 else "")
        elif line.startswith("CHECK_MS "):
            row["check_ms"] = line.split()[1]
        elif line.startswith("STEP1_MS "):
            row["step1_ms"] = line.split()[1]
        elif line.startswith("RUN_MS "):
            row["run_ms"] = line.split()[1]
        elif line.startswith("RUNDIRECT\t"):
            parts = line.split("\t")
            if parts[1] != "OK":
                row["verdict"] = "RUN_REFUSED"
                row["reason"] = parts[2] if len(parts) > 2 else ""
    if row["verdict"] == "NO_CERT" and tail:
        row["verdict"] = "LEAN_ERROR"
        row["reason"] = " | ".join(t[:200] for t in tail[:3])
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--cycles", type=int, default=4)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--filter", default="")
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--max-rec-depth", type=int, default=0,
                    help="override the emitted `set_option maxRecDepth`; the emitted\n1000000 is not enough for the largest CVA6 certificates (try 20000000)")
    ap.add_argument("--keep", action="store_true", help="keep the generated probes")
    args = ap.parse_args()

    pat = re.compile(args.filter) if args.filter else None
    seen, files = set(), []
    for root in args.roots:
        for dirpath, _dirs, names in os.walk(root):
            for n in sorted(names):
                if not n.endswith("_Lgraph.lean"):
                    continue
                p = os.path.join(dirpath, n)
                if pat and not pat.search(p):
                    continue
                try:
                    h = hashlib.md5(open(p, "rb").read()).hexdigest()
                except OSError:
                    continue
                if h in seen:
                    continue
                if b"_designCert" not in open(p, "rb").read(1 << 20):
                    continue
                seen.add(h)
                files.append(p)
    files.sort(key=lambda p: os.path.getsize(p))
    if args.limit:
        files = files[:args.limit]
    print(f"# {len(files)} unique certificates", file=sys.stderr)

    lean_env = dict(os.environ)
    lean_env["LEAN_NUM_THREADS"] = "1"
    # `lake env` once, reused for every job
    lp = subprocess.run(["lake", "env", "printenv", "LEAN_PATH"], cwd=LEAN_DIR,
                        capture_output=True, text=True)
    if lp.returncode == 0 and lp.stdout.strip():
        lean_env["LEAN_PATH"] = lp.stdout.strip()

    workdir = tempfile.mkdtemp(prefix="d2sweep_", dir=os.environ.get("TMPDIR", "/tmp"))
    cols = ["module", "verdict", "reason", "sources", "nodes", "outputs", "flops",
            "mems", "inputs", "check_ms", "step1_ms", "run_ms", "cycles", "wall_s",
            "rss_kb", "path"]
    rows = []
    try:
        with ThreadPoolExecutor(max_workers=args.jobs) as ex:
            futs = [ex.submit(run_one, p, workdir, args.cycles, args.timeout, lean_env,
                              args.max_rec_depth)
                    for p in files]
            for i, f in enumerate(futs):
                r = f.result()
                rows.append(r)
                print(f"[{i+1}/{len(files)}] {r['module']:40s} {r['verdict']:12s} "
                      f"nodes={r['nodes']:>7s} wall={r['wall_s']:>7s}s {r['reason'][:60]}",
                      file=sys.stderr, flush=True)
    finally:
        if not args.keep:
            shutil.rmtree(workdir, ignore_errors=True)
        else:
            print(f"# probes kept in {workdir}", file=sys.stderr)

    rows.sort(key=lambda r: (r["verdict"] != "ACCEPTED", r["module"]))
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\t".join(cols) + "\n")
        for r in rows:
            f.write("\t".join(str(r.get(c, "")) for c in cols) + "\n")
    n_ok = sum(1 for r in rows if r["verdict"] == "ACCEPTED")
    print(f"# ACCEPTED {n_ok}/{len(rows)} -> {args.out}", file=sys.stderr)
    return 0 if n_ok == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())

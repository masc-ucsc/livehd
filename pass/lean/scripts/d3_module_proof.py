#!/usr/bin/env python3
"""Prove one design through the multi-module chunk path, writing every artifact
into a PROJECT-LOCAL run directory.

Proving all chunk facts of a large design in one process dies in the audit:
`collectAxioms` forces the kernel to check what elaboration only dispatched.
Auditing an IMPORTED theorem costs ~7 MB instead, because the body was checked
when its module was built. So the facts are built as modules and a composition
imports them.

Everything this produces -- per-group build logs with time and RSS, the
composition transcript, the digests, and the exact commands -- is written under
`--out-dir`. Nothing is left only on a terminal, and no later run depends on a
previous run's scrollback.

DIGEST ENFORCEMENT. The generated modules record the certificate's sha256 and
the generator's tool digest. Those document provenance; they do not enforce it,
because a stale `.olean` would still be on disk and lake would happily reuse
it. Before reusing anything this RECOMPUTES both from the inputs on disk and
compares. On a mismatch it regenerates and forces a rebuild rather than
trusting the artifact.
"""
import argparse, hashlib, importlib.util, json, pathlib, shutil, subprocess, sys, time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent
LEAN = ROOT / "formal/lean"
_spec = importlib.util.spec_from_file_location("d3_sweep_mod", HERE / "d3_sweep.py")
sweep = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sweep)


def run(cmd, log: pathlib.Path, cwd=None):
    t0 = time.time()
    with log.open("w", encoding="utf-8") as fh:
        p = subprocess.run(["/usr/bin/time", "-v"] + cmd, cwd=cwd,
                           stdout=fh, stderr=subprocess.STDOUT)
    txt = log.read_text(encoding="utf-8", errors="replace")
    rss = next((int(l.split()[-1]) for l in txt.splitlines()
                if "Maximum resident" in l), 0)
    return p.returncode, rss, time.time() - t0, txt


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cert", required=True)
    ap.add_argument("--module", required=True)
    ap.add_argument("--bindings", type=int, required=True)
    ap.add_argument("--chunk-size", type=int, default=32)
    ap.add_argument("--per-group", type=int, default=4)
    ap.add_argument("--group-max-kb", type=int, default=10000000)
    ap.add_argument("--final-max-kb", type=int, default=14000000)
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args()

    cert = pathlib.Path(a.cert).resolve()
    out = pathlib.Path(a.out_dir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    csha = hashlib.sha256(cert.read_bytes()).hexdigest()
    tsha = sweep.tool_digest()
    ns = "".join(ch for ch in a.module.title() if ch.isalnum())
    gen = LEAN / "LeanSemanticPrimitives/Gen" / ns

    # ---- digest enforcement, recomputed from disk ------------------------
    stamp = gen / "provenance.json"
    stale = True
    if stamp.is_file():
        try:
            old = json.loads(stamp.read_text())
            stale = not (old.get("cert_sha256") == csha
                         and old.get("tool_digest") == tsha)
        except Exception:
            stale = True
    if stale and gen.exists():
        print(f"d3_module_proof: inputs changed or unstamped -- discarding {gen}")
        shutil.rmtree(gen)
        for d in (LEAN / ".lake/build/lib/lean/LeanSemanticPrimitives/Gen" / ns,):
            if d.exists():
                shutil.rmtree(d)

    nchunk = (a.bindings + a.chunk_size - 1) // a.chunk_size
    ngroup = (nchunk + a.per_group - 1) // a.per_group
    subprocess.run([sys.executable, str(HERE / "d3_gen_modules.py"),
                    "--cert", str(cert), "--module", a.module,
                    "--bindings", str(a.bindings),
                    "--chunk-size", str(a.chunk_size),
                    "--per-group", str(a.per_group)], check=True)
    stamp.write_text(json.dumps(
        {"cert": str(cert), "cert_sha256": csha, "tool_digest": tsha,
         "module": a.module, "namespace": ns, "bindings": a.bindings,
         "chunk_size": a.chunk_size, "per_group": a.per_group,
         "chunks": nchunk, "groups": ngroup}, indent=2) + "\n")

    gb = str(HERE / "d3_guarded_build.py")
    rows = []
    for m in ["Cert"] + [f"G{g}" for g in range(ngroup)]:
        cmd = [sys.executable, gb, "--max-kb", str(a.group_max_kb), "--",
               "lake", "build", f"LeanSemanticPrimitives.Gen.{ns}.{m}"]
        rc, rss, wall, txt = run(cmd, out / f"build_{m}.log", cwd=LEAN)
        rows.append({"module": m, "rc": rc, "max_rss_kb": rss,
                     "wall_s": round(wall, 2),
                     "cgroup": next((l for l in txt.splitlines()
                                     if l.startswith("d3_guarded_build")), "")})
        print(f"  {m:<5} rc={rc} max_rss={rss:,} kB wall={wall:.1f}s")
        if rc != 0:
            (out / "result.json").write_text(json.dumps(
                {"status": "group_build_failed", "module": m,
                 "groups": rows}, indent=2) + "\n")
            return 1

    comp = out / "composition.lean"
    comp.write_text(
        f"import LeanSemanticPrimitives.Gen.{ns}.G{ngroup-1}\n"
        "import LeanSemanticPrimitives.Compiler.D3Harness\n"
        "set_option maxRecDepth 1000000\nset_option maxHeartbeats 0\n"
        "open Compiler\n\n"
        f"reify_design_chunked {a.module}_designCert as d3_fast size {a.chunk_size}\n"
        f"prove_reified_chunked {a.module}_designCert as d3_fast "
        f"size {a.chunk_size} using {ns}\n"
        "d3_proof_gate d3_fast.correct\n", encoding="utf-8")
    cmd = [sys.executable, gb, "--max-kb", str(a.final_max_kb), "--",
           "lake", "env", "lean", str(comp)]
    rc, rss, wall, txt = run(cmd, out / "composition.log", cwd=LEAN)
    gate = next((l for l in txt.splitlines() if "D3GATE proof=1" in l), "")
    res = {"status": "proved" if gate else "not_proved",
           "module": a.module, "cert": str(cert), "cert_sha256": csha,
           "tool_digest": tsha, "bindings": a.bindings,
           "chunk_size": a.chunk_size, "chunks": nchunk, "groups": ngroup,
           "group_max_kb": a.group_max_kb, "final_max_kb": a.final_max_kb,
           "final_rc": rc, "final_max_rss_kb": rss, "final_wall_s": round(wall, 2),
           "gate_line": gate, "group_builds": rows,
           "command": " ".join(sys.argv)}
    (out / "result.json").write_text(json.dumps(res, indent=2) + "\n")
    print(f"  FINAL rc={rc} max_rss={rss:,} kB wall={wall:.1f}s")
    print(f"  {gate or 'NO GATE LINE -- not proved'}")
    print(f"  artifacts: {out}")
    return 0 if gate else 1


if __name__ == "__main__":
    raise SystemExit(main())

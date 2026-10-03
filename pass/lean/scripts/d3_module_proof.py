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
import argparse, hashlib, importlib.util, json, os, pathlib, re, shutil, signal, subprocess, sys, time

ALLOWED = {"propext", "Classical.choice", "Quot.sound"}
# Distinct from any Lean exit status so a timeout can never be read as a
# toolchain error, or the reverse. 124 is what `timeout(1)` uses, and
# `d3_sweep.run_group` already uses the same convention.
RC_TIMEOUT = 124
# The composition always names its model `d3_fast`, so the gate must be for
# `d3_fast.correct` specifically -- any other theorem's gate line appearing in
# the transcript is not evidence about this model.
GATE_RE = re.compile(r"^D3GATE proof=1 thm=(d3_fast\.correct) axioms=\[([^\]]*)\]$")

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent
LEAN = ROOT / "formal/lean"
_spec = importlib.util.spec_from_file_location("d3_sweep_mod", HERE / "d3_sweep.py")
sweep = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sweep)


def runner_digest() -> str:
    """`d3_sweep.tool_digest()` plus the inputs IT does not cover.

    TOOL_FILES already carries ReifyProof, CompileDesign, the *Defs* modules,
    ReifyGen, Reify and D3Harness. It does NOT carry `CompileGraph.lean`,
    `CompileOp.lean`, or either of the scripts that generate and drive these
    modules -- all of which change what a generated module MEANS, so a stamp
    keyed on `tool_digest` alone would treat an olean built before such a
    change as current.

    `d3_guarded_build.py` is here for a different reason, and it is worth
    stating because it is the easy one to leave out: it does not change what a
    module means, it changes what "built under the cap" CLAIMS. Weaken the
    wrapper -- drop the refusal when no delegated cgroup exists, stop reading
    `memory.events` -- and every existing stamp still matches while the
    guarantee behind it is gone. A digest over meaning alone cannot catch
    that, so the enforcement mechanism is digested alongside the inputs.
    """
    h = hashlib.sha256()
    h.update(sweep.tool_digest().encode())
    for f in (HERE / "d3_gen_modules.py", HERE / "d3_module_proof.py",
              HERE / "d3_guarded_build.py",
              LEAN / "LeanSemanticPrimitives/Compiler/CompileGraph.lean",
              LEAN / "LeanSemanticPrimitives/Compiler/CompileOp.lean",
              LEAN / "LeanSemanticPrimitives/Compiler/ReifyProof.lean"):
        try:
            h.update(f.name.encode() + b":" + hashlib.sha256(f.read_bytes()).digest())
        except OSError:
            h.update(f.name.encode() + b":<missing>")
    return h.hexdigest()


def olean_snapshot(ns: str) -> str:
    """The BUILT artifacts the composition will import, not the sources.

    Two halves, because either can change under an unchanged source digest:
    `d3_sweep.artifact_digest()` covers the compiled Compiler library, and the
    generated `Gen/<ns>` oleans cover this design's facts. `.lake` is shared,
    so another worktree rebuilding the library would change what an import
    MEANS while every source hash stayed put.
    """
    h = hashlib.sha256()
    h.update(sweep.artifact_digest().encode())
    odir = LEAN / ".lake/build/lib/lean/LeanSemanticPrimitives/Gen" / ns
    for f in sorted(odir.glob("*.olean")) if odir.is_dir() else []:
        h.update(f.name.encode() + b":" + hashlib.sha256(f.read_bytes()).digest())
    return h.hexdigest()


def run(cmd, log: pathlib.Path, cwd=None, timeout: float = 0):
    """Run one stage under `/usr/bin/time -v`, optionally bounded in time.

    `timeout <= 0` means unbounded, which is what every call did before this
    and is still right for the cheap group builds. On expiry the return code
    is `RC_TIMEOUT`, which the caller turns into `status="timeout"` --
    UNDECIDED, never a refutation: nothing was proved and nothing disproved,
    the clock ran out.

    Measured need: `serdiv_gate` ran 90 minutes saturating one core with RSS
    flat at 3,441,760 kB and zero bytes of output, while the compositions that
    did converge took 525-742 s. Nothing in this driver would ever have
    stopped it, so one non-converging design hangs an entire cohort.

    The kill goes to the process GROUP. `/usr/bin/time` forks `lake`, which
    forks `lean`; signalling the direct child leaves the real worker running
    and still charging memory. `start_new_session=True` gives the stage its
    own group for `killpg` to reach. SIGTERM first so Lean can unwind, then
    SIGKILL for whatever ignored it.
    """
    t0 = time.time()
    with log.open("w", encoding="utf-8") as fh:
        proc = subprocess.Popen(["/usr/bin/time", "-v"] + cmd, cwd=cwd,
                                stdout=fh, stderr=subprocess.STDOUT,
                                start_new_session=True)
        try:
            rc = proc.wait(timeout=timeout if timeout and timeout > 0 else None)
        except subprocess.TimeoutExpired:
            for sig in (signal.SIGTERM, signal.SIGKILL):
                try:
                    os.killpg(os.getpgid(proc.pid), sig)
                except (ProcessLookupError, PermissionError):
                    break
                try:
                    proc.wait(timeout=20)
                    break
                except subprocess.TimeoutExpired:
                    continue
            # Deliberately NO final unbounded `proc.wait()` here. If SIGKILL to
            # the group somehow did not reap it, blocking forever would restore
            # exactly the hang this timeout exists to prevent. Report the
            # timeout and move on; a survivor is visible in `ps`.
            rc = RC_TIMEOUT
    txt = log.read_text(encoding="utf-8", errors="replace")
    if rc == RC_TIMEOUT:
        txt += (f"\n[d3_module_proof] STAGE TIMED OUT after {timeout:.0f}s and was "
                f"killed by process group. UNDECIDED: nothing proved, nothing "
                f"refuted.\n")
        log.write_text(txt, encoding="utf-8")
    rss = next((int(l.split()[-1]) for l in txt.splitlines()
                if "Maximum resident" in l), 0)
    return rc, rss, time.time() - t0, txt


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cert", required=True)
    ap.add_argument("--module", required=True)
    ap.add_argument("--bindings", type=int, required=True)
    ap.add_argument("--chunk-size", type=int, default=32)
    ap.add_argument("--per-group", type=int, default=4)
    ap.add_argument("--group-max-kb", type=int, default=10000000)
    ap.add_argument("--final-max-kb", type=int, default=14000000)
    ap.add_argument("--phase-markers", action="store_true",
                    help="have the composition append a timestamped line per "
                         "proof phase to <out-dir>/phases.txt. DIAGNOSTIC only: "
                         "a marker records that a phase was REACHED, never that "
                         "it holds, and the file is written as the phases run "
                         "so it survives a kill that discards buffered stdout. "
                         "This is the only way to localise a composition that "
                         "produces no output before its bound -- serdiv_gate "
                         "ran 90 minutes and emitted nothing at all.")
    ap.add_argument("--timeout", type=float, default=0,
                    help="seconds to allow the COMPOSITION stage; 0 means "
                         "unbounded (the historical behaviour). On expiry the "
                         "row is status=timeout and module_proof=0, which is "
                         "UNDECIDED -- not a failed theorem. The group builds "
                         "are left unbounded: they are cheap and have never "
                         "been the stage that fails to converge.")
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args()

    cert = pathlib.Path(a.cert).resolve()
    out = pathlib.Path(a.out_dir).resolve()
    # Project-local by ENFORCEMENT, not by caller convention: artifacts and
    # scratch outside the project are not reachable from the repo afterwards,
    # and /tmp does not survive a host rebuild.
    if ROOT not in out.parents and out != ROOT:
        print(f"REFUSING: --out-dir {out} is outside the project at {ROOT}.",
              file=sys.stderr)
        return 2
    tmp = pathlib.Path(os.environ.get("TMPDIR", "")).resolve() if os.environ.get("TMPDIR") else None
    if tmp is None or (ROOT not in tmp.parents and tmp != ROOT):
        tmp = ROOT / "temp/d3_tmpdir"
        tmp.mkdir(parents=True, exist_ok=True)
        # TMPDIR is what Lean and lake honour; TMP and TEMP are honoured by
        # other tools that may run underneath, and a stray one would put
        # scratch outside the project.
        for var in ("TMPDIR", "TMP", "TEMP"):
            os.environ[var] = str(tmp)
        print(f"d3_module_proof: TMPDIR set project-local -> {tmp}")
    out.mkdir(parents=True, exist_ok=True)
    csha = hashlib.sha256(cert.read_bytes()).hexdigest()
    tsha = runner_digest()
    ns = "".join(ch for ch in a.module.title() if ch.isalnum())
    gen = LEAN / "LeanSemanticPrimitives/Gen" / ns

    # ---- digest enforcement, recomputed from disk ------------------------
    stamp = gen / "provenance.json"
    want = {"cert_sha256": csha, "tool_digest": tsha, "module": a.module,
            "bindings": a.bindings, "chunk_size": a.chunk_size,
            "per_group": a.per_group}
    stale = True
    if stamp.is_file():
        try:
            old = json.loads(stamp.read_text())
            # A DIFFERENT module normalising to the same namespace would
            # silently share `Gen/<ns>` and overwrite the other's facts.
            if old.get("module") not in (None, a.module):
                print(f"REFUSING: namespace {ns} is already stamped for module "
                      f"{old.get('module')!r}, not {a.module!r}. Two module "
                      f"names normalise to one namespace; rename or namespace "
                      f"explicitly rather than overwriting.", file=sys.stderr)
                return 2
            # Every field that changes what the modules MEAN, not just the two.
            stale = any(old.get(k) != v for k, v in want.items())
        except Exception:
            stale = True
    if stale and gen.exists():
        print(f"d3_module_proof: inputs changed or unstamped -- discarding {gen}")
        shutil.rmtree(gen)
        for d in (LEAN / ".lake/build/lib/lean/LeanSemanticPrimitives/Gen" / ns,):
            if d.exists():
                shutil.rmtree(d)

    # A design with no bindings still gets ONE chunk, of length zero.
    # `prove_reified_chunked` already treats `nb == 0` as one chunk, so a
    # generator that produced none left the composition importing `G-1`.
    # Measured: both zero-node designs in the first cohort failed exactly
    # there, with `object file ... Gen/<Ns>/G.olean does not exist`.
    nchunk = max(1, (a.bindings + a.chunk_size - 1) // a.chunk_size)
    ngroup = (nchunk + a.per_group - 1) // a.per_group
    subprocess.run([sys.executable, str(HERE / "d3_gen_modules.py"),
                    "--cert", str(cert), "--module", a.module,
                    "--bindings", str(a.bindings),
                    "--chunk-size", str(a.chunk_size),
                    "--per-group", str(a.per_group),
                    "--gen-digest", tsha], check=True)
    # The embedded genSha must be the digest this run stamped, or the modules
    # on disk and the run's provenance would describe different generators.
    emitted = (gen / "Cert.lean").read_text(encoding="utf-8")
    if f'_genSha  : String := "{tsha}"' not in emitted:
        print(f"REFUSING: {gen / 'Cert.lean'} does not embed the expected "
              f"generator digest {tsha[:16]}...", file=sys.stderr)
        return 2
    stamp.write_text(json.dumps(
        {**want, "cert": str(cert), "namespace": ns,
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
    # The marker file is named by an absolute path because the composition is
    # elaborated with cwd=LEAN, not from --out-dir.
    phases = out / "phases.txt"
    if a.phase_markers and phases.exists():
        phases.unlink()
    comp.write_text(
        f"import LeanSemanticPrimitives.Gen.{ns}.G{ngroup-1}\n"
        "import LeanSemanticPrimitives.Compiler.D3Harness\n"
        "set_option maxRecDepth 1000000\nset_option maxHeartbeats 0\n"
        + (f'set_option d3.proofPhase "{phases}"\n' if a.phase_markers else "")
        + "open Compiler\n\n"
        f"reify_design_chunked {a.module}_designCert as d3_fast size {a.chunk_size}\n"
        f"prove_reified_chunked {a.module}_designCert as d3_fast "
        f"size {a.chunk_size} using {ns}\n"
        "d3_proof_gate d3_fast.correct\n", encoding="utf-8")
    # Snapshot BEFORE and AFTER, and abort on drift. Recording it only at the
    # end would name whatever the oleans became, not what was imported.
    snap_before = olean_snapshot(ns)
    cgpath_file = out / "composition.cgroup"
    cmd = [sys.executable, gb, "--max-kb", str(a.final_max_kb),
           "--cgroup-path-file", str(cgpath_file), "--",
           "lake", "env", "lean", str(comp)]
    rc, rss, wall, txt = run(cmd, out / "composition.log", cwd=LEAN,
                             timeout=a.timeout)
    # The wrapper prints `peak=` on a normal exit. On a TIMEOUT the kill took
    # the whole process group, including the wrapper, so it never printed and
    # never ran its cleanup: the charge was reported as 0 and the cgroup was
    # left behind. Both are recoverable from the corpse, whose path the
    # wrapper wrote before starting.
    charge_kb = next((int(x.split("peak=")[1].split()[0].replace(",", ""))
                      for x in txt.splitlines()
                      if "d3_guarded_build:" in x and "peak=" in x), 0)
    charge_src = "wrapper"
    if not charge_kb and cgpath_file.exists():
        try:
            cgdir = pathlib.Path(cgpath_file.read_text().strip())
            charge_kb = int((cgdir / "memory.peak").read_text().strip()) // 1024
            charge_src = "salvaged-from-orphan-cgroup"
        except (OSError, ValueError):
            pass
    # Remove the cgroup the killed wrapper could not. Only ever this run's
    # own, named from the path it recorded, and only once it holds no process.
    if cgpath_file.exists():
        try:
            cgdir = pathlib.Path(cgpath_file.read_text().strip())
            if cgdir.is_dir() and not (cgdir / "cgroup.procs").read_text().strip():
                cgdir.rmdir()
        except OSError:
            pass
    snap_after = olean_snapshot(ns)
    drifted = snap_before != snap_after
    # A gate line is necessary and NOT sufficient: the command must also have
    # exited 0, the line must match exactly, and its axioms must lie inside the
    # allowed set. A substring test would credit a line inside an error message.
    gate, axioms_ok = "", False
    for l in txt.splitlines():
        m = GATE_RE.match(l.strip())
        if m:
            gate = l.strip()
            axioms_ok = set(x.strip() for x in m.group(2).split(",") if x.strip()) <= ALLOWED
            break
    # Drift is disqualifying however the composition itself exited: if the
    # oleans moved under it, the gate line describes an import that is no
    # longer the one on disk.
    proved = (rc == 0) and bool(gate) and axioms_ok and not drifted
    if drifted:
        print(f"REFUSING to credit: olean snapshot drifted during the "
              f"composition ({snap_before[:16]} -> {snap_after[:16]}). A shared "
              f".lake rebuild can change what an import means with every source "
              f"digest unchanged.", file=sys.stderr)
    # `timeout` is kept DISTINCT from `not_proved`. Both give module_proof=0,
    # so neither overclaims, but collapsing them loses the only thing a
    # reviewer needs to know: whether the gate was evaluated and disagreed, or
    # was never reached. 143 is SIGTERM, i.e. killed from outside.
    status = ("proved" if proved else
              "timeout" if rc == RC_TIMEOUT else
              "killed" if rc == 143 else
              "not_proved")
    res = {"status": status,
           "gate_axioms_within_allowed": axioms_ok,
           "module": a.module, "cert": str(cert), "cert_sha256": csha,
           "tool_digest": tsha, "bindings": a.bindings,
           "chunk_size": a.chunk_size, "chunks": nchunk, "groups": ngroup,
           "group_max_kb": a.group_max_kb, "final_max_kb": a.final_max_kb,
           "olean_digest": snap_before, "olean_digest_after": snap_after,
           "olean_drift": drifted,
           "timeout_s": a.timeout,
           "phase_markers": str(phases) if a.phase_markers else "",
           # Which phases were REACHED, in order, with their timestamps. A
           # marker is never evidence that a phase HOLDS -- it fires after the
           # command is elaborated, whatever the command concluded.
           "phases_reached": ([l.strip() for l in
                               phases.read_text(errors="replace").splitlines()
                               if l.strip()]
                              if a.phase_markers and phases.exists() else []),
           "final_rc": rc, "final_max_rss_kb": rss, "final_wall_s": round(wall, 2),
           "final_cgroup_peak_kb": charge_kb,
           "final_cgroup_peak_source": charge_src,
           "gate_line": gate, "group_builds": rows,
           "command": " ".join(sys.argv)}
    (out / "result.json").write_text(json.dumps(res, indent=2) + "\n")
    print(f"  FINAL rc={rc} max_rss={rss:,} kB wall={wall:.1f}s")
    print(f"  {gate or 'NO GATE LINE'}  ->  {res['status']}")
    print(f"  artifacts: {out}")
    return 0 if proved else 1


if __name__ == "__main__":
    raise SystemExit(main())

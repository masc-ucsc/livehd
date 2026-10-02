#!/usr/bin/env python3
"""Runner, resume and join tests for the D3 sweep.

These exercise what only matters when a run is long: whether the manifest is
actually enforced, whether a killed run keeps the rows it finished, whether a
resumed run can quietly mix configurations or corpora, and whether the results
join can report a denominator it did not measure.

Lean is STUBBED. The subject here is the runner's bookkeeping, and a real probe
costs minutes; the stub emits a well-formed gate log so these run in seconds and
stay deterministic. Parser behaviour on malformed logs is covered by
`d3_sweep_parser_test.py`, the checker itself by `probes/d3_harness_test.lean`,
and process teardown by `d3_sweep_cancel_test.py`.

Run: python3 pass/lean/scripts/d3_runner_test.py
"""

import csv
import hashlib
import json
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SWEEP = HERE / "d3_sweep.py"
JOIN = HERE / "d3_join.py"

# The runner as a MODULE, so schema constants are quoted from the source of
# truth rather than re-spelled here -- a test that hardcodes "time-max-rss"
# keeps passing after the runner renames it.  (`sweep` is the subprocess helper
# below, hence the distinct name.)
import importlib.util as _ilu
_spec = _ilu.spec_from_file_location("d3_sweep_mod", SWEEP)
sweep_mod = _ilu.module_from_spec(_spec)
_spec.loader.exec_module(sweep_mod)

CERT_BODY = """-- fixture
import LeanSemanticPrimitives.Compiler.CompileDesign
def {m}_designCert : DesignCert := {{ sources := #[], nodes := #[], outputs := #[], flops := #[], memories := #[] }}
{salt}
"""

# A CWD-SENSITIVE stand-in for lake. It answers `--version` differently
# depending on the directory it is invoked from, which is exactly how elan
# behaves: it picks a toolchain from the nearest `lean-toolchain`, and the
# repository root has none. A version probe run from the wrong directory
# therefore reports a toolchain that executed nothing.
STUB = r'''#!/usr/bin/env python3
import pathlib, re, sys, time, os
# `d3_sweep` resolves the lean binary and LEAN_PATH once, THROUGH lake, then
# execs that binary per probe.  The stub answers both resolution calls with
# ITSELF, so the direct path and the `lake env lean` path exercise the same
# fake compiler and can be compared against each other.
if sys.argv[1:3] == ["env", "which"] and "lean" in sys.argv:
    # Hand back a DISTINCT name rather than this path, so the resolved binary
    # identifies itself as lean.  Returning the same path made the stub answer
    # the version preflight as `lake`, and the preflight correctly refused the
    # run -- a real check firing on a fixture that was lying to it.
    me = pathlib.Path(os.path.realpath(__file__))
    # Named after THIS stub.  A shared `fake_lean` in the temp directory made the
    # drift stub resolve to the plain stub that an earlier case had created, so
    # the drifted run silently ran the non-drifting compiler and every drift
    # assertion failed for a reason that had nothing to do with drift.
    ln = me.with_name("lean_" + me.name)
    if not (ln.is_symlink() and os.path.realpath(ln) == str(me)):
        try:
            ln.unlink()
        except FileNotFoundError:
            pass
        ln.symlink_to(me)
    print(str(ln)); raise SystemExit(0)
if sys.argv[1:3] == ["env", "printenv"]:
    print(os.environ.get(sys.argv[3], "")); raise SystemExit(0)
if sys.argv[1:4] == ["env", "env", "-0"]:
    # The COMPLETE environment, as `lake env env -0` gives it: real Lake sets 17
    # variables and rewrites PATH and LD_LIBRARY_PATH, so the runner snapshots
    # all of it. `LEAN` is what the runner execs.
    me = pathlib.Path(os.path.realpath(__file__))
    ln = me.with_name("lean_" + me.name)
    if not (ln.is_symlink() and os.path.realpath(ln) == str(me)):
        try:
            ln.unlink()
        except FileNotFoundError:
            pass
        ln.symlink_to(me)
    snap = dict(os.environ)
    snap["LEAN"] = str(ln)
    snap["LEAN_PATH"] = snap.get("LEAN_PATH", "")
    snap["LEAN_SYSROOT"] = str(me.parent)
    sys.stdout.write("\0".join(f"{k}={v}" for k, v in snap.items()) + "\0")
    raise SystemExit(0)
AS_LEAN = "lean" in pathlib.Path(sys.argv[0]).name
if "--version" in sys.argv:
    here = pathlib.Path.cwd()
    where = "LEANDIR" if (here / "lean-toolchain").is_file() else "ROOT"
    what = "lean" if (AS_LEAN or "lean" in sys.argv) else "lake"
    print(f"{what}-version-from-{where}")
    raise SystemExit(0)
probe = pathlib.Path(sys.argv[-1])
delay = float(os.environ.get("STUB_DELAY", "0"))
if delay:
    time.sleep(delay)
m = probe.stem
mo = re.search(r'd3_residual (\d+)', probe.read_text())
samples = mo.group(1) if mo else "32"
print("reify_design: d3_fast emitted, 10 sources, 12 bindings")
print(f"D3GATE module={m} sources=10 nodes=12 outputs=2 flops=1 mems=1 inputs=3 bindings=12")
print("D3GATE sim_exec=1 obs=123")
print("D3GATE selftest_base=1 mut_out=rejected mut_flop=rejected mut_mem=rejected "
      "mutable_out=1 mutable_flop=1 mutable_mem=1")
print(f"D3GATE agree=1 samples={samples} distinct_obs=9")
'''


def mark_for(run_id: str) -> str:
    return str(ROOT / "temp" / "d3_sweep" / "runs" / run_id)


def residual(mark: str) -> list:
    found, me = [], os.getpid()
    for e in pathlib.Path("/proc").iterdir():
        if not e.name.isdigit() or int(e.name) == me:
            continue
        try:
            cl = (e / "cmdline").read_bytes().replace(b"\0", b" ").decode(errors="replace")
        except (OSError, ProcessLookupError):
            continue
        if mark in cl and "d3_runner_test" not in cl:
            found.append(int(e.name))
    return found


def sha256(p: pathlib.Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def make_certs(d: pathlib.Path, blocks, salt=""):
    d.mkdir(parents=True, exist_ok=True)
    return {b: _w(d / f"{b}_gate_Lgraph.lean", CERT_BODY.format(m=f"{b}_gate", salt=salt))
            for b in blocks}


def _w(p: pathlib.Path, text: str) -> pathlib.Path:
    p.write_text(text)
    return p


def make_manifest(path, certs, nodes=None, available=None, blocked_cause=None):
    nodes, available = nodes or {}, available or {}
    blocked_cause = blocked_cause or {}
    lines = ["# fixture manifest",
             "block\tcert_available\tcertificate\tsha256\tnodes\tblocked_cause"]
    for b, f in certs.items():
        av = available.get(b, 1)
        lines.append(f"{b}\t{av}\t{f.stem[:-len('_Lgraph')] if av else ''}\t"
                     f"{sha256(f) if av else ''}\t{nodes.get(b, 100)}\t"
                     f"{blocked_cause.get(b, '') if not av else ''}")
    path.write_text("\n".join(lines) + "\n")
    return path


def sweep(tmp, manifest, certs_dirs, out, extra=(), env_extra=None, timeout=300):
    env = dict(os.environ, LAKE=str(tmp / "fake_lake"))
    env.update(env_extra or {})
    dirs = [str(d) for d in (certs_dirs if isinstance(certs_dirs, (list, tuple))
                             else [certs_dirs])]
    return subprocess.run(
        # --allow-dirty: the fixtures run from a worktree that is dirty by
        # construction (the tests themselves are uncommitted while being
        # written). The guard that refuses a dirty CANONICAL run is exercised
        # directly by `dirty_worktree_refused` below.
        [sys.executable, str(SWEEP), "--certs", *dirs, "--manifest", str(manifest),
         "--out", str(out), "--jobs", "2", "--timeout", "60", "--allow-dirty", *extra],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout)


def rows_of(p: pathlib.Path):
    with p.open() as fh:
        return list(csv.DictReader((l for l in fh if not l.startswith("#")), delimiter="\t"))


def check(name, cond, note, failures, detail=""):
    if cond:
        print(f"ok   {name:<26} {note}")
    else:
        print(f"FAIL {name:<26} {note}")
        if detail:
            print(f"       {detail[:400]}")
        failures.append(name)


def main() -> int:
    f = []
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="d3_runner_test_", dir=str(ROOT / "temp")))
    try:
        stub = tmp / "fake_lake"
        stub.write_text(STUB)
        stub.chmod(0o755)

        blocks = [f"blk{i:02d}" for i in range(6)]
        cdir = tmp / "certs"
        certs = make_certs(cdir, blocks)
        nodes = {b: 500 * (i + 1) for i, b in enumerate(blocks)}
        man = make_manifest(tmp / "man.tsv", certs, nodes=nodes)
        out = tmp / "res.tsv"

        # 1. manifest-driven run, checkpoint, sidecar
        p = sweep(tmp, man, cdir, out)
        rs = rows_of(out)
        meta = json.loads((tmp / "res.tsv.meta.json").read_text())
        check("manifest_run", p.returncode == 0 and len(rs) == len(blocks)
              and all(r["verdict"] == "agree" for r in rs),
              f"{len(rs)} rows, all agree", f, p.stderr[-400:])
        check("key_and_module", rs[0]["target_key"] in blocks
              and rs[0]["module"].endswith("_gate"),
              f"target_key={rs[0]['target_key']} module={rs[0]['module']}", f)
        check("rss_captured", any(r.get("max_rss_kb") for r in rs),
              f"peak RSS recorded (e.g. {rs[0].get('max_rss_kb')} kB)", f)
        # Provenance without a cgroup: `max_rss_kb` is /usr/bin/time's and says
        # so, and `cgroup_peak_kb` stays EMPTY rather than defaulting to a zero
        # that would read as "the cgroup peaked at nothing".  Guarded on a
        # non-empty row set so it cannot pass vacuously.
        timed = [r for r in rs if r.get("max_rss_kb")]
        check("rss_provenance_without_cgroup",
              timed and all(r["max_rss_source"] == sweep_mod.SRC_TIME
                            and r["cgroup_peak_kb"] == "" for r in timed),
              f"{len(timed)} measured row(s) labelled {sweep_mod.SRC_TIME!r} with "
              f"cgroup_peak_kb blank", f,
              str({k: rs[0].get(k) for k in
                   ("max_rss_kb", "max_rss_source", "cgroup_peak_kb")}))

        # 2. refuse to clobber an existing output
        p = sweep(tmp, man, cdir, out)
        check("existing_output_refused", p.returncode != 0 and "REFUSING to overwrite" in p.stderr,
              "a second run without --resume/--force refuses", f, p.stderr[-200:])

        # 3. resume reuses terminal rows
        p = sweep(tmp, man, cdir, out, extra=["--resume"])
        check("resume_reuse", "reusing 6 terminal row(s)" in p.stderr and "0 to run" in p.stderr,
              "all 6 rows reused", f, p.stderr[-300:])

        # 4. resume refuses a changed configuration
        p = sweep(tmp, man, cdir, out, extra=["--resume", "--samples", "8"])
        check("resume_config_guard", p.returncode != 0 and "resume REFUSED" in p.stderr,
              "a changed sample count refuses", f, p.stderr[-200:])

        # 5. resume refuses a changed SELECTED set
        p = sweep(tmp, man, cdir, out, extra=["--resume", "--limit", "3"])
        check("resume_selection_guard",
              p.returncode != 0 and "selection_digest" in p.stderr,
              "a narrowed selection refuses to merge with a full run", f, p.stderr[-300:])

        # 6. a manifest edit to an UNAVAILABLE row still invalidates resume
        man_edit = make_manifest(tmp / "man_edit.tsv", certs, nodes=nodes)
        man_edit.write_text(man_edit.read_text().replace(
            "# fixture manifest", "# fixture manifest (annotated)"))
        p = sweep(tmp, man_edit, cdir, out, extra=["--resume"])
        check("resume_manifest_bytes",
              p.returncode != 0 and "manifest_digest" in p.stderr,
              "editing the manifest at all invalidates resume", f, p.stderr[-300:])

        # 7. corrupt sidecar
        side = tmp / "res.tsv.meta.json"
        keep = side.read_text()
        side.write_text(keep[: len(keep) // 2])
        p = sweep(tmp, man, cdir, out, extra=["--resume"])
        check("corrupt_sidecar", p.returncode != 0 and "unreadable" in p.stderr,
              "a truncated sidecar refuses rather than guessing", f, p.stderr[-200:])
        side.write_text(keep)

        # 8. duplicate rows in the previous output
        dup_out = tmp / "dup_res.tsv"
        shutil.copy(out, dup_out)
        shutil.copy(side, tmp / "dup_res.tsv.meta.json")
        lines = dup_out.read_text().rstrip("\n").split("\n")
        dup_out.write_text("\n".join(lines + [lines[-1]]) + "\n")
        # Re-bind: the sidecar now carries `results_sha256`, so an edited table is
        # refused on the BINDING before the duplicate-key check is ever reached.
        # Both checks matter and they are tested separately -- `resume_rebound_*`
        # below covers the binding; this case is about duplicate keys, so the
        # sidecar is updated to match the file it describes.
        _dm = json.loads((tmp / "dup_res.tsv.meta.json").read_text())
        _dm["results_sha256"] = hashlib.sha256(dup_out.read_bytes()).hexdigest()
        (tmp / "dup_res.tsv.meta.json").write_text(json.dumps(_dm, indent=2))
        p = sweep(tmp, man, cdir, dup_out, extra=["--resume"])
        # the binding, tested on its own: edit the table, leave the sidecar alone
        bind_out = tmp / "bind_res.tsv"
        shutil.copy(out, bind_out)
        shutil.copy(side, tmp / "bind_res.tsv.meta.json")
        _bm = json.loads((tmp / "bind_res.tsv.meta.json").read_text())
        _bm["results_sha256"] = hashlib.sha256(bind_out.read_bytes()).hexdigest()
        (tmp / "bind_res.tsv.meta.json").write_text(json.dumps(_bm, indent=2))
        pb_ok = sweep(tmp, man, cdir, bind_out, extra=["--resume"])
        check("resume_rebound_ok", pb_ok.returncode == 0,
              "a results table matching its sidecar binding resumes normally", f,
              pb_ok.stderr[-200:])
        # A REAL edit to a data row: edit the verdict field, not the header
        # (`agree` is also a column name) and not a no-op substitution.
        _bl = bind_out.read_text().splitlines()
        _hdr = next(i for i, l in enumerate(_bl) if not l.startswith("#"))
        _vi = _bl[_hdr].split("\t").index("verdict")
        _fl = _bl[_hdr + 1].split("\t")
        _fl[_vi] = "EDITED"
        _bl[_hdr + 1] = "\t".join(_fl)
        bind_out.write_text("\n".join(_bl) + "\n")
        pb = sweep(tmp, man, cdir, bind_out, extra=["--resume"])
        check("resume_rebound_tamper",
              pb.returncode != 0 and "changed after the run" in pb.stderr,
              "and an edited table with an untouched sidecar refuses", f,
              pb.stderr[-200:])

        # ---- resume requires the FULL schema, provenance columns included -----
        # An earlier revision exempted PROVENANCE_COLS so that a table written
        # before them could still resume. That exemption was unreachable:
        # TOOL_FILES includes d3_sweep.py, so a genuinely older table necessarily
        # carries a different tool_digest and is refused by the CONFIG comparison
        # before any column is read. Migrating a real old run is an offline job.
        # Reached here only by fabricating a state that cannot occur -- dropping
        # a column and re-binding the sidecar -- which is exactly why the
        # exemption had to go: this pins that no column is privileged.
        _ol = out.read_text().splitlines()
        _oh = next(i for i, l in enumerate(_ol) if not l.startswith("#"))
        for _col in ("cgroup_peak_kb", "proof_max_rss_kb", "cert_sha256"):
            _sx = tmp / f"drop_{_col}.tsv"
            _si = _ol[_oh].split("\t").index(_col)
            _sk = lambda line: "\t".join(
                v for j, v in enumerate(line.split("\t")) if j != _si)
            _sx.write_text("\n".join(
                l if l.startswith("#") else _sk(l) for l in _ol) + "\n")
            _sm = json.loads(side.read_text())
            _sm["results_sha256"] = hashlib.sha256(_sx.read_bytes()).hexdigest()
            (tmp / f"drop_{_col}.tsv.meta.json").write_text(json.dumps(_sm, indent=2))
            ps = sweep(tmp, man, cdir, _sx, extra=["--resume"])
            check(f"resume_missing_col_{_col}",
                  ps.returncode != 0 and "missing column" in ps.stderr
                  and _col in ps.stderr,
                  f"a resume missing {_col} is refused", f, ps.stderr[-300:])

        # ---- the GENERIC aborted_run marker is refused everywhere -------------
        # A void run's rows must not be extended by a resume or credited by a
        # join. Tested on the generic key rather than a specific reason, because
        # the point of the key is that a reason added later refuses by default.
        ab_out = tmp / "aborted_res.tsv"
        shutil.copy(out, ab_out)
        _am = json.loads(side.read_text())
        _am["config"]["aborted_run"] = True
        _am["config"]["aborted_reason"] = "fatal_cleanup"
        _am["config"]["aborted_at_target"] = blocks[0]
        _am["results_sha256"] = hashlib.sha256(ab_out.read_bytes()).hexdigest()
        (tmp / "aborted_res.tsv.meta.json").write_text(json.dumps(_am, indent=2))
        pa = sweep(tmp, man, cdir, ab_out, extra=["--resume"])
        check("resume_refuses_aborted_run",
              pa.returncode != 0 and "aborted_run" in pa.stderr,
              "resume refuses rows from a run marked aborted_run", f,
              pa.stderr[-300:])
        ja = subprocess.run([sys.executable, str(JOIN), "--manifest", str(man),
                             "--results", str(ab_out), "--out", str(tmp / "ab_join.tsv"),
                             "--meta", str(tmp / "aborted_res.tsv.meta.json")],
                            cwd=ROOT, capture_output=True, text=True, timeout=120)
        check("join_refuses_aborted_run",
              ja.returncode != 0 and "aborted_run" in ja.stderr,
              "and the join refuses to credit them", f, ja.stderr[-300:])

        # an UNBOUND sidecar (legacy, no results_sha256) must not resume a
        # manifest run: the next checkpoint would re-bind rows nothing vouches for
        unb_out = tmp / "unbound_res.tsv"
        shutil.copy(out, unb_out)
        _um = json.loads(side.read_text()); _um.pop("results_sha256", None)
        (tmp / "unbound_res.tsv.meta.json").write_text(json.dumps(_um, indent=2))
        pu = sweep(tmp, man, cdir, unb_out, extra=["--resume"])
        check("resume_refuses_unbound_table",
              pu.returncode != 0 and "no `results_sha256`" in pu.stderr,
              "a sidecar with no results binding refuses to resume a manifest run",
              f, pu.stderr[-250:])

        check("resume_duplicate_rows", p.returncode != 0 and "duplicate target_key" in p.stderr,
              "a duplicated row refuses the resume", f, p.stderr[-200:])

        # 9. a certificate whose bytes changed
        victim = blocks[0]
        certs[victim].write_text(certs[victim].read_text() + "\n-- regenerated\n")
        p = sweep(tmp, man, cdir, out, extra=["--resume"])
        check("hash_drift_refused", p.returncode != 0 and "hash drift" in p.stderr,
              "a regenerated certificate stops the run", f, p.stderr[-200:])
        certs[victim].write_text(CERT_BODY.format(m=f"{victim}_gate", salt=""))

        # 10. the same filename in two --certs directories
        cdir2 = tmp / "certs2"
        make_certs(cdir2, blocks[:1])
        p = sweep(tmp, man, [cdir, cdir2], tmp / "amb.tsv")
        check("ambiguous_cert_dirs",
              p.returncode != 0 and "two certificate directories" in p.stderr,
              "the same certificate in two corpora refuses", f, p.stderr[-200:])

        # 11. manifest refusals
        dup_man = tmp / "dupman.tsv"
        dup_man.write_text(man.read_text() + f"{blocks[0]}\t1\t{blocks[0]}_gate\t"
                                             f"{sha256(certs[blocks[0]])}\t100\t\n")
        p = sweep(tmp, dup_man, cdir, tmp / "d.tsv")
        check("manifest_duplicate", p.returncode != 0 and "duplicate targets" in p.stderr,
              "a repeated target refuses", f)

        bad_av = tmp / "badav.tsv"
        bad_av.write_text(man.read_text().replace(f"{blocks[1]}\t1\t", f"{blocks[1]}\tyes\t"))
        p = sweep(tmp, bad_av, cdir, tmp / "b.tsv")
        check("manifest_bad_available", p.returncode != 0 and "is not 0 or 1" in p.stderr,
              "cert_available must be exactly 0 or 1", f)

        # 12. tier selection, and tier output excludes no-certificate rows
        tier_man = make_manifest(tmp / "man_tier.tsv", certs, nodes=nodes,
                                 available={blocks[5]: 0},
                                 blocked_cause={blocks[5]: "emission failed"})
        tier_out = tmp / "tier.tsv"
        p = sweep(tmp, tier_man, cdir, tier_out, extra=["--tier", "tiny"])
        tr = rows_of(tier_out)
        check("tier_excludes_nocert",
              p.returncode == 0 and all(r["target_key"] != blocks[5] for r in tr)
              and all(0 < int(r["manifest_nodes"]) <= 1000 for r in tr if r.get("manifest_nodes")),
              f"tier=tiny selected {len(tr)} row(s), no no-certificate row present", f,
              p.stderr[-300:])

        # 13. --one-per-tier picks the LARGEST in each tier
        opt_out = tmp / "opt.tsv"
        p = sweep(tmp, make_manifest(tmp / "man_opt.tsv", certs, nodes=nodes), cdir,
                  opt_out, extra=["--one-per-tier"])
        orows = rows_of(opt_out)
        tiny = [r for r in orows if r["tier"] == "tiny"]
        check("one_per_tier_largest",
              p.returncode == 0 and len(tiny) == 1 and tiny[0]["manifest_nodes"] == "1000",
              f"tiny representative is the largest "
              f"({tiny[0]['manifest_nodes'] if tiny else '?'} nodes, not 500)", f,
              p.stderr[-300:])
        check("tier_from_manifest",
              bool(tiny) and tiny[0]["nodes"] != tiny[0]["manifest_nodes"],
              f"tier uses the manifest size ({tiny[0]['manifest_nodes'] if tiny else '?'}), "
              f"not the probe log's ({tiny[0]['nodes'] if tiny else '?'})", f)

        # 14. a killed run keeps its finished rows, and resume completes it
        kill_out = tmp / "kill.tsv"
        env = dict(os.environ, LAKE=str(stub), STUB_DELAY="2")
        proc = subprocess.Popen(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(kill_out), "--jobs", "1", "--timeout", "60", "--allow-dirty"],
            cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + 60
        while time.time() < deadline:
            if kill_out.is_file() and len(rows_of(kill_out)) >= 2:
                break
            if proc.poll() is not None:
                break
            time.sleep(0.5)
        partial = len(rows_of(kill_out)) if kill_out.is_file() else 0
        proc.send_signal(signal.SIGKILL)
        proc.wait(timeout=30)
        time.sleep(1)
        after_kill = rows_of(kill_out) if kill_out.is_file() else []
        p = sweep(tmp, man, cdir, kill_out, extra=["--resume"], env_extra={"STUB_DELAY": "0"})
        final = rows_of(kill_out)
        check("checkpoint_survives_kill",
              partial >= 2 and len(after_kill) == partial,
              f"{partial} row(s) survived SIGKILL of the driver", f)
        check("resume_after_kill",
              p.returncode == 0 and len(final) == len(blocks)
              and all(r["verdict"] == "agree" for r in final),
              f"resume completed the run to {len(final)} rows", f, p.stderr[-300:])

        # 13b. the toolchain the sidecar records must be the one the probes use
        ver_out = tmp / "ver.tsv"
        sweep(tmp, man, cdir, ver_out)
        vcfg = json.loads((tmp / "ver.tsv.meta.json").read_text())["config"]
        check("toolchain_from_leandir",
              vcfg.get("lake_version") == "lake-version-from-LEANDIR"
              and vcfg.get("lean_version") == "lean-version-from-LEANDIR",
              f"versions probed where the probes run, not from the repo root "
              f"(lake={vcfg.get('lake_version')!r} lean={vcfg.get('lean_version')!r})", f,
              json.dumps(vcfg, indent=2)[:400])
        check("toolchain_recorded",
              bool(vcfg.get("lean_toolchain")) and vcfg["lean_toolchain"] != "unknown",
              f"lean_toolchain={vcfg.get('lean_toolchain')!r}", f)

        # 13c. a failed version probe refuses a canonical run outright
        badver = tmp / "fake_lake_badver"
        badver.write_text(STUB.replace('print(f"{what}-version-from-{where}")\n    raise SystemExit(0)',
                                       'raise SystemExit(3)'))
        badver.chmod(0o755)
        bp = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(tmp / "badver.tsv"), "--jobs", "1", "--timeout", "60",
             "--allow-dirty"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(badver)),
            capture_output=True, text=True, timeout=180)
        check("version_probe_failure_refused",
              bp.returncode != 0 and "toolchain not identified" in bp.stderr,
              "a nonzero version probe refuses, and --allow-dirty does not waive it", f,
              bp.stderr[-250:])

        # 13d. a shared/external Lean build root refuses a canonical run
        #
        # This is the failure that voided the first pilot: `.lake` was a symlink
        # into another worktree, which rebuilt CompileDesign.olean fourteen
        # minutes into the run. Source-level digests saw nothing.
        import importlib.util as _ilu
        _spec = _ilu.spec_from_file_location("d3_sweep_mod", SWEEP)
        d3mod = _ilu.module_from_spec(_spec)
        _spec.loader.exec_module(d3mod)

        real_lake = d3mod.LEAN_DIR / ".lake"
        ext, why = d3mod.build_root_is_external()
        check("build_root_local", not ext,
              f"this worktree's build root is local: {d3mod.build_root()}", f, why)

        # simulate the shared layout: move .lake aside and symlink it elsewhere
        shadow = tmp / "shared_lake"
        shadow.mkdir()
        moved = False
        try:
            real_lake.rename(tmp / "real_lake")
            real_lake.symlink_to(shadow)
            moved = True
            ext2, why2 = d3mod.build_root_is_external()
            dp2 = subprocess.run(
                [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
                 "--out", str(tmp / "shared.tsv"), "--jobs", "1", "--timeout", "60",
                 "--allow-dirty"],
                cwd=ROOT, env=dict(os.environ, LAKE=str(stub)),
                capture_output=True, text=True, timeout=180)
            check("external_build_root_refused",
                  ext2 and dp2.returncode != 0 and "build root is shared" in dp2.stderr,
                  "a symlinked build root refuses a canonical run", f, dp2.stderr[-250:])
        finally:
            if moved:
                real_lake.unlink()
                (tmp / "real_lake").rename(real_lake)
        check("build_root_restored", not d3mod.build_root_is_external()[0],
              "the real build root was restored after the test", f)

        # 13e. artifact digest moves when a loaded .olean changes
        base = (d3mod.build_root() / "build" / "lib" / "lean"
                / "LeanSemanticPrimitives" / "Compiler")
        victim_olean = base / "D3Harness.olean"
        before_dig = d3mod.artifact_digest()
        keep_bytes = victim_olean.read_bytes()
        try:
            victim_olean.write_bytes(keep_bytes + b"\x00")
            after_dig = d3mod.artifact_digest()
        finally:
            victim_olean.write_bytes(keep_bytes)
        check("artifact_digest_drift", before_dig != after_dig,
              f"touching a loaded .olean moves the digest "
              f"({before_dig[:12]} -> {after_dig[:12]})", f)
        check("artifact_digest_stable", d3mod.artifact_digest() == before_dig,
              "and restoring the bytes restores the digest", f)

        # 13f. ARTIFACT DRIFT — the failure that voided the first pilot
        #
        # A fake artifact directory (D3_ARTIFACT_DIR) stands in for the compiled
        # .olean closure, so the test can move it mid-run without touching the
        # real build output. The fake lake mutates one object while a chosen
        # module is elaborating.
        CRIT = d3mod.CRITICAL_OLEANS

        def fake_artifacts(d: pathlib.Path):
            d.mkdir(parents=True, exist_ok=True)
            for m in CRIT:
                (d / f"{m}.olean").write_bytes(b"v1-" + m.encode())
            return d

        DRIFT_STUB = STUB.replace(
            'probe = pathlib.Path(sys.argv[-1])',
            'probe = pathlib.Path(sys.argv[-1])\n'
            'ad = os.environ.get("D3_ARTIFACT_DIR")\n'
            'trig = os.environ.get("DRIFT_ON", "")\n'
            'if ad and trig and probe.stem == trig:\n'
            '    import pathlib as _p\n'
            '    f = _p.Path(ad) / "CompileDesign.olean"\n'
            '    f.write_bytes(f.read_bytes() + b"-mutated")\n')

        drift_lake = tmp / "fake_lake_drift"
        drift_lake.write_text(DRIFT_STUB)
        drift_lake.chmod(0o755)

        adir = fake_artifacts(tmp / "artifacts")
        drift_out = tmp / "drift.tsv"
        dres = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(drift_out), "--jobs", "3", "--timeout", "60", "--allow-dirty", "--runner-selftest"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(drift_lake),
                               D3_ARTIFACT_DIR=str(adir), DRIFT_ON=f"{blocks[2]}_gate",
                               STUB_DELAY="1"),
            capture_output=True, text=True, timeout=300)

        drows = rows_of(drift_out) if drift_out.is_file() else []
        dmeta = json.loads((tmp / "drift.tsv.meta.json").read_text())
        check("drift_nonzero_exit", dres.returncode != 0,
              f"a drifted run exits nonzero (rc={dres.returncode})", f, dres.stderr[-250:])
        check("drift_sidecar_marked", dmeta["config"].get("aborted_artifact_drift") is True,
              f"sidecar records aborted_artifact_drift at "
              f"{dmeta['config'].get('aborted_at_target')!r} "
              f"phase {dmeta['config'].get('aborted_phase')!r}", f)
        check("drift_no_tainted_row",
              all(r.get("drift", "") == "" for r in drows)
              and all(r.get("run_status") != "aborted_artifact_drift" for r in drows),
              f"the tainted row was not written ({len(drows)} clean row(s) preserved)", f,
              str([(r['target_key'], r.get('run_status')) for r in drows]))
        leaked = residual(mark_for(dmeta["run_id"])) if "run_id" in dmeta else []
        check("drift_workers_torn_down", not leaked,
              "every worker process group was terminated on abort", f, str(leaked))

        # resume must refuse the aborted sidecar outright
        # same LAKE and artifact dir, so the only thing resume can object to is
        # the abort marker itself
        rp = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(drift_out), "--jobs", "1", "--timeout", "60",
             "--allow-dirty", "--runner-selftest", "--resume"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(drift_lake),
                               D3_ARTIFACT_DIR=str(adir), DRIFT_ON=""),
            capture_output=True, text=True, timeout=180)
        check("drift_resume_refused",
              rp.returncode != 0 and "aborted_artifact_drift" in rp.stderr,
              "resume refuses rows from a drifted run", f, rp.stderr[-250:])

        # and so must the join. The drift sidecar also carries the self-test
        # brand, which the join refuses first, so the ABORT guard is tested on a
        # copy with the brand cleared -- otherwise this would pass on the wrong
        # refusal and the drift check could rot unnoticed.
        drift_meta = json.loads((tmp / "drift.tsv.meta.json").read_text())
        drift_meta["config"]["runner_selftest"] = False
        unbranded = tmp / "drift_unbranded.meta.json"
        unbranded.write_text(json.dumps(drift_meta, indent=2))
        jp = subprocess.run([sys.executable, str(JOIN), "--manifest", str(man),
                             "--results", str(drift_out), "--out", str(tmp / "drift_join.tsv"),
                             "--meta", str(unbranded)],
                            cwd=ROOT, capture_output=True, text=True, timeout=120)
        check("drift_join_refused",
              jp.returncode != 0 and "aborted_artifact_drift" in jp.stderr,
              "the join refuses a drifted sweep on the drift marker itself", f,
              jp.stderr[-250:])

        # 13g. --native must be subject to the same check. It returned from
        # run_one before the post-run digest test, so a drifted native run could
        # end on a row that looked ordinary.
        fake_artifacts(adir)
        nat_out = tmp / "drift_native.tsv"
        np_ = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(nat_out), "--jobs", "2", "--timeout", "60", "--allow-dirty", "--runner-selftest",
             "--native"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(drift_lake), D3_ARTIFACT_DIR=str(adir),
                               DRIFT_ON=f"{blocks[1]}_gate", STUB_DELAY="1"),
            capture_output=True, text=True, timeout=300)
        nmeta = json.loads((tmp / "drift_native.tsv.meta.json").read_text())
        nrows = rows_of(nat_out) if nat_out.is_file() else []
        check("drift_native_aborts",
              np_.returncode != 0 and nmeta["config"].get("aborted_artifact_drift") is True
              and all(r.get("drift", "") == "" for r in nrows),
              f"a drifted --native run aborts (rc={np_.returncode}) with no tainted row", f,
              np_.stderr[-250:])

        # 13h. an exception raised after the probe must not escape the check
        # either: main synthesizes a `runner_error` row for it.
        fake_artifacts(adir)
        exc_out = tmp / "drift_exc.tsv"
        ep = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(exc_out), "--jobs", "2", "--timeout", "60", "--allow-dirty",
             "--runner-selftest"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(drift_lake), D3_ARTIFACT_DIR=str(adir),
                               DRIFT_ON=f"{blocks[1]}_gate",
                               D3_TEST_RAISE_ON=f"{blocks[3]}_gate", STUB_DELAY="1"),
            capture_output=True, text=True, timeout=300)
        emeta = json.loads((tmp / "drift_exc.tsv.meta.json").read_text())
        erows = rows_of(exc_out) if exc_out.is_file() else []
        check("drift_with_exception_aborts",
              ep.returncode != 0 and emeta["config"].get("aborted_artifact_drift") is True
              and all(r.get("drift", "") == "" for r in erows),
              f"drift concurrent with a worker exception still aborts "
              f"(rc={ep.returncode})", f, ep.stderr[-250:])

        # and without drift, a worker exception alone is an ordinary terminal row
        fake_artifacts(adir)
        only_exc = tmp / "only_exc.tsv"
        op_ = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(only_exc), "--jobs", "2", "--timeout", "60", "--allow-dirty",
             "--runner-selftest"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(stub), D3_ARTIFACT_DIR=str(adir),
                               D3_TEST_RAISE_ON=f"{blocks[3]}_gate"),
            capture_output=True, text=True, timeout=300)
        orows = rows_of(only_exc) if only_exc.is_file() else []
        err_row = next((r for r in orows if r["target_key"] == blocks[3]), None)
        check("exception_without_drift_is_a_row",
              op_.returncode == 0 and err_row is not None
              and err_row["verdict"] == "runner_error" and len(orows) == len(blocks),
              f"a worker exception alone is one terminal row, run completes "
              f"({len(orows)} rows)", f, op_.stderr[-250:])

        # 13i. the seam must not be honourable without the flag, and must never
        # apply to a canonical run: it changes what is HASHED, not what Lean
        # LOADS, so an inherited variable would validate the wrong files.
        fake_artifacts(adir)
        inh = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(tmp / "inherit.tsv"), "--jobs", "1", "--timeout", "60",
             "--allow-dirty"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(stub), D3_ARTIFACT_DIR=str(adir)),
            capture_output=True, text=True, timeout=180)
        check("artifact_seam_needs_flag",
              inh.returncode != 0 and "--runner-selftest was not passed" in inh.stderr,
              "an inherited D3_ARTIFACT_DIR refuses rather than being honoured", f,
              inh.stderr[-250:])

        sel_out = tmp / "selftest_manifest.tsv"
        sel = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(sel_out), "--jobs", "1", "--timeout", "60",
             "--allow-dirty", "--runner-selftest"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(stub), D3_ARTIFACT_DIR=str(adir)),
            capture_output=True, text=True, timeout=180)
        smeta = json.loads((tmp / "selftest_manifest.tsv.meta.json").read_text())
        check("selftest_branded", smeta["config"].get("runner_selftest") is True,
              "a self-test run brands its own sidecar", f, sel.stderr[-200:])
        sj = subprocess.run([sys.executable, str(JOIN), "--manifest", str(man),
                             "--results", str(sel_out), "--out", str(tmp / "sj.tsv"),
                             "--meta", str(tmp / "selftest_manifest.tsv.meta.json")],
                            cwd=ROOT, capture_output=True, text=True, timeout=120)
        check("selftest_join_refused",
              sj.returncode != 0 and "runner-selftest" in sj.stderr,
              "the join refuses a self-test sweep", f, sj.stderr[-250:])

        # a missing critical artifact refuses a canonical run up front
        (adir / "CompileDesign.olean").unlink()
        mp = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(tmp / "missing.tsv"), "--jobs", "1", "--timeout", "60",
             "--allow-dirty", "--runner-selftest"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(stub), D3_ARTIFACT_DIR=str(adir)),
            capture_output=True, text=True, timeout=180)
        check("missing_olean_refused",
              mp.returncode != 0 and "artifact(s) absent" in mp.stderr,
              "an absent critical .olean refuses a canonical run", f, mp.stderr[-200:])

        # 14a. a canonical run from a dirty worktree refuses without --allow-dirty
        dirty_out = tmp / "dirty.tsv"
        env = dict(os.environ, LAKE=str(stub))
        dp = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(man),
             "--out", str(dirty_out), "--jobs", "1", "--timeout", "60"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
        is_dirty = bool(subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain"],
                                       capture_output=True, text=True).stdout.strip())
        check("dirty_worktree_refused",
              (dp.returncode != 0 and "dirty worktree" in dp.stderr) if is_dirty
              else dp.returncode == 0,
              "a manifest run refuses a dirty worktree without --allow-dirty"
              if is_dirty else "worktree is clean; guard not applicable", f,
              dp.stderr[-200:])

        # 14b. exactly one of output / sidecar refuses
        lone = tmp / "lone.tsv"
        shutil.copy(out, lone)
        p = sweep(tmp, man, cdir, lone, extra=["--resume"])
        check("resume_lone_output", p.returncode != 0 and "without the other" in p.stderr,
              "an output with no sidecar refuses to resume", f, p.stderr[-200:])
        lone.unlink()
        shutil.copy(side, tmp / "lone.tsv.meta.json")
        p = sweep(tmp, man, cdir, lone, extra=["--resume"])
        check("resume_lone_sidecar", p.returncode != 0 and "without the other" in p.stderr,
              "a sidecar with no output refuses to resume", f, p.stderr[-200:])

        # 14c. a truncated previous output refuses
        trunc = tmp / "trunc.tsv"
        shutil.copy(out, trunc)
        shutil.copy(side, tmp / "trunc.tsv.meta.json")
        tl = trunc.read_text().split("\n")
        trunc.write_text("\n".join(tl[:2] + ["blkXX\tonly\ttwo\tfields"]) + "\n")
        p = sweep(tmp, man, cdir, trunc, extra=["--resume"])
        check("resume_truncated_rows", p.returncode != 0 and "REFUSED" in p.stderr,
              "a truncated row refuses rather than being partly believed", f, p.stderr[-250:])

        # 15. join at the manifest denominator.
        # The sweep and the join must use the SAME manifest -- the digest check
        # refuses otherwise, which is the point -- so run tier_man in full here
        # rather than joining a different manifest's results.
        tfull = tmp / "tier_full.tsv"
        sweep(tmp, tier_man, cdir, tfull)
        join_out = tmp / "join.tsv"
        jp = subprocess.run([sys.executable, str(JOIN), "--manifest", str(tier_man),
                             "--results", str(tfull), "--out", str(join_out),
                             "--meta", str(tmp / "tier_full.tsv.meta.json")],
                            cwd=ROOT, capture_output=True, text=True, timeout=120)
        if not join_out.is_file():
            print(f"FAIL join_denominator      join produced no output\n"
                  f"       rc={jp.returncode} stderr={jp.stderr[-400:]}")
            f.append("join_denominator")
            jr = []
        else:
            jr = rows_of(join_out)
        brow = next((r for r in jr if r["target"] == blocks[5]), None) if jr else None
        check("join_denominator", len(jr) == len(blocks)
              and brow is not None and brow["agree"] == "0"
              and brow["verdict"] == "no_certificate",
              f"{len(jr)} rows for {len(blocks)} targets; the no-certificate row "
              f"cannot be credited", f, jp.stdout[-300:])

        small = make_manifest(tmp / "man_small.tsv", {b: certs[b] for b in blocks[:3]},
                              nodes=nodes)
        jp = subprocess.run([sys.executable, str(JOIN), "--manifest", str(small),
                             "--results", str(out), "--out", str(tmp / "join2.tsv")],
                            cwd=ROOT, capture_output=True, text=True, timeout=120)
        check("join_orphan", jp.returncode != 0 and "no target claims" in jp.stderr,
              "result rows outside the manifest are refused", f, jp.stderr[-200:])

        # join: a manifest with the SAME NAME but different bytes must be refused
        samename = tmp / "sn" / tier_man.name
        samename.parent.mkdir(exist_ok=True)
        samename.write_text(tier_man.read_text().replace("\t100\t", "\t999\t"))
        jp = subprocess.run([sys.executable, str(JOIN), "--manifest", str(samename),
                             "--results", str(out), "--out", str(tmp / "join_sn.tsv"),
                             "--meta", str(side)],
                            cwd=ROOT, capture_output=True, text=True, timeout=120)
        check("join_manifest_bytes",
              jp.returncode != 0 and "different manifest" in jp.stderr,
              "same filename, different bytes is refused by digest", f, jp.stderr[-200:])

        # join: a result row with no certificate hash cannot be credited
        nohash = tmp / "nohash.tsv"
        lines = out.read_text().rstrip("\n").split("\n")
        hdr = lines[0].split("\t")
        ci = hdr.index("cert_sha256")
        fixed = [lines[0]]
        for ln in lines[1:]:
            cells = ln.split("\t")
            cells[ci] = ""
            fixed.append("\t".join(cells))
        nohash.write_text("\n".join(fixed) + "\n")
        subprocess.run([sys.executable, str(JOIN), "--manifest", str(man),
                        "--results", str(nohash), "--out", str(tmp / "join_nh.tsv")],
                       cwd=ROOT, capture_output=True, text=True, timeout=120)
        nh = rows_of(tmp / "join_nh.tsv")
        check("join_missing_hash",
              bool(nh) and all(r["verdict"] == "unauthenticated" and r["agree"] == "0"
                               for r in nh),
              "a result with no certificate hash is unauthenticated, not credited", f,
              str(nh[:1]))

        # join: cert_available must be exactly 0 or 1
        badav = tmp / "man_badav.tsv"
        badav.write_text(man.read_text().replace(f"{blocks[1]}\t1\t", f"{blocks[1]}\tmaybe\t"))
        jp = subprocess.run([sys.executable, str(JOIN), "--manifest", str(badav),
                             "--results", str(out), "--out", str(tmp / "join_bad.tsv")],
                            cwd=ROOT, capture_output=True, text=True, timeout=120)
        check("join_bad_available", jp.returncode != 0 and "is not 0 or 1" in jp.stderr,
              "a non-boolean cert_available refuses the join", f, jp.stderr[-200:])

        stale = tmp / "man_stale.tsv"
        stale.write_text(man.read_text().replace(sha256(certs[blocks[2]]), "0" * 64))
        subprocess.run([sys.executable, str(JOIN), "--manifest", str(stale),
                        "--results", str(out), "--out", str(tmp / "join3.tsv")],
                       cwd=ROOT, capture_output=True, text=True, timeout=120)
        srow = next((r for r in rows_of(tmp / "join3.tsv") if r["target"] == blocks[2]), None)
        check("join_hash_mismatch",
              srow is not None and srow["verdict"] == "hash_mismatch" and srow["agree"] == "0",
              "a stale result is recorded and uncreditable", f, str(srow))

    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("\nRUNNER-TEST", "OK" if not f else f"FAILED: {f}")
    return 0 if not f else 1


if __name__ == "__main__":
    sys.exit(main())

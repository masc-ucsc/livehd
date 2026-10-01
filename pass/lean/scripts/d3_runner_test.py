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

CERT_BODY = """-- fixture
import LeanSemanticPrimitives.Compiler.CompileDesign
def {m}_designCert : DesignCert := {{ sources := #[], nodes := #[], outputs := #[], flops := #[], memories := #[] }}
{salt}
"""

STUB = r'''#!/usr/bin/env python3
import pathlib, re, sys, time, os
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
        p = sweep(tmp, man, cdir, dup_out, extra=["--resume"])
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

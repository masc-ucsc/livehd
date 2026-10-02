#!/usr/bin/env python3
"""Tests for cgroup v2 enforcement, using tiny synthetic allocators.

The sampled caps are advisory: they bound sustained memory, not instantaneous
peaks. Measured on real runs at a 5 s interval against a 19,000,000 kB limit,
one probe peaked 161,996 kB OVER and was never killed while another was caught
20,996 kB over -- same limit, opposite outcomes, decided by sampling phase.
cgroup `memory.max` is enforced by the kernel continuously and `memory.peak`
reports a true peak instead of a sampled lower bound.

Everything here allocates at most a few hundred MB and runs in seconds. No Lean,
no certificate, no design. All temporary files live under the project's own
`temp/`, never `/tmp`.

Where delegation is unavailable the mechanism tests SKIP rather than fail -- that
is a property of the host, not a defect -- but the refusal tests still run,
because refusing to pretend is the behaviour that matters most there.

Run: python3 pass/lean/scripts/d3_cgroup_test.py
"""

import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SWEEP = HERE / "d3_sweep.py"

spec = importlib.util.spec_from_file_location("d3_sweep", SWEEP)
sweep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sweep)

spec2 = importlib.util.spec_from_file_location("d3_runner_test", HERE / "d3_runner_test.py")
rt = importlib.util.module_from_spec(spec2)
spec2.loader.exec_module(rt)

# A "lean" that allocates ALLOC_MB and touches it, so the pages are resident.
# It also emits a well-formed gate report, so a run that is NOT killed produces
# an ordinary row and the two outcomes stay distinguishable.
ALLOC_STUB = r'''#!/usr/bin/env python3
import os, pathlib, re, sys
if sys.argv[1:3] == ["env", "which"] and "lean" in sys.argv:
    me = pathlib.Path(os.path.realpath(__file__))
    ln = me.with_name("lean_" + me.name)
    if not (ln.is_symlink() and os.path.realpath(ln) == str(me)):
        try: ln.unlink()
        except FileNotFoundError: pass
        ln.symlink_to(me)
    print(str(ln)); raise SystemExit(0)
if sys.argv[1:4] == ["env", "env", "-0"]:
    me = pathlib.Path(os.path.realpath(__file__))
    ln = me.with_name("lean_" + me.name)
    if not (ln.is_symlink() and os.path.realpath(ln) == str(me)):
        try: ln.unlink()
        except FileNotFoundError: pass
        ln.symlink_to(me)
    snap = dict(os.environ); snap["LEAN"] = str(ln)
    sys.stdout.write("\0".join(f"{k}={v}" for k, v in snap.items()) + "\0")
    raise SystemExit(0)
AS_LEAN = "lean" in pathlib.Path(sys.argv[0]).name
if "--version" in sys.argv:
    print(("lean" if AS_LEAN or "lean" in sys.argv else "lake") + "-version-stub")
    raise SystemExit(0)
mode = os.environ.get("STUB_MODE", "")
if mode == "selfkill137":
    # Exit 137 WITHOUT an OOM: any SIGKILL yields 137, so 137 alone must never be
    # read as a memory kill.
    os.kill(os.getpid(), 9)
if mode == "sleep":
    import time as _t; _t.sleep(float(os.environ.get("SLEEP_S", "30")))
mb = int(os.environ.get("ALLOC_MB", "0"))
if mb:
    buf = bytearray(mb * 1024 * 1024)
    for i in range(0, len(buf), 4096):
        buf[i] = 1
probe = pathlib.Path(sys.argv[-1])
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


def main() -> int:
    fails, skipped = [], []
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="d3_cgroup_test_", dir=str(ROOT / "temp")))
    t_start = time.time()

    def check(name, cond, note, detail=""):
        if cond:
            print(f"ok   {name:<34} {note}")
        else:
            print(f"FAIL {name:<34} {note}")
            if detail:
                print(f"     {detail[:600]}")
            fails.append(name)

    def skip(name, why):
        print(f"skip {name:<34} {why}")
        skipped.append(name)

    try:
        base = sweep.cgroup_base()
        print(f"  cgroup_base() -> {base or 'UNAVAILABLE'}\n")

        # ---- refusals, which must hold whether or not delegation exists ------
        stub = tmp / "fake_lake"
        stub.write_text(ALLOC_STUB)
        stub.chmod(0o755)
        cdir = tmp / "certs"
        certs = rt.make_certs(cdir, ["aaa"])

        def run(extra, env_extra=None, out="o.tsv", timeout=180):
            # A DISTINCT output per case: the runner refuses to overwrite an
            # existing results table, which is correct behaviour and was failing
            # these cases rather than the mechanism under test.
            env = dict(os.environ, LAKE=str(stub), TMPDIR=str(tmp))
            env.update(env_extra or {})
            return subprocess.run(
                [sys.executable, str(SWEEP), "--certs", str(cdir),
                 "--out", str(tmp / out), "--jobs", "1", "--timeout", "120", *extra],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout)

        # A driver whose own detection is forced to find nothing. Needed because
        # the unavailable branch must be exercised from an AVAILABLE host, and
        # patching only this process would leave the subprocess still seeing a
        # usable base -- the two would then disagree for a reason that is an
        # artefact of the test rather than a property of the code.
        FORCE = ("import importlib.util,sys\n"
                 "s=importlib.util.spec_from_file_location('sw',%r)\n"
                 "m=importlib.util.module_from_spec(s); s.loader.exec_module(m)\n"
                 "m.cgroup_base=lambda: None\n"
                 "sys.argv=['d3_sweep.py']+sys.argv[1:]\n"
                 "sys.exit(m.main())\n") % str(SWEEP)
        forced = tmp / "force_unavail.py"
        forced.write_text(FORCE)

        def run_forced(extra, out, env_extra=None):
            env = dict(os.environ, LAKE=str(stub), TMPDIR=str(tmp))
            env.update(env_extra or {})
            return subprocess.run(
                [sys.executable, str(forced), "--certs", str(cdir),
                 "--out", str(tmp / out), "--jobs", "1", "--timeout", "120", *extra],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)

        r = run(["--enforce", "cgroup"], out="refuse1.tsv")
        check("refuse_cgroup_without_limit",
              r.returncode == 2 and "needs --kill-over-rss-kb" in r.stderr,
              "--enforce cgroup without --kill-over-rss-kb is refused: a cgroup with "
              "no memory.max imposes no limit", r.stderr[-300:])

        if base is None:
            r = run_forced(["--enforce", "cgroup", "--kill-over-rss-kb", "100000"],
                           "refuse2.tsv")
            check("refuse_when_unavailable",
                  r.returncode == 2 and "false guarantee" in r.stderr,
                  "where delegation is absent, --enforce cgroup REFUSES rather than "
                  "silently running under the weaker sampled limit", r.stderr[-300:])
            for n in ("cgroup_enforces_limit", "cgroup_peak_is_exact",
                      "cgroup_cleanup", "cgroup_under_limit_runs",
                      "sidecar_records_cgroup_enforcement",
                      "cgroup_kills_over_limit", "cgroup_killed_row_exact_peak",
                      "cgroup_killed_row_zero_credit",
                      "exit137_without_oom_is_not_rss_killed",
                      "cgroup_timeout_is_timeout_not_oom", "no_leftover_cgroups",
                      "sampled_kill_disarmed_under_cgroup",
                      "requested_vs_actual_recorded",
                      "auto_uses_cgroup_when_available"):
                skip(n, "no usable cgroup delegation here (a child cannot migrate in)")

        # Mechanism cases need a usable base. Where a child cannot migrate in
        # they are SKIPPED, not failed: that is a property of the host. The
        # refusal and forced-fallback cases below always run, because refusing
        # to pretend is exactly what matters on such a host.
        if base is not None:
            # ---- the mechanism, directly, with a tiny allocator -------------------
            cg = sweep.Cgroup(base, f"unit{os.getpid()}", 64 * 1024)      # 64 MB
            try:
                alloc = tmp / "alloc.py"
                alloc.write_text(
                    "import sys\n"
                    "buf = bytearray(int(sys.argv[1]) * 1024 * 1024)\n"
                    "for i in range(0, len(buf), 4096): buf[i] = 1\n"
                    "print('allocated')\n")
                # Launch through the SAME wrapper the runner uses, rather than a
                # preexec_fn. CPython documents preexec_fn as unsafe once threads
                # exist, and this runner always has them (RSS sampler plus a thread
                # pool); the wrapper is a fresh single-threaded process that joins the
                # cgroup and immediately execs, so the probe is inside before it runs.
                cgexec = tmp / "cgexec.py"
                cgexec.write_text(sweep.CGEXEC_SRC)
                p = subprocess.run(
                    [sys.executable, str(cgexec), str(cg.path),
                     sys.executable, str(alloc), "256"],
                    capture_output=True, text=True, timeout=120)
                check("cgroup_enforces_limit",
                      p.returncode == -9 and cg.oom_killed(),
                      f"256 MB under a 64 MB memory.max is OOM-killed by the kernel "
                      f"(rc={p.returncode}, oom_kill={cg.oom_killed()})",
                      p.stderr[-200:])
                peak = cg.peak_kb()
                check("cgroup_peak_is_exact",
                      60 * 1024 <= peak <= 70 * 1024,
                      f"memory.peak reports {peak} kB, at the 65,536 kB limit -- an exact "
                      f"peak, not a sampled bound")
            finally:
                cg.destroy()
            check("cgroup_cleanup", not cg.path.exists(),
                  "the per-probe cgroup is removed afterwards")

            # ---- end to end: under the limit, the run is ordinary ----------------
            r = run(["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                    env_extra={"ALLOC_MB": "32"}, out="under.tsv")
            rows = rt.rows_of(tmp / "under.tsv") if (tmp / "under.tsv").is_file() else []
            check("cgroup_under_limit_runs",
                  r.returncode == 0 and rows and rows[0]["verdict"] == "agree",
                  "a probe that stays under memory.max completes normally under cgroup "
                  "enforcement", r.stderr[-300:])
            meta = json.loads((tmp / "under.tsv.meta.json").read_text())
            check("sidecar_records_cgroup_enforcement",
                  meta["scheduling"]["rss_enforcement"] == "cgroup-memory-max"
                  and meta["scheduling"]["rss_killed_max_rss_source"]
                      == "cgroup-memory-peak-exact-accounted"
                  and meta["scheduling"]["cgroup_base"],
                  "the sidecar records that the KERNEL enforced the limit and that a "
                  "killed row's RSS would be an exact memory.peak",
                  json.dumps(meta["scheduling"])[:300])

            # ---- end to end: over the limit, the kernel kills it ------------------
            r = run(["--enforce", "cgroup", "--kill-over-rss-kb", "80000"],
                    env_extra={"ALLOC_MB": "400"}, out="over.tsv")
            rows = rt.rows_of(tmp / "over.tsv") if (tmp / "over.tsv").is_file() else []
            killed = [x for x in rows if x["run_status"] == "rss_killed"]
            check("cgroup_kills_over_limit",
                  r.returncode == 0 and killed,
                  f"a probe exceeding memory.max becomes an rss_killed row "
                  f"({len(killed)} of {len(rows)})", r.stderr[-400:])
            check("cgroup_killed_row_exact_peak",
                  killed and killed[0]["max_rss_kb"].isdigit()
                  and int(killed[0]["max_rss_kb"]) > 0
                  and "cgroup-ACCOUNTED" in killed[0]["detail"],
                  "and carries memory.peak as max_rss_kb, labelled an EXACT "
                  "cgroup-ACCOUNTED peak -- a different quantity from /usr/bin/time's "
                  "max RSS, which the legacy column name would otherwise imply",
                  str(killed[0] if killed else {})[:300])
            check("cgroup_killed_row_zero_credit",
                  killed and killed[0]["verdict"] == "rss_killed"
                  and all(killed[0][g] == "0" for g in ("cert", "compile", "agree"))
                  and killed[0]["proof"] == "na",
                  "with zero gate credit -- a scheduling outcome, not a design failure")

            # ---- exit 137 WITHOUT an OOM is not an rss_killed row ----------------
            # Any SIGKILL yields 137 -- our own teardown, an external kill, a crash
            # handler. Classifying on the exit code alone would manufacture memory
            # evidence from events that had nothing to do with memory. Only a DELTA in
            # the kernel's oom_kill counter is sound.
            r = run(["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                    env_extra={"STUB_MODE": "selfkill137"}, out="k137.tsv")
            rows = rt.rows_of(tmp / "k137.tsv") if (tmp / "k137.tsv").is_file() else []
            check("exit137_without_oom_is_not_rss_killed",
                  rows and rows[0]["run_status"] != "rss_killed",
                  f"a probe that exits 137 with no OOM event is NOT rss_killed "
                  f"(got run_status={rows[0]['run_status'] if rows else 'NO ROW'})",
                  str(rows[:1])[:300])

            # ---- a timeout under cgroup still cleans up --------------------------
            r = run(["--enforce", "cgroup", "--kill-over-rss-kb", "400000",
                     "--timeout", "2"],
                    env_extra={"STUB_MODE": "sleep", "SLEEP_S": "60"},
                    out="tmo.tsv", timeout=180)
            rows = rt.rows_of(tmp / "tmo.tsv") if (tmp / "tmo.tsv").is_file() else []
            check("cgroup_timeout_is_timeout_not_oom",
                  rows and rows[0]["run_status"] == "timeout",
                  f"a probe killed by --timeout under cgroup enforcement is a `timeout` "
                  f"row, not rss_killed (got {rows[0]['run_status'] if rows else 'NO ROW'})",
                  str(rows[:1])[:300])

            # ---- no cgroup and no process survives any of the above --------------
            leftover = [d.name for d in base.iterdir()
                        if d.is_dir() and d.name.startswith("d3-")]
            check("no_leftover_cgroups", not leftover,
                  f"no per-probe cgroup is left behind after OOM, timeout, 137 and "
                  f"ordinary exits ({leftover or 'none'})")
            orphans = subprocess.run(
                ["ps", "-u", str(os.getuid()), "-o", "args="],
                capture_output=True, text=True).stdout
            check("no_orphan_processes", "cgexec.py" not in orphans,
                  "and no cgexec wrapper or probe process survives")

            # ---- the sampled killer is DISARMED when the kernel enforces ----------
            m_under = json.loads((tmp / "under.tsv.meta.json").read_text())
            check("sampled_kill_disarmed_under_cgroup",
                  m_under["scheduling"]["sampled_kill_armed"] is False,
                  "with the kernel enforcing, the sampled killer is disarmed so it "
                  "cannot win the race and emit sampled evidence under a sidecar that "
                  "claims an exact cgroup peak",
                  str(m_under["scheduling"])[:300])
            check("requested_vs_actual_recorded",
                  m_under["scheduling"]["rss_enforcement_requested"] == "cgroup"
                  and m_under["scheduling"]["rss_enforcement"] == "cgroup-memory-max",
                  "and the sidecar records what was REQUESTED and what ACTUALLY "
                  "enforced, separately")

            # ---- auto falls back, and says so ------------------------------------
            r = run(["--enforce", "auto", "--kill-over-rss-kb", "400000"],
                    env_extra={"ALLOC_MB": "16"}, out="auto.tsv")
            m_auto = json.loads((tmp / "auto.tsv.meta.json").read_text())
            check("auto_uses_cgroup_when_available",
                  r.returncode == 0
                  and m_auto["scheduling"]["rss_enforcement_requested"] == "auto"
                  and m_auto["scheduling"]["rss_enforcement"] == "cgroup-memory-max",
                  "--enforce auto takes the kernel mechanism where it exists, and the "
                  "sidecar still distinguishes the request from the outcome",
                  r.stderr[-300:])

            # ---- sampled mode remains honestly labelled --------------------------
            r = run(["--enforce", "sampled", "--kill-over-rss-kb", "400000"],
                    env_extra={"ALLOC_MB": "16"}, out="samp.tsv")
            m_samp = json.loads((tmp / "samp.tsv.meta.json").read_text())
            check("sampled_mode_labelled_advisory",
                  r.returncode == 0
                  and m_samp["scheduling"]["rss_enforcement"] == "sampled-advisory"
                  and m_samp["scheduling"]["sampled_kill_armed"] is True
                  and m_samp["scheduling"]["rss_killed_max_rss_source"]
                      == "sampled-aggregate-lower-bound",
                  "--enforce sampled keeps the advisory label, arms the sampled killer, "
                  "and declares its RSS provenance as a lower bound",
                  str(m_samp["scheduling"])[:300])

        # ---- join-denied: mkdir works, migration does not --------------------
        # The live defect this replaces: detection proved only mkdir/rmdir, so a
        # sandboxed context that creates and configures a child perfectly but
        # denies every write to cgroup.procs was reported AVAILABLE. The sidecar
        # then said cgroup-memory-max while every probe died before doing any
        # work. Simulated here with a directory that looks like a cgroup and
        # whose cgroup.procs cannot be written.
        fake = tmp / "fakecg"
        (fake / "sub").mkdir(parents=True)
        for f, v in (("cgroup.controllers", "cpu memory pids"),
                     ("cgroup.subtree_control", "cpu memory pids")):
            (fake / f).write_text(v)
        joiner = tmp / "cgexec_real.py"
        joiner.write_text(sweep.CGEXEC_SRC)     # the REAL wrapper the runner uses
        procs = fake / "sub" / "cgroup.procs"
        procs.write_text("")
        procs.chmod(0o444)                      # readable, NOT writable
        rj = subprocess.run(
            [sys.executable, str(joiner), str(fake / "sub"), "/bin/true"],
            capture_output=True, text=True, timeout=30)
        check("join_probe_detects_denied_migration",
              rj.returncode != 0 and "Permission" in (rj.stderr or ""),
              "the join probe FAILS when cgroup.procs cannot be written -- the "
              "exact condition that was previously reported as available",
              (rj.stderr or "")[-200:])

        # and detection built on that probe must reject such a candidate
        # BEHAVIOURAL agreement, not a source grep: whatever detection concludes,
        # the runtime path must agree. A grep could pass while the two drifted --
        # which is exactly what happened when detection used its own join probe
        # and the real wrapper got PermissionError.
        # Drive the subprocess the same way this process concluded: unforced when
        # a base exists, forced when it does not. Otherwise a disagreement would
        # only mean the two halves were asked different questions.
        rt_strict = (run(["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                         env_extra={"ALLOC_MB": "16"}, out="agree.tsv")
                     if base is not None else
                     run_forced(["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                                "agree.tsv", env_extra={"ALLOC_MB": "16"}))
        rt_rows = (rt.rows_of(tmp / "agree.tsv")
                   if (tmp / "agree.tsv").is_file() else [])
        rt_worked = (rt_strict.returncode == 0 and rt_rows
                     and rt_rows[0]["verdict"] == "agree")
        rt_refused = (rt_strict.returncode == 2
                      and "false guarantee" in rt_strict.stderr)
        check("detection_agrees_with_runtime",
              (base is not None and rt_worked) or (base is None and rt_refused),
              f"detection said {'AVAILABLE' if base else 'UNAVAILABLE'} and the "
              f"runtime path {'ran a probe to agree' if rt_worked else 'refused'} "
              f"-- the two cannot disagree, because the preflight IS the runtime "
              f"path (same Cgroup class, same CGEXEC_SRC wrapper)",
              rt_strict.stderr[-300:])

        # ---- strict refusal and auto fallback, actually exercised -------------
        rs = run_forced(["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                        "strict.tsv")
        check("strict_refuses_when_unavailable",
              rs.returncode == 2 and "false guarantee" in rs.stderr,
              "--enforce cgroup REFUSES where migration is impossible, rather than "
              "running under the weaker sampled limit while claiming the stronger "
              "one", rs.stderr[-300:])

        ra = run_forced(["--enforce", "auto", "--kill-over-rss-kb", "400000"],
                        "autofb.tsv", env_extra={"ALLOC_MB": "16"})
        ma = json.loads((tmp / "autofb.tsv.meta.json").read_text()) \
            if (tmp / "autofb.tsv.meta.json").is_file() else {}
        check("auto_falls_back_and_records_actual",
              ra.returncode == 0
              and ma.get("scheduling", {}).get("rss_enforcement_requested") == "auto"
              and ma.get("scheduling", {}).get("rss_enforcement") == "sampled-advisory"
              and ma.get("scheduling", {}).get("sampled_kill_armed") is True,
              "--enforce auto falls back and records sampled-advisory as the ACTUAL "
              "enforcement, with the sampled killer re-armed",
              (ra.stderr[-300:] + str(ma.get("scheduling"))[:200]))
        check("auto_fallback_is_announced",
              "falling back to the SAMPLED advisory limit" in ra.stderr,
              "and says so on stderr rather than degrading silently",
              ra.stderr[-300:])

    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"\nCGROUP-TEST {'OK' if not fails else f'FAILED: {fails}'}"
          + (f" ({len(skipped)} skipped)" if skipped else "")
          + f"  [{time.time() - t_start:.1f}s]")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())

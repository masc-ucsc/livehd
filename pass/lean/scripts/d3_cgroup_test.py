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
import re
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
    # SLEEP_ONLY restricts the sleep to one module, so a multi-target case can
    # have one FAST probe and one slow one. Unset means every probe sleeps,
    # which is what the timeout cases rely on.
    _only = os.environ.get("SLEEP_ONLY", "")
    if not _only or _only in pathlib.Path(sys.argv[-1]).stem:
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

        # The same idea as `run_forced`, but with the patch supplied per case, so
        # a failure mode that cannot be provoked from outside -- an unreadable
        # `memory.events`, a cgroup that refuses to be removed -- can be exercised
        # WITHOUT a seam in the runner. The real class and the real wrapper run;
        # one method is replaced in the child only.
        PATCH = ("import importlib.util,sys\n"
                 "s=importlib.util.spec_from_file_location('sw',%r)\n"
                 "m=importlib.util.module_from_spec(s); s.loader.exec_module(m)\n"
                 "%s\n"
                 "sys.argv=['d3_sweep.py']+sys.argv[1:]\n"
                 "sys.exit(m.main())\n")

        # Detection builds a Cgroup of its own (named `detect-...`) and requires
        # `oom_killed() is False` and `destroy()` to succeed. A patch applied to
        # the whole class therefore breaks DETECTION, the run refuses, and the
        # case under test never executes -- which is what the first version of
        # these cases did. Tagging at construction confines the fault to the
        # probe's cgroup, so the preflight still passes for the real reason.
        MARK_PROBE = (
            "_init = m.Cgroup.__init__\n"
            "def _ni(self, base, name, max_kb):\n"
            "    _init(self, base, name, max_kb)\n"
            "    self._probe = not name.startswith('detect-')\n"
            "m.Cgroup.__init__ = _ni\n")

        def run_patched(patch_src, extra, out, env_extra=None, timeout=180,
                        certs=None, jobs="1"):
            drv = tmp / f"patch_{out.replace('.', '_')}.py"
            drv.write_text(PATCH % (str(SWEEP), patch_src))
            env = dict(os.environ, LAKE=str(stub), TMPDIR=str(tmp))
            env.update(env_extra or {})
            return subprocess.run(
                [sys.executable, str(drv), "--certs", str(certs or cdir),
                 "--out", str(tmp / out), "--jobs", jobs, "--timeout", "120", *extra],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout)

        # Reports failure from destroy() while really removing the directory, so
        # the case leaves nothing behind for the leftover scan, and only for the
        # PROBE's cgroup so detection still passes for the right reason.
        LEAK_PATCH = (MARK_PROBE +
                      "_d = m.Cgroup.destroy\n"
                      "def _nd(self):\n"
                      "    ok = _d(self)\n"
                      "    return False if getattr(self, '_probe', False) else ok\n"
                      "m.Cgroup.destroy = _nd\n")

        # The REALISTIC failure, and the one that matters for the shared machine:
        # the first destroy attempt reports failure WITHOUT removing anything, so
        # an actual cgroup directory survives -- which is what production looks
        # like when the probe's children are still holding it after destroy() has
        # spent its 5 s. Later attempts behave normally, modelling the retry that
        # runs once teardown has killed the holders.
        # LEAK_PATCH cannot test this: it removes the directory for real and only
        # REPORTS failure, so nothing ever survives to be recovered.
        SURVIVE_PATCH = (MARK_PROBE +
                         "_d = m.Cgroup.destroy\n"
                         "def _nd(self):\n"
                         "    if (getattr(self, '_probe', False)\n"
                         "            and not getattr(self, '_tried', False)):\n"
                         "        self._tried = True\n"
                         "        return False\n"
                         "    return _d(self)\n"
                         "m.Cgroup.destroy = _nd\n")

        # Never recovers: every attempt on the probe's cgroup fails and removes
        # nothing, so the directory genuinely leaks. The case asserts the runner
        # says so, then removes it here.
        NEVER_PATCH = (MARK_PROBE +
                       "_d = m.Cgroup.destroy\n"
                       "m.Cgroup.destroy = lambda self: (\n"
                       "    False if getattr(self, '_probe', False) else _d(self))\n")

        def run_dir_of(stderr):
            mo = re.search(r"dir=(\S+)", stderr)
            return pathlib.Path(mo.group(1)) if mo else None

        def run_forced(extra, out, env_extra=None):
            env = dict(os.environ, LAKE=str(stub), TMPDIR=str(tmp))
            env.update(env_extra or {})
            return subprocess.run(
                [sys.executable, str(forced), "--certs", str(cdir),
                 "--out", str(tmp / out), "--jobs", "1", "--timeout", "120", *extra],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)

        # The SAME answer on every host: argument validation runs before any host
        # probing, so an available and an unavailable machine reject an invalid
        # command line identically. Previously detection ran first, so an
        # unavailable host said "not available" and an available one said "needs
        # --kill-over-rss-kb" for the identical argv -- the diagnostic depended on
        # the machine. Both runners are exercised here to pin that.
        for label, runner in (("detected", run),
                              ("forced-unavailable",
                               lambda extra, out=None, env_extra=None, _f=run_forced:
                                   _f(extra, "refuse1f.tsv"))):
            for args, why in ((["--enforce", "cgroup"], "absent"),
                              (["--enforce", "cgroup", "--kill-over-rss-kb", "0"],
                               "zero"),
                              (["--enforce", "auto", "--kill-over-rss-kb", "0"],
                               "zero under auto")):
                rr = runner(args, out="refuse1.tsv")
                check(f"refuse_without_positive_limit_{why.split()[0]}_{label}",
                      rr.returncode == 2
                      and "needs a POSITIVE --kill-over-rss-kb" in rr.stderr,
                      f"--kill-over-rss-kb {why} is refused on a {label} host: a "
                      f"cgroup with no memory.max imposes no limit",
                      rr.stderr[-200:])

        # a negative budget never reaches memory.max: it is truthy, so it passed
        # every `if limit:` guard before being rejected at the argument layer
        rneg = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--out",
             str(tmp / "neg.tsv"), "--enforce", "cgroup",
             "--kill-over-rss-kb", "-5"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(stub), TMPDIR=str(tmp)),
            capture_output=True, text=True, timeout=60)
        check("reject_negative_budget",
              rneg.returncode != 0 and "is negative" in rneg.stderr,
              "a negative --kill-over-rss-kb is rejected at the argument layer, "
              "before it can reach memory.max as a nonsensical write",
              rneg.stderr[-200:])

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
                      "hybrid_arms_both_guards",
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
            # The run-wide `rss_killed_max_rss_source` claim this used to make --
            # that a killed row's RSS WOULD be an exact memory.peak -- is no
            # longer true under hybrid enforcement, where either guard can act.
            # `hybrid_sidecar_defers_provenance_to_the_row` covers what replaced
            # it; this keeps the part that is still a run-wide fact.
            check("sidecar_records_cgroup_enforcement",
                  meta["scheduling"]["rss_enforcement"] == "cgroup-memory-max"
                  and meta["scheduling"]["rss_enforcement_requested"] == "cgroup"
                  and meta["scheduling"]["cgroup_base"],
                  "the sidecar records that the KERNEL is the primary mechanism, "
                  "what was requested, and the base it used",
                  json.dumps(meta["scheduling"])[:300])

            # ---- the SUCCESSFUL path retains the kernel's own peak ----------------
            # The regression this pins: `run_one` read `cg_peak` in its `finally`
            # and then discarded it on every path except an OOM kill, so a GREEN
            # cgroup run kept no exact peak at all -- the sidecar's
            # `rss_killed_max_rss_source` describes killed rows only.  Found on
            # the first real Lean smoke, where the row was green and the peak was
            # gone.
            ok_row = rows[0] if rows else {}
            check("cgroup_peak_retained_on_success",
                  ok_row.get("cgroup_peak_kb", "").isdigit()
                  and int(ok_row["cgroup_peak_kb"]) > 0,
                  f"a green cgroup row carries memory.peak in its own column "
                  f"(got {ok_row.get('cgroup_peak_kb')!r})", str(ok_row)[:300])
            # Two instruments, two columns, neither overwriting the other.  The
            # allocator touches 32 MB, so both figures are positive and the row
            # cannot pass by both being blank.
            check("cgroup_peak_distinct_from_time_rss",
                  ok_row.get("max_rss_kb", "").isdigit()
                  and int(ok_row["max_rss_kb"]) > 0
                  and ok_row.get("max_rss_source") == sweep.SRC_TIME,
                  f"while max_rss_kb independently keeps /usr/bin/time's figure, "
                  f"labelled {sweep.SRC_TIME!r} "
                  f"(got {ok_row.get('max_rss_kb')!r} / "
                  f"{ok_row.get('max_rss_source')!r})", str(ok_row)[:300])

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
            # The killed row states its instrument in a machine-readable column
            # too, and fills `cgroup_peak_kb` uniformly with every other cgroup
            # row -- so a consumer never has to special-case the one status where
            # the peak was also promoted into `max_rss_kb`.
            check("cgroup_killed_row_provenance",
                  killed and killed[0]["max_rss_source"] == sweep.SRC_CGROUP
                  and killed[0]["cgroup_peak_kb"] == killed[0]["max_rss_kb"],
                  f"and names its source {sweep.SRC_CGROUP!r} in both columns "
                  f"(got {killed[0]['max_rss_source'] if killed else None!r} / "
                  f"{killed[0]['cgroup_peak_kb'] if killed else None!r})",
                  str(killed[0] if killed else {})[:300])

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

            # ---- HYBRID, case 1: the KERNEL guard acts -----------------------
            # Anonymous memory over `memory.max`, with the sampled guard set far
            # higher so it cannot be the one that fires. The row must carry the
            # EXACT cgroup peak, not a sampled bound.
            r = run(["--enforce", "cgroup", "--kill-over-rss-kb", "80000",
                     "--max-aggregate-rss-kb", "4000000"],
                    env_extra={"ALLOC_MB": "400"}, out="hy_kernel.tsv")
            hk = [x for x in (rt.rows_of(tmp / "hy_kernel.tsv")
                              if (tmp / "hy_kernel.tsv").is_file() else [])
                  if x["run_status"] == "rss_killed"]
            check("hybrid_kernel_guard_acts",
                  hk and hk[0]["max_rss_source"] == sweep.SRC_CGROUP
                  and "cgroup-ACCOUNTED" in hk[0]["detail"],
                  f"a charge overrun is attributed to the KERNEL guard "
                  f"(source={hk[0]['max_rss_source'] if hk else None!r})",
                  str(hk[:1])[:300] or r.stderr[-300:])

            # ---- HYBRID, case 2: the SAMPLED guard acts, under cgroup mode ----
            # The whole point of the hybrid. `memory.max` is set high enough that
            # the kernel will not fire, while the sampled aggregate ceiling is
            # low -- which is the shape of the real hazard: resident set the
            # cgroup is not charged for. The scripted-sampler seam provides the
            # RSS sequence, so the trip is deterministic rather than a race
            # against a real allocator.
            r = run(["--enforce", "cgroup", "--kill-over-rss-kb", "900000",
                     "--rss-sample-seconds", "0.1"],
                    env_extra={"ALLOC_MB": "16", "STUB_MODE": "sleep",
                               "SLEEP_S": "20",
                               "D3_TEST_RSS_SEQ": "1000,1000,9999999"},
                    out="hy_sampled.tsv", timeout=180)
            hs = [x for x in (rt.rows_of(tmp / "hy_sampled.tsv")
                              if (tmp / "hy_sampled.tsv").is_file() else [])
                  if x["run_status"] == "rss_killed"]
            m_hs = (json.loads((tmp / "hy_sampled.tsv.meta.json").read_text())
                    if (tmp / "hy_sampled.tsv.meta.json").is_file() else {})
            check("hybrid_sampled_guard_acts_under_cgroup",
                  hs and hs[0]["max_rss_source"] == sweep.SRC_SAMPLED
                  and "LOWER BOUND" in hs[0]["detail"],
                  f"an RSS overrun the cgroup was NOT charged for is still caught, "
                  f"by the sampled guard, with its own provenance "
                  f"(source={hs[0]['max_rss_source'] if hs else None!r})",
                  str(hs[:1])[:400] or r.stderr[-400:])
            check("hybrid_sampled_trip_is_nonterminal",
                  hs and hs[0]["verdict"] == "rss_killed"
                  and all(hs[0][g] == "0" for g in ("cert", "compile", "agree")),
                  "and is a scheduling outcome with zero gate credit, exactly as "
                  "in sampled-only mode")
            check("hybrid_sidecar_still_says_cgroup_is_primary",
                  m_hs.get("scheduling", {}).get("rss_enforcement") == "cgroup-memory-max",
                  "while the sidecar still names the kernel as the PRIMARY "
                  "mechanism -- the sampled guard is secondary, not a fallback",
                  str(m_hs.get("scheduling", {}))[:200])

            # ---- HYBRID, case 3: the sampled guard kills the PROOF stage -------
            # End to end, and the reason the classification had to become
            # stage-aware. `SLEEP_ONLY=proof` matches the proof probe's stem
            # (`<m>.proof`) and not the sim probe's, so the sim stage finishes
            # fast and the proof stage is still running when the scripted
            # sampler trips. The sim's rc is then 0 and only the proof
            # subprocess is killed -- the shape that used to be misreported as
            # an ordinary proof failure.
            r = run(["--enforce", "cgroup", "--kill-over-rss-kb", "900000",
                     "--prove", "--rss-sample-seconds", "0.5"],
                    env_extra={"ALLOC_MB": "16", "STUB_MODE": "sleep",
                               "SLEEP_S": "25", "SLEEP_ONLY": "proof",
                               "D3_TEST_RSS_SEQ": "10,10,10,9999999"},
                    out="hy_proofkill.tsv", timeout=180)
            pk = (rt.rows_of(tmp / "hy_proofkill.tsv")
                  if (tmp / "hy_proofkill.tsv").is_file() else [])
            check("sampled_kill_on_proof_is_attributed_to_the_stage",
                  pk and pk[0]["proof"] == "0"
                  and "sampled aggregate hard limit" in pk[0]["detail"],
                  f"a sampled kill landing on the proof subprocess is reported as "
                  f"a proof-STAGE resource outcome, not an ordinary proof failure "
                  f"(proof={pk[0]['proof'] if pk else None!r})",
                  str(pk[:1])[:400] or r.stderr[-400:])
            check("sampled_kill_on_proof_keeps_executable_gates",
                  pk and all(pk[0][g] == "1" for g in
                             ("cert", "compile", "reify", "typecheck", "sim",
                              "checker", "agree"))
                  and pk[0]["verdict"] == "agree",
                  "and the cert..agree the SIM stage already earned survive it -- "
                  "the row is not relabelled rss_killed with zero credit",
                  str(pk[:1])[:300])
            check("sampled_kill_on_proof_rss_is_a_lower_bound",
                  pk and pk[0]["proof_max_rss_source"] == sweep.SRC_SAMPLED
                  and pk[0]["proof_max_rss_kb"].isdigit()
                  and pk[0]["max_rss_source"] == sweep.SRC_SAMPLED,
                  f"with the killed_at figure standing in for the /usr/bin/time "
                  f"report SIGKILL destroyed, labelled a lower bound -- and the "
                  f"whole-target provenance drops to the weaker claim too "
                  f"(proof={pk[0]['proof_max_rss_source'] if pk else None!r})",
                  str(pk[:1])[:300])

            # ---- a measured peak survives a row that turns into runner_error ------
            # Three ways the semantic row can become unusable AFTER the kernel's
            # accounting was read successfully. The reading is gone once the
            # cgroup directory is removed, so in each case it must be carried out
            # on the row rather than discarded with the verdict.
            #
            # (a) `peak_kb()` succeeds, `oom_killed()` does not. The row is
            # correctly `runner_error` -- an unknown OOM state cannot be told
            # apart from a design result -- but the peak WAS measured.
            r = run_patched(MARK_PROBE +
                            "_ok = m.Cgroup.oom_killed\n"
                            "m.Cgroup.oom_killed = lambda self: (\n"
                            "    None if getattr(self, '_probe', False) else _ok(self))\n",
                            ["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                            "peak_noevents.tsv", env_extra={"ALLOC_MB": "32"})
            rows = (rt.rows_of(tmp / "peak_noevents.tsv")
                    if (tmp / "peak_noevents.tsv").is_file() else [])
            check("peak_kept_when_events_unreadable",
                  rows and rows[0]["verdict"] == "runner_error"
                  and rows[0]["cgroup_peak_kb"].isdigit()
                  and int(rows[0]["cgroup_peak_kb"]) > 0,
                  f"unreadable memory.events still fails closed, and keeps the peak "
                  f"it DID read (verdict={rows[0]['verdict'] if rows else None!r}, "
                  f"peak={rows[0]['cgroup_peak_kb'] if rows else None!r})",
                  str(rows[:1])[:400] or r.stderr[-300:])

            # (b) An exception thrown AFTER the probe and after accounting. It
            # unwinds past every return in `run_one`, so `main` synthesizes the
            # row -- and the synthesized row has to recover the peak from the
            # side channel, because the cgroup it came from is already gone.
            r = run(["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                    env_extra={"ALLOC_MB": "32", "D3_TEST_RAISE_ON": "aaa_gate"},
                    out="peak_raise.tsv")
            rows = (rt.rows_of(tmp / "peak_raise.tsv")
                    if (tmp / "peak_raise.tsv").is_file() else [])
            check("peak_kept_when_row_synthesized",
                  rows and rows[0]["verdict"] == "runner_error"
                  and "D3_TEST_RAISE_ON" in rows[0]["detail"]
                  and rows[0]["cgroup_peak_kb"].isdigit()
                  and int(rows[0]["cgroup_peak_kb"]) > 0,
                  f"a post-probe exception yields a synthesized runner_error row that "
                  f"still carries the peak "
                  f"(peak={rows[0]['cgroup_peak_kb'] if rows else None!r})",
                  str(rows[:1])[:400] or r.stderr[-300:])

            # (c) Cleanup failure. A cgroup that will not die keeps its memory
            # charge, so the budget the run is scheduling against is now wrong
            # and the NEXT probe would launch under a partly-consumed limit.
            # That must fail closed rather than warn and return a verdict.
            # The patch really does remove the directory and merely REPORTS
            # failure, so the case leaves nothing behind for the leftover scan.
            r = run_patched(LEAK_PATCH,
                            ["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                            "leak.tsv", env_extra={"ALLOC_MB": "32"})
            rows = (rt.rows_of(tmp / "leak.tsv")
                    if (tmp / "leak.tsv").is_file() else [])
            check("cleanup_failure_fails_closed",
                  rows and rows[0]["verdict"] == "runner_error"
                  and "could not be removed" in rows[0]["detail"]
                  and "could not remove the cgroup" in r.stderr,
                  f"a cgroup that will not die is a runner_error, not an ordinary "
                  f"verdict (got {rows[0]['verdict'] if rows else None!r})",
                  str(rows[:1])[:400] or r.stderr[-300:])
            check("cleanup_failure_keeps_peak",
                  rows and rows[0]["cgroup_peak_kb"].isdigit()
                  and int(rows[0]["cgroup_peak_kb"]) > 0,
                  f"and still reports the peak it read before the failed removal "
                  f"(peak={rows[0]['cgroup_peak_kb'] if rows else None!r})",
                  str(rows[:1])[:400])
            check("cleanup_failure_status_is_fatal",
                  rows and rows[0]["run_status"] == "fatal_cleanup"
                  and rows[0]["verdict"] == "runner_error",
                  f"carrying the dedicated fatal status without losing "
                  f"runner_error (got {rows[0]['run_status'] if rows else None!r})",
                  str(rows[:1])[:300])

            # ---- cleanup failure is FATAL TO THE RUN, not to one row -------------
            # Two targets, jobs=1. A leaked cgroup keeps its memory charge, so the
            # budget every LATER probe is scheduled against is already wrong.
            # Carrying on would run the rest of a cohort under a silently
            # weakened limit while each row still got an ordinary verdict.
            cdir2 = tmp / "certs2"
            rt.make_certs(cdir2, ["aaa", "bbb"])
            r2 = run_patched(SURVIVE_PATCH,
                             ["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                             "fatal2.tsv", env_extra={"ALLOC_MB": "32"}, certs=cdir2)
            rows2 = (rt.rows_of(tmp / "fatal2.tsv")
                     if (tmp / "fatal2.tsv").is_file() else [])
            meta2 = (json.loads((tmp / "fatal2.tsv.meta.json").read_text())
                     if (tmp / "fatal2.tsv.meta.json").is_file() else {})
            check("fatal_cleanup_exits_nonzero",
                  r2.returncode != 0 and "ABORTED" in r2.stderr,
                  f"a two-target run aborts on the first cleanup failure "
                  f"(rc={r2.returncode})", r2.stderr[-400:])
            check("fatal_cleanup_keeps_failing_row",
                  len(rows2) == 1 and rows2[0]["run_status"] == "fatal_cleanup"
                  and rows2[0]["cgroup_peak_kb"].isdigit(),
                  f"the failing row is PRESERVED as the diagnosis, unlike a drifted "
                  f"row ({len(rows2)} row(s))", str(rows2)[:300])
            # The strong form of "the second never launched": its probe file is
            # written INSIDE run_one, after the shutdown guard, so an absent probe
            # proves the target never got as far as being prepared -- not merely
            # that its row was dropped.
            rd = run_dir_of(r2.stderr)
            probes = sorted(p.name for p in (rd / "probes").iterdir()) if rd else []
            check("fatal_cleanup_second_never_launched",
                  len(probes) == 1 and meta2.get("targets") == 2,
                  f"the queued target never launched: {len(probes)} probe(s) prepared "
                  f"of {meta2.get('targets')} selected -- the jobs=1 worker had already "
                  f"picked it up, so only the shutdown guard could stop it "
                  f"({probes})", str(probes)[:200])
            check("fatal_cleanup_sidecar_marked",
                  meta2.get("config", {}).get("aborted_run") is True
                  and meta2["config"].get("aborted_reason") == "fatal_cleanup"
                  and meta2["config"].get("aborted_at_target"),
                  f"and the sidecar carries the GENERIC abort marker with its reason "
                  f"(reason={meta2.get('config', {}).get('aborted_reason')!r}, at "
                  f"{meta2.get('config', {}).get('aborted_at_target')!r})",
                  json.dumps(meta2.get("config", {}))[:300])
            check("fatal_cleanup_not_mislabelled_drift",
                  not meta2.get("config", {}).get("aborted_artifact_drift"),
                  "without borrowing the artifact-drift marker, which would report "
                  "the wrong cause")
            # Aborting the EVIDENCE does not give the machine its memory back.
            # Here the first attempt left a real directory behind, so these
            # assertions are about the resource, not the bookkeeping.
            c2 = meta2.get("config", {})
            left2 = c2.get("cleanup_path", "")
            check("fatal_cleanup_retry_frees_the_cgroup",
                  c2.get("cleanup_retry_succeeded") is True
                  and left2 and not pathlib.Path(left2).exists()
                  and c2.get("cleanup_residual") == "",
                  f"the cgroup that SURVIVED the worker's destroy is removed by the "
                  f"post-teardown retry, and the path is gone from disk "
                  f"(retry_succeeded={c2.get('cleanup_retry_succeeded')!r})",
                  f"{left2} exists={pathlib.Path(left2).exists() if left2 else None}")
            check("fatal_cleanup_retry_reported",
                  "removed on retry" in r2.stderr and left2 in r2.stderr,
                  "and the run says so, naming the path", r2.stderr[-300:])

            # ---- a retry that does NOT recover must say the leak is still there --
            # The one claim nobody could check is "no leak", so it is never made.
            r4 = run_patched(NEVER_PATCH,
                             ["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                             "never.tsv", env_extra={"ALLOC_MB": "32"}, certs=cdir2)
            meta4 = (json.loads((tmp / "never.tsv.meta.json").read_text())
                     if (tmp / "never.tsv.meta.json").is_file() else {})
            c4 = meta4.get("config", {})
            resid = c4.get("cleanup_residual", "")
            check("fatal_cleanup_residual_declared",
                  r4.returncode != 0
                  and c4.get("cleanup_retry_succeeded") is False
                  and resid and pathlib.Path(resid).exists()
                  and "SURVIVES" in r4.stderr and resid in r4.stderr,
                  f"an unrecoverable leak stays aborted, names the residual path, and "
                  f"never claims zero leak (residual={resid!r}, still present)",
                  r4.stderr[-400:])
            # This case leaks on purpose, so the test owns the cleanup. Removing
            # it here also re-proves it was genuinely there.
            if resid and pathlib.Path(resid).exists():
                try:
                    (pathlib.Path(resid) / "cgroup.kill").write_text("1")
                except OSError:
                    pass
                try:
                    pathlib.Path(resid).rmdir()
                except OSError as e:
                    check("fatal_cleanup_residual_removable", False,
                          f"the test could not remove the residual it created: {e}")

            # ---- a LIVE worker is torn down, not waited on -----------------------
            # jobs=2 with the second probe sleeping 60 s. The fast probe fails
            # cleanup first; if teardown did not kill the sleeper the run could
            # only finish by waiting it out, so the wall time is the assertion.
            t_live = time.time()
            r3 = run_patched(LEAK_PATCH,
                             ["--enforce", "cgroup", "--kill-over-rss-kb", "400000"],
                             "fatal_live.tsv",
                             env_extra={"ALLOC_MB": "32", "STUB_MODE": "sleep",
                                        "SLEEP_S": "60", "SLEEP_ONLY": "bbb_gate"},
                             certs=cdir2, jobs="2", timeout=120)
            live_s = time.time() - t_live
            rd3 = run_dir_of(r3.stderr)
            probes3 = sorted(p.name for p in (rd3 / "probes").iterdir()) if rd3 else []
            # BOTH halves are needed. The wall time alone would also pass if the
            # sleeper had never started -- the shutdown guard would have stopped
            # it and nothing would have been torn down. Two prepared probes prove
            # it got past that guard and was live, so only teardown can explain
            # the run ending in under a second.
            check("fatal_cleanup_tears_down_live_worker",
                  r3.returncode != 0 and live_s < 30 and len(probes3) == 2,
                  f"an already-running probe is killed rather than waited out: "
                  f"{len(probes3)} probe(s) launched, run ended in {live_s:.1f}s "
                  f"against a 60 s sleeper (rc={r3.returncode})",
                  f"{probes3} {r3.stderr[-300:]}")

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

            # ---- HYBRID: both guards armed, because they bound different things --
            # The sampled killer used to be disarmed here. That was about
            # provenance -- it must not write a sampled lower bound under a
            # sidecar claiming an exact cgroup peak -- and provenance is now
            # recorded per row. What it missed is that `memory.max` bounds cgroup
            # CHARGE: warm page-cache pages are charged to whoever faulted them
            # in, so a probe mapping warm oleans is nearly invisible to it
            # (measured: 6,897,312 kB RSS against a 694,948 kB cgroup peak). The
            # sampled guard is the only one that sees resident set at all.
            m_under = json.loads((tmp / "under.tsv.meta.json").read_text())
            check("hybrid_arms_both_guards",
                  m_under["scheduling"]["sampled_kill_armed"] is True
                  and set(m_under["scheduling"]["rss_guards"]) >=
                      {"cgroup-memory-max", "sampled-aggregate-hard-kill"},
                  f"under cgroup enforcement BOTH guards are armed: "
                  f"{m_under['scheduling'].get('rss_guards')}",
                  str(m_under["scheduling"])[:300])
            check("hybrid_sidecar_defers_provenance_to_the_row",
                  "per-row" in m_under["scheduling"]["rss_killed_max_rss_source"],
                  "and the sidecar stops naming one mechanism run-wide, because "
                  "either guard can be the one that acts and they write different "
                  "quantities into max_rss_kb",
                  m_under["scheduling"]["rss_killed_max_rss_source"])
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

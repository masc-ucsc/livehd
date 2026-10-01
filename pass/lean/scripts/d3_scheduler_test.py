#!/usr/bin/env python3
"""Scheduler tests: run order, memory deferral, and the aggregate RSS stop.

This machine is shared, so a sweep's schedule is part of its correctness: a run
that finishes by evicting someone else's work is not a better run. The subject
here is only WHICH targets start and WHEN -- never what a started target
measures. Every assertion below is written so that it would fail if the schedule
were allowed to change the experiment:

  * the SELECTION must stay whole. Deferring a target must not shrink
    `selection_digest`, or a memory policy could quietly reduce a denominator
    and report the smaller number as coverage.
  * a deferred row must be NONTERMINAL. If `--resume` treated it as finished,
    "we did not run this" would silently become "this is done".
  * scheduling must stay OUT of the resume configuration. Resuming the deferred
    remainder necessarily drops `--defer-over-rss-kb`, so if that option were
    part of the compared config the resume could never happen.

Lean is stubbed through `d3_runner_test`'s fixtures; a real probe costs minutes
and nothing here depends on one.

Run: python3 pass/lean/scripts/d3_scheduler_test.py
"""

import csv
import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SWEEP = HERE / "d3_sweep.py"

spec = importlib.util.spec_from_file_location("d3_runner_test", HERE / "d3_runner_test.py")
rt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rt)


def rss_table(path: pathlib.Path, rows) -> pathlib.Path:
    """`rows` is a list of (module, max_rss_kb) with max_rss_kb possibly junk."""
    path.write_text("# scheduling only\nmodule\tmax_rss_kb\n"
                    + "".join(f"{m}\t{v}\n" for m, v in rows))
    return path


def run(tmp, certs, out, extra, env_extra=None, timeout=300):
    env = dict(os.environ, LAKE=str(tmp / "fake_lake"))
    env.update(env_extra or {})
    return subprocess.run(
        [sys.executable, str(SWEEP), "--certs", str(certs), "--out", str(out),
         "--timeout", "60", *extra],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout)


def order_of(stdout: str) -> list:
    out = []
    for line in stdout.splitlines():
        s = line.strip()
        if s and s[0].isdigit() and ". " in s:
            out.append(s.split(". ", 1)[1].split()[0])
    return out


def deferred_of(stdout: str) -> list:
    return [l.split()[1] for l in stdout.splitlines() if l.strip().startswith("DEFER ")]


def main() -> int:
    failures = []
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="d3_sched_test_", dir=str(ROOT / "temp")))

    def check(name, cond, note, detail=""):
        if cond:
            print(f"ok   {name:<28} {note}")
        else:
            print(f"FAIL {name:<28} {note}")
            if detail:
                print(f"     {detail[:900]}")
            failures.append(name)

    try:
        (tmp / "fake_lake").write_text(rt.STUB)
        (tmp / "fake_lake").chmod(0o755)
        cdir = tmp / "certs"
        blocks = ["aaa", "bbb", "ccc", "ddd", "eee"]
        certs = rt.make_certs(cdir, blocks)
        mods = [f"{b}_gate" for b in blocks]

        # Deliberately NOT alphabetical, and deliberately crossing the threshold
        # in the middle: alphabetical order would be ccc,ddd,... and RSS order is
        # eee,ccc,aaa | bbb(over), ddd(unknown).
        tbl = rss_table(tmp / "rss.tsv", [
            (mods[0], 3000), (mods[1], 9000), (mods[2], 2000), (mods[4], 1000),
        ])

        # ---- 1. dry run prints the order, cheapest first, with prior RSS ------
        d = run(tmp, cdir, tmp / "o1.tsv",
                ["--order-by", str(tbl), "--jobs", "1", "--dry-run"])
        got = order_of(d.stdout)
        check("dry_run_order", got == [mods[4], mods[2], mods[0], mods[1], mods[3]],
              f"cheapest-first run order: {got}", d.stdout + d.stderr)
        check("dry_run_shows_rss", "prior_rss=1000 kB" in d.stdout,
              "each target carries the RSS the order was built from", d.stdout)
        check("dry_run_writes_nothing", not (tmp / "o1.tsv").exists(),
              "a dry run leaves no results table behind")

        # ---- 2. a target with no recorded RSS runs LAST -----------------------
        check("unknown_rss_last", got and got[-1] == mods[3],
              f"{mods[3]} has no RSS row and sorts last, not first", str(got))
        check("unknown_rss_named", "no recorded RSS" in d.stderr,
              "and the run says so rather than silently guessing", d.stderr)

        # ---- 3. junk and duplicate RSS values ---------------------------------
        bad = rss_table(tmp / "bad.tsv", [
            (mods[0], "not-a-number"), (mods[1], ""), (mods[2], "-5"),
            (mods[3], "7000"), (mods[3], "1"), (mods[4], "0"),
        ])
        d3 = run(tmp, cdir, tmp / "o3.tsv",
                 ["--order-by", str(bad), "--jobs", "1", "--dry-run"])
        g3 = order_of(d3.stdout)
        # ddd is the only usable value; the LAST duplicate row wins (1), and
        # unparseable/empty/zero/negative all count as "no measurement" -- which
        # sorts last rather than sorting as small.
        check("bad_rss_values", d3.returncode == 0 and g3 and g3[0] == mods[3],
              f"junk/blank/zero/negative RSS are treated as unmeasured: {g3}",
              d3.stdout + d3.stderr)
        check("duplicate_rss_row", d3.returncode == 0,
              "a duplicated module row does not crash the schedule", d3.stderr)

        # ---- 4. defer by threshold, with the selection kept whole -------------
        out4 = tmp / "o4.tsv"
        d4 = run(tmp, cdir, out4,
                 ["--order-by", str(tbl), "--defer-over-rss-kb", "5000", "--jobs", "1"])
        check("defer_exit_clean", d4.returncode == 0,
              "a deferral is a clean exit, not a failure", d4.stderr[-700:])
        rows4 = {r["module"]: r for r in rt.rows_of(out4)}
        check("defer_all_rows_present", set(rows4) == set(mods),
              f"every selected target has a row: {sorted(rows4)}")
        dfr = {m for m, r in rows4.items() if r["run_status"] == "deferred"}
        check("defer_over_threshold", dfr == {mods[1], mods[3]},
              f"over-threshold and unmeasured are deferred: {sorted(dfr)}")
        check("defer_not_failed",
              all(rows4[m]["verdict"] == "deferred" and rows4[m]["launched"] == "0"
                  and all(rows4[m][g] == "0" for g in ("cert", "compile", "agree"))
                  for m in dfr),
              "a deferred row says `deferred`, never a verdict recomputed from zeros",
              json.dumps({m: rows4[m] for m in sorted(dfr)})[:600])
        check("defer_others_ran",
              all(rows4[m]["run_status"] == "done" for m in set(mods) - dfr),
              "every under-threshold target still ran")

        meta4 = json.loads((out4.with_suffix(".tsv.meta.json")).read_text())
        check("defer_selection_whole", meta4["targets"] == len(mods),
              f"the sidecar still covers all {len(mods)} targets, not the subset run")
        check("defer_sched_metadata",
              meta4["scheduling"]["defer_over_rss_kb"] == 5000
              and meta4["scheduling"]["order_by_digest"]
              and {x["module"] for x in meta4["scheduling"]["deferred"]} == dfr,
              "threshold, order-file digest and the deferred list are recorded",
              json.dumps(meta4.get("scheduling"))[:500])
        check("defer_not_in_config",
              "defer_over_rss_kb" not in meta4["config"]
              and "order_by" not in meta4["config"],
              "scheduling stays OUT of the resume-compared config")

        # ---- 5. a full selection digest is unchanged by deferring -------------
        d5 = run(tmp, cdir, tmp / "o5.tsv",
                 ["--order-by", str(tbl), "--jobs", "1", "--dry-run"])
        sel_plain = [l for l in d5.stderr.splitlines() if "selection=" in l]
        sel_defer = [l for l in d4.stderr.splitlines() if "selection=" in l]
        check("defer_same_selection_digest",
              sel_plain and sel_defer
              and sel_plain[0].split("selection=")[1] == sel_defer[0].split("selection=")[1],
              "deferring changes no selection digest -- same experiment, different schedule",
              f"{sel_plain}\n{sel_defer}")

        # ---- 6. resume WITHOUT the defer option picks the deferred ones up ----
        d6 = run(tmp, cdir, out4, ["--resume", "--jobs", "1", "--dry-run"])
        todo6, reuse6 = order_of(d6.stdout), d6.stderr
        check("resume_deferred_nonterminal", set(todo6) == dfr,
              f"resume (no defer flag) runs exactly the deferred ones: {todo6}",
              d6.stdout + d6.stderr)
        check("resume_reuses_done", "reusing 3 terminal row(s)" in reuse6,
              "and reuses the 3 rows that completed", reuse6)

        d6b = run(tmp, cdir, out4, ["--resume", "--jobs", "1"])
        rows6 = {r["module"]: r for r in rt.rows_of(out4)}
        check("resume_completes", d6b.returncode == 0
              and all(r["run_status"] == "done" for r in rows6.values())
              and set(rows6) == set(mods),
              "the resumed run finishes the table with no deferred rows left",
              d6b.stderr[-700:])

        # ---- 7. partial sidecar / results written before the deferral ---------
        # The sidecar must describe the run that exists, so that a reader cannot
        # mistake a partial table for a complete one.
        check("partial_sidecar_counts",
              meta4["targets"] == len(mods) and len(rt.rows_of(out4)) == len(mods),
              "the partial run's sidecar and table both state the FULL denominator")

        # ---- 8. an RSS stop defers everything still QUEUED --------------------
        # ThreadPoolExecutor submits every future up front, so "stop launching"
        # has to be enforced where the work STARTS, not where it is submitted.
        # A 1 kB cap trips on the monitor's first sample; the stub is slowed so
        # the queue is still draining when it does.
        out8 = tmp / "o8.tsv"
        d8 = run(tmp, cdir, out8,
                 ["--order-by", str(tbl), "--jobs", "1", "--max-aggregate-rss-kb", "1"],
                 env_extra={"STUB_DELAY": "0.6", "D3_RSS_INTERVAL": "0.05"})
        rows8 = {r["module"]: r for r in rt.rows_of(out8)}
        stopped = {m for m, r in rows8.items() if r["run_status"] == "deferred"}
        check("rss_stop_defers_queued", d8.returncode == 0 and len(stopped) >= 1,
              f"queued targets became deferred after the cap tripped: {sorted(stopped)}",
              d8.stderr[-900:])
        check("rss_stop_not_launched",
              all(rows8[m]["launched"] == "0" and rows8[m]["verdict"] == "deferred"
                  for m in stopped),
              "and none of them was launched")
        check("rss_stop_reported", "CAP TRIPPED" in d8.stderr,
              "the run reports that the cap was what stopped it", d8.stderr[-400:])
        check("rss_stop_resumable",
              set(order_of(run(tmp, cdir, out8,
                               ["--resume", "--jobs", "1", "--dry-run"]).stdout)) == stopped,
              "an RSS stop is resumable on the same terms as a threshold deferral")

        # ---- 8b. the HARD limit stops a probe that is already running --------
        # The soft cap cannot help here: it only declines to start more work,
        # and the case that matters is ONE probe growing past the budget alone.
        out8b = tmp / "o8b.tsv"
        d8b = run(tmp, cdir, out8b,
                  ["--order-by", str(tbl), "--jobs", "1",
                   "--max-aggregate-rss-kb", "0", "--kill-over-rss-kb", "1"],
                  env_extra={"STUB_DELAY": "3", "D3_RSS_INTERVAL": "0.05"})
        rows8b = {r["module"]: r for r in rt.rows_of(out8b)}
        killed = {m for m, r in rows8b.items() if r["run_status"] == "rss_killed"}
        check("hard_kill_terminates_running", d8b.returncode == 0 and len(killed) >= 1,
              f"a running probe was terminated by the hard limit: {sorted(killed)}",
              d8b.stderr[-900:])
        check("hard_kill_launched_not_failed",
              all(rows8b[m]["launched"] == "1" and rows8b[m]["verdict"] == "rss_killed"
                  and all(rows8b[m][g] == "0" for g in ("compile", "agree"))
                  for m in killed),
              "it is recorded as launched-then-killed, not as a gate failure",
              str({m: rows8b[m].get("detail") for m in killed})[:400])
        check("hard_kill_resumable",
              set(order_of(run(tmp, cdir, out8b,
                               ["--resume", "--jobs", "1", "--dry-run"]).stdout)) >= killed,
              "and an rss_killed row is nonterminal, so --resume runs it again")
        check("hard_kill_reported", "HARD limit" in d8b.stderr,
              "the run says the hard limit is what ended it", d8b.stderr[-300:])
        # The two outcomes must stay DISTINGUISHABLE in one run: the probe that
        # was running is `rss_killed`, everything still queued behind it is
        # `deferred`. Collapsing them would claim a design was terminated when
        # it was never started.
        queued = {m for m, r in rows8b.items() if r["run_status"] == "deferred"}
        check("hard_kill_queued_deferred_not_killed",
              queued and not (queued & killed)
              and all(rows8b[m]["launched"] == "0" for m in queued),
              f"queued targets are deferred, not killed: {sorted(queued)}",
              str({m: rows8b[m]["run_status"] for m in rows8b}))
        check("soft_cap_off_in_this_run",
              json.loads((out8b.with_suffix(".tsv.meta.json")).read_text())
                  ["scheduling"]["max_aggregate_rss_kb"] == 0,
              "and this case ran with the SOFT cap disabled, so the hard limit "
              "is demonstrably what acted (case 8 covers soft-stop-only)")

        # ---- 8d. the hard monitor SURVIVES the soft cap -----------------------
        # The regression for the bug that cost a real pilot: the soft cap tripped,
        # the next sample saw _RSS_STOP already set and the sampler thread
        # returned, so the hard limit stopped watching. vpu_mask then grew from
        # 18,002,680 kB to 19,048,836 kB -- past a 19,000,000 kB hard limit --
        # with nothing sampling, and had to be killed by hand.
        #
        # The sequence is the whole point: cross SOFT, stay below HARD for at
        # least one sample (this is where the old code returned), then cross
        # HARD. Against the old loop the final crossing is never observed and
        # nothing is ever rss_killed.
        out8d = tmp / "o8d.tsv"
        d8d = run(tmp, cdir, out8d,
                  ["--order-by", str(tbl), "--jobs", "1",
                   "--max-aggregate-rss-kb", "1000", "--kill-over-rss-kb", "5000"],
                  env_extra={"STUB_DELAY": "3", "D3_RSS_INTERVAL": "0.05",
                             "D3_TEST_RSS_SEQ": "10,1500,2000,2500,9000"})
        rows8d = {r["module"]: r for r in rt.rows_of(out8d)}
        k8d = {m for m, r in rows8d.items() if r["run_status"] == "rss_killed"}
        check("hard_survives_soft_cap",
              d8d.returncode == 0 and len(k8d) >= 1
              and "reached the cap" in d8d.stderr and "HARD limit" in d8d.stderr,
              f"soft cap tripped first, then the hard limit still fired: {sorted(k8d)}",
              d8d.stderr[-900:])
        m8d = json.loads((out8d.with_suffix(".tsv.meta.json")).read_text())
        check("hard_survives_soft_cap_order",
              0 < m8d["aggregate_rss_tripped_kb"] < m8d["aggregate_rss_killed_kb"],
              f"and in that order: soft at {m8d['aggregate_rss_tripped_kb']}, "
              f"hard at {m8d['aggregate_rss_killed_kb']}")

        # ---- 8e. the scripted sampler can never reach a canonical run ---------
        d8e = subprocess.run(
            [sys.executable, str(SWEEP), "--certs", str(cdir), "--manifest", str(tbl),
             "--out", str(tmp / "o8e.tsv"), "--allow-dirty"],
            cwd=ROOT, env=dict(os.environ, LAKE=str(tmp / "fake_lake"),
                               D3_TEST_RSS_SEQ="1,2,3"),
            capture_output=True, text=True, timeout=120)
        check("rss_seam_refused_on_manifest",
              d8e.returncode == 2 and "D3_TEST_RSS_SEQ" in d8e.stderr,
              "a manifest run refuses the scripted RSS sampler outright",
              d8e.stderr[-300:])

        # ---- 8c. direct lean and `lake env lean` agree on every gate ----------
        # The two invocation forms must differ ONLY in process count. Measured on
        # three real certificates the output is byte-identical; this pins the
        # plumbing (argv shape, LEAN_PATH propagation) so a future change cannot
        # quietly make the direct path run something else.
        od, ol = tmp / "od.tsv", tmp / "ol.tsv"
        rd = run(tmp, cdir, od, ["--order-by", str(tbl), "--jobs", "1"])
        rl = run(tmp, cdir, ol, ["--order-by", str(tbl), "--jobs", "1", "--via-lake"])
        GATES = ["cert", "compile", "reify", "typecheck", "sim", "checker", "agree",
                 "verdict", "distinct_obs", "selftest_base", "mut_out", "mut_flop"]
        gd = {r["module"]: tuple(r[g] for g in GATES) for r in rt.rows_of(od)}
        gl = {r["module"]: tuple(r[g] for g in GATES) for r in rt.rows_of(ol)}
        check("direct_vs_lake_same_gates",
              rd.returncode == 0 and rl.returncode == 0 and gd == gl and gd,
              f"identical gates both ways over {len(gd)} module(s)",
              f"{rd.stderr[-300:]}\n{rl.stderr[-300:]}")
        md = json.loads((od.with_suffix(".tsv.meta.json")).read_text())["config"]
        ml = json.loads((ol.with_suffix(".tsv.meta.json")).read_text())["config"]
        check("direct_recorded_in_config",
              md.get("lean_bin") and md.get("via_lake") is False
              and ml.get("via_lake") is True and not ml.get("lean_bin"),
              "which binary ran is part of `config`, so a resume cannot mix the two",
              f"{md.get('lean_bin')!r} / {ml.get('via_lake')!r}")

        # ---- 9. the descendant walk sees a grandchild the marker misses -------
        spec2 = importlib.util.spec_from_file_location("d3_sweep", SWEEP)
        sw = importlib.util.module_from_spec(spec2)
        spec2.loader.exec_module(sw)
        kid = subprocess.Popen([sys.executable, "-c",
                                "import subprocess,sys;"
                                "subprocess.run([sys.executable,'-c','import time;time.sleep(4)'])"])
        try:
            import time
            time.sleep(1.0)
            desc = sw._descendants(os.getpid())
            check("descendant_tree_walk", len(desc) >= 2,
                  f"the ppid walk finds the child AND the grandchild ({len(desc)} pids); "
                  f"neither names a probe path, so the marker scan alone would miss them")
            check("aggregate_counts_tree", sw._aggregate_rss_kb() > 0,
                  "and the aggregate is a positive resident total over that tree")
        finally:
            kid.kill()
            kid.wait()

    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("\nSCHEDULER-TEST", "OK" if not failures else f"FAILED: {failures}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())

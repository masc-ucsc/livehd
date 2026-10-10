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
JOIN = HERE / "d3_join.py"

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
                 ["--order-by", str(tbl), "--jobs", "1", "--max-aggregate-rss-kb", "1",
                  "--rss-sample-seconds", "0.05"],
                 env_extra={"STUB_DELAY": "0.6"})
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
                   "--max-aggregate-rss-kb", "0", "--kill-over-rss-kb", "1",
                    "--rss-sample-seconds", "0.05"],
                  env_extra={"STUB_DELAY": "3"})
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
                   "--max-aggregate-rss-kb", "1000", "--kill-over-rss-kb", "5000",
                    "--rss-sample-seconds", "0.05"],
                  env_extra={"STUB_DELAY": "3", 
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

        # The between-stages seam is refused on the same grounds, in BOTH modes:
        # it fabricates the budget state the scheduler acts on. The `:soft`
        # suffix must not be a way around the guard.
        for _v, _lbl in (("blk00_gate", "hard"), ("blk00_gate:soft", "soft")):
            d8f = subprocess.run(
                [sys.executable, str(SWEEP), "--certs", str(cdir),
                 "--manifest", str(tbl), "--out", str(tmp / f"o8f_{_lbl}.tsv"),
                 "--allow-dirty"],
                cwd=ROOT, env=dict(os.environ, LAKE=str(tmp / "fake_lake"),
                                   D3_TEST_KILL_BETWEEN_STAGES=_v),
                capture_output=True, text=True, timeout=120)
            check(f"between_stages_seam_refused_on_manifest_{_lbl}",
                  d8f.returncode == 2
                  and "D3_TEST_KILL_BETWEEN_STAGES" in d8f.stderr,
                  f"a manifest run refuses the between-stages seam ({_lbl} mode)",
                  d8f.stderr[-300:])

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

        # ---- 8f. a TIMEOUT is a limit outcome, not a design result ------------
        # Learned from a real run: scoreboard_gate exceeded 3600 s, and because a
        # timeout was not a nonterminal scheduler status its verdict was
        # recomputed from its own all-zero gates into `cert` -- which reads as
        # "this design passed only the first gate" -- while run_status=done made
        # the row terminal, so --resume would have reused a timeout as finished
        # work. Both halves are pinned here.
        #
        # Run WITH a manifest: an exploratory run records no cert_sha256, so the
        # join could only ever call it `unauthenticated` and would never reach
        # the timeout branch under test.
        man8 = rt.make_manifest(tmp / "man8.tsv", certs)
        out8f = tmp / "o8f.tsv"
        TMO = ["--timeout", "1"]
        d8f = rt.sweep(tmp, man8, cdir, out8f,
                       extra=["--order-by", str(tbl), "--jobs", "1"] + TMO,
                       env_extra={"STUB_DELAY": "4"})
        rows8f = {r["module"]: r for r in rt.rows_of(out8f)}
        tmo = {m for m, r in rows8f.items() if r["run_status"] == "timeout"}
        check("timeout_is_its_own_status", d8f.returncode == 0 and len(tmo) >= 1,
              f"a probe that runs past --timeout gets run_status=timeout: {sorted(tmo)}",
              d8f.stderr[-700:])
        # `tmo and` on every one of these: `all(...)` over an EMPTY set is True,
        # so without it a build that produces no timeout row at all would pass
        # every assertion below while testing nothing.
        check("timeout_verdict_not_recomputed",
              tmo and all(rows8f[m]["verdict"] == "timeout" for m in tmo),
              "its verdict stays `timeout` instead of being recomputed from zeros",
              str({m: rows8f[m]["verdict"] for m in tmo}))
        check("timeout_not_cert",
              tmo and all(rows8f[m]["verdict"] != "cert" for m in tmo),
              "and is never `cert`, which would be a claim about the design")
        check("timeout_launched_and_detailed",
              tmo and all(rows8f[m]["launched"] == "1" and "timeout" in rows8f[m]["detail"]
                          for m in tmo),
              "launched=1 is kept (it DID run) and the detail still says timeout",
              str({m: (rows8f[m]["launched"], rows8f[m]["detail"]) for m in tmo}))
        # The resume MUST carry the same --timeout: `timeout` is part of the
        # semantic config, so a resume at a different one is correctly refused
        # and would be testing the config guard, not resumability.
        rr = rt.sweep(tmp, man8, cdir, out8f,
                      extra=["--resume", "--jobs", "1", "--dry-run"] + TMO)
        check("timeout_resumable", tmo and set(order_of(rr.stdout)) >= tmo,
              "and --resume (same config) runs it again rather than treating it as done",
              rr.stdout[-400:] + rr.stderr[-400:])

        # ---- 8f-native: the --native early return must not skip it ------------
        # An earlier version of this fix classified the timeout only after the
        # native branch had already returned, so a --native timeout stayed
        # `done` and terminal -- the exact bug the non-native path was fixed for.
        out8n = tmp / "o8n.tsv"
        d8n = rt.sweep(tmp, man8, cdir, out8n,
                       extra=["--jobs", "1", "--native"] + TMO,
                       env_extra={"STUB_DELAY": "4"})
        rows8n = {r["module"]: r for r in rt.rows_of(out8n)}
        ntmo = {m for m, r in rows8n.items() if r["run_status"] == "timeout"}
        check("native_timeout_nonterminal",
              d8n.returncode == 0 and len(ntmo) >= 1
              and all(rows8n[m]["verdict"] == "timeout" for m in ntmo),
              f"a --native timeout is also run_status/verdict=timeout: {sorted(ntmo)}",
              d8n.stderr[-700:])
        check("native_timeout_not_native_ok",
              ntmo and all(rows8n[m]["verdict"] != "native_ok" for m in ntmo),
              "and never `native_ok`, which would credit a kernel-checked claim "
              "that was never finished")

        # ---- 8g. the three limit outcomes stay distinguishable ----------------
        out8g = tmp / "o8g.tsv"
        d8g = rt.sweep(tmp, man8, cdir, out8g,
                       extra=["--order-by", str(tbl), "--jobs", "1",
                              "--defer-over-rss-kb", "5000"] + TMO,
                       env_extra={"STUB_DELAY": "4"})
        rows8g = {r["module"]: r for r in rt.rows_of(out8g)}
        kinds = {m: r["run_status"] for m, r in rows8g.items()}
        check("limit_outcomes_distinct",
              "timeout" in kinds.values() and "deferred" in kinds.values(),
              f"timeout and deferred coexist in one run: {sorted(set(kinds.values()))}",
              str(kinds))
        check("deferred_never_launched",
              any(k == "deferred" for k in kinds.values())
              and any(k == "timeout" for k in kinds.values())
              and all(rows8g[m]["launched"] == "0" for m, k in kinds.items() if k == "deferred")
              and all(rows8g[m]["launched"] == "1" for m, k in kinds.items() if k == "timeout"),
              "a deferred target was never launched; a timed-out one was")

        # ---- 8h. the join gives a timeout zero credit, never a failure --------
        j8 = subprocess.run(
            [sys.executable, str(JOIN), "--manifest", str(man8), "--results", str(out8f),
             "--out", str(tmp / "j8.tsv")],
            cwd=ROOT, capture_output=True, text=True, timeout=120)
        jt = [r for r in rt.rows_of(tmp / "j8.tsv") if r["status"] == "timeout"]
        check("join_timeout_uncredited",
              j8.returncode != 0 and jt
              and all(all(r[g] == "0" for g in ("cert", "compile", "reify", "typecheck",
                                                "sim", "checker", "agree"))
                      and r["proof"] == "na" for r in jt),
              f"zero credit on every gate for {len(jt)} timeout row(s), and the join "
              f"refuses to call the table complete",
              j8.stdout[-400:] + j8.stderr[-400:])
        check("join_timeout_not_failure",
              jt and all(r["verdict"] == "timeout" for r in jt),
              "and records it as `timeout`, never as a design failure")

        # ---- 8i. the soft cap logs ONCE, and records the FIRST crossing ------
        # Before the latch this branch re-ran on every sample after the crossing:
        # a real run emitted ~300 identical lines, and each pass overwrote
        # `tripped_at_kb`, so the sidecar held the LAST crossing rather than the
        # first. The scripted sequence crosses soft once and then stays above it,
        # with values that keep RISING so a last-write-wins bug is visible.
        out8i = tmp / "o8i.tsv"
        d8i = run(tmp, cdir, out8i,
                  ["--order-by", str(tbl), "--jobs", "1",
                   "--max-aggregate-rss-kb", "1000", "--kill-over-rss-kb", "9000",
                    "--rss-sample-seconds", "0.05"],
                  env_extra={"STUB_DELAY": "3", 
                             "D3_TEST_RSS_SEQ": "10,1500,2000,2500,3000"})
        n_soft = sum(1 for l in d8i.stderr.splitlines() if "reached the cap" in l)
        check("soft_cap_logs_once", d8i.returncode == 0 and n_soft == 1,
              f"the soft cap logs exactly once, not once per sample (saw {n_soft})",
              d8i.stderr[-500:])
        m8i = json.loads((out8i.with_suffix(".tsv.meta.json")).read_text())
        check("soft_cap_records_first_crossing",
              m8i["aggregate_rss_tripped_kb"] == 1500,
              f"and records the FIRST crossing (1500), not the last "
              f"(got {m8i['aggregate_rss_tripped_kb']})")
        check("soft_cap_latch_keeps_sampling",
              m8i["aggregate_rss_peak_kb"] >= 3000,
              f"while the sampler keeps running after the latch "
              f"(peak {m8i['aggregate_rss_peak_kb']} >= 3000)")

        # ---- 8j. an rss_killed row carries the sampled kill peak --------------
        # The kill destroys its own measurement: /usr/bin/time writes at exit and
        # never gets there under SIGKILL, so max_rss_kb/user_s/sys_s came back
        # EMPTY on the one real rss_killed row. An empty cell reads as "not
        # measured" when the truth is "measured by another instrument, as at
        # least this much".
        out8j = tmp / "o8j.tsv"
        d8j = run(tmp, cdir, out8j,
                  ["--order-by", str(tbl), "--jobs", "1",
                   "--max-aggregate-rss-kb", "0", "--kill-over-rss-kb", "5000",
                    "--rss-sample-seconds", "0.05"],
                  env_extra={"STUB_DELAY": "3", 
                             "D3_TEST_RSS_SEQ": "10,1000,7777"})
        rows8j = {r["module"]: r for r in rt.rows_of(out8j)}
        killed = {m for m, r in rows8j.items() if r["run_status"] == "rss_killed"}
        m8j = json.loads((out8j.with_suffix(".tsv.meta.json")).read_text())
        check("rss_killed_row_has_rss",
              killed and all(rows8j[m]["max_rss_kb"] == str(m8j["aggregate_rss_killed_kb"])
                             for m in killed),
              f"an rss_killed row carries max_rss_kb = the sampled kill peak "
              f"({m8j['aggregate_rss_killed_kb']}), not an empty cell",
              str({m: rows8j[m]["max_rss_kb"] for m in killed}))
        check("rss_killed_rss_provenance_stated",
              killed and all("LOWER BOUND" in rows8j[m]["detail"]
                             and "SAMPLED" in rows8j[m]["detail"] for m in killed),
              "and its detail says the figure is a SAMPLED LOWER BOUND, so it "
              "cannot be mistaken for a /usr/bin/time peak",
              str({m: rows8j[m]["detail"][:120] for m in killed}))
        check("rss_killed_times_stay_blank",
              killed and all(rows8j[m]["user_s"] == "" and rows8j[m]["sys_s"] == ""
                             for m in killed),
              "while user_s/sys_s stay blank -- nothing measured them at all")

        # ---- 8k. the sampler interval is configurable -------------------------
        check("rss_sample_seconds_recorded",
              m8j["scheduling"].get("rss_sample_seconds") == 0.05
              and m8j["scheduling"].get("rss_sample_seconds_effective") == 0.05
              and m8j["scheduling"].get("rss_enforcement") == "sampled-advisory",
              "the sidecar records the sample interval and that enforcement is "
              "SAMPLED-ADVISORY, not a guarantee",
              str(m8j["scheduling"])[:300])

        # ---- 8l. the RECORDED interval is the EFFECTIVE interval --------------
        # The bug this replaces: `_rss_monitor` honoured a D3_RSS_INTERVAL
        # environment variable while the sidecar recorded --rss-sample-seconds.
        # A run could therefore sample at 0.05 s and certify 5.0 -- and these very
        # tests, which set that variable, were certifying the mismatch.
        src = (HERE / "d3_sweep.py").read_text()
        body = src.split("def _rss_monitor")[1].split("\ndef ")[0]
        check("no_env_interval_override", "os.environ" not in body,
              "_rss_monitor reads no environment variable for its interval, so the "
              "CLI value is its only source")

        # Behavioural proof, not just a source grep. With a 3 s CLI interval and a
        # ~1 s stub the sampler gets at most one sample, so the scripted sequence
        # never reaches its 9999 entry and nothing is killed. Were the env var
        # honoured (0.001 s) it would reach it immediately and kill. The outcome
        # therefore discriminates the two.
        out8l = tmp / "o8l.tsv"
        d8l = run(tmp, cdir, out8l,
                  ["--order-by", str(tbl), "--jobs", "1",
                   "--max-aggregate-rss-kb", "0", "--kill-over-rss-kb", "5000",
                   "--rss-sample-seconds", "3"],
                  env_extra={"STUB_DELAY": "1", "D3_RSS_INTERVAL": "0.001",
                             "D3_TEST_RSS_SEQ": "10,10,10,10,10,10,10,9999"})
        rows8l = {r["module"]: r for r in rt.rows_of(out8l)}
        k8l = {m for m, r in rows8l.items() if r["run_status"] == "rss_killed"}
        m8l = json.loads((out8l.with_suffix(".tsv.meta.json")).read_text())
        # The discriminator is the observed PEAK, not merely the absence of a kill.
        # The scripted sequence is 10 x7 then 9999. At the 3 s CLI interval a ~1 s
        # stub yields one or two samples, so the peak stays at 10; were the 0.001 s
        # environment value honoured the sampler would consume the whole sequence
        # within milliseconds and the peak would be 9999. An earlier version of
        # this check only asserted "nothing was killed", which passed against the
        # old code too and therefore proved nothing.
        peak8l = m8l["aggregate_rss_peak_kb"]
        check("inherited_env_cannot_alter_sampling",
              d8l.returncode == 0 and not k8l and peak8l < 5000,
              f"an inherited D3_RSS_INTERVAL does not speed up sampling: observed "
              f"peak {peak8l} stayed far below the 9999 the sequence ends on, so the "
              f"3 s CLI interval governed (killed: {sorted(k8l)})",
              d8l.stderr[-400:])
        # `.get` deliberately: a MISSING field must be a clean FAIL, not a KeyError
        # that aborts the suite and leaves later assertions unrun -- which is
        # exactly what a falsification run produced before this change.
        check("sidecar_interval_is_effective",
              m8l["scheduling"].get("rss_sample_seconds") == 3.0
              and m8l["scheduling"].get("rss_sample_seconds_effective") == 3.0,
              "and the sidecar records 3.0 as both requested and effective, with no "
              "third value possible", str(m8l["scheduling"])[:200])

        # ---- 8m. the interval is validated -----------------------------------
        for bad, why in (("0", "zero busy-loops"), ("-1", "negative busy-loops"),
                         ("nan", "NaN breaks every sleep comparison"),
                         ("inf", "infinite silently disables the limit"),
                         ("abc", "not a number")):
            rbad = subprocess.run(
                [sys.executable, str(SWEEP), "--certs", str(cdir), "--out",
                 str(tmp / "o8m.tsv"), "--rss-sample-seconds", bad],
                cwd=ROOT, capture_output=True, text=True, timeout=60)
            check(f"reject_interval_{bad}",
                  rbad.returncode != 0 and "--rss-sample-seconds" in rbad.stderr,
                  f"--rss-sample-seconds {bad} rejected ({why})", rbad.stderr[-160:])

        # ---- 8n. killed-row RSS provenance is machine-readable ---------------
        check("killed_rss_source_machine_readable",
              m8j["scheduling"].get("rss_killed_max_rss_source")
              == "sampled-aggregate-lower-bound",
              "the sidecar states the killed row's max_rss provenance as a FIELD, "
              "not only as prose in `detail`",
              str(m8j["scheduling"].get("rss_killed_max_rss_source")))

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

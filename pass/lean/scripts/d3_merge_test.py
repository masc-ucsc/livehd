#!/usr/bin/env python3
"""Tests for `d3_merge.py`.

A merger is only worth having if it refuses. Concatenating two TSVs is trivial;
what is hard is being sure the rows were made by the same compiler, the same
runner, the same toolchain and the same manifest, and that nobody is counted
twice. So the positive case here is one test and the refusals are the rest.

Every assertion that quantifies over a set also requires that set non-empty.
`all(...)` over an empty collection is `True`, and a merger that produced no rows
at all would otherwise satisfy most of this file while doing nothing -- the same
vacuity that let an earlier timeout suite pass 6 of 9 assertions against code
that had none of the fix.

Run: python3 pass/lean/scripts/d3_merge_test.py
"""

import copy
import csv
import hashlib
import importlib.util
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MERGE = HERE / "d3_merge.py"
JOIN = HERE / "d3_join.py"

_spec = importlib.util.spec_from_file_location("d3_sweep", HERE / "d3_sweep.py")
sweep = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sweep)
RESULT_COLS = sweep.RESULT_COLS


MANIFEST = {"path": None, "digest": ""}


def base_cfg():
    return {
        "manifest": MANIFEST["path"], "manifest_digest": MANIFEST["digest"],
        "samples": 32, "timeout": 3600, "native": False,
        "artifact_digest": "a" * 64, "tool_digest": "t" * 64,
        "lean_version": "Lean 4.31.0", "lean_bin_version": "Lean 4.31.0",
        "lean_toolchain": "leanprover/lean4:v4.31.0", "lake_version": "Lake 5.0.0",
        "lean_bin": "/toolchains/lean", "lean_env_digest": "e" * 64, "via_lake": False,
        "lake": "/bin/lake", "build_root": "/wt/.lake", "build_root_external": False,
        "worktree_head": "c" * 40, "worktree_dirty": False,
        "tier": "", "selection_digest": "", "runner_selftest": False,
        "prove": False,
    }


def row(key, module, sha, **over):
    r = {c: "" for c in RESULT_COLS}
    r.update({"target_key": key, "module": module, "cert_sha256": sha,
              "verdict": "agree", "run_status": "done", "launched": "1",
              "proof": "na", "drift": "", "wall_s": "1.00", "max_rss_kb": "1000",
              "distinct_obs": "32", "requested_samples": "32",
              "selftest_base": "1", "mut_out": "rejected", "mut_flop": "na",
              "mut_mem": "na", "mutable_out": "1", "mutable_flop": "0",
              "mutable_mem": "0"})
    for g in ("cert", "compile", "reify", "typecheck", "sim", "checker", "agree"):
        r[g] = "1"
    r.update(over)
    return r


def stage(d: pathlib.Path, name, keys, tier="small", run_id=None, cfg_over=None,
          meta_over=None, rows_over=None):
    """Write one stage: a results TSV plus its sidecar."""
    certs = {k: hashlib.sha256(k.encode()).hexdigest() for k in keys}
    mods = {k: f"{k}_gate" for k in keys}
    rows = [row(k, mods[k], certs[k]) for k in keys]
    if rows_over:
        rows = rows_over(rows)
    cfg = base_cfg()
    cfg["tier"] = tier
    cfg["selection_digest"] = sweep.selection_digest(
        [type("T", (), {"key": k, "module": mods[k], "sha256": certs[k]})() for k in keys])
    cfg.update(cfg_over or {})
    tsv = d / f"{name}.tsv"
    with tsv.open("w") as fh:
        fh.write("# fixture\n" + "\t".join(RESULT_COLS) + "\n")
        for r in rows:
            fh.write("\t".join(str(r.get(c, "")) for c in RESULT_COLS) + "\n")
    meta = {"config": cfg, "run_id": run_id or f"RUN-{name}",
            "results_sha256": hashlib.sha256(tsv.read_bytes()).hexdigest(),
            "scheduling": {"jobs": 1, "only": "", "limit": 0, "one_per_tier": False,
                           "order_by": "", "order_by_digest": ""},
            "cert_sha256": certs, "modules": mods, "targets": len(keys)}
    meta.update(meta_over or {})
    (d / f"{name}.tsv.meta.json").write_text(json.dumps(meta, indent=2))
    return tsv


def run_merge(out, *inputs, lock=None):
    cmd = [sys.executable, str(MERGE), "--out", str(out),
           "--inputs", *[str(i) for i in inputs]]
    if lock:
        cmd += ["--source-lock", str(lock)]
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=120)


def rows_of(p):
    with p.open() as fh:
        return list(csv.DictReader((l for l in fh if not l.startswith("#")), delimiter="\t"))


def main() -> int:
    fails = []
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="d3_merge_test_", dir=str(ROOT / "temp")))

    def check(name, cond, note, detail=""):
        if cond:
            print(f"ok   {name:<30} {note}")
        else:
            print(f"FAIL {name:<30} {note}")
            if detail:
                print(f"     {detail[:600]}")
            fails.append(name)

    def refuses(name, note, out_name="o.tsv", **kw):
        """Build a second stage with `kw` applied and require the merge to refuse."""
        a = stage(tmp, f"a_{name}", ["k1", "k2"], tier="tiny", run_id="RUN-A")
        b = stage(tmp, f"b_{name}", ["k3", "k4"], tier="small", run_id="RUN-B", **kw)
        r = run_merge(tmp / f"{name}_{out_name}", a, b)
        ok = r.returncode == 2 and "MERGE REFUSED" in r.stderr
        check(name, ok, note, r.stderr[-400:] or r.stdout[-300:])
        return r

    try:
        # Every fixture points at a REAL manifest: the merger requires the file to
        # exist and to hash to the recorded digest.
        _man = tmp / "fixture_manifest.tsv"
        _lines = ["# fixture manifest", "module\tcert_available\tsha256\tnodes"]
        for k in ["k1", "k2", "k3", "k4", "k5", "k9"]:
            _lines.append(f"{k}\t1\t{hashlib.sha256(k.encode()).hexdigest()}\t10")
        _man.write_text("\n".join(_lines) + "\n")
        MANIFEST["path"] = str(_man)
        MANIFEST["digest"] = hashlib.sha256(_man.read_bytes()).hexdigest()

        # ---- 0. the provenance columns survive a merge ---------------------
        # Two stages measured by DIFFERENT instruments is the normal case once
        # some runs are cgroup-enforced and some are not, and the merged table is
        # what a reader audits. Carried through per row, never flattened to one
        # run-wide answer, because there is no single true answer to flatten to.
        pa = stage(tmp, "pa", ["k1", "k2"], tier="tiny", run_id="RUN-PA",
                   rows_over=lambda rs: [dict(r, max_rss_source=sweep.SRC_TIME,
                                              cgroup_peak_kb="") for r in rs])
        pb = stage(tmp, "pb", ["k3", "k4"], tier="small", run_id="RUN-PB",
                   rows_over=lambda rs: [dict(r, max_rss_source=sweep.SRC_TIME,
                                              cgroup_peak_kb="4242") for r in rs])
        pout = tmp / "prov.tsv"
        pr = run_merge(pout, pa, pb)
        prows = {x["target_key"]: x for x in (rows_of(pout) if pout.is_file() else [])}
        check("merge_preserves_provenance",
              pr.returncode == 0 and len(prows) == 4
              and all(prows[k]["cgroup_peak_kb"] == "" for k in ("k1", "k2"))
              and all(prows[k]["cgroup_peak_kb"] == "4242" for k in ("k3", "k4"))
              and all(prows[k]["max_rss_source"] == sweep.SRC_TIME for k in prows),
              "per-row memory provenance survives the merge unflattened",
              pr.stderr[-400:] or str({k: (v["max_rss_source"], v["cgroup_peak_kb"])
                                       for k, v in prows.items()})[:300])

        # ---- 1. the positive case ----------------------------------------
        a = stage(tmp, "a", ["k1", "k2"], tier="tiny", run_id="RUN-A")
        b = stage(tmp, "b", ["k3", "k4"], tier="small", run_id="RUN-B")
        out = tmp / "merged.tsv"
        r = run_merge(out, a, b)
        mrows = rows_of(out) if out.is_file() else []
        check("positive_disjoint_merge",
              r.returncode == 0 and len(mrows) == 4
              and {x["target_key"] for x in mrows} == {"k1", "k2", "k3", "k4"},
              f"two disjoint stages merge into {len(mrows)} rows", r.stderr[-400:])
        mm = json.loads((out.with_suffix(".tsv.meta.json")).read_text())
        check("merged_sidecar_sources",
              len(mm["sources"]) == 2
              and {s["run_id"] for s in mm["sources"]} == {"RUN-A", "RUN-B"}
              and all(s["results_sha256"] and s["sidecar_sha256"] for s in mm["sources"]),
              "the merged sidecar names both run_ids with file and sidecar digests")
        union = [type("T", (), {"key": k, "module": mm["modules"][k],
                                "sha256": mm["cert_sha256"][k]})() for k in mm["cert_sha256"]]
        check("selection_digest_recomputed",
              mm["config"]["selection_digest"] == sweep.selection_digest(union)
              and mm["targets"] == 4,
              "selection digest is recomputed over the union, not copied")
        check("merged_caveats",
              any("proof" in c and "na" in c for c in mm["caveats"])
              and any("IMPORTED" in c for c in mm["caveats"]),
              "proof=na and the imported-certificate caveat are carried")

        # ---- 2. permutation identity --------------------------------------
        out2 = tmp / "merged2.tsv"
        r2 = run_merge(out2, b, a)                      # inputs reversed
        check("permutation_identity",
              r2.returncode == 0 and out.read_bytes() == out2.read_bytes(),
              "reversing the input order produces byte-identical output",
              f"{hashlib.sha256(out.read_bytes()).hexdigest()[:16]} vs "
              f"{hashlib.sha256(out2.read_bytes()).hexdigest()[:16]}")
        m2 = json.loads((out2.with_suffix(".tsv.meta.json")).read_text())
        check("permutation_identity_sidecar",
              mm["config"]["selection_digest"] == m2["config"]["selection_digest"]
              and [s["run_id"] for s in mm["sources"]] == [s["run_id"] for s in m2["sources"]],
              "and a sidecar whose source order is canonical, not CLI order")

        # ---- 3. the merged table is usable by d3_join ----------------------
        man = _man
        j = subprocess.run(
            [sys.executable, str(JOIN), "--manifest", str(man), "--results", str(out),
             "--meta", str(out.with_suffix(".tsv.meta.json")), "--out", str(tmp / "j.tsv")],
            cwd=ROOT, capture_output=True, text=True, timeout=120)
        jr = rows_of(tmp / "j.tsv") if (tmp / "j.tsv").is_file() else []
        agreed = [x for x in jr if x["agree"] == "1"]
        check("join_accepts_merged_table",
              len(jr) == 6 and len(agreed) == 4
              and all(x["proof"] == "na" for x in jr),
              f"d3_join reads the merged table: {len(jr)} rows, {len(agreed)} agree, "
              f"proof na throughout", j.stdout[-300:] + j.stderr[-300:])

        # ---- 4. limit rows stay nonterminal and uncredited -----------------
        c = stage(tmp, "c", ["k5"], tier="mid", run_id="RUN-C",
                  rows_over=lambda rs: [row("k5", "k5_gate", rs[0]["cert_sha256"],
                                            run_status="timeout", verdict="timeout",
                                            **{g: "0" for g in ("cert", "compile", "reify",
                                                                "typecheck", "sim",
                                                                "checker", "agree")})])
        out4 = tmp / "merged4.tsv"
        r4 = run_merge(out4, a, b, c)
        m4 = json.loads((out4.with_suffix(".tsv.meta.json")).read_text())
        tr = [x for x in rows_of(out4) if x["target_key"] == "k5"]
        check("limit_rows_preserved",
              r4.returncode == 0 and tr
              and tr[0]["run_status"] == "timeout" and tr[0]["verdict"] == "timeout"
              and all(tr[0][g] == "0" for g in ("cert", "agree"))
              and m4["incomplete_limit_rows"] == ["k5"],
              "a timeout row keeps its status, keeps zero credit, and the merged "
              "sidecar lists it as incomplete", r4.stderr[-300:])

        # ---- 5. every refusal class ----------------------------------------
        ov_a = stage(tmp, "ov_a", ["k1", "k2"], run_id="RUN-A")
        ov_b = stage(tmp, "ov_b", ["k2", "k3"], run_id="RUN-B")
        rov = run_merge(tmp / "ov.tsv", ov_a, ov_b)
        check("reject_overlapping_rows",
              rov.returncode == 2 and "both contain result rows" in rov.stderr,
              "two stages containing the same target are refused", rov.stderr[-300:])

        for key, val, note in [
            ("samples", 16, "different sample counts"),
            ("timeout", 900, "different probe timeouts"),
            ("artifact_digest", "z" * 64, "a different compiled Lean"),
            ("tool_digest", "z" * 64, "a different runner/harness"),
            ("manifest_digest", "z" * 64, "a different manifest"),
            ("lean_version", "Lean 4.34.1", "a different Lean version"),
            ("lean_env_digest", "z" * 64, "a different execution environment"),
            ("lean_toolchain", "leanprover/lean4:v4.34.1", "a different toolchain"),
            ("build_root", "/other/.lake", "a different build root"),
            ("worktree_head", "d" * 40, "a different commit"),
            ("native", True, "a native run merged with a non-native one"),
        ]:
            refuses(f"reject_{key}", f"{note} is refused", cfg_over={key: val})

        refuses("reject_selftest", "a --runner-selftest run is refused",
                cfg_over={"runner_selftest": True})
        refuses("reject_drifted_run", "an artifact-drift run is refused",
                cfg_over={"aborted_artifact_drift": True})
        refuses("reject_aborted_run", "a run marked aborted_run is refused on the "
                "GENERIC marker, so an abort reason the merger has never heard of "
                "still cannot be merged",
                cfg_over={"aborted_run": True, "aborted_reason": "fatal_cleanup",
                          "aborted_at_target": "k3"})
        refuses("reject_dirty_worktree", "a dirty-worktree run is refused",
                cfg_over={"worktree_dirty": True})
        refuses("reject_unknown_config_key",
                "an unclassified config key is refused, not ignored",
                cfg_over={"some_new_semantic_flag": True})
        refuses("reject_row_outside_selection",
                "a row outside the sidecar's selection is refused",
                meta_over={"cert_sha256": {"k3": hashlib.sha256(b"k3").hexdigest()},
                           "modules": {"k3": "k3_gate"}, "targets": 1})
        refuses("reject_missing_cert_hash",
                "a row with no certificate hash is refused",
                rows_over=lambda rs: [dict(r, cert_sha256="") for r in rs])
        refuses("reject_cert_hash_mismatch",
                "a row whose certificate hash differs from the sidecar is refused",
                rows_over=lambda rs: [dict(rs[0], cert_sha256="9" * 64)] + rs[1:])
        refuses("reject_duplicate_keys",
                "a duplicated target_key within one input is refused",
                rows_over=lambda rs: rs + [dict(rs[0])])
        refuses("reject_drift_row",
                "a row marked drift is refused",
                rows_over=lambda rs: [dict(rs[0], drift="after")] + rs[1:])
        refuses("reject_sidecar_count_mismatch",
                "a sidecar whose targets count contradicts its selection is refused",
                meta_over={"targets": 99})
        refuses("reject_no_run_id", "a sidecar with no run_id is refused",
                meta_over={"run_id": ""})

        # missing / unreadable sidecar
        sa = stage(tmp, "ms_a", ["k1"], run_id="RUN-A")
        sb = stage(tmp, "ms_b", ["k2"], run_id="RUN-B")
        (tmp / "ms_b.tsv.meta.json").unlink()
        rms = run_merge(tmp / "ms.tsv", sa, sb)
        check("reject_missing_sidecar",
              rms.returncode == 2 and "no sidecar" in rms.stderr,
              "a results table with no sidecar is refused", rms.stderr[-300:])
        (tmp / "ms_b.tsv.meta.json").write_text("{not json")
        rbad = run_merge(tmp / "ms2.tsv", sa, sb)
        check("reject_unreadable_sidecar",
              rbad.returncode == 2 and "unreadable sidecar" in rbad.stderr,
              "an unparseable sidecar is refused", rbad.stderr[-300:])

        # overlapping SELECTIONS with disjoint rows
        # Now SUBSUMED: with exact row/selection equality required, overlapping
        # selections imply overlapping rows. The fixture is kept because it is
        # refused for a reason that is itself worth pinning -- the sidecar's
        # recorded selection_digest does not match its own cert/module maps.
        oa = stage(tmp, "os_a", ["k1"], run_id="RUN-A",
                   meta_over={"cert_sha256": {k: hashlib.sha256(k.encode()).hexdigest()
                                              for k in ("k1", "k9")},
                              "modules": {k: f"{k}_gate" for k in ("k1", "k9")},
                              "targets": 2})
        ob = stage(tmp, "os_b", ["k2"], run_id="RUN-B",
                   meta_over={"cert_sha256": {k: hashlib.sha256(k.encode()).hexdigest()
                                              for k in ("k2", "k9")},
                              "modules": {k: f"{k}_gate" for k in ("k2", "k9")},
                              "targets": 2})
        ros = run_merge(tmp / "os.tsv", oa, ob)
        check("reject_forged_selection_digest",
              ros.returncode == 2 and "cert/module maps hash to" in ros.stderr,
              "a sidecar whose selection_digest disagrees with its own cert/module "
              "maps is refused, so a stale or forged digest cannot enter the union",
              ros.stderr[-300:])

        # a stage whose rows do not COVER its selection -- the partial-run case
        pa = stage(tmp, "pr_a", ["k1", "k2"], run_id="RUN-A")
        pb = stage(tmp, "pr_b", ["k3", "k4"], run_id="RUN-B")
        # drop one row, leaving the sidecar claiming two targets
        _l = pb.read_text().splitlines()
        pb.write_text("\n".join(_l[:-1]) + "\n")
        _mb = json.loads((tmp / "pr_b.tsv.meta.json").read_text())
        _mb["results_sha256"] = hashlib.sha256(pb.read_bytes()).hexdigest()
        (tmp / "pr_b.tsv.meta.json").write_text(json.dumps(_mb, indent=2))
        rpr = run_merge(tmp / "pr.tsv", pa, pb)
        check("reject_partial_stage",
              rpr.returncode == 2 and "produced no row" in rpr.stderr,
              "a stage whose rows do not cover its selection is refused: its "
              "unmeasured targets would vanish from the merged denominator",
              rpr.stderr[-300:])

        # ---- tampering is REFUSED, not recorded --------------------------
        # Recording an edited row's post-edit hash is not authentication: it
        # describes whatever the file now is. The sidecar binds the results bytes
        # at the moment they are written, so an edit with an unchanged sidecar is
        # a contradiction the merge must refuse.
        ta = stage(tmp, "tp_a", ["k1", "k2"], run_id="RUN-A")
        tb = stage(tmp, "tp_b", ["k3", "k4"], run_id="RUN-B")
        r_clean = run_merge(tmp / "tp1.tsv", ta, tb)
        check("bound_source_merges", r_clean.returncode == 0,
              "an unmodified, bound stage merges normally", r_clean.stderr[-300:])
        m_clean = json.loads((tmp / "tp1.tsv.meta.json").read_text())
        check("binding_recorded",
              set(m_clean["source_binding"]) == {"sidecar"}
              and all(x["bound_by"] == "sidecar" for x in m_clean["sources"]),
              "and the merged sidecar records HOW each source was bound")

        _lines = tb.read_text().splitlines()
        _vi = _lines[1].split("\t").index("verdict")
        for _i in range(2, len(_lines)):
            _f = _lines[_i].split("\t")
            if _f[_vi] == "agree":
                _f[_vi] = "agree_TAMPERED"
                _lines[_i] = "\t".join(_f)
                break
        tb.write_text("\n".join(_lines) + "\n")          # sidecar left untouched
        r_tamper = run_merge(tmp / "tp2.tsv", ta, tb)
        check("reject_tampered_rows",
              r_tamper.returncode == 2 and "rows changed after the run" in r_tamper.stderr
              and not (tmp / "tp2.tsv").exists(),
              "an edited row with an unchanged sidecar is REFUSED and nothing is "
              "written", r_tamper.stderr[-300:])

        # ...and a self-contradictory row is refused even when properly bound
        cb = stage(tmp, "ct_b", ["k3", "k4"], run_id="RUN-B",
                   rows_over=lambda rs: [dict(rs[0], verdict="agree_TAMPERED")] + rs[1:])
        rct = run_merge(tmp / "ct.tsv", ta, cb)
        check("reject_row_contradicting_its_gates",
              rct.returncode == 2 and "contradicts itself" in rct.stderr,
              "a row whose verdict disagrees with its own gates is refused even "
              "with a valid binding", rct.stderr[-300:])

        # ---- the legacy source-lock path ----------------------------------
        la = stage(tmp, "lk_a", ["k1", "k2"], run_id="RUN-A")
        lb = stage(tmp, "lk_b", ["k3", "k4"], run_id="RUN-B")
        for f in (tmp / "lk_a.tsv.meta.json", tmp / "lk_b.tsv.meta.json"):
            d = json.loads(f.read_text()); d.pop("results_sha256", None)
            f.write_text(json.dumps(d, indent=2))          # simulate a legacy sidecar
        r_nolock = run_merge(tmp / "lk0.tsv", la, lb)
        check("reject_legacy_without_lock",
              r_nolock.returncode == 2 and "no --source-lock entry" in r_nolock.stderr,
              "a legacy sidecar with no results_sha256 and no lock is refused",
              r_nolock.stderr[-300:])

        lock = tmp / "lock.tsv"
        lock.write_text("file\tresults_sha256\tsidecar_sha256\n" + "".join(
            f"{f.name}\t{hashlib.sha256(f.read_bytes()).hexdigest()}\t"
            f"{hashlib.sha256((tmp / (f.name + '.meta.json')).read_bytes()).hexdigest()}\n"
            for f in (la, lb)))
        r_lock = run_merge(tmp / "lk1.tsv", la, lb, lock=lock)
        m_lock = json.loads((tmp / "lk1.tsv.meta.json").read_text()) if r_lock.returncode == 0 else {}
        check("legacy_lock_authenticates",
              r_lock.returncode == 0
              and set(m_lock.get("source_binding", [])) == {"source-lock"},
              "a matching source lock authenticates a legacy stage, and the merged "
              "sidecar says the binding came from the lock", r_lock.stderr[-300:])

        bad_lock = tmp / "badlock.tsv"
        bad_lock.write_text(lock.read_text().replace(
            hashlib.sha256(lb.read_bytes()).hexdigest(), "f" * 64, 1))
        r_badlock = run_merge(tmp / "lk2.tsv", la, lb, lock=bad_lock)
        check("reject_legacy_lock_mismatch",
              r_badlock.returncode == 2 and "source lock expects" in r_badlock.stderr,
              "a lock whose expected digest does not match the file is refused",
              r_badlock.stderr[-300:])

        # duplicate run_id
        da = stage(tmp, "dup_a", ["k1", "k2"], run_id="SAME")
        db = stage(tmp, "dup_b", ["k3", "k4"], run_id="SAME")
        rdup = run_merge(tmp / "dup.tsv", da, db)
        check("reject_duplicate_run_id",
              rdup.returncode == 2 and "share run_id" in rdup.stderr,
              "two inputs sharing a run_id are refused", rdup.stderr[-300:])

        # missing manifest
        ma = stage(tmp, "mm_a", ["k1", "k2"], run_id="RUN-A",
                   cfg_over={"manifest": str(tmp / "gone.tsv")})
        mb = stage(tmp, "mm_b", ["k3", "k4"], run_id="RUN-B",
                   cfg_over={"manifest": str(tmp / "gone.tsv")})
        rmm = run_merge(tmp / "mm.tsv", ma, mb)
        check("reject_missing_manifest",
              rmm.returncode == 2 and "not readable" in rmm.stderr,
              "an absent manifest is refused, not skipped -- the denominator "
              "cannot be the easiest input to omit", rmm.stderr[-300:])

        # header damage
        ha = stage(tmp, "hd_a", ["k1", "k2"], run_id="RUN-A")
        hb = stage(tmp, "hd_b", ["k3", "k4"], run_id="RUN-B")
        _hl = hb.read_text().splitlines()
        _hl[1] = _hl[1].replace("\tverdict\t", "\tverdict\tverdict\t", 1)
        hb.write_text("\n".join(_hl) + "\n")
        _hm = json.loads((tmp / "hd_b.tsv.meta.json").read_text())
        _hm["results_sha256"] = hashlib.sha256(hb.read_bytes()).hexdigest()
        (tmp / "hd_b.tsv.meta.json").write_text(json.dumps(_hm, indent=2))
        rhd = run_merge(tmp / "hd.tsv", ha, hb)
        check("reject_header_mismatch",
              rhd.returncode == 2 and "does not match RESULT_COLS" in rhd.stderr,
              "a duplicated/extra/missing header column is refused -- DictReader "
              "would hide all three", rhd.stderr[-300:])

        refuses("reject_missing_required_config",
                "a sidecar missing a required semantic field is refused "
                "(two absent fields would compare EQUAL)",
                cfg_over={"lean_toolchain": ""})
        refuses("reject_proof_not_na", "a row claiming proof credit is refused",
                rows_over=lambda rs: [dict(rs[0], proof="ok")] + rs[1:])
        refuses("reject_samples_mismatch",
                "a row whose requested_samples disagrees with the config is refused",
                rows_over=lambda rs: [dict(rs[0], requested_samples="8")] + rs[1:])

        # ---- 5b. the merged output is itself bound, and the join checks it ---
        ba = stage(tmp, "bd_a", ["k1", "k2"], run_id="RUN-A")
        bb = stage(tmp, "bd_b", ["k3", "k4"], run_id="RUN-B")
        bout = tmp / "bound.tsv"
        rb = run_merge(bout, ba, bb)
        bmeta = json.loads((tmp / "bound.tsv.meta.json").read_text())
        check("merged_output_is_bound",
              rb.returncode == 0
              and bmeta.get("results_sha256")
              == hashlib.sha256(bout.read_bytes()).hexdigest(),
              "the merged sidecar binds the merged table's bytes", rb.stderr[-300:])

        def join(res, meta=None):
            cmd = [sys.executable, str(JOIN), "--manifest", str(_man),
                   "--results", str(res), "--out", str(tmp / "jx.tsv")]
            if meta:
                cmd += ["--meta", str(meta)]
            return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                                  timeout=120)
        jb = join(bout, tmp / "bound.tsv.meta.json")
        check("join_accepts_bound_merge",
              "changed after the run" not in jb.stderr,
              "a join of the unmodified merged table does not complain about binding",
              jb.stderr[-300:])
        _bl = bout.read_text().splitlines()
        _vi = _bl[[i for i, l in enumerate(_bl) if not l.startswith("#")][0]].split("\t").index("verdict")
        _first = [i for i, l in enumerate(_bl) if not l.startswith("#")][1]
        _ff = _bl[_first].split("\t"); _ff[_vi] = "EDITED"; _bl[_first] = "\t".join(_ff)
        bout.write_text("\n".join(_bl) + "\n")
        jt2 = join(bout, tmp / "bound.tsv.meta.json")
        check("join_refuses_post_merge_edit",
              jt2.returncode == 2 and "changed after the run" in jt2.stderr,
              "a post-merge edit is refused by the join", jt2.stderr[-300:])
        ju = join(bout)                       # no --meta at all
        check("join_without_meta_is_unauthenticated",
              ju.returncode != 0 and "UNAUTHENTICATED" in (ju.stdout + ju.stderr),
              "a join without --meta says it is not milestone evidence",
              (ju.stdout + ju.stderr)[-300:])

        # ---- 5c. source-lock identity and validation -------------------------
        la2 = stage(tmp, "lv_a", ["k1", "k2"], run_id="RUN-A")
        lb2 = stage(tmp, "lv_b", ["k3", "k4"], run_id="RUN-B")
        for f in (tmp / "lv_a.tsv.meta.json", tmp / "lv_b.tsv.meta.json"):
            d = json.loads(f.read_text()); d.pop("results_sha256", None)
            f.write_text(json.dumps(d, indent=2))

        def mklock(name, text):
            q = tmp / name; q.write_text(text); return q

        good_rows = "".join(
            f"{f.name}\t{hashlib.sha256(f.read_bytes()).hexdigest()}\t"
            f"{hashlib.sha256((tmp / (f.name + '.meta.json')).read_bytes()).hexdigest()}\n"
            for f in (la2, lb2))
        HDR = "file\tresults_sha256\tsidecar_sha256\n"
        gl = mklock("lv_ok.tsv", HDR + good_rows)
        rgl = run_merge(tmp / "lv1.tsv", la2, lb2, lock=gl)
        mgl = json.loads((tmp / "lv1.tsv.meta.json").read_text()) if rgl.returncode == 0 else {}
        check("lock_identity_recorded",
              rgl.returncode == 0
              and mgl.get("source_lock", {}).get("sha256")
              == hashlib.sha256(gl.read_bytes()).hexdigest()
              and mgl["source_lock"]["entries"] == 2,
              "the merged sidecar records the lock's path, digest and entry count, "
              "so `source-lock` binding can be re-checked", rgl.stderr[-300:])

        for nm, text, why, needle in [
            ("lv_badhdr", "name\tr\ts\n" + good_rows,
             "a lock with the wrong header is refused", "header is"),
            ("lv_dupe", HDR + good_rows + good_rows.splitlines()[0] + "\n",
             "a lock with a duplicate file entry is refused", "duplicate entry"),
            ("lv_nothex", HDR + f"{la2.name}\tnothex\t{'a'*64}\n",
             "a lock digest that is not 64 hex characters is refused",
             "64 hex characters"),
            ("lv_extra", HDR + good_rows + f"ghost.tsv\t{'a'*64}\t{'b'*64}\n",
             "a lock naming a file that was not merged is refused",
             "not among the inputs"),
        ]:
            r_ = run_merge(tmp / f"{nm}.out.tsv", la2, lb2, lock=mklock(f"{nm}.tsv", text))
            check(f"reject_{nm[3:]}", r_.returncode == 2 and needle in r_.stderr,
                  why, r_.stderr[-250:])

        # ---- 5d. native_ok may not appear on a non-native run ----------------
        nb = stage(tmp, "nv_b", ["k3", "k4"], run_id="RUN-B",
                   rows_over=lambda rs: [dict(rs[0], verdict="native_ok")] + rs[1:])
        rnv = run_merge(tmp / "nv.tsv", ba, nb)
        check("reject_native_ok_on_non_native_run",
              rnv.returncode == 2 and "native=False" in rnv.stderr,
              "verdict=native_ok on a run with native=false is refused",
              rnv.stderr[-300:])

        # ---- 5e. malformed input is a refusal, not a traceback ---------------
        za = stage(tmp, "mz_a", ["k1", "k2"], run_id="RUN-A")
        zb = stage(tmp, "mz_b", ["k3", "k4"], run_id="RUN-B")
        (tmp / "mz_b.tsv.meta.json").write_text(json.dumps({"config": "not-an-object",
                                                            "run_id": "RUN-B"}))
        rz = run_merge(tmp / "mz.tsv", za, zb)
        check("malformed_config_is_refusal_not_traceback",
              rz.returncode == 2 and "MERGE REFUSED" in rz.stderr
              and "Traceback" not in rz.stderr,
              "a config that is not an object is refused cleanly", rz.stderr[-250:])

        refuses("reject_missing_semantic_field",
                "a sidecar missing ANY semantic field is refused, not just one of "
                "a short required list",
                cfg_over={"build_root": None})

        # ---- 6. the classification must cover every key the RUNNER emits ----
        # Fixtures only contain keys this test author thought of, and temp/ is
        # gitignored so real sidecars may not exist. So the authoritative source
        # is d3_sweep.py itself: every `cfg[...] = ` it performs, plus every key
        # `run_config` returns. If the runner grows a new semantic config field,
        # the merger must refuse rather than ignore it -- this is what notices.
        import ast as _ast, importlib.util as _il
        _s = _il.spec_from_file_location("d3_merge", MERGE)
        _m = _il.module_from_spec(_s); _s.loader.exec_module(_m)
        known = set(_m.SEMANTIC_KEYS) | set(_m.SELECTION_KEYS) | set(_m.ABORT_KEYS)
        tree = _ast.parse((HERE / "d3_sweep.py").read_text())
        emitted = set()
        for node in _ast.walk(tree):
            if (isinstance(node, _ast.Subscript) and isinstance(node.value, _ast.Name)
                    and node.value.id == "cfg"
                    and isinstance(node.slice, _ast.Constant)
                    and isinstance(node.slice.value, str)):
                emitted.add(node.slice.value)
            if isinstance(node, _ast.FunctionDef) and node.name == "run_config":
                for sub in _ast.walk(node):
                    if isinstance(sub, _ast.Dict):
                        emitted.update(k.value for k in sub.keys
                                       if isinstance(k, _ast.Constant)
                                       and isinstance(k.value, str))
        check("runner_config_keys_all_classified",
              bool(emitted) and not (emitted - known),
              f"all {len(emitted)} config key(s) d3_sweep.py emits are classified "
              f"as semantic or selection",
              f"unclassified: {sorted(emitted - known)}")

        # and, when real sidecars happen to exist, they must agree too
        real = sorted((ROOT / "temp" / "d3_canonical_runs").glob("*.tsv.meta.json"))
        unclassified = {}
        for f in real:
            try:
                cfg = json.loads(f.read_text()).get("config", {})
            except (OSError, json.JSONDecodeError):
                continue
            if set(cfg) - known:
                unclassified[f.name] = sorted(set(cfg) - known)
        check("real_sidecar_keys_all_classified",
              not unclassified,
              f"and every config key in {len(real)} real sidecar(s) on disk "
              f"({'none present' if not real else 'all classified'})",
              str(unclassified)[:400])

        # single input is not a merge
        r1 = run_merge(tmp / "one.tsv", a)
        check("reject_single_input",
              r1.returncode == 2 and "at least two" in r1.stderr,
              "one input is not a merge", r1.stderr[-200:])

    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("\nMERGE-TEST", "OK" if not fails else f"FAILED: {fails}")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())

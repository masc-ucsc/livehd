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


def base_cfg():
    return {
        "manifest": "", "manifest_digest": "m" * 64,
        "samples": 32, "timeout": 3600, "native": False,
        "artifact_digest": "a" * 64, "tool_digest": "t" * 64,
        "lean_version": "Lean 4.31.0", "lean_bin_version": "Lean 4.31.0",
        "lean_toolchain": "leanprover/lean4:v4.31.0", "lake_version": "Lake 5.0.0",
        "lean_bin": "/toolchains/lean", "lean_env_digest": "e" * 64, "via_lake": False,
        "lake": "/bin/lake", "build_root": "/wt/.lake", "build_root_external": False,
        "worktree_head": "c" * 40, "worktree_dirty": False,
        "tier": "", "selection_digest": "", "runner_selftest": False,
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
    cfg["selection_digest"] = hashlib.sha256(("|".join(sorted(keys))).encode()).hexdigest()
    cfg.update(cfg_over or {})
    tsv = d / f"{name}.tsv"
    with tsv.open("w") as fh:
        fh.write("# fixture\n" + "\t".join(RESULT_COLS) + "\n")
        for r in rows:
            fh.write("\t".join(str(r.get(c, "")) for c in RESULT_COLS) + "\n")
    meta = {"config": cfg, "run_id": run_id or f"RUN-{name}",
            "scheduling": {"jobs": 1, "only": "", "limit": 0, "one_per_tier": False,
                           "order_by": "", "order_by_digest": ""},
            "cert_sha256": certs, "modules": mods, "targets": len(keys)}
    meta.update(meta_over or {})
    (d / f"{name}.tsv.meta.json").write_text(json.dumps(meta, indent=2))
    return tsv


def run_merge(out, *inputs):
    return subprocess.run(
        [sys.executable, str(MERGE), "--out", str(out), "--inputs", *[str(i) for i in inputs]],
        cwd=ROOT, capture_output=True, text=True, timeout=120)


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
        man = tmp / "man.tsv"
        lines = ["# fixture manifest", "module\tcert_available\tsha256\tnodes"]
        for k in ["k1", "k2", "k3", "k4", "k5"]:
            av = 0 if k == "k5" else 1
            lines.append(f"{k}\t{av}\t{hashlib.sha256(k.encode()).hexdigest() if av else ''}\t10")
        man.write_text("\n".join(lines) + "\n")
        mcfg = json.loads((out.with_suffix(".tsv.meta.json")).read_text())
        mcfg["config"]["manifest_digest"] = hashlib.sha256(man.read_bytes()).hexdigest()
        (out.with_suffix(".tsv.meta.json")).write_text(json.dumps(mcfg, indent=2))
        j = subprocess.run(
            [sys.executable, str(JOIN), "--manifest", str(man), "--results", str(out),
             "--meta", str(out.with_suffix(".tsv.meta.json")), "--out", str(tmp / "j.tsv")],
            cwd=ROOT, capture_output=True, text=True, timeout=120)
        jr = rows_of(tmp / "j.tsv") if (tmp / "j.tsv").is_file() else []
        agreed = [x for x in jr if x["agree"] == "1"]
        check("join_accepts_merged_table",
              len(jr) == 5 and len(agreed) == 4
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
        check("reject_overlapping_selection",
              ros.returncode == 2 and "OVERLAPPING SELECTIONS" in ros.stderr,
              "disjoint ROWS but overlapping selections are still refused -- the "
              "union digest would describe a set neither run measured",
              ros.stderr[-300:])

        # tampering: a row edited after the stage was written
        # The property is not "the merge fails" -- an edited row can still be
        # well-formed. It is that the merged sidecar pins the SOURCE FILE digest,
        # so the edit is visible by comparing against the stage as it was.
        ta = stage(tmp, "tp_a", ["k1", "k2"], run_id="RUN-A")
        tb = stage(tmp, "tp_b", ["k3", "k4"], run_id="RUN-B")
        before_sha = hashlib.sha256(tb.read_bytes()).hexdigest()
        r_before = run_merge(tmp / "tp1.tsv", ta, tb)
        m_before = json.loads((tmp / "tp1.tsv.meta.json").read_text())
        rec_before = {x["results"]: x["results_sha256"] for x in m_before["sources"]}
        # Edit a DATA row's verdict, not the header -- `agree` is also a column
        # name, and renaming the column makes the table malformed, which the
        # merger refuses for a different (also correct) reason. The property
        # under test is about a well-formed but altered row.
        _lines = tb.read_text().splitlines()
        _vi = _lines[1].split("\t").index("verdict")
        for _i in range(2, len(_lines)):
            _f = _lines[_i].split("\t")
            if _f[_vi] == "agree":
                _f[_vi] = "agree_TAMPERED"
                _lines[_i] = "\t".join(_f)
                break
        tb.write_text("\n".join(_lines) + "\n")
        after_sha = hashlib.sha256(tb.read_bytes()).hexdigest()
        r_after = run_merge(tmp / "tp2.tsv", ta, tb)
        m_after = json.loads((tmp / "tp2.tsv.meta.json").read_text())
        rec_after = {x["results"]: x["results_sha256"] for x in m_after["sources"]}
        check("tamper_changes_recorded_source_digest",
              r_before.returncode == 0 and r_after.returncode == 0
              and before_sha != after_sha
              and rec_before.get(tb.name) == before_sha
              and rec_after.get(tb.name) == after_sha
              and rec_before.get(tb.name) != rec_after.get(tb.name),
              "editing a source row changes the digest the merged sidecar records, "
              "so the edit is detectable after the fact",
              f"{str(rec_before)[:150]} -> {str(rec_after)[:150]}")
        check("tamper_not_silently_identical",
              hashlib.sha256((tmp / "tp1.tsv").read_bytes()).hexdigest()
              != hashlib.sha256((tmp / "tp2.tsv").read_bytes()).hexdigest(),
              "and the merged output itself differs, so the two cannot be confused")

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

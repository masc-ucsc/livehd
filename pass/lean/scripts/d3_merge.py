#!/usr/bin/env python3
"""Merge disjoint D3 sweep stages into ONE authenticated result table.

The practical problem: a 20 GB memory budget forces a sweep to be run as stages
(tiny, then small, then five mid targets), and `d3_join.py` accepts exactly one
results table. Without a merger the only way to get one table is to rerun
modules that already passed, which spends hours re-measuring settled rows.

The danger is the obvious one. "Concatenate the TSVs" is a one-line shell
pipeline, and it silently produces a table whose rows were made by different
compilers, different runners, different stimulus counts or different manifests --
a number that looks like coverage and is not evidence of anything. Every refusal
below exists to stop a specific version of that.

What may differ between stages is exactly the SELECTION: which subset ran, and
how it was scheduled. What produced each row -- the compiler, the runner, the
toolchain, the environment, the manifest, the sample count, the timeout -- must
be identical, because that is what makes two rows comparable at all.

Usage:
  d3_merge.py --out merged.tsv --inputs a.tsv b.tsv [c.tsv ...]

Exit 0 only if the merge is complete and every check passed.
"""

import argparse
import csv
import hashlib
import importlib.util
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent

# Reuse the runner's own definitions rather than a parallel, weaker copy: a
# merger that re-implemented RESULT_COLS or `selection_digest` would drift from
# the thing it is validating, and the drift would not be visible in either file.
_spec = importlib.util.spec_from_file_location("d3_sweep", HERE / "d3_sweep.py")
sweep = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sweep)

RESULT_COLS = sweep.RESULT_COLS
GATES = sweep.GATES
NONTERMINAL = sweep.NONTERMINAL_SCHEDULER_STATUSES

# Identical across every input, or the rows are not comparable.  This is the
# whole safety argument: each key names something that can change what a row
# MEANS.
SEMANTIC_KEYS = [
    "manifest", "manifest_digest",          # same denominator, same bytes
    "samples", "timeout", "native",         # same experiment
    "artifact_digest",                      # same compiled Lean the probes load
    "tool_digest",                          # same runner + harness sources
    "lean_version", "lean_bin_version", "lean_toolchain", "lake_version",
    "lean_bin", "lean_env_digest", "via_lake",   # same toolchain AND environment
    "lake", "build_root", "build_root_external",
    "worktree_head", "worktree_dirty",
    "runner_selftest",                      # must be identical AND false
]

# Present only on a run that aborted on artifact drift.  Classified so the
# unknown-key check does not fire on them; such runs are refused earlier anyway.
ABORT_KEYS = ["aborted_artifact_drift", "aborted_at_target", "aborted_phase",
              "artifact_digest_end"]

# May differ: these describe WHICH targets ran and in what order, not what a row
# means.  Anything in `config` outside both lists is unknown to this merger and
# is refused rather than ignored -- a new semantic field must not be able to
# differ silently just because this file has not heard of it.
SELECTION_KEYS = ["tier", "selection_digest"]


class MergeError(Exception):
    pass


def sha256_file(p: pathlib.Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def read_tsv(path: pathlib.Path):
    with path.open(encoding="utf-8") as fh:
        return list(csv.DictReader((l for l in fh if not l.startswith("#")),
                                   delimiter="\t"))


class Source:
    """One input stage, validated against its own sidecar before anything else.

    An input that is not internally consistent cannot be made consistent by
    merging it with another, so this happens first and independently per input.
    """

    def __init__(self, tsv: pathlib.Path):
        self.tsv = tsv
        self.meta_path = tsv.with_suffix(tsv.suffix + ".meta.json")
        if not tsv.is_file():
            raise MergeError(f"{tsv}: results table does not exist")
        if not self.meta_path.is_file():
            raise MergeError(
                f"{tsv.name}: no sidecar at {self.meta_path.name}. A results table "
                f"without its sidecar cannot be authenticated -- nothing says which "
                f"compiler, runner or manifest produced it.")
        try:
            self.meta = json.loads(self.meta_path.read_text())
        except (OSError, json.JSONDecodeError) as e:
            raise MergeError(f"{self.meta_path.name}: unreadable sidecar: {e}")
        if not isinstance(self.meta, dict) or "config" not in self.meta:
            raise MergeError(f"{self.meta_path.name}: sidecar has no `config`")

        self.cfg = self.meta["config"]
        self.run_id = self.meta.get("run_id", "")
        self.sched = self.meta.get("scheduling", {}) or {}
        self.certs = self.meta.get("cert_sha256", {}) or {}
        self.modules = self.meta.get("modules", {}) or {}
        self.rows = read_tsv(tsv)
        self.results_sha = sha256_file(tsv)
        self.meta_sha = sha256_file(self.meta_path)
        self._validate()

    def _bad(self, why: str):
        raise MergeError(f"{self.tsv.name}: {why}")

    def _validate(self):
        if not self.run_id:
            self._bad("sidecar records no run_id")

        # --- runs that are not evidence at all --------------------------------
        if self.cfg.get("runner_selftest"):
            self._bad("ran with --runner-selftest, which redirects the artifact "
                      "digest away from the build the probes load. It is a runner "
                      "regression, not evidence, and cannot be merged.")
        if self.cfg.get("aborted_artifact_drift"):
            self._bad(f"is marked aborted_artifact_drift (at "
                      f"{self.cfg.get('aborted_at_target')!r}): its rows came from a "
                      f"run whose build artifacts changed underneath it.")
        if self.cfg.get("worktree_dirty"):
            self._bad("ran from a DIRTY worktree, so its rows are not reproducible. "
                      "A merged table is presented as authenticated evidence and "
                      "cannot rest on a tree nobody can reconstruct.")

        # --- the sidecar must describe itself consistently --------------------
        if set(self.certs) != set(self.modules):
            self._bad("sidecar's cert_sha256 and modules describe different target "
                      "sets")
        declared = self.meta.get("targets")
        if declared is not None and declared != len(self.certs):
            self._bad(f"sidecar says targets={declared} but lists {len(self.certs)} "
                      f"certificate hashes")

        # --- rows must match the sidecar they ship with -----------------------
        seen = set()
        for r in self.rows:
            missing = [c for c in RESULT_COLS if c not in r]
            if missing:
                self._bad(f"row {r.get('target_key')!r} is missing column(s) {missing}")
            key = (r.get("target_key") or "").strip()
            if not key:
                self._bad("a row has an empty target_key")
            if key in seen:
                self._bad(f"duplicate target_key {key!r} within this input")
            seen.add(key)
            if key not in self.certs:
                # The row describes a target this run never selected, so the
                # sidecar cannot vouch for it.
                self._bad(f"row {key!r} is OUTSIDE the sidecar's selection")
            got = (r.get("cert_sha256") or "").strip()
            if not got:
                self._bad(f"row {key!r} carries no certificate hash, so it cannot be "
                          f"tied to the bytes it claims to describe")
            if got != self.certs[key]:
                self._bad(f"row {key!r} was produced from certificate {got[:12]} but "
                          f"the sidecar records {self.certs[key][:12]}")
            if (r.get("module") or "") != self.modules.get(key):
                self._bad(f"row {key!r} names module {r.get('module')!r}, sidecar "
                          f"resolves {self.modules.get(key)!r}")
            if (r.get("drift") or "").strip():
                self._bad(f"row {key!r} is marked drift={r.get('drift')!r}: the "
                          f"compiler moved during that run")
        self.keys = seen

    # the selection controls this stage used, for the merged sidecar's record
    def selection_record(self) -> dict:
        return {
            "run_id": self.run_id,
            "results": self.tsv.name,
            "results_sha256": self.results_sha,
            "sidecar": self.meta_path.name,
            "sidecar_sha256": self.meta_sha,
            "tier": self.cfg.get("tier", ""),
            "selection_digest": self.cfg.get("selection_digest", ""),
            "only": self.sched.get("only", ""),
            "limit": self.sched.get("limit", 0),
            "one_per_tier": self.sched.get("one_per_tier", False),
            "order_by": self.sched.get("order_by", ""),
            "order_by_digest": self.sched.get("order_by_digest", ""),
            "jobs": self.sched.get("jobs"),
            "targets_selected": len(self.certs),
            "rows_written": len(self.rows),
        }


class _T:
    """Minimal stand-in so `sweep.selection_digest` can be reused verbatim."""
    __slots__ = ("key", "module", "sha256")

    def __init__(self, key, module, sha):
        self.key, self.module, self.sha256 = key, module, sha


def merge(paths, out_path: pathlib.Path) -> dict:
    srcs = [Source(p) for p in paths]
    if len(srcs) < 2:
        raise MergeError("a merge needs at least two inputs")

    # CANONICAL ORDER.  Sorting by run_id means the output bytes -- and therefore
    # the merged digest -- do not depend on the order the inputs were typed.
    srcs.sort(key=lambda s: (s.run_id, s.results_sha))

    ref = srcs[0]
    for s in srcs[1:]:
        for k in SEMANTIC_KEYS:
            a, b = ref.cfg.get(k), s.cfg.get(k)
            if a != b:
                raise MergeError(
                    f"semantic configuration differs between {ref.tsv.name} and "
                    f"{s.tsv.name}: {k} is {a!r} vs {b!r}. Rows produced under "
                    f"different {k} are not comparable and must not share a table.")
        unknown = ((set(s.cfg) | set(ref.cfg))
                   - set(SEMANTIC_KEYS) - set(SELECTION_KEYS) - set(ABORT_KEYS))
        if unknown:
            # Refused, not ignored: an unrecognised config field might be
            # semantic, and defaulting to "probably fine" is how a merger starts
            # hiding real differences.
            raise MergeError(
                f"sidecar carries config key(s) this merger does not classify: "
                f"{sorted(unknown)}. Classify them as semantic or selection in "
                f"d3_merge.py before merging.")

    # The manifest must be the same BYTES, not just the same recorded digest.
    man = pathlib.Path(ref.cfg.get("manifest") or "")
    if man.is_file():
        actual = sha256_file(man)
        if actual != ref.cfg.get("manifest_digest"):
            raise MergeError(
                f"the manifest on disk ({man.name}, {actual[:16]}) no longer matches "
                f"the digest these runs recorded ({str(ref.cfg.get('manifest_digest'))[:16]}). "
                f"The denominator moved after the stages ran.")

    # --- disjointness ------------------------------------------------------
    for i, a in enumerate(srcs):
        for b in srcs[i + 1:]:
            both = sorted(a.keys & b.keys)
            if both:
                raise MergeError(
                    f"{a.tsv.name} and {b.tsv.name} both contain result rows for "
                    f"{both[:5]}{'...' if len(both) > 5 else ''}. A merge cannot "
                    f"choose between two measurements of one target.")
            sel = sorted(set(a.certs) & set(b.certs))
            if sel:
                raise MergeError(
                    f"{a.tsv.name} and {b.tsv.name} have OVERLAPPING SELECTIONS "
                    f"({sel[:5]}{'...' if len(sel) > 5 else ''}). Even with disjoint "
                    f"rows the union selection would double-count those targets, so "
                    f"the recomputed selection digest would describe a set neither "
                    f"run measured.")

    # --- the merge itself --------------------------------------------------
    rows, certs, modules = [], {}, {}
    for s in srcs:
        rows.extend(s.rows)
        certs.update(s.certs)
        modules.update(s.modules)
    rows.sort(key=lambda r: r["target_key"])          # canonical row order

    union = [_T(k, modules[k], certs[k]) for k in certs]
    new_sel = sweep.selection_digest(union)

    cfg = {k: ref.cfg.get(k) for k in SEMANTIC_KEYS}
    cfg["selection_digest"] = new_sel
    cfg["tier"] = "+".join(sorted({(s.cfg.get("tier") or "all") for s in srcs}))
    cfg["runner_selftest"] = False

    limit_rows = sorted(r["target_key"] for r in rows
                        if (r.get("run_status") or "") in NONTERMINAL)
    meta = {
        "merged": True,
        "merged_by": "d3_merge.py",
        "sources": [s.selection_record() for s in srcs],
        "config": cfg,
        "cert_sha256": certs,
        "modules": modules,
        "targets": len(certs),
        "rows": len(rows),
        "incomplete_limit_rows": limit_rows,
        "caveats": [
            "proof is `na` on every row: no per-design translation theorem is "
            "generated or checked by this pass, so no row carries proof credit.",
            "CERTIFICATES ARE IMPORTED. Passing a gate is COMPATIBILITY evidence; "
            "it is not evidence that this branch can emit the certificate end to "
            "end.",
            "This is a MERGE of separately executed stages, not a single run. "
            "Every stage shares the semantic configuration listed in `config`; "
            "they differ only in which targets they selected (see `sources`).",
            "Rows with run_status in {deferred, rss_killed, timeout} are LIMIT "
            "outcomes carrying zero credit, and they keep that status here. The "
            "merged table is incomplete until they run.",
        ],
    }

    body = [
        f"# merged by d3_merge.py from {len(srcs)} stage(s), in canonical run_id order",
        *[f"#   {s.run_id}  {s.tsv.name}  rows={len(s.rows)}  tier={s.cfg.get('tier') or 'all'}"
          for s in srcs],
        f"# selection_digest (recomputed over the union of {len(certs)} targets) = {new_sel}",
        f"# manifest_digest = {ref.cfg.get('manifest_digest')}",
        "# proof=na throughout. IMPORTED certificates: compatibility evidence only.",
        "\t".join(RESULT_COLS),
    ]
    for r in rows:
        body.append("\t".join(str(r.get(c, "")) for c in RESULT_COLS))
    sweep._atomic_write(out_path, "\n".join(body) + "\n")
    sweep._atomic_write(out_path.with_suffix(out_path.suffix + ".meta.json"),
                        json.dumps(meta, indent=2, sort_keys=True) + "\n")
    return meta


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--inputs", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    meta = merge([pathlib.Path(p) for p in a.inputs], out)

    print(f"merged {len(meta['sources'])} stage(s) -> {out} "
          f"({meta['rows']} rows over {meta['targets']} selected targets)")
    for s in meta["sources"]:
        print(f"  {s['run_id']}  {s['results']:<24} rows={s['rows_written']:<4} "
              f"tier={s['tier'] or 'all'}  sel={s['selection_digest'][:12]}")
    print(f"  recomputed selection digest {meta['config']['selection_digest'][:16]}")
    # Gate counts are deliberately NOT printed here. They belong to d3_join,
    # which states them against a manifest denominator; printing them from a
    # merged table invites reading `N agree` without the denominator that makes
    # it mean anything.
    if meta["incomplete_limit_rows"]:
        print(f"  {len(meta['incomplete_limit_rows'])} limit row(s) carry zero credit "
              f"and keep their status: {meta['incomplete_limit_rows'][:6]}")
    print("  proof=na throughout; imported certificates are compatibility evidence only.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except MergeError as e:
        print(f"MERGE REFUSED: {e}", file=sys.stderr)
        sys.exit(2)

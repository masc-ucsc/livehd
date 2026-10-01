#!/usr/bin/env python3
"""Join D3 sweep results onto a target manifest, at the manifest's denominator.

A sweep measures the certificates it could run.  A milestone is stated over
TARGETS.  Those are different sets, and the gap between them is where coverage
claims go wrong: 85 agreeing modules is not 122 CORE-ET targets, and no amount
of summing the results table makes it so.

So the output has EXACTLY as many rows as the manifest -- 122 for CORE-ET, 30
for the frozen CVA6 set -- and a target with no certificate occupies a row that
can never be credited with `sim` or `agree`.  The join refuses rather than
guesses:

  * a result row that no manifest target claims (an orphan)
  * two result rows for one target
  * a certificate whose bytes differ from the manifest's record
  * a sidecar whose configuration does not match the manifest being joined

CORE-ET joins by module name.  CVA6 joins by the manifest's CERTIFICATE name to
the sweep's module, while the output keeps the BLOCK name: `ras` and `ras_gate`
are the same target under two spellings, and collapsing them early loses the one
the milestone is written in.

Usage:
  d3_join.py --manifest M.tsv --results R.tsv --out J.tsv [--meta R.tsv.meta.json]
Exit 0 iff the join is complete and consistent.
"""

import argparse
import csv
import hashlib
import json
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
GATES = ["cert", "compile", "reify", "typecheck", "sim", "checker", "agree", "proof"]
CREDITABLE = ["cert", "compile", "reify", "typecheck", "sim", "checker", "agree"]


class JoinError(Exception):
    pass


def atomic_write(path: pathlib.Path, text: str) -> None:
    """Same-directory temp, fsync, replace.  This is a milestone artifact; a
    half-written one must never be readable as a result table."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.parent / f".{path.name}.partial"
    with tmp.open("w", encoding="utf-8") as fh:
        fh.write(text)
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, path)


def read_tsv(path: pathlib.Path):
    with path.open(encoding="utf-8") as fh:
        return list(csv.DictReader((l for l in fh if not l.startswith("#")), delimiter="\t"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--results", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--meta", default="")
    a = ap.parse_args()

    man_path = pathlib.Path(a.manifest)
    res_path = pathlib.Path(a.results)
    man = read_tsv(man_path)
    res = read_tsv(res_path)
    if not man:
        raise JoinError(f"{man_path} has no rows")

    is_cva6 = "block" in man[0]
    key_col = "block" if is_cva6 else "module"
    keys = [r[key_col] for r in man]
    dupes = sorted({k for k in keys if keys.count(k) > 1})
    if dupes:
        raise JoinError(f"duplicate targets in the manifest: {dupes}")

    # Index results by the manifest's own key when the sweep recorded one, and
    # fall back to the certificate module for a sweep that predates `target_key`.
    by_key, by_module = {}, {}
    for r in res:
        k = (r.get("target_key") or "").strip()
        m = (r.get("module") or "").strip()
        if k:
            if k in by_key:
                raise JoinError(f"two result rows for target {k!r}")
            by_key[k] = r
        if m:
            if m in by_module:
                raise JoinError(f"two result rows for module {m!r}")
            by_module[m] = r

    out_rows, problems, orphan_candidates = [], [], set(by_key) | set(by_module)
    for mrow in man:
        key = mrow[key_col]
        avail_raw = str(mrow.get("cert_available", "")).strip()
        if avail_raw not in ("0", "1"):
            # Not "anything that is not 1 means unavailable": a typo would then
            # quietly convert a target into a blocked row and shrink the
            # effective denominator without saying so.
            raise JoinError(f"{key}: cert_available={avail_raw!r} is not 0 or 1")
        available = avail_raw == "1"
        cert_name = (mrow.get("certificate") or "").strip()
        # CVA6: the manifest names the certificate (`ras_gate`), the sweep names
        # the module it ran (`ras_gate`). The output keeps the block (`ras`).
        module = cert_name if is_cva6 else key
        rk = by_key.get(key)
        rm = by_module.get(module) if module else None
        if rk is not None and rm is not None and rk is not rm:
            raise JoinError(
                f"{key}: target key and certificate module resolve to DIFFERENT result rows "
                f"({rk.get('module')!r} vs {rm.get('module')!r})")
        r = rk if rk is not None else rm
        orphan_candidates.discard(key)
        orphan_candidates.discard(module)

        row = {"target": key, "certificate": cert_name,
               "cert_available": 1 if available else 0,
               "manifest_sha256": (mrow.get("sha256") or ""),
               "nodes": mrow.get("nodes", ""), "proof": "na"}

        if not available:
            # Explicit, and uncreditable. This is the row that stops 85/85 from
            # being read as 122/122.
            for g in CREDITABLE:
                row[g] = 0
            row["verdict"] = "no_certificate"
            row["detail"] = mrow.get("blocked_cause", "") or "no certificate"
            row["status"] = "blocked_upstream"
        elif r is None:
            for g in CREDITABLE:
                row[g] = 0
            row["verdict"] = "not_run"
            row["detail"] = "the manifest claims a certificate but the sweep has no row"
            row["status"] = "missing_result"
            problems.append(f"{key}: no result row")
        else:
            got = (r.get("cert_sha256") or "").strip()
            want = row["manifest_sha256"]
            if not want or not got:
                # An absent hash on either side means the row cannot be tied to
                # the bytes it claims to describe, which is not a weaker form of
                # evidence -- it is none.
                for g in CREDITABLE:
                    row[g] = 0
                row["verdict"] = "unauthenticated"
                row["detail"] = (f"missing certificate hash (manifest={want[:12] or 'none'}, "
                                 f"result={got[:12] or 'none'})")
                row["status"] = "unauthenticated"
                problems.append(f"{key}: missing certificate hash on "
                                f"{'manifest' if not want else 'result'} side")
            elif want != got:
                for g in CREDITABLE:
                    row[g] = 0
                row["verdict"] = "hash_mismatch"
                row["detail"] = (f"result was produced from {got[:12]}, "
                                 f"manifest records {want[:12]}")
                row["status"] = "stale_result"
                problems.append(f"{key}: certificate hash mismatch")
            elif (r.get("run_status") or "").strip() in ("deferred", "rss_killed"):
                # The scheduler declined to start it, so no gate was attempted.
                # Crediting this as `ran` with an all-zero gate row would read as
                # "it got nowhere" when the truth is "it was never tried", and
                # the two have opposite implications for the milestone.
                for g in CREDITABLE:
                    row[g] = 0
                st = (r.get("run_status") or "").strip()
                row["verdict"] = st
                row["detail"] = r.get("detail", "") or f"{st} by the scheduler"
                row["status"] = st
                problems.append(f"{key}: {st}, no gate was judged")
            else:
                for g in CREDITABLE:
                    row[g] = 1 if str(r.get(g, "0")).strip() == "1" else 0
                row["verdict"] = r.get("verdict", "")
                row["detail"] = r.get("detail", "")
                row["status"] = "ran"
                for extra in ("tier", "distinct_obs", "wall_s", "module", "run_status"):
                    row[extra] = r.get(extra, "")
        out_rows.append(row)

    if orphan_candidates:
        problems.append(f"result rows no target claims: {sorted(orphan_candidates)}")

    if a.meta:
        try:
            meta = json.loads(pathlib.Path(a.meta).read_text())
        except (OSError, json.JSONDecodeError) as e:
            raise JoinError(f"sidecar {a.meta} is unreadable: {e}")
        cfg = meta.get("config", {}) or {}
        if cfg.get("runner_selftest"):
            raise JoinError(
                "the sweep was run with --runner-selftest, which redirects the artifact "
                "digest away from the build the probes load: it is a runner regression, "
                "not evidence, and cannot be joined")
        if cfg.get("aborted_artifact_drift"):
            # Rows from a run whose compiler changed partway are not evidence,
            # and a join is where they would otherwise acquire the authority of
            # a milestone table.
            raise JoinError(
                f"the sweep is marked aborted_artifact_drift (at "
                f"{cfg.get('aborted_at_target')!r}, phase {cfg.get('aborted_phase')!r}): "
                f"its rows came from a run whose build artifacts changed and cannot be "
                f"joined or credited")
        # Basename equality proves nothing: two files can share a name and
        # differ in every row. Authenticate the manifest's BYTES.
        want_digest = cfg.get("manifest_digest", "")
        got_digest = hashlib.sha256(man_path.read_bytes()).hexdigest()
        if want_digest and want_digest != got_digest:
            raise JoinError(
                f"the sweep ran against a different manifest: sidecar records "
                f"{want_digest[:16]}, {man_path.name} hashes to {got_digest[:16]}")
        if not want_digest:
            problems.append("sidecar records no manifest digest; the join is unauthenticated")
        sel = cfg.get("selection_digest", "")
        if sel:
            print(f"  sweep selection digest {sel[:16]} "
                  f"(tier={cfg.get('tier') or 'all'}, samples={cfg.get('samples')})")

    cols = (["target", "certificate", "module", "cert_available", "status", "verdict"]
            + CREDITABLE + ["proof", "tier", "nodes", "distinct_obs", "wall_s",
                            "manifest_sha256", "detail"])
    digest = hashlib.sha256()
    for r in out_rows:
        digest.update(f"{r['target']}:{r['verdict']}:{r.get('agree', 0)}\n".encode())
    body = [
        f"# join of {res_path.name} onto {man_path.name}",
        f"# denominator = {len(man)} targets (one row each, including targets with "
        f"no certificate)",
        "# IMPORTED CERTIFICATES: compatibility evidence only. These rows do NOT show "
        "this branch can generate the certificates end to end.",
        "# proof=na throughout: no per-design translation proof is generated or checked "
        "by this pass.",
        f"# manifest_sha256={hashlib.sha256(man_path.read_bytes()).hexdigest()}",
        f"# join_sha256={digest.hexdigest()}",
        "\t".join(cols),
    ]
    for r in out_rows:
        body.append("\t".join(str(r.get(c, "")) for c in cols))
    atomic_write(pathlib.Path(a.out), "\n".join(body) + "\n")

    if len(out_rows) != len(man):
        problems.append(f"produced {len(out_rows)} rows for {len(man)} targets")

    print(f"joined {res_path.name} onto {man_path.name}: {len(out_rows)} rows "
          f"(denominator {len(man)})")
    for g in CREDITABLE:
        print(f"  {g:<10} {sum(1 for r in out_rows if r.get(g) == 1)}/{len(out_rows)}")
    print(f"  {'proof':<10} 0/{len(out_rows)} (na -- not attempted)")
    ndef = sum(1 for r in out_rows if r.get("status") in ("deferred", "rss_killed"))
    if ndef:
        print(f"  {ndef} target(s) were DEFERRED by the scheduler and never attempted; "
              f"they are uncredited and the table is incomplete until they run.")
    nocert = sum(1 for r in out_rows if r["cert_available"] == 0)
    if nocert:
        print(f"  {nocert} target(s) have no certificate and cannot be credited; "
              f"they remain milestone blockers.")
    if problems:
        print("\nJOIN PROBLEMS:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except JoinError as e:
        print(f"JOIN ERROR: {e}", file=sys.stderr)
        sys.exit(2)

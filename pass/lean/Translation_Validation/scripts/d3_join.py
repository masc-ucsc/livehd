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

CANONICAL JOINS REQUIRE `--meta`.  Without it nothing checks the artifact digest,
the manifest digest, the selftest brand, the drift marker, or that the rows are
the ones their sidecar vouches for.  A join run without `--meta` is a convenience
for looking at a table; it is NOT milestone evidence, and it says so.

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

# <ROOT>/pass/lean/Translation_Validation/scripts/<this file>: four levels down.
ROOT = pathlib.Path(__file__).resolve().parents[4]
GATES = ["cert", "compile", "reify", "typecheck", "sim", "checker", "agree", "proof"]
CREDITABLE = ["cert", "compile", "reify", "typecheck", "sim", "checker", "agree"]


class JoinError(Exception):
    pass


# What the sweep writes into `proof_max_rss_source` when the proof STAGE was
# stopped by the aggregate limit rather than finishing.  It is a field, not
# prose in `detail`, precisely so a reader does not have to grep English.
PROOF_RESOURCE_SRC = "sampled-aggregate-lower-bound"

PROOF_STATES = ("accepted", "accepted_unaudited", "refused", "resource",
                "unattempted", "no_certificate", "not_run", "uncreditable")


def proof_disposition(r: dict) -> tuple:
    """(proof, proof_status, axioms) for a COMPLETE result row.

    Four outcomes, and they are not orderings of one another:

    * `accepted`   -- the theorem closed AND the audited axiom set is recorded.
    * `refused`    -- the proof stage ran and the theorem did not close.
    * `resource`   -- the proof stage was stopped at a limit. UNDECIDED: it is
                      not a refusal, and the row says so rather than letting a
                      zero be read as one.
    * `unattempted`-- no proof stage was run for this row.

    Independent of `agree`.  A design can agree on 32 samples and have its
    theorem refused, or be proved and have been scheduled out of its executable
    stage; collapsing the two columns is how a sampled row starts being read as
    proved.

    `accepted_unaudited` exists for rows written before the sweep recorded the
    axiom set: the claim is there, the thing that authenticates it is not, so it
    is reported separately and NOT counted as accepted.
    """
    proof = (r.get("proof") or "na").strip()
    axioms = (r.get("proof_axioms") or "").strip()
    if proof == "1":
        return (proof, "accepted" if axioms else "accepted_unaudited", axioms)
    if proof == "0":
        # `proof_stop` is the sweep's own field for "the stage was stopped and
        # the theorem is undecided" -- a search budget, a timeout, a memory
        # limit. The RSS-source check stays beside it because a row written
        # before that field existed carries only the former.
        if (r.get("proof_stop") or "").strip():
            return (proof, "resource", axioms)
        if (r.get("proof_max_rss_source") or "").strip() == PROOF_RESOURCE_SRC:
            return (proof, "resource", axioms)
        return (proof, "refused", axioms)
    return ("na", "unattempted", axioms)


def corpus_origin(meta: dict, root: pathlib.Path) -> str:
    """Whether the certificates came from THIS worktree or were imported.

    The header asserted "IMPORTED CERTIFICATES: compatibility evidence only" on
    every join, including joins over a corpus this worktree emitted, where it
    says the opposite of the truth. The sidecar records the resolved `certs`
    paths, so this is derivable and does not need asserting.
    """
    # `certs` sits at the TOP of the sidecar, not under `config`; reading it from
    # the wrong level answered UNKNOWN for every join, which is a safe default
    # and still the wrong answer.
    certs = meta.get("certs") or []
    if not certs:
        return ("certificate origin UNKNOWN: the sidecar records no `certs` path, so "
                "whether this corpus was emitted here cannot be established.")
    inside = []
    for c in certs:
        try:
            pathlib.Path(c).resolve().relative_to(root.resolve())
            inside.append(True)
        except ValueError:
            inside.append(False)
    if all(inside):
        return ("certificates were EMITTED BY THIS WORKTREE; a gate passed here is "
                "end-to-end evidence for this branch.")
    if not any(inside):
        return ("IMPORTED CERTIFICATES: compatibility evidence only. These rows do NOT "
                "show this branch can generate the certificates end to end.")
    return ("MIXED certificate origin: some directories are inside this worktree and "
            "some are not, so the rows do not share one provenance.")


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
               "nodes": mrow.get("nodes", ""),
               # Filled from the RESULT row below. It was hardcoded `"na"` here
               # and never written again, so a design whose theorem was proved
               # and axiom-audited arrived in the milestone table as "no proof
               # was attempted" -- the join asserting the opposite of its own
               # input. See `proof_disposition`.
               "proof": "na", "proof_status": "", "proof_axioms": ""}

        if not available:
            # Explicit, and uncreditable. This is the row that stops 85/85 from
            # being read as 122/122.
            for g in CREDITABLE:
                row[g] = 0
            row["verdict"] = "no_certificate"
            row["detail"] = mrow.get("blocked_cause", "") or "no certificate"
            row["status"] = "blocked_upstream"
            row["proof_status"] = "no_certificate"
        elif r is None:
            for g in CREDITABLE:
                row[g] = 0
            row["verdict"] = "not_run"
            row["detail"] = "the manifest claims a certificate but the sweep has no row"
            row["status"] = "missing_result"
            row["proof_status"] = "not_run"
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
                # The row cannot be tied to the bytes it claims to describe, so
                # a `proof=1` on it is not creditable either -- the theorem was
                # about SOMETHING, and nothing here says what.
                #
                # `proof` is left EMPTY rather than `na` or `0`: "na" reads as
                # "not attempted" and "0" as "refused", and a proof that was
                # attempted and cannot be credited is neither. Empty means the
                # column cannot speak for this row; `proof_status` says why.
                row["proof"], row["proof_status"] = "", "uncreditable"
                row["detail"] = (f"missing certificate hash (manifest={want[:12] or 'none'}, "
                                 f"result={got[:12] or 'none'})")
                row["status"] = "unauthenticated"
                problems.append(f"{key}: missing certificate hash on "
                                f"{'manifest' if not want else 'result'} side")
            elif want != got:
                for g in CREDITABLE:
                    row[g] = 0
                row["verdict"] = "hash_mismatch"
                row["proof"], row["proof_status"] = "", "uncreditable"
                row["detail"] = (f"result was produced from {got[:12]}, "
                                 f"manifest records {want[:12]}")
                row["status"] = "stale_result"
                problems.append(f"{key}: certificate hash mismatch")
            elif (r.get("run_status") or "").strip() in ("deferred", "rss_killed",
                                                         "timeout"):
                # A LIMIT outcome, not a design result: the scheduler declined
                # to start it, or started it and killed it at a limit.  Either
                # way there is no COMPLETE probe result, so no milestone credit.
                #
                # Not "no gate was judged": a timed-out probe may legitimately
                # have emitted gate output before the kill, and the sweep KEEPS
                # it as raw observation.  What is missing is a finished run, and
                # that is what credit requires.  Crediting the partial row as
                # `ran` would read as "it got nowhere" when the truth is "we
                # stopped it" -- opposite implications for the milestone.
                for g in CREDITABLE:
                    row[g] = 0
                st = (r.get("run_status") or "").strip()
                row["verdict"] = st
                row["detail"] = r.get("detail", "") or f"{st} by the scheduler"
                row["status"] = st
                # The whole row was limit-stopped, so the proof stage was too --
                # or never started. Either way undecided, never a refusal.
                row["proof"], row["proof_status"], row["proof_axioms"] = (
                    (r.get("proof") or "na").strip(), "resource",
                    (r.get("proof_axioms") or "").strip())
                problems.append(f"{key}: {st}, no gate was judged")
            else:
                for g in CREDITABLE:
                    row[g] = 1 if str(r.get(g, "0")).strip() == "1" else 0
                row["verdict"] = r.get("verdict", "")
                row["detail"] = r.get("detail", "")
                row["status"] = "ran"
                row["proof"], row["proof_status"], row["proof_axioms"] = \
                    proof_disposition(r)
                for extra in ("tier", "distinct_obs", "wall_s", "module", "run_status"):
                    row[extra] = r.get(extra, "")
        out_rows.append(row)

    if orphan_candidates:
        problems.append(f"result rows no target claims: {sorted(orphan_candidates)}")

    if not a.meta:
        problems.append(
            "no --meta sidecar was given, so nothing verified the artifact digest, "
            "the manifest digest, the selftest brand, the drift marker, or the row "
            "bytes. This join is UNAUTHENTICATED and is not milestone evidence.")
    # Without --meta there is no sidecar, so the origin cannot be derived and
    # the header says UNKNOWN rather than asserting either answer.
    cfg = {}
    meta_doc = {}
    if a.meta:
        try:
            meta = json.loads(pathlib.Path(a.meta).read_text())
        except (OSError, json.JSONDecodeError) as e:
            raise JoinError(f"sidecar {a.meta} is unreadable: {e}")
        # The rows must be the rows this sidecar vouches for.  Both the runner
        # and the merger bind their output; a join that skipped the check would
        # be the one step in the chain where an edited table passes unnoticed.
        bound = (meta.get("results_sha256") or "").strip()
        if bound:
            actual = hashlib.sha256(res_path.read_bytes()).hexdigest()
            if actual != bound:
                raise JoinError(
                    f"{res_path.name} hashes to {actual[:16]} but its sidecar binds "
                    f"{bound[:16]}: the results table changed after the run or merge "
                    f"that wrote it.")
        else:
            problems.append(
                f"{a.meta}: no results_sha256, so the rows are UNBOUND; this join is "
                f"not authenticated and must not be cited as milestone evidence")
        meta_doc = meta
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
        if cfg.get("aborted_run"):
            # The generic marker.  A join is exactly where a void run's rows
            # would otherwise acquire the authority of a milestone table, so an
            # abort reason this code has never heard of must still refuse.
            raise JoinError(
                f"the sweep is marked aborted_run (reason "
                f"{cfg.get('aborted_reason')!r}, at {cfg.get('aborted_at_target')!r}): "
                f"the run was void and its rows cannot be joined or credited")
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
            + CREDITABLE + ["proof", "proof_status", "proof_axioms", "tier", "nodes",
                            "distinct_obs", "wall_s", "manifest_sha256", "detail"])
    digest = hashlib.sha256()
    for r in out_rows:
        digest.update(f"{r['target']}:{r['verdict']}:{r.get('agree', 0)}\n".encode())
    body = [
        f"# join of {res_path.name} onto {man_path.name}",
        f"# denominator = {len(man)} targets (one row each, including targets with "
        f"no certificate)",
        f"# {corpus_origin(meta_doc, ROOT)}",
        "# proof_status is INDEPENDENT of agree: accepted / accepted_unaudited / "
        "refused / resource (undecided) / unattempted / no_certificate / not_run / "
        "uncreditable.",
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
    tally = {k: 0 for k in PROOF_STATES}
    for r in out_rows:
        tally[r.get("proof_status") or "unattempted"] = \
            tally.get(r.get("proof_status") or "unattempted", 0) + 1
    print(f"  {'proof':<10} {tally['accepted']}/{len(out_rows)} accepted "
          f"(axiom set recorded)")
    for k in ("accepted_unaudited", "refused", "resource", "uncreditable"):
        if tally.get(k):
            print(f"  {'':<10} {tally[k]} {k}")
    print(f"  {'':<10} {tally.get('unattempted', 0)} unattempted, "
          f"{tally.get('no_certificate', 0)} no certificate, "
          f"{tally.get('not_run', 0)} not run")
    ndef = sum(1 for r in out_rows if r.get("status") in ("deferred", "rss_killed",
                                                         "timeout"))
    if ndef:
        print(f"  {ndef} target(s) hit a LIMIT (deferred / rss_killed / timeout) rather "
              f"than being judged; they are uncredited, carry zero milestone credit, "
              f"and the table is incomplete until they run. None is a design failure.")
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

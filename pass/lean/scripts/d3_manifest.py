#!/usr/bin/env python3
"""Build the Direction 3 TARGET MANIFESTS, with provenance and content digests.

Why this exists: a sweep over the certificates that happen to be on disk
measures the corpus, not the target.  85/85 and 46/46 read like completeness and
are not — 37 CORE-ET modules have no certificate at all.  A manifest with ONE
ROW PER REQUIRED TARGET, including the rows with nothing to run, is what keeps a
denominator from drifting to whatever was convenient to measure.

Two properties this file is strict about:

* **The canonical CVA6-30 is FROZEN, not recomputed.**  A rule like "the 30
  largest blocks that currently have a certificate" silently re-selects itself
  whenever the corpus changes, so a later run can show a better number because
  it quietly swapped the hard targets out.  The first run writes
  `cva6_30_frozen.tsv`; every later run READS it and verifies it, and changing
  the target set then requires editing a committed file.

* **Everything is digested.**  Per-file SHA256 plus a deterministic corpus
  digest, so a result table can be tied to the exact bytes it was produced from.

Usage: d3_manifest.py [--coreet-certs DIR] [--cva6-certs DIR] [--out DIR]
                      [--refreeze]
Exit 0 iff every assertion holds.
"""

import argparse
import csv
import hashlib
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_COREET = "/soe/czeng14/projects/livehd-new/generated/vc_sweep2/lean"
DEFAULT_CVA6 = "/soe/czeng14/projects/livehd-new/generated/cva6_vc/lean"
CVA6_PLAN = ROOT / "pass" / "lean" / "CVA6_COVERAGE_PLAN.md"
COREET_SWEEP = ROOT / "pass" / "lean" / "SWEEP_b1-b2.tsv"

EXPECT_COREET = 122
EXPECT_CVA6_BLOCKS = 78
EXPECT_CVA6_SELECTED = 30

SELECTION_RULE = (
    "the 30 CVA6 core-hierarchy blocks (of the authoritative 78 in "
    "CVA6_COVERAGE_PLAN.md Phase 2) that had a certificate when the list was "
    "first frozen, ranked by node count, largest first. Ranking by size keeps "
    "the selection re-derivable and biases it toward the hard cases. Once "
    "frozen the list is READ, never recomputed."
)


class ManifestError(Exception):
    pass


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def corpus_digest(files) -> str:
    """Order-independent digest of (name, sha256) pairs."""
    h = hashlib.sha256()
    for name, digest in sorted(files):
        h.update(f"{name}:{digest}\n".encode())
    return h.hexdigest()


def certs_in(d: pathlib.Path) -> dict:
    return {p.name[: -len("_Lgraph.lean")]: p for p in sorted(d.glob("*_Lgraph.lean"))}


def git(repo: pathlib.Path, *args) -> str:
    try:
        return subprocess.run(["git", "-C", str(repo), *args], capture_output=True,
                              text=True, timeout=30).stdout.strip()
    except Exception:
        return ""


def write_tsv(path: pathlib.Path, cols, rows, header_comments=()):
    with path.open("w", encoding="utf-8") as fh:
        for c in header_comments:
            fh.write(f"# {c}\n")
        fh.write("\t".join(cols) + "\n")
        for r in rows:
            fh.write("\t".join(str(r.get(c, "")) for c in cols) + "\n")


def coreet_manifest(certs_dir: pathlib.Path, out: pathlib.Path,
                    sweep_path: pathlib.Path = None) -> dict:
    certs = certs_in(certs_dir)
    rows = list(csv.DictReader((sweep_path or COREET_SWEEP).open(), delimiter="\t"))
    names = [r["module"] for r in rows]
    if len(names) != EXPECT_COREET:
        raise ManifestError(f"CORE-ET denominator is {len(names)}, expected {EXPECT_COREET}")
    dupes = sorted({n for n in names if names.count(n) > 1})
    if dupes:
        raise ManifestError(f"duplicate CORE-ET targets: {dupes}")
    orphans = sorted(set(certs) - set(names))
    if orphans:
        raise ManifestError(
            f"{len(orphans)} certificate(s) not named in the denominator: {orphans}")

    man = []
    for r in rows:
        m = r["module"]
        have = m in certs
        man.append({
            "module": m,
            "cert_available": 1 if have else 0,
            "certificate": certs[m].name if have else "",
            "sha256": sha256(certs[m]) if have else "",
            "emit_verdict": r.get("verdict", ""),
            "nodes": r.get("nodes", ""),
            "flops": r.get("flops", ""),
            "mems": r.get("memories", ""),
            "blocked_cause": "" if have else r.get("verdict", "unknown"),
        })
    cols = ["module", "cert_available", "certificate", "sha256", "emit_verdict",
            "nodes", "flops", "mems", "blocked_cause"]
    write_tsv(out / "coreet_122.tsv", cols, man, [
        f"CORE-ET target manifest: {len(man)} rows, one per module the sweep attempted.",
        "cert_available=0 rows have NO certificate and therefore no D3 gate can apply;",
        "blocked_cause records why emission failed, upstream of pass.lean.",
    ])
    have = sum(r["cert_available"] for r in man)
    digest = corpus_digest([(r["module"], r["sha256"]) for r in man if r["sha256"]])
    return {"targets": len(man), "with_cert": have, "digest": digest}


def cva6_manifests(certs_dir: pathlib.Path, out: pathlib.Path, refreeze: bool,
                   plan_path: pathlib.Path = None) -> dict:
    plan = (plan_path or CVA6_PLAN).read_text()
    sec = plan.split("### TOP LEVEL", 1)[1].split("\n## ", 1)[0]
    blocks = re.findall(r"^- `([A-Za-z0-9_]+)`", sec, re.M)
    if len(blocks) != EXPECT_CVA6_BLOCKS:
        raise ManifestError(
            f"CVA6 authoritative list is {len(blocks)}, expected {EXPECT_CVA6_BLOCKS}")
    dupes = sorted({b for b in blocks if blocks.count(b) > 1})
    if dupes:
        raise ManifestError(f"duplicate CVA6 blocks in the plan: {dupes}")

    emit = {}
    emit_tsv = certs_dir.parent / "emit.tsv"
    if emit_tsv.is_file():
        seen = []
        for r in csv.DictReader(emit_tsv.open(), delimiter="\t"):
            seen.append(r["module"])
            emit[r["module"]] = r
        dup_emit = sorted({m for m in seen if seen.count(m) > 1})
        if dup_emit:
            raise ManifestError(f"duplicate modules in emit.tsv: {dup_emit}")

    certs = certs_in(certs_dir)
    # Stripping `_gate` can collide: `foo` and `foo_gate` would both map to
    # `foo`, and one certificate would silently shadow the other.
    base = {}
    for c in certs:
        b = c[:-5] if c.endswith("_gate") else c
        if b in base:
            raise ManifestError(
                f"certificate name collision on block {b!r}: {base[b]!r} and {c!r}")
        base[b] = c
    stray = sorted(set(base) - set(blocks))
    if stray:
        raise ManifestError(f"certificate(s) outside the authoritative 78: {stray}")

    corpus = []
    for b, cert in sorted(base.items()):
        e = emit.get(b, {})
        corpus.append({
            "block": b, "certificate": cert,
            "sha256": sha256(certs[cert]),
            "nodes": e.get("nodes", ""), "sources": e.get("sources", ""),
            "flops": e.get("flops", ""), "mems": e.get("mems", ""),
        })
    cols = ["block", "certificate", "sha256", "nodes", "sources", "flops", "mems"]
    write_tsv(out / "cva6_corpus.tsv", cols, corpus, [
        f"CVA6 certificate corpus: {len(corpus)} blocks, all within the authoritative "
        f"{len(blocks)}.",
    ])

    # ---- the frozen NAME selection, and the live join against it -----------
    #
    # The freeze file carries the target NAMES.  Its size figures are recorded
    # with an `_at_freeze` suffix and are history, never current evidence: a
    # result table that quoted them would describe certificates that may since
    # have been regenerated.
    frozen_path = out / "cva6_30_frozen.tsv"
    frozen_cols = ["block", "nodes_at_freeze", "sha256_at_freeze"]
    if frozen_path.is_file() and not refreeze:
        frozen_names = [r["block"] for r in csv.DictReader(
            (l for l in frozen_path.open() if not l.startswith("#")), delimiter="\t")]
        created = False
    else:
        def nodes_of(r):
            try:
                return int(r["nodes"])
            except (ValueError, KeyError, TypeError):
                return -1
        pool = [r for r in corpus if nodes_of(r) >= 0]
        if len(pool) < EXPECT_CVA6_SELECTED:
            blank = [r["block"] for r in corpus if nodes_of(r) < 0]
            raise ManifestError(
                f"only {len(pool)} blocks have a usable node count "
                f"(blank: {blank}); cannot freeze {EXPECT_CVA6_SELECTED}")
        picked = sorted(pool, key=lambda r: (-nodes_of(r), r["block"]))[:EXPECT_CVA6_SELECTED]
        write_tsv(frozen_path, frozen_cols,
                  [{"block": r["block"], "nodes_at_freeze": r["nodes"],
                    "sha256_at_freeze": r["sha256"]} for r in picked], [
            "FROZEN canonical CVA6 target NAMES. Edit this file to change the targets;",
            "d3_manifest.py READS the names and never recomputes the selection.",
            "The `_at_freeze` columns are history. Current evidence lives in",
            "cva6_30.tsv, which is re-joined against the corpus on every run.",
            f"RULE used at freeze time: {SELECTION_RULE}",
            f"frozen on {time.strftime('%Y-%m-%d')} from {certs_dir}",
        ])
        frozen_names = [r["block"] for r in picked]
        created = True

    if len(frozen_names) != EXPECT_CVA6_SELECTED:
        raise ManifestError(
            f"frozen CVA6 set has {len(frozen_names)} names, expected {EXPECT_CVA6_SELECTED}")
    d = sorted({n for n in frozen_names if frozen_names.count(n) > 1})
    if d:
        raise ManifestError(f"duplicate blocks in the frozen set: {d}")
    outside = [n for n in frozen_names if n not in blocks]
    if outside:
        raise ManifestError(f"frozen blocks outside the authoritative 78: {outside}")

    by_block = {r["block"]: r for r in corpus}
    target30 = []
    for b in frozen_names:
        cur = by_block.get(b)
        if cur is None:
            # An explicit row, not a warning: a target that lost its certificate
            # must still occupy a line in the denominator.
            target30.append({"block": b, "cert_available": 0, "certificate": "",
                             "sha256": "", "nodes": "", "sources": "", "flops": "",
                             "mems": "",
                             "blocked_cause": emit.get(b, {}).get("emit", "no certificate")})
        else:
            target30.append({"block": b, "cert_available": 1,
                             "certificate": cur["certificate"], "sha256": cur["sha256"],
                             "nodes": cur["nodes"], "sources": cur["sources"],
                             "flops": cur["flops"], "mems": cur["mems"],
                             "blocked_cause": ""})
    t30_cols = ["block", "cert_available", "certificate", "sha256", "nodes", "sources",
                "flops", "mems", "blocked_cause"]
    target30_digest = corpus_digest(
        [(r["block"], f"{r['cert_available']}:{r['sha256']}") for r in target30])
    write_tsv(out / "cva6_30.tsv", t30_cols, target30, [
        "The canonical 30 CVA6 targets, joined against the CURRENT corpus.",
        "Names come from cva6_30_frozen.tsv; every other column is current evidence.",
        "The sweep/results join consumes THIS file, not the freeze file.",
        f"target30_sha256={target30_digest}",
    ])
    missing_now = [r["block"] for r in target30 if r["cert_available"] == 0]

    no_cert = sorted(set(blocks) - set(base))
    write_tsv(out / "cva6_no_certificate.tsv", ["block", "reason"],
              [{"block": b, "reason": emit.get(b, {}).get("emit", "not attempted in emit.tsv")}
               for b in no_cert],
              [f"{len(no_cert)} of the authoritative {len(blocks)} blocks have no certificate."])

    digest = corpus_digest([(r["block"], r["sha256"]) for r in corpus])
    return {"blocks": len(blocks), "corpus": len(corpus), "frozen": len(frozen_names),
            "frozen_created": created, "frozen_missing_cert": missing_now,
            "no_cert": len(no_cert), "digest": digest, "target30_digest": target30_digest}


def provenance(coreet: pathlib.Path, cva6: pathlib.Path, out: pathlib.Path,
               digests: dict) -> None:
    src_repo = pathlib.Path("/soe/czeng14/projects/livehd-new")
    rows = []
    for label, d in (("coreet", coreet), ("cva6", cva6)):
        files = sorted(d.glob("*_Lgraph.lean"))
        newest = max((f.stat().st_mtime for f in files), default=0)
        oldest = min((f.stat().st_mtime for f in files), default=0)
        rows.append({
            "corpus": label,
            "source_dir": str(d),
            "source_repo": str(src_repo),
            # NOT provenance: this is what the source repo is checked out at NOW,
            # which need not be, and here is not, the commit that produced these
            # files.  Recorded so a reader can see what was observed, labelled so
            # it cannot be mistaken for the generator.
            "observed_source_repo_head": git(src_repo, "rev-parse", "HEAD") or "unknown",
            "observed_source_repo_branch": git(src_repo, "rev-parse", "--abbrev-ref", "HEAD") or "unknown",
            "generator_commit": "unknown (pass.lean stamps no commit into the certificate)",
            "files": len(files),
            "corpus_sha256": digests.get(label, ""),
            "oldest_mtime": time.strftime("%Y-%m-%d %H:%M", time.localtime(oldest)) if oldest else "",
            "newest_mtime": time.strftime("%Y-%m-%d %H:%M", time.localtime(newest)) if newest else "",
            # NOT the commit that contains this file: it is generated before
            # that commit exists.  It records the worktree HEAD the generation
            # ran from, which is all it can honestly identify.
            "generation_worktree_base_head": git(ROOT, "rev-parse", "HEAD") or "unknown",
            "status": "IMPORTED: compatibility evidence only. Passing a gate here does NOT "
                      "show this branch can generate the certificate end to end.",
        })
    write_tsv(out / "PROVENANCE.tsv", list(rows[0].keys()), rows)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--coreet-certs", default=DEFAULT_COREET)
    ap.add_argument("--cva6-certs", default=DEFAULT_CVA6)
    ap.add_argument("--out", default=str(ROOT / "pass" / "lean" / "manifests"))
    ap.add_argument("--refreeze", action="store_true",
                    help="re-select the canonical 30 (changes the target set; use deliberately)")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    try:
        ce = coreet_manifest(pathlib.Path(a.coreet_certs), out)
        print(f"CORE-ET  targets={ce['targets']}  with certificate={ce['with_cert']}  "
              f"without={ce['targets'] - ce['with_cert']}")
        print(f"         corpus sha256={ce['digest'][:16]}...")

        cv = cva6_manifests(pathlib.Path(a.cva6_certs), out, a.refreeze)
        print(f"CVA6     authoritative={cv['blocks']}  with certificate={cv['corpus']}  "
              f"no certificate={cv['no_cert']}")
        print(f"         frozen target set={cv['frozen']} "
              f"({'CREATED' if cv['frozen_created'] else 'read from cva6_30_frozen.tsv'})")
        if cv["frozen_missing_cert"]:
            print(f"         WARNING: frozen targets with no certificate now: "
                  f"{cv['frozen_missing_cert']}")
        print(f"         corpus sha256={cv['digest'][:16]}...  "
              f"target30 sha256={cv['target30_digest'][:16]}...")

        provenance(pathlib.Path(a.coreet_certs), pathlib.Path(a.cva6_certs), out,
                   {"coreet": ce["digest"], "cva6": cv["digest"]})
    except ManifestError as e:
        print(f"MANIFEST ERROR: {e}", file=sys.stderr)
        return 1
    print(f"wrote manifests to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

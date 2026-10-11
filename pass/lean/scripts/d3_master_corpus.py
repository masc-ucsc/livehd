#!/usr/bin/env python3
"""Flatten a D4 emission run into a D3 certificate corpus, schema-checked.

Why this exists
---------------
The imported corpora named in pass/lean/manifests/PROVENANCE.tsv were emitted
before the D2 clock work.  Every one of them -- 85 CORE-ET and 46 CVA6 -- ends
in the pre-D2 trailer

    def X_step : RuntimeInput -> RuntimeState -> RuntimeResult :=
      compileAndRun X_designCert

and master's `compileAndRun` now takes a `ClockEdges` first, so the certificate
does not typecheck here.  The `DesignCert` literal itself is fine: `clocks`
defaults, which is why the cert and compile gates pass and only `typecheck`
refuses.  Those corpora are therefore COMPATIBILITY evidence that has reached
its limit, exactly as their provenance warned, and no D3 target can be credited
from them.

A corpus this branch can actually run is one THIS branch emitted.  The D4
procedures already produce it, one certificate per block under
`<run>/blocks/<block>/lean/<name>_Lgraph.lean`.  This script collects those into
the flat directory a manifest and a sweep expect.

Two gates, and the second one is the one that bites
---------------------------------------------------
* The certificate must be POST-D2: the emitted trailer applies `compileAndRun`
  at the `ClockEdges` arity and the `DesignCert` carries a `clocks` field.  The
  check is on the bytes, not on the run they came from, so one stale file in a
  fresh directory is refused here rather than surfacing 80 designs later as a
  verdict.

* The emission must have been ACCEPTED.  A D4 run writes a certificate for
  blocks it then rejects -- `EMIT_FAILED` with "certificate written and NOT
  run" is a real row in both corpora -- so a collector that trusts the file
  system picks up 4 CORE-ET and 4 CVA6 certificates that their own run refused.
  Acceptance comes from the run's `rows.tsv` (`emit_status=EMITTED`), keyed by
  the block DIRECTORY name, since a CVA6 certificate is `<block>_gate` and the
  row is `<block>`.  A block with no row at all is refused rather than skipped:
  unknown acceptance is not acceptance.

Layout
------
`--out DIR` is the corpus ROOT, not the certificate directory:

    DIR/lean/<name>_Lgraph.lean   the certificates, flat, as a sweep expects
    DIR/emit.tsv                  module/nodes/sources/flops/mems, from the
                                  emission run's own rows.tsv
    DIR/PROVENANCE.tsv            per-file sha256, the corpus digest, sources,
                                  and every block that was skipped, with why

`emit.tsv` sits beside `lean/` because that is where `d3_manifest.py` looks for
it, and without it a CVA6 manifest is written with blank node counts -- which a
sweep then refuses, one target at a time, for a reason that has nothing to do
with the target.

Usage:
  d3_master_corpus.py --from <d4-run-dir> [--from ...] --out <dir> [--force]
Exit 0 iff every certificate collected is post-D2, accepted, and the
destination matches.
"""

import argparse
import csv
import hashlib
import pathlib
import re
import sys

PRE_D2_STEP = re.compile(r"^\s*compileAndRun\s+(\w+)_designCert\s*$", re.M)
POST_D2_STEP = re.compile(
    r"^\s*compileAndRun\s+(\w+)_designCert\s+\(allEdges\s+\1_designCert\)\s*$", re.M)
CLOCKS_FIELD = re.compile(r"^\s*clocks\s*:?=", re.M)

EMIT_COLS = ["module", "nodes", "sources", "flops", "mems"]


def refuse(msg: str):
    """Every refusal leaves by the same door, with the same status.

    `raise SystemExit(str)` exits 1, which is the status a caller cannot tell
    from an ordinary failure; this script's contract is that 2 means it
    refused.
    """
    print(f"REFUSING: {msg}", file=sys.stderr)
    sys.exit(2)


def classify(text: str) -> str:
    """'post-d2', 'pre-d2', or a reason it is neither."""
    if POST_D2_STEP.search(text):
        return "post-d2" if CLOCKS_FIELD.search(text) else "no clocks field"
    if PRE_D2_STEP.search(text):
        return "pre-d2"
    return "no recognisable compileAndRun trailer"


def read_rows(rd: pathlib.Path):
    """(acceptance, emission records) from a run's rows.tsv.

    `memories` becomes `mems` here because that is the column `d3_manifest.py`
    reads; renaming at the boundary beats teaching one file the other's
    spelling.
    """
    rows_tsv = rd / "rows.tsv"
    if not rows_tsv.is_file():
        refuse(f"{rd} has no rows.tsv, so no block's emission can be shown "
               f"to have been accepted")
    accept, recs = {}, {}
    with rows_tsv.open(encoding="utf-8") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            m = (r.get("module") or "").strip()
            if not m:
                continue
            if m in accept:
                refuse(f"{rd}/rows.tsv names {m} twice")
            accept[m] = (r.get("emit_status") or "").strip()
            recs[m] = {"module": m, "nodes": r.get("nodes", ""),
                       "sources": r.get("sources", ""), "flops": r.get("flops", ""),
                       "mems": r.get("memories", "")}
    return accept, recs


def collect(run_dirs, out: pathlib.Path, force: bool) -> int:
    found, skipped, problems = {}, [], []
    emit_recs = {}

    for rd in run_dirs:
        blocks = rd / "blocks"
        if not blocks.is_dir():
            problems.append(f"{rd}: no blocks/ directory")
            continue
        accept, recs = read_rows(rd)
        for m, rec in recs.items():
            if m in emit_recs and emit_recs[m] != rec:
                problems.append(f"{m}: two different emission records across runs")
            emit_recs[m] = rec
        for cert in sorted(blocks.glob("*/lean/*_Lgraph.lean")):
            block = cert.parent.parent.name
            name = cert.name
            status = accept.get(block)
            if status is None:
                problems.append(f"{name}: block {block!r} has no row in {rd}/rows.tsv")
                continue
            if status != "EMITTED":
                skipped.append((name, block, f"emit_status={status}", str(cert)))
                continue
            if name in found:
                problems.append(f"{name}: offered by both {found[name][0]} and {cert}")
                continue
            text = cert.read_text(encoding="utf-8", errors="replace")
            kind = classify(text)
            if kind != "post-d2":
                problems.append(f"{name}: {kind} ({cert})")
                continue
            found[name] = (cert, cert.read_bytes())

    if problems:
        print("REFUSING to build a corpus:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 2

    leandir = out / "lean"
    leandir.mkdir(parents=True, exist_ok=True)
    written = kept = 0
    for name, (_src, data) in sorted(found.items()):
        dst = leandir / name
        if dst.exists():
            if dst.read_bytes() == data:
                kept += 1
                continue
            if not force:
                print(f"REFUSING: {dst} exists with different bytes; pass --force "
                      f"to replace it, and re-generate every manifest that pinned it",
                      file=sys.stderr)
                return 2
        dst.write_bytes(data)
        written += 1

    # A certificate that is no longer collected must not linger: the next
    # manifest would pin it and the sweep would run a target this corpus does
    # not claim.
    stale = sorted(p for p in leandir.glob("*_Lgraph.lean") if p.name not in found)
    if stale and not force:
        print(f"REFUSING: {len(stale)} certificate(s) in {leandir} are no longer "
              f"collected ({[p.name for p in stale][:4]}...); pass --force to remove "
              f"them", file=sys.stderr)
        return 2
    for p in stale:
        p.unlink()

    pairs = sorted((n, hashlib.sha256(d).hexdigest()) for n, (_s, d) in found.items())
    h = hashlib.sha256()
    for n, dg in pairs:
        h.update(f"{n}:{dg}\n".encode())

    emit_path = out / "emit.tsv"
    with emit_path.open("w", encoding="utf-8") as fh:
        fh.write("\t".join(EMIT_COLS) + "\n")
        for m in sorted(emit_recs):
            fh.write("\t".join(str(emit_recs[m][c]) for c in EMIT_COLS) + "\n")

    prov = out / "PROVENANCE.tsv"
    with prov.open("w", encoding="utf-8") as fh:
        fh.write("# Flattened from D4 emission runs by d3_master_corpus.py.\n")
        fh.write("# Collected iff the certificate is post-D2 AND its block's row in\n")
        fh.write("# the run's rows.tsv says emit_status=EMITTED. A certificate written\n")
        fh.write("# beside a failed emission is skipped and listed below, not collected.\n")
        for rd in run_dirs:
            fh.write(f"# source\t{rd}\n")
        fh.write(f"# corpus_sha256\t{h.hexdigest()}\n")
        fh.write(f"# files\t{len(pairs)}\n")
        fh.write(f"# skipped\t{len(skipped)}\n")
        for name, block, why, path in sorted(skipped):
            fh.write(f"# skipped_row\t{block}\t{name}\t{why}\t{path}\n")
        fh.write("certificate\tsha256\tsource\n")
        for n, dg in pairs:
            fh.write(f"{n}\t{dg}\t{found[n][0]}\n")

    print(f"corpus {leandir}: {len(pairs)} certificate(s) "
          f"({written} written, {kept} already identical, "
          f"{len(stale)} stale removed)")
    print(f"skipped       {len(skipped)} certificate(s) whose emission was not accepted")
    print(f"corpus_sha256 {h.hexdigest()}")
    print(f"emit.tsv      {emit_path} ({len(emit_recs)} row(s))")
    print(f"provenance    {prov}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="runs", action="append", required=True,
                    help="a D4 emission run directory holding blocks/<b>/lean/ "
                         "and rows.tsv")
    ap.add_argument("--out", required=True)
    ap.add_argument("--force", action="store_true",
                    help="replace a destination file whose bytes differ, and "
                         "remove certificates no longer collected")
    a = ap.parse_args()
    return collect([pathlib.Path(r).resolve() for r in a.runs],
                   pathlib.Path(a.out).resolve(), a.force)


if __name__ == "__main__":
    sys.exit(main())

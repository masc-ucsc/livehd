#!/usr/bin/env python3
"""Build the direct sweep's input manifest from the CONFIGURED record set.

Why this exists. The sweep used to be pointed at `replay/jobs` and told to walk
it. Measured, that is the wrong input in three separate ways:

  * it found 124 `DesignCert` files and **zero DINO** ones -- the replay's DINO
    jobs are `legacy_chunked_wf`/`legacy_full_bridge`, which emit a legacy
    `GraphCert`, so the three clock-aware DINO certificates were simply not
    discoverable there;
  * eight of those 124 are candidates the static gate REFUSED. A directory walk
    cannot tell them from accepted exports, so a direct checker accepting their
    payload would read as if the candidate had been admitted;
  * a walk keyed on file paths silently drops every configured case that has no
    usable `DesignCert` at all, so "ACCEPTED n/n" is computed over whatever
    happened to be on disk rather than over what was asked for.

So the manifest is built from the INVENTORY, every configured case is
classified -- including the ones with nothing to sweep -- and each entry
carries the identity and the hashes the sweep ran against.

Usage:
  sweep_manifest.py --out M.json [--inventory INV]... [--extra FAMILY=DIR]...
"""

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha(p):
    try:
        return hashlib.sha256(Path(p).read_bytes()).hexdigest()
    except OSError:
        return None


def has_design_cert(p):
    try:
        return "_designCert" in Path(p).read_text(errors="replace")
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--inventory", action="append", default=[], type=Path)
    ap.add_argument("--extra", action="append", default=[],
                    help="FAMILY=DIR -- a directory of current artifacts for a family "
                         "whose replay jobs carry no DesignCert (DINO). Recorded with "
                         "its own provenance, never presented as a replay record.")
    ap.add_argument("--fingerprint", type=Path,
                    default=ROOT / "generated" / "master_port" / "d2_runtime"
                                 / "library_fingerprint.txt")
    a = ap.parse_args()

    invs = a.inventory or [ROOT / "generated" / "master_port" / "replay" / "inventory.json"]
    results = ROOT / "generated" / "master_port" / "replay" / "results"

    entries = []
    for inv_path in invs:
        inv = json.loads(Path(inv_path).read_text())
        for j in inv["jobs"]:
            key = f"{j['family']}__{j['case']}__{j['mode']}"
            rp = results / f"{key}.json"
            rec = json.loads(rp.read_text()) if rp.exists() else {}
            e = {"family": j["family"], "case": j["case"], "top": j["top"],
                 "mode": j["mode"], "inventory": str(inv_path.name),
                 "export_state": rec.get("export_state", "(no record)"),
                 "proof_state": rec.get("proof_state", "(no record)")}
            # An EXPORTED artifact and a produced-then-REFUSED one are different
            # things and are never merged into one field.
            art = rec.get("artifact")
            refused_art = rec.get("artifact_produced_then_refused")
            path = art or refused_art
            if path and (ROOT / path).exists():
                full = ROOT / path
                e["artifact"] = path
                e["artifact_sha256"] = sha(full)
                side = full.with_name(f"{j['top']}_io.json")
                if side.exists():
                    e["sidecar"] = str(side.relative_to(ROOT))
                    e["sidecar_sha256"] = sha(side)
                # CLASSIFY ON THE STATE, NOT ON WHICH FIELD HELD THE PATH.
                #
                # The first version read `artifact` as "accepted" and
                # `artifact_produced_then_refused` as "refused". But the four
                # records migrated from the pre-rewrite run store a REFUSED
                # candidate's path in `artifact` -- they predate the second
                # field -- so they came through as `sweepable`. Four gate-refused
                # candidates would have been swept as accepted exports, which is
                # precisely the promotion this manifest exists to prevent.
                if not has_design_cert(full):
                    e["classification"] = "legacy_only"
                    e["why"] = "the artifact is a legacy GraphCert; it has no DesignCert to check"
                elif e["export_state"] == "EXPORTED":
                    e["classification"] = "sweepable"
                else:
                    e["classification"] = "gate_refused"
                    e["why"] = rec.get("detail", "not an accepted export")
                    e["gate_failures"] = rec.get("gate_failures", [])[:8]
                    if art and not refused_art:
                        e["path_field"] = ("recorded under `artifact` although the export was "
                                           f"{e['export_state']}; this record predates "
                                           "`artifact_produced_then_refused`")
            else:
                e["classification"] = "no_artifact"
                e["why"] = rec.get("detail") or ("not yet run" if not rec else "no artifact recorded")
            entries.append(e)

    for spec in a.extra:
        fam, _, d = spec.partition("=")
        if not d:
            sys.exit(f"--extra wants FAMILY=DIR, got {spec!r}")
        for f in sorted(Path(d).glob("*_Lgraph.lean")):
            top = f.name[: -len("_Lgraph.lean")]
            e = {"family": fam, "case": top, "top": top, "mode": "verified_compiler",
                 "inventory": f"--extra {d}", "artifact": str(f.resolve().relative_to(ROOT)),
                 "artifact_sha256": sha(f),
                 "export_state": "(outside the replay)", "proof_state": "(outside the replay)",
                 "classification": "sweepable" if has_design_cert(f) else "legacy_only",
                 "provenance": "supplied by --extra; not a replay record. Its freshness is "
                               "whatever produced that directory, recorded by hash here."}
            side = f.with_name(f"{top}_io.json")
            if side.exists():
                e["sidecar"] = str(side.resolve().relative_to(ROOT))
                e["sidecar_sha256"] = sha(side)
            entries.append(e)

    # DEDUPE BY PAYLOAD, KEEP EVERY CONFIGURED CASE. Two configurations can emit
    # a byte-identical certificate; the sweep should run it once, but neither
    # case may disappear from the ledger. So the duplicates are recorded as
    # aliases of the entry that will actually run.
    first = {}
    for e in entries:
        h = e.get("artifact_sha256")
        if not h or e["classification"] not in ("sweepable", "gate_refused"):
            continue
        tag = f"{e['family']}/{e['mode']}/{e['case']}"
        if h in first:
            first[h].setdefault("aliases", []).append(tag)
            e["duplicate_of"] = f"{first[h]['family']}/{first[h]['mode']}/{first[h]['case']}"
        else:
            first[h] = e

    counts = {}
    for e in entries:
        counts[e["classification"]] = counts.get(e["classification"], 0) + 1
    fam_cases = {}
    for e in entries:
        fam_cases.setdefault(e["family"], set()).add(e["case"])

    man = {
        "built_at": __import__("time").strftime("%Y-%m-%dT%H:%M:%SZ", __import__("time").gmtime()),
        "scope": ("every configured case, classified -- including the ones with nothing to "
                  "sweep. `sweepable` runs; `gate_refused` runs only as a separate "
                  "investigation and its result may NOT promote the export or any "
                  "RTL-alignment disposition; `legacy_only` and `no_artifact` are carried so "
                  "they are not silently dropped."),
        "configured_cases": {k: len(v) for k, v in sorted(fam_cases.items())},
        "counts": counts,
        "to_run": sorted(e["artifact"] for e in first.values()
                         if e["classification"] == "sweepable"),
        "investigate_only": sorted(e["artifact"] for e in first.values()
                                   if e["classification"] == "gate_refused"),
        "library_provenance": (a.fingerprint.read_text().splitlines()
                               if a.fingerprint.exists() else ["(no fingerprint recorded)"]),
        "entries": entries,
    }
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(man, indent=1) + "\n")
    print(f"configured cases: {man['configured_cases']}")
    print(f"classification  : {counts}")
    print(f"to run          : {len(man['to_run'])} unique payload(s)")
    print(f"investigate only: {len(man['investigate_only'])} gate-refused payload(s)")
    print(f"-> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

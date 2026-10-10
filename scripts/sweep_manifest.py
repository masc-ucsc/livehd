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


def verify_digest(recorded, current):
    """-> (state, detail). Never mints a digest from the file as it stands now.

    The orchestrator records a 16-hex PREFIX of sha256; this file computes the
    full digest. Comparing them means comparing prefixes, and the width
    difference is exactly why the first version compared nothing at all.
    """
    if not recorded or not isinstance(recorded, str):
        return "unrecorded", "the record carries no digest to check against"
    n = len(recorded)
    if current[:n] == recorded:
        return "match", ""
    return "mismatch", (f"recorded {recorded}, current {current[:n]} "
                        f"-- the bytes on disk are not the bytes the record describes")


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
    ap.add_argument("--results", type=Path,
                    help="results directory (default: the replay's). Lets a control drive "
                         "this CLI against its own fixtures instead of a reimplementation.")
    ap.add_argument("--fingerprint", type=Path,
                    default=ROOT / "generated" / "master_port" / "d2_runtime"
                                 / "library_fingerprint.txt")
    a = ap.parse_args()

    invs = a.inventory or [ROOT / "generated" / "master_port" / "replay" / "inventory.json"]
    results = a.results or (ROOT / "generated" / "master_port" / "replay" / "results")

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

                # THE FILE AS IT STANDS NOW IS NOT EVIDENCE OF WHAT WAS PROVED.
                #
                # This hashed the file and wrote that down. It never compared it
                # with the digest the EXPORT recorded -- the two are not even the
                # same width (the orchestrator stores a 16-hex prefix, this stores
                # 64), which is proof enough that no comparison was happening.
                #
                # Measured externally: flipping one 5-bit source constant from 20
                # to 19 left the manifest labelling the case `sweepable` and
                # carrying its PROVEN record, and the sweep then reported ACCEPTED
                # against the ORIGINAL artifact_sha256. A direct evaluator may
                # perfectly well admit the changed model; what it may not do is
                # attribute that run to the original source and proof.
                #
                # Absent provenance stays unresolved -- a missing recorded digest
                # is not a clean one, and it is never minted from the file now.
                # COMPARE AGAINST THE DIGEST THAT DESCRIBES *THIS* PATH. An
                # accepted export records `artifact_sha256`; a produced-then-
                # refused candidate records `artifact_produced_sha256`. Reading
                # the former for the latter reported five refusals as
                # "provenance unresolved" when their digest was recorded all
                # along -- an absent-evidence verdict minted by looking in the
                # wrong place.
                rec_digest = (rec.get("artifact_sha256") if art
                              else rec.get("artifact_produced_sha256"))
                prov = verify_digest(rec_digest, e["artifact_sha256"])
                e["artifact_provenance"] = prov[0]
                if prov[1]:
                    e["provenance_detail"] = f"artifact: {prov[1]}"
                if "sidecar" in e:
                    sp = verify_digest(rec.get("sidecar_sha256"), e["sidecar_sha256"])
                    e["sidecar_provenance"] = sp[0]
                    if sp[1]:
                        e["provenance_detail"] = ((e.get("provenance_detail", "") + "; ")
                                                  + f"sidecar: {sp[1]}").lstrip("; ")
                # CLASSIFY ON THE STATE, NOT ON WHICH FIELD HELD THE PATH.
                #
                # The first version read `artifact` as "accepted" and
                # `artifact_produced_then_refused` as "refused". But the four
                # records migrated from the pre-rewrite run store a REFUSED
                # candidate's path in `artifact` -- they predate the second
                # field -- so they came through as `sweepable`. Four gate-refused
                # candidates would have been swept as accepted exports, which is
                # precisely the promotion this manifest exists to prevent.
                # THE ARTIFACT'S DIGEST GOVERNS THE DISPOSITION; a sidecar that
                # was never recorded does not.
                #
                # A produced-then-refused candidate records `artifact_produced_
                # sha256` and no sidecar digest at all -- the orchestrator only
                # writes one on the EXPORTED path. Taking the worst of the two
                # therefore reported five refusals as "provenance unresolved"
                # although the artifact digest matched exactly. It is the
                # ARTIFACT that an export or proof disposition is attributed to.
                #
                # A sidecar that IS recorded and disagrees is still a mismatch:
                # that is contradicted evidence, not absent evidence.
                prov_state = e.get("artifact_provenance", "match")
                if e.get("sidecar_provenance") == "mismatch":
                    prov_state = "mismatch"
                if prov_state == "mismatch":
                    # A NAMED NON-SUCCESS. Not swept, not investigated, not
                    # silently dropped: the bytes on disk are not the bytes the
                    # record describes, and that has to be visible.
                    e["classification"] = "provenance_mismatch"
                    e["why"] = e.get("provenance_detail", "digest disagrees with the record")
                elif prov_state == "unrecorded":
                    e["classification"] = "provenance_unresolved"
                    e["why"] = e.get("provenance_detail", "the record carries no digest to check")
                elif not has_design_cert(full):
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

    # --extra IS A FALLBACK, AND ONLY A FALLBACK.
    #
    # The first version appended these unconditionally and relied on
    # content-hash dedupe to sort it out. That only works when the payloads are
    # byte-identical: if a FRESH replay export for the same family/case/mode
    # differs from the retained one at all -- which is the normal case, since a
    # fresh export has its own run id -- BOTH ran, under the same configured
    # identity, and the retained one was indistinguishable from a second
    # current configuration.
    #
    # So the current eligible record wins EXPLICITLY, by family/case/mode. A
    # retained artifact that loses is kept as historical evidence and labelled
    # as such; it is not run and is not a configuration.
    eligible = {(e["family"], e["case"], e["mode"]) for e in entries
                if e["classification"] == "sweepable"}
    for spec in a.extra:
        fam, _, d = spec.partition("=")
        if not d:
            sys.exit(f"--extra wants FAMILY=DIR, got {spec!r}")
        for f in sorted(Path(d).glob("*_Lgraph.lean")):
            top = f.name[: -len("_Lgraph.lean")]
            ident = (fam, top, "verified_compiler")
            e = {"family": fam, "case": top, "top": top, "mode": "verified_compiler",
                 "inventory": f"--extra {d}", "artifact": str(f.resolve().relative_to(ROOT)),
                 "artifact_sha256": sha(f),
                 "export_state": "(outside the replay)", "proof_state": "(outside the replay)",
                 "artifact_provenance": "unrecorded",
                 "provenance": ("supplied by --extra: a RETAINED artifact, not a replay "
                                "record. No export recorded its digest, so its provenance "
                                "is this hash and the directory it came from.")}
            side = f.with_name(f"{top}_io.json")
            if side.exists():
                e["sidecar"] = str(side.resolve().relative_to(ROOT))
                e["sidecar_sha256"] = sha(side)
            if ident in eligible:
                e["classification"] = "fallback_superseded"
                e["why"] = ("a current eligible export exists for this family/case/mode; "
                            "this retained artifact is kept as historical evidence and is "
                            "NOT run")
            elif has_design_cert(f):
                e["classification"] = "fallback_retained"
                e["why"] = ("no current eligible export for this family/case/mode, so this "
                            "RETAINED artifact is used -- and is labelled as retained, not "
                            "presented as a fresh export")
            else:
                e["classification"] = "legacy_only"
            entries.append(e)

    # DEDUPE BY PAYLOAD, KEEP EVERY CONFIGURED CASE. Two configurations can emit
    # a byte-identical certificate; the sweep should run it once, but neither
    # case may disappear from the ledger. So the duplicates are recorded as
    # aliases of the entry that will actually run.
    first = {}
    for e in entries:
        h = e.get("artifact_sha256")
        if not h or e["classification"] not in ("sweepable", "gate_refused",
                                                "fallback_retained"):
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
                         if e["classification"] in ("sweepable", "fallback_retained")),
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

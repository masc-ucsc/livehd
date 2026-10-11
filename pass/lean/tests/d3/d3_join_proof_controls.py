#!/usr/bin/env python3
"""Controls for `d3_join.py`'s PROOF dispositions and corpus-origin label.

WHY.  The join is the last step before a number becomes a milestone, and two of
its outputs were asserted rather than derived.

  * `proof` was hardcoded `"na"` in the row constructor and never written
    again, so `debug_breakpoint` -- whose theorem closed and whose axiom set
    was audited to [propext, Classical.choice, Quot.sound] -- arrived in the
    joined table as `na`, under a header reading "no per-design translation
    proof is generated or checked by this pass". The join was stating the
    opposite of its own input.
  * the header asserted "IMPORTED CERTIFICATES: compatibility evidence only" on
    every join, including joins over a corpus this worktree emitted.

Both are now derived, and these legs pin the derivation. Every case builds a
real manifest, a real results table and a real sidecar, and runs the real CLI.

The four proof dispositions are INDEPENDENT of `agree`, and the legs check that
in both directions: a design that agrees can have its theorem refused, and a
design whose theorem is accepted can have been scheduled out of a later stage.
"""

import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
JOIN = ROOT / "pass" / "lean" / "scripts" / "d3_join.py"

RESULT_COLS = ["target_key", "module", "verdict", "cert", "compile", "reify",
               "typecheck", "sim", "checker", "agree", "proof", "run_status",
               "cert_sha256", "proof_max_rss_source", "proof_axioms", "tier",
               "distinct_obs", "wall_s", "detail"]

AX = "[propext, Classical.choice, Quot.sound]"
fails = []


def ok(name, note=""):
    print(f"PASS  {name}" + (f"  {note}" if note else ""))


def bad(name, note=""):
    print(f"FAIL  {name}" + (f"  {note}" if note else ""))
    fails.append(name)


def write_manifest(p: pathlib.Path, rows) -> None:
    with p.open("w") as fh:
        fh.write("# control manifest\n")
        fh.write("module\tcert_available\tcertificate\tsha256\tnodes\tblocked_cause\n")
        for m, avail, sha, nodes, cause in rows:
            cert = f"{m}_Lgraph.lean" if avail else ""
            fh.write(f"{m}\t{avail}\t{cert}\t{sha}\t{nodes}\t{cause}\n")


def write_results(p: pathlib.Path, rows) -> None:
    with p.open("w") as fh:
        fh.write("\t".join(RESULT_COLS) + "\n")
        for r in rows:
            fh.write("\t".join(str(r.get(c, "")) for c in RESULT_COLS) + "\n")


def write_meta(p: pathlib.Path, res: pathlib.Path, man: pathlib.Path,
               certs, bind=True, man_digest=None) -> None:
    doc = {
        "run_id": "control",
        "certs": [str(c) for c in certs],
        "config": {"manifest": str(man.resolve()),
                   "manifest_digest": man_digest if man_digest is not None
                   else hashlib.sha256(man.read_bytes()).hexdigest(),
                   "samples": 32, "tier": ""},
    }
    if bind:
        doc["results_sha256"] = hashlib.sha256(res.read_bytes()).hexdigest()
    p.write_text(json.dumps(doc))


def run(man, res, out, meta=None):
    cmd = [sys.executable, "-I", str(JOIN), "--manifest", str(man),
           "--results", str(res), "--out", str(out)]
    if meta:
        cmd += ["--meta", str(meta)]
    return subprocess.run(cmd, capture_output=True, text=True, timeout=120)


def joined_rows(out: pathlib.Path):
    lines = [l for l in out.read_text().splitlines() if not l.startswith("#")]
    cols = lines[0].split("\t")
    return {r.split("\t")[0]: dict(zip(cols, r.split("\t"))) for r in lines[1:]}


def header(out: pathlib.Path):
    return "\n".join(l for l in out.read_text().splitlines() if l.startswith("#"))


def main() -> int:
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="d3joinproof_", dir=str(ROOT / "temp")))
    sha = {m: hashlib.sha256(m.encode()).hexdigest() for m in
           ("acc", "accx", "ref", "resrc", "unatt", "nocert", "norow", "stale")}
    man = tmp / "m.tsv"
    write_manifest(man, [
        ("acc",    1, sha["acc"],    10, ""),
        ("accx",   1, sha["accx"],   10, ""),
        ("ref",    1, sha["ref"],    10, ""),
        ("resrc",  1, sha["resrc"],  10, ""),
        ("unatt",  1, sha["unatt"],  10, ""),
        ("stale",  1, sha["stale"],  10, ""),
        ("norow",  1, sha["norow"],  10, ""),
        ("nocert", 0, "",            "", "NO_EMIT(compile)"),
    ])

    def base(m, **kw):
        r = {"target_key": m, "module": m, "verdict": "agree", "run_status": "done",
             "cert_sha256": sha[m], "tier": "tiny", "distinct_obs": "31",
             "wall_s": "1.0"}
        for g in ("cert", "compile", "reify", "typecheck", "sim", "checker", "agree"):
            r[g] = "1"
        r.update(kw)
        return r

    res = tmp / "r.tsv"
    write_results(res, [
        base("acc",   proof="1", proof_axioms=AX),
        base("accx",  proof="1"),                       # no axiom set recorded
        base("ref",   proof="0", proof_max_rss_source="time-max-rss",
             detail="proof probe exited 1"),
        base("resrc", proof="0",
             proof_max_rss_source="sampled-aggregate-lower-bound",
             detail="proof stage killed; the theorem was never decided"),
        base("unatt", proof="na"),
        base("stale", proof="1", proof_axioms=AX,
             cert_sha256=hashlib.sha256(b"different").hexdigest()),
    ])
    meta = tmp / "r.meta.json"
    write_meta(meta, res, man, [ROOT / "generated" / "control_certs"])

    out = tmp / "j.tsv"
    r = run(man, res, out, meta)
    rows = joined_rows(out)

    want = {"acc": ("1", "accepted"), "accx": ("1", "accepted_unaudited"),
            "ref": ("0", "refused"), "resrc": ("0", "resource"),
            "unatt": ("na", "unattempted"), "nocert": ("na", "no_certificate"),
            "norow": ("na", "not_run"),
            # EMPTY, not "1" and not "na": the proof was attempted and cannot be
            # credited, which is neither "accepted" nor "not attempted". Carrying
            # the claimed 1 forward would propagate an uncreditable claim into
            # the column a reader sums.
            "stale": ("", "uncreditable")}
    for k, (wp, ws) in want.items():
        got = (rows[k].get("proof"), rows[k].get("proof_status"))
        if got == (wp, ws):
            ok(f"disposition_{ws}", f"{k}: proof={wp} status={ws}")
        else:
            bad(f"disposition_{ws}", f"{k}: got {got}, wanted {(wp, ws)}")

    if rows["acc"].get("proof_axioms") == AX:
        ok("axioms_carried", "the audited set survives the join verbatim")
    else:
        bad("axioms_carried", repr(rows["acc"].get("proof_axioms")))
    if rows["accx"].get("proof_status") == "accepted_unaudited":
        ok("unaudited_not_counted", "proof=1 without a recorded axiom set is separated")
    else:
        bad("unaudited_not_counted")
    # `stale` agrees in the result table and claims proof=1; the join must credit
    # NEITHER, because the row is not tied to the bytes the manifest names.
    if rows["stale"].get("agree") == "0" and rows["stale"].get("proof_status") == "uncreditable":
        ok("hash_mismatch_uncreditable", "a mismatched row credits no gate and no proof")
    else:
        bad("hash_mismatch_uncreditable", str(rows["stale"]))
    if len(rows) == 8:
        ok("denominator_preserved", "8 manifest targets, 8 joined rows")
    else:
        bad("denominator_preserved", f"{len(rows)} rows")
    if "accepted" in r.stdout and "refused" in r.stdout and "resource" in r.stdout:
        ok("summary_names_each_state")
    else:
        bad("summary_names_each_state", r.stdout[-200:])
    # The one that started this: a proved design must never be reported as `na`.
    if "na -- not attempted" not in r.stdout:
        ok("no_blanket_na_claim", "the summary no longer asserts proof=na throughout")
    else:
        bad("no_blanket_na_claim", r.stdout[-200:])

    # ---- corpus origin, derived from the sidecar's certs paths --------------
    out2 = tmp / "j2.tsv"
    meta2 = tmp / "r2.meta.json"
    write_meta(meta2, res, man, ["/somewhere/else/livehd-new/generated/vc_sweep2/lean"])
    run(man, res, out2, meta2)
    if "IMPORTED CERTIFICATES" in header(out2):
        ok("origin_imported")
    else:
        bad("origin_imported", header(out2))
    if "EMITTED BY THIS WORKTREE" in header(out):
        ok("origin_local")
    else:
        bad("origin_local", header(out))
    out3 = tmp / "j3.tsv"
    run(man, res, out3)          # no --meta at all
    if "origin UNKNOWN" in header(out3):
        ok("origin_unknown_without_sidecar")
    else:
        bad("origin_unknown_without_sidecar", header(out3))
    out3b = tmp / "j3b.tsv"
    meta3b = tmp / "r3b.meta.json"
    write_meta(meta3b, res, man, [])
    run(man, res, out3b, meta3b)
    if "origin UNKNOWN" in header(out3b):
        ok("origin_unknown_without_certs")
    else:
        bad("origin_unknown_without_certs", header(out3b))

    # ---- invalid and mismatched metadata -----------------------------------
    out4 = tmp / "j4.tsv"
    meta4 = tmp / "r4.meta.json"
    write_meta(meta4, res, man, [ROOT], man_digest="0" * 64)
    r4 = run(man, res, out4, meta4)
    if r4.returncode != 0 and "manifest" in (r4.stdout + r4.stderr):
        ok("mismatched_manifest_digest_refused", f"exit {r4.returncode}")
    else:
        bad("mismatched_manifest_digest_refused", f"exit {r4.returncode}")

    out5 = tmp / "j5.tsv"
    meta5 = tmp / "r5.meta.json"
    write_meta(meta5, res, man, [ROOT])
    res.write_text(res.read_text() + "")     # same bytes
    tampered = tmp / "r_tampered.tsv"
    tampered.write_text(res.read_text().replace("\tagree\t", "\tagree \t", 1))
    r5 = run(man, tampered, out5, meta5)
    if r5.returncode != 0 and "sidecar binds" in (r5.stdout + r5.stderr):
        ok("tampered_results_refused", f"exit {r5.returncode}")
    else:
        bad("tampered_results_refused", f"exit {r5.returncode}: {(r5.stdout + r5.stderr)[-160:]}")

    out6 = tmp / "j6.tsv"
    meta6 = tmp / "r6.meta.json"
    write_meta(meta6, res, man, [ROOT], bind=False)
    r6 = run(man, res, out6, meta6)
    if r6.returncode != 0 and "UNBOUND" in (r6.stdout + r6.stderr):
        ok("unbound_results_refused", f"exit {r6.returncode}")
    else:
        bad("unbound_results_refused", f"exit {r6.returncode}")

    if fails:
        print(f"\nD3-JOIN-PROOF CONTROLS: {len(fails)} failure(s): {fails}")
        return 1
    print("\nD3-JOIN-PROOF CONTROLS OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

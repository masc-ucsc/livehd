#!/usr/bin/env python3
"""Install the pinned Futamura-projection contribution into tracked master paths.

The source is READ-ONLY: /mada/users/czeng14/projects/livehd-futamura/livehd at
`9bb41a09d6b57111ed2b3fd10102ec4ff174b6c6`.  The asset list and the pinned
sha256 of every file come from the recorded baseline packet, not from walking
the tree -- a walk picks up whatever happens to be there, and the point of a
pinned contribution is that the bytes are named in advance.

Every file is verified against its pinned digest BEFORE it is copied.  A
mismatch refuses the whole install rather than landing part of it: a half
installed contribution is harder to reason about than none.

Adaptation is recorded, never silent.  The map this writes is the artifact --
one row per asset, with the pinned digest, the digest actually installed,
whether it was adapted, and why.  An unadapted row has identical digests, so
"verbatim" is checkable rather than asserted.

Usage:
  fp_install.py --baseline <result.json> --source <dir> --out <map.tsv> [--apply]
Without --apply it reports what it would do and writes nothing.
"""

import argparse
import hashlib
import json
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]

# The ONE adaptation this checkpoint makes, and the reason it is forced.
#
# FP's `Projection/CertLoad.lean` opens `Compiler.CertIO`, which on the FP
# branch is the DCERT1 codec.  On master that name is the DCERT2 codec and FP's
# codec lives beside it as `Compiler.CertIO.V1` (ledger S22).  Left alone,
# CertLoad would silently get the DCERT2 contract -- and its own DCERT1 grammar
# guard, `#guard (parseCert "DCERT1\n0\n0\n0\n0\n0\n".toUTF8).toOption.isSome`,
# would fail, because DCERT2 refuses that magic.
#
# Repointing to V1 keeps the HISTORICAL contract historical: the guards still
# test the grammar they were written for, instead of becoming a wrong-magic
# rejection that tests nothing.  The fresh DCERT2 transport is a SEPARATE
# obligation and is not discharged by this.
#
# The SECOND adaptation, and the reason it is forced.
#
# `OperatorBridge` carries three `rfl` theorems named `..._unchanged_by_overlay`.
# Their job is to certify that FP's `bv_shl_step` overlay did not change the
# operator semantics, and they do it by asserting `eval_op`/`denote_op` equal a
# fold over the UNGUARDED `2 ^ k` formula -- which is what the pinned shared
# base computed.
#
# Master's `Op_SHL` saturates at the result width, so those three are FALSE here
# and the build says so by name. That is the tripwire working, not a defect in
# it: the semantics genuinely differ from the base FP overlaid.
#
# The overlay on master is inert in the strongest sense -- a definition is added
# and no call site is touched, so `eval_op` is byte-identical to before. The
# statement that says so here names master's step, `bv_shl_step`, instead of the
# base's formula. Deleting master's guard to make FP's original statement true
# would be the R46 regression a second time.
ADAPTATIONS = {
    # The build driver is itself a pinned asset, so its master adaptation has to
    # live here or a re-install silently reverts it -- which is exactly what
    # happened once: `Compiler/CertIOV1` vanished from the list and the whole
    # CertLoad chain went back to unresolvable.
    "formal/lean/.build-proj.sh": [
        ("  Compiler/CertIO\n  Projection/ObjectLanguage",
         "  # Compiler/CertIO is NOT built here. On the FP branch it was the DCERT1\n"
         "  # codec with no heavy dependencies; master's is DCERT2 and imports\n"
         "  # Compiler/CompileDesign, whose whole chain imports Mathlib. This driver\n"
         "  # is deliberately \"no lake, no Mathlib\" -- it excludes Translation/OpBridge\n"
         "  # for the same reason -- so DCERT2 is out of its scope and belongs to\n"
         "  # `lake build`. Nothing in the Projection contribution needs it:\n"
         "  # Projection/CertLoad takes the V1 route.\n"
         "  Compiler/CertIOV1\n"
         "  Projection/ObjectLanguage",
         "master's Compiler/CertIO is DCERT2 and needs Mathlib, which this driver "
         "excludes by design; CertIOV1 is what CertLoad resolves against"),
    ],
    "formal/lean/LeanSemanticPrimitives/Projection/OperatorBridge.lean": [
        ("""theorem bv_shl_step_unfolds (w : Nat) (a b : BV) :
    bv_shl_step w a b = mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat) := rfl""",
         """theorem bv_shl_step_unfolds (w : Nat) (a b : BV) :
    bv_shl_step w a b
      = mk_bv w (if (bv_uint b).toNat \u2265 w then 0
                 else bv_uint a * (2 : Int) ^ (bv_uint b).toNat) := rfl""",
         "master's Op_SHL saturates at the result width; FP's unguarded unfolding "
         "is false here"),
        ("""    eval_op .Op_SHL w (a :: bs)
      = bs.foldl (fun acc b =>
          bv_bitwise w (fun x y => xor x y) acc
            (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat))) (mk_bv w 0) := rfl""",
         """    eval_op .Op_SHL w (a :: bs)
      = bs.foldl (fun acc b =>
          bv_bitwise w (fun x y => xor x y) acc
            (bv_shl_step w a b)) (mk_bv w 0) := rfl""",
         "same: the overlay is inert on master, and this is the statement that "
         "says so against master's step"),
        ("""    denote_op .Op_SHL w (a :: bs)
      = bs.foldl (fun acc b =>
          bv_bitwise w (fun x y => xor x y) acc
            (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat))) (mk_bv w 0) := rfl""",
         """    denote_op .Op_SHL w (a :: bs)
      = bs.foldl (fun acc b =>
          bv_bitwise w (fun x y => xor x y) acc
            (bv_shl_step w a b)) (mk_bv w 0) := rfl""",
         "same, for denote_op"),
    ],
    "formal/lean/LeanSemanticPrimitives/Projection/CertLoad.lean": [
        ("import LeanSemanticPrimitives.Compiler.CertIO",
         "import LeanSemanticPrimitives.Compiler.CertIOV1",
         "master's Compiler.CertIO is DCERT2; FP's codec is Compiler.CertIO.V1"),
        ("open Compiler Compiler.CertIO Projection.Hw",
         "open Compiler Compiler.CertIO.V1 Projection.Hw",
         "same: the V1 route, so the DCERT1 grammar guards still test DCERT1"),
    ],
}

COLS = ["asset", "pinned_sha256", "source_sha256", "installed_sha256", "verbatim",
        "adapted_edits", "adaptation_reason", "state"]


def sha256(p: pathlib.Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--apply", action="store_true")
    a = ap.parse_args()

    doc = json.loads(pathlib.Path(a.baseline).read_text())
    assets = doc.get("assets") or []
    if not assets:
        print("REFUSING: the baseline packet lists no assets", file=sys.stderr)
        return 2
    src_root = pathlib.Path(a.source).resolve()

    problems, rows = [], []
    for rec in sorted(assets, key=lambda r: r["path"]):
        rel = rec["path"]
        want = (rec.get("pinned_sha256") or "").strip()
        src = src_root / rel
        if not src.is_file():
            problems.append(f"{rel}: absent from the pinned source tree")
            continue
        got = sha256(src)
        if want and got != want:
            problems.append(f"{rel}: source is {got[:12]}, the baseline pins {want[:12]}")
            continue
        rows.append({"asset": rel, "pinned_sha256": want, "source_sha256": got})

    if problems:
        print("REFUSING to install:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 2

    installed = adapted = 0
    for r in rows:
        rel = r["asset"]
        dst = ROOT / rel
        edits = ADAPTATIONS.get(rel, [])
        text = (src_root / rel).read_bytes()
        reasons = []
        if edits:
            s = text.decode("utf-8")
            applied = 0
            for old, new, why in edits:
                if s.count(old) != 1:
                    problems.append(f"{rel}: adaptation anchor {old!r} appears "
                                    f"{s.count(old)} times, expected 1")
                    continue
                s = s.replace(old, new, 1)
                applied += 1
                reasons.append(why)
            text = s.encode("utf-8")
            r["adapted_edits"] = applied
        else:
            r["adapted_edits"] = 0
        r["adaptation_reason"] = "; ".join(reasons)
        r["installed_sha256"] = hashlib.sha256(text).hexdigest()
        r["verbatim"] = "1" if r["installed_sha256"] == r["source_sha256"] else "0"
        if a.apply:
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(text)
            if (src_root / rel).stat().st_mode & 0o111:
                dst.chmod(dst.stat().st_mode | 0o755)
            r["state"] = "installed"
            installed += 1
            if r["adapted_edits"]:
                adapted += 1
        else:
            r["state"] = "would_install"

    if problems:
        print("REFUSING: adaptation anchors did not match:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 2

    out = pathlib.Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8") as fh:
        fh.write(f"# source\t{src_root}\n")
        fh.write(f"# pin\t{doc.get('pin','')}\n")
        fh.write(f"# baseline\t{pathlib.Path(a.baseline).resolve()}\n")
        fh.write("# verbatim=1 means the installed bytes equal the pinned source bytes.\n")
        fh.write("# An adapted row records every edit and why it was forced.\n")
        fh.write("\t".join(COLS) + "\n")
        for r in rows:
            fh.write("\t".join(str(r.get(c, "")) for c in COLS) + "\n")

    verb = sum(1 for r in rows if r["verbatim"] == "1")
    print(f"assets {len(rows)}: {verb} verbatim, {len(rows) - verb} adapted"
          + (f"; installed {installed} ({adapted} adapted)" if a.apply else "; DRY RUN"))
    print(f"map {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

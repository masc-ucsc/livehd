#!/usr/bin/env python3
"""Install the pinned Futamura-projection contribution into tracked master paths.

The source is READ-ONLY: /WORKROOT/projects/livehd-futamura/livehd at
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

ROOT = pathlib.Path(__file__).resolve().parents[4]

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
# Master's `Op_SHL` saturates at the result width, so those three do not close
# by `rfl` here -- and I first read that as the statements being FALSE on
# master. They are not. A `rfl` failure is a failure of definitional reduction;
# `2 ^ w` divides `u * 2 ^ k` whenever `k >= w`, so the guarded and unguarded
# terms are the same value, and all three ORIGINAL statements are proved
# (`shl_step_eq`, `shl_fold_eq`). The same holds for `evalOp_Sext`.
#
# So the adaptation here is PROOF-ONLY: every statement is the pinned one,
# verbatim, and only the proof terms change. The overlay on master is inert in
# the strongest sense -- a definition is added and no call site is touched, so
# `eval_op` is byte-identical to before. Deleting master's guard to force FP's
# `rfl` through would be the R46 regression a second time.
HELPER_WHY = ("master's eval_op carries two boundary repairs, so the pinned rfl "
              "proofs do not reduce; the equalities still hold and these lemmas "
              "prove them from core lemmas only")

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
    "formal/lean/scripts/record_experiment.py": [
        ("""    scratch = os.path.abspath(os.path.join(LEAN, '..', '..', '..',
                                           '.perfwork', 'recorder-tests'))""",
         """    # ADAPTED PATH, same contract. On the FP branch `formal/lean/../../..`
    # landed beside the repository; in this worktree the same arithmetic lands
    # in `projects/`, OUTSIDE the tree, and the workspace rule is that runtime
    # scratch stays inside the project. The refusal below is untouched: the
    # directory is still created and a failure still raises rather than falling
    # back to /tmp.
    scratch = os.path.abspath(os.path.join(LEAN, '..', '..',
                                           'generated', 'master_port', 'fp',
                                           '.perfwork', 'recorder-tests'))""",
         "scratch must stay inside the worktree; the no-/tmp refusal is unchanged"),
    ],
    "formal/lean/scripts/build-opt.sh": [
        ("  Compiler/DesignSemantics Compiler/CertIO\n",
         "  # master's Compiler/CertIO is DCERT2 and its chain imports Mathlib, which\n"
         "  # this route does not build. The historical V1 runners take CertIOV1, the\n"
         "  # same reconciliation CertLoad and the probes take. Dependency ORDER and\n"
         "  # the invalidation rule below are unchanged; nothing is deleted.\n"
         "  Compiler/DesignSemantics Compiler/CertIOV1\n",
         "historical V1 route for the optimised build's shared dependency list"),
    ],
    "formal/lean/scripts/build-native.sh": [
        ("  Compiler/CertIO\n",
         "  Compiler/CertIOV1\n",
         "same reconciliation, for the unoptimised native route"),
    ],
    "formal/lean/scripts/proto_probe.lean": [
        ("import LeanSemanticPrimitives.Compiler.CertIO",
         "import LeanSemanticPrimitives.Compiler.CertIOV1",
         "master's Compiler.CertIO is DCERT2 and needs Mathlib, which .build-proj.sh "
         "excludes; the historical runners take the V1 route, as CertLoad does"),
        ("CertIO.loadCert", "CertIO.V1.loadCert",
         "same transport, at the call site", 9),
    ],
    "formal/lean/scripts/s2_probe.lean": [
        ("import LeanSemanticPrimitives.Compiler.CertIO",
         "import LeanSemanticPrimitives.Compiler.CertIOV1",
         "master's Compiler.CertIO is DCERT2 and needs Mathlib, which .build-proj.sh "
         "excludes; the historical runners take the V1 route, as CertLoad does"),
        ("CertIO.loadCert", "CertIO.V1.loadCert",
         "same transport, at the call site", 1),
    ],
    "formal/lean/scripts/s2a_probe.lean": [
        ("import LeanSemanticPrimitives.Compiler.CertIO",
         "import LeanSemanticPrimitives.Compiler.CertIOV1",
         "master's Compiler.CertIO is DCERT2 and needs Mathlib, which .build-proj.sh "
         "excludes; the historical runners take the V1 route, as CertLoad does"),
        ("CertIO.loadCert", "CertIO.V1.loadCert",
         "same transport, at the call site", 1),
    ],
    "formal/lean/scripts/s3_probe.lean": [
        ("import LeanSemanticPrimitives.Compiler.CertIO",
         "import LeanSemanticPrimitives.Compiler.CertIOV1",
         "master's Compiler.CertIO is DCERT2 and needs Mathlib, which .build-proj.sh "
         "excludes; the historical runners take the V1 route, as CertLoad does"),
        ("CertIO.loadCert", "CertIO.V1.loadCert",
         "same transport, at the call site", 2),
    ],
    "formal/lean/scripts/total_probe.lean": [
        ("import LeanSemanticPrimitives.Compiler.CertIO",
         "import LeanSemanticPrimitives.Compiler.CertIOV1",
         "master's Compiler.CertIO is DCERT2 and needs Mathlib, which .build-proj.sh "
         "excludes; the historical runners take the V1 route, as CertLoad does"),
        ("CertIO.loadCert", "CertIO.V1.loadCert",
         "same transport, at the call site", 1),
    ],
    "formal/lean/LeanSemanticPrimitives/Projection/OperatorBridge.lean": [
        # PROOF-ONLY. Every statement below is the pinned FP statement verbatim.
        # Three were `rfl` and one closed with `split <;> rfl`; master's eval_op
        # does not REDUCE to the pinned base's shape because it carries two
        # boundary repairs. Both equalities nonetheless HOLD, and are proved.
        ("theorem bv_shl_step_unfolds", "/-! ### Arithmetic the bridge needs, and the record this corrects\n\nMASTER ADAPTATION, PROOF ONLY.  Every theorem statement below this block is the\npinned FP statement, unchanged.  What changed is that three of them were `rfl`\nand one closed with `split <;> rfl`, and master's `eval_op` does not REDUCE to\nthe pinned base's shape -- it carries two boundary repairs the base did not\nhave.\n\nA `rfl` that fails is a failure of definitional reduction.  It is not evidence\nthat the equality is false, and I recorded it as such once; it is not.  Both\nequalities hold here and are proved below, from core lemmas only -- this file\nis built by `.build-proj.sh`, which has no Mathlib.\n\n`Op_SHL`: master guards the fold term with `if k >= w then 0`.  That is\nEXTENSIONALLY the same value, because `2^w` divides `u * 2^k` whenever\n`k >= w`; the guard exists so the term EVALUATES, not because the result\ndiffers.  `Op_Sext`: master takes a STRICTLY-greater branch at `n > a.width`\nand returns plain zero extension there, which agrees with the pinned formula\nbecause `bv_uint a < 2^a.width <= 2^(n-1)` puts the sign bit at `n-1` clear.\nChanging `>` to `>=` would break it: `a = mk_bv 4 (-1), n = 4, w = 8` gives 255\nunder `>` and 15 under `>=`. -/\n\nprivate theorem p2pos (k : Nat) : (0:Int) < 2 ^ k := Int.pow_pos (by decide)\nprivate theorem p2ne (k : Nat) : (2:Int) ^ k ≠ 0 := Int.ne_of_gt (p2pos k)\n\nprivate theorem uint_nonneg (x : BV) : (0:Int) ≤ bv_uint x := by\n  unfold bv_uint; exact Int.emod_nonneg _ (p2ne _)\n\nprivate theorem uint_lt (x : BV) : bv_uint x < 2 ^ x.width := by\n  unfold bv_uint; exact Int.emod_lt_of_pos _ (p2pos _)\n\nprivate theorem p2mono {p q : Nat} (h : p ≤ q) : (2:Int) ^ p ≤ 2 ^ q := by\n  have : (2:Nat) ^ p ≤ 2 ^ q := Nat.pow_le_pow_right (by decide) h\n  exact_mod_cast this\n\n/-- The arithmetic bridge for master's STRICTLY-greater `Op_Sext` branch: at\n`n > a.width` the value is already reduced mod `2^n` and its sign bit is\nclear. -/\ntheorem sext_wide (a : BV) (n : Nat) (h : n > a.width) :\n    bv_uint a % (2 ^ n : Int) = bv_uint a ∧ bv_uint a < (2 ^ (n - 1) : Int) := by\n  have h0 := uint_nonneg a\n  have h1 := uint_lt a\n  have h2 : (2:Int) ^ a.width ≤ 2 ^ (n - 1) := p2mono (by omega)\n  have hlt1 : bv_uint a < (2 ^ (n - 1) : Int) := Int.lt_of_lt_of_le h1 h2\n  have hn1 : (2:Int) ^ (n - 1) ≤ 2 ^ n := p2mono (by omega)\n  exact ⟨Int.emod_eq_of_lt h0 (Int.lt_of_lt_of_le hlt1 hn1), hlt1⟩\n\ntheorem sint_resize (a : BV) (n : Nat) :\n    bv_sint (bv_resize n a)\n      = (if n = 0 then 0\n         else if bv_uint a % (2^n:Int) < (2^(n-1):Int) then bv_uint a % (2^n:Int)\n         else bv_uint a % (2^n:Int) - (2^n:Int)) := by\n  unfold bv_sint bv_resize bv_uint mk_bv\n  simp\n\ntheorem evalOp_Sext_shape (w : Nat) (a : BV) (n : Nat) :\n    (if n = 0 then mk_bv w 0\n     else if n > a.width then mk_bv w (bv_uint a)\n     else (let u := bv_uint a % (2 ^ n : Int)\n           if u < (2 ^ (n - 1) : Int) then mk_bv w u else mk_bv w (u - (2 ^ n : Int))))\n      = mk_bv w (bv_sint (bv_resize n a)) := by\n  rw [sint_resize]\n  by_cases h0 : n = 0\n  · simp [h0]\n  · by_cases hw : n > a.width\n    · obtain ⟨hmod, hlt⟩ := sext_wide a n hw\n      simp [h0, hw, hmod, hlt]\n    · simp only [h0, hw, if_false]\n      split <;> simp_all\n\n/-- Master's guarded `Op_SHL` fold term equals the pinned unguarded one.\n`2^w` divides `u * 2^k` when `k >= w`, so the guard changes no value. -/\ntheorem shl_step_eq (w : Nat) (u : Int) (k : Nat) :\n    mk_bv w (if k ≥ w then (0:Int) else u * 2 ^ k) = mk_bv w (u * 2 ^ k) := by\n  by_cases h : k ≥ w\n  · unfold mk_bv\n    simp only [h, if_true]\n    have hk : (2:Int) ^ k = 2 ^ w * 2 ^ (k - w) := by\n      rw [← Int.pow_add]; congr 1; omega\n    rw [hk, ← Int.mul_assoc, Int.mul_comm u ((2:Int) ^ w), Int.mul_assoc]\n    simp [Int.mul_emod_right]\n  · simp [h]\n\ntheorem shl_fold_eq (w : Nat) (a : BV) (bs : List BV) :\n    bs.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc\n      (mk_bv w (if (bv_uint b).toNat ≥ w then 0\n                else bv_uint a * (2:Int) ^ (bv_uint b).toNat))) (mk_bv w 0)\n    = bs.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc\n      (mk_bv w (bv_uint a * (2:Int) ^ (bv_uint b).toNat))) (mk_bv w 0) := by\n  have h : (fun acc b => bv_bitwise w (fun x y => xor x y) acc\n            (mk_bv w (if (bv_uint b).toNat ≥ w then 0\n                      else bv_uint a * (2:Int) ^ (bv_uint b).toNat)))\n       = (fun acc b => bv_bitwise w (fun x y => xor x y) acc\n            (mk_bv w (bv_uint a * (2:Int) ^ (bv_uint b).toNat))) := by\n    funext acc b; rw [shl_step_eq]\n  rw [h]\n\ntheorem bv_shl_step_unfolds", HELPER_WHY),
        ("""    bv_shl_step w a b = mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat) := rfl""",
         """    bv_shl_step w a b = mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat) :=
  shl_step_eq w (bv_uint a) (bv_uint b).toNat""",
         "statement unchanged; rfl replaced by the divisibility argument"),
        ("""            (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat))) (mk_bv w 0) := rfl

theorem denoteOp_SHL_unchanged_by_overlay""",
         """            (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat))) (mk_bv w 0) := by
  unfold eval_op
  exact shl_fold_eq w a bs

theorem denoteOp_SHL_unchanged_by_overlay""",
         "statement unchanged; rfl replaced by shl_fold_eq"),
        ("""            (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat))) (mk_bv w 0) := rfl

/-- The SIGNED reading""",
         """            (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat))) (mk_bv w 0) := by
  unfold denote_op
  exact shl_fold_eq w a bs

/-- The SIGNED reading""",
         "statement unchanged; rfl replaced by shl_fold_eq, for denote_op"),
        ("""  simp only [eval_op, bv_sint, bv_resize, bv_uint, mk_bv, Int.emod_emod]
  split
  · simp_all
  · split <;> rfl""",
         """  unfold eval_op
  exact evalOp_Sext_shape w a _""",
         "statement unchanged; the closing split <;> rfl replaced by the "
         "strictly-greater arithmetic bridge"),
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

COLS = ["asset", "pinned_sha256", "source_sha256", "installed_sha256",
        "destination_sha256", "verbatim", "adapted_edits",
        "adaptation_reason", "state"]


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

    # PASS 1: resolve every adaptation and validate every anchor. NOTHING is
    # written in this pass. The earlier version interleaved validation with
    # writing, so a bad anchor on the last asset refused AFTER the first
    # fifty-five had already landed -- a refusal that leaves a half-installed
    # contribution behind is worse than no check at all.
    staged = {}
    for r in rows:
        rel = r["asset"]
        text = (src_root / rel).read_bytes()
        reasons, applied = [], 0
        for edit in ADAPTATIONS.get(rel, []):
            old, new_, why = edit[0], edit[1], edit[2]
            # An explicit count, because a transported call site can legitimately
            # repeat. Stating it means a source that gains or loses one is a
            # refusal rather than a silently different edit.
            want = edit[3] if len(edit) > 3 else 1
            t = text.decode("utf-8")
            n = t.count(old)
            if n != want:
                problems.append(f"{rel}: adaptation anchor appears {n} times, "
                                f"expected exactly {want}: {old[:70]!r}")
                continue
            text = t.replace(old, new_, n).encode("utf-8")
            applied += n
            reasons.append(why)
        r["adapted_edits"] = applied
        r["adaptation_reason"] = "; ".join(reasons)
        r["installed_sha256"] = hashlib.sha256(text).hexdigest()
        r["verbatim"] = "1" if r["installed_sha256"] == r["source_sha256"] else "0"
        staged[rel] = text

    if problems:
        print("REFUSING before writing anything: adaptation anchors did not match:",
              file=sys.stderr)
        for q in problems:
            print(f"  {q}", file=sys.stderr)
        return 2

    # PASS 2: write, then READ BACK. `destination_sha256` is what is on disk
    # now, not what was intended -- a map that records the plan rather than the
    # result cannot catch a short write or a stale file.
    installed = adapted = 0
    for r in rows:
        rel = r["asset"]
        if not a.apply:
            r["state"] = "would_install"
            r["destination_sha256"] = ""
            continue
        dst = ROOT / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(staged[rel])
        if (src_root / rel).stat().st_mode & 0o111:
            dst.chmod(dst.stat().st_mode | 0o755)
        r["destination_sha256"] = sha256(dst)
        if r["destination_sha256"] != r["installed_sha256"]:
            problems.append(f"{rel}: wrote {r['installed_sha256'][:12]} but the "
                            f"destination reads back {r['destination_sha256'][:12]}")
        r["state"] = "installed"
        installed += 1
        if r["adapted_edits"]:
            adapted += 1

    if problems:
        print("REFUSING: destinations do not match what was written:", file=sys.stderr)
        for q in problems:
            print(f"  {q}", file=sys.stderr)
        return 2

    out = pathlib.Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8") as fh:
        fh.write(f"# source\t{src_root}\n")
        fh.write(f"# pin\t{doc.get('pin','')}\n")
        fh.write(f"# baseline\t{pathlib.Path(a.baseline).resolve()}\n")
        fh.write("# verbatim=1 means the installed bytes equal the pinned source bytes.\n")
        fh.write("# destination_sha256 is READ BACK from the installed file, so the map\n")
        fh.write("# records what is on disk rather than what was intended.\n")
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

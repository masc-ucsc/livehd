/-
`Sym.Sat` -- an UNSAT verdict checked by Lean's own LRAT checker (M3 S2).

The obligation is a `BVLogicalExpr`. `cnfOf` is exactly the CNF `bv_decide`
checks: `Std`'s verified bit-blaster, relabelled, through the AIG-to-CNF
translation. An external SAT solver (cadical) is UNTRUSTED: it is handed
`cnfOf φ` as DIMACS and returns an LRAT proof, which `checkUnsat` checks
with `Std.Tactic.BVDecide.LRAT.check`. `checkUnsat_sound` is the same chain
as `Std`'s `unsat_of_verifyBVExpr_eq_true`, over proof BYTES (binary LRAT is
not valid UTF-8, so the `String` entry point cannot carry it).
-/
import Std.Tactic.BVDecide

namespace Projection.Sym
open Std.Tactic.BVDecide Std.Sat

/-- The CNF the checker checks. -/
def cnfOf (φ : BVLogicalExpr) : CNF Nat := AIG.toCNF φ.bitblast.relabelNat

/-- Check an LRAT proof (binary or text) that `φ` is unsatisfiable. -/
def checkUnsat (φ : BVLogicalExpr) (proof : ByteArray) : Bool :=
  match LRAT.parseLRATProof proof with
  | .ok p    => LRAT.check p (cnfOf φ)
  | .error _ => false

theorem checkUnsat_sound {φ : BVLogicalExpr} {proof : ByteArray}
    (h : checkUnsat φ proof = true) : φ.Unsat := by
  apply BVLogicalExpr.unsat_of_bitblast
  rw [← AIG.Entrypoint.relabelNat_unsat_iff]
  rw [← AIG.toCNF_equisat]
  unfold checkUnsat at h
  split at h
  · exact LRAT.check_sound _ _ h
  · cases h

end Projection.Sym

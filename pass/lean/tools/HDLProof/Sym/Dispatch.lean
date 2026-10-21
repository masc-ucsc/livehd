/-
`Sym.Dispatch` -- `symPrim`: one symbolic semantics for every primitive the
residuals use, and `symPrim_sound` (M3 S1/S4).
-/
import HDLProof.Sym.Mem
import LeanSemanticPrimitives.Projection.AxiomGate

open Projection
namespace HDLProof.Sym
open Std.Tactic.BVDecide

/-! ## The dispatcher -/

/-- The symbolic primitive, or a refusal. -/
def symPrimO (p : Prim) (ss : List SVal) : Option SVal :=
  match p with
  | .addI      => symIntBin SInt.add ss
  | .subI      => symIntBin SInt.sub ss
  | .ltI       => symIntCmp SInt.lt ss
  | .leI       => symIntCmp SInt.le ss
  | .eqI       => symIntCmp SInt.eq ss
  | .andB      => symBoolBin andE ss
  | .orB       => symBoolBin orE ss
  | .notB      => symNotB ss
  | .isNil     => symIsNil ss
  | .hd        => symHd ss
  | .tl        => symTl ss
  | .consP     => symConsP ss
  | .bvMk      => symBvMk ss
  | .bvWidth   => symBvWidth ss
  | .bvUint    => symBvUint ss
  | .bvSint    => symBvSint ss
  | .bvBit     => symBvBit ss
  | .bvAnd     => symBvBitwise .and ss
  | .bvOr      => symBvBitwise .or ss
  | .bvXor     => symBvBitwise .xor ss
  | .bvNot     => symBvNot ss
  | .bvResize  => symBvResize ss
  | .bvSra     => symBvSra ss
  | .bvShl     => symBvShl ss
  | .bvGetMask => symBvGetMask ss
  | .memRead   => symMemRead ss
  | .memWriteBE => symMemWriteBE ss
  | .memWrite  => symMemWrite ss
  | _          => none

def symPrim (p : Prim) (ss : List SVal) : Except String SVal :=
  match allConc ss with
  | some vs => match evalPrim p vs with
               | .ok v => .ok (.conc v)
               | .error m => .error m
  | none =>
    match symPrimO p ss with
    | some s => .ok s
    | none   => .error s!"symbolic {repr p}: unsupported operand shapes"

theorem symPrimO_sound {σ : Asg} {p : Prim} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symPrimO p ss = some s) (hr : SRelL σ ss vs) (he : evalPrim p vs = .ok v) : SRel σ s v := by
  cases p with
  | addI => exact symIntBin_sound (g := (· + ·)) (fun i j => SInt.val_add σ i j) (fun _ _ => rfl) h hr he
  | subI => exact symIntBin_sound (g := (· - ·)) (fun i j => SInt.val_sub σ i j) (fun _ _ => rfl) h hr he
  | ltI => exact symIntCmp_sound (g := fun a b => decide (a < b)) (fun i j => SInt.bit_lt σ i j)
             (fun _ _ => rfl) h hr he
  | leI => exact symIntCmp_sound (g := fun a b => decide (a ≤ b)) (fun i j => SInt.bit_le σ i j)
             (fun _ _ => rfl) h hr he
  | eqI => exact symIntCmp_sound (g := fun a b => decide (a = b)) (fun i j => SInt.bit_eq σ i j)
             (fun _ _ => rfl) h hr he
  | andB => exact symBoolBin_sound (g := (· && ·)) (fun i j => bit_andE σ i j) (fun _ _ => rfl) h hr he
  | orB => exact symBoolBin_sound (g := (· || ·)) (fun i j => bit_orE σ i j) (fun _ _ => rfl) h hr he
  | notB => exact symNotB_sound h hr he
  | isNil => exact symIsNil_sound h hr he
  | hd => exact symHd_sound h hr he
  | tl => exact symTl_sound h hr he
  | consP => exact symConsP_sound h hr he
  | bvMk => exact symBvMk_sound h hr he
  | bvWidth => exact symBvWidth_sound h hr he
  | bvUint => exact symBvUint_sound h hr he
  | bvSint => exact symBvSint_sound h hr he
  | bvBit => exact symBvBit_sound h hr he
  | bvAnd => exact symBvBitwise_sound (f := (· && ·)) (fun x y i => BitVec.getLsbD_and)
               (fun w va vb x y hx hy => by simp [evalPrim, hx, hy]) h hr he
  | bvOr => exact symBvBitwise_sound (f := (· || ·)) (fun x y i => BitVec.getLsbD_or)
               (fun w va vb x y hx hy => by simp [evalPrim, hx, hy]) h hr he
  | bvXor => exact symBvBitwise_sound (f := xor) (fun x y i => BitVec.getLsbD_xor)
               (fun w va vb x y hx hy => by simp [evalPrim, hx, hy]) h hr he
  | bvNot => exact symBvNot_sound h hr he
  | bvResize => exact symBvResize_sound h hr he
  | bvSra => exact symBvSra_sound h hr he
  | bvShl => exact symBvShl_sound h hr he
  | bvGetMask => exact symBvGetMask_sound h hr he
  | memRead => exact symMemRead_sound h hr he
  | memWriteBE => exact symMemWriteBE_sound h hr he
  | memWrite => exact symMemWrite_sound h hr he
  | _ => simp [symPrimO] at h

/-- **`symPrim` is sound**: related operands, a successful concrete primitive,
and a symbolic answer -- the answer denotes the concrete result. -/
theorem symPrim_sound {σ : Asg} {p : Prim} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symPrim p ss = .ok s) (hr : SRelL σ ss vs) (he : evalPrim p vs = .ok v) : SRel σ s v := by
  unfold symPrim at h
  split at h
  · rename_i xs hxs
    rw [allConc_sound hxs hr] at he
    rw [he] at h
    simp only [Except.ok.injEq] at h
    subst h; exact .conc
  · split at h
    · rename_i s' hs
      simp only [Except.ok.injEq] at h; subst h
      exact symPrimO_sound hs hr he
    · cases h

#audit_axioms symMemRead_sound
#audit_axioms symMemWriteBE_sound
#audit_axioms symMemWrite_sound
#audit_axioms symPrimO_sound
#audit_axioms symPrim_sound

end HDLProof.Sym

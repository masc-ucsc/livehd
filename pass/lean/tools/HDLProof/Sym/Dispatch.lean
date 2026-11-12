/-
`Sym.Dispatch` -- `symPrim`: one symbolic semantics for every primitive the
residuals use, and `symPrim_sound` (M3 S1/S4).
-/
import HDLProof.Sym.Mem
import HDLProof.Sym.EqV
import HDLProof.Sym.CtorP
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
  | .mulI      => symIntBin SInt.mul ss
  | .divI      => symIntBin IntDiv.edivS ss
  | .modI      => symIntBin IntDiv.emodS ss
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
  | .eqV       => match ss with
                  | [a, b] => (symEqV 64 a b).map .bool
                  | _ => none
  | .mkCtorP   => symMkCtorP ss
  | .ctorTagP  => symCtorTagP ss
  | .ctorFieldsP => symCtorFieldsP ss

/-- The primitives whose result is a bit vector built by `mk_bv`, so NORMALIZED (its raw value is
its unsigned value): the symbolic answer is marked `.nbv`, which `eqV`, `ctorFieldsP` and `caseT`
can read. -/
def bvResult : Prim → Bool
  | .bvMk | .bvAnd | .bvOr | .bvXor | .bvNot | .bvResize | .bvSra | .bvShl | .bvGetMask => true
  | _ => false

theorem bv_sra_mk (w : Nat) (x s : BV) : ∃ z, bv_sra w x s = mk_bv w z := by
  refine ⟨if (bv_uint s).toNat ≥ x.width then (if x.width = 0 then 0 else if bv_sint x ≥ 0 then 0 else -1)
          else bv_sint x / 2 ^ (bv_uint s).toNat, ?_⟩
  by_cases h : (bv_uint s).toNat ≥ x.width <;> simp [bv_sra, h]

theorem evalPrim_normal {p : Prim} (hp : bvResult p = true) {vs : List Val} {v : Val}
    (he : evalPrim p vs = .ok v) : ∃ w z, v = ofBV (mk_bv w z) := by
  cases p <;> simp [bvResult] at hp <;> unfold evalPrim at he <;> split at he <;>
    (try contradiction) <;> (try (split at he)) <;> (try contradiction) <;> (try (split at he)) <;>
    (try contradiction) <;> simp only [Except.ok.injEq] at he <;>
    first
      | (subst he; exact ⟨_, _, rfl⟩)
      | (subst he; obtain ⟨z, hz⟩ := bv_sra_mk _ _ _; exact ⟨_, z, by rw [hz]⟩)

/-- Mark a bit-vector answer as normalized. -/
def toNbv : SVal → SVal
  | .bv w e => .nbv w e
  | s => s

theorem SRel_toNbv {σ : Asg} {s : SVal} {v : Val} (hr : SRel σ s v)
    (hn : ∃ w z, v = ofBV (mk_bv w z)) : SRel σ (toNbv s) v := by
  cases s with
  | bv w e =>
      obtain ⟨b, hb, hw, hu⟩ := SRel_bv_inv hr
      obtain ⟨w', z, rfl⟩ := hn
      rw [asBV_ofBV] at hb; cases hb
      refine .nbv ?_
      simp only [width_mk_bv] at hw; subst hw
      rw [← hu, bv_uint_mk_bv]
      simp only [ofBV, mkBV, mk_bv]
  | _ => exact hr

/-- Why a primitive has no symbolic answer, for the refusal message: a width argument that
depends on the data, a raw (possibly unnormalized) bit vector where `eqV`/`ctorFieldsP` need the
raw value, a symbolic constructor tag, or an operand shape with no symbolic semantics. -/
def refusalClass (p : Prim) (ss : List SVal) : String :=
  let widthPrim := match p with
    | .bvMk | .bvAnd | .bvOr | .bvXor | .bvNot | .bvResize | .bvSra | .bvShl | .bvGetMask
    | .memRead | .memWriteBE => true
    | _ => false
  let isInt : SVal → Bool := fun | .int _ => true | _ => false
  let isRaw : SVal → Bool := fun | .bv _ _ => true | _ => false
  if widthPrim && (ss.head?.map isInt).getD false then "data-dependent-width"
  else if (p == .eqV || p == .ctorFieldsP) && ss.any isRaw then "unnormalized-bv"
  else if p == .mkCtorP && (ss.head?.map isInt).getD false then "symbolic-tag"
  else "unsupported operand shapes"

/-- A primitive's symbolic answer: a value; `dead`, when every operand is concrete and the
concrete primitive fails (no concrete run that succeeds reaches here); or a refusal. -/
inductive PrimR where
  | ok (s : SVal)
  | dead
  | refuse (m : String)

def symPrimR (p : Prim) (ss : List SVal) : PrimR :=
  match allConc ss with
  | some vs => match evalPrim p vs with
               | .ok v => .ok (.conc v)
               | .error _ => .dead
  | none =>
    match symPrimO p ss with
    | some s => .ok (if bvResult p then toNbv s else s)
    | none   => .refuse s!"symbolic {repr p}: {refusalClass p ss}"

/-- The two sides of a guarded union, recombined under its guard; a dead side drops out. -/
def combineR (p : Prim) (c : BVExpr 1) : PrimR → PrimR → PrimR
  | .ok a, .ok b => match mergeA c a b with
      | some s => .ok s
      | none => .refuse s!"symbolic {repr p}: resource-limit: guarded union exceeds 64 leaves"
  | .ok a, .dead => .ok a
  | .dead, .ok b => .ok b
  | .dead, .dead => .dead
  | .refuse m, _ => .refuse m
  | _, .refuse m => .refuse m

/-- `symPrimR`, distributed over a guarded-union operand when it has no answer as is: each side
of the union is answered (at most `d` splits deep) and the answers merged under its guard. -/
def symPrimD (p : Prim) : Nat → List SVal → PrimR
  | 0, ss => symPrimR p ss
  | d + 1, ss =>
    match symPrimR p ss with
    | .refuse m =>
        match splitAltL ss with
        | none => .refuse m
        | some (c, xs, ys) => combineR p c (symPrimD p d xs) (symPrimD p d ys)
    | r => r

def symPrim (p : Prim) (ss : List SVal) : Except String SVal :=
  match symPrimD p 6 ss with
  | .ok s => .ok s
  | .dead => .error s!"primitive {repr p}: the concrete primitive fails on these operands"
  | .refuse m => .error m

theorem symPrimO_sound {σ : Asg} {p : Prim} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symPrimO p ss = some s) (hr : SRelL σ ss vs) (he : evalPrim p vs = .ok v) : SRel σ s v := by
  cases p with
  | addI => exact symIntBin_sound (g := (· + ·)) (fun i j => SInt.val_add σ i j) (fun _ _ => rfl) h hr he
  | subI => exact symIntBin_sound (g := (· - ·)) (fun i j => SInt.val_sub σ i j) (fun _ _ => rfl) h hr he
  | mulI => exact symIntBin_sound (g := (· * ·)) (fun i j => SInt.val_mul σ i j) (fun _ _ => rfl) h hr he
  | divI => exact symIntBin_sound (g := fun a b => if b = 0 then 0 else a / b)
              (fun i j => IntDiv.val_edivS σ i j) (fun _ _ => rfl) h hr he
  | modI => exact symIntBin_sound (g := fun a b => if b = 0 then 0 else a % b)
              (fun i j => IntDiv.val_emodS σ i j) (fun _ _ => rfl) h hr he
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
  | eqV =>
      match ss, h with
      | [a, b], h =>
        simp only [symPrimO, Option.map_eq_some_iff] at h
        obtain ⟨e, he', rfl⟩ := h
        obtain ⟨va, vb, rfl, ra, rb⟩ := SRelL_two hr
        simp only [evalPrim, Except.ok.injEq] at he; subst he
        exact .bool (symEqV_sound 64 ra rb he')
  | mkCtorP => exact symMkCtorP_sound h hr he
  | ctorTagP => exact symCtorTagP_sound h hr he
  | ctorFieldsP => exact symCtorFieldsP_sound h hr he

theorem symPrimR_sound {σ : Asg} {p : Prim} {ss : List SVal} {vs : List Val} {v : Val}
    (hr : SRelL σ ss vs) (he : evalPrim p vs = .ok v) :
    (∀ s, symPrimR p ss = .ok s → SRel σ s v) ∧ (symPrimR p ss = .dead → False) := by
  unfold symPrimR
  split
  · rename_i xs hxs
    rw [allConc_sound hxs hr] at he
    rw [he]
    exact ⟨fun s h => (by simp only [PrimR.ok.injEq] at h; subst h; exact .conc), fun h => (by cases h)⟩
  · split
    · rename_i s' hs
      refine ⟨fun s h => ?_, fun h => (by cases h)⟩
      simp only [PrimR.ok.injEq] at h; subst h
      have r := symPrimO_sound hs hr he
      split
      · rename_i hb; exact SRel_toNbv r (evalPrim_normal hb he)
      · exact r
    · exact ⟨fun s h => (by cases h), fun h => (by cases h)⟩

/-- What a `PrimR` says about the concrete result `v`. -/
def PrimROK (σ : Asg) (v : Val) (r : PrimR) : Prop :=
  (∀ s, r = .ok s → SRel σ s v) ∧ (r = .dead → False)

theorem combineR_sound {σ : Asg} {p : Prim} {c : BVExpr 1} {ra rb : PrimR} {v : Val}
    (ht : bitOf (c.eval σ) = true → PrimROK σ v ra) (hf : bitOf (c.eval σ) = false → PrimROK σ v rb) :
    PrimROK σ v (combineR p c ra rb) := by
  cases ra with
  | ok a =>
    cases rb with
    | ok b =>
      simp only [combineR]
      split
      · rename_i m hm
        refine ⟨fun s h => ?_, fun h => (by cases h)⟩
        cases h
        cases hc : bitOf (c.eval σ)
        · exact mergeA_f hc ((hf hc).1 b rfl) hm
        · exact mergeA_t hc ((ht hc).1 a rfl) hm
      · exact ⟨fun s h => (by cases h), fun h => (by cases h)⟩
    | dead =>
      refine ⟨fun s h => ?_, fun h => (by cases h)⟩
      simp only [combineR, PrimR.ok.injEq] at h; subst h
      cases hc : bitOf (c.eval σ)
      · exact ((hf hc).2 rfl).elim
      · exact (ht hc).1 _ rfl
    | refuse m => exact ⟨fun s h => (by simp [combineR] at h), fun h => (by simp [combineR] at h)⟩
  | dead =>
    cases rb with
    | ok b =>
      refine ⟨fun s h => ?_, fun h => (by simp [combineR] at h)⟩
      simp only [combineR, PrimR.ok.injEq] at h; subst h
      cases hc : bitOf (c.eval σ)
      · exact (hf hc).1 _ rfl
      · exact ((ht hc).2 rfl).elim
    | dead =>
      refine ⟨fun s h => (by simp [combineR] at h), fun _ => ?_⟩
      cases hc : bitOf (c.eval σ)
      · exact (hf hc).2 rfl
      · exact (ht hc).2 rfl
    | refuse m => exact ⟨fun s h => (by simp [combineR] at h), fun h => (by simp [combineR] at h)⟩
  | refuse m =>
    cases rb <;> exact ⟨fun s h => (by simp [combineR] at h), fun h => (by simp [combineR] at h)⟩

theorem symPrimD_sound {σ : Asg} {p : Prim} {v : Val} :
    ∀ (d : Nat) {ss : List SVal} {vs : List Val}, SRelL σ ss vs → evalPrim p vs = .ok v →
      PrimROK σ v (symPrimD p d ss)
  | 0, ss, vs, hr, he => by
      simp only [symPrimD]
      exact symPrimR_sound hr he
  | d + 1, ss, vs, hr, he => by
      have base := symPrimR_sound (p := p) hr he
      simp only [symPrimD]
      cases hR : symPrimR p ss with
      | refuse m =>
        dsimp only
        cases hsp : splitAltL ss with
        | none => exact ⟨fun s h => (by cases h), fun h => (by cases h)⟩
        | some t =>
          obtain ⟨c, xs, ys⟩ := t
          obtain ⟨h1, h2⟩ := splitAltL_sound hr hsp
          exact combineR_sound (fun hc => symPrimD_sound d (h1 hc) he)
            (fun hc => symPrimD_sound d (h2 hc) he)
      | ok s => rw [hR] at base; exact base
      | dead => rw [hR] at base; exact base

/-- **`symPrim` is sound**: related operands, a successful concrete primitive,
and a symbolic answer -- the answer denotes the concrete result. -/
theorem symPrim_sound {σ : Asg} {p : Prim} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symPrim p ss = .ok s) (hr : SRelL σ ss vs) (he : evalPrim p vs = .ok v) : SRel σ s v := by
  unfold symPrim at h
  split at h
  · rename_i s' hs
    simp only [Except.ok.injEq] at h; subst h
    exact (symPrimD_sound 6 hr he).1 _ hs
  · cases h
  · cases h

#audit_axioms symMemRead_sound
#audit_axioms symMemWriteBE_sound
#audit_axioms symMemWrite_sound
#audit_axioms SInt.val_mul
#audit_axioms IntDiv.val_edivS
#audit_axioms IntDiv.val_emodS
#audit_axioms symBvBit_sound
#audit_axioms symBvGetMask_sound
#audit_axioms symEqV_sound
#audit_axioms symMkCtorP_sound
#audit_axioms symCtorTagP_sound
#audit_axioms symCtorFieldsP_sound
#audit_axioms evalPrim_normal
#audit_axioms symPrimO_sound
#audit_axioms symPrimD_sound
#audit_axioms symPrim_sound

end HDLProof.Sym

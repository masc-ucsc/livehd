/-
`Sym.Refine` -- refinement with stuttering, by k-induction over a pair (M3 S6).

An IMPLEMENTATION design `DI` and a SPECIFICATION design `DS` run side by
side. The implementation steps every cycle. The specification steps only on
cycles where a WITNESS output of the implementation fires (`adv`; `none`
means lockstep) and otherwise HOLDS -- that is the stuttering. The
specification's inputs are the implementation's, through an index map.
The refinement relation is a list of output pairs `(a, b)`: on every
witnessed cycle, implementation output `a` equals specification output `b`
(as normalized values).

Both designs are symbolically stepped from their own projection residuals;
the specification's next state is `merge adv stepped held`. Soundness
(`runRefK_sound`) reuses `symStep_sound` for each design and `merge_t`/
`merge_f` for the stutter: on every run, at every cycle whose assumptions up
to it held, a witnessed cycle relates the implementation's outputs to the
specification's outputs at its own (stuttered) position.
-/
import LeanSemanticPrimitives.Projection.Sym.KInd

namespace Projection.Sym
open Std.Tactic.BVDecide Projection.MemProjection Projection.Hw Projection.HwMem Compiler

/-! ## The concrete pair -/

/-- The specification's inputs: input `k` is the implementation's `m[k]`. -/
def mapInputs (i : RuntimeInput) (m : List Nat) : RuntimeInput :=
  (m.map fun k => i[k]?.getD (mk_bv 1 0)).toArray

def advBit (outs : Array BV) : Option PropSpec → Bool
  | none   => true
  | some p => propBit outs p

/-- Each pair's outputs agree, as normalized values. -/
def pairsBit (oI oS : Array BV) (pairs : List (Nat × Nat)) : Bool :=
  pairs.all fun q => match oI[q.1]?, oS[q.2]? with
    | some x, some y => decide (bv_uint x = bv_uint y)
    | _, _ => false

structure RefSys where
  DI : DesignCert
  DS : DesignCert
  sI0 : StateM
  sS0 : StateM
  inMap : List Nat
  adv : Option PropSpec

/-- The specification's state: it steps only on witnessed cycles. -/
noncomputable def stutM (sy : RefSys) (ins : Nat → RuntimeInput) : Nat → StateM
  | 0 => sy.sS0
  | n + 1 =>
      if advBit (outM sy.DI sy.sI0 ins n) sy.adv then
        (resM sy.DS (edgesAll sy.DS) (mapInputs (ins n) sy.inMap) (stutM sy ins n)).nextState
      else stutM sy ins n

noncomputable def outS (sy : RefSys) (ins : Nat → RuntimeInput) (n : Nat) : Array BV :=
  (resM sy.DS (edgesAll sy.DS) (mapInputs (ins n) sy.inMap) (stutM sy ins n)).outputs

/-- The refinement property of a cycle. -/
def refBit (oI oS : Array BV) (adv : Option PropSpec) (pairs : List (Nat × Nat)) : Bool :=
  !advBit oI adv || pairsBit oI oS pairs

/-! ## Symbolic pieces -/

def advE (so : SVal) : Option PropSpec → Option (BVExpr 1)
  | none   => some trueE
  | some p => (sNth so p.out >>= bvOfS).map fun e =>
      if p.nz then nzBit e.2 else notE (nzBit e.2)

theorem advE_sound {σ : Asg} {so : SVal} {outs : Array BV} {adv : Option PropSpec} {c : BVExpr 1}
    (hr : SRel σ so (encBVs outs)) (h : advE so adv = some c) : bitOf (c.eval σ) = advBit outs adv := by
  cases adv with
  | none => simp [advE] at h; subst h; simp [advBit, trueE, bitOf]
  | some p =>
      simp only [advE, Option.map_eq_some_iff, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨⟨w, e⟩, ⟨sk, hk, hb⟩, rfl⟩ := h
      obtain ⟨b, hbk, rb⟩ := sNth_sound so outs.toList p.out sk hr hk
      obtain ⟨_, hu⟩ := bvOfS_sound rb hb
      simp only [advBit, propBit]
      rw [show outs[p.out]? = outs.toList[p.out]? by simp, hbk]
      cases hnz : p.nz
      · simp only [Bool.false_eq_true, if_false, bit_notE, bit_nzBit hu]
        cases bv_nonzero b <;> rfl
      · simp only [if_true, bit_nzBit hu]
        cases bv_nonzero b <;> rfl

/-- Normalized equality of two bit vectors of any widths. -/
def eqBvE (x y : Σ w, BVExpr w) : BVLogicalExpr :=
  let W := max x.1 y.1
  .literal (.bin (zextE W x.2) .eq (zextE W y.2))

theorem eval_eqBvE {σ : Asg} {x y : Σ w, BVExpr w} {bx by' : BV}
    (hx : bv_uint bx = ((x.2.eval σ).toNat : Int)) (hy : bv_uint by' = ((y.2.eval σ).toNat : Int)) :
    (eqBvE x y).eval σ = decide (bv_uint bx = bv_uint by') := by
  obtain ⟨wx, ex⟩ := x
  obtain ⟨wy, ey⟩ := y
  simp only [eqBvE, BVLogicalExpr.eval, BoolExpr.eval, BVPred.eval, BVBinPred.eval]
  rw [hx, hy]
  have l1 := (ex.eval σ).isLt
  have l2 := (ey.eval σ).isLt
  have m1 : 2 ^ wx ≤ 2 ^ max wx wy := Nat.pow_le_pow_right (by decide) (by omega)
  have m2 : 2 ^ wy ≤ 2 ^ max wx wy := Nat.pow_le_pow_right (by decide) (by omega)
  apply Bool.eq_iff_iff.mpr
  simp only [beq_iff_eq, decide_eq_true_eq]
  constructor
  · intro h
    have := congrArg BitVec.toNat h
    rw [toNat_zextE, toNat_zextE, Nat.mod_eq_of_lt (by omega), Nat.mod_eq_of_lt (by omega)] at this
    omega
  · intro h
    apply BitVec.eq_of_toNat_eq
    rw [toNat_zextE, toNat_zextE, Nat.mod_eq_of_lt (by omega), Nat.mod_eq_of_lt (by omega)]
    omega

def pairsE (soI soS : SVal) : List (Nat × Nat) → Option BVLogicalExpr
  | [] => some (.const true)
  | q :: qs => do
      let x ← sNth soI q.1 >>= bvOfS
      let y ← sNth soS q.2 >>= bvOfS
      let rest ← pairsE soI soS qs
      pure (.gate .and (eqBvE x y) rest)

theorem pairsE_sound {σ : Asg} {soI soS : SVal} {oI oS : Array BV}
    (hI : SRel σ soI (encBVs oI)) (hS : SRel σ soS (encBVs oS)) :
    ∀ (pairs : List (Nat × Nat)) (φ : BVLogicalExpr), pairsE soI soS pairs = some φ →
      φ.eval σ = pairsBit oI oS pairs
  | [], φ, h => by simp [pairsE] at h; subst h; rfl
  | q :: qs, φ, h => by
      simp only [pairsE, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨x, ⟨sx, hsx, hbx⟩, y, ⟨sy, hsy, hby⟩, rest, hrest, rfl⟩ := h
      obtain ⟨bx, hbxk, rbx⟩ := sNth_sound soI oI.toList q.1 sx hI hsx
      obtain ⟨by', hbyk, rby⟩ := sNth_sound soS oS.toList q.2 sy hS hsy
      obtain ⟨wx, ex⟩ := x
      obtain ⟨wy, ey⟩ := y
      obtain ⟨_, hux⟩ := bvOfS_sound rbx hbx
      obtain ⟨_, huy⟩ := bvOfS_sound rby hby
      show (BVLogicalExpr.eval σ (eqBvE ⟨wx, ex⟩ ⟨wy, ey⟩) && BVLogicalExpr.eval σ rest) = _
      rw [eval_eqBvE hux huy, pairsE_sound hI hS qs rest hrest]
      simp only [pairsBit, List.all_cons]
      rw [show oI[q.1]? = oI.toList[q.1]? by simp, show oS[q.2]? = oS.toList[q.2]? by simp, hbxk, hbyk]

/-- The specification's inputs, symbolically: the implementation's input
variables, re-indexed. -/
def mapInputVar (inW : List Nat) (inB : Nat) (m : List Nat) : SVal :=
  sListOf (m.map fun k => bvVar (inW.getD k 0) (inB + k))

theorem mapInputVar_rel {σ : Asg} {inW : List Nat} {inB : Nat} {i : RuntimeInput} {m : List Nat}
    (hw : i.toList.map BV.width = inW) (ha : AgreeAt σ inB (inputVals i)) (hm : m.all (· < inW.length) = true) :
    SRel σ (mapInputVar inW inB m) (encInput (mapInputs i m)) := by
  unfold mapInputVar encInput encBVs encArr mapInputs
  apply sListOf_rel
  have hlen : i.toList.length = inW.length := by rw [← hw]; simp
  induction m with
  | nil => exact .nil
  | cons k ks ih =>
      simp only [List.all_cons, Bool.and_eq_true, decide_eq_true_eq] at hm
      refine .cons ?_ (ih hm.2)
      have hk : k < i.size := by simp at hlen; omega
      show SRel σ (bvVar (inW.getD k 0) (inB + k)) (encBV (i[k]?.getD (mk_bv 1 0)))
      rw [Array.getElem?_eq_getElem hk, Option.getD_some]
      refine bvVar_rel ?_ ?_
      · rw [← hw]; simp [List.getD_eq_getElem?_getD, Array.getElem?_eq_getElem hk]
      · apply ha k
        simp [inputVals, Array.getElem?_eq_getElem hk]

/-! ## Expanding concrete structure, so the two sides of a merge align -/

mutual
def expandV : Val → SVal
  | .cons a b  => .cons (expandV a) (expandV b)
  | .ctor k vs => if k = bvTag then .conc (.ctor k vs) else .ctor k (expandVL vs)
  | v          => .conc v

def expandVL : List Val → List SVal
  | []      => []
  | v :: vs => expandV v :: expandVL vs
end

mutual
theorem expandV_sound (σ : Asg) : ∀ v : Val, SRel σ (expandV v) v
  | .cons a b => by rw [expandV]; exact .cons (expandV_sound σ a) (expandV_sound σ b)
  | .ctor k vs => by
      rw [expandV]
      split
      · exact .conc
      · exact .ctor (expandVL_sound σ vs)
  | .int _ => .conc
  | .bool _ => .conc
  | .nil => .conc

theorem expandVL_sound (σ : Asg) : ∀ vs : List Val, SRelL σ (expandVL vs) vs
  | [] => by rw [expandVL]; exact .nil
  | v :: vs => by rw [expandVL]; exact .cons (expandV_sound σ v) (expandVL_sound σ vs)
end

mutual
def expandS : SVal → SVal
  | .conc v     => expandV v
  | .cons a b   => .cons (expandS a) (expandS b)
  | .ctor k fs  => .ctor k (expandSL fs)
  | s           => s

def expandSL : List SVal → List SVal
  | []      => []
  | s :: ss => expandS s :: expandSL ss
end

theorem expandS_sound {σ : Asg} : ∀ (s : SVal) {v : Val}, SRel σ s v → SRel σ (expandS s) v := by
  refine expandS.mutual_induct
    (motive_1 := fun s => ∀ {v : Val}, SRel σ s v → SRel σ (expandS s) v)
    (motive_2 := fun ss => ∀ {vs : List Val}, SRelL σ ss vs → SRelL σ (expandSL ss) vs)
    ?_ ?_ ?_ ?_ ?_ ?_ |>.1
  · intro v w hr; rw [SRel_conc_inv hr, expandS]; exact expandV_sound σ v
  · intro a b ih1 ih2 v hr
    obtain ⟨_, _, rfl, r1, r2⟩ := SRel_cons_inv hr
    rw [expandS]; exact .cons (ih1 r1) (ih2 r2)
  · intro k fs ih v hr
    obtain ⟨_, rfl, r⟩ := SRel_ctor_inv hr
    rw [expandS]; exact .ctor (ih r)
  · intro s h1 h2 h3 v hr
    cases s with
    | conc _ => exact absurd rfl (h1 _)
    | cons _ _ => exact absurd rfl (h2 _ _)
    | ctor _ _ => exact absurd rfl (h3 _ _)
    | int _ => exact hr
    | bool _ => exact hr
    | bv _ _ => exact hr
  · intro vs hr; rw [SRelL_nil hr, expandSL]; exact .nil
  · intro s ss ih1 ih2 vs hr
    obtain ⟨_, _, rfl, r1, r2⟩ := SRelL_cons_inv hr
    rw [expandSL]; exact .cons (ih1 r1) (ih2 r2)

/-! ## The pair unroll -/

structure RefSpec where
  /-- `(implementation output, specification output)` pairs that must agree -/
  pairs  : List (Nat × Nat)
  /-- the stutter witness: an implementation output; `none` is lockstep -/
  adv    : Option PropSpec
  /-- specification input `k` is implementation input `inMap[k]` -/
  inMap  : List Nat
  asmOut : Option PropSpec
  asmIn  : List (Nat × Int)
  k      : Nat
  fuelI  : Nat
  fuelS  : Nat
  /-- the implementation's input widths -/
  inW    : List Nat
  LI     : Shape
  LS     : Shape

/-- A pair step: the refinement formula, the assumption, the next states. -/
abbrev PStep := BVLogicalExpr × BVLogicalExpr × SVal × SVal

def advLit (c : BVExpr 1) : BVLogicalExpr := .literal (.bin c .eq (.const 1#1))

theorem eval_advLit (σ : Asg) (c : BVExpr 1) : (advLit c).eval σ = bitOf (c.eval σ) := by
  simp only [advLit, BVLogicalExpr.eval, BoolExpr.eval, BVPred.eval, BVBinPred.eval, BVExpr.eval_const,
    bitOf]
  generalize c.eval σ = x
  have : ∀ y : BitVec 1, y = 0#1 ∨ y = 1#1 := by
    intro y
    have := y.isLt
    rcases (show y.toNat = 0 ∨ y.toNat = 1 by omega) with h | h
    · left; exact BitVec.eq_of_toNat_eq (by simp [h])
    · right; exact BitVec.eq_of_toNat_eq (by simp [h])
  rcases this x with h | h <;> subst h <;> decide

def punroll (DI DS : DesignCert) (RI RS : Program) (sp : RefSpec) :
    Nat → Nat → SVal → SVal → SymSt → Except String (List PStep × SymSt)
  | 0, _, _, _, st => .ok ([], st)
  | j + 1, inB, sI, sS, st =>
    match symStep RI sp.fuelI st (.conc (encEdges (edgesAll DI))) (inputVar sp.inW inB) sI with
    | .error m => .error m
    | .ok (rI, st1) =>
    match splitResult rI with
    | none => .error "the implementation's answer is not a result record"
    | some (nI, oI) =>
    match symStep RS sp.fuelS st1 (.conc (encEdges (edgesAll DS))) (mapInputVar sp.inW inB sp.inMap) sS with
    | .error m => .error m
    | .ok (rS, st2) =>
    match splitResult rS with
    | none => .error "the specification's answer is not a result record"
    | some (nS, oS) =>
    match advE oI sp.adv with
    | none => .error "the witness output is not a bit vector"
    | some c =>
    match pairsE oI oS sp.pairs with
    | none => .error "a related output is not a bit vector"
    | some peq =>
    match asmOf oI sp.asmOut sp.inW inB sp.asmIn with
    | none => .error "the assumption output is not a bit vector"
    | some asm =>
    match merge c (expandS nS) (expandS sS) with
    | none => .error "the specification's stepped and held states differ in shape"
    | some sS' =>
    match punroll DI DS RI RS sp j (inB + sp.inW.length) nI sS' st2 with
    | .error m => .error m
    | .ok (rest, st3) => .ok ((.gate .or (.not (advLit c)) peq, asm, nI, sS') :: rest, st3)

theorem punroll_extends {DI DS : DesignCert} {RI RS : Program} {sp : RefSpec} :
    ∀ (k inB : Nat) (sI sS : SVal) (st : SymSt) (steps : List PStep) (st' : SymSt),
      punroll DI DS RI RS sp k inB sI sS st = .ok (steps, st') → st.Extends st' ∧ steps.length = k
  | 0, _, _, _, st, steps, st', h => by
      simp only [punroll, Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h; exact ⟨.refl _, rfl⟩
  | j + 1, inB, sI, sS, st, steps, st', h => by
      unfold punroll at h
      split at h; · cases h
      rename_i rI st1 h1
      split at h; · cases h
      split at h; · cases h
      rename_i rS st2 h2
      split at h; · cases h
      split at h; · cases h
      split at h; · cases h
      split at h; · cases h
      split at h; · cases h
      split at h; · cases h
      rename_i rest st3 h3
      simp only [Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      have ⟨x3, l3⟩ := punroll_extends j _ _ _ _ _ _ h3
      exact ⟨(symStep_extends h1).trans ((symStep_extends h2).trans x3), by simp [l3]⟩

/-- What a pair step denotes. -/
def PFacts (σ : Asg) (sy : RefSys) (sp : RefSpec) (ins : Nat → RuntimeInput) (n : Nat) (s : PStep) : Prop :=
  s.1.eval σ = refBit (outM sy.DI sy.sI0 ins n) (outS sy ins n) sp.adv sp.pairs
    ∧ s.2.1.eval σ = asmBit (outM sy.DI sy.sI0 ins n) (ins n) sp.asmOut sp.asmIn
    ∧ SRel σ s.2.2.1 (StateM.encStateM (traceM sy.DI sy.sI0 ins (n + 1)))
    ∧ SRel σ s.2.2.2 (StateM.encStateM (stutM sy ins (n + 1)))

/-- The runtime shape the two steps of cycle `n` need. -/
def PRt (sy : RefSys) (ins : Nat → RuntimeInput) (n : Nat) : Prop :=
  runtimeFullOKB sy.DI (ins n) (traceM sy.DI sy.sI0 ins n) (edgesAll sy.DI) = true
    ∧ runtimeFullOKB sy.DS (mapInputs (ins n) sy.inMap) (stutM sy ins n) (edgesAll sy.DS) = true

theorem punroll_sound {sf wf : Nat} {sy : RefSys} {RI RS : Program} {sp : RefSpec}
    (hpI : projectMemDesignAt sf wf sy.DI = .ok RI) (hAI : admitsB sy.DI = true)
    (hpS : projectMemDesignAt sf wf sy.DS = .ok RS) (hAS : admitsB sy.DS = true)
    (hadv : sp.adv = sy.adv) (hmapEq : sp.inMap = sy.inMap)
    (hmap : sp.inMap.all (· < sp.inW.length) = true) (hrange : inAsmRangeOK sp.inW sp.asmIn = true)
    {ins : Nat → RuntimeInput} (hins : ∀ n, (ins n).toList.map BV.width = sp.inW)
    {σ : Asg} {stF : SymSt} (hc : stF.Consistent σ) :
    ∀ (k m inB : Nat) (sI sS : SVal) (st : SymSt) (steps : List PStep) (st' : SymSt),
      punroll sy.DI sy.DS RI RS sp k inB sI sS st = .ok (steps, st') → st'.Extends stF →
      (∀ j, j < k → AgreeAt σ (inB + j * sp.inW.length) (inputVals (ins (m + j)))) →
      SRel σ sI (StateM.encStateM (traceM sy.DI sy.sI0 ins m)) →
      SRel σ sS (StateM.encStateM (stutM sy ins m)) →
      ∀ j (hj : j < steps.length), (∀ i, i ≤ j → PRt sy ins (m + i)) →
        PFacts σ sy sp ins (m + j) (steps[j]'hj)
  | 0, _, _, _, _, _, steps, _, h, _, _, _, _ => by
      simp only [punroll, Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      intro j hj; simp at hj
  | k + 1, m, inB, sI, sS, st, steps, st', h, hx, hin, hSI, hSS => by
      unfold punroll at h
      split at h; · cases h
      rename_i rI st1 h1
      split at h; · cases h
      rename_i nI oI hspI
      split at h; · cases h
      rename_i rS st2 h2
      split at h; · cases h
      rename_i nS oS hspS
      split at h; · cases h
      rename_i c hc'
      split at h; · cases h
      rename_i peq hpeq
      split at h; · cases h
      rename_i asm hasm
      split at h; · cases h
      rename_i sS' hmg
      split at h; · cases h
      rename_i rest st3 h3
      simp only [Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      have x3 := (punroll_extends k _ _ _ _ _ _ h3).1.trans hx
      have x2 := (symStep_extends h2).trans x3
      intro j hj hrt
      -- cycle m
      have hrt0 := hrt 0 (by omega)
      have hin0 : AgreeAt σ inB (inputVals (ins m)) := by simpa using hin 0 (by omega)
      have rrI := symStep_eval (resM_residual hpI hAI (by simpa using hrt0.1)) h1 hc x2 .conc
        (inputVar_rel (hins m) hin0) hSI
      obtain ⟨rnI, roI⟩ := splitResult_sound rrI hspI
      have rrS := symStep_eval (resM_residual hpS hAS (by simpa using hrt0.2)) h2 hc x3 .conc
        (by rw [← hmapEq]; exact mapInputVar_rel (hins m) hin0 hmap) hSS
      obtain ⟨rnS, roS⟩ := splitResult_sound rrS hspS
      have hcb := advE_sound roI hc'
      have hS' : SRel σ sS' (StateM.encStateM (stutM sy ins (m + 1))) := by
        have hst : stutM sy ins (m + 1) =
            if advBit (outM sy.DI sy.sI0 ins m) sy.adv then
              (resM sy.DS (edgesAll sy.DS) (mapInputs (ins m) sy.inMap) (stutM sy ins m)).nextState
            else stutM sy ins m := rfl
        rw [hst, ← hadv]
        cases hb : advBit (outM sy.DI sy.sI0 ins m) sp.adv
        · simp only [Bool.false_eq_true, if_false]
          exact merge_f (by rw [hcb]; exact hb) _ _ (expandS_sound _ hSS) hmg
        · simp only [if_true]
          exact merge_t (by rw [hcb]; exact hb) _ _ (expandS_sound _ rnS) hmg
      have f0 : PFacts σ sy sp ins m (.gate .or (.not (advLit c)) peq, asm, nI, sS') := by
        refine ⟨?_, asmOf_sound roI (hins m) hin0 hrange hasm, rnI, hS'⟩
        show (!(advLit c).eval σ || peq.eval σ) = _
        rw [eval_advLit, hcb, pairsE_sound roI roS sp.pairs peq hpeq]
        rfl
      cases j with
      | zero => simpa using f0
      | succ j =>
          have ih := punroll_sound hpI hAI hpS hAS hadv hmapEq hmap hrange hins hc k (m + 1)
            (inB + sp.inW.length) nI sS' st2 rest st3 h3 hx
            (fun j hj => by
              have := hin (j + 1) (by omega)
              rwa [show inB + sp.inW.length + j * sp.inW.length = inB + (j + 1) * sp.inW.length by
                rw [Nat.succ_mul]; omega, show m + 1 + j = m + (j + 1) by omega])
            rnI hS' j (by simpa using hj)
            (fun i hi => by have := hrt (i + 1) (by omega); rwa [show m + 1 + i = m + (i + 1) by omega])
          simp only [List.getElem_cons_succ]
          rwa [show m + 1 + j = m + (j + 1) by omega] at ih

/-! ## The checker -/

def toSteps (ps : List PStep) : List StepS := ps.map fun s => (s.1, s.2.1, s.2.2.1)

def refSideOK (sy : RefSys) (sp : RefSpec) : Bool :=
  decide (0 < sp.k) && stMatchesB sp.LI sy.sI0 && stMatchesB sp.LS sy.sS0
    && stateFullOKB sy.DI sy.sI0 && stateFullOKB sy.DS sy.sS0
    && inputOKB sy.DI sp.inW.length && inputOKB sy.DS sp.inMap.length
    && flopBoundOKB sy.DI sp.LI.fws.length && flopBoundOKB sy.DS sp.LS.fws.length
    && edgesOKB sy.DI (edgesAll sy.DI) && edgesOKB sy.DS (edgesAll sy.DS)
    && inAsmRangeOK sp.inW sp.asmIn && sp.inMap.all (· < sp.inW.length)
    && decide (sp.adv = sy.adv) && decide (sp.inMap = sy.inMap)

def refBaseRun (sy : RefSys) (RI RS : Program) (sp : RefSpec) : Except String (List PStep × SymSt) :=
  punroll sy.DI sy.DS RI RS sp sp.k 0 (.conc (StateM.encStateM sy.sI0)) (.conc (StateM.encStateM sy.sS0))
    ⟨sp.k * sp.inW.length, #[]⟩

def refStepRun (sy : RefSys) (RI RS : Program) (sp : RefSpec) : Except String (List PStep × SymSt) :=
  punroll sy.DI sy.DS RI RS sp (sp.k + 1) (layoutSize sp.LI + layoutSize sp.LS) (stateVar sp.LI 0)
    (stateVar sp.LS (layoutSize sp.LI))
    ⟨layoutSize sp.LI + layoutSize sp.LS + (sp.k + 1) * sp.inW.length, #[]⟩

def refBaseObl (sy : RefSys) (RI RS : Program) (sp : RefSpec) : Option BVLogicalExpr :=
  match refBaseRun sy RI RS sp with
  | .ok (steps, st) => if scopedB st then some (.gate .and (defsE st) (baseBad (toSteps steps))) else none
  | .error _ => none

def refStepObl (sy : RefSys) (RI RS : Program) (sp : RefSpec) : Option BVLogicalExpr :=
  match refStepRun sy RI RS sp with
  | .ok (s :: steps, st) =>
      if scopedB st && decide (stateShapeOf s.2.2.1 = some sp.LI) && decide (stateShapeOf s.2.2.2 = some sp.LS)
      then some (.gate .and (defsE st) (conjE (stepConj (toSteps (s :: steps)))))
      else none
  | _ => none

def runRefKOf (sy : RefSys) (RI RS : Program) (sp : RefSpec) (pfB pfS : ByteArray) : Bool :=
  refSideOK sy sp
    && (match refBaseObl sy RI RS sp with | some φ => checkUnsat φ pfB | none => false)
    && (match refStepObl sy RI RS sp with | some φ => checkUnsat φ pfS | none => false)

/-! ## Soundness -/

section
variable {sf wf : Nat} {sy : RefSys} {RI RS : Program} {sp : RefSpec} {pfB pfS : ByteArray}
  {ins : Nat → RuntimeInput}

theorem refSideOK_parts (h : refSideOK sy sp = true) :
    0 < sp.k ∧ stMatchesB sp.LI sy.sI0 = true ∧ stMatchesB sp.LS sy.sS0 = true
    ∧ stateFullOKB sy.DI sy.sI0 = true ∧ stateFullOKB sy.DS sy.sS0 = true
    ∧ inputOKB sy.DI sp.inW.length = true ∧ inputOKB sy.DS sp.inMap.length = true
    ∧ flopBoundOKB sy.DI sp.LI.fws.length = true ∧ flopBoundOKB sy.DS sp.LS.fws.length = true
    ∧ edgesOKB sy.DI (edgesAll sy.DI) = true ∧ edgesOKB sy.DS (edgesAll sy.DS) = true
    ∧ inAsmRangeOK sp.inW sp.asmIn = true ∧ sp.inMap.all (· < sp.inW.length) = true
    ∧ sp.adv = sy.adv ∧ sp.inMap = sy.inMap := by
  unfold refSideOK at h
  simp only [Bool.and_eq_true, decide_eq_true_eq] at h
  obtain ⟨⟨⟨⟨⟨⟨⟨⟨⟨⟨⟨⟨⟨⟨h1, h2⟩, h3⟩, h4⟩, h5⟩, h6⟩, h7⟩, h8⟩, h9⟩, h10⟩, h11⟩, h12⟩, h13⟩, h14⟩, h15⟩ := h
  exact ⟨h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11, h12, h13, h14, h15⟩

theorem toSteps_length (ps : List PStep) : (toSteps ps).length = ps.length := by simp [toSteps]

theorem toSteps_get (ps : List PStep) (i : Nat) (h : i < (toSteps ps).length) :
    (toSteps ps)[i]'h = ((ps[i]'(by simpa [toSteps_length] using h)).1,
      (ps[i]'(by simpa [toSteps_length] using h)).2.1, (ps[i]'(by simpa [toSteps_length] using h)).2.2.1) := by
  simp [toSteps]

theorem refObl_parts (hchk : runRefKOf sy RI RS sp pfB pfS = true) :
    refSideOK sy sp = true
    ∧ (∃ steps st, refBaseRun sy RI RS sp = .ok (steps, st) ∧ scopedB st = true
        ∧ BVLogicalExpr.Unsat (BoolExpr.gate .and (defsE st) (baseBad (toSteps steps))))
    ∧ (∃ s steps st, refStepRun sy RI RS sp = .ok (s :: steps, st) ∧ scopedB st = true
        ∧ stateShapeOf s.2.2.1 = some sp.LI ∧ stateShapeOf s.2.2.2 = some sp.LS
        ∧ BVLogicalExpr.Unsat (BoolExpr.gate .and (defsE st) (conjE (stepConj (toSteps (s :: steps)))))) := by
  unfold runRefKOf at hchk
  simp only [Bool.and_eq_true] at hchk
  obtain ⟨⟨hside, hb⟩, hs⟩ := hchk
  refine ⟨hside, ?_, ?_⟩
  · cases hob : refBaseObl sy RI RS sp with
    | none => rw [hob] at hb; cases hb
    | some φ =>
      rw [hob] at hb
      unfold refBaseObl at hob
      split at hob
      · rename_i steps st hrun
        split at hob
        · rename_i hsc
          simp only [Option.some.injEq] at hob; subst hob
          exact ⟨steps, st, hrun, hsc, checkUnsat_sound hb⟩
        · cases hob
      · cases hob
  · cases hos : refStepObl sy RI RS sp with
    | none => rw [hos] at hs; cases hs
    | some φ =>
      rw [hos] at hs
      unfold refStepObl at hos
      split at hos
      · rename_i s steps st hrun
        split at hos
        · rename_i hc
          simp only [Option.some.injEq] at hos; subst hos
          simp only [Bool.and_eq_true, decide_eq_true_eq] at hc
          exact ⟨s, steps, st, hrun, hc.1.1, hc.1.2, hc.2, checkUnsat_sound hs⟩
        · cases hos
      · cases hos

theorem prt_of_inv (hside : refSideOK sy sp = true) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) {n : Nat}
    (h : StMatches sp.LI (traceM sy.DI sy.sI0 ins n) ∧ stateFullOKB sy.DI (traceM sy.DI sy.sI0 ins n) = true
      ∧ StMatches sp.LS (stutM sy ins n) ∧ stateFullOKB sy.DS (stutM sy ins n) = true) :
    PRt sy ins n := by
  obtain ⟨_, _, _, _, _, hinI, hinS, hfbI, hfbS, hedI, hedS, _, _, _, hmapEq⟩ := refSideOK_parts hside
  obtain ⟨hmI, hfI, hmS, hfS⟩ := h
  unfold stateFullOKB at hfI hfS
  simp only [Bool.and_eq_true] at hfI hfS
  have hsz : (ins n).size = sp.inW.length := by rw [← hins n]; simp
  have hszS : (mapInputs (ins n) sy.inMap).size = sp.inMap.length := by simp [mapInputs, hmapEq]
  have hfsI : (traceM sy.DI sy.sI0 ins n).flops.size = sp.LI.fws.length := by rw [← hmI.1]; simp
  have hfsS : (stutM sy ins n).flops.size = sp.LS.fws.length := by rw [← hmS.1]; simp
  refine ⟨?_, ?_⟩
  · unfold runtimeFullOKB runtimeOKB
    simp only [Bool.and_eq_true]
    exact ⟨⟨⟨⟨hfI.1, by rw [hsz]; exact hinI⟩, by rw [hfsI]; exact hfbI⟩, hedI⟩, hfI.2⟩
  · unfold runtimeFullOKB runtimeOKB
    simp only [Bool.and_eq_true]
    exact ⟨⟨⟨⟨hfS.1, by rw [hszS]; exact hinS⟩, by rw [hfsS]; exact hfbS⟩, hedS⟩, hfS.2⟩

end

section
variable {sf wf : Nat} {sy : RefSys} {RI RS : Program} {sp : RefSpec} {pfB pfS : ByteArray}
  {ins : Nat → RuntimeInput}

theorem prun_base {steps : List PStep} {st st0 : SymSt} {k inB : Nat} {sI sS : SVal}
    (h : punroll sy.DI sy.DS RI RS sp k inB sI sS st0 = .ok (steps, st)) :
    st.base = st0.base ∧ steps.length = k := by
  have ⟨x, l⟩ := punroll_extends k inB sI sS st0 steps st h
  exact ⟨x.1.symm, l⟩

/-- Both layouts and both runtime shapes hold at every cycle. -/
theorem refInv_all (hpI : projectMemDesignAt sf wf sy.DI = .ok RI) (hAI : admitsB sy.DI = true)
    (hpS : projectMemDesignAt sf wf sy.DS = .ok RS) (hAS : admitsB sy.DS = true)
    (hchk : runRefKOf sy RI RS sp pfB pfS = true) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, StMatches sp.LI (traceM sy.DI sy.sI0 ins n) ∧ stateFullOKB sy.DI (traceM sy.DI sy.sI0 ins n) = true
      ∧ StMatches sp.LS (stutM sy ins n) ∧ stateFullOKB sy.DS (stutM sy ins n) = true := by
  obtain ⟨hside, -, ⟨s, steps, stS, hrun, hsc, hshI, hshS, -⟩⟩ := refObl_parts hchk
  obtain ⟨_, hm0I, hm0S, hf0I, hf0S, _, _, _, _, _, _, hrange, hmap, hadv, hmapEq⟩ := refSideOK_parts hside
  intro n
  induction n with
  | zero => exact ⟨stMatchesB_sound hm0I, hf0I, stMatchesB_sound hm0S, hf0S⟩
  | succ n ih =>
      have rt := prt_of_inv hside hins ih
      have hb := (prun_base hrun).1
      let xs := stateVals (traceM sy.DI sy.sI0 ins n) ++ stateVals (stutM sy ins n) ++ insVals ins n (sp.k + 1)
      have hxl : xs.length = stS.base := by
        rw [hb, List.length_append, List.length_append, stateVals_length ih.1, stateVals_length ih.2.2.1,
          insVals_length hins]
      obtain ⟨hc, hag⟩ := extendSt_agree hxl hsc
      rw [AgreeAt_append, AgreeAt_append] at hag
      simp only [List.length_append, stateVals_length ih.1, stateVals_length ih.2.2.1, Nat.zero_add] at hag
      have hSI := stateVar_rel ih.1 hag.1.1
      have hSS := stateVar_rel ih.2.2.1 hag.1.2
      have facts := punroll_sound hpI hAI hpS hAS hadv hmapEq hmap hrange hins hc (sp.k + 1) n _ _ _ _ _ _
        hrun (.refl _) (insVals_agree hins (sp.k + 1) hag.2) hSI hSS 0 (by simp)
        (fun i hi => by have : i = 0 := by omega
                        subst this; simpa using rt)
      have hfS : stateFullOKB sy.DS (stutM sy ins (n + 1)) = true := by
        show stateFullOKB sy.DS (if advBit (outM sy.DI sy.sI0 ins n) sy.adv then _ else _) = true
        split
        · exact (resM_spec hAS rt.2).2.2
        · exact ih.2.2.2
      exact ⟨stateShapeOf_sound facts.2.2.1 hshI, (resM_spec hAI rt.1).2.2,
        stateShapeOf_sound facts.2.2.2 hshS, hfS⟩

/-- **Refinement with stuttering is sound.** If the checker accepts, then on
every run (implementation inputs of the declared widths, the specification
fed through the input map and stepping only on witnessed cycles), at every
cycle whose assumptions up to it all held: if the cycle is witnessed, every
related implementation output equals its specification output. -/
theorem runRefKOf_sound (hpI : projectMemDesignAt sf wf sy.DI = .ok RI) (hAI : admitsB sy.DI = true)
    (hpS : projectMemDesignAt sf wf sy.DS = .ok RS) (hAS : admitsB sy.DS = true)
    (hchk : runRefKOf sy RI RS sp pfB pfS = true) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (outM sy.DI sy.sI0 ins j) (ins j) sp.asmOut sp.asmIn = true) →
      refBit (outM sy.DI sy.sI0 ins n) (outS sy ins n) sp.adv sp.pairs = true := by
  obtain ⟨hside, ⟨stepsB, stB, hrunB, hscB, hunsB⟩, ⟨s, steps, stS, hrunS, hscS, -, -, hunsS⟩⟩ :=
    refObl_parts hchk
  obtain ⟨hk, _, _, _, _, _, _, _, _, _, _, hrange, hmap, hadv, hmapEq⟩ := refSideOK_parts hside
  have hinv := refInv_all hpI hAI hpS hAS hchk hins
  have rt : ∀ n, PRt sy ins n := fun n => prt_of_inv hside hins (hinv n)
  intro n
  induction n using Nat.strongRecOn with
  | ind n ih =>
  intro hasm
  cases hokf : refBit (outM sy.DI sy.sI0 ins n) (outS sy ins n) sp.adv sp.pairs
  case true => rfl
  case false =>
  exfalso
  rcases Nat.lt_or_ge n sp.k with hn | hn
  · obtain ⟨hbB, hlB⟩ := prun_base hrunB
    let xs := insVals ins 0 sp.k
    have hxl : xs.length = stB.base := by rw [hbB, insVals_length hins]
    obtain ⟨hc, hag⟩ := extendSt_agree hxl hscB
    have facts := punroll_sound hpI hAI hpS hAS hadv hmapEq hmap hrange hins hc sp.k 0 0 _ _ _ _ _ hrunB
      (.refl _) (fun j hj => by simpa using insVals_agree hins sp.k hag j hj) .conc .conc
    have hev : BVLogicalExpr.eval (extendSt xs.toArray stB)
        (BoolExpr.gate .and (defsE stB) (baseBad (toSteps stepsB))) = true := by
      rw [eval_and, eval_defsE hc, Bool.true_and]
      refine baseBad_true [] (toSteps stepsB) n (by rw [toSteps_length]; omega) rfl (fun i hi hin => ?_) ?_
      · rw [toSteps_get]
        have f := (facts i (by rw [toSteps_length] at hi; exact hi) (fun i' _ => by simpa using rt i')).2.1
        simp only at f ⊢; rw [f]; simpa using hasm i hin
      · rw [toSteps_get]
        have f := (facts n (by omega) (fun i' _ => by simpa using rt i')).1
        simp only at f ⊢; rw [f]; simpa using hokf
    rw [hunsB] at hev; cases hev
  · obtain ⟨hbS, hlS⟩ := prun_base hrunS
    let m := n - sp.k
    let xs := stateVals (traceM sy.DI sy.sI0 ins m) ++ stateVals (stutM sy ins m) ++ insVals ins m (sp.k + 1)
    have hxl : xs.length = stS.base := by
      rw [hbS, List.length_append, List.length_append, stateVals_length (hinv m).1,
        stateVals_length (hinv m).2.2.1, insVals_length hins]
    obtain ⟨hc, hag⟩ := extendSt_agree hxl hscS
    rw [AgreeAt_append, AgreeAt_append] at hag
    simp only [List.length_append, stateVals_length (hinv m).1, stateVals_length (hinv m).2.2.1,
      Nat.zero_add] at hag
    have hSI := stateVar_rel (hinv m).1 hag.1.1
    have hSS := stateVar_rel (hinv m).2.2.1 hag.1.2
    have facts := punroll_sound hpI hAI hpS hAS hadv hmapEq hmap hrange hins hc (sp.k + 1) m _ _ _ _ _ _
      hrunS (.refl _) (insVals_agree hins (sp.k + 1) hag.2) hSI hSS
    have hlen : (s :: steps).length = sp.k + 1 := hlS
    have hev : BVLogicalExpr.eval (extendSt xs.toArray stS)
        (BoolExpr.gate .and (defsE stS) (conjE (stepConj (toSteps (s :: steps))))) = true := by
      rw [eval_and, eval_defsE hc, Bool.true_and]
      refine stepConj_true (toSteps (s :: steps)) (by simp [toSteps]) (fun i hi => ?_) (fun i hi hi' => ?_) ?_
      · rw [toSteps_get]
        rw [toSteps_length] at hi
        have f := (facts i hi (fun i' _ => rt (m + i'))).2.1
        simp only at f ⊢; rw [f]; exact hasm (m + i) (by omega)
      · rw [toSteps_get]
        rw [toSteps_length] at hi hi'
        have f := (facts i hi (fun i' _ => rt (m + i'))).1
        simp only at f ⊢; rw [f]; exact ih (m + i) (by omega) (fun j hj => hasm j (by omega))
      · rw [toSteps_get]
        have f := (facts ((s :: steps).length - 1) (by simp) (fun i' _ => rt (m + i'))).1
        simp only [toSteps_length] at f ⊢
        refine f.trans ?_
        have : m + ((s :: steps).length - 1) = n := by omega
        rw [this]; exact hokf
    rw [hunsS] at hev; cases hev

end

#audit_axioms punroll_sound
#audit_axioms refInv_all
#audit_axioms runRefKOf_sound

end Projection.Sym

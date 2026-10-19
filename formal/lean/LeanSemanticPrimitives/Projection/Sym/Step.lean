/-
`Sym.Step` -- one clock step of a design, symbolically (M3 S3).

`symStep` runs a memory-route residual's entry body on symbolic arguments.
`symStep_sound` transfers `projectMemDesignAt_correct`: for a runtime-valid
concrete step whose arguments the symbolic ones denote, the symbolic result
denotes `encResultM r`, where `r` is THE result -- its denotation is
`interpretDesign D e i (denote st)`. `splitResult`/`sNth` then take the next
state and the outputs out of that result, each with its relation.
-/
import LeanSemanticPrimitives.Projection.Sym.Layout
import LeanSemanticPrimitives.Projection.MemProjection
import LeanSemanticPrimitives.Projection.SurfaceSemantics

namespace Projection.Sym
open Std.Tactic.BVDecide Projection.MemProjection Projection.Hw Projection.HwMem Compiler

/-- The residual's entry body, run on symbolic `[edges, input, state]`. -/
def symStep (R : Program) (fuel : Nat) (st : SymSt) (sE sI sS : SVal) :
    Except String (SVal × SymSt) :=
  match R.fn R.entry with
  | some fd => if fd.arity = 3 then symEval fuel st [sE, sI, sS] fd.body
               else .error "the residual entry does not take three arguments"
  | none    => .error "the residual has no entry function"

theorem symStep_extends {R : Program} {fuel : Nat} {st : SymSt} {sE sI sS s : SVal} {st' : SymSt}
    (h : symStep R fuel st sE sI sS = .ok (s, st')) : st.Extends st' := by
  unfold symStep at h
  split at h
  · split at h
    · exact symEval_extends _ _ _ _ _ _ h
    · cases h
  · cases h

/-- **One step, symbolically.** -/
theorem symStep_sound {sf wf : Nat} {D : DesignCert} {R : Program}
    {i : RuntimeInput} {stM : StateM} {e : ClockEdges}
    (hproj : projectMemDesignAt sf wf D = .ok R) (hA : admitsB D = true)
    (hR : runtimeFullOKB D i stM e = true)
    {σ : Asg} {fuel : Nat} {st0 stS stF : SymSt} {sE sI sS sres : SVal}
    (hsym : symStep R fuel st0 sE sI sS = .ok (sres, stS))
    (hc : stF.Consistent σ) (hx : stS.Extends stF)
    (rE : SRel σ sE (encEdges e)) (rI : SRel σ sI (encInput i))
    (rS : SRel σ sS (StateM.encStateM stM)) :
    ∃ r : StateM.ResultM,
      r.denote = Compiler.interpretDesign D e i (StateM.denote stM)
      ∧ stateFullOKB D r.nextState = true
      ∧ SRel σ sres (StateM.encResultM r) := by
  obtain ⟨r, hden, hok, hiff⟩ := projectMemDesignAt_correct hproj hA hR
  refine ⟨r, hden, hok, ?_⟩
  have hev := (hiff (StateM.encResultM r)).mpr rfl
  unfold symStep at hsym
  split at hsym
  · rename_i fd hfd
    split at hsym
    · cases hev with
      | call hl hf ha hb =>
        have hvs := EvalList_lits_inj _ _ hl
        subst hvs
        rw [hfd] at hf
        cases hf
        exact symEval_sound hc _ _ _ _ _ _ hsym hx _ _
          (.cons rE (.cons rI (.cons rS .nil))) hb
    · cases hsym
  · cases hsym

/-! ## Taking the result apart -/

/-- The result record's two fields: the next state and the outputs. -/
def splitResult : SVal → Option (SVal × SVal)
  | .ctor t [sn, so] => if t = StateM.tagResultM then some (sn, so) else none
  | .conc (.ctor t [vn, vo]) => if t = StateM.tagResultM then some (.conc vn, .conc vo) else none
  | _ => none

theorem splitResult_sound {σ : Asg} {s : SVal} {r : StateM.ResultM} {sn so : SVal}
    (hr : SRel σ s (StateM.encResultM r)) (h : splitResult s = some (sn, so)) :
    SRel σ sn (StateM.encStateM r.nextState) ∧ SRel σ so (encBVs r.outputs) := by
  unfold StateM.encResultM at hr
  cases s with
  | ctor t fs =>
      match fs, h with
      | [a, b], h =>
        simp only [splitResult] at h
        split at h
        · simp only [Option.some.injEq, Prod.mk.injEq] at h
          obtain ⟨rfl, rfl⟩ := h
          obtain ⟨vs, he, rl⟩ := SRel_ctor_inv hr
          cases he
          obtain ⟨_, _, h1, r1, rl'⟩ := SRelL_cons_inv rl
          cases h1
          obtain ⟨_, _, h2, r2, _⟩ := SRelL_cons_inv rl'
          cases h2
          exact ⟨r1, r2⟩
        · cases h
  | conc v =>
      have := SRel_conc_inv hr
      subst this
      simp only [splitResult] at h
      split at h
      · simp only [Option.some.injEq, Prod.mk.injEq] at h
        obtain ⟨rfl, rfl⟩ := h
        exact ⟨.conc, .conc⟩
      · cases h
  | _ => simp [splitResult] at h

/-- The `k`-th element of a symbolic list. -/
def sNth : SVal → Nat → Option SVal
  | .cons h _, 0 => some h
  | .cons _ t, k + 1 => sNth t k
  | .conc (.cons h _), 0 => some (.conc h)
  | .conc (.cons _ t), k + 1 => sNth (.conc t) k
  | _, _ => none

theorem sNth_sound {σ : Asg} {α : Type} {f : α → Val} :
    ∀ (s : SVal) (xs : List α) (k : Nat) (sk : SVal), SRel σ s (encListG f xs) → sNth s k = some sk →
      ∃ x, xs[k]? = some x ∧ SRel σ sk (f x)
  | .cons h t, x :: xs, 0, sk, hr, hk => by
      simp only [sNth, Option.some.injEq] at hk; subst hk
      obtain ⟨_, _, he, r1, _⟩ := SRel_cons_inv hr
      cases he; exact ⟨x, rfl, r1⟩
  | .cons h t, x :: xs, k + 1, sk, hr, hk => by
      simp only [sNth] at hk
      obtain ⟨_, _, he, _, r2⟩ := SRel_cons_inv hr
      cases he
      obtain ⟨y, hy, ry⟩ := sNth_sound t xs k sk r2 hk
      exact ⟨y, by simpa using hy, ry⟩
  | .cons _ _, [], _, _, hr, _ => by
      obtain ⟨_, _, he, _, _⟩ := SRel_cons_inv hr; cases he
  | .conc v, xs, k, sk, hr, hk => by
      have := SRel_conc_inv hr
      subst this
      cases xs with
      | nil => cases k <;> simp [encListG, sNth] at hk
      | cons x xs =>
        cases k with
        | zero =>
            simp only [encListG, sNth, Option.some.injEq] at hk; subst hk
            exact ⟨x, rfl, .conc⟩
        | succ k =>
            simp only [encListG, sNth] at hk
            obtain ⟨y, hy, ry⟩ := sNth_sound (.conc (encListG f xs)) xs k sk .conc hk
            exact ⟨y, by simpa using hy, ry⟩
  | .int _, _, _, _, _, hk => by simp [sNth] at hk
  | .bool _, _, _, _, _, hk => by simp [sNth] at hk
  | .bv _ _, _, _, _, _, hk => by simp [sNth] at hk
  | .ctor _ _, _, _, _, _, hk => by simp [sNth] at hk

/-- An output (or any bit vector) as an expression of its width. -/
def bvOfS (s : SVal) : Option (Σ w, BVExpr w) := asSBV s

theorem bvOfS_sound {σ : Asg} {s : SVal} {b : BV} {w : Nat} {e : BVExpr w}
    (hr : SRel σ s (ofBV b)) (h : bvOfS s = some ⟨w, e⟩) :
    b.width = w ∧ bv_uint b = ((e.eval σ).toNat : Int) := by
  obtain ⟨b', hb', hw, hu⟩ := asSBV_sound hr h
  rw [asBV_ofBV] at hb'
  cases hb'
  exact ⟨hw, hu⟩

end Projection.Sym

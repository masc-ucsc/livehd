/-
`Sym.Eval` -- symbolic evaluation of a residual term (M3 S1).

`symEval n st env t` runs `t` with the symbolic environment `env`. Concrete
structure is followed exactly (a concrete `ite` condition takes one branch, a
`caseT` on a concrete tag takes its alternative); a SYMBOLIC `ite` runs both
branches and `merge`s them.

Every `let`-bound symbolic leaf is CUT: it becomes a fresh variable whose
DEFINITION is recorded in the state (`SymSt.defs`). The expressions handed to
the bit-blaster therefore stay small -- one graph node's operation over
variables -- however much the residual shares, which is what keeps `Std`'s
structurally hashed bit-blaster cache from walking a DAG as a tree.

`symEval_sound`: for every assignment that satisfies the recorded definitions
(`SymSt.Consistent`), a concrete `Eval` of the same term in a related
environment yields a value the symbolic answer denotes.
-/
import LeanSemanticPrimitives.Projection.Sym.Dispatch
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection.Sym
open Std.Tactic.BVDecide

/-! ## Cut variables -/

structure SymSt where
  /-- the first cut variable's index; every index below it is a primary input -/
  base : Nat
  /-- cut variable `base + k` is DEFINED as `defs[k]` -/
  defs : Array (Σ w, BVExpr w)

/-- Variable `idx` holds the value of the definition. -/
def DefOK (σ : Asg) (idx : Nat) : (Σ w, BVExpr w) → Prop
  | ⟨w, e⟩ => (BVExpr.var idx : BVExpr w).eval σ = e.eval σ

/-- The assignment satisfies every recorded definition. -/
def SymSt.Consistent (σ : Asg) (st : SymSt) : Prop :=
  ∀ k (h : k < st.defs.size), DefOK σ (st.base + k) (st.defs[k]'h)

/-- `b` extends `a`: same base, `a`'s definitions a prefix of `b`'s. -/
def SymSt.Extends (a b : SymSt) : Prop :=
  a.base = b.base ∧ ∃ hle : a.defs.size ≤ b.defs.size,
    ∀ k (h : k < a.defs.size), b.defs[k]'(Nat.lt_of_lt_of_le h hle) = a.defs[k]'h

theorem SymSt.Extends.refl (a : SymSt) : a.Extends a := ⟨rfl, Nat.le_refl _, fun _ _ => rfl⟩

theorem SymSt.Extends.trans {a b c : SymSt} (h1 : a.Extends b) (h2 : b.Extends c) : a.Extends c := by
  obtain ⟨hb1, hle1, he1⟩ := h1
  obtain ⟨hb2, hle2, he2⟩ := h2
  exact ⟨hb1.trans hb2, Nat.le_trans hle1 hle2, fun k h => by
    rw [he2 k (Nat.lt_of_lt_of_le h hle1), he1 k h]⟩

theorem SymSt.Consistent.of_extends {σ : Asg} {a b : SymSt} (hc : b.Consistent σ) (h : a.Extends b) :
    a.Consistent σ := by
  obtain ⟨hb, hle, he⟩ := h
  intro k hk
  have := hc k (Nat.lt_of_lt_of_le hk hle)
  rw [he k hk] at this
  rw [hb]; exact this

/-- Record `e` as a new cut variable. Variables and constants stay as they are. -/
def cutE (st : SymSt) {w : Nat} (e : BVExpr w) : BVExpr w × SymSt :=
  match e with
  | .var _   => (e, st)
  | .const _ => (e, st)
  | _        => (.var (st.base + st.defs.size), { st with defs := st.defs.push ⟨w, e⟩ })

theorem cutE_extends (st : SymSt) {w : Nat} (e : BVExpr w) : st.Extends (cutE st e).2 := by
  unfold cutE
  split
  · exact .refl _
  · exact .refl _
  · refine ⟨rfl, by simp, fun k h => ?_⟩
    simp [Array.getElem_push_lt h]

/-- A cut expression evaluates as the original, under any assignment that is
consistent with any extension of the state the cut produced. -/
theorem cutE_eval {σ : Asg} (st : SymSt) {w : Nat} (e : BVExpr w) {stF : SymSt}
    (hc : stF.Consistent σ) (hx : (cutE st e).2.Extends stF) :
    (cutE st e).1.eval σ = e.eval σ := by
  have hc' := hc.of_extends hx
  cases e with
  | var => rfl
  | const => rfl
  | _ =>
    have := hc' st.defs.size (by simp [cutE])
    simp only [cutE, Array.getElem_push_eq] at this
    exact this

mutual

/-- Cut every symbolic leaf of a value. -/
def cutS (st : SymSt) : SVal → SVal × SymSt
  | .conc v => (.conc v, st)
  | .int ⟨k, e⟩ => let r := cutE st e; (.int ⟨k, r.1⟩, r.2)
  | .bool e => let r := cutE st e; (.bool r.1, r.2)
  | .bv w e => let r := cutE st e; (.bv w r.1, r.2)
  | .cons h t =>
      let r1 := cutS st h
      let r2 := cutS r1.2 t
      (.cons r1.1 r2.1, r2.2)
  | .ctor k fs => let r := cutL st fs; (.ctor k r.1, r.2)

def cutL (st : SymSt) : List SVal → List SVal × SymSt
  | [] => ([], st)
  | s :: ss =>
      let r1 := cutS st s
      let r2 := cutL r1.2 ss
      (r1.1 :: r2.1, r2.2)

end

theorem cutS_sound {σ : Asg} {stF : SymSt} (hc : stF.Consistent σ) :
    ∀ (st : SymSt) (s : SVal) {v : Val}, SRel σ s v → (cutS st s).2.Extends stF →
      SRel σ (cutS st s).1 v ∧ st.Extends (cutS st s).2 := by
  refine cutS.mutual_induct
    (motive_1 := fun st s => ∀ {v : Val}, SRel σ s v → (cutS st s).2.Extends stF →
      SRel σ (cutS st s).1 v ∧ st.Extends (cutS st s).2)
    (motive_2 := fun st ss => ∀ {vs : List Val}, SRelL σ ss vs → (cutL st ss).2.Extends stF →
      SRelL σ (cutL st ss).1 vs ∧ st.Extends (cutL st ss).2)
    ?_ ?_ ?_ ?_ ?_ ?_ ?_ ?_ |>.1
  · intro st v' v hr _
    exact ⟨hr, .refl _⟩
  · intro st k e v hr hx
    simp only [cutS] at hx ⊢
    obtain ⟨z, rfl, hz⟩ := SRel_int_inv hr
    exact ⟨.int (by simp only [SInt.val] at hz ⊢; rw [cutE_eval st e hc hx]; exact hz), cutE_extends st e⟩
  · intro st e v hr hx
    simp only [cutS] at hx ⊢
    obtain ⟨b, rfl, hb⟩ := SRel_bool_inv hr
    exact ⟨.bool (by rw [cutE_eval st e hc hx]; exact hb), cutE_extends st e⟩
  · intro st w e v hr hx
    simp only [cutS] at hx ⊢
    obtain ⟨b, hb, hw, hu⟩ := SRel_bv_inv hr
    exact ⟨.bv hb hw (by rw [cutE_eval st e hc hx]; exact hu), cutE_extends st e⟩
  · intro st h t _ ih1 ih2 v hr hx
    simp only [cutS] at hx ⊢
    obtain ⟨h', t', rfl, r1, r2⟩ := SRel_cons_inv hr
    have ⟨s2, e2⟩ := ih2 r2 hx
    have ⟨s1, e1⟩ := ih1 r1 (e2.trans hx)
    exact ⟨.cons s1 s2, e1.trans e2⟩
  · intro st k fs ih v hr hx
    simp only [cutS] at hx ⊢
    obtain ⟨vs, rfl, r⟩ := SRel_ctor_inv hr
    have ⟨s, e⟩ := ih r hx
    exact ⟨.ctor s, e⟩
  · intro st vs hr _
    simp only [cutL]
    exact ⟨hr, .refl _⟩
  · intro st s ss _ ih1 ih2 vs hr hx
    simp only [cutL] at hx ⊢
    obtain ⟨v, vs', rfl, r1, r2⟩ := SRelL_cons_inv hr
    have ⟨s2, e2⟩ := ih2 r2 hx
    have ⟨s1, e1⟩ := ih1 r1 (e2.trans hx)
    exact ⟨.cons s1 s2, e1.trans e2⟩

/-! ## The evaluator -/

/-- A constructor with these fields: concrete when every field is. -/
def mkCtorS (k : Nat) (ss : List SVal) : SVal :=
  match allConc ss with
  | some vs => .conc (.ctor k vs)
  | none    => .ctor k ss

/-- A scrutinee whose TAG is known, with its fields. -/
def ctorView : SVal → Option (Nat × List SVal)
  | .ctor k fs        => some (k, fs)
  | .conc (.ctor k vs) => some (k, vs.map .conc)
  | _                 => none

mutual

def symEval : Nat → SymSt → List SVal → Term → Except String (SVal × SymSt)
  | 0, _, _, _ => .error "symbolic evaluation: out of fuel"
  | n + 1, st, env, t =>
    match t with
    | .lit v => .ok (.conc v, st)
    | .var i =>
        match env[i]? with
        | some s => .ok (s, st)
        | none   => .error s!"unbound de Bruijn index {i}"
    | .letIn e b =>
        match symEval n st env e with
        | .ok (s, st1) => symEval n (cutS st1 s).2 ((cutS st1 s).1 :: env) b
        | .error m     => .error m
    | .ite c a b =>
        match symEval n st env c with
        | .ok (.conc (.bool true), st1)  => symEval n st1 env a
        | .ok (.conc (.bool false), st1) => symEval n st1 env b
        | .ok (.bool ce, st1) =>
            match symEval n st1 env a with
            | .ok (sa, st2) =>
                match symEval n st2 env b with
                | .ok (sb, st3) =>
                    match merge ce sa sb with
                    | some m => .ok (m, st3)
                    | none   => .error "ite: the branches differ in shape"
                | .error m => .error m
            | .error m => .error m
        | .ok _       => .error "ite: the condition is not a Boolean"
        | .error m    => .error m
    | .prim p ts =>
        match symEvalList n st env ts with
        | .ok (ss, st1) =>
            match symPrim p ss with
            | .ok s    => .ok (s, st1)
            | .error m => .error m
        | .error m => .error m
    | .ctorT k ts =>
        match symEvalList n st env ts with
        | .ok (ss, st1) => .ok (mkCtorS k ss, st1)
        | .error m      => .error m
    | .caseT s alts =>
        match symEval n st env s with
        | .ok (sv, st1) =>
            match ctorView sv with
            | some (tag, fs) =>
                match findAlt alts tag with
                | some a =>
                    if a.arity = fs.length then symEval n st1 (fs ++ env) a.body
                    else .error "caseT: arity mismatch"
                | none => .error "caseT: no alternative"
            | none => .error "caseT: the scrutinee's tag is symbolic"
        | .error m => .error m
    | .call _ _ => .error "call: a residual is call-free"
termination_by n _ _ _ => (n, 0)

def symEvalList : Nat → SymSt → List SVal → List Term → Except String (List SVal × SymSt)
  | _, st, _, [] => .ok ([], st)
  | n, st, env, t :: ts =>
      match symEval n st env t with
      | .ok (s, st1) =>
          match symEvalList n st1 env ts with
          | .ok (ss, st2) => .ok (s :: ss, st2)
          | .error m      => .error m
      | .error m => .error m
termination_by n _ _ ts => (n, ts.length + 1)

end

/-! ## Soundness -/

theorem SRelL_get {σ : Asg} :
    ∀ {env : List SVal} {ρ : List Val} {i : Nat} {s : SVal} {v : Val},
      SRelL σ env ρ → env[i]? = some s → ρ[i]? = some v → SRel σ s v
  | [], _, _, _, _, _, h, _ => by simp at h
  | _ :: _, _, 0, _, _, hr, h1, h2 => by
      obtain ⟨v0, vs, rfl, r0, _⟩ := SRelL_cons_inv hr
      simp at h1 h2; subst h1; subst h2; exact r0
  | _ :: _, _, i + 1, _, _, hr, h1, h2 => by
      obtain ⟨v0, vs, rfl, _, rs⟩ := SRelL_cons_inv hr
      simp at h1 h2
      exact SRelL_get rs h1 h2

theorem SRelL_append {σ : Asg} :
    ∀ {a : List SVal} {b : List Val} {c : List SVal} {d : List Val},
      SRelL σ a b → SRelL σ c d → SRelL σ (a ++ c) (b ++ d)
  | [], _, _, _, h1, h2 => by rw [SRelL_nil h1]; exact h2
  | _ :: _, _, _, _, h1, h2 => by
      obtain ⟨v, vs, rfl, r, rs⟩ := SRelL_cons_inv h1
      exact .cons r (SRelL_append rs h2)

theorem SRelL_conc {σ : Asg} : ∀ (vs : List Val), SRelL σ (vs.map .conc) vs
  | [] => .nil
  | _ :: vs => .cons .conc (SRelL_conc vs)

theorem mkCtorS_sound {σ : Asg} {k : Nat} {ss : List SVal} {vs : List Val} (hr : SRelL σ ss vs) :
    SRel σ (mkCtorS k ss) (.ctor k vs) := by
  unfold mkCtorS
  split
  · rename_i xs hxs
    rw [allConc_sound hxs hr]; exact .conc
  · exact .ctor hr

theorem ctorView_sound {σ : Asg} {sv : SVal} {tag : Nat} {vs : List Val} {k : Nat} {fs : List SVal}
    (hr : SRel σ sv (.ctor tag vs)) (h : ctorView sv = some (k, fs)) : k = tag ∧ SRelL σ fs vs := by
  cases sv with
  | ctor k' fs' =>
      simp only [ctorView, Option.some.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      obtain ⟨vs', he, r⟩ := SRel_ctor_inv hr
      cases he; exact ⟨rfl, r⟩
  | conc x =>
      cases x <;> simp [ctorView] at h
      obtain ⟨rfl, rfl⟩ := h
      have := SRel_conc_inv hr
      cases this
      exact ⟨rfl, SRelL_conc _⟩
  | _ => simp [ctorView] at h

/-- The concrete condition was `true`/`false`; the symbolic one agrees. -/
theorem cond_bit {σ : Asg} {ce : BVExpr 1} {b : Bool} (hr : SRel σ (.bool ce) (.bool b)) :
    bitOf (ce.eval σ) = b := by
  obtain ⟨b', he, hb⟩ := SRel_bool_inv hr
  cases he; exact hb

/-! ### The state only grows -/

theorem cutS_extends : ∀ (st : SymSt) (s : SVal), st.Extends (cutS st s).2 := by
  refine cutS.mutual_induct
    (motive_1 := fun st s => st.Extends (cutS st s).2)
    (motive_2 := fun st ss => st.Extends (cutL st ss).2) ?_ ?_ ?_ ?_ ?_ ?_ ?_ ?_ |>.1
  · intro st v; exact .refl _
  · intro st k e; exact cutE_extends st e
  · intro st e; exact cutE_extends st e
  · intro st w e; exact cutE_extends st e
  · intro st h t _ ih1 ih2; simp only [cutS]; exact ih1.trans ih2
  · intro st k fs ih; simp only [cutS]; exact ih
  · intro st; exact .refl _
  · intro st s ss _ ih1 ih2; simp only [cutL]; exact ih1.trans ih2

theorem symEvalList_extends_of {n : Nat}
    (A : ∀ st env t s st', symEval n st env t = .ok (s, st') → st.Extends st') :
    ∀ ts st env ss st', symEvalList n st env ts = .ok (ss, st') → st.Extends st'
  | [], st, env, ss, st', h => by
      unfold symEvalList at h
      simp only [Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨_, rfl⟩ := h; exact .refl _
  | t :: ts, st, env, ss, st', h => by
      unfold symEvalList at h
      split at h
      · rename_i s1 st1 h1
        split at h
        · rename_i ss2 st2 h2
          simp only [Except.ok.injEq, Prod.mk.injEq] at h
          obtain ⟨_, rfl⟩ := h
          exact (A _ _ _ _ _ h1).trans (symEvalList_extends_of A ts _ _ _ _ h2)
        · cases h
      · cases h

theorem symEval_extends :
    ∀ n st env t s st', symEval n st env t = .ok (s, st') → st.Extends st' := by
  intro n
  induction n with
  | zero => intro st env t s st' h; unfold symEval at h; cases h
  | succ m ih =>
    have B := symEvalList_extends_of ih
    intro st env t s st' h
    unfold symEval at h
    cases t <;> dsimp only at h
    case lit v =>
        simp only [Except.ok.injEq, Prod.mk.injEq] at h; obtain ⟨_, rfl⟩ := h; exact .refl _
    case var i =>
        split at h
        · simp only [Except.ok.injEq, Prod.mk.injEq] at h; obtain ⟨_, rfl⟩ := h; exact .refl _
        · cases h
    case letIn e b =>
        split at h
        · rename_i s1 st1 h1
          exact (ih _ _ _ _ _ h1).trans ((cutS_extends st1 s1).trans (ih _ _ _ _ _ h))
        · cases h
    case ite c a b =>
        split at h
        · rename_i st1 h1; exact (ih _ _ _ _ _ h1).trans (ih _ _ _ _ _ h)
        · rename_i st1 h1; exact (ih _ _ _ _ _ h1).trans (ih _ _ _ _ _ h)
        · rename_i ce st1 h1
          split at h
          · rename_i sa st2 h2
            split at h
            · rename_i sb st3 h3
              split at h
              · simp only [Except.ok.injEq, Prod.mk.injEq] at h; obtain ⟨_, rfl⟩ := h
                exact (ih _ _ _ _ _ h1).trans ((ih _ _ _ _ _ h2).trans (ih _ _ _ _ _ h3))
              · cases h
            · cases h
          · cases h
        · cases h
        · cases h
    case prim p ts =>
        split at h
        · rename_i ss st1 h1
          split at h
          · simp only [Except.ok.injEq, Prod.mk.injEq] at h; obtain ⟨_, rfl⟩ := h
            exact B _ _ _ _ _ h1
          · cases h
        · cases h
    case ctorT k ts =>
        split at h
        · rename_i ss st1 h1
          simp only [Except.ok.injEq, Prod.mk.injEq] at h; obtain ⟨_, rfl⟩ := h
          exact B _ _ _ _ _ h1
        · cases h
    case caseT sc alts =>
        split at h
        · rename_i sv st1 h1
          split at h
          · split at h
            · split at h
              · exact (ih _ _ _ _ _ h1).trans (ih _ _ _ _ _ h)
              · cases h
            · cases h
          · cases h
        · cases h
    case call f ts => cases h

section
variable {P : Program} {σ : Asg} {stF : SymSt}

theorem symEvalList_sound_of {n : Nat}
    (A : ∀ st env t s st', symEval n st env t = .ok (s, st') → st'.Extends stF →
          ∀ ρ v, SRelL σ env ρ → Eval P ρ t v → SRel σ s v) :
    ∀ ts st env ss st', symEvalList n st env ts = .ok (ss, st') → st'.Extends stF →
      ∀ ρ vs, SRelL σ env ρ → EvalList P ρ ts vs → SRelL σ ss vs
  | [], st, env, ss, st', h, hx, ρ, vs, henv, hev => by
      unfold symEvalList at h
      simp only [Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      cases hev; exact .nil
  | t :: ts, st, env, ss, st', h, hx, ρ, vs, henv, hev => by
      unfold symEvalList at h
      split at h
      · rename_i s1 st1 h1
        split at h
        · rename_i ss2 st2 h2
          simp only [Except.ok.injEq, Prod.mk.injEq] at h
          obtain ⟨rfl, rfl⟩ := h
          cases hev with
          | cons e1 e2 =>
            have x2 := symEvalList_extends_of (symEval_extends n) ts _ _ _ _ h2
            exact .cons (A _ _ _ _ _ h1 (x2.trans hx) _ _ henv e1)
              (symEvalList_sound_of A ts _ _ _ _ h2 hx _ _ henv e2)
        · cases h
      · cases h

/-- **`symEval` is sound.** Under any assignment consistent with the final
definitions, the symbolic answer denotes what the concrete evaluation of the
same term, in a related environment, returns. -/
theorem symEval_sound (hc : stF.Consistent σ) :
    ∀ n st env t s st', symEval n st env t = .ok (s, st') → st'.Extends stF →
      ∀ ρ v, SRelL σ env ρ → Eval P ρ t v → SRel σ s v := by
  intro n
  induction n with
  | zero => intro st env t s st' h; unfold symEval at h; cases h
  | succ m ih =>
    have B := symEvalList_sound_of (P := P) ih
    intro st env t s st' h hx ρ v henv hev
    unfold symEval at h
    cases t <;> dsimp only at h
    case lit w =>
      simp only [Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      cases hev; exact .conc
    case var i =>
      split at h
      · rename_i s0 hs
        simp only [Except.ok.injEq, Prod.mk.injEq] at h
        obtain ⟨rfl, rfl⟩ := h
        cases hev with
        | var hv => exact SRelL_get henv hs hv
      · cases h
    case letIn e b =>
      split at h
      · rename_i s1 st1 h1
        cases hev with
        | letIn e1 e2 =>
          have hcx : (cutS st1 s1).2.Extends stF := (symEval_extends m _ _ _ _ _ h).trans hx
          have r1 := ih _ _ _ _ _ h1 ((cutS_extends st1 s1).trans hcx) _ _ henv e1
          have rc := (cutS_sound hc st1 s1 r1 hcx).1
          exact ih _ _ _ _ _ h hx _ _ (.cons rc henv) e2
      · cases h
    case ite c a b =>
      split at h
      · rename_i st1 h1
        cases hev with
        | iteT ec ea => exact ih _ _ _ _ _ h hx _ _ henv ea
        | iteF ec eb =>
          have := ih _ _ _ _ _ h1 ((symEval_extends m _ _ _ _ _ h).trans hx) _ _ henv ec
          cases SRel_conc_inv this
      · rename_i st1 h1
        cases hev with
        | iteT ec ea =>
          have := ih _ _ _ _ _ h1 ((symEval_extends m _ _ _ _ _ h).trans hx) _ _ henv ec
          cases SRel_conc_inv this
        | iteF ec eb => exact ih _ _ _ _ _ h hx _ _ henv eb
      · rename_i ce st1 h1
        split at h
        · rename_i sa st2 h2
          split at h
          · rename_i sb st3 h3
            split at h
            · rename_i mg hm
              simp only [Except.ok.injEq, Prod.mk.injEq] at h
              obtain ⟨rfl, rfl⟩ := h
              have x3 := hx
              have x2 := (symEval_extends m _ _ _ _ _ h3).trans x3
              have x1 := (symEval_extends m _ _ _ _ _ h2).trans x2
              cases hev with
              | iteT ec ea =>
                have rc := ih _ _ _ _ _ h1 x1 _ _ henv ec
                exact merge_t (cond_bit rc) sa sb (ih _ _ _ _ _ h2 x2 _ _ henv ea) hm
              | iteF ec eb =>
                have rc := ih _ _ _ _ _ h1 x1 _ _ henv ec
                exact merge_f (cond_bit rc) sa sb (ih _ _ _ _ _ h3 x3 _ _ henv eb) hm
            · cases h
          · cases h
        · cases h
      · cases h
      · cases h
    case prim p ts =>
      split at h
      · rename_i ss st1 h1
        split at h
        · rename_i s0 hs
          simp only [Except.ok.injEq, Prod.mk.injEq] at h
          obtain ⟨rfl, rfl⟩ := h
          cases hev with
          | prim el ep => exact symPrim_sound hs (B _ _ _ _ _ h1 hx _ _ henv el) ep
        · cases h
      · cases h
    case ctorT k ts =>
      split at h
      · rename_i ss st1 h1
        simp only [Except.ok.injEq, Prod.mk.injEq] at h
        obtain ⟨rfl, rfl⟩ := h
        cases hev with
        | ctorT el => exact mkCtorS_sound (B _ _ _ _ _ h1 hx _ _ henv el)
      · cases h
    case caseT sc alts =>
      split at h
      · rename_i sv st1 h1
        split at h
        · rename_i tag fs hv
          split at h
          · rename_i a ha
            split at h
            · rename_i har
              cases hev with
              | caseT es hfa har' eb =>
                have rs := ih _ _ _ _ _ h1 ((symEval_extends m _ _ _ _ _ h).trans hx) _ _ henv es
                obtain ⟨rfl, rfs⟩ := ctorView_sound rs hv
                rw [ha] at hfa
                cases hfa
                exact ih _ _ _ _ _ h hx _ _ (SRelL_append rfs henv) eb
            · cases h
          · cases h
        · cases h
      · cases h
    case call f ts => cases h

end

#audit_axioms SInt.val_add
#audit_axioms SInt.val_neg
#audit_axioms SInt.bit_lt
#audit_axioms SInt.bit_eq
#audit_axioms eval_muxE
#audit_axioms bitwise_rel
#audit_axioms sra_rel
#audit_axioms shl_rel
#audit_axioms getmask_rel
#audit_axioms merge_t
#audit_axioms merge_f
#audit_axioms symPrim_sound
#audit_axioms cutS_sound
#audit_axioms symEval_extends
#audit_axioms symEval_sound

end Projection.Sym

/-
# The memory route with `env0` inlined

`mainM` binds the source environment and then folds the nodes over it:

    letN "env0" (mkSourcesM …) (letN "env" (evalNodesM nodes env0 nsrc) …)

A dynamic `letN` makes the specializer RESIDUALIZE its value: the slot list
becomes one residual variable, and every later read of slot `k` becomes a
fresh chain of `k` let-bound `tl`s. The residual is therefore quadratic in the
slot count. B11 (750 slots) gives 214k terms, 69k of them `tl`. B01 (40k
slots) did not finish specializing in 5 h.

The scalar route removed exactly this binding (`Proto/RewriteTotal`, the
TOTAL variant `hwAPVarT`) and proved the rewritten interpreter adequate. This
module does the same for the memory entry:

* `hwMemSVar` is `hwMemS` with `mainM`'s body rewritten by the same TOTAL
  rewrite (`goInlineT "env0"`). The kernel checks that its side conditions
  hold on the real body.
* `SEval_goInlineT` is proved once for ANY body: the rewrite preserves
  successful evaluation. It uses only `useLater = 0`, `noBranch` and
  `z ≠ x`. The remaining conditions (`selfRef`, `useInNext`, `beforeTotal`)
  are about FAILURE order, which a successful-evaluation statement does not
  need (`Proto/InterpreterVariant`'s header).
* `memVar_iff`: on every admitted step the variant's entry evaluates to
  exactly what `hwMemP`'s does.
* `projectMemVarDesignAt_correct` restates `projectMemDesignAt_correct`
  for the variant, with the same conclusion.

Nothing in the memory route's adequacy chain is reproved or edited.
-/
import LeanSemanticPrimitives.Projection.Proto.RewriteTotal
import LeanSemanticPrimitives.Projection.MemProjection
import LeanSemanticPrimitives.Projection.HardwareMemWriteAdmit
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace MemVariant

open Surface Compiler Projection.Hw Projection.HwMem Projection.MemProjection Projection.ProtoVar

/-! ## 1. Name lookup -/

theorem slookup_cons_ne {σ : SEnv} {x y : String} {d : Val} (h : x ≠ y) :
    slookup ((x, d) :: σ) y = slookup σ y := by
  simp [slookup, h]

theorem slookup_cons_self {σ : SEnv} {x : String} {d : Val} :
    slookup ((x, d) :: σ) x = some d := by
  simp [slookup]

theorem slookup_append : ∀ (l σ : SEnv) (y : String),
    slookup (l ++ σ) y = (slookup l y).or (slookup σ y)
  | [],          _, _ => by simp [slookup]
  | (z, w) :: l, σ, y => by
      by_cases h : z = y
      · subst h; simp [slookup]
      · simp only [List.cons_append, slookup_cons_ne h]; exact slookup_append l σ y

theorem slookup_zip_none : ∀ (bs : List String) (vs : List Val) (y : String),
    bs.contains y = false → slookup (bs.zip vs) y = none
  | [],      _,       _, _ => by simp [slookup]
  | _ :: _,  [],      _, _ => by simp [slookup]
  | b :: bs, v :: vs, y, h => by
      have hb : b ≠ y := by
        intro hc; subst hc; simp at h
      have hr : bs.contains y = false := by
        simp only [List.contains_cons, Bool.or_eq_false_iff] at h; exact h.2
      simp only [List.zip_cons_cons, slookup_cons_ne hb]
      exact slookup_zip_none bs vs y hr

theorem slookup_zip_some : ∀ (bs : List String) (vs : List Val) (y : String),
    bs.length = vs.length → bs.contains y = true → ∃ w, slookup (bs.zip vs) y = some w
  | [],      _,       _, _, h => by simp at h
  | _ :: _,  [],      _, hl, _ => by simp at hl
  | b :: bs, v :: vs, y, hl, h => by
      by_cases hb : b = y
      · subst hb; exact ⟨v, by simp [slookup]⟩
      · have hr : bs.contains y = true := by
          simp only [List.contains_cons, Bool.or_eq_true, beq_iff_eq] at h
          rcases h with h | h
          · exact absurd h.symm hb
          · exact h
        simp only [List.zip_cons_cons, slookup_cons_ne hb]
        exact slookup_zip_some bs vs y (by simpa using hl) hr

/-! ## 2. Substituting a binding into a branch-free expression -/

mutual

theorem subst_sound {P : SProgram} {x : String} {e : SExp} {σ : SEnv} {d : Val}
    (he : SEval P σ e d) : ∀ (e2 : SExp) (w : Val), noBranchT e2 = true →
      SEval P ((x, d) :: σ) e2 w → SEval P σ (substRefT x e e2) w
  | .lit _,       _, _,   h => by cases h; exact .lit
  | .ref y,       _, _,   h => by
      cases h with
      | ref hy =>
        by_cases hyx : y = x
        · subst hyx
          rw [slookup_cons_self] at hy
          cases hy
          simpa [substRefT] using he
        · have hne : x ≠ y := fun h => hyx h.symm
          rw [slookup_cons_ne hne] at hy
          have : (y == x) = false := beq_eq_false_iff_ne.mpr hyx
          simp only [substRefT, this]
          exact .ref hy
  | .letN _ _ _,  _, hnb, _ => by simp [noBranchT] at hnb
  | .ite _ _ _,   _, hnb, _ => by simp [noBranchT] at hnb
  | .switch _ _,  _, hnb, _ => by simp [noBranchT] at hnb
  | .prim _ es,   _, hnb, h => by
      simp only [noBranchT] at hnb
      cases h with
      | prim hes hp =>
        simp only [substRefT]
        exact .prim (substL_sound he es _ hnb hes) hp
  | .mk _ es,     _, hnb, h => by
      simp only [noBranchT] at hnb
      cases h with
      | mk hes =>
        simp only [substRefT]
        exact .mk (substL_sound he es _ hnb hes)
  | .call _ es,   _, hnb, h => by
      simp only [noBranchT] at hnb
      cases h with
      | call hes hfn hlen hb =>
        simp only [substRefT]
        exact .call (substL_sound he es _ hnb hes) hfn hlen hb

theorem substL_sound {P : SProgram} {x : String} {e : SExp} {σ : SEnv} {d : Val}
    (he : SEval P σ e d) : ∀ (es : List SExp) (ws : List Val), noBranchL es = true →
      SEvalList P ((x, d) :: σ) es ws → SEvalList P σ (substRefL x e es) ws
  | [],      _, _,   h => by cases h; exact .nil
  | e2 :: es, _, hnb, h => by
      simp only [noBranchL, Bool.and_eq_true] at hnb
      cases h with
      | cons h1 h2 =>
        simp only [substRefL]
        exact .cons (subst_sound he e2 _ hnb.1 h1) (substL_sound he es _ hnb.2 h2)

end

/-! ## 3. Dropping a binding nothing reads -/

/-- Two environments agree everywhere except possibly at `x`. -/
def Agree (x : String) (σ1 σ2 : SEnv) : Prop := ∀ y, y ≠ x → slookup σ1 y = slookup σ2 y

/-- `x` is either not read, or reads the same in both environments. -/
def XOK (x : String) (σ1 σ2 : SEnv) (n : Nat) : Prop := n = 0 ∨ slookup σ1 x = slookup σ2 x

theorem Agree.cons {x y : String} {σ1 σ2 : SEnv} (h : Agree x σ1 σ2) (d : Val) :
    Agree x ((y, d) :: σ1) ((y, d) :: σ2) := by
  intro z hz
  by_cases hyz : y = z
  · subst hyz; simp [slookup]
  · rw [slookup_cons_ne hyz, slookup_cons_ne hyz]; exact h z hz

theorem Agree.append {x : String} {σ1 σ2 : SEnv} (h : Agree x σ1 σ2) (l : SEnv) :
    Agree x (l ++ σ1) (l ++ σ2) := by
  intro z hz
  rw [slookup_append, slookup_append, h z hz]

theorem XOK.left {x : String} {σ1 σ2 : SEnv} {a b : Nat} (h : XOK x σ1 σ2 (a + b)) : XOK x σ1 σ2 a :=
  h.imp (fun h => by omega) id

theorem XOK.right {x : String} {σ1 σ2 : SEnv} {a b : Nat} (h : XOK x σ1 σ2 (a + b)) : XOK x σ1 σ2 b :=
  h.imp (fun h => by omega) id

theorem ref_agree {x y : String} {σ1 σ2 : SEnv} (hag : Agree x σ1 σ2)
    (hx : XOK x σ1 σ2 (countRefT x (.ref y))) : slookup σ1 y = slookup σ2 y := by
  by_cases hyx : y = x
  · subst hyx
    rcases hx with h0 | h0
    · simp [countRefT] at h0
    · exact h0
  · exact hag y hyx

theorem letN_agree {x y : String} {σ1 σ2 : SEnv} {e b : SExp} (hag : Agree x σ1 σ2)
    (hx : XOK x σ1 σ2 (countRefT x (.letN y e b))) (d : Val) :
    XOK x σ1 σ2 (countRefT x e) ∧ Agree x ((y, d) :: σ1) ((y, d) :: σ2)
      ∧ XOK x ((y, d) :: σ1) ((y, d) :: σ2) (countRefT x b) := by
  simp only [countRefT] at hx
  refine ⟨hx.left, hag.cons d, ?_⟩
  by_cases hyx : y = x
  · subst hyx; right; simp [slookup]
  · have hb : (y == x) = false := beq_eq_false_iff_ne.mpr hyx
    rw [hb] at hx
    rcases hx.right with h0 | h0
    · simp only [Bool.false_eq_true, if_false] at h0; exact Or.inl h0
    · right; rw [slookup_cons_ne hyx, slookup_cons_ne hyx, h0]

theorem countRefA_found {x : String} : ∀ {as : List SAlt} {tag : Nat} {bs : List String} {body : SExp},
    sFindAlt as tag = some (tag, bs, body) →
      ∃ k, countRefA x as = k + (if bs.contains x then 0 else countRefT x body)
  | [],                _, _, _, h => by simp [sFindAlt] at h
  | (t, bs', b') :: as, tag, bs, body, h => by
      have hc : countRefA x ((t, bs', b') :: as)
          = (if bs'.contains x then 0 else countRefT x b') + countRefA x as := rfl
      simp only [sFindAlt] at h
      split at h
      · cases h
        exact ⟨countRefA x as, by rw [hc]; omega⟩
      · obtain ⟨k, hk⟩ := countRefA_found (x := x) h
        exact ⟨(if bs'.contains x then 0 else countRefT x b') + k, by rw [hc, hk]; omega⟩

theorem switch_agree {x : String} {σ1 σ2 : SEnv} {s : SExp} {as : List SAlt} {tag : Nat}
    {bs : List String} {body : SExp} {vs : List Val} (hag : Agree x σ1 σ2)
    (hx : XOK x σ1 σ2 (countRefT x (.switch s as)))
    (halt : sFindAlt as tag = some (tag, bs, body)) (hlen : bs.length = vs.length) :
    XOK x σ1 σ2 (countRefT x s) ∧ Agree x (bs.zip vs ++ σ1) (bs.zip vs ++ σ2)
      ∧ XOK x (bs.zip vs ++ σ1) (bs.zip vs ++ σ2) (countRefT x body) := by
  simp only [countRefT] at hx
  refine ⟨hx.left, hag.append _, ?_⟩
  cases hc : bs.contains x with
  | true =>
      right
      obtain ⟨w, hw⟩ := slookup_zip_some bs vs x hlen hc
      rw [slookup_append, slookup_append, hw]; rfl
  | false =>
      have hnone := slookup_zip_none bs vs x hc
      rcases hx.right with h0 | h0
      · left
        obtain ⟨k, hk⟩ := countRefA_found (x := x) halt
        rw [hc] at hk
        simp only [Bool.false_eq_true, if_false] at hk
        omega
      · right
        rw [slookup_append, slookup_append, hnone]
        simpa using h0

mutual

theorem drop_sound {P : SProgram} {x : String} : ∀ (σ1 σ2 : SEnv) (b : SExp) (v : Val),
    Agree x σ1 σ2 → XOK x σ1 σ2 (countRefT x b) → SEval P σ1 b v → SEval P σ2 b v
  | _,  _,  _, _, _,   _,  .lit => .lit
  | _,  _,  _, _, hag, hx, .ref hy => .ref ((ref_agree hag hx) ▸ hy)
  | _,  _,  _, _, hag, hx, .letN he hb => by
      obtain ⟨h1, h2, h3⟩ := letN_agree hag hx _
      exact .letN (drop_sound _ _ _ _ hag h1 he) (drop_sound _ _ _ _ h2 h3 hb)
  | _,  _,  _, _, hag, hx, .iteT hc ha => by
      simp only [countRefT] at hx
      exact .iteT (drop_sound _ _ _ _ hag hx.left.left hc) (drop_sound _ _ _ _ hag hx.left.right ha)
  | _,  _,  _, _, hag, hx, .iteF hc hb => by
      simp only [countRefT] at hx
      exact .iteF (drop_sound _ _ _ _ hag hx.left.left hc) (drop_sound _ _ _ _ hag hx.right hb)
  | _,  _,  _, _, hag, hx, .prim hes hp => by
      simp only [countRefT] at hx
      exact .prim (dropL_sound _ _ _ _ hag hx hes) hp
  | _,  _,  _, _, hag, hx, .mk hes => by
      simp only [countRefT] at hx
      exact .mk (dropL_sound _ _ _ _ hag hx hes)
  | _,  _,  _, _, hag, hx, .switch hs halt hlen hb => by
      obtain ⟨h1, h2, h3⟩ := switch_agree hag hx halt hlen
      exact .switch (drop_sound _ _ _ _ hag h1 hs) halt hlen (drop_sound _ _ _ _ h2 h3 hb)
  | _,  _,  _, _, hag, hx, .call hes hfn hlen hb => by
      simp only [countRefT] at hx
      exact .call (dropL_sound _ _ _ _ hag hx hes) hfn hlen hb

theorem dropL_sound {P : SProgram} {x : String} : ∀ (σ1 σ2 : SEnv) (es : List SExp) (vs : List Val),
    Agree x σ1 σ2 → XOK x σ1 σ2 (countRefL x es) → SEvalList P σ1 es vs → SEvalList P σ2 es vs
  | _, _, _, _, _,   _,  .nil => .nil
  | _, _, _, _, hag, hx, .cons he hes => by
      simp only [countRefL] at hx
      exact .cons (drop_sound _ _ _ _ hag hx.left he) (dropL_sound _ _ _ _ hag hx.right hes)

end

/-! ## 4. The rewrite preserves successful evaluation -/

theorem SEval_inlineNextLetT {P : SProgram} {x : String} {t t' : SExp} {r : InlineReport}
    (hrw : inlineNextLetT x t = some (t', r)) (hok : r.ok = true) {σ : SEnv} {v : Val}
    (h : SEval P σ t v) : SEval P σ t' v := by
  unfold inlineNextLetT at hrw
  split at hrw
  · rename_i y e z e2 body
    split at hrw
    · rename_i hyz
      simp only [Bool.and_eq_true, beq_iff_eq, bne_iff_ne, ne_eq] at hyz
      obtain ⟨rfl, _⟩ := hyz
      dsimp only at hrw
      split at hrw
      · rename_i hcond
        simp only [Option.some.injEq, Prod.mk.injEq] at hrw
        obtain ⟨rfl, rfl⟩ := hrw
        simp only [Bool.and_eq_true, beq_iff_eq] at hcond
        obtain ⟨⟨⟨⟨_, _⟩, hlater⟩, hnb⟩, _⟩ := hcond
        cases h with
        | letN he hrest =>
          cases hrest with
          | letN he2 hbody =>
            refine .letN (subst_sound he e2 _ hnb he2) ?_
            refine drop_sound _ _ _ _ ?_ (Or.inl hlater) hbody
            intro w hw
            by_cases hzw : z = w
            · subst hzw; simp [slookup]
            · rw [slookup_cons_ne hzw, slookup_cons_ne hzw, slookup_cons_ne (fun h => hw h.symm)]
      · simp only [Option.some.injEq, Prod.mk.injEq] at hrw
        obtain ⟨-, rfl⟩ := hrw
        simp [InlineReport.ok] at hok
    · cases hrw
  · cases hrw

/-- The rewritten alternatives keep every tag and binder list; the one alternative
whose body changed was rewritten by `goInlineT`. -/
theorem SEval_goInlineT {P : SProgram} {x : String} :
    ∀ (n : Nat),
      (∀ {t t' : SExp} {r : InlineReport}, goInlineT x n t = some (t', r) → r.ok = true →
          ∀ {σ : SEnv} {v : Val}, SEval P σ t v → SEval P σ t' v)
      ∧ (∀ {as as' : List SAlt} {r : InlineReport}, goAltsT x n as = some (as', r) → r.ok = true →
          ∀ {tag : Nat} {bs : List String} {body : SExp},
            sFindAlt as tag = some (tag, bs, body) →
            ∃ body', sFindAlt as' tag = some (tag, bs, body')
              ∧ ∀ {σ : SEnv} {v : Val}, SEval P σ body v → SEval P σ body' v) := by
  intro n
  induction n with
  | zero =>
    refine ⟨fun {t t' r} h hok => ?_, fun {as as' r} h hok => ?_⟩
    · cases t with
      | letN y e b =>
        simp only [goInlineT] at h
        split at h
        · rename_i r0 hr0
          rw [h] at hr0
          exact fun hv => SEval_inlineNextLetT hr0 hok hv
        · cases h
      | switch s as => simp [goInlineT] at h
      | _ => simp [goInlineT] at h
    · cases as with
      | nil => simp [goAltsT] at h
      | cons a rest => simp [goAltsT] at h
  | succ n ih =>
    obtain ⟨ihT, ihA⟩ := ih
    refine ⟨fun {t t' r} h hok => ?_, fun {as as' r} h hok => ?_⟩
    · cases t with
      | letN y e b =>
        simp only [goInlineT] at h
        split at h
        · rename_i r0 hr0
          rw [h] at hr0
          exact fun hv => SEval_inlineNextLetT hr0 hok hv
        · simp only [Option.map_eq_some_iff] at h
          obtain ⟨⟨b', r'⟩, hb, heq⟩ := h
          simp only [Prod.mk.injEq] at heq
          obtain ⟨rfl, rfl⟩ := heq
          intro σ v hv
          cases hv with
          | letN he hbd => exact .letN he (ihT hb hok hbd)
      | switch s as =>
        simp only [goInlineT, Option.map_eq_some_iff] at h
        obtain ⟨⟨as'', r'⟩, has, heq⟩ := h
        simp only [Prod.mk.injEq] at heq
        obtain ⟨rfl, rfl⟩ := heq
        intro σ v hv
        cases hv with
        | switch hs halt hlen hb =>
          obtain ⟨body', halt', hbody⟩ := ihA has hok halt
          exact .switch hs halt' hlen (hbody hb)
      | _ => simp [goInlineT] at h
    · cases as with
      | nil => simp [goAltsT] at h
      | cons a rest =>
        obtain ⟨t0, bs0, b0⟩ := a
        simp only [goAltsT] at h
        intro tag bs body halt
        split at h
        · rename_i p hp
          simp only [Option.some.injEq, Prod.mk.injEq] at h
          obtain ⟨rfl, rfl⟩ := h
          simp only [sFindAlt] at halt ⊢
          split at halt
          · rename_i htag
            simp only [Option.some.injEq, Prod.mk.injEq] at halt
            obtain ⟨rfl, rfl, rfl⟩ := halt
            exact ⟨p.1, by simp [htag], fun hv => ihT hp hok hv⟩
          · rename_i htag
            exact ⟨body, by simp [htag, halt], id⟩
        · simp only [Option.map_eq_some_iff] at h
          obtain ⟨⟨rest', r'⟩, hrest, heq⟩ := h
          simp only [Prod.mk.injEq] at heq
          obtain ⟨rfl, rfl⟩ := heq
          simp only [sFindAlt] at halt ⊢
          split at halt
          · rename_i htag
            simp only [Option.some.injEq, Prod.mk.injEq] at halt
            obtain ⟨-, rfl, rfl⟩ := halt
            exact ⟨b0, by simp [htag], id⟩
          · rename_i htag
            obtain ⟨body', h1, h2⟩ := ihA hrest hok halt
            exact ⟨body', by simp [htag, h1], h2⟩

/-! ## 5. Swapping a function nothing else calls (B1, for any name)

`Proto/VariantTransport` proves this for the name `main`; the memory entry is
`mainM`, so the same congruence is restated with the name as a parameter. -/

mutual

def noCallB (m : String) : SExp → Bool
  | .lit _        => true
  | .ref _        => true
  | .letN _ e b   => noCallB m e && noCallB m b
  | .ite c a b    => noCallB m c && noCallB m a && noCallB m b
  | .prim _ es    => noCallL m es
  | .mk _ es      => noCallL m es
  | .switch s as  => noCallB m s && noCallA m as
  | .call f es    => (f != m) && noCallL m es

def noCallL (m : String) : List SExp → Bool
  | []      => true
  | e :: es => noCallB m e && noCallL m es

def noCallA (m : String) : List SAlt → Bool
  | []               => true
  | (_, _, b) :: as  => noCallB m b && noCallA m as

end

/-- Every function of `P` other than `m` has an `m`-free body. -/
def FreeOf (P : SProgram) (m : String) : Prop :=
  ∀ f fd, f ≠ m → sFn P f = some fd → noCallB m fd.body = true

theorem altBody_noCall {m : String} : ∀ {as : List SAlt} {tag : Nat} {p : SAlt},
    noCallA m as = true → sFindAlt as tag = some p → noCallB m p.2.2 = true
  | [],              _, _, _,   h => by simp [sFindAlt] at h
  | (k, bs, b) :: r, tag, p, hn, h => by
      simp only [noCallA, Bool.and_eq_true] at hn
      simp only [sFindAlt] at h
      split at h
      · cases h; exact hn.1
      · exact altBody_noCall hn.2 h

mutual

theorem SEval_congrN {P Q : SProgram} {m : String}
    (hag : ∀ f, f ≠ m → sFn Q f = sFn P f) (hmf : FreeOf P m) :
    ∀ (σ : SEnv) (e : SExp) (v : Val),
      noCallB m e = true → SEval P σ e v → SEval Q σ e v
  | _, _, _, _, .lit          => .lit
  | _, _, _, _, .ref hx       => .ref hx
  | σ, _, v, hnm, .letN he hb => by
      simp only [noCallB, Bool.and_eq_true] at hnm
      exact .letN (SEval_congrN hag hmf _ _ _ hnm.1 he)
                  (SEval_congrN hag hmf _ _ _ hnm.2 hb)
  | σ, _, v, hnm, .iteT hc ha => by
      simp only [noCallB, Bool.and_eq_true] at hnm
      exact .iteT (SEval_congrN hag hmf _ _ _ hnm.1.1 hc)
                  (SEval_congrN hag hmf _ _ _ hnm.1.2 ha)
  | σ, _, v, hnm, .iteF hc hb => by
      simp only [noCallB, Bool.and_eq_true] at hnm
      exact .iteF (SEval_congrN hag hmf _ _ _ hnm.1.1 hc)
                  (SEval_congrN hag hmf _ _ _ hnm.2 hb)
  | σ, _, v, hnm, .prim hes hp => by
      simp only [noCallB] at hnm
      exact .prim (SEvalList_congrN hag hmf _ _ _ hnm hes) hp
  | σ, _, _, hnm, .mk hes => by
      simp only [noCallB] at hnm
      exact SEval.mk (P := Q) (SEvalList_congrN hag hmf _ _ _ hnm hes)
  | σ, _, _, hnm, .switch hs halt hlen hb => by
      simp only [noCallB, Bool.and_eq_true] at hnm
      exact .switch (SEval_congrN hag hmf _ _ _ hnm.1 hs) halt hlen
                    (SEval_congrN hag hmf _ _ _ (altBody_noCall hnm.2 halt) hb)
  | σ, _, _, hnm, .call hes hfn hlen hb => by
      simp only [noCallB, Bool.and_eq_true, bne_iff_ne, ne_eq] at hnm
      exact SEval.call (P := Q) (SEvalList_congrN hag hmf _ _ _ hnm.2 hes)
        ((hag _ hnm.1).trans hfn) hlen
        (SEval_congrN hag hmf _ _ _ (hmf _ _ hnm.1 hfn) hb)

theorem SEvalList_congrN {P Q : SProgram} {m : String}
    (hag : ∀ f, f ≠ m → sFn Q f = sFn P f) (hmf : FreeOf P m) :
    ∀ (σ : SEnv) (es : List SExp) (vs : List Val),
      noCallL m es = true → SEvalList P σ es vs → SEvalList Q σ es vs
  | _, _, _, _, .nil => .nil
  | σ, _, _, hnm, .cons he hes => by
      simp only [noCallL, Bool.and_eq_true] at hnm
      exact .cons (SEval_congrN hag hmf _ _ _ hnm.1 he)
                  (SEvalList_congrN hag hmf _ _ _ hnm.2 hes)

end

def freeOfB (P : SProgram) (m : String) : Bool :=
  P.funs.all (fun fd => fd.name == m || noCallB m fd.body)

theorem freeOfB_sound {P : SProgram} {m : String} (h : freeOfB P m = true) : FreeOf P m := by
  intro f fd hne hfn
  obtain ⟨hmem, hname⟩ := sFn_mem hfn
  have := (List.all_eq_true.mp h) fd hmem
  simp only [Bool.or_eq_true, beq_iff_eq] at this
  rcases this with hm | hb
  · exact absurd (hname ▸ hm) (by simpa using hne)
  · exact hb

/-- A NAME-PRESERVING map of the function table: `sFn` maps with it. -/
theorem sFn_mapF_any {P : SProgram} {F : SFun → SFun} (hname : ∀ h, (F h).name = h.name)
    (f : String) : sFn { P with funs := P.funs.map F } f = (sFn P f).map F := by
  unfold sFn
  rw [funNames_mapF hname]
  cases idxOf (funNames P) f with
  | none   => simp
  | some i => simp [List.getElem?_map]

/-! ## 6. The variant program -/

/-- `mainM`'s body, read by name. -/
def mainMBody : Option SExp := (sFn hwMemS "mainM").map SFun.body

/-- The TOTAL rewrite of `Proto/RewriteTotal`, applied to `mainM`. -/
def rewrittenM : Option (SExp × InlineReport) := mainMBody.bind (goInlineT "env0" rewriteDepth)

/-- Replace `mainM`'s body by the rewritten one (identity elsewhere). -/
def swapMainM (b : SExp) (g : SFun) : SFun := if g.name == "mainM" then { g with body := b } else g

def hwMemSVar : SProgram :=
  match rewrittenM with
  | some (b, r) => if r.ok then { hwMemS with funs := hwMemS.funs.map (swapMainM b) } else hwMemS
  | none        => hwMemS

set_option maxRecDepth 100000 in
/-- The side conditions hold on the real body: CHECKED BY THE KERNEL. -/
theorem rewrittenM_applied : rewrittenM.map (fun p => p.2.ok) = some true := by rfl

set_option maxRecDepth 100000 in
/-- The `env0` binder is there before and gone after. -/
theorem env0M_bound_once_before : mainMBody.map (countBindT "env0") = some 1 := by rfl

set_option maxRecDepth 100000 in
theorem env0M_binder_gone_after : rewrittenM.map (fun p => countBindT "env0" p.1) = some 0 := by rfl

set_option maxRecDepth 100000 in
/-- The rewritten body calls no `mainM`. -/
theorem rewrittenM_free : rewrittenM.map (fun p => noCallB "mainM" p.1) = some true := by rfl

set_option maxRecDepth 100000 in
/-- No function of `hwMemS` other than `mainM` calls `mainM`. -/
theorem freeOfB_hwMemS : freeOfB hwMemS "mainM" = true := by rfl

theorem rewrittenM_spec : ∃ fd b r, sFn hwMemS "mainM" = some fd
    ∧ goInlineT "env0" rewriteDepth fd.body = some (b, r) ∧ rewrittenM = some (b, r)
    ∧ r.ok = true ∧ noCallB "mainM" b = true := by
  have h1 := rewrittenM_applied
  have h2 := rewrittenM_free
  unfold rewrittenM mainMBody at h1 h2 ⊢
  cases hfd : sFn hwMemS "mainM" with
  | none => rw [hfd] at h1; simp at h1
  | some fd =>
    rw [hfd] at h1 h2
    simp only [Option.map_some, Option.bind_some] at h1 h2 ⊢
    cases hg : goInlineT "env0" rewriteDepth fd.body with
    | none => rw [hg] at h1; simp at h1
    | some p =>
      rw [hg] at h1 h2
      simp only [Option.map_some, Option.some.injEq] at h1 h2
      exact ⟨fd, p.1, p.2, rfl, by rw [hg], rfl, h1, h2⟩

theorem swapMainM_name (b : SExp) (g : SFun) : (swapMainM b g).name = g.name := by
  unfold swapMainM; split <;> rfl

theorem hwMemSVar_eq : ∃ b, hwMemSVar = { hwMemS with funs := hwMemS.funs.map (swapMainM b) }
    ∧ ∃ fd r, sFn hwMemS "mainM" = some fd ∧ goInlineT "env0" rewriteDepth fd.body = some (b, r)
      ∧ r.ok = true ∧ noCallB "mainM" b = true := by
  obtain ⟨fd, b, r, hfd, hg, hrw, hok, hfree⟩ := rewrittenM_spec
  refine ⟨b, ?_, fd, r, hfd, hg, hok, hfree⟩
  unfold hwMemSVar; rw [hrw]; simp [hok]

theorem sFn_hwMemSVar_ne (f : String) (hne : f ≠ "mainM") : sFn hwMemSVar f = sFn hwMemS f := by
  obtain ⟨b, hv, -⟩ := hwMemSVar_eq
  rw [hv, sFn_mapF_any (swapMainM_name b)]
  cases h : sFn hwMemS f with
  | none => rfl
  | some g =>
    have hn := (sFn_mem h).2
    simp only [Option.map_some, Option.some.injEq]
    unfold swapMainM
    have : (g.name == "mainM") = false := by rw [hn]; exact beq_eq_false_iff_ne.mpr hne
    rw [this]; rfl

/-! ## 7. Every successful `mainM` call of `hwMemS` is one of the variant -/

theorem SEvalList_lits_inj {P : SProgram} {σ : SEnv} : ∀ (args vs : List Val),
    SEvalList P σ (args.map SExp.lit) vs → vs = args
  | [],      _, h => by cases h; rfl
  | a :: as, _, h => by
      cases h with
      | cons ha ht => cases ha; rw [SEvalList_lits_inj as _ ht]

theorem mainM_transfer {vs : List Val} {v : Val}
    (h : SEval hwMemS [] (.call "mainM" (vs.map SExp.lit)) v) :
    SEval hwMemSVar [] (.call "mainM" (vs.map SExp.lit)) v := by
  obtain ⟨b, hv, fd, r, hfd, hg, hok, hfree⟩ := hwMemSVar_eq
  cases h with
  | call hes hfn hlen hb =>
    rename_i vs' fd'
    rw [hfd] at hfn
    cases hfn
    -- the body, rewritten, still evaluates to `v` (in `hwMemS`) ...
    have h1 := (SEval_goInlineT rewriteDepth).1 hg hok hb
    -- ... and moves to the variant: it calls no `mainM`, and nothing else changed
    have h2 := SEval_congrN (Q := hwMemSVar) (m := "mainM") sFn_hwMemSVar_ne
      (freeOfB_sound freeOfB_hwMemS) _ _ _ hfree h1
    have hfv : sFn hwMemSVar "mainM" = some { fd with body := b } := by
      rw [hv, sFn_mapF_any (swapMainM_name b), hfd]
      simp only [Option.map_some, Option.some.injEq]
      unfold swapMainM
      have : (fd.name == "mainM") = true := by rw [(sFn_mem hfd).2]; rfl
      rw [this]; rfl
    have hvs : vs' = vs := SEvalList_lits_inj vs vs' hes
    subst hvs
    exact .call (SEvalList_lits vs') hfv hlen h2

/-! ## 8. Resolution and binding-time analysis of the variant

Kernel reductions, as for `hwMemA` (`MemProjection`). -/

def hwMemVarResolved : Except String (Program × List Bool) := resolveProgram hwMemSVar
def hwMemPVar : Program := match hwMemVarResolved with | .ok (p, _) => p | .error _ => ⟨[], 0⟩
def hwMemInlVar : List Bool := match hwMemVarResolved with | .ok (_, i) => i | .error _ => []
def hwMemAVar : Except BTAError AProgram := bta hwMemPVar hwMemInlVar [.stat, .dyn, .dyn, .dyn] 200
def hwMemAPVar : AProgram := match hwMemAVar with | .ok a => a | .error _ => ⟨[], 0⟩

set_option maxHeartbeats 4000000 in
set_option maxRecDepth 1000000 in
theorem hwMemVarResolved_isOk : Option.isSome (Except.toOption hwMemVarResolved) = true := rfl

theorem hwMemPVar_resolves : resolveProgram hwMemSVar = .ok (hwMemPVar, hwMemInlVar) := by
  have h0 := hwMemVarResolved_isOk
  unfold hwMemPVar hwMemInlVar
  unfold hwMemVarResolved at h0 ⊢
  cases h : resolveProgram hwMemSVar with
  | ok pi   => cases pi; rfl
  | error e => rw [h] at h0; simp [Except.toOption] at h0

theorem hwMemSVar_entry : hwMemSVar.entry = "mainM" := by
  obtain ⟨b, hv, -⟩ := hwMemSVar_eq
  rw [hv]; rfl

set_option maxHeartbeats 4000000 in
set_option maxRecDepth 1000000 in
theorem hwMemAVar_isOk : Option.isSome (Except.toOption hwMemAVar) = true := rfl

set_option maxHeartbeats 4000000 in
set_option maxRecDepth 1000000 in
theorem hwMemAPVar_entry_params :
    (hwMemAPVar.fn hwMemAPVar.entry).map AFunDef.params = some [.stat, .dyn, .dyn, .dyn] := rfl

theorem hwMemAVar_ok : hwMemAVar = .ok hwMemAPVar := by
  have h0 := hwMemAVar_isOk
  unfold hwMemAPVar
  cases h : hwMemAVar with
  | ok a    => rfl
  | error z => rw [h] at h0; simp [Except.toOption] at h0

theorem hwMemAPVar_erases : eraseProgram hwMemAPVar = hwMemPVar := bta_erases hwMemAVar_ok

theorem hwMemAPVar_wf : wfAProgram hwMemAPVar = true := bta_sound hwMemAVar_ok

theorem hwMemAPVar_entry :
    ∃ afd, hwMemAPVar.fn hwMemAPVar.entry = some afd
         ∧ afd.params = [.stat, .dyn, .dyn, .dyn] := by
  have h := hwMemAPVar_entry_params
  cases hfn : hwMemAPVar.fn hwMemAPVar.entry with
  | none     => rw [hfn] at h; simp at h
  | some afd =>
      rw [hfn] at h
      simp only [Option.map_some, Option.some.injEq] at h
      exact ⟨afd, rfl, h⟩

/-! ## 9. On every admitted step the two entries agree -/

/-- `admits_run`, one level down: the `SEval` derivation `mainM_adequate` builds,
under exactly `admits_run`'s premises. -/
theorem admits_run_seval {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hA : admitsB D = true) (hR : runtimeOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      SEval hwMemS []
        (.call "mainM" [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                        .lit (StateM.encStateM st)])
        (StateM.encResultM r) := by
  unfold runtimeOKB at hR
  simp only [Bool.and_eq_true] at hR
  obtain ⟨⟨⟨hst, hin⟩, hfb⟩, hed⟩ := hR
  obtain ⟨hwf, hsem, hgsrc, hadm, harity, hprop, -⟩ := admits_static_premises hA
  obtain ⟨r, hev, -, -⟩ :=
    mainM_adequate (G := geomAt D) hwf hsem (stateOKB_shape hst) hgsrc hadm harity hprop
      (admits_writeAddrFits hA i st) (inputOKB_sound hin) (flopBoundOKB_sound hfb)
      (edgesOKB_flops hed) (edgesOKB_mems hed) (stateOKB_flopSize hst)
  exact ⟨r, hev⟩

/-- The same, on the plain-write route (`admitsWB`, `HardwareMemWriteAdmit`). -/
theorem admits_run_sevalW {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hA : admitsWB D = true) (hR : runtimeOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      SEval hwMemS []
        (.call "mainM" [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                        .lit (StateM.encStateM st)])
        (StateM.encResultM r) := by
  unfold runtimeOKB at hR
  simp only [Bool.and_eq_true] at hR
  obtain ⟨⟨⟨hst, hin⟩, hfb⟩, hed⟩ := hR
  have hwf   := admitsW_wf hA
  have hsem  := admitsW_semwf hA
  have hadm  := admitsW_nodeAdmitted hA
  have harit := admitsW_arity hA
  have hprop := admitsW_writePropagates hA
  have hfits := admits_writeAddrFitsW hA i st
  obtain ⟨r, hev, -, -⟩ :=
    mainM_adequate_gen (G := geomAtW D) hwf hsem (stateOKB_shape hst) (admitsW_geom_src hA)
      (fun j c hcj hj hmemc ss hpre =>
        node_stepW hwf hsem hcj hj (hadm _ hmemc) (harit _ hmemc)
          (hprop j _ hcj) (hfits _ hmemc) hpre)
      (inputOKB_sound hin) (flopBoundOKB_sound hfb) (edgesOKB_flops hed) (edgesOKB_mems hed)
      (stateOKB_flopSize hst)
  exact ⟨r, hev⟩

/-- From ONE `SEval` derivation of `hwMemS`'s `mainM`: the two resolved entries
evaluate to the same values. -/
theorem memVar_iff_of {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hw : ∃ r : StateM.ResultM,
      SEval hwMemS []
        (.call "mainM" [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                        .lit (StateM.encStateM st)])
        (StateM.encResultM r)) (v : Val) :
    Eval hwMemPVar [] (.call hwMemPVar.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
      ↔ Eval hwMemP [] (.call hwMemP.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v := by
  obtain ⟨r, hs⟩ := hw
  have hP : Eval hwMemP [] (.call hwMemP.entry
      ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
      (StateM.encResultM r) := by
    refine SEval_entry hwMemP_resolves ?_
    simpa [hwMemS_entry] using hs
  have hV : Eval hwMemPVar [] (.call hwMemPVar.entry
      ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
      (StateM.encResultM r) := by
    refine SEval_entry hwMemPVar_resolves ?_
    rw [hwMemSVar_entry]
    exact mainM_transfer (vs := [encDesign D, encEdges e, encInput i, StateM.encStateM st]) hs
  constructor
  · intro h; rw [Eval_det h hV]; exact hP
  · intro h; rw [Eval_det h hP]; exact hV

/-- **The variant computes what the memory interpreter computes**, on every
admitted step: its entry evaluates to `v` exactly when `hwMemP`'s does. -/
theorem memVar_iff {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hA : admitsB D = true) (hR : runtimeOKB D i st e = true) (v : Val) :
    Eval hwMemPVar [] (.call hwMemPVar.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
      ↔ Eval hwMemP [] (.call hwMemP.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v :=
  memVar_iff_of (admits_run_seval hA hR) v

theorem memVar_iffW {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hA : admitsWB D = true) (hR : runtimeOKB D i st e = true) (v : Val) :
    Eval hwMemPVar [] (.call hwMemPVar.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
      ↔ Eval hwMemP [] (.call hwMemP.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v :=
  memVar_iff_of (admits_run_sevalW hA hR) v

/-! ## 10. The first projection through the variant -/

def projectMemVarDesignAt (sf wf : Nat) (D : DesignCert) : Except MixError Program :=
  mixDriver sf wf hwMemAPVar [encDesign D]

/-- **`projectMemDesignAt_correct`, for the variant**: the same conclusion. -/
theorem projectMemVarDesignAt_correct {sf wf : Nat} {D : DesignCert} {R : Program}
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hproj : projectMemVarDesignAt sf wf D = .ok R)
    (hA : admitsB D = true) (hR : runtimeFullOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true
      ∧ ∀ v : Val,
          Eval R [] (.call R.entry
            ([encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
          ↔ v = StateM.encResultM r := by
  obtain ⟨r, hden, hok, hiff⟩ := memEntry_unique hA hR
  have hR0 : runtimeOKB D i st e = true := by
    unfold runtimeFullOKB at hR; simp only [Bool.and_eq_true] at hR; exact hR.1
  refine ⟨r, hden, hok, fun v => ?_⟩
  exact specializeFour_correct_of hwMemAPVar_entry hwMemAPVar_erases
    (fun w => (memVar_iff hA hR0 w).trans (hiff w)) hproj v

/-- The same, on the plain-write route. -/
theorem projectMemVarDesignAtW_correct {sf wf : Nat} {D : DesignCert} {R : Program}
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hproj : projectMemVarDesignAt sf wf D = .ok R)
    (hA : admitsWB D = true) (hR : runtimeFullOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true
      ∧ ∀ v : Val,
          Eval R [] (.call R.entry
            ([encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
          ↔ v = StateM.encResultM r := by
  obtain ⟨r, hden, hok, hiff⟩ := memEntry_uniqueW hA hR
  have hR0 : runtimeOKB D i st e = true := by
    unfold runtimeFullOKB at hR; simp only [Bool.and_eq_true] at hR; exact hR.1
  refine ⟨r, hden, hok, fun v => ?_⟩
  exact specializeFour_correct_of hwMemAPVar_entry hwMemAPVar_erases
    (fun w => (memVar_iffW hA hR0 w).trans (hiff w)) hproj v

#audit_axioms subst_sound
#audit_axioms drop_sound
#audit_axioms SEval_inlineNextLetT
#audit_axioms SEval_goInlineT
#audit_axioms SEval_congrN
#audit_axioms rewrittenM_applied
#audit_axioms env0M_binder_gone_after
#audit_axioms freeOfB_hwMemS
#audit_axioms mainM_transfer
#audit_axioms hwMemPVar_resolves
#audit_axioms hwMemAPVar_erases
#audit_axioms hwMemAPVar_entry
#audit_axioms admits_run_seval
#audit_axioms memVar_iff
#audit_axioms projectMemVarDesignAt_correct
#audit_axioms admits_run_sevalW
#audit_axioms memVar_iffW
#audit_axioms projectMemVarDesignAtW_correct

end MemVariant
end Projection

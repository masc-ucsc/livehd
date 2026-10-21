/-
`Sym.ConjT` -- conjunctive induction with REGISTER facts, case splits and
substitution (M8 A4/A6).

`Sym.Conj` takes conjuncts on outputs only, and every step obligation starts
from a fully free state. Hardware gives more structure than that:

  * registers that are EQUAL (two cores' copies of one architectural value),
    possibly only in some control state;
  * a control register with a few legal values (a one-hot FSM state), in each
    of which most of the next-state logic is a single mux branch.

Here a conjunct is an output property, `eq f g` (flops `f` and `g` hold equal
values), `eqIf c f g` (they do whenever the case `c` holds: an assignment of
values to flops) or `oneOf cs` (the state is in one of the cases `cs`). A
target may be SPLIT on a `oneOf` conjunct: one STEP obligation per case. Each
case's step starts from a TEMPLATE state: the case's flops are CONSTANTS, and
for every `eq` (and every `eqIf` whose condition the case contains) one flop
reads the other's VARIABLE. Constants fold the control logic while the
residual is evaluated (`Sym.Prim`: reading a literal is concrete); shared
variables make equal registers' next-state logic the same expression.

Soundness: at cycle `n` every conjunct holds (induction), so the state is in
one of the split's cases and satisfies every fact the template uses; such a
state is represented by the template (`stateVarT_rel`). The templates use ALL
equality conjuncts, not only the target's hypotheses -- the induction gives
all of them -- so a template depends on the case alone and one symbolic run
serves every target in that case. `runConjTOf_sound_ref`. k = 1.
Mathlib-free.
-/
import HDLProof.Sym.Conj
import LeanSemanticPrimitives.Projection.AxiomGate

open Projection
namespace HDLProof.Sym
open Std.Tactic.BVDecide Projection.MemProjection Projection.Hw Projection.HwMem Compiler

/-! ## Facts about registers -/

/-- The value of flop `k`, if there is one. -/
def flopVal (fl : Array BV) (k : Nat) : Option Int := (fl[k]?).map bv_uint

/-- An assignment of values to flops. -/
abbrev Case := List (Nat × Int)

def caseHolds (fl : Array BV) (c : Case) : Bool := c.all fun q => flopVal fl q.1 == some q.2

inductive ConjK where
  | out   (p : PropSpec)
  | eq    (f g : Nat)
  | eqIf  (c : Case) (f g : Nat)
  | oneOf (cs : List Case)
  deriving Repr, Inhabited

/-- A conjunct at a cycle: the flops at its start, the outputs it produces. -/
def ConjK.holds (fl : Array BV) (outs : Array BV) : ConjK → Bool
  | .out p => propBit outs p
  | .eq f g => (flopVal fl f).isSome && flopVal fl f == flopVal fl g
  | .eqIf c f g => !caseHolds fl c || ((flopVal fl f).isSome && flopVal fl f == flopVal fl g)
  | .oneOf cs => cs.any (caseHolds fl)

/-! ## Registers of a symbolic state -/

/-- The flop list of a state value. -/
def stFlops : SVal → Option SVal
  | .ctor t [fl, _] => if t = StateM.tagStateM then some fl else none
  | .conc (.ctor t [fl, _]) => if t = StateM.tagStateM then some (.conc fl) else none
  | _ => none

theorem stFlops_sound {σ : Asg} {s : SVal} {st : StateM} {fl : SVal}
    (hr : SRel σ s (StateM.encStateM st)) (h : stFlops s = some fl) :
    SRel σ fl (encListG encBV st.flops.toList) := by
  unfold StateM.encStateM at hr
  unfold stFlops at h
  split at h
  · rename_i t f m
    split at h
    · cases h
      obtain ⟨vs, he, rl⟩ := SRel_ctor_inv hr
      cases he
      obtain ⟨_, _, h1, r1, _⟩ := SRelL_cons_inv rl
      cases h1
      simpa [encBVs, encArr] using r1
    · cases h
  · rename_i t f m
    split at h
    · cases h
      have := SRel_conc_inv hr
      cases this
      exact .conc
    · cases h
  · cases h

/-- Flop `k` of a state value, as an expression of its width. -/
def flopE (s : SVal) (k : Nat) : Option (Σ w, BVExpr w) := do
  let fl ← stFlops s
  let x ← sNth fl k
  bvOfS x

theorem flopE_sound {σ : Asg} {s : SVal} {st : StateM} {k w : Nat} {e : BVExpr w}
    (hr : SRel σ s (StateM.encStateM st)) (h : flopE s k = some ⟨w, e⟩) :
    ∃ b, st.flops[k]? = some b ∧ b.width = w ∧ bv_uint b = ((e.eval σ).toNat : Int) := by
  simp only [flopE, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨fl, hfl, x, hx, hb⟩ := h
  obtain ⟨b, hbk, rb⟩ := sNth_sound fl st.flops.toList k x (stFlops_sound hr hfl) hx
  obtain ⟨b', hb', hw, hu⟩ := asSBV_sound rb hb
  have hbb : asBV (encBV b) = some b := asBV_ofBV b
  rw [hbb] at hb'
  cases hb'
  exact ⟨b, by simpa using hbk, hw, hu⟩

/-- `flop k = v`. Out of range for the flop's width it can never hold. -/
def valLit (s : SVal) (q : Nat × Int) : Option BVLogicalExpr := do
  let ⟨w, e⟩ ← flopE s q.1
  pure (if 0 ≤ q.2 ∧ q.2 < 2 ^ w then .literal (.bin e .eq (.const (BitVec.ofNat w q.2.toNat)))
        else .const false)

theorem valLit_sound {σ : Asg} {s : SVal} {st : StateM} {q : Nat × Int} {φ : BVLogicalExpr}
    (hr : SRel σ s (StateM.encStateM st)) (h : valLit s q = some φ) :
    φ.eval σ = (flopVal st.flops q.1 == some q.2) := by
  simp only [valLit, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨⟨w, e⟩, he, rfl⟩ := h
  obtain ⟨b, hb, hw, hu⟩ := flopE_sound hr he
  simp only [flopVal, hb, Option.map_some]
  have h0 := bv_uint_nonneg' b
  have h1 := bv_uint_lt' b
  rw [hw] at h1
  have hsome : (some (bv_uint b) == some q.2) = (bv_uint b == q.2) := by
    by_cases hc : bv_uint b = q.2 <;> simp [hc]
  rw [hsome]
  split
  · rename_i hq
    simp only [BVLogicalExpr.eval, BoolExpr.eval, BVPred.eval, BVBinPred.eval, BVExpr.eval_const]
    have hn : q.2.toNat < 2 ^ w := by
      have : (q.2.toNat : Int) < ((2 ^ w : Nat) : Int) := by
        rw [Int.toNat_of_nonneg hq.1]; exact_mod_cast hq.2
      exact_mod_cast this
    have hofv : (BitVec.ofNat w q.2.toNat).toNat = q.2.toNat := by
      rw [BitVec.toNat_ofNat, Nat.mod_eq_of_lt hn]
    have hiff : e.eval σ = BitVec.ofNat w q.2.toNat ↔ (((e.eval σ).toNat : Int) = q.2) := by
      constructor
      · intro hc
        have : (e.eval σ).toNat = q.2.toNat := by rw [hc, hofv]
        rw [this, Int.toNat_of_nonneg hq.1]
      · intro hc
        apply BitVec.eq_of_toNat_eq
        rw [hofv]
        have := congrArg Int.toNat hc
        simpa using this
    rw [hu]
    by_cases hv : (((e.eval σ).toNat : Int) = q.2)
    · have h1 : (e.eval σ == BitVec.ofNat w q.2.toNat) = true := beq_iff_eq.mpr (hiff.mpr hv)
      have h2 : (((e.eval σ).toNat : Int) == q.2) = true := beq_iff_eq.mpr hv
      rw [h1, h2]
    · have h1 : (e.eval σ == BitVec.ofNat w q.2.toNat) = false := by simpa using mt hiff.mp hv
      have h2 : (((e.eval σ).toNat : Int) == q.2) = false := by simpa using hv
      rw [h1, h2]
  · rename_i hq
    simp only [BVLogicalExpr.eval, BoolExpr.eval]
    have : bv_uint b ≠ q.2 := by
      intro hc; apply hq; rw [← hc]; exact ⟨h0, h1⟩
    simp [this]

/-- Two expressions equal, when their widths agree. -/
def eqLitW {wf wg : Nat} (ef : BVExpr wf) (eg : BVExpr wg) : Option BVLogicalExpr :=
  if h : wg = wf then some (.literal (.bin ef .eq (h ▸ eg))) else none

theorem eqLitW_sound {σ : Asg} {wf wg : Nat} {ef : BVExpr wf} {eg : BVExpr wg} {φ : BVLogicalExpr}
    (h : eqLitW ef eg = some φ) : φ.eval σ = decide ((ef.eval σ).toNat = (eg.eval σ).toNat) := by
  unfold eqLitW at h
  split at h
  · rename_i hw
    subst hw
    simp only [Option.some.injEq] at h
    subst h
    simp only [BVLogicalExpr.eval, BoolExpr.eval, BVPred.eval, BVBinPred.eval]
    by_cases he : ef.eval σ = eg.eval σ
    · simp [he]
    · have : (ef.eval σ).toNat ≠ (eg.eval σ).toNat := fun hc => he (BitVec.eq_of_toNat_eq hc)
      simp [he, this]
  · cases h

/-- `flop f = flop g`, when their widths agree. -/
def eqLit (s : SVal) (f g : Nat) : Option BVLogicalExpr := do
  let ⟨_, ef⟩ ← flopE s f
  let ⟨_, eg⟩ ← flopE s g
  eqLitW ef eg

theorem eqLit_sound {σ : Asg} {s : SVal} {st : StateM} {f g : Nat} {φ : BVLogicalExpr}
    (hr : SRel σ s (StateM.encStateM st)) (h : eqLit s f g = some φ) :
    φ.eval σ = ((flopVal st.flops f).isSome && flopVal st.flops f == flopVal st.flops g) := by
  simp only [eqLit, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨⟨wf, ef⟩, hf, ⟨wg, eg⟩, hg, h⟩ := h
  obtain ⟨b, hb, _, hub⟩ := flopE_sound hr hf
  obtain ⟨c, hc, _, huc⟩ := flopE_sound hr hg
  rw [eqLitW_sound h]
  simp only [flopVal, hb, hc, Option.map_some, Option.isSome_some, Bool.true_and]
  rw [hub, huc]
  by_cases he : (ef.eval σ).toNat = (eg.eval σ).toNat
  · simp [he]
  · have : ((ef.eval σ).toNat : Int) ≠ (eg.eval σ).toNat := fun hc => he (by exact_mod_cast hc)
    simp [he, this]

/-- A case holds: every assignment in it. -/
def caseLit (s : SVal) (c : Case) : Option BVLogicalExpr := (c.mapM (valLit s)).map conjE

theorem mapM_valLit {σ : Asg} {s : SVal} {st : StateM} (hr : SRel σ s (StateM.encStateM st)) :
    ∀ (c : Case) (ls : List BVLogicalExpr), c.mapM (valLit s) = some ls →
      (conjE ls).eval σ = caseHolds st.flops c
  | [], ls, h => by simp at h; subst h; rfl
  | q :: c, ls, h => by
      simp only [List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨x, hx, xs, hxs, rfl⟩ := h
      rw [conjE, eval_and, valLit_sound hr hx, mapM_valLit hr c xs hxs]
      simp [caseHolds, List.all_cons]

theorem caseLit_sound {σ : Asg} {s : SVal} {st : StateM} (hr : SRel σ s (StateM.encStateM st))
    (c : Case) (φ : BVLogicalExpr) (h : caseLit s c = some φ) : φ.eval σ = caseHolds st.flops c := by
  simp only [caseLit, Option.map_eq_some_iff] at h
  obtain ⟨ls, hls, rfl⟩ := h
  exact mapM_valLit hr c ls hls

/-- A conjunct, from a state value and an output vector. -/
def holdsE (s so : SVal) : ConjK → Option BVLogicalExpr
  | .out p => propOf so p
  | .eq f g => eqLit s f g
  | .eqIf c f g => do
      let cl ← caseLit s c
      let el ← eqLit s f g
      pure (.gate .or (.not cl) el)
  | .oneOf cs => (cs.mapM (caseLit s)).map disjE

theorem holdsE_sound {σ : Asg} {s so : SVal} {st : StateM} {outs : Array BV}
    (hr : SRel σ s (StateM.encStateM st)) (ho : SRel σ so (encBVs outs)) :
    ∀ (k : ConjK) (φ : BVLogicalExpr), holdsE s so k = some φ → φ.eval σ = k.holds st.flops outs
  | .out p, φ, h => propOf_sound ho h
  | .eq f g, φ, h => eqLit_sound hr h
  | .eqIf c f g, φ, h => by
      simp only [holdsE, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨cl, hc, el, he, rfl⟩ := h
      rw [eval_or, eval_not, caseLit_sound hr c cl hc, eqLit_sound hr he]
      rfl
  | .oneOf cs, φ, h => by
      simp only [holdsE, Option.map_eq_some_iff] at h
      obtain ⟨ls, hls, rfl⟩ := h
      rw [eval_disjE]
      show ls.any _ = cs.any _
      induction cs generalizing ls with
      | nil => simp at hls; subst hls; rfl
      | cons c cs ih =>
          simp only [List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
            Option.some.injEq] at hls
          obtain ⟨l, hl, ls', hls', rfl⟩ := hls
          rw [List.any_cons, List.any_cons, caseLit_sound hr c l hl, ih ls' hls']

/-! ## Templates -/

/-- One substitution in the step's free state: flop `f` reads flop `g`'s
variable, or is the constant `v`. -/
inductive TFact where
  | alias (f g : Nat)
  | cnst  (f : Nat) (v : Int)
  deriving Repr, Inhabited, DecidableEq

def TFact.key : TFact → Nat
  | .alias f _ => f
  | .cnst f _ => f

def TFact.Holds (fl : Array BV) : TFact → Prop
  | .alias f g => ∃ b c, fl[f]? = some b ∧ fl[g]? = some c ∧ b.width = c.width ∧ bv_uint b = bv_uint c
  | .cnst f v => ∃ b, fl[f]? = some b ∧ bv_uint b = v

abbrev Tmpl := List TFact

/-- Flop `f` (width `w`) of the template state whose variables start at `k`. -/
def tEntry (T : Tmpl) (k f w : Nat) : SVal :=
  match T.find? (fun x => x.key == f) with
  | some (.alias _ g) => bvVar w (k + g)
  | some (.cnst _ v) => .bv w (.const (BitVec.ofNat w v.toNat))
  | none => bvVar w (k + f)

def bvVarsT (T : Tmpl) (k : Nat) : List Nat → Nat → List SVal
  | [], _ => []
  | w :: ws, f => tEntry T k f w :: bvVarsT T k ws (f + 1)

def stateVarT (L : Shape) (T : Tmpl) (k : Nat) : SVal :=
  .ctor StateM.tagStateM [sListOf (bvVarsT T k L.fws 0), sListOf (imgsVar L.mds (k + L.fws.length))]

theorem pbvOf_eq {b c : BV} (hw : b.width = c.width) (hv : bv_uint b = bv_uint c) : pbvOf b = pbvOf c := by
  unfold pbvOf; rw [hv]
  cases b; cases c; simp only at hw; subst hw; rfl

theorem agree_at {σ : Asg} {k : Nat} {xs : List BV} (h : AgreeAt σ k (xs.map pbvOf)) {i : Nat} {b : BV}
    (hb : xs[i]? = some b) : σ.get (k + i) = pbvOf b :=
  h i (pbvOf b) (by simp [hb])

theorem tEntry_rel {σ : Asg} {T : Tmpl} {k f : Nat} {xs : List BV} {b : BV}
    (hag : AgreeAt σ k (xs.map pbvOf)) (hT : ∀ x ∈ T, x.Holds xs.toArray) (hb : xs[f]? = some b) :
    SRel σ (tEntry T k f b.width) (ofBV b) := by
  unfold tEntry
  split
  · rename_i f' g hfind
    have hmem := List.mem_of_find?_eq_some hfind
    have hkey := List.find?_some hfind
    simp only [TFact.key, beq_iff_eq] at hkey
    subst hkey
    obtain ⟨b', c, hb', hc, hw, hv⟩ := hT _ hmem
    simp only [List.getElem?_toArray] at hb' hc
    rw [hb] at hb'; cases hb'
    exact bvVar_rel rfl (by rw [agree_at hag hc, pbvOf_eq hw hv])
  · rename_i f' v hfind
    have hmem := List.mem_of_find?_eq_some hfind
    have hkey := List.find?_some hfind
    simp only [TFact.key, beq_iff_eq] at hkey
    subst hkey
    obtain ⟨b', hb', hv⟩ := hT _ hmem
    simp only [List.getElem?_toArray] at hb'
    rw [hb] at hb'; cases hb'
    refine .bv (asBV_ofBV b) rfl ?_
    rw [BVExpr.eval_const, BitVec.toNat_ofNat, ← hv]
    have h0 := bv_uint_nonneg' b
    have h1 := bv_uint_lt' b
    have hlt : (bv_uint b).toNat < 2 ^ b.width := by
      have : ((bv_uint b).toNat : Int) < ((2 ^ b.width : Nat) : Int) := by
        rw [Int.toNat_of_nonneg h0]; exact_mod_cast h1
      exact_mod_cast this
    rw [Nat.mod_eq_of_lt hlt, Int.toNat_of_nonneg h0]
  · exact bvVar_rel rfl (agree_at hag hb)

theorem bvVarsT_rel {σ : Asg} {T : Tmpl} {k : Nat} {xs : List BV}
    (hag : AgreeAt σ k (xs.map pbvOf)) (hT : ∀ x ∈ T, x.Holds xs.toArray) :
    ∀ (f : Nat), SRelLF σ ofBV (bvVarsT T k ((xs.drop f).map BV.width) f) (xs.drop f) := by
  intro f
  generalize hys : xs.drop f = ys
  induction ys generalizing f with
  | nil => exact .nil
  | cons y ys ih =>
      have hy : xs[f]? = some y := by
        have := congrArg List.head? hys
        simpa [List.head?_drop] using this
      have hrest : xs.drop (f + 1) = ys := by
        rw [← List.drop_drop, hys]; rfl
      simp only [List.map_cons, bvVarsT]
      exact .cons (tEntry_rel hag hT hy) (ih (f + 1) hrest)

/-- **A template state represents every state that meets its facts.** -/
theorem stateVarT_rel {σ : Asg} {L : Shape} {T : Tmpl} {st : StateM} {k : Nat} (hm : StMatches L st)
    (ha : AgreeAt σ k (stateVals st)) (hT : ∀ x ∈ T, x.Holds st.flops) :
    SRel σ (stateVarT L T k) (StateM.encStateM st) := by
  obtain ⟨hf, hl, hs⟩ := hm
  unfold stateVals at ha
  rw [AgreeAt_append] at ha
  unfold StateM.encStateM stateVarT encBVs encArr
  have hT' : ∀ x ∈ T, x.Holds st.flops.toList.toArray := by simpa using hT
  have := bvVarsT_rel ha.1 hT' 0
  simp only [List.drop_zero] at this
  rw [hf] at this
  refine .ctor (.cons (sListOf_rel ofBV this) (.cons ?_ .nil))
  have e : k + (st.flops.toList.map pbvOf).length = k + L.fws.length := by rw [← hf]; simp
  rw [e] at ha
  exact sListOf_rel _ (imgsVar_rel L.mds st.mems.toList _ (by simpa using hl)
    (fun j m aw dw h1 h2 => hs j m aw dw (by simpa using h1) h2) ha.2)

/-! ## The specification -/

structure ConjTSpec where
  conjs  : List ConjK
  /-- per conjunct: the conjuncts its STEP assumes (all when absent) -/
  hyps   : List (Option (List Nat)) := []
  /-- per conjunct: the `oneOf` conjunct its STEP is split on, if any -/
  split  : List (Option Nat) := []
  asmOut : Option PropSpec := none
  asmIn  : List (Nat × Int) := []
  fuel   : Nat
  inW    : List Nat
  L      : Shape

/-- The runs and side conditions of `Sym.Conj` need only these fields. -/
def ConjTSpec.plain (sp : ConjTSpec) : ConjSpec :=
  { conjs := [], asmOut := sp.asmOut, asmIn := sp.asmIn, fuel := sp.fuel, inW := sp.inW, L := sp.L }

@[simp] theorem ConjTSpec.plain_inW (sp : ConjTSpec) : sp.plain.inW = sp.inW := rfl
@[simp] theorem ConjTSpec.plain_asmOut (sp : ConjTSpec) : sp.plain.asmOut = sp.asmOut := rfl
@[simp] theorem ConjTSpec.plain_asmIn (sp : ConjTSpec) : sp.plain.asmIn = sp.asmIn := rfl
@[simp] theorem ConjTSpec.plain_fuel (sp : ConjTSpec) : sp.plain.fuel = sp.fuel := rfl
@[simp] theorem ConjTSpec.plain_L (sp : ConjTSpec) : sp.plain.L = sp.L := rfl

def ConjTSpec.hypsOf (sp : ConjTSpec) (t : Nat) : List Nat :=
  match sp.hyps[t]? with
  | some (some l) => l
  | _ => List.range sp.conjs.length

/-- The cases target `t`'s STEP is split into: the `oneOf` it names, or one
case with no assignment. -/
def ConjTSpec.casesOf (sp : ConjTSpec) (t : Nat) : List Case :=
  match sp.split[t]? with
  | some (some s) =>
      match sp.conjs[s]? with
      | some (.oneOf cs) => cs
      | _ => [[]]
  | _ => [[]]

def subCase (c d : Case) : Bool := c.all (fun q => d.contains q)

/-- The template of a case: its assignments as constants, then every equality
conjunct (an `eqIf` only when the case contains its condition) as a shared
variable, when the two flops have one width. -/
def ConjTSpec.tmplOf (sp : ConjTSpec) (cas : Case) : Tmpl :=
  cas.map (fun q => TFact.cnst q.1 q.2) ++
  sp.conjs.filterMap fun k => match k with
    | .eq f g => if (sp.L.fws[f]?).isSome && sp.L.fws[f]? == sp.L.fws[g]? then some (.alias f g) else none
    | .eqIf c f g =>
        if subCase c cas && (sp.L.fws[f]?).isSome && sp.L.fws[f]? == sp.L.fws[g]? then some (.alias f g) else none
    | _ => none

theorem caseHolds_sub {fl : Array BV} {c d : Case} (hs : subCase c d = true) (hd : caseHolds fl d = true) :
    caseHolds fl c = true := by
  unfold subCase at hs; unfold caseHolds at hd ⊢
  rw [List.all_eq_true] at hs hd ⊢
  intro q hq
  exact hd q (List.contains_iff_mem.mp (by simpa using hs q hq))

/-- Every fact a template uses holds in a state where every conjunct holds and
the case does. -/
theorem tmplOf_holds {sp : ConjTSpec} {cas : Case} {st : StateM} {outs : Array BV}
    (hm : StMatches sp.L st) (hall : sp.conjs.all (·.holds st.flops outs) = true)
    (hcase : caseHolds st.flops cas = true) : ∀ x ∈ sp.tmplOf cas, x.Holds st.flops := by
  intro x hx
  unfold ConjTSpec.tmplOf at hx
  rcases List.mem_append.mp hx with hx | hx
  · obtain ⟨q, hq, rfl⟩ := List.mem_map.mp hx
    unfold caseHolds at hcase
    have := (List.all_eq_true.mp hcase) q hq
    simp only [flopVal, beq_iff_eq, Option.map_eq_some_iff] at this
    obtain ⟨b, hb, hv⟩ := this
    exact ⟨b, hb, hv⟩
  · obtain ⟨k, hk, hkx⟩ := List.mem_filterMap.mp hx
    have hholds := (List.all_eq_true.mp hall) k hk
    have hw : ∀ f g, (sp.L.fws[f]?).isSome = true → sp.L.fws[f]? = sp.L.fws[g]? →
        (flopVal st.flops f).isSome = true → flopVal st.flops f = flopVal st.flops g →
        TFact.Holds st.flops (.alias f g) := by
      intro f g _ hfg hsome heq
      simp only [flopVal] at hsome heq
      cases hb : st.flops[f]? with
      | none => rw [hb] at hsome; cases hsome
      | some b =>
      rw [hb, Option.map_some] at heq
      obtain ⟨c, hc, hcv⟩ := Option.map_eq_some_iff.mp heq.symm
      refine ⟨b, c, hb, hc, ?_, hcv.symm⟩
      have h1 := hm.1
      have wb : sp.L.fws[f]? = some b.width := by
        rw [← h1]; simp [List.getElem?_map, hb]
      have wc : sp.L.fws[g]? = some c.width := by
        rw [← h1]; simp [List.getElem?_map, hc]
      rw [wb, wc] at hfg; exact Option.some.inj hfg
    cases k with
    | eq f g =>
        simp only at hkx
        split at hkx
        · rename_i hc
          simp only [Option.some.injEq] at hkx; subst hkx
          simp only [Bool.and_eq_true, beq_iff_eq] at hc
          simp only [ConjK.holds, Bool.and_eq_true, beq_iff_eq] at hholds
          exact hw f g hc.1 hc.2 hholds.1 hholds.2
        · cases hkx
    | eqIf c f g =>
        simp only at hkx
        split at hkx
        · rename_i hc
          simp only [Option.some.injEq] at hkx; subst hkx
          simp only [Bool.and_eq_true, beq_iff_eq] at hc
          simp only [ConjK.holds, Bool.or_eq_true, Bool.not_eq_true', Bool.and_eq_true,
            beq_iff_eq] at hholds
          rcases hholds with hn | hy
          · rw [caseHolds_sub hc.1.1 hcase] at hn; cases hn
          · exact hw f g hc.1.2 hc.2 hy.1 hy.2
        · cases hkx
    | out p => simp at hkx
    | oneOf cs => simp at hkx

/-! ## Runs and obligations -/

def stepRunT (D : DesignCert) (R : Program) (sp : ConjTSpec) (cas : Case) :
    Except String (List StepO × SymSt) :=
  unrollO D R sp.fuel sp.asmOut sp.asmIn sp.inW 2 (layoutSize sp.L) (stateVarT sp.L (sp.tmplOf cas) 0)
    ⟨layoutSize sp.L + 2 * sp.inW.length, #[]⟩

/-- Every case some target is split into (and the empty one). -/
def ConjTSpec.allCases (sp : ConjTSpec) : List Case :=
  ([[]] ++ (List.range sp.conjs.length).flatMap sp.casesOf).eraseDups

def findRun {α : Type} : List (Case × α) → Case → Option α
  | [], _ => none
  | (c, a) :: rest, d => if c == d then some a else findRun rest d

theorem findRun_map {α : Type} (f : Case → α) :
    ∀ (l : List Case) (d : Case) (a : α), findRun (l.map fun c => (c, f c)) d = some a → a = f d
  | [], _, _, h => by simp [findRun] at h
  | c :: l, d, a, h => by
      simp only [List.map_cons, findRun] at h
      split at h
      · rename_i hc; simp only [beq_iff_eq] at hc; subst hc; exact (Option.some.inj h).symm
      · exact findRun_map f l d a h

/-- BASE: from the initial state, some conjunct fails at cycle 0. -/
def baseOblT (sp : ConjTSpec) (s0 : StateM) (st : SymSt) (s : StepO) : Option BVLogicalExpr :=
  (sp.conjs.mapM (holdsE (.conc (StateM.encStateM s0)) s.2.1)).map fun ps =>
    coi st (.gate .and s.1 (.not (conjE ps)))

/-- STEP(t) in case `cas`, from the two steps `a b` out of the case's template. -/
def stepOblT (sp : ConjTSpec) (cas : Case) (st : SymSt) (a b : StepO) (t : Nat) : Option BVLogicalExpr := do
  let s0V := stateVarT sp.L (sp.tmplOf cas) 0
  let hs ← (sp.hypsOf t).mapM fun i => sp.conjs[i]? >>= holdsE s0V a.2.1
  let k ← sp.conjs[t]?
  let g ← holdsE a.2.2 b.2.1 k
  pure (coi st (conjE [a.1, conjE hs, b.1, .not g]))

/-- The check over the runs. `pfS[t][m]` is the proof of target `t` in its
`m`-th case. -/
def checkT (sp : ConjTSpec) (s0 : StateM) (rb r0 : Except String (List StepO × SymSt))
    (runs : List (Case × Except String (List StepO × SymSt))) (pfB : ByteArray)
    (pfS : List (List ByteArray)) : Bool :=
  (match rb with
    | .ok ([s], st) =>
        scopedB st && (match baseOblT sp s0 st s with | some φ => checkUnsat φ pfB | none => false)
    | _ => false)
  && (match r0 with
    | .ok (a :: _, st) => scopedB st && decide (stateShapeOf a.2.2 = some sp.L)
    | _ => false)
  && (List.range sp.conjs.length).all fun t =>
       (sp.casesOf t).zipIdx.all fun (cas, m) =>
         match findRun runs cas with
         | some (.ok ([a, b], st)) =>
             scopedB st && (match stepOblT sp cas st a b t with
               | some φ => checkUnsat φ ((pfS.getD t []).getD m ByteArray.empty)
               | none => false)
         | _ => false

def runConjTOf (D : DesignCert) (R : Program) (sp : ConjTSpec) (s0 : StateM) (pfB : ByteArray)
    (pfS : List (List ByteArray)) : Bool :=
  sideOKC D sp.plain s0
    && checkT sp s0 (baseRunC D R sp.plain s0) (stepRunC D R sp.plain)
         (sp.allCases.map fun c => (c, stepRunT D R sp c)) pfB pfS

/-! ## Soundness -/

section
variable {D : DesignCert} {R : Program} {sp : ConjTSpec} {s0 : StateM} {pfB : ByteArray}
  {pfS : List (List ByteArray)} {ins : Nat → RuntimeInput}

theorem conjT_parts (h : runConjTOf D R sp s0 pfB pfS = true) :
    sideOKC D sp.plain s0 = true
    ∧ (∃ s st, baseRunC D R sp.plain s0 = .ok ([s], st) ∧ scopedB st = true
        ∧ ∃ φ, baseOblT sp s0 st s = some φ ∧ BVLogicalExpr.Unsat φ)
    ∧ (∃ a rest st, stepRunC D R sp.plain = .ok (a :: rest, st) ∧ scopedB st = true
        ∧ stateShapeOf a.2.2 = some sp.L)
    ∧ (∀ t, t < sp.conjs.length → ∀ cas ∈ sp.casesOf t, ∃ a b st,
        stepRunT D R sp cas = .ok ([a, b], st) ∧ scopedB st = true
        ∧ ∃ φ, stepOblT sp cas st a b t = some φ ∧ BVLogicalExpr.Unsat φ) := by
  unfold runConjTOf checkT at h
  simp only [Bool.and_eq_true] at h
  obtain ⟨hside, ⟨hb, h0⟩, hs⟩ := h
  refine ⟨hside, ?_, ?_, ?_⟩
  · split at hb
    · rename_i s st hrun
      simp only [Bool.and_eq_true] at hb
      refine ⟨s, st, hrun, hb.1, ?_⟩
      cases ho : baseOblT sp s0 st s with
      | none => rw [ho] at hb; exact absurd hb.2 (by simp)
      | some φ => rw [ho] at hb; exact ⟨φ, rfl, checkUnsat_sound hb.2⟩
    · cases hb
  · split at h0
    · rename_i a rest st hrun
      simp only [Bool.and_eq_true, decide_eq_true_eq] at h0
      exact ⟨a, rest, st, hrun, h0.1, h0.2⟩
    · cases h0
  · intro t ht cas hcas
    rw [List.all_eq_true] at hs
    have h1 := hs t (List.mem_range.mpr ht)
    rw [List.all_eq_true] at h1
    obtain ⟨m, hmlt, hm⟩ := List.getElem_of_mem hcas
    have h2 := h1 (cas, m) (List.mem_zipIdx_iff_getElem?.mpr (List.getElem?_eq_some_iff.mpr ⟨hmlt, hm⟩))
    simp only at h2
    have hcasA : cas ∈ sp.allCases := by
      unfold ConjTSpec.allCases
      rw [List.mem_eraseDups]
      exact List.mem_append_right _ (List.mem_flatMap.mpr ⟨t, List.mem_range.mpr ht, hcas⟩)
    split at h2
    · rename_i a b st hfind
      have hrun := findRun_map (stepRunT D R sp) _ cas _ hfind
      simp only [Bool.and_eq_true] at h2
      refine ⟨a, b, st, hrun.symm, h2.1, ?_⟩
      cases ho : stepOblT sp cas st a b t with
      | none => rw [ho] at h2; exact absurd h2.2 (by simp)
      | some φ => rw [ho] at h2; exact ⟨φ, rfl, checkUnsat_sound h2.2⟩
    · cases h2

/-- The layout and the memory route's runtime shape hold at EVERY cycle (from
the plain run, as in `Sym.Conj`). -/
theorem invT_all (hres : MemResidual D R) (hA : admitsWB D = true)
    (hchk : runConjTOf D R sp s0 pfB pfS = true) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, StMatches sp.L (traceM D s0 ins n) ∧ stateFullOKB D (traceM D s0 ins n) = true := by
  obtain ⟨hside, -, ⟨a, rest, stS, hrun, hsc, hshape⟩, -⟩ := conjT_parts hchk
  intro n
  induction n with
  | zero =>
      obtain ⟨hm, hf, _⟩ := sideOKC_parts hside
      exact ⟨stMatchesB_sound hm, hf⟩
  | succ n ih =>
      have rt := runtime_of_invC hside ih.1 ih.2 (hins n)
      refine ⟨?_, (resM_spec hA rt).2.2⟩
      have ⟨hx, hlen⟩ := unrollO_extends 2 _ _ _ _ _ hrun
      let xs := stateVals (traceM D s0 ins n) ++ insVals ins n 2
      have hxl : xs.length = stS.base := by
        rw [← hx.1, List.length_append, stateVals_length ih.1, insVals_length hins]
        rfl
      obtain ⟨hc, hag⟩ := extendSt_agree hxl hsc
      rw [AgreeAt_append, stateVals_length ih.1, Nat.zero_add] at hag
      have hS := stateVar_rel ih.1 hag.1
      have facts := unrollO_sound (sideOKC_parts hside).2.2.2.2.2 hres hA hins hc 2 n _ _ _ _ _
        hrun (.refl _) (insVals_agree hins 2 hag.2) hS 0 (by simp)
        (fun i hi => by
          have : i = 0 := by omega
          subst this; simpa using rt)
      exact stateShapeOf_sound facts.2.2 hshape

theorem mapM_holdsE_all {σ : Asg} {s so : SVal} {st : StateM} {outs : Array BV}
    (hr : SRel σ s (StateM.encStateM st)) (ho : SRel σ so (encBVs outs)) :
    ∀ (ks : List ConjK) (es : List BVLogicalExpr), ks.mapM (holdsE s so) = some es →
      (conjE es).eval σ = ks.all (·.holds st.flops outs)
  | [], es, h => by simp at h; subst h; rfl
  | k :: ks, es, h => by
      simp only [List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨e, he, es', hes', rfl⟩ := h
      rw [conjE, eval_and, holdsE_sound hr ho k e he, mapM_holdsE_all hr ho ks es' hes', List.all_cons]

theorem hyps_eval {σ : Asg} {s so : SVal} {st : StateM} {outs : Array BV}
    (hr : SRel σ s (StateM.encStateM st)) (ho : SRel σ so (encBVs outs)) (conjs : List ConjK)
    (hall : conjs.all (·.holds st.flops outs) = true) :
    ∀ (idx : List Nat) (es : List BVLogicalExpr),
      idx.mapM (fun i => conjs[i]? >>= holdsE s so) = some es → (conjE es).eval σ = true
  | [], es, h => by simp at h; subst h; rfl
  | i :: idx, es, h => by
      simp only [List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨e, ⟨k, hk, he⟩, es', hes', rfl⟩ := h
      rw [conjE, eval_and, holdsE_sound hr ho k e he, hyps_eval hr ho conjs hall idx es' hes']
      rw [(List.all_eq_true.mp hall) k (List.mem_of_getElem? hk)]
      rfl

/-- **Conjunctive induction with register facts, case splits and substitution
is sound.** -/
theorem runConjTOf_sound (hres : MemResidual D R) (hA : admitsWB D = true)
    (hchk : runConjTOf D R sp s0 pfB pfS = true) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (outM D s0 ins j) (ins j) sp.asmOut sp.asmIn = true) →
      sp.conjs.all (·.holds (traceM D s0 ins n).flops (outM D s0 ins n)) = true := by
  obtain ⟨hside, ⟨sB, stB, hrunB, hscB, φB, hφB, hunsB⟩, -, hstep⟩ := conjT_parts hchk
  have hinv := invT_all hres hA hchk hins
  have rt : ∀ n, runtimeFullOKB D (ins n) (traceM D s0 ins n) (edgesAll D) = true :=
    fun n => runtime_of_invC hside (hinv n).1 (hinv n).2 (hins n)
  have hrange := (sideOKC_parts hside).2.2.2.2.2
  intro n
  induction n with
  | zero =>
    intro hasm
    cases hall : sp.conjs.all (·.holds (traceM D s0 ins 0).flops (outM D s0 ins 0))
    case true => rfl
    case false =>
    exfalso
    have hbB := (unrollO_extends 1 _ _ _ _ _ hrunB).1.1
    let xs := insVals ins 0 1
    have hxl : xs.length = stB.base := by rw [← hbB, insVals_length hins]; simp
    obtain ⟨hc, hag⟩ := extendSt_agree hxl hscB
    have facts := unrollO_sound hrange hres hA hins hc 1 0 0 _ _ _ _ hrunB (.refl _)
      (fun j hj => by simpa using insVals_agree hins 1 hag j hj) .conc 0 (by simp)
      (fun i _ => by simpa using rt i)
    simp only [List.getElem_cons_zero, Nat.zero_add] at facts
    unfold baseOblT at hφB
    simp only [Option.map_eq_some_iff] at hφB
    obtain ⟨ps, hps, rfl⟩ := hφB
    have hev := eval_coi hc (body := .gate .and sB.1 (.not (conjE ps))) (by
      rw [eval_and, eval_not, facts.1, mapM_holdsE_all .conc facts.2.1 _ _ hps]
      simp only [traceM] at hall
      rw [hall]
      simpa using hasm 0 (Nat.le_refl 0))
    rw [hunsB] at hev; cases hev
  | succ n ih =>
    intro hasm
    have hprev := ih (fun j hj => hasm j (by omega))
    rw [List.all_eq_true]
    intro k hk
    obtain ⟨t, ht, rfl⟩ := List.getElem_of_mem hk
    cases hgt : sp.conjs[t].holds (traceM D s0 ins (n + 1)).flops (outM D s0 ins (n + 1))
    case true => rfl
    case false =>
    exfalso
    -- the case the state at cycle n is in
    obtain ⟨cas, hcasIn, hcase⟩ : ∃ cas ∈ sp.casesOf t, caseHolds (traceM D s0 ins n).flops cas = true := by
      unfold ConjTSpec.casesOf
      split
      · rename_i s hsplit
        split
        · rename_i cs hcs
          have hm := (List.all_eq_true.mp hprev) _ (List.mem_of_getElem? hcs)
          simp only [ConjK.holds, List.any_eq_true] at hm
          exact hm
        · exact ⟨[], List.mem_singleton_self _, rfl⟩
      · exact ⟨[], List.mem_singleton_self _, rfl⟩
    obtain ⟨a, b, stS, hrunS, hscS, φ, hφ, huns⟩ := hstep t ht cas hcasIn
    have hbS := (unrollO_extends 2 _ _ _ _ _ hrunS).1.1
    let xs := stateVals (traceM D s0 ins n) ++ insVals ins n 2
    have hxl : xs.length = stS.base := by
      rw [← hbS, List.length_append, stateVals_length (hinv n).1, insVals_length hins]
    obtain ⟨hc, hag⟩ := extendSt_agree hxl hscS
    rw [AgreeAt_append, stateVals_length (hinv n).1, Nat.zero_add] at hag
    have hT := tmplOf_holds (hinv n).1 hprev hcase
    have hS := stateVarT_rel (hinv n).1 hag.1 hT
    have facts := unrollO_sound hrange hres hA hins hc 2 n _ _ _ _ _ hrunS (.refl _)
      (insVals_agree hins 2 hag.2) hS
    have f0 := facts 0 (by simp) (fun i hi => by
      have : i = 0 := by omega
      subst this; simpa using rt n)
    have f1 := facts 1 (by simp) (fun i hi => by
      rcases Nat.lt_or_ge i 1 with h | h
      · have : i = 0 := by omega
        subst this; simpa using rt n
      · have : i = 1 := by omega
        subst this; simpa using rt (n + 1))
    simp only [List.getElem_cons_zero, List.getElem_cons_succ, Nat.add_zero] at f0 f1
    unfold stepOblT at hφ
    simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def, Option.some.injEq] at hφ
    obtain ⟨hs, hhs, k', hk', g, hg, rfl⟩ := hφ
    have hk'eq : k' = sp.conjs[t] := by
      rw [List.getElem?_eq_getElem ht] at hk'; exact (Option.some.inj hk').symm
    subst hk'eq
    have hev := eval_coi hc (body := conjE [a.1, conjE hs, b.1, .not g]) (by
      rw [eval_conjE]
      simp only [List.all_cons, List.all_nil, Bool.and_true, eval_not]
      rw [f0.1, f1.1, hyps_eval hS f0.2.1 sp.conjs hprev _ _ hhs,
        holdsE_sound f0.2.2 f1.2.1 _ g hg, hgt]
      have h0 := hasm n (by omega)
      have h1 := hasm (n + 1) (Nat.le_refl _)
      simp [h0, h1])
    rw [huns] at hev; cases hev

end

/-! ## Against the reference semantics -/

/-- **In the shared reference semantics**: at every cycle whose assumptions up
to it all held, every conjunct holds of `interpretDesign`'s flops (at the
cycle's start) and outputs. -/
theorem runConjTOf_sound_ref {D : DesignCert} {R : Program} {sp : ConjTSpec}
    {s0 : StateM} {pfB : ByteArray} {pfS : List (List ByteArray)} (hres : MemResidual D R)
    (hA : admitsWB D = true) (hr : runConjTOf D R sp s0 pfB pfS = true)
    (ins : Nat → RuntimeInput) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (refOut D (StateM.denote s0) ins j) (ins j) sp.asmOut sp.asmIn = true) →
      sp.conjs.all (·.holds (refState D (StateM.denote s0) ins n).flops
        (refOut D (StateM.denote s0) ins n)) = true := by
  have hside := (conjT_parts hr).1
  have hinv := invT_all hres hA hr hins
  have hR : ∀ n, runtimeFullOKB D (ins n) (traceM D s0 ins n) (edgesAll D) = true :=
    fun n => runtime_of_invC hside (hinv n).1 (hinv n).2 (hins n)
  have hden := traceM_denote hA hR
  intro n hasm
  rw [← (hden n).2, ← (hden n).1]
  exact runConjTOf_sound hres hA hr hins n (fun j hj => by rw [(hden j).2]; exact hasm j hj)

end HDLProof.Sym

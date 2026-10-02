/-
# The residual-fragment checker, and the simulator bundle

Phase 5, first increment.  `projectDesign_correct` relates a residual to the
reference semantics through `Eval`, which has no fuel.  What a user RUNS is
`evalFuel`, which does.  This file closes that gap from the executable side:
an EXECUTABLE checker that accepts a residual, computes an evaluation bound for
it, and whose acceptance is a PROVED guarantee rather than a hope.

## The accepted fragment, and why it is this narrow

Measured before it was chosen, on every residual this branch can produce:

    design                       funs  entry arity  calls in body  calls anywhere
    tinyD                           1            3              0               0
    seqD                            1            3              0               0
    Scaling.fanD   1, 4, 16, 64     1            3              0               0
    Scaling.chainD 16, 64, 256      1            3              0               0
    Scaling.flopD  8, 32            1            3              0               0

So the specializer, on this interpreter, produces a SINGLE CALL-FREE FUNCTION
of arity 3.  That is a fact about `hwS` and `mix` together -- `main` is the only
non-`inline` function in `hwS`, so there is nothing for a residual call to point
at -- and it is accepted DELIBERATELY as the fragment rather than engineering a
general acyclic-call-graph analysis for a case that does not arise.  Widening
it is a later increment, and the measurement above is what would justify one.

## What acceptance buys

`evalFuel` decrements fuel once per LEVEL, not per node: `evalFuel (n+1)` hands
`n` to every subterm AND (through `evalFuelList`) to every list element.  So the
fuel a call-free term needs is its HEIGHT, which is computable.  That is the
whole reason call-freeness is the fragment condition: with a call, the fuel
needed depends on the callee and no syntactic bound exists.

`checkResidual` therefore returns the bound, and `checkResidual_sound` proves it
is enough: an accepted residual NEVER answers `.outOfFuel` at that bound.  It
may still answer `.typeError` -- `hd` of a non-cons is value-dependent and no
syntactic check can exclude it -- so `runProjected` reports that as an error
rather than pretending it cannot happen.
-/

import LeanSemanticPrimitives.Projection.ProjectionCorrect

namespace Projection
namespace Hw

open Compiler

/-! ## Height -/

mutual

/-- The fuel `evalFuel` needs for a call-free term: once per level. -/
def height : Term → Nat
  | .lit _        => 1
  | .var _        => 1
  | .letIn e b    => 1 + max (height e) (height b)
  | .ite c a b    => 1 + max (height c) (max (height a) (height b))
  | .prim _ ts    => 1 + heightList ts
  | .ctorT _ ts   => 1 + heightList ts
  | .caseT s alts => 1 + max (height s) (heightAlts alts)
  | .call _ ts    => 1 + heightList ts

def heightList : List Term → Nat
  | []      => 0
  | t :: ts => max (height t) (heightList ts)

def heightAlts : List Alt → Nat
  | []              => 0
  | (_, _, b) :: as => max (height b) (heightAlts as)

end

theorem height_pos (t : Term) : 1 ≤ height t := by
  cases t <;> simp [height]

/-! ## The checker -/

mutual

/-- Accept a term at binder depth `d`: every variable in scope, every primitive
at its declared arity, and NO CALL. -/
def fragB (d : Nat) : Term → Bool
  | .lit _        => true
  | .var i        => decide (i < d)
  | .letIn e b    => fragB d e && fragB (d + 1) b
  | .ite c a b    => fragB d c && fragB d a && fragB d b
  | .prim p ts    => decide (ts.length = p.arity) && fragListB d ts
  | .ctorT _ ts   => fragListB d ts
  | .caseT s alts => fragB d s && fragAltsB d alts
  | .call _ _     => false

def fragListB (d : Nat) : List Term → Bool
  | []      => true
  | t :: ts => fragB d t && fragListB d ts

def fragAltsB (d : Nat) : List Alt → Bool
  | []              => true
  | (_, k, b) :: as => fragB (d + k) b && fragAltsB d as

end

/-- Locating an alternative preserves acceptance. -/
theorem fragAltsB_find {d : Nat} :
    ∀ (alts : List Alt) (tag : Nat) (a : Alt), fragAltsB d alts = true →
      findAlt alts tag = some a → fragB (d + a.arity) a.body = true
  | [],              _,   _, _, hf => by simp [findAlt] at hf
  | (t, k, b) :: as, tag, a, h, hf => by
      simp only [fragAltsB, Bool.and_eq_true] at h
      simp only [findAlt, Alt.tag] at hf
      by_cases ht : t = tag
      · rw [if_pos ht] at hf; cases hf; exact h.1
      · rw [if_neg ht] at hf; exact fragAltsB_find as tag a h.2 hf

/-- …and its body is no taller than the alternatives block. -/
theorem heightAlts_find :
    ∀ (alts : List Alt) (tag : Nat) (a : Alt),
      findAlt alts tag = some a → height a.body ≤ heightAlts alts
  | [],              _,   _, hf => by simp [findAlt] at hf
  | (t, k, b) :: as, tag, a, hf => by
      simp only [findAlt, Alt.tag] at hf
      by_cases ht : t = tag
      · rw [if_pos ht] at hf; cases hf; simp only [heightAlts, Alt.body]; omega
      · rw [if_neg ht] at hf
        have := heightAlts_find as tag a hf
        simp only [heightAlts]
        omega

/-! ## Soundness: the computed bound is enough

The only content is the fuel arithmetic.  Every case either returns without
recursing, or recurses at fuel `m` where `m` dominates the subterm's height --
which is exactly how `height` was defined. -/

mutual

theorem fragB_no_outOfFuel {P : Program} :
    ∀ (t : Term) (d : Nat) (ρ : Env) (n : Nat),
      fragB d t = true → ρ.length = d → height t ≤ n →
      evalFuel n P ρ t ≠ .outOfFuel
  | .lit _, _, _, n, _, _, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m => simp [evalFuel]
  | .var i, d, ρ, n, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          have hi : i < ρ.length := by
            simp only [fragB, decide_eq_true_eq] at hf; omega
          simp only [evalFuel, List.getElem?_eq_getElem hi]
          simp
  | .letIn e b, d, ρ, n, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB, Bool.and_eq_true] at hf
          simp only [height] at hn
          simp only [evalFuel]
          cases hr : evalFuel m P ρ e with
          | outOfFuel     =>
              exact absurd hr (fragB_no_outOfFuel e d ρ m hf.1 hρ (by omega))
          | typeError msg => simp
          | value v       =>
              exact fragB_no_outOfFuel b (d + 1) (v :: ρ) m hf.2
                (by simp [hρ]) (by omega)
  | .ite c a b, d, ρ, n, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB, Bool.and_eq_true] at hf
          simp only [height] at hn
          simp only [evalFuel]
          cases hr : evalFuel m P ρ c with
          | outOfFuel     =>
              exact absurd hr (fragB_no_outOfFuel c d ρ m hf.1.1 hρ (by omega))
          | typeError msg => simp
          | value v       =>
              cases v with
              | bool bb =>
                  cases bb with
                  | true  => exact fragB_no_outOfFuel a d ρ m hf.1.2 hρ (by omega)
                  | false => exact fragB_no_outOfFuel b d ρ m hf.2 hρ (by omega)
              | _ => simp
  | .prim p ts, d, ρ, n, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB, Bool.and_eq_true] at hf
          simp only [height] at hn
          simp only [evalFuel]
          cases hr : evalFuelList m P ρ ts with
          | inl vs => cases hp : evalPrim p vs <;> simp [hp]
          | inr r  =>
              cases r with
              | outOfFuel =>
                  exact absurd hr (fragListB_no_outOfFuel ts d ρ m hf.2 hρ (by omega))
              | typeError msg => simp
              | value v       => simp
  | .ctorT k ts, d, ρ, n, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB] at hf
          simp only [height] at hn
          simp only [evalFuel]
          cases hr : evalFuelList m P ρ ts with
          | inl vs => simp
          | inr r  =>
              cases r with
              | outOfFuel =>
                  exact absurd hr (fragListB_no_outOfFuel ts d ρ m hf hρ (by omega))
              | typeError msg => simp
              | value v       => simp
  | .caseT s alts, d, ρ, n, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB, Bool.and_eq_true] at hf
          simp only [height] at hn
          simp only [evalFuel]
          cases hr : evalFuel m P ρ s with
          | outOfFuel     =>
              exact absurd hr (fragB_no_outOfFuel s d ρ m hf.1 hρ (by omega))
          | typeError msg => simp
          | value v       =>
              cases v with
              | ctor tag vs =>
                  cases hfa : findAlt alts tag with
                  | none   => simp [hfa]
                  | some a =>
                      simp only [hfa]
                      by_cases hk : a.arity = vs.length
                      · simp only [if_pos hk]
                        exact fragAltsB_no_outOfFuel alts d ρ m tag a vs hf.2 hfa hρ
                          hk (by omega)
                      · simp [hk]
              | _ => simp
  | .call _ _, _, _, _, hf, _, _ => by simp [fragB] at hf

theorem fragListB_no_outOfFuel {P : Program} :
    ∀ (ts : List Term) (d : Nat) (ρ : Env) (n : Nat),
      fragListB d ts = true → ρ.length = d → heightList ts ≤ n →
      evalFuelList n P ρ ts ≠ .inr .outOfFuel
  | [],      _, _, _, _,  _,  _  => by simp [evalFuelList]
  | t :: ts, d, ρ, n, hf, hρ, hn => by
      simp only [fragListB, Bool.and_eq_true] at hf
      simp only [heightList] at hn
      simp only [evalFuelList]
      cases hr : evalFuel n P ρ t with
      | outOfFuel     =>
          exact absurd hr (fragB_no_outOfFuel t d ρ n hf.1 hρ (by omega))
      | typeError msg => simp
      | value v       =>
          cases hrs : evalFuelList n P ρ ts with
          | inl vs => simp
          | inr r  =>
              cases r with
              | outOfFuel =>
                  exact absurd hrs (fragListB_no_outOfFuel ts d ρ n hf.2 hρ (by omega))
              | typeError msg => simp
              | value v'      => simp

theorem fragAltsB_no_outOfFuel {P : Program} :
    ∀ (alts : List Alt) (d : Nat) (ρ : Env) (n : Nat) (tag : Nat) (a : Alt)
      (vs : List Val),
      fragAltsB d alts = true → findAlt alts tag = some a → ρ.length = d →
      a.arity = vs.length → heightAlts alts ≤ n →
      evalFuel n P (vs ++ ρ) a.body ≠ .outOfFuel
  | [],              _, _, _, _,   _, _,  _, hfa, _,  _,  _  => by
      simp [findAlt] at hfa
  | (t, k, b) :: as, d, ρ, n, tag, a, vs, h, hfa, hρ, hk, hn => by
      simp only [fragAltsB, Bool.and_eq_true] at h
      simp only [findAlt, Alt.tag] at hfa
      simp only [heightAlts] at hn
      by_cases ht : t = tag
      · rw [if_pos ht] at hfa
        cases hfa
        refine fragB_no_outOfFuel b (d + k) (vs ++ ρ) n h.1 ?_ (by omega)
        simp only [List.length_append, hρ]
        simp only [Alt.arity] at hk
        omega
      · rw [if_neg ht] at hfa
        exact fragAltsB_no_outOfFuel as d ρ n tag a vs h.2 hfa hρ hk (by omega)

end

/-! ## The program-level checker

One function, entry index 0, arity 3, call-free body.  The returned number is
the bound for the ENTRY-CALL form -- one level above the body, because
`evalFuel` spends a level on the `call` node itself. -/

def checkResidual (R : Program) : Option Nat :=
  match R.funs with
  | [fd] =>
      if R.entry = 0 && fd.arity = 3 && fragB 3 fd.body then
        some (height fd.body + 1)
      else none
  | _ => none

theorem checkResidual_entry {R : Program} {b : Nat} (h : checkResidual R = some b) :
    ∃ fd, R.fn R.entry = some fd ∧ fd.arity = 3 ∧ fragB 3 fd.body = true ∧
          b = height fd.body + 1 := by
  unfold checkResidual at h
  split at h <;> try simp at h
  rename_i g hg
  obtain ⟨⟨⟨he, ha⟩, hfr⟩, hb⟩ := h
  exact ⟨g, by simp [Program.fn, he, hg], ha, hfr, hb.symm⟩

/-- **Acceptance means the bound is enough.**  An accepted residual, called on
three arguments, never answers `.outOfFuel` at the bound the checker computed.

It can still answer `.typeError`: `hd` of a non-cons is value-dependent and no
syntactic check excludes it.  That is why `runProjected` has an error case for
it rather than a `panic`. -/
theorem checkResidual_sound {R : Program} {b : Nat} (h : checkResidual R = some b)
    (a₀ a₁ a₂ : Val) :
    evalFuel b R [] (.call R.entry [.lit a₀, .lit a₁, .lit a₂]) ≠ .outOfFuel := by
  obtain ⟨fd, hfd, ha, hfr, rfl⟩ := checkResidual_entry h
  -- a height is at least 1, so the bound is two successors -- which is what
  -- lets `evalFuel` unfold through the `call` node and then the literals
  obtain ⟨k, hk⟩ : ∃ k, height fd.body = k + 1 :=
    ⟨height fd.body - 1, by have := height_pos fd.body; omega⟩
  rw [hk]
  simp only [evalFuel, evalFuelList, hfd, ha]
  have h3 : (3 : Nat) = [a₀, a₁, a₂].length := rfl
  simp only [if_pos h3]
  exact fragB_no_outOfFuel fd.body 3 [a₀, a₁, a₂] (k + 1) hfr rfl (by omega)

/-! ## The bundle

The simulator is DATA: the design it was specialized for, the residual, and the
checked bound.  It is built once, by `mkSim`, and `runProjected` re-specializes
nothing.

Bundling the ORIGIN DESIGN is what lets the next increment's `stepOf` validate
the `Step` contract's (redundant) design argument against the one the residual
was actually specialized for, and check the runtime shape, before evaluating.
`SimWF` is the separate proposition that the bundle is honest; nothing in this
file assumes it. -/

inductive SimError where
  -- NOTE: there is deliberately no `designMismatch`.  `Step`'s design argument
  -- is redundant for an already-specialized simulator, and comparing a whole
  -- `DesignCert` every cycle would be O(|D|) of pure overhead.  `stepOf`
  -- IGNORES it and `StepCorrect` is stated for `sim.design` alone.
  | runtimeShape
  | boundExceeded
  | residualTypeError (msg : String)
  | undecodableResult
  deriving Repr, DecidableEq, Inhabited

structure ProjectedSimulator where
  /-- the design this residual was specialized FOR -/
  design : DesignCert
  /-- the residual itself -/
  prog   : Program
  /-- the bound `checkResidual` computed for it -/
  bound  : Nat
  deriving Inhabited

/-- The bundle is honest: the residual really is this design's projection, it
passed the checker with this bound, and the design is supported. -/
structure SimWF (sim : ProjectedSimulator) : Prop where
  proj : projectDesign sim.design = .ok sim.prog
  chk  : checkResidual sim.prog = some sim.bound
  sup  : SupportedByProjection sim.design

/-- Specialize once, check once.  Everything expensive happens here. -/
def mkSim (D : DesignCert) : Option ProjectedSimulator :=
  match projectDesign D with
  | .error _ => none
  | .ok R    =>
      match checkResidual R with
      | none   => none
      | some b => some ⟨D, R, b⟩

/-- `mkSim` builds only honest bundles, for the two fields that are facts about
its own computation.  `sup` is a property of the DESIGN and is not decided
here. -/
theorem mkSim_proj_chk {D : DesignCert} {sim : ProjectedSimulator}
    (h : mkSim D = some sim) :
    projectDesign sim.design = .ok sim.prog ∧ checkResidual sim.prog = some sim.bound := by
  unfold mkSim at h
  split at h <;> try simp at h
  rename_i R hR
  split at h <;> try simp at h
  rename_i b hb
  cases h
  exact ⟨hR, hb⟩

/-- **One cycle, with no caller-selected fuel.**  The bound comes from the
simulator, where the checker put it; a caller cannot pass a smaller one and get
a silent `.outOfFuel`, nor a larger one and claim more. -/
def runProjected (sim : ProjectedSimulator) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : Except SimError RuntimeResult :=
  match evalFuel sim.bound sim.prog []
      (.call sim.prog.entry
        [.lit (encEdges e), .lit (encInput i), .lit (encState s)]) with
  | .outOfFuel   => .error .boundExceeded
  | .typeError m => .error (.residualTypeError m)
  | .value v     =>
      match decResult v with
      | none   => .error .undecodableResult
      | some r => .ok r

/-- `.boundExceeded` is UNREACHABLE for an honest bundle.  The error case
exists because `runProjected` is total, not because it can fire. -/
theorem runProjected_never_boundExceeded {sim : ProjectedSimulator}
    (hchk : checkResidual sim.prog = some sim.bound)
    (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    runProjected sim e i s ≠ .error .boundExceeded := by
  unfold runProjected
  have h := checkResidual_sound hchk (encEdges e) (encInput i) (encState s)
  cases hr : evalFuel sim.bound sim.prog []
      (.call sim.prog.entry
        [.lit (encEdges e), .lit (encInput i), .lit (encState s)]) with
  | outOfFuel     => exact absurd hr h
  | typeError msg => simp
  | value v       => cases hd : decResult v <;> simp [hd]

/-! ## Measurements

Guards, not proofs: these record what the specializer actually produces, and
they fail the build the moment it stops producing it.  `mkSim D = some _` IS
the fragment invariant -- `checkResidual` accepts only a one-function program
whose entry has arity 3 and whose body `fragB` accepts, and `fragB` rejects
every `call`. -/

namespace FragmentCheck
open Compiler Projection.Acceptance

-- The residual of each shared fixture is in the fragment…
#guard (mkSim tinyD).isSome
#guard (mkSim seqD).isSome

-- …with these checked bounds.  Pinned so a change in the residual's shape is
-- visible rather than silent.
#guard ((mkSim tinyD).map ProjectedSimulator.bound) == some 9
#guard ((mkSim seqD).map ProjectedSimulator.bound)  == some 19

/-- …and `runProjected`, at that bound and with no caller fuel, agrees with the
shared reference semantics. -/
def agrees (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) : Bool :=
  match mkSim D with
  | none     => false
  | some sim =>
      match runProjected sim e i s with
      | .ok r    => encResult r == encResult (interpretDesign D e i s)
      | .error _ => false

#guard agrees tinyD (allEdges tinyD) tinyIn tinySt
-- the three regimes: capture, hold, reset-beats-enable
#guard agrees seqD (allEdges seqD) (seqIn 5 1 0) (seqSt 0)
#guard agrees seqD (allEdges seqD) (seqIn 3 0 0) (seqSt 4)
#guard agrees seqD (allEdges seqD) (seqIn 3 1 1) (seqSt 4)

end FragmentCheck

end Hw
end Projection

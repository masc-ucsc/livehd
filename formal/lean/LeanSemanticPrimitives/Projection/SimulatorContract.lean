/-
  The common simulator contract.

  Milestone 0 of `SIMULATOR_PLAN.md`: one `DesignCert`, one runtime, one
  reference semantics, shared with every other executable track rather than
  re-invented here.  `Compiler.interpretDesign` is imported verbatim at a pinned
  revision -- see `SHARED_SEMANTICS.md`.  A copied-and-modified `interpretDesign`
  would turn the Milestone 5 equivalence theorem into a statement about two
  different semantics, which is worth nothing.

  This module deliberately contains NO Futamura-specific machinery.  It is the
  vocabulary every track states its result in, so that cross-track equivalence
  is transitivity rather than a fresh proof per pair.  Nothing here mentions
  `mix`, the object language, or the residual program; nothing here imports
  `compileDesign` either, which is what keeps the eventual comparison with the
  verified compiler non-circular.
-/

import LeanSemanticPrimitives.Compiler.DesignSemantics

namespace Projection
open Compiler

/-! ## One cycle -/

/-- One cycle of an executable implementation, which may fail.  The error type
is a parameter because the tracks fail differently -- a specializer reports
`MixError`, a checked loader reports something else -- and none of that should
leak into the contract. -/
abbrev Step (ε : Type) :=
  DesignCert → ClockEdges → RuntimeInput → RuntimeState → Except ε RuntimeResult

/-- The single obligation every executable track owes.

Stated on success only, deliberately: an implementation is free to refuse a
design it does not support, and refusing is not incorrect.  What it may not do
is return a wrong answer. -/
def StepCorrect {ε : Type} (step : Step ε) (D : DesignCert) : Prop :=
  ∀ e i s r, step D e i s = .ok r → r = interpretDesign D e i s

/-- Any two correct implementations agree wherever both succeed.

This is the whole point of routing every track through `interpretDesign`:
pairwise equivalence is a corollary, not a proof obligation per pair.  Adding an
n-th track costs one `StepCorrect` instance, not n-1 comparisons. -/
theorem step_agree {ε₁ ε₂ : Type} {s₁ : Step ε₁} {s₂ : Step ε₂} {D : DesignCert}
    (h₁ : StepCorrect s₁ D) (h₂ : StepCorrect s₂ D)
    {e : ClockEdges} {i : RuntimeInput} {st : RuntimeState} {r₁ r₂ : RuntimeResult}
    (e₁ : s₁ D e i st = .ok r₁) (e₂ : s₂ D e i st = .ok r₂) : r₁ = r₂ := by
  rw [h₁ e i st r₁ e₁, h₂ e i st r₂ e₂]

/-! ## Traces

A one-cycle theorem is not a simulator result.  What a user runs is a trace, so
the contract lifts to one: thread `nextState` through a list of inputs. -/

/-- One stimulus: which domains fire, and the inputs they see.  A pair rather
than a named record, so nothing here duplicates the direct simulator's `Tick`. -/
abbrev Stim := ClockEdges × RuntimeInput

/-- The reference trace. -/
def refTrace (D : DesignCert) : RuntimeState → List Stim → List RuntimeResult
  | _, []            => []
  | s, (e, i) :: is =>
      let r := interpretDesign D e i s
      r :: refTrace D r.nextState is

/-- An implementation's trace, stopping at the first cycle it refuses. -/
def stepTrace {ε : Type} (step : Step ε) (D : DesignCert) :
    RuntimeState → List Stim → Except ε (List RuntimeResult)
  | _, []            => .ok []
  | s, (e, i) :: is =>
      match step D e i s with
      | .error e => .error e
      | .ok r =>
        match stepTrace step D r.nextState is with
        | .error e => .error e
        | .ok rest => .ok (r :: rest)

/-- One cycle correct implies every trace correct.  The induction is the only
content: each step's `nextState` is what the next step starts from, on both
sides, so a single divergence would have to appear at the first cycle. -/
theorem stepTrace_correct {ε : Type} {step : Step ε} {D : DesignCert}
    (h : StepCorrect step D) :
    ∀ (s : RuntimeState) (is : List Stim) (rs : List RuntimeResult),
      stepTrace step D s is = .ok rs → rs = refTrace D s is := by
  intro s is
  induction is generalizing s with
  | nil => intro rs hr; simp only [stepTrace] at hr; cases hr; rfl
  | cons ei is ih =>
      obtain ⟨e, i⟩ := ei
      intro rs hr
      simp only [stepTrace] at hr
      split at hr <;> try contradiction
      rename_i r hstep
      split at hr <;> try contradiction
      rename_i rest hrest
      cases hr
      have hr1 : r = interpretDesign D e i s := h e i s r hstep
      subst hr1
      simp only [refTrace]
      rw [ih _ _ hrest]

/-- …and therefore any two correct implementations produce the same trace. -/
theorem trace_agree {ε₁ ε₂ : Type} {s₁ : Step ε₁} {s₂ : Step ε₂} {D : DesignCert}
    (h₁ : StepCorrect s₁ D) (h₂ : StepCorrect s₂ D)
    {st : RuntimeState} {is : List Stim} {r₁ r₂ : List RuntimeResult}
    (e₁ : stepTrace s₁ D st is = .ok r₁) (e₂ : stepTrace s₂ D st is = .ok r₂) :
    r₁ = r₂ := by
  rw [stepTrace_correct h₁ st is r₁ e₁, stepTrace_correct h₂ st is r₂ e₂]

/-! ## Milestone 0 acceptance

A single `DesignCert`, input and state, passed unchanged to the reference
semantics in THIS branch.  The point is not the arithmetic -- it is that the
certificate type, the runtime types and `interpretDesign` are the shared ones,
so an adapter for any track can be handed exactly these values. -/

namespace Acceptance

/-- `y = x &&& 12`, on 4 bits.  Two sources (slots 0 and 1), one node (slot 2),
one output.  Constants are certificate SOURCES, never node ops. -/
def tinyD : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := #[{ op := .Op_And, width := 4, deps := #[0, 1] }]
  outputs  := #[{ slot := 2, width := 4 }]
  flops    := #[]
  memories := #[]

def tinyIn : RuntimeInput := #[mk_bv 4 5]
def tinySt : RuntimeState := { flops := #[], mems := #[] }

-- 0b0101 &&& 0b1100 = 0b0100
#guard (interpretDesign tinyD (allEdges tinyD) tinyIn tinySt).outputs == #[mk_bv 4 4]

-- and it is combinational: the next state is empty, so a trace is stable
#guard (refTrace tinyD tinySt [(allEdges tinyD, tinyIn), (allEdges tinyD, tinyIn)]).length == 2

/-! ### A sequential certificate

`tinyD` is combinational, so it exercises nothing that carries a cycle.  This
one adds exactly the "sequential shell" the plan's vertical slice names -- one
flop with an enable and a synchronous reset, and a REGISTERED output (the output
reads the flop's Q source slot, not the node) -- while still using only `Op_And`
for combinational logic, so it does not widen the operator subset `I_hw` has to
support.

Deliberately one design, not a family: reset priority over enable, the
old-state fallback when disabled, and the resize on the way into the flop are
all visible in the three vectors below.

```
  q <= rst ? 0 : (en ? (d & 4'b1100) : q)
  out = q
``` -/
def seqD : DesignCert where
  sources  := #[ .input 0 4      -- slot 0: d
               , .const 4 12     -- slot 1: mask 0b1100
               , .input 1 1      -- slot 2: en
               , .input 2 1      -- slot 3: rst
               , .flopQ  0 4 ]   -- slot 4: q
  nodes    := #[{ op := .Op_And, width := 4, deps := #[0, 1] }]   -- slot 5
  outputs  := #[{ slot := 4, width := 4 }]
  flops    := #[{ width := 4, din := 5, enable := some 2, resetPin := some 3
                , resetValue := 0, resetActiveLow := false }]
  memories := #[]

/-- `d`, `en`, `rst`. -/
def seqIn (d en rst : Int) : RuntimeInput := #[mk_bv 4 d, mk_bv 1 en, mk_bv 1 rst]

def seqSt (q : Int) : RuntimeState := { flops := #[mk_bv 4 q], mems := #[] }

-- enabled, not reset: q captures d & 0b1100, and the output is the OLD q
#guard (interpretDesign seqD (allEdges seqD) (seqIn 5 1 0) (seqSt 0)).outputs             == #[mk_bv 4 0]
#guard (interpretDesign seqD (allEdges seqD) (seqIn 5 1 0) (seqSt 0)).nextState.flops     == #[mk_bv 4 4]

-- disabled: q holds, and holding is the OLD STATE, not the din
#guard (interpretDesign seqD (allEdges seqD) (seqIn 3 0 0) (seqSt 4)).outputs             == #[mk_bv 4 4]
#guard (interpretDesign seqD (allEdges seqD) (seqIn 3 0 0) (seqSt 4)).nextState.flops     == #[mk_bv 4 4]

-- reset beats enable, and loads `resetValue` rather than the din
#guard (interpretDesign seqD (allEdges seqD) (seqIn 3 1 1) (seqSt 4)).nextState.flops     == #[mk_bv 4 0]

-- two cycles of the reference trace: 0 -> 4 -> 4, seen as outputs 0 then 4
#guard ((refTrace seqD (seqSt 0) [(allEdges seqD, seqIn 5 1 0), (allEdges seqD, seqIn 5 1 0)]).map
          (fun r => r.outputs)) == [#[mk_bv 4 0], #[mk_bv 4 4]]

/-! ### Clocks: what a quiet domain means

The facts the multi-clock semantics has to get right, EXECUTED rather than
assumed.  `#[false]` is the single declared domain not firing this step. -/

-- a quiet domain HOLDS: the flop would have captured 4, and does not
#guard (interpretDesign seqD #[false] (seqIn 5 1 0) (seqSt 0)).nextState.flops == #[mk_bv 4 0]

-- a SYNCHRONOUS reset is sampled at the edge like `din`, so in a quiet step it
-- does NOT act -- the distinction the one-clock model never had to make
#guard (interpretDesign seqD #[false] (seqIn 3 1 1) (seqSt 4)).nextState.flops == #[mk_bv 4 4]

/-- `seqD` with the very same reset made ASYNCHRONOUS, and nothing else changed. -/
def seqAsyncD : DesignCert :=
  { seqD with
      flops := #[{ width := 4, din := 5, enable := some 2, resetPin := some 3
                 , resetValue := 0, resetActiveLow := false, asyncReset := true }] }

-- an ASYNCHRONOUS reset acts whether or not the domain fires…
#guard (interpretDesign seqAsyncD #[false] (seqIn 3 1 1) (seqSt 4)).nextState.flops == #[mk_bv 4 0]
#guard (interpretDesign seqAsyncD #[true]  (seqIn 3 1 1) (seqSt 4)).nextState.flops == #[mk_bv 4 0]
-- …and with the reset deasserted it is an ordinary flop again, so quiet holds
#guard (interpretDesign seqAsyncD #[false] (seqIn 5 1 0) (seqSt 0)).nextState.flops == #[mk_bv 4 0]

-- a quiet-domain MEMORY keeps its pre-step image, whatever the graph computed
private def memHoldImg : Int → BV := fun a => mk_bv 4 (a + 1)
private def memD : MemoryDesc := { aw := 2, dw := 4, nextImg := 0 }
private def memSt : RuntimeState := { flops := #[], mems := #[memHoldImg] }
#guard (srcMemNext (fun _ => .mem (fun _ => mk_bv 4 9)) #[false] memSt 0 memD 3)
         == memHoldImg 3
#guard (srcMemNext (fun _ => .mem (fun _ => mk_bv 4 9)) #[true]  memSt 0 memD 3)
         == mk_bv 4 9

/-! ### Conservativity, executed

`interpretDesign_allEdges` proves that the all-fire stimulus recovers the
one-clock semantics for every design whose clock ordinals are declared.  These
run it on both shared certificates, so the theorem is not the only evidence. -/

#guard (interpretDesign tinyD (allEdges tinyD) tinyIn tinySt).outputs
         == (interpretDesignLegacy tinyD tinyIn tinySt).outputs
#guard (interpretDesign seqD (allEdges seqD) (seqIn 5 1 0) (seqSt 0)).nextState.flops
         == (interpretDesignLegacy seqD (seqIn 5 1 0) (seqSt 0)).nextState.flops
#guard (interpretDesign seqD (allEdges seqD) (seqIn 3 1 1) (seqSt 4)).nextState.flops
         == (interpretDesignLegacy seqD (seqIn 3 1 1) (seqSt 4)).nextState.flops

/-- …and as a theorem for these two, discharging the range hypotheses by
computation: both are one-domain designs with every ordinal 0. -/
theorem tiny_conservative (i : RuntimeInput) (s : RuntimeState) :
    interpretDesign tinyD (allEdges tinyD) i s = interpretDesignLegacy tinyD i s :=
  interpretDesign_allEdges tinyD (by decide) (by decide) i s

theorem seq_conservative (i : RuntimeInput) (s : RuntimeState) :
    interpretDesign seqD (allEdges seqD) i s = interpretDesignLegacy seqD i s :=
  interpretDesign_allEdges seqD (by decide) (by decide) i s

end Acceptance

end Projection

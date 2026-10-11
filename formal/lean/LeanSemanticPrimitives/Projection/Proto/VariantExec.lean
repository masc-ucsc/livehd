/-
# EXECUTED total-variant regressions

`ProjectedStep.lean`'s `#guard`s build their bundle with `mkSim`, which uses
`projectDesign` and therefore `hwAP`.  So until now NOTHING had actually been
RUN through `mixDriver` + `hwAPVarT`: the variant had theorems and no execution.

These guards run the compiled evaluator on a bundle built from the PROVED
specializer and the TOTAL variant, at EXPLICIT budgets.  They are CHECKS, not
proofs -- `#guard` is the compiler's evaluator, not the kernel -- and they are
kept apart from the kernel-certified theorems in `VariantAdequacy.lean` for
exactly that reason.  What they add is that the path is EXECUTABLE at all,
which no theorem states: every fixture theorem is conditional on
`mixDriver … = .ok R`, and that hypothesis is not discharged anywhere.

`seqD` (`SimulatorContract.lean:156`):
  slot 5 = d & 0b1100;  flop din = slot 5, enable = `en`, reset = `rst`
  (active high, reset value 0);  the OUTPUT is the OLD `q`.
So the three transitions have concrete, different next states, and the guards
below pin those VALUES rather than only agreement with the reference.
-/
import LeanSemanticPrimitives.Projection.Proto.VariantAdequacy

namespace Projection
namespace ProtoVar

open Projection.Surface Projection.Hw Compiler Projection.Acceptance

/-- `mkSim`, but through the PROVED specializer and the TOTAL variant, with the
budgets explicit instead of `projectDesign`'s hardcoded pair.  `none` unless
the projection SUCCEEDS and the fragment checker ACCEPTS -- it fails closed. -/
def mkSimVarT (sf wf : Nat) (D : DesignCert) : Option ProjectedSimulator :=
  match mixDriver sf wf hwAPVarT [encDesign D] with
  | .error _ => none
  | .ok R    =>
      match checkResidual R with
      | none   => none
      | some b => some ⟨D, R, b⟩

-- It builds at `projectDesign`'s own budget.  This is the first thing on the
-- variant path that has been RUN rather than hypothesised.
#guard (mkSimVarT 20000 200 seqD).isSome
#guard (mkSimVarT 20000 200 tinyD).isSome

-- The bound the checker returned, for the record.
#guard ((mkSimVarT 20000 200 seqD).map (fun s => s.bound)).isSome

/-! ## One cycle, through `stepOf` -/

def varTStep (sf wf : Nat) (D : DesignCert) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : Option RuntimeResult :=
  match mkSimVarT sf wf D with
  | none     => none
  | some sim => match stepOf sim D e i s with
                | .ok r    => some r
                | .error _ => none

/-- The next-state flops, as VALUES. -/
def varTNext (sf wf : Nat) (d en rst q : Int) : Option (Array BV) :=
  (varTStep sf wf seqD (allEdges seqD) (seqIn d en rst) (seqSt q)).map
    (fun r => r.nextState.flops)

def varTOut (sf wf : Nat) (d en rst q : Int) : Option (Array BV) :=
  (varTStep sf wf seqD (allEdges seqD) (seqIn d en rst) (seqSt q)).map
    (fun r => r.outputs)

/-- …and agreement with the shared reference semantics, separately. -/
def varTAgrees (sf wf : Nat) (d en rst q : Int) : Bool :=
  match varTStep sf wf seqD (allEdges seqD) (seqIn d en rst) (seqSt q) with
  | none   => false
  | some r => encResult r ==
      encResult (interpretDesign seqD (allEdges seqD) (seqIn d en rst) (seqSt q))

-- RESET asserted: reset beats enable, so the flop takes 0 whatever `d` is.
#guard varTNext 20000 200 13 1 1 3 == some #[mk_bv 4 0]
#guard varTOut  20000 200 13 1 1 3 == some #[mk_bv 4 3]   -- the OLD q
#guard varTAgrees 20000 200 13 1 1 3

-- ENABLED, not reset: the flop takes `d & 0b1100` = 13 & 12 = 12.
#guard varTNext 20000 200 13 1 0 3 == some #[mk_bv 4 12]
#guard varTOut  20000 200 13 1 0 3 == some #[mk_bv 4 3]
#guard varTAgrees 20000 200 13 1 0 3

-- HELD: enable low, so the flop keeps `q` -- and `d` is ignored.
#guard varTNext 20000 200 13 0 0 3 == some #[mk_bv 4 3]
#guard varTNext 20000 200  5 0 0 3 == some #[mk_bv 4 3]
#guard varTOut  20000 200 13 0 0 3 == some #[mk_bv 4 3]
#guard varTAgrees 20000 200 13 0 0 3

/-! ## A THREADED trace

Each cycle starts from the previous one's `nextState`, which is what a
one-cycle guard does not check.  Starting at `q = 3`:

    reset   (13,1,1)  q: 3  -> 0    out 3
    enabled (13,1,0)  q: 0  -> 12   out 0
    held    (13,0,0)  q: 12 -> 12   out 12
-/

def seqTraceStim : List Stim :=
  [(allEdges seqD, seqIn 13 1 1), (allEdges seqD, seqIn 13 1 0),
   (allEdges seqD, seqIn 13 0 0)]

def varTTrace (sf wf : Nat) (q : Int) : Option (List RuntimeResult) :=
  match mkSimVarT sf wf seqD with
  | none     => none
  | some sim => match stepTrace (stepOf sim) seqD (seqSt q) seqTraceStim with
                | .ok rs   => some rs
                | .error _ => none

-- The trace runs to completion, which `seq_varT_trace` does NOT say: that
-- theorem assumes `stepTrace … = .ok rs` and concludes agreement, i.e. PARTIAL
-- correctness.  Completion is this guard's content.
#guard (varTTrace 20000 200 3).isSome
#guard ((varTTrace 20000 200 3).map List.length) == some 3

-- The threaded next-state VALUES, cycle by cycle.
#guard ((varTTrace 20000 200 3).map (fun rs => rs.map (fun r => r.nextState.flops)))
        == some [#[mk_bv 4 0], #[mk_bv 4 12], #[mk_bv 4 12]]

-- …and the outputs, which lag by one cycle because the output is the OLD q.
#guard ((varTTrace 20000 200 3).map (fun rs => rs.map (fun r => r.outputs)))
        == some [#[mk_bv 4 3], #[mk_bv 4 0], #[mk_bv 4 12]]

-- And it agrees with the reference trace.
#guard ((varTTrace 20000 200 3).map (fun rs => rs.map encResult))
        == some ((refTrace seqD (seqSt 3) seqTraceStim).map encResult)

/-! ## Malformed runtime: the refusal happens BEFORE the residual runs -/

/-- A state with the wrong number of flops. -/
def badSt : RuntimeState := { flops := #[], mems := #[] }

/-- A short input vector. -/
def badIn : RuntimeInput := #[mk_bv 4 1]

def varTRefuses (sf wf : Nat) (i : RuntimeInput) (s : RuntimeState) : Bool :=
  match mkSimVarT sf wf seqD with
  | none     => false
  | some sim => match stepOf sim seqD (allEdges seqD) i s with
                | .error .runtimeShape => true
                | _                    => false

#guard varTRefuses 20000 200 (seqIn 13 1 0) badSt
#guard varTRefuses 20000 200 badIn (seqSt 3)

-- Fails CLOSED on an impossible budget: no bundle, hence no execution, rather
-- than falling back to some other bound.
#guard (mkSimVarT 1 1 seqD).isNone
#guard varTAgrees 1 1 13 1 0 3 == false

end ProtoVar
end Projection

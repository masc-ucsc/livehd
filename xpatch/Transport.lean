/-
Relational state transport between two designs: the definition and the theorem.

Two designs read the same inputs `I`, one per cycle: A (states `St`, step `step`, power-on state
`init`) and B (states `StB`, step `stepB`, power-on state `initB`), for example a modified A.

A transport from A to B is three small pieces:
* a monitor `mon : H → St → I → H`: a little extra state, updated each cycle from A's state and
  input (for example the increment A added in the last cycle);
* a relation `R : St → H → StB`: a full B state, built from an A state and a monitor value;
* a start value `h0 : H` with the base fact `R init h0 = initB`.

The obligation is one commuting square, for EVERY state, input and monitor value, not only the
ones a run visits:
    stepB (R s h) i = R (step s i) (mon h s i).

Conclusion (`transport`): for every input sequence, B's state after the run is R applied to A's
state and the monitor's value after the same run. So A's run and a monitor pass give B's state
with zero cycles of B simulated.

This file is standalone (no imports): it states the theorem for arbitrary types. It is NOT linked
to LiveHD's certificate semantics; the obligation for a concrete pair of designs is discharged
separately, on a product design, by `sym_verify` (see README.md).
-/
namespace Xpatch.Transport

universe u v w x

/-- Run a step function over a list of inputs, left to right. -/
def run {S : Type u} {I : Type x} (step : S → I → S) : S → List I → S
  | s, [] => s
  | s, i :: is => run step (step s i) is

/-- Run A and the monitor side by side; the monitor reads A's state BEFORE A's step, as
`mon h s i` does in the square. -/
def runMon {St : Type u} {H : Type w} {I : Type x}
    (step : St → I → St) (mon : H → St → I → H) : St → H → List I → St × H
  | s, h, [] => (s, h)
  | s, h, i :: is => runMon step mon (step s i) (mon h s i) is

/-- The square, iterated: from any related pair of states, B's run stays related. -/
theorem run_R {St : Type u} {StB : Type v} {H : Type w} {I : Type x}
    (step : St → I → St) (stepB : StB → I → StB) (mon : H → St → I → H) (R : St → H → StB)
    (square : ∀ s h i, stepB (R s h) i = R (step s i) (mon h s i)) :
    ∀ (ins : List I) (s : St) (h : H),
      run stepB (R s h) ins = R (runMon step mon s h ins).1 (runMon step mon s h ins).2 := by
  intro ins
  induction ins with
  | nil => intro s h; rfl
  | cons i is ih =>
    intro s h
    show run stepB (stepB (R s h) i) is = R (runMon step mon (step s i) (mon h s i) is).1
                                            (runMon step mon (step s i) (mon h s i) is).2
    rw [square s h i]
    exact ih (step s i) (mon h s i)

/-- Relational state transport: under the base fact and the square, B's run from its reset
state equals R applied to A's run and the monitor's run. -/
theorem transport {St : Type u} {StB : Type v} {H : Type w} {I : Type x}
    (step : St → I → St) (stepB : StB → I → StB) (mon : H → St → I → H) (R : St → H → StB)
    (init : St) (initB : StB) (h0 : H)
    (base : R init h0 = initB)
    (square : ∀ s h i, stepB (R s h) i = R (step s i) (mon h s i)) :
    ∀ ins : List I,
      run stepB initB ins = R (runMon step mon init h0 ins).1 (runMon step mon init h0 ins).2 := by
  intro ins
  rw [← base]
  exact run_R step stepB mon R square ins init h0

/-- Any observation of B (an output, a test's pass or fail) is the same observation of
R (A's state) (monitor): it carries over with no cycle of B simulated. -/
theorem transport_obs {St : Type u} {StB : Type v} {H : Type w} {I : Type x} {O : Type}
    (step : St → I → St) (stepB : StB → I → StB) (mon : H → St → I → H) (R : St → H → StB)
    (init : St) (initB : StB) (h0 : H) (obs : StB → O)
    (base : R init h0 = initB)
    (square : ∀ s h i, stepB (R s h) i = R (step s i) (mon h s i)) (ins : List I) :
    obs (run stepB initB ins) = obs (R (runMon step mon init h0 ins).1 (runMon step mon init h0 ins).2) := by
  rw [transport step stepB mon R init initB h0 base square ins]

end Xpatch.Transport

#print axioms Xpatch.Transport.transport
#print axioms Xpatch.Transport.transport_obs

/-
# `DirectBench` — phase 6's measurement and differential harness

One invocation reports, for one certificate:

  * the semantic check's cost and verdict;
  * the direct interpreter's first-step and steady-state cost;
  * the verified compiler's compile cost, and the compiled simulator's
    steady-state cost;
  * a cycle-by-cycle differential between the two, on ordered outputs, flop
    state and sampled memory addresses.

The direct interpreter is NOT expected to beat the compiled one — its value is
being a simple executable reference semantics.  The numbers exist so the claim
can be stated with a measurement attached rather than asserted.

`compileDesign` is called ONCE, outside the loop.  `compileAndRun` recompiles on
every call, so timing it per cycle would measure the compiler, not the compiled
simulator.
-/
import LeanSemanticPrimitives.Compiler.DirectVsCompiled
import LeanSemanticPrimitives.Compiler.DirectSim

namespace Compiler
namespace Direct

/-- Force an array of bit vectors, so a timed region measures evaluation rather
than the construction of thunks. -/
def outSum (r : RuntimeResult) : Nat :=
  r.outputs.foldl (fun a b => a + (bv_uint b).toNat) 0

/-- Force a value by PRINTING it inside the timed region.

A bare `let x := e` is not enough: Lean may leave `e` as a thunk until the
`IO.println` that reads it, which is after the timer has stopped.  Nor is a
`if x == 0 then pure () else pure ()` guard — both branches are identical, so
the optimiser deletes the test and with it the force.  An `IO.println` of the
value cannot be elided, and that is the whole trick.  Measuring without it
reports a first step of 0 ms for a design that takes 80. -/
@[inline] def echoNat (label : String) (n : Nat) : IO Unit :=
  IO.println s!"{label} {n}"

def benchMain (name : String) (D : DesignCert) (args : List String) : IO UInt32 := do
  let cycles := (args.head?.bind parseNat?).getD 20
  let addrs : List Int := [0, 1, 2, 3, 255]
  IO.println s!"# bench {name} sources={D.sources.size} nodes={D.nodes.size} outputs={D.outputs.size} flops={D.flops.size} mems={D.memories.size} cycles={cycles}"
  let t0 ← IO.monoMsNow
  let verdict := match checkDesign D with
    | .ok _    => "ACCEPTED"
    | .error e => "REFUSED " ++ e.render
  IO.println s!"verdict {verdict}"
  let t1 ← IO.monoMsNow
  IO.println s!"check_ms {t1 - t0}"
  if (designErrors D).isSome then
    return 1
  let s0 := zeroState D
  let i0 := zeroInput D
  -- direct: first step, then steady state
  let t2 ← IO.monoMsNow
  echoNat "direct_step1_outsum" (outSum (directStepRaw D i0 s0))
  let t3 ← IO.monoMsNow
  IO.println s!"direct_step1_ms {t3 - t2}"
  let t4 ← IO.monoMsNow
  let mut st := s0
  let mut acc := 0
  for _ in [0:cycles] do
    let r := directStepRaw D i0 st
    acc := acc + outSum r
    st := r.nextState
  echoNat "direct_run_outsum" acc
  let t5 ← IO.monoMsNow
  IO.println s!"direct_run_ms {t5 - t4}"
  -- the verified compiler, compiled ONCE
  let t6 ← IO.monoMsNow
  match compileDesign D with
  | .error e =>
      let t7 ← IO.monoMsNow
      IO.println s!"compile_ms {t7 - t6} REFUSED {repr e}"
      IO.println "diff SKIPPED B1+B2 refuses this certificate"
      return 0
  | .ok R =>
      echoNat "compile_bindings" R.bindings.size
      let t7 ← IO.monoMsNow
      IO.println s!"compile_ms {t7 - t6}"
      let t8 ← IO.monoMsNow
      let mut st2 := s0
      let mut acc2 := 0
      for _ in [0:cycles] do
        let r := denoteResidual R i0 st2
        acc2 := acc2 + outSum r
        st2 := r.nextState
      echoNat "residual_run_outsum" acc2
      let t9 ← IO.monoMsNow
      IO.println s!"residual_run_ms {t9 - t8}"
      let t10 ← IO.monoMsNow
      let msg := diffTrace D R addrs s0 (List.replicate cycles i0) 0
      IO.println s!"diff_result {msg.getD "OK"}"
      let t11 ← IO.monoMsNow
      IO.println s!"diff_ms {t11 - t10} cycles {cycles}"
      match msg with
      | none   => return 0
      | some _ => return 1

end Direct
end Compiler

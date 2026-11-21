/-
# vc's step, compiled after its operators' fast forms

`denoteResidual` (the vc route's step, `ResidualSemantics`) is compiled in its own module,
before `FastResidual`, so two of its operators keep their slow forms: `rshlV` computes
`bv_uint a * 2 ^ k` with an `Int` power, and `rnotV` loops over every bit. A `@[csimp]`
lemma only reaches code compiled after it. And every cycle, `denoteResidual` turns the
binding array into a list before it walks it.

This module changes neither. It gives the step a second, word-for-word spelling compiled
after `FastResidual`, and the binding walk as one fold over the array, with `@[csimp]`
theorems that each is EQUAL to the original. Compiled code that imports this module runs
these; every definition and every proof keeps meaning exactly what it did.

`CompileDesignDefs` imports it, so every vc user (`cert_trace route:vc`, the proof tools'
`sim:vc`) does. Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.FastResidual

namespace Compiler

open Residual

/-- `denoteExpr`, word for word. Compiled here, its `rshlV` and `rnotV` calls take their
fast forms (`FastBV.rshlV_eq_fast`, `FastBV.rnotV_eq_fast`). -/
def denoteExprFast (env : SlotEnv) : ResidualExpr → CertVal
  | .rsum w nAdd args     => .bv (rsumV w nAdd (refBVs env args))
  | .rmult w args         => .bv (rmultV w (refBVs env args))
  | .rand w args          => .bv (randV w (refBVs env args))
  | .rorBits w args       => .bv (rorBitsV w (refBVs env args))
  | .rxor w args          => .bv (rxorV w (refBVs env args))
  | .rredOr w args        => .bv (rredOrV w (refBVs env args))
  | .req w args           => .bv (reqV w (refBVs env args))
  | .rshl w args          => .bv (rshlV w (refBVs env args))
  | .rmuxN w args         => .bv (rmuxNV w (refBVs env args))
  | .rnot w a             => .bv (rnotV w (refBV env a))
  | .rult w a b           => .bv (rultV w (refBV env a) (refBV env b))
  | .rugt w a b           => .bv (rugtV w (refBV env a) (refBV env b))
  | .rslt w a b           => .bv (rsltV w (refBV env a) (refBV env b))
  | .rsgt w a b           => .bv (rsgtV w (refBV env a) (refBV env b))
  | .rsra w a b           => .bv (rsraV w (refBV env a) (refBV env b))
  | .rsext w a amt        => .bv (rsextV w (refBV env a) (refBV env amt))
  | .rgetMask w a m       => .bv (rgetMaskV w (refBV env a) (refBV env m))
  | .rudiv w a b          => .bv (rudivV w (refBV env a) (refBV env b))
  | .rsdiv w a b          => .bv (rsdivV w (refBV env a) (refBV env b))
  | .rsetMask w a m v     => .bv (rsetMaskV w (refBV env a) (refBV env m) (refBV env v))
  | .rmux w sel fv tv     => .bv (rmuxV w (refBV env sel) (refBV env fv) (refBV env tv))
  | .rmemRead w m a en    => .bv (rmemReadV w (refMem env m) (refBV env a) (refBV env en))
  | .rmemWrite m a d en   => .mem (rmemWriteV (refMem env m) (refBV env a) (refBV env d)
                                              (refBV env en))
  | .rmemWriteBE w bw m a d be =>
      .mem (rmemWriteBEV w (refMem env m) (refBV env a) (refBV env d) (refBV env be) bw)
  | .rmemLoad w n m bus en =>
      .mem (rmemLoadV w n (refMem env m) (refBV env bus) (refBV env en))
  | .rmemToBus w n m      => .bv (rmemToBusV w n (refMem env m))

@[csimp] theorem denoteExpr_eq_fast : @denoteExpr = @denoteExprFast := by
  funext env e
  cases e <;> rfl

/-- `runBindings` is a left fold that pushes each binding's value. -/
theorem runBindings_eq_foldl : ∀ (bs : List ResidualBinding) (env : SlotEnv),
    runBindings bs env = bs.foldl (fun env b => env.push (denoteExpr env b.rhs)) env
  | [], _ => rfl
  | b :: bs, env => by
    simp only [runBindings, List.foldl_cons]
    exact runBindings_eq_foldl bs _

/-- `denoteResidual`, with the binding walk as one fold over the array (no list built each
cycle). Compiled after `denoteExpr_eq_fast`, the fold calls `denoteExprFast`. -/
def denoteResidualFast (R : ResidualProgram) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : RuntimeResult :=
  let env := R.bindings.foldl (fun env b => env.push (denoteExpr env b.rhs))
    (sourceEnvArr R.sources i s)
  { outputs   := R.outputs.map fun o => bv_resize o.width (refBV env o.slot)
    nextState :=
      { flops := R.flopUpdates.mapIdx fun idx f => flopNext env e s idx f
        mems  := R.memoryUpdates.mapIdx fun idx m => memNext env e s idx m } }

@[csimp] theorem denoteResidual_eq_fast : @denoteResidual = @denoteResidualFast := by
  funext R e i s
  simp only [denoteResidual, denoteResidualFast, runBindings_eq_foldl, Array.foldl_toList]

end Compiler

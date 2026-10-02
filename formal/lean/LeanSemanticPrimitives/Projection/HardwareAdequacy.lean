/-
# `I_hw` adequacy — the concrete bridge

Phase 4 item 2, increment B.  `SurfaceSemantics.lean` is generic and mentions no
hardware; this file is where it is pointed at `hwS`.

Three things live here, and they are kept apart from `HardwareInterpreter.lean`
on purpose:

* `hwResolved_ok` is the one place the kernel reduces `resolveProgram hwS`.  It
  costs about 4 s of the build, and isolating it means that cost is paid once
  and is visible in the per-module timings rather than smeared through a 3 s
  module.  It is also the fact that D1.1 warned could be intolerable; it is not.
* the canonicality corollary, which is what turns a `ResultRel` into an equation
  between VALUES and so makes an `iff` statable;
* the `hwS`-specialized transfer lemmas every per-function proof goes through.

Nothing here appeals to `evalFuel`, to the fixture `#guard`s, to `projectDesign`
or to the verified compiler.
-/

import LeanSemanticPrimitives.Projection.HardwareInterpreter
import LeanSemanticPrimitives.Projection.SurfaceSemantics

namespace Projection
namespace Hw

open Surface Compiler

/-! ## The resolver equation

`hwP` and `hwInl` are DEFINED by matching on `hwResolved`, so the equation
`hwResolved = .ok (hwP, hwInl)` holds as soon as the error branch is excluded —
and excluding it only needs the head constructor, not the payload.  That is why
the statement below is `isSome` rather than a literal `.ok (…, …)` with the
41-function program written out: the kernel stops at the constructor.

Measured on this machine: the whole module costs ~4.3 s wall, against ~0.3 s for
loading its imports alone.  The default heartbeat budget is NOT enough — without
`maxHeartbeats` the elaborator gives up and reports a bare "not definitionally
equal", which reads like the equation being false rather than like a budget. -/

set_option maxHeartbeats 2000000 in
set_option maxRecDepth 100000 in
theorem hwResolved_isOk : Option.isSome (Except.toOption hwResolved) = true := rfl

theorem hwResolved_ok : hwResolved = .ok (hwP, hwInl) := by
  have h0 := hwResolved_isOk
  unfold hwP hwInl
  cases h : hwResolved with
  | ok pi    => cases pi; rfl
  | error e  => rw [h] at h0; simp [Except.toOption] at h0

theorem hwP_resolves : resolveProgram hwS = .ok (hwP, hwInl) := hwResolved_ok

/-! ## Transfer

Every per-function fact about `I_hw` is stated over the NAMED program and
crossed here.  No proof below mentions a de Bruijn index or a function position,
so inserting a helper into `hwS` cannot invalidate one. -/

theorem hw_transfer {σ : SEnv} {e : SExp} {v : Val} {t : Term}
    (hs : SEval hwS σ e v)
    (hr : resolve (funNames hwS) (σ.map Prod.fst) e = .ok t) :
    Eval hwP (σ.map Prod.snd) t v :=
  SEval_sound hwP_resolves σ e v t hs hr

/-- The whole-cycle entry call, which is the exact shape `IHwAdequate` uses.
`hwP.entry` is never computed. -/
theorem hw_entry {vs : List Val} {v : Val}
    (h : SEval hwS [] (.call "main" (vs.map SExp.lit)) v) :
    Eval hwP [] (.call hwP.entry (vs.map Term.lit)) v :=
  SEval_entry hwP_resolves h

/-! ## Canonicality, in the shape adequacy uses

`IHwAdequate` is an `iff`, so the RIGHT-to-left direction has to produce an
object value and the LEFT-to-right direction has to pin the one it is given.
`ResultRel_canonical` is what makes those the same value: an object result
satisfying `ResultRel _ rr` is `encResult rr` on the nose. -/

theorem resultRel_iff_eq {v : Val} {rr : RuntimeResult} (h : MemFree rr.nextState) :
    ResultRel v rr ↔ v = encResult rr :=
  ⟨ResultRel_canonical, fun he => he ▸ ResultRel_encResult h⟩

/-- Specialized to what `SupportedByProjection` actually supplies: a memory-free
certificate makes every cycle's next state representable. -/
theorem resultRel_interpret_iff {D : Compiler.DesignCert} (hm : D.memories = #[])
    {v : Val} {e : Compiler.ClockEdges} {i : Compiler.RuntimeInput}
    {s : Compiler.RuntimeState} :
    ResultRel v (Compiler.interpretDesign D e i s)
      ↔ v = encResult (Compiler.interpretDesign D e i s) :=
  resultRel_iff_eq (interpretDesign_memFree hm e i s)

end Hw
end Projection

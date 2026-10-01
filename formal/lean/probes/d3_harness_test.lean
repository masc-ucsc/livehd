/-
# Focused tests for `D3Harness`

These test the CHECKER, not any design.  A sweep row is only worth reading if
`sameResult` provably rejects a wrong answer at each observable boundary, and
if `numInputs` counts the ordinals the stimulus has to cover.  Both were wrong
once: an input-perturbation "control" passed on a design whose outputs are
constant zero, which said nothing about the checker at all.

Run: `lake env lean probes/d3_harness_test.lean` (silent = all pass).
-/
import LeanSemanticPrimitives.Compiler.D3Harness

open Compiler
open Compiler.D3

private def mkD (srcs : Array SourceDesc) : DesignCert :=
  { sources := srcs, nodes := #[], outputs := #[], flops := #[], memories := #[] }

--------------------------------------------------------------------------------
-- numInputs — the stimulus width
--------------------------------------------------------------------------------

/- Ordinals are not dense: the count is the largest mentioned plus one. -/
#guard numInputs (mkD #[.input 0 8, .input 3 8]) == 4

/- An ASYNC RESET reads its own ordinal out of `RuntimeInput`, so it enlarges
the stimulus even though it is not an `.input` source.  Missing this gives a
short array, `sourceValue` falls back to zero, and the reset reads as
permanently deasserted — a differential test would pass while never exercising
reset at all. -/
#guard numInputs (mkD #[.input 0 8, .flopQAsync 0 8 9 0 false]) == 10

/- Constants and plain flops contribute nothing. -/
#guard numInputs (mkD #[.const 8 3, .flopQ 2 8]) == 0

--------------------------------------------------------------------------------
-- sameResult — positivity first, then each boundary
--------------------------------------------------------------------------------

private def r0 : RuntimeResult :=
  { outputs   := #[mk_bv 8 5, mk_bv 4 2],
    nextState := { flops := #[mk_bv 8 7], mems := #[fun x => mk_bv 8 x] } }

/- Positivity.  Without this a `sameResult` that returned `false` always would
score a rejection on every mutation below and look perfect. -/
#guard sameResult r0 r0 16 == true

private def rejects : Option RuntimeResult → Bool
  | none    => false          -- not applicable counts as NOT tested, never as a pass
  | some r' => !(sameResult r' r0 16)

#guard rejects (mutateOutput r0) == true
#guard rejects (mutateFlop r0) == true
#guard rejects (mutateMem r0 0 0) == true

/- Shape changes are caught separately from value changes: a reifier that
drops an output and a reifier that computes one wrongly are different bugs. -/
#guard sameResult { r0 with outputs := #[mk_bv 8 5] } r0 16 == false
#guard sameResult { r0 with nextState := { r0.nextState with flops := #[] } } r0 16 == false
#guard sameResult { r0 with nextState := { r0.nextState with mems := #[] } } r0 16 == false

/-- The memory limitation, stated as a test rather than a comment: images are
`Int → BV` and are compared at sampled addresses only, so a difference outside
the window is invisible.  Widening the window finds it. -/
private def rfar : RuntimeResult :=
  { r0 with nextState := { r0.nextState with
      mems := #[fun x => if x = 99 then mk_bv 8 0 else mk_bv 8 x] } }

#guard sameResult rfar r0 16 == true
#guard sameResult rfar r0 100 == false

/- A width-0 position admits no visible mutation, so the mutator must decline
it instead of reporting a change the checker cannot see. -/
#guard mutateOutput { outputs := #[mk_bv 0 0], nextState := { flops := #[], mems := #[] } } |>.isNone

--------------------------------------------------------------------------------
-- compileDesign refusal — the sweep's `compile` gate must be able to fail
--------------------------------------------------------------------------------

/-- A dependency outside the slot space. -/
private def badDep : DesignCert :=
  { sources  := #[.const 8 1]
    nodes    := #[{ op := LGraphOp.Op_And, width := 8, deps := #[99], origin := 0 }]
    outputs  := #[], flops := #[], memories := #[] }

#guard (match compileDesign badDep with | .ok _ => false | .error _ => true) == true
#guard compilesOk badDep == false

/-- …and the well-formed counterpart is accepted, so the test above is not
passing because `compileDesign` refuses everything. -/
private def goodDep : DesignCert :=
  { sources  := #[.const 8 1, .const 8 2]
    nodes    := #[{ op := LGraphOp.Op_And, width := 8, deps := #[0, 1], origin := 0 }]
    outputs  := #[{ slot := 2, width := 8 }], flops := #[], memories := #[] }

#guard (match compileDesign goodDep with | .ok _ => true | .error _ => false) == true
#guard compilesOk goodDep == true

--------------------------------------------------------------------------------
-- checkerSelfTest — `na` is reported, never scored as a pass
--------------------------------------------------------------------------------

#eval do
  match compileDesign goodDep with
  | .error _ => IO.println "SELFTEST-SETUP-FAILED"
  | .ok R =>
    let st := checkerSelfTest goodDep R
    -- goodDep has one output, no flops, no memories.
    IO.println s!"base={st.base} out={st.out} flop={st.flop} mem={st.mem}"
    if st.base && st.out == "rejected" && st.flop == "na" && st.mem == "na" then
      IO.println "D3HARNESS-TEST OK"
    else
      IO.println "D3HARNESS-TEST FAILED"

--------------------------------------------------------------------------------
-- observable — the diversity diagnostic must use sameResult's own boundary
--
-- Counting distinct OUTPUTS alone would call these two runs identical and
-- report the design as unresponsive, when its flop next-state (or its memory
-- image) is in fact tracking the stimulus. The agreement row for such a design
-- does carry differential evidence, so the diagnostic must see what the checker
-- sees.
--------------------------------------------------------------------------------

private def rA : RuntimeResult :=
  { outputs := #[mk_bv 8 1], nextState := { flops := #[mk_bv 8 1], mems := #[] } }
private def rB : RuntimeResult :=
  { outputs := #[mk_bv 8 1], nextState := { flops := #[mk_bv 8 2], mems := #[] } }

/- identical outputs ... -/
#guard (rA.outputs == rB.outputs) == true
/- ... but a different observable signature, because the flops differ. -/
#guard ((observable rA 16) == (observable rB 16)) == false

private def rM1 : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[], mems := #[fun x => mk_bv 8 x] } }
private def rM2 : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[], mems := #[fun x => mk_bv 8 (x + 1)] } }

/- memory-only variation is visible too, within the sampled window. -/
#guard ((observable rM1 16) == (observable rM2 16)) == false
/- and a zero-width window sees nothing, which is why `addrs` is explicit. -/
#guard ((observable rM1 0) == (observable rM2 0)) == true

--------------------------------------------------------------------------------
-- width-0 observables — present, but not mutable
--
-- `mk_bv 0 v` is always 0, so a width-0 position admits no VISIBLE change. A
-- design can therefore have outputs.size > 0 and still no mutable output, and
-- the self-test must report `na` for it. Judging that against the raw count
-- would mark a correct report as a checker failure, which is why the report
-- carries `mutable_*` separately.
--------------------------------------------------------------------------------

private def rZeroOut : RuntimeResult :=
  { outputs := #[mk_bv 0 0], nextState := { flops := #[mk_bv 0 0], mems := #[fun _ => mk_bv 0 0] } }

/- every observable is PRESENT ... -/
#guard rZeroOut.outputs.size == 1
#guard rZeroOut.nextState.flops.size == 1
#guard rZeroOut.nextState.mems.size == 1
/- ... and none of them is mutable. -/
#guard (mutateOutput rZeroOut).isNone == true
#guard (mutateFlop rZeroOut).isNone == true
#guard (mutateMem rZeroOut 0 0).isNone == true

/- A mixed case: the mutator must find the first WIDE position, not the first. -/
private def rMixed : RuntimeResult :=
  { outputs := #[mk_bv 0 0, mk_bv 8 3], nextState := { flops := #[], mems := #[] } }
#guard (mutateOutput rMixed).isSome == true
#guard (match mutateOutput rMixed with
        | some r => sameResult r rMixed 16
        | none   => true) == false

--------------------------------------------------------------------------------
-- descriptor-derived applicability must AGREE with the mutators
--
-- The report derives `mutable_*` from the DesignCert descriptors, never from
-- `mutateX ... |>.isSome`. Asking the mutator whether it applies and then
-- validating its answer against itself is circular: a `mutateOutput` that
-- regressed to `none` everywhere would report `mut_out=na, mutable_out=0` and
-- pass every check. These tests are what ties the two independent sources
-- together, so a regression in either one shows up here.
--------------------------------------------------------------------------------

private def certOf (outs : Array OutputDesc) (fl : Array FlopDesc)
    (ms : Array MemoryDesc) : DesignCert :=
  { sources := #[], nodes := #[], outputs := outs, flops := fl, memories := ms }

private def fd (w : Nat) : FlopDesc :=
  { width := w, din := 0, enable := none, resetPin := none,
    resetValue := 0, resetActiveLow := false }

/- positive width: descriptor says mutable, and the mutator agrees. -/
#guard ((certOf #[{ slot := 0, width := 8 }] #[] #[]).outputs.any (fun o => o.width > 0)) == true
#guard (mutateOutput { outputs := #[mk_bv 8 1], nextState := { flops := #[], mems := #[] } }).isSome == true

/- present but width 0: descriptor says NOT mutable, and the mutator declines. -/
#guard ((certOf #[{ slot := 0, width := 0 }] #[] #[]).outputs.any (fun o => o.width > 0)) == false
#guard (mutateOutput { outputs := #[mk_bv 0 0], nextState := { flops := #[], mems := #[] } }).isNone == true

/- mixed [width 0, width 8]: mutable, and the mutator finds the wide one. -/
#guard ((certOf #[{ slot := 0, width := 0 }, { slot := 1, width := 8 }] #[] #[]).outputs.any
         (fun o => o.width > 0)) == true
#guard (mutateOutput { outputs := #[mk_bv 0 0, mk_bv 8 3], nextState := { flops := #[], mems := #[] } }).isSome == true

/- the same three shapes for flops. -/
#guard ((certOf #[] #[fd 8] #[]).flops.any (fun f => f.width > 0)) == true
#guard ((certOf #[] #[fd 0] #[]).flops.any (fun f => f.width > 0)) == false
#guard ((certOf #[] #[fd 0, fd 8] #[]).flops.any (fun f => f.width > 0)) == true
#guard (mutateFlop { outputs := #[], nextState := { flops := #[mk_bv 0 0, mk_bv 8 1], mems := #[] } }).isSome == true

/- memories, including the index selection: memory 0 is zero-width, memory 1 is
   not, so a mutator fixed at index 0 would wrongly report "not mutable". -/
private def memCert : DesignCert :=
  certOf #[] #[] #[{ aw := 4, dw := 0, nextImg := 0 }, { aw := 4, dw := 8, nextImg := 0 }]
#guard (memCert.memories.any (fun m => m.dw > 0)) == true
#guard firstWideMem memCert == some 1
private def rTwoMems : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[], mems := #[(fun _ => mk_bv 0 0), (fun x => mk_bv 8 x)] } }
#guard (mutateMem rTwoMems 0 0).isNone == true
#guard (mutateMem rTwoMems ((firstWideMem memCert).getD 0) 0).isSome == true
#guard firstWideMem (certOf #[] #[] #[{ aw := 4, dw := 0, nextImg := 0 }]) == none

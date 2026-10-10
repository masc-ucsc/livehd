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

/-- Address plans used by the fixtures below.  `sameResult` now takes the plan
rather than a count, so the tests name the boundary explicitly. -/
private def PLAN1 : Array (List Int) := #[[0, 1, 2, 3]]
private def PLAN0 : Array (List Int) := #[[]]
private def PLAN_FAR : Array (List Int) := #[[0, 1, 2, 3, 99]]

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
#guard sameResult r0 r0 PLAN1 == true

private def rejects : Option RuntimeResult → Bool
  | none    => false          -- not applicable counts as NOT tested, never as a pass
  | some r' => !(sameResult r' r0 PLAN1)

#guard rejects (mutateOutput r0) == true
#guard rejects (mutateFlop r0) == true
#guard rejects (mutateMem r0 0 (0 : Int)) == true

/- Shape changes are caught separately from value changes: a reifier that
drops an output and a reifier that computes one wrongly are different bugs. -/
#guard sameResult { r0 with outputs := #[mk_bv 8 5] } r0 PLAN1 == false
#guard sameResult { r0 with nextState := { r0.nextState with flops := #[] } } r0 PLAN1 == false
#guard sameResult { r0 with nextState := { r0.nextState with mems := #[] } } r0 PLAN1 == false

/-- The memory limitation, stated as a test rather than a comment: images are
`Int → BV` and are compared at sampled addresses only, so a difference outside
the window is invisible.  Widening the window finds it. -/
private def rfar : RuntimeResult :=
  { r0 with nextState := { r0.nextState with
      mems := #[fun x => if x = 99 then mk_bv 8 0 else mk_bv 8 x] } }

#guard sameResult rfar r0 PLAN1 == true
#guard sameResult rfar r0 PLAN_FAR == false

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
#guard ((observable rA PLAN1) == (observable rB PLAN1)) == false

private def rM1 : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[], mems := #[fun x => mk_bv 8 x] } }
private def rM2 : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[], mems := #[fun x => mk_bv 8 (x + 1)] } }

/- memory-only variation is visible too, within the sampled window. -/
#guard ((observable rM1 PLAN1) == (observable rM2 PLAN1)) == false
/- and a zero-width window sees nothing, which is why `addrs` is explicit. -/
#guard ((observable rM1 PLAN0) == (observable rM2 PLAN0)) == true

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
#guard (mutateMem rZeroOut 0 (0 : Int)).isNone == true

/- A mixed case: the mutator must find the first WIDE position, not the first. -/
private def rMixed : RuntimeResult :=
  { outputs := #[mk_bv 0 0, mk_bv 8 3], nextState := { flops := #[], mems := #[] } }
#guard (mutateOutput rMixed).isSome == true
#guard (match mutateOutput rMixed with
        | some r => sameResult r rMixed PLAN1
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
#guard (mutateMem rTwoMems 0 (0 : Int)).isNone == true
#guard (mutateMem rTwoMems ((firstWideMem memCert).getD 0) (0 : Int)).isSome == true
#guard firstWideMem (certOf #[] #[] #[{ aw := 4, dw := 0, nextImg := 0 }]) == none

--------------------------------------------------------------------------------
-- HIGH BITS AND HIGH ADDRESSES
--
-- The previous harness minted every input and flop at width 128 and every
-- memory word at width 64, then let `bv_resize` zero-extend. Every bit above
-- those positions was constant zero on every stimulus, so a translation bug
-- living there was invisible while the gate reported `agree=1`. It also sampled
-- memory addresses 0..15 only.
--
-- Each fixture below differs from its partner ONLY in the region the old
-- harness could not see. `sameResult` must reject every one of them, and
-- `bvRand` must actually vary those bits.
--------------------------------------------------------------------------------

/- A 512-bit value whose bits above 127 are not all equal: the old width-128
   stimulus could not produce this. -/
#guard (bvRand 1 512).width == 512
#guard ((bvRand 1 512).value / (2 ^ 128)) != 0
#guard ((bvRand 1 512).value / (2 ^ 384)) != 0
/- independent chunks: two seeds differ above bit 128, not only below -/
#guard (((bvRand 1 512).value / (2 ^ 256)) == ((bvRand 2 512).value / (2 ^ 256))) == false
/- width 0 is explicit, and a very wide width does not overflow the host -/
#guard bvRand 3 0 == mk_bv 0 0
#guard (bvRand 4 4096).width == 4096

private def hiOutA : RuntimeResult :=
  { outputs := #[mk_bv 256 (2 ^ 200)], nextState := { flops := #[], mems := #[] } }
private def hiOutB : RuntimeResult :=
  { outputs := #[mk_bv 256 (2 ^ 200 + 2 ^ 199)], nextState := { flops := #[], mems := #[] } }

/- identical below bit 128, different above it -/
#guard (bv_uint (hiOutA.outputs[0]!) % (2 ^ 128)) == (bv_uint (hiOutB.outputs[0]!) % (2 ^ 128))
#guard sameResult hiOutA hiOutB PLAN1 == false

private def hiFlopA : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[mk_bv 300 (2 ^ 299)], mems := #[] } }
private def hiFlopB : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[mk_bv 300 0], mems := #[] } }
#guard (bv_uint (hiFlopA.nextState.flops[0]!) % (2 ^ 128))
       == (bv_uint (hiFlopB.nextState.flops[0]!) % (2 ^ 128))
#guard sameResult hiFlopA hiFlopB PLAN1 == false

/- memory DATA above bit 63: the old 64-bit word could not express it -/
private def hiMemA : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[], mems := #[fun _ => mk_bv 128 (2 ^ 100)] } }
private def hiMemB : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[], mems := #[fun _ => mk_bv 128 0] } }
#guard sameResult hiMemA hiMemB PLAN1 == false

/- memory ADDRESS above 15: the old 0..15 window could not reach it -/
private def hiAddrA : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[], mems := #[fun x => mk_bv 32 x] } }
private def hiAddrB : RuntimeResult :=
  { outputs := #[], nextState := { flops := #[], mems := #[fun x => if x = 65535 then mk_bv 32 0 else mk_bv 32 x] } }
/- invisible to the old window ... -/
#guard sameResult hiAddrA hiAddrB PLAN1 == true
/- ... and caught by a plan that includes the top address of a 16-bit space. -/
#guard (memAddrs 16).contains 65535 == true
#guard sameResult hiAddrA hiAddrB #[memAddrs 16] == false

/- Address-plan invariants, at every width that matters.  `aw = 64` is the one
   the earlier cap broke: it clipped to 24 and drew addresses modulo 2^24, so no
   address bit above 23 was ever sampled. -/
#guard (memAddrs 0) == [0]
#guard (memAddrs 1) == [0, 1]
#guard (memAddrs 16).contains 65535 == true
#guard (memAddrs 16).contains 32768 == true
#guard (memAddrs 4).contains 15 == true
#guard (memAddrs 64).contains (Int.ofNat (2 ^ 64 - 1)) == true
#guard (memAddrs 64).contains (Int.ofNat (2 ^ 63)) == true
/- ... and the random draws really do reach above bit 23 at aw = 64 -/
#guard ((memAddrs 64).any fun a => a > Int.ofNat (2 ^ 40)) == true

/- bounded at every width, and every address inside the space -/
#guard (memAddrs 0).length ≤ 11
#guard (memAddrs 1).length ≤ 11
#guard (memAddrs 4).length ≤ 11
#guard (memAddrs 16).length ≤ 11
#guard (memAddrs 64).length ≤ 11
#guard ((memAddrs 0).all fun a => 0 ≤ a ∧ a < Int.ofNat (2 ^ 0)) == true
#guard ((memAddrs 1).all fun a => 0 ≤ a ∧ a < Int.ofNat (2 ^ 1)) == true
#guard ((memAddrs 4).all fun a => 0 ≤ a ∧ a < Int.ofNat (2 ^ 4)) == true
#guard ((memAddrs 16).all fun a => 0 ≤ a ∧ a < Int.ofNat (2 ^ 16)) == true
#guard ((memAddrs 64).all fun a => 0 ≤ a ∧ a < Int.ofNat (2 ^ 64)) == true

/- full-width stimulus: a 512-bit input really is driven above bit 127.

   Sampled at k = 4, the FIRST pseudo-random stimulus.  k = 0..3 are now the
   deterministic edge prefix (`bvStim`), and full-widthness is a property of the
   random TAIL -- at k = 0 the correct value is all zero and has nothing above
   bit 127 by construction.  Checking the prefix here would pin the wrong thing. -/
private def wideCert : DesignCert :=
  { sources := #[.input 0 512], nodes := #[], outputs := #[], flops := #[], memories := #[] }
#guard (inputWidths wideCert) == #[512]
#guard ((stimIn wideCert 4)[0]!).width == 512
#guard (((stimIn wideCert 4)[0]!).value / (2 ^ 128)) != 0
/- and the prefix is what it claims to be on the same port -/
#guard ((stimIn wideCert 0)[0]!).value == 0
#guard ((stimIn wideCert 1)[0]!).value + 1 == 2 ^ 512
#guard ((stimIn wideCert 2)[0]!).value == 1
#guard ((stimIn wideCert 3)[0]!).value == 2 ^ 511

--------------------------------------------------------------------------------
-- stimSt must stop zero-extending STATE as well as inputs
--
-- The sameResult fixtures above prove the COMPARATOR sees high bits. They say
-- nothing about whether the stimulus ever puts anything there. These do: a
-- 512-bit flop and a 128-bit memory word, driven at their declared widths.
--------------------------------------------------------------------------------

private def wideFlop : FlopDesc :=
  { width := 512, din := 0, enable := none, resetPin := none,
    resetValue := 0, resetActiveLow := false }

private def stateCert : DesignCert :=
  { sources := #[], nodes := #[], outputs := #[], flops := #[wideFlop],
    memories := #[{ aw := 16, dw := 128, nextImg := 0 }] }

#guard ((stimSt stateCert 4).flops[0]!).width == 512
#guard (((stimSt stateCert 4).flops[0]!).value / (2 ^ 128)) != 0
#guard (((stimSt stateCert 4).flops[0]!).value / (2 ^ 384)) != 0

#guard (((stimSt stateCert 4).mems[0]!) 0).width == 128
#guard ((((stimSt stateCert 4).mems[0]!) 0).value / (2 ^ 64)) != 0
/- different addresses hold different words, including at a high address.
   Tail sample again: under the edge prefix every cell of a memory holds the
   SAME class value, which is the intended coherence and not an address bug. -/
#guard (((stimSt stateCert 4).mems[0]!) 0 == ((stimSt stateCert 4).mems[0]!) 65535) == false
#guard ((((stimSt stateCert 4).mems[0]!) 65535).value / (2 ^ 64)) != 0
/- the prefix reaches STATE too, and coherently: at k = 1 the flop and every
   memory cell are all ones, not just the inputs -/
#guard ((stimSt stateCert 0).flops[0]!).value == 0
#guard ((stimSt stateCert 1).flops[0]!).value + 1 == 2 ^ 512
#guard (((stimSt stateCert 1).mems[0]!) 0).value + 1 == 2 ^ 128
#guard (((stimSt stateCert 1).mems[0]!) 65535).value + 1 == 2 ^ 128
#guard (((stimSt stateCert 0).mems[0]!) 12345).value == 0
/- and the plan this design gets really does reach the top of its space -/
#guard (addrPlan stateCert)[0]!.contains 65535 == true

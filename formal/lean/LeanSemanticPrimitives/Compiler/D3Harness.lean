/-
# `D3Harness` — per-design gates for the Direction 3 sweep

Direction 3 emits a straight-line Lean `def` per design and must then say, for
that design, *which* of a chain of claims actually holds.  The chain is not one
claim: a certificate can elaborate while `compileDesign` refuses it, the
compiler can accept while the reifier cannot fold some constructor, the reified
`def` can typecheck while its first execution diverges from the interpreter.
Collapsing those into one "supported" bit is how a sweep comes to report
coverage it does not have.

So the harness reports a GATE LINE per claim, and the sweep driver reads the
last line printed rather than an exit code:

    D3GATE shape=...        the certificate's measured shape
    D3GATE sim_exec=1       the reified def RAN on one stimulus
    D3GATE agree=1|0        reified def = denoteResidual on N stimuli

`compileDesign` acceptance is NOT reported here: `reify_design` runs the
compiler at elaboration time and fails loudly if it refuses, and the kernel
checked form (`compilesOk = true` by `native_decide`) lives in the emitted
certificate.  Those are two different claims and the driver records them
separately.

## What the agreement gate does and does not establish

It is a DIFFERENTIAL test on sampled stimuli, not a proof.  It is here because
it is the gate that catches a reifier bug immediately and costs milliseconds,
and because the per-design proof — the claim that would subsume it — is the one
Direction 3 measured as unaffordable.  Nothing in this file should be reported
as verification.

Memory next-state images are `Int → BV`, so they admit no decidable equality.
They are compared at sampled addresses only, and that limitation is why
`sameResult` takes the address count explicitly instead of hiding a default.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign

namespace Compiler
namespace D3

/-- How many primary-input ordinals the design reads.

`DesignCert` has no input count: inputs are named by ORDINAL inside
`SourceDesc`, so the count is the largest ordinal mentioned.  An async reset
reads its own ordinal out of `RuntimeInput` (see `SourceDesc.flopQAsync`), so it
counts too — missing that produced a short stimulus array and a reset that read
as constant zero, which is exactly the shape a differential test would pass. -/
def numInputs (D : DesignCert) : Nat :=
  D.sources.foldl (fun a s => match s with
    | .input idx _                => max a (idx + 1)
    | .flopQAsync _ _ resetIn _ _ => max a (resetIn + 1)
    | _                           => a) 0

/-- Deterministic pseudo-random 64-bit-ish payload.  Two rounds of a
multiply-xor mix: one round leaves the low bits of `k` visible, which makes
consecutive stimuli differ only in their low bits and lets a width-mismatch bug
pass unnoticed. -/
def rnd (a b : Nat) : Int :=
  let x := (a * 2654435761 + b * 40503 + 12345) % 4294967291
  let y := (x * 2246822519 + b * 668265263 + 374761393) % 4294967291
  Int.ofNat (x * 4294967296 + y)

/-- Each primary-input ordinal's declared width: the widest `SourceDesc` that
reads it.

An ordinal can be read by more than one source, and an async reset reads its
ordinal as a 1-bit condition, so the stimulus has to cover the WIDEST claim on
each ordinal or the high bits of a wide port are never driven. -/
def inputWidths (D : DesignCert) : Array Nat :=
  D.sources.foldl (fun acc s => match s with
    | .input idx w                => acc.set! idx (max (acc[idx]!) w)
    | .flopQAsync _ _ resetIn _ _ => acc.set! resetIn (max (acc[resetIn]!) 1)
    | _                           => acc) (Array.replicate (numInputs D) 0)

/-- 32 deterministic pseudo-random bits. -/
def rnd32 (a b : Nat) : Nat := (rnd a b).toNat % 4294967296

/-- A deterministic value whose bits vary across the WHOLE of `w`.

Built from `ceil(w/32)` independent chunks, each mixed from a different `b`, so
no chunk is a function of its neighbours and a difference confined to the top
bits of a 512-bit port still shows up.  The previous harness minted everything
at width 128, which `bv_resize` then zero-extended: every bit above 127 was
constant zero on every stimulus, and a translation bug living there could not
have been seen.  Width 0 is explicit -- `mk_bv 0 v` is always 0 and there is
nothing to randomise. -/
def bvRand (seed w : Nat) : BV :=
  if w = 0 then mk_bv 0 0
  else
    let chunks := (w + 31) / 32
    mk_bv w (Int.ofNat ((List.range chunks).foldl
      (fun acc i => acc * 4294967296 + rnd32 seed (i + 1)) 0))

def stimIn (D : DesignCert) (k : Nat) : RuntimeInput :=
  let ws := inputWidths D
  Array.ofFn (n := ws.size) (fun i => bvRand (k * 7919 + i.val) (ws[i.val]!))

/-- Memory images are sampled, never compared whole, so the addresses matter as
much as the data.  The plan per memory: the lowest few, the TOP representable
address, the high-bit address `2^(aw-1)`, and pseudo-random addresses drawn at
the full address width.

No cap on `aw`.  An earlier version clipped it to 24 and drew the random
addresses modulo `2^24`, so for a 64-bit address space no address bit above 23
was ever exercised -- the exact blindness this plan exists to remove.  Lean's
`Nat` is arbitrary precision, so `2 ^ 64` is an ordinary numeral here and the
clip bought nothing.

Every address is `< 2 ^ aw` by construction: `top` is `2^aw - 1`, the high-bit
address is at most `top` for `aw ≥ 1`, the lows are clipped to the space (an
`aw = 0` memory has exactly ONE cell, and sampling 1..3 there would compare
addresses the design cannot have), and the random draws are reduced modulo the
size.  The count is bounded by 11 regardless of `aw`. -/
def memAddrs (aw : Nat) : List Int :=
  let size : Nat := 2 ^ aw
  let top : Nat := size - 1
  let lows : List Nat := List.range (min 4 size)
  let highs : List Nat := if aw = 0 then [] else [top, 2 ^ (aw - 1), top - top / 3]
  let rands : List Nat :=
    if aw = 0 then [] else
      (List.range 4).map fun i => ((bvRand (aw * 31 + i + 1) aw).value.toNat) % size
  ((lows ++ highs ++ rands).eraseDups).map Int.ofNat

/-- The address plan for every memory of a design, in memory order. -/
def addrPlan (D : DesignCert) : Array (List Int) :=
  D.memories.map (fun m => memAddrs m.aw)

def stimSt (D : DesignCert) (k : Nat) : RuntimeState :=
  { flops := Array.ofFn (n := D.flops.size)
               (fun i => bvRand (k * 6151 + i.val + 977) ((D.flops[i.val]!).width)),
    mems  := Array.ofFn (n := D.memories.size)
               (fun i x => bvRand (k * 3571 + i.val + 131 + x.toNat) ((D.memories[i.val]!).dw)) }

/-- Structural agreement on everything that admits it, plus the sampled reads
for the function-valued memory images.

Sizes are compared FIRST and separately.  `Array.==` on arrays of different
length is already false, but a size mismatch and a value mismatch are different
defects — one is a reifier shape bug, the other a semantics bug — and a single
boolean cannot tell the sweep which it found.

`plan` is the per-memory address sample.  It is an argument rather than a
constant so the mutation test and the diversity diagnostic can be given exactly
the same boundary; a checker that samples addresses the mutator never touches
would report rejections it did not earn. -/
def sameResult (a b : RuntimeResult) (plan : Array (List Int)) : Bool :=
  a.outputs.size == b.outputs.size
  && a.nextState.flops.size == b.nextState.flops.size
  && a.nextState.mems.size == b.nextState.mems.size
  && a.outputs == b.outputs
  && a.nextState.flops == b.nextState.flops
  && (List.range a.nextState.mems.size).all (fun j =>
       (plan[j]?.getD [0]).all (fun addr =>
         (a.nextState.mems[j]!) addr == (b.nextState.mems[j]!) addr))

/-- The reified definition against the interpreter, on `n` stimuli. -/
def agree (D : DesignCert) (fast : RuntimeInput → RuntimeState → RuntimeResult)
    (R : ResidualProgram) (n : Nat) : Bool :=
  let plan := addrPlan D
  (List.range n).all fun k =>
    sameResult (fast (stimIn D k) (stimSt D k))
               (denoteResidual R (stimIn D k) (stimSt D k)) plan

/-- Force every observable.  Reading only `outputs.size` lets the compiler
delete the whole chain — measured, and it reported a 20,000-iteration loop as
0.012 ms. -/
def obs (r : RuntimeResult) : Int :=
  r.outputs.foldl (fun a b => a + bv_uint b) 0
    + r.nextState.flops.foldl (fun a b => a + bv_uint b) 0

--------------------------------------------------------------------------------
-- Does the checker actually reject a wrong answer?
--
-- An earlier attempt at this perturbed the design's INPUTS and expected
-- disagreement.  That is not a test of the checker: a circuit may be genuinely
-- insensitive to the perturbed inputs on the sampled states, and one measured
-- here is (`debug_breakpoint`, four width-1 outputs, all zero on every
-- stimulus).  Such a design makes the input-perturbation control report
-- "agrees" while the checker is working perfectly.
--
-- The valid test mutates the FAST RESULT at an observable boundary, shape
-- preserved, and requires rejection.  That is a property of `sameResult` alone
-- and holds for every design that HAS the observable in question -- which is
-- why each mutator returns `Option` and the report says `na` rather than
-- silently scoring a design that has no flops as if its flop check passed.
--------------------------------------------------------------------------------

/-- A guaranteed-visible one-bit change.  Width 0 admits none: `mk_bv 0 v` is
always `0`, so a zero-width position is NOT a usable mutation site and the
search below skips it instead of reporting a mutation that cannot be seen. -/
def bumpBV (b : BV) : BV := mk_bv b.width (b.value + 1)

def firstWide (a : Array BV) : Option (Nat × BV) :=
  (List.range a.size).findSome? fun i =>
    if (a[i]!).width > 0 then some (i, a[i]!) else none

def mutateOutput (r : RuntimeResult) : Option RuntimeResult :=
  (firstWide r.outputs).map fun p =>
    { r with outputs := r.outputs.set! p.1 (bumpBV p.2) }

def mutateFlop (r : RuntimeResult) : Option RuntimeResult :=
  (firstWide r.nextState.flops).map fun p =>
    { r with nextState := { r.nextState with flops := r.nextState.flops.set! p.1 (bumpBV p.2) } }

/-- Memory mutation must land on an address `sameResult` actually samples, or a
rejection would depend on the sampling window rather than on the checker.  The
memory INDEX is supplied by the caller rather than fixed at 0, so the choice can
be made from the design's descriptors -- a design whose memory 0 is zero-width
and whose memory 1 is not would otherwise look unmutatable. -/
def mutateMem (r : RuntimeResult) (idx : Nat) (addr : Int) : Option RuntimeResult :=
  if idx ≥ r.nextState.mems.size then none
  else
    let f := r.nextState.mems[idx]!
    if (f addr).width = 0 then none
    else
      let g : Int → BV := fun x => if x = addr then bumpBV (f x) else f x
      some { r with nextState := { r.nextState with mems := r.nextState.mems.set! idx g } }

/-- Per-design self-test of the CHECKER, deterministic and independent of
whether the circuit responds to stimulus.

`base` is the positivity half: a result must agree with itself.  Without it a
`sameResult` that returned `false` unconditionally would score three rejections
and look perfect. -/
structure SelfTest where
  base : Bool
  out  : String
  flop : String
  mem  : String
  /-- Whether a VISIBLE mutation exists at all, which is not the same as the
  design having the observable.  A width-0 value admits no visible change
  (`mk_bv 0 v` is always `0`), so a design with `outputs = #[mk_bv 0 0]` has
  `outputs.size = 1` and still no mutable output.  Judging `na` against the raw
  count would call that correct report a checker failure.

  Derived from the `DesignCert` DESCRIPTORS, never from the mutators.  Asking
  the mutator whether it applies and then checking its answer against itself is
  circular: a `mutateOutput` that regressed to `none` everywhere would report
  `mut_out=na, mutable_out=0` and pass.  The descriptors are an independent
  source of the same fact, and `d3_harness_test.lean` pins that the two agree on
  positive-width, present-width-0 and mixed shapes. -/
  mutableOut  : Bool
  mutableFlop : Bool
  mutableMem  : Bool

/-- First memory whose DESCRIPTOR has a positive data width.  Chosen from the
certificate, so the mutation target does not depend on the mutator's own view. -/
def firstWideMem (D : DesignCert) : Option Nat :=
  (List.range D.memories.size).findSome? fun j =>
    if (D.memories[j]!).dw > 0 then some j else none

def checkerSelfTest (D : DesignCert) (R : ResidualProgram) : SelfTest :=
  let plan := addrPlan D
  let r := denoteResidual R (stimIn D 0) (stimSt D 0)
  let judge : Option RuntimeResult → String
    | none    => "na"
    | some r' => if sameResult r' r plan then "ACCEPTED" else "rejected"
  let memIdx := (firstWideMem D).getD 0
  -- Mutate at the MAXIMUM sampled address, not address 0 and not whichever
  -- entry happens to be last (that is a random draw, not the top).  A checker
  -- that only ever looks low would still reject a low mutation while being
  -- blind to every high address it claims to cover.
  let memAddr := (plan[memIdx]?.getD [0]).foldl max 0
  { base := sameResult r r plan
    out  := judge (mutateOutput r)
    flop := judge (mutateFlop r)
    mem  := judge (mutateMem r memIdx memAddr)
    -- descriptor-derived, independent of the mutators above
    mutableOut  := D.outputs.any (fun o => o.width > 0)
    mutableFlop := D.flops.any (fun f => f.width > 0)
    mutableMem  := D.memories.any (fun m => m.dw > 0) }

/-- Everything `sameResult` can see, flattened into one comparable value:
outputs, next-state flops, and the sampled memory reads.

The diagnostic below must use EXACTLY this boundary.  Counting distinct outputs
alone would call a design unresponsive when its outputs are constant but its
flop next-state or its memory image varies with the stimulus — and that design's
agreement row does carry differential evidence. -/
def observable (r : RuntimeResult) (plan : Array (List Int)) : Array BV :=
  let memVals := ((List.range r.nextState.mems.size).map fun j =>
    (plan[j]?.getD [0]).map fun a => (r.nextState.mems[j]!) a).flatten
  r.outputs ++ r.nextState.flops ++ memVals.toArray

/-- Stimulus-diversity diagnostic: how many DISTINCT observable signatures the
sampled stimuli produced, at `sameResult`'s own observation boundary.

`1` means every stimulus drove the design to the same observable state, so the
agreement row for it compares one value against itself `n` times and carries no
differential evidence.  That is a property of the STIMULUS and the DESIGN, not
of the checker — a sound checker still reports `agree=1` — which is why this is
reported beside `checkerSelfTest` rather than instead of it. -/
def distinctObservables (D : DesignCert) (f : RuntimeInput → RuntimeState → RuntimeResult)
    (n : Nat) : Nat :=
  let plan := addrPlan D
  let sigs := (List.range n).map fun k => observable (f (stimIn D k) (stimSt D k)) plan
  (sigs.foldl (fun acc s => if acc.contains s then acc else s :: acc) []).length

/-- Print one gate line per claim, in the order the driver expects.  Each line
is emitted only once the claim it reports has been evaluated, so the LAST line
present in a truncated log names the first gate that failed. -/
def report (name : String) (D : DesignCert)
    (fast : RuntimeInput → RuntimeState → RuntimeResult) (R : ResidualProgram)
    (samples : Nat := 32) : IO Unit := do
  IO.println s!"D3GATE module={name} sources={D.sources.size} nodes={D.nodes.size} \
outputs={D.outputs.size} flops={D.flops.size} mems={D.memories.size} \
inputs={numInputs D} bindings={R.bindings.size}"
  let r := fast (stimIn D 1) (stimSt D 1)
  IO.println s!"D3GATE sim_exec=1 obs={obs r}"
  let st := checkerSelfTest D R
  IO.println s!"D3GATE selftest_base={if st.base then 1 else 0} mut_out={st.out} \
mut_flop={st.flop} mut_mem={st.mem} \
mutable_out={if st.mutableOut then 1 else 0} \
mutable_flop={if st.mutableFlop then 1 else 0} \
mutable_mem={if st.mutableMem then 1 else 0}"
  IO.println s!"D3GATE agree={if agree D fast R samples then 1 else 0} samples={samples} \
distinct_obs={distinctObservables D fast samples}"

end D3
end Compiler

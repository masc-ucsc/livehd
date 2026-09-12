/-
# `DirectTests` — phase 2's execution gate, as build-time assertions

Every `#guard` below runs during `lake build`, so a regression is a BUILD
failure rather than a test someone forgot to run.

Three kinds of check:

  1. the five accepted shapes execute, for several cycles, and agree with
     `interpretDesign` (which is quadratic and never otherwise run — these
     designs are small enough that it can be);
  2. each malformed certificate is REFUSED, and refused for the stated reason,
     rather than evaluating to zero through a fallback;
  3. the asynchronous-reset and memory cycle rules produce the values the
     contract says they should, not merely values both sides agree on.
-/
import LeanSemanticPrimitives.Compiler.DirectExamples

namespace Compiler
namespace Direct
namespace Tests

open Examples

--------------------------------------------------------------------------------
-- Helpers
--------------------------------------------------------------------------------

/-- Direct execution and the reference semantics agree on the observable
outputs and on the next flop state.  Memories are functions, so they are sampled
separately rather than compared with `DecidableEq`. -/
def agrees (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : Bool :=
  let a := directStepRaw D i s
  let b := interpretDesign D i s
  decide (a.outputs = b.outputs) && decide (a.nextState.flops = b.nextState.flops)

/-- …and on a memory image, sampled at the given addresses. -/
def agreesMem (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)
    (m : Nat) (addrs : List Int) : Bool :=
  let a := (directStepRaw D i s).nextState.mems[m]!
  let b := (interpretDesign D i s).nextState.mems[m]!
  addrs.all fun x => decide (a x = b x)

/-- Run `n` cycles and collect the outputs, by iterating the CHECKED trace
runner.  `none` means the design or some cycle was refused. -/
def outputTrace (D : DesignCert) (s : RuntimeState) (is : List RuntimeInput) :
    Option (List (Array BV)) :=
  match runDirect D s is with
  | .error _ => none
  | .ok t    => some (t.steps.map RuntimeResult.outputs)

def finalFlops (D : DesignCert) (s : RuntimeState) (is : List RuntimeInput) :
    Option (Array BV) :=
  match runDirect D s is with
  | .error _ => none
  | .ok t    => some t.finalState.flops

def refusalTag (D : DesignCert) : Option String :=
  (designErrors D).map SimError.tag

--------------------------------------------------------------------------------
-- 1. The five accepted shapes are accepted
--------------------------------------------------------------------------------

#guard acceptsDesign combAddMask
#guard acceptsDesign counterEnabled
#guard acceptsDesign counterAsyncReset
#guard acceptsDesign romTable
#guard acceptsDesign memReadWrite

--------------------------------------------------------------------------------
-- 2. Combinational
--------------------------------------------------------------------------------

-- (3 + 5) & 0xF = 8
#guard (directStepRaw combAddMask #[mk_bv 4 3, mk_bv 4 5] (zeroState combAddMask)).outputs
        = #[mk_bv 4 8]
-- (9 + 12) & 0xF = 21 & 15 = 5  (the 4-bit sum wraps first)
#guard (directStepRaw combAddMask #[mk_bv 4 9, mk_bv 4 12] (zeroState combAddMask)).outputs
        = #[mk_bv 4 5]
#guard agrees combAddMask #[mk_bv 4 3, mk_bv 4 5] (zeroState combAddMask)
#guard agrees combAddMask #[mk_bv 4 9, mk_bv 4 12] (zeroState combAddMask)
#guard agrees combAddMask #[mk_bv 4 15, mk_bv 4 15] (zeroState combAddMask)

--------------------------------------------------------------------------------
-- 3. Sequential: four enabled cycles count 0,1,2,3 and leave Q = 4
--------------------------------------------------------------------------------

def en1 : RuntimeInput := #[mk_bv 1 1]
def en0 : RuntimeInput := #[mk_bv 1 0]

#guard outputTrace counterEnabled (zeroState counterEnabled) [en1, en1, en1, en1]
        = some [#[mk_bv 4 0], #[mk_bv 4 1], #[mk_bv 4 2], #[mk_bv 4 3]]
#guard finalFlops counterEnabled (zeroState counterEnabled) [en1, en1, en1, en1]
        = some #[mk_bv 4 4]
-- the enable actually holds
#guard outputTrace counterEnabled (zeroState counterEnabled) [en1, en0, en0, en1]
        = some [#[mk_bv 4 0], #[mk_bv 4 1], #[mk_bv 4 1], #[mk_bv 4 1]]
#guard finalFlops counterEnabled (zeroState counterEnabled) [en1, en0, en0, en1]
        = some #[mk_bv 4 2]
#guard agrees counterEnabled en1 (zeroState counterEnabled)
#guard agrees counterEnabled en0 { flops := #[mk_bv 4 9], mems := #[] }

--------------------------------------------------------------------------------
-- 4. Asynchronous reset is visible IN THE SAME CYCLE
--------------------------------------------------------------------------------

def rstAsserted : RuntimeInput := #[mk_bv 1 0]   -- active low
def rstReleased : RuntimeInput := #[mk_bv 1 1]

-- Q holds 5 and reset is asserted: the output must read 0 THIS cycle.  A plain
-- `flopQ` source would report 5 -- that difference is the whole reason
-- `flopQAsync` exists.
#guard (directStepRaw counterAsyncReset rstAsserted { flops := #[mk_bv 4 5], mems := #[] }).outputs
        = #[mk_bv 4 0]
-- With reset released the same state reads 5.
#guard (directStepRaw counterAsyncReset rstReleased { flops := #[mk_bv 4 5], mems := #[] }).outputs
        = #[mk_bv 4 5]
-- reset, release, count, re-assert
#guard outputTrace counterAsyncReset { flops := #[mk_bv 4 5], mems := #[] }
        [rstAsserted, rstReleased, rstReleased, rstAsserted]
        = some [#[mk_bv 4 0], #[mk_bv 4 0], #[mk_bv 4 1], #[mk_bv 4 0]]
#guard agrees counterAsyncReset rstAsserted { flops := #[mk_bv 4 5], mems := #[] }
#guard agrees counterAsyncReset rstReleased { flops := #[mk_bv 4 5], mems := #[] }

--------------------------------------------------------------------------------
-- 5. Inlined ROM
--------------------------------------------------------------------------------

#guard (directStepRaw romTable #[mk_bv 2 0] (zeroState romTable)).outputs = #[mk_bv 8 10]
#guard (directStepRaw romTable #[mk_bv 2 2] (zeroState romTable)).outputs = #[mk_bv 8 30]
#guard (directStepRaw romTable #[mk_bv 2 3] (zeroState romTable)).outputs = #[mk_bv 8 40]
#guard agrees romTable #[mk_bv 2 1] (zeroState romTable)
-- a ROM contributes NO entry to the runtime state
#guard (zeroState romTable).mems.size = 0

--------------------------------------------------------------------------------
-- 6. Mutable memory: the read sees the PRE-write image, the next image has the write
--------------------------------------------------------------------------------

/-- `waddr = 1`, `wdata = 77`, `we = 1`, `raddr = 1`. -/
def wr1 : RuntimeInput := #[mk_bv 2 1, mk_bv 8 77, mk_bv 1 1, mk_bv 2 1]
/-- no write, raddr = 1 -/
def rd1 : RuntimeInput := #[mk_bv 2 0, mk_bv 8 0, mk_bv 1 0, mk_bv 2 1]

-- cycle 1: the read is of the OLD image, so 0, not 77
#guard (directStepRaw memReadWrite wr1 (zeroState memReadWrite)).outputs = #[mk_bv 8 0]
-- but the next image does hold 77 at address 1, and 0 elsewhere
#guard (directStepRaw memReadWrite wr1 (zeroState memReadWrite)).nextState.mems[0]! 1
        = mk_bv 8 77
#guard (directStepRaw memReadWrite wr1 (zeroState memReadWrite)).nextState.mems[0]! 2
        = mk_bv 8 0
-- cycle 2 reads it back
#guard (outputTrace memReadWrite (zeroState memReadWrite) [wr1, rd1])
        = some [#[mk_bv 8 0], #[mk_bv 8 77]]
#guard agrees memReadWrite wr1 (zeroState memReadWrite)
#guard agreesMem memReadWrite wr1 (zeroState memReadWrite) 0 [0, 1, 2, 3]
#guard agreesMem memReadWrite rd1 (zeroState memReadWrite) 0 [0, 1, 2, 3]

--------------------------------------------------------------------------------
-- 6b. Scale: a 1000-deep dependency chain
--------------------------------------------------------------------------------

-- node i computes i+1, so the last node is `n`, taken mod 2^8
#guard acceptsDesign (addChain 1000)
#guard (directStepRaw (addChain 1000) #[mk_bv 8 0] (zeroState (addChain 1000))).outputs
        = #[mk_bv 8 232]     -- 1000 mod 256
#guard agrees (addChain 200) #[mk_bv 8 0] (zeroState (addChain 200))

--------------------------------------------------------------------------------
-- 7. Malformed certificates are refused, and refused for the RIGHT reason
--------------------------------------------------------------------------------

#guard refusalTag badArityNot          = some "badArity"
#guard refusalTag badKindMemRead       = some "depKindMismatch"
#guard refusalTag badDepOrder          = some "depNotEarlier"
#guard refusalTag badZeroWidth         = some "zeroWidth"
#guard refusalTag badUnsupportedOp     = some "unsupportedOp"
#guard refusalTag badPredicateWidth    = some "badResultWidth"
#guard refusalTag badFlopOrdinal       = some "flopOrdOutOfRange"
#guard refusalTag badOutputKind        = some "slotNotBV"
#guard refusalTag badAsyncResetValue   = some "asyncResetValueMismatch"
#guard refusalTag badRomSize           = some "romTooLarge"

-- and the refusal reaches the public API, before any evaluation
#guard (directStep badArityNot #[mk_bv 4 1, mk_bv 4 1] (zeroState badArityNot)).isOk = false
#guard (runDirect badKindMemRead (zeroState badKindMemRead) [#[mk_bv 2 0]]).isOk = false

--------------------------------------------------------------------------------
-- 8. Runtime shape refusals
--------------------------------------------------------------------------------

-- the design reads two primary inputs; one is not enough
#guard (checkRuntime combAddMask #[mk_bv 4 1] (zeroState combAddMask)).isOk = false
#guard (checkRuntime combAddMask #[mk_bv 4 1, mk_bv 4 2] (zeroState combAddMask)).isOk = true
-- a state with the wrong number of flops
#guard (checkRuntime counterEnabled en1 { flops := #[], mems := #[] }).isOk = false
#guard (checkRuntime counterEnabled en1 { flops := #[mk_bv 4 0], mems := #[] }).isOk = true
-- a state with the wrong number of memories
#guard (checkRuntime memReadWrite wr1 { flops := #[], mems := #[] }).isOk = false

--------------------------------------------------------------------------------
-- 9. Negative controls: each single-field mutation changes the trace
--------------------------------------------------------------------------------

/-- A mutant must either be refused or produce a DIFFERENT output trace.  Both
sides are run through the checked entry point, so a mutant that the checker
happens to reject also counts as detected. -/
def mutantDetected (orig mutant : DesignCert) (is : List RuntimeInput) : Bool :=
  match runDirect mutant (zeroState mutant) is with
  | .error _ => true
  | .ok tm =>
    match runDirect orig (zeroState orig) is with
    | .error _ => false
    | .ok to   => decide (tm.steps.map RuntimeResult.outputs
                            ≠ to.steps.map RuntimeResult.outputs)

def countUp : List RuntimeInput := [en1, en1, en0, en1]
def rstSeq  : List RuntimeInput := [rstAsserted, rstReleased, rstReleased]
def memSeq  : List RuntimeInput := [wr1, rd1]

#guard mutantDetected counterEnabled     mutOperator          countUp
#guard mutantDetected counterEnabled     mutDependency        countUp
#guard mutantDetected counterEnabled     mutConstant          countUp
#guard mutantDetected counterEnabled     mutOutputSlot        countUp
#guard mutantDetected counterEnabled     mutFlopEnable        countUp
#guard mutantDetected counterAsyncReset  mutFlopResetPolarity rstSeq
#guard mutantDetected memReadWrite       mutMemoryNextImg     memSeq

-- the mutants are WELL-FORMED: what is being tested is the evaluator's
-- sensitivity to each field, not the checker's
#guard acceptsDesign mutOperator
#guard acceptsDesign mutDependency
#guard acceptsDesign mutConstant
#guard acceptsDesign mutOutputSlot
#guard acceptsDesign mutFlopEnable
#guard acceptsDesign mutFlopResetPolarity
#guard acceptsDesign mutMemoryNextImg

--------------------------------------------------------------------------------
-- 10. `inputArity`
--------------------------------------------------------------------------------

#guard inputArity combAddMask       = 2
#guard inputArity counterEnabled    = 1
#guard inputArity counterAsyncReset = 1
#guard inputArity memReadWrite      = 4

end Tests
end Direct
end Compiler

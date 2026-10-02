/-
# Last-use analysis for the incremental walk

The walk re-establishes one fact per LIVE produced slot at every step, so the
analysis below is what keeps that from being O(N) facts per step. Two cases are
easy to get wrong and both are pinned here:

  * an EARLY-DEAD value -- produced, consumed once, then never read again. It
    must drop out of the live set immediately, or a long chain pays for it to
    the end.
  * a TERMINAL-ONLY reference -- a value whose only consumer is an output, a
    flop field or a memory next-image. No later binding reads it, so a last-use
    computed from binding operands alone would call it dead at the step that
    produced it and drop the fact the final rewrite needs.

Every check is a `#guard`, so this file fails the build if any is false.

Run: lake env lean probes/d3_liveness_test.lean
-/
import LeanSemanticPrimitives.Compiler.ReifyProof

open Compiler Compiler.ReifyProof

--------------------------------------------------------------------------------
-- Typed operand reads: the positional memory/bit-vector split
--------------------------------------------------------------------------------

-- a memory READ takes its image as a memory and its address/enable as bit vectors
#guard exprRefsTyped (.rmemRead 8 1 2 3) == #[(1, true), (2, false), (3, false)]
-- a memory WRITE likewise, with data also a bit vector
#guard exprRefsTyped (.rmemWrite 1 2 3 4) == #[(1, true), (2, false), (3, false), (4, false)]
#guard exprRefsTyped (.rmemWriteBE 8 8 1 2 3 4)
         == #[(1, true), (2, false), (3, false), (4, false)]
-- a mux reads three bit vectors, selector first
#guard exprRefsTyped (.rmux 8 1 2 3) == #[(1, false), (2, false), (3, false)]
-- array-valued constructors keep operand ORDER
#guard exprRefsTyped (.rsum 8 1 #[4, 7, 2]) == #[(4, false), (7, false), (2, false)]
#guard exprRefsTyped (.rnot 8 5) == #[(5, false)]
#guard exprRefsTyped (.rgetMask 8 6 7) == #[(6, false), (7, false)]

--------------------------------------------------------------------------------
-- The fixture: a chain, a diamond, an early-dead value, a terminal-only output
--
--   sources   0, 1
--   b0 -> 2   reads sources 0,1
--   b1 -> 3   reads b0                  \
--   b2 -> 4   reads b0                   }  DIAMOND over b0
--   b3 -> 5   reads b1, b2              /   -- and b0 is EARLY-DEAD after b2
--   b4 -> 6   reads b3                      -- chain
--   b5 -> 7   reads source 0                -- TERMINAL-ONLY: read by no binding
--   output      slot 7                      --   only by the output
--------------------------------------------------------------------------------

def fx : ResidualProgram :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.const 8 (-1) ]
    bindings := #[ { ty := .bv 8, rhs := .rand 8 #[0, 1] }      -- b0 -> slot 2
                 , { ty := .bv 8, rhs := .rnot 8 2 }            -- b1 -> slot 3
                 , { ty := .bv 8, rhs := .rnot 8 2 }            -- b2 -> slot 4
                 , { ty := .bv 8, rhs := .rand 8 #[3, 4] }      -- b3 -> slot 5
                 , { ty := .bv 8, rhs := .rnot 8 5 }            -- b4 -> slot 6
                 , { ty := .bv 8, rhs := .rnot 8 0 } ]          -- b5 -> slot 7
    outputs  := #[ { slot := 7, width := 8 } ]
    flopUpdates   := #[]
    memoryUpdates := #[] }

-- slot 7 is read by NOTHING but the output, and `terminalRefs` must see it
#guard terminalRefs fx == #[(7, false)]

-- after b0 (step 0): b0's slot 2 is live -- b1 and b2 both still read it
#guard (liveAfter fx)[0]! == #[(2, false)]
-- after b1: slot 2 still live (b2 reads it), slot 3 live (b3 reads it)
#guard ((liveAfter fx)[1]!).toList.length == 2
#guard ((liveAfter fx)[1]!).contains (2, false)
#guard ((liveAfter fx)[1]!).contains (3, false)
-- after b2: slot 2 is EARLY-DEAD -- its last consumer has run. 3 and 4 live.
#guard ((liveAfter fx)[2]!).contains (2, false) == false
#guard ((liveAfter fx)[2]!).contains (3, false)
#guard ((liveAfter fx)[2]!).contains (4, false)
#guard ((liveAfter fx)[2]!).toList.length == 2
-- after b3: 3 and 4 are now dead too, only 5 survives
#guard (liveAfter fx)[3]! == #[(5, false)]
-- after b4: slot 6 is produced but NOTHING reads it -- dead immediately, and the
-- live set is empty rather than carrying it to the end
#guard (liveAfter fx)[4]! == #[]
-- after b5: slot 7 is live ONLY because the OUTPUT reads it. This is the case a
-- binding-operand-only analysis gets wrong.
#guard (liveAfter fx)[5]! == #[(7, false)]

-- the live cut never exceeds 2 here, which is the figure the walk's cost tracks
#guard ((liveAfter fx).map (fun a => a.size)).toList.foldl max 0 == 2

--------------------------------------------------------------------------------
-- Terminal consumers that are NOT outputs: flop fields and memory next-images
--------------------------------------------------------------------------------

def fxSeq : ResidualProgram :=
  { sources  := #[ SourceDesc.input 0 8 ]
    bindings := #[ { ty := .bv 8, rhs := .rnot 8 0 }            -- slot 1: flop din
                 , { ty := .bv 1, rhs := .rnot 1 0 }            -- slot 2: flop enable
                 , { ty := .bv 1, rhs := .rnot 1 0 }            -- slot 3: flop reset
                 , { ty := .mem 4 8, rhs := .rmemWrite 0 0 0 0 } ]  -- slot 4: mem image
    outputs  := #[]
    flopUpdates   := #[ { width := 8, din := 1, enable := some 2, resetPin := some 3,
                          resetValue := 0, resetActiveLow := false } ]
    memoryUpdates := #[ { aw := 4, dw := 8, nextImg := 4 } ] }

-- din, enable and resetPin are all terminal consumers, and the memory image is
-- read AS A MEMORY
#guard (terminalRefs fxSeq).contains (1, false)
#guard (terminalRefs fxSeq).contains (2, false)
#guard (terminalRefs fxSeq).contains (3, false)
#guard (terminalRefs fxSeq).contains (4, true)
#guard (terminalRefs fxSeq).size == 4
-- every one of them stays live to the end; the memory-valued one keeps its
-- memory flag, because its fact is a `refMem` fact and not a `refBV` one
#guard ((liveAfter fxSeq)[3]!).contains (4, true)
#guard ((liveAfter fxSeq)[3]!).contains (4, false) == false
#guard ((liveAfter fxSeq)[3]!).size == 4

#eval IO.println "D3LIVENESS OK"

/-
# `bvStim` — direct tests for the edge-stimulus prefix

Checks the stimulus generator itself, not a design through it, so a regression
in the prefix is caught where it lives rather than as a mysterious change in one
module's `distinct_obs`.

Every check is a `#guard`, so this file FAILS THE BUILD if any of them is false.

Run: lake env lean probes/d3_stimulus_test.lean
-/
import LeanSemanticPrimitives.Compiler.D3Harness

open Compiler Compiler.D3

-- ---------------------------------------------------------------------------
-- Width 0: every class collapses to the one value a 0-bit port can take.
-- `mk_bv 0 v` is 0 for every v, so this is about the function not diverging or
-- indexing `w - 1 = 0 - 1` on a Nat.
-- ---------------------------------------------------------------------------
#guard (bvStim 0 0 0).value == 0
#guard (bvStim 0 0 1).value == 0
#guard (bvStim 0 0 2).value == 0
#guard (bvStim 0 0 3).value == 0
#guard (bvStim 7 0 9).value == 0
#guard (bvStim 0 0 0).width == 0

-- ---------------------------------------------------------------------------
-- Width 1: all-ones, LSB and MSB are all the single bit.  k=2 and k=3 coincide
-- here, which is correct rather than a gap.
-- ---------------------------------------------------------------------------
#guard (bvStim 0 1 0).value == 0
#guard (bvStim 0 1 1).value == 1
#guard (bvStim 0 1 2).value == 1
#guard (bvStim 0 1 3).value == 1
#guard (bvStim 0 1 3).width == 1

-- ---------------------------------------------------------------------------
-- Width 33: the width that exposed the gap (txfma_frac_zero_detect's input).
-- all ones = 2^33 - 1 = 8589934591, MSB = 2^32 = 4294967296.
-- ---------------------------------------------------------------------------
#guard (bvStim 0 33 0).value == 0
#guard (bvStim 0 33 1).value == 8589934591
#guard (bvStim 0 33 2).value == 1
#guard (bvStim 0 33 3).value == 4294967296
#guard (bvStim 0 33 1).width == 33
-- the four classes are four DISTINCT values at this width
#guard ([0, 1, 2, 3].map (fun k => (bvStim 0 33 k).value)).eraseDups.length == 4

-- ---------------------------------------------------------------------------
-- A wide width: 640, the widest port in the corpus (null_vpu).  Stated by
-- property rather than by a 193-digit literal.
-- ---------------------------------------------------------------------------
#guard (bvStim 0 640 0).value == 0
-- all ones: every bit set, and one more would carry out of the width
#guard (List.range 640).all (fun i => bv_bit (bvStim 0 640 1) i)
#guard (bvStim 0 640 1).value + 1 == 2 ^ 640
-- LSB only
#guard (bvStim 0 640 2).value == 1
-- MSB only: bit 639 set, nothing else
#guard bv_bit (bvStim 0 640 3) 639
#guard (List.range 639).all (fun i => ! bv_bit (bvStim 0 640 3) i)
#guard (bvStim 0 640 3).value == 2 ^ 639
-- the tail is still full-width: a 640-bit random draw must have bits above 127,
-- which is the regression `bvRand`'s chunking fixed and the prefix must not undo
#guard (List.range' 128 512).any (fun i => bv_bit (bvStim 5 640 9) i)

-- ---------------------------------------------------------------------------
-- The prefix does not disturb the tail: k >= 4 is exactly `bvRand`.
-- ---------------------------------------------------------------------------
#guard (bvStim 11 64 4).value == (bvRand 11 64).value
#guard (bvStim 11 64 31).value == (bvRand 11 64).value

-- ---------------------------------------------------------------------------
-- The zero-detector fixture: BOTH observations occur within the edge prefix.
--
-- This is `txfma_frac_zero_detect`'s shape -- NOT(reduction_OR(input)) on a
-- 33-bit input -- and it is the case the old uniform stimulus could not reach:
-- exactly one of 2^33 inputs gives output 1.
-- ---------------------------------------------------------------------------
def zeroDetectCert : DesignCert :=
  { sources  := #[ SourceDesc.const 34 (-1)
                 , SourceDesc.const 2 (-1)
                 , SourceDesc.input 0 33 ]
    nodes    := #[ { op := LGraphOp.Op_GetMask, width := 34, deps := #[2, 0], origin := 0 }
                 , { op := LGraphOp.Op_Ror,     width := 1,  deps := #[3],    origin := 1 }
                 , { op := LGraphOp.Op_GetMask, width := 2,  deps := #[4, 1], origin := 2 }
                 , { op := LGraphOp.Op_Not,     width := 1,  deps := #[5],    origin := 3 } ]
    outputs  := #[ { slot := 6, width := 1 } ]
    flops    := #[]
    memories := #[] }

/-- The design's own semantics, through the verified compiler. -/
def zdOut (k : Nat) : Int :=
  ((compileAndRun zeroDetectCert (stimIn zeroDetectCert k)
      (stimSt zeroDetectCert k)).outputs[0]!).value

-- k = 0 drives the input all-zero, which is the ONLY input that detects.
#guard zdOut 0 == 1
-- every other edge class is nonzero, so the detector reads 0.
#guard zdOut 1 == 0
#guard zdOut 2 == 0
#guard zdOut 3 == 0
-- BOTH observations occur within the 4-sample prefix: the property the old
-- uniform stimulus could not deliver at any realistic sample count.
#guard ([0, 1, 2, 3].map zdOut).eraseDups.length == 2
-- and `distinctObservables` -- what the row's `distinct_obs` reports -- agrees,
-- at 4 samples and at the 32 the sweep uses.
#guard distinctObservables zeroDetectCert (compileAndRun zeroDetectCert)
         4 == 2
#guard distinctObservables zeroDetectCert (compileAndRun zeroDetectCert)
         32 == 2

-- Falsification: with the OLD stimulus (pseudo-random at every k) the same
-- design yields ONE observation over 32 samples.  If this ever reports 2, the
-- fixture has stopped demonstrating anything and the guards above are passing
-- for a reason other than the prefix.
def zdOutRand (k : Nat) : Int :=
  let ws := inputWidths zeroDetectCert
  let inp : RuntimeInput :=
    Array.ofFn (n := ws.size) (fun i => bvRand (k * 7919 + i.val) (ws[i.val]!))
  ((compileAndRun zeroDetectCert inp { flops := #[], mems := #[] }).outputs[0]!).value
#guard ((List.range 32).map zdOutRand).eraseDups.length == 1

#eval IO.println "D3STIM OK"

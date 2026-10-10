/-
# `flopNextV`: the sequential semantics, in one place

`flopNext` reads its operands out of a `SlotEnv`; a generated fast function names
them. Both are now the SAME definition applied to different arguments, because
two spellings of reset priority is how the two drift apart -- and the drift would
be invisible, since each spelling is individually plausible.

`flopNext_eq` is the bridge. The guards below pin every behaviour the refactor
had to preserve, at the value level where they are readable.

Run: lake env lean probes/d3_flopnext_test.lean
-/
import LeanSemanticPrimitives.Compiler.Reify

open Compiler

-- shorthands: width 8 throughout, so truncation is visible
private def V (n : Int) : BV := mk_bv 8 n
private def B (n : Int) : BV := mk_bv 1 n

-- CALLER-ONLY ADAPTATION. `flopNextV` gained an explicit `edge` and
-- `asyncReset`; every assertion below is the pinned D3 one, with `true false`
-- appended. That is D3's own contract written out: its clockless helper always
-- committed, which is this helper with the domain FIRING and the reset
-- synchronous -- so each original case keeps its original meaning. The quiet
-- and asynchronous arms are exercised in d3_generated_clock_test.lean.


-- ---- no reset, no enable: always writes the resized din ---------------------
#guard flopNextV 8 (V 7) none none 0 false (some (V 99)) true false == V 7
#guard flopNextV 8 (V 7) none none 0 false none true false == V 7
-- din WIDER than the flop is resized down, not rejected
#guard flopNextV 4 (mk_bv 8 255) none none 0 false none true false == mk_bv 4 15

-- ---- enable ----------------------------------------------------------------
#guard flopNextV 8 (V 7) (some (B 1)) none 0 false (some (V 99)) true false == V 7
-- enable FALSE retains the old state ...
#guard flopNextV 8 (V 7) (some (B 0)) none 0 false (some (V 99)) true false == V 99
-- ... and falls back to zero ONLY when the index is absent. A missing index is
-- not the same as a zero flop, and conflating them hides an indexing bug.
#guard flopNextV 8 (V 7) (some (B 0)) none 0 false none true false == V 0

-- ---- reset polarity --------------------------------------------------------
-- active HIGH: pin 1 resets, pin 0 does not
#guard flopNextV 8 (V 7) none (some (B 1)) 5 false none true false == V 5
#guard flopNextV 8 (V 7) none (some (B 0)) 5 false none true false == V 7
-- active LOW: the senses swap
#guard flopNextV 8 (V 7) none (some (B 0)) 5 true none true false == V 5
#guard flopNextV 8 (V 7) none (some (B 1)) 5 true none true false == V 7

-- ---- resetValue is loaded, and TRUNCATED to the width -----------------------
#guard flopNextV 8 (V 7) none (some (B 1)) 300 false none true false == mk_bv 8 300
#guard flopNextV 8 (V 7) none (some (B 1)) 300 false none true false == V 44   -- 300 mod 256
#guard flopNextV 8 (V 7) none (some (B 1)) (-1) false none true false == V 255

-- ---- RESET HAS PRIORITY OVER ENABLE ----------------------------------------
-- reset asserted and enable FALSE: reset wins, the old state is NOT retained
#guard flopNextV 8 (V 7) (some (B 0)) (some (B 1)) 5 false (some (V 99)) true false == V 5
-- reset asserted and enable TRUE: reset still wins, din is NOT written
#guard flopNextV 8 (V 7) (some (B 1)) (some (B 1)) 5 false (some (V 99)) true false == V 5
-- reset DEasserted: enable decides as usual
#guard flopNextV 8 (V 7) (some (B 0)) (some (B 0)) 5 false (some (V 99)) true false == V 99
#guard flopNextV 8 (V 7) (some (B 1)) (some (B 0)) 5 false (some (V 99)) true false == V 7

-- ---- the bridge agrees with `flopNext` on a real environment ----------------
private def envF : SlotEnv := #[CertVal.bv (V 7), CertVal.bv (B 1), CertVal.bv (B 0)]
private def stF  : RuntimeState := { flops := #[V 99, V 11], mems := #[] }
private def fd (en rp : Option Nat) (ral : Bool) : ResidualFlopUpdate :=
  -- CALLER-ONLY: the record gained `clock` and `asyncReset`. D3's fixture is a
  -- one-domain design with a synchronous reset, which is ordinal 0 and `false`.
  { width := 8, din := 0, enable := en, resetPin := rp, resetValue := 5,
    resetActiveLow := ral, clock := 0, asyncReset := false }

/-- the one declared domain, firing -- D3's implicit contract, written out. -/
private def eF : ClockEdges := #[true]

-- enable true / false, both polarities, and a MISSING old-state index (2)
#guard flopNext envF eF stF 0 (fd none none false)        == flopNextV 8 (V 7) none none 5 false (some (V 99)) true false
#guard flopNext envF eF stF 0 (fd (some 2) none false)    == V 99
#guard flopNext envF eF stF 2 (fd (some 2) none false)    == V 0
#guard flopNext envF eF stF 0 (fd none (some 1) false)    == V 5
#guard flopNext envF eF stF 0 (fd none (some 2) true)     == V 5
#guard flopNext envF eF stF 0 (fd (some 2) (some 1) false) == V 5

#eval IO.println "D3FLOPNEXT OK"

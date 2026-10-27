/-
# Proof emitters on NON-DEFAULT clock metadata

The original D3 fixtures all sit on clock ordinal 0 with synchronous resets,
which are exactly the record's DEFAULTS. A generated `D_flops` / `D_mems` fact
that drops `clock` / `asyncReset` / memory `clock` still elaborates there, and
its `rfl` still closes -- so caller adaptation alone cannot catch the loss.

This fixture is the opposite: everything that can sit off the default does.

  * a flop on clock ordinal 1 with an ASYNCHRONOUS, active-low reset and a
    nonzero reset value;
  * a second flop on ordinal 0 with a synchronous active-high reset, so the two
    domains are genuinely independent;
  * a WRITABLE memory on ordinal 1, so commit-versus-hold is observable;
  * distinct source and output widths.

Each emitter is exercised, each generated theorem goes through `d3_proof_gate`,
and the model is compared against `denoteResidual` on quiet, independent and
simultaneous edges. The gate is the real acceptance: a failed generated proof
rests on `sorryAx`, and the gate rejects it.
-/
import LeanSemanticPrimitives.Compiler.ReifyProof

open Compiler

/-- inputs: 0 = d, 1 = en, 2 = rst_hi, 3 = rst_lo_n, 4 = waddr, 5 = wen -/
def ndCert : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.input 1 1
                 , SourceDesc.input 2 1, SourceDesc.input 3 1
                 , SourceDesc.input 4 2, SourceDesc.input 5 1
                 , SourceDesc.memImg 0 2 8 ]
    nodes    := #[ { op := LGraphOp.Op_Or, width := 8, deps := #[0, 0], origin := 1 }
                 , { op := LGraphOp.Op_MemWrite, width := 8, deps := #[6, 4, 0, 5]
                   , origin := 2 } ]
    outputs  := #[ { slot := 7, width := 4 } ]
    flops    := #[ -- NON-DEFAULT: ordinal 1, asynchronous, active low, reset 7
                   { width := 8, din := 0, enable := none, resetPin := some 3
                   , resetValue := 7, resetActiveLow := true, clock := 1
                   , asyncReset := true }
                   -- default-shaped, so the two domains differ
                 , { width := 8, din := 7, enable := some 1, resetPin := some 2
                   , resetValue := 0, resetActiveLow := false, clock := 0
                   , asyncReset := false } ]
    memories := #[ { aw := 2, dw := 8, nextImg := 8, clock := 1 } ]   -- NON-DEFAULT
    clocks   := #[ { name := "clk_a" }, { name := "clk_b" } ] }

reify_design_named ndCert as ndM
prove_reified_incr ndCert as ndM
d3_proof_gate ndM.correct

-- the generated facts must carry the metadata, not the defaults
example : ndCert.flops[0]!.clock = 1 := rfl
example : ndCert.flops[0]!.asyncReset = true := rfl
example : ndCert.memories[0]!.clock = 1 := rfl

def ndR : ResidualProgram :=
  match compileDesign ndCert with | .ok R => R | .error _ => default

def ndIn (d en rhi rlo wa wen : Nat) : RuntimeInput :=
  #[mk_bv 8 d, mk_bv 1 en, mk_bv 1 rhi, mk_bv 1 rlo, mk_bv 2 wa, mk_bv 1 wen]
def ndSt : RuntimeState := { flops := #[mk_bv 8 3, mk_bv 8 9], mems := #[fun _ => mk_bv 8 5] }

def ndSame (a b : RuntimeResult) : Bool :=
  a.outputs == b.outputs && a.nextState.flops == b.nextState.flops
    && (List.range 4).all fun k =>
         (a.nextState.mems.map (fun f => f (Int.ofNat k)))
           == (b.nextState.mems.map (fun f => f (Int.ofNat k)))

-- quiet, independent each way, simultaneous
def ndEdges : List ClockEdges := [#[false,false], #[true,false], #[false,true], #[true,true]]
def ndInputs : List RuntimeInput :=
  [ ndIn 0 0 0 0 0 0, ndIn 200 1 0 1 2 1, ndIn 200 0 0 1 2 1
  , ndIn 17 1 1 1 1 1, ndIn 17 1 0 0 3 0 ]

#guard ndEdges.all fun e => ndInputs.all fun i => ndSame (ndM e i ndSt) (denoteResidual ndR e i ndSt)

-- the ASYNC reset acts on a QUIET domain -- the arm that a defaulted
-- `asyncReset := false` would silently lose.
#guard ((ndM #[false, false] (ndIn 0 0 0 0 0 0) ndSt).nextState.flops[0]!) == mk_bv 8 7
-- ...while the SYNCHRONOUS flop on the other quiet domain simply holds.
-- (its stored value is 9, not 3 -- the first version of this line asserted the
-- OTHER flop's initial value and failed for that reason, not a semantic one.)
#guard ((ndM #[false, false] (ndIn 0 0 1 1 0 0) ndSt).nextState.flops[1]!) == mk_bv 8 9
-- the memory on ordinal 1 commits only when ordinal 1 fires
#guard ((ndM #[false, true] (ndIn 200 1 0 1 2 1) ndSt).nextState.mems[0]! 2) == mk_bv 8 200
#guard ((ndM #[true, false] (ndIn 200 1 0 1 2 1) ndSt).nextState.mems[0]! 2) == mk_bv 8 5

#eval IO.println "D3-NONDEFAULT-METADATA OK"

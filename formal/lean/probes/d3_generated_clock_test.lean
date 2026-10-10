/-
# Generated-model clock regression

Building `ReifyGen` typechecks the GENERATOR. It does not typecheck the terms
the generator writes -- those are elaborated when a design is actually reified.
Measured: with `flopNextV` clock-aware but the emission unchanged, three of the
four commands emitted an application of type `Bool → Bool → BV` where `BV` was
required, and the fourth emitted a hand-rolled update with no notion of an edge
at all.

So this runs all four commands on a real certificate and compares each
generated model against `denoteResidual` ON THE SAME edge, input and state.
A build-only or shape-only check cannot catch these failures.

The fixture carries, deliberately:
  * TWO clock domains, so quiet / independent / simultaneous edges differ;
  * a SYNCHRONOUS reset (active high) and an ASYNCHRONOUS one (active low),
    so the `edge || asyncReset` arm is exercised both ways;
  * a NONZERO reset value;
  * an ENABLE, so the hold path is reached with the clock firing;
  * a MEMORY on the second domain, so commit-vs-hold is observable;
  * distinct source and output widths.
-/
import LeanSemanticPrimitives.Compiler.ReifyGen

open Compiler

/-- inputs: 0 = din, 1 = enable, 2 = reset_hi, 3 = reset_lo_n,
            4 = write address, 5 = write enable

The MEMORY IS ACTUALLY WRITTEN. The first version of this fixture pointed the
memory's `nextImg` straight at its `memImg` SOURCE, so the commit arm and the
hold arm produced the SAME function and the comparison could not tell them
apart -- the non-vacuity guard was being satisfied by the flops alone. An
`Op_MemWrite` node now stands between them, so a committed step and a held step
differ in one cell and agree in every other. -/
def tDesign : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.input 1 1
                 , SourceDesc.input 2 1, SourceDesc.input 3 1
                 , SourceDesc.input 4 2, SourceDesc.input 5 1
                 , SourceDesc.memImg 0 2 8 ]
    nodes    := #[ { op := LGraphOp.Op_Or, width := 8, deps := #[0, 0], origin := 1 }
                 -- [mem, addr, data, enable] -> mem
                 , { op := LGraphOp.Op_MemWrite, width := 8, deps := #[6, 4, 0, 5]
                   , origin := 2 } ]
    outputs  := #[ { slot := 7, width := 4 } ]
    flops    := #[ { width := 8, din := 7, enable := some 1, resetPin := some 2
                   , resetValue := 0, resetActiveLow := false, clock := 0
                   , asyncReset := false }
                 , { width := 8, din := 0, enable := none, resetPin := some 3
                   , resetValue := 7, resetActiveLow := true, clock := 1
                   , asyncReset := true } ]
    memories := #[ { aw := 2, dw := 8, nextImg := 8, clock := 1 } ]
    clocks   := #[ { name := "clk_a" }, { name := "clk_b" } ] }

reify_design        tDesign as tDefault
reify_design_named  tDesign as tNamed
reify_design_shared tDesign as tShared
reify_design_chunked tDesign as tChunked size 1

/-- The compiled residual the generated models must agree with. -/
def tR : ResidualProgram :=
  match compileDesign tDesign with
  | .ok R    => R
  | .error _ => default

def inp (d en rhi rlo : Nat) (wa wen : Nat) : RuntimeInput :=
  #[mk_bv 8 d, mk_bv 1 en, mk_bv 1 rhi, mk_bv 1 rlo, mk_bv 2 wa, mk_bv 1 wen]

def st0 : RuntimeState :=
  { flops := #[mk_bv 8 3, mk_bv 8 9], mems := #[fun _ => mk_bv 8 5] }

/-- quiet, independent each way, and simultaneous. -/
def edgeCases : List ClockEdges :=
  [ #[false, false], #[true, false], #[false, true], #[true, true] ]

def inputCases : List RuntimeInput :=
  [ inp 0 0 0 0 0 0, inp 200 1 0 1 2 1, inp 200 0 0 1 2 1
  , inp 17 1 1 1 1 1, inp 17 1 0 0 3 0 ]

def sameRes (a b : RuntimeResult) : Bool :=
  a.outputs == b.outputs
    && a.nextState.flops == b.nextState.flops
    && (List.range 4).all fun k =>
         (a.nextState.mems.map (fun f => f (Int.ofNat k)))
           == (b.nextState.mems.map (fun f => f (Int.ofNat k)))

def agreesOn (f : ClockEdges → RuntimeInput → RuntimeState → RuntimeResult) : Bool :=
  edgeCases.all fun e => inputCases.all fun i => sameRes (f e i st0) (denoteResidual tR e i st0)

-- The four generated models, against `denoteResidual` on the same values.
#guard agreesOn tDefault
#guard agreesOn tNamed
#guard agreesOn tShared
#guard agreesOn tChunked

-- NON-VACUITY, stated per observable rather than globally.
--
-- A single whole-result guard is satisfied by the flops alone, which is how the
-- first version of this fixture claimed memory evidence it did not have.

/-- the memory image after one step, read at `a`. -/
def memAt (e : ClockEdges) (i : RuntimeInput) (a : Nat) : BV :=
  ((denoteResidual tR e i st0).nextState.mems[0]!) (Int.ofNat a)

-- the flops must respond to the edge vector...
#guard (denoteResidual tR #[false, false] (inp 200 1 0 1 2 1) st0).nextState.flops
     != (denoteResidual tR #[true, true]  (inp 200 1 0 1 2 1) st0).nextState.flops

-- ...and so must the MEMORY, at the written cell: committed vs held.
#guard memAt #[false, false] (inp 200 1 0 1 2 1) 2
    != memAt #[false, true]  (inp 200 1 0 1 2 1) 2

-- the written cell takes the written value when the domain fires...
#guard memAt #[false, true] (inp 200 1 0 1 2 1) 2 == mk_bv 8 200

-- ...and holds the previous image when it does not.
#guard memAt #[false, false] (inp 200 1 0 1 2 1) 2 == mk_bv 8 5

-- a NEIGHBOUR cell is untouched either way.
#guard memAt #[false, true]  (inp 200 1 0 1 2 1) 1 == mk_bv 8 5
#guard memAt #[false, false] (inp 200 1 0 1 2 1) 1 == mk_bv 8 5

-- and with the write enable low, even a firing domain changes nothing.
#guard memAt #[false, true] (inp 200 1 0 1 3 0) 3 == mk_bv 8 5

#eval IO.println "GENERATED-CLOCK-REGRESSION OK"

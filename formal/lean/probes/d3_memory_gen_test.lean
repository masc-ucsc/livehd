/-
# `prove_reified_incr` with WRITABLE MEMORY next-images

The walk machinery for memory already existed -- `refMem` terminal facts,
`bindAgreeMem_push`, `refMem_sourceEnv` -- so this milestone was mostly removing
two refusals and emitting the `mems` field. The one real gap is below.

THE GAP THAT WAS NOT OBVIOUS. A memory update's `nextImg` can name either a
PRODUCED slot (a write) or a SOURCE slot (a memory the design declares and never
writes, whose next image is the old one). The terminal-reference helper read
every source slot with `refBV`, which is right for a data or control operand and
wrong here: it would have emitted a `BV` field where `Int → BV` is required. A
fixture with only a produced-memory binding does not reach it.

Run: lake env lean probes/d3_memory_gen_test.lean
-/
import LeanSemanticPrimitives.Compiler.ReifyProof
set_option maxRecDepth 4000000
set_option maxHeartbeats 0
open Compiler

--------------------------------------------------------------------------------
-- 1. A WRITE: `nextImg` names a produced memory-valued binding, and a read of
--    that same slot in the same cycle (so forwarding is in the design, not just
--    in the semantics probe).
--------------------------------------------------------------------------------
def memW : DesignCert :=
  { sources := #[ SourceDesc.memImg 0 4 8, SourceDesc.input 0 4
                , SourceDesc.input 1 8, SourceDesc.input 2 1 ]
    nodes   := #[ { op := LGraphOp.Op_MemWrite, width := 8, deps := #[0,1,2,3], origin := 0 }
                , { op := LGraphOp.Op_MemRead,  width := 8, deps := #[4,1,3],   origin := 1 } ]
    outputs := #[ { slot := 5, width := 8 } ]
    flops   := #[]
    memories := #[ { aw := 4, dw := 8, nextImg := 4 } ] }
def memWR : ResidualProgram := match compileDesign memW with | .ok R => R | .error _ => default
-- the write really is memory-valued, and the update's terminal ref is a MEMORY ref
#guard memWR.bindings[0]!.ty == ValueType.mem 0 8
#guard (ReifyProof.terminalRefs memWR).contains (4, true)
#guard (ReifyProof.terminalRefs memWR).contains (4, false) == false
#guard (ReifyProof.terminalRefs memWR).contains (5, false)
reify_design_named memW as mw
prove_reified_incr memW as mw
d3_proof_gate mw.correct

-- the emitted next-image behaves as the semantics say: the written cell changes,
-- its neighbours do not, and a disabled write preserves the image
private def oldImg : Int → BV := fun x => mk_bv 8 (x * 3)
private def stM : RuntimeState := { flops := #[], mems := #[oldImg] }
private def iM (a d e : Int) : RuntimeInput := #[mk_bv 4 a, mk_bv 8 d, mk_bv 1 e]
#guard ((mw (iM 5 200 1) stM).nextState.mems[0]! 5) == mk_bv 8 200
#guard ((mw (iM 5 200 1) stM).nextState.mems[0]! 6) == oldImg 6
#guard ((mw (iM 5 200 0) stM).nextState.mems[0]! 5) == oldImg 5
-- and the same-cycle read sees the write
#guard ((mw (iM 5 200 1) stM).outputs[0]!) == mk_bv 8 200

--------------------------------------------------------------------------------
-- 2. A SOURCE-ONLY `nextImg`: the design declares a memory it never writes, so
--    the next image IS the old source image. This is the case the BV-only
--    terminal helper got wrong.
--------------------------------------------------------------------------------
def memU : DesignCert :=
  { sources := #[ SourceDesc.memImg 0 4 8, SourceDesc.input 0 4, SourceDesc.input 1 1 ]
    nodes   := #[ { op := LGraphOp.Op_MemRead, width := 8, deps := #[0,1,2], origin := 0 } ]
    outputs := #[ { slot := 3, width := 8 } ]
    flops   := #[]
    memories := #[ { aw := 4, dw := 8, nextImg := 0 } ] }
def memUR : ResidualProgram := match compileDesign memU with | .ok R => R | .error _ => default
-- `nextImg` is a SOURCE slot, and it is a MEMORY-kind terminal reference
#guard memUR.memoryUpdates[0]!.nextImg == 0
#guard (ReifyProof.terminalRefs memUR).contains (0, true)
reify_design_named memU as mu
prove_reified_incr memU as mu
d3_proof_gate mu.correct

-- `memU` has its OWN input ordinals -- address at 0, enable at 1 -- so it needs
-- its own stimulus builder. Reusing `memW`'s put an 8-bit datum where the 1-bit
-- enable is read, which disabled the read and made the output 0.
private def iU (a e : Int) : RuntimeInput := #[mk_bv 4 a, mk_bv 1 e]
-- the emitted field really is the untouched image function, not a bit vector
#guard ((mu (iU 2 1) stM).nextState.mems[0]! 5) == oldImg 5
#guard ((mu (iU 2 1) stM).nextState.mems[0]! 9) == oldImg 9
#guard ((mu (iU 2 1) stM).outputs[0]!) == oldImg 2
-- and a disabled read is zero, which confirms the enable really is ordinal 1
#guard ((mu (iU 2 0) stM).outputs[0]!) == mk_bv 8 0

#eval IO.println "D3MEMGEN OK"

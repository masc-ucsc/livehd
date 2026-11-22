/-
# tv on vc's optimized program, on one real design (round 1, Stage 6)

`reify_design_opt` / `prove_reified_opt` (`Compiler/ReifyOpt.lean`) on SoomRV's
`CacheWriteInterface` block (90 nodes, 8 flops, 6 inputs, no memory), the certificate
`pass.lean` emitted for it in the census's certificate set (2026-11-03):

    reify_design_opt  -- the optimizer at elaboration time, its certificate checked, the
                         certificate and the optimized program written as literals, the
                         straight-line model emitted from the optimized program
    prove_reified_opt -- in the kernel: compileDesign D = .ok R, validate R cert = true,
                         applyCert R cert = Ropt (decide +kernel), the model is
                         denoteResidual Ropt (simp), so the model is interpretDesign D
                         (validate_sound, compileDesign_correct); audited axioms

Then 40 steps of the compiled optimized model against `interpretDesign` from the reset
state, with a fixed pseudo-random input sequence and every third step quiet.

Run (after `lake build LeanSemanticPrimitives.Compiler.ReifyOpt`):

    cd formal/lean && lake env lean probes/d3/d3_opt_cachewrite_test.lean

Measured 2026-11-21: sources 92 -> 40, bindings 90 -> 78; the reification and the proof in
43 s at 7.3 GB peak (56 s with the 40-step check), against 71 s at 7.8 GB for
`reify_design` + `prove_reified` of the unoptimized program. The path is `prove_reified`'s single `simp`, so it is for small
designs (the kernel's arrays are lists, so the checker costs about bindings^2 too).
-/
import LeanSemanticPrimitives.Compiler.ReifyOpt

set_option maxRecDepth 1000000
set_option maxHeartbeats 0

open Compiler

def CacheWriteInterface_designCert : DesignCert :=
  {
    sources  := #[
        SourceDesc.flopQ 0 2
      , SourceDesc.flopQ 1 1
      , SourceDesc.flopQ 2 10
      , SourceDesc.flopQ 3 128
      , SourceDesc.flopQ 4 2
      , SourceDesc.flopQ 5 1
      , SourceDesc.flopQ 6 1
      , SourceDesc.flopQ 7 1
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 1 (0)
      , SourceDesc.const 1 ((-Int.ofNat 1))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 1 ((-Int.ofNat 1))
      , SourceDesc.const 1 (0)
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 1 (0)
      , SourceDesc.const 1 (0)
      , SourceDesc.const 8 ((Int.ofNat 128))
      , SourceDesc.const 9 ((Int.ofNat 128))
      , SourceDesc.const 4 ((Int.ofNat 10))
      , SourceDesc.const 2 ((Int.ofNat 3))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 10 ((Int.ofNat 1023))
      , SourceDesc.const 9 ((Int.ofNat 128))
      , SourceDesc.const 129 ((Int.ofNat 340282366920938463463374607431768211455))
      , SourceDesc.const 4 ((Int.ofNat 15))
      , SourceDesc.const 3 ((Int.ofNat 5))
      , SourceDesc.const 3 ((Int.ofNat 4))
      , SourceDesc.const 2 ((Int.ofNat 2))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 3 ((Int.ofNat 5))
      , SourceDesc.const 8 ((Int.ofNat 138))
      , SourceDesc.const 138 ((Int.ofNat 1023))
      , SourceDesc.const 2 ((Int.ofNat 2))
      , SourceDesc.const 2 ((Int.ofNat 3))
      , SourceDesc.const 3 ((Int.ofNat 4))
      , SourceDesc.const 4 ((Int.ofNat 10))
      , SourceDesc.const 10 (0)
      , SourceDesc.const 3 ((Int.ofNat 7))
      , SourceDesc.const 9 ((Int.ofNat 138))
      , SourceDesc.const 139 ((Int.ofNat 348449143727040986586495598010130648529920))
      , SourceDesc.const 8 ((Int.ofNat 128))
      , SourceDesc.const 128 (0)
      , SourceDesc.const 2 ((Int.ofNat 2))
      , SourceDesc.const 2 ((Int.ofNat 1))
      , SourceDesc.const 2 ((Int.ofNat 1))
      , SourceDesc.const 2 (0)
      , SourceDesc.const 2 (0)
      , SourceDesc.const 1 (0)
      , SourceDesc.const 1 ((-Int.ofNat 1))
      , SourceDesc.const 1 (0)
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 2 (0)
      , SourceDesc.const 2 (0)
      , SourceDesc.const 1 (0)
      , SourceDesc.const 1 ((-Int.ofNat 1))
      , SourceDesc.const 1 (0)
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 3 ((Int.ofNat 3))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 9 ((Int.ofNat 138))
      , SourceDesc.const 139 ((Int.ofNat 348449143727040986586495598010130648530943))
      , SourceDesc.const 3 ((Int.ofNat 5))
      , SourceDesc.const 3 ((Int.ofNat 4))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 8 ((Int.ofNat 143))
      , SourceDesc.const 143 (0)
      , SourceDesc.const 3 ((Int.ofNat 5))
      , SourceDesc.const 3 ((Int.ofNat 4))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 9 ((Int.ofNat 143))
      , SourceDesc.const 8 ((Int.ofNat 143))
      , SourceDesc.const 143 (0)
      , SourceDesc.const 3 ((Int.ofNat 5))
      , SourceDesc.const 4 ((Int.ofNat 10))
      , SourceDesc.const 4 ((Int.ofNat 15))
      , SourceDesc.const 2 ((Int.ofNat 2))
      , SourceDesc.const 2 ((Int.ofNat 2))
      , SourceDesc.const 3 ((Int.ofNat 4))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 1 (0)
      , SourceDesc.input 6 1
      , SourceDesc.input 4 1
      , SourceDesc.input 1 10
      , SourceDesc.input 2 128
      , SourceDesc.input 3 2
      , SourceDesc.input 0 1
      ]
    nodes    := #[
        { op := LGraphOp.Op_GetMask, width := 1, deps := #[6, 8], origin := 176 }
      , { op := LGraphOp.Op_EQ, width := 1, deps := #[9, 92], origin := 628 }
      , { op := LGraphOp.Op_And, width := 1, deps := #[87, 93], origin := 124 }
      , { op := LGraphOp.Op_MuxBool, width := 1, deps := #[94, 6, 10], origin := 344 }
      , { op := LGraphOp.Op_GetMask, width := 1, deps := #[95, 11], origin := 988 }
      , { op := LGraphOp.Op_And, width := 1, deps := #[91, 96], origin := 92 }
      , { op := LGraphOp.Op_MuxBool, width := 1, deps := #[97, 12, 13], origin := 276 }
      , { op := LGraphOp.Op_GetMask, width := 1, deps := #[7, 14], origin := 184 }
      , { op := LGraphOp.Op_Or, width := 1, deps := #[15, 99], origin := 88 }
      , { op := LGraphOp.Op_Sext, width := 1, deps := #[100, 16], origin := 1140 }
      , { op := LGraphOp.Op_MuxBool, width := 1, deps := #[94, 7, 17], origin := 300 }
      , { op := LGraphOp.Op_MuxBool, width := 1, deps := #[94, 102, 101], origin := 332 }
      , { op := LGraphOp.Op_MuxBool, width := 1, deps := #[97, 18, 103], origin := 272 }
      , { op := LGraphOp.Op_Sext, width := 128, deps := #[89, 19], origin := 1412 }
      , { op := LGraphOp.Op_Sext, width := 138, deps := #[105, 20], origin := 1000000013 }
      , { op := LGraphOp.Op_SHL, width := 138, deps := #[106, 21], origin := 1416 }
      , { op := LGraphOp.Op_Or, width := 138, deps := #[88, 107], origin := 1420 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[4, 22], origin := 160 }
      , { op := LGraphOp.Op_GetMask, width := 1, deps := #[5, 23], origin := 168 }
      , { op := LGraphOp.Op_GetMask, width := 10, deps := #[2, 24], origin := 144 }
      , { op := LGraphOp.Op_Sext, width := 129, deps := #[3, 25], origin := 1000000019 }
      , { op := LGraphOp.Op_GetMask, width := 128, deps := #[112, 26], origin := 152 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[113, 27], origin := 1473 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[111, 28], origin := 1474 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[110, 29], origin := 1475 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[109, 30], origin := 1476 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[99, 31], origin := 1477 }
      , { op := LGraphOp.Op_Or, width := 143, deps := #[114, 115, 116, 117, 118, 92], origin := 1392 }
      , { op := LGraphOp.Op_Or, width := 144, deps := #[119], origin := 1000000026 }
      , { op := LGraphOp.Op_SRA, width := 138, deps := #[120, 32], origin := 1088 }
      , { op := LGraphOp.Op_Sext, width := 138, deps := #[121, 33], origin := 1084 }
      , { op := LGraphOp.Op_MuxBool, width := 138, deps := #[94, 122, 108], origin := 308 }
      , { op := LGraphOp.Op_GetMask, width := 10, deps := #[123, 34], origin := 540 }
      , { op := LGraphOp.Op_Or, width := 144, deps := #[119], origin := 1000000030 }
      , { op := LGraphOp.Op_SRA, width := 141, deps := #[125, 35], origin := 1220 }
      , { op := LGraphOp.Op_Sext, width := 3, deps := #[126, 36], origin := 1216 }
      , { op := LGraphOp.Op_MuxBool, width := 3, deps := #[94, 127, 90], origin := 316 }
      , { op := LGraphOp.Op_GetMask, width := 1, deps := #[128, 37], origin := 324 }
      , { op := LGraphOp.Op_Sum 2, width := 11, deps := #[129, 124], origin := 536 }
      , { op := LGraphOp.Op_Sext, width := 10, deps := #[130, 38], origin := 1036 }
      , { op := LGraphOp.Op_MuxBool, width := 10, deps := #[97, 39, 131], origin := 264 }
      , { op := LGraphOp.Op_SHL, width := 8, deps := #[129, 40], origin := 1472 }
      , { op := LGraphOp.Op_Sext, width := 139, deps := #[123, 41], origin := 1000000038 }
      , { op := LGraphOp.Op_GetMask, width := 128, deps := #[134, 42], origin := 576 }
      , { op := LGraphOp.Op_Or, width := 129, deps := #[135], origin := 1000000040 }
      , { op := LGraphOp.Op_SRA, width := 128, deps := #[136, 133], origin := 556 }
      , { op := LGraphOp.Op_Sext, width := 128, deps := #[137, 43], origin := 1024 }
      , { op := LGraphOp.Op_MuxBool, width := 128, deps := #[97, 44, 138], origin := 260 }
      , { op := LGraphOp.Op_Sext, width := 2, deps := #[128, 45], origin := 844 }
      , { op := LGraphOp.Op_Sum 2, width := 2, deps := #[46, 129], origin := 596 }
      , { op := LGraphOp.Op_GetMask, width := 1, deps := #[141, 47], origin := 928 }
      , { op := LGraphOp.Op_MuxBool, width := 2, deps := #[142, 48, 140], origin := 216 }
      , { op := LGraphOp.Op_MuxBool, width := 2, deps := #[97, 49, 143], origin := 248 }
      , { op := LGraphOp.Op_MuxBool, width := 1, deps := #[142, 50, 51], origin := 224 }
      , { op := LGraphOp.Op_MuxBool, width := 1, deps := #[97, 52, 145], origin := 252 }
      , { op := LGraphOp.Op_GetMask, width := 1, deps := #[146, 53], origin := 1164 }
      , { op := LGraphOp.Op_MuxBool, width := 2, deps := #[147, 54, 144], origin := 188 }
      , { op := LGraphOp.Op_MuxBool, width := 2, deps := #[86, 148, 55], origin := 196 }
      , { op := LGraphOp.Op_MuxBool, width := 1, deps := #[147, 56, 57], origin := 268 }
      , { op := LGraphOp.Op_MuxBool, width := 1, deps := #[86, 150, 58], origin := 200 }
      , { op := LGraphOp.Op_GetMask, width := 1, deps := #[103, 59], origin := 1440 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[128, 60], origin := 320 }
      , { op := LGraphOp.Op_SHL, width := 3, deps := #[153, 61], origin := 1478 }
      , { op := LGraphOp.Op_Or, width := 3, deps := #[154, 152], origin := 1436 }
      , { op := LGraphOp.Op_Sext, width := 139, deps := #[123, 62], origin := 1000000061 }
      , { op := LGraphOp.Op_GetMask, width := 138, deps := #[156, 63], origin := 312 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[157, 64], origin := 1479 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[142, 65], origin := 1480 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[155, 66], origin := 1481 }
      , { op := LGraphOp.Op_Or, width := 143, deps := #[158, 159, 160, 96], origin := 916 }
      , { op := LGraphOp.Op_Sext, width := 143, deps := #[161, 67], origin := 920 }
      , { op := LGraphOp.Op_MuxBool, width := 143, deps := #[142, 162, 68], origin := 232 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[157, 69], origin := 1482 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[129, 70], origin := 1483 }
      , { op := LGraphOp.Op_SHL, width := 143, deps := #[155, 71], origin := 1484 }
      , { op := LGraphOp.Op_Or, width := 143, deps := #[164, 165, 166, 96], origin := 1004 }
      , { op := LGraphOp.Op_Sext, width := 144, deps := #[163, 72], origin := 1000000072 }
      , { op := LGraphOp.Op_MuxBool, width := 144, deps := #[97, 167, 168], origin := 1448 }
      , { op := LGraphOp.Op_Sext, width := 143, deps := #[169, 73], origin := 256 }
      , { op := LGraphOp.Op_MuxBool, width := 143, deps := #[86, 170, 74], origin := 192 }
      , { op := LGraphOp.Op_SRA, width := 138, deps := #[171, 75], origin := 680 }
      , { op := LGraphOp.Op_Sext, width := 10, deps := #[172, 76], origin := 676 }
      , { op := LGraphOp.Op_SRA, width := 128, deps := #[171, 77], origin := 688 }
      , { op := LGraphOp.Op_SRA, width := 141, deps := #[171, 78], origin := 696 }
      , { op := LGraphOp.Op_Sext, width := 2, deps := #[175, 79], origin := 692 }
      , { op := LGraphOp.Op_SRA, width := 139, deps := #[171, 80], origin := 704 }
      , { op := LGraphOp.Op_Sext, width := 1, deps := #[177, 81], origin := 700 }
      , { op := LGraphOp.Op_Sext, width := 1, deps := #[171, 82], origin := 708 }
      , { op := LGraphOp.Op_SRA, width := 142, deps := #[171, 83], origin := 716 }
      , { op := LGraphOp.Op_Sext, width := 1, deps := #[180, 84], origin := 712 }
      ]
    outputs  := #[
        { slot := 132, width := 10 }
      , { slot := 98, width := 1 }
      , { slot := 139, width := 128 }
      , { slot := 85, width := 1 }
      , { slot := 104, width := 1 }
      , { slot := 0, width := 2 }
      , { slot := 1, width := 1 }
      , { slot := 93, width := 1 }
      ]
    flops    := #[
        { width := 2, din := 149, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false, clock := 0, asyncReset := false }
      , { width := 1, din := 151, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false, clock := 0, asyncReset := false }
      , { width := 10, din := 173, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false, clock := 0, asyncReset := false }
      , { width := 128, din := 174, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false, clock := 0, asyncReset := false }
      , { width := 2, din := 176, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false, clock := 0, asyncReset := false }
      , { width := 1, din := 178, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false, clock := 0, asyncReset := false }
      , { width := 1, din := 179, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false, clock := 0, asyncReset := false }
      , { width := 1, din := 181, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false, clock := 0, asyncReset := false }
      ]
    memories := #[]
    clocks   := #[
        { name := "clk" }
      ]
  }


reify_design_opt CacheWriteInterface_designCert as CWI_opt
prove_reified_opt CacheWriteInterface_designCert as CWI_opt

#print axioms CWI_opt.correct

/-- 40 steps of the optimized model against the source semantics, outputs and next flops. -/
def cwiDifferential : IO Unit := do
  let D := CacheWriteInterface_designCert
  let ins : List (Nat × Nat) := D.sources.toList.filterMap fun
    | .input idx w => some (idx, w)
    | _ => none
  let nIn := ins.foldl (fun m (idx, _) => max m (idx + 1)) 0
  let mut st : RuntimeState :=
    { flops := D.flops.map (fun f => mk_bv f.width f.resetValue), mems := #[] }
  let mut seed : Nat := 0x2545F491
  let mut bad := 0
  for k in [0:40] do
    let mut i : Array BV := Array.replicate nIn (mk_bv 1 0)
    for (idx, w) in ins do
      seed := (seed * 6364136223846793005 + 1442695040888963407) % 2 ^ 64
      i := i.set! idx (mk_bv w ((seed / 2 ^ 17 : Nat) : Int))
    let e : ClockEdges := Array.replicate D.clocks.size (k % 3 != 2)
    let a := CWI_opt e i st
    let b := interpretDesign D e i st
    if a.outputs != b.outputs || a.nextState.flops != b.nextState.flops then bad := bad + 1
    st := b.nextState
  IO.println (if bad == 0 then "PASS d3_opt_cachewrite differential: 40 steps"
              else s!"FAIL d3_opt_cachewrite differential: {bad} of 40 steps differ")

#eval cwiDifferential

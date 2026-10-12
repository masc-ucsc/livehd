/-
# Memory-aware hardware dispatch, in the SAME backend

A separate module so the original memory-free `hwS` and every theorem about it
stay byte-identical while this route is being proved.  Nothing here edits
`HardwareInterpreter`; `hwMemS` is built FROM `hwS` by adding functions, and the
whole program is resolved afresh by `resolveProgram`, so no pre-resolved numeric
function id is reused.

## What this increment does, precisely

* `srcValM` answers the two memory source forms and DELEGATES every other form to
  the original `srcVal`, so the scalar cases and the SHL/SRA/Sext work behind
  them are untouched:
    - `memImg i _ _`   -> the i-th encoded image of the state's memory list, or
      the ABSENT image (`[]`, zero-width zero) when the list is shorter --
      matching `sourceValue`'s `s.mems[idx]?.getD (fun _ => mk_bv 0 0)`;
    - `memConst _ dw ct` -> first-order initialised words built from `ct` with a
      zero default, so the word count is `contents.size` and the contents are
      preserved -- matching the reference bound, which is NOT `2 ^ aw`.
* `applyOpM` dispatches the real opcodes 26 (`Op_MemRead`) and 28
  (`Op_MemWriteBE`) to the corresponding object primitives, reading operands in
  certificate dependency order and taking the node width and the payload
  (`byte_w`) from the certificate; every other opcode DELEGATES to the original
  `applyOp`.

## What is STAGED and not claimed

`main` and `mkSources` are NOT rewired here: the memory-bearing state/result
encoding and the per-memory clock-controlled commit inside `main` are the next
increment.  So this module gives the SOURCE and OPERATOR dispatch only.  No
whole-step, adequacy or support credit follows from it, `SourceSupported`,
`OpSupported` and `memFree` are untouched, and the public route still refuses
memory-bearing designs.
-/
import LeanSemanticPrimitives.Projection.HardwareInterpreter
import LeanSemanticPrimitives.Projection.MemImage

namespace Projection
namespace HwMem
open Surface Compiler Projection.Hw

@[inline] private def R (x : String) : SExp := .ref x
@[inline] private def C (f : String) (as : List SExp) : SExp := .call f as
@[inline] private def P (p : Prim) (as : List SExp) : SExp := .prim p as

/-- The absent image, as an SExp: no words, zero-width zero default. -/
private def absentImgE : SExp := .mk memImgTag [nil, P .bvMk [int 0, int 0]]

/-- `memAt(ms, i)` -- the i-th image, or the absent image when the list is
shorter.  This is `s.mems[idx]?.getD (fun _ => mk_bv 0 0)` in the object
language. -/
private def fMemAt : SFun :=
  { name := "memAt", params := ["ms", "i"], inline := true
  , body := .ite (P .isNil [R "ms"]) absentImgE
              (.ite (P .eqI [R "i", int 0])
                 (P .hd [R "ms"])
                 (C "memAt" [P .tl [R "ms"], P .subI [R "i", int 1]])) }

/-- `romWords(ct, dw)` -- the ROM's initialised words, one `BV` per content
entry.  The length is the CONTENT count, so `contents.size` is preserved. -/
private def fRomWords : SFun :=
  { name := "romWords", params := ["ct", "dw"], inline := true
  , body := .ite (P .isNil [R "ct"]) nil
              (cons (P .bvMk [R "dw", P .hd [R "ct"]])
                    (C "romWords" [P .tl [R "ct"], R "dw"])) }

/-- `srcValM(sd, inp, fq, ms)` -- the two memory source forms, everything else
delegated to the original `srcVal`. -/
private def fSrcValM : SFun :=
  { name := "srcValM", params := ["sd", "inp", "fq", "ms"], inline := true
  , body :=
      .switch (R "sd")
        [ (tagSrcMemImg, ["i", "aw", "dw"], C "memAt" [R "ms", R "i"])
        , (tagSrcMemConst, ["aw", "dw", "ct"],
            .mk memImgTag [C "romWords" [R "ct", R "dw"], P .bvMk [R "dw", int 0]])
        , (tagSrcInput, ["idx", "w"],  C "srcVal" [R "sd", R "inp", R "fq"])
        , (tagSrcConst, ["w", "v"],    C "srcVal" [R "sd", R "inp", R "fq"])
        , (tagSrcFlopQ, ["idx", "w"],  C "srcVal" [R "sd", R "inp", R "fq"])
        , (tagSrcFlopQA, ["idx", "w", "ri", "rv", "al"],
                                       C "srcVal" [R "sd", R "inp", R "fq"]) ] }

/-- `applyOpM(op, w, deps, env, n)` -- opcodes 26 and 28 to the memory
primitives, in certificate dependency order, with the node width and the
payload; everything else delegated to the original `applyOp`. -/
private def fApplyOpM : SFun :=
  { name := "applyOpM", params := ["op", "w", "deps", "env", "n"], inline := true
  , body := .switch (R "op")
      [(tagOp, ["code", "pay"],
        .ite (P .eqI [R "code", int 26])
          -- Op_MemRead [mem, addr, enable] -> bv
          (P .memRead
            [ R "w"
            , C "slot" [R "env", R "n", P .hd [R "deps"]]
            , C "slot" [R "env", R "n", P .hd [P .tl [R "deps"]]]
            , C "slot" [R "env", R "n", P .hd [P .tl [P .tl [R "deps"]]]] ])
        (.ite (P .eqI [R "code", int 28])
          -- Op_MemWriteBE byte_w [mem, addr, data, byte_enable] -> mem
          (P .memWriteBE
            [ R "w"
            , C "slot" [R "env", R "n", P .hd [R "deps"]]
            , C "slot" [R "env", R "n", P .hd [P .tl [R "deps"]]]
            , C "slot" [R "env", R "n", P .hd [P .tl [P .tl [R "deps"]]]]
            , C "slot" [R "env", R "n", P .hd [P .tl [P .tl [P .tl [R "deps"]]]]]
            , R "pay" ])
          (C "applyOp" [R "op", R "w", R "deps", R "env", R "n"])))] }

/-! ## The whole-cycle caller layer

`mkSourcesM` calls `srcValM` and threads the memory-image list; `evalNodesM`
calls `applyOpM`; `memNexts` builds the per-memory next state, each image
committed on ITS OWN clock -- `m.nextImg`'s slot when `firesAt edges m.clock`,
the EXACT prior image otherwise.  `mainM` is the new entry, consuming
`encDesign`, `encEdges`, `encInput` and the full `StateM` encoding, and producing
the memory-bearing result.

The scalar and flop helpers (`lenL`, `nthD`, `slot`, `nz`, `mkOutputs`,
`flopNexts`, `firesAt`, `capture`, every operator case) are REUSED unchanged, so
the SHL/SRA/Sext work and the clock/reset/async logic behind them are the same
code the memory-free route runs.

SLOT/VALUE KINDS.  A memory slot carries an encoded IMAGE, a scalar slot carries
a `BV`.  `mkOutputs` coerces its slot with the scalar reading, so an output whose
slot names a MEMORY would be mis-read.  That is not silently assumed away: it is
an explicit shape premise on admitted designs, recorded in the obligations, and
no design is admitted yet. -/

private def fMkSourcesM : SFun :=
  { name := "mkSourcesM", params := ["srcs", "inp", "fq", "ms", "acc"], inline := true
  , body := .ite (P .isNil [R "srcs"]) (R "acc")
              (C "mkSourcesM"
                 [ P .tl [R "srcs"], R "inp", R "fq", R "ms"
                 , cons (C "srcValM" [P .hd [R "srcs"], R "inp", R "fq", R "ms"])
                        (R "acc") ]) }

private def fEvalNodeM : SFun :=
  { name := "evalNodeM", params := ["nd", "env", "n"], inline := true
  , body := .switch (R "nd")
              [(tagNode, ["op", "w", "deps", "org"],
                C "applyOpM" [R "op", R "w", R "deps", R "env", R "n"])] }

private def fEvalNodesM : SFun :=
  { name := "evalNodesM", params := ["nodes", "env", "n"], inline := true
  , body := .ite (P .isNil [R "nodes"]) (R "env")
              (C "evalNodesM"
                 [ P .tl [R "nodes"]
                 , cons (C "evalNodeM" [P .hd [R "nodes"], R "env", R "n"]) (R "env")
                 , P .addI [R "n", int 1] ]) }

/-- One memory's next image: `m.nextImg`'s slot when its own clock fires, the
exact prior image when quiet. -/
private def fMemNext : SFun :=
  { name := "memNext", params := ["m", "edges", "env", "n", "ms", "idx"], inline := true
  , body := .switch (R "m")
              [(tagMemory, ["aw", "dw", "nxt", "ck"],
                .ite (C "firesAt" [R "edges", R "ck"])
                     (C "slot" [R "env", R "n", R "nxt"])
                     (C "memAt" [R "ms", R "idx"]))] }

private def fMemNexts : SFun :=
  { name := "memNexts", params := ["mems", "edges", "env", "n", "ms", "idx"], inline := true
  , body := .ite (P .isNil [R "mems"]) nil
              (cons (C "memNext"
                       [P .hd [R "mems"], R "edges", R "env", R "n", R "ms", R "idx"])
                    (C "memNexts"
                       [P .tl [R "mems"], R "edges", R "env", R "n", R "ms",
                        P .addI [R "idx", int 1]])) }

/-- The memory-aware whole cycle. -/
private def fMainM : SFun :=
  { name := "mainM", params := ["d", "edges", "inp", "st"], inline := false
  , body :=
      .switch (R "d")
        [(tagDesign, ["srcs", "nodes", "outs", "flops", "mems", "clks"],
          .switch (R "st")
            [(StateM.tagStateM, ["fq", "ms"],
              lets
                [ ("nsrc", C "lenL" [R "srcs"])
                , ("nnod", C "lenL" [R "nodes"])
                , ("nall", P .addI [R "nsrc", R "nnod"])
                , ("env0", C "mkSourcesM" [R "srcs", R "inp", R "fq", R "ms", nil])
                , ("env",  C "evalNodesM" [R "nodes", R "env0", R "nsrc"])
                , ("os",   C "mkOutputs" [R "outs", R "env", R "nall"])
                , ("nf",   C "flopNexts"
                             [R "flops", R "edges", R "env", R "nall", R "fq", int 0])
                , ("nm",   C "memNexts"
                             [R "mems", R "edges", R "env", R "nall", R "ms", int 0])
                ]
                (.mk StateM.tagResultM
                   [.mk StateM.tagStateM [R "nf", R "nm"], R "os"]))])] }

/-- The memory-aware program: the original `hwS` with the new functions ADDED.  No
existing function is edited, so the memory-free route is literally the same
code, and `resolveProgram` assigns every id afresh. -/
def hwMemS : SProgram :=
  { funs := hwS.funs ++ [fMemAt, fRomWords, fSrcValM, fApplyOpM,
                         fMkSourcesM, fEvalNodeM, fEvalNodesM, fMemNext, fMemNexts, fMainM]
  , entry := "mainM" }

def hwMemResolved : Except String (Program × List Bool) := resolveProgram hwMemS

def hwMemP : Program := match hwMemResolved with | .ok (p, _) => p | .error _ => ⟨[], 0⟩

/-- The resolved index of a function, by name -- used to call the new dispatch
entries directly.  `none` would mean the program failed to resolve. -/
def idOf (n : String) : Option Nat := Surface.idxOf (hwMemS.funs.map SFun.name) n

end HwMem
end Projection

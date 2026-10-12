/-
Executions of the ACTUAL memory-aware hardware dispatch.

Every check below runs `evalFuel` on `hwMemP` -- the resolved hardware Program --
calling `srcValM` / `applyOpM` at the ids `resolveProgram` assigned them.  None of
it calls `evalPrim` directly, and none of it is a six-function toy: the callee is
the real dispatch, which delegates its non-memory cases to the original `srcVal`
and `applyOp` inside the same program.
-/
import LeanSemanticPrimitives.Projection.HardwareMem
open Projection Projection.HwMem Projection.MemImg Compiler

private def img0 : Val := MemImg.encMemImg (MemImg.zeroImg 5 32)
private def romImg : Val := MemImg.encMemImg (MemImg.ofMemConst 32 #[11, 22, 33])

private def srcValMId  : Nat := (idOf "srcValM").getD 999
private def applyOpMId : Nat := (idOf "applyOpM").getD 999
private def F : Nat := 40000

/-- call `srcValM(sd, inp, fq, ms)` through the resolved program -/
private def runSrc (sd : Val) (ms : Val) : EvalResult :=
  evalFuel F hwMemP [] (.call srcValMId [.lit sd, .lit .nil, .lit .nil, .lit ms])

/-- call `applyOpM(op, w, deps, env, n)` through the resolved program -/
private def runOp (op : Val) (w : Int) (deps env : Val) (n : Int) : EvalResult :=
  evalFuel F hwMemP [] (.call applyOpMId [.lit op, .lit (.int w), .lit deps, .lit env, .lit (.int n)])

/- the program resolves, and both dispatch entries exist -/
#guard (idOf "srcValM").isSome && (idOf "applyOpM").isSome
#guard (match hwMemResolved with | .ok _ => true | .error _ => false)

/-! ### Source dispatch -/

/- memImg 0: the state's image -/
#guard (match runSrc (.ctor tagSrcMemImg [.int 0, .int 5, .int 32]) (.cons img0 .nil) with
        | .value v => v == img0 | _ => false)

/- memImg 3 against a one-image list: the ABSENT image, exactly -/
#guard (match runSrc (.ctor tagSrcMemImg [.int 3, .int 5, .int 32]) (.cons img0 .nil) with
        | .value v => v == .ctor memImgTag [.nil, ofBV (mk_bv 0 0)] | _ => false)

/- memConst: initialised words, contents.size long, default 0 -/
#guard (match runSrc (.ctor tagSrcMemConst
            [.int 5, .int 32, .cons (.int 11) (.cons (.int 22) (.cons (.int 33) .nil))]) .nil with
        | .value v => v == romImg | _ => false)

/-! ### Operator dispatch.  env is NEWEST-FIRST; slot s is read at n-1-s. -/

/- env with slots 0..3 = [image, addr, data, byte_enable], so newest-first the
   list is [be, data, addr, image] and n = 4. -/
private def envOf (img a d be : Val) : Val := .cons be (.cons d (.cons a (.cons img .nil)))
private def deps4 : Val := .cons (.int 0) (.cons (.int 1) (.cons (.int 2) (.cons (.int 3) .nil)))
private def deps3 : Val := .cons (.int 0) (.cons (.int 1) (.cons (.int 2) .nil))

/- Op_MemRead (code 26) on a ROM: the initialised word -/
#guard (match runOp (.ctor tagOp [.int 26, .nil]) 32 deps3
          (.cons (ofBV (mk_bv 1 1)) (.cons (ofBV (mk_bv 5 2)) (.cons romImg .nil))) 3 with
        | .value v => v == ofBV (mk_bv 32 33) | _ => false)

/- a DISABLED read is zero -/
#guard (match runOp (.ctor tagOp [.int 26, .nil]) 32 deps3
          (.cons (ofBV (mk_bv 1 0)) (.cons (ofBV (mk_bv 5 2)) (.cons romImg .nil))) 3 with
        | .value v => v == ofBV (mk_bv 32 0) | _ => false)

/- Op_MemWriteBE with payload byte_w = 1 (PicoRV32's shape): mask 8 is bit 3
   only, so the stored word is exactly 8 -- then read it back through the same
   operator dispatch. -/
#guard (match runOp (.ctor tagOp [.int 28, .int 1]) 32 deps4
          (envOf img0 (ofBV (mk_bv 5 7)) (ofBV (mk_bv 32 0xFFFFFFFF)) (ofBV (mk_bv 32 8))) 4 with
        | .value img' =>
            (match runOp (.ctor tagOp [.int 26, .nil]) 32 deps3
               (.cons (ofBV (mk_bv 1 1)) (.cons (ofBV (mk_bv 5 7)) (.cons img' .nil))) 3 with
             | .value v => v == ofBV (mk_bv 32 8) | _ => false)
        | _ => false)

/- byte_w = 8: mask 1 is byte lane 0, so 0x000000FF -/
#guard (match runOp (.ctor tagOp [.int 28, .int 8]) 32 deps4
          (envOf img0 (ofBV (mk_bv 5 7)) (ofBV (mk_bv 32 0xFFFFFFFF)) (ofBV (mk_bv 4 1))) 4 with
        | .value img' =>
            (match runOp (.ctor tagOp [.int 26, .nil]) 32 deps3
               (.cons (ofBV (mk_bv 1 1)) (.cons (ofBV (mk_bv 5 7)) (.cons img' .nil))) 3 with
             | .value v => v == ofBV (mk_bv 32 0x000000FF) | _ => false)
        | _ => false)

/- an ENABLED OUT-OF-RANGE write is refused inside the dispatch: the primitive
   errors, so the interpreter answers a typeError rather than a value. -/
#guard (match runOp (.ctor tagOp [.int 28, .int 8]) 32 deps4
          (envOf img0 (ofBV (mk_bv 8 255)) (ofBV (mk_bv 32 0xDEADBEEF)) (ofBV (mk_bv 4 0xF))) 4 with
        | .typeError _ => true | _ => false)

/- a MALFORMED image is refused the same way -/
#guard (match runOp (.ctor tagOp [.int 26, .nil]) 32 deps3
          (.cons (ofBV (mk_bv 1 1)) (.cons (ofBV (mk_bv 5 0))
            (.cons (.ctor memImgTag [.cons (.int 5) .nil, ofBV (mk_bv 32 7)]) .nil))) 3 with
        | .typeError _ => true | _ => false)

/- DELEGATION still works: a non-memory source goes to the original `srcVal`. -/
#guard (match runSrc (.ctor tagSrcConst [.int 8, .int 5]) .nil with
        | .value v => v == ofBV (mk_bv 8 5) | _ => false)

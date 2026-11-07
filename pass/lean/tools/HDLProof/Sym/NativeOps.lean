import HDLProof.Sym.Step
import LeanSemanticPrimitives.Compiler.CompileDesignDefs

/-! Symbolic operations used by the direct and compiled traversals. This module
contains executable definitions only; admission to a proof route also requires
the step soundness witness. Operand widths remain attached to their expressions.
-/

open Projection Compiler Std.Tactic.BVDecide
namespace HDLProof.Sym.Native

def widthVal (w : Nat) : SVal := .conc (.int w)

def constVal (w : Nat) (v : Int) : SVal := .conc (ofBV (mk_bv w v))

def resize (w : Nat) (x : SVal) : Option SVal := symBvResize [widthVal w, x]

def select (w : Nat) (c a b : SVal) : Option SVal := do
  let ⟨_, C⟩ ← asSBV c
  let ⟨_, A⟩ ← asSBV a
  let ⟨_, B⟩ ← asSBV b
  pure (.bv w (muxE (nzBit C) (zextE w A) (zextE w B)))

def foldBits (op : BVBinOp) (w : Nat) (init : SVal) (xs : List SVal) : Option SVal :=
  xs.foldlM (fun a b => symBvBitwise op [widthVal w, a, b]) init

def sum (w nAdd : Nat) (xs : List SVal) : Option SVal := do
  let mut result : BVExpr w := .const 0#w
  for (x, k) in xs.zipIdx do
    let ⟨_, X⟩ ← asSBV x
    result := if k < nAdd then .bin result .add (zextE w X)
      else .bin result .add (.bin (.un .not (zextE w X)) .add (.const 1#w))
  pure (.bv w result)

def product (w : Nat) (xs : List SVal) : Option SVal := do
  let mut result : BVExpr w := .const 1#w
  for x in xs do
    let ⟨_, X⟩ ← asSBV x
    result := .bin result .mul (zextE w X)
  pure (.bv w result)

def neg {w : Nat} (a : BVExpr w) : BVExpr w :=
  .bin (.un .not a) .add (.const 1#w)

/-- Divide at a width large enough to preserve both operands. The reference
returns zero for division by zero, unlike BitVec's unsigned division. -/
def divide (signed : Bool) (w : Nat) (a b : SVal) : Option SVal := do
  let ⟨wa, A⟩ ← asSBV a
  let ⟨wb, B⟩ ← asSBV b
  let n := max wa wb + 1
  let x := if signed then sextE n A else zextE n A
  let y := if signed then sextE n B else zextE n B
  let sx : BVExpr 1 := .extract (n - 1) 1 x
  let sy : BVExpr 1 := .extract (n - 1) 1 y
  let ax := muxE sx (neg x) x
  let ay := muxE sy (neg y) y
  let q := BVExpr.bin ax .udiv ay
  let value := muxE (.bin sx .xor sy) (neg q) q
  let resized := if signed then sextE w value else zextE w value
  pure (.bv w (muxE (nzBit B) resized (.const 0#w)))

def compare (signed greater : Bool) (w : Nat) (a b : SVal) : Option SVal := do
  let ⟨_, A⟩ ← asSBV a
  let ⟨_, B⟩ ← asSBV b
  let x := if signed then SInt.ofSint A else SInt.ofUint A
  let y := if signed then SInt.ofSint B else SInt.ofUint B
  pure (.bv w (zextE w (if greater then SInt.lt y x else SInt.lt x y)))

def eqAll (w : Nat) (xs : List SVal) : Option SVal := do
  match xs with
  | [] => pure (constVal w 1)
  | a :: rest =>
    let ⟨_, A⟩ ← asSBV a
    let mut result := trueE
    for b in rest do
      let ⟨_, B⟩ ← asSBV b
      result := andE result (SInt.eq (SInt.ofUint A) (SInt.ofUint B))
    pure (.bv w (zextE w result))

def redOr (w : Nat) (xs : List SVal) : Option SVal := do
  let mut result := falseE
  for a in xs do
    let ⟨_, A⟩ ← asSBV a
    result := orE result (nzBit A)
  pure (.bv w (zextE w result))

def muxN (w : Nat) (sel : SVal) (xs : List SVal) : Option SVal := do
  let ⟨_, S⟩ ← asSBV sel
  let mut result : BVExpr w := .const 0#w
  for (x, k) in xs.zipIdx do
    let ⟨_, X⟩ ← asSBV x
    result := muxE (SInt.eq (SInt.ofUint S) (SInt.lit k)) (zextE w X) result
  pure (.bv w result)

def sext (w : Nat) (a amount : SVal) : Option SVal := do
  let ⟨wa, A⟩ ← asSBV a
  let ⟨_, N⟩ ← asSBV amount
  -- Above the operand width the sign bit is zero. Enumerating only the
  -- operand's finite width avoids allocating 2^amount for a symbolic amount.
  let mut result := zextE w A
  for n in List.range (wa + 1) do
    let value := if n == 0 then (.const 0#w) else sextE w (zextE n A)
    result := muxE (SInt.eq (SInt.ofUint N) (SInt.lit n)) value result
  pure (.bv w result)

def setMask (w : Nat) (a mask value : SVal) : Option SVal := do
  let ⟨wa, A⟩ ← asSBV a
  let ⟨_, V⟩ ← asSBV value
  let .conc mv := mask | none
  let m ← asBV mv
  let mut result := A
  for (dst, src) in (mask_indices_bv m).zipIdx do
    if dst < wa then
      let bit : BVExpr 1 := .extract src 1 V
      let one : BVExpr wa := .const (BitVec.ofNat wa (2 ^ dst))
      result := .bin (.bin result .and (.un .not one)) .or
        (muxE bit one (.const 0#wa))
  pure (.bv w (zextE w result))

def memLoad (w n : Nat) (image bus enable : SVal) : Option SVal := do
  let (words, dflt) ← imgParts image
  let ⟨_, B⟩ ← asSBV bus
  let ⟨_, E⟩ ← asSBV enable
  let values ← wordsAt w words
  if n > words.length then none else do
    let result := values.mapIdx fun k old =>
      let slice : BVExpr w := .extract (k * w) w B
      SVal.bv w (if k < n then muxE (nzBit E) slice old else old)
    pure (.ctor memImgTag [sListOf result, dflt])

def memToBus (w n : Nat) (image : SVal) : Option SVal := do
  let (words, dflt) ← imgParts image
  let dw := w / n
  let mut result : BVExpr w := .const 0#w
  for k in List.range n do
    let ⟨_, X⟩ ← asSBV (words[k]?.getD dflt)
    result := .bin result .or
      (.shiftLeft (zextE w (zextE dw X)) (.const (BitVec.ofNat w (k * dw))))
  pure (.bv w result)

/-- Independent IR dispatch. Unsupported shapes refuse rather than taking the
total reference semantics' malformed-operand fallback. -/
def evalOp (op : LGraphOp) (w : Nat) (xs : List SVal) : Option SVal :=
  match op, xs with
  | .Op_Const c, [] => some (constVal w c)
  | .Op_Sum n, _ => sum w n xs
  | .Op_Mult, _ => product w xs
  | .Op_UDiv, [a, b] => divide false w a b
  | .Op_SDiv, [a, b] => divide true w a b
  | .Op_And, [] => some (constVal w 0)
  | .Op_And, a :: rest => resize w a >>= fun x => foldBits .and w x rest
  | .Op_Or, _ => foldBits .or w (constVal w 0) xs
  | .Op_Xor, _ => foldBits .xor w (constVal w 0) xs
  | .Op_Ror, _ => redOr w xs
  | .Op_EQ, _ => eqAll w xs
  | .Op_Not, [a] => symBvNot [widthVal w, a]
  | .Op_ULT, [a, b] => compare false false w a b
  | .Op_UGT, [a, b] => compare false true w a b
  | .Op_SLT, [a, b] => compare true false w a b
  | .Op_SGT, [a, b] => compare true true w a b
  | .Op_SHL, [] => some (constVal w 0)
  | .Op_SHL, a :: shifts => do
    let shifted ← shifts.mapM fun b => symBvShl [widthVal w, a, b]
    foldBits .xor w (constVal w 0) shifted
  | .Op_SRA, [a, b] => symBvSra [widthVal w, a, b]
  | .Op_Sext, [a, n] => sext w a n
  | .Op_GetMask, [a, m] => symBvGetMask [widthVal w, a, m]
  | .Op_SetMask, [a, m, v] => setMask w a m v
  | .Op_MuxBool, [sel, fv, tv] => select w sel tv fv
  | .Op_MuxN, sel :: vals => muxN w sel vals
  | .Op_MemRead, [m, a, en] => symMemRead [widthVal w, m, a, en]
  | .Op_MemWrite, [m, a, d, en] => symMemWrite [m, a, d, en]
  | .Op_MemWriteBE bw, [m, a, d, be] => symMemWriteBE [widthVal w, m, a, d, be, widthVal bw]
  | .Op_MemLoad n, [m, bus, en] => memLoad w n m bus en
  | .Op_MemToBus n, [m] => memToBus w n m
  | _, _ => none

/-- Compiled bindings select operands through their own constructors, not
through the source certificate or compileOp. Arithmetic helpers are shared. -/
def evalExpr (env : Array SVal) : ResidualExpr → Option SVal
  | .rsum w n args => args.toList.mapM (env[·]?) >>= sum w n
  | .rmult w args => args.toList.mapM (env[·]?) >>= product w
  | .rand w args => args.toList.mapM (env[·]?) >>= evalOp .Op_And w
  | .rorBits w args => args.toList.mapM (env[·]?) >>= foldBits .or w (constVal w 0)
  | .rxor w args => args.toList.mapM (env[·]?) >>= foldBits .xor w (constVal w 0)
  | .rredOr w args => args.toList.mapM (env[·]?) >>= redOr w
  | .req w args => args.toList.mapM (env[·]?) >>= eqAll w
  | .rshl w args => args.toList.mapM (env[·]?) >>= evalOp .Op_SHL w
  | .rmuxN w args => args.toList.mapM (env[·]?) >>= evalOp .Op_MuxN w
  | .rnot w a => env[a]? >>= fun x => symBvNot [widthVal w, x]
  | .rult w a b => do compare false false w (← env[a]?) (← env[b]?)
  | .rugt w a b => do compare false true w (← env[a]?) (← env[b]?)
  | .rslt w a b => do compare true false w (← env[a]?) (← env[b]?)
  | .rsgt w a b => do compare true true w (← env[a]?) (← env[b]?)
  | .rsra w a b => do symBvSra [widthVal w, ← env[a]?, ← env[b]?]
  | .rsext w a n => do sext w (← env[a]?) (← env[n]?)
  | .rgetMask w a m => do symBvGetMask [widthVal w, ← env[a]?, ← env[m]?]
  | .rsetMask w a m v => do setMask w (← env[a]?) (← env[m]?) (← env[v]?)
  | .rmux w sel fv tv => do select w (← env[sel]?) (← env[tv]?) (← env[fv]?)
  | .rmemRead w m a en => do symMemRead [widthVal w, ← env[m]?, ← env[a]?, ← env[en]?]
  | .rmemWrite m a d en => do symMemWrite [← env[m]?, ← env[a]?, ← env[d]?, ← env[en]?]
  | .rmemWriteBE w bw m a d be => do
      symMemWriteBE [widthVal w, ← env[m]?, ← env[a]?, ← env[d]?, ← env[be]?, widthVal bw]
  | .rmemLoad w n m bus en => do memLoad w n (← env[m]?) (← env[bus]?) (← env[en]?)
  | .rmemToBus w n m => do memToBus w n (← env[m]?)
  | .rudiv w a b => do divide false w (← env[a]?) (← env[b]?)
  | .rsdiv w a b => do divide true w (← env[a]?) (← env[b]?)

end HDLProof.Sym.Native

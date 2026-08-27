/-
# `denoteExpr` / `denoteResidual` — the TARGET semantics

Step 4, second half.  This file contains **no** `NodeSemantics.interpOp`, no
`eval_op`, and no `GraphCert` traversal.  Grep it: those names do not appear.

## How independent, exactly?

Independence has degrees, and being honest about which one this is matters more
than claiming the strongest.

The bug class that actually occurred on the manual path (the thirteen in
`STEP5_BRIDGE_BUGS.md`) is **operand order, arity splits, side conditions,
widths, and memory forwarding** — not "the fold associated the wrong way".  So
these definitions are *structurally* independent at the operand-mapping level:
separate type, separate dispatch, every operand position written out.  They
share `BV`'s low-level modular arithmetic (`mk_bv`, `bv_uint`, `bv_bit`,
`bits_to_int`), which the plan explicitly permits.

Per-operator, the resulting bridge obligation is one of:

  * **real content** — the two spellings differ structurally and the proof does
    work: `rsum` (explicit `foldl` over `bv_uint` vs `List.sum` after take/drop),
    `rredOr` / `req` (explicit recursion vs `List.any` / `List.all`),
    `rsext` (bit formula `Y[i] = a[min i (n-1)]` vs modular arithmetic — this is
    the general Sext lemma the fast model cannot express at all),
    `rmemWrite` / `rmemWriteBE` (enable test inside the lambda vs outside, so the
    proof is a genuine `funext` plus case split).
  * **mapping only** — the bodies coincide once unfolded, so the lemma checks
    exactly the operand mapping and nothing else: `rmux`, `rult`, `rsra`,
    `rgetMask`, `rand`, `rorBits`, `rxor`, `rshl`, `rmuxN`.

That second group is where a bit-level redefinition would buy more validation at
substantially more proof cost, against a bug class that has not occurred.  It is
recorded as follow-up work, not claimed as done.
-/
import LeanSemanticPrimitives.Compiler.ResidualIR

namespace Compiler
namespace Residual

--------------------------------------------------------------------------------
-- Target-language primitives.  Independent spellings.
--------------------------------------------------------------------------------

/-- `Op_Sum nAdd`: first `nAdd` operands added, the rest subtracted. -/
def rsumV (w nAdd : Nat) (vs : List BV) : BV :=
  let plus  := (vs.take nAdd).foldl (fun acc x => acc + bv_uint x) (0 : Int)
  let minus := (vs.drop nAdd).foldl (fun acc x => acc + bv_uint x) (0 : Int)
  mk_bv w (plus - minus)

def rmultV (w : Nat) (vs : List BV) : BV :=
  mk_bv w (vs.foldl (fun acc x => acc * bv_uint x) (1 : Int))

def randV (w : Nat) : List BV → BV
  | []        => mk_bv w 0
  | a :: rest => rest.foldl (fun acc b => bv_bitwise w (fun x y => x && y) acc b) (bv_resize w a)

def rorBitsV (w : Nat) (vs : List BV) : BV :=
  vs.foldl (fun acc b => bv_bitwise w (fun x y => x || y) acc b) (mk_bv w 0)

def rxorV (w : Nat) (vs : List BV) : BV :=
  vs.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc b) (mk_bv w 0)

/-- Reduction OR.  Explicit recursion, not `List.any`. -/
def rredOrV (w : Nat) : List BV → BV
  | []        => mk_bv w 0
  | a :: rest => if bv_nonzero a then mk_bv w 1 else rredOrV w rest

/-- Equality reduction.  Explicit recursion against the head, not `List.all`. -/
def reqTail (a : BV) : List BV → Bool
  | []        => true
  | b :: rest => (bv_uint b == bv_uint a) && reqTail a rest

def reqV (w : Nat) : List BV → BV
  | []        => mk_bv w 1
  | a :: rest => mk_bv w (if reqTail a rest then 1 else 0)

/-- `Op_SHL`: XOR-fold of `a * 2^b` over the shift operands. -/
def rshlV (w : Nat) : List BV → BV
  | []       => mk_bv w 0
  | a :: bs  => bs.foldl (fun acc b =>
      bv_bitwise w (fun x y => xor x y) acc
        (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat))) (mk_bv w 0)

def rnotV (w : Nat) (a : BV) : BV := mk_bv w (bits_to_int w fun i => ! bv_bit a i)

def rultV (w : Nat) (a b : BV) : BV := mk_bv w (if bv_uint a < bv_uint b then 1 else 0)
def rugtV (w : Nat) (a b : BV) : BV := mk_bv w (if bv_uint b < bv_uint a then 1 else 0)
def rsltV (w : Nat) (a b : BV) : BV := mk_bv w (if bv_sint a < bv_sint b then 1 else 0)
def rsgtV (w : Nat) (a b : BV) : BV := mk_bv w (if bv_sint b < bv_sint a then 1 else 0)

def rsraV (w : Nat) (a b : BV) : BV := bv_sra w a b

/-- **General** sign extension, stated at bit level: keep `n = amt` bits, with
the sign bit at `n-1`, so `Y[i] = a[min i (n-1)]` and `n = 0 ⇒ Y = 0`.

This is the shape the manual fast model cannot express — `pass_lean.cpp` emits
`bv_sext`, which extends from the OPERAND's own width and so ignores `amt`
entirely, covering only `amt = wa` and `amt = w ∧ w ≤ wa`.  Grounding also found
the official doc misleading here (*"sign-extend from bit position b"* would give
`Sext(0b1000,4) = +8`; the measured answer is `−8`, and `lgyosys_tolg.cpp` wires
pin `b` to `create_integer(width)`).  `b` is a WIDTH.

Spelled as *"truncate `a` to `n` bits, then read the result as signed"* — an
independent and frankly clearer decomposition than the source's explicit
`u < 2^(n-1)` arithmetic, and one whose bridge lemma is a short real proof
rather than a bit-blast.  (The strictly stronger bit-level characterisation
`Y[i] = a[min i (n-1)]` would validate more; it is follow-up work, not done.) -/
def rsextV (w : Nat) (a amt : BV) : BV :=
  let n := (bv_uint amt).toNat
  if n = 0 then mk_bv w 0
  else mk_bv w (bv_sint (mk_bv n (bv_uint a)))

def rgetMaskV (w : Nat) (a m : BV) : BV := bv_get_mask w a m

/-- Deps are `[sel, falseVal, trueVal]`.  Swapping the last two here is exactly
what `compile_mux_correct` refuses to prove. -/
def rmuxV (w : Nat) (sel fv tv : BV) : BV :=
  if bv_nonzero sel then bv_resize w tv else bv_resize w fv

def rmuxNV (w : Nat) : List BV → BV
  | []          => mk_bv w 0
  | sel :: args =>
      let idx := (bv_uint sel).toNat
      match args[idx]? with
      | none   => mk_bv w 0
      | some v => bv_resize w v

--------------------------------------------------------------------------------
-- Memory.  The enable test sits INSIDE the lambda here and OUTSIDE it in
-- `cert_mem_write`, so each bridge is a real `funext` plus a case split.
--------------------------------------------------------------------------------

def rmemReadV (w : Nat) (m : Int → BV) (a en : BV) : BV :=
  if bv_uint en = 0 then mk_bv w 0 else bv_resize w (m (bv_uint a))

def rmemWriteV (m : Int → BV) (a d en : BV) : Int → BV :=
  fun x =>
    if bv_uint en = 0 then m x
    else if x = bv_uint a then d else m x

def rMaskedUpdateV (w : Nat) (old new bev : BV) (byteW : Nat) : BV :=
  mk_bv w (bits_to_int w fun i => if bv_bit bev (i / byteW) then bv_bit new i else bv_bit old i)

def rmemWriteBEV (w : Nat) (m : Int → BV) (a d bev : BV) (byteW : Nat) : Int → BV :=
  fun x =>
    if bv_uint bev = 0 then m x
    else if x = bv_uint a then rMaskedUpdateV w (m (bv_uint a)) d bev byteW else m x

end Residual

--------------------------------------------------------------------------------
-- `denoteExpr`
--------------------------------------------------------------------------------

open Residual

/-- Read a ref as a bit vector. -/
@[inline] def refBV (env : SlotEnv) (r : ResidualRef) : BV := (denoteRef env r).asBV
/-- Read a ref as a memory image. -/
@[inline] def refMem (env : SlotEnv) (r : ResidualRef) : Int → BV := (denoteRef env r).asMem

@[inline] def refBVs (env : SlotEnv) (rs : Array ResidualRef) : List BV :=
  rs.toList.map (refBV env)

/-- The target semantics.  Dispatches on `ResidualExpr`'s own constructors. -/
def denoteExpr (env : SlotEnv) : ResidualExpr → CertVal
  | .rsum w nAdd args     => .bv (rsumV w nAdd (refBVs env args))
  | .rmult w args         => .bv (rmultV w (refBVs env args))
  | .rand w args          => .bv (randV w (refBVs env args))
  | .rorBits w args       => .bv (rorBitsV w (refBVs env args))
  | .rxor w args          => .bv (rxorV w (refBVs env args))
  | .rredOr w args        => .bv (rredOrV w (refBVs env args))
  | .req w args           => .bv (reqV w (refBVs env args))
  | .rshl w args          => .bv (rshlV w (refBVs env args))
  | .rmuxN w args         => .bv (rmuxNV w (refBVs env args))
  | .rnot w a             => .bv (rnotV w (refBV env a))
  | .rult w a b           => .bv (rultV w (refBV env a) (refBV env b))
  | .rugt w a b           => .bv (rugtV w (refBV env a) (refBV env b))
  | .rslt w a b           => .bv (rsltV w (refBV env a) (refBV env b))
  | .rsgt w a b           => .bv (rsgtV w (refBV env a) (refBV env b))
  | .rsra w a b           => .bv (rsraV w (refBV env a) (refBV env b))
  | .rsext w a amt        => .bv (rsextV w (refBV env a) (refBV env amt))
  | .rgetMask w a m       => .bv (rgetMaskV w (refBV env a) (refBV env m))
  | .rmux w sel fv tv     => .bv (rmuxV w (refBV env sel) (refBV env fv) (refBV env tv))
  | .rmemRead w m a en    => .bv (rmemReadV w (refMem env m) (refBV env a) (refBV env en))
  | .rmemWrite m a d en   => .mem (rmemWriteV (refMem env m) (refBV env a) (refBV env d)
                                              (refBV env en))
  | .rmemWriteBE w bw m a d be =>
      .mem (rmemWriteBEV w (refMem env m) (refBV env a) (refBV env d) (refBV env be) bw)

--------------------------------------------------------------------------------
-- `denoteResidual`
--------------------------------------------------------------------------------

/-- Run the compiled binding sequence.  Binding `i` is PUSHED, so it lands at
slot `sources.size + i` by construction — the dense invariant is maintained by
the data structure rather than asserted. -/
def runBindings (bs : List ResidualBinding) (env : SlotEnv) : SlotEnv :=
  match bs with
  | []      => env
  | b :: bs => runBindings bs (env.push (denoteExpr env b.rhs))

/-- Flop next value.  RESET HAS PRIORITY over enable; polarity is explicit;
`resetValue` is loaded rather than assumed zero. All three are exactly what the
manual emitter got wrong (it read 3 of a Flop's 8 pins with no `else`). -/
def flopNext (env : SlotEnv) (s : RuntimeState) (idx : Nat) (f : ResidualFlopUpdate) : BV :=
  let inReset : Bool :=
    match f.resetPin with
    | none   => false
    | some r => let rv := bv_nonzero (refBV env r)
                if f.resetActiveLow then !rv else rv
  if inReset then mk_bv f.width f.resetValue
  else
    let en : Bool :=
      match f.enable with
      | none   => true
      | some e => bv_nonzero (refBV env e)
    if en then bv_resize f.width (refBV env f.din)
    else s.flops[idx]?.getD (mk_bv f.width 0)

def denoteResidual (R : ResidualProgram) (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  let env := runBindings R.bindings.toList (sourceEnvArr R.sources i s)
  { outputs   := R.outputs.map fun o => bv_resize o.width (refBV env o.slot)
    nextState :=
      { flops := R.flopUpdates.mapIdx fun idx f => flopNext env s idx f
        mems  := R.memoryUpdates.map fun m => refMem env m.nextImg } }

end Compiler

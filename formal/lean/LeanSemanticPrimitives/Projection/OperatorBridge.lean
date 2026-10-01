/-
  Every object-language primitive, against the shared hardware semantics.

  Milestone 2 item 2 of `SIMULATOR_PLAN.md`.  `I_hw` is going to be an ordinary
  object `Program`, so its adequacy proof reduces node by node to "this `prim`
  term computes what `eval_op` computes".  This file supplies those steps.

  EVERY LEMMA HERE IS `rfl`, AND THAT IS THE RESULT, not a convenience.  It is
  the case only because `evalPrim`'s bit-vector cases call `LGraphModel`'s own
  `mk_bv`, `bv_bitwise`, `bv_not`, `bv_resize`, `bv_uint` and `bv_bit` -- see
  the decision recorded in `ObjectLanguageSemantics.lean` and
  `RuntimeEncoding.lean`.  An object language that reimplemented the bit
  operations would need each of these proved, several of them with a
  normalisation hypothesis, and would still be one edit away from drifting.

  What is NOT `rfl`, and is the actual content of Milestone 2, is the graph
  level: which primitive term corresponds to which node, and that walking the
  slot space in the object language reproduces `evalGraphG`.  That lives in
  `HardwareInterpreter.lean`.  The line between the two files is deliberate --
  everything mechanical is here, and what is left is exactly the interesting
  part.
-/

import LeanSemanticPrimitives.Projection.RuntimeEncoding

namespace Projection
open Compiler

/-! ## Primitives

Widths cross the boundary as `Int.ofNat w`: everything in `L` counts in `Int`,
and `widthOf` is the single place it becomes the `Nat` the hardware model wants.
A width that arrived negative would have been rejected by `asBV` before reaching
any of these. -/

@[simp] theorem prim_bvMk (w : Nat) (v : Int) :
    evalPrim .bvMk [.int (Int.ofNat w), .int v] = .ok (encBV (mk_bv w v)) := rfl

@[simp] theorem prim_bvWidth (a : BV) :
    evalPrim .bvWidth [encBV a] = .ok (.int (Int.ofNat a.width)) := rfl

@[simp] theorem prim_bvUint (a : BV) :
    evalPrim .bvUint [encBV a] = .ok (.int (bv_uint a)) := rfl

@[simp] theorem prim_bvBit (a : BV) (i : Nat) :
    evalPrim .bvBit [encBV a, .int (Int.ofNat i)] = .ok (.bool (bv_bit a i)) := rfl

@[simp] theorem prim_bvResize (w : Nat) (a : BV) :
    evalPrim .bvResize [.int (Int.ofNat w), encBV a] = .ok (encBV (bv_resize w a)) := rfl

@[simp] theorem prim_bvNot (w : Nat) (a : BV) :
    evalPrim .bvNot [.int (Int.ofNat w), encBV a] = .ok (encBV (bv_not w a)) := rfl

@[simp] theorem prim_bvAnd (w : Nat) (a b : BV) :
    evalPrim .bvAnd [.int (Int.ofNat w), encBV a, encBV b]
      = .ok (encBV (bv_bitwise w (· && ·) a b)) := rfl

@[simp] theorem prim_bvOr (w : Nat) (a b : BV) :
    evalPrim .bvOr [.int (Int.ofNat w), encBV a, encBV b]
      = .ok (encBV (bv_bitwise w (· || ·) a b)) := rfl

@[simp] theorem prim_bvXor (w : Nat) (a b : BV) :
    evalPrim .bvXor [.int (Int.ofNat w), encBV a, encBV b]
      = .ok (encBV (bv_bitwise w xor a b)) := rfl

@[simp] theorem prim_bvSra (w : Nat) (a s : BV) :
    evalPrim .bvSra [.int (Int.ofNat w), encBV a, encBV s]
      = .ok (encBV (bv_sra w a s)) := rfl

@[simp] theorem prim_bvGetMask (w : Nat) (a m : BV) :
    evalPrim .bvGetMask [.int (Int.ofNat w), encBV a, encBV m]
      = .ok (encBV (bv_get_mask w a m)) := rfl

/-! ## The one local overlay, and the proof that it changed nothing

`Translation/LGraphModel.lean` in THIS branch is the semantic base plus one
definitional factoring: `bv_shl_step` names the shifted copy `Op_SHL` folds,
which was previously written inline.  `Projection/overlays/` carries the patch
and a drift check for it.

The theorems below are the obligation that goes with taking that liberty, and
they are stated so that they cannot be satisfied by a semantic change: each
restates `Op_SHL`'s body AS IT READ BEFORE the overlay -- the inline
power/mod/sign expression, with no mention of `bv_shl_step` -- and proves it by
`rfl`.  If the overlay had altered meaning rather than introduced a name, these
would not typecheck.  Both operator-semantics functions the overlay touched are
covered. -/

theorem bv_shl_step_unfolds (w : Nat) (a b : BV) :
    bv_shl_step w a b = mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat) := rfl

theorem evalOp_SHL_unchanged_by_overlay (w : Nat) (a : BV) (bs : List BV) :
    eval_op .Op_SHL w (a :: bs)
      = bs.foldl (fun acc b =>
          bv_bitwise w (fun x y => xor x y) acc
            (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat))) (mk_bv w 0) := rfl

theorem denoteOp_SHL_unchanged_by_overlay (w : Nat) (a : BV) (bs : List BV) :
    denote_op .Op_SHL w (a :: bs)
      = bs.foldl (fun acc b =>
          bv_bitwise w (fun x y => xor x y) acc
            (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat))) (mk_bv w 0) := rfl

/-- The SIGNED reading, delegating to the pinned `bv_sint`.  `Op_SLT`/`Op_SGT`
and `Op_Sext` all need it, and none of them re-derives sign conversion. -/
@[simp] theorem prim_bvSint (a : BV) :
    evalPrim .bvSint [encBV a] = .ok (.int (bv_sint a)) := rfl

/-! ## Derived tests

`bv_nonzero` is a `Bool` in the hardware model and has no primitive of its own,
because it does not need one: it is `bv_uint x ≠ 0`, and `L` has integer
equality.  Spelling it out here rather than adding a primitive keeps `Prim`
minimal -- every primitive costs an `evalPrim` case, an encoding code, and a
`primTable` entry in `MixProgram.lean`. -/

theorem prim_eqI_zero (a : BV) :
    evalPrim .eqI [.int (bv_uint a), .int 0] = .ok (.bool (bv_uint a = 0)) := rfl

theorem bv_nonzero_eq (a : BV) : bv_nonzero a = !(decide (bv_uint a = 0)) := by
  simp [bv_nonzero]

/-! ## Operators

The graph level asks for `eval_op`, not for a primitive.  For the operators the
first vertical slice supports, the two differ only by how the operand list is
folded -- so these lemmas are what let `HardwareInterpreter.lean` emit a
primitive term per node and still be talking about `eval_op`.

`Op_And` resizes its FIRST operand to the node width and then folds the rest in
unchanged (`LGraphModel.lean:160-162`).  That asymmetry is not an oversight to
smooth over: it is what the fast model does, so the emitted term has to mirror
it exactly, and `and_two` below is the shape `I_hw` emits for a two-input node. -/

theorem evalOp_And_nil (w : Nat) : eval_op .Op_And w [] = mk_bv w 0 := rfl

theorem evalOp_And_cons (w : Nat) (a : BV) (args : List BV) :
    eval_op .Op_And w (a :: args)
      = args.foldl (fun acc b => bv_bitwise w (fun x y => x && y) acc b) (bv_resize w a) := rfl

theorem evalOp_And_two (w : Nat) (a b : BV) :
    eval_op .Op_And w [a, b] = bv_bitwise w (fun x y => x && y) (bv_resize w a) b := rfl

/-- …and the same operator over `CertVal`, which is what `interpretDesign`
actually calls.  `Op_And` is not a memory operator, so `eval_op_cert` delegates
definitionally. -/
theorem evalOpCert_And (w : Nat) (l : List BV) :
    eval_op_cert .Op_And w (l.map CertVal.bv) = .bv (eval_op .Op_And w l) :=
  eval_op_cert_bv .Op_And w l (fun _ => ⟨by simp, by simp, by simp⟩)

/-! ### Batch 1: `Op_Or`, `Op_SRA`, `Op_GetMask`

`Op_Or` DOES NOT share `Op_And`'s shape, and the difference is the kind that is
easy to smooth over by accident.  `Op_And` seeds the fold with `bv_resize w a`
and folds the REST; `Op_Or` seeds with `mk_bv w 0` and folds ALL of them,
including the first (`LGraphModel.lean:163-164`).  Each is transcribed from the
pinned model, not generalised from the other. -/

theorem evalOp_Or_fold (w : Nat) (args : List BV) :
    eval_op .Op_Or w args
      = args.foldl (fun acc b => bv_bitwise w (fun x y => x || y) acc b) (mk_bv w 0) := rfl

theorem evalOp_Or_nil (w : Nat) : eval_op .Op_Or w [] = mk_bv w 0 := rfl

theorem evalOp_Or_two (w : Nat) (a b : BV) :
    eval_op .Op_Or w [a, b]
      = bv_bitwise w (fun x y => x || y)
          (bv_bitwise w (fun x y => x || y) (mk_bv w 0) a) b := rfl

theorem evalOpCert_Or (w : Nat) (l : List BV) :
    eval_op_cert .Op_Or w (l.map CertVal.bv) = .bv (eval_op .Op_Or w l) :=
  eval_op_cert_bv .Op_Or w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOp_SRA (w : Nat) (a s : BV) :
    eval_op .Op_SRA w [a, s] = bv_sra w a s := rfl

theorem evalOpCert_SRA (w : Nat) (l : List BV) :
    eval_op_cert .Op_SRA w (l.map CertVal.bv) = .bv (eval_op .Op_SRA w l) :=
  eval_op_cert_bv .Op_SRA w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOp_GetMask (w : Nat) (a m : BV) :
    eval_op .Op_GetMask w [a, m] = bv_get_mask w a m := rfl

/-! ### Batch 2: `Op_Xor`, `Op_Not`, `Op_Sum`

`Op_Xor` is `Op_Or`'s shape with `xor`: zero-seeded, folding EVERY operand.
`Op_Not` is strictly unary.  `Op_Sum` carries its split point as the OPCODE
PAYLOAD -- `n_add` operands are added and the REST subtracted, all through
`bv_uint`, with the node width applied once at the end by `mk_bv`.  So the
truncation is of the SUM, not of each term. -/

theorem evalOp_Xor_fold (w : Nat) (args : List BV) :
    eval_op .Op_Xor w args
      = args.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc b) (mk_bv w 0) := rfl

theorem evalOp_Xor_two (w : Nat) (a b : BV) :
    eval_op .Op_Xor w [a, b]
      = bv_bitwise w (fun x y => xor x y)
          (bv_bitwise w (fun x y => xor x y) (mk_bv w 0) a) b := rfl

theorem evalOpCert_Xor (w : Nat) (l : List BV) :
    eval_op_cert .Op_Xor w (l.map CertVal.bv) = .bv (eval_op .Op_Xor w l) :=
  eval_op_cert_bv .Op_Xor w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOp_Not (w : Nat) (a : BV) : eval_op .Op_Not w [a] = bv_not w a := rfl

theorem evalOpCert_Not (w : Nat) (l : List BV) :
    eval_op_cert .Op_Not w (l.map CertVal.bv) = .bv (eval_op .Op_Not w l) :=
  eval_op_cert_bv .Op_Not w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOp_Sum (nAdd w : Nat) (args : List BV) :
    eval_op (.Op_Sum nAdd) w args
      = mk_bv w (((args.map bv_uint).take nAdd).sum
                 - ((args.map bv_uint).drop nAdd).sum) := rfl

theorem evalOpCert_Sum (nAdd w : Nat) (l : List BV) :
    eval_op_cert (.Op_Sum nAdd) w (l.map CertVal.bv) = .bv (eval_op (.Op_Sum nAdd) w l) :=
  eval_op_cert_bv (.Op_Sum nAdd) w l (fun _ => ⟨by simp, by simp, by simp⟩)

/-! ### Batch 3: `Op_EQ`, `Op_MuxBool`, `Op_MuxN`, `Op_Ror`

The first operators whose answer genuinely DEPENDS on operand values at run
time, so the residual keeps a comparison or an `ite`.  That is not leftover
dispatch: what must disappear is the certificate's own structure, not the
hardware's conditionals.

`Op_EQ` compares every operand against the FIRST, and an empty operand list is
`1`, not `0`.  `Op_Ror` is a REDUCTION -- any operand nonzero -- and yields a
one-bit answer, not a bitwise `Op_Or`.  `Op_MuxBool`'s operands are
`[sel, false_v, true_v]` in that order, so a polarity slip is a silent swap.
`Op_MuxN` takes its selector FIRST and indexes the rest from 0, with
out-of-range answering zero rather than wrapping or erroring. -/

theorem evalOp_EQ_nil (w : Nat) : eval_op .Op_EQ w [] = mk_bv w 1 := rfl

theorem evalOp_EQ_cons (w : Nat) (a : BV) (args : List BV) :
    eval_op .Op_EQ w (a :: args)
      = mk_bv w (if args.all fun b => bv_uint b = bv_uint a then 1 else 0) := rfl

theorem evalOpCert_EQ (w : Nat) (l : List BV) :
    eval_op_cert .Op_EQ w (l.map CertVal.bv) = .bv (eval_op .Op_EQ w l) :=
  eval_op_cert_bv .Op_EQ w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOp_Ror (w : Nat) (xs : List BV) :
    eval_op .Op_Ror w xs = mk_bv w (if xs.any bv_nonzero then 1 else 0) := rfl

theorem evalOpCert_Ror (w : Nat) (l : List BV) :
    eval_op_cert .Op_Ror w (l.map CertVal.bv) = .bv (eval_op .Op_Ror w l) :=
  eval_op_cert_bv .Op_Ror w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOp_MuxBool (w : Nat) (sel fv tv : BV) :
    eval_op .Op_MuxBool w [sel, fv, tv]
      = if bv_nonzero sel then bv_resize w tv else bv_resize w fv := rfl

theorem evalOpCert_MuxBool (w : Nat) (l : List BV) :
    eval_op_cert .Op_MuxBool w (l.map CertVal.bv) = .bv (eval_op .Op_MuxBool w l) :=
  eval_op_cert_bv .Op_MuxBool w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOp_MuxN_nil (w : Nat) : eval_op .Op_MuxN w [] = mk_bv w 0 := rfl

theorem evalOp_MuxN_cons (w : Nat) (sel : BV) (args : List BV) :
    eval_op .Op_MuxN w (sel :: args)
      = (let idx := (bv_uint sel).toNat
         if idx < args.length then bv_resize w ((args[idx]?).getD (mk_bv w 0))
         else mk_bv w 0) := rfl

theorem evalOpCert_MuxN (w : Nat) (l : List BV) :
    eval_op_cert .Op_MuxN w (l.map CertVal.bv) = .bv (eval_op .Op_MuxN w l) :=
  eval_op_cert_bv .Op_MuxN w l (fun _ => ⟨by simp, by simp, by simp⟩)

/-! ### Batch 4: the comparisons, and `Op_Sext`

UNSIGNED and SIGNED comparison are different operators over the same bits, and
the difference is the whole point of having four of them: at width 4, `8` is
`0b1000`, which is 8 unsigned and -8 signed.  So `Op_ULT 8 1` is false while
`Op_SLT 8 1` is true.  `Op_UGT`/`Op_SGT` are the same tests with the operands
the other way round -- there is no separate "greater" primitive. -/

theorem evalOp_ULT (w : Nat) (a b : BV) :
    eval_op .Op_ULT w [a, b] = mk_bv w (if bv_uint a < bv_uint b then 1 else 0) := rfl

theorem evalOp_UGT (w : Nat) (a b : BV) :
    eval_op .Op_UGT w [a, b] = mk_bv w (if bv_uint a > bv_uint b then 1 else 0) := rfl

theorem evalOp_SLT (w : Nat) (a b : BV) :
    eval_op .Op_SLT w [a, b] = mk_bv w (if bv_sint a < bv_sint b then 1 else 0) := rfl

theorem evalOp_SGT (w : Nat) (a b : BV) :
    eval_op .Op_SGT w [a, b] = mk_bv w (if bv_sint a > bv_sint b then 1 else 0) := rfl

theorem evalOpCert_ULT (w : Nat) (l : List BV) :
    eval_op_cert .Op_ULT w (l.map CertVal.bv) = .bv (eval_op .Op_ULT w l) :=
  eval_op_cert_bv .Op_ULT w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOpCert_UGT (w : Nat) (l : List BV) :
    eval_op_cert .Op_UGT w (l.map CertVal.bv) = .bv (eval_op .Op_UGT w l) :=
  eval_op_cert_bv .Op_UGT w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOpCert_SLT (w : Nat) (l : List BV) :
    eval_op_cert .Op_SLT w (l.map CertVal.bv) = .bv (eval_op .Op_SLT w l) :=
  eval_op_cert_bv .Op_SLT w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOpCert_SGT (w : Nat) (l : List BV) :
    eval_op_cert .Op_SGT w (l.map CertVal.bv) = .bv (eval_op .Op_SGT w l) :=
  eval_op_cert_bv .Op_SGT w l (fun _ => ⟨by simp, by simp, by simp⟩)

/-- **`Op_Sext` IS a composition of two pinned helpers**, and this is the
theorem that says so.  Its pinned body is an inline power/mod/sign formula, but
that formula is exactly `bv_sint` applied to `bv_resize`: truncating to the low
`n` bits and then READING THOSE BITS AS SIGNED is what sign extension is.

So nothing re-derives the formula -- `I_hw` emits `bv_sint (bv_resize n a)` and
this theorem carries the obligation.  The `n = 0` case needs no special
handling either: `bv_resize 0` has width 0 and `bv_sint` of a width-0 vector is
0, which is the pinned answer.

Not `rfl`, and the reason is one step of arithmetic: the pinned body takes
`bv_uint a % 2 ^ n` once, while `bv_uint (bv_resize n a)` takes it twice.
`Int.emod_emod` closes exactly that gap. -/
theorem evalOp_Sext (w : Nat) (a amount : BV) :
    eval_op .Op_Sext w [a, amount]
      = mk_bv w (bv_sint (bv_resize (bv_uint amount).toNat a)) := by
  simp only [eval_op, bv_sint, bv_resize, bv_uint, mk_bv, Int.emod_emod]
  split
  · simp_all
  · split <;> rfl

theorem evalOpCert_Sext (w : Nat) (l : List BV) :
    eval_op_cert .Op_Sext w (l.map CertVal.bv) = .bv (eval_op .Op_Sext w l) :=
  eval_op_cert_bv .Op_Sext w l (fun _ => ⟨by simp, by simp, by simp⟩)

theorem evalOpCert_GetMask (w : Nat) (l : List BV) :
    eval_op_cert .Op_GetMask w (l.map CertVal.bv) = .bv (eval_op .Op_GetMask w l) :=
  eval_op_cert_bv .Op_GetMask w l (fun _ => ⟨by simp, by simp, by simp⟩)

/-! ## Sources

`sourceValue` is the other half of what one cycle needs.  These are stated as
equalities on `CertVal` rather than on `Val`, because that is the form
`interpretDesign` produces; `HardwareInterpreter.lean` crosses to `Val` once,
through `encBV`, rather than once per source kind. -/

theorem sourceValue_input (i : RuntimeInput) (s : RuntimeState) (idx w : Nat) :
    sourceValue i s (.input idx w) = .bv (bv_resize w (i[idx]?.getD (mk_bv w 0))) := rfl

theorem sourceValue_const (i : RuntimeInput) (s : RuntimeState) (w : Nat) (v : Int) :
    sourceValue i s (.const w v) = .bv (mk_bv w v) := rfl

theorem sourceValue_flopQ (i : RuntimeInput) (s : RuntimeState) (idx w : Nat) :
    sourceValue i s (.flopQ idx w) = .bv (bv_resize w (s.flops[idx]?.getD (mk_bv w 0))) := rfl

/-- The asynchronous case, spelled out because its reset is read from the INPUT
array by ordinal rather than from a slot -- `sourceValue` runs before any slot
exists.  `I_hw` has to index the raw input list for it, not the slot
environment. -/
theorem sourceValue_flopQAsync (i : RuntimeInput) (s : RuntimeState)
    (idx w ri : Nat) (rv : Int) (al : Bool) :
    sourceValue i s (.flopQAsync idx w ri rv al) =
      .bv (if (if al then !(bv_nonzero (i[ri]?.getD (mk_bv 1 0)))
               else bv_nonzero (i[ri]?.getD (mk_bv 1 0)))
           then mk_bv w rv
           else bv_resize w (s.flops[idx]?.getD (mk_bv w 0))) := rfl

/-! ## Flop next state

Reset before enable, the reset VALUE rather than a hardcoded zero, and the
old-state fallback when disabled.  All three are read from the descriptor, and
all three are pinned by a vector in `SimulatorContract.Acceptance.seqD`.

The edge is part of the rule now: a quiet domain holds, and an ASYNCHRONOUS
reset acts regardless of it while a synchronous one does not.  Those two are
pinned by vectors too, in the same place. -/

theorem srcFlopNext_eq (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (f : FlopDesc) :
    srcFlopNext rho e s idx f =
      (if (match f.resetPin with
           | none   => false
           | some r => xor f.resetActiveLow (bv_nonzero (rho r).asBV))
          && (fires e f.clock || f.asyncReset)
       then mk_bv f.width f.resetValue
       else if fires e f.clock
               && (match f.enable with
                   | none    => true
                   | some en => bv_nonzero (rho en).asBV)
            then bv_resize f.width (rho f.din).asBV
            else s.flops[idx]?.getD (mk_bv f.width 0)) := rfl

end Projection

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
all three are pinned by a vector in `SimulatorContract.Acceptance.seqD`. -/

theorem srcFlopNext_eq (rho : Nat → CertVal) (s : RuntimeState) (idx : Nat) (f : FlopDesc) :
    srcFlopNext rho s idx f =
      (if (match f.resetPin with
           | none   => false
           | some r => xor f.resetActiveLow (bv_nonzero (rho r).asBV))
       then mk_bv f.width f.resetValue
       else if (match f.enable with
                | none   => true
                | some e => bv_nonzero (rho e).asBV)
            then bv_resize f.width (rho f.din).asBV
            else s.flops[idx]?.getD (mk_bv f.width 0)) := rfl

end Projection

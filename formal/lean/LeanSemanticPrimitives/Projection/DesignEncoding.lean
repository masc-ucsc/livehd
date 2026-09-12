/-
  The hardware domain as object-language data.

  Milestone 1 of `SIMULATOR_PLAN.md`, static half.  `mix` specializes a program
  written in `L`, and the thing it specializes with respect to is a *value* --
  so before there can be a hardware interpreter `I_hw` to specialize, a
  `DesignCert` has to BE a `Val`.  This file is that bridge, and nothing more:
  no semantics, no interpreter, no specialization.

  THE ONLY THEOREM THAT MATTERS is `decDesign_encDesign`.  Downstream needs
  exactly one thing from this encoding -- that no two distinct certificates
  encode alike, so that "the specializer was given `encDesign D`" pins down `D`
  and `projectDesign_correct` can be stated about `D` rather than about a value.
  `encDesign_inj` is that consequence, and it is three lines.

  TAGS 100-119, disjoint by construction from everything already allocated:
  `Term` uses 10-20, `ATerm` 30-42, the object specializer's own data 60-80, and
  `bvTag` is 1000.  A collision would not be a type error -- both live in `Val`
  -- it would be a silent mis-decode, so the ranges are kept far apart and the
  allocation is recorded here in one place.

  MEMORY IS NOT THE PROBLEM HERE.  The plan flags memory as the hard case, and
  it is -- but the hardness is entirely in `RuntimeState.mems`, which is
  FUNCTION-valued (`Array (Int -> BV)`) and therefore has no finite
  representative in `Val`.  Every *static* memory component -- `memImg`,
  `memConst` with its literal table, `MemoryDesc`, and all three memory
  operators -- is ordinary finite data and round-trips like anything else.  So
  this file is TOTAL over `DesignCert`: the memory-free restriction lives in
  `RuntimeEncoding.lean`, where it actually bites, and not here.
-/

import LeanSemanticPrimitives.Projection.Encoding
import LeanSemanticPrimitives.Compiler.DesignCert

namespace Projection
open Compiler

/-! ## Generic list plumbing

Six different lists are encoded below (sources, nodes, outputs, flops, memories,
dependency indices) and a seventh in `RuntimeEncoding.lean`.  Writing the cons
walk once and proving its round trip once is the single largest economy in this
file: each concrete encoder then costs one line and each round trip one
application. -/

def encListG {α : Type} (e : α → Val) : List α → Val
  | []      => .nil
  | x :: xs => .cons (e x) (encListG e xs)

def decListG {α : Type} (d : Val → Option α) : Val → Option (List α)
  | .nil      => some []
  | .cons a b =>
    match d a, decListG d b with
    | some x, some xs => some (x :: xs)
    | _,      _       => none
  | _         => none

theorem decListG_encListG {α : Type} {e : α → Val} {d : Val → Option α}
    (h : ∀ x, d (e x) = some x) :
    ∀ xs : List α, decListG d (encListG e xs) = some xs
  | []      => rfl
  | x :: xs => by simp [encListG, decListG, h x, decListG_encListG h xs]

/-- `Array` is what `DesignCert` actually uses; `List` is what a cons chain
decodes to.  Bridging once here keeps every concrete encoder below free of
`Array`/`List` conversion noise. -/
def encArr {α : Type} (e : α → Val) (xs : Array α) : Val := encListG e xs.toList

def decArr {α : Type} (d : Val → Option α) (v : Val) : Option (Array α) :=
  (decListG d v).map List.toArray

theorem decArr_encArr {α : Type} {e : α → Val} {d : Val → Option α}
    (h : ∀ x, d (e x) = some x) (xs : Array α) : decArr d (encArr e xs) = some xs := by
  simp [decArr, encArr, decListG_encListG h]

/-! ## Scalars

`encNat`/`decNat` already exist in `Encoding.lean` and are reused.  `Int` and
`Bool` are `Val` constructors outright, so their encoders are the identity in
all but name -- named anyway, so that `decDesign` reads uniformly and a field's
type is visible at its use site. -/

@[inline] def encInt (i : Int) : Val := .int i
def decInt : Val → Option Int
  | .int i => some i
  | _      => none
@[simp] theorem decInt_encInt (i : Int) : decInt (encInt i) = some i := rfl

@[inline] def encBool (b : Bool) : Val := .bool b
def decBool : Val → Option Bool
  | .bool b => some b
  | _       => none
@[simp] theorem decBool_encBool (b : Bool) : decBool (encBool b) = some b := rfl

/-- `Option Nat` as a zero-or-one-element chain, NOT as a sentinel integer.

`FlopDesc.enable` and `.resetPin` are `Option Nat`, and "absent" has to be
distinguishable from slot 0 -- a flop whose enable is driven by slot 0 is an
ordinary design.  A `-1` sentinel (the shape `MixProgram.lean` uses for a
missing specialization index) would work arithmetically but `isNil` is the test
`I_hw` can actually perform on it in `L`, with no comparison against a magic
number. -/
def encONat : Option Nat → Val
  | none   => .nil
  | some n => .cons (encNat n) .nil

def decONat : Val → Option (Option Nat)
  | .nil         => some none
  | .cons a .nil => (decNat a).map some
  | _            => none

@[simp] theorem decONat_encONat (o : Option Nat) : decONat (encONat o) = some o := by
  cases o <;> simp [encONat, decONat]

/-! ## Operators

A flat code plus a payload slot, in the shape `primCode`/`primOfCode` already
established for `Prim` -- rather than 29 tags.  Three operators carry data
(`Op_Const`, `Op_Sum`, `Op_MemWriteBE`); the payload is `nil` for the other 26,
so every operator has the same two-field shape and `I_hw`'s dispatch is one
`ctorTagP` plus one integer compare rather than a 29-deep tag chain. -/

def opCode : LGraphOp → Nat
  | .Op_Const _      => 0
  | .Op_Sum _        => 1
  | .Op_Sub          => 2
  | .Op_Mult         => 3
  | .Op_Div          => 4
  | .Op_UDiv         => 5
  | .Op_SDiv         => 6
  | .Op_And          => 7
  | .Op_Or           => 8
  | .Op_Xor          => 9
  | .Op_Ror          => 10
  | .Op_Not          => 11
  | .Op_LT           => 12
  | .Op_GT           => 13
  | .Op_ULT          => 14
  | .Op_UGT          => 15
  | .Op_SLT          => 16
  | .Op_SGT          => 17
  | .Op_EQ           => 18
  | .Op_SHL          => 19
  | .Op_SRA          => 20
  | .Op_MuxBool      => 21
  | .Op_MuxN         => 22
  | .Op_Sext         => 23
  | .Op_GetMask      => 24
  | .Op_SetMask      => 25
  | .Op_MemRead      => 26
  | .Op_MemWrite     => 27
  | .Op_MemWriteBE _ => 28

/-- The payload of an operator that has one; `nil` otherwise. -/
def opPayload : LGraphOp → Val
  | .Op_Const c      => encInt c
  | .Op_Sum n        => encNat n
  | .Op_MemWriteBE b => encNat b
  | _                => .nil

def opOfCode : Nat → Val → Option LGraphOp
  | 0,  v => (decInt v).map .Op_Const
  | 1,  v => (decNat v).map .Op_Sum
  | 2,  _ => some .Op_Sub
  | 3,  _ => some .Op_Mult
  | 4,  _ => some .Op_Div
  | 5,  _ => some .Op_UDiv
  | 6,  _ => some .Op_SDiv
  | 7,  _ => some .Op_And
  | 8,  _ => some .Op_Or
  | 9,  _ => some .Op_Xor
  | 10, _ => some .Op_Ror
  | 11, _ => some .Op_Not
  | 12, _ => some .Op_LT
  | 13, _ => some .Op_GT
  | 14, _ => some .Op_ULT
  | 15, _ => some .Op_UGT
  | 16, _ => some .Op_SLT
  | 17, _ => some .Op_SGT
  | 18, _ => some .Op_EQ
  | 19, _ => some .Op_SHL
  | 20, _ => some .Op_SRA
  | 21, _ => some .Op_MuxBool
  | 22, _ => some .Op_MuxN
  | 23, _ => some .Op_Sext
  | 24, _ => some .Op_GetMask
  | 25, _ => some .Op_SetMask
  | 26, _ => some .Op_MemRead
  | 27, _ => some .Op_MemWrite
  | 28, v => (decNat v).map .Op_MemWriteBE
  | _,  _ => none

/-! ## The tag table

One place, as in `Encoding.lean`: encoder, decoder and (later) `I_hw` written in
`L` all refer to these names, so a renumbering cannot leave one copy behind. -/

def tagSrcInput    : Nat := 100
def tagSrcConst    : Nat := 101
def tagSrcFlopQ    : Nat := 102
def tagSrcFlopQA   : Nat := 103
def tagSrcMemImg   : Nat := 104
def tagSrcMemConst : Nat := 105
def tagOp          : Nat := 110
def tagNode        : Nat := 111
def tagOutput      : Nat := 112
def tagFlop        : Nat := 113
def tagMemory      : Nat := 114
def tagDesign      : Nat := 115

@[inline] def encOp (op : LGraphOp) : Val := .ctor tagOp [encNat (opCode op), opPayload op]

def decOp : Val → Option LGraphOp
  | .ctor tg [c, p] => if tg = tagOp then (decNat c).bind (fun n => opOfCode n p) else none
  | _               => none

/-- `rfl` first, `simp` only as a fallback -- and the order is worth 16x here.

After `cases`, 26 of the 29 operators have a LITERAL code and a `nil` payload,
so the whole decode chain (the tag compare, `decNat` on a numeral, the
`opOfCode` dispatch) reduces definitionally.  Only `Op_Const`, `Op_Sum` and
`Op_MemWriteBE` carry a variable, which is exactly what blocks `rfl`.  Handing
all 29 to `simp` with the full unfold set instead costs 12.9 s of a 0.8 s
proof. -/
@[simp] theorem decOp_encOp (op : LGraphOp) : decOp (encOp op) = some op := by
  cases op <;>
    first
      | rfl
      | simp [encOp, decOp, opCode, opPayload, opOfCode, encInt, decInt]

/-! ## Certificate components

Dispatch on FIELD COUNT first and tag second, exactly as `decTerm` does, and for
the same reason: it keeps each `if` chain short and puts the field names in
scope once.  Here the six `SourceDesc` constructors have field counts 2, 2, 2,
5, 3 and 3, so the count already separates `flopQAsync` from everything else. -/

def encSource : SourceDesc → Val
  | .input idx w              => .ctor tagSrcInput [encNat idx, encNat w]
  | .const w v                => .ctor tagSrcConst [encNat w, encInt v]
  | .flopQ idx w              => .ctor tagSrcFlopQ [encNat idx, encNat w]
  | .flopQAsync idx w ri rv al =>
      .ctor tagSrcFlopQA [encNat idx, encNat w, encNat ri, encInt rv, encBool al]
  | .memImg idx aw dw         => .ctor tagSrcMemImg [encNat idx, encNat aw, encNat dw]
  | .memConst aw dw contents  =>
      .ctor tagSrcMemConst [encNat aw, encNat dw, encArr encInt contents]

def decSource : Val → Option SourceDesc
  | .ctor tg [a, b] =>
    if tg = tagSrcInput then
      match decNat a, decNat b with
      | some i, some w => some (.input i w)
      | _, _ => none
    else if tg = tagSrcConst then
      match decNat a, decInt b with
      | some w, some v => some (.const w v)
      | _, _ => none
    else if tg = tagSrcFlopQ then
      match decNat a, decNat b with
      | some i, some w => some (.flopQ i w)
      | _, _ => none
    else none
  | .ctor tg [a, b, c] =>
    if tg = tagSrcMemImg then
      match decNat a, decNat b, decNat c with
      | some i, some aw, some dw => some (.memImg i aw dw)
      | _, _, _ => none
    else if tg = tagSrcMemConst then
      match decNat a, decNat b, decArr decInt c with
      | some aw, some dw, some ct => some (.memConst aw dw ct)
      | _, _, _ => none
    else none
  | .ctor tg [a, b, c, d, e] =>
    if tg = tagSrcFlopQA then
      match decNat a, decNat b, decNat c, decInt d, decBool e with
      | some i, some w, some ri, some rv, some al => some (.flopQAsync i w ri rv al)
      | _, _, _, _, _ => none
    else none
  | _ => none

@[simp] theorem decSource_encSource (sd : SourceDesc) : decSource (encSource sd) = some sd := by
  cases sd <;>
    simp [encSource, decSource, tagSrcInput, tagSrcConst, tagSrcFlopQ,
          tagSrcFlopQA, tagSrcMemImg, tagSrcMemConst,
          decArr_encArr (e := encInt) (d := decInt) decInt_encInt]

def encNode (c : DenseNodeCert) : Val :=
  .ctor tagNode [encOp c.op, encNat c.width, encArr encNat c.deps, encNat c.origin]

def decNode : Val → Option DenseNodeCert
  | .ctor tg [o, w, d, g] =>
    if tg = tagNode then
      match decOp o, decNat w, decArr decNat d, decNat g with
      | some o', some w', some d', some g' =>
          some { op := o', width := w', deps := d', origin := g' }
      | _, _, _, _ => none
    else none
  | _ => none

@[simp] theorem decNode_encNode (c : DenseNodeCert) : decNode (encNode c) = some c := by
  simp [encNode, decNode, tagNode, decArr_encArr (e := encNat) (d := decNat) decNat_encNat]

def encOutput (o : OutputDesc) : Val := .ctor tagOutput [encNat o.slot, encNat o.width]

def decOutput : Val → Option OutputDesc
  | .ctor tg [s, w] =>
    if tg = tagOutput then
      match decNat s, decNat w with
      | some s', some w' => some { slot := s', width := w' }
      | _, _ => none
    else none
  | _ => none

@[simp] theorem decOutput_encOutput (o : OutputDesc) : decOutput (encOutput o) = some o := by
  simp [encOutput, decOutput, tagOutput]

/-- All six pins, including the two the manual emitter dropped.  `resetValue`
and `resetActiveLow` are carried here for the same reason `FlopDesc` carries
them: transcribing what an emitter happened to read would turn its bug into a
theorem. -/
def encFlop (f : FlopDesc) : Val :=
  .ctor tagFlop [encNat f.width, encNat f.din, encONat f.enable,
                 encONat f.resetPin, encInt f.resetValue, encBool f.resetActiveLow]

def decFlop : Val → Option FlopDesc
  | .ctor tg [w, d, e, r, rv, al] =>
    if tg = tagFlop then
      match decNat w, decNat d, decONat e, decONat r, decInt rv, decBool al with
      | some w', some d', some e', some r', some rv', some al' =>
          some { width := w', din := d', enable := e', resetPin := r',
                 resetValue := rv', resetActiveLow := al' }
      | _, _, _, _, _, _ => none
    else none
  | _ => none

@[simp] theorem decFlop_encFlop (f : FlopDesc) : decFlop (encFlop f) = some f := by
  simp [encFlop, decFlop, tagFlop]

def encMemory (m : MemoryDesc) : Val :=
  .ctor tagMemory [encNat m.aw, encNat m.dw, encNat m.nextImg]

def decMemory : Val → Option MemoryDesc
  | .ctor tg [a, d, n] =>
    if tg = tagMemory then
      match decNat a, decNat d, decNat n with
      | some a', some d', some n' => some { aw := a', dw := d', nextImg := n' }
      | _, _, _ => none
    else none
  | _ => none

@[simp] theorem decMemory_encMemory (m : MemoryDesc) : decMemory (encMemory m) = some m := by
  simp [encMemory, decMemory, tagMemory]

/-! ## The certificate -/

def encDesign (D : DesignCert) : Val :=
  .ctor tagDesign
    [encArr encSource D.sources, encArr encNode D.nodes, encArr encOutput D.outputs,
     encArr encFlop D.flops, encArr encMemory D.memories]

def decDesign : Val → Option DesignCert
  | .ctor tg [s, n, o, f, m] =>
    if tg = tagDesign then
      match decArr decSource s, decArr decNode n, decArr decOutput o,
            decArr decFlop f, decArr decMemory m with
      | some s', some n', some o', some f', some m' =>
          some { sources := s', nodes := n', outputs := o', flops := f', memories := m' }
      | _, _, _, _, _ => none
    else none
  | _ => none

/-- Milestone 1, item 4: every static certificate component round-trips, with
no exception for memory. -/
theorem decDesign_encDesign (D : DesignCert) : decDesign (encDesign D) = some D := by
  simp [encDesign, decDesign, tagDesign,
        decArr_encArr (e := encSource)  (d := decSource)  decSource_encSource,
        decArr_encArr (e := encNode)    (d := decNode)    decNode_encNode,
        decArr_encArr (e := encOutput)  (d := decOutput)  decOutput_encOutput,
        decArr_encArr (e := encFlop)    (d := decFlop)    decFlop_encFlop,
        decArr_encArr (e := encMemory)  (d := decMemory)  decMemory_encMemory]

/-- The consequence everything downstream actually uses: a specializer handed
`encDesign D` was handed `D`, so `projectDesign_correct` can be a statement
about the certificate rather than about a value that resembles one. -/
theorem encDesign_inj {D E : DesignCert} (h : encDesign D = encDesign E) : D = E := by
  have := decDesign_encDesign D
  rw [h, decDesign_encDesign E] at this
  exact (Option.some.inj this).symm

end Projection

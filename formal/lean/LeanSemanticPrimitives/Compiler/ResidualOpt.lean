/-
# Proved compiler passes on vc's compiled program: the frame (definitions)

`compileDesign` maps each certificate node to one binding, so vc's program keeps every
binding the exporter wrote, and rebuilds every source -- most of them constants -- at the
start of every cycle. This module optimizes that program, in the translation-validation
style: an UNPROVED optimizer (`optimizeRaw`) proposes a certificate, a small checker
(`validate`) accepts or refuses it, and a fixed, simple rebuild (`applyCert`) turns the
program and an accepted certificate into the new program. `optimize` falls back to the
original program when the checker refuses, so it is ALWAYS correct
(`ResidualOptSound.optimize_correct`: `denoteResidual (optimize R) = denoteResidual R`).

## The certificate

`pos` maps every OLD slot to its NEW slot; `keep` says which bindings stay; `fold` says
which bindings are constants; `just` says why a binding that is neither still has a value
in the new program; `newSources` are the new sources. The rebuild renames every reference
by `pos` and drops the bindings not kept. The checker requires, for every reference a kept
binding, an output, a flop or a memory reads:
- a source: the new sources hold the same descriptor at its new slot (so identical
  constants may share one new source);
- a binding: it is earlier (for a binding's operands) and either kept, or folded with the
  new source at its new slot the constant the CHECKER computed for it, or aliased: its new
  slot holds its value for a reason the checker verified (`Just`):
  - `cse`: the new program computes, at that slot, the binding's own expression renamed;
  - `copy q`: the binding's value is old slot `q`'s (`copyOf`: a one-operand OR, XOR, AND,
    sum or product of the same width, GetMask with a constant mask that keeps every bit
    of a same-width operand, or a mux whose arms are one slot or whose select the checker
    knows, choosing a same-width arm), and the binding's new slot is `q`'s;
and that the kept bindings land, in order, right after the new sources.

The checker computes every folded binding's constant itself, in one pass: a folded
binding may read only constant sources and earlier folded bindings, whose values the
checker already knows, and `denoteExpr` on those values is the binding's value on every
step (it reads nothing else). In the same pass it records each slot's static width
(`srcWidth`, `exprWidth`: the value is a reduced bit vector of that width on every step),
which a copy needs, and the renamed expression of each kept binding, which a `cse` needs.

## The passes so far

- constant folding and propagation: a binding whose operands are all constants (constant
  sources, or bindings folded before it) is evaluated once, and its readers read a
  constant source instead;
- dead-code elimination: a binding nothing reachable from an output, a flop or a memory
  reads is dropped, and so is every source nothing reads (a folded binding reads
  nothing: its operands may die with it);
- constant sharing: constant sources with the same width and value become one, folded
  constants included;
- copy propagation: a binding whose value is one of its operands' (`copyOf`) reads that
  operand's new slot instead;
- common subexpressions: a binding whose expression, renamed, is an earlier kept
  binding's reads that binding's new slot instead;
- branch folding: a mux with a known select, or with one slot on both arms, is a copy of
  the arm it takes.

Mathlib-free; the proofs are in `ResidualOptSound`.
-/
import LeanSemanticPrimitives.Compiler.ResidualSemantics

namespace Compiler
namespace ResidualOpt

open Residual

/-! ## Renaming references -/

def renameExpr (σ : Nat → Nat) : ResidualExpr → ResidualExpr
  | .rsum w n args             => .rsum w n (args.map σ)
  | .rmult w args              => .rmult w (args.map σ)
  | .rand w args               => .rand w (args.map σ)
  | .rorBits w args            => .rorBits w (args.map σ)
  | .rxor w args               => .rxor w (args.map σ)
  | .rredOr w args             => .rredOr w (args.map σ)
  | .req w args                => .req w (args.map σ)
  | .rshl w args               => .rshl w (args.map σ)
  | .rmuxN w args              => .rmuxN w (args.map σ)
  | .rnot w a                  => .rnot w (σ a)
  | .rult w a b                => .rult w (σ a) (σ b)
  | .rugt w a b                => .rugt w (σ a) (σ b)
  | .rslt w a b                => .rslt w (σ a) (σ b)
  | .rsgt w a b                => .rsgt w (σ a) (σ b)
  | .rsra w a b                => .rsra w (σ a) (σ b)
  | .rsext w a b               => .rsext w (σ a) (σ b)
  | .rgetMask w a b            => .rgetMask w (σ a) (σ b)
  | .rudiv w a b               => .rudiv w (σ a) (σ b)
  | .rsdiv w a b               => .rsdiv w (σ a) (σ b)
  | .rsetMask w a m v          => .rsetMask w (σ a) (σ m) (σ v)
  | .rmux w s f t              => .rmux w (σ s) (σ f) (σ t)
  | .rmemRead w m a en         => .rmemRead w (σ m) (σ a) (σ en)
  | .rmemWrite m a d en        => .rmemWrite (σ m) (σ a) (σ d) (σ en)
  | .rmemWriteBE w bw m a d be => .rmemWriteBE w bw (σ m) (σ a) (σ d) (σ be)
  | .rmemLoad w n m bus en     => .rmemLoad w n (σ m) (σ bus) (σ en)
  | .rmemToBus w n m           => .rmemToBus w n (σ m)

/-- Every reference an expression reads. -/
def exprRefs : ResidualExpr → List Nat
  | .rsum _ _ args | .rmult _ args | .rand _ args | .rorBits _ args | .rxor _ args
  | .rredOr _ args | .req _ args | .rshl _ args | .rmuxN _ args => args.toList
  | .rnot _ a => [a]
  | .rult _ a b | .rugt _ a b | .rslt _ a b | .rsgt _ a b | .rsra _ a b | .rsext _ a b
  | .rgetMask _ a b | .rudiv _ a b | .rsdiv _ a b => [a, b]
  | .rsetMask _ a m v => [a, m, v]
  | .rmux _ s f t => [s, f, t]
  | .rmemRead _ m a en => [m, a, en]
  | .rmemWrite m a d en => [m, a, d, en]
  | .rmemWriteBE _ _ m a d be => [m, a, d, be]
  | .rmemLoad _ _ m bus en => [m, bus, en]
  | .rmemToBus _ _ m => [m]

def renameOutput (σ : Nat → Nat) (o : ResidualOutput) : ResidualOutput :=
  { o with slot := σ o.slot }

def renameFlop (σ : Nat → Nat) (f : ResidualFlopUpdate) : ResidualFlopUpdate :=
  { f with din := σ f.din, enable := f.enable.map σ, resetPin := f.resetPin.map σ }

def renameCommit (σ : Nat → Nat) (c : MemCommit) : MemCommit :=
  { c with nextImg := σ c.nextImg }

def renameMem (σ : Nat → Nat) (m : ResidualMemoryUpdate) : ResidualMemoryUpdate :=
  { m with nextImg := σ m.nextImg, commits := m.commits.map (renameCommit σ),
           asyncImg := m.asyncImg.map σ }

def flopRefs (f : ResidualFlopUpdate) : List Nat :=
  f.din :: (f.enable.toList ++ f.resetPin.toList)

def memRefs (m : ResidualMemoryUpdate) : List Nat :=
  m.nextImg :: (m.commits.toList.map (·.nextImg) ++ m.asyncImg.toList)

/-! ## Static widths -/

/-- The width of a source's value, when it is a bit vector (`none`: a memory image). -/
def srcWidth : SourceDesc → Option Nat
  | .input _ w => some w
  | .const w _ => some w
  | .flopQ _ w => some w
  | .flopQAsync _ w _ _ _ => some w
  | _ => none

/-- The width of an expression's value, when it is a bit vector (`none`: a memory). -/
def exprWidth : ResidualExpr → Option Nat
  | .rsum w _ _ | .rmult w _ | .rand w _ | .rorBits w _ | .rxor w _ | .rredOr w _ | .req w _
  | .rshl w _ | .rmuxN w _ | .rnot w _ | .rult w _ _ | .rugt w _ _ | .rslt w _ _ | .rsgt w _ _
  | .rsra w _ _ | .rsext w _ _ | .rgetMask w _ _ | .rudiv w _ _ | .rsdiv w _ _
  | .rsetMask w _ _ _ | .rmux w _ _ _ | .rmemRead w _ _ _ | .rmemToBus w _ _ => some w
  | .rmemWrite .. | .rmemWriteBE .. | .rmemLoad .. => none

/-! ## Copies -/

/-- A constant mask that keeps bits `0 .. L-1` of its operand, with `w ≤ L` and the run
inside the mask's own width. -/
def lowMaskCovers (w : Nat) (m : BV) : Bool :=
  let M := FastBV.uintNat m
  let L := Nat.log2 (M + 1)
  M == 2 ^ L - 1 && decide (w ≤ L) && decide (L ≤ m.width)

/-- The operand whose value an expression is, when the expression is a copy of it: given
which slots hold known constants (`kn`), their values (`cv`) and each slot's static width
(`wq`). Shared by the optimizer and the checker. -/
@[specialize] def copyOf (kn : Nat → Bool) (cv : Nat → CertVal) (wq : Nat → Option Nat) :
    ResidualExpr → Option Nat
  | .rorBits w args | .rxor w args | .rand w args | .rmult w args =>
      match args.toList with
      | [q] => if wq q == some w then some q else none
      | _ => none
  | .rsum w n args =>
      match args.toList with
      | [q] => if decide (1 ≤ n) && wq q == some w then some q else none
      | _ => none
  | .rgetMask w a m =>
      if wq a == some w && kn m &&
          (match cv m with
           | .bv mv => lowMaskCovers w mv
           | .mem _ => false)
      then some a else none
  | .rmux w s f t =>
      -- branch folding: equal arms, or a select the checker knows
      if f == t && wq f == some w then some f
      else if kn s then
        match cv s with
        | .bv sv =>
          let a := if bv_nonzero sv then t else f
          if wq a == some w then some a else none
        | .mem _ => none
      else none
  | _ => none

/-! ## The certificate, the rebuild and the checker -/

/-- Why a binding that is neither kept nor folded still has a value in the new program. -/
inductive Just where
  /-- none: the binding is dropped, and nothing may read it -/
  | none
  /-- the new program computes the binding's own expression, renamed, at its new slot -/
  | cse
  /-- the binding's value is old slot `q`'s (`copyOf`) -/
  | copy (q : Nat)
deriving Inhabited, DecidableEq, Repr

structure OptCert where
  newSources : Array SourceDesc
  /-- old slot ↦ new slot -/
  pos        : Array Nat
  /-- old binding ↦ kept? -/
  keep       : Array Bool
  /-- old binding ↦ a constant? (only for a binding not kept) -/
  fold       : Array Bool := #[]
  /-- old binding ↦ why it has a value though neither kept nor folded -/
  just       : Array Just := #[]
deriving Inhabited

/-- The renaming: old slot ↦ new slot. -/
@[inline] def OptCert.σ (c : OptCert) (r : Nat) : Nat := c.pos.getD r 0

/-- The kept bindings, in order, renamed. -/
def keptRenamed (σ : Nat → Nat) : List (ResidualBinding × Bool) → List ResidualBinding
  | []                  => []
  | (b, true) :: rest   => { b with rhs := renameExpr σ b.rhs } :: keptRenamed σ rest
  | (_, false) :: rest  => keptRenamed σ rest

/-- The fixed rebuild: rename everything by `pos`, keep the bindings `keep` names. -/
def applyCert (R : ResidualProgram) (c : OptCert) : ResidualProgram :=
  { sources       := c.newSources
    bindings      := (keptRenamed c.σ (R.bindings.toList.zip c.keep.toList)).toArray
    outputs       := R.outputs.map (renameOutput c.σ)
    flopUpdates   := R.flopUpdates.map (renameFlop c.σ)
    memoryUpdates := R.memoryUpdates.map (renameMem c.σ) }

/-- A folded binding whose constant is `v`: the new source at `σ r` is a constant that
denotes `v` exactly (same width and value, and `v` reduced, as `mk_bv` makes it). -/
def constOK (c : OptCert) (r : Nat) : CertVal → Bool
  | .bv x => mk_bv x.width x.value == x && c.newSources[c.σ r]? == some (.const x.width x.value)
  | .mem _ => false

/-- Old binding `k` is folded: marked a constant and not kept. -/
@[inline] def isFold (c : OptCert) (k : Nat) : Bool :=
  !(c.keep.getD k false) && c.fold.getD k false

@[inline] def justOf (c : OptCert) (k : Nat) : Just := c.just.getD k .none

/-- Old binding `k` is aliased: neither kept nor folded, and justified. -/
@[inline] def isAlias (c : OptCert) (k : Nat) : Bool :=
  !(c.keep.getD k false) && !(c.fold.getD k false) && (justOf c k != .none)

/-- Old slot `r` holds a value the checker knows without running the program: a constant
source, or a folded binding. -/
def known (R : ResidualProgram) (c : OptCert) (r : Nat) : Bool :=
  if r < R.sources.size then
    match R.sources[r]? with
    | some (.const _ _) => true
    | _ => false
  else isFold c (r - R.sources.size)

/-- The checker's constant environment before the bindings: each source's value at a step
with no inputs and empty state, which for a constant source is its value at every step. -/
def constEnv (R : ResidualProgram) : SlotEnv :=
  sourceEnvArr R.sources #[] { flops := #[], mems := #[] }

/-- Reference `r` may be read below old slot `bound`: a source whose descriptor the new
sources hold at `σ r`, or an earlier binding that is kept, or folded with its constant
(from the checker's environment `cenv`) at `σ r`. -/
def refOK (R : ResidualProgram) (c : OptCert) (cenv : SlotEnv) (bound r : Nat) : Bool :=
  if r < R.sources.size then c.newSources[c.σ r]? == R.sources[r]?
  else r < bound && (c.keep.getD (r - R.sources.size) false ||
    (isFold c (r - R.sources.size) && constOK c r (denoteRef cenv r)) ||
    isAlias c (r - R.sources.size))

/-- The checker's next environment entry for old binding `k`: its constant when folded. -/
@[inline] def cval (c : OptCert) (cenv : SlotEnv) (k : Nat) (b : ResidualBinding) : CertVal :=
  if isFold c k then denoteExpr cenv b.rhs else badVal

/-- An aliased binding's reason holds: `cse`, its renamed expression is what the new program
computes at its new slot (`nr`: the renamed expressions of the kept bindings, in order);
`copy q`, its value is `q`'s and its new slot is `q`'s. -/
def aliasOK (R : ResidualProgram) (c : OptCert) (cenv : SlotEnv) (wenv : Array (Option Nat))
    (nr : Array ResidualExpr) (k : Nat) (rhs : ResidualExpr) : Bool :=
  let slot := R.sources.size + k
  match justOf c k with
  | .none => false
  | .cse =>
      (exprRefs rhs).all (refOK R c cenv slot) && decide (c.newSources.size ≤ c.σ slot) &&
      nr[c.σ slot - c.newSources.size]? == some (renameExpr c.σ rhs)
  | .copy q =>
      refOK R c cenv slot q && c.σ slot == c.σ q &&
      copyOf (known R c) (denoteRef cenv) (fun r => wenv.getD r none) rhs == some q

/-- Walk the bindings (`k` old index, `t` new index; the checker's constants `cenv`, static
widths `wenv` and the kept bindings' renamed expressions `nr`): a kept binding reads only
references `refOK` below it, and lands at new slot `newSources.size + t`; a folded binding
reads only references the checker knows; an aliased binding's reason holds. -/
def scanOK (R : ResidualProgram) (c : OptCert) :
    List (ResidualBinding × Bool) → Nat → Nat → SlotEnv → Array (Option Nat) →
    Array ResidualExpr → Bool
  | [], _, _, _, _, _ => true
  | (b, true) :: rest, k, t, cenv, wenv, nr =>
      (exprRefs b.rhs).all (refOK R c cenv (R.sources.size + k)) &&
      c.σ (R.sources.size + k) == c.newSources.size + t &&
      scanOK R c rest (k + 1) (t + 1) (cenv.push (cval c cenv k b)) (wenv.push (exprWidth b.rhs))
        (nr.push (renameExpr c.σ b.rhs))
  | (b, false) :: rest, k, t, cenv, wenv, nr =>
      (if isFold c k then (exprRefs b.rhs).all (known R c)
       else if isAlias c k then aliasOK R c cenv wenv nr k b.rhs
       else true) &&
      scanOK R c rest (k + 1) t (cenv.push (cval c cenv k b)) (wenv.push (exprWidth b.rhs)) nr

/-- The checker's constants after the bindings (the environment `scanOK` ends with). -/
def cenvRun (c : OptCert) : List ResidualBinding → Nat → SlotEnv → SlotEnv
  | [], _, cenv => cenv
  | b :: rest, k, cenv => cenvRun c rest (k + 1) (cenv.push (cval c cenv k b))

/-- The checker. Linear in the program; no hash map, nothing it trusts from the optimizer. -/
def validate (R : ResidualProgram) (c : OptCert) : Bool :=
  let b := R.sources.size + R.bindings.size
  let cenv := cenvRun c R.bindings.toList 0 (constEnv R)
  c.keep.size == R.bindings.size &&
  scanOK R c (R.bindings.toList.zip c.keep.toList) 0 0 (constEnv R) (R.sources.map srcWidth) #[] &&
  R.outputs.all (fun o => refOK R c cenv b o.slot) &&
  R.flopUpdates.all (fun f => (flopRefs f).all (refOK R c cenv b)) &&
  R.memoryUpdates.all (fun m => (memRefs m).all (refOK R c cenv b))

/-! ## The optimizer (unproved: its every output is checked) -/

/-- Fold `f` over the references `exprRefs e` lists, without building the list. -/
@[inline] def foldRefs {α : Type} (f : α → Nat → α) (init : α) : ResidualExpr → α
  | .rsum _ _ args | .rmult _ args | .rand _ args | .rorBits _ args | .rxor _ args
  | .rredOr _ args | .req _ args | .rshl _ args | .rmuxN _ args => args.foldl f init
  | .rnot _ a => f init a
  | .rult _ a b | .rugt _ a b | .rslt _ a b | .rsgt _ a b | .rsra _ a b | .rsext _ a b
  | .rgetMask _ a b | .rudiv _ a b | .rsdiv _ a b => f (f init a) b
  | .rsetMask _ a m v => f (f (f init a) m) v
  | .rmux _ s t e => f (f (f init s) t) e
  | .rmemRead _ m a en => f (f (f init m) a) en
  | .rmemWrite m a d en => f (f (f (f init m) a) d) en
  | .rmemWriteBE _ _ m a d be => f (f (f (f init m) a) d) be
  | .rmemLoad _ _ m bus en => f (f (f init m) bus) en
  | .rmemToBus _ _ m => f init m

/-- A hash of `renameExpr f e`, computed without building it: the constructor, its other
fields and its operands' new names. Equal renamed expressions hash equal. -/
@[specialize] def hashUnder (f : Nat → Nat) (e : ResidualExpr) : UInt64 :=
  let mx := fun (h : UInt64) (n : Nat) => mixHash h (hash n)
  let ha := fun (h : UInt64) (args : Array Nat) => args.foldl (fun h r => mx h (f r)) (mx h args.size)
  match e with
  | .rsum w n args             => ha (mx (mx 0 w) n) args
  | .rmult w args              => ha (mx 1 w) args
  | .rand w args               => ha (mx 2 w) args
  | .rorBits w args            => ha (mx 3 w) args
  | .rxor w args               => ha (mx 4 w) args
  | .rredOr w args             => ha (mx 5 w) args
  | .req w args                => ha (mx 6 w) args
  | .rshl w args               => ha (mx 7 w) args
  | .rmuxN w args              => ha (mx 8 w) args
  | .rnot w a                  => mx (mx 9 w) (f a)
  | .rult w a b                => mx (mx (mx 10 w) (f a)) (f b)
  | .rugt w a b                => mx (mx (mx 11 w) (f a)) (f b)
  | .rslt w a b                => mx (mx (mx 12 w) (f a)) (f b)
  | .rsgt w a b                => mx (mx (mx 13 w) (f a)) (f b)
  | .rsra w a b                => mx (mx (mx 14 w) (f a)) (f b)
  | .rsext w a b               => mx (mx (mx 15 w) (f a)) (f b)
  | .rgetMask w a b            => mx (mx (mx 16 w) (f a)) (f b)
  | .rudiv w a b               => mx (mx (mx 17 w) (f a)) (f b)
  | .rsdiv w a b               => mx (mx (mx 18 w) (f a)) (f b)
  | .rsetMask w a m v          => mx (mx (mx (mx 19 w) (f a)) (f m)) (f v)
  | .rmux w s t u              => mx (mx (mx (mx 20 w) (f s)) (f t)) (f u)
  | .rmemRead w m a en         => mx (mx (mx (mx 21 w) (f m)) (f a)) (f en)
  | .rmemWrite m a d en        => mx (mx (mx (mx 22 (f m)) (f a)) (f d)) (f en)
  | .rmemWriteBE w bw m a d be => mx (mx (mx (mx (mx (mx 23 w) bw) (f m)) (f a)) (f d)) (f be)
  | .rmemLoad w n m bus en     => mx (mx (mx (mx (mx 24 w) n) (f m)) (f bus)) (f en)
  | .rmemToBus w n m           => mx (mx (mx 25 w) n) (f m)

/-- A hash of every bit of `v`, without allocating: its low 64 bits and its residue modulo
the prime 2^31 - 1. The low 64 bits alone (all `hash (v : Int)` reads) collide on wide
constants: SoomRV's SoC has 442 constants of one width that share them. -/
@[inline] def intHash (v : Int) : UInt64 :=
  mixHash (Int64.ofInt v).toUInt64 (Int64.ofInt (v % 2147483647)).toUInt64

/-- A constant's hash (its width and value), as a small `Nat`. -/
@[inline] def constHash (w : Nat) (v : Int) : Nat :=
  (mixHash (hash w) (intHash v) >>> 2).toNat

/-- A flat open-addressing table in one array: `slots[2i]` is an entry's payload + 1 (0:
empty) and `slots[2i+1]` its hash; the capacity is `slots.size / 2`, a power of two, and at
most half of it is used. The index where probing from `i` stops: an empty slot, or the
entry with hash `h` whose payload `same` accepts. -/
@[specialize] def flatFind (slots : Array Nat) (mask h : Nat) (same : Nat → Bool) :
    Nat → Nat → Nat
  | 0, i => i
  | fuel + 1, i =>
    let p := slots[2 * i]!
    if p == 0 || (slots[2 * i + 1]! == h && same (p - 1)) then i
    else flatFind slots mask h same fuel ((i + 1) &&& mask)

/-- The table with twice the capacity and the same entries. -/
def flatGrow (slots : Array Nat) : Array Nat := Id.run do
  let ncap := slots.size
  let mask := ncap - 1
  let mut ns : Array Nat := Array.replicate (2 * ncap) 0
  for i in [0:slots.size / 2] do
    let p := slots[2 * i]!
    if p != 0 then
      let h := slots[2 * i + 1]!
      let j := flatFind ns mask h (fun _ => false) ncap (h &&& mask)
      ns := (ns.set! (2 * j) p).set! (2 * j + 1) h
  return ns

/-- Look `h` up in a flat table (`same` decides a hash match): the slot index where an equal
entry is, or where a new one goes. -/
@[inline] def flatLookup (slots : Array Nat) (h : Nat) (same : Nat → Bool) : Nat :=
  let cap := slots.size / 2
  flatFind slots (cap - 1) h same cap (h &&& (cap - 1))

/-- Put payload `p` with hash `h` at index `i` (from `flatLookup`), growing the table when it
is half full; `n` counts the entries. -/
@[inline] def flatPut (slots : Array Nat) (n i h p : Nat) : Array Nat × Nat :=
  let slots := (slots.set! (2 * i) (p + 1)).set! (2 * i + 1) h
  if 4 * (n + 1) ≥ slots.size then (flatGrow slots, n + 1) else (slots, n + 1)

/-- One forward pass, the checker's way, then liveness, then the new slots.

The forward pass folds a binding all of whose operands are known (its value must be a
reduced `BV`, `constOK`), records static widths, and otherwise marks a copy (`copyOf`) or
a repeated expression. Every slot gets a class, and slots of one class get one new slot:
a constant's class is its width and value (folded constants included), a copy's is its
operand's, a repeated expression's is its first binding's, and every other slot is its own.
An expression's key for repetition is the expression with its operands renamed to their
classes (an earlier entry's with the classes it saw: the slots before it).

Liveness then runs from the outputs, flops and memories back: a folded binding reads
nothing, a copy reads its operand, a repeated expression its first binding and its own
operands (the checker reads them), any other binding its operands. A binding that is not
read gets no reason (`Just.none`) and is dropped.

Speed (the certificate does not depend on it): constant classes and repeated expressions
are found in flat open-addressing tables (`flatFind`) that hold slot numbers and hashes
(`constHash`, `hashUnder`), not keys; a constant's new slot is its class's, so the new
slots need no second table; references are folded over (`foldRefs`), not listed. -/
def optimizeRaw (R : ResidualProgram) : OptCert := Id.run do
  let S := R.sources.size
  let B := R.bindings.size
  let mut cenv : SlotEnv := constEnv R
  let mut kn : Array Bool := R.sources.map fun sd => match sd with
    | .const _ _ => true
    | _ => false
  -- static widths: a source's from its descriptor, a binding's as `w + 1` (0: none)
  let mut bw : Array Nat := Array.mkEmpty B
  let mut fold : Array Bool := Array.replicate B false
  let mut just : Array Just := Array.replicate B .none
  let mut first : Array Nat := Array.replicate B 0
  -- constant classes: a table of class representatives (the first constant source, or the
  -- first folded binding's slot, with the class's width and value)
  let mut ct : Array Nat := Array.replicate 2048 0
  let mut ctN : Nat := 0
  let mut cls : Array Nat := Array.range S
  for j in [0:S] do
    match R.sources[j]! with
    | .const w v =>
      let h := constHash w v
      let i := flatLookup ct h fun p => match R.sources[p]! with
        | .const w' v' => w' == w && v' == v
        | _ => false
      let p := ct[2 * i]!
      if p != 0 then cls := cls.set! j (p - 1)
      else (ct, ctN) := flatPut ct ctN i h j
    | _ => pure ()
  -- repeated expressions: a table of first bindings
  let mut st : Array Nat := Array.replicate (2 * 2 ^ (Nat.log2 (2 * B + 1024) + 1)) 0
  let mut stN : Nat := 0
  for k in [0:B] do
    let rhs := R.bindings[k]!.rhs
    let canFold : Bool := foldRefs (fun acc r => acc && kn.getD r false) true rhs
    let v : CertVal := if canFold then denoteExpr cenv rhs else badVal
    let ok : Bool := canFold && (match v with
      | .bv x => mk_bv x.width x.value == x
      | .mem _ => false)
    if ok then
      fold := fold.set! k true
      match v with
      | .bv x =>
        let h := constHash x.width x.value
        let i := flatLookup ct h fun p =>
          if p < S then (match R.sources[p]! with
            | .const w' v' => w' == x.width && v' == x.value
            | _ => false)
          else (match cenv.getD p badVal with
            | .bv y => y.width == x.width && y.value == x.value
            | .mem _ => false)
        let p := ct[2 * i]!
        if p != 0 then cls := cls.push (p - 1)
        else
          (ct, ctN) := flatPut ct ctN i h (S + k)
          cls := cls.push (S + k)
      | .mem _ => cls := cls.push (S + k)
    else
      let wq := fun (r : Nat) => if r < S then srcWidth R.sources[r]! else
        (let b := bw.getD (r - S) 0; if b == 0 then none else some (b - 1))
      match copyOf (fun r => kn.getD r false) (fun r => cenv.getD r badVal) wq rhs with
      | some q =>
        just := just.set! k (.copy q)
        cls := cls.push (cls.getD q q)
      | none =>
        let f := fun r => cls.getD r r
        let h := (hashUnder f rhs >>> 2).toNat
        let i := flatLookup st h fun p =>
          renameExpr (fun r => if r < S + p then cls.getD r r else r) R.bindings[p]!.rhs ==
            renameExpr f rhs
        let hit := st[2 * i]!
        if hit != 0 then
          just := just.set! k .cse
          first := first.set! k (hit - 1)
          cls := cls.push (S + (hit - 1))
        else
          (st, stN) := flatPut st stN i h k
          cls := cls.push (S + k)
    kn := kn.push ok
    cenv := cenv.push (if ok then v else badVal)
    bw := bw.push (match exprWidth rhs with | some w => w + 1 | none => 0)
  let mark (live : Array Bool) (r : Nat) : Array Bool :=
    if r < live.size then live.set! r true else live
  let mut live : Array Bool := Array.replicate (S + B) false
  for o in R.outputs do live := mark live o.slot
  for f in R.flopUpdates do
    for r in flopRefs f do live := mark live r
  for m in R.memoryUpdates do
    for r in memRefs m do live := mark live r
  -- bindings read only earlier slots, so one pass from the last binding back is enough
  for kk in [0:B] do
    let k := B - 1 - kk
    if live[S + k]! && !fold[k]! then
      match just[k]! with
      | .copy q => live := mark live q
      | .cse =>
        live := mark live (S + first[k]!)
        live := foldRefs mark live R.bindings[k]!.rhs
      | .none =>
        live := foldRefs mark live R.bindings[k]!.rhs
  -- a constant takes its class's new slot, the first time one of the class is read:
  -- `clsPos` holds it (S + B: not placed yet)
  let mut newSources : Array SourceDesc := #[]
  let mut pos : Array Nat := Array.replicate (S + B) 0
  let mut clsPos : Array Nat := Array.replicate (S + B) (S + B)
  for j in [0:S] do
    if live[j]! then
      let sd := R.sources[j]!
      match sd with
      | .const _ _ =>
        let c := cls[j]!
        let p := clsPos[c]!
        if p < S + B then pos := pos.set! j p
        else
          clsPos := clsPos.set! c newSources.size
          pos := pos.set! j newSources.size
          newSources := newSources.push sd
      | _ =>
        pos := pos.set! j newSources.size
        newSources := newSources.push sd
  -- a folded binding something reads becomes a constant source, shared like the others
  for k in [0:B] do
    if live[S + k]! && fold[k]! then
      match cenv[S + k]! with
      | .bv x =>
        let c := cls[S + k]!
        let p := clsPos[c]!
        if p < S + B then pos := pos.set! (S + k) p
        else
          clsPos := clsPos.set! c newSources.size
          pos := pos.set! (S + k) newSources.size
          newSources := newSources.push (.const x.width x.value)
      | .mem _ => pure ()
  let S' := newSources.size
  let mut keep : Array Bool := Array.replicate B false
  let mut t := 0
  for k in [0:B] do
    if live[S + k]! && !fold[k]! && just[k]! == .none then
      keep := keep.set! k true
      pos := pos.set! (S + k) (S' + t)
      t := t + 1
  -- an alias takes its target's new slot (targets are earlier); an unread binding no reason
  for k in [0:B] do
    if live[S + k]! && !fold[k]! then
      match just[k]! with
      | .copy q => pos := pos.set! (S + k) pos[q]!
      | .cse => pos := pos.set! (S + k) pos[S + first[k]!]!
      | .none => pure ()
    else
      just := just.set! k .none
  return { newSources, pos, keep, fold, just }

/-- The optimized program, and whether the checker accepted the optimizer's certificate. -/
def optimizeWith (R : ResidualProgram) : ResidualProgram × Bool :=
  let c := optimizeRaw R
  if validate R c then (applyCert R c, true) else (R, false)

/-- The optimized program; the original when the checker refuses. -/
def optimize (R : ResidualProgram) : ResidualProgram := (optimizeWith R).1

end ResidualOpt
end Compiler

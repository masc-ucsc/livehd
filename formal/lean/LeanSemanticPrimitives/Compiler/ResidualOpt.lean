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
which bindings are constants; `newSources` are the new sources. The rebuild renames every
reference by `pos` and drops the bindings not kept. The checker requires, for every
reference a kept binding, an output, a flop or a memory reads:
- a source: the new sources hold the same descriptor at its new slot (so identical
  constants may share one new source);
- a binding: it is earlier (for a binding's operands) and either kept, or folded with the
  new source at its new slot the constant the CHECKER computed for it;
and that the kept bindings land, in order, right after the new sources.

The checker computes every folded binding's constant itself, in one pass: a folded
binding may read only constant sources and earlier folded bindings, whose values the
checker already knows, and `denoteExpr` on those values is the binding's value on every
step (it reads nothing else).

## The passes so far

- constant folding and propagation: a binding whose operands are all constants (constant
  sources, or bindings folded before it) is evaluated once, and its readers read a
  constant source instead;
- dead-code elimination: a binding nothing reachable from an output, a flop or a memory
  reads is dropped, and so is every source nothing reads (a folded binding reads
  nothing: its operands may die with it);
- constant sharing: constant sources with the same width and value become one, folded
  constants included.

Mathlib-free; the proofs are in `ResidualOptSound`.
-/
import LeanSemanticPrimitives.Compiler.ResidualSemantics
import Std.Data.HashMap

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

/-! ## The certificate, the rebuild and the checker -/

structure OptCert where
  newSources : Array SourceDesc
  /-- old slot ↦ new slot -/
  pos        : Array Nat
  /-- old binding ↦ kept? -/
  keep       : Array Bool
  /-- old binding ↦ a constant? (only for a binding not kept) -/
  fold       : Array Bool := #[]
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
    (isFold c (r - R.sources.size) && constOK c r (denoteRef cenv r)))

/-- The checker's next environment entry for old binding `k`: its constant when folded. -/
@[inline] def cval (c : OptCert) (cenv : SlotEnv) (k : Nat) (b : ResidualBinding) : CertVal :=
  if isFold c k then denoteExpr cenv b.rhs else badVal

/-- Walk the bindings (`k` old index, `t` new index, `cenv` the checker's constants): a kept
binding reads only references `refOK` below it, and lands at new slot
`newSources.size + t`; a folded binding reads only references the checker knows. -/
def scanOK (R : ResidualProgram) (c : OptCert) :
    List (ResidualBinding × Bool) → Nat → Nat → SlotEnv → Bool
  | [], _, _, _ => true
  | (b, true) :: rest, k, t, cenv =>
      (exprRefs b.rhs).all (refOK R c cenv (R.sources.size + k)) &&
      c.σ (R.sources.size + k) == c.newSources.size + t &&
      scanOK R c rest (k + 1) (t + 1) (cenv.push (cval c cenv k b))
  | (b, false) :: rest, k, t, cenv =>
      (!isFold c k || (exprRefs b.rhs).all (known R c)) &&
      scanOK R c rest (k + 1) t (cenv.push (cval c cenv k b))

/-- The checker's constants after the bindings (the environment `scanOK` ends with). -/
def cenvRun (c : OptCert) : List ResidualBinding → Nat → SlotEnv → SlotEnv
  | [], _, cenv => cenv
  | b :: rest, k, cenv => cenvRun c rest (k + 1) (cenv.push (cval c cenv k b))

/-- The checker. Linear in the program; no hash map, nothing it trusts from the optimizer. -/
def validate (R : ResidualProgram) (c : OptCert) : Bool :=
  let b := R.sources.size + R.bindings.size
  let cenv := cenvRun c R.bindings.toList 0 (constEnv R)
  c.keep.size == R.bindings.size &&
  scanOK R c (R.bindings.toList.zip c.keep.toList) 0 0 (constEnv R) &&
  R.outputs.all (fun o => refOK R c cenv b o.slot) &&
  R.flopUpdates.all (fun f => (flopRefs f).all (refOK R c cenv b)) &&
  R.memoryUpdates.all (fun m => (memRefs m).all (refOK R c cenv b))

/-! ## The optimizer (unproved: its every output is checked) -/

/-- Constant folding and propagation, the checker's way (one pass, the same environment),
then liveness from the outputs, flops and memories, then constant sharing, then the
renaming. -/
def optimizeRaw (R : ResidualProgram) : OptCert := Id.run do
  let S := R.sources.size
  let B := R.bindings.size
  -- folding: a binding all of whose operands are known; its value must be a reduced `BV`
  -- (`constOK`), else it stays a binding
  let mut cenv : SlotEnv := constEnv R
  let mut kn : Array Bool := R.sources.map fun sd => match sd with
    | .const _ _ => true
    | _ => false
  let mut fold : Array Bool := Array.replicate B false
  for k in [0:B] do
    let rhs := R.bindings[k]!.rhs
    let canFold : Bool := (exprRefs rhs).all fun r => kn.getD r false
    let v : CertVal := if canFold then denoteExpr cenv rhs else badVal
    let ok : Bool := canFold && (match v with
      | .bv x => mk_bv x.width x.value == x
      | .mem _ => false)
    fold := fold.set! k ok
    kn := kn.push ok
    cenv := cenv.push (if ok then v else badVal)
  let mark (live : Array Bool) (r : Nat) : Array Bool :=
    if r < live.size then live.set! r true else live
  let mut live : Array Bool := Array.replicate (S + B) false
  for o in R.outputs do live := mark live o.slot
  for f in R.flopUpdates do
    for r in flopRefs f do live := mark live r
  for m in R.memoryUpdates do
    for r in memRefs m do live := mark live r
  -- bindings read only earlier slots, so one pass from the last binding back is enough;
  -- a folded binding becomes a constant and reads nothing
  for kk in [0:B] do
    let k := B - 1 - kk
    if live[S + k]! && !fold[k]! then
      for r in exprRefs R.bindings[k]!.rhs do live := mark live r
  let mut newSources : Array SourceDesc := #[]
  let mut pos : Array Nat := Array.replicate (S + B) 0
  let mut constAt : Std.HashMap (Nat × Int) Nat := {}
  for j in [0:S] do
    if live[j]! then
      let sd := R.sources[j]!
      match sd with
      | .const w v =>
        match constAt.get? (w, v) with
        | some t => pos := pos.set! j t
        | none =>
          constAt := constAt.insert (w, v) newSources.size
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
        match constAt.get? (x.width, x.value) with
        | some t => pos := pos.set! (S + k) t
        | none =>
          constAt := constAt.insert (x.width, x.value) newSources.size
          pos := pos.set! (S + k) newSources.size
          newSources := newSources.push (.const x.width x.value)
      | .mem _ => pure ()
  let S' := newSources.size
  let mut keep : Array Bool := Array.replicate B false
  let mut t := 0
  for k in [0:B] do
    if live[S + k]! && !fold[k]! then
      keep := keep.set! k true
      pos := pos.set! (S + k) (S' + t)
      t := t + 1
  return { newSources, pos, keep, fold }

/-- The optimized program, and whether the checker accepted the optimizer's certificate. -/
def optimizeWith (R : ResidualProgram) : ResidualProgram × Bool :=
  let c := optimizeRaw R
  if validate R c then (applyCert R c, true) else (R, false)

/-- The optimized program; the original when the checker refuses. -/
def optimize (R : ResidualProgram) : ResidualProgram := (optimizeWith R).1

end ResidualOpt
end Compiler

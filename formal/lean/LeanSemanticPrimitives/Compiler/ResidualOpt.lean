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

`pos` maps every OLD slot to its NEW slot; `keep` says which bindings stay; `newSources`
are the new sources. The rebuild renames every reference by `pos` and drops the bindings
not kept. The checker requires, for every reference a kept binding, an output, a flop or a
memory reads:
- a source: the new sources hold the same descriptor at its new slot (so identical
  constants may share one new source);
- a binding: it is earlier (for a binding's operands) and kept;
and that the kept bindings land, in order, right after the new sources.

## The passes so far

- dead-code elimination: a binding nothing reachable from an output, a flop or a memory
  reads is dropped, and so is every source nothing reads;
- constant sharing: constant sources with the same width and value become one.

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

/-- Reference `r` may be read below old slot `bound`: a source whose descriptor the new
sources hold at `σ r`, or an earlier binding that is kept. -/
def refOK (R : ResidualProgram) (c : OptCert) (bound r : Nat) : Bool :=
  if r < R.sources.size then c.newSources[c.σ r]? == R.sources[r]?
  else r < bound && c.keep.getD (r - R.sources.size) false

/-- Walk the bindings (`k` old index, `t` new index): a kept binding reads only references
`refOK` below it, and lands at new slot `newSources.size + t`. -/
def scanOK (R : ResidualProgram) (c : OptCert) : List (ResidualBinding × Bool) → Nat → Nat → Bool
  | [], _, _ => true
  | (b, true) :: rest, k, t =>
      (exprRefs b.rhs).all (refOK R c (R.sources.size + k)) &&
      c.σ (R.sources.size + k) == c.newSources.size + t &&
      scanOK R c rest (k + 1) (t + 1)
  | (_, false) :: rest, k, t => scanOK R c rest (k + 1) t

/-- The checker. Linear in the program; no hash map, nothing it trusts from the optimizer. -/
def validate (R : ResidualProgram) (c : OptCert) : Bool :=
  let b := R.sources.size + R.bindings.size
  c.keep.size == R.bindings.size &&
  scanOK R c (R.bindings.toList.zip c.keep.toList) 0 0 &&
  R.outputs.all (fun o => refOK R c b o.slot) &&
  R.flopUpdates.all (fun f => (flopRefs f).all (refOK R c b)) &&
  R.memoryUpdates.all (fun m => (memRefs m).all (refOK R c b))

/-! ## The optimizer (unproved: its every output is checked) -/

/-- Liveness from the outputs, flops and memories, then constant sharing, then the
renaming. -/
def optimizeRaw (R : ResidualProgram) : OptCert := Id.run do
  let S := R.sources.size
  let B := R.bindings.size
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
    if live[S + k]! then
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
  let S' := newSources.size
  let mut keep : Array Bool := Array.replicate B false
  let mut t := 0
  for k in [0:B] do
    if live[S + k]! then
      keep := keep.set! k true
      pos := pos.set! (S + k) (S' + t)
      t := t + 1
  return { newSources, pos, keep }

/-- The optimized program, and whether the checker accepted the optimizer's certificate. -/
def optimizeWith (R : ResidualProgram) : ResidualProgram × Bool :=
  let c := optimizeRaw R
  if validate R c then (applyCert R c, true) else (R, false)

/-- The optimized program; the original when the checker refuses. -/
def optimize (R : ResidualProgram) : ResidualProgram := (optimizeWith R).1

end ResidualOpt
end Compiler

/-
# `interpretDesign` — the SOURCE semantics

Step 3.  This is the right-hand side of `compileDesign_correct`, and it is the
*spec*: it says what a `DesignCert` means, in terms of the interpreter that
already exists.

It **reuses `evalGraphG`** rather than duplicating graph semantics.  That is why
B2 (`interpreter-value-polymorphic`) is this branch's foundation and not its
sibling: one generic traversal, instantiated at `CertVal`, plus every proof in
`GraphRefine` for free.

Note the deliberate asymmetry with the target: the source environment is a
nested `Nat → CertVal` built by `envSetG`, so a lookup costs O(N) and evaluating
every slot costs O(N²).  That is *fine here* — `interpretDesign` is never
executed.  It is only ever reasoned about, through `evalGraphG_char`, which
materialises no lookups at all.  `denoteResidual` is the one that runs, and it
uses dense arrays.

## Clocks

A step takes an edge vector `e : ClockEdges` — which declared clock domains fire
in this step.  A flop or memory whose domain is quiet HOLDS; an asynchronous
reset acts regardless.  The one-clock semantics every earlier certificate was
written against is kept below VERBATIM as `interpretDesignLegacy`, and
`interpretDesign_allEdges` proves that under the all-fire stimulus the two
coincide for every design whose clock ordinals are declared — so adding clocks
changed the meaning of no existing certificate, as a theorem rather than a claim.
-/
import LeanSemanticPrimitives.Compiler.DesignCertWF
import LeanSemanticPrimitives.Compiler.Runtime

namespace Compiler

/-- Source-side slot environment: sources get their runtime value, everything
else is irrelevant (the traversal overwrites it). -/
def srcEnv (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : Nat → CertVal :=
  fun k =>
    match D.sources[k]? with
    | some sd => sourceValue i s sd
    | none    => badVal

/-- Source-side flop rule.  Polarity is spelled with `xor` here and as an
explicit `if` on the target side, so `flopNext_agree` genuinely checks reset
polarity rather than unfolding to it.  (An earlier draft also spelled the two
boolean branches as `match` vs `if`; that difference validates nothing, only
costing proof effort, so it was dropped.  Reset PRIORITY over enable, the reset
VALUE, and the old-state fallback are all still checked, because the source and
target read them from independently constructed descriptors.)

`resetValue` and `resetActiveLow` are read here because `graph/cell.cpp` gives a
Flop eight pins; the manual emitter read three with no `else`, silently dropping
`initial` (reset value became hardcoded 0) and conflating `negreset` with
`reset_pin` (polarity inverted). Transcribing the emitter would have turned that
bug into a theorem.

CLOCKS.  `edge` is whether this flop's domain fires.  A quiet domain HOLDS —
unless the reset is ASYNCHRONOUS and asserted, in which case it resets regardless
of the edge (that is what asynchronous means); a synchronous reset is sampled at
the edge like `din` and so must NOT act in a quiet step.  Under a firing edge both
cases collapse to the one-clock rule (`srcFlopNext_fires`). -/
def srcFlopNext (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (f : FlopDesc) : BV :=
  let resetActive : Bool :=
    match f.resetPin with
    | none   => false
    | some r => xor f.resetActiveLow (bv_nonzero (rho r).asBV)
  let edge : Bool := fires e f.clock
  if resetActive && (edge || f.asyncReset) then mk_bv f.width f.resetValue
  else
    let enabled : Bool :=
      match f.enable with
      | none    => true
      | some en => bv_nonzero (rho en).asBV
    if edge && enabled then bv_resize f.width (rho f.din).asBV
    else s.flops[idx]?.getD (mk_bv f.width 0)

/-- Source-side memory rule: the post-write image when the memory's domain
fires, the pre-step image otherwise.  (A write port whose enable is low already
leaves the image unchanged; the hold is for the write that WOULD have happened
had the edge come.) -/
def srcMemNext (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (m : MemoryDesc) : Int → BV :=
  if fires e m.clock then (rho m.nextImg).asMem
  else s.mems[idx]?.getD (fun _ => mk_bv 0 0)

/-- What a `DesignCert` MEANS, for one step in which the clocks `e` fire. -/
def interpretDesign (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    RuntimeResult :=
  let G   := D.toGraphCert
  let rho := evalGraphG G.topo G (srcEnv D i s)
  { outputs   := D.outputs.map fun o => bv_resize o.width (rho o.slot).asBV
    nextState :=
      { flops := D.flops.mapIdx fun idx f => srcFlopNext rho e s idx f
        mems  := D.memories.mapIdx fun idx m => srcMemNext rho e s idx m } }

/-- Runtime well-formedness: the state and input have the shape `D` expects.
Kept MINIMAL on purpose — every hypothesis added here is meaning subtracted from
`compileDesign_correct`. -/
structure RuntimeWF (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : Prop where
  flopsSized : s.flops.size = D.flops.size
  memsSized  : s.mems.size  = D.memories.size

--------------------------------------------------------------------------------
-- The one-clock semantics, kept verbatim, and conservativity
--------------------------------------------------------------------------------

/-- The flop rule as it stood before clock provenance: every step is an edge. -/
def srcFlopNextLegacy (rho : Nat → CertVal) (s : RuntimeState) (idx : Nat) (f : FlopDesc) : BV :=
  let resetActive : Bool :=
    match f.resetPin with
    | none   => false
    | some r => xor f.resetActiveLow (bv_nonzero (rho r).asBV)
  if resetActive then mk_bv f.width f.resetValue
  else
    let enabled : Bool :=
      match f.enable with
      | none   => true
      | some e => bv_nonzero (rho e).asBV
    if enabled then bv_resize f.width (rho f.din).asBV
    else s.flops[idx]?.getD (mk_bv f.width 0)

/-- `interpretDesign` as it stood before clock provenance. -/
def interpretDesignLegacy (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  let G   := D.toGraphCert
  let rho := evalGraphG G.topo G (srcEnv D i s)
  { outputs   := D.outputs.map fun o => bv_resize o.width (rho o.slot).asBV
    nextState :=
      { flops := D.flops.mapIdx fun idx f => srcFlopNextLegacy rho s idx f
        mems  := D.memories.map fun m => (rho m.nextImg).asMem } }

theorem srcFlopNext_fires (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (f : FlopDesc) (h : fires e f.clock = true) :
    srcFlopNext rho e s idx f = srcFlopNextLegacy rho s idx f := by
  simp [srcFlopNext, srcFlopNextLegacy, h]

theorem srcMemNext_fires (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (m : MemoryDesc) (h : fires e m.clock = true) :
    srcMemNext rho e s idx m = (rho m.nextImg).asMem := by
  simp [srcMemNext, h]

/-- **Conservativity.**  For every design whose clock ordinals are declared, the
all-fire stimulus recovers the one-clock semantics exactly — at every input and
every state.  Every certificate emitted before clocks existed satisfies the
hypotheses trivially (`clocks` defaults to one domain, every ordinal to 0), so
this is the theorem that adding clocks changed the meaning of none of them. -/
theorem interpretDesign_allEdges (D : DesignCert)
    (hf : ∀ f ∈ D.flops, f.clock < D.clocks.size)
    (hm : ∀ m ∈ D.memories, m.clock < D.clocks.size)
    (i : RuntimeInput) (s : RuntimeState) :
    interpretDesign D (allEdges D) i s = interpretDesignLegacy D i s := by
  simp only [interpretDesign, interpretDesignLegacy]
  congr 1
  congr 1
  · apply Array.ext
    · simp
    · intro idx h1 h2
      simp only [Array.getElem_mapIdx]
      exact srcFlopNext_fires _ _ _ _ _ (fires_allEdges (hf _ (Array.getElem_mem _)))
  · apply Array.ext
    · simp
    · intro idx h1 h2
      simp only [Array.getElem_mapIdx, Array.getElem_map]
      exact srcMemNext_fires _ _ _ _ _ (fires_allEdges (hm _ (Array.getElem_mem _)))

end Compiler

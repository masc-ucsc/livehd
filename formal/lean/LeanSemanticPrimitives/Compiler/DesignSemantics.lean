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
bug into a theorem. -/
def srcFlopNext (rho : Nat → CertVal) (s : RuntimeState) (idx : Nat) (f : FlopDesc) : BV :=
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

/-- What a `DesignCert` MEANS. -/
def interpretDesign (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  let G   := D.toGraphCert
  let rho := evalGraphG G.topo G (srcEnv D i s)
  { outputs   := D.outputs.map fun o => bv_resize o.width (rho o.slot).asBV
    nextState :=
      { flops := D.flops.mapIdx fun idx f => srcFlopNext rho s idx f
        mems  := D.memories.map fun m => (rho m.nextImg).asMem } }

/-- Runtime well-formedness: the state and input have the shape `D` expects.
Kept MINIMAL on purpose — every hypothesis added here is meaning subtracted from
`compileDesign_correct`. -/
structure RuntimeWF (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : Prop where
  flopsSized : s.flops.size = D.flops.size
  memsSized  : s.mems.size  = D.memories.size

end Compiler

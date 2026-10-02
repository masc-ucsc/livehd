/-
# `ReifyProof` — the last mile, as a KERNEL-CHECKED theorem

`reify_design D as F` (in `ReifyGen`) emits the straight-line definition `F` and
stops there.  `Reify.lean`'s header has always described a reifier that emits
"a proof that it equals `denoteResidual R`", but no such proof was ever
generated: the `proof` gate was `na` for every design in the corpus, and the
only per-design theorem that existed — `<Top>_step_correct` in the emitted
certificate — is about `compileAndRun`, i.e. about the COMPILER, and says
nothing about the reified artifact that actually runs.

`prove_reified D as F` closes that gap.  It emits, for the reified `F`:

    <F>_eq_compileAndRun : ∀ i st, F i st = compileAndRun D i st
    <F>_correct          : ∀ i st, F i st = interpretDesign D i st

The second is the one that matters: the straight-line definition agrees with the
SOURCE semantics, for every input and every state.  Not sampled — quantified.

## Why the proof has the shape it has

Three facts about kernel reduction decide everything here, and all three were
measured rather than assumed:

  * `compileDesign D` DOES reduce, at least at this size.  The emitted
    certificates say it does not
    (*"`decide` is not usable — `Array.map` is not kernel-reducible"*) and reach
    for `native_decide`, which adds `ofReduceBool` to the axiom set.  That is
    true of `decide`, which must evaluate a `Decidable` instance, but plain
    `rfl` reduces the match directly and succeeds.  So `compilesOk D = true` is
    proved here by `rfl`, and the emitted theorems depend on NO axiom beyond
    `propext`, `Classical.choice` and `Quot.sound`.
  * The `ResidualProgram`'s PROJECTIONS reduce, but evaluating the `Array.map`s
    inside them does not — `Array.map` is well-founded recursion, so the kernel
    will not unfold it.  Hence `R.outputs = D.outputs.map compileOutput` is
    `rfl` (a projection) while `R.outputs.size = 1` is not (a map).  The
    emitted lemmas stop at the projection and let `simp` evaluate the map, which
    it can do on a literal array.
  * `simp` will not reduce `match compileDesign D with ...` on its own, so the
    witness `compileDesign D = .ok R` has to be handed to it explicitly.

That leaves exactly one thing the metaprogram must write out: the compiled
BINDINGS, as a literal, so `runBindings` has a concrete list to walk.  It is
checked by `rfl` against the real compiler output, so a wrong literal is a build
failure rather than a weaker theorem.

## MEASURED ON ONE DESIGN

Everything above is measured on `tima_adder`: 4 nodes, 5 sources, 1 output, no
flops, no memories, and the constructors `rgetMask`, `rsum`, `rand`.  Nothing
here establishes that `rfl` still reduces `compileDesign`, or that this `simp`
call still closes the goal, at 1,436 nodes or 14,860 -- the residual term grows
with the design and both tactics are doing work proportional to it.  `Reify.lean`
exists precisely because the naive version of this proof is quadratic (measured
4.35x per doubling), and `prove_reified` does NOT yet use its step lemmas: it
hands the whole chain to `simp` in one go, which is fine for four bindings and
is the first thing expected to fall over on a large one.  Treat the cost as
unknown above `tima_adder` until it is measured, and expect the step-lemma walk
to be needed.

## What is NOT needed

No per-operator lemmas.  `simp [denoteExpr, CertVal.asBV]` discharges every
constructor uniformly, because `denoteExpr`'s dispatch and the reifier's
`rhsSyntax` name the same value-level function for each one.  `tima_adder`
exercises `rgetMask`, `rsum` and `rand` and needed nothing operator-specific;
the first design with memories or flops will exercise `refMem` and `flopNext`
and may need more, which is why `prove_reified` fails loudly rather than
emitting a weaker theorem.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign
import LeanSemanticPrimitives.Compiler.ReifyGen
import LeanSemanticPrimitives.Compiler.Reify

open Lean Elab Command Meta

namespace Compiler

/-- The axioms a D3 proof may depend on.  Anything else -- `sorryAx` from an
unfinished proof, `ofReduceBool` from `native_decide` -- is a FAILURE, not a
footnote: both would make the theorem claim more than the kernel checked. -/
def d3AllowedAxioms : List Name := [``propext, ``Classical.choice, ``Quot.sound]

namespace ReifyProof

--------------------------------------------------------------------------------
-- Liveness, for the incremental walk
--
-- The walk re-establishes one fact per LIVE produced slot at every step.  Keeping
-- every earlier binding live makes that O(N) facts per step and O(N^2) overall --
-- precisely what the walk exists to avoid -- so the generator has to know when a
-- value is dead.
--
-- Complexity, stated honestly: the walk is O(N * live-cutwidth), where the
-- cutwidth is the largest number of produced slots simultaneously live.  It is
-- NOT universally subquadratic: a design where one early binding feeds every
-- later one keeps that slot live throughout, and a design with a wide
-- reconvergent front has a correspondingly wide cut.  What the analysis buys is
-- that the cost tracks the DATAFLOW rather than the binding count.
--------------------------------------------------------------------------------

/-- Every slot an expression reads, paired with HOW it is read: `true` means as a
memory image (`refMem`), `false` as a bit vector (`refBV`).

The distinction is positional, not a property of the producing binding: a
`rmemRead` reads its first operand as a memory and its address and enable as bit
vectors.  Getting it from the producer's `ty` instead would be wrong for exactly
those positions. -/
def exprRefsTyped : ResidualExpr → Array (ResidualRef × Bool)
  | .rsum _ _ a | .rmult _ a | .rand _ a | .rorBits _ a | .rxor _ a
  | .rredOr _ a | .req _ a | .rshl _ a | .rmuxN _ a => a.map (fun r => (r, false))
  | .rnot _ a => #[(a, false)]
  | .rult _ a b | .rugt _ a b | .rslt _ a b | .rsgt _ a b
  | .rsra _ a b | .rsext _ a b | .rgetMask _ a b => #[(a, false), (b, false)]
  | .rmux _ s f t => #[(s, false), (f, false), (t, false)]
  | .rmemRead _ m a e => #[(m, true), (a, false), (e, false)]
  | .rmemWrite m a d e => #[(m, true), (a, false), (d, false), (e, false)]
  | .rmemWriteBE _ _ m a d be => #[(m, true), (a, false), (d, false), (be, false)]

/-- Slots read by something OTHER than a binding: the outputs, the flop update
fields and the memory next-images.

Missing these is the subtle half of the analysis.  A value whose only consumer is
an output is read by no later binding at all, so a last-use computed from binding
operands alone would call it dead at the step that produced it and drop the fact
the final rewrite needs. -/
def terminalRefs (R : ResidualProgram) : Array (ResidualRef × Bool) :=
  (R.outputs.map (fun o => (o.slot, false)))
    ++ (R.flopUpdates.flatMap (fun f =>
          #[(f.din, false)]
            ++ (match f.enable with | some e => #[(e, false)] | none => #[])
            ++ (match f.resetPin with | some r => #[(r, false)] | none => #[])))
    ++ (R.memoryUpdates.map (fun m => (m.nextImg, true)))

/-- `liveAfter R` at index `k` lists the PRODUCED slots that must still be
readable once step `k` has run: those a later binding reads, plus those any
terminal consumer reads.  Source slots are absent on purpose -- they travel as
one universal source-agreement fact, not one per slot. -/
def liveAfter (R : ResidualProgram) : Array (Array (ResidualRef × Bool)) :=
  let nsrc := R.sources.size
  let n := R.bindings.size
  Array.ofFn (n := n) fun k =>
    let laterBindings : Array (ResidualRef × Bool) :=
      (Array.ofFn (n := n) (fun j => j.val)).foldl (fun acc j =>
        if j > k.val then acc ++ exprRefsTyped (R.bindings[j]!).rhs else acc) #[]
    let all := laterBindings ++ terminalRefs R
    -- produced slots only, still in range, de-duplicated
    (all.filter (fun p => p.1 ≥ nsrc ∧ p.1 < nsrc + k.val + 1)).toList.eraseDups.toArray


private def qNat (n : Nat) : Term := quote n

private def qRefs (rs : Array ResidualRef) : MetaM Term := do
  let xs : Array Term := rs.map fun r => (qNat r : Term)
  `(#[$xs,*])

private def qValueType : ValueType → MetaM Term
  | .bv w       => do `(Compiler.ValueType.bv $(qNat w))
  | .mem aw dw  => do `(Compiler.ValueType.mem $(qNat aw) $(qNat dw))

/-- The residual expression as its own CONSTRUCTOR syntax -- the dual of
`ReifyGen.rhsSyntax`, which builds the value-level application instead.  Total
over the datatype: a missing case is a compile error here, not a silent gap. -/
def qExpr : ResidualExpr → MetaM Term
  | .rsum w n a     => do `(Compiler.ResidualExpr.rsum $(qNat w) $(qNat n) $(← qRefs a))
  | .rmult w a      => do `(Compiler.ResidualExpr.rmult $(qNat w) $(← qRefs a))
  | .rand w a       => do `(Compiler.ResidualExpr.rand $(qNat w) $(← qRefs a))
  | .rorBits w a    => do `(Compiler.ResidualExpr.rorBits $(qNat w) $(← qRefs a))
  | .rxor w a       => do `(Compiler.ResidualExpr.rxor $(qNat w) $(← qRefs a))
  | .rredOr w a     => do `(Compiler.ResidualExpr.rredOr $(qNat w) $(← qRefs a))
  | .req w a        => do `(Compiler.ResidualExpr.req $(qNat w) $(← qRefs a))
  | .rshl w a       => do `(Compiler.ResidualExpr.rshl $(qNat w) $(← qRefs a))
  | .rmuxN w a      => do `(Compiler.ResidualExpr.rmuxN $(qNat w) $(← qRefs a))
  | .rnot w a       => do `(Compiler.ResidualExpr.rnot $(qNat w) $(qNat a))
  | .rult w a b     => do `(Compiler.ResidualExpr.rult $(qNat w) $(qNat a) $(qNat b))
  | .rugt w a b     => do `(Compiler.ResidualExpr.rugt $(qNat w) $(qNat a) $(qNat b))
  | .rslt w a b     => do `(Compiler.ResidualExpr.rslt $(qNat w) $(qNat a) $(qNat b))
  | .rsgt w a b     => do `(Compiler.ResidualExpr.rsgt $(qNat w) $(qNat a) $(qNat b))
  | .rsra w a b     => do `(Compiler.ResidualExpr.rsra $(qNat w) $(qNat a) $(qNat b))
  | .rsext w a m    => do `(Compiler.ResidualExpr.rsext $(qNat w) $(qNat a) $(qNat m))
  | .rgetMask w a m => do `(Compiler.ResidualExpr.rgetMask $(qNat w) $(qNat a) $(qNat m))
  | .rmux w s f t   => do
      `(Compiler.ResidualExpr.rmux $(qNat w) $(qNat s) $(qNat f) $(qNat t))
  | .rmemRead w m a e => do
      `(Compiler.ResidualExpr.rmemRead $(qNat w) $(qNat m) $(qNat a) $(qNat e))
  | .rmemWrite m a d e => do
      `(Compiler.ResidualExpr.rmemWrite $(qNat m) $(qNat a) $(qNat d) $(qNat e))
  | .rmemWriteBE w bw m a d be => do
      `(Compiler.ResidualExpr.rmemWriteBE $(qNat w) $(qNat bw) $(qNat m) $(qNat a)
         $(qNat d) $(qNat be))

private def qBinding (b : ResidualBinding) : MetaM Term := do
  `({ ty := $(← qValueType b.ty), rhs := $(← qExpr b.rhs) })

private def qBindings (bs : Array ResidualBinding) : MetaM Term := do
  let xs ← bs.mapM qBinding
  `(#[$xs,*])

/-- The same bindings as a LIST literal.  `runBindings` walks a list, and
`runBindings_step` matches a `cons`; stating the walk over `#[...].toList` leaves
an `Array.toList` the rewrite cannot see through. -/
def qBindingsList (bs : Array ResidualBinding) : MetaM Term := do
  let xs ← bs.mapM qBinding
  `([$xs,*])

/-- The VALUE-level right-hand side of a binding, written against named earlier
values rather than an environment lookup.

A source slot `j` becomes `refBV e0 j` (or `refMem e0 j`), and a produced slot
becomes `<F>.val<k> e0` -- a NAME, not the value's expansion.  That is the whole
point: inlining instead would duplicate a re-read binding at every consumer, so a
diamond's value tree grows with the graph's reconvergence rather than its size.

The positional memory/bit-vector split comes from `exprRefsTyped`'s rule, applied
per constructor here. -/
def valRef (base : Name) (nsrc : Nat) (isMem : Bool) (r : ResidualRef) : MetaM Term :=
  if r < nsrc then
    let e0 := mkIdent (Name.mkSimple "e0")
    if isMem then `(refMem $e0 $(quote r)) else `(refBV $e0 $(quote r))
  else
    let nm := mkIdent (base ++ Name.mkSimple s!"val{r - nsrc}")
    let e0 := mkIdent (Name.mkSimple "e0")
    `($nm $e0)

private def valRefs (base : Name) (nsrc : Nat) (rs : Array ResidualRef) : MetaM Term := do
  let args ← rs.mapM (valRef base nsrc false)
  `([$args,*])

/-- Fail LOUDLY, by construction: this match is total over `ResidualExpr`, so a
constructor added later is a compile error in this file rather than a silent gap
inside a generated proof. -/
private def valSyntax (base : Name) (nsrc : Nat) : ResidualExpr → MetaM Term
  | .rsum w n a     => do `(Residual.rsumV $(quote w) $(quote n) $(← valRefs base nsrc a))
  | .rmult w a      => do `(Residual.rmultV $(quote w) $(← valRefs base nsrc a))
  | .rand w a       => do `(Residual.randV $(quote w) $(← valRefs base nsrc a))
  | .rorBits w a    => do `(Residual.rorBitsV $(quote w) $(← valRefs base nsrc a))
  | .rxor w a       => do `(Residual.rxorV $(quote w) $(← valRefs base nsrc a))
  | .rredOr w a     => do `(Residual.rredOrV $(quote w) $(← valRefs base nsrc a))
  | .req w a        => do `(Residual.reqV $(quote w) $(← valRefs base nsrc a))
  | .rshl w a       => do `(Residual.rshlV $(quote w) $(← valRefs base nsrc a))
  | .rmuxN w a      => do `(Residual.rmuxNV $(quote w) $(← valRefs base nsrc a))
  | .rnot w a       => do `(Residual.rnotV $(quote w) $(← valRef base nsrc false a))
  | .rult w a b     => do `(Residual.rultV $(quote w) $(← valRef base nsrc false a) $(← valRef base nsrc false b))
  | .rugt w a b     => do `(Residual.rugtV $(quote w) $(← valRef base nsrc false a) $(← valRef base nsrc false b))
  | .rslt w a b     => do `(Residual.rsltV $(quote w) $(← valRef base nsrc false a) $(← valRef base nsrc false b))
  | .rsgt w a b     => do `(Residual.rsgtV $(quote w) $(← valRef base nsrc false a) $(← valRef base nsrc false b))
  | .rsra w a b     => do `(Residual.rsraV $(quote w) $(← valRef base nsrc false a) $(← valRef base nsrc false b))
  | .rsext w a m    => do `(Residual.rsextV $(quote w) $(← valRef base nsrc false a) $(← valRef base nsrc false m))
  | .rgetMask w a m => do `(Residual.rgetMaskV $(quote w) $(← valRef base nsrc false a) $(← valRef base nsrc false m))
  | .rmux w s f t   => do
      `(Residual.rmuxV $(quote w) $(← valRef base nsrc false s) $(← valRef base nsrc false f)
         $(← valRef base nsrc false t))
  | .rmemRead w m a e => do
      `(Residual.rmemReadV $(quote w) $(← valRef base nsrc true m) $(← valRef base nsrc false a)
         $(← valRef base nsrc false e))
  | .rmemWrite m a d e => do
      `(Residual.rmemWriteV $(← valRef base nsrc true m) $(← valRef base nsrc false a)
         $(← valRef base nsrc false d) $(← valRef base nsrc false e))
  | .rmemWriteBE w bw m a d be => do
      `(Residual.rmemWriteBEV $(quote w) $(← valRef base nsrc true m) $(← valRef base nsrc false a)
         $(← valRef base nsrc false d) $(← valRef base nsrc false be) $(quote bw))

/-- How a slot is written in terms of the named values, at an EXPLICIT
environment rather than the `e0` the value definitions bind.

The definitions are parameterised over `e0`; a theorem statement about a
particular `i`/`st` has to apply them to that design's actual source
environment, or the term mentions a variable the statement never bound. -/
def valRefAt (base : Name) (nsrc : Nat) (r : ResidualRef) (env : Term) : MetaM Term :=
  if r < nsrc then `(refBV $env $(quote r))
  else
    let nm := mkIdent (base ++ Name.mkSimple s!"val{r - nsrc}")
    `($nm $env)

end ReifyProof

open ReifyProof

/-- `prove_reified <designCert> as <reifiedDef>` -/
syntax (name := proveReified) "prove_reified " ident " as " ident : command

@[command_elab proveReified]
def elabProveReified : CommandElab := fun stx => do
  match stx with
  | `(command| prove_reified $d:ident as $f:ident) => do
    let R ← liftTermElabM do
      let dExpr ← Term.elabTerm d none
      let cert ← unsafe evalExpr DesignCert (mkConst ``DesignCert) dExpr
      match compileDesign cert with
      | .error _ => throwError "prove_reified: compileDesign refused {d}"
      | .ok R    => pure R
    let bindsLit ← liftTermElabM (qBindings R.bindings)
    let nm  := f.getId
    let rNm       := mkIdent (nm ++ `R)
    let okNm      := mkIdent (nm ++ `compilesOk)
    let hRNm      := mkIdent (nm ++ `hR)
    let srcNm     := mkIdent (nm ++ `R_sources)
    let outNm     := mkIdent (nm ++ `R_outputs)
    let flopNm    := mkIdent (nm ++ `R_flops)
    let memNm     := mkIdent (nm ++ `R_mems)
    let bindNm    := mkIdent (nm ++ `R_bindings)
    let eqNm      := mkIdent (nm ++ `eq_compileAndRun)
    let corNm     := mkIdent (nm ++ `correct)
    elabCommand (← `(command|
      def $rNm : Compiler.ResidualProgram :=
        match Compiler.compileDesign $d with
        | .ok R => R
        | .error _ => default))
    -- `rfl`, NOT `native_decide`: see the module header.  This is what keeps
    -- `ofReduceBool` out of every theorem below.
    elabCommand (← `(command|
      theorem $okNm : Compiler.compilesOk $d = true := by rfl))
    elabCommand (← `(command|
      theorem $hRNm : Compiler.compileDesign $d = .ok $rNm :=
        Compiler.compileDesign_ok_witness $d $okNm))
    -- Projections only.  Evaluating the maps is left to `simp`, which can do it
    -- on a literal array; the kernel cannot, because `Array.map` is well-founded.
    elabCommand (← `(command|
      theorem $srcNm : ($rNm).sources = ($d).sources := rfl))
    elabCommand (← `(command|
      theorem $outNm : ($rNm).outputs = ($d).outputs.map Compiler.compileOutput := rfl))
    elabCommand (← `(command|
      theorem $flopNm : ($rNm).flopUpdates = ($d).flops.map Compiler.compileFlop := rfl))
    elabCommand (← `(command|
      theorem $memNm : ($rNm).memoryUpdates = ($d).memories.map Compiler.compileMemory := rfl))
    -- The one literal the metaprogram must write out, checked by `rfl` against
    -- the compiler's actual output: a wrong literal fails the build.
    elabCommand (← `(command|
      theorem $bindNm : ($rNm).bindings = $bindsLit := rfl))
    elabCommand (← `(command|
      theorem $eqNm : ∀ i st, $f i st = Compiler.compileAndRun $d i st := by
        intro i st
        simp only [Compiler.compileAndRun, $hRNm:ident, Compiler.denoteResidual,
                   $srcNm:ident, $outNm:ident, $flopNm:ident, $memNm:ident, $bindNm:ident]
        simp [$f:ident, $d:ident, Compiler.runBindings, Compiler.denoteExpr,
              Compiler.refBV, Compiler.refBVs, Compiler.refMem, Compiler.denoteRef,
              Compiler.sourceEnvArr, Compiler.compileOutput, CertVal.asBV]))
    elabCommand (← `(command|
      theorem $corNm : ∀ i st, $f i st = Compiler.interpretDesign $d i st := by
        intro i st
        rw [$eqNm:ident i st]
        exact Compiler.compileAndRun_correct $d $okNm i st))
    logInfo m!"prove_reified: {corNm} proved, {R.bindings.size} bindings"
  | _ => throwUnsupportedSyntax

/-- `prove_reified_incr <designCert> as <name>` — the opt-in INCREMENTAL path.

Emits, for a COMBINATIONAL design:

    <F>.val0 .. <F>.valN   one named value per binding, each naming its
                           predecessors rather than inlining them
    <F>                    the fast function, built DIRECTLY from those names
    <F>.walk               runBindings reaches an environment whose terminal
                           slots hold exactly those values -- proved one binding
                           at a time, every intermediate environment generalized
    <F>.eq_compileAndRun   <F> = compileAndRun D
    <F>.correct            <F> = interpretDesign D

`reify_design` / `prove_reified` are untouched and remain the path every sweep
uses; this exists to be compared against them.

WHY THE FAST FUNCTION IS EMITTED HERE rather than bridged to `reify_design`'s.
An earlier shape proved `<F>.vals : reified = <result built from the names>`, and
that proof has to unfold every value definition -- which rebuilds the inlined DAG
and costs exactly what the single `simp` costs. Emitting the function FROM the
names removes the bridge: its body already mentions them, so no proof has to
relate the two spellings.

The walk carries, at each step, ONLY the facts `liveAfter` says are still needed:
the universal source agreements (one each for bit vectors and memory images) and
one fact per live produced slot. A dead value's fact is not transported, which is
what keeps the cost at O(N * live-cutwidth) rather than O(N^2).

FAILS LOUDLY on flops and memory updates: `denoteResidual` folds `flopNext` over
`flopUpdates`, and nothing here proves anything about it. -/
syntax (name := proveReifiedIncr) "prove_reified_incr " ident " as " ident : command

@[command_elab proveReifiedIncr]
def elabProveReifiedIncr : CommandElab := fun stx => do
  match stx with
  | `(command| prove_reified_incr $d:ident as $f:ident) => do
    let R ← liftTermElabM do
      let dExpr ← Term.elabTerm d none
      let cert ← unsafe evalExpr DesignCert (mkConst ``DesignCert) dExpr
      match compileDesign cert with
      | .error e => throwError "prove_reified_incr: compileDesign refused {d}"
      | .ok R    => pure R
    if !R.flopUpdates.isEmpty || !R.memoryUpdates.isEmpty then
      throwError "prove_reified_incr: {d} has {R.flopUpdates.size} flop update(s) and \
        {R.memoryUpdates.size} memory update(s). The incremental walk covers the \
        COMBINATIONAL result only: `denoteResidual` folds `flopNext` over the flop \
        updates and nothing here proves anything about it. Refusing rather than \
        emitting a theorem whose statement looks complete."
    let nsrc  := R.sources.size
    let nb    := R.bindings.size
    let base  := f.getId
    let e0    := mkIdent (Name.mkSimple "e0")
    let iId   := mkIdent (Name.mkSimple "i")
    let stId  := mkIdent (Name.mkSimple "st")
    let live  := ReifyProof.liveAfter R
    -- ---- values -----------------------------------------------------------
    for k in [0 : nb] do
      let b   := R.bindings[k]!
      let nm  := mkIdent (base ++ Name.mkSimple s!"val{k}")
      let rhs ← liftTermElabM (ReifyProof.valSyntax base nsrc b.rhs)
      match b.ty with
      | .bv _    => elabCommand (← `(command| def $nm ($e0 : Compiler.SlotEnv) : BV := $rhs))
      | .mem _ _ => elabCommand (← `(command| def $nm ($e0 : Compiler.SlotEnv) : Int → BV := $rhs))
    -- ---- the fast function, straight from the names -----------------------
    let envT ← liftTermElabM `(Compiler.sourceEnvArr ($d).sources $iId $stId)
    let outs : Array Term ← R.outputs.mapM fun o => do
      let v ← liftTermElabM (ReifyProof.valRefAt base nsrc o.slot envT)
      `(bv_resize $(quote o.width) $v)
    elabCommand (← `(command|
      def $f ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState) :
          Compiler.RuntimeResult :=
        { outputs := #[$outs,*], nextState := { flops := #[], mems := #[] } }))
    -- ---- the residual, and its shape, as named facts -----------------------
    let rNm := mkIdent (base ++ `R)
    elabCommand (← `(command|
      def $rNm : Compiler.ResidualProgram :=
        match Compiler.compileDesign $d with | .ok R => R | .error _ => default))
    let okNm := mkIdent (base ++ `compilesOk)
    elabCommand (← `(command| theorem $okNm : Compiler.compilesOk $d = true := by rfl))
    let hRNm := mkIdent (base ++ `hR)
    elabCommand (← `(command|
      theorem $hRNm : Compiler.compileDesign $d = .ok $rNm :=
        Compiler.compileDesign_ok_witness $d $okNm))
    let bindsLit ← liftTermElabM (ReifyProof.qBindings R.bindings)
    let bindsList ← liftTermElabM (ReifyProof.qBindingsList R.bindings)
    let bNm := mkIdent (base ++ `R_bindings)
    elabCommand (← `(command| theorem $bNm : ($rNm).bindings = $bindsLit := rfl))
    let sNm := mkIdent (base ++ `R_sources)
    elabCommand (← `(command| theorem $sNm : ($rNm).sources = ($d).sources := rfl))
    -- ---- the walk ----------------------------------------------------------
    let envN  := fun (k : Nat) => mkIdent (Name.mkSimple s!"e{k}")
    let szN   := fun (k : Nat) => mkIdent (Name.mkSimple s!"hsz{k}")
    let agN   := fun (k : Nat) => mkIdent (Name.mkSimple s!"hag{k}")
    let agmN  := fun (k : Nat) => mkIdent (Name.mkSimple s!"hagm{k}")
    let bFact := fun (k sl : Nat) => mkIdent (Name.mkSimple s!"hb{k}_{sl}")
    let valN  := fun (k : Nat) => mkIdent (base ++ Name.mkSimple s!"val{k}")
    let termSlots := (R.outputs.map (fun o => o.slot)).toList.eraseDups
    -- the promised conclusion, one conjunct per distinct output slot
    -- The STATEMENT must name the actual source environment, not the proof's
    -- local `e0`: `e0` is introduced by `set` inside the proof and is not bound
    -- where the theorem is stated. `set` then folds these occurrences into `e0`,
    -- so the walk's facts match without any further rewriting.
    let mkFact : Nat → Nat → TermElabM Term := fun k sl =>
      if sl < nsrc then
        `(Compiler.refBV $(envN k) $(quote sl) = Compiler.refBV $envT $(quote sl))
      else `(Compiler.refBV $(envN k) $(quote sl) = $(valN (sl - nsrc)) $envT)
    let walkNm := mkIdent (base ++ `walk)
    let mut tacs : Array (TSyntax `tactic) := #[]
    let he0 := mkIdent (Name.mkSimple "he0")
    tacs := tacs.push (← `(tactic| intro $iId:ident $stId:ident))
    tacs := tacs.push (← `(tactic|
      set $(envN 0):ident : Compiler.SlotEnv :=
        Compiler.sourceEnvArr ($d).sources $iId $stId with $he0:ident))
    tacs := tacs.push (← `(tactic|
      have $(szN 0):ident : ($(envN 0)).size = $(quote nsrc) := by
        rw [$he0:ident, Compiler.sourceEnvArr_size]; rfl))
    tacs := tacs.push (← `(tactic|
      have $(agN 0):ident : ∀ j, j < ($(envN 0)).size →
        Compiler.refBV $(envN 0) j = Compiler.refBV $(envN 0) j := fun _ _ => rfl))
    tacs := tacs.push (← `(tactic|
      have $(agmN 0):ident : ∀ j, j < ($(envN 0)).size →
        Compiler.refMem $(envN 0) j = Compiler.refMem $(envN 0) j := fun _ _ => rfl))
    for k in [0 : nb] do
      let b := R.bindings[k]!
      let isMem := match b.ty with | .mem _ _ => true | .bv _ => false
      let rhsQ ← liftTermElabM (ReifyProof.qExpr b.rhs)
      let pushV ← liftTermElabM (if isMem then `(CertVal.mem ($(valN k) $(envN 0)))
                                 else `(CertVal.bv ($(valN k) $(envN 0))))
      -- operand reads: source agreement, or a live produced-slot fact
      let reads := ReifyProof.exprRefsTyped b.rhs
      let mut simpArgs : Array (TSyntax `Lean.Parser.Tactic.simpLemma) := #[]
      simpArgs := simpArgs.push (← `(Lean.Parser.Tactic.simpLemma| Compiler.denoteExpr))
      simpArgs := simpArgs.push (← `(Lean.Parser.Tactic.simpLemma| Compiler.refBVs))
      simpArgs := simpArgs.push (← `(Lean.Parser.Tactic.simpLemma| List.map_cons))
      simpArgs := simpArgs.push (← `(Lean.Parser.Tactic.simpLemma| List.map_nil))
      simpArgs := simpArgs.push (← `(Lean.Parser.Tactic.simpLemma| $(valN k):ident))
      for (sl, m) in reads do
        if sl < nsrc then
          let ag := if m then agmN k else agN k
          simpArgs := simpArgs.push (← `(Lean.Parser.Tactic.simpLemma|
            $ag:ident $(quote sl) (by simp only [$(szN 0):ident]; omega)))
        else
          simpArgs := simpArgs.push (← `(Lean.Parser.Tactic.simpLemma| $(bFact k sl):ident))
      let hvN := mkIdent (Name.mkSimple s!"hv{k}")
      tacs := tacs.push (← `(tactic|
        have $hvN:ident : Compiler.denoteExpr $(envN k) $rhsQ = $pushV := by
          simp only [$simpArgs,*]))
      tacs := tacs.push (← `(tactic| rw [Compiler.runBindings_step _ _ $(envN k) _ $hvN]))
      tacs := tacs.push (← `(tactic|
        have $(szN (k+1)):ident : (($(envN k)).push $pushV).size = $(quote (nsrc + k + 1)) := by
          simp [$(szN k):ident]))
      tacs := tacs.push (← `(tactic|
        have $(agN (k+1)):ident := Compiler.srcAgree_push (base := $(envN 0)) $pushV
          (by simp only [$(szN k):ident, $(szN 0):ident]; omega) $(agN k):ident))
      tacs := tacs.push (← `(tactic|
        have $(agmN (k+1)):ident := Compiler.srcAgreeMem_push (base := $(envN 0)) $pushV
          (by simp only [$(szN k):ident, $(szN 0):ident]; omega) $(agmN k):ident))
      -- transport exactly the live set, and nothing else
      let mut keep : Array (TSyntax `Lean.Parser.Tactic.locationWildcard) := #[]
      let mut names : Array Ident := #[szN (k+1), agN (k+1), agmN (k+1)]
      for (sl, m) in live[k]! do
        if sl == nsrc + k then
          let fnm := bFact (k+1) sl
          let selfLem := if m then mkIdent ``Compiler.refMem_push_self
                         else mkIdent ``Compiler.refBV_push_self
          let proj := if m then mkIdent ``CertVal.asMem else mkIdent ``CertVal.asBV
          let readFn := if m then mkIdent ``Compiler.refMem else mkIdent ``Compiler.refBV
          tacs := tacs.push (← `(tactic|
            have $fnm:ident : $readFn (($(envN k)).push $pushV) $(quote sl)
                = $(valN k) $(envN 0) := by
              have hh := $selfLem:ident $(envN k) $pushV
              rw [$(szN k):ident] at hh
              simpa [$proj:ident] using hh))
          names := names.push fnm
        else
          let fnm := bFact (k+1) sl
          let lem := if m then mkIdent ``Compiler.bindAgreeMem_push
                     else mkIdent ``Compiler.bindAgree_push
          tacs := tacs.push (← `(tactic|
            have $fnm:ident := $lem:ident (env := $(envN k)) $pushV $(quote sl) _
              (by simp only [$(szN k):ident]; omega) $(bFact k sl):ident))
          names := names.push fnm
      let _ := keep
      let locs : Array Ident := names
      tacs := tacs.push (← `(tactic|
        generalize (($(envN k)).push $pushV) = $(envN (k+1)):ident at $[$locs:ident]* ⊢))
    -- close
    let anonCtor : Array Term ← termSlots.toArray.mapM fun sl =>
      if sl < nsrc then `($(agN nb) $(quote sl) (by simp only [$(szN 0):ident]; omega))
      else `($(bFact nb sl))
    let trivTerm ← liftTermElabM `(trivial)
    let closing : Array Term := if anonCtor.isEmpty then #[trivTerm] else anonCtor
    tacs := tacs.push (← `(tactic|
      exact ⟨$(envN nb), by simp [Compiler.runBindings], $closing,*⟩))
    let factTerms ← liftTermElabM (termSlots.toArray.mapM (mkFact nb))
    -- the promised conjunction, built right-associated
    let conj ← liftTermElabM (
      let rec build : List Term → TermElabM Term
        | []      => `(True)
        | [x]     => pure x
        | x :: xs => do `($x ∧ $(← build xs))
      build factTerms.toList)
    let eNm := envN nb
    elabCommand (← `(command|
      theorem $walkNm : ∀ ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState),
        ∃ $eNm:ident,
          Compiler.runBindings $bindsList
            (Compiler.sourceEnvArr ($d).sources $iId $stId) = $eNm
          ∧ $conj := by
        $tacs*))
    -- ---- shape lemmas, then the two theorems -------------------------------
    let btlNm := mkIdent (base ++ `R_bindings_toList)
    elabCommand (← `(command| theorem $btlNm : ($rNm).bindings.toList = $bindsList := rfl))
    let oProjNm := mkIdent (base ++ `R_outputs_proj)
    elabCommand (← `(command|
      theorem $oProjNm : ($rNm).outputs = ($d).outputs.map Compiler.compileOutput := rfl))
    let fProjNm := mkIdent (base ++ `R_flops_proj)
    elabCommand (← `(command|
      theorem $fProjNm : ($rNm).flopUpdates = ($d).flops.map Compiler.compileFlop := rfl))
    let mProjNm := mkIdent (base ++ `R_mems_proj)
    elabCommand (← `(command|
      theorem $mProjNm : ($rNm).memoryUpdates = ($d).memories.map Compiler.compileMemory := rfl))
    let hNames : Array Ident :=
      if termSlots.isEmpty then #[mkIdent (Name.mkSimple "htermNone")]
      else (Array.ofFn (n := termSlots.length) (fun t => t.val)).map
             fun t => mkIdent (Name.mkSimple s!"hterm{t}")
    -- only the real facts go into the final `simp`; the `True` placeholder for a
    -- design with no outputs is not a rewrite rule.
    let hFacts : Array Ident := if termSlots.isEmpty then #[] else hNames
    -- `rfl` projections of the certificate, so the final `simp` never has to
    -- unfold the whole `DesignCert` literal to see that there are no flops.
    let dfNm := mkIdent (base ++ `D_flops)
    elabCommand (← `(command| theorem $dfNm : ($d).flops = #[] := rfl))
    let dmNm := mkIdent (base ++ `D_mems)
    elabCommand (← `(command| theorem $dmNm : ($d).memories = #[] := rfl))
    let doNm := mkIdent (base ++ `D_outputs)
    let outsLit ← liftTermElabM do
      let os ← R.outputs.mapM fun o =>
        `({ slot := $(quote o.slot), width := $(quote o.width) })
      `(#[$os,*])
    elabCommand (← `(command| theorem $doNm : ($d).outputs = $outsLit := rfl))
    let hSimp ← hFacts.mapM fun h => `(Lean.Parser.Tactic.simpLemma| $h:ident)
    let eqNm := mkIdent (base ++ `eq_compileAndRun)
    let eVar := mkIdent (Name.mkSimple "eW")
    let hEq  := mkIdent (Name.mkSimple "hEqW")
    elabCommand (← `(command|
      theorem $eqNm : ∀ ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState),
          $f $iId $stId = Compiler.compileAndRun $d $iId $stId := by
        intro $iId:ident $stId:ident
        obtain ⟨$eVar:ident, $hEq:ident, $hNames,*⟩ := $walkNm $iId $stId
        simp only [Compiler.compileAndRun, $hRNm:ident, Compiler.denoteResidual,
                   $sNm:ident, $btlNm:ident]
        rw [$hEq:ident]
        simp only [$oProjNm:ident, $fProjNm:ident, $mProjNm:ident]
        simp [$f:ident, Compiler.compileOutput, $dfNm:ident, $dmNm:ident,
              $doNm:ident, $hSimp,*]))
    let corNm := mkIdent (base ++ `correct)
    elabCommand (← `(command|
      theorem $corNm : ∀ ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState),
          $f $iId $stId = Compiler.interpretDesign $d $iId $stId := by
        intro $iId:ident $stId:ident
        rw [$eqNm:ident $iId $stId]
        exact Compiler.compileAndRun_correct $d $okNm $iId $stId))
    logInfo m!"prove_reified_incr: {nb} value(s), {f} emitted, {termSlots.length} \
      terminal slot(s), max live cut {(live.map (fun a => a.size)).foldl max 0}"
  | _ => throwUnsupportedSyntax

/-- `audit_axioms <thm>` — FAIL the build unless `<thm>`'s axiom set is within
`d3AllowedAxioms`.

`#print axioms` only prints; a probe that merely printed `sorryAx` would still
exit 0 and could still be read as a pass.  This throws, so the gate line below
it is unreachable unless the audit really held. -/
syntax (name := auditAxioms) "audit_axioms " ident : command

@[command_elab auditAxioms]
def elabAuditAxioms : CommandElab := fun stx => do
  match stx with
  | `(command| audit_axioms $t:ident) => do
    let cs ← liftCoreM do
      let n ← resolveGlobalConstNoOverload t
      collectAxioms n
    let bad := cs.filter fun a => !(d3AllowedAxioms.contains a)
    if bad.isEmpty then
      logInfo m!"audit_axioms: {t} depends on axioms: {cs.toList}"
    else
      throwError "audit_axioms: {t} depends on DISALLOWED axiom(s) {bad.toList}; \
                  allowed: {d3AllowedAxioms}"
  | _ => throwUnsupportedSyntax

/-- `d3_proof_gate <thm>` — audit, and ONLY on success emit the sweep's gate line.

Audit and report are one command on purpose.  Two commands would not do: Lean
keeps elaborating after an error, so a separate `#eval IO.println` would still
print its line when the audit above it had thrown, and the runner would credit
`proof=1` for a theorem resting on `sorryAx`.  Here the print is in the same
elaboration that throws, so it cannot be reached. -/
syntax (name := d3ProofGate) "d3_proof_gate " ident : command

@[command_elab d3ProofGate]
def elabD3ProofGate : CommandElab := fun stx => do
  match stx with
  | `(command| d3_proof_gate $t:ident) => do
    let (n, cs) ← liftCoreM do
      let n ← resolveGlobalConstNoOverload t
      pure (n, ← collectAxioms n)
    let bad := cs.filter fun a => !(d3AllowedAxioms.contains a)
    if !bad.isEmpty then
      throwError "d3_proof_gate: {t} depends on DISALLOWED axiom(s) {bad.toList}; \
                  allowed: {d3AllowedAxioms}"
    logInfo m!"D3GATE proof=1 thm={n} axioms={cs.toList}"
  | _ => throwUnsupportedSyntax

end Compiler

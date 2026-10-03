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

end ReifyProof

open ReifyProof

/-- Right-nested conjunction.  `obtain ⟨a, b, c⟩` and the anonymous constructor
both nest RIGHT, so a left fold here would destructure correctly only for a
single conjunct. -/
partial def mkConjR (ts : List Term) : TermElabM Term :=
  match ts with
  | []      => `(True)
  | [x]     => pure x
  | x :: xs => do `($x ∧ $(← mkConjR xs))


/-- Refuse to claim success for a theorem that did not close.

Both emitters used to `logInfo "... proved"` unconditionally, because
`elabCommand` reports a failed proof as an error and carries on rather than
throwing. On a flop design `prove_reified` therefore announced "proved" for a
theorem resting on `sorryAx`. The gate downstream caught it -- so nothing was
ever credited -- but the log said the opposite of the truth, and a log that
lies about the one thing it reports is worse than no log. -/
def auditOrThrow (what : Name) (where_ : String) : CommandElabM Unit := do
  let cs ← liftCoreM (collectAxioms what)
  let bad := cs.filter fun a => !(d3AllowedAxioms.contains a)
  if !bad.isEmpty then
    throwError "{where_}: the generated `{what}` depends on DISALLOWED axiom(s) \
      {bad.toList}; allowed: {d3AllowedAxioms}. The theorem did not close -- \
      refusing to report success."

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
    auditOrThrow (nm ++ `correct) "prove_reified"
    logInfo m!"prove_reified: {corNm} proved, {R.bindings.size} bindings"
  | _ => throwUnsupportedSyntax

/-- `prove_reified_chunked <designCert> as <name> [size <n>]` — proves the model
`reify_design_chunked` emitted, with the SAME `size`.

Each chunk carries its own obligation, for an ARBITRARY environment:

    <F>.chunk_j_eq (e) : <F>.chunk_j e = runBindings <F>.seg_j e

so no single goal grows with the design -- that is the whole point of chunking
rather than one let-chain. They compose through `Compiler.runBindings_append`
into one environment equality, and the roots are then read from that
environment exactly as `denoteResidual` reads them, which is why the closing
step needs no per-slot facts at all.

Emits `<F>.correct : <F> = interpretDesign <D>` and audits its axioms. -/
syntax (name := proveReifiedChunked) "prove_reified_chunked " ident " as " ident
  (" size " num)? : command

@[command_elab proveReifiedChunked]
def elabProveReifiedChunked : CommandElab := fun stx => do
  match stx with
  | `(command| prove_reified_chunked $d:ident as $f:ident $[size $sz]?) => do
    let R ← liftTermElabM do
      let dExpr ← Term.elabTerm d none
      let cert ← unsafe evalExpr DesignCert (mkConst ``DesignCert) dExpr
      match compileDesign cert with
      | .error _ => throwError "prove_reified_chunked: compileDesign refused {d}"
      | .ok R    => pure R
    markPhase "prover_start"
    let csize := match sz with | some k => max 1 k.getNat | none => 32
    let nb    := R.bindings.size
    let nseg  := if nb == 0 then 1 else (nb + csize - 1) / csize
    let base  := f.getId
    let iId   := mkIdent (Name.mkSimple "i")
    let stId  := mkIdent (Name.mkSimple "st")
    let eArg  := mkIdent (Name.mkSimple "e")
    let bnd   : Nat → Nat := fun j => min (j * csize) nb
    let segNm : Nat → Ident := fun j => mkIdent (base ++ Name.mkSimple s!"seg{j}")
    let chkNm : Nat → Ident := fun j => mkIdent (base ++ Name.mkSimple s!"chunk{j}")
    let eqNmJ : Nat → Ident := fun j => mkIdent (base ++ Name.mkSimple s!"chunk{j}_eq")
    let env ← getEnv
    if !(env.contains base) then
      throwError "prove_reified_chunked: no model named `{base}` is in scope. \
        This command PROVES a model emitted by `reify_design_chunked {d} as {f}`."
    if !(env.contains (base ++ Name.mkSimple "chunk0")) then
      throwError "prove_reified_chunked: `{base}` is in scope but `{base}.chunk0` \
        is not. That is the signature of a model emitted by a DIFFERENT reifier; \
        a chunk theorem must not be credited for it."
    -- the residual, and the facts the closing step reads off it
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
    markPhase "compiles_ok"
    -- The four projections come from the GENERIC `compileDesign_parts`,
    -- instantiated at `hR`, rather than from four `rfl`s.
    --
    -- Each `rfl` made the KERNEL evaluate `compileDesign D` and force the whole
    -- residual, once per projection. Measured, that was the single largest step
    -- of the proof (`projections` 5.24 s on a 133-binding design where every
    -- chunk theorem together cost 0.20 s) and it is where the 1,186-binding
    -- proof was killed: the markers stop at `compiles_ok`, the step before.
    --
    -- `compileDesign_parts` is proved once, for an arbitrary design, so
    -- instantiating it computes nothing. The statements are unchanged -- the
    -- same four equalities, now derived from the generic theorem instead of by
    -- evaluation -- so this is reuse, not a weakened obligation.
    let partsNm := mkIdent (base ++ `parts)
    elabCommand (← `(command|
      theorem $partsNm :
          Compiler.DesignCert.DepsBounded $d
          ∧ Compiler.compileGraph $d = .ok ($rNm).bindings
          ∧ ($rNm).sources = ($d).sources
          ∧ ($rNm).outputs = ($d).outputs.map Compiler.compileOutput
          ∧ ($rNm).flopUpdates = ($d).flops.map Compiler.compileFlop
          ∧ ($rNm).memoryUpdates = ($d).memories.map Compiler.compileMemory :=
        Compiler.compileDesign_parts $d $rNm $hRNm))
    let sNm := mkIdent (base ++ `R_sources)
    elabCommand (← `(command|
      theorem $sNm : ($rNm).sources = ($d).sources := ($partsNm).2.2.1))
    let oProjNm := mkIdent (base ++ `R_outputs_proj)
    elabCommand (← `(command|
      theorem $oProjNm : ($rNm).outputs = ($d).outputs.map Compiler.compileOutput :=
        ($partsNm).2.2.2.1))
    let fProjNm := mkIdent (base ++ `R_flops_proj)
    elabCommand (← `(command|
      theorem $fProjNm : ($rNm).flopUpdates = ($d).flops.map Compiler.compileFlop :=
        ($partsNm).2.2.2.2.1))
    let mProjNm := mkIdent (base ++ `R_mems_proj)
    elabCommand (← `(command|
      theorem $mProjNm : ($rNm).memoryUpdates = ($d).memories.map Compiler.compileMemory :=
        ($partsNm).2.2.2.2.2))
    markPhase "projections"
    -- 1. the segment lists, and the per-chunk obligations
    for j in [0 : nseg] do
      let lit ← liftTermElabM (ReifyProof.qBindingsList (R.bindings.extract (bnd j) (bnd (j+1))))
      elabCommand (← `(command| def $(segNm j) : List Compiler.ResidualBinding := $lit))
      elabCommand (← `(command|
        theorem $(eqNmJ j) ($eArg : Compiler.SlotEnv) :
            $(chkNm j) $eArg = Compiler.runBindings $(segNm j) $eArg := by
          simp only [$(chkNm j):ident, $(segNm j):ident, Compiler.runBindings,
                     Compiler.denoteExpr, Compiler.refBVs,
                     List.map_cons, List.map_nil]))
      markPhase s!"chunk_eq {j}"
    -- 2. the flat list IS the segments appended, by one `rfl`
    let segsEq := mkIdent (base ++ `segs_eq)
    let appTerm ← liftTermElabM do
      let rec go (j : Nat) : TermElabM Term :=
        if j + 1 ≥ nseg then `($(segNm j))
        else do `($(segNm j) ++ $(← go (j+1)))
      go 0
    elabCommand (← `(command|
      theorem $segsEq : ($rNm).bindings.toList = $appTerm := rfl))
    markPhase "segs_eq"
    -- 3. compose the chunks into ONE environment equality
    let srcEnv ← liftTermElabM `(Compiler.sourceEnvArr ($d).sources $iId $stId)
    let mut comp := srcEnv
    for j in [0 : nseg] do
      comp ← liftTermElabM `($(chkNm j) $comp)
    let mut rules : Array (TSyntax `Lean.Parser.Tactic.rwRule) :=
      #[← `(Lean.Parser.Tactic.rwRule| $segsEq:ident)]
    for _ in [0 : nseg - 1] do
      rules := rules.push (← `(Lean.Parser.Tactic.rwRule| Compiler.runBindings_append))
    for j in [0 : nseg] do
      rules := rules.push (← `(Lean.Parser.Tactic.rwRule| $(eqNmJ j):ident))
    let envEq := mkIdent (base ++ `env_eq)
    elabCommand (← `(command|
      theorem $envEq ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState) :
          $comp = Compiler.runBindings (($rNm).bindings.toList) $srcEnv := by
        rw [$rules,*]))
    markPhase "env_eq"
    -- 4. the roots, read from that environment exactly as denoteResidual reads them
    let dfNm := mkIdent (base ++ `D_flops)
    let flopsLit ← liftTermElabM do
      let fs ← R.flopUpdates.mapM fun fu => do
        let enq ← (match fu.enable with | none => `(none) | some e => do `(some $(quote e)))
        let rpq ← (match fu.resetPin with | none => `(none) | some r => do `(some $(quote r)))
        let rvq ← (if fu.resetValue < 0 then `(-(Int.ofNat $(quote fu.resetValue.natAbs)))
                   else `(Int.ofNat $(quote fu.resetValue.toNat)))
        let ral := if fu.resetActiveLow then mkIdent ``true else mkIdent ``false
        `({ width := $(quote fu.width), din := $(quote fu.din), enable := $enq,
            resetPin := $rpq, resetValue := $rvq, resetActiveLow := $ral })
      `(#[$fs,*])
    elabCommand (← `(command| theorem $dfNm : ($d).flops = $flopsLit := rfl))
    let dmNm := mkIdent (base ++ `D_mems)
    let memsLit ← liftTermElabM do
      let ms ← R.memoryUpdates.mapM fun mu =>
        `({ aw := $(quote mu.aw), dw := $(quote mu.dw), nextImg := $(quote mu.nextImg) })
      `(#[$ms,*])
    elabCommand (← `(command| theorem $dmNm : ($d).memories = $memsLit := rfl))
    let doNm := mkIdent (base ++ `D_outputs)
    let outsLit ← liftTermElabM do
      let os ← R.outputs.mapM fun o => `({ slot := $(quote o.slot), width := $(quote o.width) })
      `(#[$os,*])
    elabCommand (← `(command| theorem $doNm : ($d).outputs = $outsLit := rfl))
    let carNm := mkIdent (base ++ `eq_compileAndRun)
    elabCommand (← `(command|
      theorem $carNm : ∀ ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState),
          $f $iId $stId = Compiler.compileAndRun $d $iId $stId := by
        intro $iId:ident $stId:ident
        simp only [Compiler.compileAndRun, $hRNm:ident, Compiler.denoteResidual, $sNm:ident]
        rw [← $envEq:ident $iId $stId]
        simp only [$oProjNm:ident, $fProjNm:ident, $mProjNm:ident]
        simp [$f:ident, Compiler.compileOutput, Compiler.compileFlop,
              Compiler.compileMemory, Compiler.flopNext_eq,
              $dfNm:ident, $dmNm:ident, $doNm:ident]))
    markPhase "eq_compileAndRun"
    let corNm := mkIdent (base ++ `correct)
    elabCommand (← `(command|
      theorem $corNm : ∀ ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState),
          $f $iId $stId = Compiler.interpretDesign $d $iId $stId := by
        intro $iId:ident $stId:ident
        rw [$carNm:ident $iId $stId]
        exact Compiler.compileAndRun_correct $d $okNm $iId $stId))
    markPhase "correct"
    auditOrThrow (base ++ `correct) "prove_reified_chunked"
    logInfo m!"prove_reified_chunked: {corNm} proved over {nb} binding(s) in \
      {nseg} chunk(s) of at most {csize}"
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
    let nsrc  := R.sources.size
    let nb    := R.bindings.size
    let base  := f.getId
    let e0    := mkIdent (Name.mkSimple "e0")
    let iId   := mkIdent (Name.mkSimple "i")
    let stId  := mkIdent (Name.mkSimple "st")
    let live  := ReifyProof.liveAfter R
    -- The model is NOT emitted here. `reify_design_named` emits it, in the
    -- Mathlib-free layer, and BOTH probes use that one reifier -- otherwise the
    -- simulation probe would run one function and the proof probe would prove a
    -- different one that happened to be built the same way.
    let env ← getEnv
    let valsMissing := (Array.ofFn (n := nb) (fun k => k.val)).filter fun k =>
      !(env.contains (base ++ Name.mkSimple s!"val{k}"))
    if !(env.contains base) then
      throwError "prove_reified_incr: no model named `{base}` is in scope. This \
        command PROVES a model emitted by `reify_design_named {d} as {f}`; it does \
        not emit one. Emitting a second model here is exactly how the proved \
        function and the executed function come apart."
    if !valsMissing.isEmpty then
      throwError "prove_reified_incr: `{base}` is in scope but its named values are \
        not ({valsMissing.size} of {nb} missing). That is the signature of a LEGACY \
        model emitted by `reify_design`: an incremental theorem must not be \
        credited for it."
    let envT ← liftTermElabM `(Compiler.sourceEnvArr ($d).sources $iId $stId)
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
    -- Terminal slots are no longer just the outputs: a flop's din, enable and
    -- resetPin are read by nothing later in the binding list, so without them the
    -- walk would drop exactly the facts the flop result needs.  `terminalRefs`
    -- already computes this for `liveAfter`; the conclusion uses the same source.
    let termRefs := (ReifyProof.terminalRefs R).toList.eraseDups
    let termSlots := termRefs
    -- the promised conclusion, one conjunct per distinct output slot
    -- The STATEMENT must name the actual source environment, not the proof's
    -- local `e0`: `e0` is introduced by `set` inside the proof and is not bound
    -- where the theorem is stated. `set` then folds these occurrences into `e0`,
    -- so the walk's facts match without any further rewriting.
    let mkFact : Nat → (Nat × Bool) → TermElabM Term := fun k p =>
      let (sl, isMem) := p
      if sl < nsrc then
        (if isMem then `(Compiler.refMem $(envN k) $(quote sl) = Compiler.refMem $envT $(quote sl))
         else `(Compiler.refBV $(envN k) $(quote sl) = Compiler.refBV $envT $(quote sl)))
      else
        (if isMem then `(Compiler.refMem $(envN k) $(quote sl) = $(valN (sl - nsrc)) $envT)
         else `(Compiler.refBV $(envN k) $(quote sl) = $(valN (sl - nsrc)) $envT))
    let walkNm := mkIdent (base ++ `walk)
    let traceCtx := (← getOptions).getBool `d3.traceCtx false
    let segSize : Nat := (← getOptions).get `d3.segment 0
    -- The per-step tactics, shared by BOTH walks so the monolithic proof and the
    -- segmented one cannot drift apart.  `baseEnv` is the environment every fact
    -- is stated against -- the proof-local `e0` in the monolithic walk, and the
    -- source environment itself inside a segment, which has no `e0` binder --
    -- and `srcSz` names the fact that that environment has `nsrc` slots.
    let mkStep : Term → Ident → Nat → CommandElabM (Array (TSyntax `tactic)) :=
      fun baseEnv srcSz k => do
        let mut tacs : Array (TSyntax `tactic) := #[]
        let b := R.bindings[k]!
        let isMem := match b.ty with | .mem _ _ => true | .bv _ => false
        let rhsQ ← liftTermElabM (ReifyProof.qExpr b.rhs)
        let pushV ← liftTermElabM (if isMem then `(CertVal.mem ($(valN k) $baseEnv))
                                   else `(CertVal.bv ($(valN k) $baseEnv)))
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
              $ag:ident $(quote sl) (by simp only [$srcSz:ident]; omega)))
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
          have $(agN (k+1)):ident := Compiler.srcAgree_push (base := $baseEnv) $pushV
            (by simp only [$(szN k):ident, $srcSz:ident]; omega) $(agN k):ident))
        tacs := tacs.push (← `(tactic|
          have $(agmN (k+1)):ident := Compiler.srcAgreeMem_push (base := $baseEnv) $pushV
            (by simp only [$(szN k):ident, $srcSz:ident]; omega) $(agmN k):ident))
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
                  = $(valN k) $baseEnv := by
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
        -- BOUNDED CONTEXT.  `generalize ... at` transports the live facts, but it
        -- does not DISCARD the ones it replaced: measured, a 48-binding walk whose
        -- live set is 1 at every step still ended with 296 hypotheses in scope.
        -- Every context-scanning tactic in the remaining steps then pays for them.
        --
        -- So drop, at the end of each step, exactly what the invariant at step k+1
        -- does not mention: this step's `hv`, its size fact, its two source
        -- agreements, the produced-slot facts it just superseded, and finally the
        -- environment itself.  `e0` and `hsz0` are NOT dropped -- every fact is
        -- stated relative to `e0`, and the source-agreement side conditions read
        -- `hsz0` to the very end.  Dependents are listed before their dependencies
        -- so `clear` never has to refuse.
        let mut dead : Array Ident := #[hvN]
        if k > 0 then
          for (sl, _) in live[k-1]! do
            dead := dead.push (bFact k sl)
          dead := dead.push (szN k)
        dead := dead.push (agN k)
        dead := dead.push (agmN k)
        if k > 0 then
          dead := dead.push (envN k)
        tacs := tacs.push (← `(tactic| clear $[$dead:ident]*))
        if traceCtx then
          let kq := quote k
          let nliveq := quote (live[k]!.size)
          tacs := tacs.push (← `(tactic|
            run_tac do
              let g ← Lean.Elab.Tactic.getMainGoal
              let lc := (← g.getDecl).lctx
              let ndecl := lc.decls.toList.filterMap id |>.length
              let gt ← Lean.instantiateMVars (← g.getType)
              Lean.logInfo s!"D3CTX step={$kq} live={$nliveq} ctx={ndecl} goal={gt.approxDepth}"))
        return tacs
    if segSize == 0 then
      -- ---- one monolithic walk (unchanged) --------------------------------
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
      let e0T ← liftTermElabM `($(envN 0):ident)
      for k in [0 : nb] do
        tacs := tacs ++ (← mkStep e0T (szN 0) k)
      let anonCtor : Array Term ← termSlots.toArray.mapM fun p =>
        let (sl, isMem) := p
        if sl < nsrc then
          (if isMem then `($(agmN nb) $(quote sl) (by simp only [$(szN 0):ident]; omega))
           else `($(agN nb) $(quote sl) (by simp only [$(szN 0):ident]; omega)))
        else `($(bFact nb sl))
      let trivTerm ← liftTermElabM `(trivial)
      let closing : Array Term := if anonCtor.isEmpty then #[trivTerm] else anonCtor
      tacs := tacs.push (← `(tactic|
        exact ⟨$(envN nb), by simp [Compiler.runBindings], $closing,*⟩))
      let factTerms ← liftTermElabM (termSlots.toArray.mapM (mkFact nb))
      let conj ← liftTermElabM (mkConjR factTerms.toList)
      let eNm := envN nb
      elabCommand (← `(command|
        theorem $walkNm : ∀ ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState),
          ∃ $eNm:ident,
            Compiler.runBindings $bindsList
              (Compiler.sourceEnvArr ($d).sources $iId $stId) = $eNm
            ∧ $conj := by
          $tacs*))
      if traceCtx then
        logInfo s!"D3SIZE walk tactics={tacs.size} bindings={nb}"
    else
      -- ---- the walk in SEGMENTS -------------------------------------------
      -- Each segment is its own theorem over its own binding list, so no goal
      -- ever carries the whole remaining list.  They are composed through
      -- `runBindings_append`, which is proved once and generically.
      let nseg   := (nb + segSize - 1) / segSize
      let bnd    : Nat → Nat := fun j => min (j * segSize) nb
      let segNm  : Nat → Ident := fun j => mkIdent (base ++ Name.mkSimple s!"seg{j}")
      let segThm : Nat → Ident := fun j => mkIdent (base ++ Name.mkSimple s!"walk_seg{j}")
      let hwN    : Nat → Ident := fun j => mkIdent (Name.mkSimple s!"hw{j}")
      let srcSz  := mkIdent (Name.mkSimple "hsrcsz")
      let boundary : Nat → Array (Nat × Bool) := fun b => if b == 0 then #[] else live[b-1]!
      let factOf : Ident → (Nat × Bool) → TermElabM Term := fun e p =>
        let (sl, isMem) := p
        if sl < nsrc then
          (if isMem then `(Compiler.refMem $e $(quote sl) = Compiler.refMem $envT $(quote sl))
           else `(Compiler.refBV $e $(quote sl) = Compiler.refBV $envT $(quote sl)))
        else
          (if isMem then `(Compiler.refMem $e $(quote sl) = $(valN (sl - nsrc)) $envT)
           else `(Compiler.refBV $e $(quote sl) = $(valN (sl - nsrc)) $envT))
      -- 1. the segment lists, named
      for j in [0 : nseg] do
        let segLit ← liftTermElabM
          (ReifyProof.qBindingsList (R.bindings.extract (bnd j) (bnd (j+1))))
        elabCommand (← `(command|
          def $(segNm j) : List Compiler.ResidualBinding := $segLit))
      -- 2. one theorem per segment, carrying only the boundary invariant
      let envTT ← liftTermElabM `($envT)
      for j in [0 : nseg] do
        let lo := bnd j
        let hi := bnd (j+1)
        let eLo := envN lo
        let eHi := envN hi
        let stmt ← liftTermElabM do
          let szLo  ← `(($eLo).size = $(quote (nsrc + lo)))
          let agLo  ← `(∀ k, k < ($envT).size →
                          Compiler.refBV $eLo k = Compiler.refBV $envT k)
          let agmLo ← `(∀ k, k < ($envT).size →
                          Compiler.refMem $eLo k = Compiler.refMem $envT k)
          let szHi  ← `(($eHi).size = $(quote (nsrc + hi)))
          let agHi  ← `(∀ k, k < ($envT).size →
                          Compiler.refBV $eHi k = Compiler.refBV $envT k)
          let agmHi ← `(∀ k, k < ($envT).size →
                          Compiler.refMem $eHi k = Compiler.refMem $envT k)
          let hypTerms ← (boundary lo).mapM (factOf eLo)
          let cclTerms ← (boundary hi).mapM (factOf eHi)
          let rest ← mkConjR (szHi :: agHi :: agmHi :: cclTerms.toList)
          let mut acc ← `(∃ $eHi:ident,
            Compiler.runBindings $(segNm j) $eLo = $eHi ∧ $rest)
          for h in (#[szLo, agLo, agmLo] ++ hypTerms).reverse do
            acc ← `($h → $acc)
          `(∀ ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState)
              ($eLo : Compiler.SlotEnv), $acc)
        let introIds : Array Ident :=
          #[iId, stId, eLo, szN lo, agN lo, agmN lo]
            ++ (boundary lo).map (fun p => bFact lo p.1)
        let mut tacs : Array (TSyntax `tactic) := #[]
        tacs := tacs.push (← `(tactic| intro $[$introIds:ident]*))
        tacs := tacs.push (← `(tactic|
          have $srcSz:ident : ($envT).size = $(quote nsrc) := by
            rw [Compiler.sourceEnvArr_size]; rfl))
        tacs := tacs.push (← `(tactic| simp only [$(segNm j):ident]))
        for k in [lo : hi] do
          tacs := tacs ++ (← mkStep envTT srcSz k)
        let cclIds : Array Term ←
          ((boundary hi).map (fun p => bFact hi p.1)).mapM fun h => `($h)
        tacs := tacs.push (← `(tactic|
          exact ⟨$eHi, by simp [Compiler.runBindings],
                 $(szN hi), $(agN hi), $(agmN hi), $cclIds,*⟩))
        elabCommand (← `(command| theorem $(segThm j) : $stmt := by $tacs*))
      -- 3. the flat list IS the segments appended -- one `rfl`, O(N) once
      let segsEqNm := mkIdent (base ++ `segs_eq)
      let appTerm ← liftTermElabM do
        let rec go (j : Nat) : TermElabM Term :=
          if j + 1 ≥ nseg then `($(segNm j))
          else do `($(segNm j) ++ $(← go (j+1)))
        go 0
      elabCommand (← `(command|
        theorem $segsEqNm : $bindsList = $appTerm := rfl))
      -- 4. compose, without ever unfolding the flat list
      let mut tacs : Array (TSyntax `tactic) := #[]
      tacs := tacs.push (← `(tactic| intro $iId:ident $stId:ident))
      tacs := tacs.push (← `(tactic|
        have $srcSz:ident : ($envT).size = $(quote nsrc) := by
          rw [Compiler.sourceEnvArr_size]; rfl))
      for j in [0 : nseg] do
        let lo := bnd j
        let hi := bnd (j+1)
        let args : Array Term ←
          if j == 0 then do
            pure #[← `($iId), ← `($stId), ← `($envT), ← `($srcSz),
                   ← `(fun _ _ => rfl), ← `(fun _ _ => rfl)]
          else do
            let fs : Array Term ← ((boundary lo).map (fun p => bFact lo p.1)).mapM fun h => `($h)
            pure (#[← `($iId), ← `($stId), ← `($(envN lo)), ← `($(szN lo)),
                    ← `($(agN lo)), ← `($(agmN lo))] ++ fs)
        let pat : Array Ident :=
          #[envN hi, hwN j, szN hi, agN hi, agmN hi]
            ++ (boundary hi).map (fun p => bFact hi p.1)
        tacs := tacs.push (← `(tactic|
          obtain ⟨$[$pat:ident],*⟩ := $(segThm j) $args*))
      let mut rules : Array (TSyntax `Lean.Parser.Tactic.rwRule) :=
        #[← `(Lean.Parser.Tactic.rwRule| $segsEqNm:ident)]
      for j in [0 : nseg] do
        if j + 1 < nseg then
          rules := rules.push (← `(Lean.Parser.Tactic.rwRule| Compiler.runBindings_append))
        rules := rules.push (← `(Lean.Parser.Tactic.rwRule| $(hwN j):ident))
      let anonCtor : Array Term ← termSlots.toArray.mapM fun p =>
        let (sl, isMem) := p
        if sl < nsrc then
          (if isMem then `($(agmN nb) $(quote sl) (by simp only [$srcSz:ident]; omega))
           else `($(agN nb) $(quote sl) (by simp only [$srcSz:ident]; omega)))
        else `($(bFact nb sl))
      let trivTerm ← liftTermElabM `(trivial)
      let closing : Array Term := if anonCtor.isEmpty then #[trivTerm] else anonCtor
      tacs := tacs.push (← `(tactic|
        exact ⟨$(envN nb), by rw [$rules,*], $closing,*⟩))
      let factTerms ← liftTermElabM (termSlots.toArray.mapM (mkFact nb))
      let conj ← liftTermElabM (mkConjR factTerms.toList)
      let eNm := envN nb
      elabCommand (← `(command|
        theorem $walkNm : ∀ ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState),
          ∃ $eNm:ident,
            Compiler.runBindings $bindsList
              (Compiler.sourceEnvArr ($d).sources $iId $stId) = $eNm
            ∧ $conj := by
          $tacs*))
      if traceCtx then
        logInfo s!"D3SIZE segmented walk segments={nseg} segSize={segSize} bindings={nb}"
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
    let flopsLit ← liftTermElabM do
      let fs ← R.flopUpdates.mapM fun fu => do
        let enq ← (match fu.enable with
          | none => `(none) | some e => do `(some $(quote e)))
        let rpq ← (match fu.resetPin with
          | none => `(none) | some r => do `(some $(quote r)))
        let rvq ← (if fu.resetValue < 0 then `(-(Int.ofNat $(quote fu.resetValue.natAbs)))
                   else `(Int.ofNat $(quote fu.resetValue.toNat)))
        let ral := if fu.resetActiveLow then mkIdent ``true else mkIdent ``false
        `({ width := $(quote fu.width), din := $(quote fu.din), enable := $enq,
            resetPin := $rpq, resetValue := $rvq, resetActiveLow := $ral })
      `(#[$fs,*])
    elabCommand (← `(command| theorem $dfNm : ($d).flops = $flopsLit := rfl))
    let dmNm := mkIdent (base ++ `D_mems)
    let memsLit ← liftTermElabM do
      let ms ← R.memoryUpdates.mapM fun mu =>
        `({ aw := $(quote mu.aw), dw := $(quote mu.dw), nextImg := $(quote mu.nextImg) })
      `(#[$ms,*])
    elabCommand (← `(command| theorem $dmNm : ($d).memories = $memsLit := rfl))
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
        simp [$f:ident, Compiler.compileOutput, Compiler.compileFlop,
              Compiler.compileMemory, Compiler.flopNext_eq,
              $dfNm:ident, $dmNm:ident, $doNm:ident, $hSimp,*]))
    let corNm := mkIdent (base ++ `correct)
    elabCommand (← `(command|
      theorem $corNm : ∀ ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState),
          $f $iId $stId = Compiler.interpretDesign $d $iId $stId := by
        intro $iId:ident $stId:ident
        rw [$eqNm:ident $iId $stId]
        exact Compiler.compileAndRun_correct $d $okNm $iId $stId))
    auditOrThrow (base ++ `correct) "prove_reified_incr"
    logInfo m!"prove_reified_incr: {corNm} proved over {nb} named value(s), \
      {termSlots.length} terminal slot(s), max live cut \
      {(live.map (fun a => a.size)).foldl max 0}"
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

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
private def qExpr : ResidualExpr → MetaM Term
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

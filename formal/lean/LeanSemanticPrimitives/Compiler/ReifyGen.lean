/-
# `ReifyGen` — the reifier metaprogram (Direction 3, deliverable 1)

`reify_design D as F` runs `compileDesign` on the `DesignCert` constant `D` **at
elaboration time**, then emits

    def F (i : RuntimeInput) (st : RuntimeState) : RuntimeResult := <let-chain>

a straight-line Lean definition with no constructor dispatch and no environment
lookup: the compiled-simulation artifact.

This is where the "per design" of `reify_correct : generated = denoteResidual R`
comes from, and the code makes it concrete.  `compileDesign` is applied to a
VALUE here, obtained with `evalExpr`; the result is folded into `Syntax` and
handed to `elabCommand`, which mutates the environment.  None of that is a
function in the logic, so there is no term the logic could quantify over.
-/
import Lean
import LeanSemanticPrimitives.Compiler.CompileDesignDefs

open Lean Elab Command Meta
open Compiler.Residual

/-- DIAGNOSTIC ONLY.  When set, the incremental walk emits, after every step, a
`run_tac` that logs the size of the LOCAL CONTEXT and of the remaining goal.

This exists to answer one question with a measurement rather than a reading of
the generator: the per-step `generalize` lists only the NEW facts, so every
stale `hv{j}`, `hsz{j}`, `hag{j}`, `hagm{j}` and dead `hb{j}_{slot}` stays in
scope for the rest of the walk.  Whether that growth -- rather than the live set
-- is what costs memory is not decidable by inspection.

The tactics are emitted ONLY when the option is already true as the command
elaborates, so a normal proof run carries no extra tactic and the measurement
does not perturb the thing it measures. -/
register_option d3.traceCtx : Bool := {
  defValue := false
  descr    := "D3: log local-context and goal size after each incremental walk step"
}

/-- Path of a file the chunked prover appends a timestamped line to after each
sub-theorem it emits. Empty (the default) writes nothing.

A proof killed by the memory guard is SIGKILLed, so its log is empty and says
nothing about which theorem it reached. The prover is a command elaborator, so
it can write this itself -- no generated `#eval`, and the line is on disk
before the next theorem starts. -/
register_option d3.proofPhase : String := {
  defValue := ""
  descr    := "D3: append chunked-prover progress to this file (empty = off)"
}

/-- Segment size for the incremental walk.  0 (the default) keeps the single
monolithic walk; `n > 0` emits the walk as segment theorems of at most `n`
bindings each, composed through `Compiler.runBindings_append`.

Registered here, not in ReifyProof, for the same reason as `d3.traceCtx`: a
certificate carrying `set_option d3.segment` is copied into BOTH probes, and the
simulation probe does not import ReifyProof. -/
register_option d3.segment : Nat := {
  defValue := 0
  descr    := "D3: emit the incremental walk in segments of this many bindings (0 = one walk)"
}

namespace Compiler

/-- Append one timestamped progress line, if `d3.proofPhase` names a file.
Written by the elaborator itself and closed immediately, so it survives the
SIGKILL that leaves the probe's own log empty. -/
def markPhase (msg : String) : CommandElabM Unit := do
  let path : String := (← getOptions).get `d3.proofPhase ""
  if path.isEmpty then return
  let t ← IO.monoMsNow
  IO.FS.withFile path IO.FS.Mode.append fun h => h.putStrLn s!"{msg} t={t}"


/-- Slot `sl` names a source (`s<sl>`) or an earlier binding (`v<sl-nsrc>`). -/
private def slotIdent (nsrc sl : Nat) : Ident :=
  mkIdent (Name.mkSimple (if sl < nsrc then s!"s{sl}" else s!"v{sl - nsrc}"))

private def refList (nsrc : Nat) (rs : Array ResidualRef) : MetaM Term := do
  let args : Array Term := rs.map fun r => (slotIdent nsrc r : Term)
  `([$args,*])

/-- `Int` has no `Quote` instance, so a reset value must be spelled out. -/
private def quoteInt (z : Int) : MetaM Term :=
  if z < 0 then `(-(Int.ofNat $(quote z.natAbs))) else `(Int.ofNat $(quote z.toNat))

/-- One binding's right-hand side, as an application of the value-level
function that `denoteExpr` would have dispatched to.  Every constructor is
covered; a missing one would silently fall through, so the match is total. -/
private def rhsSyntax (nsrc : Nat) : ResidualExpr → MetaM Term
  | .rsum w n a     => do `(rsumV $(quote w) $(quote n) $(← refList nsrc a))
  | .rmult w a      => do `(rmultV $(quote w) $(← refList nsrc a))
  | .rand w a       => do `(randV $(quote w) $(← refList nsrc a))
  | .rorBits w a    => do `(rorBitsV $(quote w) $(← refList nsrc a))
  | .rxor w a       => do `(rxorV $(quote w) $(← refList nsrc a))
  | .rredOr w a     => do `(rredOrV $(quote w) $(← refList nsrc a))
  | .req w a        => do `(reqV $(quote w) $(← refList nsrc a))
  | .rshl w a       => do `(rshlV $(quote w) $(← refList nsrc a))
  | .rmuxN w a      => do `(rmuxNV $(quote w) $(← refList nsrc a))
  | .rnot w a       => do `(rnotV $(quote w) $(slotIdent nsrc a))
  | .rult w a b     => do `(rultV $(quote w) $(slotIdent nsrc a) $(slotIdent nsrc b))
  | .rugt w a b     => do `(rugtV $(quote w) $(slotIdent nsrc a) $(slotIdent nsrc b))
  | .rslt w a b     => do `(rsltV $(quote w) $(slotIdent nsrc a) $(slotIdent nsrc b))
  | .rsgt w a b     => do `(rsgtV $(quote w) $(slotIdent nsrc a) $(slotIdent nsrc b))
  | .rsra w a b     => do `(rsraV $(quote w) $(slotIdent nsrc a) $(slotIdent nsrc b))
  | .rsext w a m    => do `(rsextV $(quote w) $(slotIdent nsrc a) $(slotIdent nsrc m))
  | .rgetMask w a m => do `(rgetMaskV $(quote w) $(slotIdent nsrc a) $(slotIdent nsrc m))
  | .rmux w s f t   => do
      `(rmuxV $(quote w) $(slotIdent nsrc s) $(slotIdent nsrc f) $(slotIdent nsrc t))
  | .rmemRead w m a e => do
      `(rmemReadV $(quote w) $(slotIdent nsrc m) $(slotIdent nsrc a) $(slotIdent nsrc e))
  | .rmemWrite m a d e => do
      `(rmemWriteV $(slotIdent nsrc m) $(slotIdent nsrc a) $(slotIdent nsrc d)
         $(slotIdent nsrc e))
  | .rmemWriteBE w bw m a d be => do
      `(rmemWriteBEV $(quote w) $(slotIdent nsrc m) $(slotIdent nsrc a) $(slotIdent nsrc d)
         $(slotIdent nsrc be) $(quote bw))

--------------------------------------------------------------------------------
-- The NAMED fast model
--
-- `reify_design` emits one straight-line `let` chain.  The named model instead
-- emits one definition per binding, each referring to its predecessors BY NAME,
-- and a function built from those names.
--
-- It lives HERE, in the Mathlib-free layer, because both probes must be able to
-- emit it: the SIMULATION probe runs the model and the PROOF probe proves it,
-- and if each emitted its own the proof would be about a different function from
-- the one that ran.  One reifier, two consumers.
--------------------------------------------------------------------------------

namespace NamedModel

/-- The VALUE-level right-hand side of a binding, written against named earlier
values rather than an environment lookup.

A source slot `j` becomes `refBV e0 j` (or `refMem e0 j`), and a produced slot
becomes `<F>.val<k> e0` -- a NAME, not the value's expansion.  That is the whole
point: inlining instead would duplicate a re-read binding at every consumer, so a
diamond's value tree grows with the graph's reconvergence rather than its size.

The positional memory/bit-vector split comes from `exprRefsTyped`'s rule, applied
per constructor here. -/
def valRef (base : Name) (nsrc : Nat) (loc : Bool) (isMem : Bool)
    (r : ResidualRef) : MetaM Term :=
  if r < nsrc then
    let e0 := mkIdent (Name.mkSimple "e0")
    if isMem then `(refMem $e0 $(quote r)) else `(refBV $e0 $(quote r))
  else if loc then
    -- SHARED form: the operand is a local the let-chain already bound, so it
    -- is evaluated once however many consumers read it. The `val{k} e0` call
    -- below is the other form, where every consumer re-evaluates the cone.
    pure (mkIdent (Name.mkSimple s!"v{r - nsrc}"))
  else
    let nm := mkIdent (base ++ Name.mkSimple s!"val{r - nsrc}")
    let e0 := mkIdent (Name.mkSimple "e0")
    `($nm $e0)

/-- The operand renderer is a PARAMETER, so one match over `ResidualExpr`
serves every form: named calls, let-bound locals, and -- for the chunk
functions -- plain reads from the environment as it stands at that binding.

The match stays total over `ResidualExpr`, so a constructor added later is a
compile error here rather than a silent gap inside a generated proof. -/
private def refsOf (ref : Bool → ResidualRef → MetaM Term)
    (rs : Array ResidualRef) : MetaM Term := do
  let args ← rs.mapM (ref false)
  `([$args,*])

private def valSyntaxG (ref : Bool → ResidualRef → MetaM Term) :
    ResidualExpr → MetaM Term
  | .rsum w n a     => do `(Residual.rsumV $(quote w) $(quote n) $(← refsOf ref a))
  | .rmult w a      => do `(Residual.rmultV $(quote w) $(← refsOf ref a))
  | .rand w a       => do `(Residual.randV $(quote w) $(← refsOf ref a))
  | .rorBits w a    => do `(Residual.rorBitsV $(quote w) $(← refsOf ref a))
  | .rxor w a       => do `(Residual.rxorV $(quote w) $(← refsOf ref a))
  | .rredOr w a     => do `(Residual.rredOrV $(quote w) $(← refsOf ref a))
  | .req w a        => do `(Residual.reqV $(quote w) $(← refsOf ref a))
  | .rshl w a       => do `(Residual.rshlV $(quote w) $(← refsOf ref a))
  | .rmuxN w a      => do `(Residual.rmuxNV $(quote w) $(← refsOf ref a))
  | .rnot w a       => do `(Residual.rnotV $(quote w) $(← ref false a))
  | .rult w a b     => do `(Residual.rultV $(quote w) $(← ref false a) $(← ref false b))
  | .rugt w a b     => do `(Residual.rugtV $(quote w) $(← ref false a) $(← ref false b))
  | .rslt w a b     => do `(Residual.rsltV $(quote w) $(← ref false a) $(← ref false b))
  | .rsgt w a b     => do `(Residual.rsgtV $(quote w) $(← ref false a) $(← ref false b))
  | .rsra w a b     => do `(Residual.rsraV $(quote w) $(← ref false a) $(← ref false b))
  | .rsext w a m    => do `(Residual.rsextV $(quote w) $(← ref false a) $(← ref false m))
  | .rgetMask w a m => do `(Residual.rgetMaskV $(quote w) $(← ref false a) $(← ref false m))
  | .rmux w s f t   => do
      `(Residual.rmuxV $(quote w) $(← ref false s) $(← ref false f)
         $(← ref false t))
  | .rmemRead w m a e => do
      `(Residual.rmemReadV $(quote w) $(← ref true m) $(← ref false a)
         $(← ref false e))
  | .rmemWrite m a d e => do
      `(Residual.rmemWriteV $(← ref true m) $(← ref false a)
         $(← ref false d) $(← ref false e))
  | .rmemWriteBE w bw m a d be => do
      `(Residual.rmemWriteBEV $(quote w) $(← ref true m) $(← ref false a)
         $(← ref false d) $(← ref false be) $(quote bw))

/-- Named-call form. -/
def valSyntax (base : Name) (nsrc : Nat) (loc : Bool) : ResidualExpr → MetaM Term :=
  valSyntaxG (fun isMem r => valRef base nsrc loc isMem r)

/-- CHUNK form: every operand, source or produced, is a read from the
environment as it stands at that binding. A chunk threads a real `SlotEnv`, so
there is nothing else for an operand to be. -/
def valSyntaxEnv (envId : Term) : ResidualExpr → MetaM Term :=
  valSyntaxG (fun isMem r =>
    let rbv := mkIdent ``Compiler.refBV
    let rmm := mkIdent ``Compiler.refMem
    if isMem then `($rmm $envId $(quote r)) else `($rbv $envId $(quote r)))

/-- How a slot is written in terms of the named values, at an EXPLICIT
environment rather than the `e0` the value definitions bind, with the same
`isMem` discipline `valRef` uses for binding operands.

The definitions are parameterised over `e0`; a theorem statement about a
particular `i`/`st` has to apply them to that design's actual source
environment, or the term mentions a variable the statement never bound.

A produced slot carries its type in its named value -- a memory-valued binding's
`val` is already `Int → BV` -- so the produced case needs no flag. A SOURCE slot
does: `valRefAt` always reads one with `refBV`, which is right for a data or
control operand and WRONG for a memory next-image that names an unchanged source
image. That certificate is legal (a design may declare a memory it never writes)
and would have produced a `BV` field where `Int → BV` is required. -/
def valRefAtTyped (base : Name) (nsrc : Nat) (isMem : Bool) (r : ResidualRef)
    (env : Term) : MetaM Term :=
  if r < nsrc then
    (if isMem then `(refMem $env $(quote r)) else `(refBV $env $(quote r)))
  else
    let nm := mkIdent (base ++ Name.mkSimple s!"val{r - nsrc}")
    `($nm $env)

/-- The bit-vector case: outputs and flop operands, which are never memory. -/
def valRefAt (base : Name) (nsrc : Nat) (r : ResidualRef) (env : Term) : MetaM Term :=
  valRefAtTyped base nsrc false r env

end NamedModel

open NamedModel in
/-- `reify_design_named <designCert> as <name>` — the named model.

Emits `<F>.val0 .. <F>.valN`, one per binding, and `<F>` built from them.
Opt-in: `reify_design` is unchanged and remains the default everywhere.

Covers outputs, flop next-state and memory next-images. -/
syntax (name := reifyDesignNamed) "reify_design_named " ident " as " ident : command

@[command_elab reifyDesignNamed]
def elabReifyDesignNamed : CommandElab := fun stx => do
  match stx with
  | `(command| reify_design_named $d:ident as $f:ident) => do
    let R ← liftTermElabM do
      let dExpr ← Term.elabTerm d none
      let cert ← unsafe evalExpr DesignCert (mkConst ``DesignCert) dExpr
      match compileDesign cert with
      | .error _ => throwError "reify_design_named: compileDesign refused {d}"
      | .ok R    => pure R
    let nsrc := R.sources.size
    let base := f.getId
    let e0   := mkIdent (Name.mkSimple "e0")
    let iId  := mkIdent (Name.mkSimple "i")
    let stId := mkIdent (Name.mkSimple "st")
    for k in [0 : R.bindings.size] do
      let b   := R.bindings[k]!
      let nm  := mkIdent (base ++ Name.mkSimple s!"val{k}")
      let rhs ← liftTermElabM (NamedModel.valSyntax base nsrc false b.rhs)
      match b.ty with
      | .bv _    => elabCommand (← `(command| def $nm ($e0 : Compiler.SlotEnv) : BV := $rhs))
      | .mem _ _ => elabCommand (← `(command| def $nm ($e0 : Compiler.SlotEnv) : Int → BV := $rhs))
    let envT ← liftTermElabM `(Compiler.sourceEnvArr ($d).sources $iId $stId)
    let outs : Array Term ← R.outputs.mapM fun o => do
      let v ← liftTermElabM (NamedModel.valRefAt base nsrc o.slot envT)
      `(bv_resize $(quote o.width) $v)
    let flops : Array Term ← (Array.ofFn (n := R.flopUpdates.size) (fun j => j.val)).mapM
      fun j => do
        let fu := R.flopUpdates[j]!
        let din ← liftTermElabM (NamedModel.valRefAt base nsrc fu.din envT)
        let en ← liftTermElabM (match fu.enable with
          | none   => `(none)
          | some e => do `(some $(← NamedModel.valRefAt base nsrc e envT)))
        let rp ← liftTermElabM (match fu.resetPin with
          | none   => `(none)
          | some r => do `(some $(← NamedModel.valRefAt base nsrc r envT)))
        let rvq ← liftTermElabM (if fu.resetValue < 0
          then `(-(Int.ofNat $(quote fu.resetValue.natAbs)))
          else `(Int.ofNat $(quote fu.resetValue.toNat)))
        let ral := if fu.resetActiveLow then mkIdent ``true else mkIdent ``false
        `(Compiler.flopNextV $(quote fu.width) $din $en $rp $rvq $ral
            (($stId).flops[$(quote j)]?))
    -- A memory next-image is just the value at the update's `nextImg` slot, and
    -- that slot's named value already has type `Int → BV` because the binding
    -- that produced it is memory-valued. No separate machinery is needed here.
    let mems : Array Term ← R.memoryUpdates.mapM fun mu =>
      liftTermElabM (NamedModel.valRefAtTyped base nsrc true mu.nextImg envT)
    elabCommand (← `(command|
      def $f ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState) :
          Compiler.RuntimeResult :=
        { outputs := #[$outs,*],
          nextState := { flops := #[$flops,*], mems := #[$mems,*] } }))
    -- EXACTLY the shape `reify_design` reports: the sweep's gate parser reads
    -- this line for the `compile` and `reify` gates and cross-checks the two
    -- counts against the probe's own shape line. A different wording here reads
    -- as "the reifier never ran".
    logInfo m!"reify_design_named: {f} emitted, {nsrc} sources, \
      {R.bindings.size} bindings"
  | _ => throwUnsupportedSyntax

/-- `reify_design_chunked <designCert> as <name> [size <n>]` — OPT-IN.

Emits the model as BOUNDED CHUNKS: each chunk is a `SlotEnv → SlotEnv` that
pushes at most `n` bindings (default 32), reading every operand from the
environment as it stands at that binding, and computing each value with the
fast value-level op rather than through `denoteExpr`.

Two properties follow, and they are the reason for this shape rather than one
let-chain. Each binding is computed ONCE per sample, as in `reify_design_shared`.
And each chunk carries a SEPARATE obligation `chunk_j e = runBindings seg_j e`
for an arbitrary `e`, so no single proof goal grows with the design -- which is
what a monolithic chain cannot promise, and what `prove_reified_chunked`
composes with `Compiler.runBindings_append`. -/
syntax (name := reifyDesignChunked) "reify_design_chunked " ident " as " ident
  (" size " num)? : command

@[command_elab reifyDesignChunked]
def elabReifyDesignChunked : CommandElab := fun stx => do
  match stx with
  | `(command| reify_design_chunked $d:ident as $f:ident $[size $sz]?) => do
    let R ← liftTermElabM do
      let dExpr ← Term.elabTerm d none
      let cert ← unsafe evalExpr DesignCert (mkConst ``DesignCert) dExpr
      match compileDesign cert with
      | .error _ => throwError "reify_design_chunked: compileDesign refused {d}"
      | .ok R    => pure R
    markPhase "reify_compiled"
    let csize := match sz with | some k => max 1 k.getNat | none => 32
    let nsrc  := R.sources.size
    let nb    := R.bindings.size
    let nseg  := if nb == 0 then 1 else (nb + csize - 1) / csize
    let base  := f.getId
    let iId   := mkIdent (Name.mkSimple "i")
    let stId  := mkIdent (Name.mkSimple "st")
    let eArg  := mkIdent (Name.mkSimple "e")
    let bnd   : Nat → Nat := fun j => min (j * csize) nb
    let chkNm : Nat → Ident := fun j => mkIdent (base ++ Name.mkSimple s!"chunk{j}")
    let envNm : Nat → Ident := fun j => mkIdent (Name.mkSimple s!"e{j}")
    -- The SEGMENT LISTS are not emitted here. They exist only to state
    -- `chunk_j e = runBindings seg_j e`, which is a proof artifact, and
    -- quoting a binding list needs `ReifyProof`, which imports this file.
    -- `prove_reified_chunked` emits them.
    --
    -- 1. the chunk functions
    for j in [0 : nseg] do
      let lo := bnd j
      let hi := bnd (j+1)
      let mut body : Term ← `($(envNm (hi - lo)):ident)
      for t in [0 : hi - lo] do
        let idx := hi - 1 - t
        let pos := idx - lo
        let b := R.bindings[idx]!
        let isMem := match b.ty with | .mem _ _ => true | .bv _ => false
        let cur ← liftTermElabM `($(envNm pos):ident)
        let rhs ← liftTermElabM (NamedModel.valSyntaxEnv cur b.rhs)
        -- `mkIdent` on a RESOLVED constant: writing `Compiler.CertVal.bv`
        -- inside the quotation picks up a macro scope and reaches the probe as
        -- `Compiler.CertVal.bv✝`, which resolves to nothing there.
        let cbv  := mkIdent ``CertVal.bv
        let cmem := mkIdent ``CertVal.mem
        let v ← liftTermElabM (if isMem then `($cmem $rhs) else `($cbv $rhs))
        body ← `(let $(envNm (pos+1)):ident := ($(envNm pos):ident).push $v
                 $body)
      elabCommand (← `(command|
        def $(chkNm j) ($(envNm 0) : Compiler.SlotEnv) : Compiler.SlotEnv := $body))
    markPhase "reify_chunks_emitted"
    -- 2. the model: source env, then the chunks in order, then the roots read
    --    from the FINAL environment exactly as `denoteResidual` reads them
    let fin := mkIdent (Name.mkSimple s!"c{nseg}")
    let rootRef : Bool → Nat → MetaM Term := fun isMem r =>
      if isMem then `(Compiler.refMem $fin $(quote r)) else `(Compiler.refBV $fin $(quote r))
    let outs : Array Term ← R.outputs.mapM fun o => do
      `(bv_resize $(quote o.width) $(← liftTermElabM (rootRef false o.slot)))
    let flops : Array Term ← (Array.ofFn (n := R.flopUpdates.size) (fun j => j.val)).mapM
      fun j => do
        let fu := R.flopUpdates[j]!
        let din ← liftTermElabM (rootRef false fu.din)
        let en ← liftTermElabM (match fu.enable with
          | none => `(none) | some e => do `(some $(← rootRef false e)))
        let rp ← liftTermElabM (match fu.resetPin with
          | none => `(none) | some r => do `(some $(← rootRef false r)))
        let rvq ← liftTermElabM (if fu.resetValue < 0
          then `(-(Int.ofNat $(quote fu.resetValue.natAbs)))
          else `(Int.ofNat $(quote fu.resetValue.toNat)))
        let ral := if fu.resetActiveLow then mkIdent ``true else mkIdent ``false
        `(Compiler.flopNextV $(quote fu.width) $din $en $rp $rvq $ral
            (($stId).flops[$(quote j)]?))
    let mems : Array Term ← R.memoryUpdates.mapM fun mu =>
      liftTermElabM (rootRef true mu.nextImg)
    let mut mbody ← `({ outputs := #[$outs,*],
                        nextState := { flops := #[$flops,*], mems := #[$mems,*] } })
    for t in [0 : nseg] do
      let j := nseg - 1 - t
      let cj := mkIdent (Name.mkSimple s!"c{j+1}")
      let pj := mkIdent (Name.mkSimple s!"c{j}")
      mbody ← `(let $cj:ident := $(chkNm j) $pj
                $mbody)
    let c0 := mkIdent (Name.mkSimple "c0")
    let full ← `(let $c0:ident : Compiler.SlotEnv :=
                   Compiler.sourceEnvArr ($d).sources $iId $stId
                 $mbody)
    elabCommand (← `(command|
      def $f ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState) :
          Compiler.RuntimeResult := $full))
    markPhase "reify_model_emitted"
    logInfo m!"reify_design_chunked: {f} emitted, {nsrc} sources, \
      {nb} bindings, {nseg} chunk(s) of at most {csize}"
  | _ => throwUnsupportedSyntax

/-- `reify_design_shared <designCert> as <name>` — OPT-IN, under development.

NOT CHUNKED, and deliberately not called so. This emits ONE monolithic
let-chain over every binding. Bounded chunk functions -- `SlotEnv → SlotEnv`
over 16-64 pushes each, with a per-chunk theorem against `runBindings` composed
by `runBindings_append` -- are the next increment and are what a scalability
claim would need; a single let-chain can still meet an elaboration cliff on a
design the size of `commit_stage`.

The same emitted meaning as `reify_design_named`, with the DAG's sharing kept at
run time instead of only in the term.

`reify_design_named` gives every binding its own top-level `val{k} e0`, and an
operand is a CALL to one. The term stays linear in node count, which is what it
was for, but a value with `F` consumers is EVALUATED `F` times, recursively, so
one evaluation costs the number of paths rather than the number of nodes.
Measured on `instr_queue_gate` (1,186 bindings): the named model takes 135.86 s
for one sample where the straight-line legacy model takes 18.10 s, both `agree`.

Here each binding is a `let` in ONE function, so it is computed once per sample
however many consumers read it, and the term is still linear in node count --
neither axis carries the reconvergence factor.

Emits only the model. The proof bridge is a separate command; until it exists a
chunked model is RUNTIME ONLY and must not be credited as proved. -/
syntax (name := reifyDesignShared) "reify_design_shared " ident " as " ident : command

@[command_elab reifyDesignShared]
def elabReifyDesignShared : CommandElab := fun stx => do
  match stx with
  | `(command| reify_design_shared $d:ident as $f:ident) => do
    let R ← liftTermElabM do
      let dExpr ← Term.elabTerm d none
      let cert ← unsafe evalExpr DesignCert (mkConst ``DesignCert) dExpr
      match compileDesign cert with
      | .error _ => throwError "reify_design_shared: compileDesign refused {d}"
      | .ok R    => pure R
    let nsrc := R.sources.size
    let iId  := mkIdent (Name.mkSimple "i")
    let stId := mkIdent (Name.mkSimple "st")
    let e0   := mkIdent (Name.mkSimple "e0")
    let vId  := fun (k : Nat) => mkIdent (Name.mkSimple s!"v{k}")
    -- A ROOT reads a source from `e0` or a produced slot from its local.
    let rootRef : Bool → Nat → MetaM Term := fun isMem r =>
      if r < nsrc then
        (if isMem then `(Compiler.refMem $e0 $(quote r))
         else `(Compiler.refBV $e0 $(quote r)))
      else pure (vId (r - nsrc))
    let outs : Array Term ← R.outputs.mapM fun o => do
      let v ← liftTermElabM (rootRef false o.slot)
      `(bv_resize $(quote o.width) $v)
    let flops : Array Term ← (Array.ofFn (n := R.flopUpdates.size) (fun j => j.val)).mapM
      fun j => do
        let fu := R.flopUpdates[j]!
        let din ← liftTermElabM (rootRef false fu.din)
        let en ← liftTermElabM (match fu.enable with
          | none   => `(none)
          | some e => do `(some $(← rootRef false e)))
        let rp ← liftTermElabM (match fu.resetPin with
          | none   => `(none)
          | some r => do `(some $(← rootRef false r)))
        let rvq ← liftTermElabM (if fu.resetValue < 0
          then `(-(Int.ofNat $(quote fu.resetValue.natAbs)))
          else `(Int.ofNat $(quote fu.resetValue.toNat)))
        let ral := if fu.resetActiveLow then mkIdent ``true else mkIdent ``false
        `(Compiler.flopNextV $(quote fu.width) $din $en $rp $rvq $ral
            (($stId).flops[$(quote j)]?))
    let mems : Array Term ← R.memoryUpdates.mapM fun mu =>
      liftTermElabM (rootRef true mu.nextImg)
    -- innermost first, then wrap one `let` per binding, outermost = binding 0
    let mut body ← `({ outputs := #[$outs,*],
                       nextState := { flops := #[$flops,*], mems := #[$mems,*] } })
    for k in [0 : R.bindings.size] do
      let idx := R.bindings.size - 1 - k
      let b := R.bindings[idx]!
      let rhs ← liftTermElabM (NamedModel.valSyntax f.getId nsrc true b.rhs)
      body ← `(let $(vId idx):ident := $rhs
               $body)
    let full ← `(let $e0:ident : Compiler.SlotEnv :=
                   Compiler.sourceEnvArr ($d).sources $iId $stId
                 $body)
    elabCommand (← `(command|
      def $f ($iId : Compiler.RuntimeInput) ($stId : Compiler.RuntimeState) :
          Compiler.RuntimeResult := $full))
    logInfo m!"reify_design_shared: {f} emitted, {nsrc} sources, \
      {R.bindings.size} bindings"
  | _ => throwUnsupportedSyntax

/-- `reify_design <designCert> as <name>` -/
syntax (name := reifyDesign) "reify_design " ident " as " ident : command

@[command_elab reifyDesign]
def elabReifyDesign : CommandElab := fun stx => do
  match stx with
  | `(command| reify_design $d:ident as $f:ident) => do
    let R ← liftTermElabM do
      let dExpr ← Term.elabTerm d none
      let cert ← unsafe evalExpr DesignCert (mkConst ``DesignCert) dExpr
      match compileDesign cert with
      | .error _ => throwError "reify_design: compileDesign refused {d}"
      | .ok R    => pure R
    let nsrc := R.sources.size
    let body ← liftTermElabM do
      -- sources first: each is one `sourceValue` read, O(1) and outside the chain
      let mut lets : Array (TSyntax `Lean.Parser.Term.doSeqItem) := #[]
      let mut stmts : Array Term := #[]
      -- build innermost result, then wrap in lets from the inside out
      let outs : Array Term ← R.outputs.mapM fun o =>
        `(bv_resize $(quote o.width) $(slotIdent nsrc o.slot))
      let mems : Array Term ← R.memoryUpdates.mapM fun m =>
        `($(slotIdent nsrc m.nextImg))
      let flops : Array Term ← R.flopUpdates.mapIdxM fun idx fu => do
        let din := slotIdent nsrc fu.din
        let base ← `(bv_resize $(quote fu.width) $din)
        let withEn ← match fu.enable with
          | none   => pure base
          | some e => `(if bv_nonzero $(slotIdent nsrc e) then $base
                        else (st.flops[$(quote idx)]?).getD (mk_bv $(quote fu.width) 0))
        match fu.resetPin with
        | none   => pure withEn
        | some r =>
            let rv := slotIdent nsrc r
            let cond ← if fu.resetActiveLow then `(!bv_nonzero $rv) else `(bv_nonzero $rv)
            let rvq ← quoteInt fu.resetValue
            `(if $cond then mk_bv $(quote fu.width) $rvq else $withEn)
      let mut res ← `({ outputs := #[$outs,*],
                        nextState := { flops := #[$flops,*], mems := #[$mems,*] } })
      -- wrap the binding chain, innermost last
      for k in [0 : R.bindings.size] do
        let k' := R.bindings.size - 1 - k
        let b := R.bindings[k']!
        let nm := mkIdent (Name.mkSimple s!"v{k'}")
        let rhs ← rhsSyntax nsrc b.rhs
        res ← `(let $nm := $rhs; $res)
      for j in [0 : nsrc] do
        let j' := nsrc - 1 - j
        let nm := mkIdent (Name.mkSimple s!"s{j'}")
        let acc ← match R.sources[j']! with
          | .memImg _ _ _ | .memConst _ _ _ =>
              `(let $nm := (sourceValue i st ($d).sources[$(quote j')]!).asMem; $res)
          | _ => `(let $nm := (sourceValue i st ($d).sources[$(quote j')]!).asBV; $res)
        res := acc
      let _ := lets; let _ := stmts
      pure res
    elabCommand (← `(command|
      def $f (i : RuntimeInput) (st : RuntimeState) : RuntimeResult := $body))
    logInfo m!"reify_design: {f} emitted, {nsrc} sources, {R.bindings.size} bindings"
  | _ => throwUnsupportedSyntax

end Compiler

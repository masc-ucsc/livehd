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

namespace Compiler

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
      let rhs ← liftTermElabM (NamedModel.valSyntax base nsrc b.rhs)
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

import Lean
import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler
open Compiler.Residual
open Lean Elab Command Meta




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



def tinyCert : DesignCert :=
  { sources := #[SourceDesc.input 0 8, SourceDesc.input 1 8]
    nodes   := #[{ op := LGraphOp.Op_And, width := 8, deps := #[0, 1] },
                 { op := LGraphOp.Op_Or,  width := 8, deps := #[2, 1] }]
    outputs := #[{ slot := 3, width := 8 }]
    flops   := #[], memories := #[] }

reify_design tinyCert as tiny_step

#print tiny_step

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



def csr_buffer_gate_designCert : DesignCert :=
  {
    sources  := #[
        SourceDesc.flopQAsync 0 12 5 (0) true
      , SourceDesc.flopQAsync 1 1 5 (0) true
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 1 ((-Int.ofNat 1))
      , SourceDesc.const 1 (0)
      , SourceDesc.const 208 ((-Int.ofNat 1))
      , SourceDesc.const 8 ((Int.ofNat 131))
      , SourceDesc.const 64 ((Int.ofNat 18446744073709551615))
      , SourceDesc.const 7 ((Int.ofNat 67))
      , SourceDesc.const 4 ((Int.ofNat 12))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 14 ((-Int.ofNat 1))
      , SourceDesc.const 13 ((Int.ofNat 1))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 14 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 12 ((Int.ofNat 4095))
      , SourceDesc.const 4 ((Int.ofNat 12))
      , SourceDesc.const 1 ((Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 1 (0)
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 2 ((Int.ofNat 1))
      , SourceDesc.const 2 ((-Int.ofNat 1))
      , SourceDesc.const 1 (0)
      , SourceDesc.input 5 1
      , SourceDesc.input 3 1
      , SourceDesc.input 4 207
      , SourceDesc.input 2 1
      , SourceDesc.input 1 1
      ]
    nodes    := #[
        { op := LGraphOp.Op_GetMask, width := 2, deps := #[44, 2], origin := 236 }
      , { op := LGraphOp.Op_Not, width := 1, deps := #[45], origin := 140 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[46, 3], origin := 244 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[43, 4], origin := 232 }
      , { op := LGraphOp.Op_Or, width := 1, deps := #[1, 48], origin := 136 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[49, 5], origin := 240 }
      , { op := LGraphOp.Op_And, width := 1, deps := #[50, 47], origin := 144 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[51, 6], origin := 264 }
      , { op := LGraphOp.Op_EQ, width := 1, deps := #[7, 52], origin := 172 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[53, 8], origin := 260 }
      , { op := LGraphOp.Op_MuxN, width := 1, deps := #[54, 9, 10], origin := 164 }
      , { op := LGraphOp.Op_GetMask, width := 208, deps := #[42, 11], origin := 220 }
      , { op := LGraphOp.Op_SRA, width := 64, deps := #[56, 12], origin := 228 }
      , { op := LGraphOp.Op_And, width := 64, deps := #[13, 57], origin := 224 }
      , { op := LGraphOp.Op_SRA, width := 12, deps := #[42, 14], origin := 292 }
      , { op := LGraphOp.Op_Sext, width := 12, deps := #[59, 15], origin := 288 }
      , { op := LGraphOp.Op_SHL, width := 13, deps := #[60, 16], origin := 308 }
      , { op := LGraphOp.Op_GetMask, width := 14, deps := #[61, 17], origin := 304 }
      , { op := LGraphOp.Op_Or, width := 13, deps := #[18, 62], origin := 296 }
      , { op := LGraphOp.Op_SHL, width := 13, deps := #[0, 19], origin := 348 }
      , { op := LGraphOp.Op_GetMask, width := 14, deps := #[64, 20], origin := 344 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[1, 21], origin := 340 }
      , { op := LGraphOp.Op_Or, width := 13, deps := #[66, 65], origin := 32 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[43, 22], origin := 312 }
      , { op := LGraphOp.Op_EQ, width := 1, deps := #[23, 68], origin := 208 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[69, 24], origin := 284 }
      , { op := LGraphOp.Op_MuxN, width := 13, deps := #[70, 67, 63], origin := 192 }
      , { op := LGraphOp.Op_SRA, width := 12, deps := #[71, 25], origin := 204 }
      , { op := LGraphOp.Op_And, width := 12, deps := #[26, 72], origin := 200 }
      , { op := LGraphOp.Op_Sext, width := 12, deps := #[73, 27], origin := 212 }
      , { op := LGraphOp.Op_And, width := 1, deps := #[28, 71], origin := 196 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[43, 29], origin := 248 }
      , { op := LGraphOp.Op_Not, width := 1, deps := #[76], origin := 148 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[77, 30], origin := 256 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[44, 31], origin := 252 }
      , { op := LGraphOp.Op_And, width := 1, deps := #[79, 78], origin := 152 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[80, 32], origin := 280 }
      , { op := LGraphOp.Op_EQ, width := 1, deps := #[33, 81], origin := 188 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[82, 34], origin := 276 }
      , { op := LGraphOp.Op_MuxN, width := 1, deps := #[83, 75, 35], origin := 184 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[41, 36], origin := 272 }
      , { op := LGraphOp.Op_EQ, width := 1, deps := #[37, 85], origin := 180 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[86, 38], origin := 268 }
      , { op := LGraphOp.Op_MuxN, width := 1, deps := #[87, 84, 39], origin := 176 }
      ]
    outputs  := #[
        { slot := 0, width := 12 }
      , { slot := 55, width := 1 }
      , { slot := 58, width := 64 }
      ]
    flops    := #[
        { width := 12, din := 74, enable := none, resetPin := some 40, resetValue := (0), resetActiveLow := false }
      , { width := 1, din := 88, enable := none, resetPin := some 40, resetValue := (0), resetActiveLow := false }
      ]
    memories := #[]
  }


reify_design csr_buffer_gate_designCert as csr_step

def csr_residual : ResidualProgram :=
  match compileDesign csr_buffer_gate_designCert with
  | .ok R => R | .error _ => default

def mkInp (k : Nat) : RuntimeInput := Array.replicate 16 (mk_bv 32 (Int.ofNat k))
def st0 : RuntimeState := { flops := Array.replicate 16 (mk_bv 32 0), mems := #[] }

/-- Force every observable: outputs AND next-state flops.  Reading only
`outputs.size` lets the compiler delete the whole chain -- measured, and it
reported a 20,000-iteration loop as 0.012 ms. -/
@[inline] def obs (r : RuntimeResult) : Int :=
  r.outputs.foldl (fun a b => a + bv_uint b) 0
    + r.nextState.flops.foldl (fun a b => a + bv_uint b) 0

def loopFast (n : Nat) : Int :=
  let rec go (k : Nat) (acc : Int) : Int :=
    if k = 0 then acc else go (k-1) (acc + obs (csr_step (mkInp k) st0))
  go n 0

def loopDeep (n : Nat) : Int :=
  let rec go (k : Nat) (acc : Int) : Int :=
    if k = 0 then acc else go (k-1) (acc + obs (denoteResidual csr_residual (mkInp k) st0))
  go n 0

-- Agreement: the reified def and the interpreter must produce the same result.
#eval (List.range 50).all fun k =>
  obs (csr_step (mkInp k) st0) == obs (denoteResidual csr_residual (mkInp k) st0)

-- `#eval timeit (return e)` does NOT force `e` in the interpreter: it reported a
-- 20,000-iteration loop as 0.008 ms.  `IO.println` between timestamps forces.
def bench : IO Unit := do
  let _ ← IO.println s!"warm {loopDeep 500}"
  let t0 ← IO.monoMsNow
  IO.println s!"fast_result {loopFast 20000}"
  let t1 ← IO.monoMsNow
  IO.println s!"deep_result {loopDeep 20000}"
  let t2 ← IO.monoMsNow
  IO.println s!"SHALLOW_ms={t1-t0} DEEP_ms={t2-t1}"

#eval bench

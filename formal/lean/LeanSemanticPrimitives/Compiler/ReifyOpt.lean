/-
# `ReifyOpt` — tv on vc's OPTIMIZED program (round 1, Stage 6)

`reify_design D as F` reifies the program `compileDesign D` produces, every binding the
exporter wrote. `reify_design_opt D as F` reifies the program vc runs: the same program
after the proved optimizer (`ResidualOpt`: folding, copies, repeated expressions, branch
folding, unread code dropped, constants shared). `prove_reified_opt D as F` proves, in the
kernel, that the reified model is `interpretDesign D`.

The optimizer itself is never trusted and never evaluated by the kernel (it uses hash
maps). At elaboration time `reify_design_opt` runs it, checks its certificate with the
proved checker `validate`, and writes two literals: the certificate `F.cert` and the
optimized program `F.Ropt` (`ToExpr`). The proof then needs only kernel evaluation of
the checker and of the fixed rebuild on those literals:

    F.hR       : compileDesign D = .ok F.R                     (as `prove_reified`)
    F.valid    : validate F.R F.cert = true                    (decide +kernel)
    F.applied  : applyCert F.R F.cert = F.Ropt                 (decide +kernel)
    F.eq_denote: ∀ e i st, F e i st = denoteResidual F.Ropt e i st   (simp, as `prove_reified`)
    F.correct  : ∀ e i st, F e i st = interpretDesign D e i st
                 = F.eq_denote, F.applied, validate_sound F.valid, compileDesign_correct F.hR

`F.eq_denote` is `prove_reified`'s single `simp` over the whole binding chain, so this
path has `prove_reified`'s cost: it is for small designs. The kernel's arrays are lists,
so `validate` costs about (bindings)^2 list steps; that too is fine only for a small design.
-/
import LeanSemanticPrimitives.Compiler.ReifyProof
import LeanSemanticPrimitives.Compiler.ResidualOptSound

open Lean Elab Command Meta

namespace Compiler

deriving instance ToExpr for ValueType
deriving instance ToExpr for SourceDesc
deriving instance ToExpr for ResidualExpr
deriving instance ToExpr for ResidualBinding
deriving instance ToExpr for ResidualOutput
deriving instance ToExpr for ResidualFlopUpdate
deriving instance ToExpr for MemCommit
deriving instance ToExpr for ResidualMemoryUpdate
deriving instance ToExpr for ResidualProgram
deriving instance ToExpr for ResidualOpt.Just
deriving instance ToExpr for ResidualOpt.OptCert

/-- Add `nm : ty := val` (a literal), compiled, so the model can read it, with its equation
lemmas enabled, so `simp [nm]` can unfold it. -/
def addLiteralDef (nm : Name) (ty val : Expr) : CommandElabM Unit :=
  liftCoreM do
    addAndCompile <| .defnDecl
      { name := nm, levelParams := [], type := ty, value := val,
        hints := ReducibilityHints.abbrev, safety := DefinitionSafety.safe }
    enableRealizationsForConst nm

/-- `reify_design_opt <designCert> as <name>` -/
syntax (name := reifyDesignOpt) "reify_design_opt " ident " as " ident : command

@[command_elab reifyDesignOpt]
def elabReifyDesignOpt : CommandElab := fun stx => do
  match stx with
  | `(command| reify_design_opt $d:ident as $f:ident) => do
    let R ← residualOf "reify_design_opt" d
    let c := ResidualOpt.optimizeRaw R
    unless ResidualOpt.validate R c do
      throwError "reify_design_opt: the checker refused the optimizer's certificate for {d}"
    let R' := ResidualOpt.applyCert R c
    let nm := f.getId
    addLiteralDef (nm ++ `cert) (mkConst ``ResidualOpt.OptCert) (toExpr c)
    addLiteralDef (nm ++ `Ropt) (mkConst ``ResidualProgram) (toExpr R')
    let roptId := mkIdent (nm ++ `Ropt)
    let body ← liftTermElabM (reifiedModel R' (← `(($roptId).sources)))
    elabCommand (← `(command|
      def $f (e : Compiler.ClockEdges) (i : RuntimeInput) (st : RuntimeState) :
          RuntimeResult := $body))
    logInfo m!"reify_design_opt: {f} emitted from the optimized program: sources \
      {R.sources.size} -> {R'.sources.size}, bindings {R.bindings.size} -> {R'.bindings.size}"
  | _ => throwUnsupportedSyntax

/-- `prove_reified_opt <designCert> as <name>` (after `reify_design_opt` of the same) -/
syntax (name := proveReifiedOpt) "prove_reified_opt " ident " as " ident : command

@[command_elab proveReifiedOpt]
def elabProveReifiedOpt : CommandElab := fun stx => do
  match stx with
  | `(command| prove_reified_opt $d:ident as $f:ident) => do
    let R ← residualOf "prove_reified_opt" d
    let R' := ResidualOpt.applyCert R (ResidualOpt.optimizeRaw R)
    let hasTable := R'.memoryUpdates.any (fun m => !m.commits.isEmpty)
    let memA := mkIdent (if hasTable then ``Compiler.memNext_eq_memPick else ``Compiler.memNext)
    let memB := mkIdent (if hasTable then ``Compiler.memNext_of_isEmpty else ``Compiler.memNext)
    let nm      := f.getId
    let rNm     := mkIdent (nm ++ `R)
    let okNm    := mkIdent (nm ++ `compilesOk)
    let hRNm    := mkIdent (nm ++ `hR)
    let certNm  := mkIdent (nm ++ `cert)
    let roptNm  := mkIdent (nm ++ `Ropt)
    let validNm := mkIdent (nm ++ `valid)
    let applNm  := mkIdent (nm ++ `applied)
    let eqNm    := mkIdent (nm ++ `eq_denote)
    let corNm   := mkIdent (nm ++ `correct)
    elabCommand (← `(command|
      def $rNm : Compiler.ResidualProgram :=
        match Compiler.Seq.compileDesignL $d with
        | .ok R => R
        | .error _ => default))
    elabCommand (← `(command|
      theorem $okNm : Compiler.compilesOk $d = true := by
        rw [Compiler.Seq.compilesOk_eq_okL]; decide +kernel))
    elabCommand (← `(command|
      theorem $hRNm : Compiler.compileDesign $d = .ok $rNm :=
        Compiler.Seq.compileDesignL_ok_witness $d ((Compiler.Seq.compilesOk_eq_okL $d).symm.trans $okNm)))
    -- the proved checker and the fixed rebuild, evaluated by the kernel on the literals
    elabCommand (← `(command|
      theorem $validNm : Compiler.ResidualOpt.validate $rNm $certNm = true := by decide +kernel))
    elabCommand (← `(command|
      theorem $applNm : Compiler.ResidualOpt.applyCert $rNm $certNm = $roptNm := by decide +kernel))
    -- the reified model is the optimized program's semantics (`prove_reified`'s simp)
    elabCommand (← `(command|
      theorem $eqNm : ∀ e i st, $f e i st = Compiler.denoteResidual $roptNm e i st := by
        intro e i st
        simp (maxSteps := 4000000)
             [$f:ident, $roptNm:ident, Compiler.denoteResidual, Compiler.runBindings,
              Compiler.denoteExpr, Compiler.refBV, Compiler.refBVs, Compiler.refMem,
              Compiler.denoteRef, Compiler.sourceEnvArr, $memA:ident, $memB:ident,
              Compiler.memHeld, Compiler.flopNext_eq, Compiler.flopNextV_resize_din,
              CertVal.asBV, CertVal.asMem]))
    elabCommand (← `(command|
      theorem $corNm : ∀ e i st, $f e i st = Compiler.interpretDesign $d e i st := by
        intro e i st
        rw [$eqNm:ident e i st, ← $applNm:ident,
          Compiler.ResidualOpt.validate_sound $validNm e i st]
        exact Compiler.compileDesign_correct $d $rNm $hRNm e i st))
    auditOrThrow (nm ++ `correct) "prove_reified_opt"
    logInfo m!"prove_reified_opt: {corNm} proved: the reified optimized program \
      ({R'.bindings.size} bindings) is interpretDesign"
  | _ => throwUnsupportedSyntax

end Compiler

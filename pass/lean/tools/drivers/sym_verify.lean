/-
`sym_verify` -- prove an unbounded invariant of a design on its projection
residual (M3).

    sym_verify <cert> [!]<output>[,[!]<output>...] [asm:[!]<output>] [in:<input>=<value>]...
               [k:<n>[,<n>...]] [closed|rebuild] [route:fused|var|plain] [emit:<file>] [sf:<n>] [wf:<n>]
               [solver:<cadical>] [work:<dir>]

The specializer is `MemFused.projectMemFDesignAt`: the memory interpreter
with `env0` inlined and the node fold fused into its readers, whose residual
is close to linear in the design. It is correct by
`projectMemFDesignAtW_correct`, so the residual is a `Sym.MemResidual` by
`MemResidual.of_projF`. `route:var` uses `MemVariant.projectMemVarDesignAt`
(`env0` inlined only, `of_projVar`); `route:plain` uses the original
`projectMemDesignAt` (`of_proj`).

Outputs and inputs are named by ordinal or by name (names come from the
`<top>_io.json` next to the certificate). The property holds at a cycle when
the output is NONZERO (or ZERO, with `!`). Assumptions restrict which cycles
count: an output (`asm:`), and inputs held at fixed values (`in:`, e.g.
`in:rst_ni=0` for "reset held"). The initial state is all-zero (flops, memory
words, defaults).

Steps: load and admit the certificate; specialize the memory interpreter to
it ONCE; then, per candidate property and k (k-major,
first proved wins), build the BASE and STEP obligations of k-induction
(`Sym.baseObl`, `Sym.stepObl`); write each as DIMACS (`Sym.cnfOf`, the CNF
the checker checks); run the UNTRUSTED solver for a binary LRAT proof; then
evaluate the VERIFIED check `Sym.runInvKOf` on that residual, whose acceptance
is `Sym.runInvKOf_sound_ref` given the specializer run (the same trusted native
run, D1, as every FP route). The check is evaluated through
`Sym.runInvKOf_of_obls` over the obligations already built; `rebuild`
evaluates `runInvKOf` itself (repeats the symbolic evaluation), `closed` the
closed `Sym.runInvK` (also re-specializes; `runInvK_sound`). A satisfiable BASE obligation is a counterexample from the initial
state, replayed on the projection's simulator; a satisfiable STEP obligation
only says the property is not k-inductive.

RESULT lines: `INVARIANT PROVED (k=…)`, `COUNTEREXAMPLE (base)`,
`NOT K-INDUCTIVE (step)`, or a refusal with its reason. With several
candidates or k values each attempt's line is prefixed `[<out> k=<k>]` and a
`SUMMARY:` line follows. `emit:<file>` writes the F2 theorem form of the
proved attempt (stated over the closed `runInvK`).
-/
import HDLProof.Sym.ConjT
import LeanSemanticPrimitives.Projection.CertLoadAny
import Lean.Data.Json

open Compiler Projection HDLProof HDLProof.Sym Projection.MemProjection Projection.HwMem
open Std.Tactic.BVDecide

def say (s : String) : IO Unit := do
  IO.println s
  -- a log file is block-buffered: flush so a long run shows its progress
  (← IO.getStdout).flush

/-- The declared width of input ordinal `i` (from its `SourceDesc.input`), or 1. -/
def inputWidthOf (D : DesignCert) (i : Nat) : Nat :=
  (D.sources.toList.findSome? fun
    | .input idx w => if idx == i then some w else none
    | _ => none).getD 1

/-- `inputWidthOf D i` for every `i < n`, in ONE pass over the sources (the
first source declaring an input wins, as in `findSome?`; 1 when none does).
Mapping `inputWidthOf` over the inputs walked every source once per input. -/
def inputWidths (D : DesignCert) (n : Nat) : List Nat :=
  let t := D.sources.foldl (fun (t : Array (Option Nat)) s => match s with
    | .input idx w => if h : idx < t.size then (if t[idx].isNone then t.set idx (some w) else t) else t
    | _ => t) (Array.replicate n none)
  (List.range n).map fun i => (t[i]?.bind id).getD 1

def inputCount (D : DesignCert) : Nat :=
  D.sources.foldl (fun m s => match s with
    | .input idx _ => max m (idx + 1)
    | .flopQAsync _ _ ri _ _ => max m (ri + 1)
    | _ => m) 0

def shapeOf (D : DesignCert) : Shape :=
  ⟨D.flops.toList.map (·.width), D.memories.toList.map fun md => (md.aw, md.dw)⟩

def zeroState (D : DesignCert) : StateM :=
  { flops := D.flops.map (fun f => mk_bv f.width 0)
  , mems  := D.memories.map (fun md =>
      { words := List.replicate (2 ^ md.aw) (mk_bv md.dw 0), dflt := mk_bv md.dw 0 }) }

/-- A port by ordinal, or by name through the io.json beside the certificate. -/
def resolvePort (cert : String) (kind : String) (tok : String) : IO (Option Nat) := do
  if let some n := tok.toNat? then return some n
  let dir := (System.FilePath.mk cert).parent.getD "."
  let stem := (System.FilePath.mk cert).fileStem.getD ""
  let io := dir / s!"{stem}_io.json"
  unless ← io.pathExists do return none
  match Lean.Json.parse (← IO.FS.readFile io) with
  | .error _ => return none
  | .ok j =>
    match j.getObjValD kind |>.getArr? with
    | .error _ => return none
    | .ok outs =>
      for o in outs do
        if (o.getObjValD "name").getStr?.toOption == some tok then
          if let .ok n := (o.getObjValD "ordinal").getNat? then return some n
      return none

inductive SatAnswer | unsat (proof : ByteArray) | sat (model : String) | failed (why : String)

/-- Write `φ`'s CNF, run the solver, return its LRAT proof bytes. -/
def solve (solver : String) (work : System.FilePath) (tag : String) (φ : BVLogicalExpr) :
    IO (SatAnswer × Nat × Nat × Nat) := do
  let t0 ← IO.monoMsNow
  let cnfPath := work / s!"{tag}.cnf"
  let lratPath := work / s!"{tag}.lrat"
  -- forced HERE (a plain `let` is floated into its use, which put the
  -- bit-blast in the solver's time): bit-blast + CNF + DIMACS + write
  let (dimacs, nClauses) ← IO.lazyPure fun _ =>
    let cnf := cnfOf φ
    (Std.Sat.CNF.dimacs cnf, cnf.clauses.size)
  IO.FS.writeFile cnfPath dimacs
  let t1 ← IO.monoMsNow
  let out ← IO.Process.output
    { cmd := solver, args := #[cnfPath.toString, lratPath.toString, "--lrat", "--binary=true",
                               "--quiet", "--shrink=0"] }
  let t2 ← IO.monoMsNow
  if out.stdout.startsWith "s UNSATISFIABLE" then
    return (.unsat (← IO.FS.readBinFile lratPath), t1 - t0, t2 - t1, nClauses)
  else if out.stdout.startsWith "s SATISFIABLE" then
    return (.sat out.stdout, t1 - t0, t2 - t1, nClauses)
  else
    return (.failed s!"solver exit {out.exitCode}: {out.stdout.take 200}{out.stderr.take 200}",
            t1 - t0, t2 - t1, nClauses)


/-! ## Counterexample replay -/

/-- cadical's `v` lines: DIMACS variable → value. -/
def parseModel (stdout : String) : Std.HashMap Nat Bool := Id.run do
  let mut m : Std.HashMap Nat Bool := {}
  for line in stdout.splitOn "\n" do
    if line.startsWith "v " then
      for tok in (line.drop 2).toString.splitOn " " do
        if let some z := tok.toInt? then
          if z ≠ 0 then m := m.insert z.natAbs (0 < z)
  return m

/-- The value the model gives primary variable `var`, `w` bits wide. Bits the
formula never mentions are 0. -/
def modelValue (map : Std.HashMap BVBit Nat) (aigSize : Nat) (model : Std.HashMap Nat Bool)
    (var w : Nat) : Nat := Id.run do
  let mut v := 0
  for b in List.range w do
    if h : b < w then
      let bit : BVBit := { var, w, idx := ⟨b, h⟩ }
      match map[bit]? with
      | some c => if model.getD (c + aigSize + 1) false then v := v ||| (1 <<< b)
      | none   => pure ()
  return v

/-- Decode the base obligation's input variables into `k` input vectors. -/
def decodeInputs (φ : BVLogicalExpr) (model : Std.HashMap Nat Bool) (inW : List Nat) (k : Nat) :
    Array RuntimeInput := Id.run do
  let entry := φ.bitblast
  let (_, map) := entry.relabelNat'
  let aigSize := entry.aig.decls.size
  let nIn := inW.length
  let mut out := #[]
  for j in List.range k do
    out := out.push ((inW.zipIdx.map fun (w, t) =>
      mk_bv w (Int.ofNat (modelValue map aigSize model (j * nIn + t) w))).toArray)
  return out

/-- Run the decoded inputs on the projection's simulator from `s0`; the first
cycle whose assumptions held so far and whose property fails. -/
def replay (D : DesignCert) (R : Program) (sp : InvSpec) (s0 : StateM) (ins : Array RuntimeInput) :
    Option Nat := Id.run do
  let some sim := mkMemSimOfW D R | return none
  let mut st := s0
  let mut asmOK := true
  for j in List.range ins.size do
    match runMemSim sim (edgesAll D) ins[j]! st with
    | none => return none
    | some r =>
      asmOK := asmOK && asmBit r.outputs ins[j]! sp.asmOut sp.asmIn
      if asmOK && !propBit r.outputs sp.prop then return some j
      st := r.nextState
  return none


/-! ## F2: the theorem form -/

def leanStr (s : String) : String :=
  "\"" ++ (s.toList.map fun c =>
    if c == '"' then "\\\"" else if c == '\\' then "\\\\" else if c == '\n' then "\\n" else c.toString).foldl (· ++ ·) "" ++ "\""

def leanBytes (b : ByteArray) : String :=
  "⟨#[" ++ ", ".intercalate (b.toList.map toString) ++ "]⟩"

def leanProp (p : PropSpec) : String := s!"\{ out := {p.out}, nz := {p.nz} }"

def leanSpec (sp : InvSpec) : String :=
  let asmOut := match sp.asmOut with | none => "none" | some p => s!"some {leanProp p}"
  let asmIn := "[" ++ ", ".intercalate (sp.asmIn.map fun (k, c) => s!"({k}, {c})") ++ "]"
  let mds := "[" ++ ", ".intercalate (sp.L.mds.map fun (a, d) => s!"({a}, {d})") ++ "]"
  s!"\{ prop := {leanProp sp.prop}, asmOut := {asmOut}, asmIn := {asmIn}, k := {sp.k}, fuel := {sp.fuel}, \
inW := {sp.inW}, L := \{ fws := {sp.L.fws}, mds := {mds} } }"

/-- A self-contained Lean file whose `invariant` is `runInvKF_sound` (`runInvKV_sound`
with `route:var`, `runInvK_sound` with `route:plain`) applied to a
`native_decide` of the closed check on THIS certificate's text. -/
def emitF2 (path : System.FilePath) (name : String) (certText : String) (sf wf : Nat) (sp : InvSpec)
    (pfB pfS : ByteArray) (route : String) : IO Unit := do
  let chk := if route == "plain" then "runInvK" else if route == "var" then "runInvKV" else "runInvKF"
  let src := s!"/- Generated by sym_verify. The certificate is the DCERT2 text below; the
LRAT proofs came from an untrusted solver and are checked by `runInvK`, which
`native_decide` evaluates (trusted-native: one `_native` axiom). -/
import HDLProof.Sym.KInd
import LeanSemanticPrimitives.Compiler.CertIOCodec
open Compiler Projection HDLProof HDLProof.Sym

namespace SymF2.{name}

def certText : String := {leanStr certText}

def D : DesignCert := match CertIO.readCertChecked certText.toUTF8 with
  | .ok d => d
  | .error _ => default

def s0 : StateM :=
  \{ flops := D.flops.map (fun f => mk_bv f.width 0)
  , mems  := D.memories.map (fun md =>
      \{ words := List.replicate (2 ^ md.aw) (mk_bv md.dw 0), dflt := mk_bv md.dw 0 }) }

def spec : InvSpec := {leanSpec sp}

def pfB : ByteArray := {leanBytes pfB}
def pfS : ByteArray := {leanBytes pfS}

theorem verdict : {chk} {sf} {wf} D spec s0 pfB pfS = true := by native_decide

theorem invariant (ins : Nat → RuntimeInput) (hins : ∀ n, (ins n).toList.map BV.width = spec.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (refOut D (StateM.denote s0) ins j) (ins j) spec.asmOut spec.asmIn = true) →
      propBit (refOut D (StateM.denote s0) ins n) spec.prop = true :=
  {chk}_sound verdict ins hins

#print axioms invariant

end SymF2.{name}
"
  IO.FS.writeFile path src

/-- One k-induction attempt for one property, on the residual `R` specialized
once for all attempts. Returns the exit code and the verdict. -/
def attempt (cert : String) (solver : String) (work : System.FilePath) (sf wf : Nat)
    (D : DesignCert) (R : Program) (bound : Nat) (s0 : StateM) (tag : String) (prop : PropSpec)
    (asmOut : Option PropSpec) (asmIn : List (Nat × Int)) (kd : Nat) (closed rebuild : Bool) (route : String)
    (emit : Option String) : IO (UInt32 × String) := do
  let pre := if tag.isEmpty then "" else s!"[{tag} k={kd}] "
  let fin (code : UInt32) (v : String) : IO (UInt32 × String) := do
    say s!"  {pre}RESULT: {v}"; return (code, v)
  let nIn := inputCount D
  let sp : InvSpec :=
    { prop, asmOut, asmIn, k := kd, fuel := bound + 16
    , inW := inputWidths D nIn, L := shapeOf D }
  unless sideOK D sp s0 do
    return ← fin 4 "REFUSED (side checks: shape, input bound, flop bound, edges, or an input value out of range)"
  let t2 ← IO.monoMsNow
  let φB ← match baseObl D R sp s0 with
    | some φ => pure φ
    | none => do
        let why := match baseRun D R sp s0 with
          | .error m => m | .ok (_, st) => if scopedB st then "?" else "definitions not scoped"
        return ← fin 4 s!"REFUSED (base obligation: {why})"
  let φS ← match stepObl D R sp with
    | some φ => pure φ
    | none => do
        let why := match stepRun D R sp with
          | .error m => m
          | .ok ([], _) => "no step"
          | .ok (s :: _, st) =>
              if !scopedB st then "definitions not scoped"
              else if stateShapeOf s.2.2 != some sp.L then "the next state leaves the layout" else "?"
        return ← fin 4 s!"REFUSED (step obligation: {why})"
  let t3 ← IO.monoMsNow
  say s!"  {pre}obligations built in {t3 - t2} ms (symbolic evaluation of {kd} + {kd + 1} steps)"
  let wtag := if tag.isEmpty then "" else s!"{tag.map fun c => if c.isAlphanum then c else '_'}_k{kd}_"
  let (aB, bbB, satB, ncB) ← solve solver work s!"{wtag}base" φB
  say s!"  {pre}base: {ncB} clauses; bit-blast+CNF+write {bbB} ms, solver {satB} ms"
  let pfB ← match aB with
    | .unsat p => pure p
    | .sat model => do
        let ins := decodeInputs φB (parseModel model) sp.inW kd
        match replay D R sp s0 ins with
        | some j =>
            say s!"  {pre}counterexample inputs (cycle: ordinal values): {(ins.toList.take (j + 1)).zipIdx.map fun (i, c) => (c, i.toList.map bv_uint)}"
            return ← fin 1 s!"COUNTEREXAMPLE (base, replayed): the projection's simulator from the initial state fails the property at cycle {j} with assumptions holding"
        | none =>
            return ← fin 7 "COUNTEREXAMPLE NOT REPLAYED (base): the solver's model did not reproduce on the simulator"
    | .failed w => return ← fin 5 s!"SOLVER FAILED (base): {w}"
  let (aS, bbS, satS, ncS) ← solve solver work s!"{wtag}step" φS
  say s!"  {pre}step: {ncS} clauses; bit-blast+CNF+write {bbS} ms, solver {satS} ms"
  let pfS ← match aS with
    | .unsat p => pure p
    | .sat _ => return ← fin 1 s!"NOT K-INDUCTIVE (step) at k = {kd}: a larger k or a stronger property is needed"
    | .failed w => return ← fin 5 s!"SOLVER FAILED (step): {w}"
  let t4 ← IO.monoMsNow
  -- `admitsWB D` held before specializing, and `R` is this process's run of
  -- the specializer (the D1 trusted native run every FP route makes), so it is
  -- a `MemResidual` (`of_projF`; `of_projVar` / `of_proj` with `route:var` / `route:plain`);
  -- `runInvKOf_sound_ref` takes exactly those two facts. By default
  -- `runInvKOf` is evaluated through `runInvKOf_of_obls`: over the `baseObl` /
  -- `stepObl` values built above, so the symbolic evaluation is not repeated.
  -- `rebuild` evaluates `runInvKOf` itself; `closed` the closed `runInvK`,
  -- which also re-specializes.
  let closedName := if route == "plain" then "runInvK" else if route == "var" then "runInvKV" else "runInvKF"
  let mode := if closed then s!"closed {closedName}: re-specialize, re-build, "
    else if rebuild then "runInvKOf: re-build, " else "runInvKOf via runInvKOf_of_obls: "
  let ok ← if closed then IO.lazyPure (fun _ =>
             if route == "plain" then runInvK sf wf D sp s0 pfB pfS
             else if route == "var" then runInvKV sf wf D sp s0 pfB pfS
             else runInvKF sf wf D sp s0 pfB pfS)
           else if rebuild then IO.lazyPure (fun _ => runInvKOf D R sp s0 pfB pfS)
           else IO.lazyPure (fun _ => sideOK D sp s0 && checkUnsat φB pfB && checkUnsat φS pfS)
  let t5 ← IO.monoMsNow
  say s!"  {pre}verified check ({mode}LRAT-check both) in {t5 - t4} ms"
  if ok then
    if let some ef := emit then
      let name := ((System.FilePath.mk ef).fileStem.getD "F2").map fun c => if c.isAlphanum then c else '_'
      emitF2 ef name (← IO.FS.readFile cert) sf wf sp pfB pfS route
      say s!"  {pre}theorem form written to {ef} ({closedName}_sound via native_decide)"
    fin 0 s!"INVARIANT PROVED (k={kd}) -- runInvKOf_sound_ref: on every run from the all-zero state, the property holds at every cycle whose assumptions held"
  else
    fin 6 "CHECK REJECTED (the verified checker did not accept the proofs)"

/-! ## Conjunctive induction with register facts and case splits (M8)

`sym_verify <cert> conj:<spec> ...`. One conjunct (or one group) per line:

    <name> [!]<output>                   an output, nonzero (zero with `!`)
    <name> eq <reg> <reg>                two registers hold equal values
    <name> eqif <case> <reg> <reg>       ... whenever <case> holds
    <name> oneof <case>;<case>;...       the state is in one of these cases
    ... [hyps: <name> ...] [split: <name of a oneof>]

A <case> is `<reg>=<value>[,<reg>=<value>...]`. A register is a flop name from
the io sidecar, or the BASE name of a bit-blasted register (`x` for `x[0]`,
`x[1]`, ...): an `eq` pairs such registers bit by bit and a case assigns them
bit by bit. `hyps:` lists the conjuncts a target's STEP assumes (all when
absent); `split:` checks the target once per case of that `oneof`, each from a
template state in which the case's registers are constants and every `eq`
(and every `eqif` the case satisfies) shares one variable. Each case is one
symbolic run, built `runs:<n>` at a time (default: `jobs`; a run holds its
symbolic state until the final check); the obligations are solved in parallel
(`jobs:<n>`, default 8). The verdict is `runConjTOf` (`runConjTOf_sound_ref`),
evaluated as `sideOKC && checkT` over the runs built here. A satisfiable STEP
is decoded through its case's template into a state and two cycles of inputs;
every conjunct is evaluated on it at both cycles, and `<work>/cti_<target>.txt`
lists the conjuncts false at each cycle, the nonzero inputs, the registers that
change and the whole state by register name (untrusted diagnostics). `replay`
also re-runs the two cycles on the projection's simulator. -/

structure SpecLine where
  name  : String
  kind  : List String
  hyps  : Option (List String)
  split : Option String

def parseSpec (text : String) : Except String (List SpecLine) := do
  let mut out : List SpecLine := []
  for raw in text.splitOn "\n" do
    let line := ((raw.splitOn "#").headD "").replace "\t" " " |>.trim
    if line.isEmpty then continue
    match (line.splitOn " ").filter (fun w => !w.isEmpty) with
    | [] => continue
    | nm :: rest =>
      let mut kind : List String := []
      let mut hyps : Option (List String) := none
      let mut split : Option String := none
      let mut mode := 0
      for t in rest do
        if t == "hyps:" then mode := 1; hyps := some (hyps.getD [])
        else if t == "split:" then mode := 2
        else if mode == 0 then kind := kind ++ [t]
        else if mode == 1 then hyps := some (hyps.getD [] ++ [t])
        else split := some t
      if kind.isEmpty then throw s!"no conjunct in {repr line}"
      out := out ++ [{ name := nm, kind, hyps, split }]
  return out

def ioNames (cert : String) (kind : String) : IO (Array String) := do
  let dir := (System.FilePath.mk cert).parent.getD "."
  let stem := (System.FilePath.mk cert).fileStem.getD ""
  let io := dir / s!"{stem}_io.json"
  unless ← io.pathExists do return #[]
  match Lean.Json.parse (← IO.FS.readFile io) with
  | .error _ => return #[]
  | .ok j =>
    match j.getObjValD kind |>.getArr? with
    | .error _ => return #[]
    | .ok arr =>
      let mut out : Array String := #[]
      for o in arr do
        match (o.getObjValD "name").getStr?, (o.getObjValD "ordinal").getNat? with
        | .ok n, .ok k =>
            if k ≥ out.size then out := out ++ Array.replicate (k + 1 - out.size) ""
            out := out.set! k n
        | _, _ => pure ()
      return out

/-- A register's flop ordinals, LOWEST bit first: one flop by its name, or the
flops `nm[0]`, `nm[1]`, ... of a bit-blasted register. -/
def resolveReg (idx : Std.HashMap String Nat) (nm : String) : Option (List Nat) := Id.run do
  if let some k := idx[nm]? then return some [k]
  let mut bits : List Nat := []
  for i in [0:65536] do
    match idx[s!"{nm}[{i}]"]? with
    | some k => bits := bits ++ [k]
    | none => break
  return if bits.isEmpty then none else some bits

/-- `reg=value,...` as flop assignments (a bit-blasted register bit by bit). -/
def parseCase (idx : Std.HashMap String Nat) (fws : List Nat) (txt : String) : Except String Case := do
  let mut out : Case := []
  for a in (txt.splitOn ",").filter (fun w => !w.isEmpty) do
    match a.splitOn "=" with
    | [nm, vs] =>
      let some v := vs.toInt? | throw s!"bad value in {repr a}"
      let some bits := resolveReg idx nm | throw s!"no register {repr nm}"
      match bits with
      | [k] => out := out ++ [(k, v)]
      | _ =>
        for (k, i) in bits.zipIdx do
          unless fws.getD k 0 == 1 do throw s!"{nm}: bit {i} is not a one-bit flop"
          out := out ++ [(k, (v >>> i) % 2)]
    | _ => throw s!"expected <reg>=<value>, got {repr a}"
  return out

def showCase (names : Array String) (c : Case) : String :=
  if c.isEmpty then "-" else
  ",".intercalate ((c.filter (·.2 != 0)).map fun q => s!"{names.getD q.1 s!"flop{q.1}"}={q.2}")
    |> fun t => if t.isEmpty then "all zero" else t

/-- The decoded cycle-0 state of a STEP in case `cas`: a flop the template
substitutes takes its constant, or the variable it shares. -/
def decodeStepT (φ : BVLogicalExpr) (model : Std.HashMap Nat Bool) (L : Shape) (inW : List Nat) (T : Tmpl) :
    StateM × Array RuntimeInput := Id.run do
  let entry := φ.bitblast
  let (_, map) := entry.relabelNat'
  let aigSize := entry.aig.decls.size
  let val (var w : Nat) : Int := Int.ofNat (modelValue map aigSize model var w)
  let flops := (L.fws.zipIdx.map fun (w, f) =>
    match T.find? (fun x => x.key == f) with
    | some (.alias _ g) => mk_bv w (val g w)
    | some (.cnst _ v) => mk_bv w v
    | none => mk_bv w (val f w)).toArray
  let mut k := L.fws.length
  let mut mems : Array MemImg := #[]
  for (aw, dw) in L.mds do
    let words := (List.range (2 ^ aw)).map fun a => mk_bv dw (val (k + a) dw)
    mems := mems.push { words, dflt := mk_bv dw (val (k + 2 ^ aw) dw) }
    k := k + 2 ^ aw + 1
  let nIn := inW.length
  let base := layoutSize L
  let ins := (List.range 2).map fun j =>
    (inW.zipIdx.map fun (w, t) => mk_bv w (val (base + j * nIn + t) w)).toArray
  return ({ flops, mems }, ins.toArray)

/-! ## Counterexample evaluation (diagnostics only)
A counterexample report evaluates the decoded state through the run's definitions.
That output is untrusted: no verdict uses it. `evalD` is `BVExpr.eval` reading the
variables from an array, with every shift amount clamped to the width (`x <<< n` for
`n >= w` equals `x <<< w`), so a large symbolic amount never makes `Nat.shiftLeft`
build `2^n`. `extendD` extends the primary values through the definitions in order,
like `extendSt`, in linear time. -/
partial def evalD (get : Nat → PBV) : {w : Nat} → BVExpr w → BitVec w
  | w, .var idx => let p := get idx; if h : p.w = w then h ▸ p.bv else p.bv.truncate w
  | _, .const val => val
  | _, .extract start len e => BitVec.extractLsb' start len (evalD get e)
  | _, .bin l op r => op.eval (evalD get l) (evalD get r)
  | _, .un op e => op.eval (evalD get e)
  | _, .append l r h => h ▸ (evalD get l ++ evalD get r)
  | _, .replicate n e h => h ▸ BitVec.replicate n (evalD get e)
  | w, .shiftLeft l r => evalD get l <<< min (evalD get r).toNat w
  | w, .shiftRight l r => evalD get l >>> min (evalD get r).toNat w
  | w, .arithShiftRight l r => (evalD get l).sshiftRight (min (evalD get r).toNat w)

partial def evalLD (get : Nat → PBV) : BVLogicalExpr → Bool
  | .literal (.bin l op r) => op.eval (evalD get l) (evalD get r)
  | .literal (.getLsbD e i) => (evalD get e).getLsbD i
  | .const b => b
  | .not e => !evalLD get e
  | .gate g a b => g.eval (evalLD get a) (evalLD get b)
  | .ite c a b => if evalLD get c then evalLD get a else evalLD get b

def extendD (xs : Array PBV) (st : SymSt) : Array PBV := Id.run do
  let mut a := xs
  for d in st.defs do
    let v := evalD (fun i => a.getD i ⟨0#0⟩) d.2
    a := a.push ⟨v⟩
  return a

/-- Two concrete cycles from the decoded state: outputs of both, and the state
after the first. -/
def replay2 (D : DesignCert) (R : Program) (st : StateM) (ins : Array RuntimeInput) :
    Option (Array BV × StateM × Array BV) := do
  let sim ← mkMemSimOfW D R
  let r0 ← runMemSim sim (edgesAll D) ins[0]! st
  let r1 ← runMemSim sim (edgesAll D) ins[1]! r0.nextState
  pure (r0.outputs, r0.nextState, r1.outputs)

/-- Registers by name: bits `x[3]`, `x[2]`, ... of a bit-blasted register are
joined back into `x` (the last index only, so the bits of `mem[1][3]` form `mem[1]`). -/
def groupedState (names : Array String) (st : StateM) : List (String × String) := Id.run do
  let mut groups : Std.HashMap String (List (Nat × Nat × Int)) := {}
  let mut order : Array String := #[]
  for i in List.range st.flops.size do
    let b := st.flops[i]!
    let nm := names.getD i s!"flop{i}"
    let parts := nm.splitOn "["
    let (base, bit) := match parts.getLast? with
      | some r =>
        if parts.length ≥ 2 && r.endsWith "]" then
          match (r.dropRight 1).toNat? with
          | some k => ("[".intercalate parts.dropLast, k)
          | none => (nm, 0)
        else (nm, 0)
      | none => (nm, 0)
    unless groups.contains base do order := order.push base
    groups := groups.insert base ((bit, b.width, bv_uint b) :: groups.getD base [])
  return order.toList.map fun base =>
    let parts := groups.getD base []
    let v := parts.foldl (fun acc (k, w, x) => if w == 1 then acc + (x <<< k) else acc + x) (0 : Int)
    let w := parts.foldl (fun acc (k, w, _) => max acc (k + w)) 0
    (base, s!"{w}'h{(Nat.toDigits 16 v.toNat).asString}")

/-- `xs` in consecutive pieces of `k` (the last may be shorter). -/
def chunksOf (xs : List Nat) (k : Nat) : List (List Nat) :=
  (List.range ((xs.length + k - 1) / k)).map fun i => (xs.drop (i * k)).take k

def conjMain (cert specPath : String) (rest : List String) : IO UInt32 := do
  let opt (k : String) : Option String := rest.findSome? (fun t => (t.dropPrefix? (k ++ ":")).map (·.toString))
  let sf := ((opt "sf").bind (·.toNat?)).getD 20000
  let wf := ((opt "wf").bind (·.toNat?)).getD 200
  let jobs := max 1 (((opt "jobs").bind (·.toNat?)).getD 8)
  let solver ← match opt "solver" with
    | some s => pure s
    | none => do pure ((← IO.appDir) / "cadical").toString
  let work := System.FilePath.mk ((opt "work").getD "sym_work")
  IO.FS.createDirAll work
  let (_, D) ← Projection.CertLoadAny.loadAny cert
  let lines ← match parseSpec (← IO.FS.readFile specPath) with
    | .ok l => pure l
    | .error e => do IO.eprintln s!"{specPath}: {e}"; return 2
  if lines.isEmpty then IO.eprintln s!"{specPath}: no conjuncts"; return 2
  let flopNames ← ioNames cert "flops"
  let mut fidx : Std.HashMap String Nat := {}
  for (n, k) in flopNames.toList.zipIdx do
    unless n.isEmpty do fidx := fidx.insert n k
  let L := shapeOf D
  let propOfTok (tok : String) : IO (Option PropSpec) := do
    let (nz, name) := if tok.startsWith "!" then (false, (tok.drop 1).toString) else (true, tok)
    return (← resolvePort cert "outputs" name).map fun n => { out := n, nz }
  -- every line becomes a group of conjuncts
  let mut conjs : Array ConjK := #[]
  let mut cname : Array String := #[]
  let mut groups : Std.HashMap String (List Nat) := {}
  for l in lines do
    let mut ks : List ConjK := []
    match l.kind with
    | ["eq", a, b] =>
        let some ba := resolveReg fidx a | IO.eprintln s!"{l.name}: no register {repr a}"; return 2
        let some bb := resolveReg fidx b | IO.eprintln s!"{l.name}: no register {repr b}"; return 2
        unless ba.length == bb.length do IO.eprintln s!"{l.name}: {a} and {b} differ in width"; return 2
        ks := (ba.zip bb).map fun (f, g) => .eq f g
    | ["eqif", c, a, b] =>
        let cas ← match parseCase fidx L.fws c with
          | .ok x => pure x
          | .error e => do IO.eprintln s!"{l.name}: {e}"; return 2
        let some ba := resolveReg fidx a | IO.eprintln s!"{l.name}: no register {repr a}"; return 2
        let some bb := resolveReg fidx b | IO.eprintln s!"{l.name}: no register {repr b}"; return 2
        unless ba.length == bb.length do IO.eprintln s!"{l.name}: {a} and {b} differ in width"; return 2
        ks := (ba.zip bb).map fun (f, g) => .eqIf cas f g
    | ["oneof", cs] =>
        let mut cases : List Case := []
        for c in (cs.splitOn ";").filter (fun w => !w.isEmpty) do
          match parseCase fidx L.fws c with
          | .ok x => cases := cases ++ [x]
          | .error e => do IO.eprintln s!"{l.name}: {e}"; return 2
        ks := [.oneOf cases]
    | [tok] =>
        let some p ← propOfTok tok | IO.eprintln s!"{l.name}: no output {repr tok}"; return 2
        ks := [.out p]
    | k => IO.eprintln s!"{l.name}: cannot read {k}"; return 2
    let start := conjs.size
    for (k, i) in ks.zipIdx do
      conjs := conjs.push k
      cname := cname.push (if ks.length == 1 then l.name else s!"{l.name}[{i}]")
    groups := groups.insert l.name ((List.range ks.length).map (start + ·))
  let n := conjs.size
  let mut hyps : Array (Option (List Nat)) := Array.replicate n none
  let mut split : Array (Option Nat) := Array.replicate n none
  for l in lines do
    let some mine := groups[l.name]? | continue
    if let some hs := l.hyps then
      let mut idx : List Nat := []
      for h in hs do
        let some g := groups[h]? | IO.eprintln s!"{l.name}: no conjunct named {repr h}"; return 2
        idx := idx ++ g
      for t in mine do hyps := hyps.set! t (some idx)
    if let some sname := l.split then
      match groups[sname]? with
      | some [s] =>
          match conjs[s]? with
          | some (.oneOf _) => for t in mine do split := split.set! t (some s)
          | _ => IO.eprintln s!"{l.name}: split {sname} is not a oneof"; return 2
      | _ => IO.eprintln s!"{l.name}: split {sname} is not a oneof"; return 2
  let asmOut ← match opt "asm" with
    | none => pure none
    | some a => do
        let some p ← propOfTok a | IO.eprintln s!"no output {repr a}"; return 2
        pure (some p)
  let mut asmIn : List (Nat × Int) := []
  for tok in rest.filterMap (fun t => (t.dropPrefix? "in:").map (·.toString)) do
    match tok.splitOn "=" with
    | [nm, vs] =>
        let some k ← resolvePort cert "inputs" nm | IO.eprintln s!"no input {repr nm}"; return 2
        let some v := vs.toInt? | IO.eprintln s!"bad value {repr vs}"; return 2
        asmIn := asmIn ++ [(k, v)]
    | _ => IO.eprintln s!"in:<input>=<value>, got {repr tok}"; return 2
  say s!"{cert}"
  say s!"  shape: sources {D.sources.size} nodes {D.nodes.size} outputs {D.outputs.size} \
flops {D.flops.size} memories {D.memories.size} clocks {D.clocks.size}"
  unless HwMem.admitsWB D do
    say "  RESULT: REFUSED (the memory route does not admit this certificate)"; return 4
  let t0 ← IO.monoMsNow
  let res ← IO.lazyPure (fun _ => MemFused.projectMemFDesignAt sf wf D)
  let R ← match res with
    | .ok R => pure R
    | .error e => do say s!"  RESULT: REFUSED (specialization failed: {repr e})"; return 4
  let some bound := Hw.checkResidual R
    | say "  RESULT: REFUSED (checkResidual rejected the residual)"; return 4
  let t1 ← IO.monoMsNow
  say s!"  residual specialized (projectMemFDesignAt, fold fused) and checked in {t1 - t0} ms (bound {bound})"
  let s0 := zeroState D
  let sp : ConjTSpec := { conjs := conjs.toList, hyps := hyps.toList, split := split.toList, asmOut, asmIn,
                          fuel := bound, inW := inputWidths D (inputCount D), L }
  let allC := sp.allCases
  let nObl := (List.range n).foldl (fun acc t => acc + (sp.casesOf t).length) 0
  say s!"  conjunctive invariant: {lines.length} lines, {n} conjuncts, {allC.length} case(s), {nObl} step obligations; \
k = 1; jobs {jobs}; initial state all-zero"
  unless sideOKC D sp.plain s0 do say "  RESULT: REFUSED (side conditions)"; return 4
  -- the base run, the plain step run (layout), and one run per case, in parallel
  let tb ← IO.asTask (prio := .dedicated) (IO.lazyPure fun _ => baseRunC D R sp.plain s0)
  let tp ← IO.asTask (prio := .dedicated) (IO.lazyPure fun _ => stepRunC D R sp.plain)
  -- case runs, `runs` at a time (each holds a symbolic run in memory until the final check)
  let runJobs := max 1 (((opt "runs").bind (·.toNat?)).getD jobs)
  let mut runList : List (Case × Except String (List StepO × SymSt)) := []
  for batch in chunksOf (List.range allC.length) runJobs do
    let mut tcs := #[]
    for j in batch do
      let c := allC.getD j []
      tcs := tcs.push (c, ← IO.asTask (prio := .dedicated) (IO.lazyPure fun _ => stepRunT D R sp c))
    for (c, task) in tcs.toList do
      match ← IO.wait task with
      | .ok r => runList := runList ++ [(c, r)]
      | .error e => do say s!"  RESULT: REFUSED ({e})"; return 4
  let rb ← match ← IO.wait tb with | .ok r => pure r | .error e => do say s!"  RESULT: REFUSED ({e})"; return 4
  let r0 ← match ← IO.wait tp with | .ok r => pure r | .error e => do say s!"  RESULT: REFUSED ({e})"; return 4
  let t2 ← IO.monoMsNow
  say s!"  symbolic runs built in {t2 - t1} ms (base, plain step, {allC.length} case run(s))"
  let (sB, stB) ← match rb with
    | .ok ([s], st) => pure (s, st)
    | .ok _ => do say "  RESULT: REFUSED (base run shape)"; return 4
    | .error e => do say s!"  RESULT: REFUSED (base run: {e})"; return 4
  match r0 with
  | .ok (a :: _, st) =>
      unless scopedB st && decide (stateShapeOf a.2.2 = some sp.L) do
        say "  RESULT: REFUSED (the step leaves the layout)"; return 4
  | .ok _ => do say "  RESULT: REFUSED (plain step run shape)"; return 4
  | .error e => do say s!"  RESULT: REFUSED (plain step run: {e})"; return 4
  for (c, r) in runList do
    match r with
    | .ok ([_, _], st) => unless scopedB st do say s!"  RESULT: REFUSED (case {showCase flopNames c}: definitions not scoped)"; return 4
    | .ok _ => do say "  RESULT: REFUSED (case run shape)"; return 4
    | .error e => do say s!"  RESULT: REFUSED (case {showCase flopNames c}: {e})"; return 4
  unless scopedB stB do say "  RESULT: REFUSED (base definitions not scoped)"; return 4
  let some φB := baseOblT sp s0 stB sB | say "  RESULT: REFUSED (a conjunct could not be read)"; return 4
  let (aB, bbB, satB, ncB) ← solve solver work "conj_base" φB
  say s!"  base: {ncB} clauses; bit-blast+CNF+write {bbB} ms, solver {satB} ms"
  let pfB ← match aB with
    | .unsat p => pure p
    | .sat _ => do say "  RESULT: COUNTEREXAMPLE (base): some conjunct fails from the initial state"; return 1
    | .failed w => do say s!"  RESULT: SOLVER FAILED (base): {w}"; return 5
  -- every (target, case) obligation, `jobs` at a time
  let mut items : Array (Nat × Nat × Case) := #[]
  for t in List.range n do
    for (c, m) in (sp.casesOf t).zipIdx do
      items := items.push (t, m, c)
  let mut pfs : Array (Array ByteArray) :=
    (List.range n).toArray.map fun t => Array.replicate (sp.casesOf t).length ByteArray.empty
  let mut failing : List String := []
  let mut totalC := 0
  let mut maxC := 0
  for batch in chunksOf (List.range items.size) jobs do
    let mut tasks := #[]
    for j in batch do
      let (t, m, c) := items[j]!
      let task ← IO.asTask (prio := .dedicated) (do
        let some (.ok ([a, b], st)) := findRun runList c
          | return (j, SatAnswer.failed "no run for the case", 0, 0, 0, none)
        let some φ := stepOblT sp c st a b t
          | return (j, SatAnswer.failed "the obligation could not be built", 0, 0, 0, none)
        let (ans, bb, sat, nc) ← solve solver work s!"conj_{t}_{m}" φ
        return (j, ans, bb, sat, nc, some φ))
      tasks := tasks.push task
    for task in tasks do
      match ← IO.wait task with
      | .error e => do say s!"  RESULT: SOLVER FAILED (task: {e})"; return 5
      | .ok (j, ans, bb, sat, nc, φo) =>
        let (t, m, c) := items[j]!
        let tag := if (sp.casesOf t).length == 1 then cname[t]! else s!"{cname[t]!} | {showCase flopNames c}"
        totalC := totalC + nc
        maxC := max maxC nc
        match ans with
        | .unsat p =>
            pfs := pfs.modify t (·.set! m p)
            say s!"  [{tag}] {nc} clauses; bit-blast+CNF+write {bb} ms, solver {sat} ms: UNSAT"
        | .sat model =>
            failing := failing ++ [tag]
            say s!"  [{tag}] {nc} clauses; bit-blast+CNF+write {bb} ms, solver {sat} ms: SAT (not preserved)"
            if let some φ := φo then
              if let some (.ok ([a, b], st)) := findRun runList c then
                -- the model's primary values: the cycle-0 state (through the case's
                -- template) and both cycles' inputs; every cut variable then follows
                -- from the run's definitions (extendSt, as in the soundness proofs), so
                -- every conjunct and register is evaluated exactly, without simulating
                let (st0, ins) := decodeStepT φ (parseModel model) sp.L sp.inW (sp.tmplOf c)
                let xs := (stateVals st0 ++ inputVals ins[0]! ++ inputVals ins[1]!).toArray
                let σa := extendD xs st
                let get := fun i => σa.getD i ⟨0#0⟩
                let s0V := stateVarT sp.L (sp.tmplOf c) 0
                let falseAt (sv so : SVal) : List String := (List.range n).filterMap fun i =>
                  match holdsE sv so (conjs[i]!) with
                  | some e => if evalLD get e then none else some (cname[i]!)
                  | none => some s!"{cname[i]!}(unreadable)"
                let f0 := falseAt s0V a.2.1
                let f1 := falseAt a.2.2 b.2.1
                let fl1 : Array BV := (List.range sp.L.fws.length).toArray.map fun f =>
                  match flopE a.2.2 f with
                  | some ⟨w, e⟩ => mk_bv w (Int.ofNat (evalD get e).toNat)
                  | none => mk_bv 1 0
                let regs0 := groupedState flopNames st0
                let regs1 := groupedState flopNames { flops := fl1, mems := #[] }
                let changed := (regs0.zip regs1).filterMap fun ((r, v0), (_, v1)) =>
                  if v0 == v1 then none else some s!"  {r}: {v0} -> {v1}"
                let inNames ← ioNames cert "inputs"
                let safe := tag.map fun ch => if ch.isAlphanum || ch == '_' then ch else '_'
                let f := work / s!"cti_{safe}.txt"
                let inLine (i : RuntimeInput) : String := " ".intercalate ((List.range i.size).filterMap fun k =>
                  let v := bv_uint i[k]!
                  if v == 0 then none else some s!"{inNames.getD k s!"in{k}"}={v}")
                IO.FS.writeFile f s!"counterexample to induction for {tag}\n\
conjuncts FALSE at cycle 0: {f0}\nconjuncts FALSE at cycle 1: {f1}\n\
nonzero inputs, cycle 0: {inLine ins[0]!}\nnonzero inputs, cycle 1: {inLine ins[1]!}\n\
registers that change from cycle 0 to cycle 1:\n{"\n".intercalate changed}\n\
state at cycle 0:\n{"\n".intercalate (regs0.map fun (r, v) => s!"  {r} = {v}")}\n"
                say s!"  [{tag}] counterexample: false at cycle 0 {f0}, at cycle 1 {f1}; state, inputs and changes in {f}"
                -- optional concrete cross-check on the projection's simulator (slow on big designs)
                if rest.contains "replay" then
                  match replay2 D R st0 ins with
                  | none => say s!"  [{tag}] replay: the simulator refused the decoded state"
                  | some (o0, st1, o1) =>
                    let broke := !(sp.conjs.getD t default).holds st1.flops o1
                    let hs := (sp.hypsOf t).all fun i => (sp.conjs.getD i default).holds st0.flops o0
                    say s!"  [{tag}] replay: hypotheses at cycle 0 {hs}, broken at cycle 1 {broke}"
        | .failed w => do say s!"  [{tag}] RESULT: SOLVER FAILED: {w}"; return 5
  let t3 ← IO.monoMsNow
  say s!"  steps: {items.size} obligations, {totalC} clauses in total, largest {maxC}; {t3 - t2} ms"
  unless failing.isEmpty do
    say s!"  RESULT: NOT INDUCTIVE (step): not preserved under their hypotheses: {failing}"
    return 1
  let pfL := pfs.toList.map (·.toList)
  let ok ← IO.lazyPure (fun _ => sideOKC D sp.plain s0 && checkT sp s0 rb r0 runList pfB pfL)
  let t4 ← IO.monoMsNow
  say s!"  verified check (runConjTOf = sideOKC && checkT over the runs above; LRAT-check all) in {t4 - t3} ms"
  if ok then
    say s!"  RESULT: INVARIANT PROVED (conjunctive, k=1) -- runConjTOf_sound_ref: on every run from the all-zero state, every conjunct holds at every cycle whose assumptions held"
    return 0
  else
    say "  RESULT: CHECK REJECTED (the verified checker did not accept the proofs)"
    return 6

def main (args : List String) : IO UInt32 := do
  match args with
  | cert :: props :: rest =>
    if let some spec := props.dropPrefix? "conj:" then
      return ← conjMain cert spec.toString rest
    let opt (k : String) : Option String := rest.findSome? (fun t => (t.dropPrefix? (k ++ ":")).map (·.toString))
    let ks := (((opt "k").getD "1").splitOn ",").filterMap (·.toNat?)
    let sf := ((opt "sf").bind (·.toNat?)).getD 20000
    let wf := ((opt "wf").bind (·.toNat?)).getD 200
    let solver ← match opt "solver" with
      | some s => pure s
      | none => do
          let sys ← IO.appDir
          pure (sys / "cadical").toString
    let work := System.FilePath.mk ((opt "work").getD "sym_work")
    IO.FS.createDirAll work
    let (_, D) ← Projection.CertLoadAny.loadAny cert
    let propOfTok (tok : String) : IO (Option PropSpec) := do
      let (nz, name) := if tok.startsWith "!" then (false, (tok.drop 1).toString) else (true, tok)
      return (← resolvePort cert "outputs" name).map fun n => { out := n, nz }
    let mut cands : List (String × PropSpec) := []
    for tok in props.splitOn "," do
      let some p ← propOfTok tok | IO.eprintln s!"no output {repr tok}"; return 2
      cands := cands ++ [(tok, p)]
    if ks.isEmpty then IO.eprintln "k:<n>[,<n>...]"; return 2
    let asmOut ← match opt "asm" with
      | none => pure none
      | some a => do
          let some p ← propOfTok a | IO.eprintln s!"no output {repr a}"; return 2
          pure (some p)
    let mut asmIn : List (Nat × Int) := []
    for tok in rest.filterMap (fun t => (t.dropPrefix? "in:").map (·.toString)) do
      match tok.splitOn "=" with
      | [nm, vs] =>
          let some k ← resolvePort cert "inputs" nm | IO.eprintln s!"no input {repr nm}"; return 2
          let some v := vs.toInt? | IO.eprintln s!"bad value {repr vs}"; return 2
          asmIn := asmIn ++ [(k, v)]
      | _ => IO.eprintln s!"in:<input>=<value>, got {repr tok}"; return 2
    say s!"{cert}"
    say s!"  shape: sources {D.sources.size} nodes {D.nodes.size} outputs {D.outputs.size} \
flops {D.flops.size} memories {D.memories.size} clocks {D.clocks.size}"
    let pstr (p : PropSpec) : String := s!"output {p.out} {if p.nz then "nonzero" else "zero"}"
    say s!"  propert{if cands.length == 1 then "y" else "ies (first proved wins)"}: \
{", ".intercalate (cands.map fun (t, p) => s!"{t} = {pstr p}")}\
{match asmOut with | some a => s!", assuming {pstr a}" | none => ""}\
{if asmIn.isEmpty then "" else s!", assuming inputs {asmIn}"}; k in {ks}; initial state all-zero"
    unless HwMem.admitsWB D do
      say "  RESULT: REFUSED (the memory route does not admit this certificate)"; return 4
    let t0 ← IO.monoMsNow
    let route := (opt "route").getD "fused"
    unless ["fused", "var", "plain"].contains route do
      IO.eprintln s!"route:{route} is not one of fused | var | plain"; return 2
    let res ← IO.lazyPure (fun _ =>
      if route == "plain" then projectMemDesignAt sf wf D
      else if route == "var" then MemVariant.projectMemVarDesignAt sf wf D
      else MemFused.projectMemFDesignAt sf wf D)
    let R ← match res with
      | .ok R => pure R
      | .error e => do say s!"  RESULT: REFUSED (specialization failed: {repr e})"; return 4
    let some bound := Hw.checkResidual R
      | say "  RESULT: REFUSED (checkResidual rejected the residual)"; return 4
    let t1 ← IO.monoMsNow
    let projName := if route == "plain" then "projectMemDesignAt"
      else if route == "var" then "projectMemVarDesignAt, env0 inlined" else "projectMemFDesignAt, fold fused"
    say s!"  residual specialized ({projName}) and checked in {t1 - t0} ms (bound {bound})"
    let s0 := zeroState D
    let single := cands.length == 1 && ks.length == 1
    let mut last : UInt32 := 2
    let mut verdicts : List String := []
    for kd in ks do
      for (tok, prop) in cands do
        let (code, v) ← attempt cert solver work sf wf D R bound s0 (if single then "" else tok) prop
          asmOut asmIn kd (rest.contains "closed") (rest.contains "rebuild") route (opt "emit")
        verdicts := verdicts ++ [s!"{tok} k={kd}: {(v.splitOn " --").head!}"]
        last := code
        if code == 0 then
          unless single do say s!"  SUMMARY: {tok} PROVED at k={kd} after {verdicts.length} attempt(s)"
          return 0
        -- a refusal or a solver failure not tied to the property repeats for every candidate
        if code == 4 || code == 5 then
          unless single do say s!"  SUMMARY: stopped ({v})"
          return code
    unless single do say s!"  SUMMARY: none proved: {verdicts}"
    return last
  | _ =>
    IO.eprintln "usage: sym_verify <cert> [!]<out>[,[!]<out>...] [asm:<output>] [in:<input>=<value>]... [k:<n>[,<n>...]] [closed|rebuild] [route:fused|var|plain] [emit:<file>] [sf:<n>] [wf:<n>] [solver:<cadical>] [work:<dir>]"
    return 2

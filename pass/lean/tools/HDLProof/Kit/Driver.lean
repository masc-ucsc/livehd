/-
`Kit.Driver` -- the drivers' shared IO helpers: certificate shapes and port names (through
the `_io.json` sidecar), the solver call, model decoding, conj spec parsing, and the
counterexample evaluator. Untrusted: no verdict depends on anything here (a verdict is a
checker's closed Bool, evaluated over the values the driver built). No theorems.
-/
import HDLProof.Sym.ConjT
import LeanSemanticPrimitives.Projection.CertLoadAny
import Lean.Data.Json

open Compiler Projection HDLProof HDLProof.Sym Projection.MemProjection Projection.HwMem
open Std.Tactic.BVDecide

namespace HDLProof.Driver

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
def solve (solver : String) (work : System.FilePath) (tag : String) (φ : BVLogicalExpr)
    (extra : Array String := #[]) : IO (SatAnswer × Nat × Nat × Nat) := do
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
                               "--quiet", "--shrink=0"] ++ extra }
  let t2 ← IO.monoMsNow
  if out.stdout.startsWith "s UNSATISFIABLE" then
    return (.unsat (← IO.FS.readBinFile lratPath), t1 - t0, t2 - t1, nClauses)
  else if out.stdout.startsWith "s SATISFIABLE" then
    return (.sat out.stdout, t1 - t0, t2 - t1, nClauses)
  else
    return (.failed s!"solver exit {out.exitCode}: {out.stdout.take 200}{out.stderr.take 200}",
            t1 - t0, t2 - t1, nClauses)



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


/-- The ports of a kind (`inputs`, `outputs`, `flops`) with their widths, by ordinal. -/
def ioPorts (cert : String) (kind : String) : IO (Array (String × Nat)) := do
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
      let mut out : Array (String × Nat) := #[]
      for o in arr do
        match (o.getObjValD "name").getStr?, (o.getObjValD "ordinal").getNat?,
            (o.getObjValD "width").getNat? with
        | .ok n, .ok k, .ok w =>
            if k ≥ out.size then out := out ++ Array.replicate (k + 1 - out.size) ("", 0)
            out := out.set! k (n, w)
        | _, _, _ => pure ()
      return out

end HDLProof.Driver

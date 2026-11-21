/-
# Port names: the `_io.json` sidecar

`lhd` writes `<top>_io.json` beside every certificate: each input, output and flop with its
ordinal, width and RTL name. A driver binds ports by name through it, so nothing hard-codes an
ordinal. A token that is a number is taken as an ordinal directly.
-/
import Lean.Data.Json

namespace HDLProof
namespace Run

structure Port where
  name    : String
  ordinal : Nat
  width   : Nat
deriving Inhabited, Repr, BEq

structure IoMeta where
  inputs  : Array Port := #[]
  outputs : Array Port := #[]
  flops   : Array Port := #[]
deriving Inhabited, Repr

/-- The sidecar's path for a certificate `<dir>/<stem>.dcert`. -/
def ioPath (cert : String) : System.FilePath :=
  let p := System.FilePath.mk cert
  (p.parent.getD ".") / s!"{p.fileStem.getD ""}_io.json"

private def portsOf (j : Lean.Json) (kind : String) : Array Port := Id.run do
  match (j.getObjValD kind).getArr? with
  | .error _ => return #[]
  | .ok arr =>
    let mut out : Array Port := #[]
    for o in arr do
      match (o.getObjValD "name").getStr?, (o.getObjValD "ordinal").getNat?,
            (o.getObjValD "width").getNat? with
      | .ok n, .ok k, .ok w => out := out.push { name := n, ordinal := k, width := w }
      | _, _, _ => pure ()
    return out.qsort (·.ordinal < ·.ordinal)

def IoMeta.parse (txt : String) : Except String IoMeta := do
  let j ← Lean.Json.parse txt
  return { inputs := portsOf j "inputs", outputs := portsOf j "outputs", flops := portsOf j "flops" }

/-- The sidecar beside `cert`, or an empty table when there is none. -/
def IoMeta.load (cert : String) : IO IoMeta := do
  let p := ioPath cert
  unless ← p.pathExists do return {}
  match IoMeta.parse (← IO.FS.readFile p) with
  | .ok m => return m
  | .error e => throw (IO.userError s!"{p}: {e}")

/-- A port of `ports` by name, or by ordinal when `tok` is a number. -/
def findPort (ports : Array Port) (tok : String) : Option Port :=
  match tok.toNat? with
  | some k => ports.find? (·.ordinal == k) |>.orElse fun _ => some { name := tok, ordinal := k, width := 0 }
  | none => ports.find? (·.name == tok)

def IoMeta.input? (m : IoMeta) (tok : String) : Option Port := findPort m.inputs tok
def IoMeta.output? (m : IoMeta) (tok : String) : Option Port := findPort m.outputs tok

def IoMeta.inputOrd (m : IoMeta) (tok : String) : Except String Nat :=
  match m.input? tok with
  | some p => .ok p.ordinal
  | none => .error s!"the design has no input `{tok}` (names come from the _io.json beside the certificate)"

def IoMeta.outputOrd (m : IoMeta) (tok : String) : Except String Nat :=
  match m.output? tok with
  | some p => .ok p.ordinal
  | none => .error s!"the design has no output `{tok}` (names come from the _io.json beside the certificate)"

/-- An output's name, or `out<k>`. -/
def IoMeta.outputName (m : IoMeta) (k : Nat) : String :=
  (m.outputs.find? (·.ordinal == k)).map (·.name) |>.getD s!"out{k}"

def IoMeta.inputName (m : IoMeta) (k : Nat) : String :=
  (m.inputs.find? (·.ordinal == k)).map (·.name) |>.getD s!"in{k}"

end Run
end HDLProof

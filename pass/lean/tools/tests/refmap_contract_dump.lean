/-
`refmap_contract_dump <file.refmap>...`: the judge's reading of each file's contract
(`HDLProof.Kit.buildJudge`, the parser `sym_refmap` uses), in a canonical form, one item per
line after a `== <file>` line: the chan's response inputs and observed outputs, the slot (its
firing formula as a prefix term, its response ports, its observed words with the slice and the
guard resolved), every `xfer`, `maxstall`, `period`, `asm`. `scripts/mk_product.py --dump` prints
the product's reading of the same file in the same form; `judge_selftest.sh` compares the two, so
the judge and the product search read one contract from one file. Exit 1 when a file is refused.
-/
import HDLProof.Kit.RefMapFile

open Compiler Projection HDLProof HDLProof.Sym HDLProof.Check HDLProof.Kit HDLProof.Driver

def canonPort (NI : Names) : PortW → String
  | .out o nxt => s!"out:{(NI.outW.getD o ("?", 0)).1}{if nxt then "@next" else ""}"
  | .inp k => s!"in:{(NI.inW.getD k ("?", 0)).1}"

def canonBit (NI : Names) : BitE → String
  | .tt => "tt"
  | .ff => "ff"
  | .nz p => s!"nz({canonPort NI p})"
  | .eqc p v => s!"eq({canonPort NI p},{v})"
  | .not a => s!"not({canonBit NI a})"
  | .and a b => s!"and({canonBit NI a},{canonBit NI b})"
  | .or a b => s!"or({canonBit NI a},{canonBit NI b})"

def canonWord (NI : Names) (w : ObsW) : String :=
  s!"{canonPort NI w.port}[{w.lo}:{w.len}]" ++
    match w.guard with
    | some (g, b) => s!"?{canonPort NI g}.{b}"
    | none => ""

def main (args : List String) : IO UInt32 := do
  for path in args do
    IO.println s!"== {path}"
    let lines := parseRM (← IO.FS.readFile path)
    let dir := (System.FilePath.mk path).parent.getD "."
    let pathOf (k : String) : Option String :=
      ((lines.find? (·.key == k)).bind (·.args.head?)).map fun p =>
        if p.startsWith "/" then p else (dir / p).toString
    let (some specCert, some implCert) := (pathOf "spec", pathOf "impl")
      | IO.eprintln s!"{path}: no spec or impl"; return 1
    let (_, DS) ← Projection.CertLoadAny.admitFile specCert
    let NI ← loadNames implCert
    let NS ← loadNames specCert
    match buildJudge lines NI NS DS with
    | .error e => IO.eprintln s!"{path}: {e}"; return 1
    | .ok (J, asm) =>
      let cn := ((lines.find? (·.key == "chan")).bind (·.args.head?)).getD "?"
      let inS (k : Nat) : String := (NS.inW.getD k ("?", 0)).1
      let outS (o : Nat) : String := (NS.outW.getD o ("?", 0)).1
      IO.println s!"chan {cn} resp={",".intercalate (J.chan.respIn.map inS)} obs={",".intercalate (J.chan.obsOut.map outS)}"
      IO.println s!"slot {cn} kind={canonBit NI J.slot.kind} resp={",".intercalate (J.slot.resp.map (canonPort NI))} obs={";".intercalate (J.slot.obs.map (canonWord NI))}"
      for x in J.xfer do IO.println s!"xfer {canonBit NI x}"
      IO.println s!"maxstall {J.maxStall}"
      IO.println s!"period {(judgePeriod lines).1}"
      IO.println s!"asm {match asm with
        | none => "-"
        | some p => (if p.nz then "" else "!") ++ (NI.outW.getD p.out ("?", 0)).1}"
  return 0

/-
# A VCD writer for a run's ports

Every input and output, by its `_io.json` name, one sample per applied cycle at time
`10 * cycle`; a value is written only when it changes.
-/
import HDLProof.Run.IoMeta

namespace HDLProof
namespace Run

structure Vcd where
  h      : IO.FS.Handle
  widths : Array Nat
  last   : IO.Ref (Array (Option Nat))

def vcdId (k : Nat) : String :=
  String.ofList [Char.ofNat (33 + k % 90), Char.ofNat (33 + (k / 90) % 90), Char.ofNat (33 + k / 8100)]

def binStr (w v : Nat) : String :=
  String.ofList ((List.range w).reverse.map fun i => if (v >>> i) % 2 == 1 then '1' else '0')

/-- Open a VCD for the ports `(name, width)`, inputs first then outputs. -/
def Vcd.create (path : String) (ports : Array (String × Nat)) : IO Vcd := do
  let h ← IO.FS.Handle.mk path .write
  h.putStrLn "$timescale 1ns $end"
  h.putStrLn "$scope module top $end"
  for ((nm, w), k) in ports.zipIdx do
    h.putStrLn s!"$var wire {max w 1} {vcdId k} {nm} $end"
  h.putStrLn "$upscope $end"
  h.putStrLn "$enddefinitions $end"
  return { h, widths := ports.map (·.2), last := ← IO.mkRef (Array.replicate ports.size none) }

def Vcd.sample (v : Vcd) (cycle : Nat) (vals : Array Nat) : IO Unit := do
  let last ← v.last.get
  let mut lines : Array String := #[]
  for k in [0:vals.size] do
    if last[k]? != some (some vals[k]!) then
      let w := max (v.widths[k]?.getD 1) 1
      lines := lines.push (if w == 1 then s!"{vals[k]! % 2}{vcdId k}" else s!"b{binStr w vals[k]!} {vcdId k}")
  if !lines.isEmpty then
    v.h.putStrLn s!"#{cycle * 10}"
    for l in lines do v.h.putStrLn l
  v.last.set (vals.map some)

end Run
end HDLProof

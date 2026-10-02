/- Bounded performance probe: how long does `mkSim` take, interpreted vs
   native, on a synthetic chain of `n` nodes.

   `chainD` is repeated here rather than imported from `Projection/Scaling.lean`
   so the probe's dependency closure stops at `ProjectedStep` -- Scaling is 41 s
   of `#guard`s that have nothing to do with this measurement. -/
import LeanSemanticPrimitives.Projection.ProjectedStep

open Compiler Projection

def chainD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2 + i - 1, 0] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := #[]
  memories := #[]

partial def tsize : Term → Nat
  | .lit _ | .var _ => 1
  | .letIn a b      => 1 + tsize a + tsize b
  | .ite a b c      => 1 + tsize a + tsize b + tsize c
  | .caseT s as     => 1 + tsize s + (as.map (fun a => tsize a.2.2)).foldl (·+·) 0
  | .prim _ ts | .ctorT _ ts | .call _ ts => 1 + (ts.map tsize).foldl (·+·) 0

def rss : IO String := do
  try
    let t ← IO.FS.readFile "/proc/self/status"
    match (t.splitOn "\n").find? (·.startsWith "VmHWM:") with
    | some l => pure (l.replace "VmHWM:" "")
    | none   => pure "n/a"
  catch _ => pure "n/a"

def one (n : Nat) : IO Unit := do
  let t0 ← IO.monoMsNow
  match Projection.Hw.mkSim (chainD n) with
  | none     => IO.println s!"n={n}  mkSim FAILED"
  | some sim =>
      let t1 ← IO.monoMsNow
      let sz := (sim.prog.funs.map (fun fd => tsize fd.body)).foldl (·+·) 0
      IO.println s!"n={n}  mkSim {t1 - t0} ms  residual size {sz}  bound {sim.bound}  RSS{← rss}"

def main (args : List String) : IO UInt32 := do
  for a in args do
    match a.toNat? with | some n => one n | none => pure ()
  return 0

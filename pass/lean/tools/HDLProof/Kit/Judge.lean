/-
`Kit.Judge` -- decide, on ONE concrete run, whether ANY stuttering alignment of the reference D0
matches the implementation D1. No map, no invariant, no solver: two simulators and the contract's
observations.

The contract (one `chan`, one `slot` of a `.refmap`, plus `xfer` and `maxstall`):
- D1 runs once on the given inputs, one step ahead of the search; its ports of every step are
  `Ports` (outputs at n, outputs at n+1, inputs at n). The search stops at the first NO MATCH.
- A set of D0 states is kept, starting from D0's initial state. At each step every D0 state may
  - STEP on the step's spec input (`specIn` of the slot's response words, as an event of the
    refinement): kept only if the channel allows the event and D0's observed words equal D1's
    (`obsOf`, masked), or
  - HOLD (take no step): allowed only if no `xfer` formula is true on D1 at that step and the state
    has held fewer than `maxstall` steps in a row. Holds are per STEP, exactly as the refinement's
    stutters (a slot that does not fire) are: on a P-slot certificate (`single_edge`, one step =
    1/P clock) an alignment that holds D0 for part of a period puts D0's phase out of step with
    D1's, and dies on its own at the next mismatch.
- Identical states are merged (keeping the smallest hold count, which dominates). The search is
  exact and has no cap: if the set becomes empty at step n, NO alignment of D0 matches D1 on this
  run (`noMatch`); otherwise one does (`matched`).
- Separately, the path that follows the slot's own stall choice (step exactly when the slot fires)
  is run, and its first failure is reported: a MATCH whose slot path fails means the stall choice,
  not D1, is wrong on this run.

The verdict is an execution of the simulators passed in; with `runMemSim` (`runMemSim_correct`)
it is the certificates' `interpretDesign` semantics. Mathlib-free.
-/
import HDLProof.Check.RefMap

open Projection
namespace HDLProof.Kit.Judge
open HDLProof.Check HDLProof.Kit HDLProof.Sym Compiler

instance : Inhabited Ports := ⟨{ outs := #[], outsNext := #[], ins := #[] }⟩

/-- One cycle of a design: `none` when the simulator refuses. -/
abbrev Sim := RuntimeInput → StateM → Option StateM.ResultM

/-- The contract the judge decides. -/
structure JSpec where
  chan     : ChanS
  slot     : SlotS
  xfer     : List BitE := []
  maxStall : Nat := 0
  specInW  : List Nat
  /-- names, for reports: the slot's observed words, the xfer formulas -/
  obsName  : Array String := #[]
  xferName : Array String := #[]

/-- A state with its memory words and flops normalized (the merge key). -/
def normState (s : StateM) : StateM :=
  { flops := s.flops.map normBV
  , mems := s.mems.map fun m => { words := m.words.map normBV, dflt := normBV m.dflt } }

/-- A hash of a normalized state. -/
def stateHash (s : StateM) : UInt64 := Id.run do
  let mut h : UInt64 := 7
  for b in s.flops do h := mixHash h (mixHash (hash b.width) (hash (bv_uint b).toNat))
  for m in s.mems do
    for b in m.words do h := mixHash h (hash (bv_uint b).toNat)
  return h

/-- A D0 candidate: its state and the holds in a row so far. -/
structure Cand where
  st     : StateM
  holds  : Nat

/-- The candidates of one step, merged by state: the smallest hold count is kept. -/
structure Frontier where
  buckets : Std.HashMap UInt64 (List Cand) := {}
  size    : Nat := 0

def Frontier.insert (F : Frontier) (c : Cand) : Frontier := Id.run do
  let h := stateHash c.st
  let bs := F.buckets.getD h []
  match bs.find? (fun d => decide (d.st = c.st)) with
  | some d =>
      if c.holds < d.holds then
        let bs' := bs.map fun e => if decide (e.st = c.st) then c else e
        return { F with buckets := F.buckets.insert h bs' }
      else return F
  | none => return { buckets := F.buckets.insert h (c :: bs), size := F.size + 1 }

def Frontier.toList (F : Frontier) : List Cand := F.buckets.fold (fun acc _ bs => bs ++ acc) []

/-- Why one candidate cannot continue at a step. -/
inductive Why where
  | refused
  | notAllowed
  | obsDiffers (i : Nat) (d0 d1 : Int)
  | xferInHold (i : Nat)
  | holdLimit
  deriving Repr

def Why.show (J : JSpec) : Why → String
  | .refused => "the D0 simulator refused the step"
  | .notAllowed => "D0 does not allow the event"
  | .obsDiffers i d0 d1 =>
      s!"observed word {J.obsName.getD i s!"#{i}"}: D0 {d0} (0x{String.ofList (Nat.toDigits 16 d0.toNat)}), D1 {d1} (0x{String.ofList (Nat.toDigits 16 d1.toNat)})"
  | .xferInHold i => s!"a hold is not allowed: D1 transfers ({J.xferName.getD i s!"xfer #{i}"})"
  | .holdLimit => "a hold would exceed maxstall"

/-- D0 stepped from `c` on the event of step `n`: the next candidate, or why not. -/
def stepCand (J : JSpec) (simS : Sim) (C : Ports) (c : Cand) : Except Why Cand := do
  let rs := respOf C J.slot
  let some r := simS (specIn J.specInW J.chan rs) c.st | throw .refused
  unless propBit r.outputs J.chan.allow do throw .notAllowed
  let d1 := obsOf C J.slot
  let d0 := outVals r.outputs J.chan.obsOut
  for ((a, b), i) in (d0.zip d1).zipIdx do
    if a != b then throw (.obsDiffers i a b)
  if d0.length != d1.length then throw (.obsDiffers (min d0.length d1.length) 0 0)
  return { st := normState r.nextState, holds := 0 }

/-- D0 holding at a step: the next candidate, or why not. -/
def holdCand (J : JSpec) (C : Ports) (c : Cand) : Except Why Cand := do
  for (x, i) in J.xfer.zipIdx do
    if x.bit C then throw (.xferInHold i)
  if c.holds + 1 > J.maxStall then throw .holdLimit
  return { st := c.st, holds := c.holds + 1 }

/-- The judge's verdict. -/
inductive Verdict where
  /-- no D0 state survives step `step`; the last candidates and why each fails -/
  | noMatch (step : Nat) (why : List (List String))
  /-- some alignment survives every step; the slot path's first failure, if any; the largest set -/
  | matched (steps : Nat) (slotFail : Option (Nat × String)) (maxFrontier : Nat)
  /-- D1's simulator refused step `step` -/
  | implRefused (step : Nat)
  /-- the environment check `env` failed on D1's ports at step `step` -/
  | envViolated (step : Nat)

/-- The exact search over the steps of `ins`, D1 (`simI` from `s0I`) and D0 (`simS` from `s0`)
together. D1 runs one step ahead of the search: step n's ports need D1's outputs at n+1 (for
`@next` words; the last step's come from one more cycle on the last input vector). So the search
stops at the first NO MATCH, and a NO MATCH at step n costs n+2 D1 steps and the D0 steps up to n,
whatever the run's length. `env` is checked on D1's ports of each step before that step is
searched: a NO MATCH at step n is reported only on a run whose environment held on steps 0..n. -/
def run (J : JSpec) (simS : Sim) (s0 : StateM) (simI : Sim) (s0I : StateM)
    (ins : Array RuntimeInput) (env : Ports → Bool := fun _ => true) : Verdict := Id.run do
  if ins.isEmpty then return .matched 0 none 1
  let some r0 := simI ins[0]! s0I | return .implRefused 0
  let mut cur := r0
  let mut F : Frontier := Frontier.insert {} { st := normState s0, holds := 0 }
  let mut maxF := 1
  -- the slot's own stall choice, followed alone
  let mut slotC : Option Cand := some { st := normState s0, holds := 0 }
  let mut slotFail : Option (Nat × String) := none
  for n in [0:ins.size] do
    let some rN := simI (ins.getD (n + 1) ins.back!) cur.nextState | return .implRefused (n + 1)
    let C : Ports := { outs := cur.outputs, outsNext := rN.outputs, ins := ins[n]! }
    cur := rN
    unless env C do return .envViolated n
    let mut G : Frontier := {}
    let mut whys : List (List String) := []
    -- each distinct D0 state is stepped once per step; the slot path reuses that step
    let mut stepped : List (StateM × Except Why Cand) := []
    for c in F.toList do
      let a := stepCand J simS C c
      stepped := (c.st, a) :: stepped
      let b := holdCand J C c
      match a with | .ok c' => G := G.insert c' | .error _ => pure ()
      match b with | .ok c' => G := G.insert c' | .error _ => pure ()
      match a, b with
      | .error wa, .error wb => whys := whys ++ [[s!"step: {wa.show J}", s!"hold: {wb.show J}"]]
      | _, _ => pure ()
    if G.size == 0 then return .noMatch n whys
    maxF := max maxF G.size
    F := G
    if let some c := slotC then
      if slotFail.isNone then
        let r := if J.slot.kind.bit C then
            match stepped.find? (fun x => decide (x.1 = c.st)) with
            | some (_, a) => a
            | none => stepCand J simS C c
          else holdCand J C c
        match r with
        | .ok c' => slotC := some c'
        | .error w => slotFail := some (n, w.show J); slotC := none
  return .matched ins.size slotFail maxF

end HDLProof.Kit.Judge

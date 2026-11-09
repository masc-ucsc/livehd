/-
`Kit.Judge` -- decide, on ONE concrete run, whether ANY stuttering alignment of the reference D0
matches the implementation D1 UNDER A CONTRACT. No map, no invariant, no solver: two simulators
and the contract's observations.

The contract (one `chan`, one `slot` of a `.refmap`, plus `xfer` and `maxstall`):
- D1's ports of every step are `Ports` (outputs at n, outputs at n+1, inputs at n). They come
  from a source (`search`): D1 run step by step from a feed (`feedSrc`, D1 one step ahead of the
  search, so the search stops at the first NO MATCH), or a fixed array (`arraySrc`: the same run
  re-read under a relaxed contract, or a control design's run).
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
  run under this contract (`noMatch`, with the last candidates and why each fails); otherwise one
  does (`matched`). A NO MATCH is a verdict on the contract as much as on D1: a contract that
  reads an output channel the change delays in the same cycle, or allows fewer holds than the
  change adds, rejects a right D1 too. The driver classifies every NO MATCH by re-reading the
  same run under relaxed contracts (`arraySrc`).
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

/-- The observed word a failing step differs on, if that is why it fails. -/
def Why.word? : Why → Option Nat
  | .obsDiffers i _ _ => some i
  | _ => none

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

/-- A candidate that cannot continue at the step: why its step fails and why its hold fails. -/
structure Last where
  cand : Cand
  step : Why
  hold : Why

/-- The judge's verdict. -/
inductive Verdict where
  /-- no D0 state survives step `step`; the last candidates and why each fails -/
  | noMatch (step : Nat) (last : List Last)
  /-- some alignment survives every step; the slot path's first failure, if any; the largest set -/
  | matched (steps : Nat) (slotFail : Option (Nat × String)) (maxFrontier : Nat)
  /-- D1's simulator refused step `step` -/
  | implRefused (step : Nat)
  /-- no input vector for step `step` keeps the environment's protocol -/
  | envViolated (step : Nat)

/-- The ports of one step from a source, with the source's next state. -/
inductive SrcR (σ : Type) where
  | ok (C : Ports) (s : σ)
  | refused (n : Nat) (s : σ)
  | envViolated (n : Nat) (s : σ)

/-- The exact search over steps `0 .. steps-1`, D0 (`simS` from `s0`) against the ports `src`
gives for each step, asked in step order. Stops at the first step no D0 state survives. Returns
the source's last state too (a feed's state holds the inputs it used). -/
def search {σ : Type} (J : JSpec) (simS : Sim) (s0 : StateM) (steps : Nat)
    (src : σ → Nat → SrcR σ) (init : σ) : Verdict × σ := Id.run do
  let mut ss := init
  let mut F : Frontier := Frontier.insert {} { st := normState s0, holds := 0 }
  let mut maxF := 1
  -- the slot's own stall choice, followed alone
  let mut slotC : Option Cand := some { st := normState s0, holds := 0 }
  let mut slotFail : Option (Nat × String) := none
  for n in [0:steps] do
    let C ← match src ss n with
      | .ok C s => do ss := s; pure C
      | .refused m s => return (.implRefused m, s)
      | .envViolated m s => return (.envViolated m, s)
    let mut G : Frontier := {}
    let mut last : List Last := []
    -- each distinct D0 state is stepped once per step; the slot path reuses that step
    let mut stepped : List (StateM × Except Why Cand) := []
    for c in F.toList do
      let a := stepCand J simS C c
      stepped := (c.st, a) :: stepped
      let b := holdCand J C c
      match a with | .ok c' => G := G.insert c' | .error _ => pure ()
      match b with | .ok c' => G := G.insert c' | .error _ => pure ()
      match a, b with
      | .error wa, .error wb => last := last ++ [{ cand := c, step := wa, hold := wb }]
      | _, _ => pure ()
    if G.size == 0 then return (.noMatch n last, ss)
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
  return (.matched steps slotFail maxF, ss)

/-- A fixed run as a source: step `n`'s ports are `ps[n]` (the caller passes `steps ≤ ps.size`). -/
def arraySrc (ps : Array Ports) : Unit → Nat → SrcR Unit :=
  fun _ n => .ok ps[n]! ()

/-- One fed step of D1: the inputs used, D1's result, whether the environment held, and the
feed's next state. -/
inductive FeedR (γ : Type) where
  | ok (i : RuntimeInput) (r : StateM.ResultM) (envOk : Bool) (g : γ)
  | refused
  | envViolated

/-- D1's run so far: the feed's state, D1's state after the last fed step, every fed step's
inputs and outputs, and the first fed step whose environment failed (a feed that does not
enforce the environment, e.g. a trace, only records it). -/
structure RunSt (γ : Type) where
  g      : γ
  st     : StateM
  ins    : Array RuntimeInput := #[]
  outs   : Array (Array BV) := #[]
  envBad : Option Nat := none

/-- Feed D1 until `k` steps are fed (or the feed stops). -/
def RunSt.extend {γ : Type} (feed : Nat → StateM → γ → FeedR γ) (R : RunSt γ) (k : Nat) :
    RunSt γ × Option (SrcR (RunSt γ)) := Id.run do
  let mut R := R
  for _ in [0:k] do
    if R.ins.size ≥ k then break
    let m := R.ins.size
    match feed m R.st R.g with
    | .ok i r ok g =>
        R := { R with g, st := r.nextState, ins := R.ins.push i, outs := R.outs.push r.outputs
                      envBad := if ok || R.envBad.isSome then R.envBad else some m }
    | .refused => return (R, some (.refused m R))
    | .envViolated => return (R, some (.envViolated m R))
  return (R, none)

/-- D1 fed step by step as a source: step `n`'s ports need D1's outputs at `n+1`, so D1 runs one
step ahead of the search and a run stops at its first NO MATCH. -/
def feedSrc {γ : Type} (feed : Nat → StateM → γ → FeedR γ) : RunSt γ → Nat → SrcR (RunSt γ) :=
  fun R n =>
    match R.extend feed (n + 2) with
    | (_, some e) => e
    | (R, none) => .ok { outs := R.outs[n]!, outsNext := R.outs[n + 1]!, ins := R.ins[n]! } R

/-- The ports of steps `0 .. lim-1` of a recorded run (`outs` has at least `lim + 1` entries),
with output `o` read `shift o` cycles later (earlier when negative; 0 before cycle 0). -/
def shiftedPorts (outs : Array (Array BV)) (ins : Array RuntimeInput) (lim : Nat)
    (shift : Nat → Int) : Array Ports := Id.run do
  let readAt (m : Nat) (o : Nat) (b : BV) : BV :=
    let d := shift o
    if d == 0 then b
    else
      let m' : Int := (m : Int) + d
      if m' < 0 then mk_bv b.width 0
      else ((outs.getD m'.toNat #[]).getD o (mk_bv b.width 0))
  let mut ps : Array Ports := #[]
  for n in [0:lim] do
    let o0 := outs.getD n #[]
    let o1 := outs.getD (n + 1) #[]
    ps := ps.push { outs := o0.mapIdx (fun o b => readAt n o b), outsNext := o1.mapIdx (fun o b => readAt (n + 1) o b)
                  , ins := ins.getD n #[] }
  return ps

end HDLProof.Kit.Judge

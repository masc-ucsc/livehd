/-
# Shared scaffolding for the native runners

`Futamura_Projection_Scripts/proto_probe.lean` grew these as script-local definitions.  A second
runner needs the same ones, so they live here and are compiled once.

**State this accurately: the duplication is NOT yet removed.**  `proto_probe`
still carries its own copies and does not import this module, so the two CAN
drift today.  What this file is, for now, is a shared-ready copy that
`total_probe` uses; finishing the reuse means deleting `proto_probe`'s
definitions and importing these, and that is not worth interrupting a running
benchmark for.

Nothing in this file is a proof or is used by one.
-/
import LeanSemanticPrimitives.Projection.Proto.VariantExec

namespace Projection
namespace Runner

open Projection Projection.Hw Compiler

/-! ## Stimulus, sized and width-aware from the design -/

def maxInputIdx (D : DesignCert) : Nat :=
  D.sources.foldl (fun a s => match s with
    | .input idx _            => max a (idx + 1)
    | .flopQAsync _ _ ri _ _  => max a (ri + 1)
    | _                       => a) 0

/-- The widest declared use of input `i`.  `srcVal` applies `bvResize w`, so a
vector element narrower than `w` silently zero-extends and the design's high
bits are never driven. -/
def inputWidth (D : DesignCert) (i : Nat) : Nat :=
  D.sources.foldl (fun a s => match s with
    | .input idx w           => if idx == i then max a w else a
    | .flopQAsync _ _ ri _ _ => if ri == i then max a 1 else a
    | _                      => a) 1

/-- Seeds 0-4 are the width corners; 5 and up are pseudorandom. -/
def patternAt (w : Nat) (seed i : Nat) : Int :=
  let allOnes : Int := Int.ofNat (2 ^ w - 1)
  let highBit : Int := Int.ofNat (2 ^ (w - 1))
  match seed with
  | 0 => 0
  | 1 => allOnes
  | 2 => highBit
  | 3 => allOnes - highBit
  | 4 => if i % 2 == 0 then allOnes else 0
  | k => Int.ofNat ((k * 2654435761 + i * 40503 + 1) % (2 ^ w))

def mkInputFor (D : DesignCert) (seed : Nat) : RuntimeInput :=
  (List.range (maxInputIdx D)).toArray.map
    (fun i => let w := inputWidth D i; mk_bv w (patternAt w seed i))

def mkStateFor (D : DesignCert) (seed : Nat) : RuntimeState :=
  { flops := (List.range D.flops.size).toArray.map
               (fun i => mk_bv (D.flops[i]!).width (Int.ofNat ((seed * 5 + i * 3) % 16)))
  , mems  := #[] }

/-- Every third cycle nothing fires, so the hold path is exercised too. -/
def edgeSchedule (D : DesignCert) (k : Nat) : ClockEdges :=
  if k % 3 == 2 then Array.replicate D.clocks.size false else allEdges D

/-- Clock domains fire INDEPENDENTLY: domain `j` fires on cycle `k` iff
`(k + j) % (j + 2) ≠ 0`.  `edgeSchedule` fires every domain or none, so on a
multi-clock design it never shows one domain stepping while another holds --
the case the edge-vector semantics exists for.  Over any six cycles two domains
see every combination: one alone, the other alone, both, neither. -/
def indepEdges (D : DesignCert) (k : Nat) : ClockEdges :=
  (List.range D.clocks.size).toArray.map (fun j => (k + j) % (j + 2) != 0)

/-- Which schedule a driver runs.  `auto` keeps `edgeSchedule` for a one-clock
design -- so no existing single-clock result changes -- and uses `indepEdges`
when the certificate declares more than one clock.  `edges:all` / `edges:indep`
force either. -/
inductive EdgeMode where
  | auto | all | indep
  deriving Inhabited, BEq

def EdgeMode.parse? : String → Option EdgeMode
  | "auto"  => some .auto
  | "all"   => some .all
  | "indep" => some .indep
  | _       => none

def scheduleEdges (m : EdgeMode) (D : DesignCert) (k : Nat) : ClockEdges :=
  match m with
  | .all   => edgeSchedule D k
  | .indep => indepEdges D k
  | .auto  => if D.clocks.size > 1 then indepEdges D k else edgeSchedule D k

def EdgeMode.describe (m : EdgeMode) (D : DesignCert) : String :=
  match m with
  | .all   => "edgeSchedule D k (every domain fires; every third cycle quiet)"
  | .indep => "indepEdges D k (domain j fires iff (k+j) % (j+2) != 0)"
  | .auto  => if D.clocks.size > 1
      then s!"indepEdges D k ({D.clocks.size} clock domains fire independently: domain j iff (k+j) % (j+2) != 0)"
      else "edgeSchedule D k (one clock; every third cycle quiet)"

/-! ## Outcomes that KEEP the failure kind -/

inductive Outcome where
  | ok          : RuntimeResult → Outcome
  | undecodable : Outcome
  | fuelOut     : Outcome
  | typeErr     : String → Outcome
  | shapeRefused : Outcome
  deriving Inhabited

def Outcome.tag : Outcome → String
  | .ok _           => "ok"
  | .undecodable    => "undecodable"
  | .fuelOut        => "boundExceeded"
  | .typeErr _      => "residualTypeError"
  | .shapeRefused   => "runtimeShape"

def Outcome.isOk : Outcome → Bool | .ok _ => true | _ => false

/-- Failure KINDS must match, not merely both be failures. -/
def Outcome.agree : Outcome → Outcome → Bool
  | .ok a,          .ok b          => encResult a == encResult b
  | .undecodable,   .undecodable   => true
  | .fuelOut,       .fuelOut       => true
  | .typeErr _,     .typeErr _     => true
  | .shapeRefused,  .shapeRefused  => true
  | _,              _              => false

def outcomeOfSim : Except SimError RuntimeResult → Outcome
  | .ok r                        => .ok r
  | .error .boundExceeded        => .fuelOut
  | .error (.residualTypeError m) => .typeErr m
  | .error .undecodableResult    => .undecodable
  | .error .runtimeShape         => .shapeRefused

def outcomeDigest : Outcome → Nat
  | .ok r        => r.outputs.size + r.nextState.flops.size
  | _            => 1

/-! ## A timed stage

Lean reorders pure work across `IO.monoMsNow`, so the result is FORCED inside
the interval by printing a digest.  The announcement comes first and is
flushed, because `IO.lazyPure` evaluates eagerly and an announcement placed
after it only appears once the stage has already finished. -/
def stage (name : String) (digest : α → Nat) (thunk : Unit → α) : IO (α × Nat) := do
  IO.print s!"  [{name} ..."
  (← IO.getStdout).flush
  let t0 ← IO.monoMsNow
  let v ← IO.lazyPure thunk
  let d ← IO.lazyPure (fun _ => digest v)
  IO.print s!" d={d}"
  let t1 ← IO.monoMsNow
  IO.println s!" {t1 - t0} ms]"
  (← IO.getStdout).flush
  return (v, t1 - t0)

end Runner
end Projection

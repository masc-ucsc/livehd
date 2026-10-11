/-
  The scaling wall, measured.

  Phase 0 of the scaling plan.  `HardwareInterpreter.lean` projects two fixtures
  of three and six slots; the question this file answers is what happens at the
  size of a real design, and the answer decides the shape of everything after it.

  WHY THE RESIDUAL GROWS QUADRATICALLY.  `I_hw` carries the slot environment as
  a cons chain -- it must, because `interpretDesign` uses `rho : Nat -> CertVal`,
  a function, and `L` is first order.  Slot `s` is read at depth `n - 1 - s` by
  `nthD`, which is `inline`, so it UNROLLS: a read at depth `k` emits `k`
  `let`-bound `tl` steps.  Reads are O(N) and their depths sum quadratically, so
  the residual is O(N^2) -- and the cost is in GENERATION, not just in running
  the result.  Nothing can be simplified after the fact because the O(N^2) term
  has to be built first.

  `fanD` makes that exact: every node reads both SOURCE slots, which sit at the
  bottom of a newest-first chain, so every read is at maximum depth and the `tl`
  count is exactly `N^2`.  That identity is the `#guard` below, and it is the
  sharpest statement of the wall -- when the partially-static environment lands,
  the same `#guard` should read `0`.

  `chainD` is the realistic shape: each node reads its immediate predecessor
  (depth 0) and one source (depth ~N), giving `N^2/2`.

  MEASURED (chain, this machine, `mixDriver 2000000 500`):

  |    N |     ms |      size |      tl |
  |-----:|-------:|----------:|--------:|
  |   16 |     15 |     1,004 |     136 |
  |   64 |    125 |     8,420 |   2,080 |
  |  256 |  1,665 |   107,204 |  32,896 |
  | 1024 | 24,332 | 1,608,260 | 524,800 |

  Every doubling of `N` costs 4x, exactly as the analysis says.  Extrapolating
  to the designs this branch is aimed at:

  * CORE-ET largest, 14,860 nodes: ~1.4 h and ~3.4e8 residual term nodes;
  * CVA6 largest, 27,523 nodes: ~4.9 h and ~1.2e9 residual term nodes.

  At a conservative 40 bytes per `Term` node that is 13 GB and 46 GB of residual
  respectively, so neither is a matter of waiting longer.  The practical ceiling
  today is roughly N = 1000-2000; DINO's 4,772-node `SingleCycleCPU` is already
  out of reach.  Fuel is NOT the constraint -- `projectDesign`'s hardcoded
  `mixDriver 20000 200` still succeeds at N = 256.

  These designs are kept after the fix: they are the regression that shows the
  new environment representation actually changed the asymptotics, and they are
  reused as the first rungs of the Milestone 7 ladder.
-/

import LeanSemanticPrimitives.Projection.HardwareInterpreter
import LeanSemanticPrimitives.Projection.ResidualFragment

namespace Projection
namespace Scaling
open Compiler

/-! ## Synthetic designs

Both use only `Op_And`, so they exercise the environment and nothing else --
operator coverage is a separate axis and must not contaminate this measurement. -/

/-- Every node reads the two SOURCE slots, so every read is at maximum depth.
The worst case for a newest-first environment, and the one that makes the
quadratic exact. -/
def fanD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun _ => { op := .Op_And, width := 4, deps := #[0, 1] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- Node `i` reads its immediate predecessor (depth 0) and source 0 (deep) --
the shape a real dataflow graph has. -/
def chainD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2 + i - 1, 0] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- `chainD` with a FLOP on every node, so the edge vector is actually read.

`fanD` and `chainD` have no flops at all, so they measure the slot environment
and nothing about clocks.  This one pins the claim step 4 of the multi-clock
port rests on: a flop's clock ORDINAL is static, so reading its edge is a
statically known number of `tl` steps into the dynamic edge array -- O(1) per
flop, not a search -- and the residual stays linear. -/
def flopD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2 + i - 1, 0] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := (List.range n).toArray.map (fun i =>
                { width := 4, din := 2 + i, enable := none, resetPin := none
                , resetValue := 0, resetActiveLow := false })
  memories := #[]

/-! ## Counting -/

partial def tsize : Term → Nat
  | .lit _ | .var _ => 1
  | .letIn a b => 1 + tsize a + tsize b
  | .ite a b c => 1 + tsize a + tsize b + tsize c
  | .caseT s as => 1 + tsize s + (as.map (fun a => tsize (Alt.body a))).foldl (·+·) 0
  | .prim _ ts | .ctorT _ ts | .call _ ts => 1 + (ts.map tsize).foldl (·+·) 0

partial def countPrim (p : Prim) : Term → Nat
  | .prim q ts => (if q == p then 1 else 0) + (ts.map (countPrim p)).foldl (·+·) 0
  | .lit _ | .var _ => 0
  | .letIn a b => countPrim p a + countPrim p b
  | .ite a b c => countPrim p a + countPrim p b + countPrim p c
  | .caseT s as => countPrim p s + (as.map (fun a => countPrim p (Alt.body a))).foldl (·+·) 0
  | .ctorT _ ts | .call _ ts => (ts.map (countPrim p)).foldl (·+·) 0

def tot (f : Term → Nat) (P : Program) : Nat := (P.funs.map (fun fd => f fd.body)).foldl (·+·) 0

/-- Specialize with fuel generous enough that fuel is never what fails; the
point of the measurement is the size of what comes out. -/
def project (D : DesignCert) : Option Program :=
  (mixDriver 2000000 500 Hw.hwAP [encDesign D]).toOption

def stats (D : DesignCert) : Option (Nat × Nat) :=
  (project D).map (fun P => (tot tsize P, tot (countPrim .tl) P))

@[inline] def tlOf (D : DesignCert) : Nat := ((stats D).map Prod.snd).getD 0
@[inline] def szOf (D : DesignCert) : Nat := ((stats D).map Prod.fst).getD 0

/-! ## The regression

Small `N` only, so the build stays fast; the shape is already unambiguous by
N = 32.  These numbers were QUADRATIC before partially-static values landed:
`tlOf (fanD n)` was exactly `n * n`, `tlOf (chainD 16)` was 136 and
`szOf (chainD 32)` was 2708.  They are pinned here, as before, so a regression
shows up as a failing build rather than as a slow one. -/

/-- For every residual `hd`/`tl`: is its operand a plain variable (a DIRECT
read), a `consP` (a chain rebuilt only to be taken apart again), or something
else (a nested peel, i.e. walking a chain)?

This is Trap 2 restated for peels.  A global `tl` COUNT conflates two unrelated
things: walking the slot environment, which the switch was meant to eliminate,
and reading the runtime input/state vectors, which are genuinely dynamic data
and must still be read.  Only the second should survive, and it shows up as
depth-one peels of a variable. -/
partial def peelKinds : Term → Nat × Nat × Nat
  | .lit _ | .var _ => (0,0,0)
  | .letIn a b =>
      let (x,y,z) := peelKinds a; let (u,v,w) := peelKinds b; (x+u, y+v, z+w)
  | .ite a b c =>
      let (x,y,z) := peelKinds a; let (u,v,w) := peelKinds b; let (r,s,t) := peelKinds c
      (x+u+r, y+v+s, z+w+t)
  | .caseT sc as =>
      as.foldl (fun acc a =>
        let (x,y,z) := acc; let (u,v,w) := peelKinds (Alt.body a); (x+u,y+v,z+w))
        (peelKinds sc)
  | .ctorT _ ts | .call _ ts =>
      ts.foldl (fun acc t => let (x,y,z) := acc; let (u,v,w) := peelKinds t; (x+u,y+v,z+w)) (0,0,0)
  | .prim p ts =>
      let base := ts.foldl (fun acc t =>
        let (x,y,z) := acc; let (u,v,w) := peelKinds t; (x+u,y+v,z+w)) (0,0,0)
      let (x,y,z) := base
      match p, ts with
      | .hd, [a] | .tl, [a] =>
          match a with
          | .var _         => (x+1, y, z)
          | .prim .consP _ => (x, y+1, z)
          | _              => (x, y, z+1)
      | _, _ => base

def peelsOf (D : DesignCert) : Nat × Nat × Nat :=
  ((project D).map (fun P =>
      P.funs.foldl (fun acc fd =>
        let (x,y,z) := acc; let (u,v,w) := peelKinds fd.body; (x+u,y+v,z+w)) (0,0,0))).getD (0,0,0)

def pOf (p : Prim) (D : DesignCert) : Nat :=
  ((project D).map (fun P => tot (countPrim p) P)).getD 99999

-- THE WALL IS GONE.  `fanD`'s `tl` count was exactly `n * n`; it is now `n`,
-- and those `n` are reads of the dynamic INPUT vector, not of a slot chain.
#guard [1, 2, 4, 8, 16, 32].all (fun n => tlOf (fanD n) == n)
#guard tlOf (chainD 16) == 16
#guard tlOf (chainD 32) == 32
#guard tlOf (chainD 64) == 64

-- and the residual size that follows from it: LINEAR, checked as a shape
-- rather than as four unexplained constants
#guard [1, 2, 4, 8, 16, 32].all (fun n => szOf (fanD n) == 16 * n + 32)
#guard [16, 32, 64, 128].all (fun n => szOf (chainD n) == 15 * n + 33)

-- each dynamic graph-node operation appears EXACTLY once: no duplication took
-- the place of the `tl` chains
#guard [1, 2, 4, 8, 16, 32].all (fun n => pOf .bvAnd (fanD n) == n)
#guard [16, 32, 64].all (fun n => pOf .bvAnd (chainD n) == n)

-- and no slot chain is rebuilt: every residual peel reads a variable directly,
-- none takes apart a `consP`, and none is nested inside another peel
#guard [1, 2, 4, 8, 16, 32].all (fun n => (peelsOf (fanD n)).2 == (0, 0))
#guard [16, 32, 64].all (fun n => (peelsOf (chainD n)).2 == (0, 0))

/-! ## The guard, exhibited

`hd (consP X loop)` must NOT reduce: the source has no value at all, because
`EvalList` needs every operand to have one, so dropping the tail would make
`mixDriver_complete` false.  These pin the guard itself rather than its
consequences. -/

-- a discarded tail that could fail or diverge: no structural answer
#guard (primStruct .hd [.cons (.stat (.int 1)) (.code (.call 0 []))]).isNone
-- a discarded tail that is a bound reference: computation-free, so it fires
#guard (primStruct .hd [.cons (.stat (.int 1)) (.code (.var 0))]).isSome
-- `tl` discards the HEAD, so the guard moves
#guard (primStruct .tl [.cons (.code (.call 0 [])) (.stat (.int 1))]).isNone
#guard (primStruct .tl [.cons (.code (.var 0)) (.stat (.int 1))]).isSome
-- `isNil` discards BOTH, and that is the one that is easy to miss
#guard (primStruct .isNil [.cons (.code (.var 0)) (.code (.call 0 []))]).isNone
#guard (primStruct .isNil [.cons (.code (.var 0)) (.code (.var 1))]).isSome

-- fuel is not the binding constraint: `projectDesign`'s own 20000/200 still works
#guard (Hw.projectDesign (chainD 64)).toOption.isSome

/-! ## The flop state vector: a SECOND chain, and it is still quadratic

`fanD` and `chainD` have no flops, and the shared `seqD` has one, so nothing
above has ever measured what `I_hw` costs per FLOP.  `flopD` does, and the
answer is that Phase 1 removed one quadratic and left another standing.

The slot ENVIRONMENT is gone -- that is what the numbers above show.  The
runtime STATE vector is a different list: `flopNext` reads the old value of flop
`idx` with `nthD fq idx`, and `idx` is static, so flop `idx` costs `idx` steps
and `F` flops cost O(F^2).  Measured below, pinned as Phase 0 pinned the first
one, and NOT fixed here: fixing it is a partially-static treatment of the state
vector, the same shape as the environment fix, and it belongs to its own
increment.

NOT caused by the clock port.  With `firesAt` replaced by a constant the numbers
are identical to the digit, so the edge vector costs nothing: a clock ORDINAL is
static, so its edge is one direct read per flop.

AND THE INSTRUMENT ABOVE CANNOT SEE IT.  `peelKinds` reports every one of these
peels as a DIRECT read of a variable, because preparation let-binds each
intermediate `tl` -- so a let-bound chain and a genuine direct read look alike.
That is a real limit of the check, recorded here rather than left to be
rediscovered. -/

#guard [8, 16, 32, 64].all (fun n => tlOf (flopD n) == n * n)
#guard [8, 16, 32, 64].all (fun n => szOf (flopD n) == 3 * n * n + 26 * n + 33)

-- what IS linear, and what the clock port had to keep linear: one node
-- operation per node, and no residual boolean that could have been decided
-- during specialization (`asyncReset` and the presence of a reset pin and an
-- enable are all static, so none of them residualizes)
#guard [8, 16, 32, 64].all (fun n => pOf .bvAnd (flopD n) == n)
#guard [8, 16, 32, 64].all (fun n => pOf .andB  (flopD n) == 0)
#guard [8, 16, 32, 64].all (fun n => pOf .orB   (flopD n) == 0)

/-! ## The checked evaluation bound scales LINEARLY

Phase 1 made the residual's SIZE linear in N.  This is the consequence for
RUNNING it: the bound `checkResidual` computes is the residual's height plus
one, and it comes out linear too -- so a cycle costs O(N), not O(N^2).

Recorded as measurements, not as a theorem about `mix`. -/

def boundOf (D : Compiler.DesignCert) : Option Nat :=
  (Hw.mkSim D).map Hw.ProjectedSimulator.bound

-- every Scaling residual is IN THE FRAGMENT: one function, arity 3, call-free
#guard [1, 4, 16, 64].all (fun n => (boundOf (fanD n)).isSome)
#guard [16, 64, 256].all (fun n => (boundOf (chainD n)).isSome)
#guard [8, 32].all (fun n => (boundOf (flopD n)).isSome)

-- and the bound is linear in N -- slope 3, measured
#guard boundOf (fanD 1)    == some 9
#guard boundOf (fanD 4)    == some 17
#guard boundOf (fanD 16)   == some 53
#guard boundOf (fanD 64)   == some 197
#guard [16, 64, 256].all (fun n => boundOf (chainD n) == some (3 * n + 5))
#guard [8, 32].all        (fun n => boundOf (flopD n)  == some (3 * n + 5))

end Scaling
end Projection

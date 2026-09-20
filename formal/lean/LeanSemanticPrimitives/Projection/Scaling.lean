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
N = 32.  **When the partially-static environment lands, the first `#guard`
should read `0` and the pinned sizes should drop to linear.** That flip is the
acceptance test for the fix, which is why the current numbers are written down
rather than merely observed. -/

-- the wall, exactly: every read at maximum depth costs N steps, N times over
#guard [1, 2, 4, 8, 16, 32].all (fun n => tlOf (fanD n) == n * n)

-- the realistic shape, ~N^2/2
#guard tlOf (chainD 16) == 136
#guard tlOf (chainD 32) == 528

-- and the residual term count that follows from it
#guard szOf (chainD 16) == 1004
#guard szOf (chainD 32) == 2708

-- fuel is not the binding constraint: `projectDesign`'s own 20000/200 still works
#guard (Hw.projectDesign (chainD 64)).toOption.isSome

end Scaling
end Projection

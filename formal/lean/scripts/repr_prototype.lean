/-
# DIAGNOSTIC PROTOTYPE -- not production, not proved, not in the core build.

Measures the ceiling of ONE representation change before any proof is touched:

    let `PRes` carry a PURE `PVal` directly, so `prepare` on it is O(1)
    instead of structurally rebuilding the spine.

## Which traversals this removes, and which it does not

The specializer's inner loop, as measured (`certio/PHASE6_PERF.md`): each node
does O(1) slot reads at depth Θ(n); each read unrolls `nthD` Θ(n) times; and
EVERY unroll step calls `prepare` on the environment result, which for a spine
of pure `dyn` leaves walks and REBUILDS the whole spine -- Θ(n).  Θ(n) nodes x
Θ(n) unrolls x Θ(n) rebuild = Θ(n^3).

  REMOVED by this change:
    * `prepare` on a pure spine: Θ(n) structural copy  ->  O(1)
    * `PVal.toPRes` on a pure spine: Θ(n) copy          ->  O(1)
      (the `var` rule stops converting; it hands the `PVal` over as-is)

  NOT removed by this change:
    * `PVal.shift` -- indices stay relative to binder depth, so entering a
      binder still shifts.  That is the de Bruijn LEVELS change, which is
      separate and larger.
    * the Θ(n) unrolling of `nthD` per slot read.  The residual is already
      free of it (Phase 1); this is specialization-time cost only.

So the claim under test is n^3 -> n^2, NOT n^3 -> n.  A suspended shift alone
would remove neither of the two traversals above, which is why it is not what
is prototyped here.

## STATUS: SUPERSEDED, and its 0 ms reading was MISDIAGNOSED

This file reports 0 ms (milliseconds -- it uses `IO.monoMsNow`) for both sides
at every n.  The first diagnosis, that the calls were deleted, was WRONG.

The generated C says what really happened: in `.native-dev/repr_prototype.c`,
`l_bench` takes ALL THREE timestamps (lines 1431, 1435, 1439) and only then
calls `l_loop` (1450, 1452).  Lean reordered the pure work to after the final
timestamp.  `l_walkA` does still call `toPRes` and `prepare` -- the work is
performed, just outside the measured interval.

The lesson is about measurement: in-process timestamps around PURE work are not
trustworthy in Lean.  Use separate processes, or an IO sequencing barrier, and
check the generated C.

SUPERSEDED BY `scripts/proto_probe.lean`, which runs the REAL `projectDesign`
against a forked specializer and times separate processes.  Its result refutes
the n^3 -> n^2 hypothesis this file was written to test.  Kept only as a record
of the trap.
-/
import LeanSemanticPrimitives.Projection.PartialEvaluator

open Projection

/-- The slot environment as the specializer holds it: a spine of pure `dyn`
leaves, newest first. -/
def spine : Nat → PVal
  | 0     => .stat (.int 0)
  | n + 1 => .cons (.dyn n) (spine n)

/-- SIDE A -- what happens today.  Walking the spine, EVERY level converts the
remaining sub-spine to a `PRes` and `prepare`s it, and both of those rebuild it
structurally.  That is the Theta(n) per unroll step the profile showed; summed
over the Theta(n) levels of one slot read it is Theta(n^2), and over Theta(n)
nodes, Theta(n^3).

`seed` is threaded through the result so the call is not loop-invariant: the
first version of this benchmark hoisted the whole loop out and reported 0 ms. -/
def walkA : PVal → Nat → Nat
  | .cons a b, seed =>
      let r := PVal.toPRes (.cons a b)
      let p := prepare r
      let h := match p.value with | .cons _ _ => 1 | _ => 0
      walkA b (seed + p.binds.length + h)
  | v, seed =>
      let p := prepare (PVal.toPRes v)
      seed + p.binds.length

/-- SIDE B -- the proposed representation: a pure `PVal` is handed over as-is,
so each level is O(1) and the walk is Theta(n) instead of Theta(n^2). -/
inductive PResB where
  | val  : PVal → PResB          -- NEW: a pure partial value, no conversion
  | code : Term → PResB
  | lets : List Term → PResB → PResB

@[inline] def toPResB (v : PVal) : PResB := .val v

@[inline] def prepareB : PResB → List Term × PVal
  | .val v     => ([], v)                   -- O(1): nothing to split, no copy
  | .code t    => ([t], .dyn 0)
  | .lets bs r => let (b, v) := prepareB r; (bs ++ b, v)

def walkB : PVal → Nat → Nat
  | .cons a b, seed =>
      let p := prepareB (toPResB (.cons a b))
      let h := match p.2 with | .cons _ _ => 1 | _ => 0
      walkB b (seed + p.1.length + h)
  | v, seed =>
      let p := prepareB (toPResB v)
      seed + p.1.length

/-- `nodes` slot reads, each walking the spine -- the shape the profile showed. -/
def loop (f : PVal → Nat → Nat) (env : PVal) (nodes : Nat) : Nat :=
  Nat.rec 0 (fun i acc => acc + f env i) nodes

def rss : IO String := do
  try
    let t ← IO.FS.readFile "/proc/self/status"
    match (t.splitOn "\n").find? (·.startsWith "VmHWM:") with
    | some l => pure (l.replace "VmHWM:" "") | none => pure "n/a"
  catch _ => pure "n/a"

def bench (n : Nat) : IO Unit := do
  let env := spine n
  -- the results are PRINTED, not discarded: binding them to `_` let the
  -- compiler delete both loops and report 0 ms for each
  let t0 ← IO.monoMsNow
  let a := loop walkA env n
  let t1 ← IO.monoMsNow
  let b := loop walkB env n
  let t2 ← IO.monoMsNow
  IO.println s!"n={n}  A (real prepare+toPRes) {t1 - t0} ms   B (O(1)) {t2 - t1} ms   [checks {a} {b}]  RSS{← rss}"

def main (args : List String) : IO UInt32 := do
  for a in args do
    match a.toNat? with | some n => bench n | none => pure ()
  return 0

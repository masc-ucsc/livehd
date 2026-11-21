/-
Tests for vc's optimizer and its checker (`Compiler.ResidualOpt`).

  (cd formal/lean && LEAN_PATH=$PWD/.olean-dev lean --run ../../pass/lean/tools/tests/residual_opt_check.lean)

`ResidualOptSound.optimize_correct` already PROVES that `optimize` never changes a result;
these cases check that the optimizer does what it should (drops what is dead, shares equal
constants), that the checker refuses wrong certificates, and that the optimized program
runs step for step like the original, a memory included.

Prints `PASS residual_opt: <n> cases` or one `FAIL` line per case that went wrong.
-/
import LeanSemanticPrimitives.Compiler.ResidualOpt
import LeanSemanticPrimitives.Compiler.CompileDesignDefs

open Compiler Compiler.ResidualOpt

/-- Sources: 0 input a, 1 const 5, 2 const 5 (a duplicate), 3 const 7, 4 input b (unread),
5 flop q. Bindings (slots 6..10): 6 = a + 5, 7 = a + 5 (through the duplicate), 8 = a * 7
(nothing reads it), 9 = 6 xor 7, 10 = 9 | q. Outputs: slots 6 and 10; the flop takes 7. -/
def prog : ResidualProgram :=
  { sources := #[.input 0 8, .const 8 5, .const 8 5, .const 8 7, .input 1 8, .flopQ 0 8]
    bindings := #[⟨.bv 8, .rsum 8 2 #[0, 1]⟩, ⟨.bv 8, .rsum 8 2 #[0, 2]⟩, ⟨.bv 8, .rmult 8 #[0, 3]⟩,
                  ⟨.bv 8, .rxor 8 #[6, 7]⟩, ⟨.bv 8, .rorBits 8 #[9, 5]⟩]
    outputs := #[{ slot := 6, width := 8 }, { slot := 10, width := 8 }]
    flopUpdates := #[{ width := 8, din := 7, enable := none, resetPin := none, resetValue := 0,
                       resetActiveLow := false, clock := 0, asyncReset := false }]
    memoryUpdates := #[] }

/-- A 12-word memory, written and read through the clamp `x < 12 ? x : 0`, with the clamp
constant written twice (slots 5 and 7), so sharing has something to do. -/
def ramCert : DesignCert :=
  let n (op : LGraphOp) (w : Nat) (deps : List Nat) : DenseNodeCert :=
    { op := op, width := w, deps := deps.toArray }
  { sources := #[.input 0 4, .input 1 8, .input 2 1, .input 3 4, .memImg 0 4 8,
                 .const 4 12, .const 4 0, .const 4 12]
    nodes := #[
      n .Op_ULT 1 [0, 5],            -- 8:  wa < 12
      n .Op_MuxBool 4 [8, 6, 0],     -- 9:  clamp wa
      n .Op_Or 4 [9],                -- 10
      n .Op_ULT 1 [3, 7],            -- 11: ra < 12 (the duplicate constant)
      n .Op_MuxBool 4 [11, 6, 3],    -- 12: clamp ra
      n .Op_Or 4 [12],               -- 13
      n .Op_MemWrite 8 [4, 10, 1, 2],-- 14
      n .Op_MemRead 8 [4, 13, 2],    -- 15
      n (.Op_Sum 2) 8 [1, 1]]        -- 16: nothing reads it
    outputs := #[{ slot := 15, width := 8 }]
    flops := #[]
    memories := #[{ aw := 4, dw := 8, nextImg := 14, depth := 12 }] }

def same (a b : RuntimeResult) : Bool := a.outputs == b.outputs && a.nextState.flops == b.nextState.flops

def main : IO UInt32 := do
  let mut fails : Array String := #[]
  let mut n := 0
  let check (name : String) (ok : Bool) (fails : Array String) : Array String :=
    if ok then fails else fails.push name
  -- 1. what the optimizer does
  let (P, acc) := optimizeWith prog
  n := n + 3
  fails := check "the checker accepted the optimizer's certificate" acc fails
  fails := check s!"sources 6 -> 3 (got {P.sources.size})" (P.sources.size == 3) fails
  fails := check s!"bindings 5 -> 4 (got {P.bindings.size})" (P.bindings.size == 4) fails
  -- 2. same results, every input and state tried
  for a in [0, 1, 5, 200, 255] do
    for q in [0, 9, 255] do
      n := n + 1
      let i : RuntimeInput := #[mk_bv 8 a, mk_bv 8 77]
      let s : RuntimeState := { flops := #[mk_bv 8 q], mems := #[] }
      fails := check s!"same step at a={a} q={q}"
        (same (denoteResidual P #[true] i s) (denoteResidual prog #[true] i s)) fails
  -- 3. the checker refuses wrong certificates
  let c := optimizeRaw prog
  let wrong : List (String × OptCert) := [
    ("a source mapped to a different constant",
      { c with newSources := c.newSources.set! 1 (.const 8 7) }),
    ("a needed binding dropped", { c with keep := c.keep.set! 1 false }),
    ("an output's binding dropped", { c with keep := c.keep.set! 0 false }),
    ("a kept binding put at the wrong slot", { c with pos := c.pos.set! 7 (c.pos[7]! + 1) }),
    ("keep of the wrong length", { c with keep := c.keep.pop })]
  for (name, w) in wrong do
    n := n + 1
    fails := check s!"refused: {name}" (!validate prog w) fails
  -- 4. a compiled program with a memory, 40 steps, optimized against unoptimized
  match compileDesign ramCert with
  | .error e => fails := fails.push s!"compileDesign refused the RAM: {repr e}"
  | .ok R =>
    let (R', acc2) := optimizeWith R
    n := n + 2
    fails := check "the RAM's certificate accepted" acc2 fails
    fails := check s!"the RAM shares its clamp constant and drops the unread sum \
                    ({R.sources.size} -> {R'.sources.size} sources, \
                    {R.bindings.size} -> {R'.bindings.size} bindings)"
      (R'.sources.size < R.sources.size && R'.bindings.size < R.bindings.size) fails
    let mut s1 : RuntimeState := { flops := #[], mems := #[fun _ => mk_bv 8 0] }
    let mut s2 := s1
    for k in List.range 40 do
      n := n + 1
      let i : RuntimeInput := #[mk_bv 4 (k % 16 : Nat), mk_bv 8 (k * 37 : Nat), mk_bv 1 1,
                                mk_bv 4 ((k * 5 + 3) % 16 : Nat)]
      let r1 := denoteResidual R #[true] i s1
      let r2 := denoteResidual R' #[true] i s2
      fails := check s!"RAM step {k}" (r1.outputs == r2.outputs) fails
      s1 := r1.nextState
      s2 := r2.nextState
    n := n + 1
    let img (s : RuntimeState) : List BV := (List.range 16).map fun (a : Nat) => (s.mems[0]!) (Int.ofNat a)
    fails := check "RAM image after 40 steps" (img s1 == img s2) fails
  for f in fails do IO.println s!"FAIL {f}"
  if fails.isEmpty then
    IO.println s!"PASS residual_opt: {n} cases"
    return 0
  return 1

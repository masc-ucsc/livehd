/-
Tests for vc's optimizer and its checker (`Compiler.ResidualOpt`).

  (cd formal/lean && LEAN_PATH=$PWD/.olean-dev lean --run ../../pass/lean/tools/tests/residual_opt_check.lean)

`ResidualOptSound.optimize_correct` already PROVES that `optimize` never changes a result;
these cases check that the optimizer does what it should (drops what is dead, shares equal
constants, folds constant bindings and propagates them, replaces copies and repeated
expressions by what they copy or repeat, folds muxes with a known select or equal arms, tells apart wide constants that one hash cannot), that the checker refuses wrong certificates, and
that the optimized program runs step for step like the original, a memory included.

Prints `PASS residual_opt: <n> cases` or one `FAIL` line per case that went wrong.
-/
import LeanSemanticPrimitives.Compiler.ResidualOpt
import LeanSemanticPrimitives.Compiler.CompileDesignDefs

open Compiler Compiler.ResidualOpt

/-- Sources: 0 input a, 1 const 5, 2 const 5 (a duplicate), 3 const 7, 4 input b (unread),
5 flop q. Bindings (slots 6..10): 6 = a + 5, 7 = a + 5 (through the duplicate: the same
expression once the constants are shared), 8 = a * 7 (nothing reads it), 9 = 6 xor 7,
10 = 9 | q. Outputs: slots 6 and 10; the flop takes 7. -/
def prog : ResidualProgram :=
  { sources := #[.input 0 8, .const 8 5, .const 8 5, .const 8 7, .input 1 8, .flopQ 0 8]
    bindings := #[⟨.bv 8, .rsum 8 2 #[0, 1]⟩, ⟨.bv 8, .rsum 8 2 #[0, 2]⟩, ⟨.bv 8, .rmult 8 #[0, 3]⟩,
                  ⟨.bv 8, .rxor 8 #[6, 7]⟩, ⟨.bv 8, .rorBits 8 #[9, 5]⟩]
    outputs := #[{ slot := 6, width := 8 }, { slot := 10, width := 8 }]
    flopUpdates := #[{ width := 8, din := 7, enable := none, resetPin := none, resetValue := 0,
                       resetActiveLow := false, clock := 0, asyncReset := false }]
    memoryUpdates := #[] }

/-- Folding. Sources: 0 input a, 1 const 3, 2 const 4. Bindings (slots 3..7):
3 = 3 + 4 (a constant: 7), 4 = 3 * a (reads the folded 3), 5 = 1 xor 3 (a constant through
the folded 3: 3 ^ 7 = 4, the value of source 2), 6 = 1 + 1 (a constant read only by a folded
binding), 7 = 6 xor 2 (6 ^ 4 = 2). Outputs: slots 4, 5 and 7. Expected: one binding (4) is
left; the sources are a, the constant 7 and the constant 4 (5's value) and 2 (7's). -/
def foldProg : ResidualProgram :=
  { sources := #[.input 0 8, .const 8 3, .const 8 4]
    bindings := #[⟨.bv 8, .rsum 8 2 #[1, 2]⟩, ⟨.bv 8, .rmult 8 #[3, 0]⟩, ⟨.bv 8, .rxor 8 #[1, 3]⟩,
                  ⟨.bv 8, .rsum 8 2 #[1, 1]⟩, ⟨.bv 8, .rxor 8 #[6, 2]⟩]
    outputs := #[{ slot := 4, width := 8 }, { slot := 5, width := 8 }, { slot := 7, width := 8 }]
    flopUpdates := #[]
    memoryUpdates := #[] }

/-- Copies and repeated expressions. Sources: 0 input a (8 bits), 1 input b (4 bits),
2 const 8 0xFF, 3 const 8 0x0F. Bindings (slots 4..9): 4 = a + b, 5 = a + b (repeated),
6 = | 4 (a one-operand OR of the same width: a copy of 4), 7 = GetMask a 0xFF (keeps every bit
of a: a copy of a), 8 = GetMask a 0x0F (keeps 4 of 8 bits: not a copy), 9 = 5 ^ 6 ^ 7 ^ 8,
10 = | b at 8 bits (a widening, not a copy). Outputs: 9, 6 and 10. Expected: 4, 8, 9 and
10 kept. -/
def copyProg : ResidualProgram :=
  { sources := #[.input 0 8, .input 1 4, .const 8 0xFF, .const 8 0x0F]
    bindings := #[⟨.bv 8, .rsum 8 2 #[0, 1]⟩, ⟨.bv 8, .rsum 8 2 #[0, 1]⟩,
                  ⟨.bv 8, .rorBits 8 #[4]⟩, ⟨.bv 8, .rgetMask 8 0 2⟩, ⟨.bv 8, .rgetMask 8 0 3⟩,
                  ⟨.bv 8, .rxor 8 #[5, 6, 7, 8]⟩, ⟨.bv 8, .rorBits 8 #[1]⟩]
    outputs := #[{ slot := 9, width := 8 }, { slot := 6, width := 8 }, { slot := 10, width := 8 }]
    flopUpdates := #[]
    memoryUpdates := #[] }

/-- Branch folding. Sources: 0 input a, 1 input b (8 bits), 2 const 1 1, 3 const 1 0,
4 input s (1 bit). Bindings (slots 5..9): 5 = mux(1, a, b) (takes b), 6 = mux(0, a, b)
(takes a), 7 = mux(s, a, a) (equal arms: a), 8 = mux(s, a, b) (kept), 9 = 5 ^ 6 ^ 7 ^ 8.
Outputs: 9 and 5. Expected: 8 and 9 kept. -/
def muxProg : ResidualProgram :=
  { sources := #[.input 0 8, .input 1 8, .const 1 1, .const 1 0, .input 2 1]
    bindings := #[⟨.bv 8, .rmux 8 2 0 1⟩, ⟨.bv 8, .rmux 8 3 0 1⟩, ⟨.bv 8, .rmux 8 4 0 0⟩,
                  ⟨.bv 8, .rmux 8 4 0 1⟩, ⟨.bv 8, .rxor 8 #[5, 6, 7, 8]⟩]
    outputs := #[{ slot := 9, width := 8 }, { slot := 5, width := 8 }]
    flopUpdates := #[]
    memoryUpdates := #[] }

/-- Wide constants with one hash: 2^64 and 2^95 agree in their low 64 bits and modulo
2^31 - 1, so the optimizer's tables see one hash for both and must compare the values.
Sources: 0 input a (128 bits), 1 const 2^64, 2 const 2^95, 3 const 2^64 (a duplicate of 1),
4 const 2^64 + 2^95. Bindings (slots 5..10): 5 = a + 1, 6 = a + 2, 7 = a + 3 (5 again once
the constants are shared), 8 = 1 + 2 (folded: source 4's value), 9 = a ^ 4,
10 = 5 ^ 6 ^ 7 ^ 8 ^ 9. Outputs: 10 and 8. Expected: four sources, 5, 6, 9 and 10 kept. -/
def wideProg : ResidualProgram :=
  { sources := #[.input 0 128, .const 128 (2 ^ 64), .const 128 (2 ^ 95), .const 128 (2 ^ 64),
                 .const 128 (2 ^ 64 + 2 ^ 95)]
    bindings := #[⟨.bv 128, .rsum 128 2 #[0, 1]⟩, ⟨.bv 128, .rsum 128 2 #[0, 2]⟩,
                  ⟨.bv 128, .rsum 128 2 #[0, 3]⟩, ⟨.bv 128, .rsum 128 2 #[1, 2]⟩,
                  ⟨.bv 128, .rxor 128 #[0, 4]⟩, ⟨.bv 128, .rxor 128 #[5, 6, 7, 8, 9]⟩]
    outputs := #[{ slot := 10, width := 128 }, { slot := 8, width := 128 }]
    flopUpdates := #[]
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
  fails := check s!"bindings 5 -> 3 (got {P.bindings.size})" (P.bindings.size == 3) fails
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
    ("a needed binding dropped", { c with keep := c.keep.set! 3 false }),
    ("an output's binding dropped", { c with keep := c.keep.set! 0 false }),
    ("a kept binding put at the wrong slot", { c with pos := c.pos.set! 7 (c.pos[7]! + 1) }),
    ("keep of the wrong length", { c with keep := c.keep.pop })]
  for (name, w) in wrong do
    n := n + 1
    fails := check s!"refused: {name}" (!validate prog w) fails
  -- 4. folding and propagation
  let (F, accF) := optimizeWith foldProg
  n := n + 4
  fails := check "the checker accepted the folding certificate" accF fails
  fails := check s!"folded: bindings 5 -> 1 (got {F.bindings.size})" (F.bindings.size == 1) fails
  fails := check s!"folded: sources a, 7, 4, 2 (got {repr F.sources})"
    (F.sources == #[.input 0 8, .const 8 7, .const 8 4, .const 8 2]) fails
  fails := check "folded: the kept product reads the folded 7"
    (F.bindings.map (·.rhs) == #[.rmult 8 #[1, 0]]) fails
  for a in [0, 1, 5, 200, 255] do
    n := n + 1
    let i : RuntimeInput := #[mk_bv 8 a]
    let s : RuntimeState := { flops := #[], mems := #[] }
    fails := check s!"folded: same step at a={a}"
      (same (denoteResidual F #[true] i s) (denoteResidual foldProg #[true] i s)) fails
  let cf := optimizeRaw foldProg
  let wrongF : List (String × OptCert) := [
    ("a binding that reads an input marked a constant",
      { cf with fold := cf.fold.set! 1 true, keep := cf.keep.set! 1 false }),
    ("a folded binding at a source with another constant",
      { cf with newSources := cf.newSources.set! 1 (.const 8 6) }),
    ("a folded binding at a source of another width",
      { cf with newSources := cf.newSources.set! 1 (.const 9 7) }),
    ("a folded binding at an input source", { cf with pos := cf.pos.set! 3 0 }),
    ("a read binding neither kept nor folded", { cf with fold := cf.fold.set! 0 false })]
  for (name, w) in wrongF do
    n := n + 1
    fails := check s!"refused: {name}" (!validate foldProg w) fails
  -- 5. copies and repeated expressions
  let (Cp, accC) := optimizeWith copyProg
  n := n + 3
  fails := check "the checker accepted the copy/repeat certificate" accC fails
  fails := check s!"copies: bindings 7 -> 4 (got {Cp.bindings.size})" (Cp.bindings.size == 4) fails
  let cc := optimizeRaw copyProg
  fails := check s!"copies: the reasons (got {repr cc.just})"
    (cc.just == #[.none, .cse, .copy 4, .copy 0, .none, .none, .none]) fails
  for a in [0, 1, 0x5A, 200, 255] do
    for b in [0, 3, 15] do
      n := n + 1
      let i : RuntimeInput := #[mk_bv 8 a, mk_bv 4 b]
      let s : RuntimeState := { flops := #[], mems := #[] }
      fails := check s!"copies: same step at a={a} b={b}"
        (same (denoteResidual Cp #[true] i s) (denoteResidual copyProg #[true] i s)) fails
  let wrongC : List (String × OptCert) := [
    ("a repeated expression at another binding's slot",
      { cc with pos := cc.pos.set! 5 cc.pos[8]! }),
    ("a copy of an operand it does not copy", { cc with just := cc.just.set! 2 (.copy 0) }),
    ("a copy whose new slot is not its operand's",
      { cc with pos := cc.pos.set! 6 cc.pos[9]! }),
    ("a GetMask that drops bits marked a copy",
      { cc with just := cc.just.set! 4 (.copy 0), keep := cc.keep.set! 4 false,
                pos := cc.pos.set! 8 cc.pos[0]! }),
    ("a widening marked a copy",
      { cc with just := cc.just.set! 6 (.copy 1), keep := cc.keep.set! 6 false,
                pos := cc.pos.set! 10 cc.pos[1]! }),
    ("a repeat marked for a binding with no earlier twin",
      { cc with just := cc.just.set! 4 .cse, keep := cc.keep.set! 4 false })]
  for (name, w) in wrongC do
    n := n + 1
    fails := check s!"refused: {name}" (!validate copyProg w) fails
  -- 6. branch folding
  let (Mx, accM) := optimizeWith muxProg
  n := n + 3
  fails := check "the checker accepted the branch-folding certificate" accM fails
  fails := check s!"muxes: bindings 5 -> 2 (got {Mx.bindings.size})" (Mx.bindings.size == 2) fails
  let cm := optimizeRaw muxProg
  fails := check s!"muxes: the reasons (got {repr cm.just})"
    (cm.just == #[.copy 1, .copy 0, .copy 0, .none, .none]) fails
  for a in [0, 1, 0x5A, 255] do
    for b in [0, 7, 200] do
      for sel in [0, 1] do
        n := n + 1
        let i : RuntimeInput := #[mk_bv 8 a, mk_bv 8 b, mk_bv 1 sel]
        let s : RuntimeState := { flops := #[], mems := #[] }
        fails := check s!"muxes: same step at a={a} b={b} s={sel}"
          (same (denoteResidual Mx #[true] i s) (denoteResidual muxProg #[true] i s)) fails
  let wrongM : List (String × OptCert) := [
    ("a known-select mux marked a copy of the arm it does not take",
      { cm with just := cm.just.set! 0 (.copy 0), pos := cm.pos.set! 5 cm.pos[0]! }),
    ("a mux with a select the checker does not know marked a copy",
      { cm with just := cm.just.set! 3 (.copy 0), keep := cm.keep.set! 3 false,
                pos := cm.pos.set! 8 cm.pos[0]! })]
  for (name, w) in wrongM do
    n := n + 1
    fails := check s!"refused: {name}" (!validate muxProg w) fails
  -- 6b. wide constants with one hash
  let (Wd, accW) := optimizeWith wideProg
  let cw := optimizeRaw wideProg
  n := n + 5
  fails := check "the checker accepted the wide-constant certificate" accW fails
  fails := check s!"wide: sources a, 2^64, 2^95, 2^64 + 2^95 (got {repr Wd.sources})"
    (Wd.sources == #[.input 0 128, .const 128 (2 ^ 64), .const 128 (2 ^ 95),
                     .const 128 (2 ^ 64 + 2 ^ 95)]) fails
  fails := check s!"wide: bindings 6 -> 4 (got {Wd.bindings.size})" (Wd.bindings.size == 4) fails
  fails := check s!"wide: the reasons (got {repr cw.just})"
    (cw.just == #[.none, .none, .cse, .none, .none, .none]) fails
  fails := check s!"wide: the folded sum (got {repr cw.fold})"
    (cw.fold == #[false, false, false, true, false, false]) fails
  for a in [0, 1, 2 ^ 64, 2 ^ 95, 2 ^ 128 - 1] do
    n := n + 1
    let i : RuntimeInput := #[mk_bv 128 a]
    let s : RuntimeState := { flops := #[], mems := #[] }
    fails := check s!"wide: same step at a={a}"
      (same (denoteResidual Wd #[true] i s) (denoteResidual wideProg #[true] i s)) fails
  n := n + 1
  fails := check "refused: 2^95 read at 2^64's new slot"
    (!validate wideProg { cw with pos := cw.pos.set! 2 cw.pos[1]! }) fails
  -- 7. a compiled program with a memory, 40 steps, optimized against unoptimized
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

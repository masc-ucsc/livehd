/-
Tests for vc's incremental step (`Compiler.ResidualInc`).

  (cd formal/lean && LEAN_PATH=$PWD/.olean-dev lean --run ../../pass/lean/tools/tests/residual_inc_check.lean)

`ResidualInc.runInc_eq_runFull` already PROVES that an incremental run (the first step full,
every later step from the last step's environment, recomputing only the bindings an operand of
which changed) is a run of `denoteResidual`. These cases run both on hand-built and compiled
programs, a RAM included (a memory image never compares equal, so its readers are always
recomputed), with inputs that change on some steps and not on others, before and after the
optimizer, and check that `wellOrdered` refuses a binding that reads a later slot. The
event-driven step (`ResidualEv`: only the bindings a changed slot's fanout reaches are visited,
`runEv_eq_runFull`) runs the same cases, and `fanout` must list every reader of every slot.

Prints `PASS residual_inc: <n> cases` or one `FAIL` line per case that went wrong.
-/
import LeanSemanticPrimitives.Compiler.ResidualInc
import LeanSemanticPrimitives.Compiler.ResidualEv
import LeanSemanticPrimitives.Compiler.CompileDesignDefs

open Compiler Compiler.ResidualOpt Compiler.ResidualInc

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

/-- A binding reads slot 3, which is the binding after it: not well-ordered. -/
def forwardProg : ResidualProgram :=
  { sources := #[.input 0 8]
    bindings := #[⟨.bv 8, .rnot 8 2⟩, ⟨.bv 8, .rnot 8 0⟩]
    outputs := #[{ slot := 1, width := 8 }]
    flopUpdates := #[]
    memoryUpdates := #[] }

def sameRes (a b : RuntimeResult) : Bool :=
  a.outputs == b.outputs && a.nextState.flops == b.nextState.flops

/-- `fanout R` lists, for every binding, every slot it reads (`FanOK`, checked). -/
def fanOKb (R : ResidualProgram) : Bool :=
  let fan := fanout R
  (List.range R.bindings.size).all fun k =>
    (exprRefs R.bindings[k]!.rhs).all fun r =>
      !(r < R.sources.size + R.bindings.size) || (fan.getD r #[]).contains k

def sameRuns (a b : List RuntimeResult) : Bool :=
  a.length == b.length && (a.zip b).all fun (x, y) => sameRes x y

def main : IO UInt32 := do
  let mut fails : Array String := #[]
  let mut n := 0
  let check (name : String) (ok : Bool) (fails : Array String) : Array String :=
    if ok then fails else fails.push name
  -- 1. a hand-built program with a flop: input a changes every 4th step, b every step
  let ticks1 : List (ClockEdges × RuntimeInput) := (List.range 40).map fun k =>
    (#[k % 3 != 2], #[mk_bv 8 ((k / 4) * 37 : Nat), mk_bv 8 (k * 11 : Nat)])
  let s1 : RuntimeState := { flops := #[mk_bv 8 3], mems := #[] }
  for (name, R) in [("prog", prog), ("prog optimized", optimize prog),
                    ("copies", copyProg), ("copies optimized", optimize copyProg),
                    ("folding optimized", optimize foldProg), ("muxes optimized", optimize muxProg)] do
    n := n + 4
    fails := check s!"{name}: well-ordered" (wellOrdered R) fails
    let ticks := ticks1.map fun (e, i) => (e, i ++ #[mk_bv 1 (i[0]!.value % 2)])
    fails := check s!"{name}: incremental run = full run, 40 steps"
      (sameRuns (runInc R s1 ticks) (runFull R s1 ticks)) fails
    fails := check s!"{name}: fanout lists every reader" (fanOKb R) fails
    fails := check s!"{name}: event-driven run = full run, 40 steps"
      (sameRuns (runEv R s1 ticks) (runFull R s1 ticks)) fails
  -- 2. a compiled RAM: the address changes every step, the data every 3rd, the enable toggles
  match compileDesign ramCert with
  | .error e => fails := fails.push s!"compileDesign refused the RAM: {repr e}"
  | .ok R =>
    let ticks : List (ClockEdges × RuntimeInput) := (List.range 60).map fun k =>
      (#[true], #[mk_bv 4 (k % 16 : Nat), mk_bv 8 ((k / 3) * 29 : Nat), mk_bv 1 ((k / 5) % 2 : Nat),
                  mk_bv 4 ((k * 5 + 3) % 16 : Nat)])
    let s0 : RuntimeState := { flops := #[], mems := #[fun _ => mk_bv 8 0] }
    for (name, P) in [("RAM", R), ("RAM optimized", optimize R)] do
      n := n + 4
      fails := check s!"{name}: well-ordered" (wellOrdered P) fails
      fails := check s!"{name}: incremental run = full run, 60 steps"
        (sameRuns (runInc P s0 ticks) (runFull P s0 ticks)) fails
      fails := check s!"{name}: fanout lists every reader" (fanOKb P) fails
      fails := check s!"{name}: event-driven run = full run, 60 steps"
        (sameRuns (runEv P s0 ticks) (runFull P s0 ticks)) fails
      -- the memory image after the runs
      n := n + 1
      let lastI := (runInc P s0 ticks).getLast?.map (·.nextState)
      let lastF := (runFull P s0 ticks).getLast?.map (·.nextState)
      let img (s : Option RuntimeState) : List BV :=
        match s with
        | some st => (List.range 16).map fun (a : Nat) => (st.mems[0]!) (Int.ofNat a)
        | none => []
      fails := check s!"{name}: the same memory image after 60 steps" (img lastI == img lastF) fails
      n := n + 1
      let lastE := (runEv P s0 ticks).getLast?.map (·.nextState)
      fails := check s!"{name}: the same memory image after 60 event-driven steps" (img lastE == img lastF) fails
  -- 3. a binding that reads a later slot is refused
  n := n + 1
  fails := check "a forward reference is not well-ordered" (!wellOrdered forwardProg) fails
  for f in fails do IO.println s!"FAIL {f}"
  if fails.isEmpty then
    IO.println s!"PASS residual_inc: {n} cases"
    return 0
  return 1

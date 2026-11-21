/-
Tests for `lhdcert-ckpt v1` (`Compiler/Ckpt.lean`) on small certificates built here.

  (cd formal/lean && LEAN_PATH=$PWD/.olean-dev lean --run ../../pass/lean/tools/tests/ckpt_check.lean)

Prints `PASS ckpt_check: <n> cases` or one `FAIL` line per case that went wrong.

The design: one 8-bit flop (`c <= c + 1`, power-on value 5) and one memory of 12 words
(`aw = 4`) with a write port (`we`, `wa`, `wd`) and a read port (`ra`); outputs: the flop and
the read word.
-/
import LeanSemanticPrimitives.Compiler.Ckpt
import LeanSemanticPrimitives.Compiler.DirectTrace
import LeanSemanticPrimitives.Compiler.DesignSemantics
import LeanSemanticPrimitives.Compiler.DirectSim

open Compiler Compiler.Ckpt

/-- Slots: 0 `we`, 1 `wa`, 2 `wd`, 3 `ra`, 4 the memory image, 5 the flop, 6 the constant 1;
nodes from slot 7. -/
def D0 : DesignCert :=
  let n (op : LGraphOp) (w : Nat) (deps : List Nat) : DenseNodeCert :=
    { op := op, width := w, deps := deps.toArray }
  { sources := #[.input 0 1, .input 1 4, .input 2 8, .input 3 4, .memImg 0 4 8, .flopQ 0 8,
                 .const 1 1]
    nodes := #[
      n (.Op_Sum 2) 8 [5, 6],              -- 7:  c + 1
      n .Op_MemWrite 8 [4, 1, 2, 0],       -- 8:  the write
      n .Op_MemRead 8 [4, 3, 6]]           -- 9:  the read
    outputs := #[{ slot := 5, width := 8 }, { slot := 9, width := 8 }]
    flops := #[{ width := 8, din := 7, enable := none, resetPin := none, resetValue := 5,
                 resetActiveLow := false }]
    memories := #[{ aw := 4, dw := 8, nextImg := 8, depth := 12 }] }

def H0 : Nat := designHash D0

def inp (we wa wd ra : Nat) : RuntimeInput :=
  #[mk_bv 1 we, mk_bv 4 wa, mk_bv 8 wd, mk_bv 4 ra]

def outs (r : RuntimeResult) : List Nat := r.outputs.toList.map fun b => (bv_uint b).toNat

def contains (s t : String) : Bool := (s.splitOn t).length > 1

def main : IO UInt32 := do
  let mut fails : Array String := #[]
  let mut n := 0
  let s0 := Direct.resetState D0
  let c0 := save D0 H0 0 #[] s0
  -- 1. a saved power-on state validates, round-trips through bytes, and restore then save is the identity
  n := n + 1
  if let some e := validate D0 H0 c0 then fails := fails.push s!"power-on checkpoint refused: {e}"
  n := n + 1
  if c0.flops != #[(8, 5)] then fails := fails.push s!"power-on flop {repr c0.flops}"
  n := n + 1
  match parse (render c0) with
  | .ok c => if c != c0 then fails := fails.push "parse (render c) ≠ c"
  | .error e => fails := fails.push s!"render/parse: {e}"
  n := n + 1
  if save D0 H0 0 #[] (restore c0) != c0 then fails := fails.push "save (restore c) ≠ c"
  -- 2. run 4 steps (writes at 3 and 11, a write past depth at 13), save, restore, and both continue alike
  let steps := [inp 1 3 0x5a 3, inp 1 11 0x77 11, inp 1 13 0x99 0, inp 0 0 0 3]
  let s4 := steps.foldl (fun s i => (interpretDesign D0 (allEdges D0) i s).nextState) s0
  let c4 := save D0 H0 4 #[] s4
  n := n + 1
  if let some e := validate D0 H0 c4 then fails := fails.push s!"checkpoint after 4 steps refused: {e}"
  n := n + 1
  if c4.mems[0]!.words != #[(3, 0x5a), (11, 0x77)] then
    fails := fails.push s!"memory words after 4 steps: {repr c4.mems[0]!.words}"
  let r := restore c4
  let probe := [inp 0 0 0 3, inp 0 0 0 11, inp 1 5 0x10 5, inp 0 0 0 5, inp 0 0 0 12]
  let runFrom (s : RuntimeState) : List (List Nat) :=
    (probe.foldl (fun (acc : List (List Nat) × RuntimeState) i =>
      let res := interpretDesign D0 (allEdges D0) i acc.2
      (acc.1 ++ [outs res], res.nextState)) ([], s)).1
  n := n + 1
  if runFrom r != runFrom s4 then
    fails := fails.push s!"resume differs: {runFrom r} vs {runFrom s4}"
  -- 3. memory image: below depth, past depth, negative
  n := n + 1
  let m := memImage { aw := 4, dw := 8, depth := 12, dflt := 7, words := #[(2, 9)] }
  if [(bv_uint (m 2)).toNat, (bv_uint (m 3)).toNat, (bv_uint (m 12)).toNat, (bv_uint (m (-1))).toNat]
      != [9, 7, 7, 7] then fails := fails.push "memImage reads"
  -- 4. refusals, one per rule
  let refuse (name : String) (c : Ckpt) (msg : String) : Option String :=
    match validate D0 H0 c with
    | some e => if contains e msg then none else some s!"{name}: message '{e}' lacks '{msg}'"
    | none => some s!"{name}: accepted"
  let mem0 := c4.mems[0]!
  let withMem (m : MemCk) : Ckpt := { c4 with mems := #[m] }
  let cases : List (String × Ckpt × String) := [
    ("design hash", { c4 with design := H0 + 1 }, "does not match the certificate's"),
    ("flop count", { c4 with flops := #[] }, "0 flops, the certificate declares 1"),
    ("flop width", { c4 with flops := #[(9, 1)] }, "flop 0: width 9, the certificate declares 8"),
    ("flop value", { c4 with flops := #[(8, 256)] }, "flop 0: value 0x100 does not fit in 8 bits"),
    ("memory count", { c4 with mems := #[] }, "0 memories, the certificate declares 1"),
    ("memory shape", withMem { mem0 with depth := 16 }, "depth=16, the certificate declares"),
    ("default", withMem { mem0 with dflt := 256 }, "default 0x100 does not fit in 8 bits"),
    ("past depth", withMem { mem0 with words := #[(12, 1)] }, "word at 0xc is past the memory's depth 12"),
    ("word value", withMem { mem0 with words := #[(1, 300)] }, "value 0x12c does not fit in 8 bits"),
    ("order", withMem { mem0 with words := #[(5, 1), (3, 2)] }, "not strictly increasing at 0x3"),
    ("repeat", withMem { mem0 with words := #[(5, 0)] }, "repeats the default")]
  for (name, c, msg) in cases do
    n := n + 1
    if let some f := refuse name c msg then fails := fails.push f
  -- a memory whose next image is not a version of itself
  n := n + 1
  let Dbad : DesignCert := { D0 with memories := #[{ aw := 4, dw := 8, nextImg := 1, depth := 12 }] }
  match validate Dbad (designHash Dbad) (save Dbad (designHash Dbad) 0 #[] (Direct.resetState Dbad)) with
  | some e => if !contains e "is not a version of memory 0" then fails := fails.push s!"imgRoot: {e}"
  | none => fails := fails.push "imgRoot: accepted"
  -- a write of 9-bit data into the 8-bit memory, and a load whose node is 16 bits wide
  for (name, Dw, msg) in [
      ("wide write", { D0 with sources := D0.sources.set! 2 (.input 2 9) },
        "node 8 stores 9-bit words into memory 0, whose words are 8 bits"),
      ("wide load", { D0 with nodes := D0.nodes.set! 1 { op := .Op_MemLoad 4, width := 16, deps := #[4, 2, 0] } },
        "node 8 stores 16-bit words into memory 0")] do
    n := n + 1
    match validate Dw (designHash Dw) (save Dw (designHash Dw) 0 #[] (Direct.resetState Dw)) with
    | some e => if !contains e msg then fails := fails.push s!"{name}: {e}"
    | none => fails := fails.push s!"{name}: accepted"
  -- 5. parse errors
  let bytes := render c4
  let parseErr (name : String) (b : ByteArray) (msg : String) : Option String :=
    match parse b with
    | .error e => if contains e msg then none else some s!"{name}: '{e}' lacks '{msg}'"
    | .ok _ => some s!"{name}: parsed"
  let flipped := bytes.set! 30 (bytes[30]! ^^^ 1)
  for (name, b, msg) in [
      ("digest", flipped, "digest mismatch"),
      ("truncated", bytes.extract 0 (bytes.size - 30), "ckpt:"),
      ("trailing", bytes ++ "x\n".toUTF8, "trailing bytes after the digest line"),
      ("magic", "hello\n".toUTF8, "not an lhdcert-ckpt v1 file (first line is 'hello')")] do
    n := n + 1
    if let some f := parseErr name b msg then fails := fails.push f
  -- 6. an ext section with arbitrary bytes round-trips, and seal recomputes the digest
  n := n + 1
  let ce := { c4 with exts := #[{ name := "host state", bytes := [0, 10, 32, 255, 10] }] }
  match parse (render ce) with
  | .ok c => if c != ce then fails := fails.push "ext round trip"
  | .error e => fails := fails.push s!"ext round trip: {e}"
  n := n + 1
  match parse flipped (checkDigest := false) with
  | .ok c => if parse (render c) |>.toOption |>.isNone then fails := fails.push "seal"
  | .error e => fails := fails.push s!"seal parse: {e}"
  -- without its digest line: refused when checked, read when sealing
  n := n + 1
  let body := renderBody c4
  match parse body, parse body (checkDigest := false) with
  | .error _, .ok c => if c != c4 then fails := fails.push "no digest line: contents"
  | r1, r2 => fails := fails.push s!"no digest line: {r1.toOption.isSome} {r2.toOption.isSome}"
  -- 7. normalize and diff
  n := n + 1
  let messy := withMem { mem0 with words := #[(11, 0x77), (3, 1), (3, 0x5a), (4, 0)] }
  if Ckpt.diff (normalize messy) c4 != none then
    fails := fails.push s!"normalize: {Ckpt.diff (normalize messy) c4}"
  n := n + 1
  match Ckpt.diff c4 { c4 with flops := #[(8, 3)] } with
  | some d => if !contains d "flop 0" then fails := fails.push s!"diff: {d}"
  | none => fails := fails.push "diff: SAME"
  -- 8. the lgraph-sim `--state` text describes the same state (lgraph-sim's own parser)
  n := n + 1
  let cs := withMem { mem0 with dflt := 0x11 }
  match Direct.parseStateFile (toStateText cs) with
  | .error e => fails := fails.push s!"to-state parse: {e}"
  | .ok st =>
    let a := st.build D0
    let b := restore cs
    let rd (s : RuntimeState) : List Nat :=
      (s.flops.toList.map fun x => (bv_uint x).toNat) ++
      ((List.range 16).map fun x => (bv_uint (s.mems[0]! (Int.ofNat x))).toNat) ++
      [(bv_uint (s.mems[0]! (-1))).toNat]
    if rd a != rd b then fails := fails.push s!"to-state: {rd a} vs {rd b}"
  for f in fails do IO.println s!"FAIL {f}"
  if fails.isEmpty then
    IO.println s!"PASS ckpt_check: {n} cases"
    return 0
  return 1

/-
Tests for DCERT4's memory depth and the load-time address check
(`Compiler.MemAddr`, run by `CertIO.certWfError` / `readCertChecked`).

  (cd formal/lean && LEAN_PATH=$PWD/.olean-dev lean --run ../../pass/lean/tools/tests/mem_addr_check.lean)

Prints `PASS mem_addr_check: <n> cases` or one `FAIL` line per case that went wrong.

The design: one memory of `aw = 4` (a 16-word table) with one write and one read
port. With `clamp`, each address goes through the clamp the front ends build,
`idx < K ? idx : 0`, then the exporter's resize `Op_Or [x]`; without it, the raw
4-bit index goes straight to the resize. `pass.bitwidth` proves the clamped
index inside `[0, K)`, and the loader must agree exactly when `K ≤ depth`.

`ramMask` writes the same clamp without a branch, as LiveHD's optimizer rewrites it:
`idx & sext(idx < K)` (the compare sign-extended to the index's width, through one or two
`Op_Sext`, the operands in either order). The loader must agree exactly as for the mux, refuse
a mask whose compare guards another index, and bound a constant mask by the constant.
-/
import LeanSemanticPrimitives.Compiler.CertIOCodec

open Compiler Compiler.CertIO

/-- Slots: 0 `wa`, 1 `wd`, 2 `we`, 3 `ra`, 4 the memory image, 5 the constant
`K`, 6 the constant 0; nodes from slot 7. -/
def ram (depth K : Nat) (clamp : Bool) : DesignCert :=
  let n (op : LGraphOp) (w : Nat) (deps : List Nat) : DenseNodeCert :=
    { op := op, width := w, deps := deps.toArray }
  { sources := #[.input 0 4, .input 1 8, .input 2 1, .input 3 4, .memImg 0 4 8,
                 .const 4 K, .const 4 0]
    nodes := #[
      n .Op_ULT 1 [0, 5],                          -- 7:  wa < K
      n .Op_MuxBool 4 [7, 6, 0],                   -- 8:  wa < K ? wa : 0
      n .Op_Or 4 [if clamp then 8 else 0],         -- 9:  the resize
      n .Op_ULT 1 [3, 5],                          -- 10: ra < K
      n .Op_MuxBool 4 [10, 6, 3],                  -- 11: ra < K ? ra : 0
      n .Op_Or 4 [if clamp then 11 else 3],        -- 12: the resize
      n .Op_MemWrite 8 [4, 9, 1, 2],               -- 13: the write
      n .Op_MemRead 8 [4, 12, 2]]                  -- 14: the read
    outputs := #[{ slot := 14, width := 8 }]
    flops := #[]
    memories := #[{ aw := 4, dw := 8, nextImg := 13, depth := depth }] }

/-- `ram` with each clamp written as a mask, `idx & sext(idx < K)`. Slots: 0 `wa`, 1 `wd`, 2 `we`,
3 `ra`, 4 the memory image, 5 the constant `K`, 6 the constant 1 (1 bit), 7 the constant 1
(2 bits), 8 the constant 7; nodes from slot 9. The write's mask goes through two sign
extensions (as lhd writes it), the read's through one; `swap` puts the mask first; the write's
compare reads slot `guard` (0: its own index); `constMask` makes the read's address `ra & 7`. -/
def ramMask (depth K : Nat) (swap : Bool := false) (guard : Nat := 0) (constMask : Bool := false) :
    DesignCert :=
  let n (op : LGraphOp) (w : Nat) (deps : List Nat) : DenseNodeCert :=
    { op := op, width := w, deps := deps.toArray }
  { sources := #[.input 0 4, .input 1 8, .input 2 1, .input 3 4, .memImg 0 4 8,
                 .const 4 K, .const 1 1, .const 2 1, .const 4 7]
    nodes := #[
      n .Op_ULT 1 [guard, 5],                      -- 9:  wa < K (or another slot < K)
      n .Op_Sext 1 [9, 6],                         -- 10: sext at 1 bit
      n .Op_Sext 4 [10, 7],                        -- 11: the mask, 1111 when the compare holds
      n .Op_And 4 (if swap then [11, 0] else [0, 11]), -- 12: wa & mask
      n .Op_Or 4 [12],                             -- 13: the resize
      n .Op_ULT 1 [3, 5],                          -- 14: ra < K
      n .Op_Sext 4 [14, 6],                        -- 15: the mask, one sign extension
      n .Op_And 4 (if constMask then [3, 8] else [3, 15]), -- 16: ra & mask (or ra & 7)
      n .Op_Or 4 [16],                             -- 17: the resize
      n .Op_MemWrite 8 [4, 13, 1, 2],              -- 18: the write
      n .Op_MemRead 8 [4, 17, 2]]                  -- 19: the read
    outputs := #[{ slot := 19, width := 8 }]
    flops := #[]
    memories := #[{ aw := 4, dw := 8, nextImg := 18, depth := depth }] }

/-- `ram`, plus a whole-array bus read of `n` words (slot 15, an output). -/
def ramBus (depth n : Nat) : DesignCert :=
  let D := ram depth depth true
  { D with nodes := D.nodes.push { op := .Op_MemToBus n, width := 8 * n, deps := #[4] }
           outputs := D.outputs.push { slot := 15, width := 8 * n } }

/-- A ROM read at a raw 4-bit address: not checked, its reads already stop at
its real `contents.size`. -/
def rom : DesignCert :=
  { sources := #[.input 0 4, .memConst 4 8 #[1, 2, 3]]
    nodes := #[{ op := .Op_MemRead, width := 8, deps := #[1, 0, 0] }]
    outputs := #[{ slot := 2, width := 8 }]
    flops := #[]
    memories := #[] }

def contains (s t : String) : Bool := (s.splitOn t).length > 1

def main : IO UInt32 := do
  let mut fails : Array String := #[]
  let mut n := 0
  -- 1. admission
  let admit (name : String) (D : DesignCert) (want : Option String) : IO (Option String) := do
    match certWfError D, want with
    | none, none => return none
    | some w, some t => return (if contains w t then none else some s!"{name}: refused, but `{w}` lacks `{t}`")
    | none, some t => return some s!"{name}: admitted, expected a refusal naming `{t}`"
    | some w, none => return some s!"{name}: refused: {w}"
  let cases : List (String × DesignCert × Option String) := [
    ("12-word memory, clamped at 12",          ram 12 12 true,  none),
    ("12-word memory, clamped at 11",          ram 12 11 true,  none),
    ("12-word memory, raw 4-bit address",      ram 12 12 false, some "bounded only by 15"),
    ("12-word memory, clamped at 13",          ram 12 13 true,  some "bounded only by 12"),
    ("16-word memory, raw 4-bit address",      ram 16 0 false,  none),
    ("12-word memory, mask clamp at 12",       ramMask 12 12,   none),
    ("12-word memory, mask clamp at 12, mask first", ramMask 12 12 (swap := true), none),
    ("12-word memory, mask clamp at 11",       ramMask 12 11,   none),
    ("12-word memory, mask clamp at 13",       ramMask 12 13,   some "bounded only by 12"),
    ("12-word memory, mask guarding another index", ramMask 12 12 (guard := 3), some "bounded only by 15"),
    ("12-word memory, constant mask 7 on the read", ramMask 12 12 (constMask := true), none),
    ("6-word memory, constant mask 7 on the read",  ramMask 6 6 (constMask := true), some "bounded only by 7"),
    ("whole table by default (DCERT3 meaning)",
       { ram 12 12 false with memories := #[{ aw := 4, dw := 8, nextImg := 13 }] }, none),
    ("depth 0",                                ram 0 0 true,    some "declares depth 0"),
    ("depth past the 2^aw table",              ram 17 12 true,  some "declares depth 17"),
    ("bus read of all 12 words",               ramBus 12 12,    none),
    ("bus read of 13 words of 12",             ramBus 12 13,    some "moves 13 words"),
    ("ROM read at a raw address",              rom,             none)]
  for (name, D, want) in cases do
    n := n + 1
    if let some f ← admit name D want then fails := fails.push f
  -- 2. the codec: DCERT4 round trip, and DCERT3 read as the whole table
  n := n + 1
  let D := ram 12 12 true
  match parseCert (renderCert D) with
  | .ok D' =>
    if (renderCert D').data != (renderCert D).data || D'.memories.map (·.depth) != #[12] then
      fails := fails.push "round trip changed a depth-12 certificate"
  | .error e => fails := fails.push s!"round trip refused: {e}"
  n := n + 1
  if !((String.fromUTF8! (renderCert D)).startsWith "DCERT4\n") then
    fails := fails.push "renderCert does not write DCERT4"
  let one (magic depth : String) : ByteArray :=
    s!"{magic}\n1\n4 0 4 8\n0\n0\n0\n1\n4 8 0 0 0 0 0 0{depth}\n1\n5 clock\n".toUTF8
  for (name, bytes, want) in [("DCERT4 depth 12", one "DCERT4" " 12", some 12),
                              ("DCERT3, no depth: the whole table", one "DCERT3" "", some 16),
                              ("DCERT4 without its depth", one "DCERT4" "", none),
                              ("DCERT3 with a stray depth", one "DCERT3" " 12", none)] do
    n := n + 1
    match parseCert bytes, want with
    | .ok D', some d =>
      if D'.memories.map (·.depth) != #[d] then
        fails := fails.push s!"{name}: depth {D'.memories.map (·.depth)}, expected {d}"
    | .error _, none => pure ()
    | .ok _, none => fails := fails.push s!"{name}: parsed, expected a refusal"
    | .error e, some _ => fails := fails.push s!"{name}: {e}"
  -- 3. the loader refuses, by name, a certificate whose address escapes
  n := n + 1
  match readCertChecked (renderCert (ram 12 12 false)) with
  | .error e =>
    if !contains e "refused: node 6 " then fails := fails.push s!"loader message: {e}"
  | .ok _ => fails := fails.push "the loader admitted an unclamped 12-word memory"
  for f in fails do IO.println s!"FAIL {f}"
  if fails.isEmpty then
    IO.println s!"PASS mem_addr_check: {n} cases"
    return 0
  return 1

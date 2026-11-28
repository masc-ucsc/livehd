import LeanSemanticPrimitives.Compiler.CertIOCodec

/-!
The operator corpus, for the route checks (RS-23): one certificate with every operator of the
routes' accepted fragment (`Compiler.DirectCheck.opShape`: 26 operators, at their accepted arities
and widths), at mixed and corner widths. lhd's Verilog reader never emits some of them (n-ary
MuxN with a select out of range, MemLoad, MemToBus, mixed-width operands), so no exported design
covers them. The other five operators are outside the fragment, each in a certificate of its own
that the routes must refuse: Sub, Div, LT and GT (every route but ref: no exporter emits them),
and Const as a node (vc: constants are sources).

    lean --run gen_op_corpus.lean <out-dir>
      writes <out-dir>/opcorpus.dcert and <out-dir>/refuse_{sub,div,lt,gt,const}.dcert

Every node is an output, so a route that computes any node differently is caught at the cycle it
does. Corner operands are constant sources, so they are hit on every cycle whatever the inputs:
division by 0, MIN / -1 at 8 and 65 bits, shift amounts 0, w-1, w and 200, sign extension from 0,
from the operand's width and from above it, a MuxN select out of range, empty operand lists, an
all-ones mask, a contiguous mask. The state: an accumulator flop with an enable and a synchronous
reset on clock 0, a flop with an asynchronous reset on clock 1, and a 4-word memory on clock 0 with
a write port, a byte-enable write port, a whole-array load, whole-array reads onto a bus and an
asynchronous whole-array reset. engine_check runs it on every route and knob against ref.
-/

open Compiler

/-- Inputs, by ordinal: name and width. -/
def inputs : Array (String × Nat) :=
  #[("a8", 8), ("b8", 8), ("a65", 65), ("b65", 65), ("s3", 3), ("sh", 8), ("en", 1), ("addr", 2),
    ("d16", 16), ("be", 2), ("bus", 64), ("m8", 8), ("rst", 1), ("addr2", 2), ("en2", 1)]

/-- Constants: width and value. -/
def consts : Array (Nat × Int) :=
  #[(8, 0), (8, 0x80), (8, 0xFF), (65, 2 ^ 64), (65, 2 ^ 65 - 1), (65, 0), (8, 7), (8, 8), (8, 200),
    (8, 65), (1, 1), (64, 0), (8, 0xF0), (3, 7), (8, 1), (65, 1)]

def nIn : Nat := inputs.size
def inp (name : String) : Nat := (inputs.findIdx? (·.1 == name)).getD 0
/-- the slot of constant `k` of `consts` -/
def cst (k : Nat) : Nat := nIn + k
def memSlot : Nat := nIn + consts.size
def accQ : Nat := memSlot + 1
def asyQ : Nat := memSlot + 2

def sources : Array SourceDesc :=
  (inputs.mapIdx fun i (_, w) => SourceDesc.input i w)
  ++ consts.map (fun (w, v) => SourceDesc.const w v)
  ++ #[.memImg 0 2 16, .flopQ 0 8, .flopQAsync 1 8 (inp "rst") 0x5A false]

/-- Nodes and the outputs so far. -/
abbrev Bld := StateM (Array DenseNodeCert × Array OutputDesc)

/-- A node; it is also an output unless `out` is false (memory images are not outputs). -/
def nd (op : LGraphOp) (w : Nat) (deps : List Nat) (out : Bool := true) : Bld Nat := do
  let (ns, os) ← get
  let slot := sources.size + ns.size
  set (ns.push { op, width := w, deps := deps.toArray, origin := ns.size },
       if out then os.push { slot, width := w } else os)
  return slot

structure Built where
  accDin : Nat
  asyDin : Nat
  nextImg : Nat
  asyncImg : Nat

def build : Bld Built := do
  let a8 := inp "a8"; let b8 := inp "b8"; let a65 := inp "a65"; let b65 := inp "b65"
  let s3 := inp "s3"; let sh := inp "sh"; let en := inp "en"; let m8 := inp "m8"
  let zero8 := cst 0; let min8 := cst 1; let neg8 := cst 2; let min65 := cst 3; let neg65 := cst 4
  let zero65 := cst 5; let w1_8 := cst 6; let w8 := cst 7; let huge := cst 8; let w65 := cst 9
  let one1 := cst 10; let zero64 := cst 11; let maskF0 := cst 12; let sel7 := cst 13
  let one8 := cst 14; let one65 := cst 15
  -- Sum with adds and subs at several widths
  let _ ← nd (.Op_Sum 2) 65 [a8, b8, a65]
  let _ ← nd (.Op_Sum 1) 8 [a8, b8]
  let _ ← nd (.Op_Sum 3) 66 [a8, b8, a65]
  let _ ← nd (.Op_Sum 0) 9 [a8, b8]
  -- Mult, n-ary
  let _ ← nd .Op_Mult 16 [a8, b8]
  let _ ← nd .Op_Mult 130 [a65, b65]
  let _ ← nd .Op_Mult 8 [a8, b8, s3]
  -- UDiv, SDiv: inputs, divisor 0, MIN / -1, mixed widths
  let _ ← nd .Op_UDiv 8 [a8, b8]
  let _ ← nd .Op_UDiv 8 [a8, zero8]
  let _ ← nd .Op_UDiv 65 [a65, b8]
  let _ ← nd .Op_UDiv 4 [a8, b65]
  let _ ← nd .Op_UDiv 65 [a65, zero65]
  let _ ← nd .Op_SDiv 8 [a8, b8]
  let _ ← nd .Op_SDiv 8 [min8, neg8]
  let _ ← nd .Op_SDiv 9 [min8, neg8]
  let _ ← nd .Op_SDiv 65 [a65, b65]
  let _ ← nd .Op_SDiv 65 [min65, neg65]
  let _ ← nd .Op_SDiv 70 [a8, b65]
  let _ ← nd .Op_SDiv 8 [a8, zero8]
  -- And, Or, Xor (n-ary, mixed widths, empty), Ror, Not
  let _ ← nd .Op_And 8 [a8, b8, m8]
  let _ ← nd .Op_And 65 [a65, b8]
  let _ ← nd .Op_And 2 []
  let _ ← nd .Op_Or 70 [a8, a65]
  let _ ← nd .Op_Or 2 []
  let _ ← nd .Op_Xor 65 [a8, b8, a65]
  let _ ← nd .Op_Xor 3 []
  let _ ← nd .Op_Ror 1 [a8, b8]
  let _ ← nd .Op_Ror 1 []
  let _ ← nd .Op_Ror 1 [a65]
  let _ ← nd .Op_Not 8 [a8]
  let _ ← nd .Op_Not 12 [a8]
  let _ ← nd .Op_Not 65 [a65]
  -- comparisons (one bit): unsigned, signed at each operand's own width, n-ary EQ
  let _ ← nd .Op_ULT 1 [a8, b65]
  let _ ← nd .Op_UGT 1 [a65, b8]
  let _ ← nd .Op_ULT 1 [a8, b8]
  let _ ← nd .Op_UGT 1 [a8, b8]
  let _ ← nd .Op_SLT 1 [a8, b8]
  let _ ← nd .Op_SLT 1 [a8, a65]
  let _ ← nd .Op_SLT 1 [min8, one8]
  let _ ← nd .Op_SGT 1 [a65, b65]
  let _ ← nd .Op_SGT 1 [one65, min65]
  let _ ← nd .Op_EQ 1 [a8, b8]
  let _ ← nd .Op_EQ 1 [a8, b8, m8]
  let _ ← nd .Op_EQ 1 [neg8, neg65]
  -- shifts: input amounts and 0, w-1, w, 200
  let _ ← nd .Op_SHL 8 [a8, sh]
  let _ ← nd .Op_SHL 70 [a8, sh]
  let _ ← nd .Op_SHL 65 [a65, s3]
  let _ ← nd .Op_SHL 8 [a8, zero8]
  let _ ← nd .Op_SHL 8 [a8, w1_8]
  let _ ← nd .Op_SHL 8 [a8, w8]
  let _ ← nd .Op_SHL 65 [a65, huge]
  let _ ← nd .Op_SRA 8 [a8, sh]
  let _ ← nd .Op_SRA 16 [a8, sh]
  let _ ← nd .Op_SRA 65 [a65, sh]
  let _ ← nd .Op_SRA 4 [a65, s3]
  let _ ← nd .Op_SRA 8 [min8, zero8]
  let _ ← nd .Op_SRA 8 [min8, w1_8]
  let _ ← nd .Op_SRA 8 [min8, w8]
  let _ ← nd .Op_SRA 65 [min65, huge]
  let _ ← nd .Op_SRA 65 [a65, w65]
  -- muxes: a wide select, MuxN with a select out of range
  let _ ← nd .Op_MuxBool 8 [en, a8, b8]
  let _ ← nd .Op_MuxBool 65 [a8, a65, b8]
  let _ ← nd .Op_MuxN 8 [s3, a8, b8, m8]
  let _ ← nd .Op_MuxN 8 [sel7, a8, b8]
  let _ ← nd .Op_MuxN 8 [s3, a8]
  let _ ← nd .Op_MuxN 65 [en, a65, b8]
  -- sign extension from 0, from inside, from the width and from above it
  let _ ← nd .Op_Sext 16 [a8, s3]
  let _ ← nd .Op_Sext 16 [a8, sh]
  let _ ← nd .Op_Sext 16 [a8, zero8]
  let _ ← nd .Op_Sext 16 [a8, w8]
  let _ ← nd .Op_Sext 70 [a65, w65]
  let _ ← nd .Op_Sext 70 [a65, huge]
  -- GetMask / SetMask: input masks, a contiguous mask, an all-ones mask
  let _ ← nd .Op_GetMask 8 [a8, m8]
  let _ ← nd .Op_GetMask 8 [a65, m8]
  let _ ← nd .Op_GetMask 4 [a8, maskF0]
  let _ ← nd .Op_GetMask 65 [a65, neg65]
  let _ ← nd .Op_SetMask 8 [a8, m8, b8]
  let _ ← nd .Op_SetMask 65 [a65, m8, a8]
  let _ ← nd .Op_SetMask 8 [a8, maskF0, s3]
  let _ ← nd .Op_SetMask 70 [a65, neg65, b8]
  -- the memory: reads (an input enable, a constant one), a write, a byte-enable write, a
  -- whole-array load, whole-array reads, a read of the image after the writes
  let _ ← nd .Op_MemRead 16 [memSlot, inp "addr", en]
  let _ ← nd .Op_MemRead 16 [memSlot, inp "addr2", one1]
  let img1 ← nd .Op_MemWrite 16 [memSlot, inp "addr", inp "d16", en] (out := false)
  let img2 ← nd (.Op_MemWriteBE 8) 16 [img1, inp "addr2", inp "d16", inp "be"] (out := false)
  let img3 ← nd (.Op_MemLoad 4) 16 [img2, inp "bus", inp "en2"] (out := false)
  let _ ← nd (.Op_MemToBus 4) 64 [memSlot]
  let _ ← nd (.Op_MemToBus 4) 64 [img3]
  let _ ← nd .Op_MemRead 16 [img3, inp "addr", one1]
  -- the asynchronous whole-array reset: the image cleared while rst is high, after the writes
  -- on a step that commits and from the stored image on one that does not
  let next ← nd (.Op_MemLoad 4) 16 [img3, zero64, inp "rst"] (out := false)
  let held ← nd (.Op_MemLoad 4) 16 [memSlot, zero64, inp "rst"] (out := false)
  -- the flops' next values
  let accD ← nd (.Op_Sum 2) 8 [accQ, a8]
  let asyD ← nd .Op_Xor 8 [asyQ, b8]
  return { accDin := accD, asyDin := asyD, nextImg := next, asyncImg := held }

def corpus : DesignCert :=
  let (b, (ns, os)) := build.run (#[], #[])
  { sources
    nodes := ns
    outputs := os ++ #[{ slot := accQ, width := 8 }, { slot := asyQ, width := 8 }]
    flops := #[
      { width := 8, din := b.accDin, enable := some (inp "en"), resetPin := some (inp "rst"),
        resetValue := 3, resetActiveLow := false, clock := 0 },
      { width := 8, din := b.asyDin, enable := none, resetPin := some (inp "rst"),
        resetValue := 0x5A, resetActiveLow := false, clock := 1, asyncReset := true } ]
    memories := #[{ aw := 2, dw := 16, nextImg := b.nextImg, clock := 0, init := #[1, 2, 3, 0xFFFF],
                    asyncImg := some b.asyncImg }]
    clocks := #[{ name := "clk" }, { name := "clk2" }] }

/-- One operator outside the fragment, on two 8-bit inputs (none for Const), as the only output. -/
def refusal (op : LGraphOp) (deps : Array Nat) : DesignCert :=
  { sources := #[.input 0 8, .input 1 8]
    nodes := #[{ op, width := if op == .Op_LT || op == .Op_GT then 1 else 8, deps }]
    outputs := #[{ slot := 2, width := if op == .Op_LT || op == .Op_GT then 1 else 8 }]
    flops := #[], memories := #[] }

def refusals : List (String × DesignCert) :=
  [("refuse_sub", refusal .Op_Sub #[0, 1]), ("refuse_div", refusal .Op_Div #[0, 1]),
   ("refuse_lt", refusal .Op_LT #[0, 1]), ("refuse_gt", refusal .Op_GT #[0, 1]),
   ("refuse_const", refusal (.Op_Const 0x1FF) #[])]

def main (args : List String) : IO UInt32 := do
  let dir := System.FilePath.mk (args.headD ".")
  IO.FS.createDirAll dir
  let ops := corpus.nodes.foldl (fun (acc : List String) n =>
    let k := ((reprStr n.op).takeWhile (· != ' ')).toString
    if acc.contains k then acc else k :: acc) []
  IO.FS.writeBinFile (dir / "opcorpus.dcert") (CertIO.renderCert corpus)
  IO.println s!"wrote {dir / "opcorpus.dcert"}: {corpus.nodes.size} nodes, {corpus.outputs.size} outputs, {ops.length} operators"
  for (name, D) in refusals do
    IO.FS.writeBinFile (dir / s!"{name}.dcert") (CertIO.renderCert D)
  IO.println s!"wrote {refusals.length} certificates the routes must refuse"
  return 0

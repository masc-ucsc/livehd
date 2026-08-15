/-
  Certificate model for pass.lean output.
  Port of translation-correctness/Translation_LGraph_Model.thy.

  The generated DINO definitions still use native BitVec for execution and
  debugging.  The verified translation path uses this small certificate language:
  a graph is data, and one generic evaluator interprets that data using the
  mathematical LGraph denotation below.
-/

import LeanSemanticPrimitives.SemanticPrimitives

--------------------------------------------------------------------------------
-- LGraph operator variant (no per-node signedness — that's in the op selection)
--------------------------------------------------------------------------------

inductive LGraphOp where
  | Op_Const (c : Int)
  | Op_Sum   (n_add : Nat)
  | Op_Sub
  | Op_Mult
  | Op_Div
  | Op_UDiv
  | Op_SDiv
  | Op_And
  | Op_Or
  | Op_Xor
  | Op_Ror
  | Op_Not
  | Op_LT
  | Op_GT
  | Op_ULT
  | Op_UGT
  | Op_SLT
  | Op_SGT
  | Op_EQ
  | Op_SHL
  | Op_SRA
  | Op_MuxBool
  | Op_MuxN
  | Op_Sext
  | Op_GetMask
  | Op_SetMask
  -- Memory operators.  Deps mirror the fast model's operand order:
  --   Op_MemRead      [mem, addr, enable]            -> bv   (0 when not enabled)
  --   Op_MemWrite     [mem, addr, data, enable]      -> mem  (unchanged when not enabled)
  --   Op_MemWriteBE b [mem, addr, data, byte_enable] -> mem  (b = byte width)
  -- A multi-write memory becomes a CHAIN of write nodes, each taking the previous
  -- node's image as its `mem` dep, so the later (higher port_id) write wins a
  -- same-cycle same-address collision exactly as `memory_write_fold` does.
  | Op_MemRead
  | Op_MemWrite
  | Op_MemWriteBE (byte_w : Nat)
deriving Repr, Inhabited, DecidableEq

--------------------------------------------------------------------------------
-- Runtime-bitwidth bitvector for certificate evaluation.
-- (width, value) pair: value is interpreted modulo 2^width.
--------------------------------------------------------------------------------

structure BV where
  width : Nat
  value : Int
deriving Repr, Inhabited, DecidableEq

def bv_width (x : BV) : Nat := x.width

def bv_uint (x : BV) : Int :=
  x.value % (2 ^ x.width)

def bv_to_bitvec (w : Nat) (x : BV) : BitVec w :=
  BitVec.ofInt w (bv_uint x)

def mk_bv (w : Nat) (v : Int) : BV :=
  { width := w, value := v % (2 ^ w) }

def bv_resize (w : Nat) (x : BV) : BV :=
  mk_bv w (bv_uint x)

def bv_nonzero (x : BV) : Bool :=
  bv_uint x ≠ 0

def bv_bit (x : BV) (i : Nat) : Bool :=
  ((bv_uint x).toNat >>> i) % 2 = 1

def bits_to_int (w : Nat) (f : Nat → Bool) : Int :=
  ((List.range w).filterMap fun i =>
    if f i then some (2 ^ i : Int) else none).sum

def bv_bitwise (w : Nat) (f : Bool → Bool → Bool) (a b : BV) : BV :=
  mk_bv w (bits_to_int w fun i => f (bv_bit a i) (bv_bit b i))

def bv_not (w : Nat) (a : BV) : BV :=
  mk_bv w (bits_to_int w fun i => ¬ bv_bit a i)

def bv_sint (x : BV) : Int :=
  let w := x.width
  let u := bv_uint x
  if w = 0 then 0
  else if u < (2 ^ (w - 1) : Int) then u else u - (2 ^ w : Int)

def bv_sra (w : Nat) (x shamt : BV) : BV :=
  mk_bv w (bv_sint x / (2 ^ (bv_uint shamt).toNat : Int))

def bv_sdiv (w : Nat) (a b : BV) : BV :=
  mk_bv w
    (if bv_uint b = 0 then 0
     else trunc_div_int (bv_sint a) (bv_sint b))

def mask_indices_bv (m : BV) : List Nat :=
  (List.range m.width).filter fun i => bv_bit m i

def pack_low_bv (x : BV) (is : List Nat) : Int :=
  match is with
  | [] => 0
  | i :: is' =>
    let packed := pack_low_bv x is'
    if bv_bit x i then (2 : Int) ^ is'.length + packed
    else packed

def bv_get_mask (w : Nat) (x m : BV) : BV :=
  mk_bv w (pack_low_bv x (mask_indices_bv m).reverse)

def bv_set_bit (x : BV) (i : Nat) (b : Bool) : BV :=
  let w := x.width
  let u := bv_uint x
  mk_bv w
    (if b then u + (if bv_bit x i then 0 else (2 ^ i : Int))
     else u - (if bv_bit x i then (2 ^ i : Int) else 0))

def bv_set_mask (w : Nat) (a m v : BV) : BV :=
  let idxs := mask_indices_bv m
  let pairs := List.zip (List.range idxs.length) idxs
  bv_resize w <|
    pairs.foldl (fun acc p =>
      bv_set_bit acc p.2 (bv_bit v p.1))
    a

--------------------------------------------------------------------------------
-- denote_op : mathematical denotation
-- eval_op   : executable evaluator (identical body; proofs are structural)
--    These are separate only so generated lemmas can reference them by name.
--------------------------------------------------------------------------------

def denote_op : LGraphOp → Nat → List BV → BV
  | LGraphOp.Op_Const c, w, _       => mk_bv w c
  | LGraphOp.Op_Sum n_add, w, args =>
    let n := n_add
    let all := args.map bv_uint
    let adds := all.take n
    let subs := all.drop n
    mk_bv w (adds.sum - subs.sum)
  | LGraphOp.Op_Sub, w, [a, b]     => mk_bv w (bv_uint a - bv_uint b)
  | LGraphOp.Op_Mult, w, args       => mk_bv w (args.map bv_uint |>.prod)
  | LGraphOp.Op_Div, w, [a, b]     =>
    mk_bv w (if bv_uint b = 0 then 0 else bv_uint a / bv_uint b)
  | LGraphOp.Op_UDiv, w, [a, b]    =>
    mk_bv w (if bv_uint b = 0 then 0 else bv_uint a / bv_uint b)
  | LGraphOp.Op_SDiv, w, [a, b]    => bv_sdiv w a b
  | LGraphOp.Op_And, w, []         => mk_bv w 0
  | LGraphOp.Op_And, w, (a :: args) =>
    args.foldl (fun acc b => bv_bitwise w (fun x y => x && y) acc b) (bv_resize w a)
  | LGraphOp.Op_Or, w, args        =>
    args.foldl (fun acc b => bv_bitwise w (fun x y => x || y) acc b) (mk_bv w 0)
  | LGraphOp.Op_Xor, w, args       =>
    args.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc b) (mk_bv w 0)
  | LGraphOp.Op_Ror, w, xs         =>
    mk_bv w (if xs.any bv_nonzero then 1 else 0)
  | LGraphOp.Op_Not, w, [a]        => bv_not w a
  | LGraphOp.Op_LT, w, [a, b]      => mk_bv w (if bv_uint a < bv_uint b then 1 else 0)
  | LGraphOp.Op_GT, w, [a, b]      => mk_bv w (if bv_uint a > bv_uint b then 1 else 0)
  | LGraphOp.Op_ULT, w, [a, b]     => mk_bv w (if bv_uint a < bv_uint b then 1 else 0)
  | LGraphOp.Op_UGT, w, [a, b]     => mk_bv w (if bv_uint a > bv_uint b then 1 else 0)
  | LGraphOp.Op_SLT, w, [a, b]     => mk_bv w (if bv_sint a < bv_sint b then 1 else 0)
  | LGraphOp.Op_SGT, w, [a, b]     => mk_bv w (if bv_sint a > bv_sint b then 1 else 0)
  | LGraphOp.Op_EQ, w, []          => mk_bv w 1
  | LGraphOp.Op_EQ, w, (a :: args) =>
    mk_bv w (if args.all fun b => bv_uint b = bv_uint a then 1 else 0)
  | LGraphOp.Op_SHL, w, []         => mk_bv w 0
  | LGraphOp.Op_SHL, w, (a :: bs)  =>
    bs.foldl (fun acc b =>
      bv_bitwise w (fun x y => xor x y) acc (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat)))
      (mk_bv w 0)
  | LGraphOp.Op_SRA, w, [a, b]     => bv_sra w a b
  | LGraphOp.Op_MuxBool, w, [sel, false_v, true_v] =>
    if bv_nonzero sel then bv_resize w true_v else bv_resize w false_v
  | LGraphOp.Op_MuxN, w, []        => mk_bv w 0
  | LGraphOp.Op_MuxN, w, (sel :: args) =>
    let idx := (bv_uint sel).toNat
    if idx < args.length then bv_resize w ((args[idx]?).getD (mk_bv w 0)) else mk_bv w 0
  | LGraphOp.Op_Sext, w, [a, amount] =>
    let n := (bv_uint amount).toNat
    if n = 0 then mk_bv w 0
    else
      let u := bv_uint a % (2 ^ n : Int)
      if u < (2 ^ (n - 1) : Int) then mk_bv w u
      else mk_bv w (u - (2 ^ n : Int))
  | LGraphOp.Op_GetMask, w, [a, m] => bv_get_mask w a m
  | LGraphOp.Op_SetMask, w, [a, m, v] => bv_set_mask w a m v
  | _, w, _                        => mk_bv w 0

def eval_op : LGraphOp → Nat → List BV → BV
  | LGraphOp.Op_Const c, w, _       => mk_bv w c
  | LGraphOp.Op_Sum n_add, w, args =>
    let all := args.map bv_uint
    let adds := all.take n_add
    let subs := all.drop n_add
    mk_bv w (adds.sum - subs.sum)
  | LGraphOp.Op_Sub, w, [a, b]     => mk_bv w (bv_uint a - bv_uint b)
  | LGraphOp.Op_Mult, w, args       => mk_bv w (args.map bv_uint |>.prod)
  | LGraphOp.Op_Div, w, [a, b]     =>
    mk_bv w (if bv_uint b = 0 then 0 else bv_uint a / bv_uint b)
  | LGraphOp.Op_UDiv, w, [a, b]    =>
    mk_bv w (if bv_uint b = 0 then 0 else bv_uint a / bv_uint b)
  | LGraphOp.Op_SDiv, w, [a, b]    => bv_sdiv w a b
  | LGraphOp.Op_And, w, []         => mk_bv w 0
  | LGraphOp.Op_And, w, (a :: args) =>
    args.foldl (fun acc b => bv_bitwise w (fun x y => x && y) acc b) (bv_resize w a)
  | LGraphOp.Op_Or, w, args        =>
    args.foldl (fun acc b => bv_bitwise w (fun x y => x || y) acc b) (mk_bv w 0)
  | LGraphOp.Op_Xor, w, args       =>
    args.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc b) (mk_bv w 0)
  | LGraphOp.Op_Ror, w, xs         =>
    mk_bv w (if xs.any bv_nonzero then 1 else 0)
  | LGraphOp.Op_Not, w, [a]        => bv_not w a
  | LGraphOp.Op_LT, w, [a, b]      => mk_bv w (if bv_uint a < bv_uint b then 1 else 0)
  | LGraphOp.Op_GT, w, [a, b]      => mk_bv w (if bv_uint a > bv_uint b then 1 else 0)
  | LGraphOp.Op_ULT, w, [a, b]     => mk_bv w (if bv_uint a < bv_uint b then 1 else 0)
  | LGraphOp.Op_UGT, w, [a, b]     => mk_bv w (if bv_uint a > bv_uint b then 1 else 0)
  | LGraphOp.Op_SLT, w, [a, b]     => mk_bv w (if bv_sint a < bv_sint b then 1 else 0)
  | LGraphOp.Op_SGT, w, [a, b]     => mk_bv w (if bv_sint a > bv_sint b then 1 else 0)
  | LGraphOp.Op_EQ, w, []          => mk_bv w 1
  | LGraphOp.Op_EQ, w, (a :: args) =>
    mk_bv w (if args.all fun b => bv_uint b = bv_uint a then 1 else 0)
  | LGraphOp.Op_SHL, w, []         => mk_bv w 0
  | LGraphOp.Op_SHL, w, (a :: bs)  =>
    bs.foldl (fun acc b =>
      bv_bitwise w (fun x y => xor x y) acc (mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat)))
      (mk_bv w 0)
  | LGraphOp.Op_SRA, w, [a, b]     => bv_sra w a b
  | LGraphOp.Op_MuxBool, w, [sel, false_v, true_v] =>
    if bv_nonzero sel then bv_resize w true_v else bv_resize w false_v
  | LGraphOp.Op_MuxN, w, []        => mk_bv w 0
  | LGraphOp.Op_MuxN, w, (sel :: args) =>
    let idx := (bv_uint sel).toNat
    if idx < args.length then bv_resize w ((args[idx]?).getD (mk_bv w 0)) else mk_bv w 0
  | LGraphOp.Op_Sext, w, [a, amount] =>
    let n := (bv_uint amount).toNat
    if n = 0 then mk_bv w 0
    else
      let u := bv_uint a % (2 ^ n : Int)
      if u < (2 ^ (n - 1) : Int) then mk_bv w u
      else mk_bv w (u - (2 ^ n : Int))
  | LGraphOp.Op_GetMask, w, [a, m] => bv_get_mask w a m
  | LGraphOp.Op_SetMask, w, [a, m, v] => bv_set_mask w a m v
  | _, w, _                        => mk_bv w 0

--------------------------------------------------------------------------------
-- Certificate values: bit-vector OR memory image
--
-- A memory node is multi-output (N read-data values plus a next-state array), and
-- `NodeCert` carries exactly one width and one result.  Rather than widen
-- `NodeCert`, the emitter DECOMPOSES a memory into single-valued cert nodes: the
-- array is a source (like a flop), each read port is an `Op_MemRead` node, and the
-- write fold becomes a CHAIN of `Op_MemWrite` nodes, one per write port, in the
-- same order `memory_write_fold` applies them.  Only the value type has to grow.
--
-- `CertVal` deliberately derives NEITHER `DecidableEq` NOR `Repr` -- it carries a
-- function.  That is safe because values never appear in the data the
-- `native_decide` well-formedness facts run over: `NodeCert` is nid/op/width/deps
-- only, and values live solely in the environment `Nat → CertVal`, which is never
-- `decide`d.  `bridge_nodup` / `bridge_some` / `bridge_depord` / `keys_sub` are
-- therefore unaffected.
--------------------------------------------------------------------------------

inductive CertVal where
  | bv  (b : BV)
  | mem (f : Int → BV)
deriving Inhabited

/-- Bit-vector projection.  The `mem` case is a type error the emitter never
produces (an op is applied to the operand kind its node shape guarantees); it
returns a zero-width zero rather than needing a partial function. -/
def CertVal.asBV : CertVal → BV
  | .bv b  => b
  | .mem _ => mk_bv 0 0

/-- Memory projection, dual to `asBV`. -/
def CertVal.asMem : CertVal → (Int → BV)
  | .mem f => f
  | .bv _  => fun _ => mk_bv 0 0

--------------------------------------------------------------------------------
-- Memory operators over the certificate value domain.
--
-- These mirror `pass_lean.cpp`'s fast model exactly:
--   read   -> memory_read_port_expr  (enable-gated; 0 when not read-enabled)
--   write  -> one step of memory_write_fold (enable-gated, else unchanged)
-- The array is indexed by the address's UNSIGNED value, matching `mem_read`'s
-- `m a` on `BitVec addr`.
--------------------------------------------------------------------------------

def cert_mem_read (w : Nat) (m : Int → BV) (a : BV) (en : BV) : BV :=
  if bv_nonzero en then bv_resize w (m (bv_uint a)) else mk_bv w 0

def cert_mem_write (m : Int → BV) (a d en : BV) : Int → BV :=
  if bv_nonzero en then (fun x => if x = bv_uint a then d else m x) else m

/-- Byte-enable masked update, the `BV` twin of `masked_word_update`: bit `i` of the
result comes from `new` when byte `i / byte_w` is enabled, else from `old`. -/
def cert_masked_update (w : Nat) (old new bev : BV) (byte_w : Nat) : BV :=
  mk_bv w (bits_to_int w (fun i => if bv_bit bev (i / byte_w) then bv_bit new i else bv_bit old i))

def cert_mem_write_be (w : Nat) (m : Int → BV) (a d bev : BV) (byte_w : Nat) : Int → BV :=
  if bv_nonzero bev then
    (fun x => if x = bv_uint a then cert_masked_update w (m (bv_uint a)) d bev byte_w else m x)
  else m

--------------------------------------------------------------------------------
-- The certificate-value evaluator, LAYERED over `eval_op`.
--
-- Deliberately NOT a rewrite of `eval_op` into the `CertVal` domain.  `eval_op`
-- and `denote_op` have 25 operator cases each, `eval_op_correct` proves them
-- equal, and every `OpBridge` lemma (getmask_bridge', sum2_bridge, and3_bridge,
-- sra_bridge_sext, ...) is stated about `eval_op`.  Rewriting it would invalidate
-- all of that.  Instead the memory operators are handled here and EVERY other
-- operator is delegated unchanged, so the existing bridge lemmas keep applying
-- verbatim and only the graph-level machinery moves to `CertVal`.
--
-- For a concrete non-memory operator the match reduces on the operator
-- constructor, so `eval_op_cert Op_And w [.bv a, .bv b]` is DEFEQ to
-- `.bv (eval_op Op_And w [a, b])` -- see `eval_op_cert_bv` below.
--------------------------------------------------------------------------------

def eval_op_cert : LGraphOp → Nat → List CertVal → CertVal
  | LGraphOp.Op_MemRead, w, [m, a, en] => .bv (cert_mem_read w m.asMem a.asBV en.asBV)
  | LGraphOp.Op_MemWrite, _w, [m, a, d, en] => .mem (cert_mem_write m.asMem a.asBV d.asBV en.asBV)
  | LGraphOp.Op_MemWriteBE byte_w, w, [m, a, d, be] =>
    .mem (cert_mem_write_be w m.asMem a.asBV d.asBV be.asBV byte_w)
  -- Wrong arity on a memory op: a defined, obviously-wrong value rather than a
  -- partial function.  The emitter never produces it; the census gate catches it.
  | LGraphOp.Op_MemRead, w, _ => .bv (mk_bv w 0)
  | LGraphOp.Op_MemWrite, _w, _ => .mem (fun _ => mk_bv 0 0)
  | LGraphOp.Op_MemWriteBE _, _w, _ => .mem (fun _ => mk_bv 0 0)
  | op, w, vs => .bv (eval_op op w (vs.map CertVal.asBV))

/-- Every non-memory operator delegates to `eval_op` unchanged.  Stated with an
explicit hypothesis so it applies uniformly; for a concrete operator literal the
goal also closes by `rfl`, which is what the emitted per-node proofs rely on. -/
theorem eval_op_cert_bv (op : LGraphOp) (w : Nat) (l : List BV)
    (h : ∀ b, op ≠ LGraphOp.Op_MemRead ∧ op ≠ LGraphOp.Op_MemWrite ∧ op ≠ LGraphOp.Op_MemWriteBE b) :
    eval_op_cert op w (l.map CertVal.bv) = .bv (eval_op op w l) := by
  have hmap : (l.map CertVal.bv).map CertVal.asBV = l := by
    induction l with
    | nil => rfl
    | cons a t ih => simp [CertVal.asBV, ih]
  cases op <;> first
    | (exact absurd rfl (h 0).1)
    | (exact absurd rfl (h 0).2.1)
    | (exact absurd rfl (h _).2.2)
    | (simp only [eval_op_cert, hmap])

--------------------------------------------------------------------------------
-- Node certificate and graph certificate records
--------------------------------------------------------------------------------

structure NodeCert where
  nid   : Nat
  op    : LGraphOp
  width : Nat
  deps  : List Nat
deriving Repr, Inhabited

structure GraphCert where
  topo    : List Nat
  sources : List Nat
  nodes   : Nat → Option NodeCert

--------------------------------------------------------------------------------
-- Certificate helpers
--------------------------------------------------------------------------------

def nodes_of_list (cs : List NodeCert) (n : Nat) : Option NodeCert :=
  cs.find? fun c => c.nid = n

def depopts_of (G : GraphCert) (n : Nat) : List Nat :=
  match G.nodes n with
  | none => []
  | some c => c.deps

def node_width_of (G : GraphCert) (n : Nat) : Option Nat :=
  match G.nodes n with
  | none => none
  | some c => some c.width

def node_op_of (G : GraphCert) (n : Nat) : Option LGraphOp :=
  match G.nodes n with
  | none => none
  | some c => some c.op

--------------------------------------------------------------------------------
-- Certificate well-formedness and scalable chunk predicates.
--------------------------------------------------------------------------------

def graphCertWf (G : GraphCert) : Prop :=
  G.topo.Nodup ∧
  G.sources.Nodup ∧
  (∀ n, n ∈ G.topo → n ∉ G.sources) ∧
  (∀ n, n ∈ G.topo →
    match G.nodes n with
    | none => False
    | some c =>
        c.nid = n ∧
        c.width > 0 ∧
        ∀ d, d ∈ c.deps → d ∈ G.topo ∨ d ∈ G.sources) ∧
  (∀ n, n ∈ G.sources → G.nodes n = none)

def depsBefore : List Nat → List NodeCert → Bool
  | _seen, [] => true
  | seen, c :: cs =>
      c.deps.all (fun d => seen.contains d) &&
      depsBefore (seen ++ [c.nid]) cs

def graphCertWfBool (cs : List NodeCert) (srcs : List Nat) : Bool :=
  (cs.map NodeCert.nid).Nodup &&
  srcs.Nodup &&
  ((cs.map NodeCert.nid).all fun n => !(srcs.contains n)) &&
  (cs.all fun c =>
    c.width > 0 &&
    (c.deps.all fun d => (cs.map NodeCert.nid).contains d || srcs.contains d)) &&
  depsBefore srcs cs

def nodeCertChunkWfBool (allIds srcs : List Nat) (cs : List NodeCert) : Bool :=
  (cs.map NodeCert.nid).Nodup &&
  ((cs.map NodeCert.nid).all fun n => allIds.contains n) &&
  ((cs.map NodeCert.nid).all fun n => !(srcs.contains n)) &&
  (cs.all fun c =>
    c.width > 0 &&
    (c.deps.all fun d => allIds.contains d || srcs.contains d))

def constNodeCertWfBool (c : NodeCert) : Bool :=
  match c.op with
  | LGraphOp.Op_Const _ => c.width > 0 && c.deps.isEmpty
  | _ => false

def validDepsBool (validRef : Nat → Bool) (ds : List Nat) : Bool :=
  ds.all validRef

def nodeCertDeps (cs : List NodeCert) : List Nat :=
  (cs.map NodeCert.deps).flatten

def simpleOpCertWfBool (opc : LGraphOp) (w : Nat) (ds : List Nat) : Bool :=
  match opc with
  | LGraphOp.Op_Const _ => ds.isEmpty
  | LGraphOp.Op_Sum nAdd => ds.length > 0 && nAdd <= ds.length
  | LGraphOp.Op_And => ds.length > 0
  | LGraphOp.Op_Or => ds.length > 0
  | LGraphOp.Op_Xor => ds.length > 0
  | LGraphOp.Op_Ror => ds.length > 0 && w = 1
  | LGraphOp.Op_Not => ds.length = 1
  | LGraphOp.Op_EQ => ds.length = 2 && w = 1
  | LGraphOp.Op_ULT => ds.length = 2 && w = 1
  | LGraphOp.Op_UGT => ds.length = 2 && w = 1
  | LGraphOp.Op_SLT => ds.length = 2 && w = 1
  | LGraphOp.Op_SGT => ds.length = 2 && w = 1
  | LGraphOp.Op_GetMask => ds.length = 2
  | LGraphOp.Op_MuxBool => ds.length = 3
  | LGraphOp.Op_MuxN => ds.length > 1
  | LGraphOp.Op_SHL => ds.length = 2
  | LGraphOp.Op_SRA => ds.length = 2
  | LGraphOp.Op_Sext => ds.length = 2
  | _ => false

def simpleNodeCertShapeWfBool (c : NodeCert) : Bool :=
  c.width > 0 && simpleOpCertWfBool c.op c.width c.deps

def simpleNodeCertWfBool (validRef : Nat → Bool) (c : NodeCert) : Bool :=
  c.width > 0 &&
  validDepsBool validRef c.deps &&
  simpleOpCertWfBool c.op c.width c.deps

--------------------------------------------------------------------------------
-- Generic certificate evaluator and mathematical denotation.
--------------------------------------------------------------------------------

def denoteNode (G : GraphCert) (sourceEnv : Nat → BV) (n : Nat) : BV :=
  match G.nodes n with
  | none => sourceEnv n
  | some c => denote_op c.op c.width (c.deps.map sourceEnv)

def evalNode (G : GraphCert) (rho : Nat → BV) (n : Nat) : BV :=
  match G.nodes n with
  | none => rho n
  | some c => eval_op c.op c.width (c.deps.map rho)

def denoteNodeEnv (G : GraphCert) (rho : Nat → BV) (n : Nat) : BV :=
  match G.nodes n with
  | none => rho n
  | some c => denote_op c.op c.width (c.deps.map rho)

def envSet (rho : Nat → BV) (n : Nat) (v : BV) : Nat → BV :=
  fun m => if m = n then v else rho m

def evalGraph : List Nat → GraphCert → (Nat → BV) → Nat → BV
  | [], _G, rho => rho
  | n :: ns, G, rho => evalGraph ns G (envSet rho n (evalNode G rho n))

def denoteGraph : List Nat → GraphCert → (Nat → BV) → Nat → BV
  | [], _G, rho => rho
  | n :: ns, G, rho => denoteGraph ns G (envSet rho n (denoteNodeEnv G rho n))

def graphDenotation (order : List Nat) (G : GraphCert) (sourceEnv : Nat → BV) : Nat → BV :=
  denoteGraph order G sourceEnv

def envCorrectOn (ns : List Nat) (rho denote : Nat → BV) : Prop :=
  ∀ n, n ∈ ns → rho n = denote n

theorem eval_op_correct (oper : LGraphOp) (w : Nat) (args : List BV) :
    eval_op oper w args = denote_op oper w args := by
  cases oper <;> simp [eval_op, denote_op]

theorem evalNode_eq_denoteNodeEnv (G : GraphCert) (rho : Nat → BV) (n : Nat) :
    evalNode G rho n = denoteNodeEnv G rho n := by
  unfold evalNode denoteNodeEnv
  split <;> simp [eval_op_correct]

theorem evalGraph_eq_denoteGraph (order : List Nat) (G : GraphCert) (rho : Nat → BV) :
    evalGraph order G rho = denoteGraph order G rho := by
  induction order generalizing rho with
  | nil =>
      rfl
  | cons n ns ih =>
      simp [evalGraph, denoteGraph, evalNode_eq_denoteNodeEnv, ih]

theorem evalGraphCorrect (order : List Nat) (G : GraphCert) (sourceEnv : Nat → BV) :
    envCorrectOn order
      (evalGraph order G sourceEnv)
      (graphDenotation order G sourceEnv) := by
  intro n hn
  simp [graphDenotation, evalGraph_eq_denoteGraph]

theorem evalGraphCorrectForCert (G : GraphCert) (sourceEnv : Nat → BV) :
    envCorrectOn G.topo
      (evalGraph G.topo G sourceEnv)
      (graphDenotation G.topo G sourceEnv) := by
  exact evalGraphCorrect G.topo G sourceEnv

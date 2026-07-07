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

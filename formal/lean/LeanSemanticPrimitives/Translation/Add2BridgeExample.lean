import LeanSemanticPrimitives.Translation.OpBridge
open OpBridge

/- COMPLETE hand-proof of the add2 fast-view bridge (step 5), importing the real
   op-bridge library (no `sorry`).  This is the exact pattern the Piece-C emitter
   must generate: factored fast-node defs, φ, per-node have-chain discharged by
   op bridges, then GraphRefine.evalGraph_of_localAgree. -/

namespace Add2Full

structure add2_in where
  in_a : BitVec 4
  in_b : BitVec 4
deriving Repr, Inhabited

structure add2_out where
  out_y : BitVec 4
deriving Repr, Inhabited

-- factored fast node values (bridge-mode emission: one def per topo node)
def fv28 (i : add2_in) : BitVec 5 := (sem_get_mask i.in_b (BitVec.ofInt 1 (-1)) : BitVec 5)
def fv24 (i : add2_in) : BitVec 5 := (sem_get_mask i.in_a (BitVec.ofInt 1 (-1)) : BitVec 5)
def fv20 (i : add2_in) : BitVec 4 := ((bv_zext (fv24 i) : BitVec 4) + (bv_zext (fv28 i) : BitVec 4))
def fv16 (i : add2_in) : BitVec 4 :=
  ((bv_zext (BitVec.ofInt 4 15) : BitVec 4) &&& (bv_zext (fv20 i) : BitVec 4))

def add2_comb (i : add2_in) : add2_out := { out_y := (bv_zext (fv16 i) : BitVec 4) }

def add2_nodeCerts : List NodeCert := [
  { nid := 28, op := LGraphOp.Op_GetMask, width := 5, deps := [2000000001, 1000000000] },
  { nid := 24, op := LGraphOp.Op_GetMask, width := 5, deps := [2000000000, 1000000001] },
  { nid := 20, op := LGraphOp.Op_Sum 2, width := 4, deps := [24, 28] },
  { nid := 16, op := LGraphOp.Op_And, width := 4, deps := [1000000002, 20] }
]

def add2_sourceEnv (i : add2_in) : Nat -> BV := fun n =>
  if n = 1000000000 then mk_bv 1 ((-Int.ofNat 1)) else
  if n = 1000000001 then mk_bv 1 ((-Int.ofNat 1)) else
  if n = 1000000002 then mk_bv 4 ((Int.ofNat 15)) else
  if n = 2000000000 then mk_bv 4 (Int.ofNat (BitVec.toNat i.in_a)) else
  if n = 2000000001 then mk_bv 4 (Int.ofNat (BitVec.toNat i.in_b)) else
  mk_bv 0 0

def add2_graphCert : GraphCert :=
  { topo := [28, 24, 20, 16], sources := [1000000000, 1000000001, 1000000002, 2000000000, 2000000001],
    nodes := nodes_of_list add2_nodeCerts }

def add2_outputsFromCert (rho : Nat -> BV) : add2_out := { out_y := (bv_to_bitvec 4 (rho 16)) }

def add2_comb_cert (i : add2_in) : add2_out :=
  add2_outputsFromCert (evalGraph add2_graphCert.topo add2_graphCert (add2_sourceEnv i))

-- fast environment φ: encoded fast values on topo nodes, sourceEnv on leaves.
def phi (i : add2_in) : Nat → BV := fun n =>
  if n = 28 then bvenc (fv28 i) else
  if n = 24 then bvenc (fv24 i) else
  if n = 20 then bvenc (fv20 i) else
  if n = 16 then bvenc (fv16 i) else
  add2_sourceEnv i n

-- well-formedness / ordering of the concrete graph (native_decide)
theorem hnodup : add2_graphCert.topo.Nodup := by decide
theorem hsome : ∀ n ∈ add2_graphCert.topo, (add2_graphCert.nodes n).isSome := by decide
theorem hdepord : GraphRefine.DepOrdered add2_graphCert add2_graphCert.topo :=
  GraphRefine.depOrdered_of_bool add2_graphCert add2_graphCert.topo (by native_decide)

-- per-source: sourceEnv value is in bvenc form (inputs defeq; constants via mk_bv_ofInt).
theorem src_in_a  (i : add2_in) : add2_sourceEnv i 2000000000 = bvenc i.in_a := by simp [add2_sourceEnv, bvenc]
theorem src_in_b  (i : add2_in) : add2_sourceEnv i 2000000001 = bvenc i.in_b := by simp [add2_sourceEnv, bvenc]
theorem src_m0    (i : add2_in) : add2_sourceEnv i 1000000000 = bvenc (BitVec.ofInt 1 (-1)) := by
  simp only [add2_sourceEnv, if_pos]; exact mk_bv_ofInt _
theorem src_m1    (i : add2_in) : add2_sourceEnv i 1000000001 = bvenc (BitVec.ofInt 1 (-1)) := by
  simp only [add2_sourceEnv]; norm_num; exact mk_bv_ofInt _
theorem src_c15   (i : add2_in) : add2_sourceEnv i 1000000002 = bvenc (BitVec.ofInt 4 15) := by
  simp only [add2_sourceEnv]; norm_num; exact mk_bv_ofInt _

-- per-node recurrence, discharged by the op bridge for that node.
theorem hrec (i : add2_in) : ∀ n ∈ add2_graphCert.topo, phi i n = evalNode add2_graphCert (phi i) n := by
  have h28 : phi i 28 = evalNode add2_graphCert (phi i) 28 := by
    show bvenc (fv28 i) = eval_op LGraphOp.Op_GetMask 5 [phi i 2000000001, phi i 1000000000]
    rw [show phi i 2000000001 = bvenc i.in_b from by simp [phi]; exact src_in_b i,
        show phi i 1000000000 = bvenc (BitVec.ofInt 1 (-1)) from by simp [phi]; exact src_m0 i]
    show bvenc (fv28 i) = bv_get_mask 5 (bvenc i.in_b) (bvenc (BitVec.ofInt 1 (-1)))
    rw [getmask_bridge i.in_b (BitVec.ofInt 1 (-1)) (by native_decide)]; simp only [fv28]
  have h24 : phi i 24 = evalNode add2_graphCert (phi i) 24 := by
    show bvenc (fv24 i) = eval_op LGraphOp.Op_GetMask 5 [phi i 2000000000, phi i 1000000001]
    rw [show phi i 2000000000 = bvenc i.in_a from by simp [phi]; exact src_in_a i,
        show phi i 1000000001 = bvenc (BitVec.ofInt 1 (-1)) from by simp [phi]; exact src_m1 i]
    show bvenc (fv24 i) = bv_get_mask 5 (bvenc i.in_a) (bvenc (BitVec.ofInt 1 (-1)))
    rw [getmask_bridge i.in_a (BitVec.ofInt 1 (-1)) (by native_decide)]; simp only [fv24]
  have h20 : phi i 20 = evalNode add2_graphCert (phi i) 20 := by
    show bvenc (fv20 i) = eval_op (LGraphOp.Op_Sum 2) 4 [phi i 24, phi i 28]
    rw [show phi i 24 = bvenc (fv24 i) from by simp [phi],
        show phi i 28 = bvenc (fv28 i) from by simp [phi], sum2_bridge (fv24 i) (fv28 i)]
    simp only [fv20]
  have h16 : phi i 16 = evalNode add2_graphCert (phi i) 16 := by
    show bvenc (fv16 i) = eval_op LGraphOp.Op_And 4 [phi i 1000000002, phi i 20]
    rw [show phi i 1000000002 = bvenc (BitVec.ofInt 4 15) from by simp [phi]; exact src_c15 i,
        show phi i 20 = bvenc (fv20 i) from by simp [phi], and_bridge (BitVec.ofInt 4 15) (fv20 i)]
    simp only [fv16]
  intro n hn
  simp only [add2_graphCert, List.mem_cons, List.not_mem_nil, or_false] at hn
  rcases hn with h | h | h | h <;> subst h
  · exact h28
  · exact h24
  · exact h20
  · exact h16

-- source agreement: φ = sourceEnv off-topo, so this is definitional.
theorem hsrc (i : add2_in) : ∀ n ∈ add2_graphCert.topo, ∀ d ∈ depopts_of add2_graphCert n,
    d ∉ add2_graphCert.topo → add2_sourceEnv i d = phi i d := by
  intro n _ d _ hd
  have hd' : d ≠ 28 ∧ d ≠ 24 ∧ d ≠ 20 ∧ d ≠ 16 := by
    simp only [add2_graphCert, List.mem_cons, List.not_mem_nil, or_false, not_or] at hd
    exact ⟨hd.1, hd.2.1, hd.2.2.1, hd.2.2.2⟩
  simp only [phi, if_neg hd'.1, if_neg hd'.2.1, if_neg hd'.2.2.1, if_neg hd'.2.2.2]

-- step-5 theorem: fast model = certificate model.
theorem add2_comb_refines_fast (i : add2_in) : add2_comb i = add2_comb_cert i := by
  have hb := GraphRefine.evalGraph_of_localAgree add2_graphCert (phi i) (add2_sourceEnv i)
    hnodup hdepord hsome (hrec i) (hsrc i)
  have h16 : evalGraph add2_graphCert.topo add2_graphCert (add2_sourceEnv i) 16 = phi i 16 :=
    hb 16 (by decide)
  unfold add2_comb_cert add2_outputsFromCert
  rw [h16, show phi i 16 = bvenc (fv16 i) from by simp [phi], bv_to_bitvec_bvenc]
  unfold add2_comb
  rw [bv_zext_id]

end Add2Full

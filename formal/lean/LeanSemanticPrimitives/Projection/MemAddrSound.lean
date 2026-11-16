/-
# `MemAddrSound` — the load-time memory-address check is sound

`CertIO.certWfError` runs `MemAddr.addrError` when a certificate is loaded: for
every read and write of a declared memory it reads a bound of the address off
the certificate and requires it below the memory's `depth`. This module proves
that bound holds on the values `interpretDesign` computes. So on every step of
a certificate the loader admits -- from every state, under every input and
clock vector -- every such address is below its memory's depth
(`memAccess_lt_depth`, `loaded_memAccess_lt_depth`).

The step semantics keep a memory as a total `Int → BV` (`cert_mem_read`,
`cert_mem_write`), and nothing in them stops at `depth`. What makes that table
the hardware's array is this theorem: the words at or past `depth`, which the
hardware does not have, are never addressed, so they are never written and
never read.

`rhoOf` is `interpretDesign`'s own slot environment (`interpretDesign_outputs`,
`interpretDesign_mems`), so the theorem is about the reference semantics itself,
not about one of the routes that are proved equal to it.
-/
import LeanSemanticPrimitives.Projection.HardwareMemWidth
import LeanSemanticPrimitives.Compiler.MemAddr
import LeanSemanticPrimitives.Compiler.CertIOCodec
import LeanSemanticPrimitives.Translation.FastBV
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace MemAddrSound

open Compiler Compiler.MemAddr FastBV

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-- The slot values of one step: the `rho` of `interpretDesign`. -/
def rhoOf (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : Nat → CertVal :=
  evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i s)

/-- `interpretDesign` reads its outputs from `rhoOf`. -/
theorem interpretDesign_outputs (D : DesignCert) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) :
    (interpretDesign D e i s).outputs
      = D.outputs.map fun o => bv_resize o.width (rhoOf D i s o.slot).asBV := rfl

/-- …and steps its memories from `rhoOf`. -/
theorem interpretDesign_mems (D : DesignCert) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) :
    (interpretDesign D e i s).nextState.mems
      = D.memories.mapIdx fun idx m => srcMemNext (rhoOf D i s) e s idx m := rfl

/-! ## 1. Unsigned values, as `Nat` (`uintNat x` is `bv_uint x`) -/

theorem modNat_natCast (w n : Nat) : modNat w (n : Int) = n % 2 ^ w := by
  show modPow2 w n = n % 2 ^ w
  exact modPow2_eq w n

theorem uintNat_mk_bv (w : Nat) (v : Int) : uintNat (mk_bv w v) = modNat w v := by
  have h1 : bv_uint (mk_bv w v) = v % (2 ^ w : Int) := by
    show v % (2 ^ w : Int) % (2 ^ w : Int) = v % (2 ^ w : Int)
    exact Int.emod_emod_of_dvd _ (Int.dvd_refl _)
  rw [bv_uint_eq_uintNat, emod_two_pow_eq] at h1
  omega

theorem uintNat_mk_bv_nat (w n : Nat) : uintNat (mk_bv w (n : Int)) = n % 2 ^ w := by
  rw [uintNat_mk_bv, modNat_natCast]

theorem uintNat_mk_bv_zero (w : Nat) : uintNat (mk_bv w 0) = 0 := by
  simpa using uintNat_mk_bv_nat w 0

/-- A value no wider than `w` is at most `2 ^ w - 1`. -/
theorem uintNat_le_cap {x : BV} {w : Nat} (h : x.width ≤ w) : uintNat x ≤ 2 ^ w - 1 := by
  have h1 := uintNat_lt x
  have h2 : 2 ^ x.width ≤ 2 ^ w := Nat.pow_le_pow_right (by decide) h
  omega

theorem uintNat_resize_le (w : Nat) (x : BV) : uintNat (bv_resize w x) ≤ uintNat x := by
  show uintNat (mk_bv w (bv_uint x)) ≤ uintNat x
  rw [bv_uint_eq_uintNat, uintNat_mk_bv_nat]
  exact Nat.mod_le _ _

theorem bv_nonzero_iff (x : BV) : bv_nonzero x = true ↔ uintNat x ≠ 0 := by
  unfold bv_nonzero
  rw [bv_uint_eq_uintNat]
  simp

/-! ## 2. The operators the bound looks through -/

/-- The exporter's resize, `Op_Or [x]`: never larger than `x`. -/
theorem or1_le (w : Nat) (x : BV) : uintNat (eval_op .Op_Or w [x]) ≤ uintNat x := by
  show uintNat (bv_bitwise w (fun a b => a || b) (mk_bv w 0) x) ≤ uintNat x
  unfold bv_bitwise
  have hz : ∀ i, bv_bit (mk_bv w 0) i = false := by
    intro i
    rw [bv_bit_eq, uintNat_mk_bv_zero]
    simp
  have hb : bits_to_int w (fun i => bv_bit (mk_bv w 0) i || bv_bit x i)
      = ((uintNat x % 2 ^ w : Nat) : Int) := by
    apply bits_to_int_eq w _ _ (Nat.mod_lt _ (Nat.two_pow_pos w))
    intro i hi
    rw [hz, Bool.false_or, bv_bit_eq, Nat.testBit_mod_two_pow]
    simp [hi]
  rw [hb, uintNat_mk_bv_nat]
  exact Nat.le_trans (Nat.mod_le _ _) (Nat.mod_le _ _)

/-- `Op_MuxBool [s, f, t]` takes one arm, resized: no larger than that arm. -/
theorem muxBool_le (w : Nat) (s f t : BV) :
    uintNat (eval_op .Op_MuxBool w [s, f, t])
      ≤ (if bv_nonzero s then uintNat t else uintNat f) := by
  show uintNat (if bv_nonzero s then bv_resize w t else bv_resize w f) ≤ _
  split
  · exact uintNat_resize_le _ _
  · exact uintNat_resize_le _ _

/-- A nonzero `Op_ULT [a, b]` means `a < b`. -/
theorem ult_nonzero {w : Nat} {a b : BV} (h : bv_nonzero (eval_op .Op_ULT w [a, b]) = true) :
    uintNat a < uintNat b := by
  have h' : bv_nonzero (mk_bv w (if bv_uint a < bv_uint b then 1 else 0)) = true := h
  by_cases hab : bv_uint a < bv_uint b
  · rw [bv_uint_eq_uintNat, bv_uint_eq_uintNat] at hab
    omega
  · rw [if_neg hab, bv_nonzero_iff, uintNat_mk_bv_zero] at h'
    exact absurd rfl h'

/-- Every operator's value, as a bit vector, is no wider than its node. -/
theorem eval_op_cert_width_le (op : LGraphOp) (w : Nat) (vs : List CertVal) :
    (eval_op_cert op w vs).asBV.width ≤ w := by
  unfold eval_op_cert
  split <;> simp [CertVal.asBV, HwMem.cert_mem_read_width, HwMem.eval_op_width, cert_mem_to_bus,
    mk_bv]

theorem source_le (i : RuntimeInput) (s : RuntimeState) (sd : SourceDesc) :
    uintNat (sourceValue i s sd).asBV ≤ srcCap sd := by
  cases sd with
  | input idx w => exact uintNat_le_cap (Nat.le_of_eq (HwMem.sourceValue_width_input i s idx w))
  | const w v =>
    show uintNat (mk_bv w v) ≤ constVal w v
    rw [uintNat_mk_bv]
    unfold constVal
    rw [emod_two_pow_eq]
    simp
  | flopQ idx w => exact uintNat_le_cap (Nat.le_of_eq (HwMem.sourceValue_width_flopQ i s idx w))
  | flopQAsync idx w ri rv al =>
    exact uintNat_le_cap (Nat.le_of_eq (HwMem.sourceValue_width_flopQAsync i s idx w ri rv al))
  | memImg _ _ _ =>
    show uintNat (mk_bv 0 0) ≤ 0
    rw [uintNat_mk_bv_zero]
    exact Nat.le_refl 0
  | memConst _ _ _ =>
    show uintNat (mk_bv 0 0) ≤ 0
    rw [uintNat_mk_bv_zero]
    exact Nat.le_refl 0

/-! ## 3. What `interpretDesign` holds at a slot -/

section
variable {D : DesignCert} {i : RuntimeInput} {s : RuntimeState}

theorem rho_source {k : Nat} (hk : k < D.sources.size) :
    rhoOf D i s k = sourceValue i s (D.sources[k]'hk) := by
  have hnot : k ∉ D.toGraphCert.topo := DesignCert.source_not_mem_topo (by simpa using hk)
  unfold rhoOf
  rw [GraphRefine.evalGraphG_not_mem _ _ _ _ hnot]
  simp [srcEnv, Array.getElem?_eq_getElem hk]

theorem rho_beyond {k : Nat} (hk : D.sources.size + D.nodes.size ≤ k) :
    rhoOf D i s k = badVal := by
  have hnot : k ∉ D.toGraphCert.topo := by
    intro hm
    obtain ⟨_, h2⟩ := (DesignCert.mem_topo_iff (D := D)).mp hm
    simp only [DesignCert.numSlots] at h2
    omega
  unfold rhoOf
  rw [GraphRefine.evalGraphG_not_mem _ _ _ _ hnot]
  simp [srcEnv, Array.getElem?_eq_none (show D.sources.size ≤ k by omega)]

theorem rho_node (hwf : DesignCert.DesignCertWF D) {j : Nat} {c : DenseNodeCert}
    (hc : D.nodes[j]? = some c) :
    rhoOf D i s (D.sources.size + j)
      = eval_op_cert c.op c.width (c.deps.toList.map (rhoOf D i s)) := by
  have hj : j < D.nodes.size := by
    rcases Nat.lt_or_ge j D.nodes.size with h | h
    · exact h
    · simp [Array.getElem?_eq_none h] at hc
  have hfacts := DesignCert.wf_graph_facts hwf
  have hmem : D.slotOfNode j ∈ D.toGraphCert.topo :=
    DesignCert.slotOfNode_mem_topo (by simpa [DesignCert.numNodes] using hj)
  have hrec := Hw.evalGraphG_rec (V := CertVal) D.toGraphCert D.toGraphCert.topo
    (srcEnv D i s) hfacts.1 hfacts.2.1 hfacts.2.2 _ hmem
  have hnode : D.toGraphCert.nodes (D.slotOfNode j)
      = some ⟨D.slotOfNode j, c.op, c.width, c.deps.toList⟩ := by
    simp [DesignCert.toGraphCert, DesignCert.nodeAt?_slotOfNode, hc]
  show evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i s) (D.slotOfNode j) = _
  rw [hrec]
  simp only [evalNodeG, hnode]
  rfl

/-- A slot at or past the sources is a node or past the end. -/
theorem rho_nonsource (hwf : DesignCert.DesignCertWF D) {r : Nat} (hr : D.sources.size ≤ r) :
    (∃ c, D.nodes[r - D.sources.size]? = some c ∧
       rhoOf D i s r = eval_op_cert c.op c.width (c.deps.toList.map (rhoOf D i s)))
    ∨ (D.nodes[r - D.sources.size]? = none ∧ rhoOf D i s r = badVal) := by
  cases hc : D.nodes[r - D.sources.size]? with
  | some c =>
    left
    refine ⟨c, rfl, ?_⟩
    have := rho_node (i := i) (s := s) hwf hc
    rwa [show D.sources.size + (r - D.sources.size) = r by omega] at this
  | none =>
    right
    refine ⟨rfl, rho_beyond ?_⟩
    have : D.nodes.size ≤ r - D.sources.size := by
      rcases Nat.lt_or_ge (r - D.sources.size) D.nodes.size with h | h
      · simp [Array.getElem?_eq_getElem h] at hc
      · exact h
    omega

/-! ## 4. The bound is sound -/

theorem capAt_sound (hwf : DesignCert.DesignCertWF D) (r : Nat) :
    uintNat (rhoOf D i s r).asBV ≤ capAt D r := by
  unfold capAt
  by_cases hr : r < D.sources.size
  · rw [Array.getElem?_eq_getElem hr, rho_source hr]
    exact source_le i s _
  · rw [Array.getElem?_eq_none (Nat.le_of_not_lt hr)]
    rcases rho_nonsource (i := i) (s := s) hwf (Nat.le_of_not_lt hr) with ⟨c, hc, hv⟩ | ⟨hc, hv⟩
    · rw [hc, hv]
      exact uintNat_le_cap (eval_op_cert_width_le _ _ _)
    · rw [hc, hv]
      show uintNat (mk_bv 0 0) ≤ 0
      rw [uintNat_mk_bv_zero]
      exact Nat.le_refl 0

theorem constAt_val {k K : Nat} (h : constAt D k = some K) :
    uintNat (rhoOf D i s k).asBV = K := by
  unfold constAt at h
  cases hk : D.sources[k]? with
  | none => simp [hk] at h
  | some sd =>
    obtain ⟨hlt, hsd⟩ := Array.getElem?_eq_some_iff.mp hk
    rw [rho_source hlt, hsd]
    cases sd with
    | const w v =>
      simp only [hk, Option.some.injEq] at h
      show uintNat (mk_bv w v) = K
      rw [← h, uintNat_mk_bv]
      unfold constVal
      rw [emod_two_pow_eq]
      simp
    | _ => simp [hk] at h

/-- The select of a clamp: when it is nonzero, its compared slot is below `K`. -/
theorem ultGuard_sound (hwf : DesignCert.DesignCertWF D) {sel t K : Nat}
    (hg : ultGuard D sel t = some K) (hnz : bv_nonzero (rhoOf D i s sel).asBV = true) :
    uintNat (rhoOf D i s t).asBV < K := by
  unfold ultGuard at hg
  by_cases hs : sel < D.sources.size
  · simp [hs] at hg
  simp only [hs, ite_false] at hg
  cases hgn : D.nodes[sel - D.sources.size]? with
  | none => simp [hgn] at hg
  | some g =>
    simp only [hgn] at hg
    have hval := rho_node (i := i) (s := s) hwf hgn
    rw [show D.sources.size + (sel - D.sources.size) = sel by omega] at hval
    rw [hval] at hnz
    cases hop : g.op <;> simp only [hop] at hg <;> try (simp at hg; done)
    cases hd : g.deps.toList with
    | nil => simp [hd] at hg
    | cons t' rest =>
      cases rest with
      | nil => simp [hd] at hg
      | cons k rest2 =>
        cases rest2 with
        | cons _ _ => simp [hd] at hg
        | nil =>
          simp only [hd] at hg
          by_cases ht : t' = t
          · subst ht
            simp only [ite_true] at hg
            rw [hop, hd] at hnz
            have hlt : uintNat (rhoOf D i s t').asBV < uintNat (rhoOf D i s k).asBV :=
              ult_nonzero (w := g.width) hnz
            rw [constAt_val hg] at hlt
            exact hlt
          · simp [ht] at hg

theorem ub_sound (hwf : DesignCert.DesignCertWF D) :
    ∀ (fuel r : Nat), uintNat (rhoOf D i s r).asBV ≤ ub D fuel r := by
  intro fuel
  induction fuel with
  | zero => intro r; exact capAt_sound hwf r
  | succ f ih =>
    intro r
    unfold ub
    by_cases hr : r < D.sources.size
    · rw [Array.getElem?_eq_getElem hr, rho_source hr]
      exact source_le i s _
    rw [Array.getElem?_eq_none (Nat.le_of_not_lt hr)]
    rcases rho_nonsource (i := i) (s := s) hwf (Nat.le_of_not_lt hr) with ⟨c, hc, hv⟩ | ⟨hc, hv⟩
    · simp only [hc]
      have hcap : uintNat (rhoOf D i s r).asBV ≤ 2 ^ c.width - 1 := by
        rw [hv]; exact uintNat_le_cap (eval_op_cert_width_le _ _ _)
      split
      next x hop hdeps =>
        -- the exporter's resize
        have hx := ih x
        have h1 : uintNat (rhoOf D i s r).asBV ≤ uintNat (rhoOf D i s x).asBV := by
          rw [hv, hop, hdeps]
          exact or1_le c.width (rhoOf D i s x).asBV
        omega
      next sel fa ta hop hdeps =>
        -- the clamp, and any other two-arm mux
        have hm : uintNat (rhoOf D i s r).asBV
            ≤ (if bv_nonzero (rhoOf D i s sel).asBV
                then uintNat (rhoOf D i s ta).asBV else uintNat (rhoOf D i s fa).asBV) := by
          rw [hv, hop, hdeps]
          exact muxBool_le c.width (rhoOf D i s sel).asBV (rhoOf D i s fa).asBV
            (rhoOf D i s ta).asBV
        have hf := ih fa
        have ht := ih ta
        by_cases hnz : bv_nonzero (rhoOf D i s sel).asBV = true
        · rw [if_pos hnz] at hm
          cases hg : ultGuard D sel ta with
          | none =>
            show _ ≤ min _ (max _ (ub D f ta))
            omega
          | some K =>
            have hK := ultGuard_sound hwf hg hnz
            show _ ≤ min _ (max _ (min (ub D f ta) (K - 1)))
            omega
        · rw [if_neg hnz] at hm
          cases hg : ultGuard D sel ta with
          | none =>
            show _ ≤ min _ (max _ (ub D f ta))
            omega
          | some K =>
            show _ ≤ min _ (max _ (min (ub D f ta) (K - 1)))
            omega
      next => exact hcap
    · rw [hc, hv]
      show uintNat (mk_bv 0 0) ≤ 0
      rw [uintNat_mk_bv_zero]
      exact Nat.le_refl 0

/-! ## 5. The theorem -/

/-- **Every address stays inside its memory.** On every step of
`interpretDesign` -- any state, any input -- the address of a read or write of a
declared memory is below that memory's `depth`, whenever the certificate passes
the load-time check. -/
theorem memAccess_lt_depth (hwf : DesignCert.DesignCertWF D) (hchk : addrError D = none)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) (hop : isAddrOp c.op = true)
    {m a k : Nat} {md : MemoryDesc} (hm : c.deps[0]? = some m) (ha : c.deps[1]? = some a)
    (hr : rootOf D (D.nodes.size + 1) m = some (some k)) (hmd : D.memories[k]? = some md) :
    bv_uint (rhoOf D i s a).asBV < (md.depth : Int) := by
  have hlt := accessError_addr (addrError_none hchk hc) hop hm ha hr hmd
  have hle := ub_sound (i := i) (s := s) hwf ubFuel a
  rw [bv_uint_eq_uintNat]
  omega

/-- A whole-array load or bus read of a declared memory moves no more words than
the memory has. -/
theorem memWhole_le_depth (hchk : addrError D = none)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) {n : Nat}
    (hop : wholeWords c.op = some n)
    {m k : Nat} {md : MemoryDesc} (hm : c.deps[0]? = some m)
    (hr : rootOf D (D.nodes.size + 1) m = some (some k)) (hmd : D.memories[k]? = some md) :
    n ≤ md.depth :=
  accessError_whole (addrError_none hchk hc) hop hm hr hmd

end

/-- The loader's check includes the address check. -/
theorem addrError_of_certWfError {D : DesignCert} (h : CertIO.certWfError D = none) :
    addrError D = none := by
  unfold CertIO.certWfError at h
  cases hs : CertIO.certShapeError D with
  | some w => simp [hs] at h
  | none =>
    cases hd : depthError D with
    | some w => simp [hs, hd] at h
    | none => simpa [hs, hd] using h

theorem certWfError_of_readCertChecked {b : ByteArray} {D : DesignCert}
    (h : CertIO.readCertChecked b = .ok D) : CertIO.certWfError D = none := by
  unfold CertIO.readCertChecked at h
  cases hp : CertIO.parseCert b with
  | error e => simp [hp] at h
  | ok D' =>
    cases hw : CertIO.certWfError D' with
    | some w => simp [hp, hw] at h
    | none =>
      simp [hp, hw] at h
      subst h
      exact hw

/-- **For a certificate the loader admitted** (`readCertChecked`, which every
DCERT3/DCERT4 route loads through): the same, on every step. -/
theorem loaded_memAccess_lt_depth {b : ByteArray} {D : DesignCert}
    (hload : CertIO.readCertChecked b = .ok D) (hwf : DesignCert.DesignCertWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) (hop : isAddrOp c.op = true)
    {m a k : Nat} {md : MemoryDesc} (hm : c.deps[0]? = some m) (ha : c.deps[1]? = some a)
    (hr : rootOf D (D.nodes.size + 1) m = some (some k)) (hmd : D.memories[k]? = some md)
    (i : RuntimeInput) (s : RuntimeState) :
    bv_uint (rhoOf D i s a).asBV < (md.depth : Int) :=
  memAccess_lt_depth hwf (addrError_of_certWfError (certWfError_of_readCertChecked hload))
    hc hop hm ha hr hmd

#audit_axioms ub_sound
#audit_axioms memAccess_lt_depth
#audit_axioms memWhole_le_depth
#audit_axioms loaded_memAccess_lt_depth

end MemAddrSound
end Projection

/-
# Resuming from a checkpoint is exact (T4)

A checkpoint keeps each flop at its declared width and each memory word below the memory's
`depth`. This module proves that this is all a step can see: two states that agree there take
every step to the same outputs and to next states that agree again.

* `StateAgree D s t`: every flop reads alike at its declared width (`bv_resize`, as
  `sourceValue` reads it), and every memory word below `depth` has the same unsigned value.
* `step_agree` (T4): `StateAgree` states give equal outputs and `StateAgree` next states, on
  every input and clock vector, for a certificate the direct simulator accepts
  (`checkDesign`) whose memory addresses pass the load-time check (`MemAddr.addrError`) and
  whose memories' next images are versions of themselves (`Ckpt.imgRootError`).
* `trace_agree`: the same for every tick sequence: equal output lists, agreeing final states.

The induction runs over the slots in dense order. A bit-vector slot has equal values; a
memory-image slot that is a version of memory `k` (`RootIs`) has images whose words below
`depth k` have equal unsigned values. The three operators that read a stored word
(`Op_MemRead`, `Op_MemWriteBE`, `Op_MemToBus`) read it below `depth` (`memAccess_lt_depth`,
`memWhole_le_depth`), and only through `bv_uint`.

Mathlib-free; the axioms are audited at the end.
-/
import LeanSemanticPrimitives.Projection.MemAddrSound
import LeanSemanticPrimitives.Compiler.CkptSound
import LeanSemanticPrimitives.Compiler.DirectCheck
import LeanSemanticPrimitives.Compiler.DirectTrace

namespace Projection
namespace CkptResume

open Compiler Compiler.MemAddr MemAddrSound

/-! ## Versions of a memory -/

/-- A write, byte-enable write or whole-array load: the operators whose result is a new
version of the image at operand 0. -/
def isImgOp : LGraphOp → Bool
  | .Op_MemWrite | .Op_MemWriteBE _ | .Op_MemLoad _ => true
  | _ => false

/-- Slot `r` holds a version of memory `k`: its image source, or an image operator over a
version. -/
inductive RootIs (D : DesignCert) : Nat → Nat → Prop
  | src {r k aw dw : Nat} : D.sources[r]? = some (.memImg k aw dw) → RootIs D r k
  | node {j k m : Nat} {c : DenseNodeCert} : D.nodes[j]? = some c → isImgOp c.op = true →
      c.deps[0]? = some m → RootIs D m k → RootIs D (D.sources.size + j) k

/-- The fuel `rootOf` needs at slot `r`: one step for a source, two more than the node's index
for a node (its chain visits strictly smaller slots and ends at a source). -/
def need (D : DesignCert) (r : Nat) : Nat :=
  if r < D.sources.size then 1 else r - D.sources.size + 2

theorem rootIs_rootOf_fuel {D : DesignCert} (hdb : DesignCert.DepsBounded D) {r k : Nat}
    (h : RootIs D r k) : ∀ f, need D r ≤ f → rootOf D f r = some (some k) := by
  induction h with
  | @src r k aw dw hs =>
    intro f hf
    cases f with
    | zero => unfold need at hf; split at hf <;> omega
    | succ f => simp [rootOf, hs]
  | @node j k m c hc hop hm _ ih =>
    intro f hf
    have hmlt : m < D.sources.size + j :=
      hdb j c hc m (Array.mem_toList_iff.mpr (Array.mem_of_getElem? hm))
    have hf' : j + 2 ≤ f := by
      unfold need at hf; simp only [show ¬ D.sources.size + j < D.sources.size by omega, ↓reduceIte] at hf
      omega
    cases f with
    | zero => omega
    | succ f =>
      have hs : D.sources[D.sources.size + j]? = none := Array.getElem?_eq_none (by omega)
      have hrec := ih f (by unfold need; split <;> omega)
      simp only [rootOf, hs, show D.sources.size + j - D.sources.size = j by omega, hc, hm]
      cases hop' : c.op <;> simp_all [isImgOp]

/-- A version of memory `k` at a slot below the end has `rootOf` at the loader's fuel. -/
theorem rootIs_rootOf {D : DesignCert} (hdb : DesignCert.DepsBounded D) {r k : Nat}
    (h : RootIs D r k) (hr : r < D.sources.size + D.nodes.size) :
    rootOf D (D.nodes.size + 1) r = some (some k) :=
  rootIs_rootOf_fuel hdb h _ (by unfold need; split <;> omega)

/-- A slot is a version of at most one memory. -/
theorem rootIs_unique {D : DesignCert} (hdb : DesignCert.DepsBounded D) {r k k' : Nat}
    (h : RootIs D r k) (h' : RootIs D r k') : k = k' := by
  have e1 := rootIs_rootOf_fuel hdb h _ (Nat.le_refl _)
  have e2 := rootIs_rootOf_fuel hdb h' _ (Nat.le_refl _)
  rw [e1] at e2; cases e2; rfl

/-! ## The relations -/

/-- Two images agree below `n`: same unsigned word at every address `0 .. n-1`. -/
def MemAgree (n : Nat) (f g : Int → BV) : Prop :=
  ∀ a : Int, 0 ≤ a → a < n → bv_uint (f a) = bv_uint (g a)

/-- Slot `r`'s two values: equal, or images of a version of a memory that agree below its
depth. -/
def Rel (D : DesignCert) (r : Nat) (x y : CertVal) : Prop :=
  x = y ∨ ∃ k md f g, RootIs D r k ∧ D.memories[k]? = some md ∧ x = .mem f ∧ y = .mem g ∧
    MemAgree md.depth f g

theorem Rel.asBV {D : DesignCert} {r : Nat} {x y : CertVal} (h : Rel D r x y) :
    x.asBV = y.asBV := by
  rcases h with h | ⟨_, _, _, _, _, _, hx, hy, _⟩
  · rw [h]
  · rw [hx, hy]; rfl

/-- Two states agree where a step can see them. -/
structure StateAgree (D : DesignCert) (s t : RuntimeState) : Prop where
  flops : ∀ k (hk : k < D.flops.size),
    bv_resize D.flops[k].width (s.flops[k]?.getD (mk_bv D.flops[k].width 0)) =
      bv_resize D.flops[k].width (t.flops[k]?.getD (mk_bv D.flops[k].width 0))
  mems : ∀ k (hk : k < D.memories.size),
    MemAgree D.memories[k].depth (s.mems[k]?.getD fun _ => mk_bv 0 0)
      (t.mems[k]?.getD fun _ => mk_bv 0 0)

/-! ## Sources -/

section
variable {D : DesignCert} {i : RuntimeInput} {s t : RuntimeState}

theorem flopQ_ok {j idx w : Nat} (h : Direct.checkSource D j (.flopQ idx w) = none) :
    ∃ f, D.flops[idx]? = some f ∧ f.width = w := by
  simp only [Direct.checkSource] at h
  split at h
  · cases h
  · split at h
    · cases h
    · rename_i f hf
      split at h
      · rename_i hw; exact ⟨f, hf, by simpa using hw⟩
      · cases h

theorem flopQAsync_ok {j idx w ri : Nat} {rv : Int} {al : Bool}
    (h : Direct.checkSource D j (.flopQAsync idx w ri rv al) = none) :
    ∃ f, D.flops[idx]? = some f ∧ f.width = w := by
  simp only [Direct.checkSource] at h
  split at h
  · cases h
  · split at h
    · cases h
    · rename_i f hf
      by_cases hw : f.width = w
      · exact ⟨f, hf, hw⟩
      · simp [hw] at h

theorem memImg_ok {j idx aw dw : Nat} (h : Direct.checkSource D j (.memImg idx aw dw) = none) :
    ∃ md, D.memories[idx]? = some md := by
  simp only [Direct.checkSource] at h
  split at h
  · cases h
  · split at h
    · cases h
    · rename_i md hmd; exact ⟨md, hmd⟩

theorem flop_read_agree (hst : StateAgree D s t) {idx w : Nat} {f : FlopDesc}
    (hf : D.flops[idx]? = some f) (hw : f.width = w) :
    bv_resize w (s.flops[idx]?.getD (mk_bv w 0)) = bv_resize w (t.flops[idx]?.getD (mk_bv w 0)) := by
  have hidx : idx < D.flops.size := by
    rcases Nat.lt_or_ge idx D.flops.size with h | h
    · exact h
    · rw [Array.getElem?_eq_none h] at hf; cases hf
  have := hst.flops idx hidx
  have hfe : D.flops[idx] = f := by
    rw [Array.getElem?_eq_getElem hidx] at hf; cases hf; rfl
  rw [hfe, hw] at this
  exact this

theorem rel_source (hsem : Direct.DesignSemWF D) (hst : StateAgree D s t) {r : Nat}
    (hr : r < D.sources.size) : Rel D r (rhoOf D i s r) (rhoOf D i t r) := by
  rw [rho_source hr, rho_source hr]
  have hget : D.sources[r]? = some (D.sources[r]'hr) := Array.getElem?_eq_getElem hr
  have hok := hsem.sourceOK r _ hget
  generalize hsd : D.sources[r]'hr = sd at hok hget
  cases sd with
  | input idx w => exact Or.inl rfl
  | const w v => exact Or.inl rfl
  | flopQ idx w =>
    obtain ⟨f, hf, hw⟩ := flopQ_ok hok
    left
    simp only [sourceValue]
    rw [flop_read_agree hst hf hw]
  | flopQAsync idx w ri rv al =>
    obtain ⟨f, hf, hw⟩ := flopQAsync_ok hok
    left
    simp only [sourceValue]
    rw [flop_read_agree hst hf hw]
  | memImg idx aw dw =>
    obtain ⟨md, hmd⟩ := memImg_ok hok
    have hidx : idx < D.memories.size := by
      rcases Nat.lt_or_ge idx D.memories.size with h | h
      · exact h
      · rw [Array.getElem?_eq_none h] at hmd; cases hmd
    have hme : D.memories[idx] = md := by
      rw [Array.getElem?_eq_getElem hidx] at hmd; cases hmd; rfl
    right
    refine ⟨idx, md, _, _, RootIs.src hget, hmd, rfl, rfl, ?_⟩
    have := hst.mems idx hidx
    rw [hme] at this
    exact this
  | memConst aw dw ct => exact Or.inl rfl

end

/-! ## Operators -/

theorem resize_of_uint {w : Nat} {x y : BV} (h : bv_uint x = bv_uint y) :
    bv_resize w x = bv_resize w y := by
  simp only [bv_resize, h]

theorem pack_congr (d : Nat) {f g : Int → BV} :
    ∀ n, (∀ k : Nat, k < n → bv_uint (f k) = bv_uint (g k)) → cert_mem_pack d f n = cert_mem_pack d g n
  | 0, _ => rfl
  | n + 1, h => by
    simp only [cert_mem_pack]
    rw [pack_congr d n (fun k hk => h k (by omega)), h n (by omega)]

theorem masked_congr {w bw : Nat} {x y n be : BV} (h : bv_uint x = bv_uint y) :
    cert_masked_update w x n be bw = cert_masked_update w y n be bw := by
  unfold cert_masked_update bv_bit
  rw [h]

/-- The memory operators. -/
def isMemOp : LGraphOp → Bool
  | .Op_MemRead | .Op_MemWrite | .Op_MemWriteBE _ | .Op_MemLoad _ | .Op_MemToBus _ => true
  | _ => false

theorem eval_op_cert_nonmem {op : LGraphOp} (hop : isMemOp op = false) (w : Nat) (vs : List CertVal) :
    eval_op_cert op w vs = .bv (eval_op op w (vs.map CertVal.asBV)) := by
  cases op <;> first | rfl | simp [isMemOp] at hop

section
variable {D : DesignCert} {i : RuntimeInput} {s t : RuntimeState}

/-- The operand list of a node whose `opShape` fixes its arity at `n`. -/
theorem deps_size {j : Nat} {c : DenseNodeCert} (hsem : Direct.DesignSemWF D)
    (hc : D.nodes[j]? = some c) : Direct.opArity c.op c.deps.size = true := by
  have h := hsem.nodeShapes j c hc
  simp only [Direct.opShape, Bool.and_eq_true] at h
  exact h.1.2

theorem deps_get {c : DenseNodeCert} {l : List Nat} (hl : c.deps.toList = l) (p : Nat) :
    c.deps[p]? = l[p]? := by
  rw [← hl, Array.getElem?_toList]

theorem rel_node (hsem : Direct.DesignSemWF D) (haddr : addrError D = none) {j : Nat}
    {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (ih : ∀ d ∈ c.deps.toList, Rel D d (rhoOf D i s d) (rhoOf D i t d)) :
    Rel D (D.sources.size + j) (eval_op_cert c.op c.width (c.deps.toList.map (rhoOf D i s)))
      (eval_op_cert c.op c.width (c.deps.toList.map (rhoOf D i t))) := by
  have hwf : DesignCert.DesignCertWF D := ⟨hsem.depsBounded, hsem.slotsInRange⟩
  have hj : j < D.nodes.size := by
    rcases Nat.lt_or_ge j D.nodes.size with h | h
    · exact h
    · rw [Array.getElem?_eq_none h] at hc; cases hc
  have hdep : ∀ d ∈ c.deps.toList, d < D.sources.size + D.nodes.size := fun d hd => by
    have := hsem.depsBounded j c hc d hd; omega
  have har := deps_size hsem hc
  -- a version of memory `k` at the operand: its root at the loader's fuel
  have hroot : ∀ {m k}, m ∈ c.deps.toList → RootIs D m k → rootOf D (D.nodes.size + 1) m = some (some k) :=
    fun hm hr => rootIs_rootOf hsem.depsBounded hr (hdep _ hm)
  by_cases hmo : isMemOp c.op = false
  · left
    rw [eval_op_cert_nonmem hmo, eval_op_cert_nonmem hmo, List.map_map, List.map_map]
    congr 2
    exact List.map_congr_left fun d hd => (ih d hd).asBV
  · cases hop : c.op with
    | Op_MemRead =>
      rw [hop] at har
      have h3 : c.deps.toList.length = 3 := by simpa [Direct.opArity] using har
      obtain ⟨m, a, en, hl⟩ : ∃ m a en, c.deps.toList = [m, a, en] := by
        match hlist : c.deps.toList, h3 with
        | [m, a, en], _ => exact ⟨m, a, en, rfl⟩
      have ihm := ih m (by simp [hl]); have iha := (ih a (by simp [hl])).asBV
      have ihe := (ih en (by simp [hl])).asBV
      simp only [hl, List.map]
      show Rel D _ (.bv (cert_mem_read c.width (rhoOf D i s m).asMem (rhoOf D i s a).asBV (rhoOf D i s en).asBV))
        (.bv (cert_mem_read c.width (rhoOf D i t m).asMem (rhoOf D i t a).asBV (rhoOf D i t en).asBV))
      rw [iha, ihe]
      rcases ihm with he | ⟨k, md, f, g, hr, hmd, hx, hy, hag⟩
      · left; rw [he]
      · left
        rw [hx, hy]
        simp only [CertVal.asMem, cert_mem_read]
        split
        · congr 1
          apply resize_of_uint
          have hlt := memAccess_lt_depth (i := i) (s := t) hwf haddr hc (by rw [hop]; rfl)
            (by rw [deps_get hl]; rfl) (by rw [deps_get hl]; rfl) (hroot (by simp [hl]) hr) hmd
          exact hag _ (Ckpt.bv_uint_nonneg _) hlt
        · rfl
    | Op_MemWrite =>
      rw [hop] at har
      have h4 : c.deps.toList.length = 4 := by simpa [Direct.opArity] using har
      obtain ⟨m, a, d, en, hl⟩ : ∃ m a d en, c.deps.toList = [m, a, d, en] := by
        match hlist : c.deps.toList, h4 with
        | [m, a, d, en], _ => exact ⟨m, a, d, en, rfl⟩
      have ihm := ih m (by simp [hl]); have iha := (ih a (by simp [hl])).asBV
      have ihd := (ih d (by simp [hl])).asBV; have ihe := (ih en (by simp [hl])).asBV
      simp only [hl, List.map]
      show Rel D _ (.mem (cert_mem_write (rhoOf D i s m).asMem (rhoOf D i s a).asBV (rhoOf D i s d).asBV (rhoOf D i s en).asBV))
        (.mem (cert_mem_write (rhoOf D i t m).asMem (rhoOf D i t a).asBV (rhoOf D i t d).asBV (rhoOf D i t en).asBV))
      rw [iha, ihd, ihe]
      rcases ihm with he | ⟨k, md, f, g, hr, hmd, hx, hy, hag⟩
      · left; rw [he]
      · right
        refine ⟨k, md, _, _, RootIs.node hc (by rw [hop]; rfl) (by rw [deps_get hl]; rfl) hr, hmd, rfl, rfl, ?_⟩
        rw [hx, hy]
        intro x h0 hlt
        simp only [CertVal.asMem, cert_mem_write]
        split
        · dsimp only
          split
          · rfl
          · exact hag x h0 hlt
        · exact hag x h0 hlt
    | Op_MemWriteBE bw =>
      rw [hop] at har
      have h4 : c.deps.toList.length = 4 := by simp [Direct.opArity] at har; simpa using har.1
      obtain ⟨m, a, d, be, hl⟩ : ∃ m a d be, c.deps.toList = [m, a, d, be] := by
        match hlist : c.deps.toList, h4 with
        | [m, a, d, be], _ => exact ⟨m, a, d, be, rfl⟩
      have ihm := ih m (by simp [hl]); have iha := (ih a (by simp [hl])).asBV
      have ihd := (ih d (by simp [hl])).asBV; have ihb := (ih be (by simp [hl])).asBV
      simp only [hl, List.map]
      show Rel D _ (.mem (cert_mem_write_be c.width (rhoOf D i s m).asMem (rhoOf D i s a).asBV (rhoOf D i s d).asBV (rhoOf D i s be).asBV bw))
        (.mem (cert_mem_write_be c.width (rhoOf D i t m).asMem (rhoOf D i t a).asBV (rhoOf D i t d).asBV (rhoOf D i t be).asBV bw))
      rw [iha, ihd, ihb]
      rcases ihm with he | ⟨k, md, f, g, hr, hmd, hx, hy, hag⟩
      · left; rw [he]
      · right
        refine ⟨k, md, _, _, RootIs.node hc (by rw [hop]; rfl) (by rw [deps_get hl]; rfl) hr, hmd, rfl, rfl, ?_⟩
        rw [hx, hy]
        intro x h0 hlt
        simp only [CertVal.asMem, cert_mem_write_be]
        split
        · dsimp only
          split
          · rename_i hxa
            have hfg : bv_uint (f (bv_uint (rhoOf D i t a).asBV)) = bv_uint (g (bv_uint (rhoOf D i t a).asBV)) := by
              rw [← hxa]; exact hag x h0 hlt
            rw [masked_congr (w := c.width) (bw := bw) (n := (rhoOf D i t d).asBV)
              (be := (rhoOf D i t be).asBV) hfg]
          · exact hag x h0 hlt
        · exact hag x h0 hlt
    | Op_MemLoad n =>
      rw [hop] at har
      have h3 : c.deps.toList.length = 3 := by simp [Direct.opArity] at har; simpa using har.1
      obtain ⟨m, bus, en, hl⟩ : ∃ m bus en, c.deps.toList = [m, bus, en] := by
        match hlist : c.deps.toList, h3 with
        | [m, bus, en], _ => exact ⟨m, bus, en, rfl⟩
      have ihm := ih m (by simp [hl]); have ihb := (ih bus (by simp [hl])).asBV
      have ihe := (ih en (by simp [hl])).asBV
      simp only [hl, List.map]
      show Rel D _ (.mem (cert_mem_load c.width n (rhoOf D i s m).asMem (rhoOf D i s bus).asBV (rhoOf D i s en).asBV))
        (.mem (cert_mem_load c.width n (rhoOf D i t m).asMem (rhoOf D i t bus).asBV (rhoOf D i t en).asBV))
      rw [ihb, ihe]
      rcases ihm with he | ⟨k, md, f, g, hr, hmd, hx, hy, hag⟩
      · left; rw [he]
      · right
        refine ⟨k, md, _, _, RootIs.node hc (by rw [hop]; rfl) (by rw [deps_get hl]; rfl) hr, hmd, rfl, rfl, ?_⟩
        rw [hx, hy]
        intro x h0 hlt
        simp only [CertVal.asMem, cert_mem_load]
        split
        · dsimp only
          split
          · rfl
          · exact hag x h0 hlt
        · exact hag x h0 hlt
    | Op_MemToBus n =>
      rw [hop] at har
      have h1 : c.deps.toList.length = 1 := by simp [Direct.opArity] at har; simpa using har.1
      obtain ⟨m, hl⟩ := List.length_eq_one_iff.mp h1
      have ihm := ih m (by simp [hl])
      simp only [hl, List.map]
      show Rel D _ (.bv (cert_mem_to_bus c.width n (rhoOf D i s m).asMem))
        (.bv (cert_mem_to_bus c.width n (rhoOf D i t m).asMem))
      rcases ihm with he | ⟨k, md, f, g, hr, hmd, hx, hy, hag⟩
      · left; rw [he]
      · left
        rw [hx, hy]
        have hn := memWhole_le_depth haddr hc (show wholeWords c.op = some n by rw [hop]; rfl)
          (by rw [deps_get hl]; rfl) (hroot (by simp [hl]) hr) hmd
        simp only [CertVal.asMem, cert_mem_to_bus]
        rw [pack_congr _ n (fun k hk => hag k (Int.natCast_nonneg k) (by omega))]
    | _ => simp [isMemOp, hop] at hmo

end

/-! ## Every slot, then a step -/

section
variable {D : DesignCert} {i : RuntimeInput} {s t : RuntimeState}

theorem rel_slot (hsem : Direct.DesignSemWF D) (haddr : addrError D = none)
    (hst : StateAgree D s t) : ∀ r, Rel D r (rhoOf D i s r) (rhoOf D i t r) := by
  have hwf : DesignCert.DesignCertWF D := ⟨hsem.depsBounded, hsem.slotsInRange⟩
  intro r
  induction r using Nat.strongRecOn with
  | ind r ih =>
    by_cases hr : r < D.sources.size
    · exact rel_source hsem hst hr
    · obtain ⟨j, rfl⟩ : ∃ j, r = D.sources.size + j := ⟨r - D.sources.size, by omega⟩
      cases hc : D.nodes[j]? with
      | none =>
        have hb : D.sources.size + D.nodes.size ≤ D.sources.size + j := by
          rcases Nat.lt_or_ge j D.nodes.size with h | h
          · rw [Array.getElem?_eq_getElem h] at hc; cases hc
          · omega
        left
        rw [rho_beyond (i := i) (s := s) hb, rho_beyond (i := i) (s := t) hb]
      | some c =>
        rw [rho_node (i := i) (s := s) hwf hc, rho_node (i := i) (s := t) hwf hc]
        exact rel_node hsem haddr hc fun d hd => ih d (hsem.depsBounded j c hc d hd)

/-- A memory's next image is a version of itself, when `imgRootError` holds. -/
theorem rootIs_of_rootOf {D : DesignCert} (hdb : DesignCert.DepsBounded D) :
    ∀ (f r k : Nat), rootOf D f r = some (some k) → RootIs D r k
  | 0, _, _, h => by simp [rootOf] at h
  | f + 1, r, k, h => by
    simp only [rootOf] at h
    split at h
    · rename_i k' aw dw hs; cases h; exact RootIs.src hs
    · cases h
    · cases h
    · split at h
      · rename_i c hc
        obtain ⟨j, hj⟩ : ∃ j, r = D.sources.size + j := ⟨r - D.sources.size, by
          have : D.sources[r]? = none := by assumption
          rcases Nat.lt_or_ge r D.sources.size with h' | h'
          · rw [Array.getElem?_eq_getElem h'] at this; cases this
          · omega⟩
        have hc' : D.nodes[j]? = some c := by rw [hj] at hc; simpa using hc
        have key : ∀ m, c.deps[0]? = some m → isImgOp c.op = true → rootOf D f m = some (some k) →
            RootIs D r k := fun m hm hi hr => by
          rw [hj]; exact RootIs.node hc' hi hm (rootIs_of_rootOf hdb f m k hr)
        revert h
        cases hop : c.op <;> cases hm : c.deps[0]? <;> intro h <;> simp only at h <;>
          first | cases h | exact key _ hm (by simp [isImgOp, hop]) h
      · cases h

end

/-! ## A step -/

section
variable {D : DesignCert} {i : RuntimeInput} {s t : RuntimeState}

theorem MemAgree.refl (n : Nat) (f : Int → BV) : MemAgree n f f := fun _ _ _ => rfl

/-- What `imgRootError` gives: every next image of memory `k` is rooted at `k`. -/
theorem imgRoot_ok (h : Ckpt.imgRootError D = none) {k : Nat} (hk : k < D.memories.size) :
    ∀ sl ∈ [D.memories[k]!.nextImg] ++ D.memories[k]!.commits.toList.map (·.nextImg) ++
      D.memories[k]!.asyncImg.toList, rootOf D (D.nodes.size + 1) sl = some (some k) := by
  intro sl hsl
  unfold Ckpt.imgRootError at h
  have h1 := List.findSome?_eq_none_iff.mp h k (List.mem_range.mpr hk)
  have h2 := List.findSome?_eq_none_iff.mp h1 sl hsl
  split at h2
  · rename_i he; exact (beq_iff_eq.mp he)
  · cases h2

/-- An image slot that is a version of memory `k` agrees below `k`'s depth. -/
theorem img_agree (hsem : Direct.DesignSemWF D) (haddr : addrError D = none)
    (hst : StateAgree D s t) {sl k : Nat} {md : MemoryDesc} (hmd : D.memories[k]? = some md)
    (hr : rootOf D (D.nodes.size + 1) sl = some (some k)) :
    MemAgree md.depth (rhoOf D i s sl).asMem (rhoOf D i t sl).asMem := by
  have hri := rootIs_of_rootOf hsem.depsBounded _ _ _ hr
  rcases rel_slot (i := i) hsem haddr hst sl with he | ⟨k', md', f, g, hr', hmd', hx, hy, hag⟩
  · rw [he]; exact MemAgree.refl _ _
  · have := rootIs_unique hsem.depsBounded hri hr'
    subst this
    rw [hmd] at hmd'; cases hmd'
    rw [hx, hy]; exact hag

/-- A flop's next value differs between the two states only through what it holds. -/
theorem ite_hold (c1 c2 : Bool) (A B x y : BV) (w : Nat) (h : bv_resize w x = bv_resize w y) :
    bv_resize w (if c1 = true then A else if c2 = true then B else x) =
      bv_resize w (if c1 = true then A else if c2 = true then B else y) := by
  cases c1 <;> cases c2 <;> simp [h]

theorem interpretDesign_flops (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    (interpretDesign D e i s).nextState.flops
      = D.flops.mapIdx fun idx f => srcFlopNext (rhoOf D i s) e s idx f := rfl

/-- **T4.** States that agree where a step can see them give equal outputs and next states
that agree again. -/
theorem step_agree (hsem : Direct.DesignSemWF D) (haddr : addrError D = none)
    (hroot : Ckpt.imgRootError D = none) (hst : StateAgree D s t) (e : ClockEdges) :
    (interpretDesign D e i s).outputs = (interpretDesign D e i t).outputs ∧
    StateAgree D (interpretDesign D e i s).nextState (interpretDesign D e i t).nextState := by
  have hbv : ∀ r, (rhoOf D i s r).asBV = (rhoOf D i t r).asBV :=
    fun r => (rel_slot (i := i) hsem haddr hst r).asBV
  refine ⟨?_, ⟨?_, ?_⟩⟩
  · rw [interpretDesign_outputs, interpretDesign_outputs]
    apply Array.map_congr_left
    intro o _
    rw [hbv]
  · intro k hk
    rw [interpretDesign_flops, interpretDesign_flops]
    simp only [Array.getElem?_mapIdx, Array.getElem?_eq_getElem hk, Option.map_some, Option.getD_some]
    unfold srcFlopNext
    simp only [hbv]
    exact ite_hold _ _ _ _ _ _ _ (hst.flops k hk)
  · intro k hk
    rw [interpretDesign_mems, interpretDesign_mems]
    simp only [Array.getElem?_mapIdx, Array.getElem?_eq_getElem hk, Option.map_some, Option.getD_some]
    have hmd : D.memories[k]? = some D.memories[k] := Array.getElem?_eq_getElem hk
    have hslots := imgRoot_ok hroot hk
    have hm! : D.memories[k]! = D.memories[k] := getElem!_pos D.memories k hk
    rw [hm!] at hslots
    have hag (sl : Nat) (hsl : sl ∈ [D.memories[k].nextImg] ++ D.memories[k].commits.toList.map (·.nextImg) ++
        D.memories[k].asyncImg.toList) :
        MemAgree D.memories[k].depth (rhoOf D i s sl).asMem (rhoOf D i t sl).asMem :=
      img_agree hsem haddr hst hmd (hslots sl hsl)
    have hheld : MemAgree D.memories[k].depth (srcMemHeld (rhoOf D i s) s k D.memories[k])
        (srcMemHeld (rhoOf D i t) t k D.memories[k]) := by
      unfold srcMemHeld
      split
      · rename_i a ha; exact hag a (by simp [ha])
      · exact hst.mems k hk
    unfold srcMemNext
    split
    · split
      · exact hag _ (by simp)
      · exact hheld
    · split
      · exact hag _ (by simp)
      · split
        · rename_i cm hcm
          have hmem : cm ∈ D.memories[k].commits := Array.mem_of_find?_eq_some hcm
          exact hag _ (List.mem_append_left _ (List.mem_append_right _
            (List.mem_map.mpr ⟨cm, Array.mem_toList_iff.mpr hmem, rfl⟩)))
        · exact hheld

/-- `StateAgree` for every tick sequence: equal output lists, agreeing final states. -/
theorem trace_agree (hsem : Direct.DesignSemWF D) (haddr : addrError D = none)
    (hroot : Ckpt.imgRootError D = none) :
    ∀ (ts : List Direct.Tick) (s t : RuntimeState), StateAgree D s t →
      (Direct.refTrace D s ts).steps.map (·.outputs) = (Direct.refTrace D t ts).steps.map (·.outputs) ∧
      StateAgree D (Direct.refTrace D s ts).finalState (Direct.refTrace D t ts).finalState
  | [], _, _, h => ⟨rfl, h⟩
  | tk :: ts, s, t, h => by
    obtain ⟨ho, hn⟩ := step_agree (i := tk.input) hsem haddr hroot h tk.edges
    obtain ⟨ho', hn'⟩ := trace_agree hsem haddr hroot ts _ _ hn
    simp only [Direct.refTrace, List.map]
    exact ⟨by rw [ho, ho'], hn'⟩

end

/-! ## Checkpoints resume exactly -/

section
variable {D : DesignCert}

theorem StateAgree.symm {s t : RuntimeState} (h : StateAgree D s t) : StateAgree D t s :=
  ⟨fun k hk => (h.flops k hk).symm, fun k hk a h0 ha => (h.mems k hk a h0 ha).symm⟩

theorem StateAgree.trans {s t u : RuntimeState} (h1 : StateAgree D s t) (h2 : StateAgree D t u) :
    StateAgree D s u :=
  ⟨fun k hk => (h1.flops k hk).trans (h2.flops k hk),
   fun k hk a h0 ha => (h1.mems k hk a h0 ha).trans (h2.mems k hk a h0 ha)⟩

/-- Every memory word below `depth` is at most the memory's `dw` bits wide: then its
unsigned value fits, and a checkpoint, which keeps `dw` bits, keeps all of it. -/
def WordsFit (D : DesignCert) (s : RuntimeState) : Prop :=
  ∀ k (hk : k < D.memories.size) (a : Int), 0 ≤ a → a < D.memories[k].depth →
    ((s.mems[k]?.getD fun _ => mk_bv 0 0) a).width ≤ D.memories[k].dw

theorem bv_uint_lt (x : BV) : bv_uint x < 2 ^ x.width :=
  Int.emod_lt_of_pos _ (Ckpt.two_pow_pos _)

/-- A restored save agrees with the saved state, when its words fit their memories. -/
theorem restore_save_agree {s : RuntimeState} (hash cycle : Nat) (exts : Array Ckpt.Ext)
    (hsz : s.mems.size = D.memories.size) (hfit : WordsFit D s) :
    StateAgree D (Ckpt.restore (Ckpt.save D hash cycle exts s)) s := by
  refine ⟨fun k hk => Ckpt.restore_save_flop D hash cycle exts s hk _ rfl, ?_⟩
  intro k hk a h0 ha
  have hdep : a.toNat < D.memories[k].depth := by omega
  rw [Ckpt.restore_save_mem D hash cycle exts s hk a h0 hdep]
  have hk' : k < s.mems.size := by omega
  have hdf : (s.mems[k]?.getD fun _ => mk_bv D.memories[k].dw 0) = (s.mems[k]?.getD fun _ => mk_bv 0 0) := by
    simp [Array.getElem?_eq_getElem hk']
  rw [hdf]
  have hw := hfit k hk a h0 ha
  generalize (s.mems[k]?.getD fun _ => mk_bv 0 0) a = x at hw ⊢
  simp only [bv_resize, Ckpt.bv_uint_mk_bv]
  apply Int.emod_eq_of_lt (Ckpt.bv_uint_nonneg x)
  have := bv_uint_lt x
  have hp : (2 : Int) ^ x.width ≤ 2 ^ D.memories[k].dw := by
    exact_mod_cast Nat.pow_le_pow_right (by decide) hw
  omega

/-- **Resuming from a checkpoint is exact.** A run from the restored checkpoint of `s` prints
the outputs a run from `s` prints, on every tick sequence, and the two keep agreeing. -/
theorem resume_exact (hsem : Direct.DesignSemWF D) (haddr : addrError D = none)
    (hroot : Ckpt.imgRootError D = none) {s : RuntimeState} (hash cycle : Nat)
    (exts : Array Ckpt.Ext) (hsz : s.mems.size = D.memories.size) (hfit : WordsFit D s)
    (ts : List Direct.Tick) :
    (Direct.refTrace D (Ckpt.restore (Ckpt.save D hash cycle exts s)) ts).steps.map (·.outputs) =
      (Direct.refTrace D s ts).steps.map (·.outputs) :=
  (trace_agree hsem haddr hroot ts _ _ (restore_save_agree hash cycle exts hsz hfit)).1

end


/-! ## Words keep fitting their memories -/

section
variable {D : DesignCert} {i : RuntimeInput} {s : RuntimeState}

/-- A bit-vector slot's value is no wider than its static kind. -/
theorem slot_width (hsem : Direct.DesignSemWF D) {r w : Nat}
    (hk : Direct.slotKind D r = some (.bv w)) : (rhoOf D i s r).asBV.width ≤ w := by
  have hwf : DesignCert.DesignCertWF D := ⟨hsem.depsBounded, hsem.slotsInRange⟩
  unfold Direct.slotKind at hk
  cases hs : D.sources[r]? with
  | some sd =>
    rw [hs] at hk
    have hr : r < D.sources.size := Direct.lt_of_getElem?_some hs
    rw [rho_source hr]
    have hsd : D.sources[r]'hr = sd := by rw [Array.getElem?_eq_getElem hr] at hs; cases hs; rfl
    rw [hsd]
    cases sd <;> simp [Direct.sourceKind] at hk <;> subst hk <;> simp
  | none =>
    rw [hs] at hk
    have hr : D.sources.size ≤ r := by
      rcases Nat.lt_or_ge r D.sources.size with h | h
      · rw [Array.getElem?_eq_getElem h] at hs; cases hs
      · exact h
    cases hc : D.nodes[r - D.sources.size]? with
    | none => simp [hc] at hk
    | some c =>
      simp only [hc, Option.map_some, Option.some.injEq] at hk
      have := rho_node (i := i) (s := s) hwf hc
      rw [show D.sources.size + (r - D.sources.size) = r by omega] at this
      rw [this]
      have hw : w = c.width := by
        cases hop : c.op <;> simp [Direct.opKind, hop] at hk <;> exact hk.symm
      rw [hw]; exact eval_op_cert_width_le _ _ _

/-- What `wordError` gives: a node storing into a version of memory `k` stores `dw` bits. -/
theorem stored_width (hword : Ckpt.wordError D = none) {j : Nat} {c : DenseNodeCert}
    (hc : D.nodes[j]? = some c) {w m k : Nat} {md : MemoryDesc} (hw : Ckpt.storedWidth D c = some w)
    (hm : c.deps[0]? = some m) (hr : rootOf D (D.nodes.size + 1) m = some (some k))
    (hmd : D.memories[k]? = some md) : w = md.dw := by
  have hj : j < D.nodes.size := Direct.lt_of_getElem?_some hc
  unfold Ckpt.wordError at hword
  have h1 := List.findSome?_eq_none_iff.mp hword j (List.mem_range.mpr hj)
  have hcj : D.nodes[j]! = c := by rw [getElem!_pos D.nodes j hj]; rw [Array.getElem?_eq_getElem hj] at hc; cases hc; rfl
  simp only [hcj, hw, hm, hr, hmd] at h1
  split at h1
  · rename_i he; exact beq_iff_eq.mp he
  · cases h1

/-- Every version of memory `k` keeps its words below `depth` at most `dw` bits wide. -/
theorem version_fits (hsem : Direct.DesignSemWF D) (hword : Ckpt.wordError D = none)
    (hfit : WordsFit D s) {r k : Nat} (hri : RootIs D r k) :
    ∀ (hk : k < D.memories.size) (a : Int), 0 ≤ a → a < D.memories[k].depth →
      ((rhoOf D i s r).asMem a).width ≤ D.memories[k].dw := by
  have hwf : DesignCert.DesignCertWF D := ⟨hsem.depsBounded, hsem.slotsInRange⟩
  induction hri with
  | @src r k aw dw hs =>
    intro hk a h0 ha
    have hr := Direct.lt_of_getElem?_some hs
    rw [rho_source hr]
    have hsd : D.sources[r]'hr = .memImg k aw dw := by
      rw [Array.getElem?_eq_getElem hr] at hs; exact Option.some.inj hs
    rw [hsd]
    exact hfit k hk a h0 ha
  | @node j k m c hc hop hm hmr ih =>
    intro hk a h0 ha
    rw [rho_node (i := i) (s := s) hwf hc]
    have hmd : D.memories[k]? = some D.memories[k] := Array.getElem?_eq_getElem hk
    have hj : j < D.nodes.size := Direct.lt_of_getElem?_some hc
    have hmlt : m < D.sources.size + D.nodes.size := by
      have := hsem.depsBounded j c hc m (Array.mem_toList_iff.mpr (Array.mem_of_getElem? hm)); omega
    have hroot := rootIs_rootOf hsem.depsBounded hmr hmlt
    have har := deps_size hsem hc
    cases hcop : c.op with
    | Op_MemWrite =>
      rw [hcop] at har
      have h4 : c.deps.toList.length = 4 := by simpa [Direct.opArity] using har
      obtain ⟨m', av, d, en, hl⟩ : ∃ m' av d en, c.deps.toList = [m', av, d, en] := by
        match hlist : c.deps.toList, h4 with
        | [m', av, d, en], _ => exact ⟨m', av, d, en, rfl⟩
      have hm' : m' = m := by rw [deps_get hl] at hm; simpa using hm
      subst hm'
      simp only [hl, List.map]
      show ((cert_mem_write (rhoOf D i s m').asMem (rhoOf D i s av).asBV (rhoOf D i s d).asBV
        (rhoOf D i s en).asBV) a).width ≤ _
      simp only [cert_mem_write]
      split
      · dsimp only
        split
        · -- the stored data: `wordError` fixes its kind at `dw`
          have hkind : ∃ w, Direct.slotKind D d = some (.bv w) := by
            have := hsem.nodeKinds j c hc 2 d (by rw [deps_get hl]; rfl)
            unfold Direct.slotIsMem at this
            cases hsk : Direct.slotKind D d with
            | none => rw [hsk] at this; cases this
            | some sk => cases sk with
              | bv w => exact ⟨w, rfl⟩
              | mem => rw [hsk] at this; simp [Direct.SlotKind.isMem, Direct.depIsMem, hcop] at this
          obtain ⟨w, hsk⟩ := hkind
          have hsw : Ckpt.storedWidth D c = some w := by
            simp only [Ckpt.storedWidth, hcop, deps_get hl]; simp [hsk]
          have := stored_width hword hc hsw (by rw [deps_get hl]; rfl) hroot hmd
          exact this ▸ slot_width hsem hsk
        · exact ih hk a h0 ha
      · exact ih hk a h0 ha
    | Op_MemWriteBE bw =>
      rw [hcop] at har
      have h4 : c.deps.toList.length = 4 := by simp [Direct.opArity] at har; simpa using har.1
      obtain ⟨m', av, d, be, hl⟩ : ∃ m' av d be, c.deps.toList = [m', av, d, be] := by
        match hlist : c.deps.toList, h4 with
        | [m', av, d, be], _ => exact ⟨m', av, d, be, rfl⟩
      have hm' : m' = m := by rw [deps_get hl] at hm; simpa using hm
      subst hm'
      simp only [hl, List.map]
      show ((cert_mem_write_be c.width (rhoOf D i s m').asMem (rhoOf D i s av).asBV (rhoOf D i s d).asBV
        (rhoOf D i s be).asBV bw) a).width ≤ _
      simp only [cert_mem_write_be]
      split
      · dsimp only
        split
        · have hsw : Ckpt.storedWidth D c = some c.width := by simp [Ckpt.storedWidth, hcop]
          have := stored_width hword hc hsw (by rw [deps_get hl]; rfl) hroot hmd
          simp only [cert_masked_update, HwMem.mk_bv_width]; omega
        · exact ih hk a h0 ha
      · exact ih hk a h0 ha
    | Op_MemLoad n =>
      rw [hcop] at har
      have h3 : c.deps.toList.length = 3 := by simp [Direct.opArity] at har; simpa using har.1
      obtain ⟨m', bus, en, hl⟩ : ∃ m' bus en, c.deps.toList = [m', bus, en] := by
        match hlist : c.deps.toList, h3 with
        | [m', bus, en], _ => exact ⟨m', bus, en, rfl⟩
      have hm' : m' = m := by rw [deps_get hl] at hm; simpa using hm
      subst hm'
      simp only [hl, List.map]
      show ((cert_mem_load c.width n (rhoOf D i s m').asMem (rhoOf D i s bus).asBV
        (rhoOf D i s en).asBV) a).width ≤ _
      simp only [cert_mem_load]
      split
      · dsimp only
        split
        · have hsw : Ckpt.storedWidth D c = some c.width := by simp [Ckpt.storedWidth, hcop]
          have := stored_width hword hc hsw (by rw [deps_get hl]; rfl) hroot hmd
          simp only [HwMem.mk_bv_width]; omega
        · exact ih hk a h0 ha
      · exact ih hk a h0 ha
    | _ => simp [isImgOp, hcop] at hop

end

/-! ## Every reachable state fits, so every checkpoint a run writes resumes exactly -/

section
variable {D : DesignCert} {i : RuntimeInput} {s : RuntimeState}

theorem wordsFit_step (hsem : Direct.DesignSemWF D) (hroot : Ckpt.imgRootError D = none)
    (hword : Ckpt.wordError D = none) (hfit : WordsFit D s) (e : ClockEdges) :
    WordsFit D (interpretDesign D e i s).nextState := by
  intro k hk a h0 ha
  rw [interpretDesign_mems]
  simp only [Array.getElem?_mapIdx, Array.getElem?_eq_getElem hk, Option.map_some, Option.getD_some]
  have hslots := imgRoot_ok hroot hk
  rw [getElem!_pos D.memories k hk] at hslots
  have hv (sl : Nat) (hsl : sl ∈ [D.memories[k].nextImg] ++ D.memories[k].commits.toList.map (·.nextImg) ++
      D.memories[k].asyncImg.toList) : ((rhoOf D i s sl).asMem a).width ≤ D.memories[k].dw :=
    version_fits hsem hword hfit (rootIs_of_rootOf hsem.depsBounded _ _ _ (hslots sl hsl)) hk a h0 ha
  have hheld : ((srcMemHeld (rhoOf D i s) s k D.memories[k]) a).width ≤ D.memories[k].dw := by
    unfold srcMemHeld
    cases hx : D.memories[k].asyncImg with
    | some x => simp only; exact hv x (by simp [hx])
    | none => simp only; exact hfit k hk a h0 ha
  unfold srcMemNext
  split
  · split
    · exact hv _ (by simp)
    · exact hheld
  · split
    · exact hv _ (by simp)
    · cases hcm : D.memories[k].commits.find? (D.memories[k].firedIs e) with
      | some cm =>
        simp only
        have hmem : cm ∈ D.memories[k].commits := Array.mem_of_find?_eq_some hcm
        exact hv _ (List.mem_append_left _ (List.mem_append_right _
          (List.mem_map.mpr ⟨cm, Array.mem_toList_iff.mpr hmem, rfl⟩)))
      | none => simp only; exact hheld

theorem mems_size_step (e : ClockEdges) :
    (interpretDesign D e i s).nextState.mems.size = D.memories.size := by
  simp [interpretDesign]

theorem wordsFit_trace (hsem : Direct.DesignSemWF D) (hroot : Ckpt.imgRootError D = none)
    (hword : Ckpt.wordError D = none) :
    ∀ (ts : List Direct.Tick) (s : RuntimeState), WordsFit D s →
      WordsFit D (Direct.refTrace D s ts).finalState
  | [], _, h => h
  | tk :: ts, _, h => wordsFit_trace hsem hroot hword ts _ (wordsFit_step (i := tk.input) hsem hroot hword h tk.edges)

theorem mems_size_trace (hsz : s.mems.size = D.memories.size) :
    ∀ (ts : List Direct.Tick), (Direct.refTrace D s ts).finalState.mems.size = D.memories.size := by
  intro ts
  induction ts generalizing s with
  | nil => exact hsz
  | cons tk ts ih => exact ih (s := (interpretDesign D tk.edges tk.input s).nextState) (mems_size_step _)

theorem wordsFit_reset : WordsFit D (Direct.resetState D) := by
  intro k hk a h0 ha
  simp [Direct.resetState, Array.getElem?_eq_getElem hk, MemoryDesc.powerOnImg, h0]

theorem wordsFit_zero : WordsFit D (Direct.zeroState D) := by
  intro k hk a h0 ha
  simp [Direct.zeroState, Array.getElem?_eq_getElem hk]

/-- A restored valid checkpoint fits. -/
theorem wordsFit_restore {hash : Nat} {c : Ckpt.Ckpt} (hv : Ckpt.validate D hash c = none) :
    WordsFit D (Ckpt.restore c) := by
  have hval := Ckpt.valid_of hv
  intro k hk a h0 ha
  have hk' : k < c.mems.size := by rw [hval.nmems]; exact hk
  have hok := Ckpt.memOK_of (hval.mems k hk')
  rw [getElem!_pos D.memories k hk] at hok
  simp only [Ckpt.restore, Array.getElem?_map, Array.getElem?_eq_getElem hk', Option.map_some,
    Option.getD_some, Ckpt.memImage, HwMem.mk_bv_width]
  rw [hok.dw]
  exact Nat.le_refl _

/-- **Every checkpoint a run from power-on writes resumes exactly**: after any ticks `ts0` from
`resetState`, a run from the saved-and-restored state prints the outputs the run itself prints,
on every tick sequence `ts`. -/
theorem resume_exact_reached (hsem : Direct.DesignSemWF D) (haddr : addrError D = none)
    (hroot : Ckpt.imgRootError D = none) (hword : Ckpt.wordError D = none)
    (ts0 ts : List Direct.Tick) (hash cycle : Nat) (exts : Array Ckpt.Ext) :
    let s := (Direct.refTrace D (Direct.resetState D) ts0).finalState
    (Direct.refTrace D (Ckpt.restore (Ckpt.save D hash cycle exts s)) ts).steps.map (·.outputs) =
      (Direct.refTrace D s ts).steps.map (·.outputs) :=
  resume_exact hsem haddr hroot hash cycle exts
    (mems_size_trace (Direct.resetState_mems D) ts0)
    (wordsFit_trace hsem hroot hword ts0 _ wordsFit_reset) ts

/-- …and from any valid checkpoint: resuming a resumed run is exact too. -/
theorem resume_exact_from_ckpt (hsem : Direct.DesignSemWF D) (haddr : addrError D = none)
    (hroot : Ckpt.imgRootError D = none) (hword : Ckpt.wordError D = none)
    {hash : Nat} {c : Ckpt.Ckpt} (hv : Ckpt.validate D hash c = none)
    (ts0 ts : List Direct.Tick) (hash' cycle : Nat) (exts : Array Ckpt.Ext) :
    let s := (Direct.refTrace D (Ckpt.restore c) ts0).finalState
    (Direct.refTrace D (Ckpt.restore (Ckpt.save D hash' cycle exts s)) ts).steps.map (·.outputs) =
      (Direct.refTrace D s ts).steps.map (·.outputs) := by
  have hval := Ckpt.valid_of hv
  have hsz : (Ckpt.restore c).mems.size = D.memories.size := by
    simp [Ckpt.restore, hval.nmems]
  exact resume_exact hsem haddr hroot hash' cycle exts (mems_size_trace hsz ts0)
    (wordsFit_trace hsem hroot hword ts0 _ (wordsFit_restore hv)) ts

end

#audit_axioms Projection.CkptResume.step_agree
#audit_axioms Projection.CkptResume.trace_agree
#audit_axioms Projection.CkptResume.resume_exact
#audit_axioms Projection.CkptResume.resume_exact_reached
#audit_axioms Projection.CkptResume.resume_exact_from_ckpt

end CkptResume
end Projection

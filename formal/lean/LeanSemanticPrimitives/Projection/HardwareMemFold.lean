/-
# The node fold over a mixed environment

The invariant the walk carries, and the two facts that re-establish it at each
node.  Nothing here is a hypothesis about kinds or values: the kinds come from
`Direct.DesignSemWF` (the checker's own output) and the values from
`evalGraphG`'s fixpoint property, which `Hw.evalGraphG_rec` already proves.

`slotValM` is the reference slot value WITH memories — a `CertVal`, not
projected through `asBV`, so a memory slot keeps its function and a scalar slot
keeps its bit vector.  `PrefixRelM` says the object's slot list is a typed
witness list for the first `m` slots whose denotation is exactly that and whose
kinds are exactly `Direct.slotIsMem`'s.
-/
import LeanSemanticPrimitives.Projection.HardwareMemNodes

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw Projection.HwLocal

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. The reference slot value, with memories -/

/-- Slot `k`'s value as the SHARED semantics computes it, kept as a `CertVal`:
a memory slot is a `.mem`, a scalar slot a `.bv`.  `Hw.slotVal` is this
projected through `asBV`, which is only correct where there are no memories. -/
def slotValM (D : DesignCert) (i : RuntimeInput) (st : StateM) (k : Nat) : CertVal :=
  evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i (StateM.denote st)) k

/-- Below `D.sources.size` the graph evaluation is the source environment, so
the slot value is `Compiler.sourceValue`. -/
theorem slotValM_source {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {k : Nat} (hk : k < D.sources.size) :
    slotValM D i st k = sourceValue i (StateM.denote st) (D.sources[k]'hk) := by
  have hnot : k ∉ D.toGraphCert.topo := DesignCert.source_not_mem_topo (by simpa using hk)
  unfold slotValM
  rw [GraphRefine.evalGraphG_not_mem _ _ _ _ hnot]
  simp [srcEnv, Array.getElem?_eq_getElem hk]

/-- **The local recurrence, at `CertVal` level.**  `Hw.nodeVal_rec` is this with
`asBV` applied and `OpSupported` assumed; the memory route needs it unprojected
and for every operator, so the projection step is dropped rather than widened. -/
theorem nodeValM_rec {D : DesignCert} {i : RuntimeInput} {st : StateM}
    (hwf : DesignCert.DesignCertWF D) {j : Nat} {c : DenseNodeCert}
    (hj : j < D.nodes.size) (hc : D.nodes[j]? = some c) :
    eval_op_cert c.op c.width (c.deps.toList.map (slotValM D i st))
      = slotValM D i st (D.slotOfNode j) := by
  have hfacts := DesignCert.wf_graph_facts hwf
  have hmem : D.slotOfNode j ∈ D.toGraphCert.topo :=
    DesignCert.slotOfNode_mem_topo (by simpa [DesignCert.numNodes] using hj)
  have hrec := evalGraphG_rec (V := CertVal) D.toGraphCert D.toGraphCert.topo
    (srcEnv D i (StateM.denote st)) hfacts.1 hfacts.2.1 hfacts.2.2 _ hmem
  have hnode : D.toGraphCert.nodes (D.slotOfNode j)
      = some ⟨D.slotOfNode j, c.op, c.width, c.deps.toList⟩ := by
    simp [DesignCert.toGraphCert, DesignCert.nodeAt?_slotOfNode, hc]
  have hR : slotValM D i st (D.slotOfNode j)
      = evalNodeG D.toGraphCert
          (evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i (StateM.denote st)))
          (D.slotOfNode j) := by
    simp only [slotValM]; rw [hrec]
  rw [hR]
  simp only [evalNodeG, hnode]
  rfl

/-! ## 2. The traversal invariant -/

/-- The object's slot list for the first `m` slots, as a TYPED witness list:
its denotation is the reference slot value and its kinds are the checker's. -/
structure PrefixRelM (D : DesignCert) (i : RuntimeInput) (st : StateM)
    (m : Nat) (ss : List SlotV) : Prop where
  len  : ss.length = m
  den  : ∀ (k : Nat) (h : k < ss.length), (ss[k]'h).denote = slotValM D i st k
  kind : ∀ (k : Nat) (h : k < ss.length),
           Direct.slotIsMem D k = some (ss[k]'h).isMem

/-- **The source prefix satisfies it.**  Nothing is assumed about the sources
beyond what `mkSourcesM` already produces. -/
theorem prefixRelM_sources (D : DesignCert) (i : RuntimeInput) (st : StateM) :
    PrefixRelM D i st D.sources.size (D.sources.toList.map (srcSlotV i st)) where
  len := by simp
  den := by
    intro k h
    have hk : k < D.sources.size := by simpa using h
    rw [List.getElem_map, srcSlotV_denote, slotValM_source hk]
    simp
  kind := by
    intro k h
    have hk : k < D.sources.size := by simpa using h
    rw [List.getElem_map]
    exact srcSlots_slotKind i st hk

/-! ## 3. Reading a dependency out of the invariant

The two consequences the node bridges consume, both derived from the CHECKER's
`nodeKinds` rather than assumed. -/

/-- A dependency the checker calls a bit vector IS one, and it holds the
reference's `asBV`. -/
theorem dep_scalar {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {m : Nat} {ss : List SlotV} (hpre : PrefixRelM D i st m ss)
    {d : Nat} (hd : d < ss.length)
    (hk : Direct.slotIsMem D d = some false) :
    (ss.map SlotV.enc)[d]'(by simpa using hd)
      = encBV (slotValM D i st d).asBV := by
  have hkind := hpre.kind d hd
  rw [hk] at hkind
  have hb : (ss[d]'hd).isMem = false := by
    have := Option.some.inj hkind
    exact this.symm
  have hv := SlotV.bv_of_not_isMem hb
  rw [List.getElem_map, hv, SlotV.enc, hpre.den d hd]

/-- A dependency the checker calls a memory IS one, and its image denotes the
reference's `asMem`. -/
theorem dep_mem {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {m : Nat} {ss : List SlotV} (hpre : PrefixRelM D i st m ss)
    {d : Nat} (hd : d < ss.length)
    (hk : Direct.slotIsMem D d = some true) :
    ∃ img : MemImg,
      (ss.map SlotV.enc)[d]'(by simpa using hd) = MemImg.encMemImg img
      ∧ MemImg.denote img = (slotValM D i st d).asMem := by
  have hkind := hpre.kind d hd
  rw [hk] at hkind
  have hb : (ss[d]'hd).isMem = true := (Option.some.inj hkind).symm
  cases hv : ss[d]'hd with
  | bv b  => rw [hv] at hb; simp [SlotV.isMem] at hb
  | img g =>
      refine ⟨g, ?_, ?_⟩
      · rw [List.getElem_map, hv]; rfl
      · have := hpre.den d hd
        rw [hv] at this
        rw [← this]
        rfl

/-- The checker's own statement that a dependency has the kind its position
requires, specialized to the two cases the bridges need. -/
theorem dep_kind_of_semwf {D : DesignCert} (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    {p d : Nat} (hp : c.deps[p]? = some d) :
    Direct.slotIsMem D d = some (Direct.depIsMem c.op p) :=
  hsem.nodeKinds j c hc p d hp

/-! ## 4. Arity, from the checker's own node shapes -/

private theorem list_len_four {α : Type} : ∀ {l : List α}, l.length = 4 →
    ∃ a b c d, l = [a, b, c, d]
  | [a, b, c, d], _ => ⟨a, b, c, d, rfl⟩
  | [],           h => by simp at h
  | [_],          h => by simp at h
  | [_, _],       h => by simp at h
  | [_, _, _],    h => by simp at h
  | _ :: _ :: _ :: _ :: _ :: _, h => by simp at h

/-- `checkDesign` already fixed each node's operand count. -/
theorem opArity_of_semwf {D : DesignCert} (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) :
    Direct.opArity c.op c.deps.size = true := by
  have h := hsem.nodeShapes j c hc
  unfold Direct.opShape at h
  simp only [Bool.and_eq_true] at h
  exact h.1.2

theorem memRead_deps {D : DesignCert} (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) (hop : c.op = .Op_MemRead) :
    ∃ d0 d1 d2, c.deps.toList = [d0, d1, d2] := by
  have h := opArity_of_semwf hsem hc
  rw [hop] at h
  simp only [Direct.opArity, beq_iff_eq] at h
  exact Hw.list_len_three (by simpa using h)

theorem memWriteBE_deps {D : DesignCert} (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} {bw : Nat} (hc : D.nodes[j]? = some c)
    (hop : c.op = .Op_MemWriteBE bw) :
    ∃ d0 d1 d2 d3, c.deps.toList = [d0, d1, d2, d3] := by
  have h := opArity_of_semwf hsem hc
  rw [hop] at h
  simp only [Direct.opArity, Bool.and_eq_true, beq_iff_eq] at h
  exact list_len_four (by simpa using h.1)

/-! ## 5. Dependency bounds and the scalar dependency premise

Both come from facts the certificate ALREADY carries: `DesignCertWF.depsBounded`
for the bound and `Direct.DesignSemWF.nodeKinds` for the kind. -/

theorem deps_bound {D : DesignCert} {i : RuntimeInput} {st : StateM}
    (hwf : DesignCert.DesignCertWF D) {j : Nat} {c : DenseNodeCert}
    (hc : D.nodes[j]? = some c) {ss : List SlotV}
    (hpre : PrefixRelM D i st (D.sources.size + j) ss) :
    ∀ d ∈ c.deps.toList, d < ss.length := by
  intro d hd
  rw [hpre.len]
  simpa using hwf.depsBounded j c hc d hd

theorem dep_at_position {c : DenseNodeCert} {d : Nat} (hd : d ∈ c.deps.toList) :
    ∃ p : Nat, c.deps[p]? = some d := by
  obtain ⟨p, hp, hpe⟩ := List.mem_iff_getElem.mp hd
  refine ⟨p, ?_⟩
  have hps : p < c.deps.size := by simpa using hp
  rw [Array.getElem?_eq_getElem hps]
  simp [← hpe]

/-- **Every dependency of a supported node is a scalar slot** — from the
checker, not assumed. -/
theorem deps_scalar {D : DesignCert} {i : RuntimeInput} {st : StateM}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (hsup : OpSupported c.op = true) {ss : List SlotV}
    (hpre : PrefixRelM D i st (D.sources.size + j) ss) :
    ∀ d ∈ c.deps.toList,
      SlotHere (ss.map SlotV.enc) (fun k => (slotValM D i st k).asBV) d := by
  intro d hd
  obtain ⟨p, hp⟩ := dep_at_position hd
  have hkind := dep_kind_of_semwf hsem hc hp
  rw [depIsMem_false_of_supported hsup p] at hkind
  exact ⟨by simpa using deps_bound hwf hc hpre d hd,
         dep_scalar hpre (deps_bound hwf hc hpre d hd) hkind⟩

/-! ## 6. One admitted SCALAR node's step -/

theorem node_step_scalar {D : DesignCert} {i : RuntimeInput} {st : StateM}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) (hj : j < D.nodes.size)
    (hsup : OpSupported c.op = true) (harity : ArityOK c.op c.deps.toList)
    {ss : List SlotV} (hpre : PrefixRelM D i st (D.sources.size + j) ss)
:
    ∃ v : SlotV,
      v.denote = slotValM D i st (D.sources.size + j)
      ∧ Direct.slotIsMem D (D.sources.size + j) = some v.isMem
      ∧ ∀ (σ : SEnv) (end_ eenv en : SExp),
          SEval hwMemS σ end_ (encNode c) →
          SEval hwMemS σ eenv (objEnv (ss.map SlotV.enc)) →
          SEval hwMemS σ en (.int (Int.ofNat ss.length)) →
          SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en]) v.enc := by
  refine ⟨.bv (eval_op c.op c.width
      (c.deps.toList.map (fun k => (slotValM D i st k).asBV))), ?_, ?_, ?_⟩
  · have hrec := nodeValM_rec (i := i) (st := st) hwf hj hc
    rw [Hw.evalOpCert_of_supported hsup, List.map_map] at hrec
    exact hrec
  · rw [nodeSlot_slotIsMem hc, opKind_isMem_of_supported hsup]
    rfl
  · intro σ end_ eenv en hnd henv hlen
    exact evalNodeM_scalar hsup harity (deps_scalar hwf hsem hc hsup hpre) hnd henv
      (by simpa using hlen)

/-! ## 7. The two memory nodes' steps

The operand kinds are again the CHECKER's (`nodeKinds` at positions 0..3), and
the enabled write's range obligation is NAMED rather than assumed away. -/

theorem deps_get (c : DenseNodeCert) {l : List Nat} (h : c.deps.toList = l) (p : Nat) :
    c.deps[p]? = l[p]? := by rw [← h]; simp

/-- The obligation an admitted WRITE node carries at this cycle: the image it
reaches at operand 0 holds the address it is given at operand 1.
`writeInRange_of_FullTable` discharges it from the admitted table shape; it is
not assumed anywhere below. -/
def NodeWriteRange (D : DesignCert) (i : RuntimeInput) (st : StateM)
    (c : DenseNodeCert) : Prop :=
  ∀ (img : MemImg) (d0 d1 : Nat), c.deps[0]? = some d0 → c.deps[1]? = some d1 →
    MemImg.denote img = (slotValM D i st d0).asMem →
    MemImg.InRange img (bv_uint (slotValM D i st d1).asBV)

theorem node_step_memRead {D : DesignCert} {i : RuntimeInput} {st : StateM}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) (hj : j < D.nodes.size)
    (hop : c.op = .Op_MemRead)
    {ss : List SlotV} (hpre : PrefixRelM D i st (D.sources.size + j) ss)
:
    ∃ v : SlotV,
      v.denote = slotValM D i st (D.sources.size + j)
      ∧ Direct.slotIsMem D (D.sources.size + j) = some v.isMem
      ∧ ∀ (σ : SEnv) (end_ eenv en : SExp),
          SEval hwMemS σ end_ (encNode c) →
          SEval hwMemS σ eenv (objEnv (ss.map SlotV.enc)) →
          SEval hwMemS σ en (.int (Int.ofNat ss.length)) →
          SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en]) v.enc := by
  obtain ⟨d0, d1, d2, hdeps⟩ := memRead_deps hsem hc hop
  have hm0 : d0 ∈ c.deps.toList := by rw [hdeps]; simp
  have hm1 : d1 ∈ c.deps.toList := by rw [hdeps]; simp
  have hm2 : d2 ∈ c.deps.toList := by rw [hdeps]; simp
  have hb0 := deps_bound hwf hc hpre d0 hm0
  have hb1 := deps_bound hwf hc hpre d1 hm1
  have hb2 := deps_bound hwf hc hpre d2 hm2
  have hp0 : c.deps[0]? = some d0 := by rw [deps_get c hdeps 0]; rfl
  have hp1 : c.deps[1]? = some d1 := by rw [deps_get c hdeps 1]; rfl
  have hp2 : c.deps[2]? = some d2 := by rw [deps_get c hdeps 2]; rfl
  have hk0 : Direct.slotIsMem D d0 = some true := by
    have := dep_kind_of_semwf hsem hc hp0; rwa [hop] at this
  have hk1 : Direct.slotIsMem D d1 = some false := by
    have := dep_kind_of_semwf hsem hc hp1; rwa [hop] at this
  have hk2 : Direct.slotIsMem D d2 = some false := by
    have := dep_kind_of_semwf hsem hc hp2; rwa [hop] at this
  obtain ⟨img, henc0, hden0⟩ := dep_mem hpre hb0 hk0
  have henc1 := dep_scalar hpre hb1 hk1
  have henc2 := dep_scalar hpre hb2 hk2
  refine ⟨.bv (cert_mem_read c.width (MemImg.denote img)
      (slotValM D i st d1).asBV (slotValM D i st d2).asBV), ?_, ?_, ?_⟩
  · have hrec := nodeValM_rec (i := i) (st := st) hwf hj hc
    rw [hop, hdeps] at hrec
    rw [← hrec, hden0]
    rfl
  · rw [nodeSlot_slotIsMem hc, hop]; rfl
  · intro σ end_ eenv en hnd henv hlen
    exact evalNodeM_memRead hop hdeps (by simpa using hb0) (by simpa using hb1)
      (by simpa using hb2) henc0 henc1 henc2 hnd henv (by simpa using hlen)

theorem node_step_memWriteBE {D : DesignCert} {i : RuntimeInput} {st : StateM}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} {bw : Nat}
    (hc : D.nodes[j]? = some c) (hj : j < D.nodes.size)
    (hop : c.op = .Op_MemWriteBE bw) (hrange : NodeWriteRange D i st c)
    {ss : List SlotV} (hpre : PrefixRelM D i st (D.sources.size + j) ss)
:
    ∃ v : SlotV,
      v.denote = slotValM D i st (D.sources.size + j)
      ∧ Direct.slotIsMem D (D.sources.size + j) = some v.isMem
      ∧ ∀ (σ : SEnv) (end_ eenv en : SExp),
          SEval hwMemS σ end_ (encNode c) →
          SEval hwMemS σ eenv (objEnv (ss.map SlotV.enc)) →
          SEval hwMemS σ en (.int (Int.ofNat ss.length)) →
          SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en]) v.enc := by
  obtain ⟨d0, d1, d2, d3, hdeps⟩ := memWriteBE_deps hsem hc hop
  have hm0 : d0 ∈ c.deps.toList := by rw [hdeps]; simp
  have hm1 : d1 ∈ c.deps.toList := by rw [hdeps]; simp
  have hm2 : d2 ∈ c.deps.toList := by rw [hdeps]; simp
  have hm3 : d3 ∈ c.deps.toList := by rw [hdeps]; simp
  have hb0 := deps_bound hwf hc hpre d0 hm0
  have hb1 := deps_bound hwf hc hpre d1 hm1
  have hb2 := deps_bound hwf hc hpre d2 hm2
  have hb3 := deps_bound hwf hc hpre d3 hm3
  have hp0 : c.deps[0]? = some d0 := by rw [deps_get c hdeps 0]; rfl
  have hp1 : c.deps[1]? = some d1 := by rw [deps_get c hdeps 1]; rfl
  have hp2 : c.deps[2]? = some d2 := by rw [deps_get c hdeps 2]; rfl
  have hp3 : c.deps[3]? = some d3 := by rw [deps_get c hdeps 3]; rfl
  have hk0 : Direct.slotIsMem D d0 = some true := by
    have := dep_kind_of_semwf hsem hc hp0; rwa [hop] at this
  have hk1 : Direct.slotIsMem D d1 = some false := by
    have := dep_kind_of_semwf hsem hc hp1; rwa [hop] at this
  have hk2 : Direct.slotIsMem D d2 = some false := by
    have := dep_kind_of_semwf hsem hc hp2; rwa [hop] at this
  have hk3 : Direct.slotIsMem D d3 = some false := by
    have := dep_kind_of_semwf hsem hc hp3; rwa [hop] at this
  obtain ⟨img, henc0, hden0⟩ := dep_mem hpre hb0 hk0
  have henc1 := dep_scalar hpre hb1 hk1
  have henc2 := dep_scalar hpre hb2 hk2
  have henc3 := dep_scalar hpre hb3 hk3
  have hrec := nodeValM_rec (i := i) (st := st) hwf hj hc
  rw [hop, hdeps] at hrec
  have hkindnode : Direct.slotIsMem D (D.sources.size + j) = some true := by
    rw [nodeSlot_slotIsMem hc, hop]; rfl
  by_cases hen : bv_nonzero (slotValM D i st d3).asBV
  · -- ENABLED: the range obligation is discharged by `hrange`, not assumed
    have hr : MemImg.InRange img (bv_uint (slotValM D i st d1).asBV) :=
      hrange img d0 d1 hp0 hp1 hden0
    refine ⟨.img (MemImg.writeBE c.width img (slotValM D i st d1).asBV
        (slotValM D i st d2).asBV (slotValM D i st d3).asBV bw), ?_, ?_, ?_⟩
    · rw [← hrec, SlotV.denote, MemImg.denote_writeBE _ _ _ _ _ _ hr, hden0]
      rfl
    · rw [hkindnode]; rfl
    · intro σ end_ eenv en hnd henv hlen
      exact evalNodeM_memWriteBE hop hdeps (by simpa using hb0) (by simpa using hb1)
        (by simpa using hb2) (by simpa using hb3) henc0 henc1 henc2 henc3 hen hr
        hnd henv (by simpa using hlen)
  · -- DISABLED: no range premise, and the image is returned EXACTLY
    refine ⟨.img img, ?_, ?_, ?_⟩
    · rw [← hrec, SlotV.denote, hden0]
      show CertVal.mem (slotValM D i st d0).asMem
         = CertVal.mem (cert_mem_write_be c.width (slotValM D i st d0).asMem
             (slotValM D i st d1).asBV (slotValM D i st d2).asBV
             (slotValM D i st d3).asBV bw)
      simp [cert_mem_write_be, hen]
    · rw [hkindnode]; rfl
    · intro σ end_ eenv en hnd henv hlen
      exact evalNodeM_memWriteBE_disabled hop hdeps (by simpa using hb0)
        (by simpa using hb1) (by simpa using hb2) (by simpa using hb3)
        henc0 henc1 henc2 henc3 hen hnd henv (by simpa using hlen)

/-! ## 8. One admitted node, whichever kind it is -/

theorem node_step {D : DesignCert} {i : RuntimeInput} {st : StateM}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) (hj : j < D.nodes.size)
    (hadm : NodeAdmitted c) (harity : ArityOK c.op c.deps.toList)
    (hrange : NodeWriteRange D i st c)
    {ss : List SlotV} (hpre : PrefixRelM D i st (D.sources.size + j) ss)
:
    ∃ v : SlotV,
      v.denote = slotValM D i st (D.sources.size + j)
      ∧ Direct.slotIsMem D (D.sources.size + j) = some v.isMem
      ∧ ∀ (σ : SEnv) (end_ eenv en : SExp),
          SEval hwMemS σ end_ (encNode c) →
          SEval hwMemS σ eenv (objEnv (ss.map SlotV.enc)) →
          SEval hwMemS σ en (.int (Int.ofNat ss.length)) →
          SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en]) v.enc := by
  rcases hadm with hsup | hop | ⟨bw, hop⟩
  · exact node_step_scalar hwf hsem hc hj hsup harity hpre
  · exact node_step_memRead hwf hsem hc hj hop hpre
  · exact node_step_memWriteBE hwf hsem hc hj hop hrange hpre

/-! ## 9. Extending the invariant, and the object's environment step -/

theorem prefixRelM_snoc {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {m : Nat} {ss : List SlotV} {v : SlotV}
    (hpre : PrefixRelM D i st m ss)
    (hden : v.denote = slotValM D i st m)
    (hkind : Direct.slotIsMem D m = some v.isMem) :
    PrefixRelM D i st (m + 1) (ss ++ [v]) := by
  have hl : ss.length = m := hpre.len
  subst hl
  refine ⟨by simp, ?_, ?_⟩
  · intro k h
    by_cases hk : k < ss.length
    · rw [List.getElem_append_left hk]
      exact hpre.den k hk
    · have hkm : k = ss.length := by
        have hlt : k < ss.length + 1 := by simpa using h
        omega
      subst hkm
      rw [List.getElem_append_right (by omega)]
      simpa using hden
  · intro k h
    by_cases hk : k < ss.length
    · rw [List.getElem_append_left hk]
      exact hpre.kind k hk
    · have hkm : k = ss.length := by
        have hlt : k < ss.length + 1 := by simpa using h
        omega
      subst hkm
      rw [List.getElem_append_right (by omega)]
      simpa using hkind

/-- The object conses the new slot onto the front, which is `objEnv` of the
list with the slot appended -- the same reversal the memory-free route uses. -/
theorem objEnv_snoc (ss : List SlotV) (v : SlotV) :
    objEnv ((ss ++ [v]).map SlotV.enc) = Val.cons v.enc (objEnv (ss.map SlotV.enc)) := by
  simp [objEnv, List.map_append, encListG]

/-! ## 10. The fold

Same shape as `Hw.evalNodes_agree`: dense node order (which is already
topological, because `topo` is `slotsFrom`), the environment built newest-first,
and the slot counter advanced by one per node.  What differs is that the
environment carries TYPED slots, so a memory-valued node result is held as an
image rather than coerced. -/

theorem evalNodesM_agree {D : DesignCert} {i : RuntimeInput} {st : StateM}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hadm : ∀ c ∈ D.nodes.toList, NodeAdmitted c)
    (harity : ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList)
    (hrange : ∀ c ∈ D.nodes.toList, NodeWriteRange D i st c) :
    ∀ (r j : Nat), j + r = D.nodes.size →
      ∀ (ss : List SlotV), PrefixRelM D i st (D.sources.size + j) ss →
        ∃ ss' : List SlotV,
          PrefixRelM D i st (D.sources.size + D.nodes.size) ss'
          ∧ ∀ (σ : SEnv) (eno eenv en : SExp),
              SEval hwMemS σ eno (encListG encNode (D.nodes.toList.drop j)) →
              SEval hwMemS σ eenv (objEnv (ss.map SlotV.enc)) →
              SEval hwMemS σ en (.int (Int.ofNat (D.sources.size + j))) →
              SEval hwMemS σ (.call "evalNodesM" [eno, eenv, en])
                (objEnv (ss'.map SlotV.enc))
  | 0,     j, hr, ss, hpre => by
      have hj : j = D.nodes.size := by omega
      subst hj
      refine ⟨ss, hpre, ?_⟩
      intro σ eno eenv en hno henv hlen
      refine SEval_call3 hno henv hlen rfl rfl ?_
      refine SEval.iteT (SEval_prim1 (.ref rfl) ?_) (.ref rfl)
      have hnil : D.nodes.toList.drop D.nodes.size = [] := by simp
      rw [hnil]
      rfl
  | r + 1, j, hr, ss, hpre => by
      have hj : j < D.nodes.size := by omega
      have hjl : j < D.nodes.toList.length := by simpa using hj
      have hdrop : D.nodes.toList.drop j
          = D.nodes.toList[j] :: D.nodes.toList.drop (j + 1) :=
        List.drop_eq_getElem_cons hjl
      have hcj : D.nodes[j]? = some D.nodes.toList[j] := by
        rw [Array.getElem?_eq_getElem hj]; simp
      have hmemc : D.nodes.toList[j] ∈ D.nodes.toList := List.getElem_mem hjl
      obtain ⟨v, hvden, hvkind, hv⟩ :=
        node_step (i := i) (st := st) hwf hsem hcj hj (hadm _ hmemc) (harity _ hmemc)
          (hrange _ hmemc) hpre
      have hpre' : PrefixRelM D i st (D.sources.size + (j + 1)) (ss ++ [v]) := by
        have hx := prefixRelM_snoc hpre hvden hvkind
        simpa [Nat.add_assoc] using hx
      obtain ⟨ss', hss', hrec⟩ :=
        evalNodesM_agree hwf hsem hadm harity hrange r (j + 1) (by omega) (ss ++ [v]) hpre'
      refine ⟨ss', hss', ?_⟩
      intro σ eno eenv en hno henv hlen
      rw [hdrop] at hno
      refine SEval_call3 hno henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine hrec _ _ _ _ (SEval_tl (.ref rfl)) ?_
        (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega))
      rw [objEnv_snoc]
      exact SEval_consP
        (hv _ _ _ _ (SEval_hd (.ref rfl)) (.ref rfl)
          (by rw [hpre.len]; exact .ref rfl)) (.ref rfl)

/-! ## 11. Sources then nodes, composed

`mkSourcesM` lands exactly the list `prefixRelM_sources` describes, so the fold
starts from it with no gap. -/

theorem sourcesThenNodes {D : DesignCert} {i : RuntimeInput} {st : StateM}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hadm : ∀ c ∈ D.nodes.toList, NodeAdmitted c)
    (harity : ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList)
    (hrange : ∀ c ∈ D.nodes.toList, NodeWriteRange D i st c) :
    ∃ ss' : List SlotV,
      PrefixRelM D i st (D.sources.size + D.nodes.size) ss'
      ∧ ∀ (σ : SEnv) (eno eenv en : SExp),
          SEval hwMemS σ eno (encListG encNode D.nodes.toList) →
          SEval hwMemS σ eenv
            (objEnv ((D.sources.toList.map (srcSlotV i st)).map SlotV.enc)) →
          SEval hwMemS σ en (.int (Int.ofNat D.sources.size)) →
          SEval hwMemS σ (.call "evalNodesM" [eno, eenv, en])
            (objEnv (ss'.map SlotV.enc)) := by
  obtain ⟨ss', hss', hrec⟩ :=
    evalNodesM_agree hwf hsem hadm harity hrange D.nodes.size 0 (by omega)
      (D.sources.toList.map (srcSlotV i st))
      (by simpa using prefixRelM_sources D i st)
  refine ⟨ss', hss', ?_⟩
  intro σ eno eenv en hno henv hlen
  exact hrec σ eno eenv en (by simpa using hno) henv (by simpa using hlen)

/-! ## 12. Non-vacuity: a real mixed design, accepted by the REAL checker

Three nodes in dependency order: a memory READ, an intervening SCALAR node on
its result, and a byte-enabled WRITE whose data operand is that scalar node.
The certificate is accepted by `Direct.checkDesign` itself, so `DesignSemWF`
comes from `checkDesign_sound` rather than being assumed — which is what makes
every kind premise above non-vacuous at a shape with memories in it. -/

def mixedDesign : DesignCert :=
  { sources  := #[ .memImg 0 5 32        -- slot 0: the mutable image
                 , .input 0 5            -- slot 1: address
                 , .input 1 32           -- slot 2: byte-enable mask
                 , .input 2 1            -- slot 3: read enable
                 , .input 3 32 ]         -- slot 4: a scalar operand
  , nodes    := #[ { op := .Op_MemRead,      width := 32, deps := #[0, 1, 3] }
                 , { op := .Op_And,          width := 32, deps := #[5, 4] }
                 , { op := .Op_MemWriteBE 1, width := 32, deps := #[0, 1, 6, 2] } ]
  , outputs  := #[ { slot := 6, width := 32 } ]
  , flops    := #[]
  , memories := #[ { aw := 5, dw := 32, nextImg := 7, clock := 0 } ]
  , clocks   := #[{ name := "clock" }] }

/- the REAL checker accepts it -/
#guard Direct.acceptsDesign mixedDesign

/- …and every node passes the admission policy, the write included -/
#guard mixedDesign.nodes.toList.all nodeAdmittedB

/- the memory node's operand 0 is an image and its operand 1 is not -/
#guard Direct.slotIsMem mixedDesign 0 == some true
#guard Direct.slotIsMem mixedDesign 1 == some false
/- the READ's result is a bit vector, the WRITE's result is an image -/
#guard Direct.slotIsMem mixedDesign 5 == some false
#guard Direct.slotIsMem mixedDesign 6 == some false
#guard Direct.slotIsMem mixedDesign 7 == some true

/-- `DesignSemWF` for it, from the checker's own soundness theorem. -/
theorem mixedDesign_semwf : Direct.DesignSemWF mixedDesign := by
  have hacc : Direct.acceptsDesign mixedDesign = true := by decide
  cases hd : Direct.designErrors mixedDesign with
  | none =>
      exact Direct.checkDesign_sound (u := ()) (by unfold Direct.checkDesign; rw [hd])
  | some e =>
      exfalso
      unfold Direct.acceptsDesign at hacc
      rw [hd] at hacc
      simp at hacc

theorem mixedDesign_admitted : ∀ c ∈ mixedDesign.nodes.toList, NodeAdmitted c := by
  intro c hc
  have h : nodeAdmittedB c = true := by
    revert hc; revert c; decide
  exact (nodeAdmittedB_iff c).mp h

theorem mixedDesign_arity : ∀ c ∈ mixedDesign.nodes.toList,
    ArityOK c.op c.deps.toList := by
  intro c hc
  revert hc; revert c; decide

#print axioms mixedDesign_semwf
#print axioms mixedDesign_admitted
#print axioms mixedDesign_arity
#print axioms sourcesThenNodes

/-! ## 13. Crossed to the resolved program

Through the production bridge, as in the source module: the argument list is
literals, so the resolved term is pinned here and no `resolve` hypothesis is
left to the caller. -/

/-- `evalNodesM` sits where `resolveProgram` put it — the same id the executed
checks call. -/
theorem evalNodesM_id : idOf "evalNodesM" = some 47 := rfl

theorem sourcesThenNodes_resolved {D : DesignCert} {i : RuntimeInput} {st : StateM}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hadm : ∀ c ∈ D.nodes.toList, NodeAdmitted c)
    (harity : ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList)
    (hrange : ∀ c ∈ D.nodes.toList, NodeWriteRange D i st c) :
    ∃ ss' : List SlotV,
      PrefixRelM D i st (D.sources.size + D.nodes.size) ss'
      ∧ Eval hwMemP []
          (.call 47
             ([ encListG encNode D.nodes.toList
              , objEnv ((D.sources.toList.map (srcSlotV i st)).map SlotV.enc)
              , Val.int (Int.ofNat D.sources.size) ].map Term.lit))
          (objEnv (ss'.map SlotV.enc)) := by
  obtain ⟨ss', hss', hrec⟩ := sourcesThenNodes hwf hsem hadm harity hrange
  refine ⟨ss', hss', ?_⟩
  have hS := hrec [] (.lit (encListG encNode D.nodes.toList))
      (.lit (objEnv ((D.sources.toList.map (srcSlotV i st)).map SlotV.enc)))
      (.lit (Val.int (Int.ofNat D.sources.size))) .lit .lit .lit
  have hr := resolve_call_lits (fns := funNames hwMemS) (scope := []) (f := "evalNodesM")
    evalNodesM_id
    [ encListG encNode D.nodes.toList
    , objEnv ((D.sources.toList.map (srcSlotV i st)).map SlotV.enc)
    , Val.int (Int.ofNat D.sources.size) ]
  have := SEval_sound hwMemP_resolves [] _ _ _ hS (by simpa using hr)
  simpa using this

#print axioms evalNodesM_id
#print axioms sourcesThenNodes_resolved
#print axioms slotValM_source
#print axioms nodeValM_rec
#print axioms prefixRelM_sources
#print axioms dep_scalar
#print axioms dep_mem
#print axioms dep_kind_of_semwf
#print axioms opArity_of_semwf
#print axioms memRead_deps
#print axioms memWriteBE_deps
#print axioms deps_scalar
#print axioms node_step_scalar
#print axioms node_step_memRead
#print axioms node_step_memWriteBE
#print axioms node_step
#print axioms prefixRelM_snoc
#print axioms objEnv_snoc
#print axioms evalNodesM_agree

end HwMem
end Projection

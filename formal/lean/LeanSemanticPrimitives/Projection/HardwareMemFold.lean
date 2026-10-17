/-
# The node fold over a mixed environment

The invariant the walk carries, and the two facts that re-establish it at each
node.  Nothing here is a hypothesis about kinds or values: the kinds come from
`Direct.DesignSemWF` (the checker's own output) and the values from
`evalGraphG`'s fixpoint property, which `Hw.evalGraphG_rec` already proves.

`slotValM` is the reference slot value WITH memories — a `CertVal`, not
projected through `asBV`, so a memory slot keeps its function and a scalar slot
keeps its bit vector.  `PrefixRelM` says the object's slot list is a typed
witness list for the first `m` slots whose denotation is exactly that, whose
kinds are exactly `Direct.slotIsMem`'s, and whose memory images have the
GEOMETRY the slot assignment `G` gives them.

## A defect this version fixes

An earlier `NodeWriteRange` quantified over every `MemImg` with a given
DENOTATION.  Denotation does not determine table length, so that premise was
strictly stronger than anything the interpreter's own image supplies — and it
was FALSE at the very design this module instantiates:
`MemImg.denote_does_not_determine_geometry` exhibits an image denoting exactly
`zeroImg 5 32` that holds no address at all.  The fold was therefore vacuous
there.  It was also imposed on reads, scalars and DISABLED writes, none of
whose proofs need a range condition.

Both are corrected.  Geometry is carried for the ACTUAL witness (§1b), and the
range obligation is split: `WritePropagates` is a static fact about the slot
assignment that every write needs, while `WriteAddrFits` — the part that
mentions an address at all — is consumed ONLY by an enabled write, and the
disabled branch is proved without it.
-/
import LeanSemanticPrimitives.Projection.HardwareMemNodes
import LeanSemanticPrimitives.Projection.AxiomGate

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

/-! ## 1b. Geometry of the ACTUAL witness image

**Why this exists.**  An earlier version stated the write's range obligation
over every `MemImg` with a given DENOTATION.  That is strictly stronger than
anything the interpreter's own image gives, and it is FALSE: `MemImg.emptyZero`
denotes exactly `zeroImg 5 32` and holds no address at all
(`MemImg.denote_does_not_determine_geometry`).  So the obligation was
unsatisfiable and the fold was vacuous on the very design it was instantiated
at.  Geometry is therefore carried for the ACTUAL witness, through a slot
assignment `G` that the sources initialise and each node preserves.

`G k = some aw` means slot `k` holds a MUTABLE table covering every `aw`-bit
address.  `G k = none` means no mutable-table claim is made: a scalar slot, or
a ROM, whose table is its actual `contents.size` and which therefore may not be
written.  Nothing forces `G` to be total in the useful sense — a design that
writes a slot carrying `none` simply cannot discharge the write. -/

/-- Slot geometry taken from the source descriptors.  A `memImg` source claims
its declared address width; a `memConst` ROM claims nothing. -/
def srcGeomOf (D : DesignCert) (k : Nat) : Option Nat :=
  match D.sources[k]? with
  | some (.memImg _ aw _) => some aw
  | _                     => none

theorem srcGeomOf_some {D : DesignCert} {k aw : Nat} (h : srcGeomOf D k = some aw) :
    ∃ idx dw, D.sources[k]? = some (.memImg idx aw dw) := by
  unfold srcGeomOf at h
  cases hs : D.sources[k]? with
  | none    => rw [hs] at h; simp at h
  | some sd =>
      rw [hs] at h
      cases sd with
      | memImg idx aw' dw' =>
          simp only [Option.some.injEq] at h
          subst h
          exact ⟨idx, dw', rfl⟩
      | _ => simp at h

/-- **The state's images have the geometry the certificate declares.**  Carried
as an explicit premise on the incoming state, not assumed of every image. -/
def StateShapeOK (D : DesignCert) (st : StateM) : Prop :=
  ∀ (idx : Nat) (md : MemoryDesc), D.memories[idx]? = some md →
    ∃ g, st.mems[idx]? = some g ∧ MemImg.FullTable md.aw g

/-- `checkSource` already pins a `memImg` source's `aw`/`dw` to the declared
memory, so the source's width IS the memory's. -/
theorem memDesc_of_sourceOK {D : DesignCert} {k idx aw dw : Nat}
    (hsem : Direct.DesignSemWF D)
    (hsd : D.sources[k]? = some (.memImg idx aw dw)) :
    ∃ md, D.memories[idx]? = some md ∧ md.aw = aw := by
  have h := hsem.sourceOK k _ hsd
  cases hm : D.memories[idx]? with
  | none    =>
      simp only [Direct.checkSource, hm] at h
      split at h <;> simp at h
  | some md =>
      refine ⟨md, rfl, ?_⟩
      simp only [Direct.checkSource, hm] at h
      split at h
      · exact absurd h (by simp)
      · split at h
        · rename_i hcond
          simp only [Bool.and_eq_true, beq_iff_eq] at hcond
          exact hcond.1
        · exact absurd h (by simp)

/-- **Source initialisation of the geometry.** -/
theorem srcGeom_of_StateShape {D : DesignCert} {st : StateM}
    (hsem : Direct.DesignSemWF D) (hstate : StateShapeOK D st)
    {k idx aw dw : Nat} (hsd : D.sources[k]? = some (.memImg idx aw dw)) :
    MemImg.FullTable aw (Hardware.srcMemImg st idx) := by
  obtain ⟨md, hmd, hawm⟩ := memDesc_of_sourceOK hsem hsd
  obtain ⟨g, hg, hfull⟩ := hstate idx md hmd
  have hsm : Hardware.srcMemImg st idx = g := by simp [Hardware.srcMemImg, hg]
  rw [hsm, ← hawm]
  exact hfull

/-! ## 2. The traversal invariant -/

/-- The object's slot list for the first `m` slots, as a TYPED witness list:
its denotation is the reference slot value, its kinds are the checker's, and
each memory slot's ACTUAL image has the geometry `G` assigns it. -/
structure PrefixRelM (D : DesignCert) (i : RuntimeInput) (st : StateM)
    (G : Nat → Option Nat) (m : Nat) (ss : List SlotV) : Prop where
  len  : ss.length = m
  den  : ∀ (k : Nat) (h : k < ss.length), (ss[k]'h).denote = slotValM D i st k
  kind : ∀ (k : Nat) (h : k < ss.length),
           Direct.slotIsMem D k = some (ss[k]'h).isMem
  geom : ∀ (k : Nat) (h : k < ss.length) (g : MemImg), (ss[k]'h) = .img g →
           ∀ aw, G k = some aw → MemImg.FullTable aw g

/-- **The source prefix satisfies it**, with the geometry initialised from the
source descriptors and the incoming state's shape.  A `memConst` ROM is assigned
`none`, so it keeps its actual `contents.size` and is never taken for a mutable
table. -/
theorem prefixRelM_sources {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hsem : Direct.DesignSemWF D) (hstate : StateShapeOK D st)
    (hGsrc : ∀ k, k < D.sources.size → G k = srcGeomOf D k) :
    PrefixRelM D i st G D.sources.size (D.sources.toList.map (srcSlotV i st)) where
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
  geom := by
    intro k h g hg aw hGaw
    have hk : k < D.sources.size := by simpa using h
    have hkl : k < D.sources.toList.length := by simpa using hk
    rw [hGsrc k hk] at hGaw
    obtain ⟨idx, dw, hsd⟩ := srcGeomOf_some hGaw
    have hlist : D.sources.toList[k]'hkl = .memImg idx aw dw := by
      have he : D.sources[k]? = some (D.sources.toList[k]'hkl) := by
        rw [Array.getElem?_eq_getElem hk]; simp
      rw [he] at hsd
      exact Option.some.inj hsd
    rw [List.getElem_map, hlist] at hg
    have hgi : g = Hardware.srcMemImg st idx := by
      simp only [srcSlotV, SlotV.img.injEq] at hg
      exact hg.symm
    rw [hgi]
    exact srcGeom_of_StateShape hsem hstate hsd

/-! ## 3. Reading a dependency out of the invariant

The two consequences the node bridges consume, both derived from the CHECKER's
`nodeKinds` rather than assumed. -/

/-- A dependency the checker calls a bit vector IS one, and it holds the
reference's `asBV`. -/
theorem dep_scalar {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV}
    (hpre : PrefixRelM D i st G m ss)
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
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV}
    (hpre : PrefixRelM D i st G m ss)
    {d : Nat} (hd : d < ss.length)
    (hk : Direct.slotIsMem D d = some true) :
    ∃ img : MemImg,
      (ss[d]'hd) = .img img
      ∧ (ss.map SlotV.enc)[d]'(by simpa using hd) = MemImg.encMemImg img
      ∧ MemImg.denote img = (slotValM D i st d).asMem := by
  have hkind := hpre.kind d hd
  rw [hk] at hkind
  have hb : (ss[d]'hd).isMem = true := (Option.some.inj hkind).symm
  cases hv : ss[d]'hd with
  | bv b  => rw [hv] at hb; simp [SlotV.isMem] at hb
  | img g =>
      refine ⟨g, rfl, ?_, ?_⟩
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
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) {j : Nat} {c : DenseNodeCert}
    (hc : D.nodes[j]? = some c) {ss : List SlotV}
    (hpre : PrefixRelM D i st G (D.sources.size + j) ss) :
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
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (hsup : OpSupported c.op = true) {ss : List SlotV}
    (hpre : PrefixRelM D i st G (D.sources.size + j) ss) :
    ∀ d ∈ c.deps.toList,
      SlotHere (ss.map SlotV.enc) (fun k => (slotValM D i st k).asBV) d := by
  intro d hd
  obtain ⟨p, hp⟩ := dep_at_position hd
  have hkind := dep_kind_of_semwf hsem hc hp
  rw [depIsMem_false_of_supported hsup p] at hkind
  exact ⟨by simpa using deps_bound hwf hc hpre d hd,
         dep_scalar hpre (deps_bound hwf hc hpre d hd) hkind⟩

/-! ## 6. What one admitted node's step delivers

The slot VALUE is produced independently of any evaluation context — the
`SEval` fact is the last, universally quantified conjunct — so the fold can
choose the witness before fixing an environment.  `NodeStepOK` also carries the
GEOMETRY of the new slot, which is what keeps the invariant going. -/

/-- The three facts a step must re-establish, plus the evaluation itself. -/
def NodeStepOK (D : DesignCert) (i : RuntimeInput) (st : StateM)
    (G : Nat → Option Nat) (j : Nat) (c : DenseNodeCert) (ss : List SlotV)
    (v : SlotV) : Prop :=
  v.denote = slotValM D i st (D.sources.size + j)
  ∧ Direct.slotIsMem D (D.sources.size + j) = some v.isMem
  ∧ (∀ g : MemImg, v = .img g →
       ∀ aw, G (D.sources.size + j) = some aw → MemImg.FullTable aw g)
  ∧ ∀ (σ : SEnv) (end_ eenv en : SExp),
      SEval hwMemS σ end_ (encNode c) →
      SEval hwMemS σ eenv (objEnv (ss.map SlotV.enc)) →
      SEval hwMemS σ en (.int (Int.ofNat ss.length)) →
      SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en]) v.enc

theorem node_step_scalar {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) (hj : j < D.nodes.size)
    (hsup : OpSupported c.op = true) (harity : ArityOK c.op c.deps.toList)
    {ss : List SlotV} (hpre : PrefixRelM D i st G (D.sources.size + j) ss) :
    ∃ v : SlotV, NodeStepOK D i st G j c ss v := by
  refine ⟨.bv (eval_op c.op c.width
      (c.deps.toList.map (fun k => (slotValM D i st k).asBV))), ?_, ?_, ?_, ?_⟩
  · have hrec := nodeValM_rec (i := i) (st := st) hwf hj hc
    rw [Hw.evalOpCert_of_supported hsup, List.map_map] at hrec
    exact hrec
  · rw [nodeSlot_slotIsMem hc, opKind_isMem_of_supported hsup]
    rfl
  · intro g hg; exact absurd hg (by simp)
  · intro σ end_ eenv en hnd henv hlen
    exact evalNodeM_scalar hsup harity (deps_scalar hwf hsem hc hsup hpre) hnd henv
      (by simpa using hlen)

/-! ## 7. The two memory nodes' steps

The operand kinds are again the CHECKER's (`nodeKinds` at positions 0..3).  The
ENABLED write's range obligation is derived from the geometry of the image the
interpreter ACTUALLY holds — never from a claim about every image with the same
denotation, which `MemImg.denote_does_not_determine_geometry` shows is false. -/

theorem deps_get (c : DenseNodeCert) {l : List Nat} (h : c.deps.toList = l) (p : Nat) :
    c.deps[p]? = l[p]? := by rw [← h]; simp

/-- A write hands its output slot the geometry of the image it updates.  A
static fact about the slot assignment; nothing about runtime values. -/
def WritePropagates (D : DesignCert) (G : Nat → Option Nat) (j : Nat)
    (c : DenseNodeCert) : Prop :=
  ∀ bw, c.op = .Op_MemWriteBE bw → ∀ d0, c.deps[0]? = some d0 →
    G (D.sources.size + j) = G d0

/-- What an ENABLED byte-enabled write needs, and **only** an enabled write:
the slot it updates is a declared MUTABLE table and the address is no wider
than that table's address width.  Reads, scalars and DISABLED writes do not
consume this and are not required to satisfy it. -/
def WriteAddrFits (D : DesignCert) (i : RuntimeInput) (st : StateM)
    (G : Nat → Option Nat) (c : DenseNodeCert) : Prop :=
  ∀ bw, c.op = .Op_MemWriteBE bw → ∀ d0 d1 d3,
    c.deps[0]? = some d0 → c.deps[1]? = some d1 → c.deps[3]? = some d3 →
    bv_nonzero (slotValM D i st d3).asBV = true →
    ∃ aw, G d0 = some aw ∧ (slotValM D i st d1).asBV.width ≤ aw

theorem node_step_memRead {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) (hj : j < D.nodes.size)
    (hop : c.op = .Op_MemRead)
    {ss : List SlotV} (hpre : PrefixRelM D i st G (D.sources.size + j) ss) :
    ∃ v : SlotV, NodeStepOK D i st G j c ss v := by
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
  obtain ⟨img, hslot0, henc0, hden0⟩ := dep_mem hpre hb0 hk0
  have henc1 := dep_scalar hpre hb1 hk1
  have henc2 := dep_scalar hpre hb2 hk2
  refine ⟨.bv (cert_mem_read c.width (MemImg.denote img)
      (slotValM D i st d1).asBV (slotValM D i st d2).asBV), ?_, ?_, ?_, ?_⟩
  · have hrec := nodeValM_rec (i := i) (st := st) hwf hj hc
    rw [hop, hdeps] at hrec
    rw [← hrec, hden0]
    rfl
  · rw [nodeSlot_slotIsMem hc, hop]; rfl
  · intro g hg; exact absurd hg (by simp)
  · intro σ end_ eenv en hnd henv hlen
    exact evalNodeM_memRead hop hdeps (by simpa using hb0) (by simpa using hb1)
      (by simpa using hb2) henc0 henc1 henc2 hnd henv (by simpa using hlen)

theorem node_step_memWriteBE {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} {bw : Nat}
    (hc : D.nodes[j]? = some c) (hj : j < D.nodes.size)
    (hop : c.op = .Op_MemWriteBE bw)
    (hprop : WritePropagates D G j c) (hfits : WriteAddrFits D i st G c)
    {ss : List SlotV} (hpre : PrefixRelM D i st G (D.sources.size + j) ss) :
    ∃ v : SlotV, NodeStepOK D i st G j c ss v := by
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
  obtain ⟨img, hslot0, henc0, hden0⟩ := dep_mem hpre hb0 hk0
  have henc1 := dep_scalar hpre hb1 hk1
  have henc2 := dep_scalar hpre hb2 hk2
  have henc3 := dep_scalar hpre hb3 hk3
  have hrec := nodeValM_rec (i := i) (st := st) hwf hj hc
  rw [hop, hdeps] at hrec
  have hkindnode : Direct.slotIsMem D (D.sources.size + j) = some true := by
    rw [nodeSlot_slotIsMem hc, hop]; rfl
  have hGout : G (D.sources.size + j) = G d0 := hprop bw hop d0 hp0
  by_cases hen : bv_nonzero (slotValM D i st d3).asBV
  · -- ENABLED: InRange comes from the ACTUAL witness's admitted geometry
    obtain ⟨aw, hG0, hwid⟩ := hfits bw hop d0 d1 d3 hp0 hp1 hp3 hen
    have hfull : MemImg.FullTable aw img := hpre.geom d0 hb0 img hslot0 aw hG0
    have hr : MemImg.InRange img (bv_uint (slotValM D i st d1).asBV) :=
      MemImg.InRange_of_FullTable_le hfull hwid
    refine ⟨.img (MemImg.writeBE c.width img (slotValM D i st d1).asBV
        (slotValM D i st d2).asBV (slotValM D i st d3).asBV bw), ?_, ?_, ?_, ?_⟩
    · rw [← hrec, SlotV.denote, MemImg.denote_writeBE _ _ _ _ _ _ hr, hden0]
      rfl
    · rw [hkindnode]; rfl
    · intro g hg aw' hGaw'
      simp only [SlotV.img.injEq] at hg
      rw [hGout, hG0] at hGaw'
      have : aw' = aw := (Option.some.inj hGaw').symm
      subst this
      rw [← hg]
      exact MemImg.writeBE_FullTable hfull _ _ _ _ _
    · intro σ end_ eenv en hnd henv hlen
      exact evalNodeM_memWriteBE hop hdeps (by simpa using hb0) (by simpa using hb1)
        (by simpa using hb2) (by simpa using hb3) henc0 henc1 henc2 henc3 hen hr
        hnd henv (by simpa using hlen)
  · -- DISABLED: no range premise, and the image is returned EXACTLY
    refine ⟨.img img, ?_, ?_, ?_, ?_⟩
    · rw [← hrec, SlotV.denote, hden0]
      show CertVal.mem (slotValM D i st d0).asMem
         = CertVal.mem (cert_mem_write_be c.width (slotValM D i st d0).asMem
             (slotValM D i st d1).asBV (slotValM D i st d2).asBV
             (slotValM D i st d3).asBV bw)
      simp [cert_mem_write_be, hen]
    · rw [hkindnode]; rfl
    · intro g hg aw' hGaw'
      simp only [SlotV.img.injEq] at hg
      rw [← hg]
      rw [hGout] at hGaw'
      exact hpre.geom d0 hb0 img hslot0 aw' hGaw'
    · intro σ end_ eenv en hnd henv hlen
      exact evalNodeM_memWriteBE_disabled hop hdeps (by simpa using hb0)
        (by simpa using hb1) (by simpa using hb2) (by simpa using hb3)
        henc0 henc1 henc2 henc3 hen hnd henv (by simpa using hlen)

/-! ## 8. One admitted node, whichever kind it is -/

theorem node_step {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) (hj : j < D.nodes.size)
    (hadm : NodeAdmitted c) (harity : ArityOK c.op c.deps.toList)
    (hprop : WritePropagates D G j c) (hfits : WriteAddrFits D i st G c)
    {ss : List SlotV} (hpre : PrefixRelM D i st G (D.sources.size + j) ss) :
    ∃ v : SlotV, NodeStepOK D i st G j c ss v := by
  rcases hadm with hsup | hop | ⟨bw, hop⟩
  · exact node_step_scalar hwf hsem hc hj hsup harity hpre
  · exact node_step_memRead hwf hsem hc hj hop hpre
  · exact node_step_memWriteBE hwf hsem hc hj hop hprop hfits hpre

/-! ## 9. Extending the invariant, and the object's environment step -/

theorem prefixRelM_snoc {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV} {v : SlotV}
    (hpre : PrefixRelM D i st G m ss)
    (hden : v.denote = slotValM D i st m)
    (hkind : Direct.slotIsMem D m = some v.isMem)
    (hgeom : ∀ g : MemImg, v = .img g → ∀ aw, G m = some aw → MemImg.FullTable aw g) :
    PrefixRelM D i st G (m + 1) (ss ++ [v]) := by
  have hl : ss.length = m := hpre.len
  subst hl
  refine ⟨by simp, ?_, ?_, ?_⟩
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
  · intro k h g hg aw hGaw
    by_cases hk : k < ss.length
    · rw [List.getElem_append_left hk] at hg
      exact hpre.geom k hk g hg aw hGaw
    · have hkm : k = ss.length := by
        have hlt : k < ss.length + 1 := by simpa using h
        omega
      subst hkm
      rw [List.getElem_append_right (by omega)] at hg
      simp only [Nat.sub_self] at hg
      exact hgeom g (by simpa using hg) aw hGaw

/-- The object conses the new slot onto the front, which is `objEnv` of the
list with the slot appended -- the same reversal the memory-free route uses. -/
theorem objEnv_snoc (ss : List SlotV) (v : SlotV) :
    objEnv ((ss ++ [v]).map SlotV.enc) = Val.cons v.enc (objEnv (ss.map SlotV.enc)) := by
  simp [objEnv, List.map_append, encListG]

/-! ## 10. The fold -/

/-- The fold, GENERALIZED over the per-node step.

The body never looks inside `node_step`; it only needs "every node can take a
step".  Naming that as a hypothesis lets the ordinary-write route reuse this
proof instead of copying it, and leaves `evalNodesM_agree` below with its
ORIGINAL statement, as a one-line instantiation. -/
theorem evalNodesM_agree_gen {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hstep : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c → j < D.nodes.size →
               c ∈ D.nodes.toList →
               ∀ ss : List SlotV, PrefixRelM D i st G (D.sources.size + j) ss →
                 ∃ v : SlotV, NodeStepOK D i st G j c ss v) :
    ∀ (r j : Nat), j + r = D.nodes.size →
      ∀ (ss : List SlotV), PrefixRelM D i st G (D.sources.size + j) ss →
        ∃ ss' : List SlotV,
          PrefixRelM D i st G (D.sources.size + D.nodes.size) ss'
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
      obtain ⟨v, hvden, hvkind, hvgeom, hv⟩ := hstep j _ hcj hj hmemc ss hpre
      have hpre' : PrefixRelM D i st G (D.sources.size + (j + 1)) (ss ++ [v]) := by
        have hx := prefixRelM_snoc hpre hvden hvkind hvgeom
        simpa [Nat.add_assoc] using hx
      obtain ⟨ss', hss', hrec⟩ :=
        evalNodesM_agree_gen hstep r (j + 1) (by omega) (ss ++ [v]) hpre'
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

/-- **The original fold, statement unchanged**, as an instantiation of the
generalized one at `node_step`. -/
theorem evalNodesM_agree {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hadm : ∀ c ∈ D.nodes.toList, NodeAdmitted c)
    (harity : ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList)
    (hprop : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c →
               WritePropagates D G j c)
    (hfits : ∀ c ∈ D.nodes.toList, WriteAddrFits D i st G c) :
    ∀ (r j : Nat), j + r = D.nodes.size →
      ∀ (ss : List SlotV), PrefixRelM D i st G (D.sources.size + j) ss →
        ∃ ss' : List SlotV,
          PrefixRelM D i st G (D.sources.size + D.nodes.size) ss'
          ∧ ∀ (σ : SEnv) (eno eenv en : SExp),
              SEval hwMemS σ eno (encListG encNode (D.nodes.toList.drop j)) →
              SEval hwMemS σ eenv (objEnv (ss.map SlotV.enc)) →
              SEval hwMemS σ en (.int (Int.ofNat (D.sources.size + j))) →
              SEval hwMemS σ (.call "evalNodesM" [eno, eenv, en])
                (objEnv (ss'.map SlotV.enc)) :=
  evalNodesM_agree_gen (fun j c hcj hj hmemc ss hpre =>
    node_step (i := i) (st := st) hwf hsem hcj hj (hadm _ hmemc) (harity _ hmemc)
      (hprop j _ hcj) (hfits _ hmemc) hpre)

/-! ## 11. Sources then nodes, composed -/

theorem sourcesThenNodes_gen {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hsem : Direct.DesignSemWF D)
    (hstate : StateShapeOK D st)
    (hGsrc : ∀ k, k < D.sources.size → G k = srcGeomOf D k)
    (hstep : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c → j < D.nodes.size →
               c ∈ D.nodes.toList →
               ∀ ss : List SlotV, PrefixRelM D i st G (D.sources.size + j) ss →
                 ∃ v : SlotV, NodeStepOK D i st G j c ss v) :
    ∃ ss' : List SlotV,
      PrefixRelM D i st G (D.sources.size + D.nodes.size) ss'
      ∧ ∀ (σ : SEnv) (eno eenv en : SExp),
          SEval hwMemS σ eno (encListG encNode D.nodes.toList) →
          SEval hwMemS σ eenv
            (objEnv ((D.sources.toList.map (srcSlotV i st)).map SlotV.enc)) →
          SEval hwMemS σ en (.int (Int.ofNat D.sources.size)) →
          SEval hwMemS σ (.call "evalNodesM" [eno, eenv, en])
            (objEnv (ss'.map SlotV.enc)) := by
  obtain ⟨ss', hss', hrec⟩ :=
    evalNodesM_agree_gen hstep D.nodes.size 0 (by omega)
      (D.sources.toList.map (srcSlotV i st))
      (by simpa using prefixRelM_sources (G := G) hsem hstate hGsrc)
  refine ⟨ss', hss', ?_⟩
  intro σ eno eenv en hno henv hlen
  exact hrec σ eno eenv en (by simpa using hno) henv (by simpa using hlen)
/-- **The original composition, statement unchanged.** -/
theorem sourcesThenNodes {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hstate : StateShapeOK D st)
    (hGsrc : ∀ k, k < D.sources.size → G k = srcGeomOf D k)
    (hadm : ∀ c ∈ D.nodes.toList, NodeAdmitted c)
    (harity : ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList)
    (hprop : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c →
               WritePropagates D G j c)
    (hfits : ∀ c ∈ D.nodes.toList, WriteAddrFits D i st G c) :
    ∃ ss' : List SlotV,
      PrefixRelM D i st G (D.sources.size + D.nodes.size) ss'
      ∧ ∀ (σ : SEnv) (eno eenv en : SExp),
          SEval hwMemS σ eno (encListG encNode D.nodes.toList) →
          SEval hwMemS σ eenv
            (objEnv ((D.sources.toList.map (srcSlotV i st)).map SlotV.enc)) →
          SEval hwMemS σ en (.int (Int.ofNat D.sources.size)) →
          SEval hwMemS σ (.call "evalNodesM" [eno, eenv, en])
            (objEnv (ss'.map SlotV.enc)) :=
  sourcesThenNodes_gen hsem hstate hGsrc (fun j c hcj hj hmemc ss hpre =>
    node_step (i := i) (st := st) hwf hsem hcj hj (hadm _ hmemc) (harity _ hmemc)
      (hprop j _ hcj) (hfits _ hmemc) hpre)

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

#audit_axioms mixedDesign_semwf
#audit_axioms mixedDesign_admitted
#audit_axioms mixedDesign_arity
#audit_axioms sourcesThenNodes

/-! ## 13. Crossed to the resolved program

Through the production bridge, as in the source module: the argument list is
literals, so the resolved term is pinned here and no `resolve` hypothesis is
left to the caller. -/

/-- `evalNodesM` sits where `resolveProgram` put it — the same id the executed
checks call. -/
theorem evalNodesM_id : idOf "evalNodesM" = some 47 := rfl

theorem sourcesThenNodes_resolved {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hstate : StateShapeOK D st)
    (hGsrc : ∀ k, k < D.sources.size → G k = srcGeomOf D k)
    (hadm : ∀ c ∈ D.nodes.toList, NodeAdmitted c)
    (harity : ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList)
    (hprop : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c →
               WritePropagates D G j c)
    (hfits : ∀ c ∈ D.nodes.toList, WriteAddrFits D i st G c) :
    ∃ ss' : List SlotV,
      PrefixRelM D i st G (D.sources.size + D.nodes.size) ss'
      ∧ Eval hwMemP []
          (.call 47
             ([ encListG encNode D.nodes.toList
              , objEnv ((D.sources.toList.map (srcSlotV i st)).map SlotV.enc)
              , Val.int (Int.ofNat D.sources.size) ].map Term.lit))
          (objEnv (ss'.map SlotV.enc)) := by
  obtain ⟨ss', hss', hrec⟩ :=
    sourcesThenNodes hwf hsem hstate hGsrc hadm harity hprop hfits
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

/-! ## 14. The fold, INSTANTIATED on the checker-accepted design

Checker acceptance alone does not instantiate a theorem, so the fold is applied
here: a full Pico-shaped mutable table, the geometry it initialises, and the
resolved `Eval` on `hwMemP`.  Both an all-zero state and an INITIALISED one are
covered, and the executed controls below show the enabled write actually
changing a word and the disabled write leaving the image exactly alone. -/

/-- `DesignSemWF` already carries both fields `DesignCertWF` names. -/
theorem designCertWF_of_semwf {D : DesignCert} (h : Direct.DesignSemWF D) :
    DesignCert.DesignCertWF D := ⟨h.depsBounded, h.slotsInRange⟩

/-- A full Pico-shaped mutable table: 32 words of 32 bits, all zero. -/
def zeroState : StateM := { flops := #[], mems := #[MemImg.zeroImg 5 32] }

/-- …and an INITIALISED one: word `k` holds `k + 1`, so a read returns
something the scalar node can actually change. -/
def initImg : MemImg :=
  ⟨(List.range 32).map (fun k => mk_bv 32 (Int.ofNat k + 1)), mk_bv 32 0⟩

def initState : StateM := { flops := #[], mems := #[initImg] }

theorem initImg_full : MemImg.FullTable 5 initImg := by
  simp [MemImg.FullTable, initImg]

theorem zeroState_shape : StateShapeOK mixedDesign zeroState := by
  intro idx md hmd
  cases idx with
  | zero =>
      have hmd' : ({ aw := 5, dw := 32, nextImg := 7, clock := 0 } : MemoryDesc) = md := by
        simpa [mixedDesign] using hmd
      subst hmd'
      exact ⟨MemImg.zeroImg 5 32, rfl, MemImg.zeroImg_FullTable 5 32⟩
  | succ n => simp [mixedDesign] at hmd

theorem initState_shape : StateShapeOK mixedDesign initState := by
  intro idx md hmd
  cases idx with
  | zero =>
      have hmd' : ({ aw := 5, dw := 32, nextImg := 7, clock := 0 } : MemoryDesc) = md := by
        simpa [mixedDesign] using hmd
      subst hmd'
      exact ⟨initImg, rfl, initImg_full⟩
  | succ n => simp [mixedDesign] at hmd

/-- The slot geometry: slot 0 is the declared mutable table and the write at
node 2 hands slot 7 the same geometry.  Every other slot claims NONE, so a
write to one of them could not be discharged. -/
def mixedG : Nat → Option Nat
  | 0 => some 5
  | 7 => some 5
  | _ => none

theorem mixedG_src :
    ∀ k, k < mixedDesign.sources.size → mixedG k = srcGeomOf mixedDesign k := by
  intro k hk
  have h5 : k < 5 := by simpa [mixedDesign] using hk
  rcases k with _|_|_|_|_|k
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · omega

theorem mixedG_prop : ∀ (j : Nat) (c : DenseNodeCert),
    mixedDesign.nodes[j]? = some c → WritePropagates mixedDesign mixedG j c := by
  intro j c hc bw hop d0 hd0
  rcases j with _|_|_|j
  · have hc' : ({ op := .Op_MemRead, width := 32, deps := #[0, 1, 3] } : DenseNodeCert) = c := by
      simpa [mixedDesign] using hc
    subst hc'; simp at hop
  · have hc' : ({ op := .Op_And, width := 32, deps := #[5, 4] } : DenseNodeCert) = c := by
      simpa [mixedDesign] using hc
    subst hc'; simp at hop
  · have hc' : ({ op := .Op_MemWriteBE 1, width := 32, deps := #[0, 1, 6, 2] } : DenseNodeCert) = c := by
      simpa [mixedDesign] using hc
    subst hc'
    have h0 : (0 : Nat) = d0 := by simpa using hd0
    subst h0
    rfl
  · simp [mixedDesign] at hc

/-- The address operand is `input 0` at width 5, so its runtime width is 5 for
EVERY input and state — nothing about the memory is assumed here. -/
theorem mixed_addr_width (i : RuntimeInput) (st : StateM) :
    (slotValM mixedDesign i st 1).asBV.width = 5 := by
  rw [slotValM_source (D := mixedDesign) (i := i) (st := st)
        (k := 1) (by simp [mixedDesign])]
  simp [mixedDesign, sourceValue, CertVal.asBV, bv_resize, mk_bv]

theorem mixedG_fits (i : RuntimeInput) (st : StateM) :
    ∀ c ∈ mixedDesign.nodes.toList, WriteAddrFits mixedDesign i st mixedG c := by
  intro c hc bw hop d0 d1 d3 hd0 hd1 hd3 _hen
  have hc3 : c = ({ op := .Op_MemRead, width := 32, deps := #[0, 1, 3] } : DenseNodeCert)
           ∨ c = ({ op := .Op_And, width := 32, deps := #[5, 4] } : DenseNodeCert)
           ∨ c = ({ op := .Op_MemWriteBE 1, width := 32, deps := #[0, 1, 6, 2] } : DenseNodeCert) := by
    simpa [mixedDesign] using hc
  rcases hc3 with h | h | h <;> subst h
  · simp at hop
  · simp at hop
  · have h0 : (0 : Nat) = d0 := by simpa using hd0
    have h1 : (1 : Nat) = d1 := by simpa using hd1
    subst h0; subst h1
    exact ⟨5, rfl, by simp [mixed_addr_width]⟩

/-- **The corrected fold, instantiated.**  Every premise is discharged: the
checker accepts the design, the state carries the declared geometry, the
geometry assignment agrees with the sources and propagates through the write,
and the enabled write's address fits the admitted table. -/
theorem mixed_fold_resolved (i : RuntimeInput) (st : StateM)
    (hst : StateShapeOK mixedDesign st) :
    ∃ ss' : List SlotV,
      PrefixRelM mixedDesign i st mixedG
        (mixedDesign.sources.size + mixedDesign.nodes.size) ss'
      ∧ Eval hwMemP []
          (.call 47
             ([ encListG encNode mixedDesign.nodes.toList
              , objEnv ((mixedDesign.sources.toList.map (srcSlotV i st)).map SlotV.enc)
              , Val.int (Int.ofNat mixedDesign.sources.size) ].map Term.lit))
          (objEnv (ss'.map SlotV.enc)) :=
  sourcesThenNodes_resolved (designCertWF_of_semwf mixedDesign_semwf)
    mixedDesign_semwf hst mixedG_src mixedDesign_admitted mixedDesign_arity
    mixedG_prop (mixedG_fits i st)

/-- …at the all-zero Pico-shaped state, which is the one the earlier broken
premise could not be satisfied at. -/
theorem mixed_fold_resolved_zero (i : RuntimeInput) :
    ∃ ss' : List SlotV,
      PrefixRelM mixedDesign i zeroState mixedG
        (mixedDesign.sources.size + mixedDesign.nodes.size) ss'
      ∧ Eval hwMemP []
          (.call 47
             ([ encListG encNode mixedDesign.nodes.toList
              , objEnv ((mixedDesign.sources.toList.map (srcSlotV i zeroState)).map SlotV.enc)
              , Val.int (Int.ofNat mixedDesign.sources.size) ].map Term.lit))
          (objEnv (ss'.map SlotV.enc)) :=
  mixed_fold_resolved i zeroState zeroState_shape

theorem mixed_fold_resolved_init (i : RuntimeInput) :
    ∃ ss' : List SlotV,
      PrefixRelM mixedDesign i initState mixedG
        (mixedDesign.sources.size + mixedDesign.nodes.size) ss'
      ∧ Eval hwMemP []
          (.call 47
             ([ encListG encNode mixedDesign.nodes.toList
              , objEnv ((mixedDesign.sources.toList.map (srcSlotV i initState)).map SlotV.enc)
              , Val.int (Int.ofNat mixedDesign.sources.size) ].map Term.lit))
          (objEnv (ss'.map SlotV.enc)) :=
  mixed_fold_resolved i initState initState_shape

/-! ### Executed controls: the write really changes a word, and really does not

Inputs, in source order: address (`input 0`, 5 bits), byte-enable mask
(`input 1`, 32 bits), read enable (`input 2`, 1 bit), scalar operand
(`input 3`, 32 bits).  With address 20 the read returns word 21, the scalar
node ANDs it with `0xF` giving 5, and the enabled write stores 5 — so the word
at 20 changes from 21 to 5. -/

private def mixedRun (st : StateM) (i : RuntimeInput) : EvalResult :=
  evalFuel 200000 hwMemP []
    (.call ((idOf "evalNodesM").getD 999)
       [ .lit (encListG encNode mixedDesign.nodes.toList)
       , .lit (objEnv ((mixedDesign.sources.toList.map (srcSlotV i st)).map SlotV.enc))
       , .lit (.int 5) ])

private def inEnabled  : RuntimeInput :=
  #[mk_bv 5 20, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 32 0xF]
private def inDisabled : RuntimeInput :=
  #[mk_bv 5 20, mk_bv 32 0,          mk_bv 1 1, mk_bv 32 0xF]

/- the initialised image really does hold 21 at address 20 beforehand -/
#guard initImg.get 20 == mk_bv 32 21

/- ENABLED: the newest slot is the written image, the word at 20 is now 5, and
   the table is still a full 32-word 2^5 table -/
#guard (match mixedRun initState inEnabled with
        | .value (.cons h _) =>
            (match MemImg.decMemImg h with
             | some g => g.get 20 == mk_bv 32 5 && g.words.length == 32
             | none   => false)
        | _ => false)

/- …and nothing else moved: address 19 still holds 20 -/
#guard (match mixedRun initState inEnabled with
        | .value (.cons h _) =>
            (match MemImg.decMemImg h with
             | some g => g.get 19 == mk_bv 32 20
             | none   => false)
        | _ => false)

/- DISABLED: a zero byte-enable leaves the image EXACTLY as it was -/
#guard (match mixedRun initState inDisabled with
        | .value (.cons h _) =>
            (match MemImg.decMemImg h with
             | some g => g == initImg
             | none   => false)
        | _ => false)

/- the all-zero state runs too, and its write stores 0 into a zero table -/
#guard (match mixedRun zeroState inEnabled with
        | .value (.cons h _) =>
            (match MemImg.decMemImg h with
             | some g => g.get 20 == mk_bv 32 0 && g.words.length == 32
             | none   => false)
        | _ => false)

#audit_axioms designCertWF_of_semwf
#audit_axioms zeroState_shape
#audit_axioms initState_shape
#audit_axioms mixedG_src
#audit_axioms mixedG_prop
#audit_axioms mixed_addr_width
#audit_axioms mixedG_fits
#audit_axioms mixed_fold_resolved
#audit_axioms mixed_fold_resolved_zero
#audit_axioms mixed_fold_resolved_init
#audit_axioms evalNodesM_id
#audit_axioms sourcesThenNodes_resolved
#audit_axioms slotValM_source
#audit_axioms nodeValM_rec
#audit_axioms prefixRelM_sources
#audit_axioms dep_scalar
#audit_axioms dep_mem
#audit_axioms dep_kind_of_semwf
#audit_axioms opArity_of_semwf
#audit_axioms memRead_deps
#audit_axioms memWriteBE_deps
#audit_axioms deps_scalar
#audit_axioms node_step_scalar
#audit_axioms node_step_memRead
#audit_axioms node_step_memWriteBE
#audit_axioms node_step
#audit_axioms prefixRelM_snoc
#audit_axioms objEnv_snoc
#audit_axioms evalNodesM_agree

end HwMem
end Projection

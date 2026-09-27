import LeanSemanticPrimitives.Translation.OpBridge

/- Small symbolic composition lemmas for the optional legacy WF emitter.
   Concrete checks are local shapes, indexed dependencies, and dense-slot maps;
   no chunk evaluates nodeCertChunkWfBool over a global allIds list. -/
deriving instance DecidableEq for NodeCert

namespace LegacyCertWF

/- The enumeration is checked once per graph. Each dependency check uses a
   dense array access, rather than membership in either complete graph list.
   A bad/missing slot cannot validate an ID: the array entry must match too. -/
def depsIndexed (ids : Array Nat) (slot : Nat → Nat) (ds : List Nat) : Bool :=
  ds.all fun d => ids[slot d]? == some d

theorem deps_indexed (G : GraphCert) (ids : Array Nat) (slot : Nat → Nat)
    (enumeration : ids.toList = G.sources ++ G.topo) (ds : List Nat)
    (checked : depsIndexed ids slot ds = true) :
    ∀ d ∈ ds, d ∈ G.topo ∨ d ∈ G.sources := by
  intro d hd
  have h : ids[slot d]? = some d := by
    simpa only [beq_iff_eq] using List.all_eq_true.mp checked d hd
  have hm := Array.mem_toList_iff.mpr (Array.mem_of_getElem? h)
  rw [enumeration] at hm
  exact (List.mem_append.mp hm).symm

def ChunkWf (G : GraphCert) (ids : List Nat) : Prop :=
  ∀ n ∈ ids, match G.nodes n with
    | none => False
    | some c => c.nid = n ∧ c.width > 0 ∧ ∀ d ∈ c.deps, d ∈ G.topo ∨ d ∈ G.sources

theorem const_shape (cs : List NodeCert)
    (h : cs.all constNodeCertWfBool = true) :
    ∀ c ∈ cs, c.width > 0 ∧ c.deps = [] := by
  intro c hc
  have h := List.all_eq_true.mp h c hc
  unfold constNodeCertWfBool at h
  split at h <;> simp_all

theorem simple_shape (cs : List NodeCert)
    (h : cs.all simpleNodeCertShapeWfBool = true) : ∀ c ∈ cs, c.width > 0 := by
  intro c hc
  have h := List.all_eq_true.mp h c hc
  simp only [simpleNodeCertShapeWfBool, Bool.and_eq_true, decide_eq_true_eq] at h
  exact h.1

theorem chunk_of_shape (G : GraphCert) (cs : List NodeCert)
    (lookup : ∀ c ∈ cs, G.nodes c.nid = some c)
    (shape : ∀ c ∈ cs, c.width > 0)
    (deps : ∀ d ∈ nodeCertDeps cs, d ∈ G.topo ∨ d ∈ G.sources) :
    ChunkWf G (cs.map NodeCert.nid) := by
  intro n hn
  obtain ⟨c, hc, rfl⟩ := List.mem_map.mp hn
  rw [lookup c hc]
  refine ⟨rfl, shape c hc, ?_⟩
  intro d hd
  apply deps d
  exact List.mem_flatten.mpr ⟨c.deps, List.mem_map.mpr ⟨c, hc, rfl⟩, hd⟩

theorem chunk_of_constants (G : GraphCert) (cs : List NodeCert)
    (lookup : ∀ c ∈ cs, G.nodes c.nid = some c)
    (shape : cs.all constNodeCertWfBool = true) : ChunkWf G (cs.map NodeCert.nid) := by
  apply chunk_of_shape G cs lookup (fun c hc => (const_shape cs shape c hc).1)
  intro d hd
  obtain ⟨ds, hds, hd⟩ := List.mem_flatten.mp hd
  obtain ⟨c, hc, rfl⟩ := List.mem_map.mp hds
  simp [(const_shape cs shape c hc).2] at hd

theorem chunk_append (G : GraphCert) (a b : List Nat)
    (ha : ChunkWf G a) (hb : ChunkWf G b) : ChunkWf G (a ++ b) := by
  intro n hn
  rcases List.mem_append.mp hn with hn | hn
  · exact ha n hn
  · exact hb n hn

theorem dense_nodup (ids : List Nat) (slot : Nat → Nat)
    (h : ids.map slot = List.range ids.length) : ids.Nodup := by
  have hm : (ids.map slot).Nodup := by rw [h]; exact List.nodup_range
  exact List.Nodup.of_map slot hm

theorem graph_of_chunks (G : GraphCert)
    (unique : (G.sources ++ G.topo).Nodup)
    (nodes : ChunkWf G G.topo)
    (sources : ∀ n ∈ G.sources, G.nodes n = none) : graphCertWf G := by
  obtain ⟨hs, ht, hd⟩ := List.nodup_append.mp unique
  exact ⟨ht, hs, fun n hn hns => hd n hns n hn rfl, nodes, sources⟩
end LegacyCertWF

import LeanSemanticPrimitives.Compiler.CompileDesign

namespace Compiler
namespace Seq

/-- `firstBadDep` as a SEQUENTIAL walk of the node list (no `nodes[i]?`). -/
def firstBadDepL (S : Nat) : Nat → List DenseNodeCert → Option (Nat × Nat)
  | _, [] => none
  | i, c :: cs =>
    match (c.deps.toList.find? fun d => decide ¬(d < S + i)) with
    | some d => some (S + i, d)
    | none   => firstBadDepL S (i + 1) cs

/-- `compileFrom` as a sequential walk. -/
def compileL (S : Nat) : Nat → List DenseNodeCert → Array ResidualBinding →
    Except CompileError (Array ResidualBinding)
  | _, [], acc => .ok acc
  | i, c :: cs, acc =>
    match compileOp (S + i) c with
    | .error err => .error err
    | .ok e      => compileL S (i + 1) cs (acc.push { ty := opValueType c, rhs := e })

def okL (D : DesignCert) : Bool :=
  match firstBadDepL D.sources.size 0 D.nodes.toList with
  | some _ => false
  | none =>
    match compileL D.sources.size 0 D.nodes.toList #[] with
    | .ok _ => true
    | .error _ => false

theorem slotsFrom_succ (b k : Nat) :
    DesignCert.slotsFrom b (k + 1) = b :: DesignCert.slotsFrom (b + 1) k := rfl

theorem firstBadDep_walk (D : DesignCert) :
    ∀ (l : List DenseNodeCert) (i : Nat), (∀ j, D.nodes[i + j]? = l[j]?) →
    (DesignCert.slotsFrom i l.length).findSome? (fun i =>
        match D.nodes[i]? with
        | none   => none
        | some c => (c.deps.toList.find? fun d => decide ¬(d < D.sources.size + i)).map
                      fun d => (D.slotOfNode i, d))
      = firstBadDepL D.sources.size i l := by
  intro l
  induction l with
  | nil => intro i _; rfl
  | cons c cs ih =>
    intro i h
    have hc : D.nodes[i]? = some c := by simpa using h 0
    have htl : ∀ j, D.nodes[(i + 1) + j]? = cs[j]? := by
      intro j; have := h (j + 1); simpa [Nat.add_assoc, Nat.add_comm 1 j] using this
    rw [List.length_cons, slotsFrom_succ, List.findSome?_cons, hc]
    simp only [firstBadDepL, DesignCert.slotOfNode]
    cases hf : (c.deps.toList.find? fun d => decide ¬(d < D.sources.size + i)) with
    | some d => rfl
    | none   => simp only [Option.map_none]; exact ih (i + 1) htl

theorem compileFrom_walk (D : DesignCert) :
    ∀ (l : List DenseNodeCert) (i : Nat) (acc : Array ResidualBinding),
    (∀ j, D.nodes[i + j]? = l[j]?) →
    compileFrom D i l.length acc = compileL D.sources.size i l acc := by
  intro l
  induction l with
  | nil => intro i acc _; rfl
  | cons c cs ih =>
    intro i acc h
    have hc : D.nodes[i]? = some c := by simpa using h 0
    have htl : ∀ j, D.nodes[(i + 1) + j]? = cs[j]? := by
      intro j; have := h (j + 1); simpa [Nat.add_assoc, Nat.add_comm 1 j] using this
    rw [List.length_cons]
    simp only [compileFrom, hc, compileL, DesignCert.slotOfNode]
    cases compileOp (D.sources.size + i) c with
    | error e => rfl
    | ok e    => exact ih (i + 1) _ htl

theorem firstBadDep_eq (D : DesignCert) :
    firstBadDep D = firstBadDepL D.sources.size 0 D.nodes.toList := by
  have h0 : ∀ j, D.nodes[0 + j]? = D.nodes.toList[j]? := by intro j; simp
  have hb := firstBadDep_walk D D.nodes.toList 0 h0
  simp only [Array.length_toList] at hb
  unfold firstBadDep
  exact hb

theorem compileGraph_eq_compileL (D : DesignCert) :
    compileGraph D = compileL D.sources.size 0 D.nodes.toList #[] := by
  have h0 : ∀ j, D.nodes[0 + j]? = D.nodes.toList[j]? := by intro j; simp
  have hg := compileFrom_walk D D.nodes.toList 0 #[] h0
  simp only [Array.length_toList] at hg
  exact hg

/-- `compilesOk` computed by two SEQUENTIAL walks of the node list. The kernel
reads `D.nodes[i]?` by walking the list literal from the start, so the
index-based definition costs O(N^2) to evaluate; this form costs O(N). -/
theorem compilesOk_eq_okL (D : DesignCert) : compilesOk D = okL D := by
  unfold compilesOk okL compileDesign
  rw [firstBadDep_eq, compileGraph_eq_compileL]
  cases firstBadDepL D.sources.size 0 D.nodes.toList with
  | some p => rfl
  | none =>
    cases compileL D.sources.size 0 D.nodes.toList #[] <;> rfl

end Seq
end Compiler


namespace Compiler
namespace Seq

/-- `compileDesign` with both passes SEQUENTIAL (`firstBadDepL`, `compileL`). -/
def compileDesignL (D : DesignCert) : Except CompileError ResidualProgram :=
  match firstBadDepL D.sources.size 0 D.nodes.toList with
  | some (n, d) => .error (.depNotEarlier n d)
  | none =>
    match compileL D.sources.size 0 D.nodes.toList #[] with
    | .error e => .error e
    | .ok bs =>
      .ok { sources       := D.sources
            bindings      := bs
            outputs       := D.outputs.map compileOutput
            flopUpdates   := D.flops.map compileFlop
            memoryUpdates := D.memories.map compileMemory }

theorem compileDesign_eq_L (D : DesignCert) : compileDesign D = compileDesignL D := by
  unfold compileDesign compileDesignL
  rw [firstBadDep_eq, compileGraph_eq_compileL]
  rfl

/-- The witness `prove_reified*` needs, with the residual named through the
SEQUENTIAL compiler, so checking it never evaluates the index-based one. -/
theorem compileDesignL_ok_witness (D : DesignCert) (h : okL D = true) :
    compileDesign D = .ok (match compileDesignL D with | .ok R => R | .error _ => default) := by
  rw [compileDesign_eq_L]
  unfold okL at h
  unfold compileDesignL
  cases hb : firstBadDepL D.sources.size 0 D.nodes.toList with
  | some p => rw [hb] at h; exact absurd h (by simp)
  | none =>
    rw [hb] at h
    cases hc : compileL D.sources.size 0 D.nodes.toList #[] with
    | error e => rw [hc] at h; exact absurd h (by simp)
    | ok bs => rfl

end Seq
end Compiler

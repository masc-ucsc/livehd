/-
# Balanced lookup trees for graph environments

`OpBridge.BT` and its lookup, moved here from `OpBridge` unchanged -- same names,
same namespace -- so a model that does not import Mathlib can use them.  The
legacy certificate part of a design above 2,000 sources or nodes looks its
sources and nodes up in these trees whether or not the model carries bridge
proofs, and a model without bridge proofs does not import `OpBridge` (it pulls
in Mathlib): SoomRV's BranchPredictionTable stopped on "Unknown identifier
`BT.find`".  `OpBridge` imports this module, so every existing use is the same.
-/

namespace OpBridge

--------------------------------------------------------------------------------
-- Balanced-lookup graph environments (step-5 scale).
--
-- A per-node bridge proof must kernel-reduce the shared lookups (phi, sourceEnv,
-- graphCert.nodes).  Spelled as a flat `if n = k then .. else ..` chain (or
-- List.find?), locating node k scans O(N) entries, so N nodes cost O(N^2) -- for
-- a 4772-node design that is tens of hours and ~100 GB.  A balanced `if`-TREE
-- does NOT help: reducing it beta-substitutes the key across the whole N-node
-- term, which is O(N) regardless of depth.
--
-- The fix is an inductive tree DATA structure plus a recursive `find`: `find`
-- navigates the tree VALUE, comparing the key locally at each visited node, so a
-- lookup is O(log N) with no global substitution -- O(N) overall (measured
-- N^1.0, bounded memory).  Emit the values as input-independent closures
-- (`fun i s => ..`) so building the tree never substitutes the design inputs
-- into N positions either.
--------------------------------------------------------------------------------

/-- Binary search tree data for graph lookups (keys are node ids). -/
inductive BT (α : Type) where
  | lf
  | nd (key : Nat) (val : α) (lo hi : BT α)

/-- O(log N) lookup that navigates the tree value (no global substitution). -/
def BT.find {α : Type} : BT α → Nat → Option α
  | .lf, _ => none
  | .nd k v lo hi, n => if n < k then BT.find lo n else if n = k then some v else BT.find hi n

/-- The keys of a tree; note it discards the values, so reducing `keys` never
forces the (potentially expensive) value closures. -/
def BT.keys {α : Type} : BT α → List Nat
  | .lf => []
  | .nd k _ lo hi => k :: (BT.keys lo ++ BT.keys hi)

/-- Off-tree keys look up to `none`.  Used by `<Top>_bridge_src` to show phi
agrees with the source env on every dependency outside the topo list. -/
theorem BT.find_eq_none {α : Type} : ∀ (t : BT α) (d : Nat), d ∉ BT.keys t → BT.find t d = none
  | .lf, d, _ => rfl
  | .nd k v lo hi, d, h => by
      simp only [BT.keys, List.mem_cons, List.mem_append, not_or] at h
      obtain ⟨hk, hlo, hhi⟩ := h
      show (if d < k then BT.find lo d else if d = k then some v else BT.find hi d) = none
      by_cases h1 : d < k
      · simp only [if_pos h1]; exact BT.find_eq_none lo d hlo
      · simp only [if_neg h1, if_neg hk]; exact BT.find_eq_none hi d hhi

end OpBridge

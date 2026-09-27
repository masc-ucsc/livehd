import LeanSemanticPrimitives.Translation.LegacyCertWF

/- Sparse IDs need not equal their ranks. An in-bounds rank alone is
   insufficient: it must resolve to the same ID. Missing ranks and ranks
   outside the array must both be rejected. -/
open LegacyCertWF

private def ids : Array Nat := #[2000000000, 7, 900000000]
private def slot (id : Nat) : Nat :=
  if id = 2000000000 then 0 else if id = 7 then 1 else if id = 900000000 then 2 else ids.size

example : depsIndexed ids slot [7, 2000000000, 7, 900000000] = true := by decide
example : depsIndexed ids slot [8] = false := by decide
example : depsIndexed ids (fun _ => 0) [7] = false := by decide
example : depsIndexed ids (fun _ => ids.size) [7] = false := by decide
example : depsIndexed #[] (fun _ => 0) [0] = false := by decide
example : depsIndexed #[] (fun _ => 0) [] = true := by decide

private def graph : GraphCert :=
  { sources := [2000000000], topo := [7, 900000000], nodes := fun _ => none }

-- The executable guard supplies exactly the dependency-membership obligation.
example : ∀ d ∈ [7, 2000000000, 900000000], d ∈ graph.topo ∨ d ∈ graph.sources :=
  deps_indexed graph ids slot (by decide) _ (by decide)

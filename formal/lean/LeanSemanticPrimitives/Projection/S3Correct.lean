/-
# S3 computes exactly what the proved specializer computes

`Projection/PartialEvaluatorS3.lean` is the proved specializer with deferred
binder shifting.  This file proves

    mixDriver_eq_S3 : @Projection.mixDriver = @S3.mixDriver

-- equal on EVERY input: the same residual program, the same error, the same
fuel behaviour -- and registers it `@[csimp]`.  Every theorem in the library
stays about the proved `mixDriver` (`mixDriver_iff`, `projectDesign_correct`,
...); compiled code that calls it runs `S3.mixDriver` instead, which removes the
shift quadratic (certio/PHASE6_PERF.md 39).

The proof is a simulation.  `rep` reads an S3 partial value as the proved one
it stands for -- `rep (.shifted k v) = PVal.shift k (rep v)`, and an array
spine `rep (.dyns cs t)` is the cons chain of its chunks' leaves onto `rep t`
-- and `repR` does the same for results.  Every helper S3 changed commutes with
`rep`/`repR` (for the array spine: `rep_mkCons`, `walkHeadAt_rep`,
`rep_hnfAt`, `toCodeAt_rep`, `toPResAt_rep`, over the leaf lemmas of section
1b), and `mixTerm` & co. are the proved code over those helpers, so the
specializers agree step for step.  No invariant is involved: every chunk is a
valid chunk, so the simulation holds for EVERY S3 environment.

DEPENDENCIES ARE KEPT LIGHT ON PURPOSE: `@[csimp]` only rewrites callers that
are compiled with this file in scope, so the modules that call `mixDriver`
(`ProjectionCorrect`, `MemProjection`, ...) must be able to import it.  Hence
no import of `PartialEvaluatorCorrect`; the one lemma needed from it,
additivity of `PVal.shift`, is re-proved here.
-/
import LeanSemanticPrimitives.Projection.PartialEvaluator
import LeanSemanticPrimitives.Projection.PartialEvaluatorS3

namespace Projection
namespace S3Correct

/-! ## 1. Representation -/

/-- The leaves an array chunk stands for, offset applied. -/
def chunkElems (c : S3.DChunk) : List Nat := (c.arr.toList.drop c.start).map (· + c.sh)

/-- The leaves of a chunk list, in order. -/
def chunksElems : List S3.DChunk → List Nat
  | []      => []
  | c :: cs => chunkElems c ++ chunksElems cs

/-- Dynamic leaves consed onto a spine. -/
def consDyns : List Nat → PVal → PVal
  | [],      x => x
  | y :: ys, x => .cons (.dyn y) (consDyns ys x)

/-- What an S3 partial value stands for.  An array spine is the cons chain of
its leaves: nothing about a chunk is assumed, so `rep` is total and no
invariant has to be threaded through the simulation. -/
def rep : S3.PVal → PVal
  | .stat v      => .stat v
  | .dyn i       => .dyn i
  | .cons a b    => .cons (rep a) (rep b)
  | .shifted k v => PVal.shift k (rep v)
  | .dyns cs t   => consDyns (chunksElems cs) (rep t)

/-- What an S3 result stands for: the same constructor, with `rep` on the
partial values inside. -/
def repR : S3.PRes → PRes
  | .stat v    => .stat v
  | .code t    => .code t
  | .cons a b  => .cons (repR a) (repR b)
  | .lets bs r => .lets bs (repR r)
  | .val v     => .val (rep v)

theorem shift_add : ∀ (a b : Nat) (v : PVal),
    PVal.shift a (PVal.shift b v) = PVal.shift (b + a) v
  | _, _, .stat _   => rfl
  | a, b, .dyn i    => by simp [PVal.shift]; omega
  | a, b, .cons x y => by simp [PVal.shift, shift_add a b x, shift_add a b y]

theorem shift_zero : ∀ v : PVal, PVal.shift 0 v = v
  | .stat _   => rfl
  | .dyn _    => by simp [PVal.shift]
  | .cons a b => by simp [PVal.shift, shift_zero a, shift_zero b]

/-! ## 1b. The array spine: its leaves, chunk by chunk -/

theorem shift_consDyns (k : Nat) : ∀ (ys : List Nat) (x : PVal),
    PVal.shift k (consDyns ys x) = consDyns (ys.map (· + k)) (PVal.shift k x)
  | [],      _ => rfl
  | _ :: ys, x => by simp [consDyns, PVal.shift, shift_consDyns k ys x]

theorem walkHead_consDyns : ∀ (ys : List Nat) (x : PVal) (n : Nat),
    walkHead (consDyns ys x) n =
      match ys[n]? with
      | some y => some (.dyn y)
      | none   => walkHead x (n - ys.length)
  | [],      _, _     => by simp [consDyns]
  | _ :: _,  _, 0     => by simp [consDyns, walkHead]
  | _ :: ys, x, n + 1 => by
      simp only [consDyns, walkHead, List.getElem?_cons_succ, List.length_cons,
        Nat.add_sub_add_right]
      exact walkHead_consDyns ys x n

theorem toCode_consDyns : ∀ (ys : List Nat) (x : PVal),
    PVal.toCode (consDyns ys x) = ys.foldr (fun y r => .prim .consP [.var y, r]) (PVal.toCode x)
  | [],      _ => rfl
  | _ :: ys, x => by simp [consDyns, PVal.toCode, toCode_consDyns ys x]

theorem toPRes_consDyns : ∀ (ys : List Nat) (x : PVal),
    PVal.toPRes (consDyns ys x) = ys.foldr (fun y r => .cons (.code (.var y)) r) (PVal.toPRes x)
  | [],      _ => rfl
  | _ :: ys, x => by simp [consDyns, PVal.toPRes, toPRes_consDyns ys x]

theorem length_chunkElems (c : S3.DChunk) : (chunkElems c).length = c.size := by
  simp [chunkElems, S3.DChunk.size]

theorem getElem?_chunkElems (c : S3.DChunk) (n : Nat) :
    (chunkElems c)[n]? = if n < c.size then some (c.arr.getD (c.start + n) 0 + c.sh) else none := by
  unfold chunkElems S3.DChunk.size
  split
  · rw [List.getElem?_map, List.getElem?_drop, Array.getElem?_toList,
      Array.getElem?_eq_getElem (by omega)]
    simp [Array.getD, show c.start + n < c.arr.size by omega]
  · rw [List.getElem?_eq_none]
    simp; omega

theorem chunkElems_pop (c : S3.DChunk) (h : c.start < c.arr.size) :
    chunkElems c = (c.arr.getD c.start 0 + c.sh) :: chunkElems { c with start := c.start + 1 } := by
  simp only [chunkElems]
  rw [List.drop_eq_getElem_cons (by simpa using h)]
  simp only [List.map_cons, Array.getD_eq_getD_getElem?, Array.getElem?_eq_getElem h,
    Option.getD_some, Array.getElem_toList]

theorem chunkElems_nil (c : S3.DChunk) (h : ¬ c.start < c.arr.size) : chunkElems c = [] := by
  simp only [chunkElems, List.map_eq_nil_iff, List.drop_eq_nil_iff, Array.length_toList]
  omega

theorem vals_toList (c : S3.DChunk) : c.vals.toList = chunkElems c := by
  simp only [S3.DChunk.vals, chunkElems, Array.toList_map]

theorem chunkElems_merge (c1 c2 : S3.DChunk) :
    chunkElems (c1.merge c2) = chunkElems c1 ++ chunkElems c2 := by
  have h : chunkElems (c1.merge c2) = (c1.vals ++ c2.vals).toList := by
    simp only [S3.DChunk.merge, chunkElems, List.drop_zero, Nat.add_zero, List.map_id']
  rw [h, Array.toList_append, vals_toList, vals_toList]

theorem chunkElems_addSh (k : Nat) (c : S3.DChunk) :
    chunkElems (c.addSh k) = (chunkElems c).map (· + k) := by
  simp [S3.DChunk.addSh, chunkElems, List.map_map, Function.comp_def, Nat.add_assoc]

theorem chunksElems_map_addSh (k : Nat) : ∀ cs : List S3.DChunk,
    chunksElems (cs.map (S3.DChunk.addSh k)) = (chunksElems cs).map (· + k)
  | []      => rfl
  | c :: cs => by simp [chunksElems, chunkElems_addSh, chunksElems_map_addSh k cs]

theorem chunksElems_insertChunk : ∀ (c : S3.DChunk) (cs : List S3.DChunk),
    chunksElems (S3.insertChunk c cs) = chunkElems c ++ chunksElems cs
  | _, []         => by simp [S3.insertChunk, chunksElems]
  | c, c2 :: rest => by
      simp only [S3.insertChunk]
      split
      · rw [chunksElems_insertChunk, chunkElems_merge]
        simp [chunksElems, List.append_assoc]
      · rfl

theorem popChunks_none : ∀ cs : List S3.DChunk, S3.popChunks cs = none → chunksElems cs = []
  | [],      _ => rfl
  | c :: cs, h => by
      simp only [S3.popChunks] at h
      split at h
      · simp at h
      · simp [chunksElems, chunkElems_nil c ‹_›, popChunks_none cs h]

theorem popChunks_some : ∀ (cs : List S3.DChunk) (x : Nat) (cs' : List S3.DChunk),
    S3.popChunks cs = some (x, cs') → chunksElems cs = x :: chunksElems cs'
  | [],      _, _,   h => by simp [S3.popChunks] at h
  | c :: cs, x, cs', h => by
      simp only [S3.popChunks] at h
      split at h
      · next hc =>
        simp only [Option.some.injEq, Prod.mk.injEq] at h
        obtain ⟨rfl, rfl⟩ := h
        simp [chunksElems, chunkElems_pop c hc, Array.getElem?_eq_getElem hc]
      · next hc => simp [chunksElems, chunkElems_nil c hc, popChunks_some cs x cs' h]

theorem chunksAt_spec : ∀ (cs : List S3.DChunk) (n : Nat),
    S3.chunksAt cs n =
      match (chunksElems cs)[n]? with
      | some y => .inl y
      | none   => .inr (n - (chunksElems cs).length)
  | [],      n => by simp [S3.chunksAt, chunksElems]
  | c :: cs, n => by
      simp only [S3.chunksAt, chunksElems]
      by_cases h : n < c.size
      · rw [if_pos h, List.getElem?_append_left (by rw [length_chunkElems]; exact h),
          getElem?_chunkElems, if_pos h]
      · rw [if_neg h, chunksAt_spec cs (n - c.size),
          List.getElem?_append_right (by rw [length_chunkElems]; omega), length_chunkElems,
          List.length_append, length_chunkElems]
        cases (chunksElems cs)[n - c.size]? with
        | some _ => rfl
        | none => simp only [Sum.inr.injEq]; omega

/-! ## 2. The partial-value operations commute with `rep` -/

theorem rep_shift (k : Nat) (v : S3.PVal) : rep (S3.PVal.shift k v) = PVal.shift k (rep v) := by
  unfold S3.PVal.shift
  by_cases hk : k = 0
  · subst hk; simp
  · simp only [hk, if_false]
    cases v with
    | stat x       => simp [rep, PVal.shift]
    | dyn i        => simp [rep, PVal.shift]
    | shifted k' w => simp only [rep]; rw [shift_add, Nat.add_comm]
    | cons a b     => simp [rep]
    | dyns cs t    => simp [rep]

theorem rep_shiftBy (k : Nat) (env : S3.PEnv) :
    (S3.PEnv.shiftBy k env).map rep = PEnv.shiftBy k (env.map rep) := by
  unfold S3.PEnv.shiftBy
  by_cases hk : k = 0
  · subst hk
    simp only [if_true]
    induction env with
    | nil => rfl
    | cons v rest ih => simp
  · simp only [hk, if_false]
    induction env with
    | nil => rfl
    | cons v rest ih => simp [PEnv.shiftBy, rep_shift, ih]

theorem rep_hnfAt : ∀ (acc : Nat) (v : S3.PVal),
    rep (S3.PVal.hnfAt acc v) = PVal.shift acc (rep v)
  | acc, .shifted k v => by
      simp only [S3.PVal.hnfAt, rep]
      rw [rep_hnfAt (acc + k) v, shift_add, Nat.add_comm]
  | _,   .stat _      => by simp [S3.PVal.hnfAt, rep, PVal.shift]
  | acc, .dyn i       => by simp [S3.PVal.hnfAt, rep, PVal.shift]
  | acc, .cons a b    => by simp [S3.PVal.hnfAt, rep, PVal.shift, rep_shift]
  | acc, .dyns cs t   => by
      rcases hp : S3.popChunks cs with _ | ⟨x, cs'⟩
      · simp only [S3.PVal.hnfAt, hp, rep, popChunks_none cs hp, consDyns]
        exact rep_hnfAt acc t
      · simp [S3.PVal.hnfAt, hp, rep, rep_shift, popChunks_some cs x cs' hp, consDyns, PVal.shift]

theorem rep_hnf (v : S3.PVal) : rep (S3.PVal.hnf v) = rep v := by
  simp [S3.PVal.hnf, rep_hnfAt]

/-- `hnfAt` exposes a real constructor. -/
theorem hnfAt_not_shifted : ∀ (acc : Nat) (v : S3.PVal) (k : Nat) (w : S3.PVal),
    S3.PVal.hnfAt acc v ≠ .shifted k w
  | acc, .shifted k' v, k, w => by simp only [S3.PVal.hnfAt]; exact hnfAt_not_shifted _ v k w
  | _, .stat _,   _, _ => by simp [S3.PVal.hnfAt]
  | _, .dyn _,    _, _ => by simp [S3.PVal.hnfAt]
  | _, .cons _ _, _, _ => by simp [S3.PVal.hnfAt]
  | acc, .dyns cs t, k, w => by
      simp only [S3.PVal.hnfAt]
      split
      · simp
      · exact hnfAt_not_shifted acc t k w

/-- ... and never an array spine. -/
theorem hnfAt_not_dyns : ∀ (acc : Nat) (v : S3.PVal) (cs : List S3.DChunk) (w : S3.PVal),
    S3.PVal.hnfAt acc v ≠ .dyns cs w
  | acc, .shifted k' v, cs, w => by simp only [S3.PVal.hnfAt]; exact hnfAt_not_dyns _ v cs w
  | _, .stat _,   _, _ => by simp [S3.PVal.hnfAt]
  | _, .dyn _,    _, _ => by simp [S3.PVal.hnfAt]
  | _, .cons _ _, _, _ => by simp [S3.PVal.hnfAt]
  | acc, .dyns cs' t, cs, w => by
      simp only [S3.PVal.hnfAt]
      split
      · simp
      · exact hnfAt_not_dyns acc t cs w

/-- What `hnf` exposes, read through `rep`: the head constructor of `rep v`. -/
theorem hnf_cases (v : S3.PVal) :
    (∃ x, S3.PVal.hnf v = .stat x ∧ rep v = .stat x) ∨
    (∃ i, S3.PVal.hnf v = .dyn i ∧ rep v = .dyn i) ∨
    (∃ a b, S3.PVal.hnf v = .cons a b ∧ rep v = .cons (rep a) (rep b)) := by
  have hr := rep_hnf v
  have hns := hnfAt_not_shifted 0 v
  have hnd := hnfAt_not_dyns 0 v
  unfold S3.PVal.hnf at hr hns hnd ⊢
  generalize hh : S3.PVal.hnfAt 0 v = h at hr hns hnd
  cases h with
  | stat x => exact Or.inl ⟨x, rfl, by simpa [rep] using hr.symm⟩
  | dyn i => exact Or.inr (Or.inl ⟨i, rfl, by simpa [rep] using hr.symm⟩)
  | cons a b => exact Or.inr (Or.inr ⟨a, b, rfl, by simpa [rep] using hr.symm⟩)
  | shifted k w => exact absurd rfl (hns k w)
  | dyns cs w => exact absurd rfl (hnd cs w)

theorem chunksCode_eq (acc : Nat) : ∀ (cs : List S3.DChunk) (tl : Term),
    S3.chunksCode acc cs tl =
      ((chunksElems cs).map (· + acc)).foldr (fun y r => .prim .consP [.var y, r]) tl
  | [],      _  => rfl
  | c :: cs, tl => by
      have ih := chunksCode_eq acc cs tl
      simp only [S3.chunksCode, List.foldr_cons] at ih ⊢
      rw [ih, ← Array.foldr_toList, vals_toList]
      simp [chunksElems, List.foldr_map]

theorem chunksPRes_eq (acc : Nat) : ∀ (cs : List S3.DChunk) (tl : S3.PRes),
    repR (S3.chunksPRes acc cs tl) =
      ((chunksElems cs).map (· + acc)).foldr (fun y r => .cons (.code (.var y)) r) (repR tl)
  | [],      _  => rfl
  | c :: cs, tl => by
      have ih := chunksPRes_eq acc cs tl
      simp only [S3.chunksPRes, List.foldr_cons] at ih ⊢
      rw [← Array.foldr_toList, vals_toList]
      generalize List.foldr (fun c r => Array.foldr (fun x r => S3.PRes.cons (.code (.var (x + acc))) r) r c.vals) tl cs = R at ih
      simp only [chunksElems, List.map_append, List.foldr_append, ← ih]
      generalize chunkElems c = L
      induction L with
      | nil => rfl
      | cons y L ihL => simp [repR, ihL]

theorem toCodeAt_rep : ∀ (acc : Nat) (v : S3.PVal),
    S3.PVal.toCodeAt acc v = PVal.toCode (PVal.shift acc (rep v))
  | acc, .shifted k v => by
      simp only [S3.PVal.toCodeAt, rep]
      rw [toCodeAt_rep (acc + k) v, shift_add, Nat.add_comm]
  | _,   .stat _      => by simp [S3.PVal.toCodeAt, rep, PVal.shift, PVal.toCode]
  | acc, .dyn i       => by simp [S3.PVal.toCodeAt, rep, PVal.shift, PVal.toCode]
  | acc, .cons a b    => by
      simp [S3.PVal.toCodeAt, rep, PVal.shift, PVal.toCode, toCodeAt_rep acc a, toCodeAt_rep acc b]
  | acc, .dyns cs t   => by
      simp only [S3.PVal.toCodeAt, rep]
      rw [toCodeAt_rep acc t, chunksCode_eq, shift_consDyns, toCode_consDyns]

theorem toCode_rep (v : S3.PVal) : S3.PVal.toCode v = PVal.toCode (rep v) := by
  simp [S3.PVal.toCode, toCodeAt_rep]

theorem toPResAt_rep : ∀ (acc : Nat) (v : S3.PVal),
    repR (S3.PVal.toPResAt acc v) = PVal.toPRes (PVal.shift acc (rep v))
  | acc, .shifted k v => by
      simp only [S3.PVal.toPResAt, rep]
      rw [toPResAt_rep (acc + k) v, shift_add, Nat.add_comm]
  | _,   .stat _      => by simp [S3.PVal.toPResAt, rep, repR, PVal.shift, PVal.toPRes]
  | acc, .dyn i       => by simp [S3.PVal.toPResAt, rep, repR, PVal.shift, PVal.toPRes]
  | acc, .cons a b    => by
      simp [S3.PVal.toPResAt, rep, repR, PVal.shift, PVal.toPRes, toPResAt_rep acc a, toPResAt_rep acc b]
  | acc, .dyns cs t   => by
      simp only [S3.PVal.toPResAt, rep]
      rw [chunksPRes_eq, toPResAt_rep acc t, shift_consDyns, toPRes_consDyns]

theorem toPRes_rep (v : S3.PVal) : repR (S3.PVal.toPRes v) = PVal.toPRes (rep v) := by
  simp [S3.PVal.toPRes, toPResAt_rep]

theorem ofPVal_rep (v : S3.PVal) : repR (S3.PRes.ofPVal v) = PRes.ofPVal (rep v) := by
  unfold S3.PRes.ofPVal
  rcases hnf_cases v with ⟨x, h, hr⟩ | ⟨i, h, hr⟩ | ⟨a, b, h, hr⟩
  · rw [h, hr]; simp [repR, PRes.ofPVal]
  · rw [h, hr]; simp [repR, PRes.ofPVal]
  · rw [h, hr]; simp [repR, rep, PRes.ofPVal]

theorem toCodeR_rep : ∀ r : S3.PRes, S3.PRes.toCode r = PRes.toCode (repR r)
  | .stat _    => rfl
  | .code _    => rfl
  | .cons a b  => by simp [S3.PRes.toCode, PRes.toCode, repR, toCodeR_rep a, toCodeR_rep b]
  | .lets bs r => by simp [S3.PRes.toCode, PRes.toCode, repR, toCodeR_rep r]
  | .val v     => by simp [S3.PRes.toCode, PRes.toCode, repR, toCode_rep]

theorem total_rep : ∀ r : S3.PRes, S3.PRes.total r = PRes.total (repR r)
  | .stat _    => rfl
  | .code t    => by cases t <;> rfl
  | .cons a b  => by simp [S3.PRes.total, PRes.total, repR, total_rep a, total_rep b]
  | .lets _ _  => rfl
  | .val _     => rfl


/-! ## 3. The result-level helpers commute -/

theorem splitArgs_rep : ∀ (Δ : Div) (rs : List S3.PRes),
    S3.splitArgs Δ rs = splitArgs Δ (rs.map repR)
  | [], [] => rfl
  | [], _ :: _ => by simp [S3.splitArgs, splitArgs]
  | .stat :: _, [] => by simp [S3.splitArgs, splitArgs]
  | .dyn :: _, [] => by simp [S3.splitArgs, splitArgs]
  | .stat :: bs, r :: rs => by
      cases r <;> simp [S3.splitArgs, splitArgs, repR, splitArgs_rep bs rs] <;>
        rcases splitArgs bs (rs.map repR) with _ | ⟨_, _⟩ <;> rfl
  | .dyn :: bs, r :: rs => by
      simp [S3.splitArgs, splitArgs, splitArgs_rep bs rs, toCodeR_rep]
      rcases splitArgs bs (rs.map repR) with _ | ⟨_, _⟩ <;> rfl

theorem buildEnv_rep : ∀ (Δ : Div) (vs : List Val) (j : Nat),
    (S3.buildEnv Δ vs j).map (List.map rep) = buildEnv Δ vs j
  | [], [], _ => rfl
  | [], _ :: _, _ => by simp [S3.buildEnv, buildEnv, Except.map]
  | .stat :: _, [], _ => by simp [S3.buildEnv, buildEnv, Except.map]
  | .stat :: bs, v :: vs, j => by
      have ih := buildEnv_rep bs vs j
      simp only [S3.buildEnv, buildEnv]
      rw [← ih]
      cases S3.buildEnv bs vs j <;> simp [Except.map, rep]
  | .dyn :: bs, vs, j => by
      have ih := buildEnv_rep bs vs (j + 1)
      simp only [S3.buildEnv, buildEnv]
      rw [← ih]
      cases S3.buildEnv bs vs (j + 1) <;> simp [Except.map, rep]

theorem rep_peelShift : ∀ v : S3.PVal,
    rep v = PVal.shift (S3.PVal.peelShift v).1 (rep (S3.PVal.peelShift v).2)
  | .shifted k v => by
      have ih := rep_peelShift v
      rcases h : S3.PVal.peelShift v with ⟨k', w⟩
      rw [h] at ih
      simp only [S3.PVal.peelShift, h, rep]
      rw [ih, shift_add, Nat.add_comm]
  | .stat _    => by simp [S3.PVal.peelShift]
  | .dyn _     => by simp [S3.PVal.peelShift]
  | .cons _ _  => by simp [S3.PVal.peelShift]
  | .dyns _ _  => by simp [S3.PVal.peelShift]

/-- The array-spine cons IS `cons`. -/
theorem rep_mkCons (h t : S3.PVal) : rep (S3.PVal.mkCons h t) = .cons (rep h) (rep t) := by
  unfold S3.PVal.mkCons
  cases h with
  | dyn j =>
    have hp := rep_peelShift t
    rcases hpe : S3.PVal.peelShift t with ⟨k, w⟩
    rw [hpe] at hp
    cases w with
    | dyns cs u =>
      simp only at hp
      simp only [rep, chunksElems_insertChunk]
      rw [hp, rep_shift]
      simp only [rep]
      rw [shift_consDyns]
      have h1 : chunkElems ⟨0, #[j], 0⟩ = [j] := by simp [chunkElems]
      rw [h1]
      by_cases hk : k = 0
      · subst hk; simp [consDyns]
      · simp [hk, chunksElems_map_addSh, consDyns]
    | _ => simp [rep, chunksElems, chunkElems, consDyns]
  | _ => simp [rep]

def repP (p : S3.Prepared) : Prepared := ⟨p.binds, rep p.value⟩

theorem prepare_rep : ∀ r : S3.PRes, prepare (repR r) = repP (S3.prepare r)
  | .val v    => by simp [repR, prepare, S3.prepare, repP]
  | .stat v   => by simp [repR, prepare, S3.prepare, repP, rep]
  | .code t   => by
      cases t <;> simp [repR, prepare, S3.prepare, repP, rep]
  | .lets bs r => by
      simp [repR, prepare, S3.prepare, repP, prepare_rep r]
  | .cons a b => by
      have ha := prepare_rep a
      have hb := prepare_rep b
      simp only [repR, prepare, S3.prepare, ha, hb]
      rcases hpa : S3.prepare a with ⟨ba, va⟩
      rcases hpb : S3.prepare b with ⟨bb, vb⟩
      simp only [repP]
      cases ba <;> cases bb <;> simp [rep, rep_shift, rep_mkCons, toCodeR_rep, repR]

theorem peelHd_rep : ∀ r : S3.PRes, (S3.peelHd r).map repR = peelHd (repR r)
  | .lets bs r => by
      simp [S3.peelHd, peelHd, repR, ← peelHd_rep r, Option.map_map]; rfl
  | .cons a b  => by
      simp only [S3.peelHd, peelHd, repR, ← total_rep]; split <;> simp
  | .val v     => by
      simp only [S3.peelHd, repR]
      rcases hnf_cases v with ⟨x, h, hr⟩ | ⟨i, h, hr⟩ | ⟨a, b, h, hr⟩
      · rw [h, hr]; simp [peelHd]
      · rw [h, hr]; simp [peelHd]
      · rw [h, hr]; simp [peelHd, ofPVal_rep]
  | .stat _    => by simp [S3.peelHd, peelHd, repR]
  | .code _    => by simp [S3.peelHd, peelHd, repR]

theorem peelTl_rep : ∀ r : S3.PRes, (S3.peelTl r).map repR = peelTl (repR r)
  | .lets bs r => by
      simp [S3.peelTl, peelTl, repR, ← peelTl_rep r, Option.map_map]; rfl
  | .cons a b  => by
      simp only [S3.peelTl, peelTl, repR, ← total_rep]; split <;> simp
  | .val v     => by
      simp only [S3.peelTl, repR]
      rcases hnf_cases v with ⟨x, h, hr⟩ | ⟨i, h, hr⟩ | ⟨a, b, h, hr⟩
      · rw [h, hr]; simp [peelTl]
      · rw [h, hr]; simp [peelTl]
      · rw [h, hr]; simp [peelTl, ofPVal_rep]
  | .stat _    => by simp [S3.peelTl, peelTl, repR]
  | .code _    => by simp [S3.peelTl, peelTl, repR]

theorem peelIsNil_rep : ∀ r : S3.PRes, (S3.peelIsNil r).map repR = peelIsNil (repR r)
  | .lets bs r => by
      simp [S3.peelIsNil, peelIsNil, repR, ← peelIsNil_rep r, Option.map_map]; rfl
  | .cons a b  => by
      simp only [S3.peelIsNil, peelIsNil, repR, ← total_rep]; split <;> simp [repR]
  | .val v     => by
      simp only [S3.peelIsNil, repR]
      rcases hnf_cases v with ⟨x, h, hr⟩ | ⟨i, h, hr⟩ | ⟨a, b, h, hr⟩
      · rw [h, hr]; simp [peelIsNil]
      · rw [h, hr]; simp [peelIsNil]
      · rw [h, hr]; simp [peelIsNil, repR]
  | .stat _    => by simp [S3.peelIsNil, peelIsNil, repR]
  | .code _    => by simp [S3.peelIsNil, peelIsNil, repR]

theorem primStruct_rep (p : Prim) (rs : List S3.PRes) :
    (S3.primStruct p rs).map repR = primStruct p (rs.map repR) := by
  rcases rs with _ | ⟨r1, _ | ⟨r2, _ | ⟨r3, rs⟩⟩⟩
  · cases p <;> simp [S3.primStruct, primStruct]
  · cases p <;> simp [S3.primStruct, primStruct, peelHd_rep, peelTl_rep, peelIsNil_rep]
  · cases p <;> try simp [S3.primStruct, primStruct]
    cases r1 <;> cases r2 <;> simp [repR]
  · cases p <;> simp [S3.primStruct, primStruct]

theorem allStatic_rep : ∀ rs : List S3.PRes, S3.allStatic rs = allStatic (rs.map repR)
  | [] => rfl
  | r :: rs => by
      cases r <;> simp [S3.allStatic, allStatic, repR, allStatic_rep rs] <;>
        cases allStatic (rs.map repR) <;> rfl

theorem dynArgCodes_rep : ∀ (Δ : Div) (rs : List S3.PRes),
    S3.dynArgCodes Δ rs = dynArgCodes Δ (rs.map repR)
  | [], [] => rfl
  | [], _ :: _ => by simp [S3.dynArgCodes, dynArgCodes]
  | _ :: _, [] => by simp [S3.dynArgCodes, dynArgCodes]
  | .stat :: bs, _ :: rs => by simp [S3.dynArgCodes, dynArgCodes, dynArgCodes_rep bs rs]
  | .dyn :: bs, r :: rs => by
      simp [S3.dynArgCodes, dynArgCodes, dynArgCodes_rep bs rs, toCodeR_rep]
      cases dynArgCodes bs (rs.map repR) <;> rfl

theorem freshDyns_rep (k : Nat) : (S3.freshDyns k).map rep = freshDyns k := by
  simp [S3.freshDyns, freshDyns, List.map_map, Function.comp_def, rep]

theorem walkHeadAt_rep : ∀ (acc : Nat) (v : S3.PVal) (n : Nat),
    (S3.walkHeadAt acc v n).map rep = walkHead (PVal.shift acc (rep v)) n
  | acc, .shifted k w, n => by
      simp only [S3.walkHeadAt, rep]
      rw [walkHeadAt_rep (acc + k) w n, shift_add, Nat.add_comm]
  | acc, .cons a _, 0     => by simp [S3.walkHeadAt, rep, PVal.shift, walkHead, rep_shift]
  | acc, .cons _ b, n + 1 => by
      simp only [S3.walkHeadAt, rep, PVal.shift, walkHead]
      exact walkHeadAt_rep acc b n
  | acc, .dyns cs t, n => by
      simp only [S3.walkHeadAt, rep]
      rw [shift_consDyns, walkHead_consDyns, chunksAt_spec, List.getElem?_map, List.length_map]
      rcases h : (chunksElems cs)[n]? with _ | y
      · simp only [Option.map_none]
        exact walkHeadAt_rep acc t _
      · simp [rep]
  | _,   .stat _, _       => by simp [S3.walkHeadAt, rep, PVal.shift, walkHead]
  | _,   .dyn _,  _       => by simp [S3.walkHeadAt, rep, PVal.shift, walkHead]

theorem walkHead_rep (v : S3.PVal) (n : Nat) :
    (S3.walkHead v n).map rep = walkHead (rep v) n := by
  simp [S3.walkHead, walkHeadAt_rep]

theorem nthSummaryEnv_rep (f : Nat) (fd : AFunDef) (env : S3.PEnv) :
    (S3.nthSummaryEnv f fd env).map repR = nthSummaryEnv f fd (env.map rep) := by
  unfold S3.nthSummaryEnv nthSummaryEnv
  split
  · rcases env with _ | ⟨pv, _ | ⟨s, _ | ⟨_, _⟩⟩⟩
    · simp
    · simp
    · simp only [List.map]
      rcases hnf_cases s with ⟨x, h, hr⟩ | ⟨i, h, hr⟩ | ⟨a, b, h, hr⟩
      · rw [h, hr]
        cases x with
        | int k =>
          simp only
          split
          · simp
          · have hw := walkHead_rep pv k.toNat
            cases hwh : S3.walkHead pv k.toNat with
            | none => rw [hwh] at hw; simp [← hw]
            | some w =>
              rw [hwh] at hw
              simp only [Option.map_some] at hw ⊢
              rw [← hw]
              rcases hnf_cases w with ⟨x', h', hr'⟩ | ⟨i', h', hr'⟩ | ⟨a', b', h', hr'⟩
              · rw [h', hr']; rfl
              · rw [h', hr']; simp [repR]
              · rw [h', hr']; rfl
        | _ => simp
      · rw [h, hr]; rfl
      · rw [h, hr]; rfl
    · simp
  · simp

/-! ## 4. The specializer simulates, at every fuel

The outputs are related through `rep`/`repR`; the requests, the residual code
and the errors (`outOfFuel` included) are EQUAL. -/

def repOut : Except MixError S3.MixOut → Except MixError MixOut
  | .ok (r, rq) => .ok (repR r, rq)
  | .error z    => .error z

def repOuts : Except MixError (List S3.PRes × List SpecRequest) →
    Except MixError (List PRes × List SpecRequest)
  | .ok (rs, rq) => .ok (rs.map repR, rq)
  | .error z     => .error z

def repArgs : Except MixError (List Term × S3.PEnv × List SpecRequest) →
    Except MixError (List Term × PEnv × List SpecRequest)
  | .ok (bs, env', rq) => .ok (bs, env'.map rep, rq)
  | .error z           => .error z

@[simp] theorem repOut_ok (r : S3.PRes) (rq : List SpecRequest) :
    repOut (.ok (r, rq)) = .ok (repR r, rq) := rfl
@[simp] theorem repOut_error (z : MixError) : repOut (.error z) = .error z := rfl

theorem map_toCode_rep : ∀ rs : List S3.PRes,
    rs.map S3.PRes.toCode = (rs.map repR).map PRes.toCode
  | [] => rfl
  | r :: rs => by simp [toCodeR_rep r, map_toCode_rep rs]

theorem map_stat_rep (vs : List Val) : (vs.map S3.PVal.stat).map rep = vs.map PVal.stat := by
  induction vs with
  | nil => rfl
  | cons v vs ih => simp [rep, ih]

/-- The term claim at one fuel. -/
abbrev TermSim (n : Nat) : Prop :=
  ∀ (A : AProgram) (idx : SpecRequest → Option Nat) (Δ : Div) (env : S3.PEnv) (t : ATerm),
    repOut (S3.mixTerm n A idx Δ env t) = mixTerm n A idx Δ (env.map rep) t

theorem mixTerms_sim (n : Nat) (hT : TermSim n) :
    ∀ (A : AProgram) (idx : SpecRequest → Option Nat) (Δ : Div) (env : S3.PEnv) (ts : List ATerm),
    repOuts (S3.mixTerms n A idx Δ env ts) = mixTerms n A idx Δ (env.map rep) ts
  | _, _, _, _, [] => by simp [S3.mixTerms, mixTerms, repOuts]
  | A, idx, Δ, env, t :: ts => by
      simp only [S3.mixTerms, mixTerms]
      rw [← hT A idx Δ env t, ← mixTerms_sim n hT A idx Δ env ts]
      rcases S3.mixTerm n A idx Δ env t with _ | ⟨r, rq₁⟩ <;>
        rcases S3.mixTerms n A idx Δ env ts with _ | ⟨rs, rq₂⟩ <;> rfl

theorem mixAlts_sim (n : Nat) (hT : TermSim n) :
    ∀ (A : AProgram) (idx : SpecRequest → Option Nat) (Δ : Div) (env : S3.PEnv) (as : List AAlt),
    S3.mixAlts n A idx Δ env as = mixAlts n A idx Δ (env.map rep) as
  | _, _, _, _, [] => by simp [S3.mixAlts, mixAlts]
  | A, idx, Δ, env, a :: as => by
      have h1 := hT A idx (List.replicate a.arity .dyn ++ Δ)
        (S3.freshDyns a.arity ++ S3.PEnv.shiftBy a.arity env) a.body
      rw [List.map_append, freshDyns_rep, rep_shiftBy] at h1
      simp only [S3.mixAlts, mixAlts]
      rw [← h1, ← mixAlts_sim n hT A idx Δ env as]
      generalize S3.mixTerm n A idx (List.replicate a.arity .dyn ++ Δ)
          (S3.freshDyns a.arity ++ S3.PEnv.shiftBy a.arity env) a.body = X
      generalize S3.mixAlts n A idx Δ env as = Y
      rcases X with _ | ⟨r, rq₁⟩ <;> rcases Y with _ | ⟨as', rq₂⟩ <;> simp [toCodeR_rep]

theorem mixPArgs_sim (n : Nat) (hT : TermSim n) :
    ∀ (A : AProgram) (idx : SpecRequest → Option Nat) (Δ : Div) (env : S3.PEnv)
      (ps : Div) (ts : List ATerm),
    repArgs (S3.mixPArgs n A idx Δ env ps ts) = mixPArgs n A idx Δ (env.map rep) ps ts
  | _, _, _, _, [], [] => by simp [S3.mixPArgs, mixPArgs, repArgs]
  | _, _, _, _, [], _ :: _ => by simp [S3.mixPArgs, mixPArgs, repArgs]
  | _, _, _, _, _ :: _, [] => by simp [S3.mixPArgs, mixPArgs, repArgs]
  | A, idx, Δ, env, .stat :: ps, t :: ts => by
      simp only [S3.mixPArgs, mixPArgs]
      rw [← hT A idx Δ env t, ← mixPArgs_sim n hT A idx Δ env ps ts]
      rcases S3.mixTerm n A idx Δ env t with _ | ⟨r, rq₁⟩
      · rfl
      · cases r <;> try rfl
        rcases S3.mixPArgs n A idx Δ env ps ts with _ | ⟨bs, env', rq₂⟩ <;> rfl
  | A, idx, Δ, env, .dyn :: ps, t :: ts => by
      simp only [S3.mixPArgs, mixPArgs]
      rw [← hT A idx Δ env t]
      rcases S3.mixTerm n A idx Δ env t with _ | ⟨r, rq₁⟩
      · rfl
      · simp only [repOut_ok]
        rw [prepare_rep r]
        have h2 := mixPArgs_sim n hT A idx Δ
          (S3.PEnv.shiftBy (S3.prepare r).binds.length env) ps ts
        rw [rep_shiftBy] at h2
        simp only [repP]
        rw [← h2]
        rcases S3.mixPArgs n A idx Δ (S3.PEnv.shiftBy (S3.prepare r).binds.length env) ps ts
          with _ | ⟨bs, env', rq₂⟩
        · rfl
        · simp [repArgs, rep_shift]

theorem mixTerm_sim : ∀ n, TermSim n
  | 0 => fun A idx Δ env t => by simp [S3.mixTerm, mixTerm]
  | n + 1 => fun A idx Δ env t => by
      have hT := mixTerm_sim n
      have hTs := mixTerms_sim n hT
      have hAs := mixAlts_sim n hT
      have hPs := mixPArgs_sim n hT
      cases t with
      | lit v => simp [S3.mixTerm, mixTerm, repR]
      | var i =>
        simp only [S3.mixTerm, mixTerm, List.getElem?_map]
        cases Δ[i]? with
        | none => cases env[i]? <;> rfl
        | some b =>
          cases env[i]? with
          | none => cases b <;> rfl
          | some v =>
            simp only [Option.map_some]
            rcases hnf_cases v with ⟨x, h, hr⟩ | ⟨j, h, hr⟩ | ⟨a, c, h, hr⟩ <;>
              rw [h, hr] <;> cases b <;> simp [repR, hr]
      | lift e =>
        simp only [S3.mixTerm, mixTerm]
        rw [← hT A idx Δ env e]
        rcases S3.mixTerm n A idx Δ env e with _ | ⟨r, rq⟩
        · rfl
        · cases r <;> rfl
      | letIn bt e body =>
        simp only [S3.mixTerm, mixTerm]
        rw [← hT A idx Δ env e]
        rcases S3.mixTerm n A idx Δ env e with _ | ⟨re, rq₁⟩
        · rfl
        · simp only [repOut_ok]
          cases btOf Δ e with
          | stat =>
            cases re with
            | stat v =>
              have h := hT A idx (.stat :: Δ) (.stat v :: env) body
              simp only [List.map, rep] at h
              simp only [repR]
              rw [← h]
              rcases S3.mixTerm n A idx (.stat :: Δ) (.stat v :: env) body
                with _ | ⟨rb, rq₂⟩ <;> rfl
            | _ => rfl
          | dyn =>
            have h := hT A idx (.dyn :: Δ) (.dyn 0 :: S3.PEnv.shiftBy 1 env) body
            simp only [List.map, rep, rep_shiftBy] at h
            rw [← h]
            rcases S3.mixTerm n A idx (.dyn :: Δ) (.dyn 0 :: S3.PEnv.shiftBy 1 env) body
              with _ | ⟨rb, rq₂⟩
            · rfl
            · cases rb <;> simp [repR, toCodeR_rep]
      | ite bt c a e =>
        simp only [S3.mixTerm, mixTerm]
        rw [← hT A idx Δ env c, ← hT A idx Δ env a, ← hT A idx Δ env e]
        rcases S3.mixTerm n A idx Δ env c with _ | ⟨rc, rq₁⟩
        · rfl
        · simp only [repOut_ok]
          cases btOf Δ c with
          | stat =>
            cases rc with
            | stat v =>
              simp only [repR]
              cases v with
              | bool bv =>
                cases bv
                · rcases S3.mixTerm n A idx Δ env e with _ | ⟨r, rq₂⟩ <;> rfl
                · rcases S3.mixTerm n A idx Δ env a with _ | ⟨r, rq₂⟩ <;> rfl
              | _ => rfl
            | _ => rfl
          | dyn =>
            rcases S3.mixTerm n A idx Δ env a with _ | ⟨ra, rq₂⟩ <;>
              rcases S3.mixTerm n A idx Δ env e with _ | ⟨re, rq₃⟩
            · rfl
            · rfl
            · rfl
            · simp [repR, toCodeR_rep]
      | prim b p ts =>
        simp only [S3.mixTerm, mixTerm]
        rw [← hTs A idx Δ env ts]
        rcases S3.mixTerms n A idx Δ env ts with _ | ⟨rs, rq⟩
        · rfl
        · simp only [repOuts]
          cases b with
          | stat =>
            simp only
            rw [← allStatic_rep]
            rcases S3.allStatic rs with _ | vs
            · rfl
            · simp only; cases evalPrim p vs <;> rfl
          | dyn =>
            simp only
            rw [← primStruct_rep]
            rcases S3.primStruct p rs with _ | r
            · simp [repR, map_toCode_rep]
            · rfl
      | ctorT b k ts =>
        simp only [S3.mixTerm, mixTerm]
        rw [← hTs A idx Δ env ts]
        rcases S3.mixTerms n A idx Δ env ts with _ | ⟨rs, rq⟩
        · rfl
        · simp only [repOuts]
          cases b with
          | stat =>
            simp only
            rw [← allStatic_rep]
            rcases S3.allStatic rs with _ | vs <;> rfl
          | dyn => simp [repR, map_toCode_rep]
      | caseT bt sc alts =>
        simp only [S3.mixTerm, mixTerm]
        rw [← hT A idx Δ env sc, ← hAs A idx Δ env alts]
        rcases S3.mixTerm n A idx Δ env sc with _ | ⟨rsc, rq₁⟩
        · rfl
        · simp only [repOut_ok]
          cases btOf Δ sc with
          | stat =>
            cases rsc with
            | stat v =>
              simp only [repR]
              cases v with
              | ctor tag vs =>
                simp only
                cases findAAlt alts tag with
                | none => rfl
                | some a =>
                  simp only
                  by_cases hc : a.arity = vs.length
                  · rw [if_pos hc, if_pos hc]
                    have h := hT A idx (List.replicate a.arity .stat ++ Δ)
                      (vs.map S3.PVal.stat ++ env) a.body
                    rw [List.map_append, map_stat_rep] at h
                    rw [← h]
                    generalize S3.mixTerm n A idx (List.replicate a.arity .stat ++ Δ)
                      (vs.map S3.PVal.stat ++ env) a.body = X
                    rcases X with _ | ⟨r, rq₂⟩ <;> rfl
                  · rw [if_neg hc, if_neg hc]; rfl
              | _ => rfl
            | _ => rfl
          | dyn =>
            rcases S3.mixAlts n A idx Δ env alts with _ | ⟨alts', rq₂⟩
            · rfl
            · simp [repR, toCodeR_rep]
      | call b f ts =>
        simp only [S3.mixTerm, mixTerm]
        rw [← hTs A idx Δ env ts]
        rcases S3.mixTerms n A idx Δ env ts with _ | ⟨rs, rq₁⟩
        · rfl
        · simp only [repOuts]
          cases A.fn f with
          | none => rfl
          | some fd =>
            cases b with
            | stat =>
              simp only [List.length_map]
              cases allStatDiv fd.params
              · rfl
              · simp only [↓reduceIte]
                by_cases hc : fd.params.length = rs.length
                · rw [if_pos hc, if_pos hc, ← allStatic_rep]
                  rcases S3.allStatic rs with _ | vs
                  · rfl
                  · have h := hT A idx fd.params (vs.map S3.PVal.stat) fd.body
                    rw [map_stat_rep] at h
                    simp only
                    rw [← h]
                    rcases S3.mixTerm n A idx fd.params (vs.map S3.PVal.stat) fd.body
                      with _ | ⟨r, rq₂⟩ <;> rfl
                · rw [if_neg hc, if_neg hc]; rfl
            | dyn =>
              simp only
              rw [← splitArgs_rep]
              rcases S3.splitArgs fd.params rs with _ | ⟨svs, dts⟩
              · rfl
              · simp only; cases idx ⟨f, svs⟩ <;> rfl
      | ucall b f ts =>
        simp only [S3.mixTerm, mixTerm]
        cases A.fn f with
        | none => rfl
        | some fd =>
          cases b with
          | stat =>
            simp only
            rw [← hTs A idx Δ env ts]
            rcases S3.mixTerms n A idx Δ env ts with _ | ⟨rs, rq₁⟩
            · rfl
            · simp only [repOuts, List.length_map]
              cases allStatDiv fd.params
              · rfl
              · simp only [↓reduceIte]
                by_cases hc : fd.params.length = rs.length
                · rw [if_pos hc, if_pos hc, ← allStatic_rep]
                  rcases S3.allStatic rs with _ | vs
                  · rfl
                  · have h := hT A idx fd.params (vs.map S3.PVal.stat) fd.body
                    rw [map_stat_rep] at h
                    simp only
                    rw [← h]
                    rcases S3.mixTerm n A idx fd.params (vs.map S3.PVal.stat) fd.body
                      with _ | ⟨r, rq₂⟩ <;> rfl
                · rw [if_neg hc, if_neg hc]; rfl
          | dyn =>
            simp only
            rw [← hPs A idx Δ env fd.params ts]
            rcases S3.mixPArgs n A idx Δ env fd.params ts with _ | ⟨bs, env', rq₂⟩
            · rfl
            · simp only [repArgs]
              rw [← nthSummaryEnv_rep]
              cases S3.nthSummaryEnv f fd env' with
              | some r => rfl
              | none =>
                simp only [Option.map_none]
                rw [← hT A idx fd.params env' fd.body]
                rcases S3.mixTerm n A idx fd.params env' fd.body with _ | ⟨rb, rq₃⟩
                · rfl
                · cases rb <;> rfl

/-! ## 5. The driver: every function above the term level is EQUAL -/

theorem mixFun_eq (s : Nat) (A : AProgram) (idx : SpecRequest → Option Nat) (req : SpecRequest) :
    S3.mixFun s A idx req = mixFun s A idx req := by
  simp only [S3.mixFun, mixFun]
  cases A.fn req.funIdx with
  | none => rfl
  | some fd =>
    simp only
    rw [← buildEnv_rep]
    rcases S3.buildEnv fd.params req.staticArgs 0 with _ | env
    · rfl
    · simp only [Except.map]
      rw [← mixTerm_sim s A idx fd.params env fd.body]
      rcases S3.mixTerm s A idx fd.params env fd.body with _ | ⟨r, rq⟩
      · rfl
      · simp [toCodeR_rep]

theorem collectFor_eq (s : Nat) (A : AProgram) (idx : SpecRequest → Option Nat) (f : Nat) :
    ∀ reqs, S3.collectFor s A idx f reqs = collectFor s A idx f reqs
  | [] => by simp [S3.collectFor, collectFor]
  | r :: rs => by
      simp only [S3.collectFor, collectFor, mixFun_eq, collectFor_eq s A idx f rs]
      by_cases hf : r.funIdx = f
      · rw [if_pos hf, if_pos hf]
        generalize mixFun s A idx r = X
        generalize collectFor s A idx f rs = Y
        rcases X with _ | ⟨_, _⟩ <;> rcases Y with _ | _ <;> rfl
      · rw [if_neg hf, if_neg hf]

theorem collectFrom_eq (s : Nat) (A : AProgram) (idx : SpecRequest → Option Nat)
    (reqs : List SpecRequest) : ∀ k f, S3.collectFrom s A idx reqs k f = collectFrom s A idx reqs k f
  | 0, _ => by simp [S3.collectFrom, collectFrom]
  | k + 1, f => by
      simp only [S3.collectFrom, collectFrom, collectFor_eq, collectFrom_eq s A idx reqs k (f + 1)]
      generalize collectFor s A idx f reqs = X
      generalize collectFrom s A idx reqs k (f + 1) = Y
      rcases X with _ | _ <;> rcases Y with _ | _ <;> rfl

theorem collectAll_eq (s : Nat) (A : AProgram) (reqs : List SpecRequest) :
    S3.collectAll s A reqs = collectAll s A reqs := by
  simp only [S3.collectAll, collectAll, collectFrom_eq]

theorem closeFM_eq (s : Nat) : ∀ k A seen, S3.closeFM s k A seen = closeFM s k A seen
  | 0, _, _ => by simp [S3.closeFM, closeFM]
  | k + 1, A, seen => by
      simp only [S3.closeFM, closeFM, collectAll_eq]
      cases collectAll s A seen with
      | error z => rfl
      | ok fresh => simp only [closeFM_eq s k]

theorem generateFrom_eq (s : Nat) (A : AProgram) (idx : SpecRequest → Option Nat) :
    ∀ reqs, S3.generateFrom s A idx reqs = generateFrom s A idx reqs
  | [] => by simp [S3.generateFrom, generateFrom]
  | r :: rs => by
      simp only [S3.generateFrom, generateFrom, mixFun_eq, generateFrom_eq s A idx rs]
      generalize mixFun s A idx r = X
      generalize generateFrom s A idx rs = Y
      rcases X with _ | ⟨_, _⟩ <;> rcases Y with _ | _ <;> rfl

theorem generate_eq (s : Nat) (A : AProgram) (reqs : List SpecRequest) :
    S3.generate s A reqs = generate s A reqs := by
  simp only [S3.generate, generate, generateFrom_eq]

theorem mixDriver_eq (s w : Nat) (A : AProgram) (statics : List Val) :
    S3.mixDriver s w A statics = mixDriver s w A statics := by
  simp only [S3.mixDriver, mixDriver, closeFM_eq, generate_eq]
  generalize closeFM s w A (groupByFun A [⟨A.entry, statics⟩]) = X
  rcases X with _ | reqs
  · rfl
  · dsimp only
    generalize generate s A reqs = Y
    rcases Y with _ | funs
    · rfl
    · dsimp only
      generalize indexOfReq reqs ⟨A.entry, statics⟩ = Z
      cases Z <;> rfl

/-- The refinement.  Every compiled caller of `mixDriver` that is elaborated
with this module in scope runs `S3.mixDriver`; every theorem about
`mixDriver` is untouched. -/
@[csimp] theorem mixDriver_eq_S3 : @mixDriver = @S3.mixDriver := by
  funext s w A statics
  exact (mixDriver_eq s w A statics).symm

end S3Correct
end Projection

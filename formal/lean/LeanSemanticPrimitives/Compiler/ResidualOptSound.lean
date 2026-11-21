/-
# The optimizer frame is sound

`validate_sound`: when the checker accepts a certificate, the rebuilt program computes
exactly what the original does, on every step, from every state and input:
`denoteResidual (applyCert R c) e i s = denoteResidual R e i s`. So `optimize` -- the rebuild
when the checker accepts, the original otherwise -- never changes a result
(`optimize_correct`), and with `compileDesign_correct` (`CompileOpt.compileOpt_correct`) the
optimized program is `interpretDesign`.

The proof walks the old bindings once. After `k` old bindings (`t` of them kept) the new
environment holds, at `σ r`, what the old one holds at `r`, for every reference `r` the
checker accepts below slot `S + k` (`Inv.agree`). A kept binding's operands are such
references, so it computes the same value (`denoteExpr_rename`) and lands at `σ (S + k)`; a
dropped binding is never read. Every slot the checker knows (a constant source or a folded
binding) holds, on this step, the checker's constant (`Inv.kc`): a folded binding reads only
known slots, and `denoteExpr` reads nothing but its operands (`denoteExpr_congr`), so its
value is the checker's, which the new constant source at `σ (S + k)` holds (`Inv.src'`). The
terminals then read equal values (`flopNext_rename`, `memNext_rename`).

Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.ResidualOpt

namespace Compiler
namespace ResidualOpt

open Residual

/-! ## Environment facts -/

theorem denoteRef_push_lt {env : SlotEnv} {v : CertVal} {r : Nat} (h : r < env.size) :
    denoteRef (env.push v) r = denoteRef env r := by
  simp [denoteRef, Array.getElem?_push_lt h, Array.getElem?_eq_getElem h]

theorem denoteRef_push_size {env : SlotEnv} {v : CertVal} :
    denoteRef (env.push v) env.size = v := by
  simp [denoteRef]

/-! ## Renaming preserves values -/

theorem refBVs_rename {env env' : SlotEnv} {σ : Nat → Nat} {args : Array Nat}
    (h : ∀ r ∈ args.toList, denoteRef env' (σ r) = denoteRef env r) :
    refBVs env' (args.map σ) = refBVs env args := by
  simp only [refBVs, Array.toList_map, List.map_map]
  apply List.map_congr_left
  intro r hr
  simp only [Function.comp, refBV, h r hr]

theorem denoteExpr_rename {env env' : SlotEnv} {σ : Nat → Nat} (e : ResidualExpr)
    (h : ∀ r ∈ exprRefs e, denoteRef env' (σ r) = denoteRef env r) :
    denoteExpr env' (renameExpr σ e) = denoteExpr env e := by
  cases e <;> simp only [exprRefs] at h <;> simp only [renameExpr, denoteExpr]
  all_goals first
    | rw [refBVs_rename h]
    | (simp only [List.mem_cons, List.not_mem_nil, or_false, forall_eq_or_imp, forall_eq] at h
       simp only [refBV, refMem, h])

theorem flopNext_rename {env env' : SlotEnv} {σ : Nat → Nat} {e : ClockEdges} {s : RuntimeState}
    {idx : Nat} (f : ResidualFlopUpdate)
    (h : ∀ r ∈ flopRefs f, denoteRef env' (σ r) = denoteRef env r) :
    flopNext env' e s idx (renameFlop σ f) = flopNext env e s idx f := by
  obtain ⟨w, din, en, rst, rv, al, ck, ar⟩ := f
  cases en <;> cases rst <;>
    simp_all [flopRefs, renameFlop, flopNext, refBV]

theorem inDom_renameMem (σ : Nat → Nat) (m : ResidualMemoryUpdate) :
    (renameMem σ m).toDesc.inDom = m.toDesc.inDom := by
  funext k
  simp [MemoryDesc.inDom, renameMem, ResidualMemoryUpdate.toDesc, renameCommit,
    Function.comp_def]

theorem domSpan_renameMem (σ : Nat → Nat) (m : ResidualMemoryUpdate) :
    (renameMem σ m).toDesc.domSpan = m.toDesc.domSpan := by
  show (m.commits.map (renameCommit σ)).foldl (fun a c => max a (c.mask.log2 + 1)) (m.clock + 1)
      = m.commits.foldl (fun a c => max a (c.mask.log2 + 1)) (m.clock + 1)
  rw [Array.foldl_map]
  rfl

theorem allFire_renameMem (σ : Nat → Nat) (m : ResidualMemoryUpdate) (e : ClockEdges) :
    (renameMem σ m).toDesc.allFire e = m.toDesc.allFire e := by
  unfold MemoryDesc.allFire
  rw [domSpan_renameMem, inDom_renameMem]

theorem firedIs_renameMem (σ : Nat → Nat) (m : ResidualMemoryUpdate) (e : ClockEdges) :
    (renameMem σ m).toDesc.firedIs e = m.toDesc.firedIs e := by
  funext c
  unfold MemoryDesc.firedIs
  rw [domSpan_renameMem, inDom_renameMem]

theorem memNext_rename {env env' : SlotEnv} {σ : Nat → Nat} {e : ClockEdges} {s : RuntimeState}
    {idx : Nat} (m : ResidualMemoryUpdate)
    (h : ∀ r ∈ memRefs m, denoteRef env' (σ r) = denoteRef env r) :
    memNext env' e s idx (renameMem σ m) = memNext env e s idx m := by
  have hnext : refMem env' (σ m.nextImg) = refMem env m.nextImg := by
    simp only [refMem, h m.nextImg (by simp [memRefs])]
  have hheld : memHeld env' s idx (renameMem σ m) = memHeld env s idx m := by
    unfold memHeld
    cases ha : m.asyncImg with
    | none => simp [renameMem, ha]
    | some a =>
      simp only [renameMem, ha, Option.map_some]
      simp only [refMem, h a (by simp [memRefs, ha])]
  have hpred : (fun c => (renameMem σ m).toDesc.firedIs e (renameCommit σ c))
      = m.toDesc.firedIs e := by
    funext c
    rw [firedIs_renameMem]
    rfl
  unfold memNext
  have hcomm : (renameMem σ m).commits = m.commits.map (renameCommit σ) := rfl
  have hempty : (m.commits.map (renameCommit σ)).isEmpty = m.commits.isEmpty := by
    simp only [Array.isEmpty, Array.size_map]
  have hck : (renameMem σ m).clock = m.clock := rfl
  have hnx : (renameMem σ m).nextImg = σ m.nextImg := rfl
  rw [allFire_renameMem, hcomm, hempty, Array.find?_map, hck, hnx]
  simp only [Function.comp_def, hpred]
  cases hE : m.commits.isEmpty <;> simp only [Bool.false_eq_true, eq_self, ite_false, ite_true]
  · cases hA : m.toDesc.allFire e <;> simp only [Bool.false_eq_true, eq_self, ite_false, ite_true]
    · cases hf : m.commits.find? (m.toDesc.firedIs e) with
      | none => simp only [Option.map_none]; exact hheld
      | some c =>
        simp only [Option.map_some, renameCommit]
        have hc : c ∈ m.commits := Array.mem_of_find?_eq_some hf
        simp only [refMem, h c.nextImg (by
          simp only [memRefs, List.mem_cons, List.mem_append, List.mem_map, Array.mem_toList_iff]
          exact Or.inr (Or.inl ⟨c, hc, rfl⟩))]
    · exact hnext
  · cases hF : fires e m.clock <;> simp only [Bool.false_eq_true, eq_self, ite_false, ite_true]
    · exact hheld
    · exact hnext

/-! ## Static widths hold: every value-producing expression is `mk_bv w _` -/

/-- `x` is a reduced bit vector of width `w`. -/
def RedW (w : Nat) (x : BV) : Prop := ∃ n : Int, x = mk_bv w n

theorem redW_mk (w : Nat) (n : Int) : RedW w (mk_bv w n) := ⟨n, rfl⟩

theorem redW_foldl {w : Nat} {f : BV → BV → BV} (hf : ∀ acc b, RedW w (f acc b)) :
    ∀ (l : List BV) (init : BV), RedW w init → RedW w (l.foldl f init)
  | [], _, h => h
  | b :: l, init, _ => redW_foldl hf l (f init b) (hf init b)

theorem redW_rredOrV (w : Nat) : ∀ l : List BV, RedW w (rredOrV w l)
  | [] => redW_mk w 0
  | a :: rest => by
    simp only [rredOrV]
    split
    · exact redW_mk w 1
    · exact redW_rredOrV w rest

theorem redW_denoteExpr (env : SlotEnv) (e : ResidualExpr) {w : Nat} (h : exprWidth e = some w) :
    ∃ x, denoteExpr env e = .bv x ∧ RedW w x := by
  have bit : ∀ (f : Bool → Bool → Bool) (a b : BV), RedW w (bv_bitwise w f a b) :=
    fun f a b => ⟨_, rfl⟩
  cases e <;> simp only [exprWidth, Option.some.injEq, reduceCtorEq] at h <;> subst h <;>
    refine ⟨_, rfl, ?_⟩
  all_goals first
    | exact ⟨_, rfl⟩
    | skip
  · -- rand
    simp only [randV]; split
    · exact redW_mk _ 0
    · exact redW_foldl (fun acc b => bit _ acc b) _ _ ⟨_, rfl⟩
  · exact redW_foldl (fun acc b => bit _ acc b) _ _ (redW_mk _ 0)
  · exact redW_foldl (fun acc b => bit _ acc b) _ _ (redW_mk _ 0)
  · exact redW_rredOrV _ _
  · simp only [reqV]; split
    · exact redW_mk _ 1
    · exact redW_mk _ _
  · simp only [rshlV]; split
    · exact redW_mk _ 0
    · exact redW_foldl (fun acc b => bit _ acc _) _ _ (redW_mk _ 0)
  · simp only [rmuxNV]; split
    · exact redW_mk _ 0
    · split
      · exact redW_mk _ 0
      · exact ⟨_, rfl⟩
  · simp only [rsraV, bv_sra]; split
    · exact redW_mk _ _
    · exact redW_mk _ _
  · simp only [rsextV]; split
    · exact redW_mk _ 0
    · split
      · exact redW_mk _ _
      · exact redW_mk _ _
  · simp only [rmuxV]; split
    · exact ⟨_, rfl⟩
    · exact ⟨_, rfl⟩
  · simp only [rmemReadV]; split
    · exact redW_mk _ 0
    · exact ⟨_, rfl⟩

theorem redW_sourceValue (i : RuntimeInput) (s : RuntimeState) (sd : SourceDesc) {w : Nat}
    (h : srcWidth sd = some w) : ∃ x, sourceValue i s sd = .bv x ∧ RedW w x := by
  cases sd <;> simp only [srcWidth, Option.some.injEq, reduceCtorEq] at h <;> subst h <;>
    refine ⟨_, rfl, ?_⟩
  · exact ⟨_, rfl⟩
  · exact redW_mk _ _
  · exact ⟨_, rfl⟩
  · repeat' split
    all_goals first
      | exact redW_mk _ _
      | exact ⟨_, rfl⟩

/-! ## A copy's value is its operand's -/

theorem resize_redW {w : Nat} {x : BV} (h : RedW w x) : bv_resize w x = x := by
  obtain ⟨n, rfl⟩ := h
  simp only [bv_resize, bv_uint, mk_bv, BV.mk.injEq, true_and]
  rw [Int.emod_emod, Int.emod_emod]

theorem uintNat_mk_zero (w : Nat) : FastBV.uintNat (mk_bv w 0) = 0 := by
  have h := FastBV.bv_uint_eq_uintNat (mk_bv w 0)
  have h0 : bv_uint (mk_bv w 0) = 0 := by simp [bv_uint, mk_bv]
  rw [h0] at h
  omega

/-- A bitwise operator that passes its right operand through a zero left operand. -/
theorem bitwise_zero_left (w : Nat) (f : Bool → Bool → Bool) (hf : ∀ b, f false b = b) (x : BV) :
    bv_bitwise w f (mk_bv w 0) x = bv_resize w x := by
  rw [FastBV.bv_bitwise_eq_nat, uintNat_mk_zero]
  have hr : bv_resize w x = FastBV.ofNatLt w (FastBV.uintNat x % 2 ^ w) := by
    simp only [bv_resize, FastBV.bv_uint_eq_uintNat, FastBV.mk_bv_natCast_mod]
  rw [hr]
  congr 1
  apply Nat.eq_of_testBit_eq
  intro i
  rw [FastBV.testBit_bitwiseNat, Nat.testBit_mod_two_pow, Nat.zero_testBit, hf]

theorem lowMask_getMask {w : Nat} {x m : BV} (hx : RedW w x) (hm : lowMaskCovers w m = true) :
    bv_get_mask w x m = x := by
  simp only [lowMaskCovers, Bool.and_eq_true, beq_iff_eq, decide_eq_true_eq] at hm
  obtain ⟨⟨hM, hwL⟩, hLm⟩ := hm
  have hM' : FastBV.uintNat m = (2 ^ Nat.log2 (FastBV.uintNat m + 1) - 1) * 2 ^ 0 := by
    rw [Nat.pow_zero, Nat.mul_one]; exact hM
  unfold bv_get_mask
  rw [FastBV.mask_indices_bv_contig m 0 _ hM' (by omega), FastBV.pack_low_bv_range',
    Nat.shiftRight_zero]
  obtain ⟨n, rfl⟩ := hx
  have hlt := FastBV.uintNat_lt (mk_bv w n)
  have hle : 2 ^ (mk_bv w n).width ≤ 2 ^ Nat.log2 (FastBV.uintNat m + 1) :=
    Nat.pow_le_pow_right (by decide) (by simpa [mk_bv] using hwL)
  rw [Nat.mod_eq_of_lt (by omega), ← FastBV.bv_uint_eq_uintNat]
  exact resize_redW ⟨n, rfl⟩

theorem rmultV_one {w : Nat} {x : BV} (h : RedW w x) : rmultV w [x] = x := by
  simp only [rmultV, List.foldl_cons, List.foldl_nil, Int.one_mul]
  exact resize_redW h

theorem randV_one {w : Nat} {x : BV} (h : RedW w x) : randV w [x] = x := by
  simp only [randV, List.foldl_nil]
  exact resize_redW h

theorem rorBitsV_one {w : Nat} {x : BV} (h : RedW w x) : rorBitsV w [x] = x := by
  simp only [rorBitsV, List.foldl_cons, List.foldl_nil]
  rw [bitwise_zero_left w _ (fun b => by simp) x]
  exact resize_redW h

theorem rxorV_one {w : Nat} {x : BV} (h : RedW w x) : rxorV w [x] = x := by
  simp only [rxorV, List.foldl_cons, List.foldl_nil]
  rw [bitwise_zero_left w _ (fun b => by simp) x]
  exact resize_redW h

/-- **A copy is its operand.** Given that the known slots hold the values `cv` gives and every
slot with a static width holds a reduced vector of that width. -/
theorem copyOf_sound {env : SlotEnv} {kn : Nat → Bool} {cv : Nat → CertVal}
    {wq : Nat → Option Nat}
    (hk : ∀ r, kn r = true → denoteRef env r = cv r)
    (hw : ∀ r w, wq r = some w → ∃ x, denoteRef env r = .bv x ∧ RedW w x)
    {e : ResidualExpr} {q : Nat} (h : copyOf kn cv wq e = some q) :
    denoteExpr env e = denoteRef env q := by
  have one : ∀ (args : Array Nat) (w : Nat), args.toList = [q] → wq q = some w →
      ∃ x, refBVs env args = [x] ∧ denoteRef env q = .bv x ∧ RedW w x := by
    intro args w ha hq
    obtain ⟨x, hx, hr⟩ := hw q w hq
    exact ⟨x, by simp [refBVs, ha, refBV, hx, CertVal.asBV], hx, hr⟩
  cases e <;> simp only [copyOf, reduceCtorEq] at h
  · -- rsum
    rename_i w n args
    split at h
    · rename_i q' hq'
      simp only [Bool.and_eq_true, decide_eq_true_eq, beq_iff_eq] at h
      split at h
      · rename_i hc
        cases h
        obtain ⟨x, hl, hx, hr⟩ := one args w hq' hc.2
        simp only [denoteExpr, hl, hx, rsumV]
        have : ([x].take n) = [x] := List.take_of_length_le (by simp; omega)
        have h2 : ([x].drop n) = [] := List.drop_of_length_le (by simp; omega)
        rw [this, h2]
        simp only [List.foldl_cons, List.foldl_nil, Int.zero_add, Int.sub_zero]
        exact congrArg CertVal.bv (resize_redW hr)
      · exact absurd h (by simp)
    · exact absurd h (by simp)
  case rmult w args =>
    split at h
    · rename_i q' hq'
      split at h
      · rename_i hc
        simp only [Option.some.injEq] at h
        subst h
        obtain ⟨x, hl, hx, hr⟩ := one args w hq' (by simpa using hc)
        simp only [denoteExpr, hl, hx]
        exact congrArg CertVal.bv (rmultV_one hr)
      · exact absurd h (by simp)
    · exact absurd h (by simp)
  case rand w args =>
    split at h
    · rename_i q' hq'
      split at h
      · rename_i hc
        simp only [Option.some.injEq] at h
        subst h
        obtain ⟨x, hl, hx, hr⟩ := one args w hq' (by simpa using hc)
        simp only [denoteExpr, hl, hx]
        exact congrArg CertVal.bv (randV_one hr)
      · exact absurd h (by simp)
    · exact absurd h (by simp)
  case rorBits w args =>
    split at h
    · rename_i q' hq'
      split at h
      · rename_i hc
        simp only [Option.some.injEq] at h
        subst h
        obtain ⟨x, hl, hx, hr⟩ := one args w hq' (by simpa using hc)
        simp only [denoteExpr, hl, hx]
        exact congrArg CertVal.bv (rorBitsV_one hr)
      · exact absurd h (by simp)
    · exact absurd h (by simp)
  case rxor w args =>
    split at h
    · rename_i q' hq'
      split at h
      · rename_i hc
        simp only [Option.some.injEq] at h
        subst h
        obtain ⟨x, hl, hx, hr⟩ := one args w hq' (by simpa using hc)
        simp only [denoteExpr, hl, hx]
        exact congrArg CertVal.bv (rxorV_one hr)
      · exact absurd h (by simp)
    · exact absurd h (by simp)
  case rmux w sel f t =>
    -- the mux's value: `bv_resize` of the arm the select takes
    have hval : ∀ (a : Nat) (x : BV), denoteRef env a = .bv x → RedW w x →
        (bv_nonzero (refBV env sel) = true → a = t) → (bv_nonzero (refBV env sel) = false → a = f) →
        denoteExpr env (.rmux w sel f t) = denoteRef env a := by
      intro a x hx hr ht hf
      simp only [denoteExpr, rmuxV]
      cases hs : bv_nonzero (refBV env sel)
      · rw [ite_eq_right (by simp)]
        rw [← hf hs] at *
        simp only [refBV, hx, CertVal.asBV, resize_redW hr]
      · rw [ite_eq_left rfl]
        rw [← ht hs] at *
        simp only [refBV, hx, CertVal.asBV, resize_redW hr]
    split at h
    · -- equal arms
      rename_i hc
      simp only [Bool.and_eq_true, beq_iff_eq] at hc
      obtain ⟨hft, hwf⟩ := hc
      have hq : f = q := by simpa using h
      subst hq
      obtain ⟨x, hx, hr⟩ := hw f w hwf
      exact hval f x hx hr (fun _ => hft) (fun _ => rfl)
    · split at h
      · rename_i hks
        split at h
        · rename_i sv hsv
          have hsel : refBV env sel = sv := by simp only [refBV, hk sel hks, hsv, CertVal.asBV]
          cases hnz : bv_nonzero sv
          · -- the select is zero: the mux takes `f`
            have h' : wq f = some w ∧ f = q := by simpa [hnz] using h
            obtain ⟨hwf, hq⟩ := h'
            subst hq
            obtain ⟨x, hx, hr⟩ := hw f w hwf
            exact hval f x hx hr (fun hs => by rw [hsel, hnz] at hs; exact absurd hs (by simp))
              (fun _ => rfl)
          · -- the select is nonzero: the mux takes `t`
            have h' : wq t = some w ∧ t = q := by simpa [hnz] using h
            obtain ⟨hwt, hq⟩ := h'
            subst hq
            obtain ⟨x, hx, hr⟩ := hw t w hwt
            exact hval t x hx hr (fun _ => rfl)
              (fun hs => by rw [hsel, hnz] at hs; exact absurd hs (by simp))
        · exact absurd h (by simp)
      · exact absurd h (by simp)
  case rgetMask w a m =>
    split at h
    · rename_i mv hmv
      have h' : ((wq a = some w ∧ kn m = true) ∧ lowMaskCovers w mv = true) ∧ a = q := by
        simpa using h
      obtain ⟨⟨⟨hwa, hkm⟩, hmask⟩, hq⟩ := h'
      subst hq
      obtain ⟨x, hx, hr⟩ := hw a w hwa
      have hm := hk m hkm
      simp only [denoteExpr, rgetMaskV, refBV, hx, hm, hmv, CertVal.asBV]
      exact congrArg CertVal.bv (lowMask_getMask hr hmask)
    · simp at h

/-! ## The walk over the bindings -/

/-- A slot other than the next one reads the same after a push (below: the same entry;
above: out of range on both sides). -/
theorem denoteRef_push_ne {env : SlotEnv} {v : CertVal} {r : Nat} (h : r ≠ env.size) :
    denoteRef (env.push v) r = denoteRef env r := by
  rcases Nat.lt_or_ge r env.size with hl | hg
  · exact denoteRef_push_lt hl
  · have h1 : env.size < r := by omega
    simp [denoteRef, Array.getElem?_eq_none (by simp; omega : (env.push v).size ≤ r),
      Array.getElem?_eq_none (by omega : env.size ≤ r)]

theorem renameExpr_id (e : ResidualExpr) : renameExpr (fun r => r) e = e := by
  cases e <;> simp [renameExpr]

/-- `denoteExpr` reads nothing but its operands. -/
theorem denoteExpr_congr {env env' : SlotEnv} (e : ResidualExpr)
    (h : ∀ r ∈ exprRefs e, denoteRef env' r = denoteRef env r) :
    denoteExpr env' e = denoteExpr env e := by
  have := denoteExpr_rename (σ := fun r => r) e h
  rwa [renameExpr_id] at this

theorem exprRefs_rename (σ : Nat → Nat) (e : ResidualExpr) :
    exprRefs (renameExpr σ e) = (exprRefs e).map σ := by
  cases e <;> simp [exprRefs, renameExpr]

section
variable (R : ResidualProgram) (c : OptCert)

/-- After `k` old bindings, `t` of them kept, with the checker's constants `cenv`, static
widths `wenv` and the kept bindings' renamed expressions `nr`. -/
structure Inv (k t : Nat) (env env' cenv : SlotEnv) (wenv : Array (Option Nat))
    (nr : Array ResidualExpr) : Prop where
  size  : env.size = R.sources.size + k
  size' : env'.size = c.newSources.size + t
  csize : cenv.size = R.sources.size + k
  wsize : wenv.size = R.sources.size + k
  nsize : nr.size = t
  agree : ∀ r, refOK R c cenv (R.sources.size + k) r = true →
            c.σ r < env'.size ∧ denoteRef env' (c.σ r) = denoteRef env r
  /-- a slot the checker knows holds, on this step, the checker's constant -/
  kc    : ∀ r, known R c r = true → denoteRef env r = denoteRef cenv r
  /-- the new constant sources keep their values -/
  src'  : ∀ j w v, c.newSources[j]? = some (.const w v) → denoteRef env' j = .bv (mk_bv w v)
  /-- a slot with a static width holds a reduced vector of that width -/
  ty    : ∀ r w, wenv.getD r none = some w → ∃ x, denoteRef env r = .bv x ∧ RedW w x
  /-- the new program computes, at each kept binding's new slot, its renamed expression -/
  nh    : ∀ t' e, nr[t']? = some e → (∀ r ∈ exprRefs e, r < c.newSources.size + t') ∧
            denoteRef env' (c.newSources.size + t') = denoteExpr env' e

theorem refOK_lt {cenv : SlotEnv} {b r : Nat} (h : refOK R c cenv b r = true) :
    r < R.sources.size ∨ r < b := by
  unfold refOK at h
  by_cases hr : r < R.sources.size
  · exact Or.inl hr
  · simp only [hr, ite_false, Bool.and_eq_true, decide_eq_true_eq] at h
    exact Or.inr h.1

theorem refOK_step {cenv : SlotEnv} {v : CertVal} {k r : Nat}
    (hc : cenv.size = R.sources.size + k)
    (h : refOK R c (cenv.push v) (R.sources.size + k + 1) r = true)
    (hne : r ≠ R.sources.size + k) : refOK R c cenv (R.sources.size + k) r = true := by
  unfold refOK at h ⊢
  by_cases hr : r < R.sources.size
  · simpa [hr] using h
  · simp only [hr, ite_false, Bool.and_eq_true, decide_eq_true_eq] at h ⊢
    refine ⟨by omega, ?_⟩
    rw [denoteRef_push_ne (by omega)] at h
    exact h.2

theorem known_new {k : Nat} : known R c (R.sources.size + k) = isFold c k := by
  unfold known
  rw [ite_eq_right (by omega)]
  congr 1
  omega

theorem getD_push_ne {α : Type} {a : Array α} {x d : α} {r : Nat} (h : r ≠ a.size) :
    (a.push x).getD r d = a.getD r d := by
  simp only [Array.getD_eq_getD_getElem?, Array.getElem?_push]
  rw [ite_eq_right h]

theorem getD_push_size {α : Type} {a : Array α} {x d : α} : (a.push x).getD a.size d = x := by
  simp [Array.getD_eq_getD_getElem?]

theorem inv_zero (i : RuntimeInput) (s : RuntimeState) :
    Inv R c 0 0 (sourceEnvArr R.sources i s) (sourceEnvArr c.newSources i s) (constEnv R)
      (R.sources.map srcWidth) #[] where
  size  := by simp [sourceEnvArr_size]
  size' := by simp [sourceEnvArr_size]
  csize := by simp [constEnv, sourceEnvArr_size]
  wsize := by simp
  nsize := by simp
  agree := by
    intro r h
    unfold refOK at h
    by_cases hr : r < R.sources.size
    · simp only [hr, ite_true, beq_iff_eq] at h
      have hlt : c.σ r < c.newSources.size := by
        rcases Nat.lt_or_ge (c.σ r) c.newSources.size with h3 | h3
        · exact h3
        · rw [Array.getElem?_eq_none h3, Array.getElem?_eq_getElem hr] at h
          simp at h
      refine ⟨by simpa [sourceEnvArr_size] using hlt, ?_⟩
      simp only [denoteRef, sourceEnvArr, Array.getElem?_map, h]
    · simp only [hr, ite_false, Bool.and_eq_true, decide_eq_true_eq] at h
      omega
  kc := by
    intro r h
    unfold known at h
    by_cases hr : r < R.sources.size
    · simp only [hr, ite_true] at h
      split at h
      · rename_i w v hsrc
        simp only [denoteRef, constEnv, sourceEnvArr, Array.getElem?_map, hsrc, Option.map_some,
          Option.getD_some, sourceValue]
      · exact absurd h (by simp)
    · have h1 : (sourceEnvArr R.sources i s).size ≤ r := by simp [sourceEnvArr_size]; omega
      have h2 : (constEnv R).size ≤ r := by simp [constEnv, sourceEnvArr_size]; omega
      simp [denoteRef, Array.getElem?_eq_none h1, Array.getElem?_eq_none h2]
  src' := by
    intro j w v h
    simp only [denoteRef, sourceEnvArr, Array.getElem?_map, h, Option.map_some, Option.getD_some,
      sourceValue]
  ty := by
    intro r w h
    simp only [Array.getD_eq_getD_getElem?, Array.getElem?_map] at h
    cases hs : R.sources[r]? with
    | none => rw [hs] at h; simp at h
    | some sd =>
      rw [hs] at h
      simp only [Option.map_some, Option.getD_some] at h
      obtain ⟨x, hx, hr⟩ := redW_sourceValue i s sd h
      exact ⟨x, by simp only [denoteRef, sourceEnvArr, Array.getElem?_map, hs, Option.map_some,
        Option.getD_some, hx], hr⟩
  nh := by
    intro t' e h
    simp at h

theorem keep_head {k : Nat} {flag : Bool} {rest : List (ResidualBinding × Bool)}
    (hk : (flag :: rest.map Prod.snd) = c.keep.toList.drop k) :
    c.keep.getD k false = flag ∧ rest.map Prod.snd = c.keep.toList.drop (k + 1) := by
  constructor
  · have h1 := congrArg (fun l => l[0]?) hk
    simp only [List.getElem?_cons_zero, List.getElem?_drop, Nat.add_zero] at h1
    rw [Array.getD_eq_getD_getElem?, ← Array.getElem?_toList, ← h1]
    rfl
  · have h2 := congrArg List.tail hk
    simpa [List.tail_drop] using h2

/-- The width map and the environment both grow by the binding's value. -/
theorem ty_push {k : Nat} {env : SlotEnv} {wenv : Array (Option Nat)} {rhs : ResidualExpr}
    (hs : env.size = R.sources.size + k) (hw : wenv.size = R.sources.size + k)
    (ty : ∀ r w, wenv.getD r none = some w → ∃ x, denoteRef env r = .bv x ∧ RedW w x) :
    ∀ r w, (wenv.push (exprWidth rhs)).getD r none = some w →
      ∃ x, denoteRef (env.push (denoteExpr env rhs)) r = .bv x ∧ RedW w x := by
  intro r w h
  by_cases hr : r = R.sources.size + k
  · subst hr
    rw [← hw, getD_push_size] at h
    obtain ⟨x, hx, hrw⟩ := redW_denoteExpr env rhs h
    exact ⟨x, by rw [← hs, denoteRef_push_size, hx], hrw⟩
  · rw [getD_push_ne (by omega)] at h
    rw [denoteRef_push_ne (by omega)]
    exact ty r w h

/-- The step that does not touch the new program (a dropped, folded or aliased binding):
`hnew` gives the new slot's value when the checker lets something read the binding. -/
theorem inv_skip {k t : Nat} {env env' cenv : SlotEnv} {wenv : Array (Option Nat)}
    {nr : Array ResidualExpr} {b : ResidualBinding}
    (hinv : Inv R c k t env env' cenv wenv nr)
    (hnew : refOK R c (cenv.push (cval c cenv k b)) (R.sources.size + k + 1)
        (R.sources.size + k) = true →
      c.σ (R.sources.size + k) < env'.size ∧
        denoteRef env' (c.σ (R.sources.size + k)) = denoteExpr env b.rhs)
    (hknown : isFold c k = true → ∀ r ∈ exprRefs b.rhs, known R c r = true) :
    Inv R c (k + 1) t (env.push (denoteExpr env b.rhs)) env' (cenv.push (cval c cenv k b))
      (wenv.push (exprWidth b.rhs)) nr := {
  size  := by simp [hinv.size]; omega
  size' := hinv.size'
  csize := by simp [hinv.csize]; omega
  wsize := by simp [hinv.wsize]; omega
  nsize := hinv.nsize
  agree := by
    intro r hr
    by_cases hrk : r = R.sources.size + k
    · subst hrk
      obtain ⟨h1, h2⟩ := hnew (by simpa [Nat.add_assoc] using hr)
      refine ⟨h1, ?_⟩
      rw [h2, ← hinv.size, denoteRef_push_size]
    · have hr0 := refOK_step R c hinv.csize (by simpa [Nat.add_assoc] using hr) hrk
      obtain ⟨hlt, heq⟩ := hinv.agree r hr0
      have hrlt : r < env.size := by
        rcases refOK_lt R c hr0 with h | h <;> rw [hinv.size] <;> omega
      exact ⟨hlt, by rw [denoteRef_push_lt hrlt, heq]⟩
  kc := by
    intro r hr
    by_cases hrk : r = R.sources.size + k
    · subst hrk
      rw [known_new] at hr
      rw [← hinv.size, denoteRef_push_size, hinv.size, ← hinv.csize, denoteRef_push_size, cval,
        ite_eq_left hr]
      exact denoteExpr_congr b.rhs (fun r' hr' => hinv.kc r' (hknown hr r' hr'))
    · rw [denoteRef_push_ne (by rw [hinv.size]; exact hrk),
        denoteRef_push_ne (by rw [hinv.csize]; exact hrk)]
      exact hinv.kc r hr
  src' := hinv.src'
  ty := ty_push R hinv.size hinv.wsize hinv.ty
  nh := hinv.nh }

/-- The new slot of a folded binding holds the checker's constant. -/
theorem hnew_fold {k t : Nat} {env env' cenv : SlotEnv} {wenv : Array (Option Nat)}
    {nr : Array ResidualExpr} {b : ResidualBinding}
    (hinv : Inv R c k t env env' cenv wenv nr)
    (hkn : ∀ r ∈ exprRefs b.rhs, known R c r = true)
    (hc : constOK c (R.sources.size + k) (denoteExpr cenv b.rhs) = true) :
    c.σ (R.sources.size + k) < env'.size ∧
      denoteRef env' (c.σ (R.sources.size + k)) = denoteExpr env b.rhs := by
  simp only [constOK] at hc
  split at hc
  · rename_i x hx
    simp only [Bool.and_eq_true, beq_iff_eq] at hc
    obtain ⟨hmk, hsrc⟩ := hc
    have hlt : c.σ (R.sources.size + k) < c.newSources.size := by
      rcases Nat.lt_or_ge (c.σ (R.sources.size + k)) c.newSources.size with h3 | h3
      · exact h3
      · rw [Array.getElem?_eq_none h3] at hsrc; simp at hsrc
    refine ⟨by rw [hinv.size']; omega, ?_⟩
    rw [hinv.src' _ _ _ hsrc, hmk, ← hx]
    exact denoteExpr_congr b.rhs (fun r' hr' => (hinv.kc r' (hkn r' hr')).symm)
  · exact absurd hc (by decide)

/-- The new slot of an aliased binding holds its value. -/
theorem hnew_alias {k t : Nat} {env env' cenv : SlotEnv} {wenv : Array (Option Nat)}
    {nr : Array ResidualExpr} {b : ResidualBinding}
    (hinv : Inv R c k t env env' cenv wenv nr)
    (ha : aliasOK R c cenv wenv nr k b.rhs = true) :
    c.σ (R.sources.size + k) < env'.size ∧
      denoteRef env' (c.σ (R.sources.size + k)) = denoteExpr env b.rhs := by
  unfold aliasOK at ha
  split at ha
  · exact absurd ha (by decide)
  · -- cse
    simp only [Bool.and_eq_true, decide_eq_true_eq, beq_iff_eq, List.all_eq_true] at ha
    obtain ⟨⟨hrefs, hle⟩, hnr⟩ := ha
    obtain ⟨_, hval⟩ := hinv.nh _ _ hnr
    have hsz : c.σ (R.sources.size + k) - c.newSources.size < nr.size := by
      rcases Nat.lt_or_ge (c.σ (R.sources.size + k) - c.newSources.size) nr.size with h3 | h3
      · exact h3
      · rw [Array.getElem?_eq_none h3] at hnr; simp at hnr
    have heq : c.newSources.size + (c.σ (R.sources.size + k) - c.newSources.size)
        = c.σ (R.sources.size + k) := by omega
    rw [heq] at hval
    refine ⟨by rw [hinv.size', ← hinv.nsize]; omega, ?_⟩
    rw [hval]
    exact denoteExpr_rename b.rhs (fun r hr => (hinv.agree r (hrefs r hr)).2)
  · -- copy
    rename_i q _
    simp only [Bool.and_eq_true, beq_iff_eq] at ha
    obtain ⟨⟨hq, hσ⟩, hcp⟩ := ha
    obtain ⟨hlt, heq⟩ := hinv.agree q hq
    refine ⟨by rw [hσ]; exact hlt, ?_⟩
    rw [hσ, heq]
    exact (copyOf_sound (fun r hr => hinv.kc r hr) (fun r w hr => hinv.ty r w hr) hcp).symm

theorem scan_sound : ∀ (pairs : List (ResidualBinding × Bool)) (k t : Nat)
    (env env' cenv : SlotEnv) (wenv : Array (Option Nat)) (nr : Array ResidualExpr),
    pairs.map Prod.snd = c.keep.toList.drop k →
    Inv R c k t env env' cenv wenv nr → scanOK R c pairs k t cenv wenv nr = true →
    ∃ t' wenv' nr', Inv R c (k + pairs.length) t' (runBindings (pairs.map Prod.fst) env)
            (runBindings (keptRenamed c.σ pairs) env') (cenvRun c (pairs.map Prod.fst) k cenv)
            wenv' nr'
  | [], k, t, env, env', cenv, wenv, nr, _, hinv, _ =>
    ⟨t, wenv, nr, by simpa [runBindings, keptRenamed, cenvRun] using hinv⟩
  | (b, true) :: rest, k, t, env, env', cenv, wenv, nr, hk, hinv, hs => by
    simp only [scanOK, Bool.and_eq_true, beq_iff_eq, List.all_eq_true] at hs
    obtain ⟨⟨hrefs, hpos⟩, hrest⟩ := hs
    have hkt := (keep_head c (by simpa using hk)).1
    have hnf : isFold c k = false := by simp [isFold, hkt]
    have hv : denoteExpr env' (renameExpr c.σ b.rhs) = denoteExpr env b.rhs :=
      denoteExpr_rename b.rhs (fun r hr => (hinv.agree r (hrefs r hr)).2)
    have hrlt : ∀ r ∈ exprRefs (renameExpr c.σ b.rhs), r < env'.size := by
      intro r hr
      rw [exprRefs_rename] at hr
      obtain ⟨r0, hr0, rfl⟩ := List.mem_map.mp hr
      exact (hinv.agree r0 (hrefs r0 hr0)).1
    have hinv' : Inv R c (k + 1) (t + 1) (env.push (denoteExpr env b.rhs))
        (env'.push (denoteExpr env' (renameExpr c.σ b.rhs))) (cenv.push (cval c cenv k b))
        (wenv.push (exprWidth b.rhs)) (nr.push (renameExpr c.σ b.rhs)) := {
      size  := by simp [hinv.size]; omega
      size' := by simp [hinv.size']; omega
      csize := by simp [hinv.csize]; omega
      wsize := by simp [hinv.wsize]; omega
      nsize := by simp [hinv.nsize]
      agree := by
        intro r hr
        by_cases hrk : r = R.sources.size + k
        · subst hrk
          rw [hpos]
          refine ⟨by simp [hinv.size'], ?_⟩
          rw [← hinv.size', ← hinv.size, denoteRef_push_size, denoteRef_push_size, hv]
        · have hr0 := refOK_step R c hinv.csize (by simpa [Nat.add_assoc] using hr) hrk
          obtain ⟨hlt, heq⟩ := hinv.agree r hr0
          have hrlt : r < env.size := by
            rcases refOK_lt R c hr0 with h | h <;> rw [hinv.size] <;> omega
          refine ⟨by simp; omega, ?_⟩
          rw [denoteRef_push_lt hlt, denoteRef_push_lt hrlt, heq]
      kc := by
        intro r hr
        by_cases hrk : r = R.sources.size + k
        · subst hrk
          rw [known_new, hnf] at hr
          exact absurd hr (by decide)
        · rw [denoteRef_push_ne (by rw [hinv.size]; exact hrk),
            denoteRef_push_ne (by rw [hinv.csize]; exact hrk)]
          exact hinv.kc r hr
      src' := by
        intro j w v h
        have hj : j < env'.size := by
          rw [hinv.size']
          have : j < c.newSources.size := by
            rcases Nat.lt_or_ge j c.newSources.size with h3 | h3
            · exact h3
            · rw [Array.getElem?_eq_none h3] at h; simp at h
          omega
        rw [denoteRef_push_lt hj]
        exact hinv.src' j w v h
      ty := ty_push R hinv.size hinv.wsize hinv.ty
      nh := by
        intro t' e he
        rw [Array.getElem?_push] at he
        split at he
        · rename_i ht
          simp only [Option.some.injEq] at he
          subst he
          rw [hinv.nsize] at ht
          subst ht
          refine ⟨fun r hr => by have := hrlt r hr; rw [hinv.size'] at this; exact this, ?_⟩
          rw [← hinv.size', denoteRef_push_size]
          exact (denoteExpr_congr _ (fun r hr => denoteRef_push_lt (hrlt r hr))).symm
        · obtain ⟨hlt, hval⟩ := hinv.nh t' e he
          have ht' : t' < nr.size := by
            rcases Nat.lt_or_ge t' nr.size with h3 | h3
            · exact h3
            · rw [Array.getElem?_eq_none h3] at he; simp at he
          have hp : c.newSources.size + t' < env'.size := by rw [hinv.size', ← hinv.nsize]; omega
          refine ⟨hlt, ?_⟩
          rw [denoteRef_push_lt hp, hval]
          exact (denoteExpr_congr _ (fun r hr => denoteRef_push_lt (by have := hlt r hr; omega))).symm }
    have hk' := (keep_head c (by simpa using hk)).2
    obtain ⟨t', wenv', nr', h⟩ := scan_sound rest (k + 1) (t + 1) _ _ _ _ _ hk' hinv' hrest
    refine ⟨t', wenv', nr', ?_⟩
    have hlen : k + (rest.length + 1) = k + 1 + rest.length := by omega
    simpa [runBindings, keptRenamed, cenvRun, List.length_cons, hlen] using h
  | (b, false) :: rest, k, t, env, env', cenv, wenv, nr, hk, hinv, hs => by
    simp only [scanOK, Bool.and_eq_true] at hs
    obtain ⟨hwhy, hrest⟩ := hs
    have hkf := (keep_head c (by simpa using hk)).1
    have hk' := (keep_head c (by simpa using hk)).2
    have hknown : isFold c k = true → ∀ r ∈ exprRefs b.rhs, known R c r = true := by
      intro hf
      rw [ite_eq_left hf] at hwhy
      simpa [List.all_eq_true] using hwhy
    have hinv' := inv_skip R c (b := b) hinv (fun hr => by
        unfold refOK at hr
        rw [ite_eq_right (by omega)] at hr
        simp only [Nat.add_sub_cancel_left, hkf, Bool.false_or, Bool.or_eq_true, Bool.and_eq_true,
          decide_eq_true_eq] at hr
        obtain ⟨_, hr⟩ := hr
        rcases hr with ⟨hf, hc⟩ | ha
        · have hcv : denoteRef (cenv.push (cval c cenv k b)) (R.sources.size + k)
              = denoteExpr cenv b.rhs := by
            rw [← hinv.csize, denoteRef_push_size, cval, ite_eq_left hf]
          rw [hcv] at hc
          exact hnew_fold R c hinv (hknown hf) hc
        · have hnf : isFold c k = false := by
            simp only [isAlias, isFold, hkf, Bool.not_false, Bool.true_and, Bool.and_eq_true,
              Bool.not_eq_true'] at ha ⊢
            exact ha.1
          rw [ite_eq_right (by simp [hnf]), ite_eq_left ha] at hwhy
          exact hnew_alias R c hinv hwhy) hknown
    obtain ⟨t', wenv', nr', h⟩ := scan_sound rest (k + 1) t _ _ _ _ _ hk' hinv' hrest
    refine ⟨t', wenv', nr', ?_⟩
    have hlen : k + (rest.length + 1) = k + 1 + rest.length := by omega
    simpa [runBindings, keptRenamed, cenvRun, List.length_cons, hlen] using h

end

/-! ## The theorems -/

theorem mapIdx_map_congr {α β γ : Type} (a : Array α) (rn : α → β) (f : Nat → β → γ)
    (g : Nat → α → γ) (h : ∀ i (hi : i < a.size), f i (rn a[i]) = g i a[i]) :
    (a.map rn).mapIdx f = a.mapIdx g := by
  apply Array.ext
  · simp
  · intro i h1 h2
    simp only [Array.getElem_mapIdx, Array.getElem_map]
    exact h i (by simpa using h2)

/-- **The checker is sound.** -/
theorem validate_sound {R : ResidualProgram} {c : OptCert} (hv : validate R c = true)
    (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    denoteResidual (applyCert R c) e i s = denoteResidual R e i s := by
  simp only [validate, Bool.and_eq_true, beq_iff_eq] at hv
  obtain ⟨⟨⟨⟨hsize, hscan⟩, hout⟩, hflop⟩, hmem⟩ := hv
  have hlen : R.bindings.toList.length = c.keep.toList.length := by simp [hsize]
  obtain ⟨t', _, _, hinv⟩ := scan_sound R c (R.bindings.toList.zip c.keep.toList) 0 0 _ _ _ _ _
    (by simp [List.map_snd_zip, hlen]) (inv_zero R c i s) hscan
  rw [List.map_fst_zip (by omega)] at hinv
  have hB : 0 + (R.bindings.toList.zip c.keep.toList).length = R.bindings.size := by
    simp [hsize]
  rw [hB] at hinv
  -- every terminal reference reads the same value
  have agreeB : ∀ r, refOK R c (cenvRun c R.bindings.toList 0 (constEnv R))
      (R.sources.size + R.bindings.size) r = true →
      denoteRef (runBindings (keptRenamed c.σ (R.bindings.toList.zip c.keep.toList))
          (sourceEnvArr c.newSources i s)) (c.σ r)
        = denoteRef (runBindings R.bindings.toList (sourceEnvArr R.sources i s)) r :=
    fun r hr => (hinv.agree r hr).2
  have hout' : (R.outputs.map (renameOutput c.σ)).map (fun o => bv_resize o.width (refBV
        (runBindings (keptRenamed c.σ (R.bindings.toList.zip c.keep.toList))
          (sourceEnvArr c.newSources i s)) o.slot))
      = R.outputs.map (fun o => bv_resize o.width (refBV
          (runBindings R.bindings.toList (sourceEnvArr R.sources i s)) o.slot)) := by
    rw [Array.map_map]
    apply Array.map_congr_left
    intro o ho
    simp only [Function.comp, renameOutput, refBV]
    rw [agreeB o.slot ((Array.all_eq_true'.mp hout) o ho)]
  have hfl' : (R.flopUpdates.map (renameFlop c.σ)).mapIdx (fun idx f => flopNext
        (runBindings (keptRenamed c.σ (R.bindings.toList.zip c.keep.toList))
          (sourceEnvArr c.newSources i s)) e s idx f)
      = R.flopUpdates.mapIdx (fun idx f => flopNext
          (runBindings R.bindings.toList (sourceEnvArr R.sources i s)) e s idx f) := by
    apply mapIdx_map_congr
    intro j hj
    apply flopNext_rename
    intro r hr
    exact agreeB r (List.all_eq_true.mp ((Array.all_eq_true.mp hflop) j hj) r hr)
  have hme' : (R.memoryUpdates.map (renameMem c.σ)).mapIdx (fun idx m => memNext
        (runBindings (keptRenamed c.σ (R.bindings.toList.zip c.keep.toList))
          (sourceEnvArr c.newSources i s)) e s idx m)
      = R.memoryUpdates.mapIdx (fun idx m => memNext
          (runBindings R.bindings.toList (sourceEnvArr R.sources i s)) e s idx m) := by
    apply mapIdx_map_congr
    intro j hj
    apply memNext_rename
    intro r hr
    exact agreeB r (List.all_eq_true.mp ((Array.all_eq_true.mp hmem) j hj) r hr)
  simp only [denoteResidual, applyCert, List.toList_toArray, hout', hfl', hme']

/-- **`optimize` never changes a result.** -/
theorem optimize_correct (R : ResidualProgram) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : denoteResidual (optimize R) e i s = denoteResidual R e i s := by
  show denoteResidual (if validate R (optimizeRaw R) = true then (applyCert R (optimizeRaw R), true)
      else (R, false)).1 e i s = _
  by_cases hv : validate R (optimizeRaw R) = true
  · rw [ite_eq_left hv]
    exact validate_sound hv e i s
  · rw [ite_eq_right hv]

end ResidualOpt
end Compiler

#print axioms Compiler.ResidualOpt.denoteExpr_rename
#print axioms Compiler.ResidualOpt.validate_sound
#print axioms Compiler.ResidualOpt.optimize_correct

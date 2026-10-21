/-
`Sym.Mem` -- the memory primitives, symbolically (M3 S4).

An image is `ctor 124 [words, default]` with a CONCRETE word list spine (the
table size is the design's). A symbolic read is an address-decode mux chain
over the words; a symbolic write rebuilds every word as
`hit_j ? updated : old_j`, where `hit_j` is "enabled and the address is `j`".
An ENABLED out-of-range write makes the concrete primitive fail, so the
soundness lemmas -- which assume the concrete step succeeded -- never have to
model it.
-/
import HDLProof.Sym.Prim

open Projection
namespace HDLProof.Sym
open Std.Tactic.BVDecide

/-! ## Lists of symbolic words -/

/-- The elements of a list whose spine is concrete. -/
def sListElems : SVal → Option (List SVal)
  | .conc .nil => some []
  | .conc (.cons h t) => (sListElems (.conc t)).map (.conc h :: ·)
  | .cons h t => (sListElems t).map (h :: ·)
  | _ => none

theorem sListElems_sound {σ : Asg} :
    ∀ (s : SVal) (ss : List SVal) (v : Val), SRel σ s v → sListElems s = some ss →
      ∃ vs, v = listVal vs ∧ SRelL σ ss vs
  | .conc .nil, ss, v, hr, h => by
      simp [sListElems] at h; subst h
      rw [SRel_conc_inv hr]; exact ⟨[], rfl, .nil⟩
  | .conc (.cons a t), ss, v, hr, h => by
      simp only [sListElems, Option.map_eq_some_iff] at h
      obtain ⟨ss', h', rfl⟩ := h
      rw [SRel_conc_inv hr]
      obtain ⟨vs, hv, rl⟩ := sListElems_sound (σ := σ) (.conc t) ss' t .conc h'
      exact ⟨a :: vs, by rw [hv]; rfl, .cons .conc rl⟩
  | .cons a t, ss, v, hr, h => by
      simp only [sListElems, Option.map_eq_some_iff] at h
      obtain ⟨ss', h', rfl⟩ := h
      obtain ⟨va, vt, rfl, ra, rt⟩ := SRel_cons_inv hr
      obtain ⟨vs, hv, rl⟩ := sListElems_sound t ss' vt rt h'
      exact ⟨va :: vs, by rw [hv]; rfl, .cons ra rl⟩
  | .conc (.int _), _, _, _, h => by simp [sListElems] at h
  | .conc (.bool _), _, _, _, h => by simp [sListElems] at h
  | .conc (.ctor _ _), _, _, _, h => by simp [sListElems] at h
  | .int _, _, _, _, h => by simp [sListElems] at h
  | .bool _, _, _, _, h => by simp [sListElems] at h
  | .bv _ _, _, _, _, h => by simp [sListElems] at h
  | .ctor _ _, _, _, _, h => by simp [sListElems] at h

/-- An image's words and default. -/
def imgParts : SVal → Option (List SVal × SVal)
  | .ctor t [wl, d] => if t = memImgTag then (sListElems wl).map (·, d) else none
  | .conc (.ctor t [wl, d]) =>
      if t = memImgTag then (sListElems (.conc wl)).map (·, .conc d) else none
  | _ => none

theorem imgParts_sound {σ : Asg} {s : SVal} {v : Val} {ws : List SVal} {d : SVal}
    (hr : SRel σ s v) (h : imgParts s = some (ws, d)) :
    ∃ vs vd, v = .ctor memImgTag [listVal vs, vd] ∧ SRelL σ ws vs ∧ SRel σ d vd := by
  cases s with
  | ctor t fs =>
      match fs, h with
      | [wl, d'], h =>
        simp only [imgParts] at h
        split at h
        · rename_i ht
          simp only [Option.map_eq_some_iff, Prod.mk.injEq] at h
          obtain ⟨ws', hws, rfl, rfl⟩ := h
          obtain ⟨vs0, rfl, rl⟩ := SRel_ctor_inv hr
          obtain ⟨vw, vs1, rfl, rw', rl'⟩ := SRelL_cons_inv rl
          obtain ⟨vd, vs2, rfl, rd, rl''⟩ := SRelL_cons_inv rl'
          cases rl''
          obtain ⟨vs, rfl, rws⟩ := sListElems_sound wl ws' vw rw' hws
          subst ht
          exact ⟨vs, vd, rfl, rws, rd⟩
        · cases h
  | conc x =>
      cases x with
      | ctor t fs =>
        match fs, h with
        | [wl, d'], h =>
          simp only [imgParts] at h
          split at h
          · rename_i ht
            simp only [Option.map_eq_some_iff, Prod.mk.injEq] at h
            obtain ⟨ws', hws, rfl, rfl⟩ := h
            rw [SRel_conc_inv hr]
            obtain ⟨vs, hv, rws⟩ := sListElems_sound (σ := σ) (.conc wl) ws' wl .conc hws
            subst ht
            exact ⟨vs, d', by rw [← hv], rws, .conc⟩
          · cases h
      | _ => simp [imgParts] at h
  | _ => simp [imgParts] at h

/-- Every word of a symbolic list, as a bit vector of width `W`. -/
def wordsAt (W : Nat) : List SVal → Option (List (BVExpr W))
  | [] => some []
  | s :: ss => do
      let ⟨w, e⟩ ← asSBV s
      if h : w = W then
        let rest ← wordsAt W ss
        pure (castE h e :: rest)
      else none

theorem wordsAt_sound {σ : Asg} {W : Nat} :
    ∀ (ss : List SVal) (es : List (BVExpr W)) (vs : List Val), SRelL σ ss vs → wordsAt W ss = some es →
      es.length = vs.length ∧ ∀ (j : Nat) (e : BVExpr W) (v : Val), es[j]? = some e → vs[j]? = some v →
        ∃ b, asBV v = some b ∧ b.width = W ∧ bv_uint b = ((e.eval σ).toNat : Int)
  | [], es, vs, hr, h => by
      simp [wordsAt] at h; subst h; rw [SRelL_nil hr]; exact ⟨rfl, fun j _ _ h => by simp at h⟩
  | s :: ss, es, vs, hr, h => by
      simp only [wordsAt, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨⟨w, e⟩, he, h⟩ := h
      split at h
      · rename_i hw
        simp only [Option.bind_eq_some_iff, Option.pure_def, Option.some.injEq] at h
        obtain ⟨rest, hrest, rfl⟩ := h
        obtain ⟨v, vs', rfl, rv, rvs⟩ := SRelL_cons_inv hr
        obtain ⟨hl, hall⟩ := wordsAt_sound ss rest vs' rvs hrest
        refine ⟨by simp [hl], fun j e' v' h1 h2 => ?_⟩
        cases j with
        | zero =>
            simp at h1 h2; subst h1; subst h2
            obtain ⟨b, hb, hbw, hu⟩ := asSBV_sound rv he
            exact ⟨b, hb, by rw [hbw]; exact hw, by rw [hu, eval_castE]⟩
        | succ j => exact hall j e' v' (by simpa using h1) (by simpa using h2)
      · cases h

/-! ## The read -/

/-- `addr = j0` selects word `j0`, … past the list, the default. -/
def selChain {W : Nat} (addr : SInt) : List (BVExpr W) → Nat → BVExpr W → BVExpr W
  | [], _, dE => dE
  | e :: es, j0, dE => muxE (SInt.eq addr (SInt.lit j0)) e (selChain addr es (j0 + 1) dE)

theorem valMemWalk_listVal (d : BV) :
    ∀ (vs : List Val) (x : Int), 0 ≤ x →
      valMemWalk (listVal vs) d x = (match vs[x.toNat]? with | some v => (asBV v).getD d | none => d)
  | [], x, _ => by simp [listVal, valMemWalk]
  | v :: vs, x, hx => by
      simp only [listVal, valMemWalk]
      by_cases h0 : x = 0
      · subst h0; simp
      · rw [if_neg h0, valMemWalk_listVal d vs (x - 1) (by omega)]
        have : x.toNat = (x - 1).toNat + 1 := by omega
        rw [this]; simp

theorem selChain_eval {σ : Asg} {W : Nat} {addr : SInt} {x : Nat} (hx : addr.val σ = x) (dE : BVExpr W) :
    ∀ (es : List (BVExpr W)) (j0 : Nat),
      (selChain addr es j0 dE).eval σ =
        (if j0 ≤ x then (match es[x - j0]? with | some e => e.eval σ | none => dE.eval σ) else dE.eval σ)
  | [], j0 => by simp [selChain]
  | e :: es, j0 => by
      simp only [selChain, eval_muxE, SInt.bit_eq, SInt.val_lit, hx]
      rw [selChain_eval hx dE es (j0 + 1)]
      by_cases h : (x : Int) = j0
      · have hn : x = j0 := by omega
        subst hn; simp
      · have hn : x ≠ j0 := by omega
        simp only [h, decide_false]
        by_cases hle : j0 ≤ x
        · have h1 : j0 + 1 ≤ x := by omega
          have h2 : x - j0 = (x - (j0 + 1)) + 1 := by omega
          simp [hle, h1, h2]
        · have h1 : ¬ (j0 + 1 ≤ x) := by omega
          simp [hle, h1]

/-- `en ≠ 0`, as a bit. -/
def nzBit {w : Nat} (e : BVExpr w) : BVExpr 1 := notE (SInt.eq (SInt.ofUint e) (SInt.lit 0))

theorem bit_nzBit {σ : Asg} {w : Nat} {e : BVExpr w} {b : BV} (hu : bv_uint b = ((e.eval σ).toNat : Int)) :
    bitOf ((nzBit e).eval σ) = bv_nonzero b := by
  simp only [nzBit, bit_notE, SInt.bit_eq, SInt.val_ofUint, SInt.val_lit]
  unfold bv_nonzero; rw [hu]; simp

def symMemRead : List SVal → Option SVal
  | [w, img, a, en] => do
      let wz ← cIntOf w
      let (wl, d) ← imgParts img
      let ⟨wd, dE⟩ ← asSBV d
      let ⟨_, A⟩ ← asSBV a
      let ⟨_, E⟩ ← asSBV en
      let words ← wordsAt wd wl
      let W := widthOf wz
      let sel := selChain (SInt.ofUint A) words 0 dE
      pure (.bv W (muxE (nzBit E) (zextE W sel) (.const 0#W)))
  | _ => none

theorem symMemRead_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symMemRead ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .memRead vs = .ok v) : SRel σ s v := by
  match ss, h with
  | [w, img, a, en], h =>
    simp only [symMemRead, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨wz, hw, ⟨wl, d⟩, hip, ⟨wd, dE⟩, hd, ⟨wa, A⟩, ha, ⟨we, E⟩, hen, words, hwords, rfl⟩ := h
    obtain ⟨vw, rest, rfl, rw', rrest⟩ := SRelL_cons_inv hr
    obtain ⟨vimg, rest', rfl, rimg, rrest'⟩ := SRelL_cons_inv rrest
    obtain ⟨va, ven, rfl, ra, ren⟩ := SRelL_two rrest'
    rw [cIntOf_sound rw' hw] at he
    obtain ⟨wvs, vd, rfl, rws, rd⟩ := imgParts_sound rimg hip
    obtain ⟨bd, hbd, hbdw, hud⟩ := asSBV_sound rd hd
    obtain ⟨ba, hba, _, hua⟩ := asSBV_sound ra ha
    obtain ⟨be, hbe, _, hue⟩ := asSBV_sound ren hen
    obtain ⟨hlen, hall⟩ := wordsAt_sound wl words wvs rws hwords
    -- the concrete read
    have hall' : valAllBV (listVal wvs) = true := by
      have : ∀ (vs : List Val), (∀ (j : Nat) (v : Val), vs[j]? = some v → (asBV v).isSome) →
          valAllBV (listVal vs) = true := by
        intro vs; induction vs with
        | nil => intro _; rfl
        | cons x xs ih =>
            intro hx
            simp only [listVal, valAllBV, Bool.and_eq_true]
            exact ⟨hx 0 x rfl, ih (fun j v hv => hx (j + 1) v (by simpa using hv))⟩
      apply this
      intro j v hv
      have hj : j < words.length := by
        rw [hlen]; exact (List.getElem?_eq_some_iff.mp hv).1
      obtain ⟨b, hb, _, _⟩ := hall j (words[j]'hj) v (by simp) hv
      simp [hb]
    simp only [evalPrim, valMemRead, hba, hbe, hbd, hall', memImgTag, Bool.and_true, if_true,
      decide_true, Option.map_some, Except.ok.injEq] at he
    subst he
    refine SRel_ofBV (by split <;> rfl) ?_
    have hx : (SInt.ofUint A).val σ = ((A.eval σ).toNat : Nat) := by rw [SInt.val_ofUint]
    rw [eval_muxE, bit_nzBit hue]
    split
    · rename_i hnz
      rw [resize_rel (A := (selChain (SInt.ofUint A) words 0 dE).eval σ) ?_ _, eval_zextE]
      rw [selChain_eval hx dE words 0, hua, valMemWalk_listVal bd wvs _ (by omega)]
      simp only [Nat.zero_le, if_true, Nat.sub_zero, Int.toNat_natCast]
      cases hv : wvs[(A.eval σ).toNat]? with
      | none =>
          have : words[(A.eval σ).toNat]? = none := by
            rw [List.getElem?_eq_none_iff]; rw [List.getElem?_eq_none_iff] at hv; omega
          rw [this]; exact hud
      | some vv =>
          have hj : (A.eval σ).toNat < words.length := by
            rw [hlen]; exact (List.getElem?_eq_some_iff.mp hv).1
          rw [List.getElem?_eq_getElem hj]
          obtain ⟨b, hb, _, hu⟩ := hall _ _ vv (List.getElem?_eq_getElem hj) hv
          simp only [hb, Option.getD_some]
          exact hu
    · simp [bv_uint_mk_bv]

/-! ## Writes -/

theorem SRelL_of_get {σ : Asg} :
    ∀ (ss : List SVal) (vs : List Val), ss.length = vs.length →
      (∀ (j : Nat) (s : SVal) (v : Val), ss[j]? = some s → vs[j]? = some v → SRel σ s v) → SRelL σ ss vs
  | [], [], _, _ => .nil
  | s :: ss, v :: vs, hl, h =>
      .cons (h 0 s v rfl rfl) (SRelL_of_get ss vs (by simpa using hl)
        (fun j s' v' h1 h2 => h (j + 1) s' v' (by simpa using h1) (by simpa using h2)))
  | [], _ :: _, hl, _ => by simp at hl
  | _ :: _, [], hl, _ => by simp at hl

theorem sListOf_listVal {σ : Asg} : ∀ {ss : List SVal} {vs : List Val}, SRelL σ ss vs →
    SRel σ (sListOf ss) (listVal vs)
  | _, _, .nil => .conc
  | _, _, .cons h1 h2 => .cons h1 (sListOf_listVal h2)

theorem valMemSet_listVal (v : BV) :
    ∀ (vs : List Val) (x : Int), 0 ≤ x → valMemSet (listVal vs) x v = listVal (vs.set x.toNat (ofBV v))
  | [], _, _ => by simp [listVal, valMemSet]
  | a :: vs, x, hx => by
      simp only [listVal, valMemSet]
      by_cases h0 : x = 0
      · subst h0; simp [listVal]
      · rw [if_neg h0, valMemSet_listVal v vs (x - 1) (by omega)]
        have : x.toNat = (x - 1).toNat + 1 := by omega
        rw [this]; simp [listVal]

theorem valLen_listVal : ∀ vs : List Val, valLen (listVal vs) = vs.length
  | [] => rfl
  | _ :: vs => by simp [listVal, valLen, valLen_listVal vs]

/-- The lane mask of a byte-enable: bit `i` is the enable's bit `i / bw`. -/
def laneMask {we : Nat} (W bw : Nat) (BE : BVExpr we) : BVExpr W :=
  zextE W (catBits BE ((List.range W).map (· / bw)))

theorem getLsbD_laneMask (σ : Asg) {we : Nat} (W bw : Nat) (BE : BVExpr we) (i : Nat) (hi : i < W) :
    ((laneMask W bw BE).eval σ).getLsbD i = (BE.eval σ).getLsbD (i / bw) := by
  rw [laneMask, getLsbD_zextE, getLsbD_catBits σ BE _ i (by simpa using hi)]
  simp [hi]

/-- `cert_masked_update`: each bit from the new datum where its lane is enabled. -/
def maskedE {we wd : Nat} (W bw : Nat) (old : BVExpr W) (Dd : BVExpr wd) (BE : BVExpr we) : BVExpr W :=
  let M := laneMask W bw BE
  .bin (.bin (zextE W Dd) .and M) .or (.bin old .and (.un .not M))

theorem maskedE_rel {σ : Asg} {we wd W bw : Nat} {old : BVExpr W} {Dd : BVExpr wd} {BE : BVExpr we}
    {bo bd be : BV} (ho : bv_uint bo = (((old.eval σ)).toNat : Int))
    (hd : bv_uint bd = ((Dd.eval σ).toNat : Int)) (hb : bv_uint be = ((BE.eval σ).toNat : Int)) :
    bv_uint (cert_masked_update W bo bd be bw) = (((maskedE W bw old Dd BE).eval σ).toNat : Int) := by
  unfold cert_masked_update
  apply uint_mk_bits
  intro i hi
  simp only [maskedE, BVExpr.eval_bin, BVExpr.eval_un, BVBinOp.eval, BVUnOp.eval, BitVec.getLsbD_or,
    BitVec.getLsbD_and, BitVec.getLsbD_not, getLsbD_laneMask σ W bw BE i hi, getLsbD_zextE, hi,
    decide_true, Bool.true_and, testBit_eq_bv_bit ho, testBit_eq_bv_bit hd, testBit_eq_bv_bit hb]
  cases (BE.eval σ).getLsbD (i / bw) <;> simp

/-- Word `j` after an update: `hit ? upd : old`. -/
def updWords {W : Nat} (addr : SInt) (en : BVExpr 1) (upd : BVExpr W → BVExpr W) (words : List (BVExpr W)) :
    List SVal :=
  (words.zipIdx).map fun p => .bv W (muxE (andE en (SInt.eq addr (SInt.lit p.2))) (upd p.1) p.1)

theorem updWords_get {W : Nat} {addr : SInt} {en : BVExpr 1} {upd : BVExpr W → BVExpr W}
    {words : List (BVExpr W)} {j : Nat} {e : BVExpr W} (h : words[j]? = some e) :
    (updWords addr en upd words)[j]? =
      some (.bv W (muxE (andE en (SInt.eq addr (SInt.lit j))) (upd e) e)) := by
  simp [updWords, List.getElem?_zipIdx, h]

def symMemWriteBE : List SVal → Option SVal
  | [w, img, a, d, bev, bw] => do
      let wz ← cIntOf w
      let bwz ← cIntOf bw
      let (wl, dflt) ← imgParts img
      let ⟨wd, _⟩ ← asSBV dflt
      let ⟨_, A⟩ ← asSBV a
      let ⟨_, Dd⟩ ← asSBV d
      let ⟨_, BE⟩ ← asSBV bev
      let W := widthOf wz
      let words ← wordsAt W wl
      if wd = W then
        pure (.ctor memImgTag
          [sListOf (updWords (SInt.ofUint A) (nzBit BE) (fun old => maskedE W (widthOf bwz) old Dd BE) words),
           dflt])
      else none
  | _ => none

def symMemWrite : List SVal → Option SVal
  | [img, a, d, en] => do
      let (wl, dflt) ← imgParts img
      let ⟨_, A⟩ ← asSBV a
      let ⟨wdd, Dd⟩ ← asSBV d
      let ⟨_, E⟩ ← asSBV en
      let words ← wordsAt wdd wl
      pure (.ctor memImgTag [sListOf (updWords (SInt.ofUint A) (nzBit E) (fun _ => Dd) words), dflt])
  | _ => none

theorem updWords_rel {σ : Asg} {W : Nat} {addr : SInt} {en : BVExpr 1} {upd : BVExpr W → BVExpr W}
    {words : List (BVExpr W)} {wvs : List Val} (hlen : words.length = wvs.length)
    (hold : ∀ (j : Nat) (e : BVExpr W) (v : Val), words[j]? = some e → wvs[j]? = some v →
      ∃ b, asBV v = some b ∧ b.width = W ∧ bv_uint b = ((e.eval σ).toNat : Int))
    {x : Nat} (hx : addr.val σ = x) {newv : Val}
    (hnew : ∀ e, words[x]? = some e →
      ∃ b, asBV newv = some b ∧ b.width = W ∧ bv_uint b = (((upd e).eval σ).toNat : Int)) :
    (bitOf (en.eval σ) = true → SRelL σ (updWords addr en upd words) (wvs.set x newv))
    ∧ (bitOf (en.eval σ) = false → SRelL σ (updWords addr en upd words) wvs) := by
  have hl : (updWords addr en upd words).length = words.length := by simp [updWords]
  constructor
  · intro hen
    apply SRelL_of_get _ _ (by simp [hl, hlen])
    intro j s v h1 h2
    have hj : j < words.length := by rw [← hl]; exact (List.getElem?_eq_some_iff.mp h1).1
    have he := List.getElem?_eq_getElem hj
    rw [updWords_get he] at h1
    cases h1
    by_cases hjx : j = x
    · subst hjx
      rw [List.getElem?_set_self (by omega)] at h2
      cases h2
      obtain ⟨b, hb, hbw, hu⟩ := hnew _ he
      refine .bv hb hbw ?_
      rw [eval_muxE, bit_andE, hen, SInt.bit_eq, SInt.val_lit, hx]
      simpa using hu
    · rw [List.getElem?_set_ne (Ne.symm hjx)] at h2
      obtain ⟨b, hb, hbw, hu⟩ := hold j _ v he h2
      refine .bv hb hbw ?_
      have hne : (x : Int) ≠ (j : Int) := by omega
      rw [eval_muxE, bit_andE, hen, SInt.bit_eq, SInt.val_lit, hx]
      simpa [hne] using hu
  · intro hen
    apply SRelL_of_get _ _ (by simp [hl, hlen])
    intro j s v h1 h2
    have hj : j < words.length := by rw [← hl]; exact (List.getElem?_eq_some_iff.mp h1).1
    have he := List.getElem?_eq_getElem hj
    rw [updWords_get he] at h1
    cases h1
    obtain ⟨b, hb, hbw, hu⟩ := hold j _ v he h2
    refine .bv hb hbw ?_
    rw [eval_muxE, bit_andE, hen]
    simpa using hu

theorem valAllBV_of_words {wvs : List Val}
    (h : ∀ (j : Nat) (v : Val), wvs[j]? = some v → (asBV v).isSome) : valAllBV (listVal wvs) = true := by
  induction wvs with
  | nil => rfl
  | cons x xs ih =>
      simp only [listVal, valAllBV, Bool.and_eq_true]
      exact ⟨h 0 x rfl, ih (fun j v hv => h (j + 1) v (by simpa using hv))⟩

theorem words_allBV {σ : Asg} {W : Nat} {words : List (BVExpr W)} {wvs : List Val}
    (hlen : words.length = wvs.length)
    (hall : ∀ (j : Nat) (e : BVExpr W) (v : Val), words[j]? = some e → wvs[j]? = some v →
      ∃ b, asBV v = some b ∧ b.width = W ∧ bv_uint b = ((e.eval σ).toNat : Int)) :
    valAllBV (listVal wvs) = true := by
  apply valAllBV_of_words
  intro j v hv
  have hj : j < words.length := by rw [hlen]; exact (List.getElem?_eq_some_iff.mp hv).1
  obtain ⟨b, hb, _, _⟩ := hall j (words[j]'hj) v (by simp) hv
  simp [hb]

theorem symMemWriteBE_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symMemWriteBE ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .memWriteBE vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [w, img, a, d, bev, bw], h =>
    simp only [symMemWriteBE, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def] at h
    obtain ⟨wz, hw, bwz, hbw, ⟨wl, dflt⟩, hip, ⟨wd, dE⟩, hdf, ⟨wa, A⟩, ha, ⟨wdd, Dd⟩, hd, ⟨wbe, BE⟩, hbe,
      words, hwords, h⟩ := h
    split at h
    · rename_i hwd
      simp only [Option.some.injEq] at h
      subst h
      obtain ⟨vw, r1, rfl, rw', rr1⟩ := SRelL_cons_inv hr
      obtain ⟨vimg, r2, rfl, rimg, rr2⟩ := SRelL_cons_inv rr1
      obtain ⟨va, r3, rfl, ra, rr3⟩ := SRelL_cons_inv rr2
      obtain ⟨vd, vbe, vbw, rfl, rd, rbe, rbw'⟩ := SRelL_three rr3
      rw [cIntOf_sound rw' hw, cIntOf_sound rbw' hbw] at he
      obtain ⟨wvs, vdf, rfl, rws, rdf⟩ := imgParts_sound rimg hip
      obtain ⟨bdf, hbdf, hbdfw, hudf⟩ := asSBV_sound rdf hdf
      obtain ⟨ba, hba, _, hua⟩ := asSBV_sound ra ha
      obtain ⟨bdd, hbdd, _, hudd⟩ := asSBV_sound rd hd
      obtain ⟨bbe, hbbe, _, hube⟩ := asSBV_sound rbe hbe
      obtain ⟨hlen, hall⟩ := wordsAt_sound wl words wvs rws hwords
      have hallbv := words_allBV hlen hall
      simp only [evalPrim, valMemWriteBE, hba, hbdd, hbbe, hbdf, hallbv, memImgTag, Bool.and_true,
        if_true, decide_true] at he
      have hx : (SInt.ofUint A).val σ = ((A.eval σ).toNat : Nat) := by rw [SInt.val_ofUint]
      have hen := bit_nzBit (e := BE) hube
      have hrel := updWords_rel (en := nzBit BE) (upd := fun old => maskedE (widthOf wz) (widthOf bwz) old Dd BE)
        hlen hall hx
        (newv := ofBV (cert_masked_update (widthOf wz)
          (valMemWalk (listVal wvs) bdf (bv_uint ba)) bdd bbe (widthOf bwz)))
        (fun e hex => by
          refine ⟨_, asBV_ofBV _, rfl, ?_⟩
          rw [hua, valMemWalk_listVal bdf wvs _ (by omega), Int.toNat_natCast]
          have hj : (A.eval σ).toNat < wvs.length := by
            rw [← hlen]; exact (List.getElem?_eq_some_iff.mp hex).1
          obtain ⟨vx, hvx⟩ : ∃ vx, wvs[(A.eval σ).toNat]? = some vx :=
            ⟨_, List.getElem?_eq_getElem hj⟩
          rw [hvx]
          obtain ⟨b, hb, _, hu⟩ := hall _ e vx hex hvx
          simp only [hb, Option.getD_some]
          exact maskedE_rel hu hudd hube)
      cases hnz : bv_nonzero bbe
      · simp only [hnz, Bool.false_eq_true, if_false, Except.ok.injEq] at he
        subst he
        exact .ctor (.cons (sListOf_listVal (hrel.2 (by rw [hen]; exact hnz))) (.cons rdf .nil))
      · cases hin : valInRange (listVal wvs) (bv_uint ba)
        · simp [hnz, hin] at he
        · simp only [hnz, hin, if_true, Except.ok.injEq] at he
          subst he
          rw [hua, valMemSet_listVal _ wvs _ (by omega), Int.toNat_natCast]
          rw [hua] at hrel
          exact .ctor (.cons (sListOf_listVal (hrel.1 (by rw [hen]; exact hnz))) (.cons rdf .nil))
    · cases h

theorem symMemWrite_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symMemWrite ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .memWrite vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [img, a, d, en], h =>
    simp only [symMemWrite, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨⟨wl, dflt⟩, hip, ⟨wa, A⟩, ha, ⟨wdd, Dd⟩, hd, ⟨we, E⟩, hen', words, hwords, rfl⟩ := h
    obtain ⟨vimg, r1, rfl, rimg, rr1⟩ := SRelL_cons_inv hr
    obtain ⟨va, vd, ven, rfl, ra, rd, ren⟩ := SRelL_three rr1
    obtain ⟨wvs, vdf, rfl, rws, rdf⟩ := imgParts_sound rimg hip
    obtain ⟨ba, hba, _, hua⟩ := asSBV_sound ra ha
    obtain ⟨bdd, hbdd, hbddw, hudd⟩ := asSBV_sound rd hd
    obtain ⟨be, hbe, _, hue⟩ := asSBV_sound ren hen'
    obtain ⟨hlen, hall⟩ := wordsAt_sound wl words wvs rws hwords
    have hallbv := words_allBV hlen hall
    -- the default must be a BV for the concrete write to succeed
    simp only [evalPrim, valMemWrite, hba, hbdd, hbe, hallbv, memImgTag, Bool.and_true, if_true,
      decide_true] at he
    have hx : (SInt.ofUint A).val σ = ((A.eval σ).toNat : Nat) := by rw [SInt.val_ofUint]
    have hen := bit_nzBit (e := E) hue
    have hrel := updWords_rel (en := nzBit E) (upd := fun _ => Dd) hlen hall hx
      (newv := ofBV bdd) (fun e _ => ⟨bdd, asBV_ofBV _, hbddw, hudd⟩)
    cases hbdf : asBV vdf with
    | none => simp [hbdf] at he
    | some bdf =>
      cases hnz : bv_nonzero be
      · simp only [hbdf, hnz, Bool.false_eq_true, if_false, Except.ok.injEq] at he
        subst he
        exact .ctor (.cons (sListOf_listVal (hrel.2 (by rw [hen]; exact hnz))) (.cons rdf .nil))
      · cases hin : valInRange (listVal wvs) (bv_uint ba)
        · simp [hbdf, hnz, hin] at he
        · simp only [hbdf, hnz, hin, if_true, Except.ok.injEq] at he
          subst he
          rw [hua, valMemSet_listVal _ wvs _ (by omega), Int.toNat_natCast]
          exact .ctor (.cons (sListOf_listVal (hrel.1 (by rw [hen]; exact hnz))) (.cons rdf .nil))

end HDLProof.Sym

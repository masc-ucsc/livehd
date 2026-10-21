/-
`Check.RefMapSound` -- the refinement-map check and its soundness.

The check runs the conjunctive checker on the implementation's invariant. Then, per case of the
invariant's split, it takes the template state and does two implementation steps (cycles `n`
and `n+1`). It builds the map's state `α(n)` from cycle `n`'s outputs, runs the spec chain over
the slots (`chainS`), and asks the solver to refute one obligation per TARGET:

- `comm g`:   the spec flops in `g`, after the chain, are not `α(n+1)`'s;
- `allow j`:  slot `j` fires but its channel is not allowed;
- `obs j`:    slot `j` fires but its observed words are not the spec's;
- `active`:   the active output holds at `n` but not at `n+1`;
- `rankLe c`, `rankLt c`, `rankSel c`: channel `c` is allowed in `α(n)` and gets no event, but
  its rank rises, or does not fall on a helpful cycle, or stays while the selector moves (or
  its requirement is not enabled).

Every step obligation assumes the cycle's assumptions, the target's invariant hypotheses at `n`,
and the active output at `n`. A BASE obligation, from the initial state through `R0` cycles,
refutes "the active output or the initial map fails at `R0`".

`runRefMapOf_sound`: if the check answers `true`, then on every run of the implementation from
`s0` the facts of `RefMapFacts` hold. These are what `Check.RefMapCompose` turns into a refinement
of the channel spec `specOf`. k = 1. Mathlib-free.
-/
import HDLProof.Check.RefMap
import HDLProof.Sym.Refine

open Projection
namespace HDLProof.Check
open HDLProof.Sym HDLProof.Kit Std.Tactic.BVDecide Projection.MemProjection Projection.Hw
  Projection.HwMem Compiler

/-! ## Concrete facts of a cycle -/

/-- The ports of cycle `n`. -/
noncomputable def portsAt (DI : DesignCert) (s0 : StateM) (ins : Nat → RuntimeInput) (n : Nat) :
    Ports :=
  ⟨outM DI s0 ins n, outM DI s0 ins (n + 1), ins n⟩

/-- A word's normalized value, as a natural number. -/
def wordAt (outs : Array BV) (o : Nat) : Nat := (bv_uint (outs.getD o default)).toNat

/-- The selector: `0` internal work, `j+1` waiting on requirement `j`. -/
def selOf (outs : Array BV) (o : Nat) : Option Nat :=
  if wordAt outs o = 0 then none else some (wordAt outs o - 1)

def helpfulB (sp : RefMapSpec) (C : Ports) (so : Nat) : Bool :=
  match selOf C.outs so with
  | none => true
  | some j => match sp.reqs[j]? with
    | some r => r.fires.bit C
    | none => false

/-- Some slot of channel `c` fires. -/
def firesChan (sp : RefMapSpec) (C : Ports) (c : Nat) : Bool :=
  sp.slots.any fun sl => sl.chan == c && sl.kind.bit C

/-- The rank obligations of cycle `C` from map state `σ`. -/
def RankOK (DS : DesignCert) (sp : RefMapSpec) (σ : StateM) (C : Ports) : Prop :=
  ∀ c ch ro so, sp.chans[c]? = some ch → ch.rank = some (ro, so) →
    propBit (specOuts DS sp σ ch []) ch.allow = true → firesChan sp C c = false →
      wordAt C.outsNext ro ≤ wordAt C.outs ro ∧
      (helpfulB sp C so = true → wordAt C.outsNext ro < wordAt C.outs ro) ∧
      (wordAt C.outsNext ro = wordAt C.outs ro →
        selOf C.outsNext so = selOf C.outs so ∧
        ∀ j, selOf C.outs so = some j → ∃ r, sp.reqs[j]? = some r ∧ r.en.bit C = true)

def ActiveAt (sp : RefMapSpec) (outs : Array BV) : Prop :=
  match sp.active with
  | none => True
  | some p => propBit outs p = true

/-- The map's state at `R0` is the declared initial one. -/
def InitOK (sp : RefMapSpec) (outs0 : Array BV) (σ : StateM) : Prop :=
  ∀ (k : Nat) (e : InitE), sp.init[k]? = some e →
    match e with
    | InitE.cnst v => ∃ b, σ.flops[k]? = some b ∧ bv_uint b = v
    | InitE.out0 o => ∃ b, σ.flops[k]? = some b ∧ bv_uint b = bv_uint (outs0.getD o default)
    | InitE.free => True

/-- A memory-free spec state of the declared widths whose flops are normalized. -/
def SpecNorm (sp : RefMapSpec) (σ : StateM) : Prop :=
  SpecSt sp σ ∧ σ.flops.toList.map normBV = σ.flops.toList

/-- The cycles up to `n` meet the assumptions. -/
def Good (DI : DesignCert) (sp : RefMapSpec) (s0 : StateM) (ins : Nat → RuntimeInput) (n : Nat) :
    Prop :=
  ∀ j, j ≤ n → asmBit (outM DI s0 ins j) (ins j) sp.inv.asmOut sp.inv.asmIn = true

structure RefMapFacts (DI DS : DesignCert) (sp : RefMapSpec) (s0 : StateM)
    (ins : Nat → RuntimeInput) : Prop where
  base : Good DI sp s0 ins sp.R0 →
    InitOK sp (outM DI s0 ins 0) (alphaSt sp (outM DI s0 ins sp.R0))
    ∧ ActiveAt sp (outM DI s0 ins sp.R0) ∧ SpecNorm sp (alphaSt sp (outM DI s0 ins sp.R0))
  step : ∀ n, sp.R0 ≤ n → Good DI sp s0 ins (n + 1) → ActiveAt sp (outM DI s0 ins n) →
    CycleOK DS sp (portsAt DI s0 ins n) (alphaSt sp (outM DI s0 ins n)) sp.slots
      (alphaSt sp (outM DI s0 ins (n + 1)))
    ∧ ActiveAt sp (outM DI s0 ins (n + 1))
    ∧ RankOK DS sp (alphaSt sp (outM DI s0 ins n)) (portsAt DI s0 ins n)
    ∧ SpecNorm sp (alphaSt sp (outM DI s0 ins n))
    ∧ SpecNorm sp (alphaSt sp (outM DI s0 ins (n + 1)))

/-! ## Literals -/

/-- Output word `o` of an output vector value, as a leaf. -/
def outLeaf (so : SVal) (o : Nat) : Option Leaf := sNth so o >>= bvOfS

theorem outLeaf_sound {σ : Asg} {so : SVal} {outs : Array BV} (ho : SRel σ so (encBVs outs))
    {o : Nat} {l : Leaf} (h : outLeaf so o = some l) : ∃ b, outs[o]? = some b ∧ LeafRel σ l b :=
  nth_sym_sound ho h

/-- Flop `k` of two state values is equal. -/
def flopEqLit (s t : SVal) (k : Nat) : Option BVLogicalExpr := do
  let x ← flopE s k
  let y ← flopE t k
  pure (eqBvE x y)

theorem flopEqLit_sound {σ : Asg} {s t : SVal} {st1 st2 : StateM}
    (h1 : SRel σ s (StateM.encStateM st1)) (h2 : SRel σ t (StateM.encStateM st2)) {k : Nat}
    {φ : BVLogicalExpr} (h : flopEqLit s t k = some φ) :
    ∃ b1 b2, st1.flops[k]? = some b1 ∧ st2.flops[k]? = some b2
      ∧ φ.eval σ = decide (bv_uint b1 = bv_uint b2) := by
  simp only [flopEqLit, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨⟨w1, e1⟩, he1, ⟨w2, e2⟩, he2, rfl⟩ := h
  obtain ⟨b1, hb1, _, hu1⟩ := flopE_sound h1 he1
  obtain ⟨b2, hb2, _, hu2⟩ := flopE_sound h2 he2
  exact ⟨b1, b2, hb1, hb2, eval_eqBvE hu1 hu2⟩

/-- Every flop of group `g` is equal. -/
def groupLit (s t : SVal) (g : List Nat) : Option BVLogicalExpr :=
  (g.mapM (flopEqLit s t)).map conjE

theorem groupLit_sound {σ : Asg} {s t : SVal} {st1 st2 : StateM}
    (h1 : SRel σ s (StateM.encStateM st1)) (h2 : SRel σ t (StateM.encStateM st2)) :
    ∀ (g : List Nat) (φ : BVLogicalExpr), groupLit s t g = some φ → φ.eval σ = true →
      ∀ k ∈ g, ∃ b1 b2, st1.flops[k]? = some b1 ∧ st2.flops[k]? = some b2 ∧ bv_uint b1 = bv_uint b2
  | [], φ, _, _, k, hk => by simp at hk
  | k :: g, φ, h, hev, k', hk' => by
      simp only [groupLit, Option.map_eq_some_iff] at h
      obtain ⟨ls, hls, rfl⟩ := h
      simp only [List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at hls
      obtain ⟨x, hx, xs, hxs, rfl⟩ := hls
      simp only [conjE, eval_and, Bool.and_eq_true] at hev
      rcases List.mem_cons.mp hk' with rfl | hk'
      · obtain ⟨b1, b2, hb1, hb2, he⟩ := flopEqLit_sound h1 h2 hx
        rw [he] at hev
        exact ⟨b1, b2, hb1, hb2, by simpa using hev.1⟩
      · exact groupLit_sound h1 h2 g (conjE xs) (by simp [groupLit, hxs]) hev.2 k' hk'

/-- The observed words of a slot equal the spec's outputs, word by word. -/
def obsLits (P : SPorts) (so : SVal) : List ObsW → List Nat → Option (List BVLogicalExpr)
  | [], [] => some []
  | w :: ws, o :: os => do
      let e ← w.sym P
      let y ← outLeaf so o
      let rest ← obsLits P so ws os
      pure (eqBvE ⟨w.len, e⟩ y :: rest)
  | _, _ => none

theorem obsLits_sound {σ : Asg} {P : SPorts} {C : Ports} (hP : PortsRel σ P C) {so : SVal}
    {outs : Array BV} (ho : SRel σ so (encBVs outs)) :
    ∀ (ws : List ObsW) (os : List Nat) (ls : List BVLogicalExpr), obsLits P so ws os = some ls →
      (conjE ls).eval σ = true → ws.map (fun w => (w.val C).getD 0) = outVals outs os
  | [], [], ls, _, _ => rfl
  | w :: ws, o :: os, ls, h, hev => by
      simp only [obsLits, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨e, he, y, hy, rest, hrest, rfl⟩ := h
      simp only [conjE, eval_and, Bool.and_eq_true] at hev
      obtain ⟨v, hv, hve⟩ := ObsW.sym_sound hP w e he
      obtain ⟨b, hb, hlb⟩ := outLeaf_sound ho hy
      have hlt : (e.eval σ).toNat < 2 ^ w.len := (e.eval σ).isLt
      have hmk : bv_uint (mk_bv w.len v) = ((e.eval σ).toNat : Int) := by
        simp only [bv_uint, mk_bv, Int.emod_emod]
        rw [← hve]
        exact Int.emod_eq_of_lt (by omega) (by exact_mod_cast hlt)
      have heq := eval_eqBvE (x := ⟨w.len, e⟩) (bx := mk_bv w.len v) (by' := b) hmk hlb.2
      rw [heq] at hev
      have h1 : bv_uint (mk_bv w.len v) = bv_uint b := by simpa using hev.1
      have hv' : v = bv_uint b := by rw [← h1, hmk, hve]
      simp only [List.map_cons, outVals, hv, Option.getD_some, Array.getD_eq_getD_getElem?, hb,
        Option.getD_some]
      rw [hv']
      congr 1
      have := obsLits_sound hP ho ws os rest hrest hev.2
      simpa [outVals, Array.getD_eq_getD_getElem?] using this
  | [], _ :: _, _, h, _ => by simp [obsLits] at h
  | _ :: _, [], _, h, _ => by simp [obsLits] at h

/-! ## Rank literals -/

def leLit (x y : Leaf) : BVLogicalExpr := advLit (SInt.le (SInt.ofUint x.2) (SInt.ofUint y.2))
def ltLit (x y : Leaf) : BVLogicalExpr := advLit (SInt.lt (SInt.ofUint x.2) (SInt.ofUint y.2))
def eqcLit (x : Leaf) (v : Nat) : BVLogicalExpr := advLit (eqConstBit x.2 v)
def leConstLit (x : Leaf) (v : Nat) : BVLogicalExpr := advLit (SInt.le (SInt.ofUint x.2) (SInt.lit v))

theorem eval_leLit {σ : Asg} {x y : Leaf} {bx by' : BV} (hx : LeafRel σ x bx) (hy : LeafRel σ y by') :
    (leLit x y).eval σ = decide (bv_uint bx ≤ bv_uint by') := by
  simp only [leLit, eval_advLit, SInt.bit_le, SInt.val_ofUint, ← hx.2, ← hy.2]

theorem eval_ltLit {σ : Asg} {x y : Leaf} {bx by' : BV} (hx : LeafRel σ x bx) (hy : LeafRel σ y by') :
    (ltLit x y).eval σ = decide (bv_uint bx < bv_uint by') := by
  simp only [ltLit, eval_advLit, SInt.bit_lt, SInt.val_ofUint, ← hx.2, ← hy.2]

theorem eval_eqcLit {σ : Asg} {x : Leaf} {bx : BV} (hx : LeafRel σ x bx) (v : Nat) :
    (eqcLit x v).eval σ = decide (bv_uint bx = v) := by
  obtain ⟨w, e⟩ := x
  simp only [eqcLit, eval_advLit]
  exact bit_eqConstBit v hx.2

theorem eval_leConstLit {σ : Asg} {x : Leaf} {bx : BV} (hx : LeafRel σ x bx) (v : Nat) :
    (leConstLit x v).eval σ = decide (bv_uint bx ≤ v) := by
  simp only [leConstLit, eval_advLit, SInt.bit_le, SInt.val_ofUint, SInt.val_lit, ← hx.2]

theorem wordAt_of {outs : Array BV} {o : Nat} {b : BV} (h : outs[o]? = some b) :
    (wordAt outs o : Int) = bv_uint b := by
  simp only [wordAt, Array.getD_eq_getD_getElem?, h, Option.getD_some]
  exact Int.toNat_of_nonneg (bv_uint_nonneg' b)

/-- The cycle is helpful: the selector is 0, or `j+1` with requirement `j` firing. -/
def helpfulLit (sp : RefMapSpec) (P : SPorts) (sA : Leaf) : Option BVLogicalExpr := do
  let fs ← (List.range sp.reqs.length).mapM fun j => do
    let r ← sp.reqs[j]?
    let f ← r.fires.sym P
    pure (conjE [eqcLit sA (j + 1), advLit f])
  pure (disjE (eqcLit sA 0 :: fs))

/-- The awaited requirement is a declared one and is enabled. -/
def reqEnLit (sp : RefMapSpec) (P : SPorts) (sA : Leaf) : Option BVLogicalExpr := do
  let es ← (List.range sp.reqs.length).mapM fun j => do
    let r ← sp.reqs[j]?
    let e ← r.en.sym P
    pure (disjE [.not (eqcLit sA (j + 1)), advLit e])
  pure (conjE (leConstLit sA sp.reqs.length :: es))

theorem mapM_range_spec {α : Type} {f : Nat → Option α} :
    ∀ (n : Nat) (xs : List α), (List.range n).mapM f = some xs →
      xs.length = n ∧ ∀ j (hj : j < xs.length), f j = some (xs[j]'hj) := by
  intro n
  induction n with
  | zero => intro xs h; simp at h; subst h; exact ⟨rfl, fun j hj => by simp at hj⟩
  | succ n ih =>
    intro xs h
    rw [List.range_succ, List.mapM_append] at h
    simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, List.mapM_cons, List.mapM_nil,
      Option.pure_def, Option.some.injEq] at h
    obtain ⟨ys, hys, zs, ⟨z, hz, a, rfl, rfl⟩, rfl⟩ := h
    obtain ⟨hl, hf⟩ := ih ys hys
    refine ⟨by simp [hl], fun j hj => ?_⟩
    rcases Nat.lt_or_ge j ys.length with h | h
    · rw [List.getElem_append_left h]; exact hf j h
    · have : j = n := by simp at hj; omega
      subst this
      rw [List.getElem_append_right (by omega)]
      simp [hl, hz]

theorem helpfulLit_sound {σ : Asg} {sp : RefMapSpec} {P : SPorts} {C : Ports} (hP : PortsRel σ P C)
    {sA : Leaf} {so : Nat} {b : BV} (hb : C.outs[so]? = some b) (hs : LeafRel σ sA b)
    {φ : BVLogicalExpr} (h : helpfulLit sp P sA = some φ) : φ.eval σ = helpfulB sp C so := by
  simp only [helpfulLit, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨fs, hfs, rfl⟩ := h
  obtain ⟨hl, hf⟩ := mapM_range_spec _ fs hfs
  rw [eval_disjE, List.any_cons, eval_eqcLit hs]
  have hw := wordAt_of hb
  unfold helpfulB selOf
  by_cases h0 : wordAt C.outs so = 0
  · have : bv_uint b = 0 := by omega
    simp [h0, this]
  · have hpos : bv_uint b ≠ 0 := by omega
    simp only [h0, if_false, show decide (bv_uint b = ((0 : Nat) : Int)) = false by simpa using hpos,
      Bool.false_or]
    have key : ∀ j (hj : j < fs.length), (fs[j]'hj).eval σ =
        (decide (bv_uint b = ((j + 1 : Nat) : Int)) && (sp.reqs[j]?.map fun r => r.fires.bit C).getD false) := by
      intro j hj
      have := hf j hj
      simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at this
      obtain ⟨r, hr, f, hfr, hrf⟩ := this
      rw [← hrf]
      simp only [conjE, eval_and, eval_eqcLit hs, eval_advLit, BitE.sym_sound hP r.fires f hfr, hr,
        Option.map_some, Option.getD_some]
      simp
    cases hrq : sp.reqs[wordAt C.outs so - 1]? with
    | none =>
      simp only [hrq]
      rw [List.any_eq_false]
      intro x hx
      obtain ⟨j, hj, rfl⟩ := List.getElem_of_mem hx
      rw [key j hj]
      by_cases hv : bv_uint b = ((j + 1 : Nat) : Int)
      · have : wordAt C.outs so - 1 = j := by omega
        rw [this] at hrq
        simp [hrq]
      · have hv' : ¬ bv_uint b = (j : Int) + 1 := by simpa using hv
        simp [hv']
    | some r =>
      simp only [hrq]
      have hjlt : wordAt C.outs so - 1 < fs.length := by
        rw [hl]; exact (List.getElem?_eq_some_iff.mp hrq).1
      apply Bool.eq_iff_iff.mpr
      rw [List.any_eq_true]
      constructor
      · rintro ⟨x, hx, hxe⟩
        obtain ⟨j, hj, rfl⟩ := List.getElem_of_mem hx
        rw [key j hj] at hxe
        simp only [Bool.and_eq_true, decide_eq_true_eq] at hxe
        have : wordAt C.outs so - 1 = j := by omega
        rw [this] at hrq
        rw [hrq] at hxe
        simpa using hxe.2
      · intro hfire
        refine ⟨_, List.getElem_mem hjlt, ?_⟩
        rw [key _ hjlt, hrq]
        simp only [Option.map_some, Option.getD_some, hfire, Bool.and_true, decide_eq_true_eq]
        omega

theorem reqEnLit_sound {σ : Asg} {sp : RefMapSpec} {P : SPorts} {C : Ports} (hP : PortsRel σ P C)
    {sA : Leaf} {so : Nat} {b : BV} (hb : C.outs[so]? = some b) (hs : LeafRel σ sA b)
    {φ : BVLogicalExpr} (h : reqEnLit sp P sA = some φ) (hev : φ.eval σ = true) :
    ∀ j, selOf C.outs so = some j → ∃ r, sp.reqs[j]? = some r ∧ r.en.bit C = true := by
  simp only [reqEnLit, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨es, hes, rfl⟩ := h
  obtain ⟨hl, hf⟩ := mapM_range_spec _ es hes
  simp only [conjE, eval_and, Bool.and_eq_true, eval_leConstLit hs, decide_eq_true_eq] at hev
  obtain ⟨hle, hall⟩ := hev
  rw [eval_conjE, List.all_eq_true] at hall
  intro j hj
  have hw := wordAt_of hb
  unfold selOf at hj
  split at hj
  · cases hj
  · rename_i h0
    simp only [Option.some.injEq] at hj
    have hjl : j < es.length := by rw [hl]; omega
    have := hf j hjl
    simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at this
    obtain ⟨r, hr, e, he, hre⟩ := this
    have hx := hall _ (List.getElem_mem hjl)
    rw [← hre] at hx
    simp only [eval_disjE, List.any_cons, List.any_nil, Bool.or_false, eval_not, eval_eqcLit hs,
      eval_advLit, BitE.sym_sound hP r.en e he] at hx
    refine ⟨r, hr, ?_⟩
    have : bv_uint b = ((j + 1 : Nat) : Int) := by omega
    simpa [this] using hx

/-! ## The spec's outputs with the response inputs at 0 -/

/-- The spec's input vector for an event on `ch` with every response word 0. -/
def specIn0S (inW : List Nat) (ch : ChanS) : SVal :=
  sListOf (((List.range inW.length).map fun k => constLeaf (inW.getD k 0) (inVal ch [] k)).map
    fun p => SVal.bv p.1 p.2)

theorem specIn0S_rel (σ : Asg) (inW : List Nat) (ch : ChanS) :
    SRel σ (specIn0S inW ch) (encBVs (specIn inW ch [])) := by
  have : ∀ ks : List Nat, LeavesRel σ (ks.map fun k => constLeaf (inW.getD k 0) (inVal ch [] k))
      (ks.map fun k => mk_bv (inW.getD k 0) (inVal ch [] k)) := by
    intro ks
    induction ks with
    | nil => exact .nil
    | cons k ks ih => exact .cons (constLeaf_rel σ _ _) ih
  have h2 := sListOf_rel ofBV (leaves_relLF (this (List.range inW.length)))
  unfold specIn0S encBVs encArr specIn
  rw [List.toList_toArray]
  exact h2

/-- Per channel, the spec's outputs at the map's state with zero responses (ranked ones only). -/
def en0S (DS : DesignCert) (RS : Program) (sp : RefMapSpec) (σ0 : SVal) :
    SymSt → List ChanS → Except String (List (Option SVal) × SymSt)
  | st, [] => .ok ([], st)
  | st, ch :: chs =>
      if ch.rank.isSome then
        match symStep RS sp.fuelS st (.conc (encEdges (edgesAll DS))) (specIn0S sp.specInW ch) σ0 with
        | .error m => .error m
        | .ok (sres, st1) =>
          match splitResult sres with
          | none => .error "the spec's answer is not a result record"
          | some (_, so) =>
            match en0S DS RS sp σ0 st1 chs with
            | .error m => .error m
            | .ok (rest, st2) => .ok (some so :: rest, st2)
      else
        match en0S DS RS sp σ0 st chs with
        | .error m => .error m
        | .ok (rest, st2) => .ok (none :: rest, st2)

theorem en0S_extends {DS : DesignCert} {RS : Program} {sp : RefMapSpec} {σ0 : SVal} :
    ∀ (chs : List ChanS) (st : SymSt) (ens : List (Option SVal)) (st' : SymSt),
      en0S DS RS sp σ0 st chs = .ok (ens, st') → st.Extends st'
  | [], st, ens, st', h => by
      simp only [en0S, Except.ok.injEq, Prod.mk.injEq] at h; obtain ⟨-, rfl⟩ := h; exact .refl _
  | ch :: chs, st, ens, st', h => by
      unfold en0S at h
      split at h
      · split at h
        · cases h
        · rename_i sres st1 h1
          split at h
          · cases h
          · split at h
            · cases h
            · rename_i rest st2 h2
              simp only [Except.ok.injEq, Prod.mk.injEq] at h
              obtain ⟨-, rfl⟩ := h
              exact (symStep_extends h1).trans (en0S_extends chs _ _ _ h2)
      · split at h
        · cases h
        · rename_i rest st2 h2
          simp only [Except.ok.injEq, Prod.mk.injEq] at h
          obtain ⟨-, rfl⟩ := h
          exact en0S_extends chs _ _ _ h2

theorem en0S_sound {DS : DesignCert} {RS : Program} {sp : RefMapSpec} {σ0 : SVal}
    (hrS : MemResidual DS RS) (hAS : admitsWB DS = true) (hside : specSideOK DS sp = true)
    {σ : Asg} {stF : SymSt} (hc : stF.Consistent σ) {σC : StateM} (hσ : SpecSt sp σC)
    (hS : SRel σ σ0 (StateM.encStateM σC)) :
    ∀ (chs : List ChanS) (st : SymSt) (ens : List (Option SVal)) (st' : SymSt),
      en0S DS RS sp σ0 st chs = .ok (ens, st') → st'.Extends stF →
      ens.length = chs.length ∧
      ∀ i (hi : i < chs.length) so, ens[i]? = some (some so) →
        SRel σ so (encBVs (specOuts DS sp σC (chs[i]'hi) []))
  | [], st, ens, st', h, _ => by
      simp only [en0S, Except.ok.injEq, Prod.mk.injEq] at h; obtain ⟨rfl, -⟩ := h
      exact ⟨rfl, fun i hi => by simp at hi⟩
  | ch :: chs, st, ens, st', h, hx => by
      unfold en0S at h
      split at h
      · split at h
        · cases h
        · rename_i sres st1 h1
          split at h
          · cases h
          · rename_i sn so hsplit
            split at h
            · cases h
            · rename_i rest st2 h2
              simp only [Except.ok.injEq, Prod.mk.injEq] at h
              obtain ⟨rfl, rfl⟩ := h
              have x1 : st1.Extends stF := (en0S_extends chs _ _ _ h2).trans hx
              have rres := symStep_eval (hrS hAS (spec_runtime hside hσ ch [])) h1 hc x1 .conc
                (specIn0S_rel σ sp.specInW ch) hS
              obtain ⟨_, ro⟩ := splitResult_sound rres hsplit
              obtain ⟨ihl, ih⟩ := en0S_sound hrS hAS hside hc hσ hS chs st1 rest st2 h2 hx
              refine ⟨by simp [ihl], fun i hi so' hso' => ?_⟩
              cases i with
              | zero => simp only [List.getElem?_cons_zero, Option.some.injEq] at hso'; subst hso'; exact ro
              | succ i => exact ih i (by simpa using hi) so' (by simpa using hso')
      · split at h
        · cases h
        · rename_i rest st2 h2
          simp only [Except.ok.injEq, Prod.mk.injEq] at h
          obtain ⟨rfl, rfl⟩ := h
          obtain ⟨ihl, ih⟩ := en0S_sound hrS hAS hside hc hσ hS chs st rest st2 h2 hx
          refine ⟨by simp [ihl], fun i hi so' hso' => ?_⟩
          cases i with
          | zero => simp at hso'
          | succ i => exact ih i (by simpa using hi) so' (by simpa using hso')

/-! ## Targets, runs and obligations -/

inductive RTarget where
  | active
  | comm (g : List Nat)
  | allow (j : Nat)
  | obs (j : Nat)
  | rankLe (c : Nat)
  | rankLt (c : Nat)
  | rankSel (c : Nat)
  deriving Repr, Inhabited, DecidableEq

def rankedChans (sp : RefMapSpec) : List Nat :=
  (List.range sp.chans.length).filter fun c => match sp.chans[c]? with
    | some ch => ch.rank.isSome
    | none => false

def targetsRM (sp : RefMapSpec) : List RTarget :=
  (if sp.active.isSome then [.active] else []) ++ sp.groups.map .comm
    ++ (List.range sp.slots.length).map .allow ++ (List.range sp.slots.length).map .obs
    ++ (rankedChans sp).flatMap fun c => [.rankLe c, .rankLt c, .rankSel c]

/-- One case's symbolic run: two implementation steps, the map's states, the spec chain, the
spec's zero-response outputs at the map's state. -/
structure RunRM where
  a    : StepO
  b    : StepO
  st   : SymSt
  recs : List SlotRec
  σ0   : SVal
  σ1   : SVal
  σe   : SVal
  ens  : List (Option SVal)

/-- The ports of cycle `n` in a step run. -/
def portsS (sp : RefMapSpec) (a b : StepO) : SPorts :=
  ⟨a.2.1, b.2.1, inputVar sp.inv.inW (layoutSize sp.inv.L)⟩

def rmRun (DI DS : DesignCert) (RI RS : Program) (sp : RefMapSpec) (cas : Case) :
    Except String RunRM :=
  match stepRunT DI RI sp.inv cas with
  | .ok ([a, b], st) =>
    match outLeaves a.2.1 sp.alpha sp.LS.fws, outLeaves b.2.1 sp.alpha sp.LS.fws with
    | some l0, some l1 =>
      match chainS DS RS sp (portsS sp a b) st (leafState l0) sp.slots with
      | .ok (recs, σe, st1) =>
        match en0S DS RS sp (leafState l0) st1 sp.chans with
        | .ok (ens, st2) => .ok ⟨a, b, st2, recs, leafState l0, leafState l1, σe, ens⟩
        | .error m => .error m
      | .error m => .error m
    | _, _ => .error "an alpha word is missing or not of its spec flop's width"
  | .ok _ => .error "expected two steps"
  | .error m => .error m

/-- Channel `c`'s rank context: allowed at the map's state, and none of its slots fires. -/
def rankCtx (sp : RefMapSpec) (R : RunRM) (c : Nat) :
    Option (Nat × Nat × BVLogicalExpr × BVLogicalExpr) := do
  let ch ← sp.chans[c]?
  let (ro, so) ← ch.rank
  let so0 ← (R.ens[c]?).join
  let en ← propOf so0 ch.allow
  let nf := conjE ((R.recs.zip sp.slots).filterMap fun (r, sl) =>
    if sl.chan = c then some (.not (advLit r.kind)) else none)
  pure (ro, so, en, nf)

/-- What a target says must not happen. -/
def badLit (sp : RefMapSpec) (R : RunRM) : RTarget → Option BVLogicalExpr
  | .active => match sp.active with
      | none => some (.const false)
      | some p => (propOf R.b.2.1 p).map .not
  | .comm g => (groupLit R.σ1 R.σe g).map .not
  | .allow j => do
      let r ← R.recs[j]?
      let al ← propOf r.so r.ch.allow
      pure (conjE [advLit r.kind, .not al])
  | .obs j => do
      let r ← R.recs[j]?
      let sl ← sp.slots[j]?
      let ls ← obsLits (portsS sp R.a R.b) r.so sl.obs r.ch.obsOut
      pure (conjE [advLit r.kind, .not (conjE ls)])
  | .rankLe c => do
      let (ro, _, en, nf) ← rankCtx sp R c
      let ra ← outLeaf R.a.2.1 ro
      let rb ← outLeaf R.b.2.1 ro
      pure (conjE [en, nf, .not (leLit rb ra)])
  | .rankLt c => do
      let (ro, so, en, nf) ← rankCtx sp R c
      let ra ← outLeaf R.a.2.1 ro
      let rb ← outLeaf R.b.2.1 ro
      let sa ← outLeaf R.a.2.1 so
      let hl ← helpfulLit sp (portsS sp R.a R.b) sa
      pure (conjE [en, nf, hl, .not (ltLit rb ra)])
  | .rankSel c => do
      let (ro, so, en, nf) ← rankCtx sp R c
      let ra ← outLeaf R.a.2.1 ro
      let rb ← outLeaf R.b.2.1 ro
      let sa ← outLeaf R.a.2.1 so
      let sb ← outLeaf R.b.2.1 so
      let re ← reqEnLit sp (portsS sp R.a R.b) sa
      pure (conjE [en, nf, eqBvE rb ra, .not (conjE [eqBvE sb sa, re])])

/-- The invariant conjuncts target `t` assumes. -/
def hypsRM (sp : RefMapSpec) (t : Nat) : List Nat :=
  match sp.hyps[t]? with
  | some (some l) => l
  | _ => List.range sp.inv.conjs.length

/-- What every step obligation assumes: both cycles' assumptions, the target's hypotheses at
`n` (on the case's template state), and the active output at `n`. -/
def commonLits (sp : RefMapSpec) (cas : Case) (R : RunRM) (t : Nat) :
    Option (List BVLogicalExpr) := do
  let s0V := stateVarT sp.inv.L (sp.inv.tmplOf cas) 0
  let hs ← (hypsRM sp t).mapM fun i => sp.inv.conjs[i]? >>= holdsE s0V R.a.2.1
  let act ← match sp.active with
    | none => some (.const true)
    | some p => propOf R.a.2.1 p
  pure [R.a.1, conjE hs, R.b.1, act]

def oblRM (sp : RefMapSpec) (cas : Case) (R : RunRM) (ti : Nat) (t : RTarget) :
    Option BVLogicalExpr := do
  let cs ← commonLits sp cas R ti
  let bad ← badLit sp R t
  pure (coi R.st (conjE (cs ++ [bad])))

/-! ## The base -/

/-- `R0 + 1` steps from the initial state. -/
def baseRunRM (DI : DesignCert) (RI : Program) (sp : RefMapSpec) (s0 : StateM) :
    Except String (List StepO × SymSt) :=
  unrollO DI RI sp.inv.fuel sp.inv.asmOut sp.inv.asmIn sp.inv.inW (sp.R0 + 1) 0
    (.conc (StateM.encStateM s0)) ⟨(sp.R0 + 1) * sp.inv.inW.length, #[]⟩

def initLit (σR o0 : SVal) (k : Nat) : InitE → Option BVLogicalExpr
  | .cnst v => valLit σR (k, v)
  | .out0 o => do
      let x ← flopE σR k
      let y ← outLeaf o0 o
      pure (eqBvE x y)
  | .free => some (.const true)

def initLits (σR o0 : SVal) : List InitE → Nat → Option (List BVLogicalExpr)
  | [], _ => some []
  | e :: es, k => do
      let x ← initLit σR o0 k e
      let r ← initLits σR o0 es (k + 1)
      pure (x :: r)

/-- BASE: the assumptions hold up to `R0`, and the active output or the initial map fails. -/
def baseOblRM (sp : RefMapSpec) (st : SymSt) (steps : List StepO) : Option BVLogicalExpr := do
  let s0 ← steps[0]?
  let sR ← steps[sp.R0]?
  let ls ← outLeaves sR.2.1 sp.alpha sp.LS.fws
  let inits ← initLits (leafState ls) s0.2.1 sp.init 0
  let act ← match sp.active with
    | none => some (.const true)
    | some p => propOf sR.2.1 p
  pure (coi st (conjE [conjE (steps.map (·.1)), .not (conjE (act :: inits))]))

/-! ## The check -/

def casesRM (sp : RefMapSpec) : List Case :=
  match sp.split with
  | some s => match sp.inv.conjs[s]? with
    | some (.oneOf cs) => cs
    | _ => [[]]
  | none => [[]]

/-- The declared shapes: the map has a word per spec flop, every spec flop is in a group, and
the spec is memory-free. -/
def sideRM (DS : DesignCert) (sp : RefMapSpec) : Bool :=
  specSideOK DS sp && (sp.alpha.length == sp.LS.fws.length) && (sp.init.length == sp.LS.fws.length)
    && (List.range sp.LS.fws.length).all (fun k => sp.groups.any (·.contains k))
    && sp.LS.mds.isEmpty

def checkRM (sp : RefMapSpec) (rb : Except String (List StepO × SymSt))
    (runs : List (Case × Except String RunRM)) (pfB : ByteArray) (pfS : List (List ByteArray)) :
    Bool :=
  (match rb with
    | .ok (steps, st) =>
        scopedB st && (match baseOblRM sp st steps with
          | some φ => checkUnsat φ pfB
          | none => false)
    | _ => false)
  && (casesRM sp).zipIdx.all fun (cas, m) =>
       match findRun runs cas with
       | some (.ok R) =>
           scopedB R.st && (targetsRM sp).zipIdx.all fun (t, ti) =>
             match oblRM sp cas R ti t with
             | some φ => checkUnsat φ ((pfS.getD ti []).getD m ByteArray.empty)
             | none => false
       | _ => false

/-- **The refinement-map check.** `pfIB`/`pfIS` are the invariant's proofs, `pfB` the base's,
`pfS[t][m]` target `t`'s in case `m`. -/
def runRefMapOf (DI DS : DesignCert) (RI RS : Program) (sp : RefMapSpec) (s0 : StateM)
    (pfIB : ByteArray) (pfIS : List (List ByteArray)) (pfB : ByteArray)
    (pfS : List (List ByteArray)) : Bool :=
  runConjTOf DI RI sp.inv s0 pfIB pfIS && sideRM DS sp
    && checkRM sp (baseRunRM DI RI sp s0)
         ((casesRM sp).map fun c => (c, rmRun DI DS RI RS sp c)) pfB pfS

/-! ## Taking the check apart -/

section
variable {DI DS : DesignCert} {RI RS : Program} {sp : RefMapSpec} {s0 : StateM}
  {pfIB pfB : ByteArray} {pfIS pfS : List (List ByteArray)}

theorem rm_parts (h : runRefMapOf DI DS RI RS sp s0 pfIB pfIS pfB pfS = true) :
    runConjTOf DI RI sp.inv s0 pfIB pfIS = true ∧ sideRM DS sp = true
    ∧ (∃ steps st, baseRunRM DI RI sp s0 = .ok (steps, st) ∧ scopedB st = true
        ∧ ∃ φ, baseOblRM sp st steps = some φ ∧ BVLogicalExpr.Unsat φ)
    ∧ (∀ cas ∈ casesRM sp, ∃ R, rmRun DI DS RI RS sp cas = .ok R ∧ scopedB R.st = true
        ∧ ∀ ti (hti : ti < (targetsRM sp).length), ∃ φ,
            oblRM sp cas R ti ((targetsRM sp)[ti]'hti) = some φ ∧ BVLogicalExpr.Unsat φ) := by
  unfold runRefMapOf checkRM at h
  simp only [Bool.and_eq_true] at h
  obtain ⟨⟨hconj, hside⟩, hb, hs⟩ := h
  refine ⟨hconj, hside, ?_, ?_⟩
  · split at hb
    · rename_i steps st hrun
      simp only [Bool.and_eq_true] at hb
      refine ⟨steps, st, hrun, hb.1, ?_⟩
      cases ho : baseOblRM sp st steps with
      | none => rw [ho] at hb; exact absurd hb.2 (by simp)
      | some φ => rw [ho] at hb; exact ⟨φ, rfl, checkUnsat_sound hb.2⟩
    · cases hb
  · intro cas hcas
    rw [List.all_eq_true] at hs
    obtain ⟨m, hmlt, hm⟩ := List.getElem_of_mem hcas
    have h2 := hs (cas, m) (List.mem_zipIdx_iff_getElem?.mpr (List.getElem?_eq_some_iff.mpr ⟨hmlt, hm⟩))
    simp only at h2
    split at h2
    · rename_i R hfind
      have hrun := findRun_map (rmRun DI DS RI RS sp) _ cas _ hfind
      simp only [Bool.and_eq_true] at h2
      refine ⟨R, hrun.symm, h2.1, fun ti hti => ?_⟩
      have h3 := (List.all_eq_true.mp h2.2) ((targetsRM sp)[ti]'hti, ti)
        (List.mem_zipIdx_iff_getElem?.mpr (by simp))
      simp only at h3
      cases ho : oblRM sp cas R ti ((targetsRM sp)[ti]'hti) with
      | none => rw [ho] at h3; cases h3
      | some φ => rw [ho] at h3; exact ⟨φ, rfl, checkUnsat_sound h3⟩
    · cases h2

theorem rmRun_parts {cas : Case} {R : RunRM} (h : rmRun DI DS RI RS sp cas = .ok R) :
    ∃ st l0 l1 st1, stepRunT DI RI sp.inv cas = .ok ([R.a, R.b], st)
      ∧ outLeaves R.a.2.1 sp.alpha sp.LS.fws = some l0 ∧ R.σ0 = leafState l0
      ∧ outLeaves R.b.2.1 sp.alpha sp.LS.fws = some l1 ∧ R.σ1 = leafState l1
      ∧ chainS DS RS sp (portsS sp R.a R.b) st R.σ0 sp.slots = .ok (R.recs, R.σe, st1)
      ∧ en0S DS RS sp R.σ0 st1 sp.chans = .ok (R.ens, R.st) := by
  unfold rmRun at h
  split at h
  · rename_i a b st hst
    split at h
    · rename_i l0 l1 h0 h1
      split at h
      · rename_i recs σe st1 hch
        split at h
        · rename_i ens st2 hen
          simp only [Except.ok.injEq] at h
          subst h
          exact ⟨st, l0, l1, st1, hst, h0, rfl, h1, rfl, hch, hen⟩
        · cases h
      · cases h
    · cases h
  · cases h
  · cases h

end

/-! ## Concrete states -/

theorem outsVals_norm (outs : Array BV) (os : List Nat) :
    (outsVals outs os).map normBV = outsVals outs os := by
  simp [outsVals, normBV_normBV]

theorem leavesRel_widths {σ : Asg} :
    ∀ {ls : List Leaf} {bs : List BV}, LeavesRel σ ls bs → bs.map BV.width = ls.map Sigma.fst
  | _, _, .nil => rfl
  | _, _, .cons h hs => by simp [h.1, leavesRel_widths hs]

/-- The map's state is a normalized spec state when its words have the declared widths. -/
theorem alphaSt_norm {σ : Asg} {sp : RefMapSpec} {so : SVal} {outs : Array BV}
    (ho : SRel σ so (encBVs outs)) {ls : List Leaf} (h : outLeaves so sp.alpha sp.LS.fws = some ls) :
    SpecNorm sp (alphaSt sp outs) ∧ SRel σ (leafState ls) (StateM.encStateM (alphaSt sp outs)) := by
  obtain ⟨hr, hw⟩ := outLeaves_rel ho sp.alpha sp.LS.fws ls h
  refine ⟨⟨⟨rfl, ?_⟩, ?_⟩, leafState_rel hr⟩
  · simp only [alphaSt, flopSt, List.toList_toArray]
    rw [leavesRel_widths hr, hw]
  · simp only [alphaSt, flopSt, List.toList_toArray, outsVals_norm]

theorem normSt_norm (s : StateM) (n : Nat) :
    (normSt s n).flops.toList.map normBV = (normSt s n).flops.toList := by
  have : (normBV ∘ normBV) = normBV := funext normBV_normBV
  simp [normSt, flopSt, this]

/-- Two normalized spec states with equal flop values are equal. -/
theorem specNorm_ext {sp : RefMapSpec} {σ1 σ2 : StateM} (h1 : SpecNorm sp σ1) (h2 : SpecNorm sp σ2)
    (heq : ∀ k, k < sp.LS.fws.length → ∃ b1 b2, σ1.flops[k]? = some b1 ∧ σ2.flops[k]? = some b2
      ∧ bv_uint b1 = bv_uint b2) : σ1 = σ2 := by
  obtain ⟨⟨hm1, hw1⟩, hn1⟩ := h1
  obtain ⟨⟨hm2, hw2⟩, hn2⟩ := h2
  cases σ1 with
  | mk f1 m1 =>
  cases σ2 with
  | mk f2 m2 =>
  simp only at hm1 hm2 hw1 hw2 hn1 hn2
  subst hm1; subst hm2
  have hl1 : f1.toList.length = sp.LS.fws.length := by rw [← hw1]; simp
  have hl2 : f2.toList.length = sp.LS.fws.length := by rw [← hw2]; simp
  have : f1.toList = f2.toList := by
    apply List.ext_getElem (by omega)
    intro k hk1 hk2
    obtain ⟨b1, b2, hb1, hb2, hu⟩ := heq k (by omega)
    have e1 : f1.toList[k] = b1 := (List.getElem?_eq_some_iff.mp (by simpa using hb1)).2
    have e2 : f2.toList[k] = b2 := (List.getElem?_eq_some_iff.mp (by simpa using hb2)).2
    have w1 : b1.width = sp.LS.fws[k]'(by omega) := by
      have := congrArg (·[k]?) hw1
      simp only [List.getElem?_map, List.getElem?_eq_getElem hk1, e1, Option.map_some] at this
      rw [List.getElem?_eq_getElem (by omega)] at this
      exact Option.some.inj this
    have w2 : b2.width = sp.LS.fws[k]'(by omega) := by
      have := congrArg (·[k]?) hw2
      simp only [List.getElem?_map, List.getElem?_eq_getElem hk2, e2, Option.map_some] at this
      rw [List.getElem?_eq_getElem (by omega)] at this
      exact Option.some.inj this
    have n1 : normBV b1 = b1 := by
      have := congrArg (·[k]?) hn1
      simp only [List.getElem?_map, List.getElem?_eq_getElem hk1, e1, Option.map_some] at this
      exact Option.some.inj this
    have n2 : normBV b2 = b2 := by
      have := congrArg (·[k]?) hn2
      simp only [List.getElem?_map, List.getElem?_eq_getElem hk2, e2, Option.map_some] at this
      exact Option.some.inj this
    rw [e1, e2, ← n1, ← n2]
    simp only [normBV, w1, w2, hu]
  simp only [StateM.mk.injEq, and_true]
  exact Array.ext' this

theorem chainC_norm {DS : DesignCert} {sp : RefMapSpec} {C : Ports} (hAS : admitsWB DS = true)
    (hside : specSideOK DS sp = true) :
    ∀ (sls : List SlotS) (σ : StateM), SpecNorm sp σ → SpecNorm sp (chainC DS sp C σ sls)
  | [], σ, h => h
  | sl :: sls, σ, h => by
      simp only [chainC]
      split
      · split
        · rename_i ch _
          exact chainC_norm hAS hside sls _
            ⟨specStep_specSt hAS hside h.1 ch (respOf C sl), normSt_norm _ _⟩
        · exact chainC_norm hAS hside sls σ h
      · exact chainC_norm hAS hside sls σ h

/-! ## From the chain's records to the cycle's facts -/

theorem recsOK_at {σ : Asg} {DS : DesignCert} {sp : RefMapSpec} {C : Ports} :
    ∀ (recs : List SlotRec) (σC : StateM) (sls : List SlotS), RecsOK σ DS sp C recs σC sls →
      recs.length = sls.length ∧ ∀ (j : Nat) (r : SlotRec) (sl : SlotS), recs[j]? = some r → sls[j]? = some sl →
        bitOf (r.kind.eval σ) = sl.kind.bit C ∧ sp.chans[sl.chan]? = some r.ch
  | [], _, [], _ => ⟨rfl, fun j r sl h => by simp at h⟩
  | r :: rs, σC, sl :: sls, ⟨hb, hch, _, hrest⟩ => by
      obtain ⟨hl, ih⟩ := recsOK_at rs _ sls hrest
      refine ⟨by simp [hl], fun j r' sl' hr hs => ?_⟩
      cases j with
      | zero =>
        simp only [List.getElem?_cons_zero, Option.some.injEq] at hr hs
        subst hr; subst hs; exact ⟨hb, hch⟩
      | succ j => exact ih j r' sl' (by simpa using hr) (by simpa using hs)
  | [], _, _ :: _, h => by simp [RecsOK] at h
  | _ :: _, _, [], h => by simp [RecsOK] at h

/-- Each fired slot allowed and observed as the spec says gives the cycle's facts. -/
theorem cycleOK_of {σ : Asg} {DS : DesignCert} {sp : RefMapSpec} {C : Ports} {P : SPorts}
    (hP : PortsRel σ P C) :
    ∀ (sls : List SlotS) (recs : List SlotRec) (σC : StateM), RecsOK σ DS sp C recs σC sls →
      (∀ (j : Nat) (r : SlotRec) (sl : SlotS), recs[j]? = some r → sls[j]? = some sl → bitOf (r.kind.eval σ) = true →
        (∃ al, propOf r.so r.ch.allow = some al ∧ al.eval σ = true) ∧
        (∃ ls, obsLits P r.so sl.obs r.ch.obsOut = some ls ∧ (conjE ls).eval σ = true)) →
      CycleOK DS sp C σC sls (chainC DS sp C σC sls)
  | [], [], σC, _, _ => rfl
  | sl :: sls, r :: rs, σC, ⟨hb, hch, hso, hrest⟩, hfacts => by
      have hfacts' : ∀ (j : Nat) (r' : SlotRec) (sl' : SlotS), rs[j]? = some r' → sls[j]? = some sl' →
          bitOf (r'.kind.eval σ) = true →
          (∃ al, propOf r'.so r'.ch.allow = some al ∧ al.eval σ = true) ∧
          (∃ ls, obsLits P r'.so sl'.obs r'.ch.obsOut = some ls ∧ (conjE ls).eval σ = true) :=
        fun j r' sl' h1 h2 h3 => hfacts (j + 1) r' sl' (by simpa using h1) (by simpa using h2) h3
      simp only [CycleOK, chainC]
      by_cases hk : sl.kind.bit C = true
      · simp only [hk, if_true, hch]
        simp only [hk, if_true] at hrest
        obtain ⟨⟨al, hal, hale⟩, ⟨ls, hls, hlse⟩⟩ := hfacts 0 r sl rfl rfl (by rw [hb, hk])
        refine ⟨r.ch, rfl, ?_, ?_, cycleOK_of hP sls rs _ hrest hfacts'⟩
        · rw [← propOf_sound hso hal]; exact hale
        · exact obsLits_sound hP hso sl.obs r.ch.obsOut ls hls hlse
      · have hk' : sl.kind.bit C = false := by simpa using hk
        simp only [hk', Bool.false_eq_true, if_false]
        simp only [hk', Bool.false_eq_true, if_false] at hrest
        exact cycleOK_of hP sls rs σC hrest hfacts'
  | [], _ :: _, _, h, _ => by simp [RecsOK] at h
  | _ :: _, [], _, h, _ => by simp [RecsOK] at h

/-- The "no slot of channel `c` fires" literal. -/
def nofireLits (c : Nat) (recs : List SlotRec) (sls : List SlotS) : List BVLogicalExpr :=
  (recs.zip sls).filterMap fun (r, sl) => if sl.chan = c then some (.not (advLit r.kind)) else none

theorem nofire_eval {σ : Asg} {C : Ports} (c : Nat) :
    ∀ (recs : List SlotRec) (sls : List SlotS), recs.length = sls.length →
      (∀ (j : Nat) (r : SlotRec) (sl : SlotS), recs[j]? = some r → sls[j]? = some sl → bitOf (r.kind.eval σ) = sl.kind.bit C) →
      (conjE (nofireLits c recs sls)).eval σ = !(sls.any fun sl => sl.chan == c && sl.kind.bit C)
  | [], [], _, _ => rfl
  | r :: rs, sl :: sls, hl, hb => by
      have ih := nofire_eval c rs sls (by simpa using hl)
        (fun j r' sl' h1 h2 => hb (j + 1) r' sl' (by simpa using h1) (by simpa using h2))
      have h0 := hb 0 r sl rfl rfl
      simp only [nofireLits, List.zip_cons_cons, List.filterMap_cons, List.any_cons] at ih ⊢
      by_cases hc : sl.chan = c
      · simp only [hc, if_true, beq_self_eq_true, Bool.true_and, conjE, eval_and, eval_not,
          eval_advLit, h0]
        rw [ih]; simp [Bool.not_or]
      · simp only [hc, if_false, show (sl.chan == c) = false by simpa using hc, Bool.false_and,
          Bool.false_or]
        exact ih
  | [], _ :: _, hl, _ => by simp at hl
  | _ :: _, [], hl, _ => by simp at hl

/-- A refuted obligation whose common part holds leaves its bad part false. -/
theorem obl_bad_false {σ : Asg} {sp : RefMapSpec} {cas : Case} {R : RunRM} {ti : Nat} {t : RTarget}
    {φ : BVLogicalExpr} (hc : R.st.Consistent σ) (h : oblRM sp cas R ti t = some φ) (hU : φ.Unsat)
    (hcom : ∀ cs, commonLits sp cas R ti = some cs → (conjE cs).eval σ = true) :
    ∃ bad, badLit sp R t = some bad ∧ bad.eval σ = false := by
  simp only [oblRM, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨cs, hcs, bad, hbad, rfl⟩ := h
  refine ⟨bad, hbad, ?_⟩
  cases hb : bad.eval σ
  · rfl
  · exfalso
    have hev := eval_coi hc (body := conjE (cs ++ [bad])) (by
      rw [eval_conjE, List.all_append, ← eval_conjE, hcom cs hcs]
      simp [hb])
    rw [hU] at hev; cases hev

theorem commonLits_true {σ : Asg} {sp : RefMapSpec} {cas : Case} {R : RunRM} {ti : Nat}
    {st : StateM} {outs : Array BV}
    (hS : SRel σ (stateVarT sp.inv.L (sp.inv.tmplOf cas) 0) (StateM.encStateM st))
    (ho : SRel σ R.a.2.1 (encBVs outs)) (hA : R.a.1.eval σ = true) (hB : R.b.1.eval σ = true)
    (hall : sp.inv.conjs.all (·.holds st.flops outs) = true) (hact : ActiveAt sp outs) :
    ∀ cs, commonLits sp cas R ti = some cs → (conjE cs).eval σ = true := by
  intro cs h
  unfold ActiveAt at hact
  unfold commonLits at h
  cases hp : sp.active with
  | none =>
    simp only [hp, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨hs, hhs, a, rfl, rfl⟩ := h
    simp only [conjE, eval_and, hA, hB, hyps_eval hS ho sp.inv.conjs hall _ _ hhs]
    rfl
  | some p =>
    simp only [hp] at hact
    simp only [hp, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨hs, hhs, act, hact', rfl⟩ := h
    simp only [conjE, eval_and, hA, hB, hyps_eval hS ho sp.inv.conjs hall _ _ hhs,
      propOf_sound ho hact', hact]
    rfl

/-! ## Soundness: the base -/

theorem initLits_sound {σ : Asg} {σR o0 : SVal} {st : StateM} {outs0 : Array BV}
    (hR : SRel σ σR (StateM.encStateM st)) (h0 : SRel σ o0 (encBVs outs0)) :
    ∀ (es : List InitE) (k : Nat) (ls : List BVLogicalExpr), initLits σR o0 es k = some ls →
      (conjE ls).eval σ = true → ∀ (i : Nat) (e : InitE), es[i]? = some e →
        match e with
        | InitE.cnst v => ∃ b, st.flops[k + i]? = some b ∧ bv_uint b = v
        | InitE.out0 o => ∃ b, st.flops[k + i]? = some b ∧ bv_uint b = bv_uint (outs0.getD o default)
        | InitE.free => True
  | [], _, _, _, _, i, e, hi => by simp at hi
  | e :: es, k, ls, h, hev, i, e', hi => by
      simp only [initLits, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨x, hx, r, hr, rfl⟩ := h
      simp only [conjE, eval_and, Bool.and_eq_true] at hev
      cases i with
      | succ i =>
        have := initLits_sound hR h0 es (k + 1) r hr hev.2 i e' (by simpa using hi)
        rwa [show k + 1 + i = k + (i + 1) by omega] at this
      | zero =>
        simp only [List.getElem?_cons_zero, Option.some.injEq] at hi
        subst hi
        simp only [Nat.add_zero]
        cases e with
        | cnst v =>
          simp only [initLit] at hx
          have := valLit_sound hR hx
          rw [this] at hev
          simp only [flopVal, beq_iff_eq, Option.map_eq_some_iff] at hev
          exact hev.1
        | out0 o =>
          simp only [initLit, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
            Option.some.injEq] at hx
          obtain ⟨fx, hfx, y, hy, rfl⟩ := hx
          obtain ⟨b, hb, _, hbu⟩ := flopE_sound hR hfx
          obtain ⟨b0, hb0, hlb0⟩ := outLeaf_sound h0 hy
          rw [eval_eqBvE hbu hlb0.2] at hev
          refine ⟨b, hb, ?_⟩
          simp only [Array.getD_eq_getD_getElem?, hb0, Option.getD_some]
          simpa using hev.1
        | free => trivial

section
variable {DI DS : DesignCert} {RI RS : Program} {sp : RefMapSpec} {s0 : StateM}
  {pfIB : ByteArray} {pfIS : List (List ByteArray)} {ins : Nat → RuntimeInput}

theorem rm_base (hrI : MemResidual DI RI) (hAI : admitsWB DI = true)
    (hconj : runConjTOf DI RI sp.inv s0 pfIB pfIS = true)
    (hins : ∀ n, (ins n).toList.map BV.width = sp.inv.inW)
    {bsteps : List StepO} {bst : SymSt} (hbrun : baseRunRM DI RI sp s0 = .ok (bsteps, bst))
    (hbsc : scopedB bst = true) {φB : BVLogicalExpr} (hφB : baseOblRM sp bst bsteps = some φB)
    (hUB : φB.Unsat) (hgood : Good DI sp s0 ins sp.R0) :
    InitOK sp (outM DI s0 ins 0) (alphaSt sp (outM DI s0 ins sp.R0))
    ∧ ActiveAt sp (outM DI s0 ins sp.R0) ∧ SpecNorm sp (alphaSt sp (outM DI s0 ins sp.R0)) := by
  have hside := (conjT_parts hconj).1
  have hinv := invT_all hrI hAI hconj hins
  have rt : ∀ n, runtimeFullOKB DI (ins n) (traceM DI s0 ins n) (edgesAll DI) = true :=
    fun n => runtime_of_invC hside (hinv n).1 (hinv n).2 (hins n)
  have hrange := (sideOKC_parts hside).2.2.2.2.2
  obtain ⟨hx, hlen⟩ := unrollO_extends (sp.R0 + 1) _ _ _ _ _ hbrun
  let xs := insVals ins 0 (sp.R0 + 1)
  have hxl : xs.length = bst.base := by
    rw [← hx.1, insVals_length hins]
  obtain ⟨hc, hag⟩ := extendSt_agree hxl hbsc
  have facts := unrollO_sound hrange hrI hAI hins hc (sp.R0 + 1) 0 0 _ _ _ _ hbrun (.refl _)
    (fun j hj => by simpa using insVals_agree hins (sp.R0 + 1) hag j hj) .conc
  have fj : ∀ j (hj : j < bsteps.length), StepFactsO (extendSt xs.toArray bst) sp.inv.asmOut
      sp.inv.asmIn (outM DI s0 ins j) (ins j) (traceM DI s0 ins (j + 1)) (bsteps[j]'hj) := by
    intro j hj
    have := facts j hj (fun i _ => by simpa using rt i)
    simpa using this
  -- the base obligation's pieces
  simp only [baseOblRM, Option.bind_eq_bind, Option.bind_eq_some_iff] at hφB
  obtain ⟨st0, hs0, sR, hsR, ls, hls, inits, hinits, hrest⟩ := hφB
  have h0lt : 0 < bsteps.length := (List.getElem?_eq_some_iff.mp hs0).1
  have hRlt : sp.R0 < bsteps.length := (List.getElem?_eq_some_iff.mp hsR).1
  have e0 : bsteps[0]'h0lt = st0 := (List.getElem?_eq_some_iff.mp hs0).2
  have eR : bsteps[sp.R0]'hRlt = sR := (List.getElem?_eq_some_iff.mp hsR).2
  have f0 := fj 0 h0lt
  have fR := fj sp.R0 hRlt
  rw [e0] at f0
  rw [eR] at fR
  have hα := alphaSt_norm fR.2.1 hls
  -- every step's assumption holds
  have hasm : (conjE (bsteps.map (·.1))).eval (extendSt xs.toArray bst) = true := by
    rw [eval_conjE, List.all_eq_true]
    intro x hx'
    obtain ⟨y, hy, rfl⟩ := List.mem_map.mp hx'
    obtain ⟨j, hj, rfl⟩ := List.getElem_of_mem hy
    rw [(fj j hj).1]
    exact hgood j (by omega)
  -- so the active output and the initial map hold
  have key : ∀ act, φB = coi bst (conjE [conjE (bsteps.map (·.1)), .not (conjE (act :: inits))]) →
      (conjE (act :: inits)).eval (extendSt xs.toArray bst) = true := by
    intro act hφ
    cases hb : (conjE (act :: inits)).eval (extendSt xs.toArray bst)
    · exfalso
      have hev := eval_coi hc (body := conjE [conjE (bsteps.map (·.1)), .not (conjE (act :: inits))])
        (by rw [eval_conjE]; simp only [List.all_cons, List.all_nil, eval_not, hasm, hb]; rfl)
      rw [← hφ, hUB] at hev; cases hev
    · rfl
  have hinit : ∀ (inits' : List BVLogicalExpr), (conjE inits').eval (extendSt xs.toArray bst) = true →
      initLits (leafState ls) st0.2.1 sp.init 0 = some inits' →
      InitOK sp (outM DI s0 ins 0) (alphaSt sp (outM DI s0 ins sp.R0)) := by
    intro inits' hev hi k e hk
    have := initLits_sound hα.2 f0.2.1 sp.init 0 inits' hi hev k e hk
    simpa using this
  cases hp : sp.active with
  | none =>
    simp only [hp, Option.bind_eq_some_iff, Option.pure_def, Option.some.injEq] at hrest
    obtain ⟨_, rfl, hφ⟩ := hrest
    have hk := key (.const true) hφ.symm
    simp only [conjE, eval_and, Bool.and_eq_true] at hk
    exact ⟨hinit inits hk.2 hinits, by simp [ActiveAt, hp], hα.1⟩
  | some p =>
    simp only [hp, Option.bind_eq_some_iff, Option.pure_def, Option.some.injEq] at hrest
    obtain ⟨a, ha, hφ⟩ := hrest
    have hk := key a hφ.symm
    simp only [conjE, eval_and, Bool.and_eq_true] at hk
    refine ⟨hinit inits hk.2 hinits, ?_, hα.1⟩
    simp only [ActiveAt, hp]
    rw [← propOf_sound fR.2.1 ha]; exact hk.1

end

/-! ## Soundness: a step -/

theorem mem_comm {sp : RefMapSpec} {g : List Nat} (h : g ∈ sp.groups) : RTarget.comm g ∈ targetsRM sp := by
  simp only [targetsRM, List.mem_append, List.mem_map]
  exact Or.inl (Or.inl (Or.inl (Or.inr ⟨g, h, rfl⟩)))

theorem mem_allow {sp : RefMapSpec} {j : Nat} (h : j < sp.slots.length) :
    RTarget.allow j ∈ targetsRM sp := by
  simp only [targetsRM, List.mem_append, List.mem_map, List.mem_range]
  exact Or.inl (Or.inl (Or.inr ⟨j, h, rfl⟩))

theorem mem_obs {sp : RefMapSpec} {j : Nat} (h : j < sp.slots.length) : RTarget.obs j ∈ targetsRM sp := by
  simp only [targetsRM, List.mem_append, List.mem_map, List.mem_range]
  exact Or.inl (Or.inr ⟨j, h, rfl⟩)

theorem mem_active {sp : RefMapSpec} {p : PropSpec} (h : sp.active = some p) :
    RTarget.active ∈ targetsRM sp := by
  simp [targetsRM, h]

theorem mem_ranked {sp : RefMapSpec} {c : Nat} {ch : ChanS} {r : Nat × Nat} (hc : sp.chans[c]? = some ch)
    (hr : ch.rank = some r) :
    RTarget.rankLe c ∈ targetsRM sp ∧ RTarget.rankLt c ∈ targetsRM sp ∧ RTarget.rankSel c ∈ targetsRM sp := by
  have hmem : c ∈ rankedChans sp := by
    simp only [rankedChans, List.mem_filter, List.mem_range, hc, hr, Option.isSome_some, and_true]
    exact (List.getElem?_eq_some_iff.mp hc).1
  refine ⟨?_, ?_, ?_⟩ <;>
  · simp only [targetsRM, List.mem_append, List.mem_flatMap]
    exact Or.inr ⟨c, hmem, by simp⟩

theorem bit_of_advLit_false {σ : Asg} {k : BVExpr 1} {rest : List BVLogicalExpr}
    (hk : bitOf (k.eval σ) = true) (h : (conjE (advLit k :: rest)).eval σ = false) :
    (conjE rest).eval σ = false := by
  simp only [conjE, eval_and, eval_advLit, hk, Bool.true_and] at h
  exact h

section
variable {DI DS : DesignCert} {RI RS : Program} {sp : RefMapSpec} {s0 : StateM}
  {pfIB pfB : ByteArray} {pfIS pfS : List (List ByteArray)} {ins : Nat → RuntimeInput}

theorem rm_step (hrI : MemResidual DI RI) (hAI : admitsWB DI = true)
    (hrS : MemResidual DS RS) (hAS : admitsWB DS = true)
    (hchk : runRefMapOf DI DS RI RS sp s0 pfIB pfIS pfB pfS = true)
    (hins : ∀ n, (ins n).toList.map BV.width = sp.inv.inW) :
    ∀ n, sp.R0 ≤ n → Good DI sp s0 ins (n + 1) → ActiveAt sp (outM DI s0 ins n) →
      CycleOK DS sp (portsAt DI s0 ins n) (alphaSt sp (outM DI s0 ins n)) sp.slots
        (alphaSt sp (outM DI s0 ins (n + 1)))
      ∧ ActiveAt sp (outM DI s0 ins (n + 1))
      ∧ RankOK DS sp (alphaSt sp (outM DI s0 ins n)) (portsAt DI s0 ins n)
      ∧ SpecNorm sp (alphaSt sp (outM DI s0 ins n))
      ∧ SpecNorm sp (alphaSt sp (outM DI s0 ins (n + 1))) := by
  obtain ⟨hconj, hsideRM, -, hcases⟩ := rm_parts hchk
  unfold sideRM at hsideRM
  simp only [Bool.and_eq_true, beq_iff_eq, List.all_eq_true, List.mem_range, List.any_eq_true,
    List.contains_iff_mem] at hsideRM
  obtain ⟨⟨⟨⟨hside, -⟩, -⟩, hcover⟩, -⟩ := hsideRM
  have hsideC := (conjT_parts hconj).1
  have hinv := invT_all hrI hAI hconj hins
  have rt : ∀ n, runtimeFullOKB DI (ins n) (traceM DI s0 ins n) (edgesAll DI) = true :=
    fun n => runtime_of_invC hsideC (hinv n).1 (hinv n).2 (hins n)
  have hrange := (sideOKC_parts hsideC).2.2.2.2.2
  intro n hn hgood hact
  have hprev := runConjTOf_sound hrI hAI hconj hins n (fun j hj => hgood j (by omega))
  -- the case the state at cycle n is in
  obtain ⟨cas, hcasIn, hcase⟩ :
      ∃ cas ∈ casesRM sp, caseHolds (traceM DI s0 ins n).flops cas = true := by
    unfold casesRM
    split
    · rename_i s hs
      split
      · rename_i cs hcs
        have hm := (List.all_eq_true.mp hprev) _ (List.mem_of_getElem? hcs)
        simp only [ConjK.holds, List.any_eq_true] at hm
        exact hm
      · exact ⟨[], List.mem_singleton_self _, rfl⟩
    · exact ⟨[], List.mem_singleton_self _, rfl⟩
  obtain ⟨R, hR, hsc, hobl⟩ := hcases cas hcasIn
  obtain ⟨st, l0, l1, st1, hstep, hl0, hσ0, hl1, hσ1, hch, hen⟩ := rmRun_parts hR
  have x1 : st1.Extends R.st := en0S_extends _ _ _ _ hen
  have x2 : st.Extends R.st := (chainS_extends _ _ _ _ _ _ hch).trans x1
  have hbS := (unrollO_extends 2 _ _ _ _ _ hstep).1.1
  let xs := stateVals (traceM DI s0 ins n) ++ insVals ins n 2
  have hxl : xs.length = R.st.base := by
    rw [← x2.1, ← hbS, List.length_append, stateVals_length (hinv n).1, insVals_length hins]
  obtain ⟨hc, hag⟩ := extendSt_agree hxl hsc
  generalize extendSt xs.toArray R.st = σ at hc hag
  rw [AgreeAt_append, stateVals_length (hinv n).1, Nat.zero_add] at hag
  have hT := tmplOf_holds (hinv n).1 hprev hcase
  have hS := stateVarT_rel (hinv n).1 hag.1 hT
  have facts := unrollO_sound hrange hrI hAI hins hc 2 n _ _ _ _ _ hstep x2
    (insVals_agree hins 2 hag.2) hS
  have f0 := facts 0 (by simp) (fun i hi => by
    have : i = 0 := by omega
    subst this; simpa using rt n)
  have f1 := facts 1 (by simp) (fun i hi => by
    rcases Nat.lt_or_ge i 1 with h | h
    · have : i = 0 := by omega
      subst this; simpa using rt n
    · have : i = 1 := by omega
      subst this; simpa using rt (n + 1))
  simp only [List.getElem_cons_zero, List.getElem_cons_succ, Nat.add_zero] at f0 f1
  -- the ports of cycle n
  have hIn0 : SRel σ (inputVar sp.inv.inW (layoutSize sp.inv.L))
      (encBVs (ins n)) := by
    have := insVals_agree hins 2 hag.2 0 (by decide)
    simp only [Nat.zero_mul, Nat.add_zero] at this
    exact inputVar_rel (hins n) this
  have hP : PortsRel σ (portsS sp R.a R.b) (portsAt DI s0 ins n) :=
    ⟨f0.2.1, f1.2.1, hIn0⟩
  -- the map's states at n and n+1, the chain, the zero-response outputs
  have hα0 := alphaSt_norm f0.2.1 hl0
  have hα1 := alphaSt_norm f1.2.1 hl1
  have rσ0 : SRel σ R.σ0
      (StateM.encStateM (alphaSt sp (outM DI s0 ins n))) := by rw [hσ0]; exact hα0.2
  have rσ1 : SRel σ R.σ1
      (StateM.encStateM (alphaSt sp (outM DI s0 ins (n + 1)))) := by rw [hσ1]; exact hα1.2
  obtain ⟨hσe, hrecs, -⟩ := chainS_sound hrS hAS hside hc hP sp.slots st R.σ0 _ R.recs R.σe st1
    hch x1 hα0.1.1 rσ0
  obtain ⟨hlenE, hens⟩ := en0S_sound hrS hAS hside hc hα0.1.1 rσ0 sp.chans st1 R.ens R.st hen (.refl _)
  obtain ⟨hlenR, hbits⟩ := recsOK_at R.recs _ sp.slots hrecs
  -- every target's bad case is false
  have hA : R.a.1.eval σ = true := by rw [f0.1]; exact hgood n (by omega)
  have hB : R.b.1.eval σ = true := by rw [f1.1]; exact hgood (n + 1) (Nat.le_refl _)
  have tfalse : ∀ t ∈ targetsRM sp, ∃ bad, badLit sp R t = some bad
      ∧ BVLogicalExpr.eval σ bad = false := by
    intro t ht
    obtain ⟨ti, hti, rfl⟩ := List.getElem_of_mem ht
    obtain ⟨φ, hφ, hU⟩ := hobl ti hti
    exact obl_bad_false hc hφ hU (commonLits_true hS f0.2.1 hA hB hprev hact)
  -- (1) the chain ends in the next cycle's map state
  have hend : chainC DS sp (portsAt DI s0 ins n) (alphaSt sp (outM DI s0 ins n)) sp.slots
      = alphaSt sp (outM DI s0 ins (n + 1)) := by
    apply specNorm_ext (chainC_norm hAS hside sp.slots _ hα0.1) hα1.1
    intro k hk
    obtain ⟨g, hg, hkg⟩ := hcover k hk
    obtain ⟨bad, hbad, hbf⟩ := tfalse (.comm g) (mem_comm hg)
    simp only [badLit, Option.map_eq_some_iff] at hbad
    obtain ⟨gl, hgl, rfl⟩ := hbad
    have hglt : BVLogicalExpr.eval σ gl = true := by simpa using hbf
    obtain ⟨b1, b2, hb1, hb2, hu⟩ := groupLit_sound rσ1 hσe g gl hgl hglt k hkg
    exact ⟨b2, b1, hb2, hb1, hu.symm⟩
  -- (2) each fired slot is allowed and observed as the spec says
  have hcyc := cycleOK_of hP sp.slots R.recs (alphaSt sp (outM DI s0 ins n)) hrecs (by
    intro j r sl hr hs hk
    have hj : j < sp.slots.length := (List.getElem?_eq_some_iff.mp hs).1
    refine ⟨?_, ?_⟩
    · obtain ⟨bad, hbad, hbf⟩ := tfalse (.allow j) (mem_allow hj)
      simp only [badLit, hr, Option.bind_eq_bind, Option.bind_some, Option.bind_eq_some_iff,
        Option.pure_def, Option.some.injEq] at hbad
      obtain ⟨al, hal, rfl⟩ := hbad
      have := bit_of_advLit_false hk hbf
      simp only [conjE, eval_and, eval_not] at this
      exact ⟨al, hal, by simpa using this⟩
    · obtain ⟨bad, hbad, hbf⟩ := tfalse (.obs j) (mem_obs hj)
      simp only [badLit, hr, hs, Option.bind_eq_bind, Option.bind_some, Option.bind_eq_some_iff,
        Option.pure_def, Option.some.injEq] at hbad
      obtain ⟨ls, hls, rfl⟩ := hbad
      have := bit_of_advLit_false hk hbf
      simp only [conjE, eval_and, eval_not] at this
      exact ⟨ls, hls, by simpa using this⟩)
  rw [hend] at hcyc
  refine ⟨hcyc, ?_, ?_, hα0.1, hα1.1⟩
  -- (3) the active output holds at n+1
  · unfold ActiveAt
    cases hp : sp.active with
    | none => trivial
    | some p =>
      show propBit (outM DI s0 ins (n + 1)) p = true
      obtain ⟨bad, hbad, hbf⟩ := tfalse .active (mem_active hp)
      simp only [badLit, hp, Option.map_eq_some_iff] at hbad
      obtain ⟨pl, hpl, rfl⟩ := hbad
      rw [← propOf_sound f1.2.1 hpl]
      simpa using hbf
  -- (4) the ranks
  · intro c ch ro so hc' hrank hen0 hnofire
    obtain ⟨tLe, tLt, tSel⟩ := mem_ranked hc' hrank
    have hclt : c < sp.chans.length := (List.getElem?_eq_some_iff.mp hc').1
    have hchc : sp.chans[c]'hclt = ch := (List.getElem?_eq_some_iff.mp hc').2
    -- the rank context: allowed at α(n), no slot of c fires
    have hctx : ∀ ro' so' en nf, rankCtx sp R c = some (ro', so', en, nf) →
        ro' = ro ∧ so' = so ∧ en.eval σ = true
        ∧ nf.eval σ = true := by
      intro ro' so' en nf h
      simp only [rankCtx, hc', hrank, Option.bind_eq_bind, Option.bind_some, Option.bind_eq_some_iff,
        Option.pure_def, Option.some.injEq, Prod.mk.injEq] at h
      obtain ⟨so0, hso0, en', hen', rfl, rfl, rfl, rfl⟩ := h
      have hso0' : R.ens[c]? = some (some so0) := by
        cases he : R.ens[c]? with
        | none => rw [he] at hso0; cases hso0
        | some x => rw [he] at hso0; simp at hso0; rw [hso0]
      have rso0 := hens c hclt so0 hso0'
      rw [hchc] at rso0
      refine ⟨rfl, rfl, by rw [propOf_sound rso0 hen']; exact hen0, ?_⟩
      have := nofire_eval (C := portsAt DI s0 ins n) c R.recs sp.slots hlenR
        (fun j r sl h1 h2 => (hbits j r sl h1 h2).1)
      unfold nofireLits at this
      rw [this]
      simpa [firesChan] using hnofire
    have hra := fun (l : Leaf) (h : outLeaf R.a.2.1 ro = some l) => outLeaf_sound f0.2.1 h
    have hrb := fun (l : Leaf) (h : outLeaf R.b.2.1 ro = some l) => outLeaf_sound f1.2.1 h
    refine ⟨?_, ?_, ?_⟩
    · obtain ⟨bad, hbad, hbf⟩ := tfalse (.rankLe c) tLe
      simp only [badLit, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at hbad
      obtain ⟨⟨ro', so', en, nf⟩, hctx', ra, hra', rb, hrb', rfl⟩ := hbad
      dsimp only at hra' hrb'
      obtain ⟨rfl, -, hen1, hnf1⟩ := hctx ro' so' en nf hctx'
      obtain ⟨ba, hba, hla⟩ := hra ra hra'
      obtain ⟨bb, hbb, hlb⟩ := hrb rb hrb'
      simp only [conjE, eval_and, eval_not, hen1, hnf1, eval_leLit hlb hla, Bool.true_and,
        Bool.and_true] at hbf
      have := wordAt_of hba
      have := wordAt_of hbb
      simp only [portsAt]
      simp at hbf; omega
    · intro hhelp
      obtain ⟨bad, hbad, hbf⟩ := tfalse (.rankLt c) tLt
      simp only [badLit, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at hbad
      obtain ⟨⟨ro', so', en, nf⟩, hctx', ra, hra', rb, hrb', sa, hsa, hl, hhl, rfl⟩ := hbad
      dsimp only at hra' hrb' hsa hhl
      obtain ⟨rfl, rfl, hen1, hnf1⟩ := hctx ro' so' en nf hctx'
      obtain ⟨ba, hba, hla⟩ := hra ra hra'
      obtain ⟨bb, hbb, hlb⟩ := hrb rb hrb'
      obtain ⟨bs, hbs, hls⟩ := outLeaf_sound f0.2.1 hsa
      have hhl' := helpfulLit_sound hP hbs hls hhl
      simp only [conjE, eval_and, eval_not, hen1, hnf1, hhl', hhelp, eval_ltLit hlb hla,
        Bool.true_and] at hbf
      have := wordAt_of hba
      have := wordAt_of hbb
      simp only [portsAt]
      simp at hbf; omega
    · intro heqr
      obtain ⟨bad, hbad, hbf⟩ := tfalse (.rankSel c) tSel
      simp only [badLit, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at hbad
      obtain ⟨⟨ro', so', en, nf⟩, hctx', ra, hra', rb, hrb', sa, hsa, sb, hsb, re, hre, rfl⟩ := hbad
      dsimp only at hra' hrb' hsa hsb hre
      obtain ⟨rfl, rfl, hen1, hnf1⟩ := hctx ro' so' en nf hctx'
      obtain ⟨ba, hba, hla⟩ := hra ra hra'
      obtain ⟨bb, hbb, hlb⟩ := hrb rb hrb'
      obtain ⟨bsa, hbsa, hlsa⟩ := outLeaf_sound f0.2.1 hsa
      obtain ⟨bsb, hbsb, hlsb⟩ := outLeaf_sound f1.2.1 hsb
      have wa := wordAt_of hba
      have wb := wordAt_of hbb
      simp only [portsAt] at heqr ⊢
      have hreq : bv_uint bb = bv_uint ba := by omega
      rw [eval_conjE] at hbf
      simp only [List.all_cons, List.all_nil, Bool.and_true, hen1, hnf1, eval_eqBvE hlb.2 hla.2,
        hreq, decide_true, Bool.true_and, eval_not] at hbf
      have h2 : BVLogicalExpr.eval σ (conjE [eqBvE sb sa, re]) = true := by simpa using hbf
      rw [eval_conjE] at h2
      simp only [List.all_cons, List.all_nil, Bool.and_true, Bool.and_eq_true,
        eval_eqBvE hlsb.2 hlsa.2, decide_eq_true_eq] at h2
      obtain ⟨hsel, hreen⟩ := h2
      refine ⟨?_, reqEnLit_sound hP hbsa hlsa hre hreen⟩
      have w1 := wordAt_of hbsa
      have w2 := wordAt_of hbsb
      have : wordAt (outM DI s0 ins (n + 1)) so' = wordAt (outM DI s0 ins n) so' := by omega
      simp only [selOf, this]

end

/-- **The refinement-map check is sound.** On every run of the implementation from `s0`, the
base holds at `R0` and every cycle from `R0` on satisfies `CycleOK`, the active output and the
rank obligations. -/
theorem runRefMapOf_sound {DI DS : DesignCert} {RI RS : Program} {sp : RefMapSpec} {s0 : StateM}
    {pfIB pfB : ByteArray} {pfIS pfS : List (List ByteArray)} (hrI : MemResidual DI RI)
    (hAI : admitsWB DI = true) (hrS : MemResidual DS RS) (hAS : admitsWB DS = true)
    (hchk : runRefMapOf DI DS RI RS sp s0 pfIB pfIS pfB pfS = true)
    {ins : Nat → RuntimeInput} (hins : ∀ n, (ins n).toList.map BV.width = sp.inv.inW) :
    RefMapFacts DI DS sp s0 ins := by
  obtain ⟨hconj, -, ⟨bsteps, bst, hbrun, hbsc, φB, hφB, hUB⟩, -⟩ := rm_parts hchk
  exact ⟨rm_base hrI hAI hconj hins hbrun hbsc hφB hUB, rm_step hrI hAI hrS hAS hchk hins⟩

end HDLProof.Check

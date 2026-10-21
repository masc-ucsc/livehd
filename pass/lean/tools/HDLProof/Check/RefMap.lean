/-
`Check.RefMap` -- refinement maps: the pieces of the checker.

An implementation certificate `DI` refines a spec certificate `DS` with stuttering. The spec
steps once per implementation EVENT. Its state at every implementation cycle IS the refinement
map's value: spec flop `k` is the implementation's output word `alpha[k]` at that cycle,
normalized.

The spec's events are CHANNEL events (`ChanS`): constant inputs select the channel, and
response inputs take the response words. The spec's outputs say whether the event is allowed
and what it presents. The implementation's events are SLOTS (`SlotS`), fired in order within a
cycle. A slot names its channel, the formula over real ports that fires it, the ports its
response words come from, and the observed words it must present. Several slots may fire in
one cycle: several commits, or a bus handshake and a commit report together.

This file holds:
- the concrete spec as a `Theory.Spec` (`specOf`): its events are evaluated with the response
  inputs at 0, and SpecFacts' Moore check relates that to the actual responses;
- the implementation's per-cycle events (`evsOf`) and observations (`obsOf`);
- `CycleOK`: what one cycle must satisfy;
- the symbolic counterparts: `specInS`, and `chainS`, a cycle's spec steps from the map's
  state, a mux per slot. Soundness is `chainS_sound`.
The obligations, the check and its soundness are in `Check.RefMapSound`. Mathlib-free.
-/
import HDLProof.Kit.BitE
import HDLProof.Theory.Progress

open Projection
namespace HDLProof.Check
open HDLProof.Sym HDLProof.Kit Std.Tactic.BVDecide Projection.MemProjection Projection.Hw
  Projection.HwMem Compiler

/-! ## The specification -/

/-- A spec channel: how the spec certificate takes an event on it. -/
structure ChanS where
  /-- spec inputs set to constants for an event on this channel (the channel select) -/
  sel    : List (Nat × Int) := []
  /-- spec inputs that receive the response words, in order -/
  respIn : List Nat := []
  /-- spec output: the event is allowed -/
  allow  : PropSpec
  /-- spec outputs: the words the event presents -/
  obsOut : List Nat := []
  /-- progress: implementation outputs (rank, selector: 0 internal, j+1 waits on requirement j) -/
  rank   : Option (Nat × Nat) := none
  deriving Repr, Inhabited

/-- An implementation event slot. -/
structure SlotS where
  chan : Nat
  kind : BitE
  resp : List PortW := []
  obs  : List ObsW := []
  deriving Repr, Inhabited

/-- An environment requirement: enabled, and its helpful action. -/
structure ReqS where
  en    : BitE
  fires : BitE
  deriving Repr, Inhabited

/-- The spec flop's value at the first refined cycle. -/
inductive InitE where
  | cnst (v : Int)
  /-- implementation output `o` at cycle 0, normalized -/
  | out0 (o : Nat)
  | free
  deriving Repr, Inhabited

structure RefMapSpec where
  /-- the implementation's invariant (proved by the conjunctive checker) and its layout -/
  inv     : ConjTSpec
  /-- the first refined cycle, after reset -/
  R0      : Nat := 1
  /-- implementation output that is nonzero from `R0` on (checked) -/
  active  : Option PropSpec := none
  alpha   : List Nat
  init    : List InitE
  chans   : List ChanS
  slots   : List SlotS
  reqs    : List ReqS := []
  /-- the spec flops checked together in one commutation obligation (each flop exactly once) -/
  groups  : List (List Nat)
  /-- per refinement-map target: the invariant conjuncts it assumes (default: all) -/
  hyps    : List (Option (List Nat)) := []
  /-- a `oneOf` conjunct of `inv` every step obligation is split on -/
  split   : Option Nat := none
  fuelS   : Nat
  specInW : List Nat
  LS      : Shape

/-! ## The concrete spec -/

def posOf : List Nat → Nat → Option Nat
  | [], _ => none
  | x :: xs, k => if x = k then some 0 else (posOf xs k).map (· + 1)

/-- Spec input `k`'s value for an event on `ch` with response words `rs`. -/
def inVal (ch : ChanS) (rs : List Int) (k : Nat) : Int :=
  match posOf ch.respIn k with
  | some i => rs.getD i 0
  | none => match ch.sel.find? (fun q => q.1 == k) with
    | some q => q.2
    | none => 0

theorem inVal_none {ch : ChanS} {k : Nat} (h : posOf ch.respIn k = none) (rs rs' : List Int) :
    inVal ch rs k = inVal ch rs' k := by
  simp only [inVal, h]

/-- The spec's input vector for an event on `ch` with response words `rs`. -/
def specIn (inW : List Nat) (ch : ChanS) (rs : List Int) : RuntimeInput :=
  ((List.range inW.length).map fun k => mk_bv (inW.getD k 0) (inVal ch rs k)).toArray

theorem specIn_size (inW : List Nat) (ch : ChanS) (rs : List Int) :
    (specIn inW ch rs).size = inW.length := by
  simp [specIn]

noncomputable def specRes (DS : DesignCert) (sp : RefMapSpec) (σ : StateM) (ch : ChanS)
    (rs : List Int) : StateM.ResultM :=
  resM DS (edgesAll DS) (specIn sp.specInW ch rs) σ

/-- One spec step: the next state, normalized and memory-free. -/
noncomputable def specStep (DS : DesignCert) (sp : RefMapSpec) (σ : StateM) (ch : ChanS)
    (rs : List Int) : StateM :=
  normSt (specRes DS sp σ ch rs).nextState sp.LS.fws.length

noncomputable def specOuts (DS : DesignCert) (sp : RefMapSpec) (σ : StateM) (ch : ChanS)
    (rs : List Int) : Array BV :=
  (specRes DS sp σ ch rs).outputs

/-- The normalized values of some outputs. -/
def outVals (outs : Array BV) (os : List Nat) : List Int :=
  os.map fun o => bv_uint (outs.getD o default)

/-- The spec as a channel spec: events evaluated with the response inputs at 0. -/
noncomputable def specOf (DS : DesignCert) (sp : RefMapSpec) :
    Theory.Spec StateM Nat (List Int) (List Int) where
  en σ c := match sp.chans[c]? with
    | some ch => propBit (specOuts DS sp σ ch []) ch.allow
    | none => false
  out σ c := match sp.chans[c]? with
    | some ch => outVals (specOuts DS sp σ ch []) ch.obsOut
    | none => []
  δ σ c rs := match sp.chans[c]? with
    | some ch => specStep DS sp σ ch rs
    | none => σ

/-! ## Spec states and the spec's side conditions -/

/-- A spec state of the declared shape: memory-free, flops of the declared widths. -/
def SpecSt (sp : RefMapSpec) (σ : StateM) : Prop :=
  σ.mems = #[] ∧ σ.flops.toList.map BV.width = sp.LS.fws

def specSideOK (DS : DesignCert) (sp : RefMapSpec) : Bool :=
  (DS.flops.toList.map (·.width) == sp.LS.fws) && (DS.memories.size == 0)
    && inputOKB DS sp.specInW.length && flopBoundOKB DS sp.LS.fws.length
    && edgesOKB DS (edgesAll DS)

theorem specSide_parts {DS : DesignCert} {sp : RefMapSpec} (h : specSideOK DS sp = true) :
    DS.flops.toList.map (·.width) = sp.LS.fws ∧ DS.memories.size = 0
    ∧ inputOKB DS sp.specInW.length = true ∧ flopBoundOKB DS sp.LS.fws.length = true
    ∧ edgesOKB DS (edgesAll DS) = true := by
  unfold specSideOK at h
  simp only [Bool.and_eq_true, beq_iff_eq] at h
  exact ⟨h.1.1.1.1, h.1.1.1.2, h.1.1.2, h.1.2, h.2⟩

theorem getElem?_of_map_eq {α β : Type} {f : α → Nat} {g : β → Nat} {xs : List α} {ys : List β}
    (h : xs.map f = ys.map g) {i : Nat} {x : α} (hx : xs[i]? = some x) :
    ∃ y, ys[i]? = some y ∧ g y = f x := by
  have := congrArg (·[i]?) h
  simp only [List.getElem?_map, hx, Option.map_some] at this
  cases hy : ys[i]? with
  | none => rw [hy] at this; cases this
  | some y => rw [hy] at this; exact ⟨y, rfl, (Option.some.inj this).symm⟩

theorem spec_runtime {DS : DesignCert} {sp : RefMapSpec} (hside : specSideOK DS sp = true)
    {σ : StateM} (hσ : SpecSt sp σ) (ch : ChanS) (rs : List Int) :
    runtimeFullOKB DS (specIn sp.specInW ch rs) σ (edgesAll DS) = true := by
  obtain ⟨hw, hm, hin, hfb, hed⟩ := specSide_parts hside
  obtain ⟨hmem, hfw⟩ := hσ
  have hsz : σ.flops.size = DS.flops.size := by
    have h1 := congrArg List.length hfw
    have h2 := congrArg List.length hw
    simp at h1 h2; omega
  unfold runtimeFullOKB runtimeOKB stateOKB
  simp only [Bool.and_eq_true]
  refine ⟨⟨⟨⟨⟨by simp [hsz], by simp [hm]⟩, by rw [specIn_size]; exact hin⟩, ?_⟩, hed⟩, ?_⟩
  · rw [show σ.flops.size = sp.LS.fws.length by rw [hsz, ← hw]; simp]; exact hfb
  · apply flopShapeOKB_complete
    intro idx f hf
    have hf' : DS.flops.toList[idx]? = some f := by simpa using hf
    have hmap : DS.flops.toList.map (·.width) = σ.flops.toList.map BV.width := by rw [hw, hfw]
    obtain ⟨b, hb, hbw⟩ := getElem?_of_map_eq hmap hf'
    exact ⟨b, by simpa using hb, hbw⟩

theorem specStep_specSt {DS : DesignCert} {sp : RefMapSpec} (hA : admitsWB DS = true)
    (hside : specSideOK DS sp = true) {σ : StateM} (hσ : SpecSt sp σ) (ch : ChanS) (rs : List Int) :
    SpecSt sp (specStep DS sp σ ch rs) := by
  obtain ⟨hw, _, _, _, _⟩ := specSide_parts hside
  have hfull := (resM_spec hA (spec_runtime hside hσ ch rs)).2.2
  unfold stateFullOKB stateOKB at hfull
  simp only [Bool.and_eq_true, decide_eq_true_eq] at hfull
  obtain ⟨⟨hle, _⟩, hshape⟩ := hfull
  have hsh := flopShapeOKB_sound hshape
  refine ⟨rfl, ?_⟩
  simp only [specStep, normSt, flopSt, List.map_map]
  generalize hnx : (specRes DS sp σ ch rs).nextState = nx at *
  have hnx' : resM DS (edgesAll DS) (specIn sp.specInW ch rs) σ = specRes DS sp σ ch rs := rfl
  rw [hnx', hnx] at hle hsh
  rw [← hw]
  apply List.ext_getElem
  · simp only [List.length_map, List.length_take]
    have : DS.flops.toList.length ≤ nx.flops.toList.length := by simpa using hle
    omega
  · intro i h1 h2
    simp only [List.getElem_map, List.getElem_take, Function.comp, normBV_width]
    have hi : i < DS.flops.size := by simpa using h2
    obtain ⟨b, hb, hbw⟩ := hsh i (DS.flops[i]'hi) (Array.getElem?_eq_getElem hi)
    have hb' : nx.flops.toList[i]? = some b := by simpa using hb
    obtain ⟨_, hval⟩ := List.getElem?_eq_some_iff.mp hb'
    rw [hval, hbw]; simp

/-! ## The implementation's side of a cycle -/

/-- Port words' normalized values (a missing word reads 0). -/
def portVals (C : Ports) (ps : List PortW) : List Int :=
  ps.map fun p => ((p.val C).map bv_uint).getD 0

/-- The response words of a slot. -/
def respOf (C : Ports) (sl : SlotS) : List Int := portVals C sl.resp

/-- The observed words of a slot. -/
def obsOf (C : Ports) (sl : SlotS) : List Int :=
  sl.obs.map fun w => (w.val C).getD 0

/-- The cycle's spec events: the slots that fire, in order. -/
def evsOf (sp : RefMapSpec) (C : Ports) : List (Nat × List Int) :=
  sp.slots.filterMap fun sl => if sl.kind.bit C then some (sl.chan, respOf C sl) else none

/-- The refinement map's state: the `alpha` words, normalized. -/
def alphaSt (sp : RefMapSpec) (outs : Array BV) : StateM := flopSt (outsVals outs sp.alpha)

/-- What one cycle must satisfy, slot by slot from the map's state `σ`: each slot that fires is
allowed and presents the spec's words, and the chain ends in the next cycle's map state. -/
def CycleOK (DS : DesignCert) (sp : RefMapSpec) (C : Ports) : StateM → List SlotS → StateM → Prop
  | σ, [], σ' => σ' = σ
  | σ, sl :: sls, σ' =>
      if sl.kind.bit C then
        ∃ ch, sp.chans[sl.chan]? = some ch ∧
          propBit (specOuts DS sp σ ch (respOf C sl)) ch.allow = true ∧
          obsOf C sl = outVals (specOuts DS sp σ ch (respOf C sl)) ch.obsOut ∧
          CycleOK DS sp C (specStep DS sp σ ch (respOf C sl)) sls σ'
      else CycleOK DS sp C σ sls σ'

/-! ## The spec's input vector, symbolically -/

/-- A constant word of width `w`. -/
def constLeaf (w : Nat) (v : Int) : Leaf := ⟨w, bvConst (mk_bv w v)⟩

theorem constLeaf_rel (σ : Asg) (w : Nat) (v : Int) : LeafRel σ (constLeaf w v) (mk_bv w v) :=
  ⟨rfl, (toNat_bvConst σ (mk_bv w v)).symm⟩

/-- Leaves of some port words. -/
def portLeaves (P : SPorts) : List PortW → Option (List Leaf)
  | [] => some []
  | p :: ps => do
      let l ← p.sym P
      let r ← portLeaves P ps
      pure (l :: r)

theorem portLeaves_sound {σ : Asg} {P : SPorts} {C : Ports} (hP : PortsRel σ P C) :
    ∀ (ps : List PortW) (ls : List Leaf), portLeaves P ps = some ls →
      ∃ bs, LeavesRel σ ls bs ∧ bs.map bv_uint = portVals C ps
  | [], ls, h => by simp [portLeaves] at h; subst h; exact ⟨[], .nil, rfl⟩
  | p :: ps, ls, h => by
      simp only [portLeaves, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨l, hl, r, hr, rfl⟩ := h
      obtain ⟨b, hb, hlb⟩ := p.sym_sound hP l hl
      obtain ⟨bs, hbs, hv⟩ := portLeaves_sound hP ps r hr
      exact ⟨b :: bs, .cons hlb hbs, by simp only [portVals] at hv ⊢; simp [hb, hv]⟩

theorem LeavesRel.get {σ : Asg} : ∀ {ls : List Leaf} {bs : List BV}, LeavesRel σ ls bs →
    ∀ {i : Nat} {l : Leaf}, ls[i]? = some l → ∃ b, bs[i]? = some b ∧ LeafRel σ l b
  | _, _, .nil, i, l, h => by simp at h
  | _, _, .cons hp hs, 0, l, h => by
      simp only [List.getElem?_cons_zero, Option.some.injEq] at h; subst h
      exact ⟨_, rfl, hp⟩
  | _, _, .cons _ hs, i + 1, l, h => by
      simp only [List.getElem?_cons_succ] at h ⊢
      exact LeavesRel.get hs h

/-- Spec input `k`'s leaf: its response leaf (of the input's width), or its constant. -/
def inLeaf (inW : List Nat) (ch : ChanS) (rls : List Leaf) (k : Nat) : Option Leaf :=
  match posOf ch.respIn k with
  | some i => match rls[i]? with
    | some l => if l.1 = inW.getD k 0 then some l else none
    | none => none
  | none => some (constLeaf (inW.getD k 0) (inVal ch [] k))

def inLeaves (inW : List Nat) (ch : ChanS) (rls : List Leaf) : List Nat → Option (List Leaf)
  | [] => some []
  | k :: ks => do
      let l ← inLeaf inW ch rls k
      let r ← inLeaves inW ch rls ks
      pure (l :: r)

theorem inLeaf_rel {σ : Asg} {inW : List Nat} {ch : ChanS} {rls : List Leaf} {bs : List BV}
    (hR : LeavesRel σ rls bs) {k : Nat} {l : Leaf} (h : inLeaf inW ch rls k = some l) :
    LeafRel σ l (mk_bv (inW.getD k 0) (inVal ch (bs.map bv_uint) k)) := by
  unfold inLeaf at h
  split at h
  · rename_i i hi
    split at h
    · rename_i l' hl'
      split at h
      · rename_i hw
        simp only [Option.some.injEq] at h; subst h
        obtain ⟨b, hb, hlb⟩ := LeavesRel.get hR hl'
        have hv : inVal ch (bs.map bv_uint) k = bv_uint b := by
          simp only [inVal, hi, List.getD_eq_getElem?_getD, List.getElem?_map, hb, Option.map_some,
            Option.getD_some]
        rw [hv, ← hw, ← hlb.1]
        exact ⟨hlb.1, by rw [show mk_bv b.width (bv_uint b) = normBV b from rfl, bv_uint_normBV]; exact hlb.2⟩
      · cases h
    · cases h
  · rename_i hi
    simp only [Option.some.injEq] at h; subst h
    rw [inVal_none hi (bs.map bv_uint) []]
    exact constLeaf_rel σ _ _

theorem inLeaves_rel {σ : Asg} {inW : List Nat} {ch : ChanS} {rls : List Leaf} {bs : List BV}
    (hR : LeavesRel σ rls bs) :
    ∀ (ks : List Nat) (ls : List Leaf), inLeaves inW ch rls ks = some ls →
      LeavesRel σ ls (ks.map fun k => mk_bv (inW.getD k 0) (inVal ch (bs.map bv_uint) k))
  | [], ls, h => by simp [inLeaves] at h; subst h; exact .nil
  | k :: ks, ls, h => by
      simp only [inLeaves, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨l, hl, r, hr, rfl⟩ := h
      exact .cons (inLeaf_rel hR hl) (inLeaves_rel hR ks r hr)

/-- The spec's input vector for an event on `ch` whose responses are the leaves `rls`. -/
def specInS (inW : List Nat) (ch : ChanS) (rls : List Leaf) : Option SVal :=
  (inLeaves inW ch rls (List.range inW.length)).map fun ls => sListOf (ls.map fun p => SVal.bv p.1 p.2)

theorem specInS_rel {σ : Asg} {inW : List Nat} {ch : ChanS} {rls : List Leaf} {bs : List BV}
    (hR : LeavesRel σ rls bs) {s : SVal} (h : specInS inW ch rls = some s) :
    SRel σ s (encBVs (specIn inW ch (bs.map bv_uint))) := by
  simp only [specInS, Option.map_eq_some_iff] at h
  obtain ⟨ls, hls, rfl⟩ := h
  have h2 := sListOf_rel ofBV (leaves_relLF (inLeaves_rel hR _ ls hls))
  unfold encBVs encArr specIn
  rw [List.toList_toArray]
  exact h2

/-! ## A cycle's spec steps, symbolically -/

/-- What the chain records of one slot: its firing bit, its channel, the spec's outputs. -/
structure SlotRec where
  kind : BVExpr 1
  ch   : ChanS
  so   : SVal

def chainS (DS : DesignCert) (RS : Program) (sp : RefMapSpec) (P : SPorts) :
    SymSt → SVal → List SlotS → Except String (List SlotRec × SVal × SymSt)
  | st, σS, [] => .ok ([], σS, st)
  | st, σS, sl :: sls =>
      match sl.kind.sym P, sp.chans[sl.chan]?, portLeaves P sl.resp with
      | some kind, some ch, some rls =>
        match specInS sp.specInW ch rls with
        | none => .error "a response word's width is not its spec input's"
        | some sIn =>
          match symStep RS sp.fuelS st (.conc (encEdges (edgesAll DS))) sIn σS with
          | .error m => .error m
          | .ok (sres, st1) =>
            match splitResult sres with
            | none => .error "the spec's answer is not a result record"
            | some (sn, so) =>
              match rebuildS sn sp.LS.fws with
              | none => .error "the spec's next state is not of the declared shape"
              | some σn =>
                match merge kind σn σS with
                | none => .error "the spec's next state does not merge"
                | some σ' =>
                  match chainS DS RS sp P st1 σ' sls with
                  | .error m => .error m
                  | .ok (rest, σe, st2) => .ok (⟨kind, ch, so⟩ :: rest, σe, st2)
      | _, _, _ => .error "a slot's kind, channel or response words do not resolve"

/-- The concrete chain: the map's state after the slots that fire. -/
noncomputable def chainC (DS : DesignCert) (sp : RefMapSpec) (C : Ports) :
    StateM → List SlotS → StateM
  | σ, [] => σ
  | σ, sl :: sls =>
      if sl.kind.bit C then
        match sp.chans[sl.chan]? with
        | some ch => chainC DS sp C (specStep DS sp σ ch (respOf C sl)) sls
        | none => chainC DS sp C σ sls
      else chainC DS sp C σ sls

/-- The records denote the concrete chain: each slot's bit, channel and spec outputs. -/
def RecsOK (σ : Asg) (DS : DesignCert) (sp : RefMapSpec) (C : Ports) :
    List SlotRec → StateM → List SlotS → Prop
  | [], _, [] => True
  | r :: rs, σC, sl :: sls =>
      bitOf (r.kind.eval σ) = sl.kind.bit C ∧ sp.chans[sl.chan]? = some r.ch ∧
      SRel σ r.so (encBVs (specOuts DS sp σC r.ch (respOf C sl))) ∧
      RecsOK σ DS sp C rs (if sl.kind.bit C then specStep DS sp σC r.ch (respOf C sl) else σC) sls
  | _, _, _ => False

theorem chainS_extends {DS : DesignCert} {RS : Program} {sp : RefMapSpec} {P : SPorts} :
    ∀ (sls : List SlotS) (st : SymSt) (σS : SVal) (recs : List SlotRec) (σe : SVal) (st' : SymSt),
      chainS DS RS sp P st σS sls = .ok (recs, σe, st') → st.Extends st'
  | [], st, σS, recs, σe, st', h => by
      simp only [chainS, Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨-, -, rfl⟩ := h; exact .refl _
  | sl :: sls, st, σS, recs, σe, st', h => by
      unfold chainS at h
      split at h
      · split at h
        · cases h
        · split at h
          · cases h
          · rename_i sres st1 h1
            split at h
            · cases h
            · split at h
              · cases h
              · split at h
                · cases h
                · split at h
                  · cases h
                  · rename_i rest σe' st2 h2
                    simp only [Except.ok.injEq, Prod.mk.injEq] at h
                    obtain ⟨-, -, rfl⟩ := h
                    exact (symStep_extends h1).trans (chainS_extends sls _ _ _ _ _ h2)
      · cases h

/-- **The chain is sound.** From a state value denoting a spec state of the declared shape, the
chain's end denotes the concrete chain's end, and its records denote each slot's bit and the
spec's outputs at the state before it. -/
theorem chainS_sound {DS : DesignCert} {RS : Program} {sp : RefMapSpec} {P : SPorts} {C : Ports}
    (hrS : MemResidual DS RS) (hAS : admitsWB DS = true) (hside : specSideOK DS sp = true)
    {σ : Asg} {stF : SymSt} (hc : stF.Consistent σ) (hP : PortsRel σ P C) :
    ∀ (sls : List SlotS) (st : SymSt) (σS : SVal) (σC : StateM) (recs : List SlotRec) (σe : SVal)
      (st' : SymSt),
      chainS DS RS sp P st σS sls = .ok (recs, σe, st') → st'.Extends stF →
      SpecSt sp σC → SRel σ σS (StateM.encStateM σC) →
      SRel σ σe (StateM.encStateM (chainC DS sp C σC sls)) ∧ RecsOK σ DS sp C recs σC sls
      ∧ SpecSt sp (chainC DS sp C σC sls)
  | [], st, σS, σC, recs, σe, st', h, _, hσ, hS => by
      simp only [chainS, Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl, -⟩ := h
      exact ⟨hS, trivial, hσ⟩
  | sl :: sls, st, σS, σC, recs, σe, st', h, hx, hσ, hS => by
      unfold chainS at h
      split at h
      · rename_i kind ch rls hk hch hrls
        split at h
        · cases h
        · rename_i sIn hIn
          split at h
          · cases h
          · rename_i sres st1 h1
            split at h
            · cases h
            · rename_i sn so hsplit
              split at h
              · cases h
              · rename_i σn hσn
                split at h
                · cases h
                · rename_i σ' hmerge
                  split at h
                  · cases h
                  · rename_i rest σe' st2 h2
                    simp only [Except.ok.injEq, Prod.mk.injEq] at h
                    obtain ⟨rfl, rfl, rfl⟩ := h
                    have x1 : st1.Extends stF := (chainS_extends sls _ _ _ _ _ h2).trans hx
                    obtain ⟨bs, hbs, hbv⟩ := portLeaves_sound hP sl.resp rls hrls
                    have rI := specInS_rel hbs hIn
                    rw [hbv] at rI
                    have hrt := spec_runtime hside hσ ch (respOf C sl)
                    have rres := symStep_eval (hrS hAS hrt) h1 hc x1 .conc rI hS
                    obtain ⟨rn, ro⟩ := splitResult_sound rres hsplit
                    have rσn := rebuildS_rel rn hσn
                    have hbit := BitE.sym_sound hP sl.kind kind hk
                    have hσ' := specStep_specSt hAS hside hσ ch (respOf C sl)
                    -- the merged state denotes the taken branch
                    have rmerge : SRel σ σ' (StateM.encStateM
                        (if sl.kind.bit C then specStep DS sp σC ch (respOf C sl) else σC)) := by
                      by_cases hb : sl.kind.bit C = true
                      · rw [if_pos hb]
                        exact merge_t (by rw [hbit, hb]) σn σS rσn hmerge
                      · rw [if_neg hb]
                        exact merge_f (by rw [hbit]; simpa using hb) σn σS hS hmerge
                    have hσm : SpecSt sp (if sl.kind.bit C then specStep DS sp σC ch (respOf C sl) else σC) := by
                      split <;> assumption
                    obtain ⟨ih1, ih2, ih3⟩ := chainS_sound hrS hAS hside hc hP sls st1 σ' _ rest σe' st2
                      h2 hx hσm rmerge
                    have hchain : chainC DS sp C σC (sl :: sls) =
                        chainC DS sp C (if sl.kind.bit C then specStep DS sp σC ch (respOf C sl) else σC) sls := by
                      simp only [chainC]
                      split
                      · rw [hch]
                      · rfl
                    rw [hchain]
                    exact ⟨ih1, ⟨hbit, hch, ro, ih2⟩, ih3⟩
      · cases h

end HDLProof.Check

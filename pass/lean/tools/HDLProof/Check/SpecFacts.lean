/-
`Check.SpecFacts` -- what the spec certificate must satisfy on its own, checked once.

The composition theorem needs a spec whose channel streams depend only on the environment's
answers. Two facts give that, both checked from a FREE spec state of the declared shape, with
FREE response words:

- **Moore**: a channel's allowed bit and observed words are the same whatever the response in
  that step (they are read before the response exists).
- **Confluence**: whenever two different channels are both allowed,
  - an event on one keeps the other allowed (`persist`);
  - it does not change the other's observed words (`outC`);
  - the two events in either order reach the same state (`comm`).

`runSpecFactsOf_sound` gives `SpecFactsOK`, whose `conf` field is `Theory.Spec.Confluent` of the
channel spec `specOf` on normalized states of the declared shape. A spec where at most one
channel is ever allowed passes `persist`/`outC`/`comm` with obligations that are trivially
unsatisfiable. Mathlib-free.
-/
import HDLProof.Check.RefMapSound

open Projection
namespace HDLProof.Check
open HDLProof.Sym HDLProof.Kit Std.Tactic.BVDecide Projection.MemProjection Projection.Hw
  Projection.HwMem Compiler

/-! ## One spec step, symbolically -/

def specEvalS (DS : DesignCert) (RS : Program) (sp : RefMapSpec) (st : SymSt) (σS sIn : SVal) :
    Except String (SVal × SVal × SymSt) :=
  match symStep RS sp.fuelS st (.conc (encEdges (edgesAll DS))) sIn σS with
  | .error m => .error m
  | .ok (sres, st1) =>
    match splitResult sres with
    | none => .error "the spec's answer is not a result record"
    | some (sn, so) =>
      match rebuildS sn sp.LS.fws with
      | none => .error "the spec's next state is not of the declared shape"
      | some σn => .ok (σn, so, st1)

theorem specEvalS_extends {DS : DesignCert} {RS : Program} {sp : RefMapSpec} {st st' : SymSt}
    {σS sIn σn so : SVal} (h : specEvalS DS RS sp st σS sIn = .ok (σn, so, st')) : st.Extends st' := by
  unfold specEvalS at h
  split at h
  · cases h
  · rename_i sres st1 h1
    split at h
    · cases h
    · split at h
      · cases h
      · simp only [Except.ok.injEq, Prod.mk.injEq] at h
        obtain ⟨-, -, rfl⟩ := h
        exact symStep_extends h1

theorem specEvalS_sound {DS : DesignCert} {RS : Program} {sp : RefMapSpec}
    (hrS : MemResidual DS RS) (hAS : admitsWB DS = true) (hside : specSideOK DS sp = true)
    {σ : Asg} {stF : SymSt} (hc : stF.Consistent σ) {st st' : SymSt} {σS sIn σn so : SVal}
    (h : specEvalS DS RS sp st σS sIn = .ok (σn, so, st')) (hx : st'.Extends stF)
    {σC : StateM} (hσ : SpecSt sp σC) (hS : SRel σ σS (StateM.encStateM σC))
    {ch : ChanS} {rs : List Int} (hIn : SRel σ sIn (encBVs (specIn sp.specInW ch rs))) :
    SRel σ σn (StateM.encStateM (specStep DS sp σC ch rs))
    ∧ SRel σ so (encBVs (specOuts DS sp σC ch rs)) := by
  unfold specEvalS at h
  split at h
  · cases h
  · rename_i sres st1 h1
    split at h
    · cases h
    · rename_i sn so' hsplit
      split at h
      · cases h
      · rename_i σn' hσn
        simp only [Except.ok.injEq, Prod.mk.injEq] at h
        obtain ⟨rfl, rfl, rfl⟩ := h
        have rres := symStep_eval (hrS hAS (spec_runtime hside hσ ch rs)) h1 hc hx .conc hIn hS
        obtain ⟨rn, ro⟩ := splitResult_sound rres hsplit
        exact ⟨rebuildS_rel rn hσn, ro⟩

/-! ## Free response words -/

/-- The widths of a channel's response inputs. -/
def respWs (inW : List Nat) (ch : ChanS) : List Nat := ch.respIn.map fun k => inW.getD k 0

/-- Response words as variables from `k0` on. -/
def respLeaves : List Nat → Nat → List Leaf
  | [], _ => []
  | w :: ws, k0 => ⟨w, .var k0⟩ :: respLeaves ws (k0 + 1)

/-- The response words `rs` stands for, at the response inputs' widths. -/
def respBVs (ws : List Nat) (rs : List Int) : List BV :=
  (List.range ws.length).map fun i => mk_bv (ws.getD i 0) (rs.getD i 0)

theorem respBVs_widths (ws : List Nat) (rs : List Int) : (respBVs ws rs).map BV.width = ws := by
  apply List.ext_getElem (by simp [respBVs])
  intro i h1 h2
  simp [respBVs, mk_bv, List.getD_eq_getElem?_getD, List.getElem?_eq_getElem h2]

theorem respLeaves_rel {σ : Asg} :
    ∀ (bs : List BV) (k0 : Nat), AgreeAt σ k0 (bs.map pbvOf) →
      LeavesRel σ (respLeaves (bs.map BV.width) k0) bs
  | [], _, _ => .nil
  | b :: bs, k0, h => by
      rw [List.map_cons, AgreeAt_cons] at h
      have hb := SRel_bv_inv (bvVar_rel (w := b.width) rfl h.1)
      obtain ⟨b', hb', _, hu⟩ := hb
      rw [asBV_ofBV] at hb'
      cases hb'
      exact .cons ⟨rfl, hu⟩ (respLeaves_rel bs (k0 + 1) h.2)

theorem posOf_spec : ∀ (l : List Nat) (k i : Nat), posOf l k = some i → l[i]? = some k
  | [], _, _, h => by simp [posOf] at h
  | x :: xs, k, i, h => by
      simp only [posOf] at h
      split at h
      · rename_i hx; simp only [Option.some.injEq] at h; subst h; simp [hx]
      · simp only [Option.map_eq_some_iff] at h
        obtain ⟨j, hj, rfl⟩ := h
        simpa using posOf_spec xs k j hj

theorem mk_bv_emod (w : Nat) (v : Int) : mk_bv w (v % 2 ^ w) = mk_bv w v := by
  simp [mk_bv, Int.emod_emod]

/-- The spec's input from the response words' normalized values is the one from `rs`. -/
theorem specIn_respBVs (inW : List Nat) (ch : ChanS) (rs : List Int) :
    specIn inW ch ((respBVs (respWs inW ch) rs).map bv_uint) = specIn inW ch rs := by
  unfold specIn
  congr 1
  apply List.map_congr_left
  intro k _
  unfold inVal
  cases hp : posOf ch.respIn k with
  | none => rfl
  | some i =>
    simp only
    have hk := posOf_spec ch.respIn k i hp
    have hi : i < ch.respIn.length := (List.getElem?_eq_some_iff.mp hk).1
    have hki : ch.respIn[i] = k := (List.getElem?_eq_some_iff.mp hk).2
    have hlen : i < (respWs inW ch).length := by simp [respWs, hi]
    have hw : (respWs inW ch).getD i 0 = inW.getD k 0 := by
      simp [respWs, List.getD_eq_getElem?_getD, List.getElem?_map, hk]
    simp only [respBVs, List.getD_eq_getElem?_getD, List.getElem?_map, List.getElem?_range hlen,
      Option.map_some, Option.getD_some]
    rw [show bv_uint (mk_bv ((respWs inW ch)[i]?.getD 0) (rs[i]?.getD 0)) =
        (rs[i]?.getD 0) % 2 ^ ((respWs inW ch)[i]?.getD 0) by simp [bv_uint, mk_bv, Int.emod_emod]]
    have hw' : (respWs inW ch)[i]?.getD 0 = inW[k]?.getD 0 := by
      simpa [List.getD_eq_getElem?_getD] using hw
    rw [hw', mk_bv_emod]

/-! ## The runs -/

/-- Moore's run for channel `ch`: its outputs at a free state with free responses, and with
zero responses. -/
def mooreRun (DS : DesignCert) (RS : Program) (sp : RefMapSpec) (ch : ChanS) :
    Except String (SVal × SVal × SymSt) :=
  let L := layoutSize sp.LS
  let ws := respWs sp.specInW ch
  match specInS sp.specInW ch (respLeaves ws L) with
  | none => .error "a response input's width"
  | some sIn =>
    match specEvalS DS RS sp ⟨L + ws.length, #[]⟩ (stateVar sp.LS 0) sIn with
    | .error m => .error m
    | .ok (_, oA, st1) =>
      match specEvalS DS RS sp st1 (stateVar sp.LS 0) (specIn0S sp.specInW ch) with
      | .error m => .error m
      | .ok (_, oZ, st2) => .ok (oA, oZ, st2)

/-- The values a pair of channels' run produces. -/
structure PairRun where
  oC0  : SVal
  oD0  : SVal
  σc   : SVal
  oCD0 : SVal
  σcd  : SVal
  σdc  : SVal
  st   : SymSt

/-- An ordered pair `c, d`'s run from a free state, responses `rC` then `rD` free. -/
def pairRun (DS : DesignCert) (RS : Program) (sp : RefMapSpec) (chc chd : ChanS) :
    Except String PairRun :=
  let L := layoutSize sp.LS
  let wc := respWs sp.specInW chc
  let wd := respWs sp.specInW chd
  let σV := stateVar sp.LS 0
  match specInS sp.specInW chc (respLeaves wc L), specInS sp.specInW chd (respLeaves wd (L + wc.length)) with
  | some inC, some inD =>
    match specEvalS DS RS sp ⟨L + wc.length + wd.length, #[]⟩ σV (specIn0S sp.specInW chc) with
    | .error m => .error m
    | .ok (_, oC0, s1) =>
    match specEvalS DS RS sp s1 σV (specIn0S sp.specInW chd) with
    | .error m => .error m
    | .ok (_, oD0, s2) =>
    match specEvalS DS RS sp s2 σV inC with
    | .error m => .error m
    | .ok (σc, _, s3) =>
    match specEvalS DS RS sp s3 σc (specIn0S sp.specInW chd) with
    | .error m => .error m
    | .ok (_, oCD0, s4) =>
    match specEvalS DS RS sp s4 σV inD with
    | .error m => .error m
    | .ok (σd, _, s5) =>
    match specEvalS DS RS sp s5 σc inD with
    | .error m => .error m
    | .ok (σcd, _, s6) =>
    match specEvalS DS RS sp s6 σd inC with
    | .error m => .error m
    | .ok (σdc, _, s7) => .ok ⟨oC0, oD0, σc, oCD0, σcd, σdc, s7⟩
  | _, _ => .error "a response input's width"

/-! ## Targets, obligations, the check -/

inductive STarget where
  | moore (c : Nat)
  | persist (c d : Nat)
  | outC (c d : Nat)
  | comm (c d : Nat)
  deriving Repr, Inhabited, DecidableEq

def chanPairs (sp : RefMapSpec) : List (Nat × Nat) :=
  (List.range sp.chans.length).flatMap fun c =>
    (List.range sp.chans.length).filterMap fun d => if c = d then none else some (c, d)

def targetsSF (sp : RefMapSpec) : List STarget :=
  (List.range sp.chans.length).map .moore
    ++ (chanPairs sp).flatMap fun (c, d) =>
      [.persist c d, .outC c d] ++ (if c < d then [.comm c d] else [])

/-- Some outputs of two output vectors are equal. -/
def obsEqLits (oA oB : SVal) (os : List Nat) : Option (List BVLogicalExpr) :=
  os.mapM fun o => do
    let x ← outLeaf oA o
    let y ← outLeaf oB o
    pure (eqBvE x y)

theorem obsEqLits_sound {σ : Asg} {oA oB : SVal} {a b : Array BV} (hA : SRel σ oA (encBVs a))
    (hB : SRel σ oB (encBVs b)) :
    ∀ (os : List Nat) (ls : List BVLogicalExpr), obsEqLits oA oB os = some ls →
      (conjE ls).eval σ = true → outVals a os = outVals b os
  | [], _, _, _ => rfl
  | o :: os, ls, h, hev => by
      simp only [obsEqLits, List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff,
        Option.pure_def, Option.some.injEq] at h
      obtain ⟨e, ⟨x, hx, y, hy, rfl⟩, es, hes, rfl⟩ := h
      simp only [conjE, eval_and, Bool.and_eq_true] at hev
      obtain ⟨bx, hbx, hlx⟩ := outLeaf_sound hA hx
      obtain ⟨by', hby, hly⟩ := outLeaf_sound hB hy
      rw [eval_eqBvE hlx.2 hly.2] at hev
      have ih := obsEqLits_sound hA hB os es (by simp [obsEqLits, hes]) hev.2
      simp only [outVals, List.map_cons, Array.getD_eq_getD_getElem?, hbx, hby, Option.getD_some]
      simp only [outVals, Array.getD_eq_getD_getElem?] at ih
      rw [ih]; simpa using hev.1

/-- Every flop of two state values is equal. -/
def allFlopsLit (s t : SVal) (n : Nat) : Option BVLogicalExpr :=
  ((List.range n).mapM (flopEqLit s t)).map conjE

theorem allFlopsLit_sound {σ : Asg} {s t : SVal} {st1 st2 : StateM}
    (h1 : SRel σ s (StateM.encStateM st1)) (h2 : SRel σ t (StateM.encStateM st2)) {n : Nat}
    {φ : BVLogicalExpr} (h : allFlopsLit s t n = some φ) (hev : φ.eval σ = true) :
    ∀ k, k < n → ∃ b1 b2, st1.flops[k]? = some b1 ∧ st2.flops[k]? = some b2 ∧ bv_uint b1 = bv_uint b2 := by
  intro k hk
  have := groupLit_sound h1 h2 (List.range n) φ (by simpa [groupLit, allFlopsLit] using h) hev k
    (List.mem_range.mpr hk)
  exact this

/-- A target's obligation, from its own run. -/
def sfObl (DS : DesignCert) (RS : Program) (sp : RefMapSpec) : STarget → Option BVLogicalExpr
  | .moore c => do
      let ch ← sp.chans[c]?
      match mooreRun DS RS sp ch with
      | .ok (oA, oZ, st) =>
          if scopedB st then
            let alA ← propOf oA ch.allow
            let alZ ← propOf oZ ch.allow
            let os ← obsEqLits oA oZ ch.obsOut
            pure (coi st (.not (conjE (.gate .beq alA alZ :: os))))
          else none
      | .error _ => none
  | .persist c d => do
      let chc ← sp.chans[c]?
      let chd ← sp.chans[d]?
      match pairRun DS RS sp chc chd with
      | .ok R =>
          if scopedB R.st then
            let enC ← propOf R.oC0 chc.allow
            let enD ← propOf R.oD0 chd.allow
            let enCD ← propOf R.oCD0 chd.allow
            pure (coi R.st (conjE [enC, enD, .not enCD]))
          else none
      | .error _ => none
  | .outC c d => do
      let chc ← sp.chans[c]?
      let chd ← sp.chans[d]?
      match pairRun DS RS sp chc chd with
      | .ok R =>
          if scopedB R.st then
            let enC ← propOf R.oC0 chc.allow
            let enD ← propOf R.oD0 chd.allow
            let os ← obsEqLits R.oCD0 R.oD0 chd.obsOut
            pure (coi R.st (conjE [enC, enD, .not (conjE os)]))
          else none
      | .error _ => none
  | .comm c d => do
      let chc ← sp.chans[c]?
      let chd ← sp.chans[d]?
      match pairRun DS RS sp chc chd with
      | .ok R =>
          if scopedB R.st then
            let enC ← propOf R.oC0 chc.allow
            let enD ← propOf R.oD0 chd.allow
            let eqs ← allFlopsLit R.σcd R.σdc sp.LS.fws.length
            pure (coi R.st (conjE [enC, enD, .not eqs]))
          else none
      | .error _ => none

/-- **The spec facts check.** `pfs[i]` is the proof of `targetsSF sp`'s `i`-th target. -/
def runSpecFactsOf (DS : DesignCert) (RS : Program) (sp : RefMapSpec) (pfs : List ByteArray) : Bool :=
  specSideOK DS sp && sp.LS.mds.isEmpty
    && (targetsSF sp).zipIdx.all fun (t, i) =>
      match sfObl DS RS sp t with
      | some φ => checkUnsat φ (pfs.getD i ByteArray.empty)
      | none => false

/-! ## Soundness -/

structure SpecFactsOK (DS : DesignCert) (sp : RefMapSpec) : Prop where
  moore : ∀ (σC : StateM) (c : Nat) (ch : ChanS) (rs : List Int), SpecNorm sp σC →
    sp.chans[c]? = some ch →
    propBit (specOuts DS sp σC ch rs) ch.allow = propBit (specOuts DS sp σC ch []) ch.allow
    ∧ outVals (specOuts DS sp σC ch rs) ch.obsOut = outVals (specOuts DS sp σC ch []) ch.obsOut
  conf : (specOf DS sp).Confluent (SpecNorm sp)

theorem stMatches_of_specSt {sp : RefMapSpec} {σC : StateM} (hmd : sp.LS.mds = [])
    (h : SpecSt sp σC) : StMatches sp.LS σC := by
  obtain ⟨hm, hw⟩ := h
  refine ⟨hw, by simp [hm, hmd], fun j m aw dw h1 _ => by simp [hm] at h1⟩

theorem layoutSize_of_mds {sp : RefMapSpec} (hmd : sp.LS.mds = []) :
    layoutSize sp.LS = sp.LS.fws.length := by
  simp [layoutSize, hmd]

theorem specOf_some {DS : DesignCert} {sp : RefMapSpec} {σ : StateM} {c : Nat}
    (h : (specOf DS sp).en σ c = true) : ∃ ch, sp.chans[c]? = some ch := by
  simp only [specOf] at h
  cases hc : sp.chans[c]? with
  | none => rw [hc] at h; cases h
  | some ch => exact ⟨ch, rfl⟩

theorem sf_parts {DS : DesignCert} {RS : Program} {sp : RefMapSpec} {pfs : List ByteArray}
    (h : runSpecFactsOf DS RS sp pfs = true) :
    specSideOK DS sp = true ∧ sp.LS.mds = []
    ∧ ∀ t ∈ targetsSF sp, ∃ φ, sfObl DS RS sp t = some φ ∧ BVLogicalExpr.Unsat φ := by
  unfold runSpecFactsOf at h
  simp only [Bool.and_eq_true, List.isEmpty_iff] at h
  obtain ⟨⟨hside, hmd⟩, hall⟩ := h
  refine ⟨hside, hmd, fun t ht => ?_⟩
  obtain ⟨i, hi, rfl⟩ := List.getElem_of_mem ht
  have h2 := (List.all_eq_true.mp hall) ((targetsSF sp)[i], i)
    (List.mem_zipIdx_iff_getElem?.mpr (by simp))
  simp only at h2
  cases ho : sfObl DS RS sp (targetsSF sp)[i] with
  | none => rw [ho] at h2; cases h2
  | some φ => rw [ho] at h2; exact ⟨φ, rfl, checkUnsat_sound h2⟩

section
variable {DS : DesignCert} {RS : Program} {sp : RefMapSpec}

theorem respBVs_length (ws : List Nat) (rs : List Int) : (respBVs ws rs).length = ws.length := by
  simp [respBVs]

theorem resp_rel {σ : Asg} {ws : List Nat} {rs : List Int} {k0 : Nat}
    (h : AgreeAt σ k0 ((respBVs ws rs).map pbvOf)) : LeavesRel σ (respLeaves ws k0) (respBVs ws rs) := by
  have := respLeaves_rel (respBVs ws rs) k0 h
  rwa [respBVs_widths] at this

theorem mooreRun_facts (hrS : MemResidual DS RS) (hAS : admitsWB DS = true)
    (hside : specSideOK DS sp = true) (hmd : sp.LS.mds = []) {ch : ChanS} {oA oZ : SVal} {st : SymSt}
    (h : mooreRun DS RS sp ch = .ok (oA, oZ, st)) (hsc : scopedB st = true) {σC : StateM}
    (hσ : SpecNorm sp σC) (rs : List Int) :
    ∃ σ, st.Consistent σ ∧ SRel σ oA (encBVs (specOuts DS sp σC ch rs))
      ∧ SRel σ oZ (encBVs (specOuts DS sp σC ch [])) := by
  unfold mooreRun at h
  simp only at h
  split at h
  · cases h
  · rename_i sIn hIn
    split at h
    · cases h
    · rename_i r1 st1 h1
      split at h
      · cases h
      · rename_i r2 st2 h2
        simp only [Except.ok.injEq, Prod.mk.injEq] at h
        obtain ⟨rfl, rfl, rfl⟩ := h
        have x2 := specEvalS_extends h2
        have x1 := (specEvalS_extends h1).trans x2
        let ws := respWs sp.specInW ch
        let xs := stateVals σC ++ (respBVs ws rs).map pbvOf
        have hxl : xs.length = st2.base := by
          rw [← x1.1, List.length_append, stateVals_length (stMatches_of_specSt hmd hσ.1),
            List.length_map, respBVs_length]
        obtain ⟨hc, hag⟩ := extendSt_agree hxl hsc
        rw [AgreeAt_append, stateVals_length (stMatches_of_specSt hmd hσ.1), Nat.zero_add] at hag
        have hS := stateVar_rel (stMatches_of_specSt hmd hσ.1) hag.1
        have hRl := resp_rel hag.2
        have rIn := specInS_rel hRl hIn
        rw [specIn_respBVs] at rIn
        refine ⟨_, hc, ?_, ?_⟩
        · exact (specEvalS_sound hrS hAS hside hc h1 x2 hσ.1 hS rIn).2
        · exact (specEvalS_sound hrS hAS hside hc h2 (.refl _) hσ.1 hS
            (specIn0S_rel _ sp.specInW ch)).2

theorem specStep_norm (hAS : admitsWB DS = true) (hside : specSideOK DS sp = true) {σ : StateM}
    (hσ : SpecNorm sp σ) (ch : ChanS) (rs : List Int) : SpecNorm sp (specStep DS sp σ ch rs) :=
  ⟨specStep_specSt hAS hside hσ.1 ch rs, normSt_norm _ _⟩

theorem pairRun_facts (hrS : MemResidual DS RS) (hAS : admitsWB DS = true)
    (hside : specSideOK DS sp = true) (hmd : sp.LS.mds = []) {chc chd : ChanS} {R : PairRun}
    (h : pairRun DS RS sp chc chd = .ok R) (hsc : scopedB R.st = true) {s : StateM}
    (hs : SpecNorm sp s) (r q : List Int) :
    ∃ σ, R.st.Consistent σ
      ∧ SRel σ R.oC0 (encBVs (specOuts DS sp s chc []))
      ∧ SRel σ R.oD0 (encBVs (specOuts DS sp s chd []))
      ∧ SRel σ R.oCD0 (encBVs (specOuts DS sp (specStep DS sp s chc r) chd []))
      ∧ SRel σ R.σcd (StateM.encStateM (specStep DS sp (specStep DS sp s chc r) chd q))
      ∧ SRel σ R.σdc (StateM.encStateM (specStep DS sp (specStep DS sp s chd q) chc r)) := by
  unfold pairRun at h
  simp only at h
  split at h
  · rename_i inC inD hC hD
    split at h; · cases h
    rename_i x1 oC0 s1 h1
    split at h; · cases h
    rename_i x2 oD0 s2 h2
    split at h; · cases h
    rename_i σc x3 s3 h3
    split at h; · cases h
    rename_i x4 oCD0 s4 h4
    split at h; · cases h
    rename_i σd x5 s5 h5
    split at h; · cases h
    rename_i σcd x6 s6 h6
    split at h; · cases h
    rename_i σdc x7 s7 h7
    simp only [Except.ok.injEq] at h
    subst h
    simp only at hsc ⊢
    have e7 := specEvalS_extends h7
    have e6 := (specEvalS_extends h6).trans e7
    have e5 := (specEvalS_extends h5).trans e6
    have e4 := (specEvalS_extends h4).trans e5
    have e3 := (specEvalS_extends h3).trans e4
    have e2 := (specEvalS_extends h2).trans e3
    have e1 := (specEvalS_extends h1).trans e2
    let wc := respWs sp.specInW chc
    let wd := respWs sp.specInW chd
    let xs := stateVals s ++ (respBVs wc r).map pbvOf ++ (respBVs wd q).map pbvOf
    have hm := stMatches_of_specSt hmd hs.1
    have hxl : xs.length = s7.base := by
      rw [← e1.1, List.length_append, List.length_append, stateVals_length hm, List.length_map,
        List.length_map, respBVs_length, respBVs_length]
    obtain ⟨hc, hag⟩ := extendSt_agree hxl hsc
    simp only [xs, AgreeAt_append, List.length_append, stateVals_length hm, Nat.zero_add,
      List.length_map, respBVs_length] at hag
    have hS := stateVar_rel hm hag.1.1
    have rC := specInS_rel (resp_rel hag.1.2) hC
    rw [specIn_respBVs] at rC
    have rD := specInS_rel (resp_rel hag.2) hD
    rw [specIn_respBVs] at rD
    have hsc' := specStep_norm hAS hside hs chc r
    have hsd' := specStep_norm hAS hside hs chd q
    have f1 := specEvalS_sound hrS hAS hside hc h1 e2 hs.1 hS (specIn0S_rel _ sp.specInW chc)
    have f2 := specEvalS_sound hrS hAS hside hc h2 e3 hs.1 hS (specIn0S_rel _ sp.specInW chd)
    have f3 := specEvalS_sound hrS hAS hside hc h3 e4 hs.1 hS rC
    have f4 := specEvalS_sound hrS hAS hside hc h4 e5 hsc'.1 f3.1 (specIn0S_rel _ sp.specInW chd)
    have f5 := specEvalS_sound hrS hAS hside hc h5 e6 hs.1 hS rD
    have f6 := specEvalS_sound hrS hAS hside hc h6 e7 hsc'.1 f3.1 rD
    have f7 := specEvalS_sound hrS hAS hside hc h7 (.refl _) hsd'.1 f5.1 rC
    exact ⟨_, hc, f1.2, f2.2, f4.2, f6.1, f7.1⟩
  · cases h

end

theorem mem_moore {sp : RefMapSpec} {c : Nat} (h : c < sp.chans.length) : STarget.moore c ∈ targetsSF sp := by
  simp only [targetsSF, List.mem_append, List.mem_map, List.mem_range]
  exact Or.inl ⟨c, h, rfl⟩

theorem mem_pair {sp : RefMapSpec} {c d : Nat} (hc : c < sp.chans.length) (hd : d < sp.chans.length)
    (hne : c ≠ d) : STarget.persist c d ∈ targetsSF sp ∧ STarget.outC c d ∈ targetsSF sp
      ∧ (c < d → STarget.comm c d ∈ targetsSF sp) := by
  have hp : (c, d) ∈ chanPairs sp := by
    simp only [chanPairs, List.mem_flatMap, List.mem_range, List.mem_filterMap]
    exact ⟨c, hc, d, hd, by simp [hne]⟩
  refine ⟨?_, ?_, fun hlt => ?_⟩ <;>
  · simp only [targetsSF, List.mem_append, List.mem_flatMap]
    exact Or.inr ⟨(c, d), hp, by simp [*]⟩

theorem bad_false_of {σ : Asg} {st : SymSt} (hc : st.Consistent σ) {bad φ : BVLogicalExpr}
    (h : coi st bad = φ) (hU : φ.Unsat) : bad.eval σ = false := by
  cases hb : bad.eval σ
  · rfl
  · have := eval_coi hc hb
    rw [h, hU] at this; cases this

section
variable {DS : DesignCert} {RS : Program} {sp : RefMapSpec}

/-- **The spec facts check is sound**: Moore, and the channel spec's confluence. -/
theorem runSpecFactsOf_sound {pfs : List ByteArray} (hrS : MemResidual DS RS) (hAS : admitsWB DS = true)
    (h : runSpecFactsOf DS RS sp pfs = true) : SpecFactsOK DS sp := by
  obtain ⟨hside, hmd, hall⟩ := sf_parts h
  -- the two channels' facts at a state, from a pair target's run
  have pairFacts : ∀ (c d : Nat) (t : STarget), t ∈ targetsSF sp → (t = .persist c d ∨ t = .outC c d ∨ t = .comm c d) →
      ∀ chc chd, sp.chans[c]? = some chc → sp.chans[d]? = some chd → True := fun _ _ _ _ _ _ _ _ _ => trivial
  clear pairFacts
  refine ⟨?_, ⟨?_, ?_, ?_, ?_⟩⟩
  -- Moore
  · intro σC c ch rs hσ hch
    have hclt := (List.getElem?_eq_some_iff.mp hch).1
    obtain ⟨φ, hφ, hU⟩ := hall _ (mem_moore hclt)
    simp only [sfObl, hch, Option.bind_eq_bind, Option.bind_some] at hφ
    split at hφ
    · rename_i oA oZ st hrun
      split at hφ
      · rename_i hsc
        simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
          Option.some.injEq] at hφ
        obtain ⟨alA, halA, alZ, halZ, os, hos, hφ⟩ := hφ
        obtain ⟨σ, hc, rA, rZ⟩ := mooreRun_facts hrS hAS hside hmd hrun hsc hσ rs
        have hb := bad_false_of hc hφ hU
        simp only [eval_not, Bool.not_eq_false'] at hb
        rw [eval_conjE] at hb
        simp only [List.all_cons, Bool.and_eq_true] at hb
        rw [← eval_conjE] at hb
        obtain ⟨hbeq, hobs⟩ := hb
        simp only [BVLogicalExpr.eval, BoolExpr.eval, Gate.eval, beq_iff_eq] at hbeq
        refine ⟨?_, obsEqLits_sound rA rZ ch.obsOut os hos hobs⟩
        rw [← propOf_sound rA halA, ← propOf_sound rZ halZ]
        exact hbeq
      · cases hφ
    · cases hφ
  -- pres
  · intro σ c r hσ hen
    obtain ⟨ch, hch⟩ := specOf_some hen
    simp only [specOf, hch]
    exact specStep_norm hAS hside hσ ch r
  -- persist
  · intro σ c d r hσ hne henc hend
    obtain ⟨chc, hchc⟩ := specOf_some henc
    obtain ⟨chd, hchd⟩ := specOf_some hend
    have hcl := (List.getElem?_eq_some_iff.mp hchc).1
    have hdl := (List.getElem?_eq_some_iff.mp hchd).1
    obtain ⟨φ, hφ, hU⟩ := hall _ (mem_pair hcl hdl hne).1
    simp only [sfObl, hchc, hchd, Option.bind_eq_bind, Option.bind_some] at hφ
    split at hφ
    · rename_i R hrun
      split at hφ
      · rename_i hsc
        simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
          Option.some.injEq] at hφ
        obtain ⟨enC, henC, enD, henD, enCD, henCD, hφ⟩ := hφ
        obtain ⟨σa, hc, r1, r2, r3, -, -⟩ := pairRun_facts hrS hAS hside hmd hrun hsc hσ r []
        have hb := bad_false_of hc hφ hU
        simp only [specOf, hchc, hchd] at henc hend ⊢
        simp only [conjE, eval_and, eval_not, propOf_sound r1 henC, propOf_sound r2 henD,
          propOf_sound r3 henCD, henc, hend, Bool.true_and, Bool.and_true] at hb
        simpa using hb
      · cases hφ
    · cases hφ
  -- outC
  · intro σ c d r hσ hne henc hend
    obtain ⟨chc, hchc⟩ := specOf_some henc
    obtain ⟨chd, hchd⟩ := specOf_some hend
    have hcl := (List.getElem?_eq_some_iff.mp hchc).1
    have hdl := (List.getElem?_eq_some_iff.mp hchd).1
    obtain ⟨φ, hφ, hU⟩ := hall _ (mem_pair hcl hdl hne).2.1
    simp only [sfObl, hchc, hchd, Option.bind_eq_bind, Option.bind_some] at hφ
    split at hφ
    · rename_i R hrun
      split at hφ
      · rename_i hsc
        simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
          Option.some.injEq] at hφ
        obtain ⟨enC, henC, enD, henD, os, hos, hφ⟩ := hφ
        obtain ⟨σa, hc, r1, r2, r3, -, -⟩ := pairRun_facts hrS hAS hside hmd hrun hsc hσ r []
        have hb := bad_false_of hc hφ hU
        simp only [specOf, hchc, hchd] at henc hend ⊢
        simp only [conjE, eval_and, eval_not, propOf_sound r1 henC, propOf_sound r2 henD,
          henc, hend, Bool.true_and, Bool.and_true, Bool.not_eq_false'] at hb
        exact obsEqLits_sound r3 r2 chd.obsOut os hos (by simpa [BVLogicalExpr.eval, BoolExpr.eval] using hb)
      · cases hφ
    · cases hφ
  -- comm
  · intro σ c d r q hσ hne henc hend
    obtain ⟨chc, hchc⟩ := specOf_some henc
    obtain ⟨chd, hchd⟩ := specOf_some hend
    have hcl := (List.getElem?_eq_some_iff.mp hchc).1
    have hdl := (List.getElem?_eq_some_iff.mp hchd).1
    simp only [specOf, hchc, hchd] at henc hend ⊢
    -- the pair in increasing order
    have key : ∀ (a b : Nat) (cha chb : ChanS) (ra rb : List Int), a < b → sp.chans[a]? = some cha →
        sp.chans[b]? = some chb →
        propBit (specOuts DS sp σ cha []) cha.allow = true →
        propBit (specOuts DS sp σ chb []) chb.allow = true →
        specStep DS sp (specStep DS sp σ cha ra) chb rb = specStep DS sp (specStep DS sp σ chb rb) cha ra := by
      intro a b cha chb ra rb hab ha hb' hena henb
      have hal := (List.getElem?_eq_some_iff.mp ha).1
      have hbl := (List.getElem?_eq_some_iff.mp hb').1
      obtain ⟨φ, hφ, hU⟩ := hall _ ((mem_pair hal hbl (by omega)).2.2 hab)
      simp only [sfObl, ha, hb', Option.bind_eq_bind, Option.bind_some] at hφ
      split at hφ
      · rename_i R hrun
        split at hφ
        · rename_i hsc
          simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
            Option.some.injEq] at hφ
          obtain ⟨enC, henC, enD, henD, eqs, heqs, hφ⟩ := hφ
          obtain ⟨σa, hc, r1, r2, -, r5, r6⟩ := pairRun_facts hrS hAS hside hmd hrun hsc hσ ra rb
          have hb := bad_false_of hc hφ hU
          simp only [conjE, eval_and, eval_not, propOf_sound r1 henC, propOf_sound r2 henD,
            hena, henb, Bool.true_and, Bool.and_true, Bool.not_eq_false'] at hb
          apply specNorm_ext (specStep_norm hAS hside (specStep_norm hAS hside hσ cha ra) chb rb)
            (specStep_norm hAS hside (specStep_norm hAS hside hσ chb rb) cha ra)
          exact allFlopsLit_sound r5 r6 heqs (by simpa [BVLogicalExpr.eval, BoolExpr.eval] using hb)
        · cases hφ
      · cases hφ
    rcases Nat.lt_or_ge c d with hlt | hge
    · exact key c d chc chd r q hlt hchc hchd henc hend
    · exact (key d c chd chc q r (by omega) hchd hchc hend henc).symm

end

end HDLProof.Check

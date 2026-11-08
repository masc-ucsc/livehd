import HDLProof.Sym.Native
import HDLProof.Sym.NativeInterp
import LeanSemanticPrimitives.Compiler.CompileDesignDefs

/-! **The IR and VC symbolic routes are proof routes.**
`irStepper_sound : MemResidual D (irStepper D)` and
`vcStepper_sound : compileDesign D = .ok R → MemResidual D (vcStepper R)`: on the admitted
fragment, a successful symbolic step denotes the interpreter's result `resM`, under every
consistent assignment. With them the checkers' `*_sound` theorems (KInd, ConjT, RefMapSound)
hold for the two routes exactly as for fp. The VC proof uses neither `compileDesign_correct`
nor Mathlib: each compiled binding evaluates as its node (`evalExpr_compileOp`). -/
open Projection Projection.Hw Projection.HwMem Projection.Surface Compiler Std.Tactic.BVDecide
namespace HDLProof.Sym.Native

/-! ## Decoding the step's arguments -/

theorem edges_rel {σ : Asg} {sE : SVal} {e edges : ClockEdges}
    (hr : SRel σ sE (encEdges e)) (h : edgesOf sE = some edges) : edges = e := by
  cases sE with
  | conc v =>
      have hv : v = encEdges e := by cases hr; rfl
      subst hv
      simp only [edgesOf] at h
      have h2 : decEdges (encEdges e) = some e := EdgesRel_encEdges e
      rw [h2] at h
      exact (Option.some.inj h).symm
  | _ => simp [edgesOf] at h

theorem state_rel {σ : Asg} {sS oF oM : SVal} {st : StateM}
    (hr : SRel σ sS (StateM.encStateM st)) (h : stateParts sS = some (oF, oM)) :
    SRel σ oF (encBVs st.flops) ∧ SRel σ oM (encArr MemImg.encMemImg st.mems) := by
  unfold stateParts at h
  split at h
  · rename_i tag a b
    split at h
    · simp only [Option.some.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      simp only [StateM.encStateM] at hr
      cases hr with
      | ctor hl =>
          obtain ⟨_, _, he1, h1, hl'⟩ := SRelL_cons_inv hl
          obtain ⟨_, _, he2, h2, _⟩ := SRelL_cons_inv hl'
          simp only [List.cons.injEq] at he1 he2
          obtain ⟨rfl, rfl⟩ := he1
          obtain ⟨rfl, -⟩ := he2
          exact ⟨h1, h2⟩
    · cases h
  · rename_i tag a b
    split at h
    · simp only [Option.some.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      have hv : Val.ctor tag [a, b] = StateM.encStateM st := by cases hr; rfl
      simp only [StateM.encStateM, Val.ctor.injEq, List.cons.injEq] at hv
      obtain ⟨-, rfl, rfl, -⟩ := hv
      exact ⟨.conc, .conc⟩
    · cases h
  · cases h


/-! ## The walk -/

theorem walk_sound {α : Type} {eval : Array SVal → α → Option SVal} {σ : Asg} {stF : SymSt}
    (hc : stF.Consistent σ) (V : Nat → Val) :
    ∀ (nodes : List α) (env : Array SVal) (st : SymSt) (out : Array SVal) (st' : SymSt),
      walk eval nodes env st = .ok (out, st') → st'.Extends stF →
      (∀ k (h : k < env.size), SRel σ env[k] (V k)) →
      (∀ (m : Nat) (h : m < nodes.length) (env' : Array SVal) (r : SVal),
          env'.size = env.size + m → (∀ k (h : k < env'.size), SRel σ env'[k] (V k)) →
          eval env' nodes[m] = some r → SRel σ r (V env'.size)) →
      out.size = env.size + nodes.length ∧ ∀ k (h : k < out.size), SRel σ out[k] (V k)
  | [], env, st, out, st', h, _, henv, _ => by
      simp only [walk, Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      exact ⟨by simp, henv⟩
  | n :: rest, env, st, out, st', h, hx, henv, hnode => by
      unfold walk at h
      split at h
      · cases h
      · rename_i value hv
        have hr := hnode 0 (by simp) env value (by simp) henv hv
        have hx1 : (cutS st value).2.Extends stF :=
          (walk_extends eval rest _ _ _ _ h).trans hx
        obtain ⟨hcut, -⟩ := cutS_sound hc st value hr hx1
        have henv' : ∀ k (h : k < (env.push (cutS st value).1).size),
            SRel σ (env.push (cutS st value).1)[k] (V k) := by
          intro k hk
          rw [Array.getElem_push]
          split
          · exact henv k ‹_›
          · have : k = env.size := by simp at hk; omega
            subst this; exact hcut
        obtain ⟨hsz, hall⟩ := walk_sound hc V rest _ _ out st' h hx henv'
          (fun m hm env' r hs h1 h2 => by
            have := hnode (m + 1) (by simp; omega) env' r (by simp at hs; omega) h1
            simpa using this h2)
        exact ⟨by simp at hsz; simp; omega, hall⟩


/-! ## One node -/

/-- The reference value of slot `k`: the interpreter's slot, encoded. -/
def V (ss : List SlotV) (k : Nat) : Val := (ss[k]?.map SlotV.enc).getD .nil

theorem V_eq {ss : List SlotV} {k : Nat} (h : k < ss.length) : V ss k = (ss[k]'h).enc := by
  simp [V, List.getElem?_eq_getElem h]

/-- Gathering scalar operands: each relates to its reference bit vector. -/
theorem gather_scalar {σ : Asg} {env : Array SVal} {ss : List SlotV} {f : Nat → BV}
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k)) :
    ∀ (ds : List Nat) (args : List SVal), (∀ d ∈ ds, d < env.size → V ss d = ofBV (f d)) →
      ds.mapM (env[·]?) = some args → SRelL σ args ((ds.map f).map ofBV)
  | [], args, _, h => by simp at h; subst h; exact .nil
  | d :: ds, args, hsc, h => by
      simp only [List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨a, ha, rest, hrest, rfl⟩ := h
      have hd : d < env.size := (Array.getElem?_eq_some_iff.mp ha).1
      have hae : env[d] = a := (Array.getElem?_eq_some_iff.mp ha).2
      have h1 := henv d hd
      rw [hae, hsc d (by simp) hd] at h1
      exact .cons h1 (gather_scalar henv ds rest (fun d' hm => hsc d' (by simp [hm])) hrest)

theorem node_scalar {σ : Asg} {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {ss : List SlotV}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hss : PrefixRelM D i st G (D.sources.size + D.nodes.size) ss)
    (hok : NodesOK D i st G 0 ss) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (hsup : OpSupported c.op = true) {env : Array SVal} (hsz : env.size = D.sources.size + j)
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k)) {r : SVal}
    (h : irNode env c = some r) : SRel σ r (V ss (D.sources.size + j)) := by
  have hj : j < D.nodes.size := (Array.getElem?_eq_some_iff.mp hc).1
  obtain ⟨hlt, hden, hkind, -, -⟩ := hok j c (Nat.zero_le _) hc
  simp only [irNode, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨args, hargs, hev⟩ := h
  -- every dependency is a scalar slot
  have hsc : ∀ d, d < env.size → d ∈ c.deps.toList →
      V ss d = ofBV (slotValM D i st d).asBV := by
    intro d hd hmem
    obtain ⟨p, hp⟩ := dep_at_position hmem
    have hk := dep_kind_of_semwf hsem hc hp
    have hdm : Direct.depIsMem c.op p = false := by
      cases hop : c.op <;> simp [hop, OpSupported] at hsup <;> simp [Direct.depIsMem]
    rw [hdm] at hk
    have hdl : d < ss.length := by rw [hss.len]; omega
    rw [V_eq hdl]
    have := dep_scalar hss hdl hk
    simpa [List.getElem_map, encBV] using this
  have hrel := gather_scalar (f := fun d => (slotValM D i st d).asBV) henv c.deps.toList args
    (fun d hm hd => hsc d hd hm) hargs
  have hr := evalOp_sound hsup hrel hev
  rw [V_eq hlt]
  have hk : (ss[D.sources.size + j]'hlt).isMem = false := by
    have h1 := nodeSlot_slotIsMem hc
    rw [opKind_isMem_of_supported hsup] at h1
    rw [h1] at hkind
    exact (Option.some.inj hkind).symm
  have hv := SlotV.bv_of_not_isMem hk
  rw [hv, SlotV.enc]
  have hrec := nodeValM_rec (i := i) (st := st) hwf hj hc
  rw [evalOpCert_of_supported hsup, List.map_map] at hrec
  have hslot : D.slotOfNode j = D.sources.size + j := rfl
  rw [hslot] at hrec
  rw [hden, ← hrec]
  simpa [CertVal.asBV, encBV, Function.comp_def] using hr


/-! ## Memory nodes: the interpreter's exact value, by determinism -/

theorem node_enc_eq {D : DesignCert} {i : RuntimeInput} {st : StateM} {G : Nat → Option Nat}
    {j : Nat} {c : DenseNodeCert} {pre : List SlotV} {v : SlotV}
    (hstep : NodeStepOK D i st G j c pre v) {X : Val}
    (hX : ∀ (σ : SEnv) (end_ eenv en : SExp), SEval hwMemS σ end_ (encNode c) →
      SEval hwMemS σ eenv (objEnv (pre.map SlotV.enc)) →
      SEval hwMemS σ en (.int (Int.ofNat (pre.map SlotV.enc).length)) →
      SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en]) X) : v.enc = X :=
  SEval_det (hstep.2.2.2 [] _ _ _ .lit .lit (by simpa using (SEval.lit (P := hwMemS) (σ := []))))
    (hX [] _ _ _ .lit .lit .lit)

theorem take_enc_get {ss : List SlotV} {n d : Nat} (hd : d < n) (hn : n ≤ ss.length) :
    ((ss.take n).map SlotV.enc)[d]'(by simp; omega) = (ss.map SlotV.enc)[d]'(by simp; omega) := by
  simp [List.getElem_take]

theorem get_of_some {env : Array SVal} {d : Nat} {x : SVal} (h : env[d]? = some x) :
    ∃ hd : d < env.size, env[d] = x :=
  ⟨(Array.getElem?_eq_some_iff.mp h).1, (Array.getElem?_eq_some_iff.mp h).2⟩

/-- Four gathered operands. -/
theorem gather4 {env : Array SVal} {d0 d1 d2 d3 : Nat} {args : List SVal}
    (h : [d0, d1, d2, d3].mapM (env[·]?) = some args) :
    ∃ (h0 : d0 < env.size) (h1 : d1 < env.size) (h2 : d2 < env.size) (h3 : d3 < env.size),
      args = [env[d0], env[d1], env[d2], env[d3]] := by
  cases e0 : env[d0]? <;> cases e1 : env[d1]? <;> cases e2 : env[d2]? <;> cases e3 : env[d3]? <;>
    simp [List.mapM_cons, e0, e1, e2, e3] at h
  obtain ⟨b0, rfl⟩ := get_of_some e0
  obtain ⟨b1, rfl⟩ := get_of_some e1
  obtain ⟨b2, rfl⟩ := get_of_some e2
  obtain ⟨b3, rfl⟩ := get_of_some e3
  exact ⟨b0, b1, b2, b3, h.symm⟩

theorem gather3 {env : Array SVal} {d0 d1 d2 : Nat} {args : List SVal}
    (h : [d0, d1, d2].mapM (env[·]?) = some args) :
    ∃ (h0 : d0 < env.size) (h1 : d1 < env.size) (h2 : d2 < env.size),
      args = [env[d0], env[d1], env[d2]] := by
  cases e0 : env[d0]? <;> cases e1 : env[d1]? <;> cases e2 : env[d2]? <;>
    simp [List.mapM_cons, e0, e1, e2] at h
  obtain ⟨b0, rfl⟩ := get_of_some e0
  obtain ⟨b1, rfl⟩ := get_of_some e1
  obtain ⟨b2, rfl⟩ := get_of_some e2
  exact ⟨b0, b1, b2, h.symm⟩

theorem gather1 {env : Array SVal} {d0 : Nat} {args : List SVal}
    (h : [d0].mapM (env[·]?) = some args) :
    ∃ (h0 : d0 < env.size), args = [env[d0]] := by
  cases e0 : env[d0]? <;> simp [List.mapM_cons, e0] at h
  obtain ⟨b0, rfl⟩ := get_of_some e0
  exact ⟨b0, h.symm⟩

theorem node_memWriteBE {σ : Asg} {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {ss : List SlotV}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hss : PrefixRelM D i st G (D.sources.size + D.nodes.size) ss)
    (hok : NodesOK D i st G 0 ss) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    {bw : Nat} (hop : c.op = .Op_MemWriteBE bw) (hfits : WriteAddrFits D i st G c)
    {env : Array SVal} (hsz : env.size = D.sources.size + j)
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k)) {r : SVal}
    (h : irNode env c = some r) : SRel σ r (V ss (D.sources.size + j)) := by
  have hj : j < D.nodes.size := (Array.getElem?_eq_some_iff.mp hc).1
  obtain ⟨hlt, hstep⟩ := hok j c (Nat.zero_le _) hc
  obtain ⟨d0, d1, d2, d3, hdeps⟩ := memWriteBE_deps hsem hc hop
  simp only [irNode, Option.bind_eq_bind, Option.bind_eq_some_iff, hdeps, hop] at h
  obtain ⟨args, hargs, hev⟩ := h
  obtain ⟨hb0, hb1, hb2, hb3, rfl⟩ := gather4 hargs
  have hn : D.sources.size + j ≤ ss.length := by rw [hss.len]; omega
  have hp0 : c.deps[0]? = some d0 := by rw [deps_get c hdeps 0]; rfl
  have hp1 : c.deps[1]? = some d1 := by rw [deps_get c hdeps 1]; rfl
  have hp2 : c.deps[2]? = some d2 := by rw [deps_get c hdeps 2]; rfl
  have hp3 : c.deps[3]? = some d3 := by rw [deps_get c hdeps 3]; rfl
  have hk0 : Direct.slotIsMem D d0 = some true := by
    have := dep_kind_of_semwf hsem hc hp0; rwa [hop] at this
  have hk1 : Direct.slotIsMem D d1 = some false := by
    have := dep_kind_of_semwf hsem hc hp1; rwa [hop] at this
  have hk2 : Direct.slotIsMem D d2 = some false := by
    have := dep_kind_of_semwf hsem hc hp2; rwa [hop] at this
  have hk3 : Direct.slotIsMem D d3 = some false := by
    have := dep_kind_of_semwf hsem hc hp3; rwa [hop] at this
  have hl0 : d0 < ss.length := by omega
  have hl1 : d1 < ss.length := by omega
  have hl2 : d2 < ss.length := by omega
  have hl3 : d3 < ss.length := by omega
  obtain ⟨img, hslot0, henc0, hden0⟩ := dep_mem hss hl0 hk0
  have henc1 := dep_scalar hss hl1 hk1
  have henc2 := dep_scalar hss hl2 hk2
  have henc3 := dep_scalar hss hl3 hk3
  -- the symbolic operands
  have r0 : SRel σ env[d0] (MemImg.encMemImg img) := by
    have e : (ss[d0]'hl0).enc = MemImg.encMemImg img := by simpa using henc0
    have := henv d0 hb0; rwa [V_eq hl0, e] at this
  have r1 : SRel σ env[d1] (ofBV (slotValM D i st d1).asBV) := by
    have e : (ss[d1]'hl1).enc = ofBV (slotValM D i st d1).asBV := by simpa [encBV] using henc1
    have := henv d1 hb1; rwa [V_eq hl1, e] at this
  have r2 : SRel σ env[d2] (ofBV (slotValM D i st d2).asBV) := by
    have e : (ss[d2]'hl2).enc = ofBV (slotValM D i st d2).asBV := by simpa [encBV] using henc2
    have := henv d2 hb2; rwa [V_eq hl2, e] at this
  have r3 : SRel σ env[d3] (ofBV (slotValM D i st d3).asBV) := by
    have e : (ss[d3]'hl3).enc = ofBV (slotValM D i st d3).asBV := by simpa [encBV] using henc3
    have := henv d3 hb3; rwa [V_eq hl3, e] at this
  -- the range obligation, from the admitted geometry of the actual image
  have hr : bv_nonzero (slotValM D i st d3).asBV →
      MemImg.InRange img (bv_uint (slotValM D i st d1).asBV) := by
    intro hen
    obtain ⟨aw, hG0, hwid⟩ := hfits.1 bw hop d0 d1 d3 hp0 hp1 hp3 hen
    exact MemImg.InRange_of_FullTable_le (hss.geom d0 hl0 img hslot0 aw hG0) hwid
  have hres := memWriteBE_sound (w := c.width) r0 r1 r2 r3 hr hev
  rw [V_eq hlt]
  -- the interpreter's slot is that image
  have hvals : ∀ d (hd : d < D.sources.size + j),
      ((ss.take (D.sources.size + j)).map SlotV.enc)[d]'(by simp; omega)
        = (ss.map SlotV.enc)[d]'(by simp; omega) := fun d hd => take_enc_get hd hn
  have heq : (ss[D.sources.size + j]'hlt).enc =
      MemImg.encMemImg (MemImg.writeBE c.width img (slotValM D i st d1).asBV
        (slotValM D i st d2).asBV (slotValM D i st d3).asBV bw) := by
    apply node_enc_eq hstep
    intro σ' end_ eenv en hnd henv' hlen
    by_cases hen : bv_nonzero (slotValM D i st d3).asBV
    · exact evalNodeM_memWriteBE hop hdeps (by simp; omega) (by simp; omega) (by simp; omega)
        (by simp; omega) (by rw [hvals d0 (by omega), henc0]) (by rw [hvals d1 (by omega), henc1])
        (by rw [hvals d2 (by omega), henc2]) (by rw [hvals d3 (by omega), henc3]) hen (hr hen)
        hnd henv' hlen
    · rw [MemImg.writeBE_disabled_eq _ _ _ _ _ _ hen]
      exact evalNodeM_memWriteBE_disabled hop hdeps (by simp; omega) (by simp; omega)
        (by simp; omega) (by simp; omega) (by rw [hvals d0 (by omega), henc0])
        (by rw [hvals d1 (by omega), henc1]) (by rw [hvals d2 (by omega), henc2])
        (by rw [hvals d3 (by omega), henc3]) hen hnd henv' hlen
  rw [heq]; exact hres

theorem node_memWrite {σ : Asg} {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {ss : List SlotV}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hss : PrefixRelM D i st G (D.sources.size + D.nodes.size) ss)
    (hok : NodesOK D i st G 0 ss) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (hop : c.op = .Op_MemWrite) (hfits : WriteAddrFitsW D i st G c)
    {env : Array SVal} (hsz : env.size = D.sources.size + j)
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k)) {r : SVal}
    (h : irNode env c = some r) : SRel σ r (V ss (D.sources.size + j)) := by
  have hj : j < D.nodes.size := (Array.getElem?_eq_some_iff.mp hc).1
  obtain ⟨hlt, hstep⟩ := hok j c (Nat.zero_le _) hc
  obtain ⟨d0, d1, d2, d3, hdeps⟩ := memWrite_deps hsem hc hop
  simp only [irNode, Option.bind_eq_bind, Option.bind_eq_some_iff, hdeps, hop] at h
  obtain ⟨args, hargs, hev⟩ := h
  obtain ⟨hb0, hb1, hb2, hb3, rfl⟩ := gather4 hargs
  have hn : D.sources.size + j ≤ ss.length := by rw [hss.len]; omega
  have hp0 : c.deps[0]? = some d0 := by rw [deps_get c hdeps 0]; rfl
  have hp1 : c.deps[1]? = some d1 := by rw [deps_get c hdeps 1]; rfl
  have hp2 : c.deps[2]? = some d2 := by rw [deps_get c hdeps 2]; rfl
  have hp3 : c.deps[3]? = some d3 := by rw [deps_get c hdeps 3]; rfl
  have hk0 : Direct.slotIsMem D d0 = some true := by
    have := dep_kind_of_semwf hsem hc hp0; rwa [hop] at this
  have hk1 : Direct.slotIsMem D d1 = some false := by
    have := dep_kind_of_semwf hsem hc hp1; rwa [hop] at this
  have hk2 : Direct.slotIsMem D d2 = some false := by
    have := dep_kind_of_semwf hsem hc hp2; rwa [hop] at this
  have hk3 : Direct.slotIsMem D d3 = some false := by
    have := dep_kind_of_semwf hsem hc hp3; rwa [hop] at this
  have hl0 : d0 < ss.length := by omega
  have hl1 : d1 < ss.length := by omega
  have hl2 : d2 < ss.length := by omega
  have hl3 : d3 < ss.length := by omega
  obtain ⟨img, hslot0, henc0, hden0⟩ := dep_mem hss hl0 hk0
  have henc1 := dep_scalar hss hl1 hk1
  have henc2 := dep_scalar hss hl2 hk2
  have henc3 := dep_scalar hss hl3 hk3
  -- the symbolic operands
  have r0 : SRel σ env[d0] (MemImg.encMemImg img) := by
    have e : (ss[d0]'hl0).enc = MemImg.encMemImg img := by simpa using henc0
    have := henv d0 hb0; rwa [V_eq hl0, e] at this
  have r1 : SRel σ env[d1] (ofBV (slotValM D i st d1).asBV) := by
    have e : (ss[d1]'hl1).enc = ofBV (slotValM D i st d1).asBV := by simpa [encBV] using henc1
    have := henv d1 hb1; rwa [V_eq hl1, e] at this
  have r2 : SRel σ env[d2] (ofBV (slotValM D i st d2).asBV) := by
    have e : (ss[d2]'hl2).enc = ofBV (slotValM D i st d2).asBV := by simpa [encBV] using henc2
    have := henv d2 hb2; rwa [V_eq hl2, e] at this
  have r3 : SRel σ env[d3] (ofBV (slotValM D i st d3).asBV) := by
    have e : (ss[d3]'hl3).enc = ofBV (slotValM D i st d3).asBV := by simpa [encBV] using henc3
    have := henv d3 hb3; rwa [V_eq hl3, e] at this
  -- the range obligation, from the admitted geometry of the actual image
  have hr : bv_nonzero (slotValM D i st d3).asBV →
      MemImg.InRange img (bv_uint (slotValM D i st d1).asBV) := by
    intro hen
    obtain ⟨aw, hG0, hwid⟩ := hfits.2 hop d0 d1 d3 hp0 hp1 hp3 hen
    exact MemImg.InRange_of_FullTable_le (hss.geom d0 hl0 img hslot0 aw hG0) hwid
  have hres := memWrite_sound r0 r1 r2 r3 hr hev
  rw [V_eq hlt]
  -- the interpreter's slot is that image
  have hvals : ∀ d (hd : d < D.sources.size + j),
      ((ss.take (D.sources.size + j)).map SlotV.enc)[d]'(by simp; omega)
        = (ss.map SlotV.enc)[d]'(by simp; omega) := fun d hd => take_enc_get hd hn
  have heq : (ss[D.sources.size + j]'hlt).enc =
      MemImg.encMemImg (MemImg.writeEn img (slotValM D i st d1).asBV
        (slotValM D i st d2).asBV (slotValM D i st d3).asBV) := by
    apply node_enc_eq hstep
    intro σ' end_ eenv en hnd henv' hlen
    by_cases hen : bv_nonzero (slotValM D i st d3).asBV
    · exact evalNodeM_memWrite hop hdeps (by simp; omega) (by simp; omega) (by simp; omega)
        (by simp; omega) (by rw [hvals d0 (by omega), henc0]) (by rw [hvals d1 (by omega), henc1])
        (by rw [hvals d2 (by omega), henc2]) (by rw [hvals d3 (by omega), henc3]) hen (hr hen)
        hnd henv' hlen
    · rw [show MemImg.writeEn img (slotValM D i st d1).asBV (slotValM D i st d2).asBV
          (slotValM D i st d3).asBV = img by simp [MemImg.writeEn, hen]]
      exact evalNodeM_memWrite_disabled hop hdeps (by simp; omega) (by simp; omega)
        (by simp; omega) (by simp; omega) (by rw [hvals d0 (by omega), henc0])
        (by rw [hvals d1 (by omega), henc1]) (by rw [hvals d2 (by omega), henc2])
        (by rw [hvals d3 (by omega), henc3]) hen hnd henv' hlen
  rw [heq]; exact hres

theorem node_memLoad {σ : Asg} {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {ss : List SlotV}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hss : PrefixRelM D i st G (D.sources.size + D.nodes.size) ss)
    (hok : NodesOK D i st G 0 ss) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    {k : Nat} (hop : c.op = .Op_MemLoad k)
    {env : Array SVal} (hsz : env.size = D.sources.size + j)
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k)) {r : SVal}
    (h : irNode env c = some r) : SRel σ r (V ss (D.sources.size + j)) := by
  have hj : j < D.nodes.size := (Array.getElem?_eq_some_iff.mp hc).1
  obtain ⟨hlt, hstep⟩ := hok j c (Nat.zero_le _) hc
  obtain ⟨d0, d1, d2, hdeps⟩ := memLoad_deps hsem hc hop
  simp only [irNode, Option.bind_eq_bind, Option.bind_eq_some_iff, hdeps, hop] at h
  obtain ⟨args, hargs, hev⟩ := h
  obtain ⟨hb0, hb1, hb2, rfl⟩ := gather3 hargs
  have hn : D.sources.size + j ≤ ss.length := by rw [hss.len]; omega
  have hp0 : c.deps[0]? = some d0 := by rw [deps_get c hdeps 0]; rfl
  have hp1 : c.deps[1]? = some d1 := by rw [deps_get c hdeps 1]; rfl
  have hp2 : c.deps[2]? = some d2 := by rw [deps_get c hdeps 2]; rfl
  have hk0 : Direct.slotIsMem D d0 = some true := by
    have := dep_kind_of_semwf hsem hc hp0; rwa [hop] at this
  have hk1 : Direct.slotIsMem D d1 = some false := by
    have := dep_kind_of_semwf hsem hc hp1; rwa [hop] at this
  have hk2 : Direct.slotIsMem D d2 = some false := by
    have := dep_kind_of_semwf hsem hc hp2; rwa [hop] at this
  have hl0 : d0 < ss.length := by omega
  have hl1 : d1 < ss.length := by omega
  have hl2 : d2 < ss.length := by omega
  obtain ⟨img, hslot0, henc0, hden0⟩ := dep_mem hss hl0 hk0
  have henc1 := dep_scalar hss hl1 hk1
  have henc2 := dep_scalar hss hl2 hk2
  have r0 : SRel σ env[d0] (MemImg.encMemImg img) := by
    have e : (ss[d0]'hl0).enc = MemImg.encMemImg img := by simpa using henc0
    have := henv d0 hb0; rwa [V_eq hl0, e] at this
  have r1 : SRel σ env[d1] (ofBV (slotValM D i st d1).asBV) := by
    have e : (ss[d1]'hl1).enc = ofBV (slotValM D i st d1).asBV := by simpa [encBV] using henc1
    have := henv d1 hb1; rwa [V_eq hl1, e] at this
  have r2 : SRel σ env[d2] (ofBV (slotValM D i st d2).asBV) := by
    have e : (ss[d2]'hl2).enc = ofBV (slotValM D i st d2).asBV := by simpa [encBV] using henc2
    have := henv d2 hb2; rwa [V_eq hl2, e] at this
  have hres := memLoad_sound (w := c.width) (n := k) r0 r1 r2 hev
  rw [V_eq hlt]
  have hvals : ∀ d (hd : d < D.sources.size + j),
      ((ss.take (D.sources.size + j)).map SlotV.enc)[d]'(by simp; omega)
        = (ss.map SlotV.enc)[d]'(by simp; omega) := fun d hd => take_enc_get hd hn
  have heq : (ss[D.sources.size + j]'hlt).enc =
      MemImg.encMemImg (memLoadImg c.width k img (slotValM D i st d1).asBV
        (slotValM D i st d2).asBV) := by
    apply node_enc_eq hstep
    intro σ' end_ eenv en hnd henv' hlen
    exact evalNodeM_memLoad hop hdeps (by simp; omega) (by simp; omega) (by simp; omega)
      (by rw [hvals d0 (by omega), henc0]) (by rw [hvals d1 (by omega), henc1])
      (by rw [hvals d2 (by omega), henc2]) hnd henv' hlen
  rw [heq]; exact hres

theorem node_memRead {σ : Asg} {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {ss : List SlotV}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hss : PrefixRelM D i st G (D.sources.size + D.nodes.size) ss)
    (hok : NodesOK D i st G 0 ss) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (hop : c.op = .Op_MemRead)
    {env : Array SVal} (hsz : env.size = D.sources.size + j)
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k)) {r : SVal}
    (h : irNode env c = some r) : SRel σ r (V ss (D.sources.size + j)) := by
  have hj : j < D.nodes.size := (Array.getElem?_eq_some_iff.mp hc).1
  obtain ⟨hlt, hden, hkind, -, -⟩ := hok j c (Nat.zero_le _) hc
  obtain ⟨d0, d1, d2, hdeps⟩ := memRead_deps hsem hc hop
  simp only [irNode, Option.bind_eq_bind, Option.bind_eq_some_iff, hdeps, hop] at h
  obtain ⟨args, hargs, hev⟩ := h
  obtain ⟨hb0, hb1, hb2, rfl⟩ := gather3 hargs
  have hp0 : c.deps[0]? = some d0 := by rw [deps_get c hdeps 0]; rfl
  have hp1 : c.deps[1]? = some d1 := by rw [deps_get c hdeps 1]; rfl
  have hp2 : c.deps[2]? = some d2 := by rw [deps_get c hdeps 2]; rfl
  have hk0 : Direct.slotIsMem D d0 = some true := by
    have := dep_kind_of_semwf hsem hc hp0; rwa [hop] at this
  have hk1 : Direct.slotIsMem D d1 = some false := by
    have := dep_kind_of_semwf hsem hc hp1; rwa [hop] at this
  have hk2 : Direct.slotIsMem D d2 = some false := by
    have := dep_kind_of_semwf hsem hc hp2; rwa [hop] at this
  have hl0 : d0 < ss.length := by rw [hss.len]; omega
  have hl1 : d1 < ss.length := by rw [hss.len]; omega
  have hl2 : d2 < ss.length := by rw [hss.len]; omega
  obtain ⟨img, hslot0, henc0, hden0⟩ := dep_mem hss hl0 hk0
  have henc1 := dep_scalar hss hl1 hk1
  have henc2 := dep_scalar hss hl2 hk2
  have r0 : SRel σ env[d0] (MemImg.encMemImg img) := by
    have e : (ss[d0]'hl0).enc = MemImg.encMemImg img := by simpa using henc0
    have := henv d0 hb0; rwa [V_eq hl0, e] at this
  have r1 : SRel σ env[d1] (ofBV (slotValM D i st d1).asBV) := by
    have e : (ss[d1]'hl1).enc = ofBV (slotValM D i st d1).asBV := by simpa [encBV] using henc1
    have := henv d1 hb1; rwa [V_eq hl1, e] at this
  have r2 : SRel σ env[d2] (ofBV (slotValM D i st d2).asBV) := by
    have e : (ss[d2]'hl2).enc = ofBV (slotValM D i st d2).asBV := by simpa [encBV] using henc2
    have := henv d2 hb2; rwa [V_eq hl2, e] at this
  have hres := memRead_sound (w := c.width) r0 r1 r2 hev
  rw [V_eq hlt]
  have hk : (ss[D.sources.size + j]'hlt).isMem = false := by
    have h1 := nodeSlot_slotIsMem hc
    rw [hop, opKind_isMem_memRead] at h1
    rw [h1] at hkind
    exact (Option.some.inj hkind).symm
  rw [SlotV.bv_of_not_isMem hk, SlotV.enc, hden]
  have hrec := nodeValM_rec (i := i) (st := st) hwf hj hc
  rw [hop, hdeps] at hrec
  have hslot : D.slotOfNode j = D.sources.size + j := rfl
  rw [hslot] at hrec
  rw [← hrec, hden0] at *
  simpa [eval_op_cert, CertVal.asBV, encBV] using hres

theorem node_memToBus {σ : Asg} {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {ss : List SlotV}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hss : PrefixRelM D i st G (D.sources.size + D.nodes.size) ss)
    (hok : NodesOK D i st G 0 ss) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    {k : Nat} (hop : c.op = .Op_MemToBus k)
    {env : Array SVal} (hsz : env.size = D.sources.size + j)
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k)) {r : SVal}
    (h : irNode env c = some r) : SRel σ r (V ss (D.sources.size + j)) := by
  have hj : j < D.nodes.size := (Array.getElem?_eq_some_iff.mp hc).1
  obtain ⟨hlt, hden, hkind, -, -⟩ := hok j c (Nat.zero_le _) hc
  obtain ⟨d0, hdeps⟩ := memToBus_deps hsem hc hop
  simp only [irNode, Option.bind_eq_bind, Option.bind_eq_some_iff, hdeps, hop] at h
  obtain ⟨args, hargs, hev⟩ := h
  obtain ⟨hb0, rfl⟩ := gather1 hargs
  have hp0 : c.deps[0]? = some d0 := by rw [deps_get c hdeps 0]; rfl
  have hk0 : Direct.slotIsMem D d0 = some true := by
    have := dep_kind_of_semwf hsem hc hp0; rwa [hop] at this
  have hl0 : d0 < ss.length := by rw [hss.len]; omega
  obtain ⟨img, hslot0, henc0, hden0⟩ := dep_mem hss hl0 hk0
  have r0 : SRel σ env[d0] (MemImg.encMemImg img) := by
    have e : (ss[d0]'hl0).enc = MemImg.encMemImg img := by simpa using henc0
    have := henv d0 hb0; rwa [V_eq hl0, e] at this
  have hres := memToBus_sound (w := c.width) (n := k) r0 hev
  rw [memToBusBV_eq] at hres
  rw [V_eq hlt]
  have hk : (ss[D.sources.size + j]'hlt).isMem = false := by
    have h1 := nodeSlot_slotIsMem hc
    rw [hop, opKind_isMem_memToBus] at h1
    rw [h1] at hkind
    exact (Option.some.inj hkind).symm
  rw [SlotV.bv_of_not_isMem hk, SlotV.enc, hden]
  have hrec := nodeValM_rec (i := i) (st := st) hwf hj hc
  rw [hop, hdeps] at hrec
  have hslot : D.slotOfNode j = D.sources.size + j := rfl
  rw [hslot] at hrec
  rw [← hrec, hden0] at *
  simpa [eval_op_cert, CertVal.asBV, encBV] using hres

theorem node_sound {σ : Asg} {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {ss : List SlotV}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hss : PrefixRelM D i st G (D.sources.size + D.nodes.size) ss)
    (hok : NodesOK D i st G 0 ss) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (hadm : NodeAdmittedW c) (hfits : WriteAddrFitsW D i st G c)
    {env : Array SVal} (hsz : env.size = D.sources.size + j)
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k)) {r : SVal}
    (h : irNode env c = some r) : SRel σ r (V ss (D.sources.size + j)) := by
  rcases hadm with hadm | hop
  · rcases hadm with hsup | hop | ⟨bw, hop⟩ | ⟨k, hop⟩ | ⟨k, hop⟩
    · exact node_scalar hwf hsem hss hok hc hsup hsz henv h
    · exact node_memRead hwf hsem hss hok hc hop hsz henv h
    · exact node_memWriteBE hwf hsem hss hok hc hop hfits.1 hsz henv h
    · exact node_memLoad hwf hsem hss hok hc hop hsz henv h
    · exact node_memToBus hwf hsem hss hok hc hop hsz henv h
  · exact node_memWrite hwf hsem hss hok hc hop hfits hsz henv h


/-! ## Flops -/

theorem SRel_bv_congr {σ : Asg} {w : Nat} {e : BVExpr w} {a b : BV}
    (h : SRel σ (.bv w e) (ofBV a)) (hw : b.width = a.width) (hu : bv_uint b = bv_uint a) :
    SRel σ (.bv w e) (ofBV b) := by
  cases h with
  | bv ha hwa hua =>
      rw [asBV_ofBV] at ha; cases ha
      exact .bv (asBV_ofBV b) (by rw [hw]; exact hwa) (by rw [hu]; exact hua)

theorem uint_resize_same {x : BV} {w : Nat} (hw : x.width = w) : bv_uint (bv_resize w x) = bv_uint x := by
  unfold bv_resize; rw [bv_uint_mk_bv]; unfold bv_uint; rw [hw, Int.emod_emod_of_dvd _ (Int.dvd_refl _)]

theorem select_bv {w : Nat} {c a b r : SVal} (h : select w c a b = some r) : ∃ e, r = .bv w e := by
  simp only [select, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨_, _, _, _, _, _, rfl⟩ := h
  exact ⟨_, rfl⟩

/-- A related `select` relates to the reference choice when both arms have width `w`. -/
theorem select_ref {σ : Asg} {w : Nat} {c a b r : SVal} {sel x y : BV}
    (hc : SRel σ c (ofBV sel)) (ha : SRel σ a (ofBV x)) (hb : SRel σ b (ofBV y))
    (hx : x.width = w) (hy : y.width = w) (h : select w c a b = some r) :
    SRel σ r (ofBV (if bv_nonzero sel then x else y)) := by
  have hs := select_sound hc ha hb h
  obtain ⟨e, rfl⟩ := select_bv h
  refine SRel_bv_congr hs ?_ ?_
  · split <;> simp [hx, hy, bv_resize, mk_bv]
  · split
    · rw [uint_resize_same hx]
    · rw [uint_resize_same hy]

theorem select_ref' {σ : Asg} {w : Nat} {c a b r : SVal} {sel x y x' y' : BV}
    (hc : SRel σ c (ofBV sel)) (ha : SRel σ a (ofBV x)) (hb : SRel σ b (ofBV y))
    (hx : x'.width = w) (hux : bv_uint x' = bv_uint (bv_resize w x))
    (hy : y'.width = w) (huy : bv_uint y' = bv_uint (bv_resize w y))
    (h : select w c a b = some r) :
    SRel σ r (ofBV (if bv_nonzero sel then x' else y')) := by
  have hs := select_sound hc ha hb h
  obtain ⟨e, rfl⟩ := select_bv h
  refine SRel_bv_congr hs ?_ ?_
  · split <;> simp [hx, hy, bv_resize, mk_bv]
  · split
    · exact hux
    · exact huy

theorem ite_width {c : Prop} [Decidable c] {a b : BV} {w : Nat} (ha : a.width = w)
    (hb : b.width = w) : (if c then a else b).width = w := by
  split <;> assumption

/-- `srcFlopNext` before its reset. -/
def flopValRef (rho : Nat → CertVal) (e : ClockEdges) (x : BV) (f : FlopDesc) : BV :=
  if fires e f.clock && (match f.enable with
      | none => true
      | some en => bv_nonzero (rho en).asBV)
  then bv_resize f.width (rho f.din).asBV else x

def resetActiveRef (rho : Nat → CertVal) (f : FlopDesc) : Bool :=
  match f.resetPin with
  | none => false
  | some r => xor f.resetActiveLow (bv_nonzero (rho r).asBV)

theorem srcFlopNext_split (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (f : FlopDesc) :
    srcFlopNext rho e s idx f =
      if resetActiveRef rho f && (fires e f.clock || f.asyncReset) then mk_bv f.width f.resetValue
      else flopValRef rho e (s.flops[idx]?.getD (mk_bv f.width 0)) f := rfl

theorem flopValue_ref {σ : Asg} {env : Array SVal} {rho : Nat → CertVal} {edges : ClockEdges}
    {held value : SVal} {x : BV} {f : FlopDesc}
    (pin : ∀ k, (k = f.din ∨ f.enable = some k) →
      ∀ y, env[k]? = some y → SRel σ y (ofBV (rho k).asBV))
    (rx : SRel σ held (ofBV x)) (hxw : x.width = f.width)
    (h : flopValue env edges held f = some value) :
    SRel σ value (ofBV (flopValRef rho edges x f)) := by
  unfold flopValue at h
  unfold flopValRef
  by_cases hf : fires edges f.clock
  · rw [if_pos hf] at h
    cases hd : env[f.din]? with
    | none => simp [hd] at h
    | some din =>
        have rd := pin f.din (Or.inl rfl) din hd
        cases hen : f.enable with
        | none =>
            simp only [hd, hen] at h
            simpa [hf] using resize_sound rd h
        | some e =>
            simp only [hd, hen] at h
            cases hc : env[e]? with
            | none => simp [hc] at h
            | some cv =>
                simp only [hc] at h
                have re := pin e (Or.inr hen) cv hc
                simpa [hf] using select_ref' (x' := bv_resize f.width (rho f.din).asBV) (y' := x) re rd rx rfl rfl hxw (uint_resize_same hxw).symm h
  · rw [if_neg hf] at h
    simp only [Option.some.injEq] at h
    subst h
    simpa [hf] using rx

theorem flopReset_ref {σ : Asg} {env : Array SVal} {rho : Nat → CertVal} {edges : ClockEdges}
    {value r : SVal} {v : BV} {f : FlopDesc}
    (pin : ∀ k, f.resetPin = some k → ∀ y, env[k]? = some y → SRel σ y (ofBV (rho k).asBV))
    (rv : SRel σ value (ofBV v)) (hvw : v.width = f.width)
    (h : flopReset env edges value f = some r) :
    SRel σ r (ofBV (if resetActiveRef rho f && (fires edges f.clock || f.asyncReset)
      then mk_bv f.width f.resetValue else v)) := by
  unfold flopReset at h
  unfold resetActiveRef
  have hmk : (mk_bv f.width f.resetValue).width = f.width := rfl
  cases hrp : f.resetPin with
  | none =>
      simp only [hrp, Option.some.injEq] at h
      subst h; simpa using rv
  | some rp =>
      simp only [hrp] at h
      by_cases hfa : (fires edges f.clock || f.asyncReset) = true
      · rw [if_pos hfa] at h
        cases hc : env[rp]? with
        | none => simp [hc] at h
        | some cv =>
            simp only [hc] at h
            have rr := pin rp hrp cv hc
            cases hal : f.resetActiveLow
            · simp only [hal, Bool.false_eq_true, if_false] at h
              have := select_ref rr (constVal_sound σ f.width f.resetValue) rv hmk hvw h
              cases hnz : bv_nonzero (rho rp).asBV <;> simp_all
            · simp only [hal, if_true] at h
              have := select_ref rr rv (constVal_sound σ f.width f.resetValue) hvw hmk h
              cases hnz : bv_nonzero (rho rp).asBV <;> simp_all
      · rw [if_neg hfa] at h
        simp only [Option.some.injEq] at h
        subst h
        have : (fires edges f.clock || f.asyncReset) = false := by simpa using hfa
        simpa [this] using rv

theorem flopNext_sound {σ : Asg} {env : Array SVal} {ss : List SlotV} {D : DesignCert}
    {i : RuntimeInput} {st : StateM} {edges : ClockEdges} {oF r : SVal} {idx : Nat} {f : FlopDesc}
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k))
    (hpin : ∀ k, (k = f.din ∨ f.enable = some k ∨ f.resetPin = some k) →
      k < env.size ∧ V ss k = ofBV (slotValM D i st k).asBV)
    (hold : SRel σ oF (encBVs st.flops)) (hshape : ∃ b, st.flops[idx]? = some b ∧ b.width = f.width)
    (h : flopNext env edges oF idx f = some r) :
    SRel σ r (ofBV (srcFlopNext (slotValM D i st) edges (StateM.denote st) idx f)) := by
  have pin : ∀ k, (k = f.din ∨ f.enable = some k ∨ f.resetPin = some k) →
      ∀ x, env[k]? = some x → SRel σ x (ofBV (slotValM D i st k).asBV) := by
    intro k hk x hx
    obtain ⟨hlt, hv⟩ := hpin k hk
    have := henv k hlt
    rw [hv, (Array.getElem?_eq_some_iff.mp hx).2] at this
    exact this
  obtain ⟨hb, hbget, hbw⟩ := hshape
  simp only [flopNext, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨held, hheld, value, hvalue, h⟩ := h
  obtain ⟨x, hx, rx⟩ := sNth_sound oF st.flops.toList idx held hold hheld
  have hxb : x = hb := by
    have : st.flops[idx]? = some x := by simpa using hx
    rw [hbget] at this; exact (Option.some.inj this).symm
  subst hxb
  have hheldref : (StateM.denote st).flops[idx]?.getD (mk_bv f.width 0) = x := by
    simp [StateM.denote, hbget]
  have rv := flopValue_ref (rho := slotValM D i st)
    (fun k hk => pin k (hk.elim Or.inl (fun h => Or.inr (Or.inl h)))) rx hbw hvalue
  have hvw : (flopValRef (slotValM D i st) edges x f).width = f.width := by
    unfold flopValRef; exact ite_width rfl hbw
  have := flopReset_ref (rho := slotValM D i st) (fun k hk => pin k (Or.inr (Or.inr hk))) rv hvw h
  rw [srcFlopNext_split, hheldref]
  exact this


/-! ## Memories -/

theorem memoryNext_sound {σ : Asg} {env : Array SVal} {ss : List SlotV} {st : StateM}
    {edges : ClockEdges} {oM r : SVal} {idx : Nat} {md : MemoryDesc}
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k))
    (himg : ∀ k, (k = md.nextImg ∨ md.asyncImg = some k ∨ ∃ c ∈ md.commits, c.nextImg = k) →
      k < env.size ∧ V ss k = MemImg.encMemImg (slotImg ss k))
    (hold : SRel σ oM (encArr MemImg.encMemImg st.mems))
    (h : memoryNext env edges oM idx md = some r) :
    SRel σ r (MemImg.encMemImg (memNextOf edges st ss idx md)) := by
  have slot : ∀ k, (k = md.nextImg ∨ md.asyncImg = some k ∨ ∃ c ∈ md.commits, c.nextImg = k) →
      ∀ x, env[k]? = some x → SRel σ x (MemImg.encMemImg (slotImg ss k)) := by
    intro k hk x hx
    obtain ⟨hlt, hv⟩ := himg k hk
    have := henv k hlt
    rw [hv, (Array.getElem?_eq_some_iff.mp hx).2] at this
    exact this
  simp only [memoryNext, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨held, hheld, h⟩ := h
  have rh : SRel σ held (MemImg.encMemImg (heldOf st ss idx md)) := by
    unfold memHeld at hheld
    unfold heldOf
    cases ha : md.asyncImg with
    | some a =>
        simp only [ha] at hheld
        exact slot a (Or.inr (Or.inl ha)) held hheld
    | none =>
        simp only [ha] at hheld
        obtain ⟨m, hm, rm⟩ := sNth_sound oM st.mems.toList idx held hold hheld
        have : st.mems[idx]? = some m := by simpa using hm
        simpa [Hardware.srcMemImg, this] using rm
  unfold memNextOf
  by_cases hc : md.commits.isEmpty = true
  · simp only [hc, if_true] at h ⊢
    unfold Hardware.memCommit
    by_cases hf : fires edges md.clock = true
    · simp only [hf, if_true] at h ⊢
      exact slot _ (Or.inl rfl) r h
    · simp only [hf, Bool.false_eq_true, if_false, Option.some.injEq] at h ⊢
      subst h; exact rh
  · simp only [hc, Bool.false_eq_true, if_false] at h ⊢
    by_cases hall : md.allFire edges = true
    · simp only [hall, if_true] at h ⊢
      exact slot _ (Or.inl rfl) r h
    · simp only [hall, Bool.false_eq_true, if_false] at h ⊢
      cases hfind : md.commits.find? (md.firedIs edges) with
      | some cm =>
          simp only [hfind] at h ⊢
          exact slot _ (Or.inr (Or.inr ⟨cm, Array.mem_of_find?_eq_some hfind, rfl⟩)) r h
      | none =>
          simp only [hfind, Option.some.injEq] at h ⊢
          subst h; exact rh


/-! ## The step's result -/

theorem mapM_rel {σ : Asg} {α : Type} {g : α → Option SVal} {F : α → Val} :
    ∀ (l : List α) (rs : List SVal), (∀ a ∈ l, ∀ r, g a = some r → SRel σ r (F a)) →
      l.mapM g = some rs → SRelL σ rs (l.map F)
  | [], rs, _, h => by simp at h; subst h; exact .nil
  | a :: l, rs, hg, h => by
      simp only [List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨r, hr, rs', hrs, rfl⟩ := h
      exact .cons (hg a (by simp) r hr) (mapM_rel l rs' (fun b hb => hg b (by simp [hb])) hrs)

theorem mem_zipIdx_get {α : Type} {a : α} {k : Nat} :
    ∀ (l : List α) (n : Nat), (a, k) ∈ l.zipIdx n → n ≤ k ∧ l[k - n]? = some a
  | [], n, h => by simp at h
  | b :: l, n, h => by
      rw [List.zipIdx_cons, List.mem_cons] at h
      rcases h with h | h
      · simp only [Prod.mk.injEq] at h; obtain ⟨rfl, rfl⟩ := h; simp
      · obtain ⟨h1, h2⟩ := mem_zipIdx_get l (n + 1) h
        refine ⟨by omega, ?_⟩
        have : k - n = (k - (n + 1)) + 1 := by omega
        rw [this]; simpa using h2

theorem zipIdx_flops (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) :
    ∀ (fl : List FlopDesc) (n : Nat),
      (fl.zipIdx n).map (fun p => ofBV (srcFlopNext rho e s p.2 p.1))
        = (flopNextsFrom rho e s n fl).map ofBV
  | [], n => rfl
  | f :: fl, n => by
      rw [List.zipIdx_cons, List.map_cons, zipIdx_flops rho e s fl (n + 1)]; rfl

theorem zipIdx_mems (e : ClockEdges) (st : StateM) (ss : List SlotV) :
    ∀ (ml : List MemoryDesc) (n : Nat),
      (ml.zipIdx n).map (fun p => MemImg.encMemImg (memNextOf e st ss p.2 p.1))
        = (memNextsOf e st ss n ml).map MemImg.encMemImg
  | [], n => rfl
  | m :: ml, n => by
      rw [List.zipIdx_cons, List.map_cons, zipIdx_mems e st ss ml (n + 1)]; rfl

theorem encArr_listVal {α : Type} (f : α → Val) (a : Array α) :
    encArr f a = listVal (a.toList.map f) := by
  simp [encArr, encListG_listVal]

theorem finish_sound {σ : Asg} {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} {ss : List SlotV} {env : Array SVal} {oF oM result : SVal}
    (henv : ∀ k (h : k < env.size), SRel σ env[k] (V ss k))
    (hout : ∀ o ∈ D.outputs.toList, o.slot < env.size ∧ V ss o.slot = ofBV (slotValM D i st o.slot).asBV)
    (hflop : ∀ f k, (f, k) ∈ D.flops.toList.zipIdx →
      (∀ x, (x = f.din ∨ f.enable = some x ∨ f.resetPin = some x) →
        x < env.size ∧ V ss x = ofBV (slotValM D i st x).asBV)
      ∧ ∃ b, st.flops[k]? = some b ∧ b.width = f.width)
    (hmem : ∀ md k, (md, k) ∈ D.memories.toList.zipIdx →
      ∀ x, (x = md.nextImg ∨ md.asyncImg = some x ∨ ∃ c ∈ md.commits, c.nextImg = x) →
        x < env.size ∧ V ss x = MemImg.encMemImg (slotImg ss x))
    (hoF : SRel σ oF (encBVs st.flops)) (hoM : SRel σ oM (encArr MemImg.encMemImg st.mems))
    (h : finish D.outputs D.flops D.memories e oF oM env = some result) :
    SRel σ result (StateM.encResultM (resOf D e i st ss)) := by
  simp only [finish, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨os, hos, fs, hfs, ms, hms, rfl⟩ := h
  have ro := mapM_rel (F := fun o => ofBV (bv_resize o.width (slotValM D i st o.slot).asBV))
    D.outputs.toList os (fun o ho r hr => by
      simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at hr
      obtain ⟨x, hx, hr⟩ := hr
      obtain ⟨hlt, hv⟩ := hout o ho
      have := henv o.slot hlt
      rw [hv, (Array.getElem?_eq_some_iff.mp hx).2] at this
      exact resize_sound this hr) hos
  have rf := mapM_rel (F := fun p => ofBV (srcFlopNext (slotValM D i st) e (StateM.denote st) p.2 p.1))
    (D.flops.toList.zipIdx) fs (fun p hp r hr => by
      obtain ⟨f, k⟩ := p
      obtain ⟨hpin, hsh⟩ := hflop f k hp
      exact flopNext_sound henv hpin hoF hsh hr) hfs
  have rm := mapM_rel (F := fun p => MemImg.encMemImg (memNextOf e st ss p.2 p.1))
    (D.memories.toList.zipIdx) ms (fun p hp r hr => by
      obtain ⟨md, k⟩ := p
      exact memoryNext_sound henv (hmem md k hp) hoM hr) hms
  rw [zipIdx_flops] at rf
  rw [zipIdx_mems] at rm
  simp only [StateM.encResultM, StateM.encStateM, resOf, encBVs, encArr_listVal, List.toList_toArray]
  exact .ctor (.cons (.ctor (.cons (sListOf_listVal rf) (.cons (sListOf_listVal rm) .nil)))
    (.cons (by simpa [List.map_map, Function.comp_def, encBV] using sListOf_listVal ro) .nil))


/-! ## The IR route is a proof route -/

/-- A memory-kind slot's reference value is its image. -/
theorem V_img {D : DesignCert} {i : RuntimeInput} {st : StateM} {G : Nat → Option Nat}
    {ss : List SlotV} (hss : PrefixRelM D i st G D.numSlots ss) {x : Nat}
    (hk : Direct.slotIsMem D x = some true) :
    x < ss.length ∧ V ss x = MemImg.encMemImg (slotImg ss x) := by
  have hl : x < ss.length := by rw [hss.len]; exact Direct.slotIsMem_lt hk
  obtain ⟨g, hg, henc, -⟩ := dep_mem hss hl hk
  refine ⟨hl, ?_⟩
  rw [V_eq hl, hg, slotImg_eq hl hg]; rfl

/-- A scalar slot's reference value is its bit vector. -/
theorem V_bv {D : DesignCert} {i : RuntimeInput} {st : StateM} {G : Nat → Option Nat}
    {ss : List SlotV} (hss : PrefixRelM D i st G D.numSlots ss) {x : Nat}
    (hl : x < ss.length) (hk : Direct.slotIsMem D x = some false) :
    V ss x = ofBV (slotValM D i st x).asBV := by
  rw [V_eq hl]
  have := dep_scalar hss hl hk
  simpa [List.getElem_map, encBV] using this

theorem array_get_of_mem {α : Type} {a : Array α} {x : α} (h : x ∈ a.toList) :
    ∃ k : Nat, a[k]? = some x := by
  obtain ⟨k, hk, rfl⟩ := List.getElem_of_mem h
  exact ⟨k, by simp [hk]⟩

theorem mapM_length {α β : Type} {f : α → Option β} :
    ∀ (l : List α) (r : List β), l.mapM f = some r → r.length = l.length
  | [], r, h => by simp at h; subst h; rfl
  | a :: l, r, h => by
      simp only [List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨b, -, r', hr', rfl⟩ := h
      simp [mapM_length l r' hr']

theorem SRelL_get {σ : Asg} : ∀ {ss : List SVal} {vs : List Val} (k : Nat) (h : k < ss.length),
    SRelL σ ss vs → ∃ h' : k < vs.length, SRel σ ss[k] vs[k]
  | [], _, k, h, _ => by simp at h
  | s :: ss, v :: vs, 0, _, .cons h1 _ => ⟨by simp, h1⟩
  | s :: ss, v :: vs, k + 1, h, .cons _ h2 => by
      obtain ⟨h', r⟩ := SRelL_get k (by simpa using h) h2
      exact ⟨by simpa using h', by simpa using r⟩

/-- **The IR stepper is sound**: on the admitted fragment, a successful symbolic step denotes
the interpreter's result `resM`, under every consistent assignment. -/
theorem irStepper_sound (D : DesignCert) : MemResidual D (irStepper D) := by
  intro e i st hA hR σ fuel st0 stS stF sE sI sS sres hsym hc hx rE rI rS
  obtain ⟨ss, hss, htake, hok, hres⟩ := resM_explicit hA hR
  rw [hres]
  have hwf := admitsW_wf hA
  have hsem := admitsW_semwf hA
  have hadm := admitsW_nodeAdmitted hA
  have hfits := admits_writeAddrFitsW hA i st
  have hR' := hR
  unfold runtimeFullOKB at hR'
  simp only [Bool.and_eq_true] at hR'
  obtain ⟨hR0, hflopOK⟩ := hR'
  have hR0' := hR0
  unfold runtimeOKB at hR0'
  simp only [Bool.and_eq_true] at hR0'
  obtain ⟨⟨⟨hst, hin⟩, hfb⟩, hed⟩ := hR0'
  have hfclk := edgesOKB_flops hed
  have hlen : ss.length = D.sources.size + D.nodes.size := hss.len
  -- take the step apart
  simp only [symStep, irStepper] at hsym
  unfold runWith at hsym
  split at hsym
  · cases hsym
  split at hsym
  · rename_i edges oF oM hE hS
    have he := edges_rel rE hE
    subst he
    obtain ⟨hoF, hoM⟩ := state_rel rS hS
    split at hsym
    · cases hsym
    rename_i values hvals
    split at hsym
    · cases hsym
    rename_i env st1 hwalk
    split at hsym
    · cases hsym
    rename_i result hfin
    simp only [Except.ok.injEq, Prod.mk.injEq] at hsym
    have hx1 : st1.Extends stF := by
      have := cutS_extends st1 result
      rw [hsym] at this
      exact this.trans hx
    -- the sources
    have hvsz : values.length = D.sources.size := by
      have := mapM_length _ _ hvals; simpa using this
    have rsrc := mapM_rel (F := fun sd => (srcSlotV i st sd).enc) D.sources.toList values
      (fun sd _ r hr => source_sound rI hoF hoM hr) hvals
    have henv0 : ∀ k (h : k < values.toArray.size), SRel σ values.toArray[k] (V ss k) := by
      intro k hk
      have hk' : k < values.length := by simpa using hk
      obtain ⟨h', r⟩ := SRelL_get k hk' rsrc
      have hks : k < D.sources.size := by omega
      have hkl : k < ss.length := by omega
      have e1 : (D.sources.toList.map (fun sd => (srcSlotV i st sd).enc))[k]'h' = (ss[k]'hkl).enc := by
        have := congrArg (fun l => (l.map SlotV.enc)[k]?) htake
        simp only [List.map_take, List.map_map] at this
        rw [List.getElem?_take_of_lt hks] at this
        simp only [List.getElem?_map, List.getElem?_eq_getElem hkl] at this
        simp only [List.getElem_map, Function.comp_def]
        simp [List.getElem?_eq_getElem (by simpa using hks : k < D.sources.toList.length)] at this
        exact this.symm
      rw [V_eq hkl]
      simpa [e1] using r
    -- the walk
    obtain ⟨henvsz, henv⟩ := walk_sound hc (V ss) D.nodes.toList values.toArray st0 env st1 hwalk
      hx1 henv0 (fun m hm env' r hs henv' hev => by
        have hm' : m < D.nodes.size := by simpa using hm
        have hcm : D.nodes[m]? = some D.nodes.toList[m] := by simp [hm']
        have hmem : D.nodes.toList[m] ∈ D.nodes.toList := List.getElem_mem hm
        have hsz' : env'.size = D.sources.size + m := by rw [hs]; simp [hvsz]
        rw [hsz']
        exact node_sound hwf hsem hss hok hcm (hadm _ hmem) (hfits _ hmem) hsz' henv' hev)
    have henvsz' : env.size = ss.length := by rw [henvsz, hlen]; simp [hvsz]
    -- the outputs, flops and memories
    have hres' := finish_sound (i := i) (st := st) (ss := ss) henv
      (fun o ho => by
        obtain ⟨k, hk⟩ := array_get_of_mem ho
        have hkind := hsem.outputKinds k o hk
        have hl : o.slot < ss.length := by rw [hlen]; exact hwf.slotsInRange.1 o ho
        exact ⟨by omega, V_bv hss hl hkind⟩)
      (fun f k hfk => by
        obtain ⟨-, hget⟩ := mem_zipIdx_get D.flops.toList 0 hfk
        have hk : D.flops[k]? = some f := by simpa using hget
        have hfm : f ∈ D.flops.toList := by
          have := List.mem_of_getElem? hget; simpa using this
        obtain ⟨hkd, hke, hkr⟩ := hsem.flopKinds k f hk
        obtain ⟨hbd, hbe, hbr⟩ := hwf.slotsInRange.2.1 f hfm
        refine ⟨fun x hx => ?_, flopShapeOKB_sound hflopOK k f hk⟩
        rcases hx with rfl | hx | hx
        · have hl : f.din < ss.length := by rw [hlen]; exact hbd
          exact ⟨by omega, V_bv hss hl hkd⟩
        · have hl : x < ss.length := by rw [hlen]; exact hbe x hx
          exact ⟨by omega, V_bv hss hl (hke x hx)⟩
        · have hl : x < ss.length := by rw [hlen]; exact hbr x hx
          exact ⟨by omega, V_bv hss hl (hkr x hx)⟩)
      (fun md k hmk x hx => by
        obtain ⟨-, hget⟩ := mem_zipIdx_get D.memories.toList 0 hmk
        have hk : D.memories[k]? = some md := by simpa using hget
        have hkind : Direct.slotIsMem D x = some true := by
          rcases hx with rfl | hx | ⟨c, hcm, rfl⟩
          · exact hsem.memKinds k md hk
          · exact hsem.memAsyncKinds k md hk x hx
          · exact hsem.memCommitKinds k md hk c hcm
        obtain ⟨hl, hv⟩ := V_img hss hkind
        exact ⟨by omega, hv⟩)
      hoF hoM hfin
    -- the cut
    have hcut := cutS_sound hc st1 result hres' (by rw [hsym]; exact hx)
    rw [hsym] at hcut
    exact hcut.1
  · cases hsym

#audit_axioms irStepper_sound

end HDLProof.Sym.Native

open Projection Projection.Hw Compiler Compiler.Residual Std.Tactic.BVDecide
namespace HDLProof.Sym.Native

set_option maxHeartbeats 4000000 in
/-- **A compiled binding evaluates exactly as its node does**, on every environment. -/
theorem evalExpr_compileOp {k : Nat} {c : DenseNodeCert} {e : ResidualExpr}
    (h : compileOp k c = .ok e) (env : Array SVal) : evalExpr env e = irNode env c := by
  unfold compileOp at h
  split at h
  · cases h
  cases hop : c.op
  case Op_Sum n =>
    simp only [hop, Except.ok.injEq] at h
    subst h
    simp only [evalExpr, irNode, hop]
    cases hm : c.deps.toList.mapM (env[·]?) with
    | none => simp
    | some xs => cases xs <;> simp [evalOp]
  case Op_Mult =>
    simp only [hop, Except.ok.injEq] at h
    subst h
    simp only [evalExpr, irNode, hop]
    cases hm : c.deps.toList.mapM (env[·]?) with
    | none => simp
    | some xs => cases xs <;> simp [evalOp]
  case Op_And =>
    simp only [hop, Except.ok.injEq] at h
    subst h
    simp only [evalExpr, irNode, hop]
    cases hm : c.deps.toList.mapM (env[·]?) with
    | none => simp
    | some xs => cases xs <;> simp [evalOp]
  case Op_Or =>
    simp only [hop, Except.ok.injEq] at h
    subst h
    simp only [evalExpr, irNode, hop]
    cases hm : c.deps.toList.mapM (env[·]?) with
    | none => simp
    | some xs => cases xs <;> simp [evalOp]
  case Op_Xor =>
    simp only [hop, Except.ok.injEq] at h
    subst h
    simp only [evalExpr, irNode, hop]
    cases hm : c.deps.toList.mapM (env[·]?) with
    | none => simp
    | some xs => cases xs <;> simp [evalOp]
  case Op_Ror =>
    simp only [hop, Except.ok.injEq] at h
    subst h
    simp only [evalExpr, irNode, hop]
    cases hm : c.deps.toList.mapM (env[·]?) with
    | none => simp
    | some xs => cases xs <;> simp [evalOp]
  case Op_EQ =>
    simp only [hop, Except.ok.injEq] at h
    subst h
    simp only [evalExpr, irNode, hop]
    cases hm : c.deps.toList.mapM (env[·]?) with
    | none => simp
    | some xs => cases xs <;> simp [evalOp]
  case Op_SHL =>
    simp only [hop, Except.ok.injEq] at h
    subst h
    simp only [evalExpr, irNode, hop]
    cases hm : c.deps.toList.mapM (env[·]?) with
    | none => simp
    | some xs => cases xs <;> simp [evalOp]
  case Op_MuxN =>
    simp only [hop, Except.ok.injEq] at h
    subst h
    simp only [evalExpr, irNode, hop]
    cases hm : c.deps.toList.mapM (env[·]?) with
    | none => simp
    | some xs => cases xs <;> simp [evalOp]
  case Op_Not =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | cons _ _ => simp
      | nil =>
        intro h
        simp only [Except.ok.injEq] at h
        subst h
        simp only [evalExpr, irNode, hop, hd]
        cases e0 : env[x0]? <;> simp [e0, evalOp, List.mapM_cons]
  case Op_ULT =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | cons _ _ => simp
        | nil =>
          intro h
          simp only [Except.ok.injEq] at h
          subst h
          simp only [evalExpr, irNode, hop, hd]
          cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> simp [e0, e1, evalOp, List.mapM_cons]
  case Op_UGT =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | cons _ _ => simp
        | nil =>
          intro h
          simp only [Except.ok.injEq] at h
          subst h
          simp only [evalExpr, irNode, hop, hd]
          cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> simp [e0, e1, evalOp, List.mapM_cons]
  case Op_SLT =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | cons _ _ => simp
        | nil =>
          intro h
          simp only [Except.ok.injEq] at h
          subst h
          simp only [evalExpr, irNode, hop, hd]
          cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> simp [e0, e1, evalOp, List.mapM_cons]
  case Op_SGT =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | cons _ _ => simp
        | nil =>
          intro h
          simp only [Except.ok.injEq] at h
          subst h
          simp only [evalExpr, irNode, hop, hd]
          cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> simp [e0, e1, evalOp, List.mapM_cons]
  case Op_SRA =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | cons _ _ => simp
        | nil =>
          intro h
          simp only [Except.ok.injEq] at h
          subst h
          simp only [evalExpr, irNode, hop, hd]
          cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> simp [e0, e1, evalOp, List.mapM_cons]
  case Op_Sext =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | cons _ _ => simp
        | nil =>
          intro h
          simp only [Except.ok.injEq] at h
          subst h
          simp only [evalExpr, irNode, hop, hd]
          cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> simp [e0, e1, evalOp, List.mapM_cons]
  case Op_GetMask =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | cons _ _ => simp
        | nil =>
          intro h
          simp only [Except.ok.injEq] at h
          subst h
          simp only [evalExpr, irNode, hop, hd]
          cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> simp [e0, e1, evalOp, List.mapM_cons]
  case Op_UDiv =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | cons _ _ => simp
        | nil =>
          intro h
          simp only [Except.ok.injEq] at h
          subst h
          simp only [evalExpr, irNode, hop, hd]
          cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> simp [e0, e1, evalOp, List.mapM_cons]
  case Op_SDiv =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | cons _ _ => simp
        | nil =>
          intro h
          simp only [Except.ok.injEq] at h
          subst h
          simp only [evalExpr, irNode, hop, hd]
          cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> simp [e0, e1, evalOp, List.mapM_cons]
  case Op_SetMask =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | nil => simp
        | cons x2 r2 =>
          cases r2 with
          | cons _ _ => simp
          | nil =>
            intro h
            simp only [Except.ok.injEq] at h
            subst h
            simp only [evalExpr, irNode, hop, hd]
            cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> cases e2 : env[x2]? <;> simp [e0, e1, e2, evalOp, List.mapM_cons]
  case Op_MuxBool =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | nil => simp
        | cons x2 r2 =>
          cases r2 with
          | cons _ _ => simp
          | nil =>
            intro h
            simp only [Except.ok.injEq] at h
            subst h
            simp only [evalExpr, irNode, hop, hd]
            cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> cases e2 : env[x2]? <;> simp [e0, e1, e2, evalOp, List.mapM_cons]
  case Op_MemRead =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | nil => simp
        | cons x2 r2 =>
          cases r2 with
          | cons _ _ => simp
          | nil =>
            intro h
            simp only [Except.ok.injEq] at h
            subst h
            simp only [evalExpr, irNode, hop, hd]
            cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> cases e2 : env[x2]? <;> simp [e0, e1, e2, evalOp, List.mapM_cons]
  case Op_MemWrite =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | nil => simp
        | cons x2 r2 =>
          cases r2 with
          | nil => simp
          | cons x3 r3 =>
            cases r3 with
            | cons _ _ => simp
            | nil =>
              intro h
              simp only [Except.ok.injEq] at h
              subst h
              simp only [evalExpr, irNode, hop, hd]
              cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> cases e2 : env[x2]? <;> cases e3 : env[x3]? <;> simp [e0, e1, e2, e3, evalOp, List.mapM_cons]
  case Op_MemWriteBE bw =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | nil => simp
        | cons x2 r2 =>
          cases r2 with
          | nil => simp
          | cons x3 r3 =>
            cases r3 with
            | cons _ _ => simp
            | nil =>
              intro h
              simp only [Except.ok.injEq] at h
              subst h
              simp only [evalExpr, irNode, hop, hd]
              cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> cases e2 : env[x2]? <;> cases e3 : env[x3]? <;> simp [e0, e1, e2, e3, evalOp, List.mapM_cons]
  case Op_MemLoad n =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | nil => simp
      | cons x1 r1 =>
        cases r1 with
        | nil => simp
        | cons x2 r2 =>
          cases r2 with
          | cons _ _ => simp
          | nil =>
            intro h
            simp only [Except.ok.injEq] at h
            subst h
            simp only [evalExpr, irNode, hop, hd]
            cases e0 : env[x0]? <;> cases e1 : env[x1]? <;> cases e2 : env[x2]? <;> simp [e0, e1, e2, evalOp, List.mapM_cons]
  case Op_MemToBus n =>
    simp only [hop] at h
    revert h
    cases hd : c.deps.toList with
    | nil => simp
    | cons x0 r0 =>
      cases r0 with
      | cons _ _ => simp
      | nil =>
        intro h
        simp only [Except.ok.injEq] at h
        subst h
        simp only [evalExpr, irNode, hop, hd]
        cases e0 : env[x0]? <;> simp [e0, evalOp, List.mapM_cons]
  all_goals (simp only [hop] at h; cases h)


/-! ## The compiled program is the certificate's, binding for binding -/

theorem compileFrom_shape (D : DesignCert) :
    ∀ (n start : Nat) (acc bs : Array ResidualBinding), compileFrom D start n acc = .ok bs →
      bs.size = acc.size + n ∧ (∀ k (h : k < acc.size), ∃ h', bs[k]'h' = acc[k]) ∧
      ∀ m (hm : m < n), ∃ c h', D.nodes[start + m]? = some c ∧
        compileOp (D.slotOfNode (start + m)) c = .ok (bs[acc.size + m]'h').rhs
  | 0, start, acc, bs, h => by
      simp only [compileFrom, Except.ok.injEq] at h; subst h
      exact ⟨rfl, fun k h => ⟨h, rfl⟩, fun m hm => absurd hm (Nat.not_lt_zero _)⟩
  | n + 1, start, acc, bs, h => by
      unfold compileFrom at h
      split at h
      · cases h
      rename_i c hc
      split at h
      · cases h
      rename_i e he
      obtain ⟨hsz, hpre, hnodes⟩ := compileFrom_shape D n (start + 1) _ bs h
      simp only [Array.size_push] at hsz hpre hnodes
      refine ⟨by omega, fun k hk => ?_, fun m hm => ?_⟩
      · obtain ⟨h', he'⟩ := hpre k (by omega)
        exact ⟨h', by rw [he']; simp [Array.getElem_push, hk]⟩
      · rcases m with _ | m
        · obtain ⟨h', he'⟩ := hpre acc.size (by omega)
          refine ⟨c, by omega, by simpa using hc, ?_⟩
          simp only [Nat.add_zero]
          rw [he']; simpa [Array.getElem_push] using he
        · obtain ⟨c', h', hc', he'⟩ := hnodes m (by omega)
          refine ⟨c', by omega, by rw [show start + (m + 1) = start + 1 + m by omega]; exact hc', ?_⟩
          have e1 : acc.size + (m + 1) = acc.size + 1 + m := by omega
          rw [show start + (m + 1) = start + 1 + m by omega]
          simp only [e1]; exact he'

theorem walk_congr {α β : Type} {fa : Array SVal → α → Option SVal} {fb : Array SVal → β → Option SVal} :
    ∀ (la : List α) (lb : List β), la.length = lb.length →
      (∀ m (h : m < la.length) (h' : m < lb.length) env, fa env la[m] = fb env lb[m]) →
      ∀ env st, walk fa la env st = walk fb lb env st
  | [], [], _, _, env, st => rfl
  | a :: la, b :: lb, hl, h, env, st => by
      simp only [walk]
      have h0 := h 0 (by simp) (by simp) env
      simp only [List.getElem_cons_zero] at h0
      rw [h0]
      split
      · rfl
      · exact walk_congr la lb (by simpa using hl) (fun m h1 h2 env => by
          have := h (m + 1) (by simp; omega) (by simp; omega) env
          rwa [List.getElem_cons_succ, List.getElem_cons_succ] at this) _ _
  | [], _ :: _, hl, _, _, _ => by simp at hl
  | _ :: _, [], hl, _, _, _ => by simp at hl

theorem vc_run_eq {D : DesignCert} {R : ResidualProgram} (hc : compileDesign D = .ok R) :
    ∀ fuel st e i s, (vcStepper R).run fuel st e i s = (irStepper D).run fuel st e i s := by
  unfold compileDesign at hc
  split at hc
  · cases hc
  split at hc
  · cases hc
  rename_i bs hbs
  simp only [Except.ok.injEq] at hc
  subst hc
  obtain ⟨hsz, -, hnodes⟩ := compileFrom_shape D D.nodes.size 0 #[] bs hbs
  simp only [Array.size_empty, Nat.zero_add] at hsz hnodes
  have hout : (D.outputs.map compileOutput).map
      (fun o : ResidualOutput => ({ slot := o.slot, width := o.width } : OutputDesc)) = D.outputs := by
    simp [Array.map_map, Function.comp_def, compileOutput]
  have hflop : (D.flops.map compileFlop).map ResidualFlopUpdate.asFlop = D.flops := by
    simp only [Array.map_map, Function.comp_def]
    conv => rhs; rw [← Array.map_id D.flops]
    congr 1
  have hmem : (D.memories.map compileMemory).map ResidualMemoryUpdate.asMemory = D.memories := by
    simp only [Array.map_map, Function.comp_def]
    conv => rhs; rw [← Array.map_id D.memories]
    congr 1
  have hlen : bs.toList.length = D.nodes.toList.length := by simp [hsz]
  have hw : ∀ env st, walk vcBinding bs.toList env st = walk irNode D.nodes.toList env st :=
    walk_congr bs.toList D.nodes.toList hlen (fun m h1 h2 env => by
      have hm : m < D.nodes.size := by simpa using h2
      obtain ⟨c, h', hc, he⟩ := hnodes m hm
      have hcm : D.nodes[m] = c := by
        rw [Array.getElem?_eq_getElem hm] at hc; exact Option.some.inj hc
      simp only [Array.getElem_toList, vcBinding]
      rw [hcm]; exact evalExpr_compileOp he env)
  intro fuel st e i s
  simp only [vcStepper, irStepper, hout, hflop, hmem]
  unfold runWith
  rw [hlen]
  simp only [hw]

/-- **The VC stepper is sound**: for a certificate the verified compiler accepts, a successful
symbolic step over the compiled bindings denotes the interpreter's result `resM`. -/
theorem vcStepper_sound {D : DesignCert} {R : ResidualProgram} (hc : compileDesign D = .ok R) :
    MemResidual D (vcStepper R) := by
  intro e i st hA hR σ fuel st0 stS stF sE sI sS sres hsym
  have h := vc_run_eq hc fuel st0 sE sI sS
  exact irStepper_sound D hA hR (by simpa [symStep, h] using hsym)

#audit_axioms vcStepper_sound

end HDLProof.Sym.Native
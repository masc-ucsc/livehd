/-
# `compileGraph` — the topological fold, and its correctness

Step 6.

## Why there is no hand-written prefix induction

The plan sketched `compileGraph_prefix_correct` as an explicit induction over the
dense node array, maintaining "every binding compiled so far equals the
corresponding interpreter value".  That induction already exists: it is
`GraphRefine.evalGraphG_of_localAgree`, proved once, generically, over any
`[NodeSemantics V]`.

So the shape here is *uniqueness of the topo fixpoint* rather than a fresh
induction: take φ to be "read slot `k` out of the compiled environment", show φ
satisfies the local recurrence at every topo slot (that is `compileOp_correct`
plus two stability facts about `Array.push`), and B2's theorem concludes
`evalGraphG … = φ`.  This is exactly why B2 is this branch's foundation and not
its sibling.

## Storage

`compileGraph` builds an `Array`, and `denoteResidual` reads it with
`Array.getElem?`.  B2's nested `Nat → V` environment is NOT used as the
compiler's storage: it is built by repeated `envSetG`, so a lookup traverses N
`if` layers — O(N) each, O(N²) overall.  That cost is fine for the interpreter
(never executed, only reasoned about) and fatal for the compiler (actually runs).
-/
import Mathlib
import LeanSemanticPrimitives.Compiler.CompileOp

namespace Compiler
open Residual GraphRefine

--------------------------------------------------------------------------------
-- `runBindings`: size, stability of earlier slots, and the value at each slot
--------------------------------------------------------------------------------

theorem runBindings_size : ∀ (bs : List ResidualBinding) (env : SlotEnv),
    (runBindings bs env).size = env.size + bs.length := by
  intro bs
  induction bs with
  | nil => intro env; simp [runBindings]
  | cons b bs ih => intro env; simp only [runBindings, ih, Array.size_push, List.length_cons]; omega

/-- Pushing later bindings never disturbs an earlier slot.  This is what makes
"the environment at the time binding `i` was evaluated" and "the final
environment" interchangeable on `i`'s dependencies. -/
theorem runBindings_stable : ∀ (bs : List ResidualBinding) (env : SlotEnv) (j : Nat),
    j < env.size → (runBindings bs env)[j]? = env[j]? := by
  intro bs
  induction bs with
  | nil => intro env j _; rfl
  | cons b bs ih =>
      intro env j hj
      simp only [runBindings]
      rw [ih _ j (by simp only [Array.size_push]; omega)]
      rw [Array.getElem?_push_lt hj, Array.getElem?_eq_getElem hj]

/-- Binding `k` lands at slot `env.size + k`, holding the value of its RHS
evaluated in the environment that existed at that moment. -/
theorem runBindings_at : ∀ (bs : List ResidualBinding) (env : SlotEnv) (k : Nat),
    k < bs.length →
      (runBindings bs env)[env.size + k]? =
        (bs[k]?).map fun b => denoteExpr (runBindings (bs.take k) env) b.rhs := by
  intro bs
  induction bs with
  | nil => intro env k hk; simp at hk
  | cons b bs ih =>
      intro env k hk
      cases k with
      | zero =>
          simp only [runBindings, Nat.add_zero, List.take_zero, List.getElem?_cons_zero,
            Option.map_some]
          rw [runBindings_stable _ _ _ (by simp only [Array.size_push]; omega)]
          exact Array.getElem?_push_size
      | succ k =>
          have hk' : k < bs.length := by simpa using hk
          have hsz : env.size + (k + 1) = (env.push (denoteExpr env b.rhs)).size + k := by
            simp only [Array.size_push]; omega
          simp only [runBindings, hsz, ih _ k hk', List.take_succ_cons,
            List.getElem?_cons_succ]

--------------------------------------------------------------------------------
-- The compiler over the whole graph
--------------------------------------------------------------------------------

/-- Advisory slot type.  The semantics never reads it — `CertVal` carries the
`bv | mem` distinction at runtime — but the exporter and any external checker
want it. -/
def opValueType (c : DenseNodeCert) : ValueType :=
  match c.op with
  | .Op_MemWrite     => .mem 0 c.width
  | .Op_MemWriteBE _ => .mem 0 c.width
  | _                => .bv c.width

/-- Compile `n` nodes starting at dense index `start`, appending to `acc`.
Structural recursion on the COUNT, so no termination proof is needed. -/
def compileFrom (D : DesignCert) (start : Nat) :
    Nat → Array ResidualBinding → Except CompileError (Array ResidualBinding)
  | 0,     acc => .ok acc
  | n + 1, acc =>
    match D.nodes[start]? with
    | none   => .error (.slotOutOfRange (D.slotOfNode start))
    | some c =>
      match compileOp (D.slotOfNode start) c with
      | .error err => .error err
      | .ok e      => compileFrom D (start + 1) n (acc.push { ty := opValueType c, rhs := e })

def compileGraph (D : DesignCert) : Except CompileError (Array ResidualBinding) :=
  compileFrom D 0 D.nodes.size #[]

--------------------------------------------------------------------------------
-- What a successful compile witnesses
--------------------------------------------------------------------------------

theorem compileFrom_spec (D : DesignCert) : ∀ (n start : Nat) (acc bs : Array ResidualBinding),
    compileFrom D start n acc = .ok bs →
      bs.size = acc.size + n
      ∧ (∀ j, j < acc.size → bs[j]? = acc[j]?)
      ∧ (∀ k, k < n → ∀ c, D.nodes[start + k]? = some c →
           ∃ b, bs[acc.size + k]? = some b
                ∧ compileOp (D.slotOfNode (start + k)) c = .ok b.rhs) := by
  intro n
  induction n with
  | zero =>
      intro start acc bs h
      simp only [compileFrom] at h
      injection h with h; subst h
      exact ⟨by omega, fun j _ => rfl, fun k hk => absurd hk (by omega)⟩
  | succ n ih =>
      intro start acc bs h
      simp only [compileFrom] at h
      split at h
      case _ hnone => exact absurd h (by simp)
      case _ c hsome =>
        split at h
        case _ err herr => exact absurd h (by simp)
        case _ e hok =>
          obtain ⟨hsz, hstab, hval⟩ := ih (start + 1) _ bs h
          set b0 : ResidualBinding := { ty := opValueType c, rhs := e } with hb0
          have hpush : (acc.push b0).size = acc.size + 1 := by simp
          refine ⟨by omega, ?_, ?_⟩
          · intro j hj
            rw [hstab j (by omega)]
            rw [Array.getElem?_push_lt hj, Array.getElem?_eq_getElem hj]
          · intro k hk
            cases k with
            | zero =>
                intro c' hc'
                refine ⟨b0, ?_, ?_⟩
                · rw [Nat.add_zero, hstab acc.size (by omega)]
                  exact Array.getElem?_push_size
                · rw [Nat.add_zero] at hc' ⊢
                  rw [hsome] at hc'
                  injection hc' with hcc; subst hcc
                  simpa [hb0] using hok
            | succ k =>
                intro c' hc'
                have hk' : k < n := by omega
                have harg : start + 1 + k = start + (k + 1) := by omega
                obtain ⟨b, hb, hcb⟩ := hval k hk' c' (by rw [harg]; exact hc')
                refine ⟨b, ?_, ?_⟩
                · rw [hpush] at hb
                  rw [show acc.size + (k + 1) = acc.size + 1 + k from by omega]
                  exact hb
                · rw [harg] at hcb; exact hcb

theorem compileGraph_size (D : DesignCert) (bs : Array ResidualBinding)
    (h : compileGraph D = .ok bs) : bs.size = D.nodes.size := by
  have := (compileFrom_spec D D.nodes.size 0 #[] bs h).1
  simpa using this

/-- Binding `i` is exactly `compileOp`'s output for node `i`. -/
theorem compileGraph_binding (D : DesignCert) (bs : Array ResidualBinding)
    (h : compileGraph D = .ok bs) (i : Nat) (c : DenseNodeCert) (hc : D.nodes[i]? = some c) :
    ∃ b, bs[i]? = some b ∧ compileOp (D.slotOfNode i) c = .ok b.rhs := by
  have hi : i < D.nodes.size := by
    by_cases h1 : i < D.nodes.size
    · exact h1
    · rw [Array.getElem?_eq_none (Nat.le_of_not_lt h1)] at hc; exact absurd hc (by simp)
  have := (compileFrom_spec D D.nodes.size 0 #[] bs h).2.2 i hi c (by simpa using hc)
  simpa using this

end Compiler

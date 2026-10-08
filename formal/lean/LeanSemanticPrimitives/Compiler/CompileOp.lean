/-
# `compileOp` and its per-operator correctness

Step 5 (+ 5b for memory).

`compileOp` maps ONE LGraph node to ONE residual expression, or refuses.  It is
proved correct once per operator family — never once per graph node.

## Refusing beats emitting zero

`eval_op` has a silent `| _, w, _ => mk_bv w 0` fallback at two sites
(`LGraphModel.lean:200`, `:256`), so today an unhandled operator or arity
evaluates to zero and the design still "verifies".  `compileOp` returns
`CompileError` instead.  A refusal is loud, which is the point.

## The eight refusals are measured, not guessed

A census over all 123 generated `*_Lgraph.lean` files (DINO ×3, CVA6, CORE-ET,
fixtures) counted every `LGraphOp.*` occurrence in a node position:

  | reachable            | count   | designs |
  |----------------------|---------|---------|
  | Op_GetMask           | 320,175 | 96      |
  | Op_And               |  91,225 | 88      |
  | Op_SRA               |  70,052 | 80      |
  | Op_SHL               |  57,347 | 71      |
  | Op_MuxN              |  41,347 | 80      |
  | Op_Or                |  30,931 | 85      |
  | Op_EQ                |  21,861 | 82      |
  | Op_MuxBool           |  11,461 | 36      |
  | Op_Sext              |   9,458 | 57      |
  | Op_Not               |   7,743 | 69      |
  | Op_Xor               |   5,198 | 26      |
  | Op_Ror               |   4,889 | 63      |
  | Op_Sum               |   1,879 | 37      |
  | Op_SLT               |   1,300 | 33      |
  | Op_ULT               |     492 | 27      |
  | Op_UGT               |      75 | 11      |
  | Op_SGT               |      42 |  6      |
  | Op_MemRead           |      22 |  6      |
  | Op_MemWriteBE        |      16 |  6      |
  | Op_Mult              |      12 |  1      |
  | Op_MemWrite          |       0 |  -      |

and found EIGHT that appear nowhere:

    Op_Const  Op_Sub  Op_Div  Op_UDiv  Op_SDiv  Op_LT  Op_GT  Op_SetMask

`Op_Const` is absent as a node op because constants are certificate SOURCES.
`Op_Sub` / `Op_LT` / `Op_GT` are normalised away by cprop into `Op_Sum` with
subtrahends and `Op_ULT` / `Op_SLT`.  The divisions and `Op_SetMask` simply do
not occur.  Each gets `.unsupportedOp`, so if one ever appears the sweep reports
a compile error rather than a wrong proof.  (`Op_MemWrite` at zero is kept
supported: it is the non-byte-enabled write the chain can emit.)
-/
import Mathlib
import LeanSemanticPrimitives.Compiler.ResidualSemantics
import LeanSemanticPrimitives.Compiler.DesignSemantics

namespace Compiler
open Residual

--------------------------------------------------------------------------------
-- The compiler, per node
--------------------------------------------------------------------------------

/-- Compile one node.  `ResidualRef = Nat` is the same dense slot space the
node's `deps` already live in, so the dep→ref mapping is the identity and the
only thing that can go wrong is which dep lands in which constructor POSITION —
which is exactly what `compileOp_correct` checks. -/
def compileOp (nid : Nat) (c : DenseNodeCert) : Except CompileError ResidualExpr :=
  if c.width = 0 then .error (.zeroWidth nid) else
  match c.op with
  -- variadic: any arity is meaningful
  | .Op_Sum n => .ok (.rsum c.width n c.deps)
  | .Op_Mult  => .ok (.rmult c.width c.deps)
  | .Op_And   => .ok (.rand c.width c.deps)
  | .Op_Or    => .ok (.rorBits c.width c.deps)
  | .Op_Xor   => .ok (.rxor c.width c.deps)
  | .Op_Ror   => .ok (.rredOr c.width c.deps)
  | .Op_EQ    => .ok (.req c.width c.deps)
  | .Op_SHL   => .ok (.rshl c.width c.deps)
  | .Op_MuxN  => .ok (.rmuxN c.width c.deps)
  -- fixed arity: a wrong arity is refused, not zero-filled
  | .Op_Not =>
      match c.deps.toList with
      | [a] => .ok (.rnot c.width a)
      | l   => .error (.badArity c.op l.length)
  | .Op_ULT =>
      match c.deps.toList with
      | [a, b] => .ok (.rult c.width a b)
      | l      => .error (.badArity c.op l.length)
  | .Op_UGT =>
      match c.deps.toList with
      | [a, b] => .ok (.rugt c.width a b)
      | l      => .error (.badArity c.op l.length)
  | .Op_SLT =>
      match c.deps.toList with
      | [a, b] => .ok (.rslt c.width a b)
      | l      => .error (.badArity c.op l.length)
  | .Op_SGT =>
      match c.deps.toList with
      | [a, b] => .ok (.rsgt c.width a b)
      | l      => .error (.badArity c.op l.length)
  | .Op_SRA =>
      match c.deps.toList with
      | [a, b] => .ok (.rsra c.width a b)
      | l      => .error (.badArity c.op l.length)
  | .Op_Sext =>
      match c.deps.toList with
      | [a, amt] => .ok (.rsext c.width a amt)
      | l        => .error (.badArity c.op l.length)
  | .Op_GetMask =>
      match c.deps.toList with
      | [a, m] => .ok (.rgetMask c.width a m)
      | l      => .error (.badArity c.op l.length)
  | .Op_MuxBool =>
      -- deps are [sel, falseVal, trueVal]; this ORDER is what the proof checks
      match c.deps.toList with
      | [sel, fv, tv] => .ok (.rmux c.width sel fv tv)
      | l             => .error (.badArity c.op l.length)
  -- memory
  | .Op_MemRead =>
      match c.deps.toList with
      | [m, a, en] => .ok (.rmemRead c.width m a en)
      | l          => .error (.badArity c.op l.length)
  | .Op_MemWrite =>
      match c.deps.toList with
      | [m, a, d, en] => .ok (.rmemWrite m a d en)
      | l             => .error (.badArity c.op l.length)
  | .Op_MemWriteBE bw =>
      match c.deps.toList with
      | [m, a, d, be] => .ok (.rmemWriteBE c.width bw m a d be)
      | l             => .error (.badArity c.op l.length)
  -- the eight the census found unreachable
  | op => .error (.unsupportedOp op)

--------------------------------------------------------------------------------
-- Value-level operator lemmas.  These are the substance: each says one TARGET
-- primitive agrees with one SOURCE operator, at the value level, for all
-- operands.  The graph-level plumbing (deps -> values) is done once, below.
--------------------------------------------------------------------------------

namespace Residual

/-- Helper: a left fold accumulating `f` is the seed plus the mapped sum. -/
theorem foldl_add_eq_sum {α : Type} (f : α → Int) :
    ∀ (l : List α) (z : Int), l.foldl (fun acc x => acc + f x) z = z + (l.map f).sum := by
  intro l
  induction l with
  | nil => intro z; simp
  | cons a l ih => intro z; simp only [List.foldl_cons, List.map_cons, List.sum_cons, ih]; ring

theorem foldl_mul_eq_prod {α : Type} (f : α → Int) :
    ∀ (l : List α) (z : Int), l.foldl (fun acc x => acc * f x) z = z * (l.map f).prod := by
  intro l
  induction l with
  | nil => intro z; simp
  | cons a l ih => intro z; simp only [List.foldl_cons, List.map_cons, List.prod_cons, ih]; ring

/-- `Op_Sum`: REAL content — explicit `foldl` over `bv_uint` after take/drop,
against `List.sum` of the mapped list.  This is the lemma that catches a wrong
add/subtract split. -/
theorem rsumV_correct (w n : Nat) (vs : List BV) :
    rsumV w n vs = eval_op (LGraphOp.Op_Sum n) w vs := by
  simp only [rsumV, eval_op, foldl_add_eq_sum, zero_add, List.map_take, List.map_drop]

theorem rmultV_correct (w : Nat) (vs : List BV) :
    rmultV w vs = eval_op LGraphOp.Op_Mult w vs := by
  simp only [rmultV, eval_op, foldl_mul_eq_prod, one_mul]

theorem randV_correct (w : Nat) (vs : List BV) :
    randV w vs = eval_op LGraphOp.Op_And w vs := by
  cases vs <;> rfl

theorem rorBitsV_correct (w : Nat) (vs : List BV) :
    rorBitsV w vs = eval_op LGraphOp.Op_Or w vs := rfl

theorem rxorV_correct (w : Nat) (vs : List BV) :
    rxorV w vs = eval_op LGraphOp.Op_Xor w vs := rfl

/-- `Op_Ror` (reduction OR): REAL content — explicit recursion against
`List.any`. -/
theorem rredOrV_correct (w : Nat) (vs : List BV) :
    rredOrV w vs = eval_op LGraphOp.Op_Ror w vs := by
  induction vs with
  | nil => rfl
  | cons a vs ih =>
      simp only [rredOrV, eval_op, List.any_cons]
      by_cases h : bv_nonzero a = true
      · simp [h]
      · rw [Bool.not_eq_true] at h
        simp only [h, Bool.false_or, Bool.false_eq_true, if_false]
        simpa [eval_op] using ih

/-- `Op_EQ`: REAL content — explicit recursion with `==` against `List.all`
with `=`. -/
theorem reqTail_correct (a : BV) :
    ∀ (l : List BV), reqTail a l = l.all fun b => bv_uint b = bv_uint a := by
  intro l
  induction l with
  | nil => rfl
  | cons b l ih =>
      have hb : (bv_uint b == bv_uint a) = decide (bv_uint b = bv_uint a) := by
        by_cases hh : bv_uint b = bv_uint a <;> simp [hh]
      simp [reqTail, List.all_cons, ih, hb]

theorem reqV_correct (w : Nat) (vs : List BV) :
    reqV w vs = eval_op LGraphOp.Op_EQ w vs := by
  cases vs with
  | nil => rfl
  | cons a l => simp only [reqV, eval_op, reqTail_correct]

theorem rshlV_correct (w : Nat) (vs : List BV) :
    rshlV w vs = eval_op LGraphOp.Op_SHL w vs := by
  cases vs <;> rfl

theorem rnotV_correct (w : Nat) (a : BV) :
    rnotV w a = eval_op LGraphOp.Op_Not w [a] := by
  have hf : (fun i => !bv_bit a i) = (fun i => decide ¬(bv_bit a i = true)) := by
    funext i; simp
  simp only [rnotV, eval_op, bv_not, hf]

theorem rultV_correct (w : Nat) (a b : BV) :
    rultV w a b = eval_op LGraphOp.Op_ULT w [a, b] := rfl

theorem rugtV_correct (w : Nat) (a b : BV) :
    rugtV w a b = eval_op LGraphOp.Op_UGT w [a, b] := rfl

theorem rsltV_correct (w : Nat) (a b : BV) :
    rsltV w a b = eval_op LGraphOp.Op_SLT w [a, b] := rfl

theorem rsgtV_correct (w : Nat) (a b : BV) :
    rsgtV w a b = eval_op LGraphOp.Op_SGT w [a, b] := rfl

theorem rsraV_correct (w : Nat) (a b : BV) :
    rsraV w a b = eval_op LGraphOp.Op_SRA w [a, b] := rfl

theorem rgetMaskV_correct (w : Nat) (a m : BV) :
    rgetMaskV w a m = eval_op LGraphOp.Op_GetMask w [a, m] := rfl

/-- `Op_MuxBool`: the operand ORDER check.  Deps are `[sel, falseVal, trueVal]`;
if `compileOp` swapped the last two this would not be provable. -/
theorem rmuxV_correct (w : Nat) (sel fv tv : BV) :
    rmuxV w sel fv tv = eval_op LGraphOp.Op_MuxBool w [sel, fv, tv] := rfl

/-- `Op_MuxN`: REAL content — `args[idx]?` match against an `idx < length`
guard. -/
theorem rmuxNV_correct (w : Nat) (vs : List BV) :
    rmuxNV w vs = eval_op LGraphOp.Op_MuxN w vs := by
  cases vs with
  | nil => rfl
  | cons sel args =>
      simp only [rmuxNV, eval_op]
      by_cases h : (bv_uint sel).toNat < args.length
      · rw [List.getElem?_eq_getElem h]; simp [h]
      · rw [List.getElem?_eq_none (by omega)]; simp [h]

/-- **The general `Op_Sext` bridge** — the coverage gap the manual path cannot
close.  `pass_lean.cpp` emits `bv_sext`, which extends from the OPERAND's own
width and so ignores the amount entirely; only `amt = wa` and `amt = w ∧ w ≤ wa`
are covered, and any other amount makes the design fail to typecheck.  This
holds for EVERY `amt`. -/
theorem rsextV_correct (w : Nat) (a amt : BV) :
    rsextV w a amt = eval_op LGraphOp.Op_Sext w [a, amt] := by
  simp only [rsextV, eval_op]
  by_cases hn : (bv_uint amt).toNat = 0
  · simp [hn]
  · simp only [hn, if_false]
    have hu : bv_uint (mk_bv (bv_uint amt).toNat (bv_uint a))
        = bv_uint a % (2 ^ (bv_uint amt).toNat : Int) := by
      simp only [bv_uint, mk_bv]
      exact Int.emod_emod_of_dvd _ dvd_rfl
    have hw : (mk_bv (bv_uint amt).toNat (bv_uint a)).width = (bv_uint amt).toNat := rfl
    simp only [bv_sint, hu, hw, hn, if_false, apply_ite (mk_bv w)]

--------------------------------------------------------------------------------
-- Step 5b: memory operator lemmas.
--
-- The enable test sits INSIDE the lambda in the target and OUTSIDE it in the
-- source, so each write bridge is a genuine `funext` plus a case split rather
-- than a definitional unfolding.
--------------------------------------------------------------------------------

theorem rmemReadV_correct (w : Nat) (m : Int → BV) (a en : BV) :
    rmemReadV w m a en = cert_mem_read w m a en := by
  simp only [rmemReadV, cert_mem_read, bv_nonzero]
  by_cases h : bv_uint en = 0 <;> simp [h]

theorem rmemWriteV_correct (m : Int → BV) (a d en : BV) :
    rmemWriteV m a d en = cert_mem_write m a d en := by
  funext x
  simp only [rmemWriteV, cert_mem_write, bv_nonzero]
  by_cases h : bv_uint en = 0 <;> simp [h]

theorem rMaskedUpdateV_correct (w : Nat) (old new bev : BV) (byteW : Nat) :
    rMaskedUpdateV w old new bev byteW = cert_masked_update w old new bev byteW := rfl

theorem rmemWriteBEV_correct (w : Nat) (m : Int → BV) (a d bev : BV) (byteW : Nat) :
    rmemWriteBEV w m a d bev byteW = cert_mem_write_be w m a d bev byteW := by
  funext x
  simp only [rmemWriteBEV, cert_mem_write_be, bv_nonzero, rMaskedUpdateV_correct]
  by_cases h : bv_uint bev = 0 <;> simp [h]

end Residual
open Residual

--------------------------------------------------------------------------------
-- Step 5, the graph-level lemma.
--
-- ONE theorem per operator FAMILY, never one per graph node.  The hypothesis
-- `hagree` is the plan's "all compiled operand references have the same values
-- as the interpreter's operands"; the conclusion is "the compiled expression
-- produces the same result as the interpreter's operator".
--------------------------------------------------------------------------------

theorem refBVs_eq (env : SlotEnv) (rho : Nat → CertVal) (args : Array ResidualRef)
    (hagree : ∀ r ∈ args.toList, denoteRef env r = rho r) :
    refBVs env args = (args.toList.map rho).map CertVal.asBV := by
  simp only [refBVs, List.map_map]
  exact List.map_congr_left fun r hr => by
    simp only [refBV, Function.comp_apply, hagree r hr]

/-- Zero width is refused, so a successful compile witnesses a nonzero width. -/
theorem width_ne_zero_of_ok {nid : Nat} {c : DenseNodeCert} {e : ResidualExpr}
    (hc : compileOp nid c = .ok e) : c.width ≠ 0 := by
  intro h
  simp only [compileOp, h] at hc
  exact absurd hc (by simp)

/-- **`compileOp_correct`.**  If every compiled operand ref reads what the
interpreter's operand reads, the compiled expression denotes what the
interpreter's operator computes — for every operator, every arity, every
operand. -/
theorem compileOp_correct (nid : Nat) (c : DenseNodeCert) (env : SlotEnv)
    (rho : Nat → CertVal) (e : ResidualExpr)
    (hc : compileOp nid c = .ok e)
    (hagree : ∀ r ∈ c.deps.toList, denoteRef env r = rho r) :
    denoteExpr env e = eval_op_cert c.op c.width (c.deps.toList.map rho) := by
  have hw : c.width ≠ 0 := width_ne_zero_of_ok hc
  cases hop : c.op with
  | Op_Sum n =>
      simp only [compileOp, hop, if_neg hw] at hc
      injection hc with he; subst he
      simp only [denoteExpr, refBVs_eq env rho c.deps hagree, rsumV_correct, eval_op_cert]
  | Op_Mult =>
      simp only [compileOp, hop, if_neg hw] at hc
      injection hc with he; subst he
      simp only [denoteExpr, refBVs_eq env rho c.deps hagree, rmultV_correct, eval_op_cert]
  | Op_And =>
      simp only [compileOp, hop, if_neg hw] at hc
      injection hc with he; subst he
      simp only [denoteExpr, refBVs_eq env rho c.deps hagree, randV_correct, eval_op_cert]
  | Op_Or =>
      simp only [compileOp, hop, if_neg hw] at hc
      injection hc with he; subst he
      simp only [denoteExpr, refBVs_eq env rho c.deps hagree, rorBitsV_correct, eval_op_cert]
  | Op_Xor =>
      simp only [compileOp, hop, if_neg hw] at hc
      injection hc with he; subst he
      simp only [denoteExpr, refBVs_eq env rho c.deps hagree, rxorV_correct, eval_op_cert]
  | Op_Ror =>
      simp only [compileOp, hop, if_neg hw] at hc
      injection hc with he; subst he
      simp only [denoteExpr, refBVs_eq env rho c.deps hagree, rredOrV_correct, eval_op_cert]
  | Op_EQ =>
      simp only [compileOp, hop, if_neg hw] at hc
      injection hc with he; subst he
      simp only [denoteExpr, refBVs_eq env rho c.deps hagree, reqV_correct, eval_op_cert]
  | Op_SHL =>
      simp only [compileOp, hop, if_neg hw] at hc
      injection hc with he; subst he
      simp only [denoteExpr, refBVs_eq env rho c.deps hagree, rshlV_correct, eval_op_cert]
  | Op_MuxN =>
      simp only [compileOp, hop, if_neg hw] at hc
      injection hc with he; subst he
      simp only [denoteExpr, refBVs_eq env rho c.deps hagree, rmuxNV_correct, eval_op_cert]
  | Op_Not =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ a hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refBV,
        hagree a (by rw [hd]; simp),
          List.map_cons, List.map_nil, rnotV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_ULT =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ a b hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refBV,
        hagree a (by rw [hd]; simp),
        hagree b (by rw [hd]; simp),
          List.map_cons, List.map_nil, rultV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_UGT =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ a b hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refBV,
        hagree a (by rw [hd]; simp),
        hagree b (by rw [hd]; simp),
          List.map_cons, List.map_nil, rugtV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_SLT =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ a b hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refBV,
        hagree a (by rw [hd]; simp),
        hagree b (by rw [hd]; simp),
          List.map_cons, List.map_nil, rsltV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_SGT =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ a b hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refBV,
        hagree a (by rw [hd]; simp),
        hagree b (by rw [hd]; simp),
          List.map_cons, List.map_nil, rsgtV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_SRA =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ a b hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refBV,
        hagree a (by rw [hd]; simp),
        hagree b (by rw [hd]; simp),
          List.map_cons, List.map_nil, rsraV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_Sext =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ a amt hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refBV,
        hagree a (by rw [hd]; simp),
        hagree amt (by rw [hd]; simp),
          List.map_cons, List.map_nil, rsextV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_GetMask =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ a m hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refBV,
        hagree a (by rw [hd]; simp),
        hagree m (by rw [hd]; simp),
          List.map_cons, List.map_nil, rgetMaskV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_MuxBool =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ sel fv tv hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refBV,
        hagree sel (by rw [hd]; simp),
        hagree fv (by rw [hd]; simp),
        hagree tv (by rw [hd]; simp),
          List.map_cons, List.map_nil, rmuxV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_MemRead =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ m a en hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refMem, refBV,
        hagree m (by rw [hd]; simp),
        hagree a (by rw [hd]; simp),
        hagree en (by rw [hd]; simp),
          List.map_cons, List.map_nil, rmemReadV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_MemWrite =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ m a d en hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refMem, refBV,
        hagree m (by rw [hd]; simp),
        hagree a (by rw [hd]; simp),
        hagree d (by rw [hd]; simp),
        hagree en (by rw [hd]; simp),
          List.map_cons, List.map_nil, rmemWriteV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_MemWriteBE bw =>
      simp only [compileOp, hop, if_neg hw] at hc
      split at hc
      case _ m a d be hd =>
        injection hc with he; subst he
        rw [hd]
        simp only [denoteExpr, refMem, refBV,
        hagree m (by rw [hd]; simp),
        hagree a (by rw [hd]; simp),
        hagree d (by rw [hd]; simp),
        hagree be (by rw [hd]; simp),
          List.map_cons, List.map_nil, rmemWriteBEV_correct, eval_op_cert]
      case _ => exact absurd hc (by simp)
  | Op_Const cst =>
      simp only [compileOp, hop, if_neg hw] at hc
      exact absurd hc (by simp)
  | Op_Sub =>
      simp only [compileOp, hop, if_neg hw] at hc
      exact absurd hc (by simp)
  | Op_Div =>
      simp only [compileOp, hop, if_neg hw] at hc
      exact absurd hc (by simp)
  | Op_UDiv =>
      simp only [compileOp, hop, if_neg hw] at hc
      exact absurd hc (by simp)
  | Op_SDiv =>
      simp only [compileOp, hop, if_neg hw] at hc
      exact absurd hc (by simp)
  | Op_LT =>
      simp only [compileOp, hop, if_neg hw] at hc
      exact absurd hc (by simp)
  | Op_GT =>
      simp only [compileOp, hop, if_neg hw] at hc
      exact absurd hc (by simp)
  | Op_SetMask =>
      simp only [compileOp, hop, if_neg hw] at hc
      exact absurd hc (by simp)

end Compiler

#!/usr/bin/env python3
"""Direction 3 reifier, reference implementation and scaling benchmark.

Emits, for a synthetic ResidualProgram of n bindings, a real Lean `def` as a
straight-line let-chain plus a per-design proof that it equals
`denoteResidual R` for an explicit literal `R`.

Two proof variants, measured head to head:

  naive     -- one `simp` blast over `runBindings`.  Every intermediate
               environment appears as a term of size O(k), so the proof is
               O(N^2) in term size no matter what the design looks like.

  composed  -- walk the fold one binding at a time with `runBindings_step`,
               keeping each environment behind a `set` so it stays a single
               free variable.  Source operands resolve in O(1) through a
               carried agreement invariant; binding operands cost one rewrite
               per level of separation.  So the cost is
               O(N + sum of dependency distances) rather than O(N^2).

Two dependency shapes, because the shape is what decides which term dominates:

  chain -- binding k reads binding k-1 and source 1.  Distance to the binding
           operand is 1; distance to the SOURCE operand is k.  This is the
           shape the original quadratic measurement used, and the source term
           is what made it quadratic.
  far   -- binding k reads binding k//2.  Distances grow as k/2 and no
           invariant can collapse them; this is the adversarial case.
"""
import argparse

# The composition lemmas are inlined rather than imported: `.lake` is shared
# with another worktree and building a new module there would write into it.
# They are a fixed constant in every measurement and cancel in the comparison.
HDR = """import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler Compiler.Residual

namespace Compiler
theorem runBindings_step (b : ResidualBinding) (bs : List ResidualBinding)
    (env : SlotEnv) (v : CertVal) (hv : denoteExpr env b.rhs = v) :
    runBindings (b :: bs) env = runBindings bs (env.push v) := by
  simp only [runBindings, hv]

theorem refBV_push_lt (env : SlotEnv) (v : CertVal) (j : Nat) (h : j < env.size) :
    refBV (env.push v) j = refBV env j := by
  simp only [refBV, denoteRef, Array.getElem?_push_lt h, Array.getElem?_eq_getElem h,
    Option.getD_some]

theorem refBV_push_self (env : SlotEnv) (v : CertVal) :
    refBV (env.push v) env.size = v.asBV := by
  simp only [refBV, denoteRef, Array.getElem?_push_size, Option.getD_some]

theorem srcAgree_push {env base : SlotEnv} (v : CertVal)
    (hsz : base.size ≤ env.size)
    (h : ∀ j, j < base.size → refBV env j = refBV base j) :
    ∀ j, j < base.size → refBV (env.push v) j = refBV base j := by
  intro j hj
  rw [refBV_push_lt env v j (Nat.lt_of_lt_of_le hj hsz)]
  exact h j hj
end Compiler
"""

W = 8
NSRC = 2


def deps(k, shape):
    """Operand slots for binding k.  Slot space: sources 0..NSRC-1, binding m at NSRC+m."""
    if shape == "chain":
        prev = (NSRC + k - 1) if k > 0 else 0
        return [prev, 1]
    else:  # far
        prev = (NSRC + k - 1) if k > 0 else 0
        back = (NSRC + k // 2) if k > 0 else 1
        return [prev, back]


def gen_program(n, shape):
    bs = []
    for k in range(n):
        d = deps(k, shape)
        bs.append(f"    {{ ty := ValueType.bv {W}, rhs := ResidualExpr.rand {W} #[{d[0]}, {d[1]}] }}")
    body = "\n  , ".join(bs)
    return (f"def R : ResidualProgram :=\n"
            f"  {{ sources  := #[SourceDesc.input 0 {W}, SourceDesc.input 1 {W}]\n"
            f"    bindings := #[\n  {body}\n    ]\n"
            f"    outputs  := #[{{ slot := {NSRC + n - 1}, width := {W} }}]\n"
            f"    flopUpdates := #[], memoryUpdates := #[] }}\n")


def slot_expr(sl, n):
    """Lean expression naming the value at slot `sl` in the inlined def."""
    return f"s{sl}" if sl < NSRC else f"v{sl - NSRC}"


def gen_fast(n, shape):
    """The straight-line let-chain: this is the compiled-simulation artifact."""
    out = [f"def fast (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :="]
    for j in range(NSRC):
        out.append(f"  let s{j} := bv_resize {W} (i[{j}]?.getD (mk_bv {W} 0))")
    for k in range(n):
        d = deps(k, shape)
        a, b = slot_expr(d[0], n), slot_expr(d[1], n)
        out.append(f"  let v{k} := randV {W} [{a}, {b}]")
    out.append(f"  {{ outputs := #[bv_resize {W} v{n - 1}]")
    out.append(f"    nextState := {{ flops := #[], mems := #[] }} }}")
    return "\n".join(out) + "\n"


def gen_naive(n):
    return ("theorem fast_correct : ∀ i s, fast i s = denoteResidual R i s := by\n"
            "  intro i s\n"
            "  simp [fast, denoteResidual, R, runBindings, sourceEnvArr, denoteExpr,\n"
            "    refBVs, refBV, denoteRef, sourceValue, CertVal.asBV]\n")


def emit_read(k, sl, tag):
    """Tactics proving `refBV E_k sl = <named value>`, as an isolated `have`.

    A SOURCE read costs ONE rewrite whatever `k` is, through the carried
    agreement invariant.  A BINDING read costs one rewrite per level of
    separation -- that residual term is what decides the exponent, and it is
    a property of the design's dependency locality, not of the proof.
    """
    L = []
    if sl < NSRC:
        L.append(f"  have {tag} : refBV E{k} {sl} = s{sl} := by")
        L.append(f"    rw [hag{k} {sl} (by rw [hs0]; omega), hb{sl}]")
    else:
        m = sl - NSRC
        L.append(f"  have {tag} : refBV E{k} {sl} = v{m} := by")
        for cur in range(k, m + 1, -1):
            L.append(f"    rw [hE{cur}, refBV_push_lt _ _ _ (by rw [hs{cur - 1}]; omega)]")
        L.append(f"    rw [hE{m + 1}, show ({sl} : Nat) = E{m}.size from (hs{m}).symm,"
                 f" refBV_push_self]")
        # `CertVal.asBV` must be discharged explicitly or the goal is left as
        # projection residue -- measured, and easy to mistake for a real failure.
        L.append("    rfl")
    return L


def gen_composed(n, shape, prune=False):
    """Walk the fold one binding at a time, keeping every environment opaque.

    Names for the node values are introduced with `set` BEFORE the walk, while
    the goal is still `fast i s = denoteResidual R i s` and therefore O(1).
    Zeta-reducing `fast` first would expand the chain and, for the `far` shape
    where a value has two consumers, duplicate shared subterms exponentially.
    """
    L = ["theorem fast_correct : ∀ i s, fast i s = denoteResidual R i s := by",
         "  intro i s"]
    for j in range(NSRC):
        L.append(f"  set s{j} : BV := bv_resize {W} (i[{j}]?.getD (mk_bv {W} 0)) with hsd{j}")
    for k in range(n):
        d = deps(k, shape)
        L.append(f"  set v{k} : BV := randV {W} [{slot_expr(d[0], n)}, {slot_expr(d[1], n)}]"
                 f" with hvd{k}")
    L.append("  simp only [denoteResidual, R]")
    L.append("  set E0 : SlotEnv := sourceEnvArr"
             " (#[SourceDesc.input 0 8, SourceDesc.input 1 8]) i s with hE0")
    L.append(f"  have hs0 : E0.size = {NSRC} := by simp [hE0, sourceEnvArr]")
    for j in range(NSRC):
        L.append(f"  have hb{j} : refBV E0 {j} = s{j} := by"
                 f" simp [hE0, hsd{j}, sourceEnvArr, refBV, denoteRef, sourceValue,"
                 f" CertVal.asBV]")
    L.append("  have hag0 : ∀ j, j < E0.size → refBV E0 j = refBV E0 j := fun _ _ => rfl")
    for k in range(n):
        d = deps(k, shape)
        L += emit_read(k, d[0], f"ra{k}")
        L += emit_read(k, d[1], f"rb{k}")
        L.append(f"  have hv{k} : denoteExpr E{k} (ResidualExpr.rand {W} #[{d[0]}, {d[1]}])"
                 f" = CertVal.bv v{k} := by")
        L.append(f"    have hu : denoteExpr E{k} (ResidualExpr.rand {W} #[{d[0]}, {d[1]}])"
                 f" = CertVal.bv (randV {W} [refBV E{k} {d[0]}, refBV E{k} {d[1]}]) := rfl")
        L.append(f"    rw [hu, ra{k}, rb{k}]")
        L.append(f"  rw [runBindings_step _ _ _ _ hv{k}]")
        L.append(f"  set E{k + 1} : SlotEnv := E{k}.push (CertVal.bv v{k}) with hE{k + 1}")
        L.append(f"  have hs{k + 1} : E{k + 1}.size = {NSRC + k + 1} := by"
                 f" rw [hE{k + 1}, Array.size_push, hs{k}]")
        L.append(f"  have hag{k + 1} : ∀ j, j < E0.size → refBV E{k + 1} j = refBV E0 j :=")
        L.append(f"    fun j hj => by rw [hE{k + 1}]; exact srcAgree_push _ (by omega) hag{k} j hj")
        if prune:
            # The local context otherwise grows to ~6N hypotheses and every
            # tactic re-scans it, which is a second, independent source of
            # quadratic cost on top of term size.  These four are provably dead
            # once the step is over.
            dead = [f"ra{k}", f"rb{k}", f"hv{k}", f"hag{k}"]
            L.append(f"  clear {' '.join(dead)}")
    L.append("  simp only [runBindings]")
    L += emit_read(n, NSRC + n - 1, "rout")
    L.append("  simp only [Array.map_singleton, rout]")
    # `Array.map` / `Array.mapIdx` on `#[]` are not kernel-reducible, so `rfl`
    # cannot close the state fields on its own -- the same non-reducibility that
    # makes a whole-goal `rfl` impossible even at n = 2.
    L.append("  simp only [Array.map_empty, Array.mapIdx_empty]")
    L.append("  rfl")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("n", type=int)
    ap.add_argument("--variant", choices=["naive", "composed", "pruned"], required=True)
    ap.add_argument("--shape", choices=["chain", "far"], default="chain")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    txt = (HDR + "\n" + gen_program(a.n, a.shape) + "\n" + gen_fast(a.n, a.shape) + "\n"
           + (gen_naive(a.n) if a.variant == "naive"
              else gen_composed(a.n, a.shape, prune=(a.variant == "pruned"))))
    open(a.out, "w").write(txt)
    print(f"{a.out}: n={a.n} variant={a.variant} shape={a.shape} lines={txt.count(chr(10))}")


main()

# Step-5 fast-view bridge — proof bugs surfaced at DINO scale

**Status:** the emitter generates a *sorry-free* step-5 bridge for DINO
`SingleCycleCPU` (4772 nodes), but it was **never verified to completion**: the
single-file monolithic typecheck is O(N²) (see "Scaling" below) and every run was
killed unfinished (the last reached **2 d 8 h / 94 GB**). Switching the graph
lookups to an **O(N) data-tree** representation made the check fast enough to run
to the point of elaborating the per-node theorems — which revealed **3
pre-existing op-proof bugs** in the emitter output. "0 sorries" ≠ "typechecks":
these nodes carry real proof holes that the never-finishing monolithic run never
exposed.

This document records the three bugs, how to fix each, and the expected runtime
after the fix. The bugs are in the **emitter** (`pass_lean.cpp`) and its op-bridge
library (`OpBridge.lean`); they are independent of the representation used for
`φ`/`graphCert` (the data-tree transformer copies the per-node proofs verbatim).

Failing nodes are cited from the generated
`SingleCycleCPU_Lgraph.lean` (bridge mode).

---

## Bug 1 — `Sext` side-condition too strict (`amt.toNat = wa`)

- **Lemma:** `OpBridge.lean` `sext_bridge {wa wam w} (a : BitVec wa) (amt : BitVec wam) (hamt : amt.toNat = wa)`.
- **Emitter:** emits `sext_bridge a amt (by decide)` for every `Op_Sext` node.
- **Symptom:** node `fv4208` — `Op_Sext` width 32, operand `fv2548` (**64-bit**),
  amount `BitVec.ofInt 6 32` → the `by decide` must prove `(ofInt 6 32).toNat = 64`
  i.e. `32 = 64`, which is **false**. Also `fv6920` (width 64, operand 127-bit,
  amount `ofInt 7 64` → `64 = 127` false).
- **Root cause:** the lemma assumes the sign-position `amount` equals the *operand*
  width `wa` (a full sign-extend from the top bit). DINO also has `Op_Sext` nodes
  where `amount = output width w ≤ wa` — a **sign-truncate**: take the low `w`
  (= `amount`) bits; the sign bit sits at/above the output width, so it is
  irrelevant. Both the cert evaluator and the fast model produce the low-`w` bits
  there, so the equality holds — just not via *this* lemma.
  - fast: `bv_sext a : BitVec w = BitVec.ofInt w a.toInt` = low `w` bits when `w ≤ wa`.
  - cert `Op_Sext` with `n = amount = w`: `u := (bv_uint a) % 2^w`; both `mk_bv w u`
    and `mk_bv w (u - 2^w)` equal the low-`w` BitVec.
- **Fix:** add a companion lemma
  `sext_bridge_low {wa wam w} (a) (amt) (h : amt.toNat = w) (hle : w ≤ wa)`
  with the same conclusion, proving the sign-truncate case. The emitter dispatches
  on the amount:
  - `amount = wa` → `sext_bridge` (existing, full sign-extend);
  - `amount = w ∧ w ≤ wa` → `sext_bridge_low` (truncate);
  - otherwise → a genuine mid-width sign-extend (none observed in DINO) — emit a
    flagged `sorry`/error rather than a wrong `by decide`.
  This needs the emitter to know each Sext's operand width `wa` (already available
  via `width_of`) alongside `w` and the amount value.

---

## Bug 2 — compare (`Op_EQ`/`ULT`/`UGT`/`SLT`) constant spelling not normalized

- **Emitter closer:** `simp only [<fv>, bv_zext_id, Int.ofNat_eq_natCast, Nat.cast_ofNat, Nat.cast_one]` after `@eq_bridge …`.
- **Symptom:** node `fv2248` — `Op_EQ` of const source
  `1000000220 = bvenc (BitVec.ofInt 6 0)` with `fv19380` — leaves the goal
  `bvenc (bool_to_bv1 (decide (fv19380 = 0#6))) = bvenc (bool_to_bv1 (decide (fv19380 = BitVec.ofInt 6 0)))`
  unsolved.
- **Root cause:** the **fast model spells a constant as `0#w` / `n#w`**, while the
  **cert source leaf is `BitVec.ofInt w c`**. For arithmetic/bitwise ops the
  constant flows through `bv_zext` and `bv_zext_id` closes it; but a **compare
  keeps its operands inside `decide (_ = _)` / `_ < _`**, so the two spellings of
  the *same* constant survive into the result and must be reconciled. The closer
  has no lemma equating `BitVec.ofInt 6 0` and `0#6`.
- **Fix:** extend the compare closer with constant-normalization simp lemmas so
  both spellings converge (e.g. `BitVec.ofInt` of a nonneg literal →
  `BitVec.ofNat`/`n#w`; and `0#w = BitVec.ofInt w 0`). Alternatively, emit the fast
  compare's constant operand in `BitVec.ofInt` form so it matches the cert leaf
  syntactically. Applies uniformly to EQ/ULT/UGT/SLT.

---

## Bug 3 — `Op_MuxN` branch values not normalized

- **Emitter closer:** same `simp only [...]` after `muxn3_bridge` (`OpBridge.lean`).
- **Symptom:** node `fv1384` — `Op_MuxN` width 64, deps `[sel = fv16972,
  const 1000002430, fv16988]` — leaves a mismatch around
  `if (fv16972).toNat = 1 then fv16988 else 0#64` between the two sides.
- **Root cause:** the same family as Bug 2. Inside the mux `if`-chain the option
  values keep their spelling — the const option as `0#64` vs `BitVec.ofInt`, and a
  data option as `bv_zext o` vs the fast operand — and `bv_zext_id` does not fire
  on every branch (option width ≠ output width, and/or the const spelling differs).
- **Fix:** extend the MuxN closer with the same constant/`bv_zext` normalization as
  Bug 2, and ensure `bv_zext` reductions fire inside `if`-branches (unfold
  `bv_zext` for mismatched-width options). Check against `muxn3_bridge`'s exact
  `if sel.toNat = 0 then … else if sel.toNat = 1 then … else 0#w` shape.

**Unifying root cause of Bugs 2 & 3:** a *constant-spelling normalization gap* in
the per-node closer for **structure-preserving ops** (compare, mux), where an
operand survives into the result term and the fast (`n#w`) vs cert
(`BitVec.ofInt w c`) spellings must be reconciled. Bug 1 is unrelated (a
too-strict lemma hypothesis).

---

## Scaling context (why these were invisible until now)

Measured on self-contained files with the same proof kind:

| representation of `φ` / `graphCert.nodes` | scaling | N=1000 → N=2000 |
|---|---|---|
| monolithic linear `if`-chain / `List.find?` | **O(N²)** | 1776 s (→ 34 h+ at 4772, never finished) |
| balanced `if`-tree | **O(N²)** (beta-substitution of the key across the N-node term) | 341 → 1277 s |
| **inductive data-tree + `find`, closure values** | **O(N)** | 135 → 261 s |

The data-tree brings the *whole* proof to linear (synthetic complete 4772 =
**662 s / 11 min, 0 sorry**). Per-node cost is constant (~139 ms/node on the
width-8 synthetic) instead of O(N) — because a lookup navigates the tree *value*
(comparing the key locally, O(log N)) with no O(N) substitution, unlike the
`if`-chain / `if`-tree where the key is beta-substituted across the entire term.

---

## Runtime estimate for the real 4772-node DINO bridge, after the fix

- Data-tree scaling is **linear**: 135 s → 261 s → 662 s at N = 1000 → 2000 → 4772
  (synthetic, width-8), ~**139 ms/node**.
- Real DINO nodes are heavier per node (64/65-bit datapaths, `GetMask` `by decide`
  side-goals on ~2093 nodes, MuxN/SRA/Sext), so the real per-node constant is
  larger — estimate **~2–5×** → ~0.3–0.7 s/node → **~25–55 min** for the rec
  suite, plus tree elaboration + `native_decide` well-formedness (seconds–minutes).
- **Reasonable window: ~30 min – 1.5 h**, linear and *finite*, versus the
  monolithic **47 h+ / never** (O(N²)).
- A clean **mini** run (defs + trees + wf + a slice of node-proofs) *after* the
  three fixes yields a real per-node number to tighten this before the full run.

## Fix / verification path (follow-up)

1. `OpBridge.lean`: add `sext_bridge_low`; add compare/mux constant-normalization
   lemmas (or reuse existing `BitVec.ofInt` simp lemmas).
2. `pass_lean.cpp`: Sext dispatch (`amount = wa` vs `amount = w ≤ wa`); extend the
   compare and MuxN closers with the normalization lemmas.
3. Regenerate (or transformer-patch) → re-run the **mini** check to 0 errors →
   run the full 4772 data-tree bridge; expect completion in the window above.

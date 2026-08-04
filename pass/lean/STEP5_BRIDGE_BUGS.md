# Step-5 fast-view bridge — proof bugs surfaced at DINO scale

**Status:** the emitter generates a *sorry-free* step-5 bridge for DINO
`SingleCycleCPU` (4772 nodes).  For a long time it was **never verified to
completion** — the single-file check was O(N²) twice over (graph lookups, then the
combiner) and every run was killed unfinished (worst: **2 d 8 h / 94 GB**).  Each
time the check got fast enough to reach further into the file, it exposed more
real proof bugs.  **"0 sorries" ≠ "typechecks".**

This document records **8 bugs** found this way, the fix for each, and the
measured cost model.  Bugs 1–3 and 5 are constant-spelling / side-condition
issues in the op bridges; 6–8 are structural (combiner shape, source-driven
outputs, an asymmetric closer).  All fixes are in the emitter
(`pass_lean.cpp`) plus `OpBridge.lean` / `GraphRefine.lean`.

**RESOLVED.** With all 8 fixes in, the emitter-generated DINO bridge typechecks
end-to-end: **`exit 0`, 0 errors, 0 sorries, 1391 s (23.2 min), 13.3 GB** at 8
cores (only 14 benign linter warnings from the Sext `first`-dispatch).  All three
theorems — `_comb_refines_fast`, `_next_refines_fast`, `_step_refines_fast` — are
proven for all 4772 nodes.

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

## Bug 4 — all-ones mask constant spelled two ways (and the systematic fix)

- **Symptom:** `bvenc (1#64 <<< 64 - 1#64 &&& fv4052) = bvenc (BitVec.ofInt 64 18446744073709551615 &&& fv4052)`
  — same value, two spellings, unprovable by the closer.
- **Root cause:** `lit_const_at` special-cased LiveHD's `-1` zext-sentinel mask as
  the shift form `(1#w <<< w) - 1#w`, while the certificate leaf used the decimal
  `BitVec.ofInt w (2^w-1)` from `int_of_const`.
- **Fix (systematic, kills Bugs 2/3/4 as a class):** `lit_const_at` now
  **delegates its integer literal to `int_of_const`** — the very function the cert
  leaf uses — so fast and cert are textually identical for *every* constant form by
  construction (zero, `-1`, i64, mask, bignum).  Value-preserving, so the LEC gate
  (which proves RTL ≡ **LGraph**, upstream of Lean text) is unaffected.
- **Regression guard:** `pass/lean/scripts/const_parity.py` asserts, per node and
  per const dep, that the cert leaf spelling appears verbatim in the consumer's
  fast body.  It flagged **4401 of 4772** nodes before the fix and **0** after, in
  seconds — replacing a ~3 h discovery run.

---

## Bug 5 — source-fact proofs assumed the old `sourceEnv` shape

- **Symptom:** after switching `sourceEnv` to `BT.find`-based lookup, all 4438
  `<Top>_src<id>` facts failed with `unsolved goals`; the resulting failing-`simp`
  storm made a run crawl for hours.
- **Root cause:** the proofs were `simp [<Top>_sourceEnv, bvenc]` /
  `simp only [<Top>_sourceEnv]; norm_num; exact mk_bv_ofInt _`, which no longer
  reduce once the lookup is `BT.find`.
- **Fix:** with the BT lookup the goals are definitional — emit **`rfl`** for
  input/flop sources and **`exact mk_bv_ofInt _`** for constants.
- **Lesson:** changing a shared definition invalidates every proof that pattern-
  matched on its old shape; grep for tactics naming that definition when you change it.

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

## ~~Runtime estimate~~ (SUPERSEDED — see "Corrected cost model" below)

The original estimate here extrapolated from synthetic sweeps (~139 ms/node ×
a guessed 2–5× real-op multiplier → "30 min – 1.5 h").  **It was wrong**, and the
way it was wrong is the lesson: the synthetic model captured the *representation*
exponent but none of the real constants, and it completely missed that a single
declaration (the combiner) could dominate everything.  Synthetic sweeps choose a
design; only timed slices of the real file predict a wall time.

---

## Bug 6 — the combiner emitted as an N-way case split

- **Where:** `<Top>_bridge_rec` emission in `pass_lean.cpp`.
- **Symptom:** one theorem of 4,778 lines whose `rcases` pattern alone is 19,115
  characters; the full-file check ran **> 3 h 56 m without finishing** while every
  other section measured ~3 minutes.  Thread accounting showed **one thread with
  11,030 CPU-seconds and 68 sleeping** — the signature of a single stuck
  declaration (Lean parallelizes theorem bodies, so the rest had long finished).
- **Root cause:** `simp only [...] at hn` turns `n ∈ topo` into an N-way
  disjunction; `rcases` then builds an N-deep `Or.casesOn` tree in which each
  level's motive carries the remaining disjuncts (O(N²)), and every branch
  `subst`s and re-checks the goal.
- **Fix:** emit the combiner in **term mode** as a right fold over
  `List.forall_mem_cons` — no case analysis, no goal substitution, and each
  level's tail is a shared suffix subterm.  4,778 emitted lines → 2.
  Cost: **> 3 h 56 m (unfinished) → ~127 s**.

## Bug 7 — outputs / flop dins driven directly by a SOURCE

- **Where:** the `_comb_refines_fast` output loop and the `_next_refines_fast`
  flop-din loop.
- **Symptom:** `decide` proved `1000004401 ∈ <Top>_graphCert.topo` **false**, and
  `Unknown identifier <Top>_fv1000004401`.
- **Root cause:** an output (or flop din) can be driven **directly by a constant,
  input, or flop** rather than by a computed node.  Its cert id is then a *source*
  id, not a topo node: there is no `_fv<id>` and `hb <id>` does not apply.  The
  output loop had no topo guard at all (the din loop had one, but only emitted a
  `TODO` + `sorry`).
- **Fix:** new `GraphRefine.evalGraph_not_mem` (`evalGraph` only updates ids in the
  topo list, so an off-topo id reads through to the source env), and both loops now
  emit `rw [GraphRefine.evalGraph_not_mem …, <Top>_src<id> …]` for source-driven
  ids.  The `sorry` placeholder is gone.
- **Why it was not caught earlier — a testing gap, worth internalizing:**
  1. It lives in the **tail** (last theorems of the file).  Every earlier full run
     was O(N²) and never reached it; the first run that *completed* found it.
  2. Every probe **truncated before the tail** — including the one that reported
     "1516 s, exit 0, 0 errors", which was built as
     `lines[:combiner] + new_combiner + end` and therefore silently dropped
     `bridge_src` and all three `_refines_fast` theorems.
  3. `add2` and the 1-flop design **do not contain the feature** (their outputs are
     driven by computed nodes), so the cheap ladder could not see it.
  → Ladder now needs a design with an output/din tied directly to a constant, and
  probes must state what they exclude.  "0 sorries + static gates pass" is not
  "typechecks"; only a full-file exit 0 is.

## Bug 8 — `_comb` closer missing the widening lemma

- **Symptom:** residual goal `bv_zext x = bv_to_bitvec 64 (bvenc x)` for outputs
  wider than their driver.
- **Root cause:** the `_comb_refines_fast` closer used
  `simp only [bv_to_bitvec_bvenc, bv_zext_id]` while the `_next` path already used
  the wider set including `bv_to_bitvec_bvenc_zext`.  Asymmetric closers.
- **Fix:** use the same closer in both paths.

---

## Corrected cost model (measured, not extrapolated)

| section | wall |
|---|---|
| head (defs + BT trees + 4438 source facts + wf) | 181 s |
| + 200 recurrence theorems | 171 s (marginal ≈ 0) |
| + all 4772 recurrence theorems | ~1457 s ← dominant |
| + combiner / bridge_src / refines | **1391 s = 23.2 min, 13.3 GB, exit 0** |

Note the per-node cost is **not uniform**: 200 theorems were free, 4772 cost ~21
minutes — the expensive ones are the wide `GetMask` nodes with `by decide` side
conditions (~2093 of them).  That is the next optimization target.

**Estimation rule learned here:** synthetic sweeps give the *exponent* (they told
us monolithic ≈ N^1.8 vs data-tree ≈ N^1.0, which chose the design); only timed
slices of the **real** file give the *constant*.  Estimating DINO from synthetic
alone was off by 10–50×.

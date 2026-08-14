# Step-5 fast-view bridge — proof bugs surfaced at DINO scale

**Status:** the emitter generates a *sorry-free* step-5 bridge for DINO
`SingleCycleCPU` (4772 nodes).  For a long time it was **never verified to
completion** — the single-file check was O(N²) twice over (graph lookups, then the
combiner) and every run was killed unfinished (worst: **2 d 8 h / 94 GB**).  Each
time the check got fast enough to reach further into the file, it exposed more
real proof bugs.  **"0 sorries" ≠ "typechecks".**

This document records **11 bugs** found this way, the fix for each, and the
measured cost model.  Bugs 1–3 and 5 are constant-spelling / side-condition
issues in the op bridges; 6–8 are structural (combiner shape, source-driven
outputs, an asymmetric closer); 9 is an arity-dispatch gap that only bites at
scale.  All fixes are in the emitter (`pass_lean.cpp`) plus `OpBridge.lean` /
`GraphRefine.lean`.

**Bugs 10 and 11 came from CVA6, not DINO.**  Bug 11 is another proof-side gap
(the flop enable/reset were never rewritten).  **Bug 10 is different in kind**: bugs 1–9 are all
defects in the *proof*, while 10 is a defect in the *model* — the fast view
zero-extended a widening arithmetic shift, so it computed a different value from
the design.  It was caught only because the certificate disagreed, which is the
step-5 bridge working as a **differential test between two independent
translations of the same graph** rather than merely as a proof obligation.

**RESOLVED (DINO).** With bugs 1–9 fixed, **all three DINO variants** typecheck
end-to-end — `exit 0`, 0 errors, 0 sorries, `_comb`/`_next`/`_step` all proven.
Bug 10 does not affect them: it needs a *widening* SRA, and all 714 of
`SingleCycleCPU`'s SRA nodes are truncating (re-confirmed on a freshly emitted
file), so its fix leaves their emitted text byte-for-byte unchanged by design.

| design | nodes | wall | peak RSS |
|---|---|---|---|
| `SingleCycleCPU` | 4,772 | 25.2 min (2 cores) | 13.3 GB |
| `PipelinedCPU` | 5,061 | 26.8 min (2 cores) | 14.9 GB |
| `PipelinedDualIssueCPU` | 10,740 | 57.4 min (4 cores) | 29.6 GB |

(The only log output is benign linter warnings from the Sext `first`-dispatch.)

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
minutes ⇒ **≈280 ms per recurrence theorem**.

**Correction (measured 2026-08-04).**  This document previously blamed the wide
`GetMask` `by decide` side conditions (~2093 of them) and called them the next
optimization target.  An isolated benchmark says otherwise: that `decide` costs
19.5 / 38 / 66 / 146 ms per node at mask width 65 / 129 / 257 / 513 — i.e.
**linear** in mask width (≈0.29 ms/bit), not quadratic — which totals only ≈20 s
across DINO's entire GetMask population, ~1.5 % of the 1391 s run.  The
closed-form all-ones replacement (`mask_indices_length_ofInt_neg_one`) is
width-independent and free, so it is worth keeping — it removes the only
width-dependent term in the per-node proof, which matters as designs widen — but
it is **not** the lever.  The ~280 ms/node lives in the rest of the per-node proof
(the `show` defeq resolution and the per-node `rw` / `simp only [fv, closer]`).
Measured table: `README.md`, "Where the per-node 280 ms is NOT going".

**Lesson, a second instance of the one below.**  The superseded runtime estimate
was an unmeasured *magnitude*; this was an unmeasured *attribution*.  Same rule:
benchmark the component in isolation before naming it the bottleneck.

**Estimation rule learned here:** synthetic sweeps give the *exponent* (they told
us monolithic ≈ N^1.8 vs data-tree ≈ N^1.0, which chose the design); only timed
slices of the **real** file give the *constant*.  Estimating DINO from synthetic
alone was off by 10–50×.

---

## Bug 9 — n-ary `Or` closer sends the kernel into unbounded recursion

- **Symptom:** `PipelinedDualIssueCPU_rec3632` (`Op_Or`, width 1, deps
  `[12976, 12980]`) fails with `(kernel) deep recursion detected`.  Exactly **1 of
  10,740** nodes; the other 10,739 and the whole tail prove fine.
- **Root cause:** `Op_Or` was the **only** operator the emitter dispatched without
  an arity guard.  `Op_And`, `Op_Xor` and `Op_Sum 2` all fall back to their binary
  bridge at `deps.size() == 2`; `Op_Or` always used the n-ary `orn_bv_bridge`,
  whose closer must unfold a `List.foldl`
  (`List.foldl_cons/nil`, `bv_to_bitvec_bvenc_zext`, `BitVec.zero_or`).  That
  unfolding rewrites through the operand terms, and when the operands are
  themselves computed nodes several `fv` levels deep the kernel recursion is
  **unbounded**.
- **Discriminator that cracked it:** node `3624` is structurally identical
  (`Op_Or`, out_w 1, operands 2 and 2) and **passes**.  The difference is operand
  depth, not shape:
  ```
  fv3624 PASSES: fv12968 = sem_get_mask s.st_pipeA_ex_mem_x2e_reg_taken ..   -- chain depth 1
  fv3632 FAILS : fv12976 = sem_get_mask (fv3576 i s) ..                      -- chain depth 5
  ```
- **Fix:** dispatch binary `Or` to the existing fold-free `or_bridge`, mirroring
  And/Xor/Sum.  Its RHS `bvenc (bv_zext a ||| bv_zext b)` matches the emitted fast
  def syntactically, so the default closer suffices and no fold is introduced.
  Genuinely n-ary `Or` keeps `orn_bv_bridge`.

### How it was found (four hypotheses falsified first)

| hypothesis | how it was killed |
|---|---|
| the 10,740-deep term-fold combiner | error at line 68,067; combiner at 105,001 — and the combiner **typechecked fine**, retiring that scale risk |
| `phiTree` depth | depth 16 and balanced; if it were the cause **all** nodes would fail, not one |
| kernel **stack size** | `--tstack=262144` (256 MB, 32× default) fails identically ⇒ recursion is *unbounded*, not merely deep.  (`maxRecDepth` is irrelevant: it governs the elaborator, this is the kernel.) |
| the op/width shape | 526 `Op_Or` nodes, 482 of them narrowing; only this one fails |

Then **staged execution** of the same proof localized the tactic — each variant is
the identical file with the proof truncated:

| variant | proof | result |
|---|---|---|
| A | `show` + `sorry` | 717 s, **passes** |
| B | `show; rw [orn_bv_bridge]` + `sorry` | 1010 s, **passes** |
| C | full (`+ closer simp only [...]`) | **deep recursion** |
| D | `show; rw [or_bridge]; simp` (the fix) | **663 s, passes** |

So the `show` defeq and the bridge rewrite are both innocent; the closer is the
culprit — and the fold-free proof is also **34 % faster** than the failing one.

### Measured effect

The fix moves 610 nodes across the three DINO designs off the fold
(134 / 142 / 334 for SingleCycle / Pipelined / DualIssue) and makes them cheaper:

| design | before fix | after fix |
|---|---|---|
| SingleCycleCPU | 23.2 min (8 cores) | 25.2 min (**2 cores**), exit 0 |
| PipelinedCPU | 27.7 min (4 cores) | 26.8 min (**2 cores**), exit 0 |

Both re-proved with no regression on a quarter/half the cores.

**Lesson:** an n-ary bridge that is *correct* at arity 2 can still be *unusable* at
arity 2, because its closer has to unfold machinery the binary bridge never
introduces.  Give every n-ary op a binary fast path, and audit the dispatch table
for ops that lack one.

---

## Bug 10 — widening `SRA` zero-extends in the fast model (a real semantic divergence)

**First bug found on CVA6 rather than DINO, and the first that is a mistranslation
rather than a proof-engineering gap.**

- **Symptom:** `cva6_alu_export_rec24564` (`Op_SRA`, width 192, operand `fv24616`
  65 bits) fails with
  ```
  error: Tactic `decide` proved that the proposition
    192 ≤ 65
  is false
  ```
  **24 of 6,305** nodes in `cva6_alu_export`: 16 at `192 > 65`, 8 at `576 > 65`.

- **Root cause — the FAST MODEL, not the certificate.**  The two models widen an
  arithmetic shift differently, and only agree while the result is *truncated*:

  | | expression | how it widens |
  |---|---|---|
  | fast (`emit_node_expr`) | `bv_zext (sem_sra a b) : BitVec w` | `sem_sra` shifts at the **operand** width `vw`, then **zero**-fills up to `w` |
  | cert (`eval_op`) | `bv_sra w a b` = `mk_bv w (bv_sint a / 2^amt)` | reads `a` **signed**, so `mk_bv w` of a negative value **sign**-fills |

  For `w ≤ vw` both keep the low `w` bits (`toNat % 2^w` = `toInt % 2^w` there), so
  they coincide.  For `w > vw` with a negative operand they differ — zeros vs ones
  in the extension.  Committed as a `native_decide` example in `OpBridge.lean`:
  operand all-ones at 65 bits, shift 0 → `bv_zext` to 192 gives `2^65 - 1`,
  `bv_sext` gives `2^192 - 1`.

- **Which side is wrong:** the fast model.  `inou/cgen/cgen_sim.cpp` — LiveHD's own
  simulator, and the artifact the LEC gate proves equivalent to the RTL — reads an
  arithmetic shift's operand as **signed**
  (`operand(e[0].driver, wbits, /*signed=*/1)`, commented "The shifted operand of an
  ARITHMETIC shift must be read as signed") and materializes the result in
  `Slop<tw>` at the **target** width.  That is sign-extension.  An arithmetic right
  shift exists to preserve sign; widening its result with zeros discards exactly
  that.  So the certificate was faithful and `bv_zext` was the defect: at those 24
  nodes the generated Lean model computes a different value from the design whenever
  the operand is negative.

  Scope: the bug is in the **generated Lean model**, not in LiveHD's RTL path or
  simulator — nothing downstream of `cgen_sim` is affected.  But anything concluded
  by executing or reasoning about that Lean model at those nodes would have been.

- **Fix:** emit `bv_sext` instead of `bv_zext` when `w > vw`, and add
  `OpBridge.sra_bridge_sext`, which needs **no side condition at all** (
  `mk_bv w V = bvenc (BitVec.ofInt w V)` holds at every width).  The bridge
  dispatch mirrors the same `w > width_of(dep0)` test.
  - The **truncating** case keeps `bv_zext` and `sra_bridge _ _ (by decide)`
    *verbatim*, so every already-verified design's emitted text is unchanged and its
    proof stands — deliberately, to avoid invalidating three green DINO runs for a
    case they do not contain.

- **Why DINO could not catch it:** **714/714** of `SingleCycleCPU`'s SRA nodes are
  truncating (`w ≤ vw`) — re-confirmed on a *freshly emitted* file under current
  cprop, not the stale artifact, precisely because the "DINO unaffected" claim
  depends on it.  In that regime zext and sext coincide, the wrong extension is
  invisible, and `sra_bridge`'s `w ≤ wa` hypothesis is always true.  A widening SRA
  is required to observe it, and no DINO variant has one — which is also why all
  three verified.

  SRA is uniquely exposed: it is the only operator that takes a **signed** operand
  and then widens it to the node width.  `Sext` widens too but handles sign
  explicitly (and has its own two bridges); `SLT`/`SGT` read signed but emit 1 bit,
  so there is no widening to get wrong.

### The second, separate defect: an unchecked `by decide`

`sra_bridge`'s `w ≤ wa` hypothesis was **honest** — it documented exactly the domain
where the two models agree.  The emitter, though, discharged it with an
unconditional `(by decide)`.  So outside that domain the symptom was
`decide proved 192 ≤ 65 is false` **~30 minutes into a run**, instead of a clear
diagnostic at emit time.

Name the anti-pattern: **never emit `by decide` for a lemma hypothesis the emitter
has not itself checked can hold.**  Either check it and dispatch (what Sext already
does, and what SRA now does), or emit a flagged `sorry` so the static gate catches
it.  `pass/lean/scripts/op_census.py` now reports the SRA arm **per node**
(`sra_bridge (truncating)` vs `sra_bridge_sext (widening, 192>65)`), so a width
regression is a seconds-long static check rather than a wasted run.

That per-node reporting also fixed a fidelity bug in the census itself: it had been
caching dispatch status per `(op, arity)`, which for a mixed population reports
whichever node came first and can **hide a failing node behind a passing twin**.
It now evaluates per node and prints one row per `(op, arity, dispatch)` — which is
how the 864-truncating / 16+8-widening split became visible at all.

### Back-port needed

`pass/isabelle/pass_isabelle.cpp:2209` emits
`((ucast (sem_sra <a> <amt>) :: <w> word))`.  `ucast` is zero-extend, so
**`pass.isabelle` carries the identical bug** and needs `scast` when
`w > value_w`.  (Third shared latent bug the Lean bridge has surfaced for
`pass.isabelle`; see the README's memory-decode notes for the earlier two.)

**Lesson:** the step-5 bridge is not only a proof obligation — it is a
**differential test between two independent translations of the same graph**.  Bugs
1–9 were all defects in the *proof*; this one was a defect in the *model*, caught
only because the certificate disagreed.  When fast and cert disagree, first decide
which one matches LiveHD's own simulator (`cgen_sim.cpp`) before reaching for a new
lemma — the temptation here was to add a companion lemma for `192 ≤ 65` and move on,
which would have *proven the wrong model correct*.

---

## Bug 11 — `_next_refines_fast` never rewrote the flop ENABLE / RESET

- **Symptom:** `cva6_tlb_gate` fails after **5 h 12 min** with a single
  `unsolved goals` in `cva6_tlb_gate_next_refines_fast`.  In the residual goal the
  flop *dins* are correctly rewritten (`fv10392` on both sides) but the *enable* is
  not:

  | side | enable term |
  |---|---|
  | fast | `bitvec_nonzero (cva6_tlb_gate_fv996 i s)` |
  | cert | `bv_nonzero (evalGraph … 996)` |

- **Root cause:** `nextStateFromCert` reads **three** cert ids per flop —
  ```
  din_e   = (bv_to_bitvec w (rho <din_id>))
  reset_e = (bv_nonzero (rho <reset_id>))
  en_e    = (bv_nonzero (rho <enable_id>))
  ```
  but the `_next_refines_fast` proof loop consulted only `flop_din_cert_ids`.
  `flop_reset_cert_ids` / `flop_enable_cert_ids` were populated and used on the
  **cert** side, then ignored by the **proof** side, so those `evalGraph` terms had
  nothing to rewrite them to the fast `fv` form.

- **Fix:** iterate all three maps per flop, **deduplicating** ids, and add
  `bv_nonzero_bvenc` to the closer.
  - Dedup is not a nicety: `rw` rewrites *every* occurrence of a pattern at once, so
    a second `rw` for an already-rewritten id fails with "did not find instance of
    pattern".  With one enable shared across 137 flops that is the normal case.
  - `bv_nonzero_bvenc` (`bv_nonzero (bvenc x) = bitvec_nonzero x`) already existed in
    `OpBridge.lean`.  This was **plumbing, not a missing proof** — unlike Bug 10,
    where the model itself was wrong.
  - Result: 138 rewrites emitted for the TLB (137 dins + the shared enable 996).

- **Why DINO could not catch it:** DINO's flops have **no reset or enable pin**, so
  the emitter writes the literals `false` / `true` on both sides — there is nothing
  to rewrite and the omission is unobservable.  `cva6_tlb_gate` has 137 flops all
  sharing one *computed* enable (node 996), so it fails on the first attempt.  Same
  shape as Bug 10: a gap only a design carrying the relevant feature can expose.

- **Verified:** `cva6_tlb_gate` now typechecks end-to-end — `exit 0`, 0 errors,
  0 sorries, `_comb`/`_next`/`_step` all proven, 18,750 s / 12.2 GB.

### Cheap localization: stub ONE declaration, re-time

Finding this cost a full 5-hour run because the bug sits in the last ~1 % of the file,
behind the expensive 99 %.  The cheap confirmation was to re-run the *identical* file
with only the combiner's body replaced by `sorry`, which skips the combiner and still
elaborates all five tail theorems:

| variant | wall | verdict |
|---|---|---|
| full file (pre-fix) | 18,731 s | `unsolved goals` in `_next_refines_fast` |
| combiner stubbed (post-fix) | 18,666 s | **exit 0** — fix confirmed |
| full file (post-fix) | 18,750 s | **exit 0**, 0 sorries — proven |

Two lessons.  (1) A stubbed declaration makes the file a **localizer, not a result** —
the stubbed obligation is *assumed*, so a green probe never means "proven".  State
what a probe assumes, every time.  (2) The same three numbers incidentally measured
the combiner at **~65 s (0.3 %)**, refuting the standing assumption that it is the
scaling risk at CVA6 sizes.  Stub-one-declaration is cheaper than cumulative-slice
bisection and answers the same question.

# pass.isabelle certificate-equivalence bridge — bug log

The Isabelle counterpart of `pass/lean/STEP5_BRIDGE_BUGS.md`, written **as bugs are
found**, not after.

Goal: prove `<Top>_comb / _next / _step` (the fast `'w word` model) equal to the
evaluated graph certificate, for DINO `SingleCycleCPU`. Background and the full plan
live alongside `pass/lean/README.md`, `pass/lean/STEP5_BRIDGE_BUGS.md`, and
`.claude/skills/prove-cert-equivalence/SKILL.md`.

---

## The dominant bug class: fast/cert operand-width disagreement

Every certificate dep is materialized at an explicit width via
`cert_dep_id(ctx, build, pin, W_cert)`. The fast model independently casts the same
operand via `ucast_pin_at(ctx, pin, W_fast)` / `driver_expr_at` /
`shift_amount_expr_at`. **If `W_cert != W_fast` for any operand, the two models
evaluate different values and no bridge lemma can ever close that node.**

Nothing checks this today, and nothing *could* have noticed it before the bridge
existed: `pass/isabelle/TODO:43-48` records the general trap — when the fast model and
the certificate share the same defect, `model = certificate` still holds and only the
LEC gate (RTL ≡ LGraph) can catch it. The corollary is the opposite failure: when they
diverge, nothing catches it until the bridge is attempted.

This is the Isabelle analogue of Lean's constant-spelling class (4 of its 8 bugs).
It differs in mechanism — Isabelle's fast literals are `'w word` terms and its
certificate literals are `int`, so they can never be *textually* identical the way
Lean's `BitVec` ones were made to be. The invariant to enforce is not textual
identity but **width agreement, operand by operand**.

### Audit of every op arm (2026-08-06)

`cert_node_expr` vs `emit_node_expr`, operand by operand:

| op | fast operand width | cert dep width | verdict |
|---|---|---|---|
| `Sum` | `w` | `w` | ok |
| `Mult` | `w` | `w` | ok |
| `Div` | `w`, `w` | `w`, `w` | ok |
| `And`/`Or`/`Xor` | `w` | `dep_w = w` | ok |
| `Ror` | **`pin_width(driver)`** per operand | **`dep_w = w` (= 1)** | **BUG 4** |
| `Not` | `w` | `w` | ok |
| `LT`/`GT` | `cmp_w = max(wa,wb)` | `cmp_w` | ok |
| `EQ` | `eq_w = max` over drivers | `dep_w = max(1, max)` | ok (`pin_width >= 1`) |
| `SHL` | a `w`; b widened by `shift_amount_expr_at` | a `w`; b **`pin_width`** | **BUG 3** |
| `SRA` | a `pin_width(a)`; b widened | a `pin_width(a)`; b widened | ok |
| `Mux` | sel `sel_w`; options `w` | sel `sel_w`; options `w` | ok |
| `Sext` | a `pin_width(a)`; b **`pin_width`** | a `pin_width(a)`; b **`pin_width`** | **BUG 2** |
| `Get_mask` | a `src_w`; mask `max(pin_w, src_w)` | a `gm_src_w`; mask `gm_mask_w` | ok (BUG 1, already fixed) |
| `Set_mask` | a `w`; mask/value `pin_width` | a `w`; mask/value `pin_width` | ok — but see note |

---

## Bug 1 — `Get_mask(a, -1)` all-ones mask at 1 bit — **already fixed in HEAD**

`sem_get_mask x m` scans `[0..<LENGTH('m)]`, so a 1-bit mask selects only bit 0.
LiveHD's canonical zext idiom is `Get_mask(a, -1)` (`pass/cprop/cprop.cpp:852`), and
the front-end collapses the `-1` sentinel's pin width to 1.

Full analysis with a worked `add2` example (`a=5, b=3` yielded `y=2` instead of `8`)
is in `bug_fix/Get_mask in pass.Isabelle` and `pass/isabelle/TODO`.

**Status: fixed before this effort began.** Both sides widen to
`max(pin_width(mask), src_w)`. Verified present in the current tree at the fast site
(`emit_node_expr`, `Get_mask` arm) and the cert site (`cert_node_expr`, `Get_mask` arm).

Note for future readers: `generated/dino_lgraph_isabelle_livehd_new_20260605/` predates
the fix, so **all 3710 of its `sem_get_mask` sites still show a 1-bit `-1` mask**. That
artifact is not evidence about the current emitter — regenerate before drawing
conclusions from it.

## Bug 2 — `Sext` amount truncated to its pin width

- **Where:** `emit_node_expr` `Sext` arm (fast) and `cert_node_expr` `Sext` arm (cert).
- **Symptom:** the fast model emitted
  `word_of_int (signed_take_bit (unat ((32 :: 1 word))) (uint …))`. `unat (32 :: 1 word)`
  is **0**, so `signed_take_bit 0` silently turns the sign-extend into a no-op. The
  certificate independently materialized the amount dep at the same 1-bit pin width,
  so `mk_bv 1 32 = 0` there too.
- **Root cause:** the sign position is a *value*, not a bit slice, but both sides used
  `pin_width(b)`. `SHL`/`SRA` already had the widening treatment
  (`shift_amount_expr_at` + `minimal_unsigned_const_width`); `Sext` was missed.
- **Fix:** widen a constant amount to `max(pin_width, minimal_unsigned_const_width(v))`
  on **both** sides, and switch the fast side from `driver_expr_at` to
  `shift_amount_expr_at`.
- **Provenance:** the fast half of this fix exists on the unmerged branch
  `fix/isabelle-get-mask-sext-width` (commit `396845fea`); `git merge-base
  --is-ancestor` confirms it never reached this branch. Reapplied here, plus the cert
  half.

## Bug 3 — `SHL` shift amount widened in the fast model but not in the certificate

- **Where:** `cert_node_expr` `SHL` arm.
- **Symptom:** the fast model calls `shift_amount_expr_at(ctx, b, bw)`, which widens a
  **constant** amount internally to `max(bw, minimal_unsigned_const_width(v))`. The
  certificate pushed `cert_dep_id(..., pin_width(ctx, b, node))` — unwidened. For any
  constant shift amount that does not fit its pin width, cert and fast read different
  amounts.
- **Why it survived:** `SRA` got the widening in its cert arm and `SHL` did not. The
  asymmetry is invisible without a bridge, exactly like Lean's asymmetric-closer Bug 8.
- **Fix:** mirror `shift_amount_expr_at`'s widening in the `SHL` cert arm.

## Bug 4 — `Ror` certificate deps truncated to 1 bit

- **Where:** `cert_node_expr`, the shared `And/Or/Xor/Ror/EQ` arm.
- **Symptom:** the arm computed one `dep_w` for all five ops (`dep_w = w`, overridden to
  the widest operand only for `EQ`) and materialized every dep at it. A `Ror` node has
  **`w = 1`** (reduce-OR; `simple_op_cert_wf_bool` in `Translation_LGraph_Model.thy`
  even asserts `w = 1`), so every operand was truncated to its bit 0.
- **Concrete divergence:** operand `= 0b10`. Fast model emits
  `(driver_expr_at … ew) \<noteq> 0` at the operand's **full** width → `0b10 ≠ 0` =
  `True`. Certificate materializes the dep at width 1 → `0b0`, and
  `denote_op Op_Ror` computes `list_ex bv_nonzero [BV 1 0]` = `False`. **Opposite
  results** for any operand whose set bits are all above bit 0.
- **Root cause:** `Ror` is the one op in that group whose operand width is unrelated to
  its output width; folding it into the shared `dep_w` was wrong.
- **Fix:** in the `Ror` case use `pin_width(ctx, e.driver, node)` per operand, matching
  the fast model.

---

## Open / noted, not yet acted on

- **`Set_mask` and the `-1` sentinel.** Fast and cert agree (both use
  `pin_width(mask)`), so the *bridge* is provable. But if LiveHD emits
  `Set_mask(a, -1, v)` the same 1-bit-sentinel reasoning as Bug 1 applies and the
  emitted semantics would be wrong against RTL. That is an LEC-gate concern, not a
  certificate-equivalence one. Confirm whether DINO contains any `Set_mask` node before
  spending effort here.
- **`Rem` and `Clock_cell` have no certificate op** and are absent from both
  `cert_node_expr` and `emit_node_expr` switches, so they fall through to
  `default: throw Emit_error("internal: unhandled Ntype_op …")`. They do not occur in
  DINO, but this should become a clean unsupported-op diagnostic rather than an
  "internal" error.
- **`<T>_cert_all_ids_distinct` is a permanent `sorry`** (chunked mode), so today's
  `graph_cert_wf` is `sorry`-tainted even on a "successful" run.

## Guard to build (Phase 5)

A `const_parity.py` analogue for Isabelle should not check textual spelling (the two
sides are different types) but **width agreement**: for every node and every operand,
the width used in the fast body must equal the width the certificate dep was
materialized at. That is a static, seconds-long check, and it is exactly the invariant
all four bugs above violated.

---

## Measured baseline: DINO SingleCycleCPU after the Phase 0 fixes (2026-08-06)

Regenerated with the fixed emitter and built via
`scripts/run_dino_lgraph_isabelle.sh`, Isabelle2025-2, `threads=8`,
`parallel_proofs=1`.  Both sessions **exit 0**.

| session | wall | cpu | factor |
|---|---|---|---|
| `DINO-Lgraph-SingleCycle-Model` | 7 m 24 s | 23 m 02 s | 3.11 |
| `DINO-Lgraph-SingleCycle-Cert`  | 4 m 56 s | 8 m 54 s  | 1.81 |

Certificate: 4772 nodes.  Op census: GetMask 2093, And 803, SRA 714, SHL 644,
Const 204, EQ 172, MuxN 150, Or 111, Ror 27, Not 26, Sext 10, Sum 7, MuxBool 6,
ULT 4, SLT 4, Xor 1.

**Where the time goes** (from `isabelle build -v`, max observed per command):

| command | line | theory | seconds |
|---|---|---|---|
| `definition` | 123  | `SingleCycleCPU_comb` | **107** |
| `definition` | 4900 | `SingleCycleCPU_next` | **83** |
| `local_setup` | 155 | — | 35 |
| `export_code` | 1406 | — | 21 |

Two monolithic `definition` commands account for ~190 s of the model build.
Each is a *single declaration*, so each is inherently single-threaded no matter
what `threads` is set to — the 3.11 parallelism factor comes from everything
else.  This is the quantified form of the known defect: `emit_let_chain` puts
the entire graph in one `let`, and `_next` re-emits the whole chain.  Splitting
node values into per-node `definition`s removes both the cost and the
serialization, and is a prerequisite for the bridge anyway (the per-node
recurrence lemmas need to name each node's value).

For contrast, the pre-fix 2026-06-05 run measured `Model` at 3 m 50 s with
`parallel_proofs = 0`, `threads = 1`, factor 1.00.  The wall time rose because
the Get_mask masks are now materialized at the source width instead of 1 bit, so
the terms are genuinely larger — that is the fix working, not a regression.

---

## Bug 5 — `ucast_eq` in a simp set does not terminate (proof-engineering, not emitter)

Not an emitter bug, but it cost a session and the diagnosis is reusable.

- **Symptom:** `bvenc_ucast` — the identity *fast `ucast` = certificate
  `bv_resize`*, which every width-polymorphic bridge lemma rewrites with —
  never finished. Killed at 400 s in a session that otherwise builds in ~35 s.
  A looping `simp` presents as a **hang**, not a failure.
- **Measured**, with each candidate tactic run under an ML `Timeout.apply` so one
  hang could not block the rest:

  | goal / tactic | result |
  |---|---|
  | `ucast x = word_of_int (uint x)` by `rule ucast_eq` | OK |
  | same by `simp add: ucast_eq` | **TIMEOUT** |
  | same by **plain `simp`** (no `ucast_eq`) | OK |
  | `bvenc (ucast x) = bv_resize _ (bvenc x)`, full set incl. `ucast_eq` | **TIMEOUT** |
  | same set **minus** `ucast_eq` | FAIL (residual goal — not a hang) |

- **Root cause:** `ucast`, `uint` and `unat` are all abbreviations for the *same*
  constant `unsigned`, differing only in the result type. So
  `ucast_eq : ucast ?w = word_of_int (uint ?w)` is really
  `unsigned ?w = word_of_int (unsigned ?w)` — a rewrite whose right-hand side
  re-introduces the head constant its left-hand side matches, with the result
  type schematic. The default simp set also carries
  `word_of_int_uint : word_of_int (uint ?w) = ?w`, which rewrites the other way.
- **Fix:** use `unsigned_ucast_eq : unsigned (ucast ?w) = take_bit LENGTH('c) (unsigned ?w)`.
  It states the composite directly, so its RHS does not re-introduce a `ucast`
  under an `unsigned`. Proof becomes three small calculational steps and checks
  in ~2 s.
- **Second trap, same root cause:** `find_theorems` on the pattern
  `uint (ucast _)` returns **zero** matches — because both are `unsigned`, the
  pattern does not mean what it appears to mean. The lemma has to be found by
  **name** (`Find_Theorems.Name "unsigned_ucast"`), not by pattern.
- **Third trap:** `rule and2_bridge` fails against a goal with a *numeral* width
  (`bv_bitwise 5 ...`) because `LENGTH(?'w)` cannot unify with the literal `5`.
  State per-node lemmas with symbolic `LENGTH('w)`, which is the shape the
  emitter needs anyway.

**Method note.** The first bisection attempt measured nothing for 10 minutes
because the probe session's *parent* still listed the hanging theory in its ROOT.
Remove a suspect theory from ROOT before bisecting, and prefer an ML harness with
`Timeout.apply` over one-tactic-per-build: it tests every candidate in a single
~6 s run and cannot be blocked by a hang. Note also that `writeln` from an ML
block does **not** surface in a batch `isabelle build` log — use `error` to force
diagnostic output out.

---

## Piece A complete (2026-08-06)

`Translation_OpBridge.thy`: 51 lemmas, no `sorry`/`oops`, session builds in 32 s.
`op_census.py` reports **100 % of 4912 DINO SingleCycleCPU certificate nodes,
25/25 (op, arity) pairs**.

Two side conditions are deliberate and gated, not oversights:

- **`sext_bridge_eq_width` requires `amount = out_width`.** The certificate keeps
  `n` bits (sign bit at `n-1`); the fast model's `signed_take_bit n` keeps `n+1`.
  They agree only because the result is truncated to the output width. Every DINO
  `Sext` satisfies it (measured: amount 32/64/1 against out_w 32/64/1), and the
  census gate hard-FAILs on any that does not — so a widening `Sext` becomes a
  build failure rather than a silent model/certificate divergence.
- **`sra_bridge` requires `out_width \<le> operand_width`.** `sem_sra` returns a word
  of the *operand's* width and the fast model then `ucast`s, so a wider output
  would zero-extend where the certificate sign-extends.

**`GetMask` was cheaper here than in Lean.** The bridge is symbolic in the mask,
so `mask_indices` is never evaluated in a per-node proof — unlike Lean, where the
generic path ran a `by decide` per node and needed a closed-form all-ones fast
path to stay affordable. No such special case is needed in Isabelle.

### Proof-engineering notes

- `is` is an Isar keyword. `(induct "is")` does not parse, and the resulting
  *syntax* errors are reported against later line numbers, which reads like a
  proof failure somewhere else. Name list variables `js`.
- A lemma whose only type-variable occurrence is inside `LENGTH('w)` needs the
  sort spelled out (`LENGTH('w::len)`); it cannot be inferred.
- `rule L[OF f0 nn]` on a lemma with a higher-order assumption
  (`\<And>p. ?f p False = p`) fails with `OF: multiple unifiers`. Instantiate the
  function explicitly: `rule L[where f = "(\<or>)"]`.
- Batching pays. Isabelle reports *every* failing proof in a theory, not just the
  first, so writing nine candidate lemmas and reading all the failures in one 6 s
  build beats one lemma per round. Batches 1 and 3 passed first try; 2 and 4 took
  three and four rounds, each round shrinking to a single named residual.

---

## Phase 1 — φ / lookup representation bake-off (measured 2026-08-06)

Harness: `pass/isabelle/scripts/bakeoff_gen.py`. Generates a synthetic chain of N
nodes (node k = `Op_Not`(node k-1), all 8-bit) and proves the real per-node shape
`phi k = eval_node G phi k` for each, which forces one `nodes` lookup plus one
`phi` lookup per dependency. The op cost is held constant so only the lookup
representation varies. Isabelle2025-2, `threads=4`, wall seconds.

| N | `flat` | `bt` | `eqns` |
|---|---|---|---|
| 100 | 56 | 15 | 14 |
| 200 | 450 | 28 | 26 |
| 400 | ≥900 (capped) | 78 | 69 |
| 800 | — | 294 | 249 |

Per-doubling exponent (marginal, minus ~5 s session startup):

| N→N | `flat` | `bt` | `eqns` |
|---|---|---|---|
| 100→200 | **3.13** | 1.20 | 1.22 |
| 200→400 | — | 1.67 | 1.61 |
| 400→800 | — | **1.99** | **1.93** |

- **`flat`** — `nodes_of_list` (= `map_of` over a list literal) plus a nested
  `if`-chain `phi`: **the shape the emitter produces today**, and it measures
  ≈ N³. Not merely slow — unusable. Extrapolating cubic to 4912 nodes is a
  refusal, not an estimate.
- **`bt`** — the direct port of Lean's fix (balanced `BT` datatype + recursive
  `bt_find`). Far better than `flat`, but the exponent climbs to ~2.
- **`eqns`** — `BT` definition, but N lookup rules (`phi_at_k`, `nodes_at_k`)
  derived **once**, so the per-node recurrence proofs never unfold a tree.
  ~15 % faster than `bt`, **asymptotically identical**.

### Why Lean's O(N) does not transfer

Lean's BT win is *not* about comparison count — Lean measured a balanced
`if`-**tree** as still O(N²). The win is that the tree is *data* navigated by
kernel **defeq** inside a `show`, so the O(N) tree term never enters the goal.
Isabelle's `simp` must rewrite with `phi_tree_def` to reduce `bt_find`, putting
the whole tree literal into each goal. `eqns` relocates that cost (one unfold per
derived rule, O(N²) to establish, O(1) to use) rather than removing it.

Conclusion: **~N² is the floor for a `simp`-based per-node proof here.**
Extrapolating `eqns` from N=800 at N² gives ≈ 2.5 h at 4912 nodes for this
synthetic; the real design will be a multiple of that.

### Dead ends, established

- **`fun` with N numeral equations is impossible.** Isabelle rejects it:
  *"Non-constructor pattern not allowed in sequential mode."* Numerals are not
  constructor patterns, so the "make the lookup rules free by making them the
  definition" route does not exist.
- Lean's defeq-in-a-`show` resolution has no Isabelle analogue (above).

### Three matching traps, all silent

Each presents as "the proof doesn't close" with no hint at the cause:

1. **`ucast_eq` in a simp set does not terminate** (Bug 5 above).
2. **`[where 'w = 8]` silently fails to apply; `[where 'w = "8"]` works.** Type
   instantiation needs the quotes.
3. **`One_nat_def` denormalizes numerals.** `simp` rewrites `phi 1` into
   `phi (Suc 0)`, so a rule stated as `phi 1 = …` stops firing. Fix:
   `simp del: One_nat_def`. **Any** emitter strategy keyed on numeric node ids is
   exposed to this — build it into the emitted proofs from the start.

Also: `eval_op` unfolds via its own simp rules before an `eval_op`-level bridge
can match, so a per-node proof must use the lower-level bridge (`bv_not`, …) or
do the `eval_node` step with an explicit `rw`. This is the Isabelle counterpart of
Lean's "resolve deps by defeq in a `show`, never `simp [phi]`".

### Recommendation

Take **`eqns`**: same asymptotics as `bt`, better constant, and its per-node
proofs are plain rule applications, which are far easier to emit and debug than
tree-navigation `simp` calls.

**Next lever to measure before committing to a wall-clock budget: chunking.** If
the quadratic is driven by per-theory context size, splitting the N recurrence
lemmas across K theories should give ≈ N²/K. Isabelle's session/theory structure
makes that natural, and it is the analogue of the chunked-combiner lever Lean
recorded but never validated at scale.

---

## The certificate bridge closes end-to-end (synthetic) — and where the cost is

`bakeoff_gen.py eqns N` now emits a **complete** bridge, not just the per-node
lemmas: `rec_k` (×N) → `combiner` → `wf_distinct`/`wf_some`/`wf_dep` →
`src_agree` → `bridge` (Piece B applied) → `comb_refines_fast`, the last being
`word_of_int (bv_uint (eval_graph … n)) = fv_n`, exactly the shape
`outputs_from_cert` produces. **This is the first time the chain has closed in
Isabelle.**

### The per-node lemmas were never the bottleneck

End-to-end vs per-node-only, `eqns`, threads=4:

| N | per-node only | end-to-end (v1) | end-to-end (v2, after fix) |
|---|---|---|---|
| 100 | 14 s | 26 s | 17 s |
| 200 | 26 s | 99 s | 38 s |
| 400 | 69 s | 580 s | **124 s** |
| 800 | 249 s | — | 539 s |

At N=400 the per-node lemmas were 69 s of 580 s. **All the earlier
representation work was measuring the wrong term.**

### Fix 1 — `src_agree` was 86 % of the build

It proved `∀m ∈ set topo. ∀d ∈ set (deps_of G m). d ∉ set topo ⟶ …` by deciding
`d ∉ set topo_list` **per dependency against an N-element set literal**:
491 s of 580 s at N=400.

Replaced with the keys-subset route — prove `set (bt_keys phi_tree) ⊆ set topo_list`
**once**, then `bt_find_eq_none` yields `phi d = src_env d` for every off-topo `d`
with no enumeration. **4.7× at N=400; the DINO extrapolation drops from ~112 h to
~7.6 h.**

Note this is precisely what Piece B's own docstring already prescribed (Lean's
`bridge_src` = `BT.find_eq_none` + `keys_sub`). The cost was not discovering the
technique — it was not following the note.

### Remaining: four lemmas, one systemic pathology

Per-command CPU at N=800 (`isabelle build -c -v`):

| lemma | CPU | why |
|---|---|---|
| `wf_dep` | 292.6 s | `dep_ordered_acc` threads a growing `insert`-set through N steps |
| `wf_some` | 192.4 s | N membership checks against an N-element set literal |
| `combiner` | 190.4 s | N simp rules against an N-conjunct goal |
| `phi_keys_sub` | 121.2 s | subset of two N-element set literals |

Exponent still climbs (1.46 → 1.85 → **2.17** at 400→800), so ≈ 7.6 h at 4912
nodes. **Every one of these is the same shape**: a lemma quantifying over
`set topo_list` discharged by `simp`, which unfolds an N-element literal and does
N-fold rewriting.

**The split that matters:** `wf_dep`, `wf_some` and `phi_keys_sub` are *ground
decidable facts about concrete data* — nothing symbolic. They belong in the code
generator (`by eval` / `code_simp`), where compiled ML does in milliseconds what
`simp` does in minutes. Only `combiner` is genuinely symbolic (it mentions `phi`,
`eval_node`, `bvenc` of word values) and needs the structural fold — the Isabelle
analogue of Lean's term-mode `List.forall_mem_cons` right fold.

Caveat on `eval`: `livehd-proof/translation-correctness/README.md` records a
whole-graph `by eval` on `graph_cert_wf_bool` consuming hours and hundreds of GB.
That was `deps_before` with O(N²) list appends. The predicates here are cheaper,
but measure before trusting it.

### Proof-shape rules for Piece C (each cost a build round)

- **The combiner needs `rec_k[symmetric]`.** Forward, `rec_k : phi k = eval_node G phi k`
  rewrites `phi k` the wrong way and collides with the `phi_at_k` lookup rule,
  which has the same LHS. Forward-stated, the combiner does not close.
- **Never let `simp` instantiate `eval_graph`.** `using bridge by (simp add: topo_list_def)`
  expanded the fold into a nested chain of function updates — the O(N²) blowup,
  visible in the goal. Use `have memN: "n ∈ set topo_list"`, `using bridge memN by blast`,
  then `unfolding` that equation.
- **`blast` on the bounded-∀ side condition HANGS** (400 s timeout at N=5, no
  error). Use `intro ballI impI` plus a named instantiation.
- **`auto` will not chain the off-topo argument** (`d ∉ topo` + `keys ⊆ topo` ⟹
  `d ∉ keys` ⟹ `bt_find = None`); four explicit Isar steps are instant.
- **Ask for subset, not equality**, on the keys fact: the BST is preorder and the
  topo list ascending, so set equality of the two literals is far more expensive
  and is not needed.
- **`Translation_OpBridge` does not import `Translation_GraphRefine`.** Pieces A
  and B are siblings; emitted theories must import both.
- **Isabelle caches by content digest.** A timing run on unchanged content
  silently reports *nothing*. Always `-c` when measuring.

---

## Ground facts belong in the code generator — measured, and it is not the lever

`wf_some`, `wf_dep` and `phi_keys_sub` are *ground decidable facts about concrete
data*. Moved from `simp` to `by eval`, each with a one-line `list_all_iff` bridge
back to the `∀…∈set…` form Piece B expects:

```isabelle
lemma wf_some_ev: "list_all (\<lambda>m. nodes_fn m \<noteq> None) topo_list"  by eval
lemma wf_dep:     "dep_ordered G topo_list"                                by eval
lemma phi_keys_sub_ev:
  "list_all (\<lambda>k. k \<in> set topo_list) (bt_keys phi_tree)"          by eval
```

| N | pre-`eval` | post-`eval` | gain |
|---|---|---|---|
| 200 | 38 s | 33 s | 1.15× |
| 400 | 124 s | 103 s | 1.20× |
| 800 | 539 s | 430 s | 1.25× |

**Exponent essentially unchanged: 2.17 → 2.12** (DINO extrapolation 7.6 h → 5.5 h).

Worth recording as a mistake in reasoning: the per-command profile showed these
four lemmas holding ~800 s of CPU at N=800, so removing three looked like the
lever. It was not. The *earlier* per-node-only sweep was already ≈ N^1.9 at
400→800, **before any of these lemmas existed** — so the quadratic never lived
there. Trimming a large constant off a quadratic that lives elsewhere buys a
constant, and that is all it bought. A per-command profile ranks *current* cost;
it does not tell you which term sets the exponent. Fit the exponent of each
component separately before choosing what to fix.

Side benefit: `wf_dep` no longer needs `dep_ordered_acc_sound`. That accumulator
lemma exists to avoid `dep_ordered`'s quadratic suffix scan in *symbolic* proof;
once the fact is decided in the code generator the scan is ~12 M cheap membership
tests in compiled ML. The lemma stays in the library for symbolic use, but the
generated path does not need it.

Also: the feared `by eval` blow-up did not materialize. `livehd-proof`'s README
records a whole-graph `by eval` consuming hours and hundreds of GB, but that was
`deps_before` with O(N²) *list appends*; these predicates are cheap.

### Where the quadratic actually is

**The N derived lookup lemmas.** Each `phi_at_k` / `nodes_at_k` unfolds an O(N)
tree literal into its goal, so establishing the rule set is O(N²). The `bt`
candidate has the same cost paid per *use* instead of per *derivation* — which is
why `bt` and `eqns` measured within 15 % of each other.

The general shape: **any representation where resolving a lookup requires the
O(N) definition body to enter a goal is O(N) per node, hence O(N²) overall.**
Escaping it needs the lookup resolved *without* the body — which is what the code
generator does, but the payloads here (`bvenc fv_k`) are symbolic terms, so `eval`
cannot reach them. Lean escapes via kernel defeq on a shared tree value; Isabelle
has no equivalent.

Options not yet measured: chunk the tree into K subtrees so each unfold is O(N/K)
(≈ N²/K total); or split the recurrence lemmas across K theories. Both are the
same lever in different clothing, and both are chunking.

Current honest budget for a DINO-scale Isabelle certificate proof: **≈ 5.5 h**,
exponent ≈ 2.1.

---

## The first DINO-scale run: it closes, and where the 9 hours went

**Synthetic** bridge at N=4912 (DINO SingleCycleCPU's node count), threads=8,
detached systemd unit:

| | |
|---|---|
| wall | **8 h 54 m 39 s** (parallel factor 2.54) |
| CPU | 22 h 37 m |
| peak RSS | **64.1 GB** |
| result | `exit=0`, **0 errors, 0 sorries** |

**Scope, stated precisely:** this is a synthetic chain — every node `Op_Not` of
the previous, all 8-bit, one dep per node, one source, no outputs or flops. It
shares DINO's *node count* and nothing else (DINO: GetMask 2093, And 803, SRA
714, SHL 644, MuxN 150, `Or` up to arity 55, widths 1–127, 36 sources, 33 flops).
It is a **scaling measurement at DINO's size, not a DINO proof.** Piece C — the
emitter that would produce DINO's actual bridge — does not exist yet, so no DINO
bridge file exists to check.

The prediction was 5.5 h; the actual was 8.9 h, a **1.6× underestimate** even
though the run used 8 threads against a threads=4 fit. Extrapolating from N=800
underestimates, because the exponent is still climbing at that size.

### Two declarations were essentially the entire run

| line | lemma | time |
|---|---|---|
| 39360 | `combiner` | **26,371 s = 7 h 19 m** |
| 39348 | `wf_distinct` | **15,425 s = 4 h 17 m** |
| — | every other command | < 223 s |

`combiner` is a single declaration and therefore single-threaded, so its 7 h 19 m
*is* the 8 h 54 m wall.

`wf_distinct` is `distinct topo_list` by `simp` — ~12 M pairwise numeral
comparisons as rewrite steps on a 4912-element literal. It is the identical
pathology already fixed in the other three well-formedness lemmas, and it was
left on `simp`. Worse, the N=800 profile had flagged it (an unattributed 90.3 s
entry noted as "probably `wf_distinct`") and it was not acted on.

### Both fixed

- `wf_distinct` → `by eval`. Ground decidable fact; the code generator runs the
  real `distinct` in compiled ML.
- `combiner` → **structural fold**. Was one `simp` carrying N rewrite rules
  against an N-conjunct goal. Now: split the bounded quantifier once with two
  locally-proved helpers (`ball_set_cons`, `ball_set_nil` — proved here rather
  than looked up, so nothing depends on a library name), then discharge each
  small goal with a directed `rule rec_k`, which is O(1) per node. This is the
  Isabelle form of Lean's `List.forall_mem_cons` term fold.

| N | before | after | gain |
|---|---|---|---|
| 400 | 103 s | 76 s | 1.36× |
| 800 | 430 s | **262 s** | **1.64×** |

**Exponent 2.12 → 1.86.** Naive extrapolation says ≈ 2 h at 4912; given the last
extrapolation was 1.6× low, budget **2–4 h** and re-measure rather than trust it.

### `by eval` extends the trusted base

`eval` is oracle-based: it trusts the code generator rather than producing
kernel-checked steps. Five lemmas now use it (`wf_distinct`, `wf_some_ev`,
`wf_dep`, `phi_keys_sub_ev`, and the harness's own). For an artifact whose point
is trustworthiness that is a real trade — `code_simp` is the checked-but-slower
alternative. This is a deliberate choice and should be made explicitly before it
reaches the emitter.

---

## Trust base: `eval` vs `code_simp`, measured (2026-08-08)

**Which methods extend the trusted base** — checked with `Thm_Deps.all_oracles`
rather than asserted:

| method | oracle |
|---|---|
| `by eval` | **`Code_Generator.holds_by_evaluation`** |
| `by code_simp` | none |
| `by simp` | none |

So `eval` runs compiled ML and asserts the result through an oracle: believing it
means trusting Isabelle's code generator and Poly/ML *in addition to* the kernel.
`code_simp` uses the same code equations but routes every step through the
simplifier, so it is kernel-checked. (`code_simp` takes no `add:` argument.)

**Cost, N=800, run back-to-back so both saw the same machine load:**

| variant | wall |
|---|---|
| `eval` | 263 s |
| `code_simp` | **TIMEOUT at 3000 s** (never finished) |

`code_simp` is >11× slower and did not complete in 50 minutes at N=800; at DINO
scale it would be worse than the original `simp`. **The kernel-checked route is
not affordable**, so the emitted bridge necessarily carries a code-generator
dependency for its ground facts. That is a real limitation of the artifact, not a
footnote: emit a `thm_oracles` audit on the final `_refines_fast` theorems so the
dependency is reported rather than assumed away.

## `wf_distinct` was dead code — and it cost 4 h 17 m

`distinct topo_list` had **exactly one occurrence in the generated theory: its own
declaration.** Nothing cited it.

`eval_graph_of_local_agree_all` takes only `dep_ordered`, `some`, `rec` and `src`
— distinctness drops out of its induction, because a repeated id is simply
re-evaluated and covered by the inductive hypothesis. It was needed only by
`dep_ordered_acc_sound`, and once `wf_dep` was discharged directly `by eval` that
route disappeared. The lemma was left behind.

In the original 8 h 54 m run it was still `by simp` and consumed **4 h 17 m —
half the wall clock, on a lemma nothing used.** Removing it now saves almost
nothing (the `eval` conversion had already neutralised it: 263 s at N=800 either
way); what it buys is **one fewer oracle site**, 4 → 3.

**The lesson is about profiling, not about `distinct`.** A per-command profile
ranks what is *expensive*; it says nothing about what is *needed*. Both times the
profile pointed at this lemma the response was to make it faster — first `simp` →
`eval` — and neither time did anyone check whether it was cited. Check the call
graph before optimising an entry in a profile.

(A full `graph_cert_wf` claim *does* still require distinctness — but `by eval`,
never `by simp`.)

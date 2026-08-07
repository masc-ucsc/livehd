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

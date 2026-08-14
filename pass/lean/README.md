# pass.lean

`pass.lean` is the Lean target for the LiveHD graph-to-theorem-prover flow.  It
is intentionally modeled after `pass.isabelle`, but it is a separate pass and a
separate proof stack.

In macos, you may need to install Lean 4:
```
brew install elan
```

## Current Implementation State

- `formal/lean` builds a Lean package named `LeanSemanticPrimitives`.
- `LeanSemanticPrimitives.SemanticPrimitives` contains total bit-vector,
  flop, function-valued memory, byte-enable, and SRAM primitives.
- `LeanSemanticPrimitives.Translation.LGraphModel` contains:
  - `LGraphOp`, `BV`, `NodeCert`, and `GraphCert`.
  - Boolean certificate shape predicates for whole graphs and chunks.
  - `denote_op` / `eval_op`.
  - `denoteGraph` / `evalGraph`.
  - `evalGraphCorrectForCert`, the generic link:
    `evaluated certificate = mathematical certificate semantics`.
- `LeanSemanticPrimitives.Translation.FastModelBridge` contains the generic
  step bridge:
  if generated `next` and `comb` equal the certificate model, then generated
  `step` equals certificate `step`.
- `pass.lean` is registered as a LiveHD pass and currently emits:
  - concrete Lean input/output/state structures;
  - concrete `<Top>_comb`, `<Top>_next`, and `<Top>_step` definitions for the
    supported non-memory graph subset;
  - concrete `NodeCert` lists and `GraphCert` data for the same topo-ordered
    graph nodes;
  - `outputsFromCert`, `nextStateFromCert`, `<Top>_comb_cert`,
    `<Top>_next_cert`, and `<Top>_step_cert` definitions;
  - definitional certificate-model bridge theorems for the cert-based model;
  - `evalGraphCorrectForCert` instantiations over the emitted certificate.
- Certificate emission can be disabled with `--set formal.lean.emit_cert=false` for
  model-only scaling gates.

Verified:

- `add2` oracle (fast model is RTL-exact and matches the certificate evaluator):
  ```lean
  ∀ a b : BitVec 4, (add2_comb {a,b}).out_y = a + b
  ∀ a b : BitVec 4, (add2_comb {a,b}).out_y = (add2_comb_cert {a,b}).out_y
  ```
  both close `by decide`.
- `Get_mask(a, -1)` all-ones zext idiom: the mask is materialized at
  `max(src_w, out_w)` (fast model + certificate in lockstep), so it selects every
  source bit instead of only bit 0.  This was a shared bug with `pass.isabelle`
  (fixed upstream by `pass.isabelle: preserve Get_mask and Sext widths`; see
  `pass/isabelle/TODO`).
- Builds against the current graph API: `bazel build //pass/lean:pass_lean`,
  `//lhd:lhd`, and `lake build` (support package) all succeed.

## Validation Pipeline (order matters)

Upstream LEC is now strong enough to be the mandatory RTL-to-LGraph semantic
gate, so theorem-prover generation must consume only graphs that have passed or
been explicitly classified by LEC.  The gate runs BEFORE `pass.lean`:

```text
1. LiveHD compile        RTL -> LGraph                        (lhd compile verilog)
2. LEC gate              prove/classify RTL == LGraph         (scripts/run_dino_lgraph_lec_gate.sh)
                           default lec.engine=auto,
                                   lec.hier=true,
                                   lec.semdiff=structural
                           accept: PROVEN, or INCONCLUSIVE (recorded);
                           reject: REFUTED  ->  do NOT generate
3. pass.lean             LGraph -> Lean model + certificate   (--emit-dir lean:)
4. Lean typecheck        lake env lean <Top>_Lgraph.lean
5. certificate bridge    generated model = graph certificate  (per-design cert theorems)
```

`scripts/run_dino_lgraph_lean.sh` runs step 2 automatically before step 3 unless
`RUN_LEC_GATE=false`.  A REFUTED design aborts the run; INCONCLUSIVE is a
recorded warning (set `LEC_STRICT=true` to make it a hard gate for CI).  The
RTL-to-LGraph equivalence proven here is what lets steps 3-5 restrict their claim
to "generated model = LGraph certificate" instead of re-proving RTL semantics.

## Chosen Proof Strategy

The Lean proof path follows the same Strategy B used for Isabelle:

```text
generated fast Lean model
  = evaluated graph certificate
  = mathematical graph-certificate semantics
```

The reusable Lean theorem already implemented is the second link:

```lean
evalGraph G = graphDenotation G
```

The first link is the remaining major proof bridge to emit per design:

```lean
<Top>_comb i s =
  outputsFromCert (evalGraph <Top>_graphCert.topo <Top>_graphCert (<Top>_sourceEnv i s))

<Top>_next i s =
  nextStateFromCert (evalGraph <Top>_graphCert.topo <Top>_graphCert (<Top>_sourceEnv i s))
```

The emitted cert-based model now has definitional bridge theorems to the
evaluated certificate.  The remaining hard bridge is the fast-view theorem:

```lean
<Top>_comb i s = <Top>_comb_cert i s
<Top>_next i s = <Top>_next_cert i s
```

### Step 5 — fast-view bridge (emitted behind `emit_fast_bridge`)

This is the "first link" above (generated fast model = evaluated certificate),
the keystone tying the executable model to the graph semantics (and, via the LEC
gate, to the RTL).  It is now **generated end-to-end** for the non-memory op set,
gated by `--set formal.lean.emit_fast_bridge=true` (default off, so ordinary
output is unchanged).  Architecture: a design-/operator-independent library plus
a small per-design instantiation the emitter prints.

- **Piece B — general theorem (proven)** — `GraphRefine.evalGraph_of_localAgree`
  (`.../Translation/GraphRefine.lean`): `evalGraph` computes the unique
  topological fixpoint of a dependency-ordered graph, so any environment `φ` that
  agrees with the source env on off-topo deps and satisfies the per-node
  recurrence `φ n = evalNode G φ n` equals `evalGraph` on every topo node.  Every
  design's step 5 reduces to this by instantiating `φ` with its encoded
  fast-model node values.  Mathlib-free; a decidable `DepOrderedB` discharges
  dependency ordering by `native_decide`.

- **Piece A — per-operator bridge library (complete for DINO's op set)** —
  `.../Translation/OpBridge.lean`: relate the certificate evaluator `eval_op`
  (width-erased `BV`) to the native `BitVec` fast op under the encoding
  `bvenc x = mk_bv w (Int.ofNat x.toNat)`.  Covers GetMask, And/Or/Xor/Not, Ror,
  the compares (EQ/ULT/UGT/SLT), SHL/SRA, MuxBool/MuxN, Sext, and Sum
  (add and add/subtract) — binary bridges are width-polymorphic (`{wa wb w}` +
  `bv_zext`), and the genuinely n-ary operators (Or of arbitrary arity, MuxN,
  the And/Or/Xor/Sum folds) are handled by fold/`muxn_fast`/`orn_bv_bridge`
  combinators so one lemma covers every arity.  Crux helpers: `bv_bit_bvenc`,
  `bv_to_bitvec_bvenc`(`_zext`), `mk_bv_ofInt`, `mk_bv_add`/`mk_bv_sub_emod`.

- **Piece C — emitter (`emit_fast_bridge`)** — per design it prints: `φ`
  (`<Top>_phi`), per-source `sourceEnv id = bvenc leaf` facts, `native_decide`
  well-formedness/ordering facts, one **standalone** recurrence theorem per topo
  node (`<Top>_rec<id>`) discharged by that node's op bridge, a thin combiner
  (`<Top>_bridge_rec`), and finally `<Top>_comb_refines_fast` (+ for sequential
  designs `<Top>_next_refines_fast` and `<Top>_step_refines_fast`) closing with
  `evalGraph_of_localAgree`.  Node values are factored into `<Top>_fv<id>` defs so
  `φ` and the per-node recurrence can name them; each node's φ-lookups are
  resolved by *defeq* in a `show` (not `simp [phi]`) to avoid an O(n²) blow-up.

- **Two cert-emission bugs step 5 surfaced and fixed** — const **shift**
  amounts (SHL/SRA) and the **Sext** amount operand were emitted at `pin_width`,
  truncating the value (e.g. a `32` on a 1-bit pin → `mk_bv 1 32 = 0`); the fast
  model widens them to fit the value, so cert ≠ fast until the cert was fixed to
  widen identically (`shift_dep_width`).

- **Mathlib dependency** — the op-bridge proofs need Mathlib, so `formal/lean`
  requires `mathlib @ v4.31.0`; fetch oleans once with `lake exe cache get`.
  `OpBridge` is NOT imported by the package root (non-bridge output stays
  Mathlib-free); the emitter adds the import only to bridge-enabled files.
  Each bridge lemma carries an `example` of its intended use in `OpBridge.lean`
  itself, so `lake build LeanSemanticPrimitives.Translation.OpBridge` checks the
  library and its usage together and the check cannot drift from the lemma.

**Status:** the emitter generates a **sorry-free** bridge for all three DINO
variants, and **all three are proven end-to-end**.

| design | nodes | wall | peak RSS | result |
|---|---|---|---|---|
| `SingleCycleCPU` | 4,772 | 23.2 min | 13.3 GB | **proven** — `exit 0`, 0 errors, 0 sorries (8 cores) |
| `PipelinedCPU` | 5,061 | 27.7 min | 14.9 GB | **proven** — `exit 0`, 0 errors, 0 sorries (4 cores) |
| `PipelinedDualIssueCPU` | 10,740 | 57.4 min | 29.6 GB | **proven** — `exit 0`, 0 errors, 0 sorries (4 cores) |

For the proven designs `_comb`, `_next` and `_step` are all shown equal to the
certificate model.  Two things worth noting:

- **The two pipelined variants were generated by the *unchanged* emitter** — the
  eight bugs fixed while closing `SingleCycleCPU` generalized to two previously
  unseen designs, with no new proof holes and no new operator bridges required.
  `PipelinedCPU` proved clean on the first attempt.
- **Bug 9 (binary `Or`) was found and fixed here.** `Op_Or` was the only operator
  the emitter dispatched without an arity guard, so a 2-input `Or` still took the
  n-ary `List.foldl` bridge; unfolding that fold sent the kernel into *unbounded*
  recursion on one node whose operands were 5 `fv` levels deep.  Routing binary
  `Or` to the existing fold-free `or_bridge` (as And/Xor/Sum already were) fixed it
  **and made the proofs faster** — DualIssue went 84.7 min (failing) → 57.4 min
  (proven) on the same cores.  See `STEP5_BRIDGE_BUGS.md`.
- **Scope of the claim.** These prove *fast model ⇔ certificate* for the RTL in
  `generated/rtl_lgraph_equiv_latest/lec/refs/<Top>_ref.sv`.  Those pipelined refs
  date from 2026-06-05 and may predate the register-file bypass fix (the
  `cpuType` default in the pipelined emit objects), so this is not a claim about
  the *current* pipelined design — regenerate the refs if that matters.

Because this machine is a shared NFS server, always run as a good
citizen: `LEAN_NUM_THREADS=8 taskset -c 0-7 nice -n 19 ionice -c 3 lake env lean
<file>`, and for long runs a detached `systemd-run --user -p CPUQuota=800%`
service (a `--scope` dies with the launcher).

### Scaling of the fast-view bridge (measured, DINO 4772 nodes)

Two independent O(N²) traps had to be removed; both are now handled by the
emitter.  **Representation of the graph lookups**, and **shape of the combiner**:

| what | bad form | cost | good form | cost |
|---|---|---|---|---|
| `φ` / `sourceEnv` / `graphCert.nodes` | flat `if n = k` chain, `List.find?` (also: a balanced `if`-*tree*, which is no better — reducing it beta-substitutes the key across the whole N-node term) | O(N²), 47 h / 94 GB unfinished | `BT` **data** + recursive `BT.find`, values as input-independent closures → O(log N) per lookup | O(N) |
| `<Top>_bridge_rec` combiner | `simp only [...] at hn` + `rcases hn with h │ h │ …` (N-way) + N bullets — an N-deep `Or.casesOn` whose motive carries the rest, plus N goal substitutions | > 3 h 56 m unfinished | term-mode right fold over `List.forall_mem_cons` | ~127 s |

Full-file decomposition once both are fixed:

| section | wall |
|---|---|
| head: 4772 `fv` defs + 3 BT trees + 4438 source facts + wf `native_decide`s | 181 s |
| + first 200 recurrence theorems | 171 s (marginal ≈ 0) |
| + all 4772 recurrence theorems | ~1457 s ← **now the dominant term** |
| + combiner / `bridge_src` / `_refines_fast` | **1391 s total (23.2 min), 13.3 GB, exit 0, 0 errors** |

Guidance for larger designs (CVA6): emit **BT-based lookups and a term-fold
combiner** from the start — the monolithic forms are not merely slow, they do not
finish.  Budget ≈ **23 min / 13.3 GB at 4772 nodes**.  The remaining hot spot is
the per-node recurrence suite (~1276 s / 4572 theorems ⇒ **≈280 ms per node**).
Note Lean parallelizes *theorem bodies*, so a file drains to one core as
stragglers finish — a single oversized declaration pins one thread and dominates
wall time.

#### Where the per-node 280 ms is NOT going: the `GetMask` `by decide` (measured)

This README previously named the ~2093 `GetMask` `by decide` side conditions as
the next win.  **Measured, that is wrong** — they are ~2 % of the run.  Isolated
benchmark, 20 side conditions per point, Mathlib-import baseline **5.85 s /
6.6 GB** subtracted (`LEAN_NUM_THREADS=4`, 4 cores):

| mask width | `by decide` (`(mask_indices M).length ≤ b`) | closed form (`mask_indices_length_ofInt_neg_one`) |
|---|---|---|
| 65  | 19.5 ms/node | 4.0 ms/node |
| 129 | 38 ms/node   | — |
| 257 | 66 ms/node   | — |
| 513 | 146 ms/node  | **~0 (below noise)** |

So `decide` here is **≈ linear in mask width** (≈0.29 ms/bit), *not* quadratic,
and the closed form is width-independent.  Scaled over the real GetMask width
distributions that totals only ≈ **20 s** for DINO (2093 nodes, ≤127 bits) and
≈ **29 s** for `cva6_tlb_gate` (1802 nodes, 114 of them at 257–576 bits).

Conclusion: keep the closed-form all-ones path — it is free, strictly less work at
every width, and it removes the only *width-dependent* term in the per-node proof,
which matters as designs get wider (a hypothetical 4096-bit cache-line node would
cost ~1.2 s on its own under `decide`).  But it is **not** the lever for the
280 ms; that budget is dominated by the rest of the per-node proof (`show` defeq
resolution plus the per-node `rw` / `simp only [fv, closer]`), which is where the
next measurement should go.

**Wired** (`OpBridge.getmask_bridge_allones_ofNat`, emitted from the `Op_GetMask`
arm of the bridge dispatch).  Notes:
- The emitter recognises the fast path by **exact cert-leaf spelling**
  (`int_of_const` renders -1 as `(-Int.ofNat 1)`) and emits
  `first | <all-ones> | getmask_bridge'`, so a future spelling drift degrades to
  the old cost instead of silently failing — coupling a lemma to emitted text is
  the failure mode behind 4 of the 8 bugs in `STEP5_BRIDGE_BUGS.md`.
- Because that dispatch makes the losing branch unelaborated, bridge output now
  also sets `linter.unreachableTactic`/`linter.unusedTactic` false (otherwise
  ~2 warnings × ~1800 GetMask nodes bury real diagnostics).  `op_census.py`
  therefore reports **`emitted on fast path : N / M all-ones nodes`** so the
  emitter's choice stays visible; to confirm Lean *takes* the branch, re-run with
  `linter.unreachableTactic` on and check the generic branch reports unreachable.
- Verified on `simple_add` through the real emitter: fast path chosen 2/2, the
  generic branch reported unreachable, `exit 0`, 0 sorries.

**Pre-flight check this added** (`op_census.py`, hard FAIL): both GetMask lemmas
need `(mask_indices M).length ≤ out_width`, which for an all-ones mask *is* the
mask width.  A Get_mask that **truncates** (source wider than output) therefore
gets a mask wider than its output, making that side condition **false** — the
generic `by decide` would be deciding a false proposition and the node could not
close by either lemma.  This is latent, not introduced by the fast path; it is
simply absent from DINO.  Measured absent from `cva6_tlb_gate` too: all **1802**
GetMask sites have `mask_w ≤ out_w` and none truncate.

### Lessons learned (step 5)

Getting DINO from "emitted" to "typechecked" took eight bug fixes and two
algorithmic rewrites.  Full detail: **`STEP5_BRIDGE_BUGS.md`** (every bug, symptom
→ root cause → fix) and **`.claude/skills/prove-cert-equivalence/SKILL.md`** (the
reusable procedure).  The transferable parts:

1. **Two independent O(N²) traps.** Graph lookups *and* the combiner (table
   above).  Fixing one only exposes the other.
2. **"0 sorries" ≠ "typechecks".** A sorry-free file can be full of proof holes,
   and an O(N²) check hides them by never *reaching* the declarations that fail.
   Every speedup exposed more bugs.  Only a full-file `exit 0` counts as done.
3. **Constant spelling needs one source of truth.** The fast emitter
   (`lit_const_at`) and the certificate leaf (`int_of_const` +
   `CertBuild::source_leaf`) must produce *textually identical* constants — four
   of the eight bugs were the same value spelled two ways (`0#w` vs
   `BitVec.ofInt w 0`, `(1#w <<< w) - 1#w` vs the decimal, …).  Ops that erase the
   const via `bv_zext` hide it; ops where the const **survives into the result**
   (compares inside `decide`, MuxN branches, mask `&&&`) fail.  `lit_const_at` now
   delegates to `int_of_const`, and **`pass/lean/scripts/const_parity.py`** guards
   it — it flagged **4401 of 4772** nodes before the fix and **0** after, in
   seconds, replacing a ~3 h discovery run.
4. **Off-topo ids.** An output or flop din can be driven *directly* by a
   constant/input/flop; its cert id is then a source, there is no `_fv<id>`, and
   the `evalGraph_of_localAgree` fact does not apply.  Use
   `GraphRefine.evalGraph_not_mem` (off-topo ids read through to the source env).
5. **Diagnosing a slow check.** Errors are reported **incrementally**, so
   `grep 'error:' <log>` at any time.  But the log is **not** a progress signal:
   stdio block-buffers at 4096 bytes when stdout is a file (use `stdbuf -oL`), and
   Lean emits messages in **file order**, so one slow early declaration withholds
   all later output.  To find a slow declaration, **bisect the file into timed
   cumulative slices**; to tell "one stuck declaration" from "starved by load",
   read per-thread CPU in `/proc/<pid>/task/*/stat` (Lean parallelizes theorem
   bodies, so a drained run means everything else already finished).
6. **Estimating runtime.** Synthetic sweeps give the *exponent* — use them to
   choose a representation.  Only timed slices of the **real** file give the
   *constant*.  Synthetic-only estimates here were off by 10–50×.
7. **Testing discipline.** A truncated probe is not a clean bill of health: one
   probe reported "exit 0, 0 errors" while silently omitting the tail that held a
   live bug.  Always state what a probe *excludes*; bugs cluster in the tail
   (`bridge_src`, `_refines_fast`), which is also the first thing lost when a run
   is killed — to test it cheaply, stub the combiner with `sorry`.  And do not run
   several ~10 GB Lean instances concurrently: they OOM each other and the
   failures look like results.

### Applying this to CVA6 (plan)

The DINO result gives a repeatable recipe.  Applying it to CVA6 is mostly a
matter of *scope selection*, because the front-end reality constrains what can be
lowered at all.  Background: `scripts/CVA6_SV2V_FILELIST_REFERENCE.md`.

**Reality check — whole-core CVA6 is not reachable today.**  Both front-ends are
blocked, in different places:
- `sv2v --top=cva6` dies on `acc_dispatcher` (CV-X-IF is enabled in
  `cv64a6_imafdc_sv39_hpdcache_wb`, so the accelerator cannot just be dropped);
- the slang path gets past elaboration and then dies at
  `pass/cprop/cprop.cpp:459` (a livehd-new EQ-fold bug).

The reachable unit is a **subtree**: `sv2v --top=<module>` plus a one-line gate
wrapper binding the config params (`scripts/cva6_module_wrappers/`).  `cva6_tlb`
is already verified through that path (749 lines, 2 modules, exit 0).

**Per-module pipeline** — identical to DINO, with the LEC gate still mandatory:

```text
sv2v subtree lowering → LEC gate (RTL ≡ LGraph) → pass.lean --set formal.lean.emit_fast_bridge=true
    → static gates (seconds) → verification ladder → full typecheck
```

**Static gates first — they cost seconds and replace hours of discovery:**
`pass/lean/scripts/op_census.py` PASS (every `(op, arity)` in the design is handled
by the step-5 dispatch), `pass/lean/scripts/const_parity.py` PASS, `sorries = 0`,
`TODO(step5) = 0`, and all three `_refines_fast` theorems present.  Never start a
long run without them.

**Verification ladder:** the 1-flop sequential design → **a module with an output
driven directly by a constant** (this case is absent from the tiny combinational
designs and cost us a 26-minute run to discover) → the target module.

**Expected work per module, in the order it will surface:**
- **New op bridges.** CVA6 reaches ops DINO never used — `Mult`, `Div`/`UDiv`/
  `SDiv`, `SetMask`, wider `Sum` arities.  The emitter already flags an
  unsupported op with a marked `sorry`, so the static gate catches it
  immediately; each needs one `OpBridge` lemma in the established
  `eval_op OP w [bvenc …] = bvenc (fast …)` shape.
- **Memories are a hard blocker.** `emit_fast_bridge` is gated on
  `memory_nodes.empty()`, so any module containing arrays is excluded until the
  **verified memory certificate** (Phase 4 below) lands.  Choose memory-free
  subtrees first and treat memory as its own milestone.
- **Scale.** Reference point: 4772 nodes → 23 min / 13.3 GB with BT lookups and
  the term-fold combiner.  Record the node count per module and compare.  For a
  module much beyond ~5 k nodes the next lever is a **chunked combiner** (K lemmas
  of ~64 nodes joined with `List.forall_mem_append`), which caps any single term
  *and* restores parallelism across declarations — designed, but **not yet
  validated at scale**.

**Suggested order:** `cva6_tlb` (lowering already verified) → other
accelerator-free, memory-free subtrees (decoder, ALU, branch unit, CSR logic) →
progressively larger cones.  Whole-core only once the two front-end blockers above
are fixed.

**Keep a per-module record** (module, node count, wall, peak RSS, new op bridges
added, blockers hit) so the cost model stays calibrated as designs grow.

### CVA6 per-module record (measured)

| module | nodes | flops | max width | GetMask (all-ones) | wall | peak RSS | result |
|---|---|---|---|---|---|---|---|
| `cva6_alu_export` | 6,305 | 0 (comb) | 576 | 2,681 (100 %) | **4 h 15.8 min** † | **25.3 GB** | `exit 0`, 0 errors, 0 sorries — `_comb` proven |
| `cva6_tlb_gate` | 2,061 | 137 | 513 | 901 (100 %) | **5 h 12.5 min** † | **12.2 GB** | `exit 0`, 0 errors, 0 sorries — `_comb`/`_next`/`_step` all proven |

† **Dominated by ONE serial declaration, not by per-node cost.**  (An earlier note
here blamed contention — load average ~45 from concurrent work.  That was wrong, and
the thread accounting refutes it.)  Measured on the `cva6_tlb_gate` run at 72 min in,
with `LEAN_NUM_THREADS=8` and `CPUQuota=800%`:

| | |
|---|---|
| busiest thread | **4301 s CPU, state R (running)** |
| second busiest | 60 s |
| idle threads | **61 of 69** |
| CPU / wall ratio | ~1.0 |

CPU ≈ wall rules starvation out: a starved job accumulates *less* CPU than wall.  This
is one declaration grinding serially while every other thread has finished — the
signature SKILL §3 describes.  The ALU showed the same ~1.1 ratio at its 67-minute
check, so both runs share this cause.

Consequence: **node count does not predict the wall here.**  DINO's
`PipelinedDualIssueCPU` (10,740 nodes) finished in 57.4 min, while `cva6_alu_export`
(6,305) took 4.3 h and `cva6_tlb_gate` (2,061 — a third the ALU) took 5.2 h.  The CVA6
modules are *smaller* than DualIssue and 4–5× slower, so the serial declaration scales
with something other than node count.

**The combiner is NOT the culprit — measured, not guessed.**  Running the identical
`cva6_tlb_gate` file with only the combiner's body replaced by `sorry`:

| variant | wall |
|---|---|
| full file | 18,750 s |
| combiner stubbed to `sorry` | **18,666 s** |

The combiner therefore costs **~65 s, 0.3 % of the run** — even at 2,061 nodes with a
term-fold body.  (An earlier revision of this note named it a suspect; the probe
refutes that.)  This also retires it as a scaling risk for CVA6-sized designs, and
means the **chunked combiner** lever listed above would buy nothing here.

Remaining suspect, and the reason Phase 1b exists: `_phiTree_keys_sub`, proven
`by native_decide`, which compiles the **whole `phiTree` including every value
closure** — at CVA6 widths (513/576 bits) that is a long serial native-compilation
step, exactly the risk flagged in SKILL §5.  Confirm the same way (stub only that
declaration and re-time) **before** optimizing: the culprit was guessed wrong three
times during the DINO work, and once more here.

Method note worth reusing: stubbing **one declaration** and re-timing is far cheaper
than bisecting the file into cumulative slices, and it answers the same question.  It
does not prove the file (the stubbed obligation is assumed), so it is a *localizer*,
never a result.

**Reproduce:**
```bash
# ALU (combinational, explicit file list, derives alu_concrete.sv from upstream)
LEAN_EMIT_CERT=true LEAN_EMIT_FAST_BRIDGE=true RUN_LEAN=true \
  scripts/run_cva6_alu_lean.sh

# TLB gate (sequential; slang + gate wrapper, NOT sv2v -- see
# scripts/CVA6_SV2V_FILELIST_REFERENCE.md for why)
CVA6_TOP=cva6_tlb_gate \
CVA6_WRAPPER_FILE=$PWD/scripts/cva6_module_wrappers/cva6_tlb_gate.sv \
CVA6_FILELIST=$PWD/generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f \
YOSYS_MEMORY_MODE=collect \
LEAN_EMIT_CERT=true LEAN_EMIT_FAST_BRIDGE=true RUN_LEAN=true \
  scripts/run_cva6_module_lean_stress.sh
```

**Node counts move with cprop — do not trust old artifacts.** Estimates taken from
the June `pass.isabelle` outputs were both wrong after the upstream "cleaner cprop
with less mask ops" change, in *opposite* directions: the ALU grew 5,638 → 6,305 and
the TLB shrank 4,126 → 2,061. Run `op_census.py` on a fresh emission for the real
number.

**Trust footprint of the ALU proof** (audited): 0 `sorry`, 0 `admit`, 0 `axiom`;
6,305 per-node `_rec` theorems (one per node, all kernel-checked); 6,548 kernel
`decide`s; and exactly **4** `native_decide` uses — `topo.Nodup`,
`∀ n ∈ topo, (nodes n).isSome`, `DepOrderedB`, and `BT.keys phiTree ⊆ topo`.  All
four are *structural* well-formedness facts about a concrete finite graph, not
semantic claims, so the Lean compiler enters the trusted base only for those; every
semantic step (per-node recurrences, combiner, closers) is kernel-checked.

**What the first two modules needed** (all three fixes are in the emitter, so later
modules inherit them): `and3_bridge` for arity-3 `And` (5 nodes in the ALU, 10 in the
TLB), the `Op_SHL` const port-0 width fix, and `sra_bridge_sext` for a **widening
SRA** — the last being a real fast-model mistranslation, not a proof gap (Bug 10 in
`STEP5_BRIDGE_BUGS.md`).

### Next benchmark after CVA6: CORE-ET / ETASP

Once CVA6 modules are generating and proving certificate equivalence the way DINO
does, the next target is **CORE-ET** (`/soe/czeng14/projects/core-et`) — the
*CORE-ET Agentic Silicon Platform*, an OpenHW Group / Ainekko project whose RTL is
an active translation of CORE-ET modules into **clean SystemVerilog** (464 `.sv`
files under `hw/ip/<block>/rtl`, with `dv/` collateral alongside).

Why it is a good next step:
- **Already per-IP modular**, which is exactly the unit our pipeline works on — no
  need for the `--top` + gate-wrapper surgery CVA6 requires to prune an
  accelerator out of the cone.
- **Clean SystemVerilog by construction**, so it should avoid the struct/config
  resolution walls that block a whole-core CVA6 lowering (`sv2v` on
  `acc_dispatcher`) — to be confirmed, not assumed.
- Individual IP blocks are small enough to sit comfortably on the measured cost
  curve (DINO's 4772 nodes ≈ 23 min / 13.4 GB is the reference).

Same pipeline and same static gates as CVA6.  Work to scope first:
- **Filelists.** No top-level `.f`/`.flist` was found; each block's compile set has
  to be assembled (or generated from the Makefile/`mk/` infrastructure) before
  `lhd compile verilog` can consume it.
- **Pick memory-free leaf IP first.** `emit_fast_bridge` is still gated on
  `memory_nodes.empty()`, so cache/array blocks (e.g. `minion/dcache`) wait on the
  Phase 4 memory certificate.
- **Floating point is a real scope question.** The tree contains VPU/FMA-style
  blocks (e.g. `minion/vpu/.../txfmafrac`).  `OpBridge` currently covers **integer
  `BitVec`** operators only; an FP datapath would need a substantially new bridge
  layer, so treat FP blocks as a separate milestone rather than a next step.

## Remaining Implementation Work

1. Port scalable certificate checking.
   - const-only chunks
   - simple mixed chunks
   - concrete dependency-list subset checks
   - chunked uniqueness
   - eventually dense topological certificates

2. Emit per-design fast-view bridge theorems — **done, behind
   `--set formal.lean.emit_fast_bridge=true`** (see "Step 5 — fast-view bridge"
   above; sorry-free for DINO `SingleCycleCPU`).  Remaining sub-item: reduce the
   single-file typecheck cost via the file-split in item 1.
   - `<Top>_comb = <Top>_comb_cert` ✓
   - `<Top>_next = <Top>_next_cert` ✓
   - `<Top>_step = <Top>_step_cert` ✓

3. Memory-node emission — **done** (fast model): function-valued memory state
   fields, read/write/byte-enable policy extraction, any number of read/write
   ports, read-during-write (`fwd`) policy, sync-read.  **Remaining**: the memory
   *certificate* is still a stub (counts only); a memory-aware certificate
   evaluator (`Val = bv | mem`, `Op_MemRead`/`Op_MemWrite[BE]`) + collision /
   read-first / write-first policy proofs are future work.

4. Harden operator semantics and tests.
   - `Get_mask` mask width and packing corner cases;
   - signed vs unsigned compare metadata;
   - signed division vs unsigned division;
   - arithmetic shift-right sign behavior;
   - mux polarity and n-way mux ordering.

## Build

Use a project-local runtime directory:

```bash
cd <livehd-new>/formal/lean
mkdir -p ../../generated/pass_lean_runtime_tmp
ELAN_HOME=<project-local-elan-home> \
TMPDIR=<livehd-new>/generated/pass_lean_runtime_tmp \
TMP=<livehd-new>/generated/pass_lean_runtime_tmp \
TEMP=<livehd-new>/generated/pass_lean_runtime_tmp \
lake build
```

Build the pass and CLI:

```bash
cd <livehd-new>
bazel build //pass/lean:pass_lean
bazel build //lhd:lhd
```

## Node bit-width cap (`max_width`)

The pass emits the typed fast model as `BitVec w` at each node's real (finite)
width `w`.  `--set formal.lean.max_width=N` caps `w`; the **default is 1024**.  That
default is a pass-level *proof-tractability* guard, **not** a LiveHD limit
(LiveHD's `bits` attribute is a finite `int32`, i.e. widths up to ~2^31), and it
exists only because `native_decide` / `by eval` blow up on very wide words.

To accept whatever finite width a node carries — matching LiveHD's own
finite-but-unbounded widths — pass `0` or `unlimited`:

```bash
--set formal.lean.max_width=0            # or: unlimited / inf / none
```

Notes:
- Constant *value* arbitrary precision is independent of this knob and always on
  (decimal string → Lean `Int` → `BitVec.ofInt w`, reduced mod 2^w).
- With no cap the emitted `BitVec w` definitions still typecheck at any finite
  width, but `native_decide` / `by eval` oracle proofs may be intractable for
  very wide words; the certificate `BV (Nat, Int)` bignum path is the
  width-agnostic reasoning vehicle.
- `w == 0` (an unsized node) is still a hard error even under `unlimited` —
  unsized is not the same as unlimited.
- `pass.isabelle` has the identical knob: `--set formal.isabelle.max_width=0`.

## Smoke Test

The current shell emitter can be tested without touching root-level temporary
directories:

```bash
cd <livehd-new>
mkdir -p generated/pass_lean_smoke/simple/lean \
         generated/pass_lean_smoke/simple/work

bazel-bin/lhd/lhd compile verilog generated/pass_lean_smoke/simple_add.v \
  --reader yosys-verilog \
  --top simple_add \
  --emit-dir lean:generated/pass_lean_smoke/simple/lean \
  --workdir generated/pass_lean_smoke/simple/work \
  --result-json generated/pass_lean_smoke/simple/result.json

cd <livehd-new>/formal/lean
ELAN_HOME=<project-local-elan-home> \
TMPDIR=<livehd-new>/generated/pass_lean_runtime_tmp \
TMP=<livehd-new>/generated/pass_lean_runtime_tmp \
TEMP=<livehd-new>/generated/pass_lean_runtime_tmp \
lake env lean <livehd-new>/generated/pass_lean_smoke/simple/lean/simple_add_Lgraph.lean
```

Expected current result: the generated fast model and non-empty graph
certificate typecheck.  This does not yet prove the fast model equals the
certificate evaluator; that requires the bridge theorem emission listed above.

## DINO LEC Gate Results (RTL-vs-graph, step 2)

`scripts/run_dino_lgraph_lec_gate.sh` proves the post-cprop LGraph is
semantically equivalent to the raw RTL, per design, before any Lean generation:

```text
impl = lhd compile verilog <design .sv> -> post-cprop LGraph
ref  = raw RTL (all modules concatenated), independently elaborated
lhd lec --impl lg:<lg> --ref verilog:<raw.sv> --top <T> --reader yosys-verilog \
        --set lec.engine=auto --set lec.hier=true --set lec.semdiff=structural
```

Because cprop reshapes the impl side, `semdiff=structural` cannot short-circuit;
the auto portfolio (ind|bmc, cvc5) discharges the proof via flop-cut miters.
Measured results (each `0 via semdiff, 1 via solver` — a real semantic proof):

| Design                  | Verdict | Engine (cvc5)     | Flop-cut miters |
|-------------------------|---------|-------------------|-----------------|
| SingleCycleCPU          | PROVEN  | ind, ~0.57 s      | 42 cuts, UNSAT  |
| PipelinedCPU            | PROVEN  | ind, ~0.56 s      | 73 cuts, UNSAT  |
| PipelinedDualIssueCPU   | PROVEN  | ind, ~1.16 s      | 106 cuts, UNSAT |

This is the genuine RTL-faithfulness gate (not a compiler-determinism check):
a mistranslation like the historical `Get_mask(a,-1)` bug would be REFUTED here.

## DINO Lean Gate

**Status — all three DINO CPUs convert RTL → Lean model today.** Each emitted
`<Top>_Lgraph.lean` has typed `<Top>_in` / `<Top>_out` / `<Top>_state` structures
(fixed-width `BitVec n`), the `<Top>_comb` / `<Top>_next` / `<Top>_step` fast
model, and (with `emit_cert=true`) the graph certificate + `evalGraph`-correct
instantiation.

| Design | Lean model | Lean typecheck | RTL ≡ LGraph (LEC gate) |
|---|---|---|---|
| SingleCycleCPU | ~19k lines | model+cert typechecks (~3 min, ~6 GB) | PROVEN — 42 flop-cut miters, cvc5 |
| PipelinedCPU | ~20k lines | model+cert typechecks (~3.7 min, ~6.5 GB) | PROVEN — 73 flop-cut miters, cvc5 |
| PipelinedDualIssueCPU | ~42k lines | **model-only** typechecks (~10 min, ~10 GB); full model+cert is the current scaling target (split generated files) | PROVEN — 106 flop-cut miters, cvc5 |

> Option namespace note: the `pass.lean` knobs are `formal.lean.*`
> (e.g. `formal.lean.emit_cert`), **not** the old `lean.*`.

### A. Generate one design directly (fastest, self-contained)

```bash
cd <livehd-new>
SC=<chisel-build>/build_singlecyclecpu_d          # dir holding SingleCycleCPU*.sv
OUT=generated/dino_readme_ex
mkdir -p "$OUT/work" "$OUT/lean"

./bazel-bin/lhd/lhd compile verilog "$SC"/*.sv \
  --reader yosys-verilog --top SingleCycleCPU \
  --workdir "$OUT/work" --emit-dir lean:"$OUT/lean" \
  --set yosys.setundef=zero \
  --set formal.lean.strict=true \
  --set formal.lean.emit_cert=true          # false = model-only (faster to typecheck)
# -> $OUT/lean/SingleCycleCPU_Lgraph.lean
#    (add --set formal.lean.max_width=0 for unlimited width; see the max_width section)
```

Swap `--top PipelinedCPU` / `PipelinedDualIssueCPU` (and their `build_*_d` dirs)
for the other two designs.

### B. Generate all three via the pipeline script (LEC gate → generate → typecheck)

```bash
cd <livehd-new>
LEAN_EMIT_CERT=true \
OUT=<livehd-new>/generated/dino_lgraph_lean_dev \
scripts/run_dino_lgraph_lean.sh
```

This runs the RTL≡LGraph **LEC gate first** (aborts on REFUTED), then `pass.lean`,
then typechecks each model. Useful env: `RUN_LEC_GATE=false` (skip gate),
`RUN_LEAN=false` (skip typecheck / model-only), `LEAN_EMIT_CERT=false`
(model-only emission), `LEAN_MAX_WIDTH=0` (unlimited width).

### C. Typecheck a generated design by hand

```bash
cd <livehd-new>/formal/lean
TMPDIR=<livehd-new>/generated/dino_lgraph_lean_dev/runtime_tmp \
lake env lean <livehd-new>/generated/dino_lgraph_lean_dev/lean/SingleCycleCPU_Lgraph.lean
```

## CVA6 Lean Gate

**Status — memory modeling ahead of `pass.isabelle`.** `Ntype_op::Memory`
nodes emit a fast model: a function-valued state field
`(BitVec addr -> BitVec data)`, one `mem_read` per read port in `<Top>_comb`, and
the write ports folded (`mem_write` / `mem_write_be`) in `<Top>_next`.
Supported (each beyond the shared `pass.isabelle` v1 restrictions, and pending
back-port there):

- **Any number of read/write ports** — each read port binds a distinct
  `n_<mem>_p<pid>` output; write ports are folded in port order (highest
  `port_id` wins on a same-cycle same-address collision).
- **Read-during-write policy** (`fwd`): `fwd=1` (write-first / transparent) reads
  the post-write image; `fwd=0` (read-first) reads the old array.
- **Sync-read** (`type == 1`): a registered read-data field per read port,
  updated via `sram_sync_read_reg_next`.
- `bits % wensize == 0` (byte/bit write-enable) still required.

The certificate for a memory-bearing design is still a **stub** (node/flop/memory
counts + a `_certificate_counts` theorem) because the `BV` bignum certificate
evaluator is bit-vector-only; the memory-aware evaluator + cert bridge is the
next step.

Minimal memory example (async-read / sync-write SRAM), verified to typecheck:

```bash
cd <livehd-new>
cat > ram1.sv <<'EOF'
module ram1(input logic clk, input logic we, input logic [3:0] waddr,
            input logic [7:0] wdata, input logic [3:0] raddr, output logic [7:0] rdata);
  logic [7:0] mem [0:15];
  always_ff @(posedge clk) if (we) mem[waddr] <= wdata;
  assign rdata = mem[raddr];
endmodule
EOF
bazel-bin/lhd/lhd compile verilog ram1.sv --reader yosys-slang --top ram1 \
  --workdir <out>/work --emit-dir lean:<out>/lean \
  --set yosys.setundef=zero --set formal.lean.strict=true --set formal.lean.emit_cert=true
# -> <out>/lean/ram1_Lgraph.lean :
#      structure ram1_state where st_rdata : (BitVec 4 -> BitVec 8)  deriving Inhabited
#      ram1_comb ... mem_read s.st_rdata ...
#      ram1_next ... mem_write s.st_rdata ...
#      -- Certificate: STUB (counts only)
```
(`yosys-slang` reader; the `yosys-verilog` reader can trip a yosys `memory -nomap`
assert on some RAM shapes. A function-typed state field cannot `deriving Repr`, so
`<Top>_state` derives `Inhabited` only when it holds a memory.)

Real CVA6 SRAM (`tc_sram`) via the module stress runner:

```bash
cd <livehd-new>
CVA6_TOP=tc_sram LEAN_EMIT_CERT=true scripts/run_cva6_module_lean_stress.sh
```

> Two shared latent bugs surfaced while porting and were fixed in `pass.lean`
> (item 1 was applied to `pass.isabelle` on 2026-07-29, when the new `undef` pin
> at 15 made the mis-decode reachable — `15 % 11 == 4` fabricated a phantom
> port-1 `enable`):
> 1. the per-port pin stride is `Ntype::Memory_port_stride` (now **16**), not the
>    hardcoded `11` both passes used — stale `11` mis-decodes memory port pins;
> 2. the `fwd` / `posclk` policy pins are parsed but unused in the async/array
>    emission, so a non-constant driver is tolerated (default) instead of aborting
>    — the current front-end drives `fwd` non-constant, which previously blocked
>    every fresh memory in both passes.

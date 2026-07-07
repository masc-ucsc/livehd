# pass.lean

`pass.lean` is the Lean target for the LiveHD graph-to-theorem-prover flow.  It
is intentionally modeled after `pass.isabelle`, but it is a separate pass and a
separate proof stack.

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
- Certificate emission can be disabled with `--set lean.emit_cert=false` for
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
                                   lec.hierarchical=true,
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

That fast-view proof is intentionally not emitted yet, because the fast model is
a fixed-width `BitVec` let-chain while the certificate evaluator uses the
dynamic `BV` representation.  It should be generated after the certificate
checker is scalable.

## Remaining Implementation Work

1. Port scalable certificate checking.
   - const-only chunks
   - simple mixed chunks
   - concrete dependency-list subset checks
   - chunked uniqueness
   - eventually dense topological certificates

2. Emit per-design fast-view bridge theorems.
   - `<Top>_comb = <Top>_comb_cert`
   - `<Top>_next = <Top>_next_cert`
   - `<Top>_step = <Top>_step_cert`

3. Port memory-node emission and certificates from `pass.isabelle`.
   - function-valued memory state fields;
   - read/write/byte-enable policy extraction;
   - memory `NodeCert` operator extension;
   - collision/read-first/write-first policy proofs.

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
        --set lec.engine=auto --set lec.hierarchical=true --set lec.semdiff=structural
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

Generate the three DINO CPU models:

```bash
cd <livehd-new>
RUN_LEAN=false \
LEAN_EMIT_CERT=true \
OUT=<livehd-new>/generated/dino_lgraph_lean_dev \
scripts/run_dino_lgraph_lean.sh
```

Then typecheck individual generated designs from the Lean package directory:

```bash
cd <livehd-new>/formal/lean
ELAN_HOME=<project-local-elan-home> \
TMPDIR=<livehd-new>/generated/dino_lgraph_lean_dev/runtime_tmp \
TMP=<livehd-new>/generated/dino_lgraph_lean_dev/runtime_tmp \
TEMP=<livehd-new>/generated/dino_lgraph_lean_dev/runtime_tmp \
lake env lean <livehd-new>/generated/dino_lgraph_lean_dev/lean/SingleCycleCPU_Lgraph.lean
```

Measured status on the current generated DINO artifacts:

- `SingleCycleCPU_Lgraph.lean`: typechecks, about 3:11 wall time and 5.9 GB RSS.
- `PipelinedCPU_Lgraph.lean`: typechecks, about 3:40 wall time and 6.5 GB RSS.
- `PipelinedDualIssueCPU_Lgraph.lean`: model-only generation and typechecking
  succeed with `LEAN_EMIT_CERT=false`, about 9:44 wall time and 9.9 GB RSS.
  Full model+certificate Lean typechecking was stopped after more than
  22 minutes.  This is the next scaling target and should be addressed by
  splitting generated model/cert data into smaller files or sections.

## CVA6 Lean Gate

Use module-level stress first:

```bash
cd <livehd-new>
CVA6_TOP=tc_sram \
RUN_LEAN=false \
LEAN_EMIT_CERT=true \
scripts/run_cva6_module_lean_stress.sh
```

Use the full-core stress runner only as a completeness probe:

```bash
cd <livehd-new>
CVA6_TARGET=cv64a6_imafdc_sv39_hpdcache_wb \
CVA6_BLACKBOX_MODULES="fpnew_top fpnew_top_multi" \
scripts/run_cva6_complex_lean_stress.sh
```

Current expected CVA6 result: compilation reaches `pass.lean` until an explicit
unsupported construct is encountered.  Full CVA6 Lean model generation is not a
passing gate yet because `Ntype_op::Memory` emission/certificates still need to
be ported from the Isabelle path.

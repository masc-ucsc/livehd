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
- `pass.lean` is registered as a LiveHD pass and currently emits a Lean
  certificate shell per graph.

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

The first link is emitted per design:

```lean
<Top>_comb i s =
  outputsFromCert (evalGraph <Top>_graphCert.topo <Top>_graphCert (<Top>_sourceEnv i s))

<Top>_next i s =
  nextStateFromCert (evalGraph <Top>_graphCert.topo <Top>_graphCert (<Top>_sourceEnv i s))
```

These bridge theorems are not emitted yet.

## Remaining Implementation Work

1. Port graph traversal from `pass.isabelle`.
   - roots = output drivers + flop/memory next-state drivers
   - reverse fan-in over combinational nodes
   - stop at graph inputs, constants, flop outputs, and memory state
   - topologically order reachable internal nodes

2. Emit concrete Lean fast models.
   - `<Top>_in`
   - `<Top>_out`
   - `<Top>_state`
   - `<Top>_comb`
   - `<Top>_next`
   - `<Top>_step`

3. Emit concrete graph certificates.
   - node certificates
   - chunked certificate lists
   - source/output/flop-driver maps
   - memory-node certificates when memory lowering is preserved

4. Port scalable certificate checking.
   - const-only chunks
   - simple mixed chunks
   - concrete dependency-list subset checks
   - chunked uniqueness
   - eventually dense topological certificates

5. Emit per-design bridge theorems.
   - `<Top>_comb_refines_cert`
   - `<Top>_next_refines_cert`
   - `<Top>_step_refines_lgraph_certificate`

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

Expected current result: the generated certificate shell typechecks.  This does
not yet prove a non-empty graph translation; that requires the concrete graph
certificate and fast-model bridge emission listed above.

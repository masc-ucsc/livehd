# LiveHD LGraph to Isabelle Translation Correctness

This directory contains the Isabelle proof stack for showing that Isabelle
theories emitted by `pass.isabelle` preserve the semantics of the source LiveHD
LGraph.

The trusted boundary for this proof stack is the exported LGraph.  Proving that
the LGraph itself is equivalent to the original SystemVerilog RTL is a separate
RTL-to-LGraph validation/LEC problem.

## Approach: Certifying Translation

`pass.isabelle` is a C++ pass and is not itself verified.  Instead, it emits,
next to the readable fixed-width word model, a checkable graph certificate for
each design.  Isabelle proves the certificate evaluator and well-formedness
checker sound once, generically, and then checks each generated artifact.

This does not prove the C++ implementation correct for all possible graphs.  It
reduces trust in the C++ pass by making each emitted artifact carry enough
evidence to be validated inside Isabelle.

The certificate and the fast word model are produced by the same emitter, so
their agreement cannot by itself expose an emitter semantic bug that both
share.  `scripts/run_dino_lgraph_lec_gate.sh` runs an RTL-vs-LGraph LEC gate on
the DINO designs; run it before generation.

## Terminology

- **Generated Fast Word Model** is the direct fixed-width Isabelle code emitted
  by `pass.isabelle` in `<top>_Lgraph.thy`, for example `SingleCycleCPU_comb`,
  `SingleCycleCPU_next`, and `SingleCycleCPU_step`.  It uses concrete Isabelle
  word types, let-bound node expressions, record selectors, and record updates.
  It is the model intended for downstream DINO/Sail refinement proofs because
  it is direct and simplifiable.
- **Evaluated Certificate** is the result of running the generic certificate
  evaluator on the generated certificate and a source environment:
  `eval_graph (topo cert) cert source_env`.  Operationally, it is an
  environment mapping node IDs to computed values, followed by projections into
  outputs or next-state fields.
- **Mathematical Certificate Semantics** is the denotational semantics of the
  certificate, written `graph_denotation`.  It is the semantics of the
  certificate, not an independent formalization of LiveHD LGraph semantics.
  This directory does not contain a certificate-to-LGraph correspondence
  theorem (`graph_denotation cert = lgraph_denotation G` for the exported graph
  `G`); the certificate is built by `pass.isabelle` from the exported LGraph.

## Proof Structure

Translation correctness is the composition of three proof links:

```text
generated fast word model
  = evaluated certificate
  = mathematical certificate semantics
  with certificate well-formedness established by a proved checker
```

### Link 1: Evaluated Certificate = Mathematical Certificate Semantics

This is a generic theorem, proved once and independent of any design:

```isabelle
theorem eval_graph_correct:
  "env_correct_on (set order)
     (eval_graph order G source_env)
     (graph_denotation order G source_env)"

theorem eval_graph_correct_for_cert:
  "env_correct_on (set (topo G))
     (eval_graph (topo G) G source_env)
     (graph_denotation (topo G) G source_env)"
```

It applies to any certificate whose operations are represented by `lgraph_op`,
and removes the need for generated per-node local correctness lemmas.  The
executable evaluator interprets nodes through `eval_op`; the denotation
interprets them through `denote_op`; `eval_op_correct` and
`eval_graph_eq_denote_graph` connect the two.

A certificate contains compact node descriptions such as:

```text
n42 = Op_Sum [n7, n9] width 64
n43 = Op_GetMask [n42, m] ...
n44 = Op_MuxBool [sel, a, b]
```

The evaluator walks the topological node list and builds an environment `rho`
from node IDs to values; output and next-state maps then read values out of
`rho`.  The `source_env` argument supplies values for source nodes, i.e. values
not computed by internal graph nodes.  For a CPU LGraph these are primary input
ports (reset, instruction-memory data, data-memory responses) and current
flop/register state.  A generated `<top>_source_env i s` maps certificate
source IDs to values extracted from the input record `i` and state record `s`:

```text
source_env reset_id     = reset i
source_env pc_flop_id   = pc s
source_env imem_data_id = imem_rdata i
```

Implementation files:

- `Translation_LGraph_Model.thy`: `lgraph_op`, width-carrying bitvector values
  (`bv`), `denote_op`/`eval_op`, `node_cert`, `graph_cert`, `eval_graph`,
  `denote_graph`, and local evaluator/denotation alignment.
- `Translation_Certificate_Evaluator.thy`: `graph_denotation`,
  `eval_graph_correct`, `eval_graph_correct_for_cert`.

### Link 2: Generated Fast Word Model = Evaluated Certificate

For each design, the generated certificate theory `<top>_Lgraph_Cert.thy`
defines:

- `<top>_source_env`, mapping input ports and flop outputs to certificate
  source IDs;
- `<top>_outputs_from_cert`, mapping evaluated output driver IDs to the output
  record;
- `<top>_next_state_from_cert`, mapping evaluated flop next-state driver IDs to
  the state record;
- `<top>_cert_step`, the step function obtained from the evaluated certificate,
  together with:

```isabelle
theorem <top>_cert_step_refines_lgraph_certificate:
  "<top>_cert_step i s =
     (let rho = eval_graph (topo <top>_graph_cert) <top>_graph_cert (<top>_source_env i s)
      in (<top>_next_state_from_cert rho, <top>_outputs_from_cert rho))"
```

The design-independent machinery for relating the fast word model to the
evaluated certificate lives in three theories:

- `Translation_OpBridge.thy` (per-operator bridge): lemmas relating the
  certificate evaluator `eval_op` on untyped `bv` values to the fast model's
  native `'w::len word` operations, through the encoding `bvenc`.  The lemmas
  are width-polymorphic; `ucast` on the fast side corresponds to `bv_resize` on
  the certificate side (`bvenc_ucast`).
- `Translation_GraphRefine.thy` (graph refinement): on a dependency-ordered
  certificate, any environment `phi` satisfying the local per-node recurrence
  `phi n = eval_node G phi n` agrees with `eval_graph`:

  ```isabelle
  theorem eval_graph_of_local_agree:
    assumes "dep_ordered G (topo G)"
    assumes "\<forall>n \<in> set (topo G). nodes G n \<noteq> None"
    assumes "\<forall>n \<in> set (topo G). phi n = eval_node G phi n"
    assumes "\<forall>n \<in> set (topo G). \<forall>d \<in> set (deps_of G n).
               d \<notin> set (topo G) \<longrightarrow> rho d = phi d"
    assumes "n \<in> set (topo G)"
    shows "eval_graph (topo G) G rho n = phi n"
  ```

  Each recurrence obligation mentions only one node and its dependencies, so
  per-node lemmas are independent and Isabelle can check them in parallel.  `eval_graph_not_mem` covers IDs outside the
  order (outputs or flop inputs driven directly by a constant, input, or flop),
  and `dep_ordered_acc_sound` gives a linear accumulator check for
  `dep_ordered`.
- `Translation_BT.thy`: a binary-tree lookup (`BT`, `bt_find`, `bt_keys`) for
  defining per-design `phi`/`nodes` functions, plus lemmas for splitting a
  bounded quantifier over a list literal.

The per-node values such a proof is stated over are emitted by `pass.isabelle`
with `formal.isabelle.emit_fast_bridge=true`: each internal node becomes a
top-level definition `<top>_fv<id>` instead of a let-binding.  The pass itself
does not emit a per-design fast-model-equals-certificate theorem.

### Link 3: Certificate Well-Formedness

The evaluator theorem is meaningful only for well-formed certificates.  For a
malformed certificate, a node could depend on a missing ID, two nodes could
share an ID, a width could be zero, or an output or flop driver could point to
a missing node.  `graph_cert_wf` (in `Translation_LGraph_Model.thy`) states:

- topological node IDs are distinct, and source IDs are distinct;
- internal nodes and sources are disjoint;
- every node in the topological order has a certificate whose `nid` matches,
  whose width is positive, and whose dependencies are internal nodes or sources;
- sources have no node certificate.

Checking `graph_cert_wf` by unfolding it over a DINO-sized graph is too slow, and
so is a single whole-graph `by eval` over the generated `node_cert` lists.  The
library therefore proves boolean checkers sound:

- `graph_cert_wf_bool_sound`: one whole-graph boolean check implies
  `graph_cert_wf` (used by `cert_wf:eval`; practical only for small designs).
- `graph_cert_wf_from_chunk_checks`: per-chunk checks
  (`node_cert_chunk_wf_bool`), chunk ID lists, and global ID/source
  distinctness imply `graph_cert_wf` (used by `cert_wf:chunked`).
- `const_node_cert_chunk_wf_bool_sound`: discharges a constant-only chunk from
  `const_node_cert_wf_bool` plus chunk-local ID facts.
- `simple_node_cert_chunk_wf_bool_sound'`: discharges a mixed chunk from the
  shape checker `simple_node_cert_shape_wf_bool` plus a concrete
  dependency-subset fact.

See [Chunked Certificate Proofs](#chunked-certificate-proofs) for the generated
lemma structure.

### Generic Composition Theorems

These theories hold the generic combinational, sequential, and whole-step
composition theorems.  They are stated over abstract generated and
specification functions.

**Combinational DAG** (`Translation_Combinational.thy`).  A generic
topological-order induction: if every emitted node expression is locally
correct whenever its dependencies are already correct, evaluating the whole
let-chain makes every emitted node equal to its denotation.

```isabelle
theorem generated_comb_equals_lgraph_denotation:
  assumes "topo_ok deps topo"
  assumes "distinct topo"
  assumes local_correct:
    "\<And>prefix rho n.
       n \<in> set topo \<Longrightarrow>
       set (deps n) \<subseteq> set prefix \<Longrightarrow>
       env_correct_on (set (deps n)) rho denote \<Longrightarrow>
       eval rho n = denote n"
  shows "env_correct_on (set topo)
           (eval_nodes topo eval source_env) denote"
```

**Flops and next state** (`Translation_Flops.thy`).  The locale
`flop_translation` proves `generated_next_equals_lgraph_flop_step` from one
per-flop equality, using the locale's field-wise state extensionality
assumption (`state_eqI`).
`generated_next` computes all flop values from the old state `s` and then
updates every field at once, matching synchronous clock-edge semantics.  The
flop transition is:

```isabelle
lgraph_flop_next reset_active reset_value enable_active din current =
  (if reset_active then reset_value
   else if enable_active then din
   else current)
```

and `generated_flop_next_equals_lgraph_flop_next` relates it to the
`flop_next` helper used by the generated model.

**Whole step** (`Translation_Step.thy`).

```isabelle
theorem generated_step_equals_lgraph_step:
  assumes "gen_next i s = spec_next i s"
  assumes "gen_comb i s = spec_comb i s"
  shows "generated_step gen_next gen_comb i s =
         lgraph_step spec_next spec_comb i s"
```

Each generated certificate theory instantiates it as
`<top>_translation_correct_conditional`.

**Operator lemmas** (`Translation_Op_Lemmas.thy`).  Reusable local
correctness/congruence lemmas for emitted node expressions (`local_*_correct`,
`source_input_correct`, `source_flop_correct`).

## Certificate Encoding

- `pass.isabelle` builds the certificate from `reachable_topo_order` and
  rejects zero-width or unsupported reachable nodes before writing it.
- Constants and internal nodes are `node_cert` entries; primary inputs and
  flop outputs are source nodes supplied by the source environment.
- Constants reused at different consumer widths are represented by synthetic
  certificate node IDs, so a LiveHD zero-width/untyped constant is
  width-specialized without changing the source LGraph node.
- A negative constant such as LiveHD `Lconst(-1)` is emitted as
  `Op_Const (- 1)`, so Isabelle parses it as a negative integer argument.
- Designs with LGraph Memory nodes get a certificate stub (node, flop, and
  memory counts only).  The certificate evaluator is bitvector-only, so no
  `graph_cert_wf` or evaluator theorems are emitted for them, and
  `emit_fast_bridge` is refused.

## Semantic Conventions

`denote_op` in `Translation_LGraph_Model.thy` fixes these conventions:

- A value is a width plus an integer.  Every result is reduced modulo
  `2^width` (`mk_bv`), and constants are arbitrary integers, so constants wider
  than 64 bits are supported.
- Comparisons, `Op_EQ`, and `Op_Ror` produce `0`/`1` at the node width.
  Unsigned comparisons use the operand's unsigned value at its own width;
  `Op_SLT`/`Op_SGT` use the signed value (`bv_sint`).
- `Op_Div`, `Op_UDiv`, and `Op_SDiv` return `0` for a zero divisor; `Op_SDiv`
  truncates toward zero.
- `Op_SRA` divides the signed operand value by `2^shamt`.
- `Op_GetMask` packs the source bits at the mask's set positions into the low
  bits of the result; `Op_SetMask` writes value bits into the mask-selected
  positions.
- Mux data operands are resized to the node width; an out-of-range `Op_MuxN`
  selector yields `0`.

## Chunked Certificate Proofs

With `cert_wf:chunked`, the certificate is split into chunks of
`cert_chunk_size` nodes, and the generator classifies each chunk as
`const-only`, `simple-mixed`, or unsupported.  Const-only and simple-mixed
chunks are proved by `simp` and the reusable soundness lemmas above, without
`by eval` over generated records or global ID lists.

Generated per-chunk lemmas:

- `<top>_node_certs_i`: the chunk-local list of `node_cert` records.
- `<top>_node_certs_ids_i`: defined as `map nid <top>_node_certs_i`.
- `<top>_cert_chunk_i_ids`: the derived ID-list equality, by `simp`.
- `<top>_cert_chunk_i_ids_distinct`: chunk-local node-ID uniqueness.
- `<top>_cert_chunk_i_ids_subset`: the chunk's node IDs are in the global
  certificate ID list.
- `<top>_cert_chunk_i_ids_disjoint`: the chunk's node IDs are disjoint from the
  source IDs.
- `<top>_cert_chunk_i_const` (const-only chunks): the constant-node shape
  predicate.
- `<top>_cert_chunk_i_shape` (simple-mixed chunks): the simple-op shape
  predicate.
- `<top>_cert_chunk_i_deps` (simple-mixed chunks): a concrete dependency list
  for the chunk, and a lemma that the chunk's dependencies are contained in it.
- `<top>_cert_chunk_i_deps_subset` (simple-mixed chunks): those dependencies
  are internal nodes or sources.  The generator computes `needed_id_chunks` and
  unfolds only the ID chunks that contain dependencies the chunk actually uses.
- `<top>_cert_chunk_i_ok`: the final per-chunk fact
  `node_cert_chunk_wf_bool <top>_cert_all_ids <top>_cert_sources <top>_node_certs_i`,
  by `const_node_cert_chunk_wf_bool_sound` or
  `simple_node_cert_chunk_wf_bool_sound'`.
- `<top>_cert_chunk_checks`: all `<top>_cert_chunk_i_ok` lemmas, collected
  into `<top>_cert_chunks_ok`, which `<top>_graph_cert_wf` uses with
  `graph_cert_wf_from_chunk_checks`.

The simple shape checker (`simple_op_cert_wf_bool`) accepts:

```text
Op_Const
Op_Sum
Op_And / Op_Or / Op_Xor, including n-ary reductions
Op_Ror
Op_Not
Op_EQ
Op_ULT / Op_UGT / Op_SLT / Op_SGT
Op_GetMask
Op_MuxBool
Op_MuxN
Op_SHL
Op_SRA
Op_Sext
```

This checker proves only structural local well-formedness: positive widths,
arity/result-width constraints, and dependency validity via the concrete
dependency subset.  Operator semantics are covered by the evaluator and
operator-lemma theories, not by the shape checker.

What each `cert_wf` mode proves:

| Mode | `<top>_graph_cert_wf` |
|------|-----------------------|
| `skip` (default) | Not stated; the certificate data is still typechecked. |
| `eval` | Proved by one whole-graph `by eval` and `graph_cert_wf_bool_sound`. |
| `sorry` | Admitted with `sorry`. |
| `chunked` | Assembled from per-chunk lemmas by `graph_cert_wf_from_chunk_checks`.  The global ID uniqueness lemma `<top>_cert_all_ids_distinct` is emitted with `sorry`.  When `cert_chunk_limit` truncates the certificate, the theory does not state `graph_cert_wf`. |

Under `cert_wf:chunked`, an unsupported chunk is handled by
`cert_wf_fallback`: `fail` (default) stops generation, `sorry` admits that
chunk, and `eval` proves it by `by eval`, which is intended only for debugging.
Every generated certificate theory for a non-memory design proves
`<top>_eval_graph_correct` (by `eval_graph_correct_for_cert`) regardless of
mode.

## Covered Designs

The generation script targets the three DINO CPU variants:

| Design | Isabelle sessions |
|--------|-------------------|
| `SingleCycleCPU` | `DINO-Lgraph-SingleCycle-Model`, `DINO-Lgraph-SingleCycle-Cert` |
| `PipelinedCPU` | `DINO-Lgraph-Pipelined-Model`, `DINO-Lgraph-Pipelined-Cert` |
| `PipelinedDualIssueCPU` | `DINO-Lgraph-DualIssue-Model`, `DINO-Lgraph-DualIssue-Cert` |

Results:

- Each generated certificate theory instantiates the generic evaluator theorem
  as `<top>_eval_graph_correct`.
- With `CERT_WF_MODE=chunked`, `CERT_CHUNK_SIZE=25`, `CERT_CHUNK_LIMIT=100`,
  and `CERT_WF_FALLBACK=fail`, certificate generation succeeds for all three
  designs (every emitted chunk is const-only or simple-mixed).
- With the same settings, `DINO-Lgraph-SingleCycle-Cert` builds: the per-chunk
  well-formedness lemmas are proved for the first 100 chunks (2,500 nodes) of
  the `SingleCycleCPU` certificate.  This chunk-limited theory does not state
  `graph_cert_wf` for the whole certificate.

## Usage

### Inputs and Related Files

- Generator: `pass/isabelle/pass_isabelle.cpp`, `pass/isabelle/pass_isabelle.hpp`
- Primitive helper definitions:
  `formal/semantic_primitives/SemanticPrimitives.thy`
- Primitive helper tests: `formal/semantic_primitives/SemanticPrimitives_Test.thy`
- This directory's session: `ROOT` declares `LGraph-Translation-Correctness`,
  whose parent is `DINO_Semantic_Primitives_Test` from
  `formal/semantic_primitives/ROOT`.

### Isabelle Build Environment Rule

Do not run DINO/LGraph Isabelle heap builds in a sandboxed process environment
that cannot write Poly/ML heap and database files.  Such builds fail with:

```text
I/O error: Operation not permitted
```

This is a filesystem-permission failure, not an Isabelle proof failure.  It
usually appears before the generated certificate theories are reached, for
example while writing the `LGraph-Translation-Correctness` heap.

Use a project-local isolated Isabelle home and a project-local temporary
directory, never `/tmp`, and run the build outside the sandbox when heap writes
are required.

A manual build from the repository root:

```bash
OUT="$PWD/generated/dino_lgraph_isabelle"
mkdir -p "$OUT/runtime_tmp" "$OUT/isabelle_home"

HOME="$OUT/isabelle_home" \
TMPDIR="$OUT/runtime_tmp" TMP="$OUT/runtime_tmp" TEMP="$OUT/runtime_tmp" \
isabelle build -v \
  -o document=false \
  -o browser_info=false \
  -d formal/semantic_primitives \
  -d formal/translation_correctness \
  -d "$OUT/isabelle" \
  DINO-Lgraph-SingleCycle-Cert
```

To build only the generic library, drop `-d "$OUT/isabelle"` and build the
`LGraph-Translation-Correctness` session instead.

### Generating DINO Artifacts

Build `lhd` first with `bazel build //lhd:lhd`, then compile each design's
SystemVerilog to an LGraph and run `pass.isabelle` on it:

```bash
OUT="$PWD/generated/dino_lgraph_isabelle"
bazel-bin/lhd/lhd compile verilog <design .sv files> \
  --reader yosys-verilog \
  --top SingleCycleCPU \
  --emit-dir isabelle:"$OUT/isabelle" \
  --set yosys.setundef=zero \
  --set formal.isabelle.cert_wf=chunked \
  --set formal.isabelle.cert_wf_fallback=fail \
  --set formal.isabelle.cert_chunk_size=25
```

This writes `$OUT/isabelle/<Top>_Lgraph.thy` (the generated fast word model)
and `$OUT/isabelle/<Top>_Lgraph_Cert.thy` (the graph certificate and its
theorems).  The pass output includes per-chunk summaries such as:

```text
pass.isabelle cert chunks for SingleCycleCPU:
  total_nodes=5383 chunk_size=25 emitted_chunks=50 full_chunks=216
  chunk 0:  nodes=25 class=const-only ops={Op_Const=25}
  ...
  chunk 35: nodes=25 class=simple-mixed
    ops={Op_And=2, Op_Const=9, Op_EQ=2, Op_GetMask=7,
         Op_MuxBool=1, Op_SHL=1, Op_SRA=1, Op_Sext=2}
```

To build the result, give each design a Model/Cert session pair.  The
certificate theory imports the model by bare name, so inside the pair qualify
that import as `"<name>-Model.<Top>_Lgraph"`.  With
`sessions/<Top>_model/ROOT`:

```text
session "<name>-Model" = "LGraph-Translation-Correctness" +
  options [document = false, browser_info = false]
  theories
    <Top>_Lgraph
```

`sessions/<Top>_cert/ROOT`:

```text
session "<name>-Cert" = "<name>-Model" +
  options [document = false, browser_info = false]
  theories
    <Top>_Lgraph_Cert
```

and both directories listed in `$OUT/isabelle/ROOTS`, the isolated-home build
above checks the pair (`DINO-Lgraph-SingleCycle-Cert` there is the Cert session
for `SingleCycleCPU`).

### `pass.isabelle` Options

When invoking `lhd` directly, pass these with `--set formal.isabelle.<option>=<value>`:

| Option | Default | Meaning |
|--------|---------|---------|
| `strict` | `true` | Abort on unsupported ops |
| `normalize` | `true` | Normalize pre-export width artifacts |
| `max_width` | `1024` | Cap on node width (`0` or `unlimited` = no cap) |
| `emit_fast_bridge` | `false` | Emit per-node fast values `<top>_fv<id>`; non-memory designs only |
| `cert_wf` | `skip` | `skip`, `eval`, `sorry`, or `chunked` |
| `cert_wf_fallback` | `fail` | `fail`, `sorry`, or `eval` for unsupported chunks under `cert_wf:chunked` |
| `cert_chunk_size` | `25` | Node certificates per chunk |
| `cert_chunk_limit` | `0` | Emit only the first N chunks, for proof-shape testing (0 = all) |

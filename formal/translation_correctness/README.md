# LiveHD LGraph to Isabelle Translation Correctness

This directory tracks the proof stack for showing that Isabelle theories emitted
by `pass.isabelle` preserve the semantics of the source LiveHD LGraph.

The trusted boundary for this proof stack is the exported LGraph.  Proving that
the LGraph itself is equivalent to the original SystemVerilog RTL is a separate
RTL-to-LGraph validation/LEC problem.

## Direct C++ Translator-Correctness Options

If we insist on using the C++ `pass.isabelle` implementation and also want a
generic proof for all translations, then we must either verify the C++ program
itself or verify a precise formal model of it and prove that the C++ source or
binary conforms to that model.  There is no shortcut: a theorem about arbitrary
outputs of an unverified C++ translator cannot follow just from testing one or
many generated designs.

The realistic options are:

1. Verify the C++ implementation directly.
2. Prove the C++ implementation refines a formal translator specification.
3. Use certifying translation and check each emitted artifact.
4. Trust the C++ translator as part of the trusted computing base.

If per-design certificate checking is rejected, then the remaining rigorous
choices are verifying C++ or trusting C++.  The current project deliberately
does not take the last option as a proof method.

For direct C++ verification, the desired theorem shape is:

```text
For all well-formed LiveHD graphs G:
  C++ pass.isabelle(G) emits artifact A
  and artifact_denotation(A) = lgraph_denotation(G)
```

To prove this directly, we would need a formal semantics for enough C++ plus
the LiveHD graph APIs used by `pass.isabelle`.  In practice, that means one of:

- Use a C/C++ verification framework such as VCC, Frama-C-like tooling for C
  subsets, VeriFast, Coq/VST after rewriting to C, or Isabelle AutoCorres after
  rewriting or extracting to C.
- Rewrite the translator in a verifiable subset of C or Rust and verify that
  implementation.
- Treat LiveHD APIs as abstract specifications, then prove the pass code
  refines those specifications.

This is likely far more work than the CPU refinement proof itself because
`pass.isabelle` is not just a pure mathematical function.  It performs graph
traversal, reachability filtering, topological ordering, width handling,
operator mapping, source/flop/output classification, name sanitization, string
emission, file generation, option handling, and diagnostics.

The chosen approach below is therefore a certifying-translation approach:
`pass.isabelle` remains C++, but it emits checkable certificates and bridge
definitions.  Isabelle proves generic checker/evaluator soundness and then
checks each generated artifact.  This does not prove the C++ implementation is
correct for all possible graphs; instead, it reduces trust in the C++ pass by
making each emitted artifact carry enough evidence to be validated inside
Isabelle.

## Locked Translation-Correctness Strategy

The chosen method for formally verifying LGraph-to-Isabelle translation is
Strategy B with an explicit fast-model bridge.  The certificate evaluator is
the semantic core, and the generated fixed-width word model remains the model
used by downstream DINO proofs.  Translation correctness is therefore the
composition of three proof links:

```text
generated fast word model
  = evaluated certificate
  = mathematical certificate semantics
  with certificate well-formedness established by a proved checker
```

Concretely, for every generated design `<top>`, the final translation theorem
should be derived from:

```isabelle
<top>_comb i s =
  outputs_from_cert
    (eval_graph (topo <top>_graph_cert) <top>_graph_cert (source_env i s))

<top>_next i s =
  next_state_from_cert
    (eval_graph (topo <top>_graph_cert) <top>_graph_cert (source_env i s))

env_correct_on (set (topo <top>_graph_cert))
  (eval_graph (topo <top>_graph_cert) <top>_graph_cert source_env)
  (graph_denotation (topo <top>_graph_cert) <top>_graph_cert source_env)

graph_cert_wf <top>_graph_cert
```

The first two facts connect the readable generated word definitions to the
evaluated certificate.  The third fact connects the evaluator to the
mathematical certificate semantics.  The fourth fact proves that the concrete
certificate has valid IDs, widths, dependencies, sources, and topological
ordering.

Terminology used in this document:

- **Generated Fast Word Model** is the direct fixed-width Isabelle code emitted
  by `pass.isabelle`, for example:
  `SingleCycleCPU_comb`,
  `SingleCycleCPU_next`, and
  `SingleCycleCPU_step`.  It uses concrete Isabelle word types, let-bound node
  expressions, record selectors, and record updates.  This is the model we want
  downstream DINO/Sail refinement proofs to use because it is direct and
  simplifiable.
- **Evaluated Certificate** is the concrete result of running the generic
  certificate evaluator on the generated certificate and a source environment:
  `eval_graph (topo cert) cert source_env`.  Operationally, it is an
  environment mapping node IDs to computed values, followed by projections into
  outputs or next-state fields.
- **Mathematical Certificate Semantics** is the abstract denotational semantics
  of the certificate, written as `graph_denotation`.  It is not automatically
  the official LiveHD LGraph semantics.  Strictly, there are two semantic
  objects:
  `graph_denotation cert` and `lgraph_denotation G`.  To connect to the source
  LiveHD graph, we still need a certificate-to-LGraph correspondence theorem or
  checker showing that `graph_denotation cert = lgraph_denotation G` for the
  exported graph `G`.

This section supersedes earlier wording that treated the fast-model bridge as
optional.  The bridge is required when downstream proofs cite `<top>_comb`,
`<top>_next`, or `<top>_step`, which is the intended workflow for DINO/Sail
refinement.

### Link 1: Evaluated Certificate = Mathematical Certificate Semantics

This is the generic theorem proved once in Isabelle:

```isabelle
eval_graph_correct:
  env_correct_on (set order)
    (eval_graph order G source_env)
    (graph_denotation order G source_env)
```

Its role is to prove that the executable graph evaluator agrees with the
independent graph denotation.  This removes the need for thousands of generated
per-node `local_correct_n*` lemmas.  The proof is independent of DINO and
applies to any certificate whose operations are represented by `lgraph_op`.

The certificate evaluator executes the graph certificate inside Isabelle.  A
certificate contains compact node descriptions such as:

```text
n42 = Op_Add [n7, n9] width 64
n43 = Op_GetMask [n42] ...
n44 = Op_MuxBool [sel, a, b]
```

The evaluator walks the topological node list and builds an environment `rho`
from node IDs to values:

```text
rho[n7]  = ...
rho[n9]  = ...
rho[n42] = eval_op Add rho[n7] rho[n9]
rho[n43] = eval_op GetMask rho[n42]
...
```

At the end, output and next-state maps read values out of `rho`.  Thus:

```isabelle
eval_graph cert source_env
```

means: evaluate all certified graph nodes using the given values for graph
sources.

The `source_env` argument supplies values for source nodes: values that are not
computed by internal graph nodes.  For a CPU LGraph, sources are typically:

- primary input ports, such as reset, instruction-memory data, and data-memory
  responses,
- current flop/register state, such as PC, pipeline registers, and register
  file flops,
- black-box or memory-interface inputs, when present.

For example, a generated environment such as:

```isabelle
SingleCycleCPU_source_env i s
```

maps certificate source IDs to values extracted from the input record `i` and
state record `s`:

```text
source_env reset_id     = reset i
source_env pc_flop_id   = pc s
source_env imem_data_id = imem_rdata i
```

Implementation files:

- `Translation_LGraph_Model.thy`
- `Translation_Certificate_Evaluator.thy`

### Link 2: Generated Fast Word Model = Evaluated Certificate

This is generated per design:

```isabelle
<top>_comb_refines_cert:
  <top>_comb i s =
    outputs_from_cert
      (eval_graph (topo <top>_graph_cert) <top>_graph_cert (source_env i s))

<top>_next_refines_cert:
  <top>_next i s =
    next_state_from_cert
      (eval_graph (topo <top>_graph_cert) <top>_graph_cert (source_env i s))
```

Its role is to connect the ergonomic, fixed-width word model emitted by
`pass.isabelle` to the verified certificate semantics.  Without this link,
`eval_graph_correct` only verifies the certificate evaluator, not the
`<top>_comb` and `<top>_next` definitions used by CPU refinement proofs.

For example, the required bridge for `SingleCycleCPU` is:

```isabelle
SingleCycleCPU_comb i s =
  outputs_from_cert
    (eval_graph SingleCycleCPU_graph_cert
      (SingleCycleCPU_source_env i s))

SingleCycleCPU_next i s =
  next_state_from_cert
    (eval_graph SingleCycleCPU_graph_cert
      (SingleCycleCPU_source_env i s))
```

This proves the generated fast word model agrees with the certificate
evaluator.

The generator must also emit:

- `source_env`, mapping input ports and flop outputs to certificate source IDs.
- `outputs_from_cert`, mapping evaluated output driver IDs to the output record.
- `next_state_from_cert`, mapping evaluated flop next-state driver IDs to the
  state record.

The bridge should be proved by generated field-level lemmas and record
extensionality.  It should not require hand-written per-internal-node proofs.

Implementation files:

- `pass/isabelle/pass_isabelle.cpp`
- generated `*_Lgraph.thy`
- generated `*_Lgraph_Cert.thy`

### Link 3: Certificate Well-Formedness Checker Soundness

The concrete certificate must be proved structurally valid:

```isabelle
graph_cert_wf <top>_graph_cert
```

This proof link is necessary because the evaluator-correctness theorem is only
meaningful for well-formed certificates.  If a certificate is malformed, bad
things can happen:

- a node depends on an ID that does not exist,
- two nodes have the same ID,
- a node appears before its dependencies,
- an operator has the wrong number of operands,
- a width is zero or inconsistent,
- an output points to a missing node,
- a flop next-state driver is missing or malformed.

The checker is the scalable way to prove those facts for huge generated graphs.
Its soundness theorem has the shape:

```isabelle
checker_accepts cert ==> graph_cert_wf cert
```

Without proving checker soundness, `checker_accepts cert = True` is just a
computation result with no logical meaning.  We would be trusting the checker
implementation instead of proving it.

Directly proving this by expanding `graph_cert_wf` over a DINO-sized graph is
too slow.  The first attempted method was a single executable boolean checker
with a generic soundness theorem:

```isabelle
graph_cert_wf_bool <top>_node_certs <top>_cert_sources = True

graph_cert_wf_bool cs sources
  ==> graph_cert_wf
        \<lparr>topo = map nid cs,
         sources = sources,
         nodes = nodes_of_list cs\<rparr>
```

That one-shot `by eval` approach is not DINO-scale: even after splitting the
certificate into chunks, evaluating a large list of generated `node_cert`
records can consume hundreds of GB.  The active strategy is therefore
`cert_wf:chunked_specialized`: each chunk is classified by the generator and
proved through the cheapest available proof path.  Constant-only chunks are
proved by reusable const-node lemmas and `simp`; unsupported mixed chunks are
reported or temporarily discharged by `sorry`, never silently sent through the
old global-list `by eval` path unless explicitly requested.

The checker must cover at least:

- distinct internal node IDs,
- distinct source IDs,
- no overlap between internal nodes and sources,
- every internal node has positive width,
- every dependency points to either a source or an internal node,
- every dependency appears before its consumer in the topological order,
- `nodes_of_list` maps each internal node ID to the corresponding certificate.

The older generated marker:

```isabelle
<top>_graph_cert_generator_checked = True
```

is only a temporary generator-side audit marker.  It is not a substitute for
`graph_cert_wf <top>_graph_cert` and should be replaced by the boolean-checker
proof path above.

### Resulting Per-Variant Translation Theorem

After the three links are available, each generated DINO variant should expose
a theorem of this shape:

```isabelle
<top>_step_refines_lgraph_certificate:
  <top>_step i s =
    (next_state_from_cert
       (eval_graph (topo <top>_graph_cert) <top>_graph_cert (source_env i s)),
     outputs_from_cert
       (eval_graph (topo <top>_graph_cert) <top>_graph_cert (source_env i s)))
```

Together with `eval_graph_correct` and `graph_cert_wf`, this is the formal
statement that the generated Isabelle step model faithfully represents the
emitted LGraph certificate.

## Certificate Evaluator Strategy

The scalable proof target is the graph certificate, not the pretty generated
let-chain.  `pass.isabelle` should emit a compact `graph_cert` containing node
ids, operation kinds, bit widths, dependencies, output mappings, and flop
next-state mappings.  Isabelle then interprets this data with one generic
evaluator:

```isabelle
<top>_comb_cert i s =
  outputs_from_env (eval_graph (topo <top>_cert) <top>_cert (source_env i s))

<top>_next_cert i s =
  flops_from_env (eval_graph (topo <top>_cert) <top>_cert (source_env i s))
```

The mathematical LGraph meaning is defined once by `denote_op` and
`denote_node`.  The evaluator uses the same primitive semantics through
`eval_op`, and the main proof is generic:

```isabelle
graph_cert_wf G ==>
topo_ok (deps_of G) (topo G) ==>
env_correct_on (set (topo G))
  (eval_graph (topo G) G source_env)
  (denote_node G source_env)
```

This replaces thousands of generated `local_correct_n*` lemmas with:

- one semantic definition for each supported LGraph op,
- one local correctness proof by cases over `lgraph_op`,
- one topological-order induction over arbitrary graph certificates.

The existing fixed-width word definitions (`<top>_comb`, `<top>_next`) remain
useful for execution and debugging.  They are not the first verified target.
Later, we can optionally prove group-level bridge theorems such as:

```isabelle
<top>_comb i s = <top>_comb_cert i s
<top>_next i s = <top>_next_cert i s
```

Those bridge proofs should be per output/flop bundle, not per internal node.

## Two Translation-Proof Strategies

There are now two distinct proof strategies in the tree.  They are related, but
they should not be mixed accidentally.

### Strategy A: Generated Let-Chain Local-Correctness Proof

This is the original strategy behind
`Translation_Combinational.generated_comb_equals_lgraph_denotation`.

Shape:

```isabelle
assumes "topo_ok dep_fun node_order"
assumes "distinct node_order"
assumes local_correct:
  "\<And>prefix rho n.
     n \<in> set node_order \<Longrightarrow>
     set (dep_fun n) \<subseteq> set prefix \<Longrightarrow>
     env_correct_on (set (dep_fun n)) rho denote \<Longrightarrow>
     eval rho n = denote n"
shows
  "env_correct_on (set node_order)
     (eval_nodes node_order eval source_env) denote"
```

Pros:

- Proves the generated fixed-width word let-chain directly.
- Gives a strong bridge from executable generated definitions to an independent
  denotation.
- Useful for final validation of `<top>_comb` and `<top>_next` against the
  certificate or another mathematical model.

Cons:

- Needs either thousands of generated `local_correct_n*` lemmas or a substantial
  generated proof script.
- Sensitive to Isabelle parser/name collisions if generated binders are named
  after record selectors such as `deps` or `topo`.
- Harder to scale on DINO-sized DAGs because proof terms mention the large
  generated fixed-width expressions.

Implementation files:

- Generic induction theorem:
  `translation-correctness/Translation_Combinational.thy`
- Fixed-width executable model emitter:
  `pass/isabelle/pass_isabelle.cpp`, especially `<top>_comb`, `<top>_next`,
  and `<top>_step` generation.
- Generated executable theories:
  `generated/dino_lgraph_isabelle/isabelle/*_Lgraph.thy`

Current status:

- The generic induction theorem exists.
- The generated cert theories no longer emit the old optional
  `*_comb_nodes_correct` theorem by default, because it is not required for the
  current certificate milestone and previously collided with record selectors.
- If this strategy is reintroduced, use explicit non-conflicting names such as
  `dep_fun`, `node_order`, `eval_fun`, and `denote_fun`.

### Strategy B: Graph Certificate Evaluator Proof

This is the current default strategy.

Shape:

```isabelle
definition <top>_graph_cert :: graph_cert where
  "<top>_graph_cert =
     \<lparr> topo = <top>_cert_topo,
       sources = <top>_cert_sources,
       nodes = <top>_cert_nodes \<rparr>"

theorem <top>_eval_graph_correct:
  "env_correct_on (set (topo <top>_graph_cert))
     (eval_graph (topo <top>_graph_cert) <top>_graph_cert source_env)
     (graph_denotation (topo <top>_graph_cert) <top>_graph_cert source_env)"
  by (rule eval_graph_correct_for_cert)
```

Pros:

- Emits graph structure as compact data instead of thousands of node-specific
  proof obligations.
- One evaluator and one theorem cover arbitrary DINO graph sizes and node
  orderings, as long as the certificate uses supported `lgraph_op` cases.
- Much easier to instantiate for SingleCycleCPU, PipelinedCPU, and
  PipelinedDualIssueCPU.
- Avoids proving every generated let binding immediately; the certificate is the
  proof-facing model.

Cons:

- The current theorem is a certificate-evaluator consistency theorem, not yet a
  full proof that the pretty fixed-width `<top>_comb` / `<top>_next` definitions
  equal the certificate model.
- Trust shifts to correct certificate emission from `pass.isabelle`, plus the
  correctness of the generic evaluator primitives.
- A later bridge theorem is still needed if downstream proofs want to cite the
  executable generated definitions rather than the certificate evaluator.

Implementation files:

- Certificate datatypes, primitive semantics, and evaluator:
  `translation-correctness/Translation_LGraph_Model.thy`
- Generic evaluator theorem:
  `translation-correctness/Translation_Certificate_Evaluator.thy`
- Whole-step composition skeleton:
  `translation-correctness/Translation_Step.thy`
- Full compact certificate emitter:
  `pass/isabelle/pass_isabelle.cpp`, especially `Cert_build`,
  `cert_node_expr`, `cert_const_node_expr`, and `emit_cert_theory`
- Generated certificate theories:
  `generated/dino_lgraph_isabelle/isabelle/*_Lgraph_Cert.thy`

Current status:

- `pass.isabelle` emits chunked `node_cert list` definitions, source-node lists,
  `nodes_of_list`, concrete `<top>_graph_cert`, and per-design
  `<top>_eval_graph_correct` theorem statements.
- Constants reused at different consumer widths are represented by synthetic
  certificate node ids, so a LiveHD zero-width/untyped constant can be
  width-specialized without changing the source LGraph node.
- LiveHD `Lconst(-1)` is emitted as `Op_Const (- 1)` so Isabelle parses it as a
  negative integer argument to `Op_Const`, not as subtraction from the
  constructor.

Recommended workflow:

1. Use Strategy B for the first verified DINO/Sail refinement path.
2. Prove DINO architectural contracts against the certificate evaluator model.
3. Add Strategy A bridge theorems only where executable/debug definitions must
   be connected to the certificate model.

## Corner Cases Required for Denotation Coverage

The LGraph denotation is only trustworthy if the primitive semantics covers the
hardware corner cases that LiveHD/Yosys can emit.  The required coverage is:

- Widths: reject or normalize zero-width nodes before certificate generation;
  truncate every result modulo `2^width`; support constants wider than 64 bits.
- Boolean convention: keep comparisons and predicates as `1`-bit values at the
  certificate boundary; convert to bool only inside primitive semantics.
- Arithmetic: modular add/sub/mul, unsigned divide-by-zero policy, and an
  explicit decision for signed division if LiveHD exposes it.
- Comparisons: compare at operand width, not at the `1`-bit output width; keep
  signed and unsigned comparisons distinct once signedness metadata is present.
- Shifts: preserve logical shift semantics, arithmetic right shift semantics,
  and the current cprop-degenerated SRA cases.
- Masks: specify get-mask packing order, non-contiguous masks, masks wider than
  the data source, and out-of-range set-mask behavior.
- Flops: reset priority, enable behavior, old-state RHS evaluation, and
  simultaneous update of all flops.
- Sources and sinks: primary inputs, constants, flop outputs, output ports, and
  per-flop next-state drivers must all be represented in the certificate.

## Component 1: Combinational DAG Correctness

Goal:

```isabelle
theorem generated_comb_equals_lgraph_denotation:
  "<top>_comb i s = lgraph_comb_outputs G i s"
```

Proof idea:

- Define a mathematical denotation for every LGraph combinational node.
- Use the topological order emitted by `reachable_topo_order` in
  `pass/isabelle/pass_isabelle.cpp`.
- Prove by induction over the generated let-binding order that every binding
  `n_k` equals the denotation of the corresponding LGraph node.
- Discharge each node case using primitive operator lemmas from
  `formal/semantic_primitives/SemanticPrimitives.thy`.

Key invariant:

```isabelle
deps_before topo ==>
  forall n in set topo. generated_binding n = denote_lgraph_node G i s n
```

## Component 2: Flop and Next-State Correctness

Goal:

```isabelle
theorem generated_next_equals_lgraph_flop_step:
  "<top>_next i s = lgraph_flop_step G i s"
```

Proof idea:

- Define the mathematical LGraph flop transition:

```isabelle
lgraph_flop_next G f i s =
  (if reset_active G f i s then reset_value G f
   else if enable_active G f i s then denote_lgraph_node G i s (din_node G f)
   else read_flop s f)
```

- Prove one generated-field lemma per flop:

```isabelle
st_field (<top>_next i s) = lgraph_flop_next G f i s
```

- Combine the field lemmas with record extensionality.

Important semantic condition:

All flop RHS expressions must be evaluated from the old state `s`, before any
record field is updated.  This matches synchronous RTL clock-edge semantics.

## Component 3: Whole-Step Translation Correctness

Goal:

```isabelle
theorem generated_step_equals_lgraph_step:
  "<top>_step i s = lgraph_step G i s"
```

where:

```isabelle
lgraph_step G i s =
  (lgraph_flop_step G i s, lgraph_comb_outputs G i s)
```

Proof idea:

- Reuse `generated_comb_equals_lgraph_denotation`.
- Reuse `generated_next_equals_lgraph_flop_step`.
- Unfold the generated step definition:

```isabelle
<top>_step i s = (<top>_next i s, <top>_comb i s)
```

This theorem is the one later DINO/Sail refinement proofs should cite.

## Current Inputs

- Generator: `pass/isabelle/pass_isabelle.cpp`
- Generated DINO theories: `generated/dino_lgraph_isabelle/isabelle/`
- Primitive helper definitions: `formal/semantic_primitives/SemanticPrimitives.thy`
- Primitive helper tests: `formal/semantic_primitives/SemanticPrimitives_Test.thy`
- Semantic microtests: `semantic_tests/`
- Current ADD refinement bridge:
  `formal/DINO_Lgraph_Bridge/DINO_Lgraph_SingleCycle_ADD_Refinement.thy`

## Isabelle Build Environment Rule

Do not run DINO/LGraph Isabelle heap builds with the default sandboxed process
environment.  In this workspace, Poly/ML heap/database writes repeatedly fail
inside the normal sandbox with:

```text
I/O error: Operation not permitted
```

This is a sandbox/filesystem-permission failure, not an Isabelle proof failure.
The build usually fails before reaching the generated certificate theories, for
example while writing the `LGraph-Translation-Correctness` heap.

Use a project-local isolated Isabelle home and project-local temporary
directory, and run the Isabelle build outside the sandbox when heap writes are
required:

```bash
cd /mada/users/czeng14/projects/livehd-proof

mkdir -p generated/dino_lgraph_isabelle/runtime_tmp

HOME=/mada/users/czeng14/projects/livehd-proof/generated/dino_lgraph_isabelle/isabelle_home_tiered_64 \
TMPDIR=/mada/users/czeng14/projects/livehd-proof/generated/dino_lgraph_isabelle/runtime_tmp \
TMP=/mada/users/czeng14/projects/livehd-proof/generated/dino_lgraph_isabelle/runtime_tmp \
TEMP=/mada/users/czeng14/projects/livehd-proof/generated/dino_lgraph_isabelle/runtime_tmp \
/mada/users/czeng14/.local/Isabelle2025-2/bin/isabelle build \
  -v \
  -o ML_system_64=true \
  -o document=false \
  -o browser_info=false \
  -o parallel_proofs=0 \
  -o threads=1 \
  -j1 \
  -d /mada/users/czeng14/projects/livehd-proof/formal/semantic_primitives \
  -d /mada/users/czeng14/projects/livehd-proof/translation-correctness \
  -d /mada/users/czeng14/projects/livehd-proof/generated/dino_lgraph_isabelle/isabelle \
  DINO-Lgraph-SingleCycle-Cert
```

This also satisfies the project rule to avoid creating temporary directories
under `/tmp`.  Use `generated/dino_lgraph_isabelle/runtime_tmp` or another
clearly named project-local runtime directory instead.

## Current Certificate Artifacts

The current Isabelle session in this directory already contains:

- `Translation_LGraph_Model.thy`: graph certificates, width-carrying bitvector
  values, primitive denotation, node evaluation, and local evaluator/denotation
  alignment.
- `Translation_Combinational.thy`: generic topological-order induction.
- `Translation_Flops.thy`: generic flop next-state proof skeleton.
- `Translation_Step.thy`: whole-step composition theorem.
- `Translation_Certificate_Evaluator.thy`: generic graph-certificate evaluator
  theorem used by generated DINO cert theories.
- Generated DINO cert theories under
  `generated/dino_lgraph_isabelle/isabelle/*_Lgraph_Cert.thy`, now recording
  full chunked `node_cert` data and instantiating the generic evaluator theorem
  for each DINO design.

### Certificate Chunk File Map

The chunked certificate method is split across reusable Isabelle libraries,
the C++ generator, generated per-design theories, and build scripts/logs.

Reusable Isabelle definitions and lemmas:

- `translation-correctness/Translation_LGraph_Model.thy`
  defines `lgraph_op`, `node_cert`, `graph_cert`, `node_cert_chunk_wf_bool`,
  `const_node_cert_wf_bool`, `node_cert_deps`,
  `simple_node_cert_shape_wf_bool`,
  `const_node_cert_chunk_wf_bool_sound`, and
  `simple_node_cert_chunk_wf_bool_sound'`.
  This is the main proof library for the current certificate chunk method.
  The relevant proof obligations are:
  `const_node_cert_wf_boolD`,
  `const_node_cert_chunk_wf_bool_sound`,
  `simple_node_cert_wf_boolD`,
  `simple_node_cert_chunk_wf_bool_sound`,
  `simple_node_cert_shape_wf_boolD`, and
  `simple_node_cert_chunk_wf_bool_sound'`.
- `translation-correctness/Translation_Certificate_Evaluator.thy`
  proves the generic evaluator-correctness theorem used after certificate
  well-formedness is available.
- `translation-correctness/Translation_Op_Lemmas.thy`
  is the place for reusable operator-correctness and corner-case lemmas.
- `translation-correctness/ROOT`
  declares the `LGraph-Translation-Correctness` Isabelle session.

Generator implementation:

- `pass/isabelle/pass_isabelle.cpp`
  emits chunked certificate data, classifies chunks as `const-only`,
  `simple-mixed`, or unsupported, computes `needed_id_chunks`, and emits the
  generated chunk lemmas.
  The chunk proof emission is in the certificate-generation block that emits:
  `<top>_cert_chunk_i_ids`, `<top>_cert_chunk_i_const`,
  `<top>_cert_chunk_i_shape`, `<top>_cert_chunk_i_deps`,
  `<top>_cert_chunk_i_deps_subset`, and `<top>_cert_chunk_i_ok`.
- `pass/isabelle/pass_isabelle.hpp`
  declares `CertWFMode`, `CertWFFallback`, `cert_chunk_size`, and
  `cert_chunk_limit`.

Generated proof structure:

- `<top>_node_certs_i`
  is the chunk-local list of `node_cert` records.
- `<top>_node_certs_ids_i`
  is definitionally derived as `map nid <top>_node_certs_i`; this avoids the
  old expensive proof `map nid chunk = ids` by `eval`.
- `<top>_cert_chunk_i_ids`
  proves the derived ID-list equality by `simp`.
- `<top>_cert_chunk_i_ids_distinct`
  proves chunk-local node-ID uniqueness.
- `<top>_cert_chunk_i_ids_subset`
  proves this chunk's node IDs are included in the global certificate ID list.
- `<top>_cert_chunk_i_ids_disjoint`
  proves this chunk's node IDs are disjoint from source IDs.
- `<top>_cert_chunk_i_const`
  is emitted only for constant-only chunks and discharges the local
  constant-node shape predicate.
- `<top>_cert_chunk_i_shape`
  is emitted for simple mixed chunks and discharges the local simple-op shape
  predicate.
- `<top>_cert_chunk_i_deps`
  proves the actual dependencies of the chunk are contained in the generated
  concrete dependency list for that chunk.
- `<top>_cert_chunk_i_deps_subset`
  proves those concrete dependencies are globally valid using only the needed
  previous ID chunks and the source list.
- `<top>_cert_chunk_i_ok`
  is the final per-chunk theorem:
  `node_cert_chunk_wf_bool <top>_cert_all_ids <top>_cert_sources <top>_node_certs_i`.
  Constant chunks use `const_node_cert_chunk_wf_bool_sound`; simple mixed
  chunks use `simple_node_cert_chunk_wf_bool_sound'`.
- `<top>_cert_chunk_checks`
  collects all generated `<top>_cert_chunk_i_ok` lemmas for the final generated
  certificate well-formedness theorem.

Generated per-design certificate theories:

- `generated/dino_lgraph_isabelle/isabelle/SingleCycleCPU_Lgraph_Cert.thy`
  contains `SingleCycleCPU_node_certs_i`, `SingleCycleCPU_node_certs_ids_i`,
  `SingleCycleCPU_cert_chunk_i_deps`, and the generated chunk lemmas.
- `generated/dino_lgraph_isabelle/isabelle/PipelinedCPU_Lgraph_Cert.thy`
  has the corresponding Pipelined CPU certificate chunks.
- `generated/dino_lgraph_isabelle/isabelle/PipelinedDualIssueCPU_Lgraph_Cert.thy`
  has the corresponding DualIssue CPU certificate chunks.

Generated executable model theories:

- `generated/dino_lgraph_isabelle/isabelle/SingleCycleCPU_Lgraph.thy`
- `generated/dino_lgraph_isabelle/isabelle/PipelinedCPU_Lgraph.thy`
- `generated/dino_lgraph_isabelle/isabelle/PipelinedDualIssueCPU_Lgraph.thy`

Generated Isabelle sessions:

- `generated/dino_lgraph_isabelle/isabelle/sessions/singlecycle_cert/ROOT`
- `generated/dino_lgraph_isabelle/isabelle/sessions/pipelined_cert/ROOT`
- `generated/dino_lgraph_isabelle/isabelle/sessions/dualissue_cert/ROOT`

Run scripts and diagnostics:

- `scripts/run_dino_lgraph_isabelle.sh`
  drives LiveHD/Yosys, `pass.isastage`, `pass.isabelle`, and optional Isabelle
  builds.  It accepts `CERT_WF_MODE`, `CERT_WF_FALLBACK`, `CERT_CHUNK_SIZE`,
  and `CERT_CHUNK_LIMIT`.
- `generated/dino_lgraph_isabelle/logs/SingleCycleCPU.log`
- `generated/dino_lgraph_isabelle/logs/PipelinedCPU.log`
- `generated/dino_lgraph_isabelle/logs/PipelinedDualIssueCPU.log`
  record per-design chunk summaries and unsupported chunk diagnostics.
- `generated/dino_lgraph_isabelle/logs/helper_gate.log`
  records the common Isabelle helper/session build when the generation script
  runs with Isabelle enabled.
- `generated/dino_lgraph_isabelle/runtime_tmp/`
  is the project-local temporary directory for Isabelle/PolyML runs.
- `generated/dino_lgraph_isabelle/isabelle_home_tiered_64/`
  is the project-local isolated Isabelle home used to avoid sandbox heap-write
  failures.

Next implementation work is to strengthen `graph_denotation` into an
independent recursive mathematical denotation and then add selective
`<top>_comb_fast_refines_cert` / `<top>_next_fast_refines_cert` bridge theorems
where downstream proof automation needs the fixed-width executable definitions.

### Reuse Plan For Later Translations

The translation proof should become cheap for later retimed, repipelined, or
otherwise modified DINO variants by separating reusable proof infrastructure
from generated per-design facts.

Reusable artifacts proved once:

- `eval_graph_correct`: the certificate evaluator agrees with the mathematical
  graph denotation.
- Operator-correctness lemmas for constants, arithmetic, shifts, comparisons,
  muxes, sign extension, `Get_mask`, and `Set_mask`.
- Sequential/flop lemmas for reset priority, enable behavior, old-state RHS
  evaluation, and simultaneous update.
- Chunk-checker soundness lemmas for constant chunks, simple mixed chunks, and
  later indexed/dense chunks.
- Chunk-composition theorem: per-chunk certificate facts imply full
  `graph_cert_wf`.
- Bridge proof schemas for generated output records and generated next-state
  records.
- CPU-level refinement framework: stuttering refinement, commit-trace
  compression, deterministic ISA reasoning, and common instruction-contract
  lemmas.

Generated per-design artifacts:

- `<top>_graph_cert`
- `<top>_source_env`
- `<top>_outputs_from_cert`
- `<top>_next_state_from_cert`
- `<top>_node_certs_i` and `<top>_node_certs_ids_i`
- `<top>_cert_chunk_i_deps`
- `<top>_cert_chunk_i_ok`
- `<top>_cert_chunk_checks`
- `<top>_graph_cert_wf`, once all chunks are emitted and composed
- `<top>_comb_refines_cert`
- `<top>_next_refines_cert`
- `<top>_step_refines_lgraph_certificate`

The intended trust chain is:

```text
generated fast Isabelle model
= evaluated certificate
= mathematical LGraph semantics
```

The first equality is proved by the generated bridge theorems
`<top>_comb_refines_cert` and `<top>_next_refines_cert`.  The second equality is
proved once by the generic evaluator-correctness theorem, assuming
`graph_cert_wf <top>_graph_cert`.

Speedup priorities:

- Use dense topological IDs instead of sparse LiveHD IDs.  Dependency validity
  should become `Node j` is valid at node `k` iff `j < k`, and `Source s` is
  valid iff `s < num_sources`.
- Replace global uniqueness proofs such as `distinct (concat id_chunks)` with
  chunked uniqueness or dense-ID construction.
- Avoid `by eval` over whole generated graph data.  Use `simp`, specialized
  const/simple chunk lemmas, concrete dependency-list subset facts, and later
  indexed/dense checkers.
- Prove `<top>_comb_refines_cert` by output groups, not by one whole-design
  proof.
- Prove `<top>_next_refines_cert` by flop or pipeline-register groups, then
  assemble by record equality.
- Cache parent Isabelle sessions such as `DINO_Semantic_Primitives`,
  `LGraph-Translation-Correctness`, generated model sessions, and the Sail
  RV64IM heap.

With this split, a new DINO variant should require almost no manual translation
proof work after generation.  The remaining human proof work is architectural:
single-cycle one-step refinement, pipeline stuttering refinement, retimed
pipeline commit-trace equivalence, or dual/OOO zero-one-many commit refinement.

## Current Progress: Chunked Certificate Proofs

The main proof-scaling work so far has focused on replacing DINO-sized
certificate checks of the form:

```isabelle
lemma <top>_cert_chunk_i_ok:
  "node_cert_chunk_wf_bool <top>_cert_all_ids <top>_cert_sources <top>_node_certs_i"
  by eval
```

This shape is too expensive because it combines:

- large generated `node_cert` record lists,
- global `all_ids = concat id_chunks`,
- list/set membership over thousands of sparse LiveHD IDs,
- code evaluation of record projections and predicates,
- and, in earlier versions, a separate `map nid chunk = ids` proof by `eval`.

### Observed Bottlenecks

The first `cert_wf:chunked` implementation used 500-node chunks.  For
`SingleCycleCPU`, the first generated proof was only:

```isabelle
lemma SingleCycleCPU_cert_chunk_0_ids:
  "map nid SingleCycleCPU_node_certs_0 = SingleCycleCPU_node_certs_ids_0"
  by eval
```

That proof alone ran for hours and grew to hundreds of GB of Poly/ML RSS.  It
was not checking real well-formedness yet; it was just projecting IDs from 500
generated records.  This established that the proof shape, not merely the chunk
size, was the problem.

After changing ID chunks to be definitionally derived from node chunks:

```isabelle
definition <top>_node_certs_ids_i where
  "<top>_node_certs_ids_i = map nid <top>_node_certs_i"
```

the ID projection proof became:

```isabelle
by (simp add: <top>_node_certs_ids_i_def)
```

This removed the first bottleneck.

The next bottleneck was the real chunk checker:

```isabelle
node_cert_chunk_wf_bool all_ids sources chunk_i
by eval
```

Even with `cert_chunk_size = 25`, the first 25-node constant chunk ran for more
than ten minutes.  This showed that the generic checker still performs too much
code evaluation over generated records and global lists.

### Implemented Fixes

The current generator supports:

```text
cert_wf:skip
cert_wf:eval
cert_wf:sorry
cert_wf:chunked
cert_chunk_size:<n>       default 25
cert_chunk_limit:<n>      emit only first n chunks for proof-shape testing
cert_wf_fallback:fail     default, stop on unsupported mixed chunks
cert_wf_fallback:sorry    benchmark with unsupported mixed chunks admitted
cert_wf_fallback:eval     explicitly allow old fallback eval
```

The script `scripts/run_dino_lgraph_isabelle.sh` exposes these as:

```bash
CERT_WF_MODE=chunked
CERT_WF_FALLBACK=fail
CERT_CHUNK_SIZE=25
CERT_CHUNK_LIMIT=35
```

The generator also emits per-chunk summaries to the design logs, for example:

```text
pass.isabelle cert chunks for SingleCycleCPU:
  total_nodes=5383 chunk_size=25 emitted_chunks=50 full_chunks=216
  chunk 0:  nodes=25 class=const-only ops={Op_Const=25}
  ...
  chunk 35: nodes=25 class=simple-mixed
    ops={Op_And=2, Op_Const=9, Op_EQ=2, Op_GetMask=7,
         Op_MuxBool=1, Op_SHL=1, Op_SRA=1, Op_Sext=2}
```

Constant-only chunks are now proved without `eval`.  The reusable library
contains:

```isabelle
definition const_node_cert_wf_bool :: "node_cert => bool"

lemma const_node_cert_wf_boolD:
  assumes "const_node_cert_wf_bool c"
  shows "width c > 0 \<and> deps c = []"

lemma const_node_cert_chunk_wf_bool_sound:
  assumes "list_all const_node_cert_wf_bool cs"
  assumes "distinct (map nid cs)"
  assumes "set (map nid cs) \<subseteq> set all_ids"
  assumes "set (map nid cs) \<inter> set srcs = {}"
  shows "node_cert_chunk_wf_bool all_ids srcs cs"
```

For each constant-only chunk, the generator emits:

```isabelle
lemma <top>_cert_chunk_i_const:
  "list_all const_node_cert_wf_bool <top>_node_certs_i"
  by (simp add: <top>_node_certs_i_def const_node_cert_wf_bool_def)

lemma <top>_cert_chunk_i_ids_distinct:
  "distinct (map nid <top>_node_certs_i)"
  by (simp add: <top>_node_certs_i_def)

lemma <top>_cert_chunk_i_ids_subset:
  "set (map nid <top>_node_certs_i) \<subseteq> set <top>_cert_all_ids"
  by (auto simp add:
      <top>_cert_all_ids_def
      <top>_cert_id_chunks_def
      <top>_node_certs_ids_i_def)

lemma <top>_cert_chunk_i_ids_disjoint:
  "set (map nid <top>_node_certs_i) \<inter> set <top>_cert_sources = {}"
  by (simp add: <top>_node_certs_i_def <top>_cert_sources_def)

lemma <top>_cert_chunk_i_ok:
  "node_cert_chunk_wf_bool <top>_cert_all_ids <top>_cert_sources <top>_node_certs_i"
  using ...
  by (rule const_node_cert_chunk_wf_bool_sound)
```

Simple/mixed chunks are now also proved without `eval`.  The reusable library
contains a shape-only checker plus a concrete dependency-list soundness theorem:

```isabelle
definition node_cert_deps :: "node_cert list => nat list"

definition simple_node_cert_shape_wf_bool :: "node_cert => bool"

lemma simple_node_cert_chunk_wf_bool_sound':
  assumes "list_all simple_node_cert_shape_wf_bool cs"
  assumes "set (node_cert_deps cs) \<subseteq> set all_ids \<union> set srcs"
  assumes "distinct (map nid cs)"
  assumes "set (map nid cs) \<subseteq> set all_ids"
  assumes "set (map nid cs) \<inter> set srcs = {}"
  shows "node_cert_chunk_wf_bool all_ids srcs cs"
```

The key change was removing the quantified generated proof:

```isabelle
valid_ref x ==> x \<in> set all_ids \<or> x \<in> set sources
```

That form was logically sound but slow because Isabelle had to reason about an
arbitrary `x`, generated disjunctions, sparse IDs, and global membership facts.
The current generator emits a concrete dependency-list proof instead:

```isabelle
definition <top>_cert_chunk_i_deps :: "nat list" where
  "<top>_cert_chunk_i_deps = [d0, d1, d2, ...]"

lemma <top>_cert_chunk_i_deps:
  "set (node_cert_deps <top>_node_certs_i)
     \<subseteq> set <top>_cert_chunk_i_deps"
  by (simp add:
      <top>_node_certs_i_def
      <top>_cert_chunk_i_deps_def
      node_cert_deps_def)

lemma <top>_cert_chunk_i_deps_subset:
  "set (node_cert_deps <top>_node_certs_i)
     \<subseteq> set <top>_cert_all_ids \<union> set <top>_cert_sources"
  using <top>_cert_chunk_i_deps
        <top>_cert_chunk_j_ids_subset ...
  by (auto simp add:
      <top>_cert_chunk_i_deps_def
      <top>_cert_sources_def
      <only-needed-id-chunk-defs>)
```

The generator computes `needed_id_chunks` from the concrete dependencies and
only unfolds ID chunks that contain dependencies actually used by the chunk.
This is a proof-size optimization, not a duplicate-ID checker; uniqueness is
still handled by separate `ids_distinct` obligations.

The simple shape checker currently accepts these cheap local families:

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

This checker only proves structural local well-formedness: positive widths,
arity/result-width constraints, and dependency validity via the concrete
dependency subset.  It is not the final operator-semantic proof; operator
corner cases still belong in the generic op-lemma and regression-test layer.

### Measured Results

The specialized constant-chunk path was validated on `SingleCycleCPU`.

First one-chunk test:

- `CERT_CHUNK_SIZE=25`
- `CERT_CHUNK_LIMIT=1`
- cert theory completed in about `1.8s`
- full build command took longer only when parent/model sessions were rebuilt

Const-prefix test:

- `CERT_CHUNK_SIZE=25`
- `CERT_CHUNK_LIMIT=35`
- chunks `0..34` are all constant-only
- `DINO-Lgraph-SingleCycle-Cert` completed in about `25s`
- peak RSS was about `1.9 GB`

This established the first proof-scaling baseline: const-only chunks are no
longer a problem.  The first mixed `SingleCycleCPU` chunk is chunk `35`, with:

```text
Op_And=2, Op_Const=9, Op_EQ=2, Op_GetMask=7,
Op_MuxBool=1, Op_SHL=1, Op_SRA=1, Op_Sext=2
```

Simple-prefix test:

- `CERT_CHUNK_SIZE=25`
- `CERT_CHUNK_LIMIT=50`
- chunks `0..34` are const-only
- chunks `35..49` are simple-mixed
- `DINO-Lgraph-SingleCycle-Cert` completed successfully
- cert theory time was about `1m32s`
- full rebuild time after library/model changes was about `6m15s`
- peak RSS was about `9 GB`

This confirms the current proof shape avoids the previous monolithic/`eval`
failure mode for the first 50 `SingleCycleCPU` certificate chunks.  It does not
yet prove the full 216 chunks of `SingleCycleCPU`, nor the full Pipelined or
DualIssue certificate sessions.

Larger simple-prefix test:

- `CERT_CHUNK_SIZE=25`
- `CERT_CHUNK_LIMIT=100`
- generation passed for all three designs with `CERT_WF_FALLBACK=fail`
- `DINO-Lgraph-SingleCycle-Cert` completed successfully
- cert theory time was about `6m21s`
- full rebuild time after library/model changes was about `11m40s`
- peak RSS was about `9.7 GB`

This run added local support for `Op_Sum` and `Op_MuxN`.  It also showed the
next practical bottleneck: dependency-heavy `deps_subset` lemmas such as
`SingleCycleCPU_cert_chunk_70_deps_subset` and
`SingleCycleCPU_cert_chunk_81_deps_subset`.  These chunks still prove, but they
may run for tens of seconds because they unfold many needed ID chunks.  The
global `distinct <top>_cert_all_ids` proof was not the first limit-100
bottleneck.

### Current Chosen Proof Path

The current path is:

1. Keep the graph certificate evaluator as the official semantic model.
2. Keep the fixed-width word model as the ergonomic/debug model used by DINO
   refinement proofs.
3. Prove `evaluated certificate = mathematical certificate semantics`
   generically.
4. Prove `generated fast word model = evaluated certificate` per design later,
   by field/output/flop bundle, not by per-node local lemmas.
5. Prove `graph_cert_wf` by chunked specialized proofs:
   - const-only chunks by `simp` and const-node soundness lemmas,
   - simple/mixed chunks by shape lemmas plus concrete dependency-list subset
     lemmas,
   - no accidental global-list `by eval` fallback.

The key engineering rule is:

```text
Do not run by eval on:
  node_cert_chunk_wf_bool <top>_cert_all_ids <top>_cert_sources <top>_node_certs_i
unless it is explicitly requested for debugging.
```

### Next Proof-Scaling Work

Recommended next steps:

1. Increase `CERT_CHUNK_LIMIT` beyond `100` and find the next unsupported or
   slow chunk shape.
2. Add more simple local op families only when their certificate check is cheap
   and local.
3. Optimize dependency-heavy `deps_subset` chunks by proving dependency
   membership against smaller chunk-local facts, or by moving to an indexed
   checker.
4. Replace remaining sparse global-list reasoning in mixed chunks with
   generated predicates:

```isabelle
<top>_is_node_id :: nat => bool
<top>_is_source  :: nat => bool
<top>_valid_ref n = (<top>_is_node_id n \<or> <top>_is_source n)
```

5. Add an indexed local checker:

```isabelle
node_cert_chunk_wf_indexed_bool
  <top>_is_node_id
  <top>_is_source
  <top>_node_certs_i
```

6. Prove a generic soundness theorem from indexed chunk checks to
   `node_cert_chunk_wf_bool`.
7. Eventually replace sparse LiveHD IDs inside the checker with dense
   topological indices:

```isabelle
datatype cert_ref = Source nat | Node nat
```

Dense checking is the long-term scalable solution because dependency validity
becomes `Source i < num_sources` or `Node j < current_index`, with no global
ID list, no `set all_ids`, no sparse lookup, and no global `distinct` proof by
evaluation.

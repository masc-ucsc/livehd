# pass.lean

`pass.lean` is the Lean target for the LiveHD graph-to-theorem-prover flow.  It
is intentionally modeled after `pass.isabelle`, but it is a separate pass and a
separate proof stack.

It has two output modes, selected with `--set formal.lean.mode=<mode>`:

- `legacy` (default): emits a per-design Lean fast model, its graph certificate
  and, optionally, the per-design fast-view bridge proofs.  Unless stated
  otherwise, the rest of this README describes this mode.
- `verified_compiler`: the Lean file holds only `<Top>_designCert` (a
  `<Top>.dcert` copy of the same certificate is written beside it); the model
  comes from the proved compiler `Compiler.compileDesign` instead of from this
  pass, so there are no `<Top>_comb`/`_next`/`_step` definitions and no
  per-node proof scripts.

Both modes run the same owned scan and certificate pipeline (see
[Code layout](#code-layout)).

In macos, you may need to install Lean 4:
```
brew install elan
```

## Code layout

```text
Pass_lean / LeanOptions                                 pass_lean.cpp, lean_options.cpp
  -> scan_design(Graph, ScanOptions) : DesignScan       design_scan.*, graph_access.*
  -> build_certificate(DesignScan, ..) : CertificateIR  certificate_ir.*, memory_lowering.*
  -> build_io_meta(DesignScan, CertificateIR)           design_io_meta.*
  -> verified_compiler: emit_design_cert, render_dcert  emit_design_cert.*, emit_dcert.*
     legacy:            emit_legacy_model               legacy_model.*, emit_legacy_*.*
```

- `graph_access.*` — graph access, width checks, and the memory policy parser.
- `design_scan.*` — owns port identities, decimal constants, flop controls,
  async-reset input provenance, memory policies and contents, clock connections,
  roots, and reachable topological nodes; no graph handle escapes it.
  Declaration order determines roots; sorted names determine runtime input and
  output ordinals.
- `certificate_ir.*` (with `certificate_builder.hpp`) — typed operations,
  sources, dependencies and state drivers; sparse ids stay available alongside
  validated dense slots.
- `memory_lowering.*` — per-read forwarding chains, byte-enable writes,
  immutable ROM images, and synchronous read registers.  It never reads a graph.
- `design_io_meta.*` — the `<Top>_io.json` sidecar (input/output ordinals,
  widths, clocks, and every flop's name in runtime order), built from the same
  scan and certificate as the Lean file.
- `lean_format.*` — shared literals, collections, lookup trees, operation
  spelling, and atomic file publication.
- `emit_design_cert.*` — `verified_compiler`: the `<Top>_designCert` literal.
  `emit_dcert.*` — the same certificate in the DCERT2 wire format read by the
  verified Lean codec `Compiler.CertIO`.
- `legacy_model.*` (`emit_legacy_model`) coordinates the legacy emitters:
  `emit_legacy_schema.*` (named ports/state, RTL mapping comments),
  `emit_legacy_fast_model.*` (fast model, `lower_fast_expr`),
  `emit_legacy_graph_cert.*` (sparse-id `GraphCert`, source environment,
  output/state projections), `emit_legacy_cert_wf.*` (optional chunked
  well-formedness proofs) and `emit_legacy_fast_bridge.*` (optional per-node
  bridge, step 5 below).

Per design the pass writes `<Top>_Lgraph.lean` and `<Top>_io.json`;
`verified_compiler` also writes `<Top>.dcert`.  The files are published as one
group: if emission fails, the previous set is kept.

## Lean Support Library and Emitted Model

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
- In legacy mode `pass.lean` emits:
  - concrete Lean input/output/state structures;
  - concrete `<Top>_comb`, `<Top>_next`, and `<Top>_step` definitions for the
    supported bit-vector and memory graph subset;
  - concrete `NodeCert` lists and `GraphCert` data for the same topo-ordered
    graph nodes;
  - `outputsFromCert`, `nextStateFromCert`, `<Top>_comb_cert`,
    `<Top>_next_cert`, and `<Top>_step_cert` definitions;
  - definitional certificate-model bridge theorems for the cert-based model;
  - `evalGraphCorrectForCert` instantiations over the emitted certificate.
- Certificate emission can be disabled with `--set formal.lean.emit_cert=false` for
  model-only scaling gates.
- Scalable certificate well-formedness checking (`--set formal.lean.cert_wf=chunked`)
  is emitted by `emit_legacy_cert_wf` against `LegacyCertWF.lean`: symbolic
  constant chunks, simple mixed shapes, concrete dependency subsets, and
  dense-slot uniqueness.

Verified:

- `add2` oracle (fast model is RTL-exact and matches the certificate evaluator):
  ```lean
  ∀ a b : BitVec 4, (add2_comb {a,b}).out_y = a + b
  ∀ a b : BitVec 4, (add2_comb {a,b}).out_y = (add2_comb_cert {a,b}).out_y
  ```
  both close `by decide`.
- `Get_mask(a, -1)` all-ones zext idiom: the mask is materialized at
  `max(src_w, out_w)` (fast model + certificate in lockstep), so it selects every
  source bit instead of only bit 0.

## Validation Pipeline (order matters)

Upstream LEC is the mandatory RTL-to-LGraph semantic gate: theorem-prover
generation consumes only graphs that have passed or been explicitly classified
by LEC.  The gate runs BEFORE `pass.lean`:

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

## Proof Strategy

The Lean proof path follows the same Strategy B used for Isabelle:

```text
generated fast Lean model
  = evaluated graph certificate
  = mathematical graph-certificate semantics
```

The second link is a reusable Lean theorem:

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

The emitted cert-based model has definitional bridge theorems to the evaluated
certificate.  What connects it to the fast model is the fast-view theorem,
emitted by step 5:

```lean
<Top>_comb i s = <Top>_comb_cert i s
<Top>_next i s = <Top>_next_cert i s
```

### Step 5 — fast-view bridge (emitted behind `emit_fast_bridge`)

This is the "first link" above (generated fast model = evaluated certificate),
the keystone tying the executable model to the graph semantics (and, via the LEC
gate, to the RTL).  It is **generated end-to-end** for the non-memory op set,
and for memories through the [memory certificate](#memory-certificate), gated by
`--set formal.lean.emit_fast_bridge=true` (default off, so ordinary output is
unchanged).  Architecture: a design-/operator-independent library plus a small
per-design instantiation the emitter prints.

- **Piece B — general theorem (proven)** — `GraphRefine.evalGraph_of_localAgree`
  (`.../Translation/GraphRefine.lean`): `evalGraph` computes the unique
  topological fixpoint of a dependency-ordered graph, so any environment `φ` that
  agrees with the source env on off-topo deps and satisfies the per-node
  recurrence `φ n = evalNode G φ n` equals `evalGraph` on every topo node.  Every
  design's step 5 reduces to this by instantiating `φ` with its encoded
  fast-model node values.  Mathlib-free; a decidable `DepOrderedB` discharges
  dependency ordering by `native_decide`.

- **Piece A — per-operator bridge library** —
  `.../Translation/OpBridge.lean`: relate the certificate evaluator `eval_op`
  (width-erased `BV`) to the native `BitVec` fast op under the encoding
  `bvenc x = mk_bv w (Int.ofNat x.toNat)`.  Covers, among others, GetMask,
  And/Or/Xor/Not, Ror, the compares (EQ/ULT/UGT/SLT), SHL/SRA, MuxBool/MuxN,
  Sext, and Sum (add and add/subtract) — binary bridges are width-polymorphic
  (`{wa wb w}` + `bv_zext`), and the genuinely n-ary operators (Or of arbitrary
  arity, MuxN, the And/Or/Xor/Sum folds) are handled by fold/`muxn_fast`/`orn_bv_bridge`
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
  An output or flop din driven *directly* by a constant/input/flop has an
  off-topo cert id (no `_fv<id>`); it is closed with `GraphRefine.evalGraph_not_mem`
  (off-topo ids read through to the source env).

- **Mathlib dependency** — the op-bridge proofs need Mathlib, so `formal/lean`
  requires `mathlib @ v4.31.0`; fetch oleans once with `lake exe cache get`.
  `OpBridge` is NOT imported by the package root (non-bridge output stays
  Mathlib-free); the emitter adds the import only to bridge-enabled files.
  Each bridge lemma carries an `example` of its intended use in `OpBridge.lean`
  itself, so `lake build LeanSemanticPrimitives.Translation.OpBridge` checks the
  library and its usage together and the check cannot drift from the lemma.

**DINO coverage:** the emitter generates a **sorry-free** bridge for all three
DINO variants, and **all three are proven end-to-end**.

| design | nodes | wall | peak RSS | result |
|---|---|---|---|---|
| `SingleCycleCPU` | 4,772 | 23.2 min | 13.3 GB | **proven** — `exit 0`, 0 errors, 0 sorries |
| `PipelinedCPU` | 5,061 | 27.7 min | 14.9 GB | **proven** — `exit 0`, 0 errors, 0 sorries |
| `PipelinedDualIssueCPU` | 10,740 | 57.4 min | 29.6 GB | **proven** — `exit 0`, 0 errors, 0 sorries |

For the proven designs `_comb`, `_next` and `_step` are all shown equal to the
certificate model.  Scope of the claim: these prove *fast model ⇔ certificate*
for the RTL in `generated/rtl_lgraph_equiv_latest/lec/refs/<Top>_ref.sv`, not for
later revisions of the DINO sources (for the pipelined variants, those refs may
predate the register-file bypass fix in the pipelined emit objects);
regenerate the refs to make the claim about newer RTL.

### Scaling design of the fast-view bridge

The emitter uses two representations that keep the generated proof linear in
the node count; the monolithic alternatives are O(N²) and do not finish
(measured on DINO, 4772 nodes):

| what | bad form | cost | good form | cost |
|---|---|---|---|---|
| `φ` / `sourceEnv` / `graphCert.nodes` | flat `if n = k` chain, `List.find?` (also: a balanced `if`-*tree*, which is no better — reducing it beta-substitutes the key across the whole N-node term) | O(N²), 47 h / 94 GB unfinished | `BT` **data** + recursive `BT.find`, values as input-independent closures → O(log N) per lookup | O(N) |
| `<Top>_bridge_rec` combiner | `simp only [...] at hn` + `rcases hn with h │ h │ …` (N-way) + N bullets — an N-deep `Or.casesOn` whose motive carries the rest, plus N goal substitutions | > 3 h 56 m unfinished | term-mode right fold over `List.forall_mem_cons` | ~127 s |

With both in place the per-node recurrence theorems dominate the run (≈280 ms
per node on DINO).

#### GetMask all-ones fast path

A GetMask whose mask is the constant `-1` (the all-ones zext idiom) and no
wider than its output is closed by the width-independent
`OpBridge.getmask_bridge_allones_ofNat` instead of the generic `getmask_bridge'`,
whose `by decide` side condition grows with the mask width.  Notes:
- The emitter writes `first | <all-ones> | getmask_bridge'`, so a node the fast
  path does not fit falls back to the generic lemma.
- Because that dispatch leaves the losing branch unelaborated, bridge output also
  sets `linter.unreachableTactic`/`linter.unusedTactic` false (otherwise
  ~2 warnings per GetMask node bury real diagnostics).  `op_census.py`
  therefore reports **`emitted on fast path : N / M all-ones nodes`** so the
  emitter's choice stays visible; to confirm Lean *takes* the branch, re-run with
  `linter.unreachableTactic` on and check the generic branch reports unreachable.
- Both GetMask lemmas need `(mask_indices M).length ≤ out_width`, which for an
  all-ones mask *is* the mask width.  A Get_mask that **truncates** (source wider
  than output) therefore gets a mask wider than its output, making that side
  condition **false**, and the node cannot close by either lemma.  `op_census.py`
  reports this as a hard FAIL.  DINO and `cva6_tlb_gate` contain no such node.

### Static gates before a long run

These take seconds; run them on the generated `<Top>_Lgraph.lean` before any long
typecheck:

- `pass/lean/scripts/op_census.py <file>` — PASS iff every `(op, arity)` in the
  design is handled by the step-5 dispatch (the emitter refuses an unsupported
  shape atomically).  It also reports node widths and GetMask all-ones counts,
  and FAILs on a truncating all-ones GetMask (above).
- `pass/lean/scripts/const_parity.py <file>` — PASS iff, for every node, each
  constant source it depends on appears in the node's fast-model body with the
  certificate leaf's exact spelling.  A value-equal but differently spelled
  constant is an unprovable per-node goal for ops where the constant survives
  into the result (compares inside `decide`, MuxN branches, mask `&&&`).
- `sorries = 0`, and all three `_refines_fast` theorems present.

Node counts move with `cprop`, so take them from `op_census.py` on a fresh
emission rather than from older artifacts.

For a new design, use a verification ladder: the 1-flop sequential design →
**a module with an output driven directly by a constant** → the target module.

### Running and diagnosing long checks

- **Only a full-file `exit 0` counts.**  A sorry-free file can still contain
  failing declarations, and a slow check may never reach them.  A truncated
  probe is not a clean bill of health: state what it excludes.  Failures cluster
  in the tail (`bridge_src`, `_refines_fast`), which is also the first thing lost
  when a run is killed — to test it cheaply, stub the combiner with `sorry`.
- **Reading the log.**  Errors are reported **incrementally**, so
  `grep 'error:' <log>` at any time.  But the log is **not** a progress signal:
  stdio block-buffers at 4096 bytes when stdout is a file (use `stdbuf -oL`), and
  Lean emits messages in **file order**, so one slow early declaration withholds
  all later output.
- **One slow declaration.**  Lean parallelizes *theorem bodies*, so a file drains
  to one core as stragglers finish — a single oversized declaration pins one
  thread and dominates wall time.  Per-thread CPU in `/proc/<pid>/task/*/stat`
  tells "one stuck declaration" (one busy thread, CPU ≈ wall) from "starved by
  load" (a starved job accumulates *less* CPU than wall).  To localize it, stub
  **one declaration** with `sorry` and re-time; that is cheaper than bisecting
  the file into timed cumulative slices.  It does not prove the file (the stubbed
  obligation is assumed), so it is a *localizer*, never a result.
- **Resources.**  Bound threads and priority, e.g.
  `LEAN_NUM_THREADS=8 taskset -c 0-7 nice -n 19 ionice -c 3 lake env lean <file>`;
  for long runs use a detached `systemd-run --user -p CPUQuota=800%` service (a
  `--scope` dies with the launcher).  Do not run several ~10 GB Lean instances
  concurrently: they OOM each other and the failures look like results.

### CVA6 modules

CVA6 is proved per module: each module is lowered as a subtree through a gate
wrapper that binds the config parameters (`scripts/cva6_module_wrappers/`), then
follows the same pipeline as DINO, with the LEC gate still mandatory:

```text
subtree lowering → LEC gate (RTL ≡ LGraph) → pass.lean --set formal.lean.emit_fast_bridge=true
    → static gates (seconds) → full typecheck
```

Proven modules:

| module | nodes | flops | max width | GetMask (all-ones) | wall | peak RSS | result |
|---|---|---|---|---|---|---|---|
| `cva6_alu_export` | 6,305 | 0 (comb) | 576 | 2,681 (100 %) | 4 h 15.8 min | 25.3 GB | `exit 0`, 0 errors, 0 sorries — `_comb` proven |
| `cva6_tlb_gate` | 2,061 | 137 | 513 | 901 (100 %) | 5 h 12.5 min | 12.2 GB | `exit 0`, 0 errors, 0 sorries — `_comb`/`_next`/`_step` all proven |

Also proven with `emit_fast_bridge=true` (`exit 0`, 0 errors, 0 sorries): the
CVA6 modules `ras`, `instr_realign`, `raw_checker`, `controller`, `csr_buffer`,
`compressed_decoder` and `instr_scan`.

**Trust footprint of the ALU proof** (audited): 0 `sorry`, 0 `admit`, 0 `axiom`;
6,305 per-node `_rec` theorems (one per node, all kernel-checked); 6,548 kernel
`decide`s; and exactly **4** `native_decide` uses — `topo.Nodup`,
`∀ n ∈ topo, (nodes n).isSome`, `DepOrderedB`, and `BT.keys phiTree ⊆ topo`.  All
four are *structural* well-formedness facts about a concrete finite graph, not
semantic claims, so the Lean compiler enters the trusted base only for those; every
semantic step (per-node recurrences, combiner, closers) is kernel-checked.

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

## Build

Use a project-local runtime directory:

```bash
cd <livehd>/formal/lean
mkdir -p ../../generated/pass_lean_runtime_tmp
ELAN_HOME=<project-local-elan-home> \
TMPDIR=<livehd>/generated/pass_lean_runtime_tmp \
TMP=<livehd>/generated/pass_lean_runtime_tmp \
TEMP=<livehd>/generated/pass_lean_runtime_tmp \
lake build
```

Build the pass and CLI, and run the pass's unit tests (scan, certificate IR,
DCERT2 and legacy emitter):

```bash
cd <livehd>
bazel build //pass/lean:pass_lean
bazel build //lhd:lhd
bazel test //pass/lean:lean_export_tests
```

## Options

The `pass.lean` knobs are `--set formal.lean.<label>=<value>` (e.g.
`formal.lean.emit_cert`), **not** the old `lean.*`.  With `lhd compile verilog`,
`--emit-dir lean:<dir>` selects the output directory.

| label | default | meaning |
|---|---|---|
| `mode` | `legacy` | `legacy` or `verified_compiler` (see the top of this README) |
| `strict` | `true` | Reject invalid dependency widths and undersized constants |
| `emit_cert` | `true` | Emit graph certificate and cert-model definitions; `false` = model-only |
| `emit_fast_bridge` | `false` | Emit the step-5 fast-view bridge (`_comb = _comb_cert`, ...) |
| `max_width` | `1024` | Cap on node bit width; `0` or `unlimited` = no cap (see below) |
| `max_nodes` | `0` | Cap on the combinational cone's node count; `0` or `unlimited` = no cap |
| `cert_wf` | `skip` | Certificate well-formedness proof mode: `skip`, `eval`, `sorry`, `chunked` |
| `cert_wf_fallback` | `fail` | `fail`, `sorry` or `eval` for chunk shapes `cert_wf=chunked` does not support |
| `cert_chunk_size` | `25` | Node certificates per chunk for `cert_wf=chunked` |
| `cert_chunk_limit` | `0` | Emit only the first N certificate chunks, for proof-shape testing (`0` = all) |
| `top` | — | Top module name override |
| `path` | `.` | Output directory for emitted Lean files |

`mode=verified_compiler` forces `emit_cert=true` and `emit_fast_bridge=false`.

### Node bit-width cap (`max_width`)

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

The emitter can be tested without touching root-level temporary directories:

```bash
cd <livehd>
mkdir -p generated/pass_lean_smoke/simple/lean \
         generated/pass_lean_smoke/simple/work

bazel-bin/lhd/lhd compile verilog generated/pass_lean_smoke/simple_add.v \
  --reader yosys-verilog \
  --top simple_add \
  --emit-dir lean:generated/pass_lean_smoke/simple/lean \
  --workdir generated/pass_lean_smoke/simple/work \
  --result-json generated/pass_lean_smoke/simple/result.json

cd <livehd>/formal/lean
ELAN_HOME=<project-local-elan-home> \
TMPDIR=<livehd>/generated/pass_lean_runtime_tmp \
TMP=<livehd>/generated/pass_lean_runtime_tmp \
TEMP=<livehd>/generated/pass_lean_runtime_tmp \
lake env lean <livehd>/generated/pass_lean_smoke/simple/lean/simple_add_Lgraph.lean
```

Expected result: the generated fast model and non-empty graph certificate
typecheck.  The theorems that the fast model equals the certificate evaluator
are only emitted with `--set formal.lean.emit_fast_bridge=true` (step 5 above).

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
a mistranslation such as a wrong `Get_mask(a,-1)` mask width would be REFUTED here.

## DINO Lean Gate

All three DINO CPUs convert RTL → Lean model.  Each emitted
`<Top>_Lgraph.lean` has typed `<Top>_in` / `<Top>_out` / `<Top>_state` structures
(fixed-width `BitVec n`), the `<Top>_comb` / `<Top>_next` / `<Top>_step` fast
model, and (with `emit_cert=true`) the graph certificate + `evalGraph`-correct
instantiation.

| Design | Lean model | Lean typecheck | RTL ≡ LGraph (LEC gate) |
|---|---|---|---|
| SingleCycleCPU | ~19k lines | model+cert typechecks (~3 min, ~6 GB) | PROVEN — 42 flop-cut miters, cvc5 |
| PipelinedCPU | ~20k lines | model+cert typechecks (~3.7 min, ~6.5 GB) | PROVEN — 73 flop-cut miters, cvc5 |
| PipelinedDualIssueCPU | ~42k lines | **model-only** typechecks (~10 min, ~10 GB) | PROVEN — 106 flop-cut miters, cvc5 |

### A. Generate one design directly (fastest, self-contained)

```bash
cd <livehd>
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
cd <livehd>
LEAN_EMIT_CERT=true \
OUT=<livehd>/generated/dino_lgraph_lean_dev \
scripts/run_dino_lgraph_lean.sh
```

This runs the RTL≡LGraph **LEC gate first** (aborts on REFUTED), then `pass.lean`,
then typechecks each model. Useful env: `RUN_LEC_GATE=false` (skip gate),
`RUN_LEAN=false` (skip typecheck / model-only), `LEAN_EMIT_CERT=false`
(model-only emission), `LEAN_MAX_WIDTH=0` (unlimited width).

### C. Typecheck a generated design by hand

```bash
cd <livehd>/formal/lean
TMPDIR=<livehd>/generated/dino_lgraph_lean_dev/runtime_tmp \
lake env lean <livehd>/generated/dino_lgraph_lean_dev/lean/SingleCycleCPU_Lgraph.lean
```

## Memory Nodes

`Ntype_op::Memory` nodes emit a fast model: a function-valued state field
`(BitVec addr -> BitVec data)`, one `mem_read` per read port in `<Top>_comb`, and
the write ports folded (`mem_write` / `mem_write_be`) in `<Top>_next`.
Supported (each beyond the `pass.isabelle` v1 restrictions):

- **Any number of read/write ports** — each read port binds a distinct
  `n_<mem>_p<pid>` output; write ports are folded in port order (highest
  `port_id` wins on a same-cycle same-address collision).
- **Read-during-write policy** (`fwd`): `fwd=1` (write-first / transparent) reads
  the post-write image; `fwd=0` (read-first) reads the old array.
- **Sync-read** (`type == 1`): a registered read-data field per read port,
  updated via `sram_sync_read_reg_next`.
- `bits % wensize == 0` (byte/bit write-enable) still required.

### Memory certificate

A memory-bearing design gets a real graph certificate and proves the step-5 bridge
(`_comb`/`_next`/`_step` = `_cert`).

The certificate evaluates the graph at **`CertVal` (`bv | mem`)** rather than `BV`,
via `evalGraphC` — the additive twin of `evalGraph` in `GraphRefine.lean`.  It is
additive: the `BV` path that non-memory designs elaborate against is unchanged.
`DepOrdered` / `DepOrderedB` are value-free and shared between the two paths, and
with them the `native_decide` well-formedness gates.

A Memory node is multi-output (one read-data value per read port plus the array next
state) while `NodeCert` carries one width and one value, so the certificate
builder (`memory_lowering.*`) **decomposes** it:

| piece | becomes |
|---|---|
| committed array image | a **source**, like a flop (`CertVal.mem (memenc s.<field>)`) |
| each write port | one `Op_MemWrite` / `Op_MemWriteBE`, **chained** in `memory_write_fold` order so a later port wins a same-address collision |
| each read port | one `Op_MemRead` over the image *that port* observes |
| every port operand | an arity-1 `Op_Or` resize |

The read-port image is the subtle part: read-during-write is per *(read, write)*
**pair** (`memory_fwd_bit`), so two read ports generally observe different prefixes
of the write chain.  A single linear chain would be wrong.  Chains are keyed by their
forwarded-set signature and shared when two reads forward from the same set; the
all-writes chain is the array next state, decoded back with `memdec`.

The bridge reuses the existing per-op lemmas rather than duplicating them.
`eval_op_cert` reduces on the operator constructor, so for a concrete operator the
per-node goal is *defeq* to that constructor applied to the underlying equation:

```lean
show CertVal.bv (bvenc (fv_n i s)) = CertVal.bv (eval_op <op> w [..])
refine congrArg CertVal.bv ?_
-- the ordinary per-op bridge proof, unchanged
```

so all ~30 `OpBridge` lemmas keep applying verbatim.  Memory nodes take the same
shape with `CertVal.mem` / `memenc` and the three memory bridges
(`mem_read_bridge`, `mem_write_bridge`, `mem_write_be_bridge`).

Two encoding details matter for soundness: `memenc` guards its domain, so
out-of-range indices cannot alias onto a written address; and every port operand
gets a resize node, so a wider address dep cannot index past the array (which
would silently return zero rather than error).

**Proven** (`exit 0`, 0 errors, 0 `sorryAx`, 3/3 `_refines_fast`):

| design | cert nodes | wall | peak RSS | shape |
|---|---|---|---|---|
| `ram1` | 18 | 10 s | — | 1R/1W async, bit-level write mask (`wensize == bits`) |
| `ram_be` | 66 | 18 s | — | byte-enable write, `byte_w = 4` |
| `ram_2w` | 26 | 97 s | — | 2 write ports — write chain and `fwd` matrix |
| `ram_sram` | 42 | 238 s | — | 3 read ports, both `Op_MemWrite` and `Op_MemWriteBE` |
| **`intpipe_csr_msgs`** | **7,447** | **25.0 min** | **14.1 GB** | CORE-ET: 2×60-bit memory, 7 read / 2 write ports |
| `SingleCycleCPU` | 4,772 | 22.7 min | 11.7 GB | **non-memory regression** |

`intpipe_csr_msgs` is a real memory-bearing design proven end-to-end: 7,422
LGraph nodes plus 25 synthetic from the decomposition, and 4 `Op_MemWriteBE` chain
nodes because its 7 read ports fall into two distinct forwarded sets.

`SingleCycleCPU` is the non-memory check: its emitted text has zero `CertVal` /
`memenc` / `evalNodeC` / `asBV` occurrences, so the `BV` path is untouched.

**Sync-read (`type == 1`)** needs no new certificate operator —
`sram_sync_read_reg_next ren raw cur = if ren then raw else cur`, so the read-data
register is a source and its next value an `Op_MuxBool` over an ungated
`Op_MemRead` (a literal enable, closed by `mem_read_en_bridge`).  None of the
proven designs above contains a type-1 memory.

Minimal memory example (async-read / sync-write SRAM), verified to typecheck:

```bash
cd <livehd>
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
```
(`yosys-slang` reader; the `yosys-verilog` reader can trip a yosys `memory -nomap`
assert on some RAM shapes. A function-typed state field cannot `deriving Repr`, so
`<Top>_state` derives `Inhabited` only when it holds a memory.)

Real CVA6 SRAM (`tc_sram`) via the module stress runner:

```bash
cd <livehd>
CVA6_TOP=tc_sram LEAN_EMIT_CERT=true scripts/run_cva6_module_lean_stress.sh
```

## Unbounded proofs on the projection simulator (`sym_verify`, `sym_refine`, `sym_refmap`)

A design's unbounded properties (invariants, refinement with stuttering) are checked on
the residual of the Futamura projection, the same artifact the FP simulator runs. The
checker is proved sound once for every design (`pass/lean/tools/HDLProof/Sym/*`, the
proof tools; see `pass/lean/tools/README.md`); a run per design only produces
obligations and checks proofs.
No per-design Lean proof is written.

The pipeline of one run:

1. The interpreter is specialized to the certificate, giving the residual
   (`MemFused.projectMemFDesignAt`, a trusted native run).
2. The residual is evaluated symbolically: one step of the design as a circuit over the state and inputs.
3. The obligations are built: base and step of k-induction, or one step obligation per conjunct and case.
4. Each obligation is bit-blasted to CNF (`Std.Tactic.BVDecide`).
5. cadical is run with `--lrat`. It is untrusted.
6. Lean's `LRAT.check` checks the proofs.
7. The verified checker accepts or rejects. Its acceptance is one of the theorems `runInvKOf_sound_ref`, `runConjTOf_sound_ref` or `runRefKOf_sound_ref`, each over `interpretDesign`.

**Trusted:**
- the front end and certificate exporter;
- the native execution of the specializer and of the checker;
- the Lean kernel.

### Build

```bash
# lhd (see Build above); the native drivers, into a project-local root:
cd formal/lean
ROOT=$PWD/../../generated/sym/nat_root
OPT=-O2 ROOT=$ROOT bash scripts/build-opt.sh ../../pass/lean/tools/drivers/sym_verify.lean \
    ../../pass/lean/tools/drivers/sym_refine.lean ../../pass/lean/tools/drivers/sym_refmap.lean
# the solver: cadical ships with the Lean toolchain; link it next to the drivers
# (or pass solver:<path>)
ln -s "$(dirname "$(elan which lean)")/cadical" $ROOT/nat/cadical   # still in formal/lean
```

The self-test runs the whole flow on small fixtures (counters, a two-phase accumulator, a
pair of registers, two cores with a case split):

```bash
SYM_NAT=<root>/nat LHD=<lhd> bash pass/lean/tools/tests/sym_selftest.sh
SYM_NAT=<root>/nat LHD=<lhd> bash pass/lean/tools/tests/refmap_selftest.sh   # refinement maps
```

### Export a design

```bash
lhd compile top.sv --reader yosys-slang --top top --set yosys.setundef=zero \
    --emit-dir lg:$W/lg --workdir $W/w1
lhd pass single_edge --top top lg:$W/lg --emit-dir lg:$W/se --workdir $W/w2
lhd compile lg:$W/se --top top --emit-dir lean:$W/lean --workdir $W/w3 \
    --set formal.lean.mode=verified_compiler --set formal.lean.max_width=0
```

This writes `$W/lean/top.dcert` and `$W/lean/top_io.json`. The sidecar names every input,
every output, and every flop in runtime order, e.g. `cpu_state[3]` or `mem[1][7]`. Properties,
specs and counterexamples refer to those names.

A relation or observer is ordinary RTL: add it to the design as extra outputs, one per
property or conjunct, then export as above.

### Invariants and k-induction

```bash
sym_verify top.dcert '!bad' k:1,2 work:$W/sym
sym_verify top.dcert 'inv_a,inv_b' asm:in_reset in:rst_ni=0 k:1
```

- **Properties.** An output name means "nonzero"; `!` means "zero". A comma-separated list gives candidates; the first one proved wins.
- **Assumptions.** `asm:<output>` and `in:<input>=<value>` say which cycles count.
- **Initial state.** All flops and memory words are zero.
- **Result lines.**
  - `INVARIANT PROVED (k=…)`;
  - `COUNTEREXAMPLE (base)`, decoded and replayed on the simulator;
  - `NOT K-INDUCTIVE (step)`.
- **F2 theorem file.** `emit:<file>` writes the proved property as a theorem.

### Conjunctive invariants (`conj:<spec>`)

A relation is given as many named conjuncts. Each conjunct's step obligation assumes only
its hypotheses and is solved on its own, in parallel, so a failure names one conjunct.

```bash
sym_verify top.dcert conj:rel.spec sf:20000000 wf:500000 jobs:12 work:$W/sym
```

Spec format: one conjunct (or one group of conjuncts) per line.

```text
<name> [!]<output>                    an output is nonzero (zero with !)
<name> eq <reg> <reg>                 two registers are equal (bit by bit for a register base name)
<name> eqif <case> <reg> <reg>        ... whenever <case> holds
<name> oneof <case>;<case>;...        the state is in one of these cases
... hyps: <name> <name> ...           the conjuncts this one's step may assume (default: all)
... split: <name of a oneof>          check this conjunct once per case of that oneof
<case> = <reg>=<value>[,<reg>=<value>...]
```

How the checker uses these lines:
- **Register equalities.** An `eq`, and an `eqif` whose case holds, make the step state use one variable for both registers. Identical next-state logic then becomes identical expressions.
- **Case splits.** With `split:`, the case's registers are constants, so the control logic folds to one branch per case.
- **Cone of influence.** Each obligation keeps only the definitions its own formula reaches.

**Options:**

| option | meaning |
|---|---|
| `jobs:<n>` | obligations solved in parallel (default 8) |
| `runs:<n>` | per-case symbolic runs built at a time (default: `jobs`) |
| `sf:<n>`, `wf:<n>` | specializer and walk fuel |
| `solver:<path>` | the cadical binary |
| `work:<dir>` | where CNF, LRAT and counterexample files go |
| `replay` | also replays step counterexamples on the simulator |

Output: one line per obligation (target, case, clause count, solver time, verdict). Then
`RESULT: INVARIANT PROVED (conjunctive, k=1)`, or `NOT INDUCTIVE (step): not preserved
under their hypotheses: [...]`.

**Counterexamples.** A step that is not preserved is decoded into
`<work>/cti_<target>.txt`. It contains:
- the conjuncts false at cycle 0 and at cycle 1, evaluated on the decoded state;
- the nonzero inputs of both cycles;
- the registers that change;
- the whole cycle-0 state, by register name.

An agent iterates on these files: decide from the RTL which fact is missing or wrong,
change the relation or the spec, and rerun. A cycle-0 list that is not empty names
conjuncts left out of the target's `hyps:`. The report is diagnostic only; verdicts never
depend on it.

### Refinement with stuttering (`sym_refine`)

```bash
sym_refine impl.dcert spec.dcert pair:<implOut>=<specOut> [adv:[!]<implOut>] \
    [map:<specIn>=<implIn>] [stateq] k:1,2,3
```

- **What is proved.** The implementation refines the specification up to stuttering.
- **Witness and stuttering.** The specification steps only on cycles where the witness `adv` fires; without `adv`, the two designs step in lockstep.
- **Output correspondence.** Every `pair:` must agree on every witnessed cycle.
- **`stateq`.** Also requires equal next states, which an identity refinement needs.
- **Counterexamples.** They are decoded and replayed on both simulators.

### Refinement maps (`sym_refmap`)

```bash
sym_refmap specfacts spec.refmap [emit:[f1:|f2:]<file>]
sym_refmap refmap impl.refmap jobs:12 [emit:[f1:|f2:]<file>]
```

- **What is proved.** The implementation refines a spec certificate with stuttering. At every
  cycle the spec's state is a map of the implementation's outputs, and each cycle's events
  (slots, over real ports) are spec steps on their channels. Ranked channels also get the
  progress obligations.
- **Spec facts.** Moore, and confluence of every pair of channels. They are checked once per
  spec and give equal streams for every two implementations of that spec.
- **Format.** The `.refmap` format, the targets, the results and the theorem files are in
  `pass/lean/tools/formats/refmap.md`. Counterexamples go to `<work>/cti_<target>.txt`.

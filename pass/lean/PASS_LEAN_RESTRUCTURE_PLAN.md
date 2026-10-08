# `pass.lean` Shared-Architecture and Legacy Refactor Plan

Status: revised after the B1+B2 modular refactor\
Implementation baseline: `origin/b1-b2-verified-compiler` at `1af4e1492`\
Primary code: `pass/lean/`\
Integration branch to create: `refactor/pass-lean-shared-architecture`

This plan starts from the architecture that now exists. It does not recreate
`DesignScan`, `CertificateIR`, or the verified compiler exporter under new
names. The remaining large C++ problem is the 2,717-line
`pass/lean/legacy_model.cpp` compatibility path.

## 1. Terminology: EPRP

LiveHD uses `Eprp`, `Eprp_method`, and `Eprp_var` as historical API names. The
repository does not give an authoritative expansion of the acronym, so do not
invent one in code or documentation.

Operationally, EPRP is the pass invocation layer:

- `Eprp` holds named commands such as `pass.lean`.
- `Eprp_method` registers the callback and accepted labels.
- `Eprp_var` carries graphs and label values into the callback.
- `Pass_lean::setup` registers the command.
- `Pass_lean::work` iterates the selected graphs.

EPRP is not part of the Lean semantics. Keep it as a thin adapter around
ordinary C++ library entry points.

## 2. Existing Architecture: Do Not Replace It

Commit `afafd4f70` introduced the shared architecture, and `1af4e1492` added the
active-low reset correction. The production verified path is already:

```text
Pass_lean / LeanOptions
  -> scan_design(Graph, ScanOptions) : DesignScan
  -> build_certificate(DesignScan, CertificateOptions) : CertificateIR
  -> emit_design_cert(DesignScan, CertificateIR, ostream)
```

The existing ownership boundaries are:

| Layer | Existing files | Responsibility |
|---|---|---|
| EPRP adapter | `pass_lean.cpp`, `pass_lean.hpp` | Registration, graph iteration, mode dispatch |
| Options | `lean_options.cpp`, `lean_options.hpp` | Parse and validate labels |
| HHDS access | `graph_access.cpp`, `graph_access.hpp` | Pin/node access, widths, constants, memory policy |
| Owned scan | `design_scan.cpp`, `design_scan.hpp` | Ports, flops, memories, roots, cycle-safe reachable topo order |
| Semantic certificate | `certificate_ir.cpp`, `certificate_ir.hpp`, `certificate_builder.hpp` | Typed ops, sources, dependencies, outputs, state drivers, dense slots |
| Memory lowering | `memory_lowering.cpp`, `memory_lowering.hpp` | Reads, writes, forwarding, byte enables, ROMs, sync-read registers |
| Lean formatting | `lean_format.cpp`, `lean_format.hpp` | Sanitization, literals, arrays, atomic output |
| Verified exporter | `emit_design_cert.cpp`, `emit_design_cert.hpp` | Emit one Lean `DesignCert` and its compiler instantiation |
| Legacy compatibility | `legacy_model.cpp`, `legacy_support.hpp` | Old fast model, old GraphCert, generated bridge and proof text |

`DesignScan` and `CertificateIR` are the shared layers. Their current public
types are the baseline interfaces. Extend them only when a consumer needs
semantic information that cannot be derived from existing fields.

Do not introduce a second `DesignScan`, a second operation enum, a second memory
lowering path, or another private certificate builder.

## 3. Canonical Data Flow

All output modes must use one graph scan and one semantic certificate:

```text
HHDS Graph
    |
    v
scan_design
    |
    v
DesignScan
    |
    v
build_certificate
    |
    v
CertificateIR
    |
    +--> emit_design_cert          production B1+B2 artifact
    +--> emit_direct_cert          optional Direction 2 schema extension
    +--> emit_runtime_cert         Direction 4 transport
    +--> emit_legacy_fast_model    compatibility/debug view
    +--> emit_legacy_graph_cert    compatibility certificate
    +--> emit_legacy_cert_wf       old per-design structural checks
    +--> emit_legacy_fast_bridge   old fast-model/certificate bridge
```

The distinction between the two certificate names is important:

- `CertificateIR` is the canonical C++ semantic object.
- `DesignCert` is its production Lean representation consumed by
  `Compiler.compileDesign` and direct semantics.
- Legacy `GraphCert` is another presentation of `CertificateIR`; it is not
  allowed to be built independently from HHDS.

## 4. Current Status

The original common-infrastructure phases are no longer proposals:

| Work item | Status at `1af4e1492` |
|---|---|
| Thin EPRP adapter and typed options | Complete |
| HHDS access extraction | Complete |
| Owned `DesignScan` | Complete |
| Typed `CertificateIR` and dense indexing | Complete |
| Memory lowering | Complete |
| Lean formatting and atomic output | Complete |
| Verified `DesignCert` emitter | Complete |
| Standalone saved-graph exporter | Complete |
| Scan/certificate tests | Complete, 14 C++ tests |
| Active-low reset polarity | Corrected and regression tested |
| Legacy emitter extraction | Partial: moved, but still monolithic and graph-coupled |
| Direction 2 schema integration | Not merged into the shared baseline |
| Direction 4 runtime transport | Not merged |

Recorded B1 validation at this baseline includes byte-identical accepted exports
for 3 DINO, 85 CORE-ET, and 46 CVA6 saved graphs. All three DINO certificates
instantiate `compileDesign_correct` without `sorryAx`.

## 5. Legacy Refactor Goal

The legacy mode remains useful for differential validation and for the old
fixed-width named model, but it must become a normal consumer of the shared
architecture.

Required public entry point:

```cpp
void emit_legacy_model(const DesignScan& design,
                       const CertificateIR& certificate,
                       const LegacyEmitOptions& options,
                       std::ostream& output);
```

After the refactor, legacy code must not:

- Receive an `hhds::Graph`, `Node`, `Node_pin`, or `Dlop`.
- Call `bits_of`, `hydrate_const`, `inp_edges`, or graph traversal helpers.
- Re-run reachable topological sorting.
- Re-parse memory ports or reconstruct forwarding policy.
- Allocate a second family of synthetic certificate IDs.
- Recreate `CertBuild`, `CertNodeInfo`, or a stringly typed operation map.
- Change graph acceptance based on whether legacy or verified output is chosen.

## 6. Legacy Refactor Work Packages

### L0: Freeze the Legacy Contract

Before moving code, record byte hashes and semantic summaries for:

- Tiny tests for every supported operator.
- A combinational design and a reset/enable flop.
- Mutable RAM, synchronous RAM, byte-enable RAM, and immutable ROM.
- All three DINO designs.
- Representative accepted CORE-ET and CVA6 modules.
- Every currently intentional refusal class.

Use `lean_export_graph` on saved graph databases. Store runtime artifacts under
`generated/legacy_refactor/`, never under `/tmp`.

Move-only commits must remain byte-identical. If a later semantic cleanup makes
textual identity undesirable, compare parsed `DesignCert`/`GraphCert` values and
record the reviewed textual delta.

### L1: Make Legacy Invocation Use the Shared Pipeline

Change `Pass_lean::emit_for_graph` so both modes first call `scan_design` and
`build_certificate`. Dispatch only after those calls:

```cpp
const auto design = scan_design(*graph, scan_options);
const auto cert   = build_certificate(design, certificate_options);

if (mode == LeanMode::VerifiedCompiler)
  emit_design_cert(design, cert, output);
else
  emit_legacy_model(design, cert, legacy_options, output);
```

During this increment, the old legacy implementation may remain behind a
temporary adapter for output comparison. Do not keep that adapter in the final
architecture.

### L2: Remove Duplicate Formatting

Move or replace legacy copies of:

- `sanitize_lean`
- integer and `BitVec` literal rendering
- list/array rendering
- balanced lookup-tree rendering
- atomic temporary-file finalization

Use `lean_format.*` for shared syntax. Add formatting helpers there only when
they are representation-neutral. Legacy theorem text belongs in a legacy
emitter, not in `lean_format.*`.

### L3: Extract Legacy Schema Emission

Create:

```text
emit_legacy_schema.cpp
emit_legacy_schema.hpp
```

This component emits `<Top>_in`, `<Top>_out`, `<Top>_state`, field mappings, and
reset-state declarations from `DesignScan`. It owns Lean names but no graph
handles and no node semantics.

Input/output/state field prefixes must continue to guarantee global selector
uniqueness. Preserve comments mapping sanitized fields back to RTL names.

### L4: Extract the Fixed-Width Fast Model

Create:

```text
emit_legacy_fast_model.cpp
emit_legacy_fast_model.hpp
```

Move the old `emit_node_expr`, source expression, width-cast, combinational,
next-state, and step generation here. Re-key the implementation over
`CertificateIR::sources`, `CertificateIR::nodes`, `CertificateIR::outputs`,
`CertificateIR::flops`, and `CertificateIR::memories`.

Generate all node right-hand sides from the structured `Operation` enum. The
operation renderer is a pure mapping:

```cpp
FastExpr lower_fast_expr(const CertificateIR&, const CertNode&);
```

It must not discover dependencies or widths. Those facts are already in the
certificate. Preserve old-state evaluation and simultaneous flop update.

### L5: Extract Legacy GraphCert Presentation

Create:

```text
emit_legacy_graph_cert.cpp
emit_legacy_graph_cert.hpp
```

Emit legacy `NodeCert`, source IDs, topo order, outputs, and state drivers only
from `CertificateIR`. Keep sparse origin IDs for diagnostics and use the
canonical dense mapping where the Lean schema requires slots.

Delete the old certificate construction functions after parity is established:

- `cert_dep_id`
- `cert_node_expr`
- `cert_memory_expand`
- the certificate-building part of `CertBuild`

### L6: Extract Certificate-WF Proof Generation

Create:

```text
emit_legacy_cert_wf.cpp
emit_legacy_cert_wf.hpp
```

Keep certificate chunking isolated here. Preserve the established scalable
proof policy:

- Constant-only chunks use symbolic constant-chunk lemmas.
- Simple mixed chunks use shape and concrete dependency-subset lemmas.
- No chunk runs `by eval` over a global `all_ids` list.
- Unsupported chunk shapes fail by default instead of silently selecting the
  old expensive fallback.
- Global uniqueness must eventually use chunked/dense checking rather than one
  giant `distinct all_ids by eval`.

This emitter consumes certificate structure only. It does not render the fast
model and does not inspect HHDS.

### L7: Extract the Legacy Fast/Certificate Bridge

Create:

```text
emit_legacy_fast_bridge.cpp
emit_legacy_fast_bridge.hpp
```

This component emits the optional per-node or grouped proof that the named
fixed-width model agrees with the legacy certificate evaluator. It consumes the
same `CertificateIR` used by both sides. It may use reusable Lean operator
bridge theorem names, but it must not duplicate operator semantics in C++.

The production B1+B2 path does not need this bridge because its model is
`compileAndRun designCert`; `compileDesign_correct` supplies the generic bridge.
Retain this generator only for legacy comparison and research experiments.

### L8: Reduce `legacy_model.cpp` to Orchestration

After L3-L7, `legacy_model.cpp` should only coordinate the sub-emitters and be
under roughly 250 lines. Delete `legacy_support.hpp` after all private semantic
structures have disappeared.

No behavior-changing fix may be hidden in a move commit. Land a semantic fix in
a separate commit with a tiny C++ test, a Lean oracle when applicable, and a
corpus delta report.

## 7. Target File and Bazel Layout

Keep the existing flat `pass/lean` architecture and add these libraries:

```text
//pass/lean:lean_graph_scan          existing
//pass/lean:lean_certificate_ir      existing
//pass/lean:lean_emitters            existing verified emitter
//pass/lean:lean_legacy_schema       planned
//pass/lean:lean_legacy_fast_model   planned
//pass/lean:lean_legacy_graph_cert   planned
//pass/lean:lean_legacy_cert_wf      planned
//pass/lean:lean_legacy_bridge       planned
//pass/lean:lean_legacy_emitter      planned orchestration
//pass/lean:pass_lean                EPRP registration only
//pass/lean:lean_export_tests        shared tests
```

Only `:pass_lean` uses `alwayslink = True`. All scanner, certificate, and emitter
libraries must be callable from tests and standalone tools without EPRP.

## 8. Proof and Research Directions

The branch names describe different proof strategies. They are not all C++
export modes, and some are experiments rather than completed implementations.

| Label | Branch | Direction and status | Shared artifact |
|---|---|---|---|
| B1+B2 | `b1-b2-verified-compiler` | Production verified residual compiler; implemented and proved | `DesignCert` |
| D2 | `direction-2-ir-semantics` | Independent direct interpreter/oracle; implemented on its branch | `DesignCert` |
| D3 | `direction-3-translation-validation` | Optional reification/translation-validation research | `DesignCert` |
| D4 | `direction-4-incremental` | Runtime certificate serialization/loading | `DesignCert` transport |
| VS | `verified-staged-compiler` | Historical staged-compiler proposal; no unique implementation commit at its current tip | Superseded by B1+B2 unless distinct results are recovered |
| IVP | `interpreter-value-polymorphic` | Generic `NodeSemantics` abstraction shared by evaluators | Lean semantic library |
| CE | `certifying-elaborator` | Experimental derivation of legacy bridge proof terms; gate 1 only | Legacy `GraphCert` plus fast model |
| FP | `projection` | Futamura first/second-projection research | `DesignCert` as static program |
| CL | `certifying-layout` | Layout/DRC certification research | Separate `LayoutCert` if integrated |
| GP | `geometry-proof` | Geometry proof experiments | Separate geometry certificate |

### B1+B2: Verified Compiler

This is the chosen production compiler direction:

```text
DesignCert
  -> Compiler.compileDesign
  -> ResidualProgram
  -> denoteResidual
```

`compileDesign_correct` proves that every accepted `DesignCert` produces a
residual model equal to `interpretDesign` for all runtime inputs and states.
The generated file contains the certificate and instantiates that theorem; it
does not emit a second handwritten fast model.

Integrate from the completed modular baseline. Do not copy the pre-refactor
`verified-compiler.cpp` or `design_cert_export.hpp`; those were removed by the
shared architecture.

### D2: Direct IR Semantics

D2 directly executes `DesignCert` without constructing `ResidualProgram`. It is
an independent semantic consumer and comparison oracle. Port its clock/edge
schema additions into `DesignScan`, `CertificateIR`, and the canonical
`emit_design_cert` only after defining backward-compatible defaults.

D2 must not retain `IR_Semantics_pass.cpp` as a second graph traversal. Once
integrated, D2 is Lean-only apart from shared schema fields.

### VS: Verified Staged Compiler

`verified-staged-compiler` currently points at `aa2f8134a`, the same old
monolithic baseline used by the historical `projection` branch, and has no
branch-specific implementation commit. Treat VS as the name of a proposed
strategy, not as completed code to merge.

Conceptually VS belongs to the same compiler-verification family as B1+B2: fix
the static design, produce a staged/residual model, and prove it equivalent to
the design interpreter. B1+B2 now supplies the concrete implementation and
theorem (`compileDesign_correct`). Mark VS superseded unless an audit finds a
strictly different theorem or target-language guarantee.

### CE: Certifying Elaborator

`certifying-elaborator` is not the verified compiler. It belongs to the legacy
bridge-proof-generation family: retain the handwritten fast model, then use a
Lean elaborator/metaprogram to construct the per-node proof terms relating that
model to `GraphCert` evaluation.

Its unique commit `e7947110e` implements only gate 1: reduction-cost probes on
18-168-node fixtures. It reports roughly flat 6.6-10 ms per node and estimates
about 38 seconds of reduction/rechecking for SingleCycleCPU, but it does not
implement the final elaborator or prove a real DINO design.

The commit message calls CE “Branch 3.” That is an older alternatives label and
must not be confused with D3 (`direction-3-translation-validation`).

After B1+B2, CE is optional. Continue it only if the legacy named fast model
must remain a formally bridged product. If continued, it consumes the outputs
of L4 and L5 and owns no graph scan, certificate construction, or C++ operator
mapping.

### IVP, D3, D4, and FP

IVP is reusable Lean infrastructure and should be merged only after checking it
does not make definitional reductions used by legacy proofs opaque.

D3 must reify or validate the canonical `DesignCert`; it gets no C++ exporter.
D4 serializes and loads the canonical certificate; it gets no graph traversal.
FP treats `DesignCert` as the static source program for specialization; it gets
no fork of `pass_lean.cpp`.

## 9. Integration Order

Create a clean worktree from the completed modular commit:

```bash
cd /mada/users/czeng14/projects/livehd-new
git fetch origin
git worktree add /mada/users/czeng14/projects/livehd-pass-lean-shared \
  -b refactor/pass-lean-shared-architecture origin/b1-b2-verified-compiler
```

Execute in this order:

1. L0: freeze legacy output and refusal behavior.
2. L1-L2: route both modes through shared scan/certificate and formatting.
3. L3-L4: split schema and fixed-width model emission.
4. L5-L6: derive legacy GraphCert and WF proofs from `CertificateIR`.
5. L7-L8: isolate the optional bridge and delete graph-coupled legacy state.
6. Integrate D2 schema/semantics without adding another exporter.
7. Integrate D4 transport over the same certificate.
8. Rebase Lean-only D3, IVP, CE, and FP experiments as consumers.

Do not start from `direction-2-ir-semantics`; doing so would discard the newer
owned scan and typed Certificate IR already validated across the saved corpus.

## 10. Validation Gates

Every move must pass:

1. `bazel build //pass/lean:pass_lean //pass/lean:lean_export_graph`.
2. `bazel test //pass/lean:lean_export_tests`.
3. Byte-identical verified `DesignCert` output for the saved accepted corpus.
4. Byte-identical legacy output for move-only commits.
5. Structural certificate equality when a reviewed formatter change occurs.
6. Tiny operator, reset, RAM, ROM, memory-forwarding, and refusal tests.
7. Three DINO `compileDesign_correct` instantiations and clean axiom audits.
8. CORE-ET and CVA6 accepted/refused counts with no silent fallback.
9. D2 direct-vs-compiled comparison after D2 integration.
10. RTL-to-LGraph LEC as a separate upstream validation gate.

LEC does not prove this exporter. It validates the upstream RTL-to-graph path.
The generic compiler theorem validates `DesignCert` interpretation, while the
saved-graph differential tests validate C++ extraction and formatting.

## 11. Commit Sequence From `1af4e1492`

Use behavior-preserving commits:

1. `pass/lean: freeze legacy model and certificate goldens`
2. `pass/lean: route legacy mode through DesignScan and CertificateIR`
3. `pass/lean: reuse shared Lean formatting in legacy output`
4. `pass/lean: extract legacy schema emitter`
5. `pass/lean: extract fixed-width fast-model emitter`
6. `pass/lean: derive legacy GraphCert from CertificateIR`
7. `pass/lean: isolate chunked certificate-WF proof emission`
8. `pass/lean: isolate optional fast-model certificate bridge`
9. `pass/lean: delete legacy graph traversal and private certificate builder`
10. `pass/lean: integrate Direction 2 schema with canonical DesignCert`
11. `pass/lean: add Direction 4 certificate transport`
12. `pass/lean: run DINO, CORE-ET, CVA6, and axiom-audit gates`

Do not combine movement with a semantic correction. A correction must name the
changed behavior and include a focused regression.

## 12. Completion Criteria

The restructure is complete when:

- `pass_lean.cpp` remains only the EPRP adapter and dispatcher.
- `legacy_model.cpp` is small orchestration or has been deleted.
- There is one HHDS scan, one memory lowering, one operation mapping, and one
  semantic `CertificateIR`.
- Verified, direct, runtime-loaded, and projection-based consumers use one
  `DesignCert` schema.
- The legacy fast model and bridge are isolated optional presentations.
- Every emitter can run in unit tests without `Eprp_var` or HHDS traversal.
- No research branch needs to copy `pass_lean.cpp` to test a proof strategy.
- VS is explicitly superseded or justified by a distinct theorem.
- CE is either completed as an optional legacy bridge generator or retained as
  a documented cost experiment, never presented as the production compiler.

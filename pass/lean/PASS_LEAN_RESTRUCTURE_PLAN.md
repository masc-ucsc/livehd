# `pass.lean` Shared-Architecture and Legacy Refactor Plan

Status: L0-L8 implemented on master; full branch migration and restored-flat
proof acceptance remain open.

Audited implementation: `4342b312bb1aee60cde01e45b04a73de274b997c`.
Original B1+B2 planning baseline: `1af4e1492`. Primary code: `pass/lean/`.

The shared architecture and legacy extraction described below have landed.
`legacy_model.cpp` is now 54 lines of orchestration, with separate schema,
fast-model, GraphCert, WF and bridge emitters. The original phases and baseline
measurements remain as historical requirements. They do not establish that all
research branches have migrated or that every restored flat artifact passes.
[Section 14](#14-current-master-audit-and-corrective-migration-gates) records
current source preservation, missing work, reproduced failures and acceptance
gates. Continue development on master without recreating the former PR branch.

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

## 4. Historical B1+B2 Baseline Status

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

## 7. Implemented File and Bazel Layout

The current flat `pass/lean` architecture provides these libraries:

```text
//pass/lean:lean_graph_scan          existing
//pass/lean:lean_certificate_ir      existing
//pass/lean:lean_format              shared formatting only
//pass/lean:lean_emitters            verified emitter
//pass/lean:lean_legacy_schema       implemented
//pass/lean:lean_legacy_fast_model   implemented
//pass/lean:lean_legacy_graph_cert   implemented
//pass/lean:lean_legacy_cert_wf      implemented
//pass/lean:lean_legacy_bridge       implemented
//pass/lean:lean_legacy_emitter      implemented orchestration
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

IVP is reusable Lean infrastructure. Its model/refinement definitions and
definitional-equality probe are already preserved on master (section 14); do
not overwrite them with old branch copies.

D3 must reify or validate the canonical `DesignCert`; it gets no C++ exporter.
D4 serializes and loads the canonical certificate; it gets no graph traversal.
FP treats `DesignCert` as the static source program for specialization; it gets
no fork of `pass_lean.cpp`.

## 9. Historical Integration Order

The following command records the original isolated PR workflow. That refactor
is now on master; do not create this historical branch again. Section 14
specifies the remaining migrations on master.

Original worktree creation:

```bash
cd "$LIVEHD_SOURCE"
git fetch origin
git worktree add "$LIVEHD_REFACTOR_WORKTREE" \
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
8. Port D3, IVP, CE, and FP as consumers, preserving their independent proofs
   and accounting for branch tooling. Current dispositions are in section 14.

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

## 13. User-owned Lean upgrade after withdrawing record nesting

**Status: pending user upgrade.** The project remains pinned to Lean 4.31.0
and Mathlib v4.31.0. At the user's direction, the automatic nesting of legacy
records above 64 fields is withdrawn. Restore the original flat schema emitter;
do not introduce a different model representation to work around Lean bugs.
The completed nested-record proofs remain historical evidence, not acceptance
of the restored flat artifacts. See [MASTER_INTEGRATION.md](MASTER_INTEGRATION.md).

The standalone 128-field allocator crash is fixed in the tested Lean 4.34.1
release. [Upstream PR #7786](https://github.com/leanprover/lean4/pull/7786)
upgraded the allocator, but this audit has not established that commit as the
cause of the observed fix. A separate constructor-field limit still applies.
The exactly-256-field boundary also has a distinct
[checking fix, PR #15075](https://github.com/leanprover/lean4/pull/15075).
Neither fix provides support for arbitrarily large executable flat records.

| Step | Owner / status | Required work and acceptance |
| --- | --- | --- |
| U0 | Agent / complete | Restore the original flat schema, remove nesting-only fixtures and harness, and regenerate all 341 accepted exports. Check that only the 22 previously nested schemas change back, every ordered field is preserved, and all model/certificate definitions and proof text remain identical. Keep historical nested evidence explicitly marked as withdrawn. Completed comparison: 319 identical exports, 22 schema-only restorations, 127 unchanged verified exports; see [restoration evidence](tests/MASTER_FLAT_RECORD_RESTORE_RESULTS.json). |
| U1 | User / pending | Upgrade `formal/lean/lean-toolchain` and the Mathlib revision in `formal/lean/lakefile.toml` together, then regenerate `formal/lean/lake-manifest.json` as a separate commit. The researched candidate is [Lean v4.34.1](https://github.com/leanprover/lean4/releases/tag/v4.34.1) with [Mathlib v4.34.1](https://github.com/leanprover-community/mathlib4/blob/v4.34.1/lean-toolchain). Recheck the target release if upgrading later. No pins change as part of the restoration. |
| U2 | After U1 | Preserve the old logs and hashes; finish or isolate any old-toolchain jobs before changing their library environment. Rebuild the support library and dependencies with the matching toolchain, including `LeanSemanticPrimitives.Compiler.CompileDesign` and the legacy bridge/WF modules. Do not reuse old-version `.olean` files or rely only on the lightweight default Lake target. |
| U3 | After U2 | Repeat standalone 64/128-field Nat and BitVec execution tests and constructor-boundary diagnostics. Recheck the original schemas for CORE-ET `txfmactl_top` (289 state fields) and CVA6 `bht` (256), `csr_regfile` (534), `fpu_wrap` (282), `frontend` (409), and `issue_stage` (326). Record an unsupported flat constructor as a limitation; ask the user to decide any representation change separately. Do not substitute nesting, `noncomputable`, skipped execution, or weaker proofs silently. |
| U4 | After U2/U3 | Replay the recorded 186 CORE-ET/CVA6 configurations and all three DINO designs. Cover the 341 previously accepted artifact/mode pairs, all DINO verified-compiler, legacy-fast, full chunked-WF, and full bridge checks. Record each failure/refusal explicitly. Preserve the 53 upstream compile/normalization dispositions; do not change native LEC or frontend guards to make the matrix pass. |
| U5 | After U4 | Record exact toolchain/dependency commits, source and artifact hashes, `/usr/bin/time -v` wall time/RSS, theorem/axiom audits, and unresolved cases. Require all chunks and complete comb/next/step bridge theorems, no missing audit, fallback, or `sorryAx`. Compare flat models and certificates with the preserved baseline; bring any semantic difference to the user before changing it. Full legacy acceptance remains open wherever the flat-record limit prevents execution or typechecking. |


## 14. Current master audit and corrective migration gates

### 14.1 Snapshot, evidence and preservation boundary

This audit examined `4342b312b`. A read-only remote query reported master at
`94c5967c6`; `8bea45dc2` remains the historical pristine upstream comparison
base, not the current remote tip. No fetch, rebase, push, toolchain upgrade or
semantic implementation change was performed by this audit.

[MASTER_REBASE_AUDIT.json](tests/MASTER_REBASE_AUDIT.json) records full commit
IDs, all 21 inspected branch refs, contribution counts, missing Lean/script
paths, reproduction inputs and hashes, results and required gates. Counts
compare each branch's contribution against its merge base, rather than treating
every old inherited file as work to recover. Identical blobs establish source
retention, not theorem validity or RTL equivalence. The independent Futamura
checkout is recorded separately because its local ref is stale.

The PR preservation boundary remains intact:

- All 117 PR change entries are present; 87 remain byte-identical to the PR
  endpoint. The remaining files include documented adapter, test, tooling and
  report follow-ups, not unexplained omissions.
- Of 87 B1/B2 contribution entries, 82 are present and 52 byte-identical. The
  five absent entries have recorded dispositions: two original deletions and
  three old exporters superseded by the shared architecture.
- All 22 PR formal Lean files and nine B1 compiler modules remain byte-identical.
  IVP's `LGraphModel`, `GraphRefine` and `defeq_probe` are also preserved exactly.
- The 74 protected native LEC files and 3,689 protected upstream graph,
  frontend, normalization, contract and warning-policy files match `8bea45dc2`
  under the audit's recorded exclusions for existing approved build repairs.
- The restored flat schema emitter matches its original pre-nesting version.
  Shared `lean_format` is already split out; `normalize` is already removed.

The old 117-file PR ledger cannot detect work omitted before the PR endpoint.
Likewise a stale branch name cannot inventory an independent live checkout.
Use the contribution inventory and a destination/disposition ledger for every
migration. Review modified files and deleted files as well as new files;
never count absence alone as evidence that every old file must be restored.

### 14.2 Direction disposition and landing order

| Direction / audited source | Current disposition | Remaining gate |
| --- | --- | --- |
| B1+B2 `b04288cca`, PR `3685a977e` | Shared architecture and L0-L8 preserved; adapter follow-ups recorded | Keep preservation boundary and replay current artifacts |
| D2 clean `c21200e0c` | Not landed: 38 Lean contribution files and 30 script paths absent; 16 Lean contributions differ | Port full clock/async/memory schema, eight Direct modules and validation tools; do not replay 133 commits or overwrite current Compiler copies |
| D3 `78a3e0d8c` | Not landed: 82 Lean contribution files absent; 13 differ | Recover translation validation, definition splits, bounded-amount repairs and memory/flop/scaling evidence with current schema proofs |
| D4 `f6ad1fe29` | Not landed: 125 Lean contribution files absent; 15 differ; transport tooling absent | Recover slot/runtime repairs, total parser/renderer roundtrip and C++ serializer/publication as shared-IR consumers |
| FP actual checkout `9bb41a09d` | All 67 Projection files, including 38 Lean modules, absent | Explicit semantic-pin migration and independent theorem rebuild; local `projection` ref `aa2f8134a` is not this implementation |
| IVP `827e53233` | Core model/refinement and defeq probe already retained | Keep definitions transparent; validate current legacy proofs after future changes |
| VS `aa2f8134a` | No unique staged-compiler implementation found | Superseded by B1+B2 unless distinct evidence is recovered |
| CE `e7947110e` | Gate-1 cost study only; two probe files absent | Recover `GATE1_DERIVATION_COST.md` and `mkgate.py` as research evidence; do not claim a complete elaborator or DINO proof |
| CL `07319df15`, GP `f22f9812e` | Geometry/layout modules, frontend tool and findings absent | Record as separate pending certificates/research; do not fold them into RTL DesignCert semantics |
| Historical Lean certificate/translation/CVA6/CORE-ET refs | Inventoried; overlapping old exporter/model copies | Account for unique results/tooling without restoring superseded graph traversal |
| Isabelle | Previous source restoration/audit checkpoint retained | No new Isabelle proof acceptance claimed; current work remains focused on Lean |
| ACL2/Rocq | Inventoried as separate backend branches | Deferred from this Lean migration; no implicit backend port |

Land migration slices on master in this order: first repair proof/replay
acceptance tooling; then port D2 schema with compatibility proofs; then reconcile
D3/D4 shared runtime repairs and narrow module imports; then port versioned D4
transport; then migrate FP with an explicit semantic pin. CE/CL/GP evidence has
its own disposition and does not block the production compiler by pretending
those experiments are implemented. At each slice preserve current adapter,
Hotmux/Concat, reset, signed-rendering and legacy-bridge fixes; maintain one
scan, memory lowering and certificate builder. The user-owned upgrade in
section 13 is a separate prerequisite for acceptance of affected flat models.

### 14.3 Clock, reset and memory schema gap (A02)

The current scan captures clock pins, but `FlopDriver`, `MemoryDriver` and the
Lean certificate omit clock-domain/edge metadata. A fresh two-posedge-clock
fixture exports and typechecks. Its interpreter changes old flop values
`#[1, 2]` to `#[7, 9]` in one logical step. Clock input levels are not edge
vectors: this model cannot express firing only A while B holds. The generic
compiler theorem proves agreement with this clockless interpreter; it does not
prove independent RTL clock behavior. Upgrading Lean does not fix this gap.

Until D2 lands, require a deliberate choice between refusing unsupported clock
configurations and explicitly documenting an all-domains-fire interpretation;
do not infer either contract silently. Port D2's complete metadata: `ClockDesc`,
`DesignCert.clocks`, `FlopDesc.clock` **and `asyncReset`**, `MemoryDesc.clock`,
`ClockEdges` and the `fires`/`allEdges` rules. Include DirectCheck's clock-range,
async-flag and edge-consistency checks, edge-independent asynchronous reset,
quiet-domain hold and memory clock behavior. Prove existing default certificates
agree under `allEdges`, then re-emit and compare the current corpus. The old
54/147 `asyncFlagMismatch` count is historical, not a measured current result.
Any asynchronous Q-read/reset difference from the preserved model must be
presented to the user before adopting changed semantics.

### 14.4 Runtime repairs are code migrations, not Lean upgrade fixes (A03/A04)

Small current-model probes reproduce `Nat.pow exponent is too big` for SHL,
SRA and Sext with amount `8589934576`. The SHL certificate first reports
`compilesOk = true`; compiled execution then panics. Normal small/boundary
controls pass. D3 commit `936870351` and D4 commit `2d98f605e` contain bounded
implementations and eager-semantics equality proofs absent from current master.
Port one consistent version, not both overlapping module copies. Guard SHL by
result width, SRA by source width with sign saturation, and preserve Sext's
zero/source-width/out-of-range cases. Rebuild the recorded equivalence theorems
for evaluator, denotation and residual semantics; test zero, exact boundary,
large amounts and the original runtime blocks. These branch proof sources were
inspected, not rebuilt in this audit.

Current `slotsFrom 0 108666` succeeds under `#eval` but overflows in `lean --run`
at an 8192 KiB main-thread stack. D4 commit `5d5239411` supplies an accumulator
and `slotsFrom_eq_range'`, preserving membership/uniqueness interfaces. Port it
with equality proofs and rerun the main-thread and large hpdcache runtime gates.
Do not claim a universal failure from the successful worker-thread `#eval` case.

D3's `*Defs` splits, narrower imports and D3/D4 memory/flop/scaling modules also
need destination entries and fresh branch-specific gates. The existing
186-configuration export matrix does not cover every branch runtime procedure.
Add those cases, inputs, modes and required theorem identities to the replay
manifest before claiming all earlier progress reproduced.

### 14.5 D4 transport and FP compatibility (A05/A06)

D4 is not Lean-only: it also has a C++ DCERT1 serializer, serializer/parity
checks, runtime scripts and `atomic_publish.hpp`. Its Lean parser is total,
and `CertIORoundTrip` proves `parseCert (renderCert D) = .ok D`. The old plan's
partial/unverified parser description is stale. That theorem covers the Lean
renderer/parser pair; it does not prove the C++ encoder agrees or establish the
provenance of arbitrary external bytes. Port the serializer as a consumer of
`CertificateIR`, preserving canonical slot/opcode/constant encodings, refusals
and parity tests; never copy the old graph-coupled exporter.

DCERT1 omits clock, asynchronous-reset flag and memory-clock fields. Its old
roundtrip proof therefore cannot cover all post-D2 certificates unchanged.
Choose a versioned metadata-complete format with compatibility tests, or refuse
unsupported metadata explicitly; do not decode missing fields into defaults
and call that general equivalence. Preserve the staged-write/rollback tests.
The original two-file publication procedure is not crash-atomic as a pair.

The actual FP checkout pins shared semantics to `90583be13`, not to a moving
tracking target. Seven of eight shared files are identical to that pin; the
model's `bv_shl_step` overlay has three universal `rfl` checks. Replace the
old automatic-repin instruction with an explicit equivalent-preserving schema
migration, overlay checks, all 38 module builds and theorem audits. Keep the
FP correctness path independent of Compiler implementation imports, so later
cross-track comparisons remain independent.

### 14.6 Runner and proof-acceptance corrections (A08-A11/A13)

These are required repairs to the Lean tooling; native `lhd lec` remains
unchanged:

| Finding | Fresh evidence | Required correction and regression |
| --- | --- | --- |
| Retired `formal.strict` forwarded to native LEC | Current native invocation exits 2 with usage error; CORE-ET runner continues after nonzero LEC, even when strict was requested | Remove only obsolete runner setting; classify PROVEN/REFUTED/UNKNOWN/usage/infrastructure and fail required gates. Keep Lean's separate `formal.lean.strict` export option |
| Patch-map presence guard insufficient | A copied script with a map for another checkout exits 0 and uses original unpatched sources | Check logical source root, source/derived hashes and every required substitution; refuse stale/mismatched maps |
| Queue audit based on line counts | Copied queue with fake tools reports PROVEN for `unrelated.theorem` using `untrusted_axiom`; sequential next/step audits absent | Require exact identities, complete required comb/next/step and chunk/global-WF theorem set, and an explicit axiom policy. Test wrong identity, missing audits/chunks, custom axiom, imported sorry and native failures alongside a positive control |
| Positive node-count gate rejects valid empty graph | One-source, zero-node constant-output export typechecks with its step theorem and no `sorryAx` | Accept valid source-only certificates while retaining WF and compiler theorem checks |
| Hotmux status not retained at export boundary | Separate census/compile diagnostics exist, but shared lowering erases Hotmux identity in certificate artifacts | Carry origin and native-proved/deferred/refuted one-hot status into export diagnostics or sidecar; do not imply the priority-value proof proves one-hotness |
| Malformed Hotmux pin order can assert | Adapter calls graph helper with dense-pin assertion before validating layout | Prevalidate adapter inputs; add gapped/malformed-pin structured-refusal controls without changing graph helper/native semantics |

The fake-tool queue probe demonstrates an acceptance bug, not a forged Lean
proof. The frozen one-time replay's separate theorem-identity checks must not
be conflated with this weaker live queue. The repaired policy must explicitly
account for trusted standard axioms and generated `native_decide` facts; banning
all reduction axioms is not a substitute for defining the proof trust boundary.
The inline CORE-ET Lean runner must also build exact imported library modules
or use the shared queue, rather than relying on potentially stale `.olean`s.

Patch provenance must record both declaration-reorder repairs and both
output-logic insertions in the current patcher. The older 125-configuration
CORE-ET matrix already contains 44 configurations using patched sources; the
separate 122-module unpatched triage invalidation does not invalidate that
whole matrix. Keep protocol, patched input hashes, binary hashes and proof
status separate. A zero unsupported-op census applies only to observed graphs:
the 189-configuration census observed 160 graphs and could not observe 29.
Do not infer absent Rem/LUT/Rxor/Popcount use in those unobserved designs.

### 14.7 Inherited signed Div and approved Rem policy (A12)

The IR has UDiv but no SDiv; Div currently maps to UDiv regardless of sign.
A fresh legacy signed division fixture evaluates `-32 / 3` to `74`, rather
than the signed quotient's 8-bit representation `246`. This is inherited,
not introduced by the refactor. The verified export also emits UDiv, but its
`_compiles` theorem fails because CompileOp does not support division; it is
not a successful verified-compiler equivalence proof. The current native
encoder selects signed division from operand signedness and handles mixed
widths; changing only the result-sign branch would be insufficient.

Record the discrepancy and bring any semantic correction to the user before
implementation. Require signed/unsigned/mixed-width value oracles, IR plumbing,
primitive/compiler coverage and differential tests for an approved repair.
The approved Rem policy remains **constant nonzero divisor only**, refusing
zero and dynamic divisors unless a future explicit nonzero theorem justifies
them. Do not implement signed Rem through UDiv or adopt solver-only `% 0`
behavior as upstream execution semantics.

### 14.8 Restored-flat acceptance and replay (A07)

Of the 341 restored current artifact hashes, 328 match historical successful
logs and 13 lack such evidence. Hash matches alone are not fresh acceptance
against the current matching toolchain/library. The 22 withdrawn nested proofs
remain historical. Four old RUNNING statuses below are stale records; no
associated proof jobs were active during the audit.

| Family / mode | Current artifacts without an exact-hash successful result | Disposition |
| --- | --- | --- |
| CORE-ET legacy-fast | `txfmactl_top` | Recorded flat-constructor failure |
| CVA6 legacy-fast | `csr_regfile`, `fpu_wrap`, `frontend`, `issue_stage` | Recorded flat-constructor failures |
| CORE-ET legacy-bridge | `txfma_wallace1`, `txfma_wallace2` | Unfinished historical attempts, not active runs |
| CVA6 legacy-bridge | `legacy_cva6_alu_export`, `legacy_cva6_pmp_gate` | Unfinished historical attempts, not active runs |
| CORE-ET legacy-bridge | `txfma_c0`, `vpu_ml`, `vpu_tensorfma` | Restored-flat/new-Or proof combination lacks completed result |
| DINO legacy-bridge | `PipelinedDualIssueCPU` | Restored-flat/new-Or proof combination lacks completed result |

Fresh declaration probes on Lean 4.31.0 and installed 4.34.1 accept 255 and
256 Nat fields and reject 257 with `constructor ... has too many fields`.
Declaration success does not establish safe execution at 256, including the
separate exactly-256 metadata bug. The limit applies to constructor fields,
not a general 256-flop promise: input/output, memories and synthesized state
can create other large records. Measure each actual schema and execution
boundary after the user's matching Lean/Mathlib upgrade. Preserve flat records;
do not reintroduce nesting or change the model to bypass the limit.

Replay the 186 CORE-ET/CVA6 configurations, all three DINO variants and recovered
branch-specific runtime cases. Each DINO needs verified compiler, legacy-fast,
**full** chunked-WF and **full** bridge checks with exact theorem audits and
`/usr/bin/time -v` wall/RSS. Keep upstream failures/refusals explicit and
separate from Lean proof results. Do not weaken native LEC, reset/memory guards,
proof completeness or refusal behavior to improve acceptance counts.

### 14.9 Audit validation and completion conditions

This audit rebuilt the optimized tools and passed the three Lean Bazel test
targets (cached results), five sweep-tooling checks and six Hotmux-oracle checks.
The inspected runners pass shell syntax checks. The small source-only and
two-clock exports typecheck; runtime panic, stack, stale-map and queue-audit
probes have explicit inputs/results/hashes in the evidence record. Six
constructor declaration probes cover two installed toolchains. No full DINO,
CORE-ET/CVA6, source-branch theorem suite or native regression replay was
performed by this audit. No implementation or toolchain pins were changed.

Acceptance remains open until each migration contribution has a destination or
explicit disposition, every semantic adaptation has the required equivalence
proof/user decision, and the current replay satisfies its complete gates.
Optional future directory grouping also requires Lean import and BUILD updates
and hash/proof checks; a compiling module move cannot always be content-free.
The no-graph-header emitter boundary follows sections 3 and L3-L8, not a
nonexistent P5 phase. Keep any shared legacy naming/value helpers in a neutral
layer if disentangling the current GraphCert-to-fast-model helper dependency;
this layering cleanup is lower priority than the acceptance defects above.


### 14.10 Audit of the measured D2 port plan

The `68c0e5e18` plan retains the semantic-port approach and clock-aware allEdges
compatibility target, with corrections recorded in
[MASTER_INTEGRATION.md R13](MASTER_INTEGRATION.md#r13-the-d2-port-measured-and-audited)
and [MASTER_D2_PORT_PLAN_AUDIT.json](tests/MASTER_D2_PORT_PLAN_AUDIT.json).
The three-module 177-edit subtotal expands to **240 edits across six shared
modules**, plus eight Direct modules: include ResidualIR, ResidualSemantics and
CompileDesign, and generalize the actual executor as well as its theorem to
arbitrary ClockEdges. Keep the existing generated step as an explicit allEdges
compatibility wrapper, not a general RTL schedule claim.

`interpretDesign_allEdges` requires valid flop/memory clock bounds;
`compilesOk` does not supply them. `fires` defaults a missing edge entry to false,
but cannot detect that a present entry names a domain absent from a certificate.
Validate clock ordinals and edge-vector length independently. Old defaults
preserve the old interpreter, not the missing provenance of old RTL exports.

The C++ slice includes owned scan **and CertificateIR/builder/memory lowering**
metadata, including synthetic read flops and synchronous ROMs. D2 refuses
unnormalized gated clocks; do not discard gate enables while accepting their
root. Preserve falling-edge/unresolved-clock refusals and current master's
clock-bus-bit guard. One MemoryDesc limits accepted committing ports to a common
supported domain; current master's single_edge lacks the cited D2 mixed-memory
clock guard, so enforce this policy in the Lean scanner without modifying
upstream/native code. Account for D2 sidecars and validation tools explicitly.

Update all tracked caller fixtures/generators and replay the full 186 CORE-ET/
CVA6 configurations, all three DINO variants and all required modes. The 77
READY triage count, historical 54/147 async mismatches and reported 943 generated
copies describe different scopes, not interchangeable acceptance counts.
Preserve historical proof artifacts and the user-owned flat-record upgrade.

Fresh audit validation built all 20 D2 dependency modules in isolation, ran
DirectTests and two boundary probes, and checked four theorem axiom reports
without `sorryAx`. These are D2 branch-library results; the clock-aware C++ port
and full current benchmark replay remain pending. No implementation or semantic
change is made by this audit.

### 14.11 D2 Step 1 implementation checkpoint

R13 Step 1 is complete on master over `5eaee438c`: six shared Lean modules and
eight Direct modules match D2 clean `c21200e0c` byte for byte. All tracked callers
and generators now use explicit `allEdges`. The verified emitter independently
checks compiler acceptance and flop/memory clock bounds, emits the clock-aware
step theorem, and proves compatibility with the preserved every-step interpreter
using those bounds. It does not infer clock well-formedness from compilation.

[MASTER_INTEGRATION.md R13.6](MASTER_INTEGRATION.md#r136-step-1-implementation-and-fresh-focused-validation)
and [MASTER_D2_STEP1_RESULTS.json](tests/MASTER_D2_STEP1_RESULTS.json) record the
fresh checks: three Bazel tests, exact library target builds, 114 tiny export
comparisons, 68 successful Lean checks, two expected compiler refusals, two
independent invalid-clock rejections and 27 legacy value comparisons. The tiny
bridge checks include full chunked-WF and complete comb/next/step obligations for
those fixtures. They do not establish full DINO scalability.

Step 2 clock/reset provenance and scanner guards remain pending; current emitted
certificate data still uses defaults and preserves the all-fire procedure.
The full 186 CORE-ET/CVA6 configurations, three DINO variants and recovered
branch runtime procedures remain acceptance gates. Keep the user-owned matching
Lean/Mathlib upgrade, flat schemas, native code and historical evidence intact.

## 15. Final stage: simulator generation across five approaches and twelve blocks/cores

User-requested table stage, appended at the end of the documented plan. The
latest execution priority is to run this coverage table before all further
optimization and performance-research experiments; prepare only the functional
approach/input prerequisites needed to execute its cells. Preserve the other
rebase and acceptance obligations without treating them as waived. The live [task and progress table](../../todo/livehd/simulator-generation-matrix.html)
tracks benchmark rows against approach columns; the [TSV matrix](tests/MASTER_SIM_GENERATION_12X5.tsv)
and [per-cell evidence record](tests/MASTER_SIM_GENERATION_12X5.json) retain the
same twelve rows and five approaches (60 cells).

Approaches: Futamura projection, verified compiler, translation validation,
legacy, and IR-as-semantics. The twelve rows are CORE-ET DCache; CVA6 HPDcache;
CORE-ET neighborhood I-cache; CVA6 MMU/TLB/PTW; CORE-ET ET-Link→AXI; CVA6 LSU;
CORE-ET VPU; CORE-ET IntPipe/scoreboards; CVA6 issue/scoreboard; CVA6 frontend;
CORE-ET TLB; and PicoRV32 whole core. PicoRV32 is at the user-provided
`/HOMEROOT/projects/picorv32` (observed clean pin
`ef203c2b0a3fb793280f5114941416c425c5b461`, source `picorv32.v`).

First freeze each row's exact top(s), configuration and source inputs and each
approach's generator/runtime. A grouped row requires its complete named unit
or all required components, not a reduced representative. Then generate with
every approach, keeping unsupported, missing, failed and stopped cases in the
table. Treat standalone generation latency as the performance priority;
measure loading/admission, generation, checking and any serialization/emission
separately from reference validation. Retain approach-native artifact identities,
reproducible commands, code/input/toolchain hashes, resource measurements,
logs and the actual proof/admission status. Do not substitute another approach's
artifact or mark sample agreement as kernel certification.

When **all 60 cells are YES**, audit the evidence, complete the required
approach-specific checks, make the final checkpoint and **commit locally**.
The user will push. Until that condition is met, report exact progress and
blockers; do not claim the final checkpoint complete. This addition does not
waive or reorder the existing branch procedures, fresh DCERT2 work, integrated
447+6 obligations, raw generated-source comparisons or deferred D3 scoreboard
work. All 60 cells are initially PENDING; this edit adds the plan and tracking,
not benchmark results. Record each block/approach generation time, memory, CPU
and detailed phase/resource metrics, plus shared hardware and software
dependency provenance, so the table supplies later optimization targets. Schedule
profiling, optimization and performance-research experiments after the table
runs. Generation success, validation and proof status remain separate.

### 15.1 What the coverage statuses mean

| Status | Meaning |
| --- | --- |
| PENDING | No current attempt or established prerequisite disposition yet. |
| RUNNING | An actual generation/check job is live, with its process identity recorded. |
| YES | Approach-native generation and its declared generation/readiness checks succeeded, within their recorded scope. |
| NO | A recorded attempt failed or was refused. Source emission may have succeeded before a later required check failed. |
| BLOCKED | A known required input, capability or preparation step prevents generation from completing. The emitter may never have been reached, or an earlier support check may have established the blocker. |

`BLOCKED` does not mean simply “not run yet,” and it does not establish that the
block can never be supported. Record which phase actually ran and which was not
reached. For example, a failed shared RTL-to-LGraph frontend leaves other
approaches without their required generated input; those emitters have not been
tested independently. PicoRV32's projection route, in contrast, was loaded and
its support checks rejected memory-related features. A timeout or fuel cap is an
undecided result at that budget, not evidence of a semantic capability rejection.
Preserve all attempts and successful earlier phases.

### 15.2 Diagnose rejected or blocked cells against the original branches

After the initial sweep has recorded a real attempt or a precise prerequisite
disposition for every one of the 60 cells and its live coverage jobs are terminal,
debug the rejection and blocking reasons. This diagnostic stage does **not** wait
for all 60 cells to become YES; the separate final successful-coverage checkpoint
condition above remains unchanged. Finish the initial approach-major sweep in
the user's order: legacy, verified compiler, IR-as-semantics, translation
validation, Futamura projection. Do not replay completed unchanged phases.

Start with the **Futamura projection branch**, whose existing coverage provides
the first baseline. Use the original checkout
`/WORKROOT/projects/livehd-futamura/livehd`, branch `projection`, pinned
at `9bb41a09d6b57111ed2b3fd10102ec4ff174b6c6`. This is a separate repository;
the similarly named ref in the master repository is not this baseline. Freeze
the compared master revision and both sides' actual build/tool/dependency
identities before the comparison.

For each affected requested block, **run the old branch's own implementation to
confirm whether it supports that exact block**. Historical moderate coverage,
an existing artifact, a smaller submodule or a source inspection does not prove
support for the requested whole unit. Use the same external RTL revision, full
component list, top, parameters, filelist, defines and includes as the table.
Cover all twelve requested units on the original FP implementation, prioritizing
rejected/blocked cells and retaining successful units as behavioral comparison
controls. Reuse already-completed matching old-branch evidence where its exact
input, configuration, implementation and required checks are established.
Retain each branch's genuine frontend, emitter, loader and simulator procedures
and declare any required differences in their dependencies or options. Bind
fuel, stack/resource limits, clocks, resets, asynchronous metadata and memory
contents; a resource stop remains undecided. Keep build preparation costs separate
from simulator-generation and execution metrics.

Compare both **functionality and behavior**: frontend acceptance and normalized
graph contents, certificate format and admitted design, every support predicate,
residual generation/checking, and simulator state/output transitions under the
same inputs and edge/reset schedule. Identify the first phase where the results
differ. A common graph or certificate handoff may help isolate that phase only
when format compatibility and complete metadata are verified; retain the
same-external-input pipeline comparison as well. Do not erase memory, weaken
support/proof predicates, discard metadata, replace the approach's backend, or
revert the established SHL/SRA/Sext correctness repairs to manufacture agreement.

Record old-branch support as observed **supported / rejected / blocked /
undecided**, separately from the master cell status. Extend the evidence record
with both revisions, input/build identities, commands, artifacts, timings and
resource metrics, old/master outcomes, observed transition differences, first
divergent phase, root cause and a reproducer. Classify findings as an inherited
limitation, port regression/bug, intentional semantic or correctness change,
frontend/configuration/transport difference, or unresolved result. Old-pass /
master-fail is a regression candidate until matching inputs and the cause are
established; failure on both sides alone does not prove the same cause. Diagnose
and make justified fixes with retained before/after evidence, preserving the
original independent proof and admission obligations.

**Finish this Futamura comparison and its findings before investigating the
other branches, and resolve the findings until every requested FP unit is
supported.** Then do the same for IR-as-semantics, followed by translation
validation, using their original implementations as baselines. Keep capability
debugging ahead of optional optimization/profiling research. Intermediate local
records remain provisional; the user pushes only after the separately required
final checkpoint is achieved.

### 15.3 Restore full support, including the shared frontend

The user's completion target is support for **all twelve requested units** in
Futamura projection, then IR-as-semantics, then translation validation. A recorded
NO or BLOCKED cell is a diagnostic finding, not completion of this support work.
Complete each approach's support before moving to the next:

| Support stage | Completion evidence |
| --- | --- |
| Futamura projection | All 12 whole units reach approach-native generation and declared readiness checks; original-branch support and any old/master differences are recorded; demonstrated port/frontend bugs are fixed and necessary missing capabilities implemented. |
| IR-as-semantics | All 12 units have usable generated simulators and their required checks/execution, with old-branch comparison and justified fixes. |
| Translation validation | All 12 units pass their declared generation/admission gates with valid private source/build/dependency closure; no missing input, capability or undecided cap is counted as support. |

Keep all five approaches and all 60 cells in the original table. The existing
all-60-YES final checkpoint condition and other migration obligations remain;
the three-stage support priority does not remove legacy or verified-compiler
cells from the denominator.

**What input FP requires.** Its generation input is the hardware design
certificate (`DesignCert`, transported as `.dcert`), produced from the full RTL
with its top, wrapper/types/parameters, ordered filelist, defines and includes:
RTL elaboration → LGraph → required normalization/`single_edge` → certificate
emission → FP specialization → residual simulator. Per-cycle input signals,
clocks, resets and initial state are inputs to the resulting simulator, and are
recorded separately. An upstream frontend refusal prevents the certificate from
being produced; it is not evidence the FP specializer itself rejects that unit.
The old implementation must use its own genuine certificate/loader route, with
DCERT1/DCERT2 and metadata differences explicit.

Compare the old branch's actual frontend and wrapper recipe with master for
every upstream failure: configuration/type bindings, wrapper dependencies and
file ordering, source adaptations, reader/plugin/dependency versions, synthesis
options, lowering, graph construction, normalization and certificate emission.
Run the old end-to-end route to establish support; examine matching phase
artifacts and localize the failure. Source differences alone do not establish
causation or historical whole-unit support.

**Rebase the needed frontend support too.** The work is not limited to the Lean
projection modules. Where a correct capability or adapter in the old route was
lost, port it into master's current frontend/graph APIs and verify it on the exact
affected units. If a master frontend or wrapper bug causes the refusal, fix it
with a reproducer and appropriate semantic/regression checks. If both branches
lack a required feature, implement that support rather than leave the block
permanently rejected. Preserve original failures and rerun affected phases only
after a relevant input/configuration/implementation change; do not rerun
unchanged completed cells or modify unrelated benchmark RTL to obtain a pass.

Whole-Pico memory support is a concrete required FP extension: implement the
memory/source/operator semantics, runtime relation and necessary adequacy/checker
proof bridge before admitting that design. Do not simply remove `memFree`, erase
memory, ignore clocks/resets or weaken any other support predicate. Similarly,
resolve the actual causes of frontend concat-width, package/configuration,
combinational-loop and `single_edge` refusals; a rejection must not be disabled
without establishing correct behavior. Keep established correctness repairs and
the existing proof/admission obligations intact. Functional support restoration
and demonstrated bug fixes precede performance optimization.

### 15.4 Implementation first: master FP must support all twelve units

The latest user priority is to **finish the rebased FP implementation on master
first**, including its required frontend/certificate support and missing
projection capabilities. The existing implementation already generated and
executed the frontend and issue/scoreboard simulators; these two results do not
establish complete twelve-unit support.

Distinguish the old branch's RTL frontend binary (`lhd`) from its projection
specializer. The pinned old FP specializer was already built in an isolated root
and run on pre-existing certificates. The original repository's native frontend
has now been built, and targeted whole DCache and I-cache comparisons have run;
section 15.5 records their distinct outcomes. Neither an absent convenience
wrapper/filelist script nor an absent `bazel-bin` proves the generic frontend
cannot consume explicitly supplied benchmark inputs.

Do not make a blanket old-branch build or twelve-unit baseline sweep a prerequisite
to starting implementation. Use old-branch source and targeted executions when
they help establish correct wrapper/configuration/frontend behavior or diagnose a
specific regression. Retain the requested old-implementation confirmation and
matched comparisons as validation work; they do not replace or delay concrete
master support work. Restore a missing helper as an input adapter when appropriate,
without modifying the old tracked implementation or claiming historical support.

Advance through actual implementation changes, meaningful semantic/regression
checks and affected whole-unit runs. Inherited limitations require correct feature
extensions as well as port fixes. Keep the complete table and evidence current,
preserve earlier successful phases, and do not stop at a manifest of refusals or a
proof-obligation list while implementation work can proceed. Complete FP support
for all twelve requested units before advancing to IR-as-semantics and then
translation validation. Periodic supervision uses the user's 20-minute interval.


### 15.5 Detailed FP branch-to-master implementation and acceptance plan

User-requested execution plan, 2026-10-12. Claude Code owns implementation on the
current master migration branch. The supervisor independently audits source,
proofs, artifacts and whole-unit results. Work continues without another approval
request for the already-authorized implementation/build/coverage tasks. Local
commits are provisional; do not push or create another git branch.

#### Objective and completion boundary

Finish the actual Futamura implementation on master, preserve the original
branch's contributions and established behavior, fix port regressions, and add
missing capabilities until **all twelve requested whole units have usable FP
simulators**. Then finish IR-as-semantics, followed by translation validation.
The five-approach, 60-cell table and its separate all-60-YES final checkpoint stay
in force. FP completion alone does not discharge the independent D2/D3/D4,
source-preservation, proof-admission or benchmark replay obligations.

Use the current shared architecture: graph access/owned scan -> CertificateIR ->
DesignCert/DCERT transport -> the projection's object-language interpreter ->
verified specialization -> checked residual -> the projection's own step runner.
Port needed adapters into current APIs; do not restore duplicate graph scanners,
private certificate builders or obsolete shell interfaces. A reference
interpreter is a comparison oracle, never the FP implementation.

#### Audited starting point and fixed baselines

| Item | Authoritative starting evidence / current limitation |
| --- | --- |
| Original FP source | Separate checkout `livehd-futamura/livehd`, branch `projection`, revision `9bb41a09d6b57111ed2b3fd10102ec4ff174b6c6`; keep tracked source unchanged for comparisons. The master repository's similarly named ref is not this baseline. |
| Master FP code | `23cc28da28bf874d8cc7287eda54b4c35724f589`: specializer already exists; new memory primitives, representation helpers and real `srcValM`/`applyOpM` dispatch exist. The memory-aware whole-cycle entry and generic adequacy remain missing. |
| Existing whole FP results | CVA6 frontend and issue/scoreboard have recorded generation/checking/interpreted residual readiness. These are two whole-unit observations, not twelve-unit support or RTL equivalence. |
| Original specializer | Pinned `gen_probe` binary already built/run on frozen certificates; SHA256 `eeada8cf4405dc36ddce6bcb1c01384860d8a174c6e37ae846b1601f6e78044e`. Matched small-certificate evidence is not old whole-unit frontend support. |
| Original native frontend | Fresh old-source build succeeded; binary SHA256 `1c089c4edf0f817bcd5cd15833feba0967e0ab45a505778e6764c8da9b87e030`. It uses a fresh dependency closure with explicitly recorded protobuf yanked-version admission, not a reproduced historical binary closure. |
| DCache comparison | Same frozen whole RTL/filelist/options: old frontend passes; retained master binary reproduces Concat width refusal in its second bitwidth pass. Pass sequences and reader versions differ; root cause is not yet localized. Old `single_edge` then actually refuses a memory commit under a phase divider. Neither result is old FP admission. |
| I-cache comparison | Old and master both reject the same first ambiguous `PaSize` imports on the frozen 31-source input. This is observed shared raw-source/configuration refusal, not established master-only regression. |
| Current table | 13 YES / 11 NO / 36 BLOCKED. FP has 3 YES / 9 BLOCKED: B12 PicoRV32 moved BLOCKED -> YES on the proved MEMORY route (scalar support still refuses it, correctly, on memFree/sources/ops -- that refusal is retained, not superseded), under the same INTERPRETED generation/readiness standard as B09/B10. The nine remaining FP blockers are upstream input/frontend refusals (eight never emit a certificate at all; B11 emits then fails `single_edge` on memory slot enables), and they remain in scope for the twelve-unit objective. This is NOT full-project acceptance: every cell has a disposition, and not every emitter was reached. |

The old native build and phase attempts are under
`livehd-futamura/baseline_frontend_build_9bb41a09d_20261012/`.
The supervisor's detailed audits are under
`automate_workspace/master_rebase/audits/`; the ledger retains their exact paths.
Treat section 14's original inventory/status tables as dated audit baselines,
not statements that newly ported files are still absent.

#### Phase 0 — preservation and comparison setup, alongside implementation

Do not defer the next coding phase for a blanket old build or baseline sweep.
Known baselines are sufficient to start Phase 1 immediately. Complete this
read-only inventory and preparation alongside functional builds/proof work:

1. Inventory every original FP contribution, including the recorded 67
   Projection paths/38 Lean modules, scripts, specifications, tests, artifacts
   and research records. Assign a master destination and disposition: unchanged,
   API/schema adaptation, correctness repair, capability extension or explicitly
   retained historical/deferred material. Verify actual counts from pinned git
   trees rather than assuming the historical counts are current.
2. Record original/master paths and raw hashes. For modified semantics/proofs,
   state the changed premise or behavior and its preservation/compatibility
   obligation. Preserve raw source pairs; do not hide differences by comment,
   whitespace, rename or proof-text normalization.
3. Preserve object-language semantics, specializer soundness, all retained
   SHL/SRA/Sext fixes and the original memory-free route. Preserve experimental
   source as evidence; do not promote Proto/ProtoShift into the production proof
   chain without its independent obligations.
4. Freeze each whole unit's RTL revision, complete component closure, top,
   wrapper/type parameters, defines/includes, ordered sources, clock/reset
   schedule and initial state. The certificate is the FP generation input;
   runtime signal vectors are a separate simulator input.
5. Bind actual tools/dependencies and private build roots. The historical master
   frontend binary's build revision is not established merely by its location:
   rebuild the affected current source with recorded identities before claiming
   a native frontend code fix. Keep preparation costs separate from generation.
6. Run targeted old phases when they resolve an implementation question; retain
   the requested matched old comparisons for all twelve whole units as final
   validation. Use genuine old loader/exporter dependencies explicitly. Missing
   convenience scripts are not evidence a generic route is impossible.

Deliverable: source/destination/preservation inventory and per-unit comparison
bindings. These records do not replace coding or establish support by themselves.

#### Phase 1 — implement the actual memory-aware whole-cycle interpreter NOW

Continue in `formal/lean/LeanSemanticPrimitives/Projection/HardwareMem.lean`.
The separate module is a staging boundary in the same backend, not a substitute
implementation. Keep original `HardwareInterpreter.hwS` and its proven route
unchanged while the memory-aware route is being proved.

1. Add a memory-bearing `main` entry taking static DesignCert plus dynamic
   edges, input and encoded StateM. Set the memory program's actual entry to it.
   Thread both flop and memory lists; do not leave the reachable entry as the old
   memory-free main while naming the program memory-aware.
2. Implement `mkSourcesM` calling the existing `srcValM` with the memory list,
   and `evalNodeM`/`evalNodesM` calling `applyOpM`. Preserve newest-first slot
   indexing, exact dependency order, node width and opcode payload.
3. Build outputs, flop next-state and memory next-state from the same evaluated
   slot environment. Preserve existing scalar/flop reset, asynchronous reset,
   enable and per-domain clock behavior. Address scalar/memory value kinds
   explicitly; old scalar helpers do not automatically prove correctness of
   `CertVal.asBV` coercion on a memory-valued slot.
4. Commit each memory's real `nextImg` slot only when its declared clock fires;
   otherwise retain its exact prior first-order image. Preserve initial words,
   defaults, word widths, lengths and descriptor metadata. Use valid clock and
   slot bounds or reproduce the reference defaults exactly, with premises stated.
5. Add a first-order memory-bearing result type/codec, round trip and denotation
   to the actual Compiler.RuntimeResult. A relation only to StateM is insufficient.
   Re-resolve function names/indices after changes; never append unrelocated
   numeric helper calls.
6. Execute the whole new entry on an encoded DesignCert/state, decode its result,
   and execute a second step using that returned state. Compare outputs and
   memory observations against the real reference on identical inputs/edges.

Required discriminating checks: initialized ROM and its out-of-contents/default
behavior; full write/read; Pico's actual `Op_MemWriteBE 1` bit mask; separate
8-bit byte-lane mask; disabled write; unrelated address preservation; active and
inactive edges; distinct memory clock domains; and a second-step read observing
committed state. Retain a negative control that fails if a quiet memory commits.
Use the audited two-clock scenario in the supervisor's
`memory_clock_reference_scenario_20261012_1555` directory as a ground-truth input,
not as proof that FP executed it.

Deliverable: compiling interpreter/caller/result-codec implementation and actual
whole-entry execution evidence. This is not yet Pico admission or generic adequacy.
Continue to Phase 2 after this local checkpoint without waiting for another plan.

#### Phase 2 — prove the memory representation and hardware adequacy bridge

1. State a nonvacuous representation relation between encoded first-order
   images/states/results and real RuntimeState/RuntimeResult. Prove round trips,
   functionality/canonicality and memory-free compatibility where required.
   Do not claim a finite encoding round trip for arbitrary `Int -> BV` functions.
2. Define reachable/valid memory shapes explicitly: mutable image lengths,
   initialized contents/defaults, word/address widths and write ranges. A
   `2^aw` mutable table covers correctly shaped aw-bit addresses; a ROM uses
   its actual contents.size. Prove initialization and preservation of the
   representation conditions through admitted steps.
3. Prove memory source evaluation, ROM construction and slot-environment
   correspondence, including absent-memory/default cases within the declared
   theorem scope. Prove the memory operator cases by composing actual primitive
   evaluation, encoded update and denotation/reference equality.
4. Discharge enabled-write InRange/width/shape/byte-width conditions rather than
   omitting them. The finite table's out-of-range no-op is not the reference's
   enabled-write semantics; malformed/out-of-range cases must remain refused
   wherever the soundness premises are unavailable. Disabled writes remain no-ops.
5. Prove dense-node traversal, outputs, flop behavior and memory next-state for
   both active and quiet branches, then whole-cycle adequacy through the same
   Surface resolution/Eval proof framework. `stepMems_quiet` alone is not active
   commit preservation, and executed guards are not a general theorem.
6. Supply a memory-capable supportedness/checking policy whose true conditions
   justify the new theorem. Preserve the original RuntimeWF and old generic
   theorem scopes; add explicit representability/shape premises to the new route
   where needed instead of silently narrowing the original theorem.
7. Audit theorem axioms and trust scope. No sorry/admit/sorryAx, invented axioms,
   unchecked reduction escape or disabled checks. Report ordinary Lean axioms
   accurately; do not describe computational guards as kernel adequacy proofs.

Deliverable: independently rebuilt source/operator/state/result/commit and
whole-cycle correspondence theorems, a justified support policy and meaningful
counterexamples to its refusal boundaries. Public support must stay closed until
these obligations are discharged; simply removing `memFree` is not a solution.

#### Phase 3 — connect specialization, checking and the actual FP runner

1. Run BTA on the new whole-cycle program with DesignCert static and exactly
   edges/input/state dynamic. Verify annotation well-formedness and erasure back
   to the resolved interpreter; preserve the original specialization semantics.
2. Generate with the proved production specializer. Preserve primitive codes and
   mirror every added primitive in Encoding, evalPrim, the object mix primTable
   and exhaustive proof cases. The read/write additions already have component
   evidence; validate their use through the whole interpreter.
3. Obtain a call-free residual with the intended three dynamic arguments and run
   the existing checker at its returned height bound. If structural calls remain,
   repair inlining/static information or make a separately proved checker
   extension with real fuel/shape premises. Never apply the call-free height
   theorem to a recursive residual or accept it by bypassing the checker.
4. Wire the actual DCERT2 support, generation and readiness drivers to the proved
   memory-capable route and result/state codec. Keep DCERT1 compatibility and
   reset/clock/async format distinctions explicit; do not discard DCERT2 metadata
   to make an old loader accept it. Keep old driver/artifact identities historical.
5. Construct a nonvacuous initial state from the real design, preserving required
   memory/ROM initialization. The readiness driver must execute the projection's
   checked residual, carrying returned memory state to subsequent cycles.
6. Use decoded encoded-memory contents and state/output observations to show
   real transitions. Count flop, memory and output changes separately. A memory
   header, a stable flop vector or generation-only success is not runtime motion.
   Do not infer equality of arbitrary memory functions from a few sampled reads.

Deliverable: runnable serial decode -> support -> generate/check -> readiness
procedure with true producer exits and immutable attempts. Run whole PicoRV32
at the frozen source/configuration, retaining exact phase metrics and genuine
memory transitions. Update its FP cell only when all declared required phases
actually pass. Compare with the old branch's genuine matched capabilities and
state the difference between inherited missing memory support and a port bug.

#### Phase 4 — restore frontend, wrapper and normalization support for all units

Do actual targeted fixes and affected whole-unit runs; do not stop at the
manifest. Keep a per-unit first-failure record and follow the first real
source/semantic divergence. Old and master tools may differ: compare matching
input artifacts before attributing a regression to one code change.

| Requested unit | Current FP support work / first investigation |
| --- | --- |
| B01 CORE-ET DCache | Trace the real Concat/mux width change before the second master bitwidth refusal; retain old frontend pass and later old memory/phase-divider refusal. A simple signed-mux diagnostic passed both binaries and did not reproduce this failure. Fix the producer/width semantics with a reproducer, not the Concat check. |
| B02 CVA6 HPDcache | Check exact whole wrapper/config/type bindings, old reader/lowering and actual combinational-loop provenance; recover or implement the required frontend capability. |
| B03 CORE-ET neighborhood I-cache | Resolve the shared ambiguous package imports using a justified configuration/input adapter. Record qualification/source differences; preserve whole components and parameters. |
| B04 CVA6 MMU/TLB/PTW | Diagnose its own loop/configuration/lowering failure using the full requested group, not a smaller MMU or isolated leaf. |
| B05 CORE-ET ET-Link -> AXI | Restore correct declaration/file ordering and required adapter behavior for the actual group; keep failed preparation attempts distinct from configured generation. |
| B06 CVA6 LSU | Diagnose its whole-unit combinational-loop path and memory/clock/configuration requirements; do not assume it shares another unit's root cause. |
| B07 CORE-ET VPU | Diagnose the actual whole VPU loop/lowering stop and then implement any newly exposed FP operators/memory shapes with their proof bridge. |
| B08 CORE-ET IntPipe/scoreboards | Preserve the complete requested group and diagnose its own loop/width/configuration failure. |
| B09 CVA6 issue/scoreboard | Preserve the existing whole-unit FP evidence as a control; refresh only when an affected dependency/implementation changes. Preserve reset/async metadata. |
| B10 CVA6 frontend | Preserve existing FP evidence and complete clock/reset/async behavior as a control; no blanket unchanged rerun. |
| B11 CORE-ET TLB | Recover proper memory commits under normalization/phase scheduling, with slot enables/clock semantics proved; retain original refusals. |
| B12 PicoRV32 whole core | Complete Phases 1-3 and real memory-enabled generation/readiness, not only the existing emitter/loader success. |

For every required newly exposed FP source/operator/clock shape, implement it in
the same backend and extend the corresponding representation/adequacy/checker
proofs before admission. Keep each failed phase and successful earlier phase.
Use targeted original-branch execution/configuration comparisons as validation,
not a blanket prerequisite to implementation. Correct shared frontend APIs on
master where a lost capability is established; if both branches lack support,
implement the correct capability rather than declare permanent completion at NO.

After each relevant code/configuration change, run the exact affected whole
unit. First-failure fixes may expose another blocker: record it and keep coding
while a functional next step is available. Do not map memories to untracked
registers, remove components, erase async/clock/reset information, disable width,
loop or single_edge checks, weaken proofs or change benchmark defaults to obtain
an apparent pass. Any justified wrapper/source adaptation needs an explicit
before/after binding and semantic explanation.

Deliverable: every requested FP unit reaches the genuine frontend/transport,
strict support, specialization/checking and residual readiness phases, with old
matching support/outcome and behavioral differences recorded. No smaller subunit
or historical third-repository census counts as this acceptance.

#### Phase 5 — fresh preservation, regression and acceptance closure

1. Build the complete relevant FP theorem/driver dependency closure in an
   independently owned source/build/dependency root with exact explicit targets.
   Verify exact-target no-build readiness after preparation; default-target
   success or equal artifact hashes alone is insufficient. Never write a build
   context with live importers or borrow mutable/hardlinked artifacts.
2. Run changed semantic unit/boundary/counterexample checks, inherited FP tests
   and required fresh16/full long validation. Reconcile inventories/two-build,
   remaining D3/D4, freshDCERT2, integrated 447+6 obligations and all thirty exact
   paired raw-source comparisons under their original scopes. Retain genuine
   failures and source/proof adaptations; do not use a narrow passing test for a
   broader gate. Sections 14-15 and the ledger define the still-independent gates.
3. Validate all twelve FP whole units using fixed inputs and meaningful edge,
   reset/async and memory schedules. Obtain matched old-implementation evidence
   per unit; classify supported/rejected/blocked/undecided by actual phase.
   Distinguish unchanged legacy behavior, an intentional correctness repair,
   inherited missing support, configuration/transport difference and a localized
   port regression. Explain any intentional difference with its proof/evidence.
4. Synchronize the exact 60-cell JSON, TSV, HTML and ledger from current phase
   evidence. Record scope: interpreted residual execution, native readiness,
   equivalence, kernel theorem and trust boundary are different claims. Record
   re-specialization as a new attempt when no residual was serialized.
5. FP support is complete only with **12/12 FP YES**, required preserved behavior
   and the declared FP proof/admission/comparison gates discharged. Then proceed
   to IR-as-semantics and TV in that order. Retain Legacy/Verified Compiler cells
   and the final all-60-YES/user-push condition; do not waive the wider migration.

#### Execution protocol and next concrete action for Claude Code

Start Phase 1 immediately from the existing `HardwareMem` dispatch. Implement
memory-bearing main, its real source/node callers, result codec and per-domain
commit, then run the audited two-clock whole-entry scenario. This is the next
bounded code/result checkpoint. Continue directly through the proof and driver
phases while runnable; a compiling checkpoint is not an instruction to stop.
If a proof attempt fails, retain its real error, isolate a smaller necessary
lemma and keep working rather than treating proof engineering as unavailable
capability. Use explicit pending obligations without representing them as passes.

For each checkpoint record source/config/tool/dependency identities; full
argv/cwd and selected environment; true producer exit; wall/user/system CPU;
raw maximum RSS in KiB plus correctly derived GiB; source/residual sizes and
counts; checked bound; clock/reset/async/memory shape; readiness observations;
and first failure or admission scope. Record NOT_REACHED for phases that did not
execute. Attempts and reuse decisions must be separate and immutable, with
content-based identity and correct propagation of pipeline producer failures.

Small regression fixtures belong to the repository and must respect AGENTS.md
runtime/dependency rules. Whole benchmark coverage belongs to separate bound
benchmark inputs/runners. Use project-local runtime directories. Supervision
continues at the user's 15-minute interval and sends another instruction only
when the current one has finished or needs verified corrective guidance. No
performance/profiling/optional native-build research before functional coverage.


### 15.6 Authorized final upstream rebase and push

Latest user instruction, 2026-10-12: finish the ongoing work, including FP
support and the final acceptance gates, then rebase and push the commits to
master. This explicitly authorizes the final rebase and push after their
prerequisites pass. It supersedes the earlier **user will push / no push**
wording only for that final action; there is no authorization to push an
incomplete intermediate checkpoint. Do not ask for renewed permission for the
already-authorized final action.

1. Complete the implementation, all required whole-unit support and all original
   final acceptance gates. Keep the five-approach/all-60-YES condition and
   separate preservation/admission/proof/replay obligations intact. A Phase 1
   entry test, 12/12 FP alone, a cached result or a provisional commit is not
   final acceptance.
2. Before integration, retain recoverable identities for the accepted local
   commit stack and exact artifacts/evidence. Verify a clean worktree and no
   live consumers of build contexts that integration or rebuilding would alter.
   Do not rewrite unrelated user work or discard conflicting implementation.
3. Read/fetch the latest remote master and rebase the local unpublished stack
   onto that actual revision. At the 2026-10-12 audit, local master had 177
   commits absent from live remote history, and remote master
   `5385554b8c64ed48e30fbd7ba3512e4c76434d53` had 56 commits absent locally;
   their merge base was `94c5967c664e5089c204a857a8db06a8f2f7c43e`.
   These are dated observations, not a permanent integration pin. The tracking
   ref was stale, so never infer current divergence only from origin/master.
4. Resolve rebase conflicts against the actual source/preservation/semantic
   requirements. Preserve upstream fixes, benchmark contracts, all FP and
   D2/D3/D4 functionality, clock/reset/memory metadata and required proof/trust
   boundaries. Record intentional adaptations and their justification.
5. Rebuild affected source/dependency closures and rerun all checks and whole
   benchmark phases invalidated by the upstream/rebase changes. Reconcile
   source inventories, exact raw-source comparisons and table evidence to the
   final integrated revision. Retain pre-rebase observations as historical;
   do not label them fresh final evidence. Final acceptance must still hold
   after integration, not merely before it.
6. Audit every required final gate against the actual integrated state, refresh
   the remote-tip check and perform a normal fast-forward push to remote
   master when all pass. If remote advances again, integrate it and repeat
   affected validation. Do not force-push, bypass checks, drop commits or push
   through an unresolved blocker. The user's push authorization persists.
7. Report the final local/remote revision, validation scope, reconciliation and
   actual push result. Mark the supervision goal complete only when the full
   original objective and final post-rebase acceptance/push requirements are
   achieved and verified.

Continue implementing section 15.5 now; this final integration stage does not
replace or delay the currently runnable FP memory proof/support work.


### 15.7 Low-memory Ubuntu experiment execution, after functional acceptance

User-requested extension, 2026-10-12: make the complete experiment suite runnable
on Ubuntu machines with less than 16 GB or less than 24 GB RAM, including machines
with stronger CPUs/more cores. Implement this phase after the preceding functional
support and original acceptance work; it must not interrupt the current FP proof
and support implementation. An intermediate user-requested rebase/push does not
make those unfinished functional gates complete.

#### What can trade CPU time for memory

Extra cores do not replace the live working set of an unchanged algorithm.
Concurrent compiler/experiment processes usually add their memory requirements.
Use additional compute for recomputation instead of retaining large caches,
compression, and independently checkable bounded pieces. Parallelize only pieces
whose combined measured memory fits. A faster CPU may offset added work; neither
speedup nor successful execution below a particular RAM limit is guaranteed.
A single memory-heavy specialization or theorem requires its own memory reduction;
setting a scheduler to one job cannot shrink that process's working set.

#### One configurable launch interface

Implement the resource runner as a single entry point that orchestrates the
existing `lhd` CLI and proved backend drivers. It must not replace their semantics
or become a second graph/certificate implementation. The following is the planned
interface for this final phase, **not a command already implemented today**:

```bash
python3 pass/lean/scripts/run_experiments.py \
  --suite acceptance \
  --resource-profile lowmem12 \
  --memory-high 7GiB --memory-max 8GiB \
  --threads 2 --build-jobs 2 --workers 1 \
  --swap-max 0 \
  --memory-saving auto \
  --resume
```

Provide shipped portable profiles for `lowmem8`, `lowmem12`, `lowmem16` and
`lowmem24`, plus a user-supplied configuration file. Resolve explicit command-line
overrides over the configuration file, then profile defaults. Print and retain
that resolved configuration, its hash and the actual applied limits for every
attempt. Keep host-specific identities/paths in local run records; do not bake
personal machine profiles into shipped configuration.

The options have distinct, enforced meanings:

- `memory-high` / `memory-max`: aggregate experiment process-tree reclaim and hard
  budgets, with all children and owned build daemons accounted for.
- `threads`: per-process thread allowance; `build-jobs`: concurrent compiler/build
  actions using the selected tool's verified interface; `workers`: concurrent
  experiment tasks. Do not treat these three as interchangeable or forward an
  unsupported jobs flag to Lake. Resource profiles include the Bazel heap/cache
  settings and a conservative scheduler allowance within the aggregate budget.
- `swap-max`: allowed swap usage, default zero. An optional compressed-swap mode
  can select a verified, already configured target-machine zswap or zram setup;
  it cannot create physical RAM or silently reconfigure the host. Report when
  requested compression or cgroup controls are unavailable.
- `memory-saving`: `off`, `auto`, or an explicit set of implemented backend
  features, such as bounded caches, recomputation, validated streaming and
  residual reuse. `auto` selects only the features whose proof/compatibility
  obligations have passed for that backend and input. Advertise the supported
  feature table and reject explicit unsupported selections. The launcher may
  enforce resource limits before algorithmic features exist, but must not claim
  that resource limits implement those features or guarantee successful fit.
- `resume`: reuse only completed, identity-matched artifacts/checkpoints whose
  dependencies still validate. Preserve attempt histories and report reused
  phases separately from newly executed ones.

Add `--print-config` and `--dry-run` for reviewable configuration and execution
plans. Preserve real exit codes, original required experiment coverage and strict
support/checking under every profile. Validation must cover option precedence,
invalid budgets, missing enforcement, unsupported features, process-tree and
concurrency limits, safe resume invalidation and identical functional results.
Use small local fixtures for launcher checks; run complete benchmark acceptance
under the selected budgets in the separate experiment phase below.

#### Implementation sequence and required deliverables

1. **Bind the target and all experiments.** Record actual Ubuntu/kernel/systemd,
   cgroup version/controllers, CPU/core counts, physical `MemTotal`, local SSD
   capacity, swap configuration and pinned tools/dependencies. Distinguish decimal
   GB from GiB. Inventory every required build, proof, generation/checking,
   readiness, replay and comparison from sections 15.5–15.6, including all five
   approaches and the independent D2/D3/D4 gates. Preserve complete whole-unit
   inputs, metadata, theorem meanings and acceptance scope. The deliverable is
   the complete suite on the smaller machine, not a reduced benchmark subset.

2. **Add project-local resource profiles and a bounded runner.** Start with these
   planning budgets, then reduce them if actual available RAM needs more reserve:

   | Nominal machine RAM | Experiment `MemoryHigh` | Experiment `MemoryMax` | Heavy jobs initially |
   | --- | --- | --- | --- |
   | 8 GB | 4 GiB | 5 GiB | 1 |
   | 12 GB (below 16 GB target) | 7 GiB | 8 GiB | 1 |
   | 16 GB | 10 GiB | 11 GiB | 1 |
   | 24 GB | 16 GiB | 18 GiB | 1 |

   These are proposed aggregate process-tree budgets, not observed passes.
   Select from actual memory, rather than rounding a sub-24-GB host up to 24 GB.
   Reserve RAM for Ubuntu, the editor and background services. Use cgroup v2/
   systemd to contain the whole runner and its children; keep already-running
   build daemons outside the group from escaping accounting by using a dedicated
   owned daemon/output root inside it. `MemoryHigh` causes reclaim/throttling;
   `MemoryMax` is a hard boundary and can result in OOM. Neither is a memory
   optimization. Verify controller delegation before running; never silently
   accept a resource profile whose limits were not applied. See the
   [systemd resource-control definitions](https://github.com/systemd/systemd/blob/main/man/systemd.resource-control.xml)
   and [kernel cgroup v2 memory controls](https://docs.kernel.org/admin-guide/cgroup-v2.html).

3. **Limit concurrency independently at each layer.** Run whole benchmarks and
   large proof/generation phases serially first. Build only exact needed targets
   and prepare the dependency closure once; preserve exact-target no-build checks.
   Start Bazel with few actions and an explicit scheduler RAM allowance. Verify
   `--jobs` and the supported RAM resource flag against the pinned Bazel version;
   scheduler resource estimates are not enforced RSS limits. Evaluate a smaller
   Bazel JVM heap, `--discard_analysis_cache`, `--nokeep_state_after_build` and
   `--notrack_incremental_state` where needed, recording extra rebuild cost.
   `--nokeep_state_after_build` alone does not reduce the current build's peak.
   These documented options trade incremental reuse for memory; see
   [Bazel's memory guidance](https://bazel.build/advanced/performance/memory).

   Lean 4.31.0's installed help verifies `--threads=N`, `--memory=MB` and
   `--tstack=KB`. Calibrate one heavy process at one/two threads before increasing
   threads. Lean's internal memory option is not a cap on the complete process
   tree; enforce the OS budget separately. `LEAN_NUM_THREADS` is not evidence
   that Lake launches only one compiler. Verify Lake's actual supported controls
   or schedule compiler invocations over a prepared dependency DAG explicitly;
   do not invent a `lake -j` flag. Preserve necessary stack/fuel bounds rather
   than reducing them until valid designs fail. Permit concurrent small jobs
   only after conservative measured peaks plus runner/cache overhead fit the
   aggregate budget. More available cores are not a reason to raise job count.

4. **Reduce the largest individual working sets.** Measure phase peaks and live
   allocations only when this final phase begins. First remove avoidable copies
   and retention of certificates, decoded graphs, environments, residual ASTs
   and proof data. Release each phase's transient structures before the next;
   share immutable DAG data where semantics permit. Evaluate bounded memoization,
   recomputation, streaming validated transport and disk-backed intermediate
   artifacts. Split a large proof/check into composable pieces only with a proved
   bridge back to the same whole result. Whole FP specialization must retain its
   static design, complete state and proved semantics. Any change to encoding,
   traversal, checking or specialization needs its affected correctness proofs
   and regression/admission checks before use; no host-simulator replacement,
   erased memories/components/clock/reset/async metadata or weaker checker.

   Where generation currently produces only an ephemeral residual, implement
   serialization/loading with the required round-trip/validation bridge and
   artifact identity before using it to avoid readiness re-specialization.
   Keep generation, serialization, checking, loading and readiness costs separate;
   cached/reused artifacts are not fresh generation evidence. Store artifacts and
   temporary files on project-local SSD paths, not RAM-backed filesystems or
   root-level temporary directories. Local storage can avoid the observed NFS
   import stalls; it does not by itself establish lower algorithmic RAM usage.

5. **Evaluate compressed swap as a separate mode.** After a no-swap baseline,
   test zswap with SSD-backed swap, or a separately configured zram mode, using
   compression supported by that target kernel. Zswap trades CPU work for reduced
   swap I/O; its compressed pool still occupies RAM and incompressible/hot data
   may benefit little. Zram also consumes physical RAM; its configured logical
   capacity is not additional installed memory. Do not stack both blindly or
   describe compression as a guaranteed RAM multiplier. Keep a bounded swap
   allowance and label swap-assisted passes separately from no-swap passes.
   Record host-wide compression backing memory as well as cgroup/process usage,
   because compressed-device accounting need not equal task RSS. Setup commands
   belong to the target-machine runbook; this plan does not change the current
   shared host's swap/kernel configuration. See the kernel's
   [zswap](https://docs.kernel.org/admin-guide/mm/zswap.html) and
   [zram](https://docs.kernel.org/admin-guide/blockdev/zram.html) documentation.

6. **Validate actual fit and results.** Calibrate on a small representative set
   plus the largest known working set, then select a configuration per relevant
   budget and run the entire required suite. Begin with the below-24-GB profile,
   then the below-16-GB profile; attempt smaller profiles after that. Record one
   controlled thread comparison rather than repeating every benchmark for every
   core count. Collect true producer exits, wall/user/system CPU, aggregate
   `memory.peak` (or a documented sampled fallback), per-process raw RSS KiB,
   swap peak, page faults, memory pressure and OOM events. Sum of unrelated
   process RSS maxima is not an observed aggregate peak. Separate build/preparation
   from runtime, and distinguish cold versus warm/cached execution.

   Check the same outputs, proof/admission results, residual bounds, edge/reset
   schedules and real state/memory transitions against the accepted baseline.
   Validate on the actual smaller Ubuntu machine: an isolated cgroup on a larger
   host is useful preparation but does not reproduce its CPU, disk, page cache
   and complete host pressure. Preserve original time caps; any additionally
   authorized slower low-memory run is a separately labelled configuration, not
   a rewrite of an earlier capped attempt. Classify OOM, timeout and I/O waits
   as resource outcomes, not semantic refusals or proof failures. Retain every
   failed attempt and report which experiments still exceed the target budget.

7. **Publish a reproducible low-memory runbook and results.** Deliver versioned
   profiles/runner commands, exact hardware/software/artifact bindings, resume
   checkpoints with revalidated dependencies, and per-experiment memory/time/
   swap/result tables. Report the lowest *demonstrated* budget for each phase and
   for the complete suite. Declare a target supported only after all required
   experiments and unchanged acceptance checks pass within its measured aggregate
   memory limit. If one job still needs more RAM, continue its proved memory
   reduction or report the exact limitation; unused CPU cores do not close it.

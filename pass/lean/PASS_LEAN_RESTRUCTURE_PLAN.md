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

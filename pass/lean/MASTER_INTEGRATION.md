# Lean refactor integration with master

> **Current migration audit:** The audit of `4342b312b` preserves the PR and
> B1/B2 source boundaries, but finds missing D2/D3/D4/FP work and reproduced
> runtime/runner acceptance defects. It does not claim all branches landed or
> a complete restored-flat replay. See [plan section 14](PASS_LEAN_RESTRUCTURE_PLAN.md#14-current-master-audit-and-corrective-migration-gates)
> and [MASTER_REBASE_AUDIT.json](tests/MASTER_REBASE_AUDIT.json).
> Earlier snapshots below remain historical evidence; R12 supplies the current
> migration dispositions and supersedes the noted stale R1/R10/R11 claims.

> **Current record-layout policy:** Automatic nesting above 64 fields has been
> withdrawn at the user's request. The original flat schema emitter is restored.
> Nested-layout measurements below describe historical checkpoints and do not
> establish acceptance of restored flat artifacts. Lean/Mathlib remain at 4.31.0;
> the user owns the upgrade in [plan section 13](PASS_LEAN_RESTRUCTURE_PLAN.md#13-user-owned-lean-upgrade-after-withdrawing-record-nesting).
> The separate large-constructor limit remains unresolved. No replacement
> representation workaround is introduced.

This integrates master `c54a435156a528d77afab2111bcb337403a0b42a` into refactor commit `61fd1646c1fd3106ce50d166ec02ca55ca19a478`. The shared architecture and the legacy L0–L8 work remain based on [PASS_LEAN_RESTRUCTURE_PLAN.md](PASS_LEAN_RESTRUCTURE_PLAN.md), with the implementation described in [LEGACY_REFACTOR.md](LEGACY_REFACTOR.md).

## Preservation boundary

The owned scan/IR data structures, certificate and memory lowering, verified-compiler emitter, legacy model and emitters, chunked-WF implementation, fast bridge, and `formal/lean` sources are unchanged by this integration. Native LEC and the other compiler/pass implementations come from master without local solver modifications. The previously approved CI and linker repairs remain.

The graph adapter and graph fixtures were ported to the new upstream representation:

| Upstream change | Commit | Adapter change |
| --- | --- | --- |
| New graph iteration API | `9cb2b1f7a` | Read body nodes and explicit sink drivers; retain deterministic operand ordering. |
| Constants stored on constant-pool pins; constant-node API removed | `9adce39b5` | Use `pin.is_const()` and `const_of(pin)`; constants still become owned values at the scan boundary. |
| Literal pin widths and signed-width accessor | `caa9e9fd9`, `ef46ffa4f` | Use `get_signed_bits()` where the old code used `get_bits()`; preserve intrinsic widths, negative constants, and strict width checks. |
| One driver per sink; arithmetic operand banks | `42773693a` | Translate sink slots into existing IR bank roles. Sum's even slots are adds and odd slots are subtracts; repeated operands remain separate dependencies. |
| Memory `init` renamed to `initial` | `fcf5a9d9a` | Read the new graph pin name into the existing initialization field; ROM contents, mutable-memory refusals, forwarding, and reset semantics are unchanged. |
| Global `formal.strict` removed | `b7a24e0b0` | Use `--set formal.lean.strict=true` (already the default); remove obsolete global forwarding. No compatibility alias is added. |

The exact fetched HLOP dependency sources were also compared: `Dlop::get_signed_bits`, `Blop::get_signed_bits64`, and `Blop::get_signed_bitsn` have the same bodies as their old `get_bits` counterparts after the API rename. This includes wide, multiword constants.

No new Concat/reduction support or change to reset, clock, signed comparison, or memory semantics is included. Unsupported operators remain explicit refusals. New graph databases must be generated with the matching compiler; old serialized graph databases are not portable across this upstream representation change.

## Strict mode

`formal.lean.strict` selects the Lean exporter's existing strict-width policy. At its default `true`, a constant that cannot fit its declared width and a dependency pin with zero/excessive width are rejected. Setting it to `false` relaxes those checks and can substitute a one-bit dependency width. Unsupported operators, X/Z constants, and unsupported memory policies are still rejected independently. It is an export policy, not a theorem-prover strength setting.

## Validation

The isolated GCC 14 / Bazel 9.2.0 debug build passes all three Lean test targets (24 cases) and both native LEC/linker targets (30 cases). Scanner coverage includes the existing asynchronous-reset and synchronous-ROM tests plus new checks for non-dense Sum banks, duplicate operands, and constant-pool widths.

Fresh tiny graphs were generated separately with the pre-merge and integrated binaries. Across all 38 fixtures, every acceptance/refusal result and refusal diagnostic matches. All 77 successful exports are byte-for-byte identical:

- 27 legacy fast-model/certificate exports;
- 23 legacy exports with `emit_fast_bridge=true`, `cert_wf=chunked`, and chunk size 2;
- 27 verified-compiler exports.

The 11 default-mode refusals and the 15 bridge/chunked-mode refusals are preserved. Optional proof-shape refusals are not represented as successful proofs. Machine-readable per-fixture evidence is in [MASTER_ADAPTER_PARITY.json](tests/MASTER_ADAPTER_PARITY.json).

Lean elaboration passes for all 23 accepted bridge/chunked-WF fixtures. The 27 accepted fast models pass sampled cross-version oracles in both directions (new fast model against the old verified certificate, and old fast model against the new verified certificate), using 16 seeds and every address of the tiny memories. Independent generated Lean oracles also prove the reset-priority examples and the banked Sum result of 41. No `sorryAx` was accepted. The full `lhd` executable builds. All 22 targeted integration tests pass, including all 17 tests from the historical native CI comparison, the scanner suite, option listing, and formal CLI checks. See [MASTER_NATIVE_REGRESSION.json](tests/MASTER_NATIVE_REGRESSION.json). The historical DINO/block proof evidence remains tied to its recorded pre-integration compiler/library context. These tiny-fixture checks do not establish a new full-design replay against master's frontend.

## Additional CLI findings

Master's retired-option list also hid `formal.lean.cert_chunk_size`, `formal.lean.cert_chunk_limit`, and `formal.lean.cert_wf_fallback`. They are implemented by this refactor, so the three blacklist entries are removed and CLI tests cover their availability. `normalize` stays retired. Native LEC options and implementation are unchanged.

A fresh RTL-to-Lean smoke test for an 8-bit addition followed by XOR produces a `Concat` node under master's frontend. The preserved Lean scanner rejects it explicitly. This is a frontend compatibility limitation, not evidence that an emitted proof passed for that RTL. Translating Concat into existing operations is pending the user's decision; the unchanged certificate/model/proof semantics are not being expanded implicitly.

The restored controls pass `lhd_options_test`, `lhd_list_options_test`, and `lhd_formal_verify_test`. A separate XOR-only RTL fixture exports successfully in both Lean modes, its generated Lean files elaborate, and the generated Verilog is proven equivalent to the RTL by native LEC. The legacy CLI run includes the restored chunk-size control, a full chunked-WF proof, and the fast bridge. Global `formal.strict` is confirmed rejected. The addition/Concat fixture remains a separate recorded failure.

The separate historical `prp-equiv-wire_ring` regression also passes using the unchanged master harness and default solver settings.


## Port onto master `8bea45dc2` (validation in progress)

The PR endpoint is `3685a977ee5be31f5939c83617f1264e2d9ddaab`.
Its net changes relative to `c54a435156a528d77afab2111bcb337403a0b42a`
are applied to master `8bea45dc2aa9aab4d8b279db11a8887c52c1108d`.
The original PR branch and a verified Git bundle preserve the prior history.
The [preservation ledger](tests/MASTER_PORT_PRESERVATION.json) accounts for
all 117 files in the PR's net change: 108 are byte-identical, eight have
explicit upstream adaptations or added tests, and this report retains its
prior content with the current audit appended. New validation records are
additional files, not replacements for historical records.

All formal Lean sources, certificate definitions and builders, memory lowering,
legacy models, and emitters are byte-identical to the PR. The existing policies
for clocks, asynchronous reset Q reads, constant widths, signed comparisons,
and unsupported operators remain in place. Native LEC implementation and its
proof procedures come from current master without modifications.

Upstream commit `eb91f09c1` changes Get_mask/Set_mask from mask-value operands
to constant half-open `[lo, hi)` endpoints. The scanner materializes the
corresponding mask in owned scan data, preserving the certificate schema.
The synthesized constant uses the existing intrinsic-width convention, including
the sign bit on positive multiword constants. The new 67-bit mask regression
initially caught an undersized synthesized declaration; its owned metadata was
corrected without relaxing the strict-width rules. The adapter rejects malformed,
nonconstant, or excessive endpoints. Reset-input tracing
recognizes zero-based windows that cover the source width. Graph fixtures use
the new endpoint API. No Concat/reduction support is added implicitly.

Current master's single optimized CI job is retained. The PR's duplicate-trigger
prevention, concurrency cancellation, unique cache keys, and cache-save handling
are carried forward. Its coverage-filter change is superseded by upstream's
removal of the coverage job. The PR's cvc5/ABC static-link collision repair and
CLI registration of the implemented chunk controls are preserved.

The [validation record](tests/MASTER_PORT_VALIDATION.json) distinguishes current
checks from historical evidence. All 38 tiny fixtures retain their acceptance
or refusal in each of the three export modes. Of 77 accepted exports, 74 are
byte-identical; only the GetMask fixture changes its mask representation from
`-1` to `255`. Both directions of sampled cross-version oracles pass for all
27 accepted fast fixtures, and all 23 new tiny bridge/chunked-WF files elaborate.
The support library was rebuilt into a fresh output directory before these
checks. All 87 historical block proofs completed, and their artifact hashes
were rechecked; this does not establish regeneration through current master.

The upstream [optimized macOS regression](https://github.com/masc-ucsc/livehd/actions/runs/37708819790)
has 4,667 passing tests, one failure, and two skipped tests. Its failure is
`CgenLlvm.ObjectOwnedStateIsPrivateAndIndependentAcrossInstances`. That native
implementation is unchanged by this port. Local validation is on Linux and
does not replace macOS validation.

The five focused C++/CLI/linker targets pass. Twelve independent Lean oracles
pass for endpoint extraction/replacement (including bit 64 and windows above the
source width), reset priority, and banked Sum. A fresh in-project XOR RTL fixture
exports and proves in both modes; the legacy run includes the full chunked-WF
and fast bridge. The [mask preservation proof](tests/legacy_semantic_audit/MaskEndpointPort.lean)
also establishes equality for **every** input between the old and new mask
fixture, covering fast output, the legacy source environment, and the DesignCert
interpreter. Its preservation theorems contain no `sorryAx`.

Full current-frontend DINO exports, saved DINO proof replay, and the optimized
regression are not yet accepted. No publication
should be inferred from this in-progress record.

## Research and completion plan (2026-10-08)

This section supersedes earlier pending decisions in this report. It is a plan,
not a claim that current frontend benchmarks pass. Implementation is held while
this plan is reviewed. The user approved Concat lowering, and has now approved
including Hotmux lowering through existing Lean Mux operations throughout the
flow. No Hotmux implementation has been added yet.

### Pinned source and upstream findings

A live `git ls-remote` check still identifies master as
`8bea45dc2aa9aab4d8b279db11a8887c52c1108d`, authored by Jose Renau on
2026-10-07. The integration checkout already has that HEAD. The latest commit
changes only `inou/cgen/cgen_llvm.cpp`; the immediately preceding
`899c706a7` moves graph legalization earlier and updates HHDS. There are 64
upstream commits since the PR's previous integration base `c54a435`.

The latest master [macOS 26 run](https://github.com/masc-ucsc/livehd/actions/runs/37708819790)
is completed with failure, not green. The recorded failing test is
`CgenLlvm.ObjectOwnedStateIsPrivateAndIndependentAcrossInstances`.
Linux validation cannot establish that this platform-specific failure is fixed.

| Area | Evidence | Required treatment |
| --- | --- | --- |
| Mask operands | `eb91f09c1`: Get_mask/Set_mask now use half-open endpoints | Preserve the PR's mask-based certificate operations via owned adapter constants. Existing endpoint and universal fixture preservation proofs pass. |
| Concat | Present already in `c54a435`; strict lane rules are in `graph/node_util.hpp` | Retain declared MSB-first windows, signed widening inside each window, and strict malformed/over-wide refusals. Approved lowering now passes five exhaustive arithmetic cases and five legacy bridge/chunked-WF proofs. |
| Hotmux | Present already in `c54a435`; native value encoding in `pass/lec/encode.cpp`; separate obligation handling in `pass/formal/pass_formal.cpp` | Add shared lowering with first-active priority and explicit default behavior. Preserve and report one-hot obligation status separately. Do not attribute introduction of this operator to the latest commit. |
| Reset/control wrappers | Fresh CVA6 controller rendering shows `Sext(rst_ni)` feeding async reset and `Sext(clk_i)` feeding clock | Trace only wrappers proven to preserve the relevant Boolean/edge condition. Keep asynchronous Q-read, reset polarity, and next-state semantics unchanged. |
| Clock normalization | `0d26c1d63` documents previously unsound gated-latch lowering; current single_edge also tracks clock-bus bit identity | Preserve the native refusals. A formerly passing proof may have depended on a historical unsound normalization; it must not be recovered by weakening the guard. |
| Memory and widths | `2d63ad67e` adds offset/out-of-depth memory handling; `eb91f09c1` changes bitwise-input extension; `59b64c714` repairs serialized pins and memory proof initialization | Audit fresh graph shape, ROM contents, initialization, addressing, and signed extension. Keep existing Lean policies; expose any additional semantic mismatch for a decision. |
| Graph lifecycle/dependencies | `899c706a7`: earlier legalization and HHDS update; current HHDS pin `a632b5b1`, HLOP pin `5945f877`, plus HHDS padding patch | Build against master's pinned dependencies and generate fresh graph databases. Saved graphs from incompatible formats are not a validation substitute. |
| CI and CLI | Master retains one optimized self-contained macOS job and removes coverage; CLI still retires three implemented Lean chunk controls | Retain current workflow structure, carry the PR's trigger/cache repairs, restore implemented Lean options, and keep normalize retired. |

The five files changed by both the PR and subsequent master are
`.github/workflows/ubuntu.yml`, `lhd/lhd_kernel_internal.hpp`,
`lhd/lhd_options_test.cpp`, `pass/lean/pass_lean.cpp`, and `pass/lec/BUILD`.
Review every resolution explicitly. Native LEC implementation and proof settings
remain those of master.

### Observed fresh-input compatibility snapshot

All 186 recorded CORE-ET/CVA6 configurations have completed their first frontend
and export attempts. These counts mean at least one export was accepted, not
that every model or theorem typechecked:

| Family | Configurations | At least one accepted export | Lean export refused in all attempted modes | Frontend failure | single_edge refusal |
| --- | ---: | ---: | ---: | ---: | ---: |
| CORE-ET | 125 | 51 | 30 | 20 | 24 |
| CVA6 | 61 | 10 | 42 | 9 | 0 |

The first Lean refusal is Hotmux for 50 configurations and async-reset input
tracing for 22. All three fresh DINO variants reach the Hotmux refusal after
Concat lowering. The 24 CORE-ET normalization refusals divide into 13 memory
refusals and 11 clock-gate-latch-phase refusals. Frontend failures include
combinational-loop reports, two Concat lane-width violations, and source
elaboration failures. These require baseline reproduction and classification;
they are not all new Lean bugs or all historical failures.

Of configurations recorded as historical successes, 37 CORE-ET and 44 CVA6
currently have no accepted export. Their previous proof scopes differ, so this
is an investigation list rather than an assertion that all 81 are equivalent
regressions. The inventory includes later recorded attempts and legacy wrappers,
not just the original sweep tables. Ongoing proof runs are provisional: the
runtime verified-compiler audit initially requested an unprinted `_compiles`
audit. The corrected harness must rerun these files and require the actual
`_step_correct` audit, `_compiles` declaration, successful Lean exit, and no
`sorryAx`. That harness issue is not a Lean theorem failure.

### Implementation sequence and acceptance gates

1. **Freeze provenance and preserve every PR change.** Retain the original PR
   checkout, original merge history bundle, and 117-file preservation ledger.
   Record master/PR hashes, dependency pins, binary hashes, Lean version and
   library hashes, benchmark inputs, wrapper configuration, and commands.
   Compare historical source fingerprints where available; otherwise label the
   old/current source identity as unestablished rather than assuming it.
   Reconcile each of the five overlap files. Keep old reports as historical
   evidence, with new evidence explicitly labeled.
2. **Finish a shared graph adapter inventory.** Inspect every relevant operator,
   width/sign annotation, reset/clock cone, memory policy, and hierarchy shape
   in fresh graphs, rather than discovering only the first unsupported node.
   Preserve endpoint and Concat work already validated. Add minimal standalone
   fixtures for each observed new representation; keep graph objects unchanged.
3. **Add Hotmux throughout the Lean flow at the shared scan boundary.** Validate
   contiguous control/value pairs and optional default. Convert each control
   using its full nonzero predicate (not just bit zero). Normalize arm widths
   with the required signed/unsigned extension or truncation, then construct
   nested existing Mux nodes in first-active priority order. Use zero when no
   control is active and no default exists. Allocate collision-free stable
   synthetic IDs and retain original-node provenance in diagnostics.

   Both legacy GraphCert and verified DesignCert will contain the lowered Mux
   operations. The legacy fast model must consume that same lowered scan/IR;
   fast bridge, dense-index chunked WF, verified compilation, and correctness
   proofs then operate on the same dependencies. No new Lean primitive or
   `Op_Hotmux` is required for this approach. Add independent tests covering
   no/one/multiple active controls, explicit/implicit defaults, multi-bit
   controls, signed mixed widths, malformed layouts, fanout, and nesting. Prove
   both fast/certificate equivalence and the independent priority-value oracle.

   Carry one-hot status into export diagnostics/validation metadata without
   changing the source graph or declaring an unresolved obligation proved.
   Distinguish native-proved, deferred, and refuted status. Refuted obligations
   must remain failures; deferred obligations remain visible alongside value
   proofs. Certificate WF proves structure, the bridge proves model/certificate
   values, and neither proves one-hotness or RTL-to-graph correctness. If a
   Lean proof of one-hotness is desired, that is a separate theorem obligation.
4. **Repair transparent reset/clock tracing with equivalence evidence.** Start
   with the observed one-bit Sext wrappers. Prove the condition is unchanged
   before following a wrapper. Add active-high/low, asserted/deasserted async
   read, next-state, synchronous-reset, truncating/nontransparent, and clock-bit
   negative cases. Do not loosen the existing primary-input reset restriction
   or clock normalization policy to accept arbitrary expressions.
5. **Classify failures outside Lean before proposing fixes.** Reproduce each
   frontend/normalization failure using pristine pinned master without Lean
   emission. Compare the previous accepted procedure and relevant historical
   commits. Keep native LEC, normalization guards, immutable contract tests, and
   compiler-warning policy unchanged. Record upstream regressions and historical
   soundness corrections separately. Bring any required semantic or native-code
   change back to the user with a minimal reproduction and proposed treatment.
6. **Run the complete matched validation matrix.** Rebuild all Lean support
   sources; rerun scanner/certificate/model, CLI, linker, and all optimized
   repository tests with isolated local runtime directories. Retain the 38 tiny
   fixtures, cross-version oracles in both directions, and independent lowering
   oracles. For all three DINO designs, regenerate RTL-to-graph artifacts and
   check legacy fast typechecking, `emit_fast_bridge=true`, full `cert_wf=chunked`
   with chunk size 100, and the verified compiler path. Record `/usr/bin/time -v`
   wall time/RSS and theorem audits for every accepted proof.

   Replay all 125 CORE-ET and 61 CVA6 configurations with recorded readers,
   filelists, wrappers, normalization, options, and historical proof scope.
   Rerun earlier refusals/timeouts as well. Track compile, normalization, export,
   typecheck, bridge, WF, and verified-correctness outcomes separately. A saved
   artifact replay only establishes library compatibility. Keep unresolved
   failures explicit and require a disposition for every previously accepted
   case. Frontend runs using the historical `--ignore-assertions` procedure do
   not establish the ignored RTL assertions.
7. **Land only after reviewable evidence.** Refresh the preservation ledger and
   current validation report, include the L0-L8 source plan reference, and check
   committed material for private absolute paths. Recheck remote master before
   publication; if it moves, review the new delta and invalidate affected proof
   evidence. Produce a normal descendant commit on master and push without
   forcing history, then link the landed commit from the superseded PR. Do not
   claim the port is ready while previously accepted cases remain unexplained.

Bazel output, repository cache, and runtime state remain in the approved local
NVMe allocation for this checkout. No global cache/config change or deletion of
another checkout's cache is needed. Original dirty workspaces remain untouched.

## Revision 2026-10-08b — audit corrections and whole-programme scope

This section supersedes the **scope** of the preceding section and adds the
items an audit found missing. Everything already recorded above is retained as
evidence; nothing here withdraws a prior measurement. Where a number below
disagrees with an earlier paragraph, the disagreement itself is the finding and
is called out explicitly rather than silently corrected.

### R1. The target is every branch, not this PR alone

The preceding plan covers only the legacy + verified-compiler pass. The
programme goal is that **master carries all accumulated progress**, so each
branch below must land, and each must be named in the landing order because
they share `formal/lean/LeanSemanticPrimitives/Compiler/`.

| Branch | Checkout | Owns (not present on master) | Shares |
| --- | --- | --- | --- |
| `b1-b2-verified-compiler` | `livehd-new` | C++ scan/IR/emitters, `CompileOp/CompileGraph/CompileDesign`, `compileDesign_correct`, chunked WF, fast bridge | `DesignCert`, `Runtime`, `DesignSemantics` |
| `direction-2-ir-semantics-clean` | `livehd-d2-ir-semantics` | **Multi-clock**: `ClockDesc`, `DesignCert.clocks`, `FlopDesc.clock`, `ClockEdges`/`fires`/`allEdges`; `DirectSemantics`, `DirectCheck`, `DirectSim`, `DirectTrace`, `DirectVsCompiled`, `DirectBench`, `DirectExamples`, `DirectTests` | same three |
| `direction-3-translation-validation` | `livehd-d3-translation-validation` | `Reify`, `ReifyGen`, `ReifyProof`, `D3Harness`, the `*Defs` module split | same three |
| `direction-4-incremental` | `livehd-d4-incremental` | `CertIO`, `CertIORoundTrip`, C++ DCERT1 serializer and staged publication (see R12 for current roundtrip/provenance scope) | same three |
| `projection` | `livehd-futamura/livehd` | `Projection/` (partial evaluator, `I_hw`, specializer), `SIMULATOR_PLAN.md` | pins `DesignCert`/`interpretDesign` at a fixed revision |

**Schema ownership and landing order.** The two `DesignCert.lean` schemas have
already diverged: this integration's copy has no clock fields, while D2's
declares `ClockDesc` and `clocks : Array ClockDesc := #[{ name := "clock" }]`.
D2's additions are defaulted, so they are a backward-compatible superset and
every existing certificate literal still elaborates. D2 owns that type.
Required order, with a gate between each step:

1. B1+B2 lands (this report).
2. D2 lands its clock-aware `DesignCert`/`Runtime`/`DesignSemantics` on top,
   together with `interpretDesign_allEdges`, which is what makes the schema
   change meaning-preserving for one-domain certificates.
3. Port D3 proofs and D4 transport onto the post-D2 schema. D4 also has a C++
   serializer/publication path; make it a consumer of the shared CertificateIR,
   with no second graph traversal and explicit clock-aware wire compatibility.
4. Migrate Futamura's fixed semantic pin explicitly with equivalence checks.
   Its independent checkout is ahead of the stale local ref; neither landing
   B1/B2 nor D2 moves that pin automatically. See R12 and plan section 14.

Adopting D2's checker is recorded on that branch (`DIRECTION2_RESULTS.md:658`)
as regressing **54 of the 147** baseline certificates with `asyncFlagMismatch`
until they are re-emitted. That re-emission belongs to step 2's gate and must
not be deferred past it.

### R2. Complete operator disposition — no operator left unaccounted

`Ntype_op` carries 31 real operators and is **byte-identical** at `c54a435` and
`8bea45dc2`; no operator is new in this rebase. The Lean model `LGraphOp`
(`Translation/LGraphModel.lean`) has 29 constructors and is identical on
`b1-b2-verified-compiler`, `direction-2-ir-semantics-clean`,
`direction-3-translation-validation` and `direction-4-incremental`.

**Therefore: every operator unsupported here is unsupported on every branch.**
None of this work is stranded elsewhere waiting to be collected.

| Operator | Status in the pass | Supported on another branch? | Disposition for master |
| --- | --- | --- | --- |
| `Sum` `Mult` `Div` `And` `Or` `Xor` `Ror` `Not` `EQ` `LT` `GT` `SHL` `SRA` `Mux` `Sext` `Get_mask` `Set_mask` `Memory` | supported (18) | — | keep; parity gates below |
| `Concat` | lowered in `design_scan.cpp` (strict MSB-first lanes) | no | keep lowering; no `Op_Concat` added |
| `Hotmux` | **refused** | no | approved: lower to nested `Mux`, first-active priority |
| `Rem` | **refused** | no | decide: `Op_Rem` in the model, or permanent refusal |
| `LUT` | **refused** | no | decide: lower to `Op_MuxN`, or permanent refusal |
| `Rxor` | **refused** | no | decide: lower via `Op_Xor` fold, or permanent refusal |
| `Popcount` | **refused** | no | decide: lower via adder tree, or permanent refusal |
| `Latch` | **refused** | no | permanent refusal — level-sensitive state is outside the single-step model |
| `Fflop` | **refused** | no | permanent refusal pending a flop-flavour decision |
| `Clock_cell` | **refused** | no | permanent refusal; clock normalization is upstream's job |
| `Sub` | **refused** | no | permanent refusal — hierarchy is flattened before export |
| `AttrSet` | **refused** | no | permanent refusal — attribute carrier, no value semantics |
| `IO` `Flop` | handled outside `scan_op` (ports / state) | — | unchanged |

**Gate O1 — operator census before any lowering work.** The snapshot above
reports *first* refusal per configuration ("Hotmux for 50 configurations"). A
first-refusal count cannot bound the remaining work: behind a Hotmux refusal
there may be `Rem`, `LUT`, `Rxor` or `Popcount` nodes never yet reached. Before
Hotmux is implemented, run a full census over fresh graphs that records **every**
distinct operator, width/sign annotation and memory policy per configuration,
not the first unsupported node. Publish it as `tests/MASTER_OP_CENSUS.json`.
Implementing Hotmux without this census cannot be claimed to unblock any stated
number of designs.

**Gate O2 — a disposition for each of the four undecided operators.** `Rem`,
`LUT`, `Rxor` and `Popcount` need an explicit decision recorded here before
landing, even if the decision is "permanent refusal". Leaving them undecided is
what lets progress be lost: a later branch adds one privately and master never
receives it.

### R3. Four arity refusals present in master and absent from this pass

`pass/lean/pass_lean.cpp` on master (identical at `c54a435` and `8bea45dc2`)
refuses malformed arities explicitly:

| master line | refusal |
| ---: | --- |
| 718 | `Div node n_… is not binary.` |
| 772 | `LT/GT node n_… is not binary.` |
| 811 | `SHL node n_… is not binary.` |
| 826 | `SRA node n_… is not binary.` |

The refactored scanner has **none** of them. `certificate_ir.cpp`'s LT/GT arm
applies `all(width)` to whatever operands arrive, so a three-operand comparison
emits a three-dependency `Op_ULT` instead of refusing; `LT`/`GT` are two-banked
(`Ntype::sink_bank_count` returns 2 for `Sum`, `LT`, `GT`), so folded
comparisons are representable upstream. `LEGACY_SEMANTIC_AUDIT.md` already lists
this family under "Nonstandard/malformed arities"; what is new here is that
**master is now the rebase target**, so these are parity obligations, not
acceptable inherited drift.

**Gate A1.** Restore all four refusals in the shared scan/IR boundary, with one
fixture each asserting the refusal and its diagnostic. Until then this report
must not state that "unsupported operators remain explicit refusals" without
qualification — malformed *arities* of supported operators currently do not.

### R4. `Operand::port` carries a bank, and the raw pid is discarded

`design_scan.cpp:402` stores `Ntype::sink_bank(node_op(node), e.sink.get_port_id())`
into the field named `port`. This is correct today: `sink_bank` is the identity
for unbanked operators, so `Mux`'s selector-at-pid-0 still resolves, and the
banked operators are exactly the ones whose role is bank-determined. Selecting
on the raw pid instead would be a silent miscompile — a `Sum` with two addends
occupies pids `{0, 2}`, so `port == 0` keeps the first and `port == 1` matches
nothing, dropping the second with no diagnostic.

Two problems remain. The field is named `port` while carrying a bank, and the
pid is unrecoverable downstream. The approved Hotmux lowering requires
"contiguous control/value pairs", which is a statement about **pid order**.

**Gate B1.** Split `Operand` into `port` (raw pid) and `bank`
(`Ntype::sink_bank` of it) before Hotmux lowering begins; update the `Sum` arm
to select on `bank` explicitly. Add a regression placing a second addend at
pid 2 with bank 0 and asserting both the add count and the dependency count.

### R5. One frozen benchmark manifest

"Re-run every already-covered benchmark" is not checkable while six
inventories disagree:

| Source | Count |
| --- | ---: |
| `SWEEP_b1-b2.tsv` | 122 rows |
| `SWEEP_cva6.tsv` | 23 rows |
| replay matrix in the preceding section | 125 CORE-ET + 61 CVA6 |
| `tests/LEGACY_PROOF_COVERAGE.json` | 90 results (document headline: 68/90) |
| `tests/LEGACY_PROOF_INVENTORY.json` | 87 entries |
| `tests/LEGACY_PROOF_COVERAGE_ADDITIONS.json` | 13 entries |

[`LEGACY_CI_AUDIT.md`](LEGACY_CI_AUDIT.md) separately records 81/90 with nine
cases lacking completion records. The preceding section's "all 87
historical block proofs completed" uses the inventory denominator, not the
coverage denominator; the two sets are not the same and neither is a subset
claim that has been checked.

**Gate M1.** Produce `tests/MASTER_BENCHMARK_MANIFEST.json`: one row per
configuration, with its identity, which inventories list it, its historical
scope (export / typecheck / bridge / WF / verified-correctness), the recorded
historical outcome, and a required disposition after the rebase. Reconcile the
denominators explicitly, including entries that appear in only one inventory.
No "previously accepted case left unexplained" claim can be evaluated before
this file exists.

### R6. Semantics coverage that is not an operator

The operator table does not capture everything that must reach master. These
are the non-operator semantics, their owner, and their gate:

| Semantics | Owner | Gate |
| --- | --- | --- |
| Multi-clock domains, edge vectors, quiet-domain holds, async reset independent of firing edges | D2 | lands at step 2; includes re-emitting the 54 `asyncFlagMismatch` certificates |
| Async-reset Q reads, reset polarity, nonzero reset value | B1 (already here) | unchanged; the 495 source-expression changes stay recorded in `LEGACY_SEMANTIC_AUDIT.md` |
| Mixed-width signed `LT`/`GT`, narrow-result unsigned `Div`, general/dynamic `Sext` | B1 (already here) | unchanged renderer rules; counterexamples stay recorded |
| Immutable ROM, synchronous ROM read registers | B1 (already here) | unchanged |
| Memory forwarding matrix, byte enables, write-port order | B1 (already here) | unchanged |
| Residual reification to named Lean declarations | D3 | step 3; research tooling, not a production path |
| Runtime certificate parse/load (`DCERT1`) | D4 | step 3; parser recorded as trusted |
| Partial evaluation / projected simulator | Futamura | step 4; re-pin only, no schema change |
| `pass.lean` has no multi-clock guard — a two-clock all-posedge design exports a certificate where every flop commits every step | unowned | **open soundness gap**; must be assigned before B1 lands or recorded here as knowingly deferred |

The last row is the one most at risk of being lost: it is recorded on the
Futamura branch's plan as out of scope there, and it is not an operator, so no
operator census will surface it.

### R7. Acceptance gates added to the implementation sequence

Insert before the existing step 3 (Hotmux):

- **O1** operator census over fresh graphs, every operator per configuration.
- **O2** recorded disposition for `Rem`, `LUT`, `Rxor`, `Popcount`.
- **A1** four arity refusals restored with fixtures.
- **B1** `Operand` split into `port` and `bank`, with the pid-2 addend regression.

Insert into the existing step 6 (validation matrix):

- **M1** frozen benchmark manifest with per-entry disposition.

Add as a new final step, after the existing step 7:

- **S1** land D2, then D3/D4, then re-pin Futamura, each with its own
  validation record. The programme is not complete while any branch still
  holds semantics that master lacks. Record the landed commit for each in this
  report so the next reader can tell what master contains without reading five
  branches.

### R8. Implementation status (2026-10-08, this checkout)

Four of the gates added above are implemented and verified here. `//pass/lean:all`
passes: `certificate_ir_test`, `design_scan_test`, `legacy_model_test`.
Nothing is committed — the tree still holds the whole 122-file port staged, and
landing that is step 7's decision, not this work's.

**B1 — `Operand` split into `port` and `bank`. DONE.**
`design_scan.hpp` now carries both: `port` is the raw sink pid (the operand's
SLOT, what a lowering needing contiguous control/value pairs reads) and `bank`
is `Ntype::sink_bank` of it (the operand's ROLE). `certificate_ir.cpp` selects
`Sum`'s adds/subs and `Mux`'s selector on `bank`; `LT`/`GT` no longer need the
`operands.size() == 2` guard for width because the arity is now refused earlier.

This also exposed a latent defect in the Concat lowering: the final `Or` pushed
**pid 0 for every lane**, which was invisible while the field held a bank (`Or`
folds one bank, so every lane is bank 0) but describes a shape
one-driver-per-sink-pin forbids. Lane k is now pid k, bank 0. The three
synthetic `Or`/`Sext`/`SHL` nodes the lowering emits set both fields explicitly.

Regression: `SetMaskAndSumKeepOperandOrder` places its second addend at pid 2
with bank 0 — the shape that silently loses an operand when a role is read off
the raw pid — and now asserts the dependency count as well as the add count.

**A1 — four arity refusals restored. DONE.**
`check_operand_arity` in `design_scan.cpp` refuses a non-binary `Div`, `LT`,
`GT`, `SHL` or `SRA`, matching master's `pass_lean.cpp` refusals by name. It
sits at the shared scan boundary, so the legacy emitters and the
verified-compiler exporter both inherit it; refusing inside one emitter would
leave the other accepting the node.

Regression: `DesignScan.MalformedBinaryArityIsRefused` walks all five operators
and asserts **both** directions — the binary form is still accepted, and the
three-operand form is refused — so the gate cannot pass by over-refusing.

**O1 (mechanism) — the refusal names the whole census. DONE.**
`scan_op` became `try_scan_op`, returning `std::optional<ScanOp>` instead of
fataling. The reachable walk records every unsupported operator with the first
node that used it, and raises **one** refusal afterwards naming all of them plus
the total unsupported node count.

This is the methodological fix: a first-refusal diagnostic cannot size the
remaining work, because a second unsupported operator behind the first is never
reached and never counted. "Lowering Hotmux unblocks 50 configurations" was
never a claim the old diagnostic could support.

Regression: `DesignScan.UnsupportedOperatorRefusalNamesTheWholeCensus` builds a
design containing both `Rem` and `Popcount` and asserts the message names each.

**O1 (the run) — NOT DONE.** Producing `tests/MASTER_OP_CENSUS.json` still needs
the frontend sweep over fresh graphs. The mechanism above is its prerequisite;
the sweep itself is step 6 work.

**M1 — frozen benchmark manifest. DONE.**
[`tests/MASTER_BENCHMARK_MANIFEST.json`](tests/MASTER_BENCHMARK_MANIFEST.json)
reconciles the five committed inventories into one keyed list:

| | |
| --- | ---: |
| reconciled entries | **256** |
| CORE-ET / CVA6 / DINO / small | 209 / 36 / 3 / 8 |
| by scope: sweep / bridge / unspecified | 145 / 90 / 21 |
| **listed by exactly one inventory** | **178** |

That last row is the finding. The inventories barely overlap — 178 of 256
entries appear in only one of them — so the 90-result coverage snapshot and the
87-entry proof inventory are different sets, not a subset relation, and neither
subsumes the sweep tables. Every entry carries its recorded historical outcome
and a disposition field defaulted to `REQUIRED: rerun and record outcome`.

Still missing as a sixth source: the 125 CORE-ET / 61 CVA6 fresh-input
configuration list quoted earlier in this report is not backed by a committed
file. It must be added before the replay, or the replay cannot be reconciled
against this manifest.

### R9. O2 — dispositions for the four undecided operators

All four are expressible with the existing 29 `LGraphOp` constructors, so none
requires a new Lean primitive. That matches the Hotmux decision: lower at the
scan boundary, leave the model alone.

| Operator | Semantics (`graph/cell.hpp`) | Proposed lowering | Cost / open question |
| --- | --- | --- | --- |
| `Rem` | truncated remainder, sign follows the DIVIDEND (Verilog `%`), binary | `a - trunc(a / b) * b` via `Op_SDiv`/`Op_UDiv` + `Op_Mult` + `Op_Sum` with the product in the subtract bank | `bv_sdiv` already rounds with `trunc_div_int`, so the identity holds for `b ≠ 0`. **`b = 0` needs a decision**: `bv_sdiv` yields 0, so the lowering yields `a`, and LiveHD's `Rem`-by-zero intent must be confirmed rather than inherited |
| `LUT` | constant lookup table | nested `Op_MuxN` over the table constants, the same shape as the approved Hotmux lowering | table size becomes node count; large LUTs inflate the certificate |
| `Rxor` | parity (0/1) of the low `b` bits of `a`, `b` a constant count | `b` × `Op_GetMask` to extract each bit, folded with `Op_Xor` | **O(b) nodes** — 64 extra nodes on a 64-bit operand, which lands on certificate size and chunked-WF proof time |
| `Popcount` | number of set bits (0..`b`) in the low `b` bits | `b` × `Op_GetMask`, zero-extended, summed by `Op_Sum` | **O(b) nodes**, same caveat |

#### R9.1 Remainder by zero is NOT defined as `a` (supersedes the row above)

The `b = 0` question is settled the other way: **do not define `a % 0 = a` in
this port.** Upstream does not speak with one voice, so adopting any of its
answers would be a silent choice, not an inheritance:

| Implementation | Remainder by zero |
| --- | --- |
| Pinned HLOP constant evaluation | returns `nil` |
| HLOP simulator | asserts |
| Native LEC, cvc5 `BITVECTOR_SREM` | returns the dividend |

Matching the solver alone would prove agreement with cvc5, not alignment with
upstream execution — and the certificate is supposed to mean what the hardware
means, not what the checker happens to compute.

Disposition:

1. **Support `Rem` only with a known NONZERO CONSTANT divisor.** Refuse a zero
   divisor and refuse a dynamic divisor that cannot be shown nonzero. Dynamic
   support may follow later behind an explicit nonzero proof obligation, which
   is a theorem, not a lowering.
2. Compute `a - trunc(a / b) * b` at a width sufficient for the operands, then
   truncate to the result width — not the other way round.
3. **Signed remainder cannot reuse the existing `Div` mapping.** `ScanOp::Div`
   lowers to `Operation::UDiv` unconditionally (`certificate_ir.cpp:130`), and
   the `Operation` enum has **no `SDiv` at all**, although the Lean model does
   carry `Op_SDiv`. Signed `Rem` therefore needs that plumbing added first.
4. Tests must cover negative operands, mixed signedness, narrow outputs, wide
   constants, and the minimum signed value divided by −1.

Recorded separately, because it is inherited rather than introduced here:
master's own exporter also emits `sem_udiv` / `Op_UDiv` for every `Div`
regardless of `node_output_is_signed` (`pass_lean.cpp:720` and `:1159`). The
refactor preserves that faithfully. Whether a signed `Div` is being mis-modelled
upstream is a separate question from `Rem`, and is not changed by this port.

#### R9.2 `LUT`, `Rxor` and `Popcount` wait on the full census

My earlier recommendation — take `Rem` and `LUT`, defer `Rxor`/`Popcount` — is
withdrawn. **DINO alone cannot justify deferring them.** All three fresh DINO
designs now export with an EMPTY census once Hotmux is lowered, which shows only
that DINO does not use these operators; CORE-ET and CVA6 are 186 configurations
and have not been swept.

Prioritize `LUT`, `Rxor` and `Popcount` from the full CORE-ET/CVA6 census, not
from DINO. `LUT` additionally needs an **explicit size bound** before any
lowering lands: its expansion is one `Op_MuxN` arm per table entry, so an
unbounded table is an unbounded certificate.

The decision for each stays recorded here either way, so no later branch can add
one privately and leave master without it.

## B1/B2 branch preservation audit after the initial port

The PR-endpoint ledger cannot detect work that disappeared before that endpoint.
This audit therefore compares the entire tracked tree at original B1/B2 commit
`b04288cca51f79fc663ca2e49c54e1fae6e7f221` with PR endpoint
`3685a977ee5be31f5939c83617f1264e2d9ddaab`, upstream
`8bea45dc2aa9aab4d8b279db11a8887c52c1108d`, and port snapshot
`84800425913f2e4f292ab74e85a0e74ccd7f6734`. B1/B2 is an ancestor of the PR
endpoint. Its merge base with upstream is
`e7cab7bfbcfdffa84045c99dbed1deb3846a3184`.

The machine-readable companion is
[`tests/MASTER_BRANCH_PRESERVATION.json`](tests/MASTER_BRANCH_PRESERVATION.json).
It records hashes and dispositions for all 87 paths in the original branch's
contribution relative to that merge base: 62 additions, 23 modifications and
two deletions. It also lists every original path absent at the PR endpoint.

| Comparison of the 2,833 original tracked files | Identical | Modified | Absent |
| --- | ---: | ---: | ---: |
| PR endpoint | 1,990 | 578 | 265 |
| Upstream `8bea45dc2` | 1,405 | 1,131 | 297 |
| Port snapshot `848004259`, before this recovery | 1,436 | 1,130 | 267 |

All 265 paths absent at the PR endpoint are also absent at upstream. Many are
intentional upstream cleanup, so restoring every deleted path would undo current
master. Modified upstream files outside the original 87-path contribution have
been compared by blob, not semantically reviewed line by line. These counts
establish source presence, not behavioral equivalence.

### Benchmark tooling recovered

Commit `848004259` recovered seven scripts. This follow-up recovers the two
remaining sweep drivers, `scripts/run_vc_sweep.sh` and
`scripts/run_cva6_vc_sweep.sh`, plus eight committed CVA6 wrappers:

- `cva6_compressed_decoder_gate`
- `cva6_controller_gate`
- `cva6_csr_buffer_gate`
- `cva6_instr_realign_gate`
- `cva6_instr_scan_gate`
- `cva6_pmp_gate`
- `cva6_ras_gate`
- `cva6_raw_checker_gate`

All eight wrapper files are byte-identical to B1/B2. Comparing merge commit
`3685a977` with its first parent `61fd1646c` shows all 17 recovered paths
being deleted at that boundary. This identifies where the loss becomes visible;
it does not assign intent.

The recovered drivers need small adaptations before their results are credible:

- Derive the active checkout from the script location and require an explicit
  read-only `COREET_ROOT`; use project-local temporary files by default.
- Require successful exporter exit status before accepting an artifact. A stale
  file left by an earlier run cannot turn a failed rerun into `EMITTED`.
- Feed only the current CVA6 success list into the proof queue, and stop that
  queue if static certificate gates fail.
- Require the proof queue's explicit `PROVEN` verdict when writing the CORE-ET
  report; exit zero alone can accompany a failed axiom gate.
- Record silent nonzero elaborator exits, including timeouts, as failures.
- Write new sweep results under the runtime output directory, preserving the
  committed historical sweep tables.

Validation: all eight wrappers compiled successfully with the current frontend
using the user-authorized read-only CVA6 inputs. This checks wrapper/frontend
compatibility only, not Lean proofs. The self-contained
`python3 pass/lean/tests/sweep_tooling_check.py` check passes five driver
scenarios covering stale artifacts, failed axiom gates, silent timeouts and
successful controls. Its fake tools never access sibling benchmarks. Shell
syntax and whitespace checks also pass.

### Lean preservation and deliberate replacements

All nine original `formal/lean/LeanSemanticPrimitives/Compiler/*.lean` modules
are byte-identical to B1/B2. `Translation/GraphRefine.lean`,
`Translation/LGraphModel.lean`, and the two original compiler probes are also
unchanged. `Translation/OpBridge.lean` has 40 added lines and no deletions:
`evalNode_bridge`, `evalNodeC_bridge`, `slt_widths_bridge`, and
`sgt_widths_bridge`.

The absence of `design_cert_export.hpp` and `verified-compiler.{cpp,hpp}` is
intentional: their responsibilities moved into the shared scanner, certificate
IR/builder, memory lowering and certificate emitter. The JSON ledger maps these
replacements. That mapping is not a substitute for the adapter equivalence and
benchmark gates elsewhere in this document.

The original native fix in `6330d2aac` must also be interpreted against current
upstream. Raw-D latch bypass is now handled by
`graph/latch_contract.cpp:latch_transparent_arm`, which returns `din` after hold
canonicalization; upstream commit `87b0b91a2` added this behavior. The current
single-edge phase guards remain relevant. The clock-forest shadow fix survives
under `canonical_top_name`, and the `<print>` include remains. The original
encoder and simulator changes were shadowed-variable renames. This recovery
changes no native LEC, graph, single-edge, or simulator implementation.

### Unresolved Isabelle losses found by the broader comparison

These are outside the Lean tooling recovery and remain open for a separate
restoration/compatibility decision:

| Area | Original B1/B2 progress | State at the port snapshot |
| --- | --- | --- |
| Binary-tree library | `Translation_BT.thy`, introduced by `b839fb6b7`, with reusable lookup/key lemmas and a ROOT session entry | Theory and ROOT entry absent; some tree machinery survives only inline in the bakeoff generator |
| Bridge generation scaffolding | `emit_fast_bridge` option, structured certificate-node data, per-node fast-value definitions | Removed from `pass/isabelle/pass_isabelle.{cpp,hpp}`; this was scaffolding, not a completed full-design bridge theorem |
| Widening arithmetic shift | Fast emitter uses `scast` on `sem_sra` result | Reverted to `ucast`: widening a negative shifted value zero-extends instead of sign-extending |
| Signed-shift proof progress | `sint_word_of_int_fits`, explicit stale-lemma label and remaining-obligation notes | Helper and notes removed; the width-conditioned `ucast` lemma is again named `sra_bridge` |
| Synthetic bridge scaling | `203ddf33e` removes unused `wf_distinct` and splits the combiner before applying directed per-node rules | `wf_distinct by simp` and the single large combiner `simp` are back |
| Audit evidence | Recorded scaling measurements, oracle/trust-base analysis, signed-SRA bug explanation | 176 lines removed from `pass/isabelle/BRIDGE_BUGS.md` |

For the shift, a four-bit result `1110` represents −2. Widening with `scast`
gives eight-bit `11111110`; `ucast` gives `00001110` (+14). This is a concrete
semantic distinction in the Isabelle exporter, not a new change to the Lean
primitive semantics. The current `sra_bridge` theorem still requires output
width no larger than input width; its presence does not validate widening.

The scaling regression is established by source comparison against the recorded
fix, not by a new full-size Isabelle timing run. The lost notes themselves were
careful that the old 4,912-node measurement was a synthetic chain, not a DINO
proof. Upstream has also changed Isabelle's graph API, memory stride, Get_mask
and Concat handling, so replacing the exporter wholesale with the old file would
lose newer work. Restore individual features only after reviewing those changes.

This audit closes the benchmark-file inventory gap it identified. It does not
establish complete preservation of all development branches or runtime-only
configurations. Full DINO and corpus proofs, the remaining Hotmux value/status
gates, and the approved nonzero-divisor Rem work remain separate acceptance
items. In particular, no wrapper compile or source hash is counted as a
certificate equivalence proof.

### Hotmux oracle harness follow-up

The previously built `design_scan_test` binary did not yet contain
`DesignScan.HotmuxValueOracle`: filtering for that name ran zero tests and exited
zero. The script checked for a fixture but did not remove an old one first, so a
rerun could have accepted a fixture from an earlier binary. The harness now
requires a newly generated, nonempty fixture with value cases, always asks Lake
to check the imported module's freshness, and prints `PASS` only after all its
checks finish. No operator or certificate semantics change here.

After rebuilding `//pass/lean:design_scan_test`, all 15 scanner tests pass and
the real oracle test emits all five expected value cases. Separately,
`python3 pass/lean/tests/hotmux_oracle_check.py` passes five self-contained
harness cases: missing test output with a stale fixture, empty case list, a
`sorry` warning, failed Lean elaboration, and a valid control. These harness and
C++ results do not yet claim that Lean decided the five real vectors; that still
requires completion of the shared `CompileDesign` dependency build and the
actual oracle invocation.

### Approved Isabelle restoration: library, scaling and signed shifts

Following review of the broader audit, the user approved restoring the Isabelle
losses in separate commits. This first restoration recovers
`Translation_BT.thy` and its session entry, the original signed-shift proof
helper and unfinished-proof notes, the directed bakeoff combiner and removal of
unused `wf_distinct`, and the missing 176 lines of historical audit evidence.
Those recovered files match B1/B2. The C++ emitter restores only the approved
`scast` result cast for SRA; current graph traversal, constant representation,
Get_mask/Concat handling and native LEC remain unchanged.

Measured with `/usr/bin/time -v`, using an isolated project-local Isabelle user
home, heaps and temporary directory:

| Check | Result | Wall time | Peak RSS (KiB) |
| --- | --- | ---: | ---: |
| Semantic primitives and full translation library, including recovered theories | PASS | 66.00 s | 2,147,352 |
| Restored synthetic bridge generator, 32 nodes (`eqns`) | PASS | 12.60 s | 1,191,892 |
| Fresh emitted four-to-eight-bit SRA model and certificate, four concrete value checks | PASS | 11.69 s | 1,160,720 |
| Same generated model changed back to `ucast` as a negative control | Expected FAIL on both negative inputs | 11.16 s | 1,166,972 |

The new `//pass/isabelle:emission_smoke` fixture emits the model, certificate and
`RestoreShiftOracle.thy` when run with `ISABELLE_EMISSION_FIXTURES` pointing to a
project-local directory. It covers −4, −8, +6 and zero shifted right by one,
then widened to eight bits. The negative control fails on −4 and −8; this pins
values rather than just emitted spelling. The value facts use Isabelle `eval`
and therefore carry its code-generator oracle dependency, as the recovered
historical notes explain. These are targeted restoration checks, not new DINO
or full-corpus Isabelle proofs. Fast-bridge scaffolding is restored separately.

### Isabelle handoff; return to Lean

The approved Isabelle restoration and its measured results are recorded in
[`../isabelle/BRIDGE_BUGS.md`](../isabelle/BRIDGE_BUGS.md#master-migration-audit-and-restoration-checkpoint).
The final generated session passes in 15.24 seconds with 1,158,280 KiB peak RSS;
it covers the recovered synthetic bridge, signed-shift value regressions,
combinational/sequential bridge scaffolding, executable small-certificate WF,
and a concrete sequential fast/certificate equality. The original zero-extending
shift fails the negative control as expected. Both C++ test targets pass.

Two inherited WF execution/proof issues were fixed while validating the
restoration: large natural-number IDs require `Code_Target_Nat`, and the final
WF result needs direct use of its existing soundness lemma after rewriting the
ID-list equality. Neither changes certificate or node semantics. Default
non-bridge `cert_wf=skip` fixture output remains unchanged.

The user requested that further work now focus on Lean. The recorded Isabelle
limitations remain explicit: no completed general emitted fast/certificate
bridge, no completed unrestricted signed-SRA lemma, no complete Isabelle
chunked-WF proof, and no new full DINO/corpus Isabelle proof claim.

### R10. Target directory structure for the landed programme

Once every branch is on master (R1), the code should be reorganized so each
direction owns a directory and the shared layer is visibly shared. The four
directories already agreed are `IR-semantics`, `Futamura-projection`,
`verified-compiler` and the common `certificate-IR`. Five more earn their own
directory, and each is argued from something this port actually hit rather than
from taste.

#### The additions

**`graph-adapter/`** — `graph_access.*`, `design_scan.*`, `memory_lowering.*`

The strongest case. This is the ONLY layer that touches HHDS, and every single
upstream break in this port landed inside it:

| upstream change | adaptation |
| --- | --- |
| graph iteration API | `fast_class()` -> `body().nodes()` |
| constants moved to pool pins | `is_const_pin()` -> `pin.is_const()`; `hydrate_const` -> `const_of` |
| signed-width accessor | `get_bits()` -> `get_signed_bits()` |
| one driver per sink pin | `inp_edges()` + sort -> `inp_pins_snapshot()`; operand BANKS |
| `Ntype_op::Nconst` removed | constants are pins, not nodes |
| Memory pin 11 renamed | `init` -> `initial` |
| Get_mask/Set_mask | mask value -> half-open `[lo, hi)` endpoints |
| async reset wrappers | one-bit `Sext` transparency (R8/step 4) |

Eight adaptations, one directory. As a folder this makes "master moved" a
single place to look, and it is where the fresh-graph operator census belongs.

**`legacy-model/`** — the five `emit_legacy_*` units, `legacy_model.*`, plus
`Translation/{FastModelBridge,GraphRefine,LegacyCertWF}.lean`

The largest single mass, and the thing `compileDesign_correct` exists to
retire. The CORE-ET census makes the case concrete: **all 15 lean-emit failures
across 122 modules were the legacy fast bridge**, naming operators the model
already supports (`Op_And` x5, `Op_Xor` x5, `Op_Sum 3` x2, `Op_Sum 16`,
`Op_Sum 0`, `Op_Mult`). That is a failure mode entirely disjoint from the
verified-compiler path, which does not use the bridge at all. Two subsystems
with disjoint failure modes should not share a directory, and isolating this one
makes its eventual removal a directory delete.

**`lgraph-semantics/`** — `Translation/LGraphModel.lean`, `OpBridge.lean`

Separate from `certificate-IR` because they answer different questions:
certificate-IR is HOW a design is encoded, this is WHAT an operator means. All
four branches carry a byte-identical 29-constructor `LGraphOp`. It is the file
that must never fork, and burying it inside the certificate layer hides that.

**`translation-validation/`** (D3) — `Reify`, `ReifyGen`, `ReifyProof`, `D3Harness`

**`runtime-transport/`** (D4) — `CertIO`, `CertIORoundTrip`

D4 especially: its parser is recorded as TRUSTED. A trust boundary should be a
directory, not a sentence in a markdown file.

#### Two more that are not directions

**`isabelle-bridge/`** — `pass/isabelle/*`, `formal/translation_correctness/*`

Now in scope: the Isabelle side was restored onto master alongside the Lean
work. It is a second proof backend over the same graph, so it belongs beside
the Lean directions rather than inside them.

**`benchmarks/`** — `SWEEP_*.tsv`, `tests/LEGACY_PROOF_*.json`,
`MASTER_BENCHMARK_MANIFEST.json`, `MASTER_OP_CENSUS.json`, the `run_*.sh` and
`coreet_*` drivers

Recorded evidence about designs currently sits in `pass/lean/tests/` next to
gtest unit tests. They have opposite lifecycles -- unit tests are source,
evidence is dated measurement -- and mixing them is part of how five
inventories drifted into disagreeing: the manifest found **178 of 256 entries
listed by only one inventory** (R5).

#### What must NOT be split

`DesignCert.lean`, `Runtime.lean` and `DesignSemantics.lean` stay in exactly ONE
place in `certificate-IR`. The evidence is the divergence this port already
has: the landed schema has no `clocks`, while D2's declares `ClockDesc` and
`clocks : Array ClockDesc := #[{ name := "clock" }]`. That happened BECAUSE each
branch kept its own copy. A layout giving each direction a private `DesignCert`
would institutionalize it.

Likewise `lean_options.*` and `pass_lean.cpp` stay as the single EPRP adapter;
per-direction option parsing is how modes drift apart.

#### The dependency rule, which is the point of the layout

```
graph-adapter -> certificate-IR -> { verified-compiler, legacy-model,
                                     runtime-transport, isabelle-bridge }
                       ^
               lgraph-semantics      (depended on by all, depends on none)
```

`IR-semantics` and `Futamura-projection` consume `certificate-IR` and
`lgraph-semantics`; neither may reach back into `graph-adapter`.

One enforceable check is worth more than the diagram: **no emitter may include a
graph-adapter header.** That follows sections 3 and L3-L8 of
PASS_LEAN_RESTRUCTURE_PLAN.md, and as a
directory boundary it is mechanically checkable in CI rather than aspirational.

#### Sequencing

This reorganization happens AFTER the branches land (R1 step 4) and AFTER the
benchmark replay, not before. Moving files while D2/D3/D4/Futamura are still
rebasing would force every one of them to re-target paths mid-flight, and a
replay must identify the inputs and implementation it validates. Keep movement
separate from semantic changes and record path mappings. Lean module moves may
require import and BUILD changes, so do not promise a compiling content-free
rename. Preserve history where possible and rerun the affected proof gates.


## Fresh replay and raw census checkpoint (2026-10-08)

The complete input matrix is now committed in
[MASTER_REPLAY_CONFIGURATIONS.json](tests/MASTER_REPLAY_CONFIGURATIONS.json):
125 CORE-ET and 61 CVA6 configurations, including the separate historical legacy
exports and configured CVA6 wrappers. It reconciles the per-scope historical
inventory; all 125 distinct CORE-ET tops are represented. External filelists
are identified by logical root, relative name and hash. This was a one-time,
explicitly authorized read-only benchmark replay, not a repository test that
searches sibling checkouts.

Fresh exports used frozen binaries built from the production sources at
`5129e8c07`, with the preserved readers, filelists, normalization and options.
These are **export results**, not accepted Lean proof results:

| Family | Configurations | Reached Lean export | Compile failures | Normalization refusals | Verified exports | Fast exports | Bridge exports / refusals |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| CORE-ET | 125 | 81 | 20 | 24 | 81 | 81 | 37 / 26 |
| CVA6 | 61 | 52 | 9 | 0 | 43 | 52 | 8 / 1 |

All three DINO designs separately export in verified-compiler, legacy-fast,
full chunked-WF, and bridge-plus-full-WF configurations. WF uses chunk size 100,
no chunk limit and fail-on-unsupported fallback. The fresh timed Lean proofs
remain pending at this checkpoint. Earlier saved-artifact proofs do not replace
these new runs.

[MASTER_OP_CENSUS.json](tests/MASTER_OP_CENSUS.json) inspects original graph
operators before lowering, including graphs subsequently refused by normalization.
It records 160 observed configurations (including DINO) and 29 unobserved compile
failures. Every operator has width/sign/arity counts; memory policy pins and
native one-hot attributes are retained. Unobserved graphs do not count as zero
occurrences. Reproduce a graph census with `//pass/lean:lean_graph_census` and
`scripts/raw_graph_census.py --tool ... --graph ... --top ... --compile-log ... --output ...`.
The three DINO graph directories were hashed before and after inspection and
were unchanged.

This corrects the earlier claim that CORE-ET had no Hotmux occurrences. A
successful lowering does not issue an unsupported-operator refusal. The raw
CORE-ET graphs contain 367 Hotmux nodes, and CVA6 contains 394. DINO's SingleCycle,
Pipelined and DualIssue graphs contain 2, 5 and 41 respectively. Native formal
checking proved 1, 4 and 40 DINO obligations, leaving one deferred in each design.
No observed graph contains Rem, LUT, Rxor or Popcount; no conclusion about those
operators is drawn for the 29 compilations that failed.

The census distinguishes native-proved, deferred, refuted, unreported and an
unclassified runtime-check status. Upstream stores the same `runtime_check`
attribute for deferred and refuted Hotmux checks, so graph attributes alone
cannot distinguish them. Classification uses the matching compilation diagnostics;
a native refutation remains a failed compilation. None of these statuses claims
that Lean proved one-hotness.

All 27 refused bridge cases have historical certificate artifacts available.
The dispatch limit is inherited: both the B1/B2 and refactored emitters handle
And arities 2/3/4, binary Xor, and the binary Sum add/subtract forms. Fresh graphs
now exercise larger arities or unary negation. For example, `debug_breakpoint`
changes from And arities 2/3 to 2/6; `intpipe_alu` adds a three-input Sum;
`legacy_cva6_alu_export` adds unary Sum with zero addends. General bridge proof
support is being investigated while keeping both models' evaluation rules fixed.

Four previously proven CORE-ET configurations (`minion_dcache_cache_op_unit`,
`minion_dcache_cache_op_unit_l2`, `minion_dcache_reduce`, `vpu_trans`) are now refused
by upstream clock-gate latch-phase normalization. `minion_dcache_tensor_load`
fails upstream Concat lane-width validation before Lean. Their native source
implementations match pinned master; a separate pristine-master binary comparison
is in progress. No native LEC, frontend, normalization or warning-policy change
is made to bypass these failures. The migration is not ready to publish yet.


### Completed proof prerequisites and upstream reproduction

The fresh serialized library build succeeds (8,571 targets), including
`CompileDesign`, `LegacyCertWF` and `OpBridge`. All 21 concrete Hotmux value
checks pass: the original five control/priority vectors plus 16 checks covering
variable signed and unsigned arms, signed defaults, implicit zero, minimum
negative values and overlapping controls. These `native_decide` checks retain
the explicit `ofReduceBool` trust dependency. Six local harness checks reject
stale, absent, incomplete, failed or sorry-bearing fixtures before success.

[MASTER_UPSTREAM_FAILURE_REPLAY.json](tests/MASTER_UPSTREAM_FAILURE_REPLAY.json)
records the completed pristine `8bea45dc2` comparison. All 29 compilation failures
and 24 normalization refusals reproduce with matching exit codes and error
categories, using the same reader, RTL filelists, flags and normalization
procedure. Both sides freshly compile the RTL; neither invokes Lean in these
failure reproductions. The separate pristine build succeeded without source
changes. These failures are therefore independent of the Lean refactor.

[MASTER_BRIDGE_ARITY_AUDIT.json](tests/MASTER_BRIDGE_ARITY_AUDIT.json) records
all 27 bridge refusals and their historical/current node shapes. All 27 old
artifact hashes match their committed historical inventories. Draft general
mixed-width And/Xor/Sum bridge theorems have now typechecked against the fresh
library, using only `propext`, `Classical.choice` and `Quot.sound`; emitter
integration and complete block replay remain outstanding. This extends proof
coverage without changing either model's evaluation rules.

The three DINO designs and accepted CORE-ET/CVA6 exports are now undergoing fresh
timed proofs. The initial broad native regression command accidentally discovered
the nested temporary baseline checkout; that run was stopped and retained as a
harness failure, then restarted over all 67 tracked Bazel packages while honoring
the existing ignore rules and test filters. Its result is still pending.


### General fold bridge integration

`Translation/NaryBridge.lean` proves mixed-width And/Xor folds and Sum with any
add/subtract split, including unary negation. The emitted fast model and
`eval_op` are unchanged. Existing small-arity bridge dispatch stays in place;
only previously unhandled shapes select the new lemmas. Generated files import
the additional module only when those proofs are needed. The benchmark queue
builds both this module and `LegacyCertWF` explicitly. Its static census also
recognizes the general folds and the existing unequal-width signed comparisons.

[MASTER_NARY_BRIDGE_RESULTS.json](tests/MASTER_NARY_BRIDGE_RESULTS.json) records
11 self-contained emitted fixtures with full WF and all-input bridge proofs:
1/6/17-input And, 1/3/17-input Xor, unary negation, 3/16-input addition, mixed
addition/subtraction, and all-subtract Sum. Inputs mix widths 17/1/3/9 and outputs
exercise both narrowing and widening. Run `scripts/nary_bridge_check.sh` with
`TEST_BIN` pointing to the built `legacy_model_test` binary. The general lemmas
use only the standard `propext`, `Classical.choice` and `Quot.sound` axioms;
concrete graph/WF checks retain the established native-decision trust boundary.

All 314 previously successful exports were regenerated and remain byte-identical.
All 27 former arity refusals now export, and their full block bridge proofs are
in progress. This is a proof-coverage extension, not a change in model values.
A focused fixture initially exposed missing concrete list-slicing rewrites in
the emitted Sum proof; selecting `take_succ_cons`/`drop_succ_cons` fixed the tactic,
and all 11 fresh fixtures then passed. No failed proof was accepted.

The full optimized Linux port regression passed in 804 seconds: 4,672 passed,
two skipped (`lhd_usyn_noabc_smoke` and `cone_unavailable_test`), exit zero.
After the general-fold emitter change, all three Lean C++ targets pass again,
and the CLI/exporter binaries build. The clean pristine-master audit checkout
has been moved outside this repository so ordinary `//...` searches cannot
rediscover its build files.


### Complete fresh DINO proof measurement

[MASTER_DINO_REPLAY_RESULTS.json](tests/MASTER_DINO_REPLAY_RESULTS.json) records
all twelve successful fresh jobs. Each design was regenerated from RTL, and
all files were reproduced byte-for-byte by the general-fold emitter. WF uses
all chunks of size 100 with no fallback. The bridge run also checks whole-graph
WF and audits all three `comb_refines_fast`, `next_refines_fast` and
`step_refines_fast` theorems. No missing audit or `sorryAx` was accepted.

| Design | Verified compiler seconds / GiB RSS | Fast typecheck seconds / GiB RSS | Full chunked WF seconds / GiB RSS | Full WF + bridge seconds / GiB RSS |
| --- | ---: | ---: | ---: | ---: |
| SingleCycleCPU | 21.4 / 6.43 | 14.0 / 0.75 | 42.6 / 6.92 | 517.2 / 8.00 |
| PipelinedCPU | 23.6 / 6.48 | 23.1 / 0.80 | 61.5 / 7.07 | 541.8 / 8.79 |
| PipelinedDualIssueCPU | 38.6 / 6.75 | 133.9 / 1.33 | 180.8 / 8.09 | 1044.4 / 12.12 |

Measurements use `/usr/bin/time -v`. Designs run concurrently, with each design's
four modes sequential and eight Lean threads per process; the corpus uses a
separate CPU allocation. The full-WF result establishes structural certificate
well-formedness. The bridge result separately establishes fast/certificate
model equivalence. Neither establishes RTL-to-graph correctness or the one
native-deferred Hotmux exclusivity obligation in each design.

A read-only remote check still reports upstream master at `8bea45dc2` after
these measurements. CORE-ET/CVA6 block proofs remain in progress; DINO completion
does not stand in for that separate matrix.

### Refreshed preservation snapshot after DINO and general-fold proofs

[MASTER_CURRENT_PRESERVATION.json](tests/MASTER_CURRENT_PRESERVATION.json)
refreshes both earlier source ledgers at `bb7e96670` without replacing their
historical snapshots. All 117 PR-change paths and all 87 original B1/B2
contribution paths have a current hash and disposition. Of the PR paths, 96
remain byte-identical to the PR endpoint; the remaining paths retain explicit
upstream adaptations, added validation, or the approved lowering/proof work.
Sixteen paths changed after the initial port ledger snapshot and are explained
individually. The 17 recovered benchmark scripts/wrappers remain present.

All 22 formal Lean files from the PR are still byte-identical, including the
primitive model, certificate vocabulary, and compiler correctness proofs.
All nine Compiler modules also match the original B1/B2 branch. `NaryBridge`
is an additional proof module. Every native LEC file other than the previously
approved linker-smoke BUILD/test changes matches upstream; that smoke test itself
matches the PR. The audit also checks 3,689 upstream graph, frontend,
normalization, contract-named and warning-policy files for exact retention.
This source check does not claim semantic equivalence for arbitrary upstream
changes, and it does not include the later D2/D3/D4/Futamura migrations.

The earlier seven unresolved Isabelle source losses have separate restoration
commits and a recorded checkpoint. General Isabelle fast/certificate equivalence
remains incomplete; no additional Isabelle work is included here. Rechecking
677 distinct recorded benchmark input files found no missing or changed files
and no conflicting recorded hashes. CORE-ET/CVA6 proofs remain in progress.

### Universal one-bit reset-wrapper condition audit

[SextCondition.lean](tests/legacy_semantic_audit/SextCondition.lean) proves that
the emitted Sext expression preserves bit zero, zero/nonzero, and both reset
polarities for every one-bit source, positive sign position, and positive
result width. All four theorem audits use only `propext`, `Classical.choice`
and `Quot.sound`; the file typechecks without warnings or `sorryAx`.

This also corrects a comment in the adapter: only sign position one replicates
the source bit across the result. A larger sign position zero-extends that
one-bit value before sign extension, preserving its Boolean condition without
replicating every bit. The adapter code and certificate/model semantics are
unchanged. Wider sources, zero amounts and dynamic amounts remain refused by
the tracing rule's existing negative tests.

### Bounded legacy records: a newly exercised inherited limitation (historical nested layout)

This checkpoint used the subsequently withdrawn nesting workaround.

The fresh `txfmactl_top` legacy-fast check failed because its 289-field state
record exceeds Lean's executable constructor capacity. Its historical legacy
scope was export; the older PROVEN label described the verified-compiler path.
Both the original B1/B2 exporter and the PR schema emitter emitted unbounded
flat records. The current corpus also contains five CVA6 state records with
256, 534, 282, 409 and 326 fields, respectively. This failure does not implicate
native LEC or a changed node interpretation.

A separate minimal execution experiment exposed a stricter practical issue:
in the pinned Lean 4.31 interpreter, reading a default field from a flat
128-field Nat or BitVec record terminated with signal 11, whereas a 64-field
record executed. The temporary schema emitter bounded physical records at 64 fields
using inherited chunks. Field names, order, widths and values remain intact.
Generated helper types, positional `.mk` constructors and `Repr` nesting change;
clients should use the preserved named fields and record-literal syntax.
Certificate IR, primitive semantics, and fast-model operation expressions are
unchanged.

[MASTER_RECORD_LAYOUT_RESULTS.json](tests/MASTER_RECORD_LAYOUT_RESULTS.json)
records six fresh fixtures. The 64/65 and 255/256 boundaries and a 534-field
mixed-width fixture prove exact input/output/state conversions in both directions
by `rfl`, every next-state field for arbitrary inputs, and executable native
checks. The 256-field fixture also proves full WF and all three bridge theorems.
A separate fixture checks function-valued memory fields and updates. No missing
audit or `sorryAx` is accepted. The first prototype retained a large flat boundary
and reproduced the interpreter crash; a separate fixture initially lacked its
helper lemma import. Both failed runs are retained, and all final fixtures pass.

Regenerating all 341 accepted artifacts leaves 319 byte-identical. The remaining
22, across 16 configurations, change only their structure declarations: all
ordered fields and the entire following function/certificate/proof text match.
All 127 verified-compiler exports are byte-identical. Every affected artifact is
being checked again, including DualIssue's three legacy modes; previous DINO
measurements remain a historical checkpoint until that reproof finishes.

The exact isolated original state declarations refine that diagnosis: the five
records **above** 256 fields reproduce the compiler error, while BHT's exactly
256-field declaration alone typechecks. BHT is part of the broader execution
repair, not a reproduced compiler refusal. Four obsolete whole-file attempts
were interrupted after those exact declaration failures were reproduced; their
logs, timing and interruption reasons are retained. This does not count as a
completed old-model proof. The corrected whole-model checks continue separately.

### Large Or proof scaling without model changes

The full corpus exposed long node proofs where the existing Or simplifier tried
to match `BitVec.zero_or` against large dependency expressions. The new
`Translation/OrBridge.lean` eliminates the initial zero in a general theorem
before those expressions are substituted. The emitter selects it above four
operands, retaining the existing small-arity proofs and empty-fold behavior.
Neither the fast expression nor the certificate operation changes.

[MASTER_OR_PROOF_RESULTS.json](tests/MASTER_OR_PROOF_RESULTS.json) records the
exact 44-arm, one-bit node from `intpipe_decode`: the old proof timed out at
600 seconds; the new proof passed in 90.20 seconds with 7,573,664 KiB peak RSS.
The same declaration prefix alone took 51.19 seconds. These are isolated node
measurements, not a claim that the full block finished. The theorem uses only
standard Lean axioms. All 14 emitted fold fixtures pass full WF and universal
bridge checks, including new 6/44/55-arm Or fixtures with mixed input widths and
8/1/760-bit outputs.

Regenerating all 341 artifacts leaves 292 byte-identical. Exact reconstruction
of the other 49 deltas confirms that they contain only the extra import and
equivalent Or node-proof rewrites. All model and certificate definitions are
identical. Full reproofs of all 49 changed bridges are in progress, with the
three slow corpus blocks and DINO prioritized. The three Lean C++ targets and
CLI/exporter build pass; shell/Python syntax and whitespace checks pass.

### Portable historical inputs

The final privacy scan found personal absolute paths inherited by restored Lean
wrappers and historical notes. The notes now name their external source document
or use explicit worktree variables. CVA6 runners require `CVA6_ROOT`; the DINO
runner accepts `HAGENT_BUILD` or three explicit RTL directories. Tool lookup
uses `PATH`, the supplied `BENDER`, and user-relative Elan/Bender locations.
No RTL selection, frontend flags, normalization, or proof procedure changes when
the same input paths are supplied. These are optional benchmark runners, not
authorization for default tests to inspect sibling repositories.

### Historical successes that no longer reach Lean on master

Nine configurations carrying a historical PROVEN label stop before the Lean
pass. Each failure reproduces with the same exit and diagnostic category on
pristine `8bea45dc2`; the full 53-case baseline comparison is in
[MASTER_UPSTREAM_FAILURE_REPLAY.json](tests/MASTER_UPSTREAM_FAILURE_REPLAY.json).
The historical labels have different scopes and are retained individually in
[MASTER_REPLAY_CONFIGURATIONS.json](tests/MASTER_REPLAY_CONFIGURATIONS.json).

| Family | Configuration | Fresh upstream failure |
| --- | --- | --- |
| CORE-ET | `minion_dcache_cache_op_unit_l2` | Clock-gate latch-phase refusal |
| CORE-ET | `minion_dcache_cache_op_unit` | Clock-gate latch-phase refusal |
| CORE-ET | `minion_dcache_reduce` | Clock-gate latch-phase refusal |
| CORE-ET | `minion_dcache_tensor_load` | Concat driver wider than its declared lane |
| CORE-ET | `vpu_trans` | Clock-gate latch-phase refusal |
| CORE-ET | `prim_write_commit_en` | Clock-gate latch-phase refusal |
| CORE-ET | `prim_phase_pair_lo_hi` | Clock-gate latch-phase refusal |
| CVA6 | `pmp_data_if` | Combinational-loop detection |
| CVA6 | `legacy_cva6_tlb_gate` | Combinational-loop detection |

The clock-phase guard was introduced by upstream `0d26c1d63` to reject
unsound latch lowering; the Concat lane guard comes from `dc07e3242`. This replay
preserves both checks. The two combinational-loop failures are reproduced
upstream, but their complete frontend root causes are not diagnosed here.
None of these nine is reported as a current successful Lean proof. In
particular, the historical `minion_dcache_reduce` PROVEN label described the
verified-compiler procedure, while its legacy scope was only export.

The DINO wrapper additionally checks that its optional native gate and Lean
export use the same RTL directories. The existing gate accepts only a common
root with the standard three names, so custom directory overrides require the
already available explicit `RUN_LEC_GATE=false` mode. Mismatches fail before
invoking the gate. This prevents an unset common root from selecting the native
script's historical default while Lean reads custom inputs. The native gate and
solver are unchanged; its current behavior rejects inconclusive results too.

### Final DINO artifacts: all twelve checks complete (historical nested layout)

This checkpoint used the subsequently withdrawn nesting workaround.

The final bounded-record and Or-proof emitter has now passed every DINO mode.
[MASTER_DINO_REPLAY_RESULTS.json](tests/MASTER_DINO_REPLAY_RESULTS.json) retains
the initial twelve-job measurement as a historical checkpoint and records the
current artifact hashes and proof attempts separately. Full chunked-WF plus
comb/next/step bridge results are:

| Design | Wall seconds | Peak RSS GiB |
| --- | ---: | ---: |
| SingleCycle | 448.670 | 7.779 |
| Pipelined | 505.790 | 8.797 |
| DualIssue | 1,092.794 | 11.840 |

Every WF chunk is included; no fallback, missing theorem audit, failed Lean exit
or `sorryAx` is accepted. The final `intpipe_decode` whole-block bridge also
passes in 1,602.71 seconds; its previous proof passed in 4,586 seconds. These
shared-host runs have different recorded scheduling contexts, so the measured
improvement is not an isolated performance experiment. Corpus reproofs remain
in progress at this checkpoint; DINO completion does not mark them complete.

### Record-layout diagnosis and completed reproof (historical nested layout)

This checkpoint used the subsequently withdrawn nesting workaround.

The bounded-record repair has now passed all **22 affected artifact checks**
across 16 configurations, including DualIssue's legacy model, full chunked WF,
and complete fast bridge. The
[record-layout evidence](tests/MASTER_RECORD_LAYOUT_RESULTS.json) records the
current artifact hashes, proof audits, measured runtime/RSS, and earlier
attempts. Two other CORE-ET bridge checks remain in progress; they are separate
from this completed layout reproof.

A fresh standalone reproduction distinguishes the Lean execution problem from
our emitter's compatibility gap. With no LiveHD or Mathlib imports, a structure
of 64 `Nat` fields or 64 `BitVec 8` fields executes
`#eval (default : T).f0`; each corresponding 128-field structure terminates with
signal 11 on the pinned Lean 4.31.0 Linux release. A debugger backtrace of the
128-field `Nat` case faults in `mi_malloc_small` inside `libleanshared.so` during
`lean_eval_const`. This is evidence of a Lean-side execution defect. It does
not identify the precise upstream cause, establish which earlier versions are
affected, or imply that all execution backends fail.

The separately diagnosed compiler constructor-field limit is a deliberate
restriction, not the same failure as this interpreter crash. Both the original
B1/B2 emitter and the PR refactor emitted unbounded flat records. Bounding the
physical records addresses that inherited compatibility gap; it does not
change node semantics. Named fields and model values are preserved, while
positional constructors and `Repr` nesting change. The threshold of 64 is our
conservative tested layout choice, not a claimed universal Lean field limit.

### Upstream record-crash report and version comparison

The subsequent upstream search found
[Lean issue #14148](https://github.com/leanprover/lean4/issues/14148), filed on
June 22, 2026. It describes constructors exceeding mimalloc's direct-small
allocation limit. Our standalone crash's `mi_malloc_small` backtrace and the
unconditional allocator call in the installed 4.31.0 header strongly match
that report. A [maintainer comment](https://github.com/leanprover/lean4/issues/14148#issuecomment-4772436754)
identifies [PR #7786](https://github.com/leanprover/lean4/pull/7786) as addressing
it; that PR merged on July 15, 2026. The issue itself remained open when checked.
This is a source/backtrace match, not a patched-runtime bisect.

Fresh execution of the identical self-contained fixtures gives:

| Installed Linux toolchain | 64-field Nat / BitVec controls | 128-field Nat / BitVec reproduction |
|---|---|---|
| 4.26.0-nightly-2025-11-18 | Both pass | Both SIGSEGV |
| 4.31.0 (project pin) | Both pass | Both SIGSEGV |
| 4.32.1 | Both pass | Both SIGSEGV |
| 4.33.1 | Both pass | Both SIGSEGV |
| 4.34.1 | Both pass | Both pass |

The crash therefore predates 4.31. These are newly executed comparisons on
installed toolchains, not claims about earlier LiveHD proof logs. The 4.34.1
header selects a general allocator when the object exceeds the small-allocation
limit. The [record-layout evidence](tests/MASTER_RECORD_LAYOUT_RESULTS.json)
contains exact toolchain commits, fixture hashes and return codes. No project
pin or dependency changed, and a whole-project upgrade has not been validated.

The separate exactly-256-field metadata problem has its own upstream report,
[#15072](https://github.com/leanprover/lean4/issues/15072), closed by
[PR #15075](https://github.com/leanprover/lean4/pull/15075) on September 8, 2026.
It reinforces the earlier qualification that BHT's original 256-field
**declaration** typechecking is not evidence of safe execution. It is distinct
from the 128-field allocation crash. The temporary bounded layout avoided
both failures, but that representation change has now been withdrawn.

### Large Or proofs over abstract operands

Two remaining full CORE-ET bridges motivated a separate proof-scaling repair.
An isolated `vpu_mask` recurrence with 128 operands at width 128 spent 627.3
seconds in kernel declaration checking and 758.4 seconds overall. An isolated
55-operand, width-760 recurrence from `minion_dcache_miss_handler_unit` reached
the 900-second diagnostic limit. These diagnostics retained the actual generated
model/certificate definitions and prior declarations; they did not substitute
for full-block acceptance.

The emitter now proves each large Or expression over abstract `BitVec` operands
before applying that theorem to the circuit's actual dependency expressions.
It uses the same fast-expression builder, dependency widths and order, including
repeated dependencies. Source equalities retain their existing proofs. This
keeps circuit expressions out of fold simplification and its generated proof
term. The resulting recurrence has the original theorem statement. No primitive,
model, certificate, or well-formedness predicate changes.

The two isolated proofs then pass in 118.9 and 49.0 seconds respectively, with
only `propext`, `Classical.choice`, and `Quot.sound` in their axiom audits. These
are shared-host measurements including the common declaration prefix, not an
isolated performance claim. An alternative diagnostic that skipped executable
compilation of proof helpers did not resolve the long kernel check; that
alternative is not part of the emitter change.

All three focused C++ suites and all 14 mixed-width/fold Lean fixtures pass.
Across 341 fresh exports, 292 are byte-identical and 49 change only the new
abstract-operand lemmas and their node-proof applications. Exact reconstruction
checks preserve every model/certificate definition, original theorem statement,
and other proof. Full reproofs of those 49 artifacts, including all DINO bridges,
are in progress at this checkpoint. The earlier long attempts remain in the
audit history.

### Flat-record restoration and user-owned toolchain upgrade

The user rejected changing the generated representation to accommodate the
Lean runtime bug. `emit_legacy_schema.cpp` is restored byte-for-byte from the
parent of `c26c85f7f`; the two nesting-only C++ fixtures and their optional Lean
harness are removed. The census parser again reads the original flat schema.
The separate large-Or proof optimization remains intact. No primitive or
certificate semantics, dependency pins, Isabelle code, or native LEC code
change in this restoration.

The original 64/128-field standalone experiment and cross-version research are
retained in [MASTER_RECORD_LAYOUT_RESULTS.json](tests/MASTER_RECORD_LAYOUT_RESULTS.json).
Its 22 successful nested reproofs are explicitly historical. Upgrading Lean is
pending the user, with the matching Mathlib update and complete replay gates in
[plan section 13](PASS_LEAN_RESTRUCTURE_PLAN.md#13-user-owned-lean-upgrade-after-withdrawing-record-nesting).
The allocator repair alone does not remove the separate constructor-size limit;
full flat-model acceptance is not claimed for affected large records.

Restoration validation passes all three focused C++ suites and builds both
`lhd` and `lean_export_graph`. Regenerating all 341 accepted artifacts gives
319 byte-identical outputs and 22 schema-only restorations across 16
configurations. Every legacy schema exactly matches its pre-nesting output;
all ordered fields, function/certificate definitions and proof text are
unchanged. All 127 verified-compiler artifacts remain byte-identical. See
[MASTER_FLAT_RECORD_RESTORE_RESULTS.json](tests/MASTER_FLAT_RECORD_RESTORE_RESULTS.json).

The separate abstract-Or proof queue has 45 completed proofs and four attempts
without terminal results (`txfma_wallace1`, `txfma_wallace2`,
`legacy_cva6_alu_export`, and `legacy_cva6_pmp_gate`). No Lean process or replay
supervisor remains live at this checkpoint; stale RUNNING records do not count
as passes. The restoration report retains exact artifact hashes to distinguish
unchanged proofs from the withdrawn nested layout. This export comparison is
not a claim that the full restored flat-model proof matrix has passed.

### R11. Corrected CORE-ET census, scaling evidence, and the D2 scope boundary

#### R11.1 The earlier census was measured without its prerequisite

The 122-module census recorded earlier in this report ran while
`generated/core-et/patched_src/` did not exist. `coreet_filelist.sh` substituted
the slang-clean derived sources only when that map was present and SILENTLY used
the unpatched tree otherwise, so the sweep measured a corpus slang rejects for
defects `coreet_patch_srcs.py` already fixes.

Re-running the identical 122 modules with the map generated:

| stage | without patches | WITH patches |
| --- | ---: | ---: |
| `static-gates` READY | 38 | **77** |
| `compile` FAIL | 49 | **20** |
| `single_edge` FAIL | 19 | 22 |
| `lean-emit` FAIL | 15 | **1** |
| `static-gates` FAIL | 1 | 2 |

The earlier conclusion -- that the frontend was the dominant blocker and that
46 `yosys-failed` modules were step-5 investigation work -- was an artifact of
the missing prerequisite, not a property of the corpus. Any figure quoted from
the unpatched run is void.

`scripts/coreet_filelist.sh` now FAILS when the map is absent, with the command
to generate it, so the mistake cannot repeat silently.

#### R11.2 The two improvements have different causes, and they are disjoint

The runs are not a clean A/B: the binary changed between them. Separating the
causes matters, because crediting both to the patcher would misdirect the next
piece of work.

| improvement | cause |
| --- | --- |
| `compile` 49 -> 20 | the SOURCE patches (`coreet_patch_srcs.py`) |
| `lean-emit` 15 -> 1 | the BRIDGE commits, principally `31c1f00d9` |

The evidence for the split is that five of the recovered modules --
`intpipe_alu`, `txfma_csa`, `vpu_mask`, `debug_breakpoint`, `txfma_wallace1` --
contain ZERO patched files in their filelists, so no source patch could have
touched them, yet all five moved from `lean-emit FAIL` to READY. The commits in
the gap are `4c29167dd` (abstract large Or operands), `31c1f00d9` (mixed-width
n-ary legacy bridges) and `e19a33736`. The 15 failures named `Op_And`, `Op_Xor`,
`Op_Sum 3`, `Op_Sum 16` and `Op_Mult`, which is exactly the mixed-width n-ary
dispatch gap `31c1f00d9` closes.

One module still refuses: `tima_top`, `legacy fast bridge does not support
LGraphOp.Op_Mult at node 688`. That is the remaining bridge gap, and it is a
single named case rather than a statistic.

Unchanged across both runs: **ZERO operator refusals**. No `Hotmux`, `Rem`,
`LUT`, `Rxor` or `Popcount`, and the all-operators refusal message never fired.
R9.2's disposition stands on the patched numbers as well as the unpatched ones.

#### R11.3 The verified compiler has no flop ceiling; the legacy model does

Two Lean limits, both measured on this machine rather than inferred:

| limit | threshold | toolchains |
| --- | --- | --- |
| interpreter SIGSEGV evaluating a flat record | >= 128 fields | 4.31.0, 4.32.2, 4.33.1 broken; **4.34.0 and 4.34.1 fine** |
| `constructor ... has too many fields` | >= 257 fields | **every** toolchain, including 4.34.1 |

The bisection is exact: 127 fields evaluate, 129 segfault; 256 fields compile,
257 do not. Elaboration is not at fault -- the same 128-field structure
`#check`s cleanly and only `#eval` crashes -- so the first limit is in the
interpreter. An upgrade removes that measured crash, but does not make
arbitrarily large flat constructors executable. Declaration success at exactly
256 fields is not an execution guarantee; see R12's fresh boundary probes.

A legacy `<Top>_state` record includes one field per flop, alongside any other
state fields. Input/output and memory records also have constructors. The limit
is measured in fields of each actual record, not a general 256-flop ceiling.
Keep the flat representation and record affected limitations for the user.

The verified compiler has no such limit, and this is now demonstrated on a real
block rather than argued. `txfmactl_top`, **289 flops**, exported in
`verified_compiler` mode:

```
flopQ sources:       289      (past the 256 constructor ceiling)
flat state records:    0      (no `structure *_state` is emitted at all)
typecheck:          exit 0    57.7 s, 7.2 GB peak RSS, sorryAx 0
txfmactl_top_step_correct depends on: [propext, Classical.choice, Quot.sound, ...]
```

State lives in `RuntimeState` ARRAYS indexed by ordinal; array length is not
constructor arity, so there is no field-count limit to reach. This was the one
module above 256 flops in the corpus that the frontend can elaborate -- and it
only elaborates WITH the patches, which is how the two findings connect.

Note on attribution: the fix for the SIGSEGV is confirmed present in 4.34.0 by
measurement. leanprover/lean4 PR #7786 is titled "perf: upgrade to mimalloc 3",
a performance change rather than a declared bug fix; it is a plausible cause but
is NOT established as the fix by anything recorded here.

#### R11.4 D2 lands D2's work only

When `direction-2-ir-semantics-clean` lands (R1 step 2), its scope is the
CLOCK SCHEMA and its direct interpreter, and nothing else. Specifically:

IN scope -- `ClockDesc`, `DesignCert.clocks`, `FlopDesc.clock` and
`FlopDesc.asyncReset`, `MemoryDesc.clock`, `ClockEdges`/`fires`/`allEdges`,
`srcFlopNext`'s edge rule, the `Legacy` copies
and `interpretDesign_allEdges` that make the schema change meaning-preserving,
D2's own `Direct*` modules and validation tools, and fresh checker-driven
re-emission. The recorded 54/147 `asyncFlagMismatch` count is historical; rerun
the checker on the current corpus instead of assuming that count still holds.

OUT of scope -- the Futamura branch's `Projection/` tree and its partial
evaluator, D3's `Reify*`, and D4's `CertIO*`. D2's `DirectSim` and the Futamura
projected simulator are DIFFERENT simulators over the same certificate; landing
them together would make it impossible to tell which one a later result is
about, and the projection branch additionally pins `DesignCert` at a fixed
revision requiring an explicit compatibility migration after D2 lands.

The rebase must be SEMANTIC, not a replay: D2 is 133 commits ahead of its own
base and 335 behind current master, and it carries its own copies of
`Compiler/*.lean`. Port the type change onto the landed schema; do not replay
133 commits across a graph API that moved underneath them.


## R12. Current implementation and all-direction audit

Audit snapshot: `4342b312bb1aee60cde01e45b04a73de274b997c`.
A read-only remote query returned `94c5967c6` for master. The pristine upstream
comparison remains `8bea45dc2`, not a claim about the current remote tip.
The complete corrective plan is [section 14](PASS_LEAN_RESTRUCTURE_PLAN.md#14-current-master-audit-and-corrective-migration-gates);
[MASTER_REBASE_AUDIT.json](tests/MASTER_REBASE_AUDIT.json) contains full branch
hashes, missing contribution paths, reproduction fixtures and result hashes.
This documentation/evidence audit changes no implementation or toolchain pins.

The PR ledger has all 117 entries present (87 byte-identical); B1/B2's five
absent contribution entries are recorded original deletions or superseded
exporters. All 22 PR formal files and nine B1 compiler modules remain exact.
The flat schema is restored. The 74 protected native LEC files and 3,689
protected upstream graph/frontend/normalization/contract/warning files remain
identical under the recorded comparison exclusions. This source-retention
result does not establish that later direction branches have landed.

The branch inventory shows D2/D3/D4's schema, proof, runtime and tooling work
still absent. The actual independent Futamura checkout is `9bb41a09d`, with 67
missing Projection files (38 Lean modules), whereas this repository's local
`projection` ref points to the old `aa2f8134a`. IVP's core definitions and probe
are already exact; VS has no unique implementation; CE has only its missing
cost-study probes, not a finished elaborator. CL/GP remain separate pending
research. Historical Lean refs and separate Isabelle/ACL2/Rocq directions have
explicit inventory/disposition rather than an implicit claim of completion.

The fresh probes establish these required gates:

- A two-clock certificate typechecks but lacks independent clock-edge state;
  the interpreter updates both flops. Port D2's flop async/clock and memory
  clock metadata, edge rules, checker and compatibility proofs before claiming
  RTL clock fidelity. Record a temporary guard/contract decision explicitly.
- Current large-amount SHL, SRA and Sext evaluate with a `Nat.pow` panic; SHL's
  compile check succeeds first. D3/D4 contain bounded repairs with equality
  proofs. Recover these separately from the user-owned Lean upgrade.
- Slot construction over 108,666 slots succeeds in a `#eval` worker but fails
  via `lean --run` at an 8 MiB main-thread stack. Port D4's proved accumulator.
- D4 has a total parser and proved Lean render/parse roundtrip, plus a C++
  serializer/publication path. Its clockless DCERT1 format needs an explicit
  post-D2 compatibility decision; Lean roundtrip alone does not prove C++ byte
  agreement or external provenance. Two-file publication is not crash-atomic.
- The restored CORE-ET runner still sends retired `formal.strict` to native
  LEC, then tolerates its usage failure. Repair runner invocation and verdict
  classification only; do not modify native LEC.
- A patch map for another checkout passes the existence guard and silently
  selects unpatched input. Require source/substitution/derived hash checks.
  Keep the 122-module triage separate from the older 125-configuration matrix,
  which already has 44 configurations using patched sources.
- A fake-tool probe of the copied queue reports PROVEN for a different theorem
  using an untrusted axiom. Require identities and full comb/next/step/WF audits
  independently of the number of print lines. The frozen replay's separate
  identity checks are a distinct procedure.
- A valid one-source/zero-node certificate typechecks (33.943 s; step audit
  present, no `sorryAx`), but the CORE-ET static gate rejects its node count.
- Signed Div still maps to UDiv: legacy `-32 / 3` evaluates to 74. The verified
  compilation theorem refuses division, so it is not a successful verified
  proof. Any correction needs the user's semantic decision and operand-sign
  tests; approved Rem remains constant nonzero-divisor only.
- Hotmux's census diagnostics exist, but export-local origin/one-hot status and
  malformed dense-pin prevalidation remain open.

Of the 341 restored artifact hashes, 328 match historical successful logs;
13 do not. Section 14 names all 13 with exact hashes in the evidence JSON.
Four historical RUNNING records are stale, not active jobs. Withdrawn nested
proofs are not flat acceptance. Fresh 255/256/257-field declarations confirm
that 257 still fails on both 4.31.0 and 4.34.1; declaration success does not
validate boundary execution. Keep flat records and the user-owned upgrade,
then replay all current artifacts plus recovered branch runtime gates with
matching libraries, complete audits and wall/RSS measurements.

Audit checks: optimized tools built; three Lean Bazel test targets passed
(cached), five sweep-tooling and six Hotmux-oracle checks passed, inspected
runners passed syntax checks, and the recorded small runtime/export/boundary
probes ran. Full benchmark, source-branch proof-library and native regression
suites were not rerun in this audit. The all-direction migration and full
restored-flat acceptance remain open.

## R13. The D2 port, measured and audited

Planning checkpoint: `68c0e5e18`, audited against D2 clean `c21200e0c`.
The semantic-port approach is retained; replaying D2's 133 commits is not the
migration procedure. The original 177-edit measurement is a correct subtotal,
but omits three shared compiler/residual modules. The plan below includes the
full dependency path and corrects the clock-gate and memory premises.
[MASTER_D2_PORT_PLAN_AUDIT.json](tests/MASTER_D2_PORT_PLAN_AUDIT.json) records
full hashes, measurements, source comparisons and fresh isolated proof results.
R13.1-R13.5 record that plan audit. R13.6 records the subsequent Step 1 port.

### R13.1 Complete Lean slice and compatibility conditions

Git `diff --numstat` between the planning checkpoint and D2 measures:

| Module | Master lines | D2 lines | Added | Deleted | Edits |
| --- | ---: | ---: | ---: | ---: | ---: |
| `DesignCert.lean` | 219 | 254 | 35 | 0 | 35 |
| `Runtime.lean` | 69 | 101 | 32 | 0 | 32 |
| `DesignSemantics.lean` | 76 | 166 | 100 | 10 | 110 |
| `ResidualIR.lean` | 139 | 145 | 6 | 0 | 6 |
| `ResidualSemantics.lean` | 230 | 236 | 14 | 8 | 22 |
| `CompileDesign.lean` | 366 | 369 | 19 | 16 | 35 |
| **Total** | **1,099** | **1,271** | **206** | **34** | **240** |

Port these six modules coherently, plus the eight `Direct*` modules. Six other
modules in their dependency closure are byte-identical to master. The new
fields of existing certificate descriptors are additive and defaulted:

```lean
structure ClockDesc where
  name : String                       -- supplied when creating a clock entry
FlopDesc:    clock      : Nat  := 0
             asyncReset : Bool := false
MemoryDesc:  clock      : Nat  := 0
DesignCert:  clocks     : Array ClockDesc := #[{ name := "clock" }]
```

Old named certificate literals can still elaborate. Their defaults preserve
the old clockless interpreter's every-step commit behavior under `allEdges`.
That is not evidence that every old RTL design had only one clock, or that its
lost provenance has been recovered.

The actual conservativity theorem has hypotheses:

```lean
interpretDesign_allEdges (D)
  (hf : ∀ f ∈ D.flops, f.clock < D.clocks.size)
  (hm : ∀ m ∈ D.memories, m.clock < D.clocks.size) (i) (s) :
  interpretDesign D (allEdges D) i s = interpretDesignLegacy D i s
```

Defaults satisfy these bounds. `compilesOk D = true` does not prove them:
a freshly checked D2 probe compiles a certificate with an empty clock table,
whose allEdges step holds state while Legacy commits. Validate clock metadata
separately and discharge the bounds in the compatibility theorem; do not add
an unsupported implication from compile acceptance to clock well-formedness.

`fires e c` only reads the edge array, with missing entries defaulting to false.
It does not know the declared clock table, so a present entry at an undeclared
ordinal can fire. Checked runs require both valid clock ordinals and
`e.size = D.clocks.size`; D2's `checkDesign` and `checkRuntime` supply those
checks. The general compiler theorem remains a semantic equality even for
malformed edge vectors, not an assertion of RTL validity.

### R13.2 Arbitrary-edge compiler, allEdges compatibility export

`interpretDesign`, `denoteResidual` and `compileAndRun` all gain `ClockEdges`.
This is not just a theorem-signature change. Port:

- Residual flop `clock`/`asyncReset` and memory `clock` descriptors;
- `compileFlop`/`compileMemory` propagation;
- Edge-sensitive target flop/memory commits and quiet-domain holds;
- `flopNext_agree`, `compileDesign_correct` and `compileAndRun_correct` for
  **every** edge vector, input and state, as already proved on D2.

Retain the recorded decision to target clock-aware semantics with `allEdges`
for the existing generated two-argument compatibility step:

```lean
def Top_step : RuntimeInput → RuntimeState → RuntimeResult :=
  compileAndRun Top_designCert (allEdges Top_designCert)
-- Top_step inp st = interpretDesign Top_designCert (allEdges Top_designCert) inp st
```

The library must still expose arbitrary-edge `compileAndRun`; its generic
correctness theorem cannot be restricted to allEdges. The exported compatibility
wrapper is an all-domains-fire schedule, not a proof for every RTL schedule.
Prove its agreement with the preserved old interpreter using the explicit
clock-bound hypotheses. A separate edge-aware caller can use the generic
function and D2 DirectSim. Do not target Legacy just to avoid the signature
migration, and do not advertise the allEdges wrapper alone as multi-clock RTL
fidelity. Legacy fast/GraphCert/bridge comparisons remain the preserved
all-fire procedure unless a separately reviewed edge-aware legacy model exists.

The tracked caller inventory includes `compiler_smoke.lean`,
`design_scan_test.cpp`, `check_legacy_oracles.py`,
`legacy_semantic_audit/MaskEndpointPort.lean` and the emitter. Adapt these
fixtures and script generators without weakening their existing assertions.
The planning checkpoint's **943** is a textual generated-artifact count, not
943 distinct current acceptance cases. Preserve old logs/artifacts and generate
new outputs with source/library/toolchain hashes; do not overwrite historical
snapshots. The historical 54/147 async-flag checker regressions are a separate
count to remeasure on the current corpus.

### R13.3 Clock resolution, memory policy and the owned IR

D2's 668-line `IR_Semantics_pass.cpp` is an old exporter and is already absent
from master. Do not restore it merely to delete it. Port its relevant behavior
through the canonical plan's shared scan/IR boundary (sections 3, 5 and D2 in
section 8), then record the duplicate exporter as superseded. The former P6/P8
citations do not exist in the current L0-L8 plan.

Clock resolution belongs in `design_scan.cpp`, using the current upstream
read-only clock analysis API. Store owned provenance in `DesignScan`, carry it
through `CertificateIR`, `certificate_ir.cpp` and `memory_lowering.cpp`, then
format it in `emit_design_cert.cpp`. Grow the owned clock table and normal and
synthetic driver descriptors explicitly. The emitter must not consult HHDS or
reconstruct domains. Preserve current ordering/IDs, async-reset Q reads,
operator/memory lowering and legacy proof semantics.

**D2 refuses unnormalized gates.** Its exporter calls `resolve_icg` to diagnose
a gate still on the clock pin and fails; it does not accept the root while
dropping the gate's enables. It also refuses falling-edge/inverted commits and
unresolvable or constant clock cones. Preserve these guards with current clock
analysis, including recognized Clock_cell chains and unsupported division or
polarity. Accept a normalized gate only when its enable is already represented;
a new gate-to-enable lowering requires separate equivalence tests and review.
Do not change `single_edge`, native LEC or graph contracts to make export pass.

Root-net identity is necessary but not sufficient for bus clocks.
`Commit_class.net_key()` identifies a root input by name; current master's
`single_edge` separately tracks selected clock bits and refuses ambiguous
`clks[0]`/`clks[1]` use. The port must preserve bit discrimination or refuse
ambiguous/unresolved selections, rather than merging them into one ordinal.
Apply deterministic ordinal ordering and the implicit-clock join rule only
after resolving actual domains: join the unique explicit domain, otherwise
retain an explicit implicit-domain entry rather than guessing.

**One MemoryDesc is a certificate restriction, not a hardware guarantee.**
Current cells have per-port `clock_pin` and one global `posclk` polarity;
global polarity does not imply a shared clock root. The `memory-mixed-edge`
normalizer guard cited by the earlier draft exists on D2, not on this master.
Master's `single_edge` skips Memory during clock-class collection and has a
separate divider/memory refusal. The Lean scanner must enforce its own common-
domain policy across every committing write and synchronous-read port:
resolve roots/polarity, join only justified implicit clocks, and refuse distinct
roots, unsupported falling edges, constants, gates or unresolved cones. Skip
asynchronous read ports because they do not commit. Retain existing supported
memory types and refusals; do not silently import D2's other frontend/memory
normalizer changes.

Synthetic synchronous-read flops inherit their memory/port domain and have no
async reset. Include synchronous ROMs: they have registered reads even though
there is no mutable MemoryDesc. An asynchronous ROM creates no state/domain
obligation. Normal flops copy `asyncReset` from the scan and maintain consistency
with `SourceDesc.flopQAsync`; synchronous reset holds when its clock is quiet,
while async reset acts on both visible Q and committed state without an edge.

D2 also exports an IO/clock sidecar: ordinal mapping, `runtime_input_arity` and
`used_by_model` distinguish ignored inputs and preserve ordinal gaps. Give it,
trace/validation scripts and publication tests explicit destination/disposition
entries. Keep staged pair-write failure behavior accurate; it is not crash-
atomic publication and must not be advertised as such.

### R13.4 Port order and acceptance

1. **Complete; see R13.6.** Port the six shared Lean modules and eight Direct
   modules, updating active tracked Lean callers and generators. Build a coherent
   library and audit arbitrary-edge correctness plus the bounded allEdges
   compatibility theorem.
2. Add owned scan/IR clock provenance, normal and synthetic driver propagation
   and the scanner refusal/identity rules above. Emit clock metadata through the
   Step 1 allEdges compatibility wrapper, with no duplicate graph exporter.
3. Add focused gates: quiet synchronous reset, quiet asynchronous reset/Q read,
   simultaneous/independent domains, memory hold/commit, synchronous ROM/read
   registers, mixed memory roots, gates/inversions/constants, bus-bit ambiguity,
   implicit-domain joins, invalid ordinals/vectors and graph-closed emission.
   Include direct-vs-compiled and allEdges-vs-preserved legacy comparisons.
4. Re-emit using the authoritative **125 CORE-ET + 61 CVA6 configuration**
   manifest and all **three DINO** designs; retain upstream failures/refusals and
   per-mode dispositions. The 77 READY triage rows are not the replay manifest.
   Remeasure async-flag mismatches instead of assuming exactly 54 fixes.
5. Run current verified, legacy-fast, full chunked-WF and full bridge procedures
   with theorem identities, complete axiom audits and `/usr/bin/time -v`.
   Preserve flat records; affected acceptance still requires the user-owned
   matching Lean/Mathlib upgrade and cannot skip constructor limits silently.
6. Record proof/binary/input/artifact hashes, source adaptations and every
   contribution's destination/disposition, including D2 validation tools.
   Only after these gates port D3/D4, then explicitly migrate FP's fixed pin.

Scope remains D2, not Projection/Reify/CertIO. Any reset, clock, memory or other
semantic difference from the preserved model must be presented to the user
before adopting changed behavior beyond the recorded compatibility contracts.

### R13.5 Audit validation and limits

A fresh project-local isolated build of D2 `c21200e0c` compiled all 20 modules
in this dependency closure: six dependencies identical to master, six changed
shared modules and eight Direct modules. `DirectTests` passed its guards.
Two additional probes confirm the valid-default/invalid-clock compatibility
boundary and missing-entry/extra-entry edge behavior. Axiom audits for
`interpretDesign_allEdges`, `compileDesign_correct`, `compileAndRun_correct`
and `Direct.directStep_eq_compileAndRun` contain only their recorded standard
axioms and no `sorryAx`. Measurements, input hashes, per-module wall time/RSS
and probe sources are in the evidence JSON.

This validates the D2 proof snapshot, not a ported C++ exporter or fresh full
DINO/CORE-ET/CVA6 run on master. No implementation, native code, toolchain pin
or record layout changed. The prior 943 generated-file count was not recounted
here; acceptance is governed by the committed manifests and exact current
artifact/mode results, not a count of historical copies.

### R13.6 Step 1 implementation and fresh focused validation

Step 1 is implemented on master over `5eaee438c`. The six shared compiler modules
and eight Direct modules are byte-identical to D2 clean `c21200e0c`. The four
tracked caller fixtures/generators listed in R13.2 now pass explicit `allEdges`;
their value checks and intentional compiler refusals remain in place.
[MASTER_D2_STEP1_RESULTS.json](tests/MASTER_D2_STEP1_RESULTS.json) records source,
binary and artifact hashes, theorem identities, axiom reports and per-proof
wall time/RSS.

The verified emitter retains its two-argument `_step` as
`compileAndRun D (allEdges D)`. It emits three separate obligations:
`_compiles`, `_clock_bounds` for both flops and memories, and `_step_correct`
instantiating arbitrary-edge compiler correctness. `_step_legacy_correct`
then composes that equality with `interpretDesign_allEdges`, using the two
explicit bounds witnesses. Both step theorems are printed for axiom auditing.
No clock-bound implication is inferred from `compilesOk`.

Fresh focused checks on the current library:

- All three Lean Bazel tests pass; optimized exporter, fixture generator and
  `lhd` build. The four explicit library targets build successfully.
- Across 38 repository-owned tiny graphs and three modes, all 114 C++ export
  acceptance/refusal results match the frozen replay exporter at `5129e8c07`.
  All 27 verified certificate definitions match; 27 legacy-fast and 23
  bridge/chunked-WF exports are byte-identical. This baseline is recorded
  explicitly; it is not the Step 1 parent commit.
- The 72 Lean jobs comprise 68 successful checks: 25 verified artifacts with
  both step theorems, 23 complete tiny bridge/chunked-WF proofs, 17 scanner value
  oracles, two tracked caller fixtures and one four-theorem library audit.
  The other four are expected controls: two compiler refusals (Div/SetMask)
  and two invalid-clock bounds rejections. Each invalid-clock control first
  proves compiler acceptance and then fails solely at its false bounds check.
- All 27 accepted legacy artifacts typecheck in the existing 16-seed value
  comparison, including output/state and every tiny RAM address. The two
  compiler-refused cases use the interpreter oracle and separately confirm
  compiler refusal. The remaining 11 fixture rows retain C++ export refusals.

The four library theorem audits contain only their recorded standard axioms;
successful generated proofs additionally contain their recorded `native_decide`
facts. No successful result uses `sorryAx`. The temporary audit matcher was
corrected to qualify namespace-local theorem names, and negative fixtures were
isolated from subsequent declarations; neither repair changed a production
proof or accepted a missing audit.

This checkpoint completes the API/caller portion of Step 1, not Step 2 or the
full migration acceptance. Certificate data emission is unchanged from the
parent, so current C++ exports still rely on the default domain and reset flag.
Owned clock provenance, async-reset flag propagation, synthetic read domains,
memory/clock refusals, sidecars and their focused gates remain pending. The
allEdges wrapper preserves every-step behavior; it does not recover RTL clock
provenance. Full 186-configuration CORE-ET/CVA6 and three-DINO replay, flat-record
upgrade acceptance and D3/D4/FP migration remain open. Lean/Mathlib pins, flat
record layout and native LEC/frontend/graph code were not changed.

## R14. Full-flow replay on master: CORE-ET, CVA6 and DINO

Rebase standard, as set by the user: *every benchmark we support must pass —
the old branch corpus and the blocks master has gained since.* This section
records the first full-flow replay against that standard, and the defects the
replay exposed. It does **not** meet the standard outright: CVA6 configuration
reconciliation, runtime traces, the legacy full-bridge and chunked-WF gates, and
D3/D4/FP all remain open. What is established here is the
verified_compiler/`allEdges` typecheck tier.

It is a **full** flow — frontend, `pass.lean` export, Lean typecheck, axiom
audit — not an export census. The earlier CORE-ET census ran
`STOP_AFTER=lean RUN_LEAN=false` and typechecked nothing; its numbers are not
evidence of proof and are superseded here.

Scope of what was run: `formal.lean.mode=verified_compiler` typechecks of the
`allEdges` compatibility export. **LEC was disabled.** Trace comparison,
`checkDesign`/`checkRuntime`, and legacy-fast / full-bridge / full-chunked-WF
acceptance are separate gates and were not run. Nothing below is a claim about
those.

Evidence, committed beside this document:

| File | Rows | Produced by |
| --- | ---: | --- |
| [SWEEP_master_coreet122.tsv](SWEEP_master_coreet122.tsv) | 122 | full-flow CORE-ET driver — **historical baseline, left as run** |
| [SWEEP_master_coreet_addendum.tsv](SWEEP_master_coreet_addendum.tsv) | 5 | post-baseline: 2 regate + 3 manifest-gap modules |
| [SWEEP_master_cva6_elab.tsv](SWEEP_master_cva6_elab.tsv) | 124 | `scripts/elab_cva6_wrappers.sh` |
| [SWEEP_master_cva6_emit.tsv](SWEEP_master_cva6_emit.tsv) | 110 | `scripts/run_cva6_vc_sweep.sh` |
| [SWEEP_master_cva6_prove.tsv](SWEEP_master_cva6_prove.tsv) | 87 | `scripts/run_lean_queue.sh` |
| [SWEEP_master_dino3.tsv](SWEEP_master_dino3.tsv) | 3 | `scripts/run_lean_queue.sh` |
| [CONST_WIDTH_CASES.tsv](CONST_WIDTH_CASES.tsv) | 18 | `pass/lean/scripts/vc_gates.py` over every emitted certificate |

The baseline table is kept exactly as the sweep produced it; the five
post-baseline modules are recorded separately rather than folded in, so the
historical 78/44 figures stay readable. A header row was prepended to the CVA6
elaboration table; it is otherwise verbatim. In the emit table, column 3 holds
`sources` for an `EMITTED` row and the **refusal reason** for a `NO_EMIT` row —
the driver reuses the slot.

### R14.1 Result

| Family | Covered | Emitted | Proven | Typecheck failures | `sorryAx` |
| --- | ---: | ---: | ---: | ---: | ---: |
| DINO | 3 | 3 | **3** | 0 | 0 |
| CORE-ET | 125 | 81 | **81** | 0 | 0 |
| CVA6 | 110 elaborated (of 124 generated) | 87 | **87** | 0 | 0 |
| **Total** | — | **171** | **171** | **0** | **0** |

Every module that emits a certificate proves it; there is no partially-verified
tier. But see R14.4 — **four of these 171 carry a model defect that a proof
cannot detect**, so "proven" and "trustworthy" are not the same count here.

CORE-ET coverage is now the full authoritative manifest. The sweep as first run
covered 122 of the 125 tops in
[MASTER_BENCHMARK_MANIFEST.json](tests/MASTER_BENCHMARK_MANIFEST.json); the
three missing tops were found by reconciling against it and have been run
(R14.3). The 122-module discovery list and the expanded CVA6 wrapper list are
**additional** coverage over the authoritative 125 CORE-ET + 61 CVA6
configuration manifest, not replacements for it. The CVA6 61 counts historical
configurations including legacy ones, and is a different axis from the 124
generated `_gate` wrappers; that reconciliation is still open.

### R14.2 What these artifacts are, and are not

The exports are **verified-compiler** artifacts. Each contains:

```
def     <Top>_designCert : DesignCert
def     <Top>_step       := compileAndRun <Top>_designCert (allEdges <Top>_designCert)
theorem <Top>_compiles
theorem <Top>_clock_bounds
theorem <Top>_step_correct
theorem <Top>_step_legacy_correct
def     <Top>_residual
```

They exercise the **D2-ported library** — clock-aware `DesignCert`,
`ClockEdges`, arbitrary-edge `compileAndRun_correct`. They are **not** D2's
direct interpreter: across all 171 exports, zero files reference `DirectSim`,
`directStep` or `runDirect`. The eight `Direct*` modules are ported and build,
but no real design has been run through them.

The array state model carrying 534 flops says nothing about the legacy
flat-constructor path, which keeps its own separate upgrade gate. It is not
authority to change nesting or toolchain pins.

### R14.3 Two gate defects found by the replay

The replay's value was not the green count. It was two defects in the gates
themselves, both found by reconciling results against an independent source.

**(a) A stale duplicate gate refused valid certificates.**
`scripts/run_coreet_module_lean.sh` carried a hand-rolled copy of
`pass/lean/scripts/vc_gates.py`, and the copy had drifted in both directions.
It refused any certificate with zero *nodes*. That is wrong: a design whose
outputs are all constant-driven is valid and provable. `null_vpu` (10 constant
sources, 10 outputs) and `minion_dcache_texsend` had each emitted a complete
four-theorem certificate and were discarded by that check alone. Both are now
**PROVEN** — 20.1 s and 12.7 s.

The baseline's 44 `EXPORT_FAIL` rows therefore contain 2 that were never
failures. They are *not* `single_edge` refusals, although their recorded detail
text mentions `pass.single_edge`; that is a skip notice, not the refusal.

**(b) The same stale copy was missing the const-width and ROM-entry gates.**
The CORE-ET sweep ran **weaker checks than the CVA6 sweep against the same
emitter**. Running `vc_gates.py` over the 80 emitted CORE-ET certificates finds
4 that violate the const-width gate (R14.4).

Both drivers now call the one implementation. The fix is deliberately *not* to
drop the degeneracy check: `vc_gates.py` refuses a certificate with **no sources
and no nodes**, which still catches the empty-filelist trap of R14.8.

That is not sufficient, and should not be recorded as if it were. A wrong but
non-empty elaboration — right module name, wrong or partial RTL — still passes
every static gate. Detecting that needs an input/provenance check (hashes of the
resolved source set against the manifest), which does not exist yet and is
listed in R14.9. Removing `nodes>0` must not be read as making accidental
elaboration acceptable.

### R14.4 The 18 flagged modules are a mask-operand question, not constant-width defects

| Module | Flags | First |
| --- | ---: | --- |
| `trans_top` | 31 | `const width 1 cannot hold value 8192 (needs 14 bits)` |
| `intpipe_csr_msgs` | 8 | `const width 46 cannot hold value 1152886320234758144 (needs 60 bits)` |
| `vpu_ml` | 1 | `const width 29 cannot hold value 4160749568 (needs 32 bits)` |
| `vpu_tensorfma` | 1 | `const width 29 cannot hold value 4160749568 (needs 32 bits)` |

Plus 14 CVA6 modules the gate refuses before typecheck — 18 in all,
`CONST_WIDTH_CASES.tsv`.

**Every flagged constant occupies one operand position.** Measured over 13 of
the 18 modules (4 CORE-ET, 9 CVA6), **168 of 168** flagged constants are
**operand 1 of `Op_GetMask`** — the *mask*, never a data operand — and in
**168 of 168** the declared mask width is **greater than or equal to the width
of the data operand it masks**.

That changes what the flag means. A mask bit above the data operand's width
selects a bit that does not exist, so dropping it cannot change the result.
These are not constants whose value is being truncated in an arithmetic
context; they are masks declared at the width of the thing they mask. An
independent primitive oracle run alongside this one compares emitted-width
against intrinsic-width masks for eight values each and reports
**2,024 / 2,024 agreement**, including `-1` and high-bit values at the data
width.

**So "const-width defect" is the wrong label and this plan previously used it.**
An earlier draft of this section called these model defects and subtracted them
from a "trustworthy" count; a later one called them unexplained flags. Both
overstated. What is established is narrower and more useful: the gate is
over-broad *for this operand position*, and the evidence available points at
benign mask semantics.

What the evidence is **not**: it is finite, certificate-level agreement on
chosen values. It is not a proof over arbitrary inputs, and it is not agreement
with the raw RTL or with its signedness. `CertificateBuilder::dep` sets a
constant source to the **consumer's** width by design, while the upstream
"a width that cannot hold its value is a lie" contract
(`graph/node_util.hpp:323`) is about the **raw pin** — a different number — so
the two were never measuring the same thing.

The investigation is therefore **mask semantics and raw graph provenance
first**: what `Op_GetMask` does with a mask narrower than its literal, and what
width the raw pin actually carried. Not a generic constant-width repair. **No
constant will be widened and no truncation semantics changed**, and any actual
semantic repair goes to the user for a decision before adoption.

The practical cost stands regardless: these 18 are refused before typecheck, and
one of them — `intpipe_csr_msgs` — is the only genuinely two-domain design in
the corpus (R15.5). The best multi-clock fixture available is blocked behind a
gate that is probably firing on a benign pattern.

### R14.5 Why a module does not emit, by verified cause

**CORE-ET, 44 of 125.** None is a Lean-side failure.

| Cause | Count |
| --- | ---: |
| `pass.single_edge` refused — memory would commit every sub-step under a phase divider | 13 |
| `pass.single_edge` refused — enable latch not transparent on the phase opposite the gated edge | 11 |
| combinational loop reported by `pass.formal` | 16 |
| Concat lane-width inconsistency | 2 |
| `read_slang` elaboration error | 2 |

The two Concat cases (`minion_dcache_top`, `minion_dcache_tensor_load`) are
**not** combinational loops. Their diagnostic leads with a lane table showing a
driver wider than its declared window —
`[0] drv=mux_94508 bits=2 window=1`. An earlier draft of this section classified
them as loops by keyword; reading the full message corrects that.

**24 of the 44 are clock-domain questions.** They are **refusal fixtures for
step 2, not targets it can convert.** `pass.single_edge` runs upstream of the
Lean scanner and refuses these before `pass.lean` is reached, so no amount of
clock provenance inside the scanner turns one of them into an emission. Their
value is that they are the real shapes — phase dividers and enable-latch
gating — against which a resolver's refusals must be checked. The upstream
disposition and the native contract stay as they are; nothing upstream will be
weakened to improve a count.

Two of them, `prim_phase_pair_lo_hi` and `prim_write_commit_en` (recovered in
R14.3), are the minimal reproducers of the enable-latch case and the right
fixtures to develop against. The third recovered primitive,
`prim_write_commit_rst_en`, emits and proves (37.2 s).

**CVA6, 23 of 110 elaborated.**

| Cause | Count |
| --- | ---: |
| const-width static gate (R14.4) | 14 |
| combinational loop reported by `pass.formal` | 8 |
| empty certificate (`unread`, a stub) | 1 |

**CVA6 wrapper elaboration, 14 of 124 generated.** These fail in `slang`, before
any LiveHD pass runs, on types the generated wrapper uses but does not declare:

```
cva6_hpdcache_subsystem_axi_arbiter  use of undeclared identifier 'axi_b_chan_t'
fpnew_cast_multi                     no implicit conversion from 'bit[1:0]' to 'pipe_config_t'
```

Twelve of the fourteen are `fpnew_*` or `hpdcache_*`, both heavily
package-parameterised. This is a `scripts/gen_cva6_wrappers.py` limitation, not
a statement about those modules' verifiability.

### R14.6 The axiom audit was a line count; it is now an audit

R13.1's guard is that clock well-formedness must not be inferred from compile
acceptance. The emitted proofs keep the obligations separate, and the audit
confirms the dependency — note that independence is a property of the
**obligations**, not of disjoint final axiom sets:

```
'scoreboard_gate_step_correct'        depends on axioms:
  [propext, Classical.choice, Quot.sound,
   scoreboard_gate_compiles._native.native_decide.ax_1_1]
'scoreboard_gate_step_legacy_correct' depends on axioms:
  [propext, Classical.choice, Quot.sound,
   scoreboard_gate_clock_bounds._native.native_decide.ax_1_1,
   scoreboard_gate_compiles._native.native_decide.ax_1_1]
```

`_step_correct` cites compile acceptance. `_step_legacy_correct` cites compile
acceptance **and** the separately proved clock bounds, because it must discharge
`hf`/`hm` for `interpretDesign_allEdges`. Both share `_compiles`; what matters is
that the bounds are proved independently and actually used.

**The queue's `2/2` did not establish this.** It compared a count of
`depends on axioms` lines against a count of `^#print axioms ` lines and grepped
for the literal `sorryAx`. That accepts a file auditing theorems that are not the
required obligations (no name is ever compared), any custom axiom not spelled
`sorryAx`, and — because Lean **wraps** long axiom lists — any axiom smuggled
onto a continuation line. Reporting `2/2` as structural safety overstated it.

Three changes, all in place:

- **`pass/lean/scripts/axiom_audit.py`** checks exact namespace-qualified
  obligation identity, parses wrapped lists, allowlists axioms (the Lean
  standard set plus the per-design `native_decide` facts), and **requires
  `_step_legacy_correct` to cite the `_clock_bounds` native fact** — the R13.1
  guard, enforced rather than described.
- **`pass/lean/scripts/axiom_audit_controls.sh`** is the adversarial suite: a
  missing obligation, a wrong theorem name, a custom axiom, an imported `sorry`,
  an axiom on a wrapped continuation line, and a legacy obligation that does not
  cite the clock bounds. The old line count passed all six; the auditor rejects
  all six and accepts the genuine log.
- **`scripts/run_lean_queue.sh`** now writes `lean_typecheck.<module>.log` and
  requires the audit to pass before printing `PROVEN`. It picks the obligation
  set **from the artifact**: `_step_correct`/`_step_legacy_correct` for a
  verified export, `_comb`/`_next`/`_step_refines_fast` for a legacy bridge
  export — never from how many `#print axioms` lines a file happens to contain.
  The first version required the verified names unconditionally, which rejected
  a sound legacy artifact for not being a verified one; the queue serves both,
  which is why its build step also builds `LegacyCertWF`.

**What was recovered without re-running, and what was not.** The shared-log
problem is real but narrower than first reported: it affects only artifacts that
went through `run_lean_queue.sh` into a *shared* output directory, which is CVA6
and DINO. The CORE-ET full-flow driver wrote its own per-module
`<Top>.lean.log`, and **all 78 survive**. An earlier draft of this section said
only four logs survived and the rest needed a re-run; that was wrong, and the
re-audit was cheap once the files were found.

Re-auditing all 78 CORE-ET logs with `axiom_audit.py` — report **and** source,
against the artifact — gives **78/78 pass**: exact theorem names, `_compiles`
for `_step_correct`, and both `_compiles` and `_clock_bounds` for
`_step_legacy_correct`, with no axiom outside the measured set.

The measured set across all 160 surviving reports is exactly:

```
propext   Classical.choice   Quot.sound
<Top>_compiles._native.native_decide.ax_N
<Top>_clock_bounds._native.native_decide.ax_N
```

and nothing else — no `funext`, no `Quot.lift`, no `Lean.ofReduceBool`, no
`Lean.trustCompiler`. The allowlist is that measured set. The first version of
the auditor allowlisted five further axioms on the assumption they might appear;
they never do, and allowlisting an unobserved axiom is how a trust assumption
widens without anyone deciding to widen it.

That version also matched native facts by **shape**
(`<Top>_<anything>._native.native_decide.ax_N`), which would have accepted
`<Top>_arbitrary_false._native…` — a `native_decide` of an arbitrary
proposition, proving something about nothing. Native facts are now accepted only
for the two independently expected witnesses, or, in legacy mode, only when the
obligation stem names a theorem the artifact itself declares.

Separately, and from the artifacts rather than the logs: all **170** emitted
files audit exactly `<Top>_step_correct` and `<Top>_step_legacy_correct`, both
declared as theorems in the same file. No file audits a non-theorem or any other
name.

**Still outstanding:** full per-module axiom text for the CVA6 and DINO
artifacts, whose logs the shared path did overwrite. Those need a re-run under
the new queue, with artifact, library and toolchain hashes recorded (R14.9).

Report counts should keep these tiers apart: **independently audited logs** (78
CORE-ET), **retained count-only results** (CVA6, DINO), **static-gate
investigations** (the 18 const-width cases), and **runtime validation**, which
has not been run at all.

### R14.7 Timing: measured wall time, not isolated cost

| Family | n | Median | Mean | Max | Concurrency |
| --- | ---: | ---: | ---: | ---: | --- |
| CVA6 | 87 | 15.9 s | 25.7 s | 161.0 s (`multiplier_gate`) | serial queue |
| CORE-ET | 78 | 18.6 s | 27.3 s | 93.7 s (`txfma_wallace1`) | 4-way parallel |
| DINO | 3 | 41.8 s | 39.5 s | 46.3 s (`SingleCycleCPU`) | serial queue |

These are wall times under the recorded concurrency on a shared NFS box, not
isolated per-module cost and not a proven bound on it. The CORE-ET figures
additionally carry 4-way contention. `scripts/run_lean_queue.sh` serializes for
exactly this reason, and the CVA6 and DINO rows went through it.

Total CVA6 proof time was 2,236 s across 87 modules.

**Size predicts cost poorly.** Univariate Pearson correlation of wall time is
**+0.50** against node count and **+0.39** against flop count; with no fitted
model these do not combine into a stated explained variance. The four slowest
modules span three orders of magnitude in size:

| Module | Nodes | Flops | Max width | Wall |
| --- | ---: | ---: | ---: | ---: |
| `multiplier_gate` | 1,259 | 6 | 256 | 161.0 s |
| `csr_regfile_gate` | 11,532 | 534 | 5,770 | 122.4 s |
| `cva6_shared_tlb_gate` | 58 | 5 | 190 | 113.8 s |
| `hpdcache_prio_1hot_encoder_gate` | 22 | 0 | 5 | 96.4 s |

A 22-node, flopless, 5-bit module at 96 s is not explained by node count, flop
count or width, and neither is a 58-node module at 114 s — while 125-node
`ct_vfdsu_ctrl_gate` finishes in 13.3 s. The fastest observed run is 12.5 s;
whether that reflects a fixed `native_decide` compile-and-link cost is a
**hypothesis**, not a measurement, and no profiling has been done. Flop and node
counts for CVA6 are grep proxies over the certificate (`resetActiveLow`,
`origin :=`), good for ranking, not exact structural counts.

### R14.8 Recovering the CVA6 flow

The CVA6 result was unobtainable at the start of this session: the sweep ran
through `bender`, which cannot resolve `--top <module>_gate` and silently emits
an **empty** filelist. That elaborates to nothing, exits 0, and yields a
certificate with `nodes := #[]` — `fpu_wrap` came out as a 2,141-byte stub. The
sweep reported 8 of 23, three of them stubs.

The fix was to drop bender here and restore the static 219-source filelist from
`livehd-new`
(`generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f`),
injecting the wrapper via `CVA6_WRAPPER_FILE` and letting yosys pick the top.
That moved CVA6 from 8-of-23-with-stubs to 87-of-110 real.
`run_cva6_vc_sweep.sh` carries this as a comment at the filelist assignment so
the bender route is not re-attempted.

The lesson has now cost two sessions: **a tool that reports success on an empty
input is worse than one that fails.** The only reliable defence is a
non-degeneracy check on the artifact — and, as R14.3 notes, a provenance check
is needed on top of it, because non-empty does not mean correct.

### R14.9 Open items in priority order

1. **The 18 flagged modules** (R14.4). All 168 measured flags are the **mask**
   operand of `Op_GetMask`, with mask width always at least the data width, so
   investigate **mask semantics and raw graph provenance** — not a generic
   constant-width repair. Likely outcome is narrowing the gate for this operand
   position. Do **not** widen constants or alter truncation semantics; any
   semantic change goes to the user first. Unblocking this also unblocks
   `intpipe_csr_msgs`, the corpus's only two-domain design.
2. **R13.4 step 2 — clock-domain resolution**, per R13.3: owned clock provenance
   in `DesignScan` → `CertificateIR` / builder / memory lowering → emitter;
   `asyncReset` commit flag; synthetic sync-read flop and ROM domains;
   deterministic root+bit identity; implicit joins; common-domain committing
   memory ports; refusal of unresolved, falling or gated clocks unless already
   normalized. Emitter stays graph-free; all-fire semantics preserved.
   `prim_phase_pair_lo_hi` / `prim_write_commit_en` are refusal fixtures for it,
   not modules it will convert — they are refused upstream by `pass.single_edge`
   before the scanner runs. **Implemented; see R15.** Note that
   `SWEEP_multiclock6` cannot be its acceptance gate (R15.6): all six members
   stop before `pass.lean`.
3. **Re-run the corpus under the new queue** so every module has a retained
   per-module log and a full axiom audit, with artifact, library and toolchain
   hashes recorded (R14.6).
4. **Provenance gate**: hash the resolved source set per module against the
   manifest, so a wrong-but-non-empty elaboration cannot pass (R14.3).
5. **CVA6 configuration reconciliation** — the authoritative 61 against the 124
   generated wrappers.
6. **Hotmux one-hot status** (native-proved / deferred / refuted) into export
   diagnostics. Every DINO export currently carries a bare `onehot-deferred`
   warning with no indication of which.
7. **`gen_cva6_wrappers.py` package-type propagation**, worth up to 14 more CVA6
   modules.
8. **Run a real design through the `Direct*` interpreter** (R14.2).
9. Acceptance for step 2 is broader than `SWEEP_multiclock6`: independent, quiet
   and simultaneous clocks; sync vs async reset and Q reads; memory/ROM hold and
   commit; invalid ordinals and vectors; mixed roots; bus bits; gates, polarity
   and constants; plus direct-vs-compiled and allEdges-vs-preserved comparison.
10. Then D3/D4 bounded runtime repairs and proof splits, metadata-complete D4
    transport, and FP's pinned implementation — each recorded in a destination
    ledger, with no graph-coupled exporter copied and no omitted clock/reset
    transport metadata silently defaulted.

## R15. Step 2 — clock-domain resolution

R13.4 step 2, implemented. The scan now **owns** clock provenance: it resolves
every committing state element to a domain, stores the ordinal, and the emitter
formats what it is given. The emitter never consults HHDS and never
reconstructs a domain.

Landed across the stage boundary R13.3 specifies:

| Stage | File | What it carries |
| --- | --- | --- |
| scan | `design_scan.{hpp,cpp}` | `ClockDomain{key,name,rising,implicit}`, `DesignScan::clocks`, `Flop::clock_ordinal`, `MemoryPort::{clock_ordinal,has_domain}`, `Memory::clock_ordinal` |
| IR | `certificate_ir.{hpp,cpp}` | `CertClock`, `FlopDriver::{clock,async_reset}`, `MemoryDriver::clock` |
| lowering | `memory_lowering.cpp` | synthetic sync-read flops inherit their **port's** domain, never async |
| emit | `emit_design_cert.cpp` | `clocks := #[…]`, per-flop `clock`/`asyncReset`, per-memory `clock` |

### R15.1 Everything fails closed

A domain that cannot be resolved is refused **by name**, never defaulted to
ordinal 0. The asymmetry matters: a wrong merge is invisible downstream — two
elements placed in one domain commit together forever after, and the certificate
proves that behaviour faithfully.

| Refused | Why it cannot be a domain |
| --- | --- |
| unresolvable cone | the certificate cannot name what the element commits on |
| falling edge (cone form) | an inversion carried in the cone, not on the `posclk` pin |
| unnormalized clock gate | accepting the root would **drop the enables** |
| constant clock | the element never commits, or always does |
| opaque root | neither a design input nor a net any state element clocks on |
| multi-bit clock root | the bit cannot be discriminated (R15.3) |
| memory on two domains | one `MemoryDesc` carries one clock |

**`commit_class_of` is not a refusal oracle, and assuming it was would have been
the quiet failure here.** It reports a commit *class*, and for a flop it sets
`role = Clock` unconditionally — "a flop's `clock_pin` cone IS a clock by
definition of the pin". The cone walk behind it (`resolve_phase`) **stops at a
constant and returns the constant as the net** rather than failing, so a flop
clocked by a literal comes back with a perfectly well-formed `Commit_class`.
Constant and opaque roots are therefore refused explicitly, in
`clock_root_validity_refusal`, because nothing upstream refuses them.

### R15.2 The implicit-domain join runs before any rejection

A memory whose write port names `clk` and whose read port names nothing is not a
two-domain memory: the unnamed port is on the module's implicit clock, which in
a design with exactly **one** explicit domain is that same clock. Joining it is
correct; rejecting it as "more than one domain" would refuse an ordinary cell.

The join is only justified when the explicit domain is **unique**. With two
explicit domains there is nothing to join an implicit port to without guessing
which, so the implicit entry is retained and the common-domain check refuses the
memory — the honest outcome, and the rule R13.3 states.

### R15.3 Bus bits are refused, not preserved — and the earlier note was backwards

`net_key()` names a graph input by its **name**, and `control_root` follows every
`Get_mask` to the whole input, so `clks[0]` and `clks[1]` produce the **same
key**. An earlier comment in `design_scan.hpp` claimed `net_key()` distinguishes
them; it is exactly the opposite, and that note has been corrected.

`pass/single_edge` tracks the selected bit separately (`clock_bit_on`) and
refuses what it cannot decide, but that walker is file-local there. Rather than
duplicate a parity-sensitive cone walk — the mistake `latch_contract.hpp` warns
about in as many words, since a parity bit counted in two places is how a double
negation survives — the scanner **refuses a multi-bit clock root outright**.
R13.3 permits exactly that: preserve bit discrimination or refuse, never merge.
A one-bit root has no ambiguity to preserve, which covers the whole current
corpus. Preserving the bit is the stronger option and is not implemented.

### R15.4 A refusal that did not fire on its own test

`clock_root_bit_refusal` first read the width with the one-argument
`bits_of(pin)`. That returns 0 whenever the width lives on the IO declaration
rather than the pin — which is the normal case — so the refusal **never fired**,
including on the unit test written to prove it fires. It now reads
`ctx.input_width`, the same source the scan uses for every other input width.

This is worth recording because the failure mode is the one this whole section
is about: a guard that silently never runs is indistinguishable from a guard
that always passes, and only a test that *expects the refusal* catches it. The
test was written first and failed; that is why the bug is in this paragraph
rather than in the corpus.

### R15.5 The corpus contains a real two-domain design, and the old default was wrong on it

`intpipe_csr_msgs` resolves **two** clocks:

```
clocks := #[ { name := "clk_i" }, { name := "clk_wr_i" } ]
```

with **51 flops on `clk_i`**, **2 flops on `clk_wr_i`**, and its memory on
`clk_wr_i` as well:

```
{ aw := 1, dw := 60, nextImg := 3380, clock := 1 }
```

What this does and does not say. The old exported `_step` **advanced every state
element on each call** — it had no clock concept, so there was no binding of
ordinal 0 to `clk_i` or to anything else, and it is wrong to describe the old
model as having committed these elements "on `clk_i`'s edge". It committed them
unconditionally, as it did every other element.

What the two-domain metadata adds is the ability for a **clock-sensitive caller**
to advance `clk_i` and `clk_wr_i` independently. `allEdges` continues to fire
every domain, so the exported compatibility step keeps exactly the old schedule
and R13.1's conservativity argument is untouched. The gain is that the
distinction now *exists* in the certificate: before, a caller had no way to
express it, and a design with two genuinely separate clocks was
indistinguishable from one with a single clock.

It is also the first exercise of the memory path: the memory's committing port
resolves to `clk_wr_i`, independently of the flops, and the common-domain check
accepts it because every committing port agrees.

The awkward part: `intpipe_csr_msgs` is also one of the 18 modules the mask
gate flags (R14.4), so it is currently refused before typecheck and cannot be
proven. The only genuinely two-domain design in the corpus is blocked behind a
gate that the evidence suggests is over-broad for that operand position, which
raises that item's priority (R14.9 item 1).

### R15.6 `SWEEP_multiclock6` cannot be the acceptance gate

R13.4 names `SWEEP_multiclock6` as step 2's gate. It cannot serve as one today: **no member of it
produces a certificate**, so none exercises the resolver's output.

These rows are the **historical** D2-era inventory
([SWEEP_multiclock6.tsv](SWEEP_multiclock6.tsv)), not fresh results from this
master:

| Module | Where it stops (historical) |
| --- | --- |
| `intpipe_csr_file` | reaches `pass.lean`, then TIMEOUT in emission (2 h, 4.5 GB) |
| `intpipe_mul_div_top` | accepted on D2; refused here by `single_edge` (enable-latch) |
| `intpipe_top` | `single_edge`: coincident commit edge |
| `core_top` | `single_edge`: memory-type-unsupported (type=24) |
| `vpu_ctrl` | `single_edge`: memory-latch-array |
| `minion_frontend` | `single_edge`: memory-type-unsupported (type=24) |

Five stop **before** `pass.lean`. `intpipe_csr_file` is the exception: it reaches
`pass.lean` and dies in emission on scale, which is a different wall and not a
clock question at all.

Worse for the gate's purpose: the inventory's own notes record that four of the
six resolve to **one** clock domain once `single_edge` folds their gated clocks.
Only `vpu_ctrl` is "genuinely two clocks" (`clock_sec` 600 flops, `clock_aon`
185), and it is refused for a memory reason, not a clock one. A gate made of
modules that never reach the code under test, most of which are single-domain
anyway, cannot distinguish a correct resolution from a plausible-looking merge.

`intpipe_csr_msgs` (R15.5) is a better fixture than any of the six: it is
genuinely two-domain, it reaches the scanner, and it resolves correctly. It is
not in the inventory. The acceptance gate for step 2 should be **extended** with
modules that actually reach `pass.lean` — `intpipe_csr_msgs` first, once the
const-width gate stops refusing it. The six existing rows and their required
dispositions are **kept**, not replaced: each still records a real upstream
refusal that must not silently start passing.

### R15.7 Tests

`DesignScanClocks.*` in `pass/lean/design_scan_test.cpp`, every one about a
refusal or an ordinal:

- independent clocks get **distinct** ordinals, all in range;
- two flops on one net **share** an ordinal (the converse failure: a resolver
  giving each flop its own domain stops any two from ever committing together);
- ordinals are **deterministic** across two scans of the same design, and the
  table is sorted by structural key;
- the **implicit** clock is a real domain, not an unresolved cone;
- a **multi-bit** root is refused;
- a **constant** clock is refused;
- an **unconventionally named** clock (`tick_strobe`) still resolves — the name
  fallback in `Design_clocks` is for designs that wire no flop to an input, and
  must not become a requirement;
- a combinational design still gets a clock table, so `_clock_bounds` is
  discharged against something real rather than a field default;
- resolved ordinals and the table **reach the certificate** — resolving
  correctly and dropping the result is indistinguishable from not resolving.

All `//pass/lean:*` tests pass.

Beyond the unit tests, one invariant is checked across the whole emitted corpus
rather than on fixtures. R13.3 requires the commit-side `FlopDesc.asyncReset`
to agree with the read-side `SourceDesc.flopQAsync` — the Lean checker calls a
disagreement `asyncFlagMismatch`, and the two are written by different code
paths (`certificate_ir.cpp`'s `dep` for the source, the flop driver for the
commit), so agreement is not structural. Across **81 emitted artifacts**, of
which **11 contain at least one asynchronous flop**, there are **zero**
mismatches in either direction.

### R15.8 Regression: zero clock refusals across the corpus

The risk this change carries is not that it fails to resolve a domain — it is
that one of its seven new refusals fires on a design that previously exported.
Measured, over the full authoritative CORE-ET manifest plus a CVA6 subset and
all of DINO:

| Family | Covered | Exported | Clock refusals fired |
| --- | ---: | ---: | ---: |
| CORE-ET | 123 of 125 | 77 | **0** |
| CVA6 subset | 6 committed wrappers | 5 | **0** |
| DINO | 3 | 3 (all **PROVEN**) | **0** |

**These CORE-ET figures are from the sweep run BEFORE the design-wide join and
the opaque-root correction** (R15.2, R15.1). They are retained because they are
what established "no clock refusal fires", which those two changes only make
stricter in one direction (opaque roots) and more permissive in another (the
join). A fresh 125-module sweep against the corrected resolver is in flight; its
table will be committed separately rather than folded in here. DINO was re-run
against the corrected resolver and still exports all three with a resolved
`clock` domain.

**Not one of the seven refusals fired anywhere.** Every `EXPORT_FAIL` is a
pre-existing cause: `single_edge`, a combinational loop, the const-width gate,
or a `read_slang` failure. `cva6_tlb`, the one CVA6 non-emit, is its usual
combinational loop.

Four modules changed disposition against the R14 baseline, and all four are the
**mask-operand** cases of R14.4 — `intpipe_csr_msgs`, `trans_top`, `vpu_ml`,
`vpu_tensorfma`. That is the gate de-duplication of R14.3 taking effect, not the
clock work: the CORE-ET path now runs the same gates CVA6 always ran, so these
four are refused before typecheck instead of reaching a green PROVEN with the
flag unexamined. All 18 flagged modules are now refused uniformly across both
families. Given R14.4's finding that the gate is probably over-broad for this
operand position, uniform refusal is the right state to *investigate* from, but
it is not a result to leave standing. Nothing else moved.

DINO re-proves with clock metadata emitted: 3/3, `axioms=2/2`, 25.6 s / 20.5 s /
32.7 s. The emitted flop lines are byte-identical to the pre-step-2 export apart
from the two new fields, so the resolution added provenance without disturbing
the model.

A caveat on the CVA6 figure: the 124 generated wrappers lived under `temp/` and
are gone, so this used the 6 wrappers committed in
`scripts/cva6_module_wrappers/`. That is enough to show no clock refusal fires
on CVA6's shape; it is not the 110-module sweep, and re-running that needs the
wrappers regenerated.

### R15.9 Scope: implemented, **not** accepted

Implemented: owned provenance through all four stages; the `asyncReset` commit
flag (0 mismatches against `flopQAsync` over 81 artifacts); synthetic sync-read
flop and ROM domains; deterministic ordinals with unused domains pruned; the
design-wide implicit join; the memory common-domain restriction; and seven
refusals, none of which fires anywhere in the corpus.

**The first value gate exists.** `pass/lean/scripts/clock_oracle.sh` takes a
certificate from the **real scanner** — a two-domain design, flop A on `clk_a`,
flop B on `clk_b` — and has Lean decide 11 concrete steps with `native_decide`.
All 11 pass:

| Edge vector | Result |
| --- | --- |
| `#[false, false]` | both flops **hold** their pre-state |
| `#[true, false]` | A commits, **B holds** |
| `#[false, true]` | B commits, **A holds** |
| `#[true, true]` | both commit simultaneously |
| `allEdges` | equals `#[true, true]` — the preserved schedule |
| `#[]` (short) | every missing domain is **quiet**, fail-safe |

The discriminating rows are the two middle ones: if the resolver had merged the
domains, firing one clock would commit both, and those cases would fail. The
script refuses to run at all unless the emitted certificate really declares both
`clk_a` and `clk_b`, so the gate cannot pass vacuously on a one-clock
certificate.

**This is still not an acceptance.** The 13 `DesignScanClocks` tests are about
ordinals and refusals, and the oracle above covers only flop hold/commit on two
independent domains. These remain unwritten:

- **quiet synchronous vs asynchronous reset and Q reads** — a synchronous reset
  must hold while its clock is quiet; an asynchronous one must act with no
  edge, on both the visible Q and the committed state;
- **memory and synchronous-ROM hold/commit** across quiet and firing domains;
- **invalid edge vectors and out-of-range ordinals** — `fires` reads only the
  edge array and will fire a present entry at an undeclared ordinal, so
  `checkDesign`/`checkRuntime` must be the thing that rejects these;
- **graph-closed emission** — that the emitter's output depends on nothing but
  the `CertificateIR` it is handed;
- **`allEdges` versus the preserved schedule**, on a design with more than one
  resolved domain.

Each needs a *fresh scanner-produced certificate* driven through Lean, as the
oracle above does, not a C++ unit test. Until they exist, R15 is an
implementation with a no-regression result and one value gate; the reset,
memory/ROM and `checkDesign` semantics are unvalidated.

Also not implemented: **bus-bit preservation** (refused instead, R15.3) and the
**IO/clock sidecar** — ordinal mapping, `runtime_input_arity`, `used_by_model`,
and the ignored-input / ordinal-gap distinction. Both remain open, and the
sidecar is explicitly incomplete rather than deferred-by-silence.

No upstream contract was touched. `pass.single_edge`, native LEC and the graph
contracts are unchanged, and nothing was weakened to make an export pass.

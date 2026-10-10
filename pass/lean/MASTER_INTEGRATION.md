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

Measured against the **corrected** resolver — design-wide join and the
graph-input root requirement both in place:

| Family | Covered | Exported | Clock refusals fired |
| --- | ---: | ---: | ---: |
| CORE-ET | **125 of 125** | 77 | **0** |
| CVA6 smoke subset | 6 committed wrappers | 5 | **0** |
| DINO | 3 | 3 (all **PROVEN**) | **0** |

The CORE-ET sweep completed 125/125: **77 EXPORTED, 44 EXPORT_FAIL, 4
GATE_FAIL**. The CVA6 figure is a **smoke subset**, not a sweep: the 124
generated wrappers lived under `temp/` and are gone, so this used the 6 wrappers
committed in `scripts/cva6_module_wrappers/`. It shows no clock refusal fires on
CVA6's shape; it is not the 110-module result and must not be read as one.
Regenerating those wrappers from the preserved configuration procedure is open
work (R14.9).

**Not one of the seven refusals fired anywhere.** Every `EXPORT_FAIL` is a
pre-existing cause: `single_edge`, a combinational loop, the const-width gate,
or a `read_slang` failure. `cva6_tlb`, the one CVA6 non-emit, is its usual
combinational loop.

Exactly four modules changed disposition against the R14 baseline, and all four
are the **mask-operand** cases of R14.4 — `intpipe_csr_msgs`, `trans_top`, `vpu_ml`,
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

### R15.9 Two genuinely multi-domain designs, PROVEN

The design-wide join of R15.2 changed what the corpus contains. The fresh
125-module sweep resolves **three** multi-clock designs, not one — and two of
them export, so unlike `intpipe_csr_msgs` they are not blocked by the mask gate:

| Module | Domains | Flop distribution | Verdict |
| --- | ---: | --- | --- |
| `txfma_f1` | **4** — `ctrl_f0_clk`, `ctrl_f1_clk`, `multype_f0_clk`, `multype_f1_clk` | 4 / 2 / 2 / 7 across 15 flops | **PROVEN** 34.2 s |
| `txfma_f4` | 2 — `ctrl_f3_clk`, `ctrl_f4_clk` | 3 / 3 across 6 flops | **PROVEN** 33.2 s |
| `intpipe_csr_msgs` | 2 — `clk_i`, `clk_wr_i` | 51 / 2 plus the memory | refused by the mask gate |

Both proofs went through the audited serial queue and the full report+source
audit. `txfma_f1`'s structure is exactly what R13.1 demands:

```
'txfma_f1_step_correct'        depends on axioms:
  [propext, Classical.choice, Quot.sound, txfma_f1_compiles._native…]
'txfma_f1_step_legacy_correct' depends on axioms:
  [propext, Classical.choice, Quot.sound,
   txfma_f1_clock_bounds._native…, txfma_f1_compiles._native…]
```

and the obligation it discharges is over a **four**-entry table:

```lean
theorem txfma_f1_clock_bounds :
    (∀ f ∈ txfma_f1_designCert.flops,    f.clock < txfma_f1_designCert.clocks.size) ∧
    (∀ m ∈ txfma_f1_designCert.memories, m.clock < txfma_f1_designCert.clocks.size)
```

This is the first time a certificate with more than one resolved clock has been
proven on master. Together with the value oracle (R15.9 below) it is what
R15.6's "the gate should be extended with modules that actually reach
`pass.lean`" was asking for — and `txfma_f1`/`txfma_f4` are better members than
anything in `SWEEP_multiclock6`, because they get all the way through.

### R15.10 Scope: implemented, **not** accepted

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

## R16. Two gates that were not gates

Both were found by reconciling recorded results against the code that produced
them, not by any test failing. Both had been reporting success for a long time.

### R16.1 Legacy replay admitted `axiom … : False`

`pass/lean/tests/replay_legacy_proofs.py` decided its verdict as

```python
ok = exit 0  and  no 'error:'  and  not sorryAx  and  no missing audits
```

None of those four looks at what a proof **depends on**. A pinned fixture
carrying

```lean
axiom unauthorizedFalse : False
theorem evil_comb_refines_fast : True := absurd unauthorizedFalse (by simp)
```

elaborates with exit 0, emits no `error:`, is not spelled `sorryAx`, and prints
the correctly-qualified theorem name — so it satisfied every condition and was
recorded **PROVEN**. Reproduced here directly; the log reads

```
'evil_Lgraph.evil_comb_refines_fast' depends on axioms: [propext, unauthorizedFalse]
```

`axiom_audit.py` rejects that same log on sight. The two checkers had drifted
because there were two of them.

**Fixed by routing legacy replay through the shared policy**, not by adding a
third checker. Scope is preserved exactly: a typecheck-only row has no
obligations and stays `TYPECHECKED` — it is not a proof and must not be labelled
one; a bridge row requires `comb_refines_fast`, plus `next`/`step` when the
design has state (`structure <Top>_state`), plus `graphCert_wf` when the
certificate is chunked.

### R16.2 …and `--resume` let a weaker request stand in for a stronger one

The first repair recorded the auditor's hash in every result and compared it on
resume, so a verdict produced before the policy existed could not be reused.
That was necessary and **not sufficient**, and the comment written beside it
claimed more than the code did: it said the obligation set was compared, and
only the hash was.

The consequence is exact. Take a bridge fixture with no WF theorem, record it
`PROVEN`, then change **only** the inventory to `cert_wf=chunked` and re-run
with `--resume`: it prints `REUSE … PROVEN`, and `graphCert_wf` is never
required of it. The identical request without `--resume` correctly reports
`MISSING_SOURCE_AUDIT: graphCert_wf`.

The expected obligation set is now computed **before** the resume check and
compared against the stored one, together with `scope`, `cert_wf`,
`replay_mode`, the matching verdict kind (`PROVEN` vs `TYPECHECKED`) and the
policy identity — and a stored row must actually have **passed** an audit when
one was owed. Anything else re-runs.

The general shape is worth naming, because it has now appeared twice in this
file: **a cache key must cover what is being asked, not only what is being
read.** Artifact hash and library hash describe the inputs; they say nothing
about the obligations demanded of them.

### R16.2.1 Validation

`pass/lean/tests/replay_legacy_proofs.py` over the 23 tiny bridge artifacts plus
the `axiom … : False` control, with the repaired policy:

```
VERDICTS: {'PROVEN': 23, 'FAILED': 1}
  evil   FAILED   audit=FAIL
    FAIL: `evil_Lgraph.evil_comb_refines_fast` depends on axiom
          `unauthorizedFalse`, which is not in the measured standard set
```

All 23 genuine bridges prove — each is chunked, so `graphCert_wf` was required
and audited — and the control is rejected for exactly the right reason.

`pass/lean/tests/replay_resume_controls.sh`, four controls, none of which
changes a byte of the artifact:

| Control | Result |
| --- | --- |
| bridge → chunked-WF with `--resume` | re-ran (`PROVEN` → `PROVEN`) |
| typecheck → bridge with `--resume` | re-ran (`TYPECHECKED` → `PROVEN`) |
| a pre-policy entry (no auditor hash) | re-ran |
| an **identical** request | still resumes |

The last one is not a formality. A resume that never reuses anything is not a
repair, it is a removal, and it would have passed the first three.

One incidental finding: this run needed `env.json` to prepend the **pinned**
toolchain's `bin`. The replay calls bare `lean`, which from the repository root
resolves 4.34.1 while the `.olean` files are built with `formal/lean`'s pinned
4.31.0 — every artifact failed in 0.24 s with `incompatible header`, which looks
nothing like a toolchain mismatch until you read the first line of the log.

### R16.3 The LEC gate was not running on current master — and now does

`lhd lec` on master **refuses a yosys reader**: `lhd/lhd_options.cpp:797`
permits `--reader yosys-slang` for `lhd compile` only. The runner called the
retired procedure anyway, so every invocation exited 2 with

```json
{"command":"usage","status":"fail","error":{"class":"usage",
 "message":"Yosys readers are only supported by lhd compile; …"}}
```

**162 of 162** recorded `lec_gate.log` files are that usage error, so no CORE-ET
row in **this** corpus carried an RTL equivalence result.

Scope that claim carefully: those 162 logs are from runs against **current
master**. They say nothing about whether the gate worked on an earlier branch,
before the reader restriction and the retirement of `formal.strict`. An earlier
draft of this section said the gate had "never run, on any module"; that is more
than the evidence supports.

Two things kept this invisible. The shell treated a nonzero exit as
"inconclusive; recorded, not fatal" and continued. And `LEC_STRICT=true` did not
harden it: the flag is passed as `--set formal.strict=`, which the usage check
rejects before any setting is read. So the one knob that looked like it would
turn an inconclusive gate into a hard failure could not reach the code that
would have honoured it.

These outcomes are not the same thing and the runner now distinguishes them:

| Outcome | Meaning |
| --- | --- |
| **API/infrastructure failure** | the gate did not run; nothing was compared |
| **UNKNOWN** | the gate ran and the solver did not decide |
| **REFUTED** | the gate ran and the graphs differ |
| **proven equivalence** | the gate ran and decided them equal |

Only the first ever occurred. The gate now **refuses with exit 5** and names the
supported shape, which is the one the error message itself gives:

```
lhd compile --reader yosys SOURCE --emit-dir lg:DIR
lhd lec --ref lg:DIR --impl lg:OTHER
```

— both sides pre-built LGraphs, with `lhd lec` reading no source.

### R16.4 The supported route, and the gate restored

An earlier draft of this section concluded the gate could not be adapted without
substantial work and left it refusing outright. That was wrong, and wrong in the
expensive direction: **only the yosys readers are rejected.**
`lhd/lhd_options.cpp:797` tests `opts.reader != "slang"`, so a source reference
through the **default Slang reader** is supported for `lhd lec`. The two-prebuilt-
graph form in the error message is a debug route, not the requirement.

Two changes were needed, both small:

- drop `--reader yosys-slang` and let the reference come through Slang;
- drop `--set formal.strict=`, which is **retired** and is itself a second usage
  failure. Native UNKNOWN already exits nonzero without it.

**The reference frontend is therefore Slang while the implementation side is
built by `inou.yosys.tolg` with yosys-slang.** That is worth stating rather than
burying: the two sides come through *different* front ends, which is exactly
what makes this a comparison and not a self-comparison.

Verified on this binary, both directions:

| Control | Exit | Verdict | Detail |
| --- | ---: | --- | --- |
| identical RTL | 0 | `proven` | structurally identical, no solver call |
| `y` mutated to `(a+b)^1` | 10 | `refuted` | `counterexample: diff y(ref=1 impl=0) @ a=0, b=0` |

and then on a real CORE-ET module: **`intpipe_imm` PROVEN equivalent to the
RTL** — the first genuine equivalence result in this corpus.

`scripts/lec_gate_controls.sh` keeps both directions runnable. A gate nobody has
watched *refute* something is a gate nobody knows can fail, which is the whole
lesson of the 162 logs.

The runner now separates the four outcomes and **refuses to continue on any of
them but `proven`**: a missing result JSON is an infrastructure failure (exit 5),
`refuted` is fatal and prints the counterexample (exit 4), and anything
undecided is fatal too (exit 6) — an undecided gate is not a passed gate.
`RUN_LEC_GATE` stays **false by default** because an export census does not want
a solver run, and because an export row should plainly be an export.

**The binary is a debug build.** It announces `lec-debug-build-slow`, and the
controls now warn about it. That is fine for correctness and disqualifying for
timings: any timed replay must fingerprint the actual binary and build mode
rather than trusting the `bazel-bin` symlink name, and record it alongside the
pinned Lean/Mathlib environment.

### R16.5 The result handling could be told a lie

Before any coverage number, the gate's *result handling* had to be fixed. It
dispatched on a grepped `verdict` while capturing the process exit, printing it,
and never reading it again. Two controlled responses walked straight through:

```
process exit 7  + verdict=proven  ->  runner exited 0, printed PROVEN
status="fail"   + verdict=proven  ->  runner exited 0, printed PROVEN
```

A `verdict` is a claim made by a run that may itself have failed. It means
nothing unless that run succeeded and the rest of the record agrees with it.

`scripts/lec_result_check.py` now validates the record structurally: process
exit 0, parseable JSON object, `command=lec`, `status=pass`, `exit_code=0`
**agreeing with the observed process exit**, and `lec.verdict=proven`. Every
disagreement is a rejection with the reason named, and the outcome class
(usage/infrastructure, REFUTED, undecided, inconsistent) is reported so a caller
can tell a fixable input problem from a real refutation.

Rewriting it caught a second error immediately: **the verdict is nested at
`lec.verdict`, not at the top level.** The first validator looked top-level and
rejected a genuinely proven module — the right direction to fail in, and a
reminder that it would have *accepted* a malformed record that happened to put
the field where I expected it. That is now a control.

`scripts/lec_admission_controls.sh`: ten rejections — exit 7 with proven,
`status=fail` with proven, refuted, undecided, no `lec` object, verdict at the
top level, a usage rejection, malformed JSON, a missing file, and a recorded
`exit_code` disagreeing with the process exit — plus one genuine record
accepted. The last matters: a checker that rejects everything is not a checker.

This is the third time in this file the same mistake appears — **grep one field
instead of validating the structure**. The axiom audit was a line count, the
resume key omitted the obligations, and this dispatched on one string. Worth
naming as a pattern rather than fixing a third time in isolation.

### R16.6 The reference could not read the benchmark's filelist

`lhd lec` cannot see the filelist. `load_side_graphs`
(`lhd/lhd_kernel_formal.cpp:157`) forwards `compile.slang` settings to the
reference read but **not `opts.raw_args`**, so every `-- -I …` / `-- -F …`
passed to `lhd lec` was silently dropped. That is why supplying the include
directory by hand changed nothing, and it is the real reason the earlier
concatenation workaround existed at all.

`lhd compile` *does* honour raw args, so the reference is built there and `lhd
lec` compares two pre-built graphs:

```
lhd compile --reader slang --top TOP --emit-dir lg:REF -- -F FILELIST --relax-enum-conversions
lhd lec --impl lg:LG_NORM --ref lg:REF --top TOP
```

The filelist keeps its order, includes and defines. **The two sides come from
different front ends by construction** — the reference through direct Slang, the
implementation through `inou.yosys.tolg` (yosys-slang) — so this is a real
comparison, not a graph copy.

One asymmetry is recorded rather than papered over: `--ignore-assertions` is a
yosys-slang flag that direct Slang **rejects**, so it is not passed to the
reference, and the reference therefore does not share the implementation's
assertion handling. A module whose semantics depend on assertion lowering needs
that benchmark contract settled before its result means anything.

### R16.7 Measured coverage

Seven CORE-ET modules ([SWEEP_lec_sample.tsv](SWEEP_lec_sample.tsv)), on the
adapted route:

| Module | Runner exit | Native `exit_code` | `lec.verdict` |
| --- | ---: | ---: | --- |
| `intpipe_alu` | 0 | 0 | **proven** |
| `intpipe_imm` | 0 | 0 | **proven** |
| `intpipe_inst_bits_stage` | 0 | 0 | **proven** |
| `debug_breakpoint` | 0 | 0 | **proven** |
| `intpipe_csr_file_fl_barrier` | 0 | 0 | **proven** |
| `txfma_f4` | 0 | 0 | **proven** |
| `txfma_f1` | 6 | 7 | unknown |

**Six genuine RTL equivalence results**, where this corpus previously had none,
and **no input failures left**. `txfma_f4` moves from "the reference would not
elaborate" to PROVEN purely by fixing how the reference is built.

The two exit columns are deliberately separate. An earlier version of this table
printed the runner's classification as though it were the observed process exit
— a predicted mapping presented as a measurement. On the concatenated route
`txfma_f1`/`txfma_f4` were **native 6 / runner 5**; on this route `txfma_f1` is
**native 7 / runner 6**.

`txfma_f1` is an **unsupported phase-schedule REFUSAL**, not a timeout and not
ordinary solver inability:

```
phase schedule refused: the phase schedule needs a total order between
5 unrelated clock roots {\x02111, ctrl_f0_clk, ctrl_f1_clk,
multype_f0_clk, multype_f1_clk}; v1 orders ONE root clock
```

The schedule *refused*; **nothing was compared**. Raising a timeout cannot help,
and recording this as "the solver could not decide in time" would be wrong.

An earlier draft of this section got the rest of it wrong too, in two ways.

**`\x02111` is not a clock named `111`.** It is the anonymous internal-root key:
`graph/latch_contract.cpp:551` and `pass/lec/phase_sched.cpp:387` both spell an
internal root as `"\x02" + class_index`, reserving plain names for graph inputs.
The log shows it JSON-escaped as `\u0002111`. I had read `net_key()` earlier in
this work and still misread its output.

**And there is no disagreement between the analyses.** Measured per side,
read-only:

| Graph | `pass.single_edge` reports |
| --- | --- |
| implementation (yosys-slang) | **4 clock nets** |
| reference (independent Slang) | **4 clock nets** |

Both counts match the Lean scanner's four resolved domains (R15.9).

**Equal counts are not identical inventories, and this does not identify the
anonymous root.** Three per-side 4s are consistent with the fifth root being a
miter artifact — which is what `net_key()`'s own comment leads one to expect, an
internal key being "stable within a graph … the miter-wide rule is that BOTH
sides normalize, not that internal keys match across sides" — but a count is not
a mapping. **Which state element, port or net `\x02111` corresponds to is an
open question**, and settling it needs the per-side root *sets* compared
element-by-element, not their sizes.

What is established is narrower: the earlier comparison was invalid on its face,
because it set a count taken from one graph against a count taken across two.
**Compare root inventories per exact graph side before concluding that two
analyses disagree** — and compare the inventories, not their cardinalities.

The disposition: a known native scheduler refusal on a multi-root miter,
recorded and not worked around. It does not block the Lean value gates, which
are independent of it.

Native LEC, the frontend and `single_edge` are untouched: this was a runner/CLI
problem throughout. **Export-only rows remain useful and are not RTL equivalence
proofs** — nothing in R14 or R15 claimed otherwise, but the `lec_gate.log` files
sitting beside those rows did.

## R17. The declared async read/commit contract is tied in one direction only

`DesignCert.lean` states the contract plainly: `FlopDesc.asyncReset` is the
commit side, "the READ side of the same fact is `SourceDesc.flopQAsync`, and the
checker ties the two (`asyncFlagMismatch`)."

It ties one direction. `checkSource`'s `.flopQAsync` branch enforces
`flopQAsync → asyncReset`; its `.flopQ` branch checks **width and ordinal range
only** and never consults `f.asyncReset`, so `asyncReset → flopQAsync` is not
enforced.

Exercised both ways on a pinned fixture
(`generated/master_port/reset_contract/AsyncContract.lean`, elaborates clean):

| Certificate | `checkDesign` | `checkRuntime` | Visible Q | Committed state |
| --- | --- | --- | ---: | ---: |
| `asyncReset := true`, read as plain `flopQ` | **admitted** | **admitted** | **99** | 7 |
| same, read as `flopQAsync` | admitted | admitted | **7** | 7 |
| `asyncReset := false`, read as `flopQAsync` | **refused** (`asyncFlagMismatch`) | — | — | — |

Reset asserted, clock **quiet**, pre-state Q = 99. Both admitted certificates
**commit the same next state**; they differ only in what they expose. That is
the hardest kind of difference to notice — nothing downstream contradicts
itself, the design simply shows a value its own commit side has already
discarded.

### R17.1 Correction: the "171 artifacts, zero mismatches" claim was false

That claim was wrong, and wrong because of how it was measured. The scan keyed
FlopDescs on the field under test:

```python
re.findall(r'\{ width := \d+, din := \d+,[^}]*asyncReset := (true|false) \}', txt)
```

Every artifact emitted **before** `asyncReset` existed has no such field, so the
match returned nothing, the loop body never ran, and those files silently
contributed "zero mismatches". The right answer for them was not zero — it was
*this method cannot see them*.

Re-measured, parsing on a field that is always present (`resetActiveLow`) and
treating an absent `asyncReset` as `false`, which is the Lean default:

| Corpus | Files | Flops | No `asyncReset` field | `flopQAsync` on `asyncReset=false` | Files affected |
| --- | ---: | ---: | ---: | ---: | ---: |
| **fresh** CORE-ET (`step2_ff`) | 81 | 1,142 | 0 | **0** | 0 |
| **fresh** DINO (`step2b_dino`) | 3 | 194 | 0 | **0** | 0 |
| **historical** CVA6 (`cva6_vc`) | 87 | 2,056 | **2,056** | **2,043** | **51** |

So: **84 fresh artifacts, zero mismatches in either direction** — that part
holds, and it is the part that says the current scanner is sound. But **51 of
the 87 historical CVA6 artifacts carry 2,043 `flopQAsync` sources on descriptors
whose `asyncReset` defaults to false**, which is the *enforced* direction and is
refused by `checkDesign` as `asyncFlagMismatch`.

Confirmed on a real one rather than inferred: the historical
`csr_regfile_gate_Lgraph.lean` (534 `flopQAsync` sources, 534 FlopDescs, none
carrying the field) still **proves its two generic theorems**, and
`checkDesign` **refuses it**. The certificate was not modified — only
`DirectCheck` imported and guards appended.

**This is the known re-emission requirement from R13, not a scanner
regression.** Those artifacts predate clock provenance; they were never emitted
with the commit-side flag. The lesson for the ledger is narrower and sharper:

> **A compiler/`allEdges` typecheck does not imply runtime admission.** The two
> theorems say the compiled model equals the interpreted one; they say nothing
> about whether `checkDesign` would accept the certificate. Historical artifacts
> must not be counted as runtime-accepted on the strength of an old proof.

Fresh and historical corpora are kept separate from here on, and a claim about
one is not evidence about the other.

The unenforced direction (`asyncReset := true` read as plain `flopQ`) occurs
**0** times anywhere — fresh or historical. The scanner writes both sides from
one field (`Flop::asynchronous`), so it cannot produce it; the R17 fixture is
synthetic and exposes a **checker admission gap** rather than a defect in
anything emitted.

**The disposition is a semantics decision and is left open.** Tightening
`.flopQ` to refuse `asyncReset := true` would reject certificates the checker
accepts today, which is a change to the admission policy rather than a bug fix,
and the standing rule is that semantic changes go to the user first. The two
readings:

- **admission-policy correction** — the comment says the two are "the same
  fact", so a certificate asserting one and not the other is ill-formed and
  should be refused;
- **explicit checker limit** — plain `flopQ` on an async flop is a *coherent*
  model (a Q that is not combinationally reset), and the checker deliberately
  admits it, in which case the contract comment overstates and should be
  narrowed instead.

Interpreter and compiler equations are unchanged either way; this is entirely
about what `checkDesign` admits.

## R18. Memory value gates across quiet and firing domains

A memory commits on its own clock, and a synchronous read is a **register** that
commits on its port's clock.

Be precise about what is new here. Ordinary write-then-read behaviour was
perfectly observable in the `allEdges` model and is not a product of clock
resolution. What requires a **quiet** edge is the clock-conditioned **hold**:
that a write does *not* commit, and that a read register does *not* load, when
their domain does not fire. An earlier version of this section said neither was
observable while every clock fires, which overstates it.

The fixture is built by the **real scanner**: a 4-entry RAM with a write port
and a synchronous read port on `clk_a`, plus a synchronous ROM on `clk_b`.

### R18.1 What the scan resolved

| | |
| --- | --- |
| clocks | 2 — `clk_a`, `clk_b` |
| `MemoryDesc`s | **1** — the RAM. The ROM carries none, as a `memConst` |
| synthetic read registers | **2**, on **different** ordinals |

The RAM's image and its read register agree on the domain; the ROM's register is
on the other one. That is R15's "a synthetic sync-read flop inherits its
**port's** domain", confirmed on a real fixture rather than asserted. The test
checks all three facts **before** any value case runs — without that, every
assertion below would still pass against a model that collapsed both registers
onto one clock.

### R18.2 The gates — 17 cases, all decided

- **`clk_a` quiet: the write does not commit**, checked at **every one of the 4
  addresses**, not only the one written.
- **`clk_a` fires: the write lands at address 2 and nowhere else**, again
  checked at all four.
- **Read registers commit per port**: `clk_a` quiet with `clk_b` firing holds
  the RAM register at 99; `clk_a` firing with `clk_b` quiet holds the ROM
  register; both quiet moves neither.
- **Enable off with the clock firing**: still no commit — the enable is not
  being papered over by the edge.
- **`checkRuntime` rejects a state whose memory count disagrees** with the
  design.
- **`checkDesign` refuses a memory on an undeclared ordinal**, with
  `memClockOutOfRange`.
- **`allEdges` is every domain firing**, and the emitted step matches the
  preserved interpreter on a design *with memory*.

### R18.3 The mixed-domain refusal, on a real fixture

Building the same design with the read port's `clock_pin` left unconnected makes
it resolve to the implicit domain while the write port is explicitly on `clk_a`.
The scanner refuses it:

```
memory n_16 (`memory_16_1`) commits on more than one clock domain:
port 0 on `clk_a` and port 1 on `clock`. A certificate carries ONE
MemoryDesc.clock, so these cannot be represented without merging two
time bases into one.
```

This is R15.2's join rule declining to guess. The design-wide join folds an
implicit element into a **unique** explicit domain — but this design has two,
`clk_a` and the ROM's `clk_b`, so there is nothing to join the implicit port to.
`DesignScanClocks.MixedDomainMemoryIsRefused` pins both the refusal and its
reason.

### R18.4 Direct versus compiled — the `Direct*` modules, finally exercised

Until now the `Direct*` modules had never been run on a scanner-produced design
at all (R14.2): they were ported, built and type-correct, with no real design
behind them. Both fixtures now compare the **checked direct interpreter**
against the **compiled residual** via `diffStep`/`diffTrace`.

**Memory fixture**, 29 cases:

- `diffStep` across **all four edge vectors**, sampling **every** tiny address,
  and again with the write disabled so a spurious commit on either side shows;
- the RAM starts at **[11, 22, 33, 44]**, not a uniform fill — a read wired to
  the wrong address is then observable, where a uniform image would let both
  sides agree while reading from the wrong place;
- **expected loaded values**, not merely agreement: `raddr=3` loads 44 and
  `romaddr=2` loads 3; `raddr=1` loads 22 and `romaddr=0` loads 1. Two
  implementations can agree and both be wrong;
- a **two-step trace** for no-forwarding (`fwd = 0`): the committing step reads
  the **old** cell (22) and the new value (77) appears only on the following
  read. One step cannot distinguish that from a broken write;
- `diffTrace` agrees across that whole trace.

**Reset fixture**, 44 cases: `diffStep` across every edge vector with the reset
both asserted and deasserted — reset flavour being where the two
implementations are most likely to drift, since the direct interpreter applies
an async reset with no edge while the residual must encode it in its binding
order — plus a reset/load/hold `diffTrace` with the expected values pinned at
each step (7, then 5, then 5 held while quiet).

### R18.5 Byte enables and forwarding

`memory_lowering.cpp` emits **`MemWriteBE`** once `wensize > 1`, resizing the
enable to one bit per lane. A selective write is therefore a **different
operator**, not a different argument — and it was untested. A second scanner
fixture uses four 2-bit lanes over 8-bit cells with `fwd = 1`, and the test
asserts a `MemWriteBE` node is actually present before any value case runs,
because with a plain `MemWrite` every lane assertion would quietly be exercising
the single-lane path again.

12 cases, all decided, against a cell holding `0xFF`:

| Enable | Result |
| --- | --- |
| `0b1111` | whole cell written → 0 |
| `0b0000` | **nothing written**, with the clock firing |
| `0b0011` | low lanes only → **240** (high nibble survives) |
| `0b1100` | high lanes only → **15** (low nibble survives) |
| `0b1111`, clock **quiet** | nothing written, whatever the lanes say |

A selective write leaves other addresses alone. And forwarding is contrasted
directly against the `fwd = 0` fixture: with `fwd = 1`, a same-address read on
the **writing** step sees the **new** data (0), while a different address is not
forwarded and reads the committed cell (255) — where the `fwd = 0` fixture reads
the old cell on that step and only sees the new value on the next.

`diffStep` covers all four lane patterns across every tiny address.

### R18.6 Still open

**Write-port collision ordering** — this fixture has one write port, so the
`fwd` matrix's per-(read, write) structure is exercised only in its 1×1 case.

The **IO/clock sidecar** and **graph-closed publication** remain untouched.

## R19. The IO/clock sidecar, and publishing a pair without losing one

A `DesignCert` names its inputs by **ordinal and nothing else**. A caller holding
an RTL port list has no way to build the input array from it: which port is
ordinal 3, how wide it is, whether the model reads it at all, and how long the
array must be are all outside the certificate. That mapping is the sidecar
(`pass/lean/design_io_meta.{hpp,cpp}`).

It is derived from the **same owned scan and certificate IR the emitter
formats**, never from the graph. A sidecar reconstructed from HHDS could
disagree with the certificate it describes, and the disagreement would surface
only at runtime.

### R19.1 What it records, and the bug the tests caught

| Field | Why |
| --- | --- |
| `runtime_input_arity` | how long `RuntimeInput` must be |
| per-input `ordinal`, `width`, `used_by_model` | which slot, how wide, read or ignored |
| outputs in emission order | position *is* identity in the output array |
| clocks with ordinals and `implicit` | which `ClockEdges` entry drives which domain |

**The runtime ordinal is the index into `design.inputs`, not `Port::id`.**
`Port::id` is the scan's internal source id — an opaque 2,000,000,000-based
counter — which `certificate_ir.cpp` remaps to the dense position that actually
indexes `RuntimeInput`. The first implementation published `Port::id`, so a
caller driving a four-input design would have been handed ordinals in the
billions. The test caught it; nothing else would have until a runtime tried to
use the file.

**Arity maxes over both source kinds**, matching `Direct.inputArity`: `.input`
ordinals and `flopQAsync`'s reset ordinal. A certificate may reference a reset
ordinal **only** through `flopQAsync` — that is well-formed — and an arity
computed from `.input` alone would be too small for it, so `checkRuntime` would
reject an array a caller had built correctly from the sidecar. (For a
*scanner-produced* certificate the two agree: `build_certificate` sets
`reset_pin` via `dep(*f.reset, 1)`, which also creates an Input source. An
earlier draft claimed a reset port is *never* an `.input` source, which is
wrong; the `max` is justified by the general case, not by that.)

**Unused ports are listed, never compacted.** Ordinals are dense, so preserving
"gaps" means keeping every declared port in the list: dropping an unused
*interior* port would shift every later port down one and silently misalign the
caller's array. Both an interior and a trailing unused port are covered, with
their declared widths.

JSON escaping is tested against quotes, backslashes and control bytes — not
theoretical, since port names come from RTL and a clock name can be
`net_key()`'s `\x01`/`\x02` sentinel.

### R19.2 Emission is graph-closed

`emit_design_cert` and `build_io_meta` both take a `DesignScan` and a
`CertificateIR` and nothing else. `DesignIoMeta.EmissionAndMetadataOutliveTheGraph`
builds a design, scans it, lets the graph go out of scope, and *then* emits both
the certificate and the sidecar — so the property is demonstrated rather than
asserted. Neither certificate nor model equations changed, and no import
requirement widened.

### R19.3 Publishing the pair: the first rollback was wrong

A certificate and its sidecar are only useful together, and publishing them is
**two renames**. D2's helper, on a failed second rename, removed the newly
published certificate and did **not** restore the preexisting one — losing
*both*.

**The first version here reproduced that loss by a different route.** Its catch
assumed that, by the time anything threw, both originals had been backed up — so
it deleted both targets on the way to restoring them. When the **first backup
rename** failed (a directory at `cert.lean.bak` is enough), the original had
never been moved, the catch deleted it anyway, and the restore then moved that
directory into its place. Measured on the committed source:

| Fault | Result before the fix |
| --- | --- |
| `cert.lean.bak` blocked | **A lost**, replaced by the directory; **B deleted** |
| `io.json.bak` blocked | A restored; **B lost**, replaced by the directory |

It also removed both backups unconditionally at the end of the catch, including
when a restore had just failed — destroying the only remaining copy.

**What it does now.** Every undo is conditioned on having done the thing:
`moved_a`, `moved_b`, `put_a`, `put_b` are tracked, and only targets this call
actually published are removed. A pre-existing `.bak` is **refused before any
mutation**, because it is either an interrupted publish's only copy of a
previous artifact or an unrelated file, and renaming over it destroys it. A
backup whose restore fails is **kept**, and the failure is reported by name
rather than swallowed — at that point it is the sole copy.

### R19.4 The fault tests

The earlier test threw during **staging**, before any target was touched, so it
never exercised rollback at all. Each case below is a distinct point of failure:

| Case | Expected |
| --- | --- |
| blocked backup A | refused up front; old pair intact; the collision left alone |
| blocked backup B | refused up front; old pair intact |
| first final rename fails | old pair restored; no litter |
| second final rename fails | old pair restored; no litter |
| staging failure | old pair intact |
| staging failure, **fresh** targets | neither file created |
| success | both replaced; `.tmp`/`.bak` cleaned |

All pass. Scratch directories are **project-local**
(`generated/pass_lean_runtime_tmp/publication/<case>`), not
`temp_directory_path()`: `/tmp` is shared between parallel jobs on this box.

**The failure model, stated accurately.** Exception- and
command-failure-safe. **Not crash atomic** — two renames cannot be, and a crash
between them leaves the new first file beside the old second. **Not
concurrency-safe** — backups are keyed on the target names, so a second
publisher races them. And a **persistent filesystem error can prevent
restoration**; the backup is then retained and named in the exception rather
than cleaned up.

### R19.5 Production integration

`pass_lean.cpp` published only the certificate; `build_io_meta`/`io_meta_json`/
`publish_pair` had no production caller at all. `Pass_lean::emit_for_graph` now
publishes `<base>_Lgraph.lean` and `<base>_io.json` as a pair, from the **same**
scan and certificate, for **both** emitters — `verified_compiler` and legacy,
with their options and equations unchanged. The sidecar's `top` is the emitted
`base_name`, so a `--top` override renames both halves together and the metadata
describes the artifact actually written.

Verified end to end on a real CORE-ET module, both modes:

```
verified: intpipe_imm_Lgraph.lean + intpipe_imm_io.json
legacy:   intpipe_imm_Lgraph.lean + intpipe_imm_io.json
```

parsing as `{"schema_version": 1, "kind": "lean_design_io", "top": "intpipe_imm",
"runtime_input_arity": 2, …}` with dense ordinals and one implicit clock.

### R19.6 Two corrections to R19

**The schema discriminator was missing.** `"kind": "lean_design_io"` is how a
consumer that reads several sidecar kinds identifies this one — not the
filename. Restored, and the JSON test now **parses and decodes** rather than
searching for escape sequences: a round trip proves a consumer reads back the
byte that went in, where finding `\u0001` in the text only proves something was
written.

**The "never an `.input` source" claim was wrong.** `build_certificate` sets
`reset_pin` through `dep(*f.reset, 1)`, which *does* create an Input source, so
for a scanner-produced certificate the two agree and the reset test does not
demonstrate the omission it claimed. What remains true, and is why the code
takes a `max`, is that `Direct.inputArity` maxes over both — and a certificate
referencing a reset ordinal **only** through `flopQAsync` is well-formed, so an
arity from `.input` alone would be too small for it.

### R19.7 Closing the remaining gates

**Restoration failure is now covered.** Reaching it from outside `publish_pair`
needs a fault injected between moving an original aside and putting it back —
which an external harness *can* do by interposing on `rename`, and an
independent control has. Rather than add an injection seam to production code so
an in-process test could do the same, the rollback decision is a function,
`detail::rollback_pair`, driven directly. That is a choice of test method, not a
claim that the path is unreachable. With a non-empty directory blocking
A's restore: the backup is **retained with its original content** and **named in
the returned list**, while B restores normally and its backup is cleaned. The
earlier code removed both backups unconditionally at that point, destroying the
only remaining copy.

**The contract in `lean_format.hpp` said too much.** "Every failure leaves
whatever was there before" is false when the *unwind itself* fails. It now says:
the previous artifact is not lost — it is in its `.bak`, and the exception names
that path — but the targets are not promised to be back where they were.

**A reset ordinal referenced only through `flopQAsync`** is now tested on a
hand-built certificate, explicitly *not* claiming the scanner emits one.

**Legacy emission after the graph's lifetime** is covered alongside the verified
emitter; both are wired to `publish_pair`, so both must hold the property.

**Scope of the in-test JSON reader, stated precisely.** `json_string_field` is a
field/escape decoder: it finds one `"key": "..."` by substring and decodes that
string. It proves a byte round trips and would not notice a structural error
elsewhere. The whole-document gate is `scripts/check_io_sidecar.py`.

**That validator's first version was a spell-checker.** Driven through its own
CLI it accepted a negative `runtime_input_arity`, negative and zero widths, a
boolean where an ordinal belongs, outputs with no name or width, a clock with no
name, and a document with a **duplicated `top` key** — and crashed with an
`AttributeError` on a port that was not an object. Two traps behind that:

- `isinstance(True, int)` is **True** in Python, so a JSON boolean passed every
  integer check. Integers are now tested with `type(x) is int`.
- `json.load` keeps the **last** of duplicated keys silently, so a document can
  say two different things and parse clean. An `object_pairs_hook` now rejects
  it.

`scripts/check_io_sidecar_controls.sh` pins 15 rejections plus a valid document
and the real emitted sidecars, all through the actual CLI.

**What `runtime_input_arity` is checked against.** `Direct.checkRuntime` tests
`i.size < inputArity D`, so the *runtime* rule is a **lower bound** — an input
array longer than the arity is accepted, and only the edge vector is exact.
Nothing here rejects a padded input array, and an earlier phrasing implied Lean
would. The metadata helper emits the **canonical** value, one past the highest
referenced ordinal, so the validator checks that exact number rather than merely
a sufficient one: a larger arity would not be refused by Lean, but it is not
what this emitter produces.

**Top override, pinned in production.** `--set formal.lean.top=renamed_top`
emits `renamed_top_Lgraph.lean` **and** `renamed_top_io.json`, with the
sidecar's `top` field reading `renamed_top` — both halves renamed together, and
the metadata describing the artifact actually written. Verified sidecars from
all three production runs (verified, legacy, override) pass
`check_io_sidecar.py`.

### R19.8 Still open

**Write-port collision ordering is covered** (R20).

A **real multi-clock CORE-ET direct/compiled trace** (`txfma_f4` is already
exported and suitable) remains the next real-design gate.

The configured 125 CORE-ET + 61 CVA6 + 3 DINO replay is untouched, and
D3/D4/FP are not started — their old clockless `CompileDesignDefs`/`Reify`
interfaces must not overwrite the post-D2 clock/reset transport when they are.


## R20. Write-port collision ordering, from the scanner

Every memory fixture before this had **one** write port, so the `fwd` matrix —
whose bit `(r * n_writes + w)` means "read *r* sees write *w*'s new data" — was
only ever exercised in its 1×1 case. Two writes and two reads is the smallest
shape where two things become observable.

The fixture is scanner-produced: a 4-entry memory with write ports 0 and 1 and
synchronous read ports 2 and 3, all on one clock, `fwd = 0b0110` — read 0
forwarded write 1 only, read 1 forwarded write 0 only. The test asserts **two**
read registers exist before any value case runs; with one, the per-read rows
would be a single shared value and the disagreement they exist to show could not
appear. `collide_oracle.sh` repeats that guard.

18 cases, all decided:

| Case | Result |
| --- | --- |
| both writes → address 1, data 55 and 77 | **77** — the later port wins |
| only the lower-priority write enabled | **55** — so the ordering is not just "the last value seen" |
| different addresses (0 and 2) | both land; all four addresses checked |
| **read 0 vs read 1**, both reading address 1 | **77 vs 55** — the two reads *disagree* |
| quiet clock | nothing commits, all four addresses |

The disagreeing reads are the point. A single shared forwarding row would make
them equal, and every other assertion here would still pass.

`diffStep` covers all three input patterns across firing and quiet clocks,
sampling every tiny address.

**Corroboration, not acceptance.** An independent control exercises 65,536 cases
over forwarding matrices 0/6/9/15, all 16 masks per writer, same and different
addresses, four edge vectors and four read-enable vectors — but it builds its
certificates directly, bypassing the HHDS scanner. It confirms the *lowering*;
this gate is what confirms the scanner produces a certificate with that
structure. Upstream `ware/rtl/cgen_memory_1rd_2wr.v` and
`spec_memory_ww_last_wins` agree on per-lane higher-port priority. The existing
refusals — undefined, whole-array, and unknown or over-62-bit forwarding
policies — are unchanged.

## R21. A real multi-clock design, traced

Every gate so far used a fixture built to be testable. This drives
**`txfma_f4`**, an actual CORE-ET export, and it reads the input and clock
ordinals **from its sidecar** rather than hard-coding them — which is what the
sidecar is for, and makes the run a test of that file too.

`scripts/gen_multiclock_trace.py` refuses to emit a trace for a design with
fewer than two clocks, so this cannot quietly degenerate into a single-domain
run.

| | |
| --- | --- |
| clocks | **2** — `0 = ctrl_f3_clk`, `1 = ctrl_f4_clk` |
| inputs | 13, of which **11** are read; `runtime_input_arity` 13 |
| outputs | 5 |
| flops | 6 |

**19 cases, all decided**, across all four edge vectors — all quiet, all firing,
`ctrl_f3_clk` alone, `ctrl_f4_clk` alone:

- `checkDesign` admits the artifact, and `checkRuntime` admits every vector;
- `diffStep` agrees between the **checked direct interpreter** and the compiled
  residual on each one;
- `allEdges` is the all-firing vector, and the emitted step equals the preserved
  interpreter; a **quiet domain is not** that schedule;
- a **4-tick `diffTrace`** carries state forward with varying inputs and edge
  vectors, so a commit that should not have happened diverges later even when
  the step it happened on looked fine alone.

### R21.1 The state widths were wrong, and the checker could not see it

The first generator built **every flop as `mk_bv 1`**, whatever the design
declared — `txfma_f4`'s flops are **[1, 52, 1, 32, 1, 1]**. `checkRuntime`
compares array **lengths**, not value widths, so it admitted that stimulus and
the trace "passed" against state no hardware would produce.

The checker is **not** changed: its length rule is what it is. The generator now
reads widths from the certificate's own descriptors, and the state reads
`#[mk_bv 1 0, mk_bv 52 42, mk_bv 1 0, mk_bv 32 116, mk_bv 1 0, mk_bv 1 1]`.
One-bit elements cannot all be distinct, so they alternate; wider ones get
distinct values.

It also **validates the sidecar against the artifact** — dense clock ordinals,
input ordinals matching position, and no flop naming an undeclared domain —
rather than trusting the file it reads.

### R21.2 An oracle from the RTL, not from the model

`txfma_f4.sv:54-58`:

```systemverilog
always_ff @(posedge ctrl_f4_clk) begin
  int_minmax_res_f5a_h <= int_minmax_res_f4a_h;
```

The sidecar maps input ordinal **8** = `int_minmax_res_f4a_h` and output
ordinal **3** = `int_minmax_res_f5a_h`, both 32-bit; the certificate puts that
flop at ordinal **3**, 32-bit, on domain **1** = `ctrl_f4_clk`. All three agree,
and the expectation is **predicted from the source** rather than read off the
model:

| | |
| --- | --- |
| `ctrl_f4_clk` fires | flop 3 **commits** input 8's value |
| only `ctrl_f3_clk` fires | flop 3 **holds** |
| nothing fires | flop 3 **holds** |
| following tick's output 3 | shows the committed value |
| **the commit tick's own** output 3 | still shows the **old** value |

That last row matters: the output reads the flop's current value, so the commit
is visible one tick later. The pre-state timing was not adjusted to make an
expectation pass — the expectation was written to match it.

Full report+source axiom audit passes. Provenance
(`generated/master_port/txfma_trace/provenance.json`) records sha256-16 over the
artifact, sidecar, trace, filelist, `lhd` binary and generator, plus the RTL
oracle's file and line range, the Lean version and the commit.

### R21.3 What this does and does not establish

One real multi-clock design, four edge vectors, a 4-tick trace, with one
flop's behaviour pinned against its RTL. It is not the configured replay and not
RTL acceptance — `txfma_f4`'s `lhd lec` comparison is separate (R16.7). What it
establishes is that the clock-resolution work holds on an artifact nobody built
for the purpose, with stimulus at the widths the hardware declares.

## R22. Destination ledger: two harness defects to carry forward

Neither is a model or semantic defect. Both are **validation harnesses that
report success without having checked what they claim**, and both are recorded
here so the repair travels with the thing being restored.

### R22.1 `mem_lean_diffsim.py` accepts mismatched trace lengths

The historical D2 runtime tool compares an RTL trace against a Lean one over
`min(rtl_rows, lean_rows)` rows. Measured on the actual script with controlled
subprocess outcomes:

| RTL rows | Lean rows | Expected | Verdict |
| ---: | ---: | ---: | --- |
| 13 | 12 | 12 | **PASS** |
| 12 | 13 | 12 | **PASS** |
| 12 | 12 | 12 | PASS (correct) |
| 11 | 12 | 12 | FAIL (correct) |

An **extra** row on either side is truncated away silently. What `min` *does*
catch is a trace short on **either** side, because the minimum then falls below
`expected` — the `rtl=11 / lean=12` row above fails for exactly that reason. The
blind spot is the opposite direction: a simulator emitting one period **too
many** passes, on either side. An earlier wording here said only a trace short
on *both* sides is caught, which is wrong.

**When that tool is restored**, it must require **exact** trace counts and
output-column shape, while preserving the behaviour that is correct: `P=1`
before-first-edge and `P=2` after-first-edge sampling, the named sidecar
mapping, and state initialization. This is an inherited admission defect, not a
semantic change, so the comparison itself stays as it is.

Generic simulator launchers and the actual D2 benchmark procedures also remain
to be restored before D2 acceptance.

### R22.2 `coreet_full_flow.sh` called a stale row a finished run

Fixed, and recorded because the shape recurs. Completion was "a non-empty row
file exists for every requested module", which says nothing about **who wrote
it**. With a previous run's rows present and the current launcher failing
outright, the sweep printed `COMPLETE` and exited 0 having run **zero** workers.
The launcher's exit status was never checked at all.

Rows now live in a directory named for the invocation, completion reads only
that directory, and the launcher's status is propagated. Previous evidence is
preserved rather than cleaned up — it is still evidence, of a different run.
`scripts/coreet_sweep_freshness_controls.sh` pins all four cases.

**The general rule, since this is now the fourth instance in this file:** a
check must be keyed to the thing it claims. The axiom audit counted lines
instead of comparing names; `--resume` compared inputs instead of the question
asked; the LEC gate read one field instead of validating the record; this read a
filename instead of a run. Each passed for a long time.

## R23. The replay harness, and six ways it accepted work it had not checked

The configured replay is a harness, and every defect in it so far has been one
shape: **a check keyed to something other than the thing it claims**. They are
recorded together because the pattern is the finding.

### R23.1 The defects, in the order they were caught

| # | The check | What it actually verified | Measured consequence |
| ---: | --- | --- | --- |
| 1 | job `passed` | export exit 0 **and a file exists** | a worker writing `THIS IS NOT VALID LEAN` passed in every mode |
| 2 | mode flags | `EMIT_FAST_BRIDGE`, a name the runner ignores | the runner's default happened to be right; the request did nothing |
| 3 | job identity | `(family, top, mode)` | `cva6_tlb` and `legacy_cva6_tlb_gate` share a top and collided |
| 4 | proof scope | the **artifact's** own structure (`auto`) | a verified certificate satisfied a `legacy_chunked_wf` request |
| 5 | re-audit promotion | `axiom_audit.py`'s exit code alone | `example : False := by rfl` — Lean exit 1 — promoted to PROVEN |
| 6 | evidence guards | fields being **non-`None`** | a null exit, an empty digest, a blank timing file, and a timing file from a *different run* all passed |

Each was found by an adversarial control, never by a job failing — which is the
point: a harness that only reports success is indistinguishable from one that
works.

### R23.2 What replaced them

**Export and proof are separate states.** `export_state` is
EXPORTED/REFUSED/FAILED/SKIPPED; `proof_state` is
PROVEN/TYPECHECKED/FAILED/NOT_RUN/UNVERIFIABLE. A refusal is a real upstream
disposition and is not a failure; a missing adapter is SKIPPED and **never** an
acceptance.

**Scope is requested, not inferred.** `MODE_OBLIGATIONS` gives each mode a
*minimum*, supplied as `--min-required`, which is **added** to the auditor's
structural derivation rather than replacing it. A mode may strengthen the
requirement; it may not weaken it. The distinction that makes this work:

- **state is a property of the design** — a model with `structure <Top>_state`
  owes `next` and `step` in any mode that asks for a refinement at all. The
  exception is the standalone **fast** typecheck, whose declared scope is "it
  elaborates": it carries no refinement theorem, so demanding one would fail a
  correct export;
- **well-formedness is a property of the requested emission** — a `cert_wf=skip`
  export defines a graph certificate and deliberately does not prove its
  well-formedness.

Deriving WF from `def <Top>_graphCert` failed a genuine full-bridge proof, and
the re-audit caught it.

**Legacy fast has its own scope.** It requested `LEAN_EMIT_FAST_BRIDGE=true` —
identical to a full bridge — and was then judged against bridge obligations it
had not asked for. It is now `false`/`skip` with an **explicitly empty**
obligation set, which the auditor distinguishes from an *absent* one: the first
is a declared scope and passes as TYPECHECKED, the second is refused. An empty
scope never becomes PROVEN, and it does not short-circuit the `sorryAx` and
axiom-allowlist checks — an earlier version returned before `parse_reports` had
run, which both crashed and would have skipped them.

**Freshness is structural.** Jobs export into a per-invocation directory that
must not already exist, so a pre-seeded artifact cannot be adopted by a worker
that exits 0 without writing anything.

**A re-audit cannot promote a run that did not succeed.** It requires
`lean_exit == 0`, no `error:` or `sorryAx` in the retained log, a retained
timing file with a **present and successful** exit status naming **this**
artifact, and digests that are present, well-formed and still matching. Absent
evidence yields **UNVERIFIABLE** — neither a proof nor a failure — and counts as
not-accepted. A post-edit digest is never minted as evidence of what a previous
run checked.

### R23.3 The rule

> A check must be keyed to the thing it claims, and **absent evidence is not
> clean evidence**.

Six instances in this harness, plus the axiom audit counting lines, `--resume`
comparing inputs instead of the question asked, the LEC gate reading one field,
and the sweep reading a filename instead of a run. Every one passed for a while.

## R24. A seventh instance, in the static gate: an empty certificate is not an absent one

R23.3's rule is not confined to the replay harness. The same shape was sitting
in `op_census.py`, the static gate every emitted artifact passes through.

**The check.** With no certificate nodes, the gate printed

> `NO certificate nodes found -- was this emitted with formal.lean.emit_cert=true?`

and returned 1. The message asks whether the certificate was *emitted*. The
test was the **node count**.

**What it cost.** Two CORE-ET modules compile to a certificate with zero nodes:

| module | sources | nodes | state | recorded |
| --- | ---: | ---: | ---: | --- |
| `minion_dcache_texsend` | 7 | 0 | 0 | `export=FAILED` |
| `null_vpu` | 10 | 0 | 0 | `export=FAILED` |

Both are tie-off stubs **in the RTL**. `minion_dcache_texsend.sv` is 56 lines
of `assign idle_o = 1'b1; assign tex_send_ready_o = 1'b1; assign l2_req_o =
'0;` and an `_unused_inputs` reduction; `null_vpu.sv` is an `always_comb` that
consumes the request boundary into `unused_inputs` and drives every output to a
constant, commented as a "fail-closed constant illegal_rm response". The
emitted Lean reproduces exactly those constants, so **the compile is faithful
and the graph is genuinely node-free** — the refusal was the gate's, not the
compiler's.

The disagreement was visible in the artifacts the whole time: the
`verified_compiler` gate reports the *identical* `minion_dcache_texsend`
artifact as `sources=7 nodes=0 / PASS`, while the legacy gate reported
`gate_status=1` on it. Two gates, one file, opposite verdicts. It also reached
back: the earlier `generated/core-et/` sweep carries the same refusal for both
modules.

**The repair.** Ask what the message asks. `emit_cert=false` is directly
observable — `legacy_model.cpp` writes `-- Certificate emission disabled by
lean.emit_cert=false.` and no `graphCert` at all — and that case still fails. A
certificate that is **present and empty** is held to coverage instead: with no
nodes, every ordinal read out of the environment has to be a source, or
something is undefined.

**Nothing was weakened.** Seven controls in
`pass/lean/scripts/empty_cert_gate_controls.sh`, all calling the gate itself on
mutations of a real artifact:

| control | wanted |
| --- | --- |
| unmodified | PASS |
| `emit_cert=false` marker, `graphCert` removed | FAIL |
| `graphCert` removed, no marker | FAIL |
| an output reads an ordinal no source defines | FAIL |
| outputs declared, environment never read | FAIL |
| the pre-certificate `_memory_count` stub | FAIL |
| a design **with** nodes | PASS, *and* via the per-node census |

The last is a regression guard with a job to do: without it, deleting the
entire per-node census would have scored 6/6.

**This was already known on D2, and the port lost it.** After repairing it
here, `pass/lean/scripts/cert_shape_check.py` on the Direction-2 branch turns
out to open with:

> REPLACES a `nodes > 0` check that was UNSOUNDLY STRICT. Two real CORE-ET
> modules -- `null_vpu` and `minion_dcache_texsend` -- are legitimate ZERO-NODE
> designs [...] the old gate refused them for having no combinational nodes,
> which is not a defect.

Same two modules, same conclusion, reached independently. So R24 is a **port
gap**, not a discovery: D2 fixed this and master's `op_census.py` kept the old
rule. Recorded that way rather than as a find.

That file is now ported (R24.1), and it checks something this repair does not.

**Outstanding.** Both jobs still carry the old `FAILED` label and must be
re-run under the repaired gate. They are the only two zero-node artifacts in
the emitted set, so the correction is bounded at two.

### R24.1 `cert_shape_check.py`: every reference must resolve

`op_census.py` reads the **legacy** `graphCert`. The ported checker reads the
**`DesignCert`**, where slots are dense -- sources occupy `0 .. nSources-1` and
node `i` occupies `nSources + i` -- so there is a single global invariant worth
checking cheaply: every output `slot`, flop `din`/`enable`/`resetPin`, node
`deps` entry and memory `nextImg` must be `< nSources + nNodes`. A malformed
emission becomes a named failure in milliseconds instead of a Lean error
minutes later. It is a pre-check, not a substitute for `checkDesign`.

Measured on arrival: **all 81 emitted `verified_compiler` certificates pass**,
`minion_dcache_texsend` among them, reported as `zero-node design (outputs
resolve directly to sources); refs valid`.

One thing was added to the D2 version. Its section guard checks only that
`sources`, `nodes` and `outputs` were **found**. If an entry spelling drifts --
`origin` renamed, `SourceDesc` respelled -- the section is still found, its
count silently falls to zero, `total` shrinks, and every reference then looks
out of range. That reads as a shape failure *in the design* rather than a
failure to read it; with few enough references it reads as a **pass**. A
section that is present but counts zero, and is not literally `#[]`, is now a
named PARSER failure.

Thirteen controls, on mutations of real certificates: four accepted shapes
(plain, zero-node, with flops, with memories); an out-of-range output slot,
node dep, flop `din` and memory `nextImg`; a certificate with neither outputs
nor flops; three parser-drift cases, one of which additionally asserts the
message does **not** say "out of range"; and an unreadable file.

## R25. The controls were writing into the live results directory

`replay_orchestrator_controls.sh` ran every CLI case with
`REPLAY_RESULTS="$T/..."` in front of it. **`replay_orchestrator.py` has never
read that variable.** `RESULTS` and `JOBS` are module constants under
`generated/master_port/replay/`, so each control run wrote its fixtures into
the live tree: `results/CTL__preseed__verified_compiler.json` and a
`jobs/CTL/verified_compiler/` directory were sitting there, left by the suite.

This is R23.1 row 2 again — a knob named in the caller that the callee ignores
— and it is the second time it has appeared in this harness.

**Blast radius, measured rather than assumed.** Every accounting path is
inventory-driven: `load_results` walks `inv["jobs"]`, `exit_status` walks the
selected jobs, and `reaudit_results.py` walks `inv["jobs"]`. Nothing globs the
results directory. `CTL` is not a family in `inventory.json` (which carries 447
jobs across CORE-ET, CVA6 and DINO), so the stray record was never counted in
any total. **No reported number changes.** It was contamination, not
corruption, and the distinction is worth stating exactly rather than being
tidied away in silence.

**The repair** is `--replay-dir`, which relocates `REPLAY`, `RESULTS` *and*
`JOBS` together — a results-only flag would have left the job tree behind. The
controls pass it; the stray record and directory were removed by explicit path.

### R25.1 `--case` and `--redo`

Correcting R24 needs a bounded rerun of exactly two cases, and `run` skips any
job that already has a result — which is what makes a rerun cheap and is worth
keeping. So:

- `--case NAME` (repeatable) narrows the selection.
- An unknown `--case` **exits 2**. Selecting nothing and printing `running 0
  job(s)` with exit 0 is a rerun that did not happen, reported as a success —
  the same shape as everything else in R23.
- The name is checked against the **whole inventory**, not the post-filter
  selection, so a case excluded by `--family`/`--mode` is not called unknown.
- `--redo` replaces an existing record. Without `--case` it is refused, so it
  cannot be aimed at everything by accident.

Seven controls, through the real CLI: no `--case` selects both jobs; `--case
alpha` selects one; a recorded case selects zero; `--redo` brings it back to
one; `--case gamma` exits 2; `--redo` alone exits 2; and a known case excluded
by `--mode` is not reported unknown.

### R25.2 `status --replay-dir` reported on the wrong tree

Caught externally, and correctly. The relocation block sat **below** the
`status` dispatch:

```
    inv = json.loads(a.inventory.read_text())
    if a.command == "status":
        return status(inv)          # <- returns here

    if a.replay_dir:                # <- relocation, never reached by `status`
```

So `status --replay-dir X` read the **default** results directory while naming
X. Measured: a relocated tree holding two `SKIPPED` records was reported as two
**queued** jobs, from the empty default tree. A relocation that only some
commands honour is worse than none — it reads as a report about the tree that
was named.

Moved above the dispatch. The control needs the two trees to **disagree**, or
it passes whatever the code does: it builds one tree with both records present
and one empty, and requires that `status` give different answers and that the
populated one report `SKIPPED`. Verified after the move — populated: 2 SKIPPED,
0 queued; empty: 0 SKIPPED, 2 queued.

## R26. The timing file's own evidence, present rather than merely unobjectionable

Also caught externally. `reaudit_results.py` promised three things about the
retained timing and checked none of them when the line was absent:

```
    cmdline = next((l for l in ttext.splitlines() if "Command being timed" in l), "")
    if cmdline and art.name not in cmdline ...          # "" -> skipped
        ...
        if rec.get(key):
            line = next((l for l in ttext.splitlines() if label in l), "")
            if line and str(rec[key]) not in line:      # "" -> skipped
```

Measured on a real-Lean-validated fast model: **removing `Command being timed`,
`Elapsed (wall clock)` or `Maximum resident set size` separately each still
returned TYPECHECKED and exit 0.** This file's own rule — absent evidence is
not clean evidence — failing on the evidence it promises. Evidence:
`automate_workspace/master_rebase/audits/timing_identity_18e0d31c0_20261010`.

### R26.1 Two refusals, not one

The repair also splits what had been one bucket:

| | meaning | verdict |
| --- | --- | --- |
| `blockers` | the evidence is **present** and says the run did not succeed | `FAILED` |
| `missing` | the evidence needed to check is **absent** | `UNVERIFIABLE` |

`UNVERIFIABLE` was already this file's verdict for a vanished log or artifact;
it now covers an absent digest, an absent `lean_exit`, an absent or empty
timing file, a missing exit line, a missing command/wall/RSS line, a line
present but carrying no value, and a record that never kept a measurement. A
present-and-bad verdict outranks an absent one, so a run known to have failed
is not called unresolved merely because a second piece of evidence is also
gone.

**The recorded value is never minted from the file as it stands now.** A record
that never carried a measurement is unresolved, not retroactively measured.

**Admission is unchanged.** Both verdicts are refusals, both are counted in the
`bad` total, and both exit non-zero. What changed is that the label now says
which it is.

### R26.2 Blast radius, measured before the live records were touched

The repaired auditor was run against a **copy** of all 279 live records:

```
BEFORE: PROVEN 122  TYPECHECKED 69  NOT_RUN 87  FAILED 1
AFTER : PROVEN 122  TYPECHECKED 69  NOT_RUN 87  FAILED 1
re-audited 192 retained result(s); 0 verdict(s) changed
```

**Nothing is reclassified.** Every accepted record already carries a complete
`/usr/bin/time -v` block with the command it timed and both measurements. The
single `FAILED` is `CORE-ET/legacy/txfmactl_top`, which has `lean_exit 1` and
three `error:` lines in its retained log — a real failure, correctly recorded
before and after.

Then replayed against the external fixture itself, on a copy so their evidence
was not disturbed:

| case | before | after |
| --- | --- | --- |
| `good` | TYPECHECKED, exit 0 | TYPECHECKED, exit 0 |
| `missing_command` | TYPECHECKED, exit 0 | **UNVERIFIABLE**, exit 1 |
| `missing_wall` | TYPECHECKED, exit 0 | **UNVERIFIABLE**, exit 1 |
| `missing_rss` | TYPECHECKED, exit 0 | **UNVERIFIABLE**, exit 1 |

Nineteen controls now pass, five of them new.

## R27. Prerequisites, as actually provisioned

Checked rather than assumed, because the P=1/P=2 source-RTL differential
depends on them:

| tool | state |
| --- | --- |
| `iverilog` | **not installed** — not on `PATH`, not on disk, no dpkg package |
| `vvp` | **not installed** (same) |
| `verilator` | `/bin/verilator`, 5.032 (Debian 5.032-1+b2) |

`mem_lean_diffsim.py` drives **iverilog**, so the RTL differential **cannot be
run here**. It already refuses honestly rather than passing: a missing tool
prints `BEHAVIORAL_DIFF_SKIPPED no-iverilog`, and — deliberately — an
*installed* iverilog that cannot elaborate the design is a `BEHAVIORAL_DIFF_FAIL`,
not a skip. Only a missing tool may skip.

This is recorded as a **missing prerequisite**, not as a result. A control
suite exercising the admission rule, or a certificate-only probe, is not an RTL
differential run and is not reported as one. (D2's `coreet_lowering_diffsim.py`
uses verilator instead, for modules whose RTL the native gate cannot elaborate;
porting it is a separate item, and it does not substitute for the iverilog
fixtures.)

Other roots in use: DINO `HAGENT_BUILD=/soe/czeng14/projects/hagent/.cache/setup_simplechisel_mcp_2025.11/build`
(the only one carrying all three of `build_singlecyclecpu_d`, `build_pipelined_d`,
`build_dualissue_d`); CVA6 `/mada/users/czeng14/projects/cva6`; CORE-ET
`/mada/users/czeng14/projects/core-et`.

## R28. The three standalone DINO fast-model procedures

`inventory.json` holds 447 jobs and **stays at 447**. Its DINO entries are
3 `legacy_chunked_wf` + 3 `legacy_full_bridge` — the six that are PROVEN.

The three **standalone fast-model** procedures are a separately requested
historical procedure and are not among them. `legacy_full_bridge` does also
elaborate the fast model on its way to the bridge, but that **does not
discharge** a separately requested run, and recording it as if it did would be
the same substitution this ledger keeps catching.

They are therefore carried in their own file,
`generated/master_port/replay/inventory_dino_fast.json`, whose results land in
the same `results/` tree keyed `DINO__<top>__legacy` — distinct from the
`legacy_chunked_wf` and `legacy_full_bridge` records, so neither can be mistaken
for the other.

## R29. CORE-ET legacy-fast, complete, with every disposition named

After the R24 gate repair and the bounded rerun of exactly the two affected
cases:

| | count |
| --- | ---: |
| EXPORTED | 80 |
| REFUSED | 41 |
| FAILED | 4 |
| — of the exported, TYPECHECKED | 79 |
| — FAILED | 1 |

`minion_dcache_texsend` and `null_vpu` were re-run with `--case ... --redo` and
are now `EXPORTED / TYPECHECKED` (0.52 s and 2.77 s). Their prior failed-run
evidence is retained: the earlier run directory, its `job.log`, its
`static_gates.log` carrying the old refusal, and the artifact it produced are
all still on disk beside the new run, which has its own timestamped directory.

### R29.1 The four remaining export failures are upstream, and are named

None is a `pass.lean` defect. Reading each job's own diagnostic:

| case | code | pass | what it says |
| --- | --- | --- | --- |
| `minion_dcache_tensor_load` | `concat-lane-width` | `pass.bitwidth` | `internal`: Concat lane 0 of 8, driver `mux_19104` occupies **2 bits** in a declared window of **1** |
| `minion_dcache_top` | `concat-lane-width` | `pass.bitwidth` | `internal`: Concat lane 0 of 2, driver `mux_94508`, same shape |
| `txfma_adder` | `yosys-failed` | `inou.yosys` | `io`: `read_slang` — design elaboration failed |
| `txfma_top_fake` | `yosys-failed` | `inou.yosys` | `io`: `read_slang` — design elaboration failed |

The first two are one upstream bitwidth defect seen twice: a lane driver may
only be *narrower* than its window (LGraph may narrow, never widen), and
truncating it would shift every lane above it, so the pass refuses rather than
emit a wrong concat. The other two never reach `pass.lean` at all — the
frontend cannot elaborate the RTL. They are classified, not repaired: both
areas are outside this work's contract.

### R29.2 The one failure is a COMPILATION failure, not a disproved theorem

`CORE-ET/legacy/txfmactl_top`: `lean_exit 1`, three `error:` lines, recorded
`FAILED` and re-audited `FAILED`. What the errors say matters:

```
error: failed to compile definition, compiler IR check failed at
  `txfmactl_top_state.default._closed_0`.
  Error: constructor `txfmactl_top_state.mk` has too many fields
error(lean.dependsOnNoncomputable): ... consider marking it as `noncomputable`
```

This is Lean's **code generator** refusing a constructor arity, at
`instInhabited...`, `..._next` and `..._nextStateFromCert`. Nothing here is a
disproved semantic theorem and nothing is `sorry`: the file does not reach the
point of compiling to executable IR.

The arity is the flat record. `txfmactl_top` declares **289** `st_` fields —
the largest in the corpus by a wide margin; the next are `vpu_ml` at 170,
`vpu_tensorfma` at 123 and `trans_top` at 96, and all three compile. So the
limit sits between 171 and 289 fields.

Flat records are a standing decision and are not changed here. The item is
recorded as a **Lean compilation/typecheck failure at a code-generator arity
limit**, open, and distinct from every other failure in this ledger.

## R30. The ported simulator, run

The launcher port is no longer "generated but unexercised".

`lake build Sims` builds `Sims.txfma_e4_Cert` and `Sims.txfma_e4_Main` from a
**real emitted certificate**, and the launcher runs:

```
$ lake env lean --run Sims/txfma_e4_Main.lean --list
txfma_e4 sources=2 nodes=1 outputs=1 flops=0 mems=0 clocks=1

$ lake env lean --run Sims/txfma_e4_Main.lean txfma_e4 --cycles 2 --show-state
# lgraph-sim txfma_e4 sources=2 nodes=1 outputs=1 flops=0 mems=0 inputs=2 clocks=1
cycle 0 out 0
...                                                                  exit 0

$ lake env lean --run Sims/txfma_e4_Main.lean not_a_design
lgraph-sim: no certificate named 'not_a_design' in this binary        exit 2
```

The reported shape matches the `DesignCert` exactly: 2 sources, 1 node, 1
output, 1 clock.

**Two invocation facts, both measured the hard way.** The first attempt failed
with `unknown module prefix 'Sims'`: `lake env` puts only **built** libraries
on `LEAN_PATH`, so `lake build Sims` is not optional. The second failed with
`unknown option '--'`: `lake env lean --run` hands everything after the file to
the program, so the `--` separator the D2 docstring showed arrives as an
argument. The docstring carried that error into this tree and is corrected.

## R31. D2 runtime acceptance: the generic registry, measured

`Main.lean` carries `Examples.registry ++ Examples.rejectRegistry` — ten
well-formed certificates and fourteen deliberately malformed ones — so the
binary itself demonstrates that a refusal exits non-zero. Every one was run,
`--cycles 1`, under `/usr/bin/time -v`:

| group | n | exit | wall | peak RSS |
| --- | ---: | --- | --- | --- |
| accepted | 10 | all **0** | 2.52–4.98 s | 793–797 MiB |
| malformed | 14 | all **1** | 2.34–2.90 s | 786–798 MiB |

The accepted set covers combinational masking, an enabled counter, an
async-reset counter, a ROM, a read/write memory, **two clock domains**, an
async reset **off its domain**, a memory off its domain, and two scaling cases
— `chain-1k` (1,000 nodes) and `chain-200k` (**200,000 nodes**, 4.98 s, 797
MiB, the only positive above 3.5 s).

Each refusal names its own cause, and they are fourteen *different* causes —
not one message repeated:

```
bad-arity-not          node 4: operator LGraphOp.Op_Not ...
bad-kind-mem-read      node 3: operand 0 (slot 1) has the wrong kind
bad-dep-order          node 3: dependency on slot 4, which ...
bad-zero-width         slot 3: width 0 is not a legal bit width
bad-unsupported-op     node 3: operator LGraphOp.Op_SDiv ...
bad-predicate-width    node 3: operator LGraphOp.Op_EQ ...
bad-flop-ordinal       source slot 0: flop ordinal 7 is ...
bad-output-kind        slot 0 holds a memory image where ...
bad-async-reset-value  source slot 1: reset value disagrees ...
bad-rom-size           source slot 0: ROM has more entries ...
bad-flop-clock         flop 0: clock ordinal 1 is not declared
bad-mem-clock          memory 0: clock ordinal 3 is not declared
bad-no-clocks          the certificate declares no clocks
bad-async-flag         source slot 1: async flop 0 is not ...
```

Four of those — `bad-flop-clock`, `bad-mem-clock`, `bad-no-clocks`,
`bad-async-flag`, plus `bad-async-reset-value` — are **clock and reset**
refusals, which is the D2 schema this port exists to carry.

### R31.1 DirectBench: the two implementations compared

`benchMain` runs the direct interpreter and the **verified compiler's**
residual on the same inputs and the same initial state, then `diffTrace`
compares them cycle by cycle and exits non-zero on any disagreement.

On `txfma_e4`, 20 cycles:

```
verdict ACCEPTED        check_ms 1
direct_step1_ms 0       direct_run_ms 2     (outsum 0)
compile_bindings 1      compile_ms 0
residual_run_ms 2                            (outsum 0)
diff_result OK          diff_ms 6 cycles 20            exit 0
14.11 s wall, 6,655,316 KiB peak
```

The RSS jump from ~796 MiB (simulator alone) to 6.5 GiB is the point of the
split: the launcher does **not** need the verified compiler or Mathlib, and the
bench does.

### R31.2 What this is not — stated properly

The earlier wording ("D2 runtime acceptance is done except the RTL
differential") claimed more than was run. Precisely what DirectBench
establishes:

- it starts from **`zeroState` and `zeroInput`**, not from a reached state or a
  stimulus;
- it drives **20 `allEdges` ticks** — every clock firing every tick, so no
  partial-fire or cross-domain schedule is exercised;
- `diffTrace` compares **outputs and flops**, and samples memory **only at
  addresses `[0, 1, 2, 3, 255]`**.

That is **finite certificate-level agreement between two implementations on one
fixed trace**. It is not an equivalence proof, not an RTL comparison, and not a
statement about any other input.

And it does not resolve a gate. **`intpipe_csr_msgs` is still `REFUSED` by the
static mask/raw-provenance gate** — `export_state: REFUSED`, `proof_state:
NOT_RUN`. The bench ran against the certificate the gate produced and then
refused. Its agreement says the direct interpreter and the verified compiler
read that candidate the same way; it says nothing about whether the candidate
should be admitted. The clock, reset and collision value gates stay exactly as
they are.

### R31.3 The live replay's library context, fingerprinted

Recorded in `generated/master_port/d2_runtime/library_fingerprint.txt` before
any shared-support rebuild: toolchain `leanprover/lean4:v4.31.0`, `lhd`
`54eb7f2b38b10512` (203,522,288 bytes), and a digest and size for each of the
25 `LeanSemanticPrimitives` `.olean` files.

Checked after `lake build Sims`: **no `.olean` under
`LeanSemanticPrimitives` was rebuilt**, and the CORE-ET `legacy_bridge` queue
running concurrently continued to report PROVEN. Building the `Sims` library
elaborates only the new modules and reuses the cached dependencies, so the live
replay's context was not disturbed.

## R32. Destination ledger: what is still open

Recorded so none of it is lost to a checkpoint.

### R32.1 Not started

| item | what it needs |
| --- | --- |
| **D3 port** | must not overwrite post-D2 clock/reset metadata with the older clockless modules |
| **D4 port** | same |
| **actual Futamura port** | the goal there is a SIMULATOR (`SIMULATOR_PLAN.md`); it shares `DesignCert`/`interpretDesign` with livehd-new at a pinned rev, and those copies are never edited |

Each carries its own historical acceptance requirements, which travel with the
port and are not discharged by this branch's.

### R32.2 Blocked on a prerequisite

The **P=1/P=2 source-RTL differential** with its phase, initialization, named
IO/clock sidecar ordinal and value-negative procedures. `iverilog` is not
installed (R27). `mem_lean_diffsim.py` and its seven controls are ported and
the admission rule is proven; the run is not possible here, and nothing else
is reported in its place.

### R32.3 Open questions, not defects

- The **async read/commit admission contract** is a decision pending with the
  user. It is not to be inferred from elapsed time or from a monitoring
  message.
- The **mask/raw-provenance investigation**: 18 modules, all flagged on
  operand 1 of `Op_GetMask` with mask width ≥ data width. Characterized as a
  mask-semantics question, explicitly **not** a constant-width defect. No
  value is widened.
- `CORE-ET/legacy/txfmactl_top`: a real proof failure, three `error:` lines,
  still open (R29.2).

### R32.4 Ported but not yet swept

`direct_sweep.py` classifies every emitted `DesignCert` with the direct
simulator's own `checkDesign` and runs it for a few cycles. Its thirteen
non-Lean controls pass; the sweep itself wants the proof slot, which the
CORE-ET `legacy_bridge` and CVA6 queues hold. Queued behind them rather than
run concurrently.

### R32.5 Unchanged by decision, not by omission

Native LEC, `single_edge` and frontend contracts; checker/evaluator policy;
the flat record layout; the pinned toolchain. No guard was weakened, no record
nested, no value widened, and no semantic policy changed.

### R31.4 DirectBench on real stateful designs

`txfma_e4` has one node, so its agreement is weak evidence. Two real CORE-ET
designs, both 20 cycles, both `exit 0`:

| design | sources | nodes | outputs | flops | mems | `direct_run_outsum` | `residual_run_outsum` | diff | wall | peak RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | ---: |
| `intpipe_csr_file_conv` | 136 | 154 | 4 | 7 | 0 | 40 | 40 | **OK** | 13.84 s | 6,659,484 KiB |
| `intpipe_csr_msgs` | 2,045 | 2,560 | 11 | **53** | **1** | 368934881474191687660 | 368934881474191687660 | **OK** | 26.63 s | 6,680,320 KiB |

`intpipe_csr_msgs` is the one that carries weight: 2,560 nodes, 53 flops, a
memory and **two clock domains**, and the direct interpreter and the verified
compiler's residual produce the *same 21-digit output sum* over 20 cycles, with
`diffTrace` agreeing cycle by cycle rather than only on the total.
`compile_bindings` equals the node count in both (154 and 2,560), so the
compiler bound every node rather than folding the design away.

### R31.5 The RTL differential, attempted and refused on the record

Not skipped quietly, and not replaced by a control. The tool was run on real
inputs — the emitted `intpipe_csr_msgs` certificate, its sidecar (20 named
input ordinals, 11 outputs, two named clocks `clk_i`/`clk_wr_i`), its RTL at
`core-et/hw/ip/minion/intpipe/csr/rtl/intpipe_csr_msgs.sv`, and a 12-period
scenario written **by signal name**, across both sampling phases:

| P | sample | result | exit |
| ---: | --- | --- | ---: |
| 1 | before-first-edge | `BEHAVIORAL_DIFF_SKIPPED no-iverilog` | 3 |
| 1 | after-first-edge | `BEHAVIORAL_DIFF_SKIPPED no-iverilog` | 3 |
| 2 | before-first-edge | `BEHAVIORAL_DIFF_SKIPPED no-iverilog` | 3 |
| 2 | after-first-edge | `BEHAVIORAL_DIFF_SKIPPED no-iverilog` | 3 |

**No differential ran.** That is the honest outcome with no `iverilog` on this
machine, and it is recorded as a refusal with a named cause, not as a pass.

One ordering repair: the `iverilog` check sat *after* the Lean side, so a run
with no reference simulator elaborated the certificate first — minutes and
gigabytes on the largest designs — and then printed the same `SKIPPED` it could
have printed immediately. The verdict and its exit status are unchanged; only
the order is. **Only a missing tool may skip**: an installed `iverilog` that
cannot elaborate the design is still a `BEHAVIORAL_DIFF_FAIL`, and that check
stays exactly where it was.

### R31.6 A wrinkle worth naming

`make_sim_launcher.py` **appends to a tracked `lakefile.toml`** while its
outputs are gitignored, so generating a launcher dirties the repository. That
is the D2 design and it is kept; the six generated `[[lean_exe]]` stanzas from
this session's runs were removed afterwards, leaving the lakefile identical to
its committed state. Worth naming because the next person to run the generator
will see the same thing and should not commit it.

## R33. The measurement comparison was substring containment

R26 required the timing lines to be *present*. It left the comparison itself
as `str(rec[key]) not in line` — **containment, not comparison**. Caught
externally, and all three of these were accepted as TYPECHECKED:

| recorded | retained | why it passed |
| --- | --- | --- |
| RSS `96028` | `796028` | `"96028"` is a substring of `"796028"` |
| wall `0:02.5` | `0:02.58` | `"0:02.5"` is a substring of `"0:02.58"` |
| RSS `796028` | `not-a-number (796028)` | the digits are in there somewhere |

An order-of-magnitude error, a truncated measurement and a corrupt line.

**The repair parses.** Each value is extracted with its own label — necessary
because the wall label itself contains colons (`h:mm:ss`), so no split on the
first colon is correct — and then validated:

- wall: `[h:]mm:ss[.ss]` → seconds;
- RSS: a bare non-negative integer → `int`.

Syntax is checked on **both** sides, and the comparison is between parsed
values. The two outcomes stay distinct: a value that parses on both sides and
**disagrees** is a contradiction → `FAILED`; a value that **cannot be parsed**
says only that the file cannot be read as a measurement → `UNVERIFIABLE`. A
recorded measurement is still never minted from the file as it stands now.

**24 controls pass**, five of them new: `truncated_rss` → FAILED,
`truncated_wall` → FAILED, `malformed_rss_line` → UNVERIFIABLE,
`malformed_recorded_wall` → UNVERIFIABLE, and `complete_still_valid` → PROVEN —
the last re-running the untouched good record *after* every mutation, so a
repair that simply refuses everything is caught.

**Blast radius, re-measured on a copy of the live records:**

```
BEFORE: PROVEN 174  TYPECHECKED 79  NOT_RUN 108  FAILED 1
AFTER : PROVEN 174  TYPECHECKED 79  NOT_RUN 108  FAILED 1
re-audited 254 retained result(s); 0 verdict(s) changed
```

Every accepted record's recorded wall and RSS parse and match its retained
timing exactly. No accepted log needs re-proving.

## R34. Icarus provisioned, and the source-RTL differential finally run

R27 recorded a missing tool. A missing optional tool is an unmet prerequisite,
not a reason to abandon an authorized validation — and `AGENTS.md` says so
itself: *"A requested leg whose tool is missing FAILS rather than skipping."*

### R34.1 The provisioned tool

Built from source into a **project-local** tree, no system installation and no
toolchain change:

```
tag        v13_0
commit     dfeee909ed9f20b4870dd93423156c0170c0e1ff
prefix     generated/validation-tools/iverilog-v13_0
configure  --prefix=<that>   (no other options)
built with gcc (Debian 14.2.0-19) 14.2.0
iverilog   5506c214f778cc5eb79e503c86b68321  version 13.0 (stable) (v13_0-dirty)
vvp        ab92b3ffd910f0c3682ace1a0befa8f1  runtime version 13.0 (stable)
```

Source, build tree, prefix and `TMPDIR` are all under the project. The
`-dirty` suffix is `autoconf.sh` regenerating build files in the checkout; the
commit is the pinned tag's.

### R34.2 Two differentials ran; one agrees, one does not

With `LHD_EXTERNAL_SIM=1` and the provisioned tools on `PATH`. **These legs had
never run on this machine** — there was no `iverilog` for them to use, and they
skip by default.

| test | lec legs | iverilog leg |
| --- | --- | --- |
| `single_edge_icg_test` | pass | **"source ICG and normalized flop-with-enable agree over 29 cycles under iverilog"** |
| `single_edge_four_classes_test` | pass | **`SINGLE_EDGE_DIFF_FAIL errs=78`** |

So a real source-RTL differential now runs, and one of them passes.

### R34.3 The four-classes disagreement, characterized

Every observed mismatch is the **same shape** — the normalized side exactly one
source period behind:

```
MISMATCH ql @38000 source=37 normalized=0
MISMATCH ql @58000 source=48 normalized=37     <- source's 38000 value
MISMATCH ql @78000 source=85 normalized=48     <- source's 58000 value
```

`normalized(t) == source(t - 20000)` for every mismatch after the first, and
the first is the initial condition under the same offset. This is a **uniform
one-period offset**, not corrupted values — the alignment class that the
testbench's own comments are about (`HALF=5` gives two posedges per source
period, reset released at `t=27` so "the phase divider starts period 1 at slot
0").

**Not repaired, and not explained away.** It is in native `single_edge` and its
contract test, both outside this work's change scope. Stated as what it is: a
previously unexercised leg, exercised for the first time, showing a reproducible
one-period offset under Icarus 13.0 — whether that is master's divider
convention or the testbench's sampling convention is an open question for
whoever owns that pass. The lec legs pass and the ICG differential passes, so
neither the harness nor the tool is the obvious culprit.

### R34.4 Five historical fixtures, and two named capability gaps

All five historical source-RTL procedures were ported — `single_edge_memory_slot_test.sh`,
`single_edge_multi_clock_test.sh`, `mem_mixed_lean_diff_test.sh`,
`mem_feedback_lean_diff_test.sh` and their RTL (`mem_mixed_rdclk_diff.v`,
`mem_async_read_feedback.v`, `mem_sync_read_feedback.v`) — and run. **Four are
blocked, by exactly two upstream capability gaps.** Neither was worked around.

**Gap 1 — there is no `multi_clock` option.**

```
--set/--config references unknown flag 'multi_clock' of pass 'pass.single_edge'
```

This is what all three `mem_*_lean_diff` fixtures hit first. It is deliberate
upstream: master's comment reads *"a design that merely has two clock nets is
already handled honestly downstream — pass/lec's detected-edge branch REFUTES
two-clock-vs-one-clock today — so declining it here would turn a correct
verdict into an unsupported exit."*

**Gap 2 — the memory-slot gating is absent**, and it is the binding one. The
flag is passed uniformly by these fixtures, so to find out whether it was
load-bearing the pass was re-run on the already-built `mem_mixed_rdclk_diff`
LGraph **without** it:

```
pass.single_edge refused 'mem_mixed_rdclk_diff':
  memory `memory_44:mem` would commit on every sub-step under a phase divider
```

Same for `single_edge_memory_slot_test`'s own fixture, with the hint *"slot
enables are not wired into the Memory cell yet"*. That is precisely the
**pre-fix behaviour** the D2 fixture's header describes — the memory-slot
gating is a D2 capability master does not carry. So dropping the unknown flag
from the invocation would not have rescued these procedures; it only moves the
refusal one step later, which is why the experiment was run rather than
assumed.

Closing either gap means changing native `pass.single_edge`, which is out of
scope. The fixtures stay in `lhd/tests/` so the procedures are not lost, the
single-clock and multi-clock procedures stay separate, and none is wired into
`lhd/tests/BUILD` — they would fail the suite for a reason that is not a
regression.

**One adaptation, to fixture source only**: the frontend renamed the built-in
type spellings (`core/str_tools.hpp` `renamed_type_spelling`: `u<N>`→`U<N>`,
`s<N>`/`i<N>`→`S<N>`, `bool`→`Bool`, `unsigned`→`Unsigned`, `signed`→`Signed`,
`string`→`String`). Nine spellings in `memslot.prp`. No period count, sampling
phase, stimulus, initialization or comparison was touched — and the fixture
still reaches, and is refused by, the pass.

### R34.5 What the differential evidence actually covers

| procedure | on master | result |
| --- | --- | --- |
| `single_edge_icg_test` (iverilog leg) | runs | **agrees over 29 cycles** |
| `single_edge_four_classes_test` (iverilog leg) | runs | **`SINGLE_EDGE_DIFF_FAIL errs=78`**, uniform one-period offset (R34.3) |
| `single_edge_memory_slot_test` | blocked | gap 2 |
| `single_edge_multi_clock_test` | blocked | gap 1 |
| `mem_mixed_lean_diff_test` | blocked | gap 1, then gap 2 |
| `mem_feedback_lean_diff_test` (2 cases) | blocked | gap 1 |

So **one** source-RTL differential passes on master today, **one** disagrees
with a characterized offset, and **four** cannot run for two named reasons.
`mem_lean_diffsim.py`'s own P=1/P=2 procedure is preserved and controlled, but
its fixtures are among the blocked four, so it remains unexercised end to end.

A further limitation of that tool, found while attempting it on a real design
and worth recording rather than discovering twice: **`build_tb` treats only a
port literally named `clk` as the clock.** The original fixtures all use `clk`,
so the historical procedure is unaffected; but a design whose clock is `clk_i`
would have its clock driven as stimulus data while an unconnected `clk`
toggled. The sidecar already names the clocks (`clocks: [{name, ordinal,
implicit}]`), so the fix is to drive the clock the sidecar names and refuse
when it declares more than one — the Lean side uses `allEdges`, so a two-domain
comparison is not expressible in this procedure at all. Recorded as open; not
changed here, because changing it without a runnable fixture would be changing
event semantics with nothing to check them against.

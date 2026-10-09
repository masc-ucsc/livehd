# Lean refactor integration with master

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
| `direction-4-incremental` | `livehd-d4-incremental` | `CertIO`, `CertIORoundTrip` (runtime certificate transport; parser recorded as trusted) | same three |
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
3. D3 and D4 rebase onto the post-D2 schema. Both are Lean-only; neither adds
   a C++ emitter and neither may introduce a second graph traversal.
4. Futamura re-pins to the landed revision. Its pin **moves** as soon as step 1
   lands; that is a required action, not a side effect.

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
graph-adapter header.** That is P5 of PASS_LEAN_RESTRUCTURE_PLAN.md, and as a
directory boundary it is mechanically checkable in CI rather than aspirational.

#### Sequencing

This reorganization happens AFTER the branches land (R1 step 4) and AFTER the
benchmark replay, not before. Moving files while D2/D3/D4/Futamura are still
rebasing would force every one of them to re-target paths mid-flight, and a
replay whose inputs moved proves nothing about the port. Record the move as a
pure rename commit, with no content change, so `git log --follow` stays usable
and the replay can be re-run across it unchanged.


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

### Bounded legacy records: a newly exercised inherited limitation

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
record executed. The schema emitter now bounds physical records at 64 fields
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

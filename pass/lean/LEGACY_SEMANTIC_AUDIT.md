# Legacy Lean semantic changes and commit provenance

Audit point: `refactor/pass-lean-shared-architecture`, `a49de5a2d`.
Immediate pre-refactor baseline: `1af4e1492`.
This report distinguishes historical saved Lean files from the immediate source-code baseline. Saved proof artifacts are not pinned to an exact emitter commit; matching a commit below means that its code implements the observed policy, not that an artifact's generation date was reconstructed.

## Direct measurements from this audit

The frozen `1af4e1492` exporter was run on project-local copies of all 79 graph databases used by the current historical replay. The original dirty workspace was not modified.

- 60 exports succeed. Parsed NodeCert values, topo/source IDs, source expressions, output projections, and next-state dependency IDs/types match the current exports in all 60. This is a structural comparison, not a whole-model equality proof.
- 19 exports refuse asynchronous reset; current legacy emission accepts these through the shared scanner/certificate path. Thus they have no accepted legacy artifact at the immediate baseline to compare against.
- The older saved artifacts differ from current emission in 21 blocks: 495 asynchronous-reset source expressions in 19 blocks and 39 constant-width source expressions in six blocks (four blocks overlap).
- All 100,481 node expressions in the 60 accepted exports were matched to current factored definitions. Removing whitespace/parentheses and replacing factored references with the corresponding local node name leaves 129 expression differences: 122 SLT and seven SGT. Of these, 99 have equal operand widths (the removed casts are identities), and 30 are the intpipe_alu comparisons between a six-bit signal and the seven-bit encoding of 64.
- All three DINO frozen/current exports on matched saved graph inputs preserve the parsed certificate/source structure. Separately saved historical DINO files can come from different graph generations, so node-ID differences across those files cannot automatically be attributed to the refactor.

Evidence: [baseline export results](tests/legacy_semantic_audit/baseline_same_graph_results.json), [all matched node-expression differences](tests/legacy_semantic_audit/fast_expression_text_audit.json), [signed-comparison widths and node IDs](tests/legacy_semantic_audit/signed_comparison_widths.json), [DINO matched-input structure](tests/legacy_semantic_audit/same_graph_dino_structure.json).

## Semantic and acceptance changes

`668218a4a` implements the typed legacy renderer; `e5a8a5ddb` makes the public legacy path use it. Unless otherwise indicated, these are the implementation/activation commits for changes relative to the immediate baseline.

| Area | Historical behavior and immediate baseline | Current behavior | Commit alignment and classification |
|---|---|---|---|
| Unsized constant widths and overflow | Older extraction could treat an unsized constant as one bit or truncate at an undersized declared width. The immediate baseline already uses intrinsic widths and rejects an undersized positive declared constant in strict mode. | Same corrected width resolver through the shared scan/certificate path. Constants can receive an operator-dependent width. | `362b1ea49` (2026-09-01), before this refactor. All 39 historical source-width differences fit this policy family. In particular the immediate baseline already emits the intpipe_alu constant at width seven on the replay graph. |
| Constant data and shift/Sext amounts | Earlier constants could acquire a sign accidentally at their minimal width or lose high amount bits. At the immediate baseline constant SRA/Sext data uses at least the result width, and amount constants have enough bits for their value. SHL data constants use the node width. | Those certificate width rules are retained. | `0bc402e50` and `362b1ea49`; inherited, not a new arithmetic rule in this refactor. |
| Asynchronous reset Q reads | Very old legacy artifacts read only stored Q. `b3268de66` added refusal guards; `bf7eabf81` added proper async sources to the verified path while legacy still refused async=1. `1af4e1492` legacy therefore refuses the 19 affected blocks. | Legacy source reads immediately select the reset value when reset is active, as well as handling reset on the state update. Computed reset sources that cannot be resolved to primary inputs remain unsupported for async reads. | Semantics originates in `bf7eabf81` (2026-08-27); carried through `afafd4f70`; enabled for legacy by `e5a8a5ddb`. 495 observed source-expression changes align. Acceptance expansion relative to the immediate legacy baseline. |
| Reset polarity / reset net | Historically negreset was incorrectly treated as the reset signal. `bf7eabf81` made it a constant polarity flag and took the signal from reset_pin. Immediate legacy refused active-low resets. | Legacy honors the active-low flag both on async Q reads and next-state reset selection. | `bf7eabf81` fixes flag/net interpretation; `1af4e1492` fixes the shared FlopDriver next-state polarity (`false` -> `f.active_low`); `e5a8a5ddb` exposes it in legacy. |
| Reset value (`initial`) | Old legacy hardcoded zero; immediate legacy refuses nonzero reset values. | Uses the recorded constant reset value. This is a reset value, not a proof that the initial machine state has that value. | Verified support originates with `7bac8d230` / `bf7eabf81`; legacy activation is `e5a8a5ddb`. Acceptance expansion. |
| Mixed-width signed LT/GT | Baseline fast expressions zero-extend both operands to a common width before toInt. This can turn a narrow negative operand into a positive one. The certificate evaluator already interprets each operand at its own width. | Fast expressions use each operand's own signed width. | New fast-renderer behavior in `668218a4a` / `e5a8a5ddb`; `a49de5a2d` adds unequal-width bridge lemmas/dispatch, not another semantic change. A separate 8-bit/4-bit counterexample proves the old and new renderer rules differ. |
| Narrow-result unsigned division | Baseline fast expression truncates operands to the result width before dividing. | Divides at max(result width, operand widths), then truncates the quotient. Certificate dependency values still determine the operands; this does not undo a prior truncation of a constant source. | New fast-renderer behavior in `668218a4a` / `e5a8a5ddb`, aligned to existing certificate evaluation. Example with 8-bit inputs 16 and 2, four-bit result: old 0/2=0, new 16/2=8. |
| General/dynamic Sext | Baseline fast expression ignores the amount operand and sign-extends from the data operand's width. `b3268de66` explicitly documented that this is valid only for the supported full-width/truncation shapes. | For other shapes, retains the low amount bits and sign-extends that value. Thus amount b denotes a width, with sign bit b-1. Existing canonical shapes retain bv_sext. | New fast behavior in `668218a4a` / `e5a8a5ddb`, fixing a defect documented in `b3268de66`. Dynamic/general bridge coverage is still limited; executable emission and optional proof support are distinct. |
| Widening arithmetic right shift | Older fast emission zero-extended the shifted negative value. The immediate baseline already sign-extends when the result is wider. | Keeps the corrected sign extension. | `0bc402e50` (2026-08-10), grounded/documented by `b3268de66`. Not newly changed by this refactor; the added tiny oracle guards an existing fix. |
| GetMask and SetMask | GetMask packs selected bits toward the low end; the -1 mask is widened to cover the source. The baseline already uses these rules. | Retains the same operators and certificate width rules. | Earlier GetMask fix / `b3268de66` grounding. No new measured semantic mismatch on matched accepted inputs. |
| Immutable ROM | Immediate legacy rejects Memory init, because its memory state would otherwise be arbitrary. Verified mode already has extracted immutable contents. | Legacy emits a constant lookup table with zero outside the recorded entry count; no mutable image field for ROM. Initialized writable RAM, whole-array updates, bulk reset, and dynamic init remain refused. | `362b1ea49` introduces verified ROM semantics; `e5a8a5ddb` enables its legacy presentation. Acceptance expansion. |
| Synchronous ROM / RAM reads | Sync RAM read registers were already represented as state with read-enable hold behavior. Immediate legacy rejects ROM, including synchronous ROM. | Sync ROM has immutable contents plus a read-data register; disabled reads hold the previous register. Sync RAM continues the existing behavior. | `8c3ce2e2d` adds legacy sync-memory certificate handling; `362b1ea49` adds verified sync-ROM state; `e5a8a5ddb` enables it in legacy. |
| Memory writes, forwarding and byte enables | Earlier forwarding mistakenly treated fwd as a Boolean. `d3338fc39` changes it to a per-read/per-write matrix and rejects unknown/oversized policy; subsequent certificate commits decompose memory into image, write chains and reads. | Same matrix-selected forwarding, write-port order (later write wins collisions), byte-enable masks and read-enable behavior, now from shared memory lowering. Undefined read-during-write and unsupported whole-array policies remain refusals. | `d3338fc39`, `62f408fc2`, `2670b483e`, `8c3ce2e2d` predate the refactor. Canonical IDs/presentation change; no new measured policy mismatch. |
| Clock polarity, depth and single-step meaning | Early legacy could ignore connected unsupported pins. `b3268de66` guards them; `bf7eabf81` makes guards value-aware. The immediate baseline expects normalized posedge, depth-one state. | Same single-step assumption; negedge and depth != 1 are refused. There is no per-domain edge vector in the legacy or verified certificate on this branch. Capturing a clock pin in the scan is not equivalent to modeling distinct clock events. | Guards predate the refactor. Multi-clock semantic commits are absent; see below. |
| Simultaneous state updates | Outputs and all next-state expressions evaluate against old state; next state is constructed afterwards. | Preserved, including the two-flop swap oracle. | Renderer extraction changes the implementation, not the update ordering. |
| Cyclic graphs / arity validation | Legacy topo walk lacks the shared active-stack cycle guard. Several node renderers also reject malformed arities. | Shared scan detects cycles; however, some malformed-arity renderers now fall through to defaults rather than rejecting. | Shared scanner from `afafd4f70`, activated for legacy by `e5a8a5ddb`. These are acceptance/failure-mode changes, not a new intended circuit semantics. Details below. |

## Additional code-level discrepancies outside the measured accepted corpus

These follow from comparing the renderer implementations. They are not established whole-design regressions and should be covered by dedicated semantic/validation checks before claiming all-input preservation.

- **ROR with a wide constant:** old fast ROR tests the constant at its pin width; the existing certificate stores constant ROR operands at the result width. New fast ROR consumes that certificate constant. For a one-bit result and constant 2, the old test is nonzero but a one-bit constant becomes zero. No such changed expression appeared in the 100,481 matched corpus nodes. This is a potential inherited certificate-width problem exposed by making fast emission follow it.
- **Nonstandard/malformed arities:** zero-input Sum/Mult change from refusal to 0/1; Not now requires exactly one operand to leave its default-zero expression; wrong-arity Div/comparisons/SRA/GetMask/SetMask can default to zero where old emission refused. A unary SRA was previously a resize and now defaults to zero. Sext requires two operands instead of accepting any nonempty operand list. SHL can now XOR several shifted copies instead of requiring exactly two operands. Malformed Mux cases are no longer covered by the old explicit source-node checks. Reachability of these cases through a normal normalized graph has not been demonstrated.
- **Wide predicate results:** ROR, EQ and comparison expressions can now cast a one-bit Boolean to the certificate's declared result width. Old fast output was one bit. This changes acceptance/typechecking for unusual result-width declarations, rather than demonstrating a change on ordinary one-bit predicate nodes.

## Clock changes on another branch

The following commits are **not ancestors of a49de5a2d**; the clock-provenance commits are on `direction-2-ir-semantics`. They must not be used as an explanation for changes supposedly included in this legacy refactor.

| Commit | Change | Presence here |
|---|---|---|
| `ea0492b37` | Upstream single_edge phase-slot gating of memory write/sync-read commits, including ICG enables and edge normalization. It does not itself change DesignCert semantics. | Absent |
| `90583be13` | ClockDesc, clock ordinals, edge-vector step semantics, quiet-domain holds, async reset independent of firing edges, and associated checks/proofs. | Absent |
| `f5b317832` | Records asyncFlagMismatch for 54 older DesignCert artifacts lacking the new explicit asyncReset flag; re-emission is required on that branch. | Absent; not the 495 legacy Q-read changes counted here |
| `2a948ced8` | Includes synchronous ROM read registers in clock-domain assignment. | Absent |

This branch still models one logical step that updates the modeled state together. It does not establish faithful independent-clock timing for an unnormalized multi-clock graph.

## Other intervening changes that can change the input design

`485510493` fixes CVA6 wrapper type reconstruction and field/enum preservation. `f82056dbb` fixes packed array dimension order (same total width, different field indexing), as well as wrapper type-resolution issues. These are ancestors of the refactor baseline but upstream of the saved graph interface. This audit reuses saved graph databases and does not rerun wrappers/RTL; it cannot claim that independently regenerated wrappers before and after those commits describe the same design.

The refactor's `770b7d2e7` and `b0d6a9e14` correct the new tiny test fixtures' input-width annotations and RAM read-port wiring before freezing their goldens. They change test inputs, not the production node semantics. Matched comparisons must use the corrected frozen fixtures on both sides.

## Proof changes versus model changes

`a49de5a2d` adds mixed-width signed operator bridge lemmas and symbolic node-lookup lemmas to avoid Sext elaboration recursion. It changes no eval_op/runtime definition or fast-model expression. `9c08cdd8a` / `fa0be1ff2` implement and scale full certificate-WF proofs. These change proof construction and optional proof acceptance; they do not establish equality with a historical model that used different source values.

The full replay proves current fast model = current certificate evaluator. A behavior-preserving refactor additionally requires old/new meaning to agree on matched graph inputs (or an explicitly reviewed semantic change). The matched export audit is not a substitute for those proof obligations.

## What 0 versus -64 means, with the missing intermediate baseline

BitVec.ofInt w 64 stores the low w bits of 64. BitVec.toInt interprets those bits in two's complement.

- At six bits: 64 mod 64 = 0, bits 000000, signed value 0.
- At seven bits: 64 mod 128 = 64, bits 1000000. The top bit is the sign bit, so signed value is 64 - 128 = -64.
- Encoding positive 64 for signed interpretation requires eight bits (01000000).

For the six-bit input bit pattern 100000, the signed value is -32. The three versions of intpipe_alu node 676 are:

| | Older saved artifact | Immediate baseline 1af4e1492, matched graph | Current refactor |
|---|---|---|---|
| Constant | six-bit 64 -> 0 | seven-bit 64 -> -64 | seven-bit 64 -> -64 |
| Left operand interpretation | six-bit -32 | zero-extend to seven bits -> +32 | retain six bits -> -32 |
| Actual comparison | -32 < 0 | +32 < -64 | -32 < -64 |
| Result | true | false | false |

The prior two-column example was mathematically correct for the older artifact versus current output, but its attribution to this refactor alone was too broad. Both immediate-baseline and current node-676 expressions return false for every six-bit input; this is not evidence of a local node-676 behavior regression introduced by the refactor. Nor does it prove that this comparison matches the intended RTL meaning of 64.

The general mixed-width renderer rule really can change behavior: compare eight-bit 0 with four-bit 15. Zero-extending the latter gives +15, so 0 < 15 is true; interpreting it at its own four-bit signed width gives -1, so 0 < -1 is false.

[SignedWidthTimeline.lean](tests/legacy_semantic_audit/SignedWidthTimeline.lean) checks the three-version example and the separate general mixed-width counterexample. It passes with no sorryAx; the timeline theorem uses propext, and the separate mixed-width counterexample uses no axioms. These are local expression proofs, not whole-block output inequivalence proofs.

## All observed historical source-expression mismatches

| Block | Async Q-read expressions | Constant-width expressions |
|---|---:|---:|
| `cva6_tlb_gate` | 137 | 0 |
| `cva6_controller_gate` | 3 | 0 |
| `cva6_csr_buffer_gate` | 2 | 0 |
| `cva6_instr_realign_gate` | 3 | 0 |
| `cva6_ras_gate` | 4 | 0 |
| `intpipe_csr_msgs` | 41 | 3 |
| `minion_frontend_thread_sched` | 2 | 0 |
| `intpipe_csr_file_fl_barrier` | 2 | 0 |
| `intpipe_csr_pmu_read_interface` | 6 | 0 |
| `intpipe_csr_file_conv` | 3 | 0 |
| `minion_debug_apb_slv` | 4 | 0 |
| `intpipe_csr_replay` | 6 | 0 |
| `minion_dcache_cache_op_unit_l2` | 35 | 2 |
| `minion_dcache_miss_handler` | 0 | 1 |
| `vpu_sh_sw` | 1 | 0 |
| `minion_dcache_miss_handler_unit` | 6 | 1 |
| `vpu_trans` | 113 | 0 |
| `minion_dcache_cache_op_unit` | 39 | 0 |
| `intpipe_alu` | 0 | 30 |
| `minion_dcache_tensor_load` | 75 | 2 |
| `vpu_mask` | 13 | 0 |
| **Total** | **495** | **39** |

The 19 async blocks all refuse in the immediate pre-refactor legacy exporter. The two additional affected blocks, intpipe_alu and minion_dcache_miss_handler, export successfully at that baseline with certificate/source structure already matching current emission. Counts describe source-expression occurrences, not 534 independent whole-design behavioral counterexamples.

## Async reset clarification

The 19 refusals belong to the old exporter; the refactored exporter accepts these graphs. Every observed refusal explicitly reports ASYNCHRONOUS reset. Constant-width changes did not cause these refusals. Historical Q reads returned stored state even while reset was active. Current async Q reads return the declared reset value while reset is asserted, otherwise stored state; synchronous Q reads are unchanged. Thus stored Q=5 with asserted reset-to-zero was observed as 5 by old combinational logic and is observed as 0 now. Reset-on-next-state alone did not model this immediate read behavior. The bug/guard history is documented by b3268de66 and bf7eabf81, and by [VERIFIED_COMPILER_STATUS.md](VERIFIED_COMPILER_STATUS.md#modeling-async-reset). Legacy support is activated by e5a8a5ddb. It is attribute-driven, not a per-design exception.

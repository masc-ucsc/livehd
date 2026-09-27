# Historical legacy proof replay

The acceptance set has 90 design/fixture cases: three DINO variants, ten CVA6
blocks, 69 CORE-ET cases (including three primitive fixtures), and eight small
arithmetic/register/memory fixtures. This is the legacy named fast model's
fast/certificate bridge. Verified-compiler proof ledgers are separate evidence.

The expanded full bridge replay is in progress. The inventory defines coverage;
it does not mean all listed artifacts have passed the new check.

## Inventory and provenance

The 87 non-DINO cases, original artifact hashes, saved-graph locations, and
historical records are listed in
[tests/LEGACY_PROOF_INVENTORY.json](tests/LEGACY_PROOF_INVENTORY.json). Paths in
that inventory are relative to the original `livehd-new` workspace. The saved
artifacts can postdate their historical logs; the replay records a new hash for
the exact file actually checked.

| Group | Cases | Historical evidence |
|---|---:|---|
| DINO | 3 | [STEP5_BRIDGE_BUGS.md](STEP5_BRIDGE_BUGS.md), all three CPU variants |
| CVA6 | 10 | [CVA6_COVERAGE_PLAN.md](CVA6_COVERAGE_PLAN.md), saved Lean typecheck logs |
| CORE-ET | 68 | `generated/core-et/coreet_lean_summary.tsv`: 68 `PROVEN` rows, including three primitive fixtures |
| CORE-ET memory | 1 | `generated/memval/intpipe_csr_msgs`, documented memory-aware bridge |
| Small fixtures | 8 | Saved arithmetic/register and memory bridge artifacts |

The CVA6 blocks are `alu`, `tlb`, `compressed_decoder`, `controller`,
`csr_buffer`, `instr_realign`, `instr_scan`, `pmp`, `ras`, and `raw_checker`.
CORE-ET covers integer-pipeline, CSR, register-file, scoreboard, cache, frontend,
debug, vector, and tensor/FMA blocks. Its recorded serial proof time totals
about 34 hours; individual historical jobs range up to 16 hours. Those times
are historical measurements, not predictions for the current emitter.

The eight small fixtures are `add2`, `simple_add`, `simple_reg`, `ram1`,
`ram_be`, `ram_2w`, `ram_sync`, and `ram_sram`. Their recorded graph directories
are empty. Some temporary RTL inputs are also gone. Their saved Lean artifacts
can be checked against the current library, but such a check does not validate
re-emission by the current scan/export pipeline. The inventory marks them
`saved_artifact`; all 79 other non-DINO cases are regenerated from copied graphs.

Four additional CORE-ET exports (`minion_dcache_texsend`, `null_vpu`,
`tima_top`, `txfmactl_top`) have no completed legacy proof in the saved ledger.
They are not counted as previously completed proofs.

## Gates and measurements

Every regenerated production/primitive case uses `legacy emit_fast_bridge=true`.
Sequential cases must audit `comb_refines_fast`, `next_refines_fast`, and
`step_refines_fast`; combinational cases must audit `comb_refines_fast`.
An accepted proof needs exit zero, no Lean errors, every expected theorem audit,
and no `sorryAx`. Native-decision axioms remain explicit.

DINO also uses `cert_wf=chunked cert_chunk_size=25`, without a chunk limit or
fallback. WF alone checks certificate structure. The additional bridge proves
equality of the named fast model and certificate evaluator for arbitrary inputs
and states. Complete WF-only measurements for all three variants are recorded
in [tests/LEGACY_SCALABILITY_RESULTS.json](tests/LEGACY_SCALABILITY_RESULTS.json).

`tests/replay_legacy_proofs.py INVENTORY RUNTIME ENV_JSON --resume` runs one module
at a time using `lean -j 8`, eight allowed CPUs, and low scheduling priority.
It records `/usr/bin/time -v` output, the Lean log, file hash, imported-library
fingerprint, expected audit results, and verdict under each case directory.
`ENV_JSON` supplies the freshly built support library's `LEAN_PATH` and toolchain
`PATH`. Build the support library before invoking the runner. Resume reuses only
successful checks whose source and library fingerprints still match.

Runtime files are under `generated/legacy_refactor/historical_replay/` and
`generated/legacy_refactor/dino_full_proofs/`. Graph databases are copied before
opening because the graph library can update metadata on open. The original
dirty workspace remains read-only throughout this replay.

## Regressions found by the replay

`intpipe_alu` initially refused a mixed-width signed comparison bridge. New
`slt_widths_bridge` and `sgt_widths_bridge` lemmas retain each operand's signed
width. Focused 8-bit/4-bit comparison fixtures elaborate complete bridges and
compare against the canonical interpreter/compiler.

The first complete SingleCycle and Pipelined bridge checks failed at wide Sext
recurrences with a recursion-depth error. A one-node 64-to-32-bit fixture
reproduces it: converting the recurrence directly with `show` can unfold wide
operator arithmetic while reducing graph lookups. Symbolic `evalNode_bridge`
and `evalNodeC_bridge` lemmas separate those lookup equalities from the operator
proof. Focused 32-bit, 64-bit, and memory-valued Sext fixtures pass after this
change. Failed and interrupted full-run logs remain in `dino_full_proofs/wf_bridge`;
the corrected full artifacts are in `dino_full_proofs/wf_bridge_fixed`.

## Structural comparison limits

All 79 regenerated historical cases preserve NodeCerts, topo/source IDs, output
projections, and next-state dependencies. Parsing source values from balanced
lookup trees exposes intentional differences in 21 cases: 495 asynchronous-reset
read guards and 39 constant-width corrections from the shared certificate
pipeline. The other 58 cases preserve those source expressions as well.
The inventory records these differences per case. This is not a claim that all
historical and current model functions are semantically identical.

The replay uses saved graph databases. It does not rerun RTL elaboration or LEC.
The separate `lhd` dependency fetch currently encounters the pinned
`yosys-slang` archive checksum mismatch; the standalone Lean exporter and its
C++ tests build successfully.

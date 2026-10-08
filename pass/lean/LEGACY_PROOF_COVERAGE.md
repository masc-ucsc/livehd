# Legacy proof coverage snapshot

Snapshot: **2026-09-28T04:07:56+00:00**. Validated emitter/library source: `a49de5a2d`.
Branch: `refactor/pass-lean-shared-architecture`.

**68/90 passed; 1 running; 21 queued; no completed failures in the corrected replay.**
This is a committed snapshot. The background queue continues; this file does not update automatically.

All three DINO cases prove the complete chunked certificate-WF theorem and combinational, next-state and step fast/certificate refinements. The WF proofs use `native_decide`; the committed axiom-audit logs record its native-evaluation dependencies. The 79 regenerated historical cases use legacy emit_fast_bridge=true with cert_wf=skip, matching the historical bridge obligation. Eight saved-artifact cases check compatibility only: their graph databases are unavailable.

Passing the current model against its current certificate does not establish old/new semantic preservation. See [the semantic/commit audit](LEGACY_SEMANTIC_AUDIT.md). This replay uses saved graph databases and does not rerun RTL elaboration or LEC.

Machine-readable records, artifact hashes, library fingerprints, axiom-audit outcomes and timings: [LEGACY_PROOF_COVERAGE.json](tests/LEGACY_PROOF_COVERAGE.json). Historical provenance and old hashes: [LEGACY_PROOF_INVENTORY.json](tests/LEGACY_PROOF_INVENTORY.json).

## Every full replay case

`regenerated` means exported by the refactored legacy pass. `saved` means the old Lean artifact was checked against the current library. RSS below is measured peak RSS for completed jobs, not current RSS of the active job. Running/queued jobs have no accepted time or peak-memory measurement yet.

| Family | Block/design | Artifact | Status | Wall time | Peak RSS GiB |
|---|---|---|---|---|---:|
| DINO | `SingleCycleCPU` | regenerated | PROVEN | 24:14.13 | 14.56 |
| DINO | `PipelinedCPU` | regenerated | PROVEN | 25:20.65 | 15.71 |
| DINO | `PipelinedDualIssueCPU` | regenerated | PROVEN | 57:09.31 | 30.11 |
| CVA6 | `cva6_alu_export` | regenerated | QUEUED | — | — |
| CVA6 | `cva6_tlb_gate` | regenerated | RUNNING | — | — |
| CVA6 | `cva6_compressed_decoder_gate` | regenerated | QUEUED | — | — |
| CVA6 | `cva6_controller_gate` | regenerated | PROVEN | 0:43.91 | 6.50 |
| CVA6 | `cva6_csr_buffer_gate` | regenerated | PROVEN | 1:27.18 | 6.38 |
| CVA6 | `cva6_instr_realign_gate` | regenerated | PROVEN | 0:20.69 | 6.41 |
| CVA6 | `cva6_instr_scan_gate` | regenerated | PROVEN | 5:43.98 | 7.14 |
| CVA6 | `cva6_pmp_gate` | regenerated | QUEUED | — | — |
| CVA6 | `cva6_ras_gate` | regenerated | PROVEN | 0:18.98 | 6.28 |
| CVA6 | `cva6_raw_checker_gate` | regenerated | PROVEN | 0:28.49 | 6.50 |
| CORE-ET | `intpipe_csr_msgs` | regenerated | QUEUED | — | — |
| small | `add2` | saved | PROVEN | 0:18.14 | 6.28 |
| small | `ram1` | saved | PROVEN | 0:14.32 | 6.23 |
| small | `ram_be` | saved | PROVEN | 0:20.34 | 6.37 |
| small | `ram_2w` | saved | PROVEN | 0:14.45 | 6.24 |
| small | `ram_sync` | saved | PROVEN | 0:14.96 | 6.23 |
| small | `ram_sram` | saved | PROVEN | 0:14.48 | 6.24 |
| small | `simple_add` | saved | PROVEN | 0:13.65 | 6.20 |
| small | `simple_reg` | saved | PROVEN | 0:15.06 | 6.22 |
| CORE-ET | `prim_write_commit_en` | regenerated | PROVEN | 0:14.99 | 6.23 |
| CORE-ET | `prim_write_commit_rst_en` | regenerated | PROVEN | 0:13.99 | 6.25 |
| CORE-ET | `prim_phase_pair_lo_hi` | regenerated | PROVEN | 0:14.27 | 6.25 |
| CORE-ET | `intpipe_imm` | regenerated | PROVEN | 1:48.36 | 6.66 |
| CORE-ET | `txfma_align_shf` | regenerated | PROVEN | 0:22.30 | 6.20 |
| CORE-ET | `txfma_e4` | regenerated | PROVEN | 1:37.08 | 6.21 |
| CORE-ET | `tima_adder` | regenerated | PROVEN | 0:15.38 | 6.20 |
| CORE-ET | `txfma_frac_zero_detect` | regenerated | PROVEN | 0:15.26 | 6.20 |
| CORE-ET | `txfma_booth_ppg_32r4_msb` | regenerated | PROVEN | 0:13.93 | 6.22 |
| CORE-ET | `txfma_norm_shf` | regenerated | PROVEN | 0:15.07 | 6.22 |
| CORE-ET | `txfma_csa` | regenerated | PROVEN | 0:13.65 | 6.22 |
| CORE-ET | `txfma_rnd_adder` | regenerated | PROVEN | 0:14.88 | 6.24 |
| CORE-ET | `minion_frontend_thread_sched` | regenerated | PROVEN | 0:14.54 | 6.24 |
| CORE-ET | `txfma_4_2_compressor` | regenerated | PROVEN | 0:14.17 | 6.24 |
| CORE-ET | `intpipe_csr_file_fl_barrier` | regenerated | PROVEN | 0:15.59 | 6.29 |
| CORE-ET | `txfma_e6` | regenerated | PROVEN | 1:12.48 | 6.33 |
| CORE-ET | `debug_breakpoint` | regenerated | PROVEN | 0:18.74 | 6.35 |
| CORE-ET | `minion_dcache_scratchpad_ctrl` | regenerated | PROVEN | 0:20.69 | 6.37 |
| CORE-ET | `txfma_trz` | regenerated | PROVEN | 0:42.82 | 6.37 |
| CORE-ET | `intpipe_inst_bits_stage` | regenerated | PROVEN | 0:38.28 | 6.47 |
| CORE-ET | `txfma_c1` | regenerated | PROVEN | 0:21.02 | 6.36 |
| CORE-ET | `intpipe_csr_pmu_read_interface` | regenerated | PROVEN | 0:19.46 | 6.38 |
| CORE-ET | `minion_dcache_lru_array` | regenerated | PROVEN | 0:24.90 | 6.52 |
| CORE-ET | `txfma_c4` | regenerated | PROVEN | 0:24.76 | 6.47 |
| CORE-ET | `minion_dcache_writeback_unit` | regenerated | PROVEN | 0:28.49 | 6.61 |
| CORE-ET | `txfma_exp_special_detect` | regenerated | PROVEN | 0:25.46 | 6.52 |
| CORE-ET | `intpipe_csr_file_conv` | regenerated | PROVEN | 0:41.07 | 6.72 |
| CORE-ET | `txfma_e1` | regenerated | PROVEN | 0:34.54 | 6.65 |
| CORE-ET | `minion_dcache_pma_unit` | regenerated | PROVEN | 0:37.26 | 6.63 |
| CORE-ET | `vpu_tensorreduce` | regenerated | PROVEN | 0:40.38 | 6.78 |
| CORE-ET | `vpu_bypass` | regenerated | PROVEN | 0:37.28 | 6.80 |
| CORE-ET | `vpu_uinst_decoder` | regenerated | PROVEN | 0:37.11 | 6.71 |
| CORE-ET | `txfma_c5` | regenerated | PROVEN | 0:59.43 | 6.82 |
| CORE-ET | `minion_debug_apb_slv` | regenerated | PROVEN | 0:57.28 | 6.90 |
| CORE-ET | `minion_dcache_atomic_alu_unit` | regenerated | PROVEN | 1:29.01 | 7.05 |
| CORE-ET | `intpipe_mask_scoreboard` | regenerated | PROVEN | 1:05.63 | 7.06 |
| CORE-ET | `intpipe_int_scoreboard` | regenerated | PROVEN | 1:07.72 | 7.15 |
| CORE-ET | `intpipe_csr_replay` | regenerated | PROVEN | 1:30.22 | 7.37 |
| CORE-ET | `txfma_trz2` | regenerated | PROVEN | 1:12.44 | 7.20 |
| CORE-ET | `txfma_c6` | regenerated | PROVEN | 1:22.25 | 7.73 |
| CORE-ET | `minion_dcache_cache_op_unit_l2` | regenerated | PROVEN | 13:19.71 | 8.19 |
| CORE-ET | `intpipe_fp_scoreboard` | regenerated | PROVEN | 1:27.29 | 7.45 |
| CORE-ET | `vpu_tensorquant` | regenerated | PROVEN | 4:28.39 | 8.17 |
| CORE-ET | `txfma_ediff_opdorder_logic` | regenerated | PROVEN | 1:39.21 | 7.57 |
| CORE-ET | `intpipe_rf` | regenerated | PROVEN | 1:48.96 | 8.72 |
| CORE-ET | `minion_dcache_miss_handler` | regenerated | PROVEN | 48:41.91 | 8.37 |
| CORE-ET | `txfma_c2` | regenerated | PROVEN | 3:05.71 | 8.38 |
| CORE-ET | `minion_frontend_rvc_expander` | regenerated | PROVEN | 8:13.01 | 7.82 |
| CORE-ET | `txfma_c3` | regenerated | PROVEN | 3:03.12 | 8.16 |
| CORE-ET | `txfma_e2` | regenerated | PROVEN | 4:49.26 | 8.41 |
| CORE-ET | `txfma_lxd` | regenerated | PROVEN | 3:45.81 | 8.41 |
| CORE-ET | `txfma_c0` | regenerated | QUEUED | — | — |
| CORE-ET | `vpu_tensorfma` | regenerated | QUEUED | — | — |
| CORE-ET | `minion_dcache_store_merge_unit` | regenerated | QUEUED | — | — |
| CORE-ET | `vpu_sh_sw` | regenerated | QUEUED | — | — |
| CORE-ET | `minion_dcache_miss_handler_unit` | regenerated | QUEUED | — | — |
| CORE-ET | `txfma_f4` | regenerated | QUEUED | — | — |
| CORE-ET | `txfma_4_2_compressor_array` | regenerated | QUEUED | — | — |
| CORE-ET | `vpu_trans` | regenerated | QUEUED | — | — |
| CORE-ET | `minion_dcache_cache_op_unit` | regenerated | QUEUED | — | — |
| CORE-ET | `vpu_decoder` | regenerated | QUEUED | — | — |
| CORE-ET | `vpu_ml` | regenerated | QUEUED | — | — |
| CORE-ET | `intpipe_alu` | regenerated | QUEUED | — | — |
| CORE-ET | `txfma_wallace2` | regenerated | QUEUED | — | — |
| CORE-ET | `intpipe_decode` | regenerated | QUEUED | — | — |
| CORE-ET | `txfma_wallace1` | regenerated | QUEUED | — | — |
| CORE-ET | `minion_dcache_tensor_load` | regenerated | QUEUED | — | — |
| CORE-ET | `vpu_mask` | regenerated | QUEUED | — | — |

## Other covered exports and fixtures

These are separate validation sets and overlap the replay cases. Export success is not a full proof result.

- **148 default legacy corpus exports:** 134 accepted and 14 refused. Every design, export result and artifact hash is listed in the committed [LEGACY_SHARED_RESULTS.tsv](tests/LEGACY_SHARED_RESULTS.tsv). Detailed refusals are also captured in [LEGACY_VALIDATION_COVERAGE.json](tests/LEGACY_VALIDATION_COVERAGE.json).
- **38 graph fixtures:** 27 semantic-oracle passes, 24 chunked-WF passes, and 23 optional-bridge passes; the remaining cases have explicit unsupported-shape/policy refusals for each gate. The final bridge record supersedes earlier attempts on those same fixtures.
- **10 owned arithmetic/state/memory oracles:** all pass; names appear below.
- **One constant-only WF oracle:** passes.
- **79 matched-input baseline re-exports for the semantic audit:** 60 accepted with matching parsed certificate/source structure; 19 refused for async reset. These are export comparisons, not new full baseline proof runs.

### All graph fixtures

| Fixture | Semantic oracle | Chunked WF | Fast bridge |
|---|---|---|---|
| `tiny_sum` | PASS | PASS | PASS |
| `tiny_mult` | PASS | REFUSED | REFUSED |
| `tiny_div` | PASS | REFUSED | REFUSED |
| `tiny_and` | PASS | PASS | PASS |
| `tiny_or` | PASS | PASS | PASS |
| `tiny_xor` | PASS | PASS | PASS |
| `tiny_ror` | PASS | PASS | PASS |
| `tiny_eq` | PASS | PASS | PASS |
| `tiny_not` | PASS | PASS | PASS |
| `tiny_lt` | PASS | PASS | PASS |
| `tiny_gt` | PASS | PASS | PASS |
| `tiny_shl` | PASS | PASS | PASS |
| `tiny_sra` | PASS | PASS | PASS |
| `tiny_mux` | PASS | PASS | PASS |
| `tiny_sext` | PASS | PASS | REFUSED |
| `tiny_getmask` | PASS | PASS | PASS |
| `tiny_setmask` | PASS | REFUSED | REFUSED |
| `tiny_unsupported` | REFUSED | REFUSED | REFUSED |
| `tiny_reset` | PASS | PASS | PASS |
| `tiny_async` | PASS | PASS | PASS |
| `tiny_active_low` | PASS | PASS | PASS |
| `tiny_nonzero` | PASS | PASS | PASS |
| `tiny_negedge` | REFUSED | REFUSED | REFUSED |
| `tiny_pipeline` | REFUSED | REFUSED | REFUSED |
| `tiny_ram` | PASS | PASS | PASS |
| `tiny_sync_ram` | PASS | PASS | PASS |
| `tiny_byte_ram` | PASS | PASS | PASS |
| `tiny_forward_ram` | PASS | PASS | PASS |
| `tiny_rom` | PASS | PASS | PASS |
| `tiny_sync_rom` | PASS | PASS | PASS |
| `tiny_ram_init` | REFUSED | REFUSED | REFUSED |
| `tiny_whole_array` | REFUSED | REFUSED | REFUSED |
| `tiny_undef` | REFUSED | REFUSED | REFUSED |
| `tiny_unknown_fwd` | REFUSED | REFUSED | REFUSED |
| `tiny_bad_type` | REFUSED | REFUSED | REFUSED |
| `tiny_bad_wensize` | REFUSED | REFUSED | REFUSED |
| `tiny_array_init` | REFUSED | REFUSED | REFUSED |
| `tiny_dynamic_init` | REFUSED | REFUSED | REFUSED |

### All owned oracles

| Fixture | Lean result | Verified-compiler refusal expected |
|---|---|---|
| `tiny_div_narrow` | PASS | yes |
| `tiny_sext_dynamic` | PASS | no |
| `tiny_slt_mixed` | PASS | no |
| `tiny_sgt_mixed` | PASS | no |
| `tiny_sra_wide` | PASS | no |
| `tiny_sext_trunc32` | PASS | no |
| `tiny_sext_trunc64` | PASS | no |
| `tiny_sext_same32` | PASS | no |
| `tiny_swap` | PASS | no |
| `tiny_sext_memory64` | PASS | no |

The additional `tiny_const_chunk` passes its constant-only WF proof and oracle. Compiler refusal in an oracle is an explicit expected unsupported-operation result; it is not a failed Lean check.

The signed-width timeline and mixed-width counterexample also pass; their [Lean source](tests/legacy_semantic_audit/SignedWidthTimeline.lean) and [audit log](tests/legacy_semantic_audit/signed_width_timeline.log) are committed.

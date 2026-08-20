# Plan: prove fast model ≡ graph certificate for every CVA6 block

**Objective:** every CVA6 module through `pass.lean` with `_comb`/`_next`/`_step`
proven equal to its certificate model. Performance work is subordinate — it buys
throughput, it is not the goal.

**Target list: the 78 blocks of the CVA6 core hierarchy reachable from module `cva6`**,
enumerated in `/soe/czeng14/projects/cva6-blockfinding/cva6_blocks.txt` (section 16
TOTALS). Those come first; the 4 CV-X-IF example modules, 2 RVFI modules, 13 CVFPU and
65 CV-HPDcache modules in that file's later totals come after. **10 of the 78 are
proven; 68 remain** — see Phase 2 for the full list with per-block status.

## Proven so far — 10 CVA6 modules (+ DINO's 3 CPUs)

All `exit 0`, 0 errors, 0 sorries, 0 axioms.

| module | nodes | flops | max w | wall | theorems |
|---|---|---|---|---|---|
| `cva6_alu_export` | 6,305 | 0 | 576 | 4 h 15.8 m † | `_comb` |
| `cva6_pmp_gate` | 5,344 | 0 | 434 | 6 h 13.8 m † | `_comb` |
| `cva6_tlb_gate` | 2,061 | 137 | 513 | 5 h 12.5 m † | `_comb`/`_next`/`_step` |
| `cva6_compressed_decoder_gate` | 1,946 | 0 | 33 | 162 s | `_comb` |
| `cva6_instr_scan_gate` | 638 | 0 | 65 | 329 s | `_comb` |
| `cva6_raw_checker_gate` | 345 | 0 | 41 | 16 s | `_comb` |
| `cva6_controller_gate` | 270 | 3 | 136 | 35 s | `_comb`/`_next`/`_step` |
| `cva6_instr_realign_gate` | 196 | 3 | 129 | 11 s | `_comb`/`_next`/`_step` |
| `cva6_csr_buffer_gate` | 96 | 2 | 209 | 77 s | `_comb`/`_next`/`_step` |
| `cva6_ras_gate` | 76 | 4 | 131 | 7 s | `_comb`/`_next`/`_step` |

† **These three walls predate the `or1/or3/or4` fix and have not been re-verified.**
Combinational modules correctly emit `_comb` only; the five with flops prove all three.

## Phase 0 — re-verify the big three (do this first)

Not an optimisation errand: their proofs were produced by the *previous* emitter, and
`or1/or3/or4` changed what it emits for them. Re-emit and re-run to confirm they still
prove, and record the new wall in the same pass. Fold-Or nodes the fix now handles:

| module | n-ary Or | now fold-free (ar 1/3/4) | still on fold | arities left |
|---|---|---|---|---|
| `cva6_pmp_gate` | 846 | **750** | 96 | 8, 16, 32 |
| `cva6_alu_export` | 418 | **354** | 64 | 5–8, 16, 32–34, 49, 57, 64, 65 |
| `cva6_tlb_gate` | 224 | **210** | 14 | 8, 12, 40, 96 |

This also delivers the pmp/ALU attribution that is still missing — if pmp drops sharply,
`Or` explains it; if not, it does not, and that is measured rather than assumed.

## Phase 1 — arity 5–8 bridges, measured against a prediction

`or5..or8_bridge` in the `or3`/`or4` pattern (one `have` per level; recall
`bv_bitwise_eq` already peels the outermost, so arity N needs N−2). Cheap and
mechanical. Clears **all** remaining fold nodes in `raw_checker` (7,8), `controller`
(6), `csr_buffer` (5), most of `compressed_decoder` (5×11, 6×5, 7, 8), and pmp's 8s.

**State the prediction before running** so the measurement can falsify it: per-node
cost is monotone in arity at ~11.6 s (ar 3–4) → 74–76 s (ar 47–60), so arity 5–8 should
be ~12–20 s/node. `compressed_decoder` has 17 such nodes ⇒ predicted saving ~200–340 s
against a 162 s total, i.e. **the prediction is already inconsistent with the observed
wall** — so either the rate is much lower at low arity or something else is going on.
Resolve that by attribution on `compressed_decoder` (stub its ar5/6 group, re-time)
*before* writing eight lemmas.

## Phase 2 — the 78-block CVA6 core hierarchy (the authoritative target list)

**Source of truth:** `/soe/czeng14/projects/cva6-blockfinding/cva6_blocks.txt`
(openhwgroup/cva6 @ `6cb200105`). Its section 16 TOTALS gives
**78 = the CVA6 core hierarchy reachable from module `cva6`**, synthesizable, under
`core/`. Independently re-extracted from sections 1–9 of that file: 78, exact match.

The same TOTALS block adds 4 (CV-X-IF example coprocessor, outside `cva6`) + 2 (RVFI
trace RTL) for a `core/`-tree total of **84**, then 13 CVFPU + 65 CV-HPDcache for a
grand total of 162. **These 78 come first.**

> Caveat from the file itself: the 78 counts *mutually exclusive variants* — 3 cache
> subsystems (std/wt/hpdcache), 2 register files (ff/fpga), 2 BHTs (bht/bht2lvl). Any
> one build elaborates fewer, so some of the 78 are alternates rather than additions.

**Status: 10 of 78 proven, 68 remaining.**

### TOP LEVEL  (1)

- `cva6` [cva6.sv]

### FRONTEND / INSTRUCTION FETCH (PC-gen + IF stage)  (8)

- `bht` [frontend/bht.sv]
- `bht2lvl` [frontend/bht2lvl.sv]
- `btb` [frontend/btb.sv]
- `frontend` [frontend/frontend.sv]
- `instr_queue` [frontend/instr_queue.sv]
- `instr_realign` [instr_realign.sv] — **PROVEN** as `cva6_instr_realign_gate`
- `instr_scan` [frontend/instr_scan.sv] — **PROVEN** as `cva6_instr_scan_gate`
- `ras` [frontend/ras.sv] — **PROVEN** as `cva6_ras_gate`

### ID STAGE (decode)  (7)

- `compressed_decoder` [compressed_decoder.sv] — **PROVEN** as `cva6_compressed_decoder_gate`
- `cva6_accel_first_pass_decoder` [cva6_accel_first_pass_decoder_stub.sv]
- `cvxif_compressed_if_driver` [cvxif_compressed_if_driver.sv]
- `decoder` [decoder.sv]
- `id_stage` [id_stage.sv]
- `macro_decoder` [macro_decoder.sv]
- `zcmt_decoder` [zcmt_decoder.sv]

### ISSUE STAGE  (7)

- `ariane_regfile` [ariane_regfile_ff.sv]
- `ariane_regfile_fpga` [ariane_regfile_fpga.sv]
- `cvxif_issue_register_commit_if_driver` [cvxif_issue_register_commit_if_driver.sv]
- `issue_read_operands` [issue_read_operands.sv]
- `issue_stage` [issue_stage.sv]
- `raw_checker` [raw_checker.sv] — **PROVEN** as `cva6_raw_checker_gate`
- `scoreboard` [scoreboard.sv]

### EX STAGE  -  FUNCTIONAL UNITS  (19)

- `ex_stage` [ex_stage.sv]
*debug/trigger*
- `trigger_module` [trigger_module.sv]
*extension / accelerator units (writeback port 4)*
- `acc_dispatcher` [?]
- `cvxif_fu` [cvxif_fu.sv]
*fixed-latency unit (FLU, writeback port 0)*
- `aes` [aes.sv]
- `alu` [alu.sv] — **PROVEN** as `cva6_alu_export`
- `alu_wrapper` [alu_wrapper.sv]
- `branch_unit` [branch_unit.sv]
- `csr_buffer` [csr_buffer.sv] — **PROVEN** as `cva6_csr_buffer_gate`
- `mult` [mult.sv]
- `multiplier` [multiplier.sv]
- `serdiv` [serdiv.sv]
*floating point (writeback port 3)*
- `fpu_wrap` [fpu_wrap.sv]
*load/store (writeback ports 1 and 2)*
- `amo_buffer` [amo_buffer.sv]
- `load_store_unit` [load_store_unit.sv]
- `load_unit` [load_unit.sv]
- `lsu_bypass` [lsu_bypass.sv]
- `store_buffer` [store_buffer.sv]
- `store_unit` [store_unit.sv]

### COMMIT / CONTROL / CSR  (4)

- `commit_stage` [commit_stage.sv]
- `controller` [controller.sv] — **PROVEN** as `cva6_controller_gate`
- `csr_regfile` [csr_regfile.sv]
- `perf_counters` [perf_counters.sv]

### MMU + PMP  (7)

- `cva6_mmu` [cva6_mmu/cva6_mmu.sv]
- `cva6_ptw` [cva6_mmu/cva6_ptw.sv]
- `cva6_shared_tlb` [cva6_mmu/cva6_shared_tlb.sv]
- `cva6_tlb` [cva6_mmu/cva6_tlb.sv] — **PROVEN** as `cva6_tlb_gate`
- `pmp` [pmp/src/pmp.sv] — **PROVEN** as `cva6_pmp_gate`
- `pmp_data_if` [pmp/src/pmp_data_if.sv]
- `pmp_entry` [pmp/src/pmp_entry.sv]

### CACHE SUBSYSTEM  (24)

*instruction cache*
- `cva6_icache` [cache_subsystem/cva6_icache.sv]
- `cva6_icache_axi_wrapper` [cache_subsystem/cva6_icache_axi_wrapper.sv]
*memory-side adapters*
- `axi_adapter` [cache_subsystem/axi_adapter.sv]
- `axi_shim` [axi_shim.sv]
- `cva6_hpdcache_if_adapter` [cache_subsystem/cva6_hpdcache_if_adapter.sv]
- `cva6_hpdcache_subsystem_axi_arbiter` [cache_subsystem/cva6_hpdcache_subsystem_axi_arbiter.sv]
- `cva6_hpdcache_subsystem_l15_adapter` [cache_subsystem/cva6_hpdcache_subsystem_l15_adapter.sv]
- `cva6_hpdcache_wrapper` [cache_subsystem/cva6_hpdcache_wrapper.sv]
- `wt_axi_adapter` [cache_subsystem/wt_axi_adapter.sv]
- `wt_l15_adapter` [cache_subsystem/wt_l15_adapter.sv]
*selectable subsystem top levels (one of three)*
- `cva6_hpdcache_subsystem` [cache_subsystem/cva6_hpdcache_subsystem.sv]
- `std_cache_subsystem` [cache_subsystem/std_cache_subsystem.sv]
- `wt_cache_subsystem` [cache_subsystem/wt_cache_subsystem.sv]
*write-back (std) data cache*
- `amo_alu` [cache_subsystem/amo_alu.sv]
- `axi_adapter_arbiter` [cache_subsystem/miss_handler.sv]
- `cache_ctrl` [cache_subsystem/cache_ctrl.sv]
- `miss_handler` [cache_subsystem/miss_handler.sv]
- `std_nbdcache` [cache_subsystem/std_nbdcache.sv]
- `tag_cmp` [cache_subsystem/tag_cmp.sv]
*write-through data cache*
- `wt_dcache` [cache_subsystem/wt_dcache.sv]
- `wt_dcache_ctrl` [cache_subsystem/wt_dcache_ctrl.sv]
- `wt_dcache_mem` [cache_subsystem/wt_dcache_mem.sv]
- `wt_dcache_missunit` [cache_subsystem/wt_dcache_missunit.sv]
- `wt_dcache_wbuffer` [cache_subsystem/wt_dcache_wbuffer.sv]

### SHARED / UTILITY  (1)

- `cva6_fifo_v3` [cva6_fifo_v3.sv]

**Batch by reused type binding**, since wrapper effort — not runtime — is the real
per-module cost:

1. **`fu_data_t` + `CVA6Cfg`** (already written for `alu`/`csr_buffer`) → `alu_wrapper`,
   `branch_unit`, `aes`, `mult`, `multiplier`, `serdiv`, `cvxif_fu`.
2. **`pmp` binding** (already written) → `pmp_entry`, `pmp_data_if`, `cva6_ptw`.
3. **Frontend bindings** (`instr_scan`/`ras`/`instr_realign`) → `instr_queue`, `bht`,
   `bht2lvl`, `btb`, then whole-block `frontend` at `FpgaEn=0`.
4. **Zero-param leaves** — `amo_alu`, `cva6_fifo_v3`, `lzc`, `popcount`, `tag_cmp`.
5. **Big pure-comb** — `decoder` (1988), `commit_stage` (415).
6. **Big sequential** — `csr_regfile` (3088), `issue_read_operands` (1117), `scoreboard`,
   `macro_decoder`, `load_unit`, `store_unit`.

**Classify by running the pipeline, not by pattern-matching.** Whether a block is
memory-blocked is decided by `emit` + `op_census.py` — seconds per module, exact, and
memory hard-errors under `emit_fast_bridge`. Never by a regex over the source: a static
reconstruction attempt classified `lzc` as memory-blocked (false positive) and, because
`alu`/`pmp_entry`/`serdiv`/`load_unit` all instantiate `lzc`, propagated that to mark
`alu` and `pmp` blocked — **both are already proven**. Memory blockage is also
*transitive*: a module whose own file is clean can still be blocked by a child.

**Config knobs move blocks out of the memory-blocked set for free** — no `pass.lean`
change: `UseSharedTlb=0` (kills both `sram` instances in `cva6_shared_tlb`, unblocking it
plus `cva6_mmu`, `load_store_unit`, `ex_stage` — ~3 kLOC), `FpgaEn=0` (keeps `bht`/`btb`/
`cva6_fifo_v3`/`instr_queue`/`frontend` on flop arrays instead of BRAM/LUTRAM),
`EnableAccelerator=0`, `FpPresent=0`, `eccEn=0`. Confirm per module that the guard really
elaborates away — the census will say so.

**Expected emitter work, from the census not from guessing:** `Op_Mult` (`multiplier`),
`Op_Div`/`SDiv` (`serdiv`, plus the signedness bug at `pass_lean.cpp:1139` mapping
`Div`→`Op_UDiv` unconditionally), `Op_SetMask`, unequal-width `SLT`, and higher
`And`/`Or`/`Xor` arities. Six lemmas already exist unwired (`sgt`, `andn`, `xorn`,
`rorn`, `muxn`, `sumn`).

## Phase 3 — memory certificate: **DONE for async/array, sync-read pending**

All four pieces landed:

- `GraphRefine` CertVal twin — `evalNodeC` / `envSetC` / `evalGraphC` +
  `evalGraphC_char` / `evalGraphC_of_localAgree` / `evalGraphC_not_mem`. Additive,
  so the BV path the other designs elaborate against is byte-identical and none of
  them needed re-proving. `DepOrdered` / `DepOrderedB` are value-free and shared,
  and with them the `native_decide` gates.
- `simpleOpCertWfBool` gained the three memory arities.
- `memenc` / `memdec` + `mem_read_bridge` / `mem_write_bridge` /
  `mem_write_be_bridge` in `OpBridge`. 0 sorries.
- `cert_memory_expand` in the emitter: array image as a source, one
  `Op_MemWrite[BE]` per write port **chained**, one `Op_MemRead` per read port over
  the image *that port* observes (the `fwd` matrix is per (read, write) pair, so
  chains are keyed by forwarded-set signature and shared), every port operand
  through an arity-1 `Op_Or` resize.

Two traps found in the process, both written up in `STEP5_BRIDGE_BUGS.md`: `memenc`
aliasing out-of-range indices onto a written address (Bug 12) and a wider address
dep indexing past the array (Bug 13).

**Proven** — `_comb`/`_next`/`_step`, exit 0, 0 errors, 0 `sorryAx`, 3/3
`_refines_fast`:

| fixture | shape |
|---|---|
| `ram1` | 1R/1W async, bit-level write mask (`wensize == bits`) |
| `ram_be` | byte-enable write, `byte_w = 4` |
| `ram_2w` | 2 write ports — write chain and `fwd` matrix |
| `ram_sync` | memory + a real flop (mixed state record) |

**Still refused, with an accurate message:** sync-read memories (`type == 1`, e.g.
`tc_sram` at `Latency=1`). The read-data register needs a source plus an
`Op_MuxBool` next-state node over an ungated `Op_MemRead` — no new certificate
operator, just emitter work.

### The "43" was not a measurement — here is one

The heading above used to claim this unblocks 43 blocks. That number is not derived
anywhere in this document, and the file's own rule is that blockage is decided by
`emit` + `op_census.py`, never by pattern-matching. Measured on CORE-ET, whose
worklist marks **31** modules `reason=memory`, those 31 are three unrelated
blockers:

| blocker | count | what it actually needs |
|---|---|---|
| `pass.single_edge` phase divider | 16 | a different pass; memory would commit on every sub-step under a phase divider |
| `unsupported Memory pin \`init\`` | 13 | ROM initial contents in the **fast model** — nothing to do with the certificate |
| certificate stub | **2** | this phase (`intpipe_csr_msgs`, `minion_dcache_reduce`) |

So the memory certificate directly unblocks 2 CORE-ET modules; `init` support is the
bigger lever there and is the natural next item. The CVA6 side still needs the same
measurement: the four `*_gate` wrappers named in the old plan
(`hpdcache_fifo_reg_gate`, the two `hpdcache_regbank_*_1rw_gate`s, `tc_sram_gate`)
turn out to emit **no Memory node at all** under the current yosys settings — the
arrays are flop-blasted — so they were never memory tests. `hpdcache_fifo_reg_gate`
is genuinely a flop-based FIFO.

## Out of scope, stated so the list reads as complete

**cvfpu (141 modules, 33 kLOC)** — `OpBridge` is integer `BitVec` only; FP needs a new
bridge layer, and cvfpu vendors a second common_cells copy that name-clashes.
**Whole-core `cva6`** — multi-blocked, and both front-ends fail on it independently
(`sv2v` on `acc_dispatcher`; slang → `pass/cprop/cprop.cpp:459`).

## Execution

**One Lean typecheck at a time** (`scripts/run_lean_queue.sh`, detached via
`systemd-run --user`; refuses to start while any `lean` is alive). Shared NFS server,
and an OOM-killed run is indistinguishable from a proof failure in the log.

Per module: wrapper → emit → static gates (`op_census.py`, `const_parity.py`,
`sorry`=0, expected `_refines_fast` present) → queue → record wall/RSS.

**Definition of done: full-file `exit 0`.** Gates green is weaker and separate —
`cva6_tlb_gate` passed every gate and still failed after 5 h on Bug 11.

**Measure performance opportunistically, never in place of proving.** State the
prediction first; four cost models have already been fitted and refuted here
(contention, node width, term size, an arity-3 threshold), each of which merely
correlated. Only stub-one-group-and-re-time has survived.

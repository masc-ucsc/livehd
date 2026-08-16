# Plan: prove every CVA6 block (fast model ≡ certificate)

**Goal:** every CVA6 module goes through `pass.lean` and has `_comb`/`_next`/`_step`
proven equal to its certificate model.

## Status

**Proven (full-file `exit 0`, 0 sorries):**

| module | nodes | flops | max w | wall | peak RSS |
|---|---|---|---|---|---|
| `cva6_alu_export` | 6,305 | 0 | 576 | 4 h 15.8 m | 25.3 GB |
| `cva6_tlb_gate` | 2,061 | 137 | 513 | 5 h 12.5 m | 12.2 GB |
| `cva6_raw_checker_gate` | 345 | 0 | 41 | **24.7 s** | 7.0 GB |
| `cva6_controller_gate` | 270 | 3 | 136 | **45.2 s** | 7.1 GB |
| `cva6_instr_realign_gate` | 196 | 3 | 129 | **17.0 s** | 6.9 GB |
| `cva6_csr_buffer_gate` | 96 | 2 | 209 | **84.7 s** | 6.9 GB |

Plus DINO's three CPUs. Four more (`instr_scan`, `compressed_decoder`, `ras`, `pmp`)
are emitted, gate-green, and in the running serial queue.

## The finding that shapes everything: cost is ~quadratic in WIDTH, not node count

| design | nodes | max w | wall | s/node |
|---|---|---|---|---|
| SingleCycleCPU | 4,772 | 127 | 1,512 s | **0.32** |
| PipelinedCPU | 5,061 | 127 | 1,608 s | **0.32** |
| DualIssueCPU | 10,740 | 127 | 3,444 s | **0.32** |
| cva6_alu_export | 6,305 | 576 | 15,348 s | 2.43 |
| cva6_tlb_gate | 2,061 | 513 | 18,750 s | 9.10 |

DINO holds at *exactly* 0.32 s/node across a 2.25× size range — linear in nodes at
fixed width. Dividing wall by **Σ(width²)** gives 9.1e-4 (ALU) and 8.1e-4 (TLB),
agreeing within 12%. The small modules confirm the direction: 96-node/209-bit
`csr_buffer` (84.7 s) cost **3.4× more** than 345-node/41-bit `raw_checker` (24.7 s).

Consequences:
- **Per-module cost is predictable before running** — `K·Σw²` with `K ≈ 8.6e-4`.
  Use it to order the queue and to decide what is worth attempting.
- **Narrow control/decode logic is nearly free** (seconds). The hours go into wide
  datapaths and 512-bit cache lines.
- The model is *conservative* at the small end (predicted minutes, actual seconds),
  so treat it as an upper bound.

## Inventory: 201 modules in scope (+141 cvfpu excluded)

| bucket | count | meaning |
|---|---|---|
| **A — READY** | **128** (118 net new) | wrapper only; works with today's `pass.lean` |
| **B — memory-blocked** | 43 | SRAM macro or unpacked array |
| **C — accelerator** | 2 | `acc_dispatcher`, cvxif example coprocessor |
| **D — FP** | 3 | `fpu_wrap`, `ex_stage`, `cva6` top |
| **E — other** | 25 | testbench, blackbox, tech cell, DPI, SV classes/queues |

CVA6 core RTL is overwhelmingly **packed**-array based, so far fewer modules infer
memory than expected. `scoreboard`'s `mem_q`, `ariane_regfile`'s `mem`, and `ras`'s
stack are all packed ⇒ flops ⇒ bucket A despite the names.

## Lever 1 — config knobs move modules B→A for free

The single highest-yield action, and it needs **no `pass.lean` change at all**. Set
these in each gate wrapper's `cva6_cfg_t`:

| knob | set | effect |
|---|---|---|
| **`UseSharedTlb`** | **0** | kills both `sram` instances in `cva6_shared_tlb` (guard at :510) ⇒ **`cva6_shared_tlb`, `cva6_mmu`, `load_store_unit`, `ex_stage` all become READY** — ~3 kLOC of core datapath |
| `FpgaEn` / `FpgaAlteraEn` | 0 | `bht`, `btb`, `cva6_fifo_v3`, `instr_queue`, `frontend`, `issue_read_operands` use flop arrays instead of `SyncDpRam`/`AsyncDpRam`/`*ThreePortRam` |
| `EnableAccelerator` | 0 | `decoder` skips the `$error` stub at :151; `cva6` skips `acc_dispatcher` |
| `FpPresent` | 0 | `ex_stage`, `cva6` skip `fpu_wrap` → `fpnew_top` |
| `HPDcacheCfg.u.eccEn` | 0 | drops the `hpdcache_sram_ecc_1rw` variants |

**Verify per module that the guard really elaborates away** — a knob that is read at
runtime rather than in a generate condition would leave the SRAM in the cone. Confirm
via the census (memory ⇒ the emitter now hard-errors on `emit_fast_bridge`, so this
fails loudly, not silently).

## Lever 2 — batch the wrapper work by reusing bindings

Wrapper effort, not runtime, is the per-module cost for bucket A. Group so each new
type binding is written once:

1. **Zero-param comb leaves (~20 modules, near-zero effort):** the 8 `prim_secded_*`
   ECC modules (no params at all), `hpdcache_amo`, `hpdcache_{decoder,mux,demux,
   prio_1hot_encoder,prio_bin_encoder,1hot_to_binary}`, `lzc`, `popcount`, `unread`,
   `amo_alu`.
2. **Reuse the `fu_data_t` + `CVA6Cfg` binding** already written for the ALU/csr_buffer
   → `aes`, `alu_wrapper`, `branch_unit`, `mult`, `multiplier`, `serdiv`.
3. **Reuse the `pmp` binding** → `pmp_entry`, `pmp_data_if`, then `cva6_ptw` (703).
4. **Reuse the frontend bindings** (`instr_scan`/`ras`/`instr_realign`) → `bht2lvl`,
   `bht`, `btb`, `instr_queue`, then whole-block `frontend` (593) at `FpgaEn=0`.
5. **Big pure-comb prizes:** `hpdcache_ctrl_pe` (1275 lines, *zero* type params, zero
   flops), `decoder` (1988), `commit_stage` (415).
6. **Big sequential prizes:** `csr_regfile` (3088), `issue_read_operands` (1117),
   `scoreboard` (356), `hpdcache_{uncached,rtab,wbuf,cmo}`, `wt_axi_adapter`,
   `miss_handler`.

## Expected `pass.lean` work (fix once, from census data — not one run at a time)

The sweep of 8 modules needed exactly one emitter change (`and4_bridge`). Expect
these next, most already half-done:

- **Op bridges that exist but are unwired:** `sgt_bridge`, `andn`, `xorn`, `rorn`,
  `muxn` (general arity), `sumn`. Cheap — a dispatch arm each.
- **No lemma yet:** `Op_Mult` (`multiplier`), `Op_Div`/`SDiv` (`serdiv`), `Op_SetMask`,
  unequal-width `SLT`. Also **fix the `Div` signedness bug** (`pass_lean.cpp:1139`
  maps `Ntype_op::Div` to `Op_UDiv` unconditionally).
- **Higher `And`/`Or`/`Xor` arities.** Observed 2/3/4 so far. `bv_bit_and_step` is
  factored out so each new arity is one `have`; note `bv_bitwise_eq` already peels the
  outermost level, so arity N needs N−2 `have`s.
- **Method:** emit + `op_census.py` for *every* candidate first (seconds each, no Lean
  run), collect the complete gap list, then make **one** emitter change.

## Memory certificate (unblocks the remaining 43)

Step 1 landed additively (`CertVal` + `Op_MemRead`/`Op_MemWrite`/`Op_MemWriteBE`).
Remaining: generalize `GraphRefine`, decompose a memory node in the emitter (array as a
source; one `Op_MemRead` per read port; the write fold as a *chain* of `Op_MemWrite`
nodes), and the `memenc` bridge lemmas. Detail in the session plan.

Note memory is **sole** blocker for all 43 except six: three `macros/behav/
hpdcache_sram_*` also carry `export "DPI-C"`, two are harnesses (`rtl/tb`, `rtl/lint`),
and `tc_sram_fpga_wrapper` name-clashes with the ASIC `tc_sram_wrapper`.

Validate on the small ones first: `hpdcache_fifo_reg_gate` (188) →
`hpdcache_regbank_wmask_1rw_gate` (168) → `..._wbyteenable_1rw_gate` (244, byte-enable)
→ `tc_sram_gate` (236).

## Out of scope, stated so the list reads as complete

- **cvfpu (141 modules, 33 kLOC).** `OpBridge` is integer `BitVec` only; FP needs a new
  bridge layer. A milestone of its own, not a wrapper away. Note it vendors a *second*
  copy of common_cells that name-clashes with `vendor/pulp-platform/common_cells`.
- **Whole-core `cva6`.** Multi-blocked (FP + accelerator + all three cache subsystems)
  and both front-ends fail on it independently (`sv2v` on `acc_dispatcher`; slang →
  `pass/cprop/cprop.cpp:459`). Reachable only after memory + FP + those front-end bugs.
- **Bucket E**: testbenches, blackboxes, tech cells, DPI, SV classes/queues.

## Filelist hygiene (will bite in batch)

Duplicate module names across the tree: `hpdcache_sram_1rw` (×4: behav/blackbox/
fakeram45/`common/local/util`), `hpdcache_sram_wbyteenable_1rw` (×4),
`hpdcache_sram_wmask_1rw` (×3), `tc_sram_wrapper` (×2), `hpdcache_wrapper` (×2),
`find_first_one` (×2). Only one of each may be in a compilation unit.

Also: `//pragma translate_off` verification mirrors in `cva6_icache.sv:537-596`,
`wt_dcache_mem.sv:373-438`, `wt_dcache_wbuffer.sv:642-695` declare unpacked arrays that
would **falsely trigger memory inference** if not stripped.

## Execution

**One Lean typecheck at a time** (`scripts/run_lean_queue.sh`, detached via
`systemd-run --user`; the runner refuses to start while any `lean` is alive). This box
is a shared NFS server, and an OOM-killed run is indistinguishable from a proof
failure in the log.

Per module: gate wrapper → emit → static gates (`op_census.py`, `const_parity.py`,
`sorry`=0, expected `_refines_fast` present) → queue → record wall/RSS in the summary
TSV and the README table.

**Definition of done is a full-file `exit 0`.** Gates green is a weaker, separate
milestone: `cva6_tlb_gate` passed every gate and still failed after 5 h on Bug 11.

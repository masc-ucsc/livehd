# Plan: prove fast model ≡ graph certificate for every CVA6 block

**Objective:** every CVA6 module through `pass.lean` with `_comb`/`_next`/`_step`
proven equal to its certificate model. Performance work is subordinate — it buys
throughput, it is not the goal.

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

## Phase 2 — the remaining ready blocks (118 net new)

Inventory: 201 modules in scope (+141 cvfpu excluded) — **128 READY** (wrapper only),
43 memory-blocked, 2 accelerator, 3 FP, 25 other (testbench/blackbox/tech-cell/DPI).

Batch by *reused binding*, since wrapper effort is the real per-module cost:

1. **Zero-param comb leaves (~20 modules, near-zero effort):** the 8 `prim_secded_*`
   ECC modules (no params at all), `hpdcache_amo`, `hpdcache_{decoder,mux,demux,
   prio_1hot_encoder,prio_bin_encoder,1hot_to_binary}`, `lzc`, `popcount`, `unread`,
   `amo_alu` (not in the filelist — add the file).
2. **Reuse the `fu_data_t` + `CVA6Cfg` binding** (already written twice) → `aes`,
   `alu_wrapper`, `branch_unit`, `mult`, `multiplier`, `serdiv`.
3. **Reuse the `pmp` binding** → `pmp_entry`, `pmp_data_if`, `cva6_ptw` (703).
4. **Reuse the frontend bindings** → `bht2lvl`, `bht`, `btb`, `instr_queue`, then
   whole-block `frontend` (593) at `FpgaEn=0`.
5. **Big pure-comb prizes:** `hpdcache_ctrl_pe` (1275 lines, *zero* type params, zero
   flops), `decoder` (1988), `commit_stage` (415).
6. **Big sequential prizes:** `csr_regfile` (3088), `issue_read_operands` (1117),
   `scoreboard` (356), `hpdcache_{uncached,rtab,wbuf,cmo}`, `wt_axi_adapter`,
   `miss_handler`.

**Config knobs move modules B→A for free** — no `pass.lean` change:
`UseSharedTlb=0` (kills both `sram` instances in `cva6_shared_tlb`, unblocking it plus
`cva6_mmu`, `load_store_unit`, `ex_stage` — ~3 kLOC), `FpgaEn=0` (keeps `bht`/`btb`/
`cva6_fifo_v3`/`instr_queue`/`frontend` on flop arrays), `EnableAccelerator=0`,
`FpPresent=0`, `eccEn=0`. Verify per module that the guard really elaborates away —
memory now hard-errors under `emit_fast_bridge`, so a mistake fails loudly.

**Expected emitter work, from the census not from guessing:** `Op_Mult`
(`multiplier`), `Op_Div`/`SDiv` (`serdiv`, plus the signedness bug at
`pass_lean.cpp:1139` mapping `Div`→`Op_UDiv` unconditionally), `Op_SetMask`,
unequal-width `SLT`, and higher `And`/`Or`/`Xor` arities. Six lemmas already exist
unwired (`sgt`, `andn`, `xorn`, `rorn`, `muxn`, `sumn`).

## Phase 3 — memory certificate (unblocks the remaining 43)

Step 1 landed additively (`CertVal` + `Op_MemRead`/`Op_MemWrite`/`Op_MemWriteBE`).
Remaining: generalise `GraphRefine`, decompose a memory node in the emitter (array as a
source; one `Op_MemRead` per read port; the write fold as a *chain* of `Op_MemWrite`
nodes), and the `memenc` bridge lemmas. Validate smallest-first:
`hpdcache_fifo_reg_gate` (188) → `hpdcache_regbank_wmask_1rw_gate` (168) →
`..._wbyteenable_1rw_gate` (244, byte-enable) → `tc_sram_gate` (236).

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

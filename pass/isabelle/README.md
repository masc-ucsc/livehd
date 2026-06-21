# Isabelle Translation Validation Flow

`pass.isabelle` exports LiveHD graph semantics to Isabelle/HOL. The working rule
for future development is:

```text
add semantic primitive
  -> generate Isabelle for small RTL/module tests
  -> run LiveHD RTL-to-LGraph-to-Verilog LEC on realistic modules
  -> fix LiveHD/cgen/pass.isabelle semantic bugs
  -> regenerate Isabelle model
  -> build certificate proofs
```

Do not start with certificate proof debugging when the regenerated RTL is not
yet equivalent to the frontend-lowered RTL. LEC validates the LiveHD frontend,
LGraph lowering, and Cgen Verilog backend independently from Isabelle. The
certificate then validates that the emitted Isabelle model follows the LGraph
certificate semantics.

## Adding New Semantics

When a new RTL construct appears, add it in the following order:

1. Add a total semantic primitive in `formal/semantic_primitives`.
   Every primitive must be total, width-explicit, and avoid `undefined`. Examples
   are `sem_sra`, `sem_get_mask`, `sem_set_mask`, `flop_next`, and the memory
   primitives for function-valued SRAM/cache state.
2. Extend `pass/isabelle/pass_isabelle.cpp` to emit the primitive from the
   corresponding LGraph operation. Keep generated word widths explicit. Reject
   zero-width, X/Z, unsupported memories, ambiguous signedness, and unsupported
   blackboxes in strict mode.
3. Extend the graph certificate operator vocabulary and checker rules for the
   same construct. The executable Isabelle model and the certificate must expose
   the same semantics.
4. Add or update semantic regression tests before trying a full CPU/cache block.
   Required corner cases include mux polarity, mask packing, non-contiguous
   `Get_mask`/`Set_mask`, arithmetic shift sign preservation, signed vs unsigned
   comparison/division, reset priority, enable behavior, and constants wider
   than 64 bits.

For memories and caches, do not scalarize large SRAMs into one flop per bit in
the final model. Use function-valued memory state and explicit read/write/SRAM
port primitives. Scalarized memory is acceptable only as a temporary diagnostic
mode.

Signed and unsigned comparisons must stay distinct through the whole flow.
`pass.isabelle` emits `Op_SLT`/`Op_SGT` for signed `LT`/`GT` graph outputs and
`Op_ULT`/`Op_UGT` for unsigned outputs. The local smoke artifact
`generated/isabelle_signed_compare_smoke` checks this path: the generated fast
model uses `sint` for signed comparisons, the certificate data records the
signed/unsigned op split, and `Signed-Compare-Smoke` typechecks the fast model.

## CVA6 Cache/Memory LEC Gate

The current CVA6 stress path uses the static sv2v/filelist plan documented in
`docs/cva6_isabelle_stress.md`. Module-level cache and memory validation is
driven by:

```bash
cd /mada/users/czeng14/projects/livehd-new
bazel build //lhd:lhd

OUT_ROOT=$PWD/generated/cva6_cache_memory_lec_runN \
YOSYS_MEMORY_MODE=collect \
scripts/run_cva6_cache_memory_lec.sh tc_sram_gate
```

Then repeat for the HPDcache regbanks, FIFO helpers, TLB/MMU modules, and only
then larger cache/core tops. Use a fresh project-local `OUT_ROOT` for each major
attempt; do not use `/tmp`.

The LEC comparison is:

```text
frontend-lowered reference RTL
    vs
LiveHD LGraph -> Cgen regenerated Verilog
```

This check is outside `pass.isabelle`, but it is required before trusting the
Isabelle export. If LEC fails, inspect the generated reference Verilog,
regenerated Verilog, and `lec_work` logs first. Recent CVA6 cache/memory runs
exposed real issues this way, including sparse mux defaults reintroducing X
after `setundef=zero`, duplicate Cgen declarations for memory-like state, and
large shift constants being emitted in Isabelle as `1 word`.

## What "Depths 1-3 Pass" Means

The bounded BMC/LEC gate builds a sequential miter between the frontend-lowered
reference RTL and the regenerated Verilog from LiveHD's LGraph, then asks Yosys
SAT to find an output mismatch within a fixed number of cycles.

`depth 1 pass` means no mismatch exists after one clock step from the chosen
initial-state convention and constrained inputs. `depth 2 pass` and `depth 3
pass` mean the same property holds for two and three clock steps. The log line:

```text
SAT proof finished - no model found: SUCCESS!
```

means the SAT solver could not find a counterexample trace up to that bound.

This is not an unbounded equivalence proof. It is a finite, high-signal smoke
gate that catches many real RTL/LGraph/Cgen semantic bugs before Isabelle
certificate work starts. For modules with state, higher depths exercise state
update, flop aliasing, reset/enable behavior, and short temporal interactions.
For full correctness, bounded BMC should be followed by generated Isabelle
certificate proofs and, where needed, unbounded module refinement invariants.

## LEC target harness scope (what each target exercises)

Each cache/memory LEC target (except the full top) is a thin synthesizable
wrapper at `scripts/cva6_module_wrappers/<top>.sv` around a **real** CVA6/HPDcache
module — the wrapper only flattens packed-struct/array ports to flat bit-vectors
and fixes parameters; the instantiated `dut` is the genuine RTL. So how much of
each module's behavior the PASS actually covers depends on (a) which DUT inputs
the wrapper leaves **free** vs **ties to constants**, (b) the parameter sizing,
and (c) the LEC depth bound. All runs use
`sat -seq N -set-at N -set-init-zero -set-def-inputs -ignore_gold_x` (state starts
at 0; gold X ignored) and compare original-RTL-via-Yosys (gold) against the
LiveHD `tolg → cgen` Verilog (gate).

- **`tc_sram_gate`** → real `tc_sram` (`…/tech_cells_generic/src/rtl/tc_sram.sv`);
  params NumWords=16, DataWidth=32, ByteWidth=8, NumPorts=1, Latency=1,
  SimInit="zeros".
  - Free: all functional inputs — `req_i`, `we_i`, `addr_i[3:0]`,
    `wdata_i[31:0]`, `be_i[3:0]`. No tie-offs.
  - Exercised: single-port read/write, per-byte write enables, 1-cycle
    registered read. Not exercised: NumPorts>1, other widths/depths, Latency-0.

- **`hpdcache_fifo_reg_gate`** → real `hpdcache_fifo_reg`
  (`…/hpdcache/rtl/src/common/hpdcache_fifo_reg.sv`); params FIFO_DEPTH=4,
  FEEDTHROUGH=1, fifo_data_t=logic[15:0].
  - Free: all — push (`w_i`,`wdata_i[15:0]`), pop (`r_i`). No tie-offs.
  - Exercised: full push/pop, full/empty (`wok_o`/`rok_o`), feedthrough path,
    depth 4. Not exercised: non-feedthrough mode (FEEDTHROUGH=0), other
    depths/widths.

- **`hpdcache_regbank_wbyteenable_1rw_gate`** → real
  `hpdcache_regbank_wbyteenable_1rw`; params ADDR_SIZE=4, DATA_SIZE=64, DEPTH=16.
  - Free: all — `cs`, `we`, `addr[3:0]`, `wdata[63:0]`, `wbyteenable[7:0]`. No
    tie-offs.
  - Exercised: 16×64 register bank, single rw port, per-byte (8×8b) write
    enable, read. Not exercised: other depths/widths.

- **`hpdcache_regbank_wmask_1rw_gate`** → real `hpdcache_regbank_wmask_1rw`;
  params ADDR_SIZE=4, DATA_SIZE=64, DEPTH=16.
  - Free: all — `cs`, `we`, `addr[3:0]`, `wdata[63:0]`, `wmask[63:0]`. No
    tie-offs.
  - Exercised: 16×64 register bank, single rw port, bit-level write mask, read.
    Not exercised: other depths/widths.

- **`cva6_tlb_gate`** → real `cva6_tlb` (`…/core/cva6_mmu/cva6_tlb.sv`); real
  `CVA6Cfg`/`pte_cva6_t`/`tlb_update_cva6_t`, params TLB_ENTRIES=4, HYP_EXT=0.
  - Free: lookup + flush — `lu_access_i`, `lu_asid_i`, `lu_vmid_i`,
    `lu_vaddr_i`, `flush_i`/`flush_vvma_i`/`flush_gvma_i`, all
    `*_to_be_flushed_i`, `s_st_enbl_i`/`g_st_enbl_i`/`v_i`, and `update_valid_i`.
  - **Tied to constants (restricts coverage):** the TLB-fill content —
    `update_i.{vpn,asid,vmid,content,g_content,is_page,is_napot_64k}=0`,
    `v_st_enbl=1`. Only the fill *valid* bit varies.
  - Exercised: real TLB lookup/hit, entry CAM match, flush
    (VMA/GVMA/ASID/VADDR), entry management — over free lookup/flush inputs.
    Not exercised: fill with varied/non-zero PTE content (pinned to 0),
    hypervisor 2-stage translation (HYP_EXT=0), >4 entries.

- **`hpdcache_lint`** (full cache top) → the real `hpdcache_lint.sv` (smallest
  real HPDcache config), **not** a wrapper — the genuine cache top with its full
  interface (`core_req*`, `mem_req/resp*`, `cfg*`, wbuf/flush, etc.), all ports
  free.
  - Exercised: the entire cache — data/directory SRAMs (incl. the 1R/18W memory),
    regbanks, FIFOs, write buffer, miss handler, arbiters, uncacheable handler.
  - Not exercised beyond the bound: at depth 1 only one cycle of state
    transition is checked; deeper temporal behavior is unproven because the
    monolithic `sat` CNF OOMs past depth 1 here (377M variables at depth 1).

## Post-rebase re-validation (after the cgen reset/`initial` fix)

The feature was rebased onto latest master (commits `55f6265b5`,
`4bd6059bb`). On the rebased code, cgen emitted a flop reset/`initial` value as
an **undriven** net for *computed* (non-constant) reset values — exposed by
`tc_sram_gate` failing LEC (`rdata_o[0] = X`). Fix (`inou/cgen/cgen_verilog.cpp`,
flop reset emission): reference the reset/`initial` driver with `get_expression`
(inlining-aware) instead of `get_wire_or_const` (which ignores the `pin2expr`
inline-expression map and returned a bare, undriven wire name). Post-fix the
reset emits as a driven inline expression, e.g.
`\dut.rdata_q <= (get_mask_1308_u & 33'shffffffff);`, and `yosys check` reports 0
problems.

All previously-tested modules were re-generated by the fixed
`bazel-bin/lhd/lhd` and re-LEC'd (gold = original CVA6 RTL via Yosys-slang;
gate = LiveHD `tolg → cgen`; `sat -seq 1 -set-init-zero -set-def-inputs
-ignore_gold_x`; fastest first). Gates/gold and logs are under
`generated/rebase_regen_check/<top>/` (gate `regenerated_from_lgraph_postfix.v`,
gold `cwd/pp.v` or `reference_pp-mem.il`, LEC dir `lec_depth1_postfix/`).

| Module | depth-1 verdict | Wall | Peak RSS | SAT instance |
|---|---|---|---|---|
| `hpdcache_fifo_reg_gate` | PASS | 0.24 s | 42 MB | 28,667 vars / 76,702 cls |
| `cva6_tlb_gate` | PASS | 2.48 s | 130 MB | 3,282 vars / 8,104 cls |
| `hpdcache_regbank_wmask_1rw_gate` | PASS | 5.51 s | 582 MB | 720,742 / 1,909,246 |
| `tc_sram_gate` | PASS *(was FAIL pre-fix)* | 9.52 s | 1.07 GB | 1,413,371 / 3,748,385 |
| `hpdcache_regbank_wbyteenable_1rw_gate` | PASS | 34.22 s | 3.28 GB | 4,375,522 / 11,620,232 |
| `hpdcache_lint` (full top) | **PASS** *(after dangling-operand fix)* | 15 h 39 m | 214 GB | 353,020,944 / 924,263,083 |

Latest rebased-branch smoke result (after replaying the three local commits on
top of `origin/master` as `6a6d9b81d`, `3242ce8b4`, `3b26da411`): the five
module-level CVA6 memory/MMU gates were regenerated through the current LiveHD
RTL -> LGraph -> cgen path and then checked with the established depth-1
SMTBMC LEC flow (`miter -equiv -make_assert`, `async2sync`, `dffunmap`,
`zinit -all`, `chformal -assert -skip 1`, `write_smt2`, `yosys-smtbmc`).
Each run has `exit_code=0` and `smtbmc.log` ending in `Status: PASSED`.

| Module | latest depth-1 SMTBMC LEC | Log directory |
|---|---|---|
| `tc_sram_gate` | PASS | `generated/rebase_small_lec_20260621_postrebase2/tc_sram_gate/lec_smtbmc_depth1/` |
| `hpdcache_regbank_wbyteenable_1rw_gate` | PASS | `generated/rebase_small_lec_20260621_postrebase2/hpdcache_regbank_wbyteenable_1rw_gate/lec_smtbmc_depth1/` |
| `hpdcache_regbank_wmask_1rw_gate` | PASS | `generated/rebase_small_lec_20260621_postrebase2/hpdcache_regbank_wmask_1rw_gate/lec_smtbmc_depth1/` |
| `hpdcache_fifo_reg_gate` | PASS | `generated/rebase_small_lec_20260621_postrebase2/hpdcache_fifo_reg_gate/lec_smtbmc_depth1/` |
| `cva6_tlb_gate` | PASS | `generated/rebase_small_lec_20260621_postrebase2/cva6_tlb_gate/lec_smtbmc_depth1/` |

Do not use `lhd lec --set lec.solver=lgyosys` as the authoritative memory/SRAM
LEC result for these wrappers yet: the current `lgcheck` path can leave
`$aldff`/`$aldffe`/`$adff`/`$adffe` cells unsupported by Yosys `sat` and then
continue with `-ignore_unknown_cells`, producing suspect X-driven
counterexamples. The SMTBMC flow above lowers those cells before proof and is
the current trusted module-level check.

Coverage note: this post-rebase sweep is **depth 1** (uniform, to validate the
fix); the deeper pre-rebase bounds (regbanks 1-3, `tc_sram` 1-5, `fifo`/`tlb`
depth 100) recorded below were on the *pre-rebase* gate and should be re-run on
the fixed gate as a follow-up. The "Full HPDcache top" depth-1 PASS recorded in
the next section (15 h, ~211 GB, 377M vars) was the **pre-rebase** gate.

**Post-rebase full-cache regression (RESOLVED):** the first `hpdcache_lint`
re-run FAILED depth-1 LEC — the single mismatching output was `core_rsp_o`,
driven with undriven **X** bits (`…000x00000x00`) where gold was all-0.

Root cause (the general defect, of which the `tc_sram` `initial`-pin case was
one instance): cgen intentionally leaves single-use combinational producer nodes
undeclared, relying on `create_combinational`'s `graph->forward_class()` walk to
visit each producer *before* its consumer so `process_simple_node` caches the
producer's expression in `pin2expr`. The post-rebase graph contains synthetic
`Sra`/bit-extract operand nodes (created during Yosys import) for which that
visit-order invariant does not hold; when a consumer is asked for such a producer
before it's been visited, `get_expression` falls through to a **bare wire name**
that is never declared or assigned → undriven X. This produced **1925 dangling
temp nets** in the cache gate (vs 84 pre-rebase); ~1923 were in dead cones that
`opt` prunes, but ≥1 reached `core_rsp_o`'s live cone → the failure. The
emission *code* barely changed across the rebase (only `is_type_flop` →
`is_type_register` in the skip gates); the dangling explosion came from the
upstream graph shape feeding `forward_class()`.

Fix: make `get_expression` recursively **inline any safe single-use combinational
producer** that hasn't been visited yet (generalized beyond the initial `Sra`-only
patch to all such operand ops), removing the dependency on `forward_class()`
visit order. Validation: a project-local pre-flight
(`generated/rebase_regen_check/check_dangling.sh`) counts genuine dangling
arithmetic temps — it dropped from **269 → 0** on the fixed gate (the 76 residual
`wr_*`/`rd_*` flags are `cgen_memory` instance ports, not bugs) — and the depth-1
full-cache LEC then **PASSED** (`SAT proof finished - no model found: SUCCESS!`,
15 h 39 m, 214 GB peak, 353,020,944 vars / 924,263,083 clauses). Logs:
`generated/rebase_regen_check/hpdcache_lint/lec_depth1_expr_inline2_fix/`
(gate `regenerated_from_lgraph_expr_inline2_fix.v`).

## Full HPDcache top (`hpdcache_lint`) — depth-1 LEC PASS

This is the headline translation-validation result: the **real CVA6 HPDcache
top `hpdcache_lint`** (the full cache, not a helper sub-block) regenerated by
LiveHD is proven bounded-equivalent to the original RTL. It is direct evidence
that the LiveHD compile pipeline
(`inou.yosys.tolg` → LGraph → `pass.cprop` → `inou.cgen.verilog`) preserves the
semantics of a full cache design, including the multi-port memory arrays.

It is a genuine reference-vs-implementation check (not a tautology):

- **Gold (reference)** — original CVA6 HPDcache RTL elaborated by the
  Yosys-slang frontend, staged as RTLIL at
  `generated/cva6_real_cache_lec_hpdcache_lint_depth10/reference_pp-mem.il`.
  Its `\src` attributes point at the original
  `.../cva6/core/cache_subsystem/hpdcache/rtl/.../*.sv`; the source filelist is
  `.../cva6_real_cache_lec_hpdcache_lint_depth10/generated_inputs/hpdcache_lint.f`.
  It does **not** round-trip through the LGraph.
- **Gate (implementation)** — Verilog regenerated by LiveHD from the LGraph,
  `generated/cva6_real_cache_lec_hpdcache_lint_depth10/regenerated_from_lgraph_after_include_fix.v`
  (pulls in the `cgen_memory_multiclock_1rd_18wr.v` 1R/18W memory primitive).

**Log:**
`generated/cva6_real_cache_lec_hpdcache_lint_depth10/lec_depth1_relaunch_detached/bmc_depth1.log`
(the `/usr/bin/time -v` resource summary is in the sibling `bmc_depth1.err`;
`run.started`, `run.finished`, and `exit_code` = `0` are all present).

**Command** (run from
`generated/cva6_real_cache_lec_hpdcache_lint_depth10/`; the reference is
RTLIL-staged so Yosys does not re-elaborate the giant reference Verilog):

```bash
/usr/bin/time -v /usr/local/bin/yosys -p "read_rtlil reference_pp-mem.il; hierarchy -top hpdcache_lint; rename -top gold; prep -top gold; design -stash gold; read_verilog -sv -icells -I<repo>/ware/rtl regenerated_from_lgraph_after_include_fix.v; hierarchy -top hpdcache_lint; proc; bmuxmap; memory; opt; flatten; rename -top gate; prep -top gate; design -stash gate; design -copy-from gold -as gold gold; design -copy-from gate -as gate gate; miter -equiv -flatten -make_outputs -ignore_gold_x gold gate miter; async2sync; hierarchy -top miter; sat -ignore_unknown_cells -seq 1 -set-at 1 trigger 1 -prove trigger 0 -set-init-zero -set-def-inputs -show-ports miter"
```

| Field | Value |
|---|---|
| Start | 2026-06-18 15:48:49 |
| Finish | 2026-06-19 06:49:54 |
| Wall-clock time | 15:01:04 (≈ 15.0 hours) |
| Exit status | 0 (clean) — `run.finished` + `exit_code 0` both written |
| Peak RSS | 220,825,612 kB ≈ 211 GB |
| SAT instance | 376,815,578 variables / 986,730,660 clauses |
| Result | `SAT proof finished - no model found: SUCCESS!` (0 counterexamples) |

This is a **depth-1** bounded proof (one cycle of state transition), not an
unbounded equivalence proof. Higher bounds are intractable with the monolithic
`yosys sat` engine on this top: even depth 1 built a 377M-variable CNF peaking
at ~211 GB, and depth 10 was OOM-killed during CNF construction (no verdict).
Deeper bounds should use the incremental `yosys-smtbmc`/Z3 flow over a
once-built, cached miter (see "Current extended-run status" below and
`scripts/run_generated_verilog_smtbmc_lec.sh`), which adds one cycle at a time
instead of unrolling the whole design into a single CNF.

Current module-level status:

- `tc_sram_gate`: bounded BMC depths 1-3 pass. Success log:
  `generated/cva6_cache_memory_lec_run38/tc_sram_gate/lec_bmc_only/bmc.log`.
  A fresh current-code regeneration passed the standard depths 1-5 gate in
  `generated/cva6_cache_memory_lec_run43/tc_sram_gate/lec_work/lgcheck_bmc.log`.
  Its standalone extended run uses depth 50, not depth 100, at
  `generated/cva6_cache_memory_lec_run43/tc_sram_gate/lec_bmc_depth50_detached/`.
  The earlier depth-100 attempt was stopped without a SAT result and is retained
  only as an explicitly marked incomplete artifact.
  A follow-up Isabelle session build for this generated theory was interrupted
  before producing a log, so it is currently classified as unverified rather
  than failed.
- `hpdcache_regbank_wbyteenable_1rw_gate`:
  bounded BMC depths 1-3 pass. Success log:
  `generated/cva6_cache_memory_lec_run36/hpdcache_regbank_wbyteenable_1rw_gate/lec_bmc_only/bmc.log`.
  Current-code regeneration and LEC are under
  `generated/cva6_cache_memory_lec_run44/`; it passed early bounded subchecks
  but did not produce a final wrapper exit/result. Its queued depth-100 SAT
  run was not started because the supervisor stopped after the wmask
  depth-100 OOM.
- `hpdcache_regbank_wmask_1rw_gate`:
  bounded BMC depths 1-3 pass. Success log:
  `generated/cva6_cache_memory_lec_run37/hpdcache_regbank_wmask_1rw_gate/lec_bmc_only/bmc.log`.
  Its post-fix depth-100 run under
  `generated/cva6_cache_memory_lec_run42/hpdcache_regbank_wmask_1rw_gate/lec_bmc_depth100_detached/`
  did not produce an equivalence result. Yosys reached a SAT instance with
  `76777553` variables and `204820731` clauses, then Minisat threw
  `Minisat::OutOfMemoryException` after `39:55:26` wall time and about
  `72505356` KB max RSS. This is a solver-capacity failure, not a semantic
  counterexample and not a proven LiveHD/pass.isabelle bug.
- `hpdcache_fifo_reg_gate`: wrapper LEC passed after signedness and width-fix
  work. Success log:
  `generated/cva6_cache_memory_lec_run35/hpdcache_fifo_reg_gate/lec_work/lgcheck_bmc.log`.
  Current-code regeneration and LEC are under
  `generated/cva6_cache_memory_lec_run45/`; its standalone depth-100 SAT run
  was launched manually under
  `generated/cva6_cache_memory_lec_run45/hpdcache_fifo_reg_gate/lec_bmc_depth100_queued/`.
  Accept it only if that directory contains `run.finished`, `exit_code = 0`,
  and `bmc_depth100.log` contains
  `SAT proof finished - no model found: SUCCESS!`.
- `cva6_tlb_gate`: bounded BMC depths 1-3 and depth 100 pass. Depth-100
  success log:
  `generated/cva6_cache_memory_lec_run41/cva6_tlb_gate/lec_bmc_depth100/bmc_depth100.log`.

The TLB gate exposed an importer alias-order bug. Yosys produced aliases such
as `$106y = tags_q[2]`, but `process_assigns` ran before flop/cell driver pins
were initialized. The alias was therefore bound to a placeholder aggregate, and
the regenerated graph made `match_stage` constant zero. The fix is to initialize
cell driver pins before processing continuous assignments in
`inou/yosys/lgyosys_tolg.cpp`. After that change, `match_stage` is driven by
logic derived from `tags_q`, and bounded BMC depths 1-3 pass for
`cva6_tlb_gate`.

If the wrapper script stalls in the LEC phase, use a project-local
`lec_bmc_only` directory and run the bounded Yosys SAT miter directly. Treat the
finite BMC log as the validation artifact; do not confuse a stuck wrapper with a
semantic failure unless the Yosys log reports a counterexample.

Long-running validation is independent of an interactive Codex process:

- `generated/cva6_cache_memory_lec_monitor_20260615/monitor_lec.sh` records a
  snapshot every 10 hours in `latest_status.txt` and appends history to
  `status.log`. Depth-job process matching is scoped to each job's artifact
  directory, so pending queued jobs are not confused with another active LEC for
  the same top module.
- `generated/cva6_cache_memory_lec_supervisor_20260615/run_remaining.sh` waits
  in 10-hour intervals for active jobs, then runs the remaining depth-100
  regbank/FIFO checks sequentially to avoid concurrent SAT memory spikes.
- Every detached job writes `run.started`, `run.finished`, and `exit_code`.
  Missing finish/exit markers mean incomplete, not pass or fail.
- A zero process exit is not sufficient SAT evidence. The monitor/supervisor
  accepts a bounded equivalence result only when the log contains
  `SAT proof finished - no model found: SUCCESS!` and contains no
  counterexample/`model found: FAIL` marker.

Current extended-run status:

- `hpdcache_lint` (full HPDcache top) depth 1: **PASS** — see the dedicated
  "Full HPDcache top" section above (15.0 h wall, 376,815,578-variable CNF,
  ~211 GB peak RSS, `SAT proof finished - no model found: SUCCESS!`, exit 0).
  Depth 10 was OOM-killed during CNF construction with no verdict, so the
  monolithic `sat` engine is effectively capped at depth 1 for this top;
  deeper bounds need the incremental `yosys-smtbmc`/Z3 flow.
- `cva6_tlb_gate` depth 100: passed in
  `generated/cva6_cache_memory_lec_run41/cva6_tlb_gate/lec_bmc_depth100/bmc_depth100.log`.
- `hpdcache_regbank_wmask_1rw_gate` depth 100: solver OOM, no equivalence
  result. The next experiment should use `yosys-smtbmc`/Z3 or another backend,
  starting with a low-depth smoke run generated from `miter -make_assert`.
  The first SMTBMC/Z3 smoke passed at depth 3 in
  `generated/cva6_cache_memory_lec_run42/hpdcache_regbank_wmask_1rw_gate/lec_smtbmc_depth3_probe/`.
  The working SMT flow is: `miter -equiv -make_assert`, `async2sync`,
  `dffunmap`, `zinit -all`, `chformal -assert -skip 1`, `write_smt2`, then
  `yosys-smtbmc -s z3`. The `zinit`/`chformal` steps are necessary to match
  the existing `sat -set-init-zero` bounded-check semantics.
  Use `scripts/run_generated_verilog_smtbmc_lec.sh` for repeatable future
  SMTBMC runs instead of hand-written generated scripts.
- `tc_sram_gate` depth 1: **PASS** — current-code regeneration proven
  equivalent in 9.8 s (`1386931` variables / `3675437` clauses, ~1 GB peak),
  `SAT proof finished - no model found: SUCCESS!`, exit 0. Log:
  `generated/cva6_cache_memory_lec_run43/tc_sram_gate/lec_bmc_depth1_detached/bmc_depth1.log`.
- `tc_sram_gate` depth 50: the original monolithic SAT run under
  `generated/cva6_cache_memory_lec_run43/tc_sram_gate/lec_bmc_depth50_detached/`
  reached a `70508635` variable / `187204547` clause instance and was then
  SIGKILLed (no `run.finished`/`exit_code`, 0-byte `time -v` summary) — collateral
  of the 2026-06-18 memory-pressure event that OOM-killed the depth-10 cache CNF
  build, not its own OOM (it held only ~18 GB). Re-launched at depth 50 under
  `generated/cva6_cache_memory_lec_run43/tc_sram_gate/lec_bmc_depth50_detached_rerun/`;
  accept only on `run.finished` + `exit_code 0` + the SUCCESS marker.
- `hpdcache_regbank_wbyteenable_1rw_gate` current-flow LEC:
  `generated/cva6_cache_memory_lec_run44/` had passed early bounded subchecks
  but has no final wrapper exit/result.
- `hpdcache_regbank_wbyteenable_1rw_gate` depth 100: queued but not started
  because the supervisor stopped after the wmask depth-100 OOM.
- `hpdcache_fifo_reg_gate` depth 100: launched under
  `generated/cva6_cache_memory_lec_run45/hpdcache_fifo_reg_gate/lec_bmc_depth100_queued/`.
  At launch it reached a `3167001` variable, `8560137` clause SAT instance.
  The run is not a pass until the success marker appears in `bmc_depth100.log`.

## Certificate Proof Gate

After LEC and generated model typechecking are clean, enable certificate output.
The intended correctness links are:

```text
evaluated graph certificate = mathematical LGraph certificate semantics
generated fast word model   = evaluated graph certificate
certificate checker sound   = real graph_cert_wf proof
```

The certificate proof must be built incrementally. Use chunked certificate
checking first, not one monolithic `by eval` proof:

```bash
RUN_ISABELLE=false \
CERT_WF_MODE=chunked \
CERT_WF_FALLBACK=fail \
CERT_CHUNK_SIZE=25 \
CERT_CHUNK_LIMIT=1 \
scripts/run_dino_lgraph_isabelle.sh
```

Increase the chunk limit only after the previous limit builds quickly. Const-only
chunks should use specialized symbolic lemmas. Simple mixed chunks should use
local shape/dependency-subset lemmas. Avoid proofs that evaluate global
`all_ids`, global `distinct`, or a whole graph certificate in one command.

For Isabelle heap builds, use a project-local isolated `HOME` and project-local
temporary directory. If Poly/ML reports `I/O error: Operation not permitted`
without a theory/proof/type error, treat it as a sandbox filesystem issue, not
as a failed proof.

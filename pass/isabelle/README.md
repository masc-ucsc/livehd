# Isabelle Translation Validation Flow

`pass.isabelle` exports LiveHD graph semantics to Isabelle/HOL. The development
flow for a new construct is:

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
`Op_ULT`/`Op_UGT` for unsigned outputs. In the generated fast model, signed
comparisons use `sint`, and the certificate data records the signed/unsigned op
split.

## CVA6 Cache/Memory LEC Gate

Module-level cache and memory validation is driven by
`scripts/run_cva6_cache_memory_lec.sh`. From the LiveHD repository root:

```bash
bazel build //lhd:lhd

OUT_ROOT=$PWD/generated/cva6_cache_memory_lec_runN \
YOSYS_MEMORY_MODE=collect \
scripts/run_cva6_cache_memory_lec.sh tc_sram_gate
```

The script accepts one or more of the wrapper tops `tc_sram_gate`,
`hpdcache_regbank_wbyteenable_1rw_gate`, `hpdcache_regbank_wmask_1rw_gate`,
`hpdcache_fifo_reg_gate`, and `cva6_tlb_gate`; with no arguments it runs all
five in that order and stops at the first failure. Validate the small wrapper
modules before larger cache/core tops. Use a fresh project-local `OUT_ROOT` for
each major attempt; do not use `/tmp`.

For each top, `$OUT_ROOT/<top>/` holds the Yosys-elaborated reference
(`reference_pp.v`), the Verilog regenerated from the LGraph
(`regenerated_from_lgraph.v`), the `pass.isabelle` output (`isabelle/`), and the
LEC logs (`lec_work/`).

The LEC comparison is:

```text
frontend-lowered reference RTL
    vs
LiveHD LGraph -> Cgen regenerated Verilog
```

This check is outside `pass.isabelle`, but it is required before trusting the
Isabelle export. If LEC fails, inspect the generated reference Verilog,
regenerated Verilog, and `lec_work` logs first. Real issues found this way
include sparse mux defaults reintroducing X after `setundef=zero`, duplicate
Cgen declarations for memory-like state, and large shift constants being
emitted in Isabelle as `1 word`.

If the wrapper script stalls in the LEC phase, use a project-local
`lec_bmc_only` directory and run the bounded Yosys SAT miter directly. Treat the
finite BMC log as the validation artifact; do not confuse a stuck wrapper with a
semantic failure unless the Yosys log reports a counterexample.

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
    monolithic `sat` CNF runs out of memory past depth 1 here (over 350M
    variables at depth 1).

## Covered designs: depth-1 LEC results

Gold is the original CVA6/HPDcache RTL elaborated by Yosys-slang; gate is the
LiveHD `tolg → cgen` Verilog; the check is
`sat -seq 1 -set-init-zero -set-def-inputs -ignore_gold_x`.

| Module | depth-1 verdict | Wall | Peak RSS | SAT instance (vars / clauses) |
|---|---|---|---|---|
| `hpdcache_fifo_reg_gate` | PASS | 0.24 s | 42 MB | 28,667 / 76,702 |
| `cva6_tlb_gate` | PASS | 2.48 s | 130 MB | 3,282 / 8,104 |
| `hpdcache_regbank_wmask_1rw_gate` | PASS | 5.51 s | 582 MB | 720,742 / 1,909,246 |
| `tc_sram_gate` | PASS | 9.52 s | 1.07 GB | 1,413,371 / 3,748,385 |
| `hpdcache_regbank_wbyteenable_1rw_gate` | PASS | 34.22 s | 3.28 GB | 4,375,522 / 11,620,232 |
| `hpdcache_lint` (full top) | **PASS** | 15 h 39 m | 214 GB | 353,020,944 / 924,263,083 |

The five wrapper modules (`tc_sram_gate`,
`hpdcache_regbank_wbyteenable_1rw_gate`, `hpdcache_regbank_wmask_1rw_gate`,
`hpdcache_fifo_reg_gate`, `cva6_tlb_gate`) also pass the depth-1 SMTBMC LEC
flow described below: each run has `exit_code=0` and `smtbmc.log` ending in
`Status: PASSED`.

## Full HPDcache top (`hpdcache_lint`) — depth-1 LEC PASS

The **real CVA6 HPDcache top `hpdcache_lint`** (the full cache, not a helper
sub-block) regenerated by LiveHD is proven bounded-equivalent to the original
RTL. This shows that the LiveHD compile pipeline
(`inou.yosys.tolg` → LGraph → `pass.cprop` → `inou.cgen.verilog`) preserves the
semantics of a full cache design, including the multi-port memory arrays.

It is a genuine reference-vs-implementation check (not a tautology):

- **Gold (reference)** — original CVA6 HPDcache RTL elaborated by the
  Yosys-slang frontend, staged as RTLIL (`reference_pp-mem.il`). Its `\src`
  attributes point at the original
  `.../cva6/core/cache_subsystem/hpdcache/rtl/.../*.sv` sources. It does **not**
  round-trip through the LGraph.
- **Gate (implementation)** — Verilog regenerated by LiveHD from the LGraph
  (`regenerated_from_lgraph.v`), which pulls in the
  `cgen_memory_multiclock_1rd_18wr.v` 1R/18W memory primitive from `ware/rtl`.

**Command** (run from the output directory; the reference is RTLIL-staged so
Yosys does not re-elaborate the giant reference Verilog):

```bash
yosys -p "read_rtlil reference_pp-mem.il; hierarchy -top hpdcache_lint; rename -top gold; prep -top gold; design -stash gold; read_verilog -sv -icells -I<repo>/ware/rtl regenerated_from_lgraph.v; hierarchy -top hpdcache_lint; proc; bmuxmap; memory; opt; flatten; rename -top gate; prep -top gate; design -stash gate; design -copy-from gold -as gold gold; design -copy-from gate -as gate gate; miter -equiv -flatten -make_outputs -ignore_gold_x gold gate miter; async2sync; hierarchy -top miter; sat -ignore_unknown_cells -seq 1 -set-at 1 trigger 1 -prove trigger 0 -set-init-zero -set-def-inputs -show-ports miter"
```

Result: `SAT proof finished - no model found: SUCCESS!` (0 counterexamples,
exit status 0); the instance size and resource use are in the table above.

This is a **depth-1** bounded proof (one cycle of state transition), not an
unbounded equivalence proof. Higher bounds are intractable with the monolithic
`yosys sat` engine on this top: depth 1 already builds a CNF with over 350M
variables, and a depth-10 run was OOM-killed during CNF construction without a
verdict. Deeper
bounds should use the incremental `yosys-smtbmc`/Z3 flow below, which adds one
cycle at a time instead of unrolling the whole design into a single CNF.

## Bounded SMTBMC LEC Flow

When monolithic `yosys sat -seq N` becomes too large, use
`scripts/run_generated_verilog_smtbmc_lec.sh`:

```bash
scripts/run_generated_verilog_smtbmc_lec.sh \
  --top TOP \
  --ref reference_pp.v \
  --impl regenerated_from_lgraph.v \
  --depth N \
  --out OUT_DIR \
  [--include DIR]
```

`YOSYS_BIN`, `YOSYS_SMTBMC_BIN`, and `YOSYS_SMTBMC_SOLVER` (default `z3`) select
the tools. The flow is: `miter -equiv -make_assert`, `async2sync`, `dffunmap`,
`zinit -all`, `chformal -assert -skip 1`, `write_smt2`, then
`yosys-smtbmc -s z3`. The `zinit`/`chformal` steps are necessary to match the
`sat -set-init-zero` bounded-check semantics.

Every run writes `run.started`, `run.finished`, and `exit_code` into its output
directory. Missing finish/exit markers mean incomplete, not pass or fail. A zero
process exit is not sufficient evidence on its own: accept an SMTBMC result only
when `smtbmc.log` ends in `Status: PASSED`, and accept a `sat` result only when
the log contains `SAT proof finished - no model found: SUCCESS!` and no
counterexample/`model found: FAIL` marker.

Do not use `lhd lec --set formal.solver=lgyosys` as the authoritative memory/SRAM
LEC result for these wrappers: the `lgcheck` path can leave
`$aldff`/`$aldffe`/`$adff`/`$adffe` cells unsupported by Yosys `sat` and then
continue with `-ignore_unknown_cells`, producing suspect X-driven
counterexamples. The SMTBMC flow lowers those cells before proof and is the
trusted module-level check.

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

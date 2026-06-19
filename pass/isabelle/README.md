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

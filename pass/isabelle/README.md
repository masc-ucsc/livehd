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

Current module-level status:

- `tc_sram_gate`: `generated/cva6_cache_memory_lec_run38`, bounded BMC depths
  1-3 pass.
- `hpdcache_regbank_wbyteenable_1rw_gate`:
  `generated/cva6_cache_memory_lec_run36`, bounded BMC depths 1-3 pass.
- `hpdcache_regbank_wmask_1rw_gate`:
  `generated/cva6_cache_memory_lec_run37`, bounded BMC depths 1-3 pass.
- `hpdcache_fifo_reg_gate`: `generated/cva6_cache_memory_lec_run35`, wrapper
  LEC passed after signedness and width-fix work.
- `cva6_tlb_gate`: `generated/cva6_cache_memory_lec_run41`, bounded BMC
  depths 1-3 pass.

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

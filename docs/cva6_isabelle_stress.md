# CVA6 pass.isabelle Stress Target

This document records the high-feature CVA6 configuration used to test whether
`pass.isabelle` is complete enough for a full core.

## Chosen Target

Default stress target:

```text
cv64a6_imafdc_sv39_hpdcache_wb
```

This target is intentionally difficult:

- 64-bit CVA6 core.
- IMAFDC instruction configuration, so the FPU wrapper and FPnew integration are reachable.
- Sv39/MMU and PMP-related logic.
- HPDcache write-back cache subsystem, which exercises SRAM/memory nodes heavily.
- Large packed structs and top-level NOC/CVXIF/RVFI-style interfaces.
- Large mux/decode/control logic and wide constants.

`cv64a6_imafdcv_sv39` enables RVV, but it moves the stress test toward vector
accelerator integration before the core memory/SRAM/blackbox semantics are
stable. For `pass.isabelle` completeness, HPDcache write-back is the more useful
first hard target.

## Active Run Plan: Static Filelist + sv2v

Build `lhd` first:

```bash
cd /mada/users/czeng14/projects/livehd-new
bazel build //lhd:lhd
```

The active CVA6 plan is:

```text
static flist-plus file
  -> sv2v monomorphization/lowering
  -> lhd compile verilog --reader yosys-verilog
  -> yosys2lg / pass.cprop / pass.bitwidth / pass.isabelle
```

Do not invoke Bender in the normal flow. Bender was already used once to
materialize target-accurate filelists under:

```text
generated/cva6_filelists/*.flistplus.f
```

Use the SV39 HPDcache write-back filelist:

```text
generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f
```

This file is a `flist-plus` snapshot: it contains `+incdir+...`,
`+define+...`, and source paths in dependency order. `sv2v` does not accept
`-f`, so translate the filelist into command-line arguments:

```bash
cd /mada/users/czeng14/projects/livehd-new

flist2sv2v() {
  while IFS= read -r line; do
    [ -z "$line" ] && continue
    case "$line" in
      //*)        : ;;
      +incdir+*)  printf -- '-I\n%s\n' "${line#+incdir+}" ;;
      +define+*)  printf -- '-D\n%s\n' "${line#+define+}" ;;
      *)          printf -- '%s\n' "$line" ;;
    esac
  done < "$1"
}
```

Lower with `sv2v`. Always pass `-D VERILATOR`; CVA6 contains tracer classes
guarded by `` `ifndef VERILATOR ``, and `sv2v` cannot parse SystemVerilog
classes.

```bash
cd /mada/users/czeng14/projects/livehd-new

OUT=$PWD/generated/cva6_sv2v_isabelle_sv39
mkdir -p "$OUT/runtime_tmp" "$OUT/logs"

FLIST=$PWD/generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f
mapfile -t SV2V_ARGS < <(flist2sv2v "$FLIST")

/mada/software/verilog/sv2v \
  --top=cva6 \
  -D VERILATOR \
  "${SV2V_ARGS[@]}" \
  -w "$OUT/cva6.lowered.v"
```

Then feed the lowered Verilog into the Dino-style Verilog reader path:

```bash
cd /mada/users/czeng14/projects/livehd-new

./bazel-bin/lhd/lhd compile verilog \
  "$OUT/cva6.lowered.v" \
  --reader yosys-verilog \
  --top cva6 \
  --workdir "$OUT/lhd_work" \
  --result-json "$OUT/logs/lhd_compile_result.json" \
  --emit-dir lg:"$OUT/lgdb" \
  --emit-dir isabelle:"$OUT/isabelle" \
  --set isabelle.strict=true \
  --set isabelle.normalize=true \
  --set yosys.setundef=zero \
  --set isabelle.cert_wf=skip
```

The old `yosys-slang` stress scripts are now fallback/debug paths only. They
remain useful for comparing frontend failures and for module-level experiments,
but they are not the preferred route for the CVA6 full-core translation plan.

All outputs are project-local:

```text
generated/cva6_sv2v_isabelle_sv39/
  cva6.lowered.v
  lgdb/
  isabelle/
  logs/lhd_compile.log
  logs/lhd_compile_result.json
  lhd_work/
  runtime_tmp/
```

No runtime directory is created under `/tmp`.

### Static Filelist Maintenance

Do not run Bender as part of the normal Isabelle generation flow. If CVA6
dependencies or target packages change, refresh the frozen filelist explicitly:

```bash
cd /mada/users/czeng14/projects/cva6-clean/cva6
bender script flist-plus \
  -t cv64a6_imafdc_sv39_hpdcache_wb \
  --top cva6 \
  --trim-incdirs auto \
  > /mada/users/czeng14/projects/livehd-new/generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f
```

After that, the regular flow consumes only the static filelist.

## Expected Failure Mode

The script runs `pass.isabelle` with:

```text
isabelle.strict=true
yosys.setundef=zero
isabelle.cert_wf=skip
```

Strict mode is deliberate. The stress run should fail at the first missing
semantic feature instead of emitting `undefined` and hiding an unsound
translation.

Known areas that this target is expected to expose:

- `Ntype_op::Memory` for SRAM/cache arrays.
- Large packed struct and interface flattening.
- Blackbox/submodule boundaries that are not yet represented as contracts.
- FPU/FPnew semantics, which should be modeled against an AFP IEEE-754/RISC-V FP
  contract rather than expanded into a huge opaque gate-level expression.
- X/Z/don't-care artifacts. The script uses `setundef=zero`; any remaining X/Z
  should be treated as a frontend/lowering issue or given explicit four-valued
  semantics before it is accepted.

## Pipeline vs Memory Modeling

Pipeline registers and SRAM/cache memories are both stateful RTL constructs, but
they should not be represented the same way in Isabelle.

For ordinary pipeline state, the current `pass.isabelle` model is appropriate:

```isabelle
comb :: input => state => output
next :: input => state => state
step :: input => state => state * output
```

The generated state record has one scalar word field per flop or pipeline
register field:

```isabelle
record pipe_state =
  pc_q          :: "64 word"
  if_id_pc_q    :: "64 word"
  if_id_instr_q :: "32 word"
  id_ex_valid_q :: "1 word"
```

The default synchronous timing convention is:

```text
output_t    = comb(input_t, state_t)
state_t+1   = next(input_t, state_t)
```

Equivalently:

```isabelle
definition step where
  "step i s =
     (let out = comb i s;
          s'  = next i s
      in (s', out))"
```

All next-state right-hand sides must read the old state `s`; record updates are
then simultaneous at the clock edge.

SRAMs and cache arrays should not be expanded into one flop field per bit. That
is semantically possible, but it creates unusably large Isabelle states and
proof terms. Instead, an SRAM should become a function-valued memory field:

```isabelle
type_synonym ('addr, 'data) mem = "'addr word => 'data word"

record cache_state =
  tag_array   :: "(8, 64) mem"
  data_array  :: "(10, 512) mem"
  valid_array :: "(8, 1) mem"
```

Reads and writes are then standard function application and function update:

```isabelle
definition mem_read where
  "mem_read m a = m a"

definition mem_write where
  "mem_write m a v = m(a := v)"
```

An SRAM primitive must encode the actual RTL policy, for example synchronous
single-port read/write behavior:

```isabelle
definition sram_1rw_step where
  "sram_1rw_step we waddr wdata raddr m =
     (let rdata = m raddr;
          m' = (if we then m(waddr := wdata) else m)
      in (m', rdata))"
```

The precise primitive must match the lowered RTL: read-before-write vs
write-before-read, synchronous vs asynchronous read, byte-enable/write-mask
behavior, reset/initialization behavior, and one-port vs multi-port behavior.

A cache is a state machine built from both styles. Cache control registers remain
ordinary scalar flop fields, while tag/data/valid/dirty arrays should be
function-valued memory fields:

```isabelle
record dcache_state =
  tag_mem   :: "(index, tag) mem"
  data_mem  :: "(line_index, line_data) mem"
  valid_mem :: "(index, valid_bits) mem"
  dirty_mem :: "(index, dirty_bits) mem"
  mshr_q    :: "mshr_state"
  miss_q    :: "1 word"
```

So the unified rule remains:

```text
step(input, old_state) = (new_state, current_output)
```

But the generated state representation differs:

```text
pipeline register -> scalar word field
SRAM/cache array   -> function-valued memory field
cache controller   -> scalar word/record fields
```

For `pass.isabelle`, this means `Ntype_op::Flop` can keep using scalar
`flop_next`, while `Ntype_op::Memory` needs explicit memory/SRAM primitives. It
must not be handled by expanding the array into bit-level flops.

## Completeness Bar

For this target to pass soundly, `pass.isabelle` needs explicit models for:

- Memory/SRAM state fields as function-map memories, not one flop per memory bit.
- Read/write-port semantics, byte-enable/write-mask behavior, read-before-write
  or write-before-read policy, and reset/initialization policy.
- Blackbox modules as explicit uninterpreted contracts or verified primitive
  blocks.
- Large interface and packed-struct ports as deterministic flattened word fields
  with a generated RTL-name map.
- Remaining operator corner cases: signed division, arithmetic shift,
  non-contiguous mask packing, mux polarity, constant truncation, and zero-width
  artifact rejection.

The stress script is therefore a completeness detector. A passing run with
`strict=true` should mean that all reachable graph constructs have an explicit
Isabelle semantics; it does not by itself prove CVA6 refines Sail.

## Current Frontend Status

The chosen frontend path is now static filelist plus `sv2v`, not runtime Bender
and not handwritten struct/tie-off wrappers.

Known status from `scripts/CVA6_SV2V_FILELIST_REFERENCE.md`:

- The static SV39 filelist exists and is the source of truth:
  `generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f`.
- Whole-core `sv2v --top=cva6` currently fails in CV-X-IF accelerator logic:
  `field 'acc_req' not found in struct ... acc_dispatcher_*.acc_req_t_CVA6Cfg[...]`.
  This target enables `CVA6ConfigCvxifEn`, so the accelerator cone is active.
- Subtree lowering works when `--top=<module>` prunes the accelerator. The
  confirmed example is `cva6_tlb_gate`: `sv2v` exits 0 and emits a 2-module,
  about 2.5 MB lowered Verilog file with no warnings.
- The lowered TLB then stalls in `yosys-verilog`/`read_verilog -sv`, because
  `sv2v` flattened CVA6 packed structs into extremely wide expressions; one
  observed lowered line is 489,823 characters.
- The old `yosys-slang` plus gate path reaches `pass.isabelle` quickly for the
  same TLB module, but full-core slang still dies later in LiveHD
  `pass/cprop/cprop.cpp:459` on an EQ-fold assertion.

These are frontend/lowering blockers, not Isabelle proof failures. The next
practical steps are:

1. Add a dedicated `sv2v` runner that consumes only the static filelist and
   emits project-local lowered Verilog plus LiveHD logs.
2. Use subtree tops that prune CV-X-IF until the whole-core `sv2v` struct-field
   bug is understood or worked around.
3. Tame wide flattened expressions before expecting `yosys-verilog` to scale:
   try narrower tops, preserve more hierarchy, measure `sv2v -E Always`, and
   only then consider a deterministic lowered-Verilog line-splitting pass.
4. Keep the old slang/gate flow as a comparison/debug route, not as the primary
   plan. It is still useful for fast module-level `pass.isabelle` experiments.
5. Once the lowered Verilog reaches LGraph generation, expect strict-mode
   `pass.isabelle` failures to expose real semantic gaps: `Ntype_op::Memory`,
   blackbox/submodule contracts, large flattened interfaces, FPU abstraction,
   and remaining operator corner cases.

## Memory/SRAM Primitive Status

Implemented in `formal/semantic_primitives`:

- `type_synonym ('a, 'd) mem = "'a word => 'd word"`
- `mem_read`
- `mem_write`
- `byte_enable_mask`
- `masked_word_update`
- `mem_write_be`
- `sram_1rw_read_first`
- `sram_1rw_write_first`
- `sram_1rw_be_read_first`
- `sram_1rw_be_write_first`
- `sram_1r1w_read_first`
- `sram_1r1w_write_first`
- `sram_1r1w_be_read_first`
- `sram_1r1w_be_write_first`
- `sram_sync_read_reg_next`

The primitive regression session passes:

```bash
HOME=$LIVEHD_ROOT/generated/semantic_primitives_build/isabelle_home_isolated \
TMPDIR=$LIVEHD_ROOT/generated/semantic_primitives_build/runtime_tmp \
TMP=$LIVEHD_ROOT/generated/semantic_primitives_build/runtime_tmp \
TEMP=$LIVEHD_ROOT/generated/semantic_primitives_build/runtime_tmp \
/soe/czeng14/.local/Isabelle2025-2/bin/isabelle build -v \
  -o document=false -o browser_info=false \
  -d $LIVEHD_ROOT/formal/semantic_primitives \
  DINO_Semantic_Primitives_Test
```

The first run must be outside the sandbox if Poly/ML heap writes fail with
`I/O error: Operation not permitted`; use the project-local `HOME` and
`runtime_tmp` shown above.

`pass.isabelle` now recognizes `Ntype_op::Memory` and reports the documented
LiveHD memory port policy in the strict-mode diagnostic:

```text
addr, bits, clock_pin, din, enable, fwd, posclk, type, wensize, size, rdport
```

Direct memory-node emission is still intentionally fail-fast. The Isabelle
primitive layer is ready, but the exporter still needs the next connection:
turn a surviving LGraph `Memory` node into a function-valued state field plus
`mem_read`/`mem_write`/SRAM helper calls, and add memory read/write certificate
operators.

## HPDcache Module-Level Results

The following legacy slang/gate module-level runs generated Isabelle without
`undefined` or TODO markers and their executable `_Lgraph.thy` model sessions
typechecked:

| Gate | Generated artifact | Isabelle model session |
| --- | --- | --- |
| `tc_sram_gate` | `generated/cva6_module_isabelle_tc_sram_gate_memprim/isabelle` | `CVA6-Tc-Sram-Gate-Memprim-Model` |
| `hpdcache_regbank_wbyteenable_1rw_gate` | `generated/cva6_module_isabelle_hpdcache_regbank_wbyteenable_1rw_gate_memprim/isabelle` | `CVA6-HPDcache-Regbank-Wbyteenable-1RW-Gate-Memprim-Model` |
| `hpdcache_fifo_reg_gate` | `generated/cva6_module_isabelle_hpdcache_fifo_reg_gate_memprim/isabelle` | `CVA6-HPDcache-Fifo-Reg-Gate-Memprim-Model` |
| `hpdcache_regbank_wmask_1rw_gate` | `generated/cva6_module_isabelle_hpdcache_regbank_wmask_1rw_gate_memprim/isabelle` | `CVA6-HPDcache-Regbank-Wmask-1RW-Gate-Memprim-Model` |

These results remain useful as a baseline for the semantic primitive layer, but
they are not the preferred frontend plan for new CVA6 coverage. New runs should
first try:

```text
static flist-plus -> sv2v --top=<bound subtree> -> yosys-verilog -> pass.isabelle
```

If a module has unbound type/config parameters, use a minimal binding top whose
only purpose is to instantiate the module with concrete CVA6 config parameters
and then let `sv2v` erase structs/types/generate constructs. Do not hand-write
struct typedefs, tie-off logic, or behavioral wrappers.

The legacy gates currently scalarize small RTL arrays into individual flop fields,
for example:

```isabelle
record hpdcache_regbank_wbyteenable_1rw_gate_state =
  st_dut_chr2e_mem :: "64 word"
  st_dut_chr2e_mem_1 :: "64 word"
  ...
  st_dut_chr2e_mem_15 :: "64 word"
```

That is acceptable for small module gates, but it is not the intended full-cache
representation. Full HPDcache requires preserving or reconstructing arrays as
function-valued memories, not scalar flop explosion.

The `hpdcache_lint` top was also tried through the legacy slang/gate route as a
concrete HPDcache instantiation:

```bash
CVA6_TOP=hpdcache_lint \
CVA6_BENDER_TOP=hpdcache \
CVA6_WRAPPER_FILE=$CVA6_ROOT/core/cache_subsystem/hpdcache/rtl/lint/hpdcache_lint.sv \
OUT=$LIVEHD_ROOT/generated/cva6_module_isabelle_hpdcache_lint_memprim \
scripts/run_cva6_module_isabelle_stress.sh
```

It got past the earlier generic-parameter failure and emitted only frontend
warnings before timing out at 300 seconds. The LiveHD stage log remained empty,
so this is still a frontend/lowering scalability bottleneck; it did not yet
reach `pass.isabelle`, and therefore did not fail on memory arrays inside the
Isabelle exporter.

For the new static-flist/`sv2v` route, the immediate HPDcache/MMU module order
is:

1. `tc_sram` / small SRAM bound tops through `sv2v`, where any auxiliary top is
   strictly an instantiation top.
2. HPDcache regbank helpers and FIFO helpers.
3. HPDcache top or `hpdcache_lint`, after wide-line behavior is measured.
4. MMU TLB/PTW/shared-TLB under the SV39 config.
5. Full `cva6` only after CV-X-IF struct lowering and wide-expression scaling
   are resolved, with FPnew abstracted/blackboxed.

## Remaining Work For Full HPDcache

To make full HPDcache translate as a cache model rather than an enormous flop
netlist:

1. Add a static-filelist `sv2v` runner and make it the default CVA6 frontend:
   no runtime Bender, no handwritten struct/tie-off wrappers, project-local
   outputs only.
2. Preserve SRAM/regbank arrays through `sv2v`/Yosys/LiveHD as `Ntype_op::Memory`, or
   add a proven array-reconstruction pass that folds scalarized memories back
   into function-valued state.
3. Extend `pass.isabelle` Memory-node emission:
   - decode port stride 11;
   - classify read/write ports from `rdport`;
   - decode `bits`, `size`, `wensize`, `type`, `fwd`, and clock policy;
   - emit one memory state selector per memory node;
   - emit `mem_read`, `mem_write`, `mem_write_be`, and the appropriate SRAM
     helper for the port policy.
4. Extend certificate data and evaluator operators with memory read/write node
   kinds and prove the corresponding operator-correctness lemmas.
5. Re-run `sv2v`-lowered module/subtree gates, then `hpdcache_lint`, then the
   full `cv64a6_imafdc_sv39_hpdcache_wb` core with FPnew blackboxed.

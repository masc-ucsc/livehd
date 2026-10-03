# txfmafrac_top accepted under a TARGETED plain-proc lowering

A module the default route cannot emit at all reaches direct-simulator
acceptance under an ALTERNATE lowering. This file is the durable record; the
logs it names are under `generated/` and are deliberately NOT staged.

**This is not a coverage change.** The generated ledger's route count stays
**109 of 122**. See "How to count this" below.

## Configuration -- this is the whole point, so it is stated first

| | |
|---|---|
| lowering | plain `proc` (candidate), NOT the shipped `proc -ifx` |
| how | `yosys.script` = `inou_yosys_read.ys` with `proc -ifx` -> `proc` |
| global default | UNCHANGED. `proc -ifx` remains the default for every other module |
| why | under `-ifx` this module has 39 combinational SCCs and pass.lean refuses; under plain `proc` it has 0 (`pass/lean/CYCLE_PROVENANCE.txt` part 6) |

## Identities

| artifact | sha256 (16) |
|---|---|
| commit | `fc1d6a70135d1db808ce9dc6983d063318081dfd` |
| `lhd` (k8-dbg) | `ee2e7824e8e0aa6b` |
| `pass/single_edge/pass_single_edge.cpp` | `ada3569d3d95ce99` |
| `inou/cgen/cgen_verilog.cpp` | `ee13dae1cc0b6495` |
| `ware/rtl/cgen_memory_1rd_1wr.v` | `e0c57724fbf66c60` |
| candidate `.ys` | `58103f928fb3be7f` |
| input graph `lgdb_raw` (library+srcmap) | `8e05a6767e32f95e` |
| CORE-ET filelist | `e665f932f81dd2c6` |
| stimulus `vectors.hex` | `497807334ddc115b` |
| raw netlist | `9bd26b6fdd4adc23` |
| normalized netlist | `af8e7cb01da42a64` |
| certificate | `e87dc22c7c8e0d3e` |

core-et revision `c72b6a2`.

## Recipes

* raw and normalized emission: `--recipe O0`, which resolves to exactly
  `inou.cgen.verilog` -- no cprop, no `pass.formal`, no normalization.
* normalization: `lhd pass single_edge` (P=1, 13 latches retyped, 140 gates
  folded into enables, 1 clock domain).
* certificate: `formal.lean.mode=verified_compiler`, `strict=true`.

## Gates

| gate | result |
|---|---|
| raw vs ORIGINAL RTL, 3142 vectors (seed 1) | **PASS** 0 mismatches |
| normalized vs ORIGINAL RTL, 3142 vectors | **PASS** 0 mismatches |
| certificate emit | PASS, 3.7 MB |
| Lean typecheck / compiles theorem | PASS, 1032.7 s, 10.06 GiB |
| checkDesign | PASS, 1709 ms |
| 4x runDirect | PASS, 4 cycles, 4185 ms |

`direct_sweep` row: ACCEPTED, sources 24100, nodes 28850, outputs 78,
flops 153, mems 0, wall 1086.0 s, rss 4.58 GiB.
Axioms: `propext, Classical.choice, Quot.sound,
txfmafrac_top_compiles._native.native_decide.ax_1_1`.

Both differentials were rerun after the support-model change, so the raw
control is not stale. Every run under `MemoryMax=20G`, `MemorySwapMax=0`;
zero OOM kills.

## Two defects this exposed, with their ACTUAL locations

They are in different components and should not be described as one.

1. **`pass/single_edge`** -- the ICG enable-latch bypass tested only the
   enable pin's immediate master node, so an enable reaching the gate through
   a `Get_mask` zext wrapper kept the latch's Q, which is a cycle stale once
   the latch is retyped. Commit `535a7abd2`. This one IS a normalization
   defect. 3141 -> 473 mismatches.
2. **the cgen memory support model and its generated template** -- a
   registered read updated unconditionally from a read that is x while
   disabled, so a disabled cycle loaded 0 instead of holding. Commit
   `fc1d6a701`. This is NOT a normalization defect: it is the Verilog support
   model disagreeing with the contract the import, pass.lean and
   pass.single_edge already share. **pass.lean's read semantics were already
   correct** (`sram_sync_read_reg_next ren raw cur = if ren then raw else cur`)
   and are not called into question by it. 473 -> 0 mismatches.

## What these gates do NOT establish

* A 3142-vector differential is FINITE-VECTOR evidence on DEFINED inputs. It
  is not proof of RTL equivalence, and the domain deliberately excludes the
  X-semantics question that `-ifx` versus plain `proc` is actually about.
* The differentials validate the EMITTED VERILOG; the Lean gates validate that
  the CERTIFICATE compiles, checks and runs. Two artifacts from one graph --
  **certificate acceptance is not RTL agreement**, and nothing here closes
  that gap.
* `mems=0`: all 17 memories are ROMs carried as ROM sources, so the
  certificate's memory-WRITE path was not exercised.
* Defect 1 was a normalization defect, so for blocks already accepted on the
  default route a passing checkDesign/runDirect does not establish RTL
  agreement -- those gates run on the normalized graph. A block with a gated
  synchronous read was exposed to defect 2 as well.

## How to count this

The ledger's **109 of 122** is the count on the DEFAULT route and is unchanged.
txfmafrac_top is accepted only under the targeted plain-proc configuration
above.

Counting the union: **110 distinct CORE-ET blocks have direct-simulator
acceptance when this alternate lowering is included**, the 110th being
txfmafrac_top under a non-default lowering, gated on a finite-vector
differential rather than on the default route.

`pass/lean/COVERAGE_LEDGER.md` is GENERATED by `scripts/coverage_ledger.py`
and must not be hand-edited. Any later integration has to represent the route
distinction explicitly -- a row copied into an ordinary census directory would
read as a default-route acceptance, which this is not.

## Logs (generated/, ignored, not staged)

```
generated/plainproc_memfix/           GATE_TABLE.txt env.txt ds_raw.log ds_norm.log
                                      typecheck.log direct raw.v norm.v lean/ lgdb_norm/
generated/plainproc_txfmafrac/        the original run and the preserved lgdb_raw
generated/plainproc_fixed/            the 473-mismatch intermediate
generated/plainproc_txfmafrac_retry/  the 3141-mismatch starting point
generated/diag209, generated/diag320  the two diagnosis rounds
```

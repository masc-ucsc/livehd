# The aegraph switch costs three modules of export coverage

`29bedeabe enable aegraph instead of cprop` makes `pass.aegraph` the compile
pipeline's optimizer. Re-measuring the CORE-ET corpus across that change:

| engine | accepted | compile-failed | single_edge-failed |
|---|---|---|---|
| `classic` (cprop) | **112** | 5 | 5 |
| `aegraph` (new default) | **109** | 6 | 7 |

Three modules lose `accepted`: `intpipe_csr_file`, `minion_dcache_top` and
`intpipe_mul_div_top`. None gains.

## `concat-lane-width`: a malformed Concat, not a refusal

The new failure class is the exporter catching an invariant violation --
the diagnostic says `internal:`, and it is right to refuse:

> Concat lane 2 (of 13, MSB-first) driver `reg_mip_next[86:81]` occupies 6
> bits but its declared window is 3 -- a lane driver may only be NARROWER
> than its window (LGraph may narrow, never widen); truncating it would shift
> every lane above it

An aegraph rewrite widened a lane's driver without widening the lane's window.
Two shapes appear: a 6-bit driver in a 3-bit window (`intpipe_csr_file`, and
the same net inside `intpipe_top` and `minion_top`), and a 49-bit driver in a
48-bit window (`minion_dcache_top`, and the same inside `core_top`) -- the
second looks like one carry bit too many.

It affects five modules in the corpus. Two of them (`intpipe_csr_file`,
`minion_dcache_top`) were accepted before; the other three were already
failing for other reasons and now stop earlier, which MASKS those reasons --
`core_top` and `minion_top` in particular no longer reach the latch analysis.

### Reproducing, one flag wide

    lhd compile verilog <anchor.sv> --reader yosys-slang --top intpipe_csr_file \
      --set yosys.filelist_file=<coreet filelist> --set yosys.setundef=zero \
      --set cprop.engine=aegraph \
      -- --ignore-assertions --relax-enum-conversions --allow-use-before-declare

`cprop.engine=aegraph` fails with `concat-lane-width`; `cprop.engine=classic`
compiles. Same module, same sources, nothing else changed.

## `intpipe_mul_div_top`: a separate regression

Accepted under `classic`, and under `aegraph` it is refused by
`pass.single_edge`:

    coincident-commit-edge: latch `u_prim_mul_div.clk_gate_wb.en_latch` and
    state element ... commit at the SAME edge

That is the clock-gate analysis seeing a different netlist, not the Concat
bug. It has not been investigated further.

## Scope

This is a measurement of upstream, not a change to it. Nothing here is fixed:
the Concat invariant belongs to the aegraph rewrite rules, which are active
work. `pass/lean/Lgraph_IR_Semantics_Simulator/evidence/CENSUS_MASTER_122.tsv` now records the DEFAULT pipeline
(`lowering` column `ifx+aegraph`), so the tracked number is 109; 112 is one
`--set cprop.engine=classic` away.

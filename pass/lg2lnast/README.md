# pass.lg2lnast (prototype design)

Goal: lower an lgraph (after `pass.bitwidth`, so every pin carries its proven
range) back into **LNAST** that is *ordinary compiler input*: `upass` -> `tolg`
accepts it and the resulting lgraph is **LEC-equivalent** to the one we started
from. Once an LNAST function carries explicit bits/sign, the simulators (slop
and LLVM) can be generated from it directly, shared LNAST passes (balanced OR,
select merge, chain collapse; later the lnast-aegraph) run once for every
backend, and a very large LNAST function can be split by size so its kernels
compile in parallel.

## Contract

1. Output is one `Lnast` per graph, in the dialect the Pyrope front end already
   emits (`lhd compile x.prp --emit-dir lnast-dump:D` shows it). Nothing
   lg2lnast-specific rides in the tree: `upass`/`tolg` must not need to know it
   came from an lgraph.
2. Round trip is the acceptance test: `lg -> lg2lnast -> ln: -> compile -> lg'`,
   then `lhd lec lg:A lg:B` proves `lg == lg'` (suite designs: xs_alu, dino,
   picorv32 first).
3. Every statement result has an explicit `type_spec` taken from the pin's
   proven range, so the front end's overflow rule ("may not fit its declared
   range") can never fire on a value the lgraph already proved.

## Dialect mapping (observed shapes)

| lgraph | LNAST |
| --- | --- |
| graph inputs / outputs | `top/io`: two `tuple_add`, each element `store(ref name, const nil, prim_type_int(max,min)[, stages(const d, const d)])`; outputs carry `stages` |
| register (default clock/reset) | `declare(ref r, prim_type_int(max,min), const 'reg', init)`; reads use `r`, the next value is `store(r, tmp)` |
| `Sum` (even sink pid +, odd -) | `plus` / `minus` chain: `op(dest, a, b)` |
| `And Or Xor Not Mult Div Rem` | `bit_and bit_or bit_xor bit_not mult div mod` (`op(dest, a, b...)`) |
| `Ror Rxor Popcount` | `red_or red_xor popcount` |
| `Get_mask` / `Set_mask` | `get_mask(dest, src, lo, hi)` / `set_mask(...)` |
| `Concat` (lane table on the cell) | `concat` with `Lnast_builder::Concat_lane{value,bits}` |
| `LT GT EQ` | `lt gt eq` (`ne le ge` as the negation the front end spells) |
| `SHL SRA Sext` | `shl sra sext` |
| `Mux` | `if` / else statements writing one tmp (what the front end emits for `if s { a } else { b }`) |
| `Hotmux` | `unique_if` |
| temps | `%<name>_<n>` (`Lnast_builder::create_lnast_tmp`), never a user identifier |
| sources | `src=` attribute copied from the cell (`srcid`) so diagnostics keep pointing at the original text |

## Status (milestone 1, in progress)

Run it: `lhd pass lg2lnast --top T lg:IN --emit-dir ln:OUT` (or `lnast-dump:DIR`).
Round trip: `lhd compile ln:OUT --top T --emit-dir lg:IMPL`, then
`lhd lec --ref lg:IN --impl lg:IMPL --top T --set formal.effort=proof`.

Covered, and proven `proven-full` by `lg2lnast_roundtrip_test` (plus the
XiangShan `Alu` hierarchy and `matched_filter`): Sum, And/Or/Xor, Not (typed and
untyped), Mult/Div/Rem, Get_mask, Sext, Concat, SHL/SRA, Ror/Rxor/Popcount,
LT/GT/EQ (as `if c {1} else {0}`: comparisons are Bool in LNAST), 2-way Mux,
Hotmux (`unique_if`), flops, dotted tuple-leaf port names, and `Sub` instances
(`fcall` + `tuple_get`). Cells are emitted in dependency order (Kahn), not hhds
`forward` order, which is not topological around loop-last cells.

Flops keep their register names (what keeps LEC fast). Clock, negedge, reset
value, reset signal, sync/async and power-on initial are always spelled as the
front end's `attr_set clock_pin|posclk|initial|reset_pin|sync|negreset`, so the
result never depends on the default-clock naming rule. Enable is an `if`.

A graph outside the subset is skipped with a `graph-skipped` warning naming the
reason, never lowered partially. Known gaps: Memory, Latch, Clock_cell, n-ary
Mux/compare, LUT, compact loops, and a cycle through instance-level dependencies
(two submodules feeding each other through independent internal paths; the
`fcall` form needs every argument before any output, `dino` top and a few ibex
graphs hit it; a flattened graph has no such cycle).

## Milestones

1. Combinational cells + flops with the default clock/reset; round trip + LEC.
   Out of scope for M1: memories, latches, `Clock_cell`, non-default clocks,
   compact loops (`Subnode_loop`), hierarchy beyond `Sub` as `func_call`.
2. LNAST-level cleanups shared with upass (balanced OR, same-condition select
   merge, chain collapse).
3. Generate LLVM straight from LNAST + bits/sign for the supported subset,
   behind a flag; compare against the current path bit-exactly with periodic
   state hashes.
4. Split very large LNAST functions by size so several kernels compile in
   parallel.

## Why not a cgen-private IR

A cgen-private IR would fix the long chains but stay invisible to every other
pass. As LNAST it is checked by LEC, reusable by upass and the lnast-aegraph,
and keeps slop and LLVM lowering independent of each other (slop remains an
independent oracle for the LLVM backend: both read the same typed LNAST but
lower it separately).

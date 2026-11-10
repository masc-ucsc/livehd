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

Memories (milestone 2 of the prototype): per-port read/write (`store(mem, addr, d)`
under the enable, `tuple_get(mem, addr)`), sync read ports as `<mem>__rd<i>` registers
loaded from the same read, the whole-array `update` bus (`set_mask(mem, mem, bus, 0,
size*bits)` under `update_enable`), the async `read_all` bus (a concat of every entry),
a zero `initial` image with optional sync/async `reset`, one clock, `ordering "old"`
(fwd = undef = 0). Reads are worklist items, so a write whose data depends on a read of
the same memory is not a cycle; writes are emitted after every combinational cell.
Proven on the XiangShan `RenameTable` (70k lines of Verilog) and on flat dino.

A graph outside the subset is skipped with a `graph-skipped` warning naming the
reason, never lowered partially.

Added for the minion/boom/gemmini work: memories with byte/bit write enables (`wensize` lanes)
lower to a read-modify-write of the entry with one conditional `set_mask` per lane (`__wlaneN`);
that form is verified by simulation (`lnast_llvm_diff_test`, `mem_bytes`) but LEC refutes its
round trip on an uninitialized memory, so it is not in the LEC list. A Sub instance cycle (the
`fcall` form needs every argument before any output) is resolved by splicing the stuck instances
into the graph in memory (`graph_util::Host_inliner`) and lowering again; `lgundef`, `fproperty`
and `lgassert` markers are skipped. Known gaps: non-zero initial images, several clocks,
forwarding/undefined orderings; Latch, Clock_cell, n-ary Mux/compare, LUT, compact loops.

## Milestone 3 prototype: LLVM straight from the typed LNAST

`pass/lnast_llvm` (`lhd pass lnast-llvm --top T lg:IN --emit-dir sim:OUT`) lowers
the LNAST this pass emits (typed statement results, `if`/`unique_if`, registers
with `attr_set` clock/reset, memories, `fcall` instances) to one native state
kernel built with `Cgen_llvm`, with no lgraph, color plan or cgen_sim involved:
`void step(uint64_t* state, void**)` reads the inputs and registers from `state`,
publishes the post-edge registers and the outputs settled on them (what a
testbench sees after `step`). Instances are inlined with their own register and
memory state (instance-path names); `if` arms merge by `mux`; memory writes carry
the path condition as the write enable (kernel-local arrays seeded from the state
and published whole, so a large memory costs O(size) per step: prototype only).
An asynchronous reset is evaluated like the synchronous one (every step has an
edge and the testbench samples after it).

Acceptance: `//pass/lnast_llvm:lnast_llvm_diff_test` drives the same closed-form
stimulus into the kernel (`lnast_llvm_run`) and into the existing
`lhd sim` llvm flow (a generated Pyrope testbench importing the lg) and compares
the per-cycle `TRACE` lines byte for byte.

Verified on XiangShan `RenameTable` (100 cycles, byte for byte against the `lhd sim` llvm flow).
Timing (`LNAST_LLVM_TIME=1`, `LHD_SIM_LLVM_TIME=1`; use an optimized `lhd`: `bazel build -c opt`,
a fastbuild `lhd` is 10-30x slower in LLVM and misleads): RenameTable generates in 20 s (walks 2.5 s,
O2 2 s, instruction selection 9 s). `LHD_SIM_LLVM_EMIT_OPT=0..3` forces the machine-code level;
level 0 compiles huge single-block functions faster (RenameTable 12 s) but the kernel runs about 2x
slower (gemmini16 0.41 s vs 0.22 s), so level 2 stays the default. A balanced OR/AND/XOR tree
changed nothing on RenameTable (its 413k pre-optimization instructions are not OR chains).

Not covered yet: latches, `Clock_cell`, negedge or several clocks, whole-array
`update`, memories with non-zero images, wide (> 64 bit) register initial values,
compact loops, `fcall` under a condition.

## Stream interface (milestone 2)

`lnast/lnast_stream.hpp`: a minimal LNAST-to-LNAST pipeline, in the spirit of upass (a statement is
processed as soon as the producer finishes it) but without its symbol table or bundles. A producer
calls `begin(header)` (module name, attributes, the complete `io`) and then `stmt(tree, nid)` for each
top-level statement; a `Pass` forwards zero, one or many statements to its `next()` sink and may buffer
a lookahead window until `end()`. `Tree_sink` rebuilds an ordinary `Lnast`; `Chain` wires the stages.
Every stage speaks plain LNAST, so dropping a stage leaves valid compiler input.

`lg2lnast` is the first producer: `--set pass.lg2lnast.passes=balance[,more]` (also accepted by
`lhd pass lnast-llvm`). Stage `balance` turns n-ary `bit_and|bit_or|bit_xor` into a log-depth binary
tree of `%balN` temporaries. The round trip stays `proven-full` (the test runs every design with and
without it; XiangShan `RenameTable` too). Limitations: the producer still builds its whole tree
(the stream only avoids a second pass), source ids are not carried across trees, and
`lnast_llvm` buffers the stream because it walks the statements twice.

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

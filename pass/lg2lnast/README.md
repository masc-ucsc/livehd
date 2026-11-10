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
and `lgassert` markers are skipped. `Latch` cells are registers
with `latch`/`enable`/`enable_high` attributes (the front end's `reg l:[latch=true, enable=g]`) and a
`Clock_cell` is `Clock(clock_pin=clk, enable=en)`. Cells are ordered before any statement is built, so
the probe for stuck instances builds nothing. Known gaps: non-zero initial images, several clocks,
forwarding/undefined orderings; n-ary Mux/compare, LUT, compact loops with a break/continue or
an activation, or a state element in the body.

## Milestone 3: LLVM straight from the typed LNAST

`pass/lnast_llvm` lowers the LNAST this pass emits (typed statement results, `if`/`unique_if`,
registers with `attr_set` clock/reset, memories, `fcall` instances) to native state kernels built with
`Cgen_llvm`, with no lgraph, color plan or cgen_sim involved. Two front doors:

- `lhd pass lnast-llvm --top T lg:IN --emit-dir sim:OUT` writes the kernels and `kernel.json`;
  `lnast_llvm_run OUT CYCLES` drives them with a closed-form stimulus.
- `lhd sim` with the developer switch `LHD_SIM_LNAST=1` (or `auto`: fall back to inou.cgen.sim when the
  design is outside the subset) simulates a Pyrope testbench on them. `lnast_sim.cpp` writes one thin
  class per DUT (`__in`/`__out` Slop ports, `step()`, power-on, checkpoint and probe hooks answered
  from a field table by the design-independent `lnast_sim_rt.hpp`) plus the `<stem>.iface.json` the
  driver generator reads, so `inou/prp/prp_sim` drives it exactly like a class inou.cgen.sim wrote.
  No per-module C++ exists: the host compiler builds the driver and that one class.

How a design becomes kernels (`lnast_llvm.cpp`):

1. **One walk, recorded.** The hierarchy is walked once (instances inlined with their own register
   and memory state, `if` arms merged by `mux`) and every `Cgen_llvm` call is recorded on a *tape*
   instead of being built at once. State is one flat allocation of 64-bit words.
2. **Views.** A clock period is: *edge* kernels (the next state and the memory writes), a *commit*
   kernel, *settle* kernels (the outputs, on the new state). Both views are replays of the same tape
   pruned to what they publish, so the logic is evaluated once per period plus the output cone again;
   the settle kernels alone settle the outputs without an edge (power-on, a loaded checkpoint).
3. **Order.** The walk records statements breadth first (dependency order), which keeps nearly every
   value live at once. Each view is re-ordered depth first from its results, so a value is computed
   next to the cone that reads it. Reads and writes of one memory keep their recorded order (memories
   are written in place; a byte-enable write is a chain of read-modify-writes).
4. **Kernels.** The ordered ops are cut into intervals: an interval of a topological order never
   closes a cycle. Each interval is replayed into its own `Cgen_llvm` on the shared compile workers,
   and a value read by another kernel crosses through a state word. Size is counted in machine words
   of code (`LHD_SIM_LNAST_CHUNK`; 1250, or 150 with activity gating, below). A view that fits in four times that is ONE kernel: a
   cut makes whatever crosses it unconditional, and picorv32 cut in two executed 4,234 instructions a
   clock against 2,807 whole, at half the speed. A larger view is cut into kernels of one to two
   times the target, each at the seam in that window where the fewest live words cross. There small
   kernels win twice: boom as one kernel spent 364 s in instruction selection; as 13 kernels 7.8 s of
   LLVM and 26.4k cycles/s; as 50 kernels 1.7 s and 35.4k.
5. **Registers.** A register's next value is stored straight into the register when no later kernel
   still reads the old value (boom: 14,269 of them), else into a pending word the commit kernel copies.
   `r' = c ? x : r` landing in place is published as `if (c) r = x`, one branch per enable: a disabled
   register is not rewritten, what only `x` needs can sink under the test, and LLVM's scheduler no
   longer sees hundreds of selects sharing one flag (one such block took 23 s to schedule).
6. **Memories** live in the state and are written in place as `if (enable) mem[address] = data`: a
   disabled port touches nothing. A design that decodes its own write address arrives as one port per
   entry (dino's register file: 64 constant-address ports); as read-modify-writes those were 64
   dependent load/store pairs every clock (dino 4.41M -> 5.84M cycles/s).
7. **Reset** is kept out of the hot path: next state is computed without it and the commit kernel
   stores the initial values of each distinct reset signal's registers in one unlikely branch (the
   memory clear is an unlikely branch too). An asynchronous reset is evaluated like the synchronous
   one (every step has an edge and the testbench samples after it).

Acceptance: `//pass/lnast_llvm:lnast_llvm_diff_test` drives the same closed-form stimulus into the
kernels (`lnast_llvm_run`) and into the inou.cgen.sim llvm flow (a generated Pyrope testbench
importing the lg) and compares the per-cycle `TRACE` lines byte for byte.

8. **Clock phases.** The root clock is a value on the tape (a constant in each view), so what reads
   it folds: three views make one period -- *rise* (clock low, ending in the rising edge: posedge
   registers, memory writes), *fall* (clock high, ending in the falling edge: negedge registers;
   present only when something happens there) and *settle* (clock low again: the outputs). A clock
   gate `Clock(clock_pin=clk, enable=en)` is what an ICG is, `clk & h` with the latch `h` following
   `en` while `clk` is low; a register on a gated clock takes its edge only while every gate on the
   way to the root clock is on (a hold, published under that condition like any enable).
9. **Latches.** A latch reads as `enable ? data : held` in every view, and `held` follows under the
   same enable, in place. The latch op is created when the walk first needs it and completed when its
   data is known, so the tape is a dataflow graph and each view orders it for itself: a latch that is
   closed in a view (its enable folds to 0 there) contributes only its held value. A level reset is a
   mux over the plain latch. Two-phase logic on a GATED clock is a structural ring in neither view
   (with the gate off the gated clock stays low through the high phase): a latch's data is evaluated
   with every latch that is provably closed while this one is open -- the two enables hold one
   condition with opposite polarities -- read as its held value (minion: 65 such reads, 1,589 ops
   cloned). A ring that remains is reported with the latches on it.

10. **Activity gating** (a design of more than 20,000 words of code; `LHD_SIM_LNAST_GATE=0|1` forces
   it). A kernel is a function of the state words it reads, so it need not run again until one of
   them changes. Every kernel owns one bit of the *dirty* words at the start of the state; the caller
   runs a kernel only when its bit is set, clearing it first (a test per dirty word, per eight
   kernels and per kernel, with direct calls: scanning for set bits and calling through a table
   lost a fifth on boom, one indirect call site the processor cannot predict). Whoever changes a
   state location sets the bits of the kernels that *read* it and of the *other* kernels that write
   it -- a latch's held value is written in every phase it is open and a memory by each of its
   ports, so the location holds what the last one wrote and the others have to assert theirs again.
   - A store compares before it marks. Stores that wake the same kernels share one accumulator
     (`acc |= old ^ new`) and one test; under an enable the compare sits inside the guarded block.
   - A register that is not written in place changes at the commit, not when its next value is
     stored: its marks go to a second set of words that the commit kernel ORs into the dirty words.
     (Set at once, a later kernel of the same view would run now, on the old value, and be clean
     when the value does change.)
   - A reset marks everything; the testbench marks the readers of an input it changes, and everything
     after power-on or a loaded checkpoint.
   - A value crossing kernels has a state word per view: the same op is a different value in another
     clock phase, and two views taking turns on one word would never let its readers rest.
   - Gated kernels are small (150 words): a kernel is also the unit that is skipped. minion runs
     15% of its kernels a clock and boom 65%:

     | words per kernel | 1250 | 600 | 300 | 150 | 75 | 40 | not gated (1250) |
     | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
     | minion, cycles/s | 84k | 102k | 114k | 134k | 118k | 87k | 29.1k |
     | boom, cycles/s | 23k | 31k | 31k | 36k | 35k | 27k | 34.3k |

   - A register that only copies state (a pipeline stage, `r' = q`) computes nothing and belongs to
     no kernel's logic; gated, such registers get small kernels of their own ahead of the others, a
     chain of copies reader first so it stays in place. Left to the first kernel they were one block
     of 1,500 stores and compares on boom that took as long to compile as forty other kernels.
   - A small design is a kernel or two that run every clock anyway, and cut small enough to skip
     anything it loses more at the cuts (picorv32 6.5M -> 2.3M cycles/s): it is not gated.
   `LHD_SIM_LNAST_STATS=1` makes the simulator print how often each kernel ran.

lhdsuite, results equal to the inou.cgen.sim llvm backend (2026-11-10, opt build, 128 jobs):

| core | setup s | host C++ s | cycles/s | inou.cgen.sim: setup / host C++ / cycles/s |
| --- | ---: | ---: | ---: | --- |
| boom | 7.7 | 5.7 | 38.9k | 53.6 / 16.6 / 18.2k |
| picorv32 | 0.4 | 5.1 | 6.47M | 0.7 / 5.2 / 5.58M |
| dino | 0.3 | 5.3 | 5.84M | 0.5 / 5.4 / 5.81M |
| bitcoin | 0.2 | 5.5 | 4.15M | 0.4 / 5.7 / 3.17M |
| minion | 45.4 | 5.7 | 143.1k | 122.0 / 38.4 / 34.0k |

Of that setup the flow itself (lowering, walk, LLVM) is 10.2 s on minion and 1.2 s on boom, where
inou.cgen.sim took 86 s and 47 s; the rest is the front end both flows share.

Three things keep generation proportional to what is simulated: lg2lnast merges the arms of a
`Hotmux` that carry the same value into one arm on the OR of their controls (minion decodes an
"undefined when these collide" value as about 257 arms of one unknown constant, 15,000 times: 3.8
million statements in one module); the walk builds the AND of the enclosing `if` conditions only
when a memory write asks for it; and only the modules the roots still call after splicing are
lowered (a module spliced into its parent needs no LNAST of its own: minion's frontend was lowered
three times). minion: 18.7M tape ops -> 318k, the walk 7.4 s -> 0.4 s.

Timing switches: `LHD_SIM_LNAST_TIME=1` (phases), `LHD_SIM_LLVM_TIME=1` (per kernel); use an optimized
`lhd` (`bazel build -c opt`): a fastbuild `lhd` is 10-30x slower in LLVM and misleads.

A compact loop whose body holds state (bitcoin: N engines written as a loop over one module) is
expanded into its N instances first (`graph_util::materialize_occurrence`, in the flow's private copy
of the library); a stateless loop stays one `for`. In the native code a `for` of at most 256 trips and
16,384 body statements in all is unrolled (`LHD_SIM_LNAST_NO_UNROLL=1` keeps it rolled): each trip
then sees a constant index, so a lane is a fixed slice rather than a variable shift of the whole
value (bitcoin's 32-trip byte swap of a 256-bit hash: 473k -> 3.32M cycles/s). A larger loop is one
native loop (`Cgen_llvm::loop_begin/loop_end`).

Not covered yet (the run is declined, or falls back under `auto`): several root clocks, a register
on an inverted clock gate or on a clock that is not a gate of the root clock, negedge memories,
memories with non-zero images, `fcall` under a condition, VCD/observation runs, testbench access
below the DUT's top level, and the random power-on fill of inou.cgen.sim for an unknown (`?`)
initializer (read as 0 here, i.e. `sim.unknown_zero=true`).

## Compact loops: a `for`, never a call

A compact-loop `Sub` (`Subnode_loop`: index port, invariant inputs, carry in/out pairs, finals read from
the last trip) is lowered as a plain LNAST `for i in first..last { body }` with the body graph spelled
inline in the same scope: the index is the loop variable, a carry is an ordinary variable assigned at
the end of the body, a final is assigned every trip, and an invariant is the caller's operand. There is
no `fcall` and nothing is copied per trip. `lnast_llvm` turns the `for` into one native loop
(`Cgen_llvm::loop_begin/loop_end`: header phis for the carries, the body emitted once, LLVM told not to
unroll unless the loop has at most 4 trips and no inner loop), so the code footprint does not grow
with the trip count. `upass` unrolls the same `for` when the LNAST is compiled again, which is exact
(LEC `proven-bounded`/`proven-full`).

Memories are never copied into the loop. The front end hands a loop the whole array as one packed bus
(`read_all` of the memory) and the body indexes it through an array-view `Memory` on its `update`
input. When the only readers of that bus are loop bodies, no packed value is built and the body reads
`mem[i]` where the memory lives. When the bus is `mem` as modified by this cycle's write (`if c { mem[a]
= d }` before the loop: a mux of `read_all` and a masked-shift merge) the shape is recognized
(`find_overlays`), none of its cells is emitted, and the body reads `(c && i == a) ? d : mem[i]`.
Any other bus is still correct: the body slices the packed value (`(bus >> i*bits) & mask`).

Domains that are not `first..` with step 1 (a negative first index, any other step) count trips `0..count-1`
and derive the index in the body (`index = first + trip * step`). Module calls inside a body are spliced into
the body graph first, so the loop is still one `for`.

Not covered: loops that write a memory (the front end keeps those unrolled), break/continue (an
activation), and state elements inside the body.

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

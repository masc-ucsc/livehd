# cgen_sim false-combinational-loop bug — reproducer + fix plan

## The bug (silent wrong simulation)

`inou.cgen.sim` emits **one** `Out cycle(In)` per module and schedules the whole
graph with a single `g->forward_class()` topological pass
(`inou/cgen/cgen_sim.cpp:750`). A sub-instance (`Ntype_op::Sub`) is handled
**atomically** (`cgen_sim.cpp:816-843`): gather *all* input drivers → call
`child.cycle()` **once** → publish *all* outputs.

When a **purely-combinational** sub (e.g. `subadd`: `x=a+b`, `y=c+d`) is
instantiated once and one of its outputs feeds back — through *parent* comb logic
— into one of its own inputs (`u.x -> c = x+topx -> u.c`), there is **no real
bit-level cycle** (`x` does not depend on `c`), but at the node level it is
`Sub → adder → Sub`, a cycle. In `hhds`, a Sub is stamped `loop_break` only if it
contains state (`graph.cpp:1749`), so a pure-comb sub cannot be cut; the fed-back
input falls into the `forward_class` "cycle survivor" tail and
`operand()` (`cgen_sim.cpp:124-125`) silently emits

```cpp
u__i.c = Slop<8>::create_integer(0) /*UNRESOLVED*/;
```

→ the sub is called with `c=0`, the output is **wrong**, and the compile exits
`0` with `errors:0 warnings:0`. `inou.cgen.verilog` handles the identical design
**correctly** (declarative `always_comb` needs no scheduling); the `yosys-verilog`
reader **flattens** the hierarchy and dodges the bug (at the cost of modular,
incremental compile). Only `cgen_sim` — which must serialize a sequential
schedule while preserving the Sub (the real slang/Pyrope modular flow) — breaks.

## Why the naive fix is wrong

Marking comb subs as `loop_break` does **not** fix it. The module call is
*atomic* (one call = all-outputs-from-all-inputs), so breaking a single input
edge just **relocates** the missing same-cycle update: the output that needs the
broken input goes bogus, and for a stateful sub that bogus value **latches into
the flop** (`var_b`). The fix must make the module call *non-atomic*, or flatten
the offending instance, or iterate eval to a fixpoint — see the plan below.

## Reproducer

```sh
inou/cgen/tests/comb_loop/repro.sh      # needs ./bazel-bin/lhd/lhd built
```

It compiles each fixture with `--reader slang` and flags the bug signature in the
emitted `top.cpp` cycle (`create_integer(0) /*UNRESOLVED*/`, or an unknown-bits
feedback const for the loop_break case).

| fixture | shape | golden (correct) | buggy today |
|---|---|---|---|
| `base_falseloop.v` | pure-comb sub, `u.x→c→u.c` | a=10,b=20,d=5,topx=3 → x_out=30, **y_out=38** | y_out=5 (c=0) |
| `var_a_crosscoupled.v` | two comb subs `u1.x→u2.c`, `u2.x→u1.c` | → **o1=11, o2=37** | o1=5, o2=7 |
| `var_b_stateful_passthru.v` | stateful sub (loop_break) + comb passthrough fed back | a=10,b=20,d=3 → x=30, **q=34** after 1 clk | q bogus (unknown/1) |
| `var_c_genuine_loop.v` | **genuine** comb loop through a sub | none — **must error** | silently emits 0 |

`var_c` is the soundness guard: a real comb loop must stay a loud error, never be
"resolved" to a value.

## Fix plan (staged)

- **Stage 0 — loud-failure safety net (LANDED).** `operand()` records when a
  valid, non-const driver has no `pin2var` binding (provably a comb-cycle
  back-edge), and `do_from_graph()` emits a located `diag::err` (category
  `unsupported`, codes `comb-loop-through-instance` at the Sub-input site /
  `combinational-loop` generically); a non-deferred `.emit()` makes `run_step`'s
  `has_halting_errors()` gate block the sim build. Catches `base`, `var_a`,
  `var_c`; does NOT fire on the no-feedback passthru, `inou/prp/tests/sim/hier.prp`,
  or any of the 16 existing sim tests. **`var_b` is NOT caught by Stage 0** — its
  bogus value is a cprop-folded *unknown-bits const*, and unknown-bits consts
  occur legitimately in correct designs (`fsm_runner`), so a blanket guard
  false-positives; `var_b` is covered by the Stage 1 cycle detector instead.
- **Stage 1 — selective flatten (near-term correctness).** A pre-cgen_sim LGraph
  pass detects the pure-comb Sub on the back-edge SCC and inlines only that
  instance into its parent (mirror of `pass_partition::build_module`; targets are
  always stateless, so LEC/VCD/checkpoint correspondence is untouched). Sacrifices
  modular compile only for the offending instances.
- **Stage 2 — per-output-cone non-atomic Sub (strategic).** Store per-output
  combinational input cones (`comb_arcs`) on `GraphIO::DeclaredIoPin`, computed by
  a per-output backward BFS that stops at `is_loop_break()` nodes, persisted via a
  `arc=` token in `library.txt`. The child emits one pure `eval_<out>(cone-inputs)`
  per output plus a `tick()` commit; cgen_sim schedules a cyclic Sub at arc
  granularity (preferably a cgen_sim-LOCAL cone topo-sort that does not touch
  shared `forward_class`). Restores full modularity even for false-loop instances.
- **Stage 3 — fallback.** Verilator-style fixpoint settle iteration, only if the
  arc approach stalls (pays per-tick runtime; cannot distinguish false vs genuine
  loops statically).
- **Rejected:** stamping comb subs as `loop_break` (see `var_b`).

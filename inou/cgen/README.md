# Simulator coloring

`sim_color_plan.cpp` builds a simulator-private, occurrence-aware DAG. It keeps
compact loops as calls; each loop definition is compiled separately, so nested
loops retain their hierarchy. Carry and independent work from one source loop
execute in the same ordinal traversal. Independent sibling loops with equal
start, step and count also fuse, regardless of source location. A topological
ancestor check rejects dependent pairs; groups are bounded to eight loops,
and activation/break chains retain their own loops. Fusion currently retains all
boundaries when the surrounding word graph is cyclic, including cycles through
registers or opaque modules; it does not yet use the phase-expanded color DAG
to discharge those dependencies. The independent-cone extraction used by ABC
runs only in synthesis, after the simulation graph has branched off.
State reads, pending updates, and observations
have explicit execution phases. Register commits remain at phase barriers and
memory forwarding retains its value dependencies.

Coloring uses reverse Kahn traversal for every graph size:

1. Seed the ready queue with sites whose consumers have all been scheduled.
   These include outputs and pending state. Visit later execution phases first.
2. After visiting a site, decrement its fanins' remaining-consumer counts.
   Prefer newly ready fanins in the same module occurrence, with wider edges
   first; use structural identities to break ties deterministically.
3. Merge consecutive topological sites while the execution phase, conditional
   owner, and non-loop runtime boundary agree. A compact loop remains an
   indivisible call but can share its color with its input and output cones.
   Stop before the estimated peak live
   payload exceeds the `sim.tune.live_words` budget (256 machine words of 64
   bits by default), or the soft 50,000 gate-equivalent limit. Preserve module
   boundaries with at least 32 sites when they are near leaves (at most one
   ordinary child level) or their interface fits the budget. Other ordinary
   boundaries may fuse. Closed colors are never reopened.
4. Materialize cross-color values and the ABI, canonicalize reusable kernels,
   and validate the final color DAG.

An interval in a topological order is convex: a path between its members cannot
leave and reenter it. This makes color contraction acyclic without repeated
forward/backward reachability searches. Traversal and interval merging use
O(V + E) storage and O(E + V log V) time, including the deterministic ready queue;
hierarchy discovery, structural hashing, and ABI construction are separate work.
The same algorithm applies below and above the former 50,000-site cutoff.

The live-word estimate loads external values at first use and retires them at
last use. It includes intermediate values, pending state and outputs; constants
do not consume the budget. State reads are phase-specific. Indivisible wide
operations or loop calls that exceed the budget stay as singleton colors and
are reported separately. This is a conservative payload estimate, not a promise
about the CPU register allocator: address registers and compiler temporaries
also affect spills. ABI state indexing visits each register's own versions,
avoiding a scan of all versions for every register.

A leaf loop body can have one color per necessary execution phase when it fits
the limits; its iteration count does not duplicate that body. Multiple loop
outputs share one state-advance action per period, even when their consumers
land in different colors. Constants are embedded in kernels, not transported
through the runtime ABI.

Stateless leaf loops reuse one scratch callee across ordinals. Stateful loops
retain independent per-ordinal state, and non-leaf loops retain per-ordinal
caches so unchanged inner reductions can be reused. VCD mode retains all
iteration instances for observation. Stateless loops gate directly on their
wrapper generation rather than scanning every child's generation; proof-only
subnodes do not count as state. Checkpoints expose allocated scratch instances,
not replicated combinational temporaries for every ordinal.

Generated loop traversals carry Clang `unroll(disable)` (or GCC `unroll 1`)
hints. LLVM pipeline tuning also disables loop unrolling; LiveHD's bounded
LLVM pass pipeline contains no unroll pass. `compile.unroll=false` remains the
front-end default, independently preserving the compact graph representation.

`sim.tune.backend=llvm` emits eligible circuit kernels as LLVM bitcode; `slop`
emits C++ kernels. LLVM uses C++ for the driver, scheduling, state storage and
runtime operations such as compact-loop calls. Packed LLVM kernels load inputs
and materialize casts at first use, rather than loading every input at entry. The
input, output, changed-bit and owner pointers carry `noalias`: the generated
caller supplies disjoint buffers and module storage. Input buffers are also
`readonly`; distinct fields within a buffer use distinct constant offsets.

Large Slop binding initializers are split into translation units of at most
128 candidate colors; small color bodies alone do not bound the compiler work
in the generated runtime initializer. Large evaluator shards contain at most
256 colors or 16,384 version sites (an indivisible color can exceed the latter).
Scheduler functions contain at most 256 colors and never cross a phase barrier;
they share the evaluator translation units. Shared-kernel calls and changed-bit
actions use the same emitter in reset evaluation and normal scheduling.

LLVM memory callbacks preserve the signedness of addresses and lane enables
when unpacking the ABI, including narrow unsigned values with their high bit set.

Code-generation cache hits skip both
coloring and emission; use a fresh workdir to measure a cold setup.

## Runtime protocol costs (2026-09-13 measurements)

Profiling XiangShan's RenameTable (rolled loops), matched_filter and minion
against Verilator showed the per-cycle cost is protocol, not arithmetic. The
emitter now:

- Extracts a constant-width slice at a runtime offset (`x#[(i*W) ..+ W]`, the
  And/Get_mask over an SRA that upass.tolg lowers it to) with
  `get_mask_op_opt(x, lo, lo + W)`, and lowers the matching packed write with
  `set_mask_op_opt`, instead of shifting and masking the whole word; cprop
  folds the `(lo + W - 1) + 1 - lo` width to the literal first. A materialized
  shift whose every reader keeps a low lane computes only that lane (LLVM:
  `Cgen_llvm::dynamic_extract`, two guarded word loads and a funnel shift).
- Versions every `In` field wider than one word (`__ver_<field>`, bumped by
  every generated writer). A non-DUT module forwards such an input into a
  child only when the version moved, a wrapper broadcasts an invariant only
  when it moved, and a color root refreshes a wide input by its version.
  Sliced root ports are additionally gated by one whole-port compare.
- Keeps color activation as a bitset in execution order: a schedule function
  skips an idle 64-color group with one word test, tests bits from a snapshot
  local and clears the group once; commit functions OR their marks into locals
  and store per word.
- Fuses `x#sext[lo..=hi]` into one signed `get_mask_op_opt`.

The activation bitset gates execution only under `sim.tune.dirty=on` (the
cross-cycle activation cache, and the default). With dirty tracking off, every
color runs once per period with direct boundary assignments, and
`sim.tune.fence=auto` places no module fences, since a fence only pays when
dirty gating can skip what it isolates (with dirty on, `auto` fences a
single-use module at 16 sites per interface word). Both knobs, with
`sim.tune.live_words` and `sim.tune.backend`, form the tune vector (default
`tv1:d=on;f=16;lw=256;be=slop`) that `lhd sim` resolves per workdir from a
profile (docs/simopt.md §13). cgen folds it into the COLOR ROOT's generation key
only: non-root bodies never read it, so flipping it regenerates only the root,
and a default-equal vector keys exactly like the defaults.

`sim.tune.live_words` (`auto` = 256, was 20) is the per-color live-word budget: a
boundary slot costs a store, a compare and a dirty mark per value, which
outweighs the register pressure it avoids (minion 1.75x, matched_filter 1.15x,
RenameTable within 4%).

# Simulator coloring

`sim_color_plan.cpp` builds a simulator-private, occurrence-aware DAG. It keeps
compact loops as calls; each loop definition is compiled separately, so nested
loops retain their hierarchy. Compact loops are PRESERVED by ruling: the
O(1)-in-trip-count code footprint is what the large benchmarks depend on. The
debug/experiment knob `sim.unroll_sites` (default 0) lets
`Cgen_sim::prepare_graph` expand a small loop into straight-line code on the
private library to measure what the rolled form costs: the replicas are spliced
into the body that held the loop and one cprop/bitwidth round folds the
now-constant ordinal arithmetic (left as instances they would still be a call
each with a change-tested carry). A rolled loop is one call per iteration with
runtime-offset lane slices and a change-tested carry; flat, it is the
constant-offset code the Verilog reader produces for a generate loop, and on
lhdtrack that was 2.5x (br_mux_onehot) to 5.6x (br_arb_lru) -- the gap the
rolled protocol has to close by hoisting invariants out of the body at cprop
time and by cheaper carry and slice handling here. The expansion never changes
a simulated value, an observation run (VCD, probe, query) keeps every loop
rolled regardless, and `:expect_instances:` fixtures count the compiled graph.
Flattening also grows a module's site count, so `sim.tune.fence` can newly
fence a single-use module (br_flow_burst_mux_lru: 2 to 7 colors). Carry and independent work from one source loop
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

Each reference-clock period follows this order:

1. Settle input changes and latch cones before the rising edge.
2. Sample all posedge flops, then commit their pending Q values together.
3. Settle logic and latches with the post-rise state.
4. Sample all negedge flops, then commit their pending Q values together.
5. Settle logic and latches with the post-fall state and publish outputs.

A data-enabled latch evaluates in each settling phase, including when its
enable or data comes from a flop. Downstream cones see its phase-local pending
value; persistent latch storage commits at the end of that settling phase.
An open primary-clock latch follows data in its transparent half-period and
retains its closing value in the other half. Opening a latch does not change
what a flop already sampled on that edge. Flop captures never read another
flop's pending value from the same edge.

The phase-expanded dependency DAG orders the supported latch cones. This is
a zero-delay model with no clock-skew correction. The compiler's existing
latch legality checks still reject simultaneously transparent latch pairs;
cyclic transparent cones are not assigned an arbitrary evaluation order.

Opposite-phase latches on a gated clock may have mutually exclusive enables
without a compile-time clock level. The planner proves exclusivity under each
latch input's mux/reset guards and uses held state only for those guarded reads.
Shared arithmetic gets a separate input value version, so other observers
continue to see settled state. Unproved or simultaneously transparent feedback
remains a dependency cycle. Minion now generates and passes its 100,000-cycle
workload; see the [fix and validation](../../repros/minion_latch_scheduler_20260920/README.md).
The earlier [prototype report](../../repros/latch_scheduler_20260920/README.md)
records the original refusal. Focused validation is not whole-suite compatibility.

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

Slop pure body and reduction helpers require inlining, so the arithmetic is
inside the rolled loop even when the body exceeds the compiler's usual inline
cost threshold. Generated loop traversals carry Clang `unroll(disable)` (or GCC `unroll 1`)
hints. LLVM pipeline tuning also disables loop unrolling; LiveHD's bounded
LLVM pass pipeline contains no unroll pass. `compile.unroll=false` remains the
front-end default, independently preserving the compact graph representation.

`sim.tune.backend=llvm` emits each circuit color directly as a native,
position-independent object; `slop` emits C++ kernels. LLVM color objects have
no external calls or undefined symbols: object emission validates the native symbol
table, including dependencies introduced by instruction selection. Wide division
uses an internal restoring loop and memory intrinsics expand locally. The C++
support compiles normally, without host bitcode or cross-language inlining.
Stateless leaf loops whose entire output computation fits one native color
also get a native rolled-loop entry. The color body is explicitly inlined into
that loop once; there is no per-iteration call or C++ arithmetic. The adapter
packs ports once, invokes the loop, and publishes its outputs. Carry and
activation values stay in native SSA, including zero-trip carry seeds. The
entry accepts count/first/step dynamically. A proven nonnegative index range
removes impossible sign/bounds guards without unrolling; unsupported or wrapping
domains retain the general representation. Shared definitions merge their range
requirements conservatively. The emitter shares objects across identical
phase computations. Binding layouts salt generation reuse; the existing bounded
workers emit these objects. Unroll-disable metadata and IR regression tests
preserve the loop regardless of trip count.

The driver, scheduling, state commits and observation remain C++. Stateful,
nested, resource-backed and multi-color loops still use the existing compact
scheduler. Observation mode also retains that path. Unused Slop pure evaluators
are not emitted for LLVM designs.

Memory entries use contiguous packed words with no Slop tags or sign padding.
A shared support template supplies reset, checkpoint and commit operations. A
color receives an explicit table of data pointers to those arrays and staged
writes; LLVM implements bounds checks, lane masks, write priority, forwarding
and combinational commits directly. Pre-rise reads replay their forwarding
prefix inside the object, including ordering-none collision draws. Whole-array
registered updates cross a packed staging bridge and still commit at the existing phase barrier. Clock
guards are sampled by the evaluator adapter. Unknown constants remain seeded
runtime inputs, never setup-time random draws. Undefined memory collisions use
a deterministic seeded packed-memory stream implemented in both the shared
support and native IR; particular undefined values need not match Slop's stream.

Packed kernels load inputs and materialize casts at first use. Input, output,
changed-bit and resource-table pointers carry `noalias`: the caller supplies
disjoint packed buffers and separate memory storage. Input buffers are also
`readonly`; distinct fields within a buffer use distinct constant offsets.

Native object emission uses bounded workers during setup. `sim.jobs` sets the
same limit for object emission and host compilation. With no explicit limit,
the worker budget is sampled from available CPUs and load, with at least one
worker; setup waits for a completed job before starting another at the limit.
A reusable worker pool spans module generation. A completion by any worker
releases the next job; waiting for a slow first job does not idle the others.
Objects are written only when bytes change and are generation-cache artifacts,
so a missing native object invalidates the generated module. Both host build
paths link through response files, allowing thousands of color objects without
exceeding the shell command-length limit.

Large Slop binding initializers are split into translation units of at most
128 candidate colors; small color bodies alone do not bound the compiler work
in the generated runtime initializer. Evaluator shards target 512 version sites
for plans of at most 16,384 sites, and 2,048 sites for larger
plans; each shard contains at most 256 colors. An indivisible color can exceed
the site target. This splits host compilation without adding color boundaries
or changing the live-word budget. Scheduler functions contain at most 256 colors
and never cross a phase or evaluator-shard boundary. They call their local
evaluator directly, preserving compiler inlining within the translation unit.
Ninja compiles evaluator shards separately; unity batching is reserved for the
small support translation units.
Shared-kernel calls and changed-bit actions use the same emitter in reset
evaluation and normal scheduling.

LLVM memory operations preserve the signedness of addresses and lane enables,
including narrow unsigned values with their high bit set.

Code-generation cache hits skip both
coloring and emission; use a fresh workdir to measure a cold setup.

Partition identity uses two fixed neighborhood-hash rounds instead of globally
numbered refinement classes. Scheduling and storage identities omit operation
and literal contents, including a Sum operand's add/subtract role on both its
incoming edge and its producer's outgoing edge; semantic fingerprints and the complete canonical kernel
serialization still distinguish changed computations. Dense color indices and
member order follow the topological intervals. The generated
`<module>.color-layout.txt` retains boundary-value offsets and activation-bit
positions by identity across edits. Removed values free their addresses; new
values occupy free addresses. Rounded array capacities absorb small changes
without rewriting the shared runtime header, with no runtime lookup or pointer
indirection. The layout is allocation metadata, never evidence for reusing
logic; missing metadata is rebuilt, and duplicate or out-of-range allocations
cannot alias live values. A fresh build may use different addresses and must
produce the same simulation results. Width changes, capacity crossings,
hierarchy changes and pressure-limit changes can still invalidate more files. See the
[September 20 incremental experiment](../../repros/sim_partition_incremental_20260920/README.md)
for measured rebuild/runtime tradeoffs and outstanding validation limits.

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

The [Minion partition stability follow-up](../../repros/minion_partition_stability_20260920/README.md)
records the arithmetic-role fingerprint bug, allocator changes, and edit measurements.

## Native state/runtime migration

The normal LLVM simulator still uses generated C++ support. The next conversion
is tracked in [llvm-native-runtime](../../todo/livehd/llvm-native-runtime.html).
`Cgen_llvm::State_layout` emits kernels that read/write shared packed storage
directly and update dirty flags in the object. Native commit functions handle
contiguous register copies and staged memory entries. `sim_native_rt` loads those
objects and supplies generic storage and scheduling without invoking a host
compiler. The code-generator tests exercise this interface; full Color_plan and
testbench-driver integration is still pending.

`write_state_object` emits an allocation/initialization descriptor alongside the
native entry point. The descriptor is the only exported symbol: code and sparse
nonzero defaults have local linkage. Boundary words occupy the public prefix;
remaining words are private to each runtime instance and have no linker symbols.
`Native_objects::instantiate` allocates and initializes that storage without a
generated C++ class. Instances retain their object code's lifetime. Repeated
instances never share mutable state, and reset restores the object defaults.

For incremental simulation, LLVM object filenames and symbols use the color's
storage identity, not its dense schedule position. Cold discovery retains the
large pressure-limited partitions. `.color-cuts.txt` saves their terminal anchors;
subsequent discovery keeps surviving cuts while enforcing the same phase,
activation, dependency and live-word constraints. An oversized edited interval
splits locally; the next surviving anchor stops global budget reflow. Tuning
changes discard incompatible hints.

The LLVM `.llvm.k` sidecar hashes the complete unoptimized IR and emitter/toolchain
salt, then verifies the object's bytes before skipping optimization and native
code generation. Missing or damaged pairs are misses. The single
`lhd.incremental` switch controls generation reuse, partition hints, and these
native-object lookups. IR construction and shared C++ headers remain costs.
Repeated-shape identities and full native runtime integration remain migration
work. The rejected independent-cone splitting experiment inflated Minion from
4,981 to 28,480 native objects and slowed 100k cycles from 2.51 s to 4.45 s;
smaller partitions are not the incremental strategy.

Checkpoint plan fingerprints live in the executable root’s small `.tune-id.cpp`,
so a semantic edit can update checkpoint diagnostics without recompiling unchanged
module storage/scheduling bodies. The support table remains independent of the
tuning vector. This is cold immutable metadata, not shared mutable color state.

Within a module, identical finalized LLVM IR shares one native object. Equality
normalizes only the exported entry symbol; widths, constants, resource bindings
and the complete ABI remain part of the comparison. Caller-owned mutable state
stays separate. The host build follows driver includes and the artifact manifest
to exclude standalone module exports that are unreachable after inlining. Those
exports remain on disk; this does not yet eliminate their generation cost.

LLVM adapters reuse a whole input/register value already required by a color
when other reads select slices of that value. LLVM performs the extraction;
the adapter no longer packs those slices separately. Constant specialization
still scans the full hierarchy to propagate newly constant outputs, but runs
constant folding and width inference again only for locally rewritten bodies.
The parent-before-child regression checks propagation through six definitions.

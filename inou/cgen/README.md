# Code generation

## Verilog export

Graphs containing only module instances, ordinary RAM wrappers, and fixed
wiring operations export as structural Verilog: plain input/output and wire
declarations, continuous assignments, sized bit literals and explicit bit
connections for extension, truncation, slices and concatenations. This path
avoids procedural alias blocks, signed declarations and expression casts so
limited structural readers such as OpenSTA can read mapped netlists. Module
input connections use the callee port width explicitly. Native memories still
retain their RTL wrappers; a timing-only black-box view requires the matching
Liberty memory interface.

General behavioral graphs keep the existing emitter for arithmetic, arrays,
compact loops and runtime properties that require richer HDL constructs.
The focused `//inou/cgen:structural_verilog` regression checks emitted wiring
against an independent reference using `formal.effort=test`.

## Simulator coloring

With `compile.upass.inline=false`, ordinary combinational instances remain
in the graph and the occurrence color planner merges their logic with the
caller. Small bodies (fewer than 32 sites) never introduce a
module fence, even when repeated; live-value pressure and execution phases
still constrain merging. Both Slop and LLVM emit those members directly in
the root color evaluator, without child evaluation calls. Larger bodies retain color boundaries when their computation amortizes their
interface, using the same density rule for single and repeated instances.
Bodies of at least 1,024 sites retain their boundary. This does not
depend on frontend inlining; `sim_small_comb_smoke` tests both backends with
that frontend transformation explicitly disabled.

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
   owner, and runtime boundary agree. Stateless leaf reductions of computed wide inputs
   with at most one word of results share a runtime region separate from
   surrounding arithmetic, so
   their opaque calls do not force those cones into the LLVM backend's Slop
   fallback. Loops with already-packed inputs or wider results can share a color with surrounding
   cones to avoid repeatedly transporting large carried values.
   Stop before the estimated peak live
   payload exceeds the `sim.tune.live_words` budget (512 machine words of 64
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
hints. LLVM pipeline tuning also disables loop unrolling. `compile.unroll=false` remains the
front-end default, independently preserving the compact graph representation.

`sim.tune.backend=llvm` (the default) emits each circuit color directly as a native,
position-independent object; `slop` emits C++ kernels. LLVM color objects have
no external calls or undefined symbols: object emission validates the native symbol
table, including dependencies introduced by instruction selection. Wide division
uses an internal restoring loop and memory intrinsics expand locally. The C++
support compiles normally, without host bitcode or cross-language inlining.
Randomized literal resources are packed lazily on first use after seed setup,
then shared by every use of that literal and width within a color. Their
packed buffers stay fixed for the run, like the shared literal values.
Stateless leaf loops get a native rolled-loop entry when their output
computation fits one native color or a resource-free pure data DAG of colors.
For multi-color bodies, the emitter composes the required pre-rise colors,
including conditional regions, by explicitly inlining their existing LLVM IR.
The composed body is then explicitly inlined into that loop once; there is no per-iteration call or C++ arithmetic. The adapter
packs ports once, invokes the loop, and publishes its outputs. Carry and
activation values stay in native SSA, including zero-trip carry seeds. The
entry accepts count/first/step dynamically. A proven nonnegative index range
removes impossible sign/bounds guards without unrolling; unsupported or wrapping
domains retain the general representation. Shared definitions merge their range
requirements conservatively. The emitter shares objects across identical
phase computations. Binding layouts salt generation reuse; the existing bounded
workers emit these objects. A native rolled-loop entry keeps unroll-disable
metadata regardless of trip count. Multi-color bodies add a derived
`.native-loop.llvm.o` object with its own content cache and artifact tracking;
the original color identities and objects remain unchanged. Backend switches
and eligibility changes remove obsolete derived objects. Stateful, nested and
observation-enabled loops still use their existing scheduling path.

Host simulator C++ uses `-Os` for
both backends, including Ninja, the built-in compiler, and standalone Bazel
exports. Native LLVM objects use the `O2` module pipeline without size
attributes, and the default (O2-equivalent) machine optimization level. The
pipeline's own unrolling heuristics and interleaving remain disabled; native
bodies are explicitly inlined before optimization.

A loop EMBEDDED in its caller's kernel (`Cgen_llvm::inline_loop`) has a
constant trip count. When `count` trips of its body fit a budget (16,384
compute instructions per loop -- arithmetic, compares, selects and control, not
the packed-word loads, stores and casts of the sealed body's ABI, which inlining
removes -- at most 256 trips, 32,768 added per kernel) the loop is marked
`llvm.loop.unroll.full`; otherwise it keeps `llvm.loop.unroll.disable`.
A body's own embedded loops count expanded (the `lhd-unrolled-extra` function
attribute carries their pending growth into the enclosing body), so an outer
loop never multiplies an inner unroll past the budget. The graph and the Slop
code keep the loop rolled; only the native code is unrolled. Rolled, a per-bit
loop paid a variable shift, mask and insert per lane every cycle and LLVM could
vectorize it over a stack copy of a wide operand (store-to-load forwarding
stalls); unrolled, every lane index is a constant. lhdtrack Pyrope over the
equivalent Verilog, LLVM backend: br_flow_arb_rr 1.75x -> 0.96x, br_mux_bin
1.34x -> 0.95x, br_csr_demux 1.19x -> 0.94x, br_fifo_shared_dynamic_ctrl
1.29x -> 1.01x, br_flow_xbar_rr 1.08x -> 0.89x (mada4).

Simulation never stops on a cell contract the slop backend resolves: hotmux
controls that overlap at run time read the FIRST active arm (as Verilator does a
`parallel_case`), and an indexed mux selector past its last arm reads 0 (two-state
X). Both used to reach `llvm.trap` and kill the driver with SIGILL (picorv32 with
BARREL_SHIFTER + TWO_CYCLE_ALU); lhd now names a driver's terminating signal
instead of reporting "exited with code -1".

An embedded loop's carry-in sink has two drivers, the external seed and the
self edge from its own carry-out; the caller binds the seed (it used to take
whichever edge sorted first, and a self edge made the seed 0: XiangShan
RenameTableWrapper's `io_v0_old_pdest` diverged from slop). Native-object jobs
are drained once after every module is emitted rather than per module, so a
design whose loops are separate definitions no longer queues one object compile
behind the previous module's (RenameTableWrapper setup 89 s -> 46 s). Each LLVM
color reads its own members' value uses and boundary writes from per-module and
per-color indices instead of scanning the whole plan, and per-color eligibility
is memoized (XiangShan Rob LLVM emission 267 s -> 177 s; slop 143 s). Write-port
guards over many memory ports are balanced OR trees (`Cgen_sim::balanced_or`):
the left-nested chain exceeded clang's 256 bracket levels and Rob's simulator
did not compile on either backend. The optimization attributes and emitter salt participate
in object reuse, while host command changes invalidate compiled support objects.

Arithmetic temporaries remain LLVM SSA values, so register allocation can keep
them out of memory. Extraction, wide-shift and native-loop scratch buffers use
fixed array types allocated at function entry. This makes them promotable and
prevents inlining a non-entry allocation from introducing stack save/restore
inside a rolled loop. Persistent registers and memories keep their instance
storage. The [stack-local regression](../../repros/sim_llvm_slop_20261007/stack-locals/index.html)
records the reproduced IR overhead and correctness checks.

Dynamic extraction loads only the selected packed word when alignment proves
that the slice cannot cross a word boundary. On little-endian hosts, aligned
8/16/32/64-bit lanes load their native integer directly, including a bounds
check; unaligned slices retain the two-word path.
Multi-output packed kernels publish at their production points rather than
retaining every result until the return. The state ABI still captures all
aliased inputs and old values before publishing. Registered-memory reads after
the commit barrier use the array directly, without scanning the cleared pending
write slots. Pre-edge reads retain their per-port forwarding rules.
The [LLVM/Slop audit](../../repros/sim_llvm_slop_20261007/index.html) records all
355 ledger slowdowns, paired screening, loop-path differences, retained fixes,
and the rejected slice-transport experiment.
Expensive one-use operands of absorbing one-bit loop carries (`carry && value`
or `carry || value`) execute in a conditional region. Once the carry reaches
its absorbing value, subsequent iterations skip those calculations and indexed
reads while retaining all other outputs and iterations. Loads are moved only
within the same block, with no intervening writes; small expressions stay
branchless. This recovers C++ short-circuit behavior without unrolling.
See the [pipeline experiments](../../repros/sim_llvm_slop_20261007/pipeline-options/index.html)
for standard O2/O3 comparisons and the remaining Slop gap.

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

LLVM concatenation coalesces adjacent slices of the same SSA source before
assembling the result. It follows constant shift guards and preserves extension
bits and lane order, avoiding bit-by-bit reconstruction of an existing bus.
This reduces the USYN one-hot demux's native kernel text from 4,597 to 532 bytes
and its simulation time from 7.96 to 1.78 seconds. The
[Verilator gap investigation](../../repros/sim_llvm_verilator_20261007/index.html)
separates this code-generation issue from the remaining state/ABI transport
costs in source FIFO and sequential netlist simulations.

Scalar mux lowering also reconstructs shared word selections after hierarchy
expansion. Constant slices with the same selector share their word mux, and
relative bit offsets can be aligned by constant rotations, including wrapped
views of a word. This exposes structured mux trees across module occurrences.
Byte-aligned slices of a wide packed input use narrower loads at the original
load position on little-endian hosts, preserving the input snapshot across
intervening writes. Sharing stays within a basic block, and extension bits and
nonzero selector semantics remain intact.

One-bit data-state commits use a conditional value selection and branchless
dirty notification. Bits feeding mux selectors or flop/latch controls keep
conditional stores: control state often remains unchanged for long stretches. LLVM one-bit boundary stores similarly compare, store, and mask
the notification instead of branching through `slop_update`. This avoids a
branch per changing bit in mapped register banks. Wider values keep conditional
stores; commits still honor their sampled flag and the existing phase barrier.
Notification destinations are deduplicated without changing color membership.
See the [2.5× follow-up](../../repros/sim_llvm_verilator_20261007/followup-2.5.html)
for measurements and the unsuccessful broader experiments.

Packed kernels load inputs and materialize casts at first use. Input, output,
changed-bit and resource-table pointers carry `noalias`: the caller supplies
disjoint packed buffers and separate memory storage. Input buffers are also
`readonly`; distinct fields within a buffer use distinct constant offsets.

Native object emission uses shared job admission during setup. `lhd.jobs`
sets the ceiling for formal, LLVM object emission and native synthesis together;
0 selects affinity-visible CPUs. Load and measured/predicted memory can reduce
active LLVM workers below that ceiling, while always allowing one global job.
Host C++ compilation uses a fixed CPU/load limit chosen at launch. The LLVM
manager's per-job units are the module's IR instruction count. Admission happens
before submission, so queued LLVM modules cannot grow to the CPU ceiling under
memory pressure. Reservations transfer to the executing thread; each callable
releases its LLVM context at completion, rather than retaining it in a future.
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
Reset, checkpoint, observation, and tuning members retain bounded cold lambda
chunks. A member exceeding 512 KiB also gets its own translation unit, so large
state walkers compile in parallel instead of accumulating in the main host
source. Regeneration removes obsolete helper units when a design shrinks or
support is disabled.
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
  `Cgen_llvm::dynamic_extract`, one guarded word load for a single bit, or
  two guarded word loads and a funnel shift for a wider lane).
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
module below 1,024 sites at 16 sites per interface word, including reused bodies). Both knobs, with
`sim.tune.live_words` and `sim.tune.backend`, form the tune vector (default
`tv1:d=on;f=16;lw=512;be=llvm`) that `lhd sim` resolves per workdir from a
profile (docs/simopt.md §13). cgen folds it into the COLOR ROOT's generation key
only: non-root bodies never read it, so flipping it regenerates only the root,
and a default-equal vector keys exactly like the defaults.

`sim.tune.live_words` (`auto` = 512; 256 until 2026-11-04, 20 before that) is the per-color live-word budget: a
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

Fixed slices within one machine word share a packed ABI carrier. Each consumer
retains its own offset and width, including slices rebased across hierarchy.
This avoids one machine word and dirty comparison per bit, and reduces the
planner's live-value estimate. Both backends use the same transport layout.
Adapters do not zero transport arrays that are completely filled before use.

LLVM adapters reuse a whole input/register value already required by a color
when other reads select slices of that value. LLVM performs the extraction;
the adapter no longer packs those slices separately. Constant specialization
still scans the full hierarchy to propagate newly constant outputs, but runs
constant folding and width inference again only for locally rewritten bodies.
The parent-before-child regression checks propagation through six definitions.

The LLVM scalar ABI packs multiple outputs into one 64-bit return register when
their combined width fits, with the first output in the low bits. This avoids
temporary input/output/change arrays for small multi-output colors. The adapter
snapshots every old result before publishing any output; native rolled loops
unpack all result lanes without introducing body calls or unrolling. Resource-free
scalar declarations carry a `const` function attribute so the host compiler knows
the call cannot access simulator state. Resource-bearing calls retain their normal
memory effects. The [ABI transport investigation](../../repros/sim_llvm_slop_20261007/abi/index.html)
records paired speedups, aliasing/alignment experiments, and remaining gaps.

The packed (array) ABI of a color kernel no longer gives every value its own
word. `Cgen_llvm::packed_layout` is the one rule the kernel and its C++ adapter
share: values of at most 64 bits pack first-fit into the current word, and a
wider value starts a fresh word and owns whole words. A value alone in its
word keeps the plain whole-word load/store; a shared word is zeroed by the
kernel (after reading its previous value for the changed bits) and each output
ORs its field in, while the adapter packs inputs with `lhd_sim_pack_field` and
unpacks outputs with `lhd_sim_unpack_field`. The transport buffers of inlined
bodies and native loops use the same rule; a native loop ENTRY keeps one word
run per value, which is what its C++ loop adapter packs. `LHD_SIM_LLVM_WORD_ABI=1`
restores the one-word-per-value layout for A/B runs. Measured 2026-11-03 on six
lhdtrack designs (mascm1): within about +/-6% either way (fifo_shared -5%,
fifo_flops +6%), because each narrow value is still produced and consumed one
at a time on both sides; the boundary slots themselves are not packed.

### Direct storage for LLVM kernels (2026-11-03)

A packed LLVM color kernel reads and writes the simulator's Slop objects
itself (`Cgen_llvm::Storage`: an int64 type tag, then the value words). Color
slots (`__rt.__color_slot_*`), a flop's Q (read), an in-place flop's Q and a
staged flop's `_din` (write, compared against Q) go through an owner table
cached per instance in the color's `__color_bindings` entry; a runtime array
element (color slots, dirty words, commit flags) is the array's base plus a
constant offset, so one table entry serves the whole array. A storage output
compares its words, stores words plus the Integer tag, and with dirty
tracking also makes the adapter's marks itself (`Cgen_llvm::Change_effect`):
the consumers' `__color_dirty` bits, an in-place flop's state-changed flag,
a staged flop's commit flag or shard bit. Marks shared by several outputs
(dirty words) accumulate in registers and are applied once at the end; a
single-user mark (a commit flag) is applied at its output. Without dirty
tracking the kernel only stores. Distinct objects get distinct alias scopes,
and an object the kernel also reads is loaded before its store. Generated
code `static_assert`s every object's type and size. Native-loop bodies,
latches, top I/O, observations and sliced reads keep the packed transport.
`LHD_SIM_LLVM_NO_STORAGE=1` disables it; the debug mask
`LHD_SIM_LLVM_STORAGE_MODE` (1 reads, 2 color writes, 4 staged, 8 in-place,
16 in-place reads, 32 marks in the kernel; default 63) selects kinds for A/B
runs, and `LHD_SIM_LLVM_DUMP_IR=1` writes each object's optimized IR.

Measured on mascm1 (cold, default dirty tracking, `lhd sim` exec seconds,
storage / packed, all checksums equal): geomean 1.09x over 14 lhdsuite and
lhdtrack designs. br_fifo_flops 0.49/0.67, xs_renametable 1.23/1.56,
br_amba_axi_shrinker 1.30/1.62, br_arb_lru 0.54/0.64, ibex 1.55/1.68; slower
on matched_filter 2.18/1.89 (186 narrow registers pack better), isolate_sub
1.61/1.45 and picorv32 2.19/2.13. With `sim.tune.dirty=off` it is 1.2-1.9x
faster than the packed path. picorv32 is now ~89% kernel time: the remaining
gap to Verilator is the bit-level kernel code itself (spills), not transport.

### Conditional regions in LLVM kernels (2026-11-03)

A color kernel used to be fully if-converted: every Mux was an LLVM `select`,
so both arms of every `if`/`case` ran every cycle (picorv32's largest kernel:
841 selects, 0 branches; Verilator's code for it branches 248 times and runs
only the taken path). After the O2 pipeline, `conditionalize_selects`
(cgen_llvm.cpp) rebuilds the branches. An instruction belongs to an arm when
every user is a select of one group with it on that side, or another
instruction of the same arm. A group is the selects on one condition value
(GVN already merged equal conditions), so all outputs assigned under one
`if` share ONE branch, cut only where a user of an earlier select would come
before the group's last select. A group whose arms hold at least 4
instructions becomes if/else blocks and its selects become phis; the new
blocks are processed again, so an `elif` chain nests. Loads, stores and calls
never move. `LHD_SIM_LLVM_NO_BRANCH=1` disables it and
`LHD_SIM_LLVM_BRANCH_MIN=<n>` sets the threshold (both part of the object
cache digest). Measured (cold, default dirty tracking): picorv32 2.20 -> 1.71 s,
ibex 1.57 -> 1.40 s, matched_filter 2.19 -> 2.09 s, same checksums; the
hottest picorv32 kernel also spills less (1929 -> 1545 stack accesses at the
first threshold tried).

### Live ranges in LLVM kernels (2026-11-03)

picorv32's largest kernel spilled to 306 stack slots (no allocas: real
spills). Two passes after O2, both in cgen_llvm.cpp:

* `rematerialize_invariant` (opt-in, `LHD_SIM_LLVM_REMAT_DIST=<n>`, 32 is a
  good value): loads of objects the kernel never
  writes (storage the kernel does not store to, the owner table, the packed
  inputs) are `!invariant.load`; a short cheap chain over them (casts,
  shifts, masks, add/sub, compares, at most 8 instructions) used more than 32
  instructions away, or in another block, is recomputed before that use
  instead of kept live. The invariant marks are added at write_module, only
  on a kernel nothing else calls (a native loop re-reads its inputs every
  iteration). Spill accesses: picorv32 2156 -> 1551, ibex 2923 -> 2426; time 1.76 -> 1.75 s,
  1.38 -> 1.35 s, matched_filter 2.06 -> 1.99 s (distance 8: matched_filter
  1.73 s but picorv32 1.85 s).
* `shrink_live_ranges` (opt-in, `LHD_SIM_LLVM_SHRINK=1`): a bottom-up list
  schedule of each block minimizing newly live values (a greedy form of
  Shobaki et al.'s sum-of-live-interval-lengths cost). Neutral on AArch64,
  whose Hybrid DAG scheduler and pressure-tracking machine scheduler reorder
  each block anyway; LLVM's own LiveRangeShrink is X86-only and only hoists
  an instruction to its operands. Scheduler experiments go through
  `LHD_SIM_LLVM_ARGS` (e.g. `-pre-RA-sched=source`): none moved picorv32's
  spills below ~2000 accesses.

On this core spills are cheap (store forwarding, L1): a 28% spill reduction
changed picorv32 by under 1%, so remat stays opt-in -- it trades spill
reloads (928 -> 569 in the hottest kernel) for owner-table reloads (97 ->
454). Addressing every flop as one struct base plus a constant offset
(measured with host-compiler `offsetof` values) cut table loads 97 -> 6 but
spilled more; same time. `/usr/bin/time -l` (instructions retired, cycles)
shows why: picorv32 runs at IPC ~7.5 in both simulators, LHD retiring ~9,650
instructions per simulated cycle against Verilator's ~2,190. The gap is the
amount of work per cycle (about half in the largest kernel, which executes
nearly all of its ~5,200 instructions every cycle: ~180 storage outputs each
load/compare/store value and tag plus changed bits, and every mux arm
evaluated), not memory traffic.

### More clock gates in simulation (2026-11-03, measured)

`LHD_SIM_FLOP_CENSUS=1` prints, before and after clock-gate inference, every
flop's enable kind, whether it sits on an inferred gate, its reset, and how
many mux arms of its D select Q (a hold the enable does not carry yet). The
color plan report's `clock-gates` line counts the cells, owned state/data
versions, and owned values crossing colors.

picorv32 (152 registers, 1317 bits): 6 gates over 72 registers (337 bits);
51 registers have an enable but no gate (`min_bits`/`min_cone`), 29 have none,
and 19 still hold through D muxes (reg_pc, reg_next_pc, cpu_state, timer, ...;
`pass.enableopt` only extracts private hold regions). Inferring every gate
(`LHD_SIM_CLOCKGATE_MIN_BITS=1 LHD_SIM_CLOCKGATE_MIN_CONE=0`: 31 gates, 122
registers) is SLOWER: under LLVM each Clock_cell is its own color (the
`separate_runtime_calls` runtime owner), so the schedule goes from 17 to 66
colors and instructions retired per cycle rise 9659 -> 12761 (dirty on) and
6173 -> 7788 (off). No gating at all is 10090. More gates need an in-kernel
guard (the cell's activation computed in the kernel, the register's update and
exclusive cone behind a branch), not more colors.

Landed (2026-11-03): `Color_plan::native_clock_cell` -- under LLVM a plain,
rise-only Clock_cell on a graph-input clock whose output is read only as a
commit activation is an ordinary 1-bit value (the OR-reduced `en`) of
whatever color holds it, not its own runtime color
(`LHD_SIM_NO_NATIVE_CLOCK_CELL=1` reverts). And a gate-owned register's
storage write publishes its ungated update under `if (activation)`
(`Cgen_llvm::add_storage_output` guard): a closed gate skips the compare,
store, changed bit and effects, and O2 sinks the register's private logic
into that branch. Only where the skipped store is a no-op: an in-place
register, or a staged one with dirty tracking (`LHD_SIM_LLVM_NO_GATE_GUARD=1`
disables). picorv32 (dirty on): 17 -> 5 colors, 9659 -> 8562 instructions
per cycle; with every gate inferred, 8007.

Open: inferring gates with `min_cone=0` changes ibex's result (also on the
Slop backend and with x-free simulation): gating `ibex_controller`'s
`ctrl_fsm_cs` (async reset) moves its first post-reset transition one edge
early. `LHD_SIM_CLOCKGATE_ONLY=<module>` limits inference to matching modules
for bisection.

### Reset-free kernels (2026-11-03)

Reset is rare, so a color's hot path should not carry it. A design input that
some register's reset comes from (through inversions; also where that input
feeds data muxes) is a reset net. Every LLVM kernel reading one marks that
input with `Cgen_llvm::speculate_input(value, inactive)`. At write time the
exported kernel becomes a dispatcher: if every marked input holds its
inactive value it runs `<kernel>.fast`, a copy with those inputs folded to
constants (no reset muxes, no reset-dependent enables or gate terms);
otherwise `<kernel>.slow`, the kernel as built. Both are inlined into the
dispatcher by O2. Only resets rooted at a design input count: a functional
clear on a reset pin toggles, so a copy speculating it would rarely run.
Debug: `LHD_SIM_LLVM_NO_RESET_SPLIT=1` disables it, `LHD_SIM_LLVM_RESET_CALLS=1`
keeps the two copies as calls, `LHD_SIM_RESET_DEBUG=1` lists the speculated
slots. The Slop backend already keeps reset in a grouped slow path. Measured
instructions per cycle: picorv32 neutral (reset is one select per register
there), dino -3.5%, ibex +0.2%; the point is the cleaner hot path.

### Color-level in-place registers and native state (2026-11-04)

The in-place proof above runs on the version order before coarsening, so a
register whose old value is read after its update stays staged (a pending
copy plus a commit) even when coarsening puts that reader in the update's own
color. After coarsening, `Color_plan::discover` promotes such a flop: a
reader in an earlier color already ran, a reader in the same color only needs
the update sunk to just after it (the update has no consumer in its slot, and
the sink never passes one of its successors, including the anti-dependencies
earlier proofs added: `a <= b; b <= a` keeps one staged endpoint), and a
reader in a later color still forces staging. Flops clocked by a Clock_cell
the LLVM backend evaluates inline (`native_clock_cell`) qualify too: the
kernel guards their store with the cell's activation. The color keeps the
anchor coarsening ended it at, so incremental cuts do not move. The report
line is `in-place-state registers= memories= color-level=`.
`LHD_SIM_NO_COLOR_IN_PLACE=1` disables it. picorv32: 57 -> 118 in-place
registers, 95 -> 34 staged commits.

With the LLVM backend, registers (Q and `_din`) and color boundary slots are
declared `__lhd_nat<W, U>` instead of Slop: the value alone at its natural
width (1, 2, 4 or 8 bytes, whole words past 64 bits), zero-extended when
canonical unsigned, else sign-extended, and no type tag. It is two-state: an
unknown value lands as zero. Kernels load and store it directly
(`Cgen_llvm::Storage::native`); generated C++ (checkpoint, observation, the
testbench's `__prp_poke`, a C++ color) converts at the access, reading through
`.get()` because Slop's operator templates bind operands by reference.
Compact-loop definitions keep Slop members (their C++ bodies and the fused
wrappers read child state directly). `LHD_SIM_SLOP_STATE=1` restores Slop
storage for A/B runs.

LLVM kernels also read whole design inputs straight from their `__in` fields
(Slop storage, `LHD_SIM_LLVM_STORAGE_MODE` bit 64) instead of having the
adapter pack them, and without dirty tracking store whole design outputs to
their `__last_out`/`__out` fields (bit 128; with dirty tracking it measured
8,442 -> 8,601 on picorv32, so it stays off there).

Native frame (LLVM only, `Cgen_sim::frame_layout`): every framed (non
compact-loop) definition's state is one flat byte region at fixed offsets --
its registers' Q and `_din`, by alignment then name, then each framed child's
region. The standalone top allocates the whole hierarchy's region plus its
extras (the color slot block, the activation words, commit flags and the
state-changed flag) in `__lhd_frame_own`; a parent constructs each child with
`__S + child_offset`. The C++ classes keep their member names as references
into the region (chunk bases `__lhd_nat_<mod>_<k>`), so checkpoint,
observation and the testbench are unchanged, while the root's generated C++
(reads, `_din` writes, commits, C++ colors) uses `__S + offset` directly.
Kernels address every register, slot and dirty/commit mark from ONE table
entry, `__S` (`Cgen_llvm::Storage::offset`, `Change_effect::offset`); each
binding checks its offsets once (`__lhd_layout_check`).
`LHD_SIM_LLVM_NO_FRAME=1` disables frame addressing in kernels.

The kernel builder loads an input early only when an output stores to the
same object (an in-place register's old value). That check must compare byte
ranges: comparing only the table entry made every input load before the first
store once all objects shared `__S` (picorv32: loads at the entry, +250
spills, a net loss).

Measured instructions per simulated cycle, before -> after the frame (dirty on
/ off): picorv32 8,442 -> 8,037 / 4,794 -> 4,488; ibex 10,996 -> 10,660 /
9,466 -> 9,071; matched_filter 5,159 -> 4,080 / 1,992 -> 1,418; dino neutral;
minion 129,330 -> 128,287 / 661,661 -> 677,996 (338 C++ colors left, whose
other members got farther apart). Remaining table entries: the unknown-literal
constants (folding them to immediates measured -3..-4% on picorv32; it needs
a compile-time value), whole design inputs, and memories.

Loads in branch arms (2026-11-04): conditionalize_selects moves an arm's
exclusive computation into its branch but never a memory read, so a register
or slot only one arm reads was still loaded before the branch on every call.
LLVM's `SinkingPass` now runs after it (`LHD_SIM_LLVM_NO_SINK=1` disables).
Instructions per cycle, dirty off: picorv32 4,488 -> 4,371, dino 2,437 ->
2,177, br_fifo_shared_dynamic_flops 4,020 -> 3,463, ibex 9,071 -> 8,747; dirty
on mixed (dino and the fifo -6%, ibex +3.7%, br_amba_axi_shrinker +4.6%, open).
Most remaining selects are chains with cheap arms (a constant, a register's
old value, another select: priority if/elif chains and hold muxes) that the
4-op threshold keeps branchless; lowering it to 2 helps picorv32 (4,371 ->
4,262) but costs matched_filter +19%, so the threshold stays (needs a
chain-level decision, not a global one).

Default live budget 512 and range metadata (2026-11-04): `sim.tune.live_words`
now defaults to 512 (picorv32 merges into 4 colors; dirty off 4,371 -> 3,780,
dirty on 7,984 -> 6,315; ibex dirty off -12%; other suite designs unchanged).
Native loads carry `!range` (a value stored extended from its width), so a
1-bit flag held in a byte needs no mask or test: picorv32 3,780 -> 3,657 /
6,315 -> 6,191, ibex dirty off -2%, the fifo -2%, AXI shrinker -2..-5%.

Branch regions without an intermediate IR (2026-11-04, conditionalize_selects):
a load may join an arm when no store of its object, unscoped write, or call
lies between it and the branch (`LHD_SIM_LLVM_NO_ARM_LOADS`); a select of two
single-use loads becomes one load from a selected address
(`select_load_addresses`, `LHD_SIM_LLVM_NO_ADDR_SELECT`); a region needs
`min_cost` real operations (4; loads count) plus one per 8 values that would
cross the split (`LHD_SIM_LLVM_BRANCH_CROSS`; instruction selection is per
block, so a split strands loads from their extensions: matched_filter's one
4-op region crossing 254 values cost +12%); after regions, a scope-aware sink
moves values into the single block that uses them (`LHD_SIM_LLVM_NO_SCOPE_SINK`).
`LHD_SIM_LLVM_BRANCH_DEBUG=1` prints each candidate. Net instructions per cycle
vs the previous build: picorv32 -1.8% / -0.7% (off / on), ibex +1.4% / -1.8%,
dino 0 / -1%, the fifo -1.6% / 0, matched_filter and AXI flat. Threshold 2
helps AXI (-5%) but costs ibex +3% (more branches and spills): the remaining
selects are cheap, and a branch around them costs more than it saves.

Calls per simulated cycle (lldb breakpoint hit counts over every function in
`drv.bin`, differenced between two run lengths): picorv32 dirty off 12 (5
kernels, `__color_run`, 4 `__color_run_part_N` shard wrappers, the testbench's
`Blop::multn`), dirty on 13; ibex dirty on 63 (44 kernels, 11 shard
wrappers, 3 machine-outlined fragments and the cold paths of `__color_run`, a
C++ memory `tick()` every other cycle, the testbench). Everything else is
inlined.

Measured instructions per simulated cycle against the morning baseline
(picorv32, checksum unchanged): dirty off 5,856 -> 5,209 (in place) -> 4,885
(native state); dirty on 8,548 -> 8,469. Dropping the Slop tag store alone was
only -3%; the copies and flags around staged registers were the larger cost.

### Ordered state updates in simulation

The shared color plan can prove an in-place state update. Storage precedence
edges are retained in the version DAG and in the color DAG after coarsening;
`ordered_members()` gives both emitters the required order inside each color.
Slop activation grouping preserves that order for colors containing in-place
state, including the placement of reset actions. LLVM emits the operations in
the same order.

A clock gate's activation is sampled before state updates and retained through
its consumers' commits. Gate chains combine their sampled activations. Native
LLVM updates qualify both data and synchronous reset with that activation, so
updates belonging to different gates can share a color. Slop keeps separate
colors for whole-cone gating. Compact bodies with independent clock ports use
each port's period tick for its own state commits; a withheld tick holds only
that domain, and a tick change reawakens a quiescent body.

For rising-edge arrays, a zero forwarding prefix orders a read before the write
group; a full prefix orders it after the group. This covers old, forwarded, and
program ordering around one write group. Write ports retain their ascending
priority. The planner proposes these edges together and rejects updates in the
cyclic residual. Partial forwarding prefixes, array reset/bulk-update actions,
synchronous read ports, derived/multiple clocks, latches, and structural control
regions retain the staged protocol. Post-edge output computations remain where
they observe a distinct state version.

Compact loops do not globally disable ordering proofs for surrounding state;
state private to a compact body retains its own schedule. With Slop, registers around
compact loops retain staging to limit their live ranges through the loop adapters; the
LLVM backend embeds a pure loop in its caller's kernel, so their in-place proof is the
same as in a loop-free design.

Color activation marks and clears store the whole 64-bit word, and a staged register
commit copies its pending value word by word: the host compiler otherwise narrows
`word |= 1 << k` to a byte store and copies a small Slop with one 16-byte move, and
the next full-width read cannot be forwarded from the store buffer (a store-to-load
interlock per executed color or commit; `ls_bad_status2.stli_other` on Zen 2).

A register may overwrite Q once its last old-Q consumer has run. This proof uses
the existing topological order and records explicit anti-dependencies; a swap
therefore retains at least one staged endpoint. Proven updates have no commit
flag or barrier copy. Memory reads and full-word writes become direct indexed
loads and stores, with no pending-write records, forwarding, per-access change
flags, or old-value loads for a full-word write. The owning color conservatively
invalidates its memory readers once when it runs, reusing existing boundary
write enables to avoid waking idle readers; array accesses do not report
changes or export extra flags. If an enable is internal to the native color,
invalidation is conservative. Under Slop, write groups with dynamic enables
and at most 16 input words get their own color, keeping enable values visible
to scheduling; larger decoded write banks retain their surrounding logic to
avoid exporting every row predicate. LLVM keeps every write group inside its
color as a conditional store: a separate write color splits the surrounding
cone in two (the write's producers before it, the in-place updates that read
the old values after it), and those boundary slots cost more than skipping an
idle write saves (picorv32 4 colors -> 2, -13% instructions dirty off, -24%
dirty on; br_fifo_flops and the AXI shrinker/isolate harnesses -19% to -44%).
`LHD_SIM_MEM_SPLIT=1` restores the split for A/B.
Partial writes retain their lane merge, and dynamic enables and
unproven address bounds retain the checks required by the memory semantics.
Proven in-range addresses need no bounds check. Private root
storage omits pending registers and array write records; shared compact-body
storage retains the fields another occurrence's schedule may need. The LLVM
adapter compares an in-place output just before storing it, without retaining
old Q across the kernel call.

The plan report's `in-place-state` line records the selected register and memory
counts. `ordered_state.prp` captures old/forwarded/program memory reads into
registers and checks a swap; planner tests check ordering at color budgets 1 and
256 and the fallback for an interleaved write prefix. A native IR test also
checks that a proven in-range, always-enabled full-word access has no calls,
conditional branches, selects, or extra data loads/stores.
Write-only native colors have no output or dirty-bitmap buffer, and do not
clear an empty bitmap on return.

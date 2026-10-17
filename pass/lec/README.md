# `pass/lec` — hierarchical equivalence: the algorithm

`lec.md` is the engine reference (encoder facts, CLI, options, tests). This file
is the **hierarchical decomposition**: what a "proven submodule" means, what is
proven where, and why each rule is the way it is.

Everything here is about `lhd lec --top T`. **Equivalence is a claim about `T`
only.** Proving other modules is a separate request (`--top X`), never something
the driver asserts on the user's behalf.

## Effort presets (`formal.effort`)

`quick-check` / `test` / `long-check` start sampling immediately. They select
**work counts, not wall time**: 256 / 2048 / 16384 input vectors. **The default
is `test`** (owner-approved 2026-10-13). `formal.timeout` caps elapsed wall
time if the target cannot be reached; sampling never gets an extra minimum
allowance or a proof attempt first. `proof` runs only the proof engines, with
no solver resource limit and a default timeout of 120 seconds. `verify` also
runs only proof engines, with no default timeout. Parallel proof workers share
elapsed wall time; their solve times are not added together to spend the budget.

An UNSET effort combined with an option the sampled presets cannot serve
(a non-cvc5 solver including `lgyosys`, a proof-only engine such as `ind`,
`formal.cross`, or a phase other than `after_reset`) falls back to `proof`.
An EXPLICIT sampled preset with those options is a usage error.

`lhd lec` and `lhd formal verify` print the SAME verdict words and exit with
the SAME codes (owner rulings 2026-10-14). Every verdict carries a modifier;
`unknown` is never printed, because it names nothing.

| verdict | meaning | exit | `lec.verdict` |
|---|---|---|---|
| PROVEN-full | unbounded (inductive) proof | 0 | `proven-full` |
| PROVEN-bounded(n) | no difference for n design cycles from reset; still a proof | 0 | `proven-bounded` (+ `bound`: n) |
| TESTED-sampled(N) | a sampled effort preset (`quick-check`/`test`/`long-check`): N input vectors agreed; not a proof | 0 | `tested-sampled` (+ `vectors`: N) |
| TIMEOUT-time | any clock limit: `formal.timeout`, the wall deadline, cvc5 TIMEOUT/RESOURCEOUT/INTERRUPTED, a step budget; no proof, no counterexample | 0 + warning | `timeout-time` |
| TIMEOUT-memory | out of memory, caught instead of a crash: cvc5 MEMOUT, a forked worker killed at the RLIMIT_AS backstop, `std::bad_alloc` | 0 + warning | `timeout-memory` |
| REFUTED-cex | a real counterexample trace, including one that needs only a free blackbox / trusted-box OUTPUT, and a refute only at a trusted-box INPUT (`bbin:`): the boxed model is the contract | 10 | `refuted-cex` |
| REFUTED-interface | a TOP port NAME exists on one side only (a width mismatch is fine; a one-sided input that is not observable -- in the cone of influence of no output, e.g. a `clk` feeding only assertions or instances whose outputs nobody reads -- is ignored and noted in the detail); the selected top only, never a hierarchy child | 7 | `refuted-interface` |
| REFUTED-unsupported | engine=auto and no engine can model/check it (an encoder refusal, a word-level cycle, a pre-solve refusal) | 7 | `refuted-unsupported` |
| REFUTED-unchecked | options forced an engine that cannot handle the logic (`engine=ind` ...), or forbade the escalation that would decide it (`hier_refute=fail`, `decompose=true`, an obligation whose window/`past()` is deeper than `formal.bound`) | 7 | `refuted-unchecked` |
| REFUTED-vacuous | the check proved nothing, a sign of a problem: a contradictory assume set (it can prune away a real counterexample), nothing to check (both tops expose no output/state with matching names; a verify run with no assert obligation), or a verify assert under an `if`/`match` guard that can never be true (a dead branch). The message names the cause (contradictory scope, nothing to compare, the dead-guard list) and the hint what to fix. There is no PROVEN-vacuous | 7 | `refuted-vacuous` |
| REFUTED-unexplained | an engine gave up with no reason, or no recognized cause: a LiveHD bug to name | 7 | `refuted-unexplained` |

The INTERNAL undecided causes of one block (UNCONFIRMED, UNDISCHARGED,
UNPAIRED: per-block progress records and `lec[hier]` lines may still print
them) are never a top-level verdict: they only make the hierarchical driver
iterate (flat retry, inline a child, another engine). A refuted hierarchy
BLOCK is not a refuted design. One that still reaches the top is a bug and
reports REFUTED-unexplained. There is no design-size gate: every design is
attempted, and one that outgrows memory reports TIMEOUT-memory.

The engine-side mapping is `query_outcome` / `verify_outcome` (query.hpp) over
`undecided_outcome` (property_types.hpp); an undecided result that folds
several attempts (the auto portfolio) is classified from its deciding
attempt's own detail (`Query_result::cause_detail`), so one arm's wall-clock
kill never hides another arm's contradictory assumptions, and one engine that
cannot check the logic -- or that crashed, or gave up with no reason -- plus
another that timed out is TIMEOUT-time (owner ruling 2026-10-15: an
undecided-by-time result is never REFUTED-unexplained; the crash stays in the
detail). Only a design-level refusal (the encoder cannot model the design)
and a contradictory assume set outrank a time limit.

The top-interface pre-check (REFUTED-interface) is linear in the port count
and the definition sizes and runs under the run's wall deadline, which is
armed before it: the clock census it needs is taken per DEFINITION
(`design_clock_inputs` descends only the state-bearing and clock-transparent
instances, with an order-free occurrence walk), only when a one-sided input
needs it, and the one-sided-input observability walks share one step budget
before a single backward pass from every compared point answers the rest. A
deadline reached there is TIMEOUT-time.

`lec.verdict` is `tested-sampled` and `lec.sampling.effort_reached` names the level.
The reached level can be LOWER than requested when the timeout or repeated
per-query caps stop sampling early. Capped queries do not count.

**What is sampled.** One BMC unrolling of the requested top (`formal.bound`,
`phase=after_reset`, concrete children, no decomposition). Sampling bypasses
the theorem cache and never uses a tested child as a proof premise. When the
pipeline-flush analysis extends the reset hold, the checked window is
`bound + flush`, so delayed output differences remain observable.

**When sampling cannot run.** The sampler is one engine. A design it cannot
model (a collapsed/trusted/ignored box such as `formal.ignore_memory`, a phase
schedule only the inductive engine composes) escalates to the proof engines
(`lec: sampled testing ... escalating`), which decide it or name the feature
no engine models: REFUTED-unsupported is never the sampler's answer alone.

**Portfolio scheduling.** `engine=auto` races `ind`, `bmc`, `bmc:lazy` and
`bmc:intblast` (induction first; a cached `bmc` strategy hint leads instead).
When admission (`lhd.jobs`, host load) runs fewer arms than the race has,
`fork_race` time-slices: a waiting arm takes the slot of the longest-running
one after a short quantum (SIGSTOP/SIGCONT of the racer's process group), so
the decisive arm is never starved behind one that deepens to the deadline. A
combinational miter races the BV `ind` query against a deferred int-blasted
leg (the int-blast retry) inside the same wall allowance. One-sided box
compare points end the race at once: every arm shares that miter, and the
hierarchical driver answers them with a flat retry.

**Nested work runs on its caller's capacity.** A process forked below the lhd
root (a racer, an isolated worker, the ABC cone helper a racer forks) exists
only for a parent that is blocked until it finishes (`Job_manager::nested()`).
Its own first unit of work never queues for admission: a nested `fork_race`
always keeps one racer running on an unadmitted token that time slicing
rotates like a lease, and `prove_equal`/property dispatch/isolated workers in
a nested process do not wait for a slot. Such a process also finds its slots
by owner (a time-sliced racer may be resumed on a lease its thread never
acquired), so `Yield` hands them to its racers. Before this, the ABC helper's
arithmetic race queued for slots held by the very racers blocked on it and
burned its whole deadline under bazel's 2-CPU cap (15 s instead of 0.3 s).

**A one-sided box counterexample is real.** When the only unmatched points of
a bmc divergence on a common output are the box points (`bbin:`/`bbox:`) of
boxes that exist on ONE side (cells with no definition, trusted boxes), the
trace needs only those boxes' free outputs: REFUTED-cex (exit 10), with the
headline naming the side (`query.hpp` `one_sided_box_counterexample`). Box
points on both sides may be one cell whose instances failed to pair, so that
stays an incomplete correspondence.

**Sampling (simulated random traces + windowed searches).** One BMC unrolling
is encoded as usual; the sampler (`effort.cpp`) then works on it in two ways:

- **Simulated traces.** Every free symbol of the miter gets a value -- each
  cycle's data inputs (biased draws: 1/8 all-zero, 1/8 all-one, else uniform;
  clock and reset inputs keep their roles), but also the power-on state of
  reset-less registers, fresh X values and free memory contents (a seeded
  pseudo-random image) -- and the unrolled terms are EVALUATED natively
  (`term_sim.hpp`: SMT-LIB semantics, one linear pass over the cone of `bad`
  through the unrolling's `symbol == term` definitions; any other assertion,
  e.g. a design assume, is a constraint a trace must satisfy or it is not
  counted). No mismatch: the trace counts `bound` vectors. A mismatch: the
  caller's solver re-derives it with every free symbol pinned (cvc5 first
  confirms the trace by substituting it into every assertion and rewriting to
  `true`), and that model feeds the ordinary witness. dino
  PipelinedDualIssueCPU (26 unrolled cycles, 246k term nodes): ~4 ms per trace.
- **Decoded fields.** Three traces in four draw input FIELDS from the
  constants the design compares them against (`Term_sim::fields`: an
  `EQUAL(field, constant)` whose bits are copies of one input port's bits
  through registers, slices, concats, extensions and agreeing mux arms), so a
  random instruction word is a real instruction: dino's ADDIW mutation is
  exposed by ~1 trace in 25-50 instead of none in 1024 uniform traces.
- **Windowed searches.** Every other trace is followed by a search around it:
  the inputs of one or two cycles stay symbolic, everything else is
  constant-folded (`Term_sim::reduce`), and a private solver asks for an output
  or paired-register divergence within a horizon after the window (`flush/2`
  cycles, clamped to 2..8; halved while the reduced cone exceeds 25k terms).
  This finds a bug a random vector essentially never hits (one operand pair in
  2^16). An UNSAT counts `free symbols x horizon cycles` vectors (a floor); a
  per-query cap (250 ms) hit counts nothing. Searches share the run with
  simulation: they may spend as much time as building and simulating did (plus
  0.5 s), so an expensive search never sets the pace.

Before this, every sampled step was an SMT query on a fresh non-incremental
solver (the cvc5 1.4.2 profiles are non-incremental) that re-bit-blasted the
whole unrolling, with the power-on state left free so a "fully pinned" query
was a real search: dino's `test` reached formal.timeout=300 s while a proof
took 1.8 s. Now (mada4): quick-check 2.7 s, test 3.5 s, long-check 11.9 s,
proof 1.8 s; the bug1 mutation is REFUTED-cex at `test` in 6.5 s (a proof
takes 199 s). Sampling stops a little before the wall deadline (5% of the
budget, 0.25-5 s), so a run the deadline cuts short still reports the vectors
it checked instead of a killed worker's empty report.

**Register leads.** Each trace also watches the name-paired registers'
next states (ref-X masked). A reachable register divergence whose outputs still
agree is only a lead: the sampler pins that trace up to the divergence cycle
and searches the following cycles for an OUTPUT mismatch (2 s cap); only that
is reported. An unconfirmed (register, cycle) is not chased again.

Counting: a vector is one cycle of one trace. Levels: quick-check 256, test
2048, long-check 16384 vectors. Seeded RNG: a run is reproducible.

**Limits.** Sampling is a test, never a proof: it is never `proven`, never a
PROVEN-bounded(n), never a theorem-cache entry, never a hierarchical premise. It
checks only what reaches an output within the sampled window. A miter outside
the simulator's fragment (an uninterpreted function: boxes, which the sampler
refuses anyway) falls back to solver-only steps.

Found by it: lhdsuite's checked-in `minion_dcache_cache_op_unit.prp` had lost
the per-call `dcache_way_encode = 0` at the `lock_way` site (a cleanup edit;
the current `prp_writer` output is correct). The legacy flow only reported
undecided for that def; a fully pinned random trace refuted it at once.
Opaque/trusted/collapsed boxes and ignored memories are diagnosed (a box makes
a sampled mismatch possibly spurious). A design with no free data input has one
trace per reset, so the ordinary bounded check runs instead.

The single-design `lhd formal verify` accepts the uncapped `verify` preset;
the sampled presets are for `lhd lec` comparisons.

Tests: `//pass/lec:effort_test` (levels, sampler accounting, simulated traces,
decoded fields, rare-mismatch search, timeout); `//pass/lec:term_sim_test`
(native evaluation against cvc5 on random QF_BV terms, memories, constraints,
reduce() equisatisfiability); `//pass/lec:lec_effort_sampling_test` (an
opcode bug REFUTED at the default effort); `//lhd/tests:lhd_effort_smoke`
(immediate sampling, proof-only verdicts, level reporting, cache separation,
diagnostics, sampled and rare failures).

The ABC cone accelerator is optional. In the ABC-free CLI build
(`--define=livehd_abc=false`), every accelerator request returns `Unsupported`;
the original obligations remain for CVC5. Stable cone digests and verdict names
live in `cone_digest.cpp`, independently of ABC. The normal build retains the
existing accelerator and its differential tests.

Mapped registers keep the source hierarchy in their bit names (`lane.state[3]`,
or `lane.state[3].flop_16` after cell-model inlining). When mapping removes some
bits, LEC relates only the retained bits to the corresponding slices of the
source register. It leaves the removed source bits arbitrary, proves every
retained next-state slice equal, and checks all outputs. Duplicate or
out-of-range bit names cannot form a relation. Explicit implementation initial
values take precedence over a proposed shared initial state. `pass semdiff
--stats` reports these projected name pairs separately; they cannot justify a
structural proof that skips the solver.

Register and memory correspondence is shared with semdiff through
`core/state_match.hpp`. Packed registers can correspond to retained scalar bits,
including sparse groups after synthesis prunes unused bits. Renamed groups are
only proposed when their shapes are unambiguous. Finite memories can correspond
to complete named entry banks, with packed entries or individual bits. These
relations are proof candidates: they never qualify as structural identity, and
LEC checks initial state, retained next-state bits, and observable outputs.
The shared bank-name normalizer also removes the cell model's final state
segment from ordinary names such as `mem._mem[0][24].flop_16`, as well as
encoded memory wrappers. Several states claiming the same alias remain
ambiguous and cannot establish a bank relation.

Memories pair across the two designs by correspondence name first. A
port-written memory with no initial contents, reset arm or `$readmem` image on
either side that no name pairs can pair POSITIONALLY: its leaf name plus the
iteration indices along its path, outermost first, so the memories a Pyrope
loop builds (`u_loop_0__li1.tile__li0.mem`) pair with the ones a Verilog
generate builds (`gen_row_1_gen_col_0_tile.mem`) when that key is unique on
both sides. semdiff's "no counterpart" for such a memory does not veto the
pair (semdiff may not see a memory inside a compact loop at all); only a
confident semdiff pair with a different partner does. A positional pair is a
correspondence choice, not a claim: induction re-proves it every step, and BMC
keeps the reference's unwritten entries unknown, exactly like an unpaired
reference memory and like any reference memory whose bucket did not collapse
(each design then holds its own free array). Remaining memories pair by
ordinal, as before.

Rolled and unrolled loop levels share one spelling: a `u_loop_<n>__li<k>`
wrapper level folds onto the next segment with its tag placed before that
segment's own `__li` tags, so `u_loop_0__li0.t__li1` and the unrolled
`t__li0__li1` name the same state (iteration tags are outermost first).

One-sided initial state follows one convention for registers and memories
(owner ruling 2026-10-16). A REFERENCE that is uninitialized (power-on X)
against an IMPLEMENTATION that initializes or resets the same state is a
refinement: the reference allows any value, so BMC starts the reference at the
implementation's value -- at power-on for an `initial` value, and at the first
checked cycle for state the implementation's reset defines (the reference's
own writes during the reset prologue do not define it). The opposite
direction, a reference with power-on contents or a reset against an
uninitialized implementation, keeps the implementation's state arbitrary and
refutes: `initial mem[i] = 8'h5a` against no initializer is REFUTED-cex, and
induction gives up on a memory whose power-on contents only the reference has
(no inductive base). The induction step cannot see a reset present on one side
only, so those refinements come back PROVEN-bounded. A `$readmem` image is
externally loadable state, not an initializer: the memory starts from the
shared array, images are never compared and never folded to their contents.
An `ordering="none"` read that collides with a write is a don't-care on the
reference and an unconstrained value on the implementation, so "none" refines
"old" but not the other way around.

Under `formal.lec.gold_x=ignore` BMC threads a knowledge state for each
reference memory (`Mem_x_state` in `encode.hpp`). A memory of at most 64
entries keeps one bit-vector per entry (`words`): bit i of entry e is unknown
when bit i of `words[e]` is set. A larger one uses an array: bit i of entry e
is unknown when `select(plane, epoch ++ e) ^ fill` has it set. A reset or a
write through an unknown address starts a new epoch with a new fill word
instead of swapping in another constant array. The all-zero array is the only
constant, nothing chains through an array-sorted ITE, and the epoch starts as a
fresh symbol, so no store chain is ever constant: cvc5's array solver aborts
with "write-chains connecting two different constant arrays" when two different
constant arrays can merge, which the old all-known/all-unknown pair of planes
allowed.

The memory VALUE arrays of memories with at most 32 entries never reach cvc5
as arrays: `Fresh_solver` rewrites every array with an index of at most five
bits into one bit-vector term per entry before each check (`array_blast.hpp`,
an exact equivalence), and a query left without arrays is solved as QF_BV.
cvc5's lazy array solver case-split on every pair of symbolic addresses of the
unrolling: two 4x8 lanes written through an unknown address took 21 s at BMC
bound 2 and timed out at bound 3 at 120 s (now about 7 s and 18 s; deeper
bounds still grow quickly, so `auto` on that design ends TIMEOUT-time).

For a memory-to-bank induction bridge, the memory is represented by stores of
independent bank state symbols at each valid address. Read definitions are
expanded into exact read-over-write muxes before cone digests are computed.
Unrepresented addresses retain the unconstrained array tail. This preserves the
full state relation while allowing the Boolean cone prover to discharge dynamic
reads. Large read muxes use ABC's internal correspondence CEC; multiplication
cones race correspondence CEC, SAT sweeping, and Ivy with exact carry-save, tree
and array arithmetic expansions under the existing wall deadline. An undecided cone retains the
original cvc5 obligation.

Sequential `auto` workers reserve half the finite solver allowance and wall
backstop for induction and half for BMC. The dispatcher runs each leg in a fresh
process, with at most one active solver per hierarchy job. A killed induction
attempt or an arbitrary-state step counterexample still leaves BMC an attempt;
unused induction time transfers to BMC. A reachable BMC counterexample refutes,
while a BMC pass remains explicitly bounded. The combined deadline never exceeds
the existing hard backstop. Parallel flat races already give both engines an
opportunity concurrently and keep their existing limits; explicit engines,
unbounded runs, and deterministic resource-limited solver grants are unchanged.
For Liberty netlist comparisons, a collapsed induction attempt likewise reserves
half for flat recovery; exhausting that first attempt cannot suppress recovery.
The flat retry preserves the original BMC reservation: if the collapsed attempt
spent the induction share, recovery goes directly to BMC.

## 1. The shape of the proof — top-down, then discharge

```
for each def D in the top's subtree, IN ANY ORDER, ALL AT ONCE:
    prove D_ref == D_impl with EVERY child of D replaced by a box
                                                     # a PREMISE, not yet a fact

then, leaves-first (pure bookkeeping, no solving):
    D is UNCONDITIONALLY proven  iff  D proved AND every child it boxed is
```

That is the default (`formal.lec.hier_order=top_down`). Every def is proved
against the *assumption* that its children are equivalent, and those assumptions
are discharged by the other entries of the same pass.

**This is an induction, not circular reasoning.** The module DAG is well-founded:
D's premise set is strictly-lower nodes, and a leaf assumes nothing. So the
closure terminates and the composition is sound — it is ordinary assume-guarantee
over a partial order. Three things follow, and the third is why it is the default:

- **No def waits on another def's verdict**, so the hierarchy is dispatched with
  *no* dependency edges — fully parallel, instead of serialized by depth.
- **Every miter is at its minimum size.** Bottom-up must *flatten* a child that
  did not prove, which is exactly the case where the parent is hardest.
- **A refuting block is absorbed by its parent, and the chain stops there** (§3).

The premise direction matters and is easy to get backwards. Assume-guarantee
gives

> (∀ children C: C_ref ≡ C_impl) ⟹ D_ref ≡ D_impl

and **nothing in the other direction**. Refuting a premise does not refute the
conclusion, so a child that fails does not make its parent fail — it leaves the
parent *conditional*. A top proven only conditionally is **never a pass**: the
driver keeps escalating (it inlines the open child, then re-proves the top flat)
and the top's verdict is that run's outcome (PROVEN-*, REFUTED-cex, TIMEOUT-*,
REFUTED-*). An undischarged premise is an internal iteration trigger, not a
verdict.

### `formal.lec.hier_order=bottom_up` — the legacy order

```
topo-order the module-def DAG, leaves first
for each def D (children already settled):
    prove D_ref == D_impl, with every PROVEN child of D replaced by a box
```

Only a proven child becomes a box; an unproven one stays flattened into its
parent. Kept for A/B measurement, and because `hier_refute=fail` (a debug mode)
is *defined* in terms of leaves-first order — "skip a def whose child already
refuted" has no meaning when no def waits on a child.

Both orders discharge **the same set of per-def obligations**, so an all-proven
run costs the same either way. They must also produce the same verdict for every
design; an ordering strategy may change cost, never an answer
(`lec_hier_topdown_test.sh` case 4).

## 2. What a proven submodule is

**A sequence transducer, and nothing else.**

`D_ref ≡ D_impl` says: *from reset, identical input sequences produce identical
output sequences.* That is the whole content of the proof, and it is all a caller
needs. So in the parent P:

| | obligation |
|---|---|
| **A** | prove the inputs P drives into D are equal, at every step — the `bbin:<box>:<port>` compare points |
| **B** | prove everything outside D is equal — the ordinary outputs and next-states |
| **C** | assume D's outputs are equal — **one shared free symbol per (instance, port, cycle)** |

C is *justified by* A plus D's own proof plus equal initial state. Nothing else is
needed, and in particular:

- **Comb vs. stateful does not matter.** From the caller's side both are "given
  X-sequence, always Y-sequence". Whether D holds registers or memories is
  invisible, so there is one box kind, not two.
- **No state cut, no uninterpreted function, no congruence rule.** The box output
  is a plain bit-vector symbol shared between the two designs.

The symbol must be **fresh per cycle**. Reusing one symbol across the unroll pins
the child's output to a constant, which false-proves a design whose only
difference is *when* that output changes. A's obligations are re-checked every
step, so the sharing stays justified step by step.

### What D's own body already says

A synthesized side folds constants and drops unread ports across module
boundaries, while the source keeps computing them; the box must not turn those
folds into differences no output can show. Read from D's body on **either** side
(D_ref ≡ D_impl carries the fact to the other), for an untrusted proven child:

- **An input port that reaches no output** (no path through logic, state or
  nested instances) owes no `bbin:` obligation: `D_ref(x,u) = D_ref(x,u') =
  D_impl(x,u')`. (A bedrock FIFO with bypass disabled: the parent ties
  `bypass_ready` to 0, the source still drives it.)
- **An output port driven by a known constant** is that constant on both sides,
  not a free symbol (the same FIFO's `bypass_ready` = 0 output).
- **A child with no input port** (a tie-off constant generator) is never boxed:
  descending it is exact, and a side that folded one of its instances away would
  leave the box one-sided.

A TRUSTED box keeps every input obligation and free output: there the boxed
model, not a body, is the contract.

### Initial state

If the run drives reset (`formal.phase=after_reset`, the default), the reset
prologue puts both copies of D in the same state and C follows outright.

If the run does **not** drive reset, we *assume* the two copies start in the same
state — exactly what the engine already does for a reset-less flop (a shared
`s0_<key>` symbol). Same rule, applied to a box.

### The one asymmetry worth knowing

From reset, C is a **consequence**. Mid-stream — the single-step inductive miter,
which starts from an arbitrary equal state rather than from reset — "the two Ds
are in corresponding states" is an **assumption**: D's own from-reset proof
compared outputs, it never built a bisimulation between D_ref's and D_impl's state
spaces. It is the standard assume-guarantee hypothesis and it is the right one to
make, but it is an assumption, which is why the from-reset frame is the one to
prefer for hierarchy.

### Legacy box model (`--set formal.lec.box_model=uf`)

The previous encoding modelled a stateless child as `UF_<def>_<port>(inputs)`
(`Comb_box`) and a stateful one as `UF_out(state)` / `UF_next(inputs, state)` with
a threaded per-instance state cut (`State_box`). It is kept as an escape hatch,
but the sequence model is strictly better:

- the UFs force the query into **QF_AUFBV**, which disables cvc5's eager
  bit-blaster — the driver's flat re-solve exists to work around exactly that;
- `Comb_box` emitted **no `bbin:` obligations at all**, so a stateless child's
  inputs were never checked and ABC saw the two sides' `APPLY_UF` as unrelated
  free inputs, leaving every cut downstream of it undecidable;
- `State_box`'s per-instance state cut can cross two interchangeable instances and
  refute two equivalent designs;
- `boxcong` existed only to recover "equal inputs ⇒ equal next state" for the UF
  form.

## 3. A REFUTED intermediate def is not a bug report

**A module boundary is not part of the specification.** Functionality moves across
it: the Pyrope and the Verilog may partition the same machine differently, so an
intermediate def's I/O behaviour need not match even when the design is
equivalent. Only the requested top's boundary is contractual.

So on a refute at def D (`formal.lec.hier_refute=escalate`, the **default**):

- **inline D into its caller P** and re-prove P;
- D's **siblings and descendants stay boxed** — only the offender is expanded, so
  the cost is bounded;
- if P proves, that P-with-D-inlined is what became a proven unit. It is *not* a
  proof of D: another caller of D may partition differently and has to inline it
  too;
- D's counterexample is kept as a **diagnostic** ("the two designs stop agreeing
  here"), never as the run's verdict. The driver reports how many refutations
  were **ABSORBED** this way.

### Why top-down makes the escalation stop at P

This is the case the order exists for. Under top-down, by the time D's refutation
is known, **P has already been proven with D boxed**, and so has every ancestor of
P with *its* child boxed. So:

1. re-prove P with D inlined, everything else still boxed;
2. if P proves, P's own boundary behaviour is intact — and every ancestor's proof
   already only ever depended on *P being equivalent*, which is now established
   without the D premise. **The chain closes. Nothing above P is re-solved.**
3. only if P *itself* refutes does the escalation move up a level, and there the
   same argument applies again.

Bottom-up has no such stopping rule: it learns D refuted *before* P is proved, so
P runs with D flattened; if P then refutes, P's parent runs with both flattened,
and the cone grows one level per step — in the worst case all the way to the top,
which is the slow path this replaces. (User ruling, 2026-08-02.)

**Measured on minion** (same compiled inputs, same trust list, order the only
variable): 146 s top-down vs 222 s bottom-up, same 166/170 defs proven, same exit.
The report is where it really shows. One def, `intpipe_csr_file`, is undecided under
both. Bottom-up flattens it into `intpipe_top`, which is then undecided and flattens
into `core_top`, which flattens into `minion_top` — where the accumulated cone hits
a word-level combinational cycle and the encode fails outright, so the run's
headline is a 29-node cycle dump. Top-down proves all three with their children
boxed (`minion_top`: *297/297 cones PROVEN, every cut discharged*) and names the
three undischarged premises. The cycle never enters a miter, because nothing is
flattened.

`formal.lec.hier_refute=fail` is the old fail-fast behaviour: it stops at the
first differing block, which localizes quickly but calls a boundary mismatch a
design bug. It is a **debug aid**, announces itself with a warning, and forces
`bottom_up` (it is a leaves-first notion).

Under this policy, **a refutation of the top is a real bug** — in the generated
Pyrope, in the Verilog read, or in LEC itself — and should be chased as one.

## 4. Correspondence: the part that can lie

A/B/C is sound. What can still go wrong is *which instance pairs with which*.

Boxes pair by `<defname>#<tag>`, name-first (an instance name survives both
front-ends) with an occurrence fallback for the unnamed remainder. With two
interchangeable instances of one def, a wrong pairing makes A fail on a named
port — a *false* refutation, but a well-localized one. The fix is a permutation
retry over the symmetric bucket, the same shape as the same-shape-memory
permutation retry in `todo/livehd/2f-lec.html`.

A box key present on **one side only** yields one-sided obligations, which the
miters gate to an incomplete correspondence — never a Proven or a Refuted.

A paired flop need not correspond on EVERY bit. A bit that reaches no compared
obligation (output, box input, property, memory port), directly or through
other observable flop bits, is dropped from the induction hypothesis and from
the step (`observe.cpp`, lec.md "Observability"): the relation proven is then
"the observable bits are equal", which is weaker than full equality and so
initially true whenever full equality is, and every remaining obligation is
still solved. This is what lets a reference register that keeps bits nothing
reads (bedrock's lower-triangle LRU state) pair with an implementation that
stores something else there, or nothing at all (a narrower register).

## 5. Timing: the phase schedule

Orthogonal to the above, and documented in `todo/livehd/2f-lec.html`
(§phase-schedule) and `todo/livehd/2f-latch.html` (§m10). In brief: a design with
a latch, a negedge endpoint or a recognized clock gate is encoded over an ordered
sequence of **microsteps** per source period —

```
close_low   active-LOW clock-role latches close (immediately before rise)
rise        posedge flops, posedge memory ports, data-gated latches
close_high  active-HIGH clock-role latches close (immediately before fall)
fall        negedge flops, negedge memory ports
```

— from a **read-only** analysis (`phase_sched.{hpp,cpp}`): no coloring, no graph
rewrite, no synthesized phase counter, no timing state threaded through ports, so
it composes across hierarchy. A close microstep only runs when something happens
in it, so a plain posedge/negedge design costs two encodes per period, not four.

### Mapped cells: one netlist, one schedule

The schedule classifies a latch from its enable's **structure** (`clk & en_l` is a
clock-role latch closing at the fall; an enable it cannot see through makes a
data latch committing at the rise, with the clock read as data). The `lg:` form
of a mapped netlist and its Verilog re-read should therefore reach it in the
same shape. Two causes of a divergence are repaired before the first query:

- **which body, and port binding** (pass/single_edge/proof_prep.cpp,
  graph/inline_sub.cpp, used by every `prepare_time_base` client): an instance
  of a `--lib` cell whose definition the design's own library holds is spliced
  from **that** body, bound by id through its own IO -- the encoder's rule too
  (an instance with a body is descended into; `sub_lib` resolves only a
  body-less one). Splicing the `--lib` model over a body the design carries
  compared the model instead of the design: an `lg:` netlist shipped with its
  own bypassed `DLCLKPx1` (`GCLK = CLK`) or `INVx1` (`Y = A`) PROVED against the
  gated RTL. A Verilog side elaborates the `--lib` models itself, so there the
  own body *is* the model. Only a **body-less** instance takes the model, bound
  by port **name** through the instance's IO: the model can number ports
  differently from the netlist's declaration (gensim's `DLCLKPx1` puts `GCLK`
  after the internal `IQ` pin), and id binding dropped the gate output and left
  every latch it enabled always-transparent. A model that lacks a port the
  instance declares, or states it at another width, refuses the run
  (`unsupported`) instead of being spliced around. Names follow the model
  either way: an own body's single state element takes the model's name (or
  its anonymity), so a correspondence key does not depend on which copy of the
  cell was spliced (cgen invents `latch_16` for gensim's anonymous latch, and
  keying on it paired two netlists' latches by instance name alone);
- **latch-enable cells** (`lib_cell_prep.{hpp,cpp}`): after edge normalization
  declines, the single-product cells (`AND2`, `NAND2`, `NOR2`, `INV`, ...) whose
  output depends on a clock are spliced on every latch enable, so
  `AND2x1(clk, en_l)` in an `lg:` netlist is the same clock-role gate as
  `clk & en_l` in the RTL or a re-read Verilog netlist.

Known gaps, where the two forms can still be classified differently (observed
as a false REFUTED):

- a single-product cell on a gate's **non-clock** operand stays opaque in the
  `lg:` form (only clock-dependent cells are spliced) while the Verilog re-read
  dissolves it: an active-low gate `NOR2x1(clk, INVx1(held_en))` exposes the NOR2
  but keeps the INVx1, and that `lg:` netlist refutes its own Verilog emission;
- cells with an `Or` in their model (`OR2`, `XOR2`, `AOI`...) stay opaque, because
  the schedule's `Or`-form gate model is itself incomplete: natively,
  `gclk_n = clk | ~held_en` (held_en latched while clk is high) consumed as
  `always_latch if (!gclk_n)` refutes against the equivalent
  `always_latch if (!clk && held_en)`, with no cell involved.

`tests/lib_cell_prep_test.sh` pins the repaired cases on a clock-gated data latch
mapped by ABC and USYN: the `lg:` netlist, its Verilog emission, and the netlist
against its own emission all prove; broken twins refute against each; a `--lib`
model with a renamed port refuses; an `lg:` netlist compiled with its own cell
bodies proves, and with one broken cell body refutes. `//graph:inline_sub_test`
pins the by-name binding and the state-name override themselves.

### Name bridges are hypotheses, re-checked by the step

A bridge that relates state by NAME or SHAPE -- the bit-blast regroup (`r` <->
`r[i]`, or a one-bit `r` <-> a read-back cell's state `r.<seg>`, core/bus_name),
a packed/scalar split, a memory <-> flop-bank pairing -- ties one side's
current state to the other's. That is sound only as the hypothesis of an
induction whose step proves the tied next states equal again, so both
inductive arms (the single-step miter and the phase-scheduled composed period
and per-microstep accelerator) fold the impl's bits back under the ref's key
and compare them. A tied key is never excused as "one-sided internal state"
(`internal_state_only` holds only for state whose current value is a free
symbol): left unmatched, it keeps the verdict open. The composed period used
to excuse `nxt:g` / `nxt:g_l` after tying `g` to the impl's `g.l` (a `DLX g`
holding latch `l`), and PROVED a ref `g = ~d` against an impl `g = d`
(`tests/lec_phase_test.sh`, pair E).

### The clock forest

Clock relationships are decided **design-wide, top-down, before any def is
proven** (`Clock_forest`, built in `lec_hierarchical`):

```
seed       the def-under-proof's clock ports, unioned across ALL its
           instantiation sites anywhere in the library
propagate  reverse-topo (top-down): each child's clock port takes the root of
           the net its parent drives into it
derive     a Clock_cell / inline gate resolves THROUGH to its reference clock —
           gating, inversion and division split the SCHEDULE, never the forest
close      with exactly one top root, every clock-spelled port in the subtree
           carries it
implicit   a def's `clock` port is its own clock: the unique other root
```

Resolving each endpoint bottom-up instead is not enough, and all three ways it
fails were live on minion:

1. A Pyrope `reg x = 0` has **no clock cone at all** — it commits on its module's
   own clock — so it became a synthetic `<implicit>` root.
2. A cone that stops at an opaque boundary names the **child's** port, so one net
   acquires as many roots as it has spellings (`clk_i` in 85 defs, `clock` in 6).
3. tolg gives any module holding a `reg x = 0` an input port named `clock`, and
   **no instantiation site ever drives it** — so no site union can tie it to the
   module's named clock port. That one unbound port refused `intpipe_csr_file`
   (declares only `clk_i`; its parent wires `clk_i = clock`) and, through it,
   `intpipe_top` and `core_top` — a module whose only clock port is `clk_i` was
   reported as having three roots, with the two sides disagreeing.

The implicit-clock rule is deliberately narrow: `clock` joins the unique *other*
root, so a module with two genuinely unrelated clocks still refuses by name.

## 6. Trust

`--trust DEF` / `formal.lec.trust` assumes a def equivalent **without proving
it** — an explicit, disclosed assumption ("PROVEN under N trusted def(s)"), for a
def the user chooses not to compare. It is not a workaround for a cell the
encoder cannot model; latches, negedge endpoints and clock gates are modelled
directly now.

## 7. Verdict discipline

PROVEN-* means equivalent (PROVEN-bounded(n) for n cycles); REFUTED-cex means
definitively different. A clock or memory limit is TIMEOUT-time / TIMEOUT-memory
(no proof, no counterexample); unsupported timing is REFUTED-unsupported (or
REFUTED-unchecked when an option forced the engine); ambiguous correspondence is
an internal trigger to iterate, and only if it is still unresolved at the top
does it become REFUTED-interface (a one-sided top port name) or
REFUTED-unexplained. Collapse, pairing, phase scheduling, proof order and every
retry must preserve that. An abstraction may cause more work or an undecided
result — never a wrong verdict. A check that compares nothing is not a proof of
anything: when neither top exposes anything to compare it is REFUTED-vacuous
(exit 7), and so is a comparison under a contradictory assume set.

Two ways the hierarchy can quietly violate this, both guarded:

- **An undischarged premise is not a proof.** A top proved with every child boxed
  is only a *conditional* result; if any premise never discharges the driver
  re-proves the top with the open child inlined (or flat), and that run's
  outcome is the answer. `rc 7` (REFUTED-<why>: the tool, the options, the
  assumptions or a bug) and `rc 10` (REFUTED-cex: here is a counterexample) must
  never be conflated (`tests/lec_verdict_policy_test.sh`).
- **An absorbed block CEX is not the design's answer.** Once a parent re-proves
  with a refuting block inlined, that block's counterexample is a diagnostic; it
  must be dropped before the run picks a fallback verdict, or a design that is
  equivalent reports `equiv_fail`.

## 8. Incremental: the unit of work is the CONE, not the def

**Status: §8.1 and §8.2 are implemented; §8.3 and §8.4 are the plan.** The
per-cone cache exists and is correct; nothing above it is cone-granular yet.

### 8.1 Why the def is the wrong unit

One def, `minion intpipe_csr_file` (23 410 nodes of own body, 16 children, all
proven inside 1.1 s):

```
ind:  cones 312/316 PROVEN (312 by abc in 1.9 s)   residue 4 -> decompose -> 2
      total 7.5 s, and it NAMES the residue: {mem:2x49#0, mem:2x49#1,
                                              nxt:reg_scause_pre}
bmc:  no cones at all -- ONE checkSat over the whole def, unrolled 6 deep
      total 126.7 s, undecided, learns nothing
```

Induction localizes the failure to three obligations in seven seconds. BMC is
then handed *the module* and re-attacks all 316 compare points monolithically,
including the 312 ABC discharged in under two seconds. The residue is the
question; the def is what gets asked. Everything else in that design — 169 defs,
one of them with 18 collapsed children — costs 13 s combined.

Collapsing is not the lever here: those 16 children are 18–2832 nodes against a
23 410-node parent, so boxing them removes almost nothing. The def is already as
small as boxing can make it.

### 8.2 The cache is already per-cone

`lec_store_cones` is deliberately **not** gated on the def's verdict:

> a cone proof is per-OBLIGATION, so a def that ends undecided because ONE cone is
> hard still proved the others, and those stay valid forever (the digest is the
> whole claim). The next run then re-attacks only the residue.

A cone's key is `cone_digest(term)` — the canonical structure of the obligation
itself, so it survives renaming, reordering, and any edit elsewhere in the def.
That is what makes *partial* hits per module possible, which the def-pair verdict
cache can never do.

### 8.3 The algorithm

```
CACHE  : cone_digest -> PROVEN        # per-obligation; verdict-independent
METHOD : def_name    -> method        # per-module; what WON here last time

lec(T):
    for each def D in subtree(T), in any order, all at once:   # §1, unchanged
        solve_def(D)
    discharge premises leaves-first                            # §1, unchanged

solve_def(D):
    if verdict_cache hits (digest D_ref, digest D_impl, opts):  return PROVEN
    if semdiff(D_ref, D_impl) is a structural identity:         return PROVEN

    cones   <- cut_at_state(D)              # one obligation per compare point
    residue <- [c for c in cones if digest(c) not in CACHE]     # PARTIAL hit
    if residue empty:                                           return PROVEN

    for c in residue:                       # per CONE, not per def
        for m in ladder(D):                 # stop at the first method that wins
            if m.prove(c) is PROVEN:
                CACHE.add(digest(c)); METHOD[D] <- m; break
        else:
            unresolved.add(c)

    return PROVEN if unresolved empty else undecided(unresolved)

ladder(D):                                  # ORDERING ONLY, never a verdict
    yield METHOD[D]                         # last winner first: a small edit
                                            # rarely changes which method wins
    yield abc, ind, bmc, int_blast          # minus the one already yielded
```

Two properties carry the weight:

- **A miss costs at most the old behaviour.** An undecided cone falls through the
  whole ladder, which is what it does today anyway — so the worst case is the
  current cost, and the common case is a hit.
- **`METHOD[D]` is keyed by def NAME, not by digest**, so it survives the very
  edit that invalidates the verdict cache. That is the case it exists for: the
  digest misses, and the method that won last time still wins.

### 8.4 What it must not do (see §7)

- **Decomposing the induction STEP is sound**; every cone shares the antecedent
  *(all cuts equal at t)*, so proving conjuncts separately is ordinary
  conjunction-splitting. This is what ABC-subtract already relies on: an OR is
  UNSAT iff every disjunct is.
- **A per-cone BMC result is BOUNDED and from reset** — a different obligation.
  "312 cones proven inductively (conditionally) + 3 cones bounded-proven" is
  *not* a closed proof; it needs the PROVEN-bounded(n) discipline §7 already applies
  at def level. Aiming BMC at the residue as a **counterexample hunt** is
  unconditionally safe and strictly cheaper.
- **The cone cache must stay verdict-independent.** Gating it on the def's
  verdict would throw away exactly the partial progress it exists to keep.

### 8.5 Two bugs this area had

- **The workdir was never created.** `Verdict_cache::save()` treats an unopenable
  path as "the cache is only ever a speedup" and returns silently, and no command
  on the formal path called `ensure_dir`. Every run stored to memory, wrote
  nothing, printed its in-memory `N stored` tally, and came back `0 hit(s)`
  forever — so the entire incremental tier (verdict cache, cone cache, known-timeout
  ledger, strategy hint) was dead. A gate that greps for the string `lec[cache]`
  cannot catch this: the line prints with `0 hit(s)`. Assert HITS.
- **An undecided def flattened every proven child** to re-enable the eager
  bit-blaster. Inlining a proven child is never a proof obligation — UNSAT
  through a box is already sound — so on a plain undecided it is a gamble with an
  unbounded re-solve. It is worth it only while chasing a CEX (§3 escalation).

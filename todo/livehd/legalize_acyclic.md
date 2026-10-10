# Legalize: one owner for combinational loops, one cross-boundary constant fold

Status: stage 1 implemented (2026-10-06, pass/legalize/acyclic.cpp); stages 2-4
open. Owner rulings from the 2026-10-06 sim-speed review.

## Rulings

1. **Legalize guarantees acyclicity at ARC level.** After legalize, the
   hierarchical graph has no combinational cycle when each instance is one
   call whose output depends on the inputs its port_reach summary lists (its
   comb arcs, like liberty timing arcs). No other pass ever reasons about
   combinational loops; a consumer orders instances by the arcs or flattens.
   (Revised 2026-10-06 from "every Sub atomic": the strict view inlined 129
   valid/ready-style Moore/Mealy handshakes in minion -- sim setup 54->154 s,
   host C++ 29->58 s, sim 2.2->5.3 s -- with no real port-level loop among
   them.) Every non-loop instance on an arc-level cycle -- stateful or not --
   is inlined (warning `comb-loop-through-module`). A STATE-FREE instance (no
   register/memory/loop/blackbox in its closure) on a cycle with instances
   ATOMIC is inlined too: no state moves, and a consumer would otherwise
   evaluate the callee in pieces (sim's per-output-group partitions; minion's
   two such instances cost ~5 s of sim setup). A rolled loop on such an
   atomic-only ring is left alone (ruling 5).
2. A **true** combinational loop (a bit depends on itself with no register,
   latch or registered memory read on the path) is a diag **error**.
3. A **false** loop (word/port granularity only: packed Get_mask/And/Or/Concat
   fields, or an instance output that does not depend on the instance input it
   feeds) is **removed** by legalize -- by inlining or by rewiring packed
   slices inside a body (`split_packed_cycle_slices`). A loop that exists only
   at port level (a packed bus whose fed-back field the output does not read)
   is not a loop: the arc view is refined to BITS inside each port-level
   cycle (2026-10-08, slice-refined arcs), so such an instance keeps its
   boundary (the XS CSR -> ExuBlock inlining cascade that broke LEC). No
   module ports are split today. The warning names the instances involved
   ("combinational path loops through module port(s) ... ideally split the
   bus/module in the RTL").
4. Loops INSIDE one module are already removed at `lnast.tolg`
   (`split_packed_selfref_wire`); that stays.
5. **Sub-loops (compact loop subnodes) are never unrolled, split or inlined
   by legalize** (ruling 2026-10-08). Keeping loops rolled through synthesis,
   simulation and LEC is a key contribution (cf. XLS `counted_for`). A ring
   through a loop instance that is a cycle only while the instance is atomic
   is no combinational loop and is left alone; consumers evaluate the loop as
   a unit. A path that really goes through a loop instance (arc level) is a
   TRUE loop: it is reported as an error (`comb-loop-through-loop`).
   (`split_loop_by_ring` remains as a utility; legalize no longer calls it.)
   The two current ring-dodging unrollers (cgen_sim
   `compact_loop_has_external_ring`, upass `loop_output_rings_back`) go away.
   A loop instance's arcs are its body summary CLOSED over the carries (from
   the second ordinal a carry-in holds the previous carry-out).
6. Prefer running the repair without bitwidth; if needed the order may be
   `bitwidth, legalize, cprop, bitwidth`.
7a. (2026-10-07) **Only intra-module passes run before legalize; anything
   hierarchical runs after.** Today: bitfuzz, cprop's per-body transform and
   bitwidth (callee read only through its declared interface) before;
   cprop's loop-invariant hoist (body + parents), enableopt, satopt, formal
   after.
7. cprop+bitwidth run ONCE per module, then once more over the coloring
   partitions (colors cross module boundaries), with constants passed across
   colors -- replacing sim's private `specialize_constants` re-runs. No pass
   re-runs cprop/bitwidth privately.

## Stage 1 -- legalize acyclic repair (DONE 2026-10-06)

`make_acyclic` (pass/legalize/acyclic.cpp), step 1 of `legalize_design`. Since
2026-10-07 legalize runs EARLY in every `lhd compile` -- `lnast.tolg -> cprop ->
bitwidth -> legalize -> enableopt -> cprop -> bitwidth -> [satopt] -> formal ->
seal` -- so enableopt, satopt and pass.formal (and its hierarchy views) all see
an acyclic design; the seal at the end re-checks one driver per sink and
freezes (debug builds also re-scan that no later pass re-created a loop; the
full suite never did). Every graph-producing path runs it (Pyrope, slang, the
yosys reader, `lhd compile lg:`); LEC does not repair loops itself, and a raw
`lg:` written by a producer that skipped legalize is that producer's bug.
Per def, callee first, to a fixpoint (64 rounds):

1. **Dependence.** A vertex is a driver pin. Registers, graph inputs and
   constants cut; an async memory read depends on its own address/enable
   (write cones are sequenced after the read by every consumer); a
   body-less blackbox is a source. ATOMIC view: an instance output depends on
   every input. ARC view: the callee's port-level `port_reach` summary, closed
   over the carries for a rolled loop. Iterative Tarjan over the atomic view;
   each SCC through an instance is re-searched in the arc view.
2. **Repair.** Inline every non-loop instance on an arc-level SCC (warning)
   and every state-free (non-loop) instance on an atomic SCC. Rolled loops are
   never touched. The arc view is port-level first; inside each port-level
   SCC it is re-searched at BIT level (field selects resolved through
   wiring, instance outputs through a backward bit walk of the callee), so a
   packed-bus false loop is no cycle.
3. **What is left** gets `split_packed_cycle_slices`; anything still cyclic is
   a diag error (`comb-loop`, or `comb-loop-through-loop`), category "time"
   (exit 6).

`flatten_false_loop_subs` is no longer called by legalize or LEC.

Not done (and not needed so far): per-output-FIELD summaries and splitting
module ports into field ports; splitting a stateful child into STATE/COMB
defs instead of inlining it.

## Stage 2 -- delete every consumer-side copy

Inventory (2026-10-06), each removed only with its fixtures green with the
code gone:

| Area | Code |
|---|---|
| graph/split_selfref | `flatten_false_loop_subs`, `repair_{simulator,private}_packed_cycles`, `comb_emit_order` cut-Sub, `word_level_cycle_nodes` (keep only if a diagnostic needs it), the through-Sub crossbar in `split_packed_selfref_wire` |
| sim | color-plan port/packed slice walk + `occurrence_packed_footprint` (keep a perf-only lane binding only if measured), `sub_false_loop_output_pids`, `compact_loop_has_external_ring`, Moore/Mealy prebind deferral |
| LEC | `has_boundary_feedback`/`inline_boundary_feedback`, WORD-LEVEL CYCLE retry, encoder collapsed-box first-visit emission |
| Verilog | cut-Sub `always_comb` closing, cycle break nets |
| bitwidth | `comb_emit_order` re-run (pipeline order, ruling 5) |
| upass | tolg Time_checker per-output Sub split, `wire_cut_nids_`, roll planner `loop_output_rings_back` |
| frontend | review slang struct-port leaf-flattening done only to dodge rings |
| dead | `pass/color` `color_cgen` |

## Stage 3 -- stale comments

split_selfref.cpp `__settle_g<k>`, lhd_kernel_formal.cpp, comb_false_loop_sub
vtb, cgen_sim_comb_loop_test, inou/prp/BUILD notes, cgen/tests/comb_loop.

## Stage 4 -- cross-color constant propagation (ruling 6)

Partly done 2026-10-07: the simulator's `specialize_constants` is now the
shared `pass/specialize` (content-named `<callee>__k<hash>` specializations),
run by sim on its private library and by synthesis on the mapper's private
copy AFTER pass.color (`synth.specialize`, default true; state-free callees
only -- a stateful def keeps its identity for DFF picks, clock-gate mapping and
LEC pairing; only the nodes it creates inherit a neighbor's color; no full
refold). Measured with test.lib: dino and picorv32 identical with it on or off
(no constant-tied state-free instance survives their compile). An earlier
variant that refolded/recolored every def moved regions and broke the ICG
mapping. LEC does not specialize (it compares the original modules).
2026-10-07 (owner ruling): the hierarchical round shares ONE specialization
per definition (`max_versions`: 0 off, 1 = the join of all instance contexts
-- the default, N = per-binding clones capped at N). A specialized definition
is always a COPY `<name>__k<hash>` with every instance re-pointed; the
original is never edited (incremental reuse) and stays available (a sim
testbench may drive any module directly). Interfaces never change: a narrower
input is a Get_mask/Sext inside the copy (narrowing the DECLARATION broke the
sim emitter's lane reads -- lhd_sim_packed_child_input_test), a narrower child
output is a fit in the parent. Contexts so far: constant inputs every instance
agrees on, narrower input widths (widest instance), constant and narrower
outputs (into parents). Open: boundary cones (a parent Sum collapsing
with a child Sum: push the single-fanout driver cone into the copy, its
leaves as ports; the mirror for outputs), LEC with both sides specialized
alike (only where ref and impl agree), and the knob as a user option.

After coloring, run cprop+bitwidth once over the color partitions, passing
constants across colors (an instance input tied to a constant in its parent
becomes a constant inside the partition). All consumers (sim, synth, LEC) use
that result; delete `sim::specialize_constants`/`refold_private_body` and the
duplicated pipeline cprop+bitwidth round. Measured motivation: minion sim
setup spends 30% in specialize_constants (cprop 16%, bitwidth 14%).

## Gates

Full `bazel test //...` (the 2026-10-06 slicing-off census: only
`prp-sim-packed_bus_bit_ring` and one sim_color_plan unit test still rely on
consumer-side repair after the cprop fix), plus the lhdsuite harness (sim
speed, colors, incremental contract) and LEC suites.

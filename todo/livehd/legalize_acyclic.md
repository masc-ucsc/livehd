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
   atomic-only ring is split instead (ruling 5).
2. A **true** combinational loop (a bit depends on itself with no register,
   latch or registered memory read on the path) is a diag **error**.
3. A **false** loop (word/port granularity only: packed Get_mask/And/Or/Concat
   fields, or an instance output that does not depend on the instance input it
   feeds) is **removed** by legalize -- by inlining, by a loop split, or by
   rewiring packed slices inside a body (`split_packed_cycle_slices`). No
   module ports are split today. The warning names the instances involved
   ("combinational path loops through module port(s) ... ideally split the
   bus/module in the RTL").
4. Loops INSIDE one module are already removed at `lnast.tolg`
   (`split_packed_selfref_wire`); that stays.
5. **Sub-loops (compact loop subnodes) are never unrolled to fix a loop.**
   Keeping loops rolled through synthesis, simulation and LEC is a key
   contribution (cf. XLS `counted_for`). A ring through a loop instance is
   repaired by splitting the loop BODY into `__ring<ports>`/`__free` halves by
   carries (legalize's `split_loops` machinery), never by materializing
   iterations. A path that really goes through a loop instance (arc level) is
   no false ring: no split breaks it, so it is reported.
   The two current ring-dodging unrollers (cgen_sim
   `compact_loop_has_external_ring`, upass `loop_output_rings_back`) go away.
   A loop instance's arcs are its body summary CLOSED over the carries (from
   the second ordinal a carry-in holds the previous carry-out).
6. Prefer running the repair without bitwidth; if needed the order may be
   `bitwidth, legalize, cprop, bitwidth`.
7. cprop+bitwidth run ONCE per module, then once more over the coloring
   partitions (colors cross module boundaries), with constants passed across
   colors -- replacing sim's private `specialize_constants` re-runs. No pass
   re-runs cprop/bitwidth privately.

## Stage 1 -- legalize acyclic repair (DONE 2026-10-06)

`make_acyclic` (pass/legalize/acyclic.cpp), step 1 of `legalize_design`. It
runs LAST in every `lhd compile` (after pass.formal), again in the sim
pipeline, and in `lhd lec` on each side's own defs (an `lg:` side may come from
a producer that never legalized: `lhd pass abc|usyn|partition`, synth's net/,
an older binary). Per def, callee first, to a fixpoint (64 rounds):

1. **Dependence.** A vertex is a driver pin. Registers, graph inputs and
   constants cut; an async memory read depends on its own address/enable
   (write cones are sequenced after the read by every consumer); a
   body-less blackbox is a source. ATOMIC view: an instance output depends on
   every input. ARC view: the callee's port-level `port_reach` summary, closed
   over the carries for a rolled loop. Iterative Tarjan over the atomic view;
   each SCC through an instance is re-searched in the arc view.
2. **Repair.** Inline every non-loop instance on an arc-level SCC (warning)
   and every state-free instance on an atomic SCC; split every rolled loop on
   an atomic-only SCC by ring/free carries (`split_loop_by_ring`, never
   unrolled; carries that read each other share a half, and the half names
   carry the ring's ports).
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

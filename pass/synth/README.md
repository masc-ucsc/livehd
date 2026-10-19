# pass/synth — the shared synthesis pipeline

`pass/synth` is the backend-neutral part of LiveHD synthesis: everything
between a colored design and a mapped region, shared by every mapper. It
includes and links no ABC (abc_cleanup.md); ABC lives in
[`pass/abc`](../abc/README.md), which binds this pipeline to its backend as
`pass.abc`. Native unate synthesis ([`pass/usyn`](../usyn/README.md)) reuses
preparation, partitioning and logical translation with its own design driver. `docs/synthesis.html` describes the method; this file maps it to
code.

The shared library's dependency closure is ABC-free: its single-design formal
helpers use `//pass/lec:lec_encode`, which is separate from the relational query
engine and ABC cone services. The public USYN pass is also independent; the
full CLI has an [ABC-free build configuration](../../lhd/README.md#abc-free-build).
Remaining native synthesis work is tracked in [2u-usyn](../../todo/livehd/2u-usyn.html).

## Pipeline

1. **Private copy.** `design_prepare` copies the full definition closure and
   opaque callee interfaces into an owned private library, preserves root order,
   and prepares compact loops. Optional `Preparation_budget` admission counts
   each copied definition's source nodes once, charges ports/pins/edges and
   traversal work, and brackets bulk copies and loop operations with process/time
   checkpoints. Refusal is sticky and discards the private result. Native USYN
   supplies this budget; existing callers may omit it. This does not predict
   allocation bytes or interrupt an individual HHDS copy/helper operation.
   The native design driver also supplies optional partition admission for
   collection, naming and reconstruction, with forced checks around builders,
   commits and flatten/signature entry/completion. Flattening also admits each
   instance's context and connectivity walks; boundary signatures admit their
   internal node/pin/edge walks. A refused decomposition can
   leave partial bodies in its output library; the driver owns and discards that
   private library. Individual bulk helpers remain uninterruptible.
   Dead logic is dropped. Proof-backed simplification
   ([`pass/satopt`](../satopt/README.md)) already ran in the compile step when
   explicitly enabled (`pass.satopt` defaults off for every command); the mapper maps what
   compile produced.
   **State sweep** (`synth.state_sweep`, default false; `state_sweep.cpp`). On the
   private copy, after `pass.specialize` and before the loops are prepared, a
   register that no top output, no instance of a body-less cell (a Liberty cell,
   a macro), no memory and no attribute cell can observe is deleted with the cells
   behind it, and a register is kept only as far as its observed bits reach: the
   demand is a per-bit set per driver pin, pushed backwards through the bitwise
   cells, muxes, slices (`Get_mask`/`Set_mask`), concatenations, adds/multiplies
   (low prefix) and constant left shifts -- every other cell demands all its operand
   bits -- and across the instance hierarchy (a child output bit no instance reads
   demands nothing inside the child; a child input bit the body never reads demands
   nothing of its driver), to a fixpoint. A register whose highest observed bit is
   below its width is narrowed to that bit (its Q pin restamped unsigned); one with
   at least 8 unobserved bits below that bit becomes one single-bit register per
   observed bit, named `reg[i]` as the bus-expansion standard names the bit of a bus
   (so the mapped names, the LEC state pairing and the placement names stay those of
   the source), and a Concat rebuilds the narrow value its readers saw. Definitions
   instantiated by a compact loop are never touched. This is what Yosys gets from
   flattening and `opt_clean` bit-wise; without it LiveHD maps and places dead
   counters, unread CSR words and the unused upper half of wide primitive outputs
   (liteeth: 18% of the mapped cells). `lhd synth --stats`/the step log report what
   went (`state-swept`), `LIVEHD_STATE_SWEEP_DEBUG=1` dumps the per-definition
   output demand and `LIVEHD_STATE_SWEEP_TRACE=<register name part>` prints why a
   register stays observed. Covered by `//pass/synth:state_sweep_test` and, with an
   unbounded LEC proof of the mapped result, `//lhd/tests:lhd_state_sweep_test`.
2. **Modules.** `ware_module.cpp` extracts width-specialized arithmetic
   modules, `memory_module.cpp` lowers small or many-ported memories, and
   `loop_cleanup.cpp` prepares compact loops. An expanded iteration takes its
   instance's color and the cells the refold then creates (the constant-position
   slices and splices a loop's per-index bit writes fold to, an accumulation
   guard moved onto its term) take a colored neighbor's
   (`specialize::inherit_colors`), so no stray color-0 region cuts a per-bit
   state update off its register (ABC would see each unwritten bit as a fresh
   input to mux). A carried loop expanded for
   synthesis leaves one two-input cell per iteration (`acc |= term` is a
   left-deep Or chain) in colored cells, which cprop's associative flattening
   treats as boundaries. For `pass.abc`, `reassoc.cpp` then merges every
   single-consumer And/Or/Xor tree the expansion minted (never the source's
   own trees, never a mapped netlist re-prepared for `tmap`) that stays inside
   one region (same flat color, same per-instance `hier_color` on every path,
   same control-cone membership, no per-cell synth policy, no latch operand --
   an ICG keeps its `clk & latch`) into one n-ary cell, width-exact under the
   blaster's truncate/extend semantics, after first moving an expanded
   `if c { acc |= x }` guard onto its term (cprop's rule, for links that share
   a region); one O(V + E) pass. The blaster lowers an n-ary And/Or/Xor as a
   balanced pairwise tree. `pass.usyn` keeps the chains: its XAG flow measured
   neutral to worse on balanced loop chains (lhdtrack `br_arb_lru` +8% delay).
   `--set pass.abc.reassoc=false` hands ABC the chains as expanded (A/B
   runs; the region cache compares the prepared bodies, so each setting reuses
   only its own rows), and `--stats` reports what merged on the
   `pass.abc loops:` line. Two alternatives were measured on 34 lhdtrack
   designs (asap7, abc and usyn) and dropped: an arrival-aware n-ary
   reduction in the blaster (a wash for ABC, a small loss for usyn: RAW levels
   see only the region's own logic), and re-running the source's
   `pass.color` on the copy once carried loops are expanded (it closes the
   `br_mux_bin_array` / `br_apb_demux_select_onehot` delay gaps but loses as
   often elsewhere: `br_arb_lru` 273 -> 368 ps, `br_multi_xfer_distributor_rr`
   +65% area), as `compile.unroll=true` does.
   `bit_facts.cpp` answers "which bits of this value are provably 0" for the two
   module boundaries that would otherwise hide the answer from the mapper, both
   opt-in: `pass.abc.ware_zero_bits` clears, inside an extracted ware shifter, the
   data-operand bits and the shift-amount bits its parent drives with 0 (one live lane of a
   word-select vector; the five low bits of `idx * 32`), and `pass.abc.memory_prune_reads` clears at a lowered memory's output
   the read-port bits no reader observes, feeds the bits a parent drives with 0
   to the module as constants, and passes a write-enable lane vector that is one
   condition on a constant lane pattern (`cond * K`) as that one bit, so the
   module's lanes share one net instead of costing an enable product per lane.
   The mask cell takes the color of the logic it feeds (a different color is a
   different region, and the constant would not cross it).
   Explicit SRAM cells declared by the selected Liberty remain opaque `Sub`
   instances. They bypass native-array lowering; their physical geometry is
   supplied by the corresponding LEF. The library interface alone does not
   authorize replacing an inferred array with unspecified collision semantics.

3. **Regions.** `pass/partition` cuts the colored source into region bodies;
   `region_driver.cpp` (`Region_driver`) maps them: per-region options
   (`region_qor.hpp`), the region cache (`region_cache.cpp`), parallel lanes
   (`lane_scheduler.cpp`), memory admission, and the ware (architecture)
   trials. The cache keeps one IO decl per child name next to the cached
   pre-bodies. A child that keeps its name but changes its interface (a
   renamed or resized region boundary port) replaces that decl when a parent
   is stored, so the parent hits again on the next unchanged run. A row loaded
   from disk whose pre-body instantiates a replaced child was built for the old
   boundary: it never hits and is not persisted.
4. **Translation.** `region_blast.cpp` translates a region body into a RAW
   `Lnet` (`lnet.hpp`, built through `Lnet_ops` in `lnet_ops.hpp` with the
   operator lowering of `blast.hpp` and the adders and multipliers of
   `arith.hpp`), plus everything the read-back needs: crossing registers
   (`Seq_flop`), native boundaries, and the creation order of inputs and
   outputs.
   `clock_gates` recognizes latch-based gates independently of library cells.
   Hooks also receive an owned `source_state` snapshot through `Region_ctx`:
   state/control identities, source spans, reset/init semantics, bit/stage
   correspondence and structural memory/ICG barriers. Native demotion never
   removes a source row; a signed semantic D reference undoes QN-only encoding.
   The standalone target validator walks source definitions before partitioning
   and diagnoses unsupported DOMINO clocks/latches at their declaration sites.
   `Blast_options::logical_state` translates all ordinary register bits/stages
   independently of Liberty and `map_register`, retaining semantic D and native
   special-state barriers. Nonconstant clocks, asynchronous resets and initial
   values cross as protected output indices in the owned snapshot; constants
   retain their exact values, including unknown bits. This mode validates its
   target before translation and applies state/control admission bounds.
   The public USYN CMOS entry uses logical translation; whole-design DOMINO
   validation remains pending there. The mapped writer rejects logical-state input;
   `pass/usyn/logical_writer` supplies independent bit-level CMOS emission;
   `region_emit` reconnects packed ports and native structures. The callable
   `design_synth` CMOS driver connects these to private-copy preparation and
   children-first partitioning and is used by the public USYN pass.
5. **Mapping** is the backend's (`region_backend.hpp`, below).
   `tmap.hpp` also exposes an optional complete-design technology-mapping
   capability. Providers register by name; an unavailable provider returns an
   explicit status. The interface validates an owned output library and the
   top's port identities/widths/signs before returning it, and discards partial
   failed results. `pass/abc/abc_tmap.cpp` supplies the optional ABC provider;
   the shared target links no provider. Logical synthesis retains its own
   authoritative artifact independently of the mapped result.
   `tmap_cache` wraps the existing region cache with bounded snapshot integrity
   checks and atomic generation publication for optional mapping providers. It
   retains old generations for concurrent readers, uses scoped graph libraries,
   and propagates process/time refusal independently of ordinary cache I/O failure.
6. **Read-back.** `region_writer.cpp` writes the backend's `Cell_netlist`
   (`cell_netlist.hpp`) into the region body: cells, registers with their
   LEC-visible names, native boundaries, source attribution. A register
   split into one-bit cells names bit i `reg[i]` and a lowered memory entry
   i `<mem>._mem[i]` (bit b: `<mem>._mem[i][b]`), following the
   bus-expansion standard in `core/bus_name.hpp`; semdiff and pass/lec parse
   the same spelling back to pair the cells with the source register.

## The Lnet (`lnet.hpp`)

A flat k-LUT network owned by one region (or one proof): nodes numbered
densely in topological order, node 0 the constant 0, each a constant, a source
(an input or a latch output) or a LUT of up to 8 fanins whose function is a
truth table over its fanins (replicated 64-bit for up to 6 inputs, 2 or 4 words
for 7 or 8). There are no complemented edges. Inputs, latches and outputs are
boundary tables; a LUT may carry its SOP as side data.

- **RAW** (`Lnet_ops`): the blaster's translation as recorded -- lazily created
  constants, explicit inverters, 2-input AND/OR/XOR folding only constants and
  `x op x`, and every output's creation position -- so a backend replays it
  object for object (`Constants::eager` for regions, `lazy` for proofs).
- **STRASH** (`strash()`): sources in CI order, structurally hashed 2-input
  gates with complemented edges folded into their tables, dead logic dropped.
  The unate cover's source network.

## The backend seam (`region_backend.hpp`)

A `Region_backend` gives each parallel lane its own session (`lane()`,
`start`/`stop`), contributes its recipe to the region-cache key (`plan`),
maps one region (`map(ctx, blast, rewrite, qor)` → `Cell_netlist`), reports
its projected memory, and owns whole-design sizing (`refine`) and scoring
(`score`). `Region_ctx` carries what the driver resolved for the region and
the services a backend calls back (admission, refusals, the graph lock).
`Driver_options::region_hook` is called with the RAW Lnet before the backend
and may return a `Region_rewrite`: logic over the same boundary for the
backend's flow (`flow`) or for technology mapping only (`tmap`), plus
decision evidence stored with the cache row.

## Cache salts

`synth_salt` hashes this package and the passes a mapped region depends on
outside its backend (the graph library, memory RTL generators and importers,
cprop, enableopt, bitwidth, color, the DFF pick, formal, the partitioner).
`//pass/abc:abc_salt` folds it in with pass/abc, `MODULE.bazel` and the ABC
patch; `//pass/usyn:usyn_salt` folds in `synth_salt` directly, without ABC.
USYN keys its own persistent logical-region cache (`pass/usyn/logical_cache.cpp`,
gated by `lhd.incremental` and a named workdir) with `usyn_salt`; its optional
tmap sub-cache uses the region cache below. The region cache takes the
engine salt from its caller, so an edit here invalidates every backend's rows.

## Tests

`lnet_test` (Lnet, `Lnet_ops`, STRASH), `arith_test`, `lane_scheduler_test`,
`reassoc_test` (chain merging, guarded links, region/width/clock-gate/scope
guards) and `region_cache_test` are unit tests here (satopt's are in `pass/satopt`). The pipeline
end to end runs through `pass.abc` and `pass.usyn`: `//pass/abc:*`,
`//lhd/tests:lhd_abc_*` and the `prp-synth-*` fixtures of
`//inou/prp:integration`, each proved with `lhd lec`.

The shared blaster maps general truncated remainder (`%`) with the restoring
divider network, restoring the dividend's sign for signed operands. The existing
non-negative power-of-two case remains a mask shortcut. A zero divisor yields the
dividend for remainder (the SMT-LIB bvurem/bvsrem convention). LiveHD treats
division and remainder by zero as X (`pass/lec/encode.cpp`: value 0 with an
all-ones x-mask), so this is one legal refinement and `lhd lec` does not constrain
it. Runtime divisors and non-power-of-two constants are supported.

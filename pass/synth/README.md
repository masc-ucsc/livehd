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
2. **Modules.** `ware_module.cpp` extracts width-specialized arithmetic
   modules, `memory_module.cpp` lowers small or many-ported memories, and
   `loop_cleanup.cpp` prepares compact loops.
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

`lnet_test` (Lnet, `Lnet_ops`, STRASH), `arith_test`, `lane_scheduler_test`
and `region_cache_test` are unit tests here (satopt's are in `pass/satopt`). The pipeline
end to end runs through `pass.abc` and `pass.usyn`: `//pass/abc:*`,
`//lhd/tests:lhd_abc_*` and the `prp-synth-*` fixtures of
`//inou/prp:integration`, each proved with `lhd lec`.

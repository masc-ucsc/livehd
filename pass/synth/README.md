# pass/synth — the shared synthesis pipeline

`pass/synth` is the backend-neutral part of LiveHD synthesis: everything
between a colored design and a mapped region, shared by every mapper. It
includes and links no ABC (abc_cleanup.md); ABC lives in
[`pass/abc`](../abc/README.md), which binds this pipeline to its backend as
`pass.abc`, and unate synthesis ([`pass/usyn`](../usyn/README.md)) is a region
hook on it. `docs/synthesis.html` describes the method; this file maps it to
code.

## Pipeline

1. **Private copy.** The design is copied into a private library and dead
   logic is dropped. Proof-backed simplification
   ([`pass/satopt`](../satopt/README.md)) already ran in the compile step when
   enabled (`lhd synth` from a source turns it on); the mapper maps what
   compile produced.
2. **Modules.** `ware_module.cpp` extracts width-specialized arithmetic
   modules, `memory_module.cpp` lowers small or many-ported memories, and
   `loop_cleanup.cpp` prepares compact loops.
3. **Regions.** `pass/partition` cuts the colored source into region bodies;
   `region_driver.cpp` (`Region_driver`) maps them: per-region options
   (`region_qor.hpp`), the region cache (`region_cache.cpp`), parallel lanes
   (`lane_scheduler.cpp`), memory admission, and the ware (architecture)
   trials.
4. **Translation.** `region_blast.cpp` translates a region body into a RAW
   `Lnet` (`lnet.hpp`, built through `Lnet_ops` in `lnet_ops.hpp` with the
   operator lowering of `blast.hpp` and the adders and multipliers of
   `arith.hpp`), plus everything the read-back needs: crossing registers
   (`Seq_flop`), native boundaries, and the creation order of inputs and
   outputs.
5. **Mapping** is the backend's (`region_backend.hpp`, below).
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
patch; `//pass/usyn:usyn_salt` folds in `abc_salt`. The region cache takes the
engine salt from its caller, so an edit here invalidates every backend's rows.

## Tests

`lnet_test` (Lnet, `Lnet_ops`, STRASH), `arith_test`, `lane_scheduler_test`
and `region_cache_test` are unit tests here (satopt's are in `pass/satopt`). The pipeline
end to end runs through `pass.abc` and `pass.usyn`: `//pass/abc:*`,
`//lhd/tests:lhd_abc_*` and the `prp-synth-*` fixtures of
`//inou/prp:integration`, each proved with `lhd lec`.

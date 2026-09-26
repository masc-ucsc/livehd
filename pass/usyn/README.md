# pass/usyn — unate synthesis

`pass.usyn` covers every synthesis region with the fewest **domino gates**
(unate functions over dual-rail inputs; a static CMOS LUT where no domino gate
builds a function) and hands the cover to ABC for technology mapping. It is a
*region hook* on the shared synthesis pipeline of [`pass/synth`](../synth/README.md):
coloring, the private design copy, the cut into regions, the translation of a
region into an `Lnet`, the region cache and the read-back are the ones
`pass.abc` uses, and the mapping itself is `pass/abc`'s ABC backend. This
directory holds no ABC code.

```
lhd synth design.v --top top --set synth.mapper=usyn --set synth.liberty=cells.lib --workdir W
lhd pass usyn lg:colored --top top --set synth.liberty=cells.lib --emit-dir lg:net --workdir W
```

`synth.mapper=usyn` also selects the `usyn` coloring profile of
`pass.color synth` (register-to-register colors: no `stop_*` cuts, no control
groups, arithmetic inline, `flop_to_flop=true`). Every `pass.abc.*` (`abc.*`)
setting reaches `pass.usyn`; an explicit `pass.usyn.*` wins. Equivalence is
checked separately with `lhd lec`: synthesis proves nothing itself.

## Flow of one region (`usyn_region.cpp`)

1. **Source.** The region's RAW Lnet (the blaster's own gates) is rebuilt in
   STRASH form (`livehd::synth::strash`): the sources in CI order, structurally
   hashed 2-input gates with complemented edges folded into their tables, dead
   logic dropped. An XOR stays one node when one domino gate admits its
   4-literal, 2-series form. A region over `max_nodes` goes to the ABC flow.
2. **Cover** (`lut_cover.cpp`). Priority cuts (`cover_cuts` per node) with
   composed truth tables of at most `support` inputs; every cut's function is
   costed once (`function_cost`): a domino gate when some polarity has an exact
   minimum-literal SOP within `literals` factored literals and `series` literals
   per product (`unate.cpp`, `exact_form`), else a non-unate static LUT. A
   depth-optimal round sets required times -- an output some cover builds in
   `domino_levels` chained domino gates must be built so, and with
   `depth_slack >= 0` every deeper output within its minimum plus the slack --
   then area-flow and `recovery_rounds` exact-area rounds minimize the
   transistor proxy (`domino_overhead`, `cmos_factor`, `static_overhead`,
   `nonunate_penalty`). With `duplicate=false` a gate absorbs a multi-reader
   node only when all its readers are inside it. The cover is checked against
   the source by simulation; a mismatch is an internal error.
3. **Hand-off.** `cover_network` turns the cover into a coarse Lnet over the
   region's boundary (one LUT per gate, its minimum SOP as side data), returned
   as a `Region_rewrite`: `abc=opt` runs it through the full pass.abc
   flow, `abc=tmap` (default) technology-maps it only (`&nf`, then the sizing tail), and
   `abc=only` leaves the region to the ABC flow without a cover. The ABC
   backend builds the SOP network in the region's own PI/PO/latch skeleton
   (`pass/abc/abc_lnet.cpp`, `lnet_into_logic`).

A region the cover refuses -- a time or memory budget (`time_budget_ms`,
`memory_budget_mb`), the node limit, or a memory region under
`cover_memories=false` -- takes the ordinary ABC flow and says why.

## Options (`--set pass.usyn.<flag>=value`)

| Flag | Default | Meaning |
|---|---|---|
| `support` / `literals` / `series` | 6 / 16 / 4 | the limits of one domino gate |
| `domino_overhead`, `cmos_factor`, `static_overhead`, `nonunate_penalty` | 5, 2, 2, 2 | transistor proxy |
| `cover_cuts` | 12 | priority cuts per node |
| `domino_levels` | 2 | outputs that fit this many domino levels must use them |
| `depth_slack` | 0 | deeper outputs within their minimum depth plus this (-1: area only) |
| `duplicate` | false | allow logic replication inside gates |
| `cover_memories` | false | cover blasted memory regions too |
| `fanout_boundary` | 0 | a node with this many sinks is always a gate boundary |
| `recovery_rounds` | 2 | exact-area recovery sweeps |
| `max_nodes` | 2,000,000 | largest region the cover admits |
| `abc` | tmap | hand-off: `opt`, `tmap` or `only` |
| `ware_trials` | false | re-run the architecture trials through this mapper |
| `large_ge` | 0 | the ABC size tier stays off for the cover's hand-off |

The default preserves the USYN cover through technology mapping without
running full ABC optimization afterward. To reproduce the earlier dino
configuration (abc_cleanup.md step 8), also set `pass.usyn.abc=opt`.
`pass.usyn` maps one region at a time (`synth.threads` is ignored).

## Reports and reuse

`<qor>.usyn.json` (schema 4, kind `usyn`; `qor.usyn` in the `lhd synth`
envelope) records the recipe, the totals and one row per region: status
(`abc_opt`, `abc_tmap`, `abc_only`, `abc_fallback` with its reason), cover
statistics (domino and static gates, costs, gates by input count and series,
outputs by domino depth, replication) and the resources observed. Mapped QoR
(area, delay) is in the region rows of `qor.json`. `<qor>.provenance` archives
the invocation and its inputs (`provenance.cpp`, checked by
`check_provenance.py`).

Regions are reused through the shared region cache (`<workdir>/usyn_cache`,
`lhd.incremental`). A region's report row is stored as decision *evidence*
with its cache row (`evidence.cpp`, schema 2) and replayed into
`regions_reused` on a hit; a missing or damaged attachment forces a re-cover.
The hook's recipe (`lnet-cover-v7:...`) and the usyn code salt
(`usyn_salt`: this package plus the ABC salt) are part of the cache key.

## Files and tests

| File | Role |
|---|---|
| `pass_usyn.cpp` | the `pass.usyn` entry: options, the hook, reports, provenance |
| `usyn_region.cpp` | one region: STRASH source, cover, hand-off, report row |
| `lut_cover.cpp` | the cut-based cover and `cover_network` |
| `unate.cpp` | truth tables, exact minimum SOPs, algebraic factoring |
| `metrics.cpp`, `resource_budget.cpp`, `evidence.cpp`, `provenance.cpp` | report metrics, admission, cache evidence, provenance |
| `measure_synth.cpp`, `compare.py` | external measurement and ABC-vs-usyn comparison tools |

Unit tests cover the cover (`lut_cover_test`), the hook (`usyn_region_test`),
the forms (`unate_test`) and the report plumbing; `//lhd/tests:lhd_unate_smoke`
runs the modes end to end with `lhd lec`, and the `*_usyn` / `prp-synth-*-usyn`
twins run the mapper-agnostic flow tests and prove every synthesis fixture.

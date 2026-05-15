# HHDS Graph Migration — Implementation Plan

This plan covers the **graph-side** migration to HHDS (the LNAST side
is already complete — see `hhds_migration.md`). It replaces LiveHD's
hand-rolled `Lgraph` storage with `hhds::Graph`, and `Graph_library`
with `hhds::GraphLibrary`.

## Strategy pivot (2026-05-15) — new `graph/` directory + per-pass migration

After landing Phase G3 reader/payload migration to shadow and most of
Phase G6 deletions (see history below), the in-place migration of the
`lgraph/` wrapper layer hit two structural walls:

1. The `Sub_node` ↔ `hhds::GraphIO` mirror needed bits / sign / position
   fields HHDS did not have. Patching HHDS unblocked it, but each
   subsequent reader (e.g., `Node_pin::get_name`) required threading
   migration plumbing through the dual-write Lgraph wrapper.
2. The `Node` / `Node_pin` / `XEdge` storage rewrite (Phase G4) is
   atomic by nature — flipping `Index_id` storage to HHDS handles
   means touching ~100+ sites simultaneously, with the Compact /
   Compact_flat / Compact_class serialization keys depending on
   `Index_id`. Doing it incrementally is hard.

**New direction**: stop migrating `lgraph/` in place. Build a small new
`graph/` directory with just the LiveHD bits that ride on top of
`hhds::Graph`, and migrate each pass directly to use
`hhds::Graph` + `hhds::Node_class` + `hhds::Pin_class` +
`livehd::attrs::*`. Once the priority passes (inou/prp, inou/yosys,
inou/cgen, pass/bitwidth, pass/cprop) are migrated, `lgraph/`
disappears from the build and gets deleted wholesale. Non-priority
passes (label, locator, opentimer, abc, submatch, sample, prp_writer)
can be migrated later or retired with their `//lgraph` dep.

`graph/` contents (landed):
- `graph/cell.{cpp,hpp}` — `Ntype_op` + cell-type metadata (sink/driver
  names, loop_first/loop_last, multi-driver/sink predicates). HHDS
  `NodeEntry::type` carries the encoded value.
- `graph/ann_place.hpp` — `Ann_place` value type used by
  `livehd::attrs::place`.
- `graph/attrs.hpp` — every LiveHD per-node / per-pin attribute tag
  backed by `hhds::flat_storage`: `bits`, `pin_offset`, `pin_name`,
  `pin_delay`, `pin_unsigned`, `color`, `place`, `loc`, `source`,
  `subid`, `const_value`, `lut`. Pre-registered at static-init in
  `graph/cell.cpp` (avoids the registry race documented in
  `hhds_migration.md §3.1`).
- `graph/BUILD` — `cc_library "graph"`, deps `//core` +
  `@hhds//hhds:graph` (notably **not** `//lgraph`).

## Baseline

- **Current (2026-05-15):** 215 pass / 11 fail / 1 skipped. The 11
  failures are all `inou/prp` constprop-verifier regressions documented
  in `hhds_migration.md §8.4`. No previously-passing test has regressed
  since the migration began.
- Historical (2026-05-14): 201 pass / 27 fail / 1 skipped. The
  improvement to 215/11 comes from deleting fixme-tagged tests, dead
  test stubs, and obsolete `inou/slang` paths along the way; no test
  that was passing then has stopped passing.
- `lgraph/` directory: still ~12k LOC; remains in tree but slated for
  deletion once the priority passes are off it.

## Per-pass migration status (priority list)

| Pass | LOC | Lgraph touchpoints | Status |
| --- | --- | --- | --- |
| `inou/prp` | 3666 | 0 in BUILD | DONE (was never coupled — produces LNAST only). Dead `do_work`/`to_lgraph` decls removed. |
| `inou/cgen` | 1226 | ~50 method calls | **PENDING** — start here next session. |
| `inou/yosys` | 5251 | ~322 | PENDING. User expects this to be a mostly-mechanical create-side migration. |
| `pass/bitwidth` | 2241 | ~55 | PENDING. |
| `pass/cprop` | 2845 | ~116 | PENDING. |

After the five priority passes are migrated, the gating tests
(`inou/yosys:all` + `inou/prp:all`) run on the new infrastructure;
non-priority passes follow.

## API mapping (lgraph → hhds)

| LiveHD type / op            | HHDS equivalent                                |
|-----------------------------|------------------------------------------------|
| `Lg_type_id`                | `hhds::Gid`                                    |
| `Index_id`                  | `hhds::Nid` / `hhds::Pid` (split node vs pin)  |
| `Hierarchy_index`           | `hhds::Tree_pos` (per-instance token)          |
| `Lgraph`                    | `hhds::Graph` (held by `std::shared_ptr`)     |
| `Graph_library`             | `hhds::GraphLibrary`                           |
| `Sub_node`                  | `hhds::GraphIO`                                |
| `Node`                      | `hhds::Node_class`                             |
| `Node_pin`                  | `hhds::Pin_class` (driver/sink folded into bit) |
| `XEdge`                     | `hhds::Edge_class`                             |
| `Fast_edge_iterator`        | `Graph::fast_class()` / `fast_flat()` / `fast_hier()` |
| `Fwd_edge_iterator`         | `Graph::forward_class()` (+ `forward_flat` / `forward_hier`) |
| `Bwd_edge_iterator`         | `Graph::backward_class()` (+ flat/hier variants) |
| `Hierarchy`                 | `Graph::hier_range()` + `Hier_instance`        |
| `Lgraph_attributes`         | per-node `Attr_host` tags via `flat_storage`   |
| HIF persistence             | `GraphLibrary::save()` / `load()`              |

## Identified HHDS API gaps (to patch upstream in `../hhds`)

These are gaps from a first scan; refine during implementation:

1. **Driver vs Sink pin semantics.** LiveHD pins carry an explicit
   driver/sink polarity. HHDS `Pin_class` encodes this in the low
   bit of `pin_pid` (`pin_pid & 2`). Need helpers `is_driver()` /
   `is_sink()` on `Pin_class`, plus a way to *flip* a pin to its
   counterpart, since LiveHD has many call sites that ask "give me
   the sink pin paired with this driver pin on the same port".
2. **Cell-type enum (`Ntype_op`).** LiveHD has a 100+ entry op enum
   (`Sum`, `Mult`, `Mux`, `Memory`, `Sub`, …) stored in `Type`. HHDS
   `Type` is generic `uint16_t`. Confirmed compatible — just hand
   `Ntype_op` values to `set_type()`.
3. **`Bits_t` per pin.** LiveHD stores bitwidth in
   `Lgraph_attributes::pin_bits[]`. Becomes an HHDS pin attribute
   tag (`livehd::attrs::bits`).
4. **Constant value storage.** `Lgraph::create_node_const(Const)`
   currently stores a `Const` (Dlop) on a special const node.
   In HHDS: use `create_constant()` which returns a `Pin_class` on
   `CONST_NODE`, attach the `Const` via a pin attribute tag.
5. **Lut-table storage.** Similar to const — pin attribute.
6. **Graph-IO bits + position metadata.** `Sub_node::IO_pin` carries
   `bits`, `graph_io_pos`, `instance_pid`. HHDS `GraphIO` carries
   only `name` + `Port_id` + `loop_last`. Need either a side-map or
   extend `GraphIO` to carry `bits`. Plan: side-map per
   `(gid, port_id) → bits` on `Graph_library` wrapper, since `bits`
   is per-instance not per-IO-decl.
7. **Hierarchy_index → Tree_pos.** `Hierarchy_index` is a signed
   int32 dense ID; `Tree_pos` is the HHDS structure-tree position
   token. Direct cast works for the live `hier_pos` field.
   Persistence of pre-resolved `Hierarchy_index` paths (currently
   stored in HIF) must be re-derived from `hier_range()`.
8. **Order-preserving edge iteration.** LiveHD provides
   `out_edges_ordered` (driver-pin port-id sorted). HHDS edge
   iteration order is currently overflow-set / inline-buffer order
   — needs documenting (and possibly sorting) for codegen
   determinism.

## Phased plan (sub-tasks)

Each phase MUST end with `bazel test //... --keep_going` passing the
201 baseline tests. **No phase merges if it regresses any of them.**

### Phase G0 — preparatory cleanup (LANDABLE STANDALONE)

- [x] `lgraph/BUILD`: depend on `@hhds//hhds:graph`. (LANDED)
- [x] `lgraph/lgraph_attrs.hpp`: define LiveHD per-pin attribute tags
      (`livehd::attrs::bits`, `pin_name`, `sign`, `instance_pid`,
      `color`) for Phase G3+ use. (LANDED)
- [ ] Audit and document every `Lgraph::` public method that lacks
      a 1:1 HHDS analogue. Update the "API gaps" section above.

### Phase G1 — `Sub_node` mirrored over `hhds::GraphIO` (LANDED — write path)

Status: write-path mirror landed across **all** creation paths
(`add_pin`, `del_pin`, `reset_pins`, `from_json` via `reload_int`).
Reads consult HHDS first for the methods listed in Phase G2 below.

### Phase G2 — Read migration (IN PROGRESS)

- [x] `Sub_node::has_pin(name)` — consults `hhds_io_->has_input/has_output`
  first; falls back to legacy. (LANDED)
- [x] `Sub_node::is_input(name)` — consults `hhds_io_->has_input` first.
  (LANDED)
- [x] `Sub_node::is_output(name)` — consults `hhds_io_->has_output` first.
  (LANDED)
- [x] `Sub_node::get_instance_pid(name)` — uses
  `hhds_io_->get_input_port_id` / `get_output_port_id`. (LANDED)
- [x] `Sub_node::has_instance_pin(instance_pid)` — uses upstream
  HHDS helper `has_pin_with_port_id`. (LANDED)
- [x] `Sub_node::is_input_from_instance_pid` /
  `is_output_from_instance_pid` — use upstream HHDS helpers
  `has_input_with_port_id` / `has_output_with_port_id`. (LANDED)
- HHDS upstream (`../hhds`): added
  `GraphIO::has_pin_with_port_id` / `has_input_with_port_id` /
  `has_output_with_port_id` to support these reads. (LANDED)
- [ ] `get_io_pos(name)` / `has_graph_pos_pin(pos)` /
  `is_input_from_graph_pos(pos)` / `is_output_from_graph_pos(pos)`
  / `get_name_from_graph_pos` — block on HHDS not tracking
  `graph_io_pos` (Verilog-style positional ordering). Add a LiveHD
  side-map keyed on `(GraphIO*, port_id) → graph_pos`, or extend
  HHDS to carry a per-pin positional index.
- [ ] `get_io_pin_from_*` / `get_pin` / `get_graph_input_io_pin`
  / `get_graph_output_io_pin` — block on HHDS not carrying `bits`.
  Migrating requires moving `bits` to `livehd::attrs::bits`
  per-pin attribute and reshaping these to return a synthetic
  view or breaking the API to take separate accessors.
- [ ] Once *all* live reads come from HHDS, drop `io_pins[]`,
  `name2id`, and `graph_pos2instance_pid` from Sub_node.

1. [x] `Graph_library` holds `mutable hhds::GraphLibrary hhds_lib_`
   and `absl::flat_hash_map<Lg_type_id::type, std::shared_ptr<hhds::GraphIO>>
   lgid_to_hhds_io_`. (LANDED)
2. [x] `Sub_node::set_hhds_io(hhds::GraphIO*)` setter; called from
   `Graph_library::add_name_int` / `rename_name_int` to point each
   Sub_node at its paired GraphIO; nulled before `expunge_int` /
   `rename_name_int` destroys the old GraphIO. (LANDED)
3. [x] `Sub_node::add_pin` / `del_pin` / `reset_pins` mirror to
   `hhds_io_`. The mirror uses `hhds::GraphIO::reset_declarations()`
   (not `clear()`) to wipe declared pins without tombstoning the
   GraphIO. (LANDED)
4. [x] Root-caused the previous corruption: `GraphIO::clear()` calls
   `delete_graphio(self)` — it destroys the GraphIO entirely, which
   invalidated the paired Graph too. Fix: added
   `hhds::GraphIO::reset_declarations()` upstream in `../hhds`.
   `Sub_node::reset_pins` now calls that instead. (LANDED)
5. [ ] Migrate `Sub_node::has_pin` / `get_pin_bits` etc. to read
   from `GraphIO` (with `bits` side-map for the LiveHD-only
   field). Requires step 4.
6. [ ] Once all Sub_node read paths are GraphIO-backed, delete the
   Sub_node `io_pins` vector and `name2id` map.

### Phase G2 — `Graph_library` IO-decl registry uses `hhds::GraphLibrary`

1. Replace the `name2id` / `id2name` maps with
   `hhds::GraphLibrary::find_io` / `create_io`.
2. Keep HIF-backed graph-body load for now (Phase G3).
3. Persistence: continue to write the JSON `library.json`; HHDS
   library directory created alongside.

### Phase G3 — `Lgraph` body storage uses `hhds::Graph` (SHADOW LANDED)

This is the biggest single change. Strategy:

1. [x] `Lgraph` holds `std::shared_ptr<hhds::Graph> hhds_graph_` as
   shadow storage. Materialized at ctor time from the paired
   `hhds::GraphIO::create_graph()`. Dormant — `node_internal[]`
   remains the authoritative source. (LANDED)
2. [x] `Lgraph` carries `idx_to_hhds_nid_`:
   `absl::flat_hash_map<Index_id, hhds::Class_index>`. (LANDED)
3. [x] `Lgraph::create_node()` (no-op variant) mirrors to
   `hhds_graph_->create_node()` and records the mapping. (LANDED)
4. [x] `Lgraph::create_node(Ntype_op)` mirrors node creation +
   `set_type` (with the bit-0 shift to preserve HHDS's
   `is_loop_last` semantics). (LANDED)
5. [x] `Lgraph::create_node_const`, `create_node_lut`,
   `create_node_sub` mirror node creation and cell-type. Const
   value / LUT table / sub-graph reference itself is not yet
   mirrored (deferred until readers depend on it through
   `livehd::attrs::const_value` and a Forest-model `set_subnode`).
   (LANDED)
7. [x] `Lgraph::add_edge(dpin, spin)` — mirrors edges to `hhds_graph_`.
   Internal-to-internal edges use
   `Node_class::create_{driver,sink}_pin(port_id)` (HHDS does
   find-or-create, so idempotent). Graph-IO endpoints
   (`Hardcoded_input_nid`/`Hardcoded_output_nid`) resolve via the
   Sub_node `instance_pid → name` table and
   `hhds_graph_->get_{input,output}_pin(name)`. (LANDED)
8. [x] `Lgraph::del_edge(dpin, spin)` — counterpart mirror, same
   endpoint resolution; uses `Pin_class::del_sink(driver_pin)` and
   `get_{driver,sink}_pin(port_id)` (which return invalid pin when
   the pin was never materialized — those branches are silently
   skipped). (LANDED)
9. HHDS upstream patch (`../hhds/hhds/graph.cpp`): relaxed
   `Graph::erase_declared_io_pin` assertion that required IO pins
   to be edge-free before erasure. `delete_pin` (called immediately
   after) already handles edge teardown correctly, and
   `GraphIO::reset_declarations` legitimately wipes declared pins
   wholesale during LiveHD's `clear_int` flow (test rebuilds reuse
   the same lgraph across cases). (LANDED)
10. [x] `Lgraph::del_node(node)` — mirrors to `hhds_graph_` by
    calling `Node_class::del_node()` on the paired HHDS node, which
    cascade-removes all incident pins and edges. Also removes the
    entry from `idx_to_hhds_nid_`. (LANDED)
11. [x] `Lgraph::del_pin(pin)` — fully covered by the existing
    mirrors: graph-IO pins go through `Sub_node::del_pin →
    hhds_io_->delete_input/output` (Phase G1.2), and other pin
    deletions just iterate `del_edge` which is now mirrored. No
    explicit del_pin mirror needed. (LANDED)

12. HHDS upstream additions (`../hhds/hhds/graph.{hpp,cpp}`): added
    `Node_class::has_out_edges()` / `has_inp_edges()` — cheap
    boolean predicates that avoid materializing the full edge
    vector. Intended for LiveHD `Lgraph::has_outputs` /
    `has_inputs` hot paths once the shadow is trustworthy on every
    graph (see step 13). (LANDED)

13. **Read-path switch (LANDED).** Root cause of earlier
    "shadow=false / legacy=true" divergence was in the HHDS
    upstream `Node_class::has_out_edges()` / `has_inp_edges()`
    naive impl — it walked `graph_->get_pins(node)` which only
    enumerates the pin linked list and skips port 0 (the
    node-as-pin, whose edges live on `NodeEntry` itself). Since
    most LiveHD nodes carry port-0 edges (single driver/sink),
    the predicate returned `false` for fully-connected nodes.
    Fixed upstream (`../hhds/hhds/graph.cpp`) to scan node-entry
    edges + walk pin list, mirroring `Graph::out_edges(Node_class)`.

14. [x] **Reader migration batch 1 (LANDED).** Migrated to consult
    `hhds_graph_` for non-IO nodes tracked in `idx_to_hhds_nid_`:
    - `has_outputs(const Node&)` / `has_inputs(const Node&)`
    - `has_outputs(const Node_pin&)` / `has_inputs(const Node_pin&)`
    - `get_num_out_edges(const Node&)` / `get_num_inp_edges(const Node&)`
    - `get_num_edges(const Node&)`
    - `get_num_out_edges(const Node_pin&)` / `get_num_inp_edges(const Node_pin&)`
    All fall back to legacy walk for Hardcoded_input/output_nid or
    shadow misses. Confirmed 219/231 baseline preserved.

15. [ ] **Reader migration batch 2** — `out_edges` / `inp_edges` /
    `out_connected_pins` / `inp_connected_pins` / `out_sinks` /
    `inp_drivers` iterators. These return `XEdge_iterator` /
    `Node_pin_iterator` (vectors of LiveHD-style handles). Cannot
    switch underlying storage until `XEdge` / `Node_pin` are
    rewritten as HHDS-handle wrappers (Phase G4).
6. [x] **Set-type mirror gotcha**: HHDS encodes `is_loop_last()`
   in bit 0 of `NodeEntry::type`. Storing `Ntype_op` raw values
   triggers heap corruption (`tcache_thread_shutdown()`) when
   downstream iteration treats those nodes as loop-last and
   mishandles them. Mitigation: shift `Ntype_op` left by 1 before
   storing (`hnode.set_type(static_cast<Type>(op) << 1)`).
   Decoding goes through a future Lgraph helper that masks/shifts
   back. (LANDED — workaround. A cleaner long-term option is a
   HHDS `set_user_type` API that hides the encoding from callers.)

16. [x] **set_type mirror plug (LANDED).** Centralized the cell-type
    mirror in `Lgraph::mirror_set_type_hhds(nid, op)` and threaded it
    through every external mutator: `Node::set_type`,
    `Node::set_type(op, bits)`, `Node::set_type_sub`,
    `Node::set_type_const`, `Node::set_type_lut`, plus the
    name-keyed `Lgraph::create_node_sub(string_view)` (which
    previously skipped the mirror entirely). The four
    `create_node*` paths also funnel through the helper now,
    deduping the inline mirror blocks. **Why this matters:** the
    yosys-to-lgraph builder and cprop both call `node.set_type(op)`
    after creation; without this fix, `get_type_op` could not be
    migrated to read the HHDS shadow because the shadow's type
    would diverge from `node_internal[]` whenever a node was
    retyped post-creation (e.g. cprop morphing `Sum` → `AttrSet`).

17. [x] **Reader migration batch 3 — cell-type readers (LANDED).**
    `Lgraph::get_type_op(Index_id)` and `Lgraph::is_type_const(Index_id)`
    now override the `Lgraph_attributes` inlines and consult the shadow
    first via `idx_to_hhds_nid_`. Reads invert the bit-0 shift applied
    by `mirror_set_type_hhds`. Falls back to legacy `node_internal[]`
    for shadow misses (graph-IO pseudo-nodes, non-master Index_ids).
    All live callers go through `current_g->get_type_op(nid)` where
    `current_g` is `Lgraph*`, so name resolution picks up the override
    via `Node`, `Node_pin`, etc. (all friends of Lgraph). `Lgraph::is_sub`
    routed through the same override.

18. [x] **Cell-type payload mirrors (LANDED).** Three payload mirrors
    added for the Sub/Nconst/LUT cell triple, each a node-level HHDS
    attribute defined in `lgraph_attrs.hpp`:

    | LiveHD payload | HHDS attribute (per-node) |
    | --- | --- |
    | `Lg_type_id` (subid_map) | `livehd::attrs::subid` (uint32) |
    | `Const` (const_map, serialized) | `livehd::attrs::const_value` (std::string) |
    | `Const` (lut_map, serialized) | `livehd::attrs::lut` (std::string) |

    All tags pre-registered at static-init in `lgraph.cpp` (same
    pattern as `lnast/lnast.cpp`, avoiding the registry race). Three
    new `Lgraph::mirror_set_{subid,const,lut}_hhds(nid, …)` helpers
    threaded through `Node::set_type_sub`, `Node::set_type_const`,
    `Node::set_type_lut`, plus all `create_node_sub` /
    `create_node_const` / `create_node_lut` paths. Reader overrides
    `Lgraph::get_type_sub`, `Lgraph::get_type_const`,
    `Lgraph::get_type_lut` consult the shadow attribute first and
    fall back to the legacy `subid_map` / `const_map` / `lut_map` via
    the base impl on shadow miss.

    **Why this matters:** removes the last cell-type readers that
    were forced to go through Lgraph_attributes side maps. Once all
    consumers funnel through the Lgraph* overrides (most already do
    via `Node`/`Node_pin`), the legacy maps become dead weight and
    can be retired in Phase G6.

20. [x] **Per-pin sign mirror + is_unsign reader (LANDED).** Pin sign
    is a small boolean (unsigned == 1, signed == 0). New helper
    `mirror_set_pin_sign_hhds(nid_master, pid, unsigned_flag)` calls
    `attr().set(1)` on set_unsign / `attr().del()` on set_sign so the
    absence-of-attribute case matches the signed default. Threaded
    through `Node_pin::set_unsign`/`set_sign`. `Node_pin::is_unsign`
    consults shadow first, falls back to the legacy
    `node_pin_unsigned_map` on shadow miss.

21. [x] **Node color mirror + get_color/has_color (LANDED).** Color
    is a small int taint used by pass diagnostics. New
    `mirror_set_color_hhds`/`mirror_del_color_hhds` helpers attach
    `livehd::attrs::color` to the HHDS node. `Node::get_color` and
    `Node::has_color` consult shadow first.

19. [x] **Per-pin bits mirror + Node_pin::get_bits migration (LANDED).**
    Bits are the most pervasive per-pin attribute. The
    `livehd::attrs::bits` tag (defined in `lgraph_attrs.hpp` from
    Phase G0) is now actively used:
    - New `Lgraph::mirror_set_pin_bits_hhds(nid_master, pid, bits)`
      helper attaches bits to the HHDS driver pin via
      `create_driver_pin(port_id)` (find-or-create), so the mirror
      works even before `add_edge` materializes the pin in HHDS.
    - Threaded through `Node_pin::set_bits`, `Node_pin::set_size`,
      `Lgraph::create_node_const` (port 0 driver), and
      `Node::set_type_const` (retypes that re-stamp bits).
    - `Node_pin::get_bits` now consults the per-pin attr first, falls
      back to the legacy `node_internal[]` bits on miss.
    - Skipped: graph-IO pseudo-node bits (those live on
      `Sub_node`/`GraphIO`, not on `idx_to_hhds_nid_`-tracked nodes).
    Confirmed 219/11/1 baseline preserved.
3. [ ] Mirror `Lgraph::add_edge(dpin, spin)` to
   `Graph::add_edge(driver_pin, sink_pin)`.
4. [ ] Per-pin Bits/Const/Lut tables become HHDS pin attributes via
   `livehd::attrs::bits`, `livehd::attrs::const_value`, …
5. [ ] Switch read paths (fast/forward/backward iterators,
   out_edges/inp_edges/get_driver_pin/get_sink_pin) over to
   `hhds_graph_`. Then drop `node_internal[]` and `Index_id`.
6. [ ] Replace `Index_id` alias with `hhds::Nid` once no reader
   uses positional indexing. Special-cased IO/CONST nids map to
   HHDS `Graph::INPUT_NODE` / `OUTPUT_NODE` / `CONST_NODE`.

### Phase G4 — `Node`, `Node_pin`, `XEdge` become thin wrappers

**Phase G4 prep (LANDED):**
- `Node::get_hhds_node()` — returns the paired `hhds::Node_class` via
  the shadow lookup. Invalid for IO pseudo-nodes / shadow misses.
- `Node_pin::get_hhds_pin()` — returns the paired `hhds::Pin_class`
  via `get_driver_pin(port_id)` / `get_sink_pin(port_id)` on the
  owning Node_class. Invalid for the same misses, plus pins never
  materialized on the shadow. Required adding `#include
  "hhds/graph.hpp"` to `node_pin.hpp` so the return type is visible.

These accessors let consumer code migrate incrementally — read HHDS
attributes / edge sets when a valid handle is returned, fall back to
legacy paths otherwise — without rewriting Node/Node_pin storage
layout in one atomic surgery.


1. `Node` holds `hhds::Node_class node_` instead of `Index_id`.
2. `Node_pin` holds `hhds::Pin_class pin_` instead of
   `(Index_id, Port_ID, sink_or_driver)`.
3. `XEdge` holds `hhds::Edge_class` (or just driver+sink pair).
4. All edge iterators delegate to `Graph::fast_class()` etc.
5. Hierarchy_index stays as an int32 but is computed from
   `Node_class::get_hier_pos()` for hier-context handles.

### Phase G5 — Update consumers (124 files)

Mostly mechanical: every site that does
`node_internal[idx]` or holds an `Index_id` longer-term needs to
shift to value-handle storage (HHDS `Node_class` doesn't have
positional `idx`). Side-map convention applies (`Class_index` key).

Estimated touch count by directory:
- `inou/yosys/`: large — Yosys-to-LGraph builder
- `pass/cprop/`, `pass/bitwidth/`: large
- `inou/cgen/`: medium
- everywhere else: small/mechanical

### Phase G6 — Delete obsolete code

Delete (per goal):
- [x] `lgraph/tests/iter_test.cpp` — was `tags = ["fixme"]`, didn't
  count toward the 201/27 baseline. Deleted in foundation session;
  build clean, tests still 201/27.
- [ ] `lgraph/graph_library.cpp` body code (registry impl) — blocked
  on G2/G4.
- [ ] `lgraph/lgedge.cpp`, `lgraph/lgedge.hpp` — blocked on G3 storage
  switch and G4 Node/Node_pin/XEdge wrapper rewrite.
- [ ] `lgraph/lgedgeiter.cpp`, `lgraph/lgedgeiter.hpp` — blocked on G3
  iterator switch.
- [ ] `lgraph/lgraphbase.{cpp,hpp}` storage internals — blocked on G3.
- [ ] Significant portions of `lgraph/lgraph.cpp` (now delegating) —
  blocked on G3+G4.
- (see Phase G6 list below — graph_bench.cpp DELETED.)
- [x] `lgraph/tests/edge_test.cpp` (exercises lgedgeiter / lgedge
  mechanics) — DELETED. User confirmed 2026-05-14: no need to preserve
  API compatibility for this test during migration. Removed in
  Phase G6 partial deletion (2026-05-15).
- [x] `lgraph/tests/lgtuple_test.cpp` (lgtuple utility — depends on
  the legacy create_node_const + node-pin model) — DELETED. User
  confirmed 2026-05-14: deletable after migration. The `lgtuple.cpp`
  / `lgtuple.hpp` library itself remains (still used by passes).
  `lgraph_test` and `node_test` kept per the user's note that they
  "may be" relevant.
- [x] `lgraph/tests/graph_bench.cpp` (side-by-side lgraph vs HHDS
  micro-benchmark) — DELETED. The user's goal text explicitly calls
  this out as obsolete ("testing HHDS vs HHDS").
- [ ] Drop `@hif//hif` from `lgraph/BUILD`. Drop `@hif`
  `http_archive` from `MODULE.bazel` once `inou/json` (which uses
  rapidjson, not hif) is the only consumer left — verify.

## Validation gate

Each phase merge must show:

```
$ bazel test //... --keep_going --test_summary=terse 2>&1 | tail -3
Executed N out of N tests: 201 tests pass, 27 fail, 1 skipped
```

Where the 27 failures are exactly the baseline set.

## Notes for future sessions

This plan was created while completing **task 1a** of the Pyrope
punch list in `TODO.md`.

### Entry point — start here

The Phased plan below (G0-G6) is historical: most of G3 landed before
the strategy pivot, and parts of G6 were done eagerly. The current
working plan is the **per-pass migration list** at the top of this
document. To pick up:

1. **Run baseline first.** Expect 215 pass / 11 fail / 1 skipped. The
   11 are the known `inou/prp` constprop-verifier regressions
   (`hhds_migration.md §8.4`). No other test should fail.

2. **Pick the next un-migrated priority pass.** Start with `inou/cgen`
   (smallest, 1226 LOC, ~50 touchpoints, pure consumer that emits
   Verilog). Its BUILD is in `inou/cgen/BUILD`; the main file is
   `inou/cgen/cgen_verilog.cpp`.

3. **Migration template (per pass):**
   - Replace `Lgraph* lg` parameters with `hhds::Graph*` (or
     `std::shared_ptr<hhds::Graph>`).
   - `lg->fast()` → `graph.forward_class()` (or `fast_class()` if
     order doesn't matter).
   - `node.get_type_op()` → `Ntype_op{static_cast<uint16_t>(node.get_type()) >> 1}`
     (invert the bit-0 shift LiveHD's mirror applies — see
     `Lgraph::mirror_set_type_hhds` for the encoding).
   - `node.setup_driver_pin_raw(pid)` →
     `node.create_driver_pin(pid)` (HHDS find-or-create).
   - `node.get_type_sub_node()` → look up GraphIO via
     `graph_lib.find_io(name)` or
     `graph.get_io_for_subnode(node.get_subnode())`.
   - `dpin.get_top_lgraph()->get_self_sub_node()` →
     `graph.get_io()` (returns `shared_ptr<hhds::GraphIO>`).
   - `dpin.get_bits()` →
     `dpin.attr(livehd::attrs::bits).has() ? dpin.attr(livehd::attrs::bits).get() : 0`.
   - `node.set_color(c)` → `node.attr(livehd::attrs::color).set(c)`.
   - Other per-pin / per-node attrs: `livehd::attrs::pin_name`,
     `pin_offset`, `pin_delay`, `pin_unsigned`, `place`, `loc`,
     `source`, `subid`, `const_value`, `lut`. All defined in
     `graph/attrs.hpp`.
   - For Verilog positional argument order, use HHDS `port_id` (it
     is the unified instance_pid + graph_io_pos per user direction
     2026-05-15).

4. **HHDS API gaps from earlier scans** — patch upstream in `../hhds`,
   not locally. Currently identified:
   - `hhds::Node_class::find_or_create_pin(name)` — cleaner than
     `resolve_driver_port(name)` + `find_or_create_pin(port_id)`.
     User specifically called this out as a candidate addition.
   - PinEntry/NodeEntry native `bits` (24 bits) + `sign` (1 bit) —
     layout overhaul documented in `TODO.md`. Drops the
     `livehd::attrs::bits` / `livehd::attrs::pin_unsigned`
     flat_storage attrs in favor of indexed loads. Its own multi-commit
     campaign; do *after* the per-pass migrations are done so the
     attribute-tag side is well-validated.

5. **Tests:**
   - `bazel test //inou/yosys/... //inou/prp/... --keep_going` for the
     gating set.
   - Each per-pass commit should preserve 215 pass / 11 fail / 1
     skipped. The single skipped test has been stable through the
     entire migration.

6. **Once all five priority passes are migrated:**
   - Drop `//lgraph` from `main/BUILD` (and any pass that still lists
     it).
   - Rebuild and confirm tests still pass.
   - Wholesale delete `lgraph/` (the directory and all source).
   - Drop `@hif//hif` from `MODULE.bazel` if no consumer remains.
   - Update this doc's status table to "DONE".

### Cumulative deletions to date (Phase G6 partial)

Roughly 15,000+ lines of obsolete LiveHD code already removed across
the sessions leading to this pivot:
- All fixme-tagged tests + their sources (8 files, ~2600 lines).
- `cops/live/` (orphaned, ~3000 lines).
- `pass/fluid/` (orphaned, ~1000 lines).
- 5 unused passes: `extractor`, `randomize_dpins`, `punch`,
  `mockturtle`, `semantic` (~5400 lines).
- `inou/json/` (~1200 lines).
- `lgraph.copy` Eprp pass + supporting code (dead `I(false)` stub).
- `Lgtuple::create_assign(Lgtuple)` / `Bundle::create_assign(*)`
  / `Bwd_edge_iterator` + `Lgraph::backward()` (all
  `I(false)` stubs with no live callers).
- `Lgraph_attributes::set_type_const(string_view|int64_t)` (unused).
- `Node::nuke()` / `Node_pin::nuke()` (TODO stubs).
- 4 obsolete lgraph tests: `edge_test`, `lgtuple_test`,
  `graph_bench`, `hierarchy_test`.

The remaining big deletion is `lgraph/` itself — blocked on the
per-pass migration. See the priority table above.

### HHDS upstream patches landed during this migration

For session continuity, the HHDS commits added in support of this
migration (all in `../hhds/hhds/`):
- `Node_class::has_inp_edges()` / `has_out_edges()` + matching
  `Graph::erase_declared_io_pin` assertion relaxation.
- Bug fix: `has_*_edges()` walks node-entry edges in addition to the
  pin linked list (covers port-0 edges).
- `GraphIO::reset_declarations()` — clears declarations without
  tombstoning the GraphIO + paired Graph.
- `GraphIO::has_pin_with_port_id` /
  `has_input_with_port_id` / `has_output_with_port_id`.
- `GraphIO::DeclaredIoPin` extended with `bits` (uint32) + `unsign`
  (bool); accessor methods `set_bits`/`get_bits`/`set_unsign`/`is_unsign`.
- `DeclaredIoPin` promoted to public + `get_input_pin_decls()` /
  `get_output_pin_decls()` for external iteration.
- `GraphLibrary::save/load` text format extended to round-trip the
  new bits + unsigned fields (no backward compat with older saves).

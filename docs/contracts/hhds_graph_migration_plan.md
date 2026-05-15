# HHDS Graph Migration — Implementation Plan

This plan covers the **graph-side** migration to HHDS (the LNAST side
is already complete — see `hhds_migration.md`). It replaces LiveHD's
hand-rolled `Lgraph` storage with `hhds::Graph`, and `Graph_library`
with `hhds::GraphLibrary`.

## Baseline (2026-05-14)

- 201 tests pass, 27 fail. The 27 failures concentrate in
  `inou/prp` (10 — known constprop verifier regressions documented
  in `hhds_migration.md §8.4`) and `inou/slang` (17 — preexisting,
  unrelated). These must remain the *only* failures after migration.
- `lgraph/` directory: ~12,205 LOC across 17 .cpp/.hpp files.
- 124 files across 37 directories `#include` lgraph headers.

## Scope and rough effort

This is roughly the same scope as the LNAST migration (which spanned
many commits over weeks). It is **not a single-session task**.
Concrete sub-tasks below; each must keep all 201 baseline tests
passing.

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
- [ ] `lgraph/tests/graph_bench.cpp` (compares lgraph vs hhds — both
  sides are now hhds) — blocked on G3 active reads making the lgraph
  side fully HHDS-backed.
- [ ] `lgraph/tests/edge_test.cpp` (exercises lgedgeiter / lgedge
  mechanics) — obsolete with G3 storage switch. User confirmed
  2026-05-14: no need to preserve API compatibility for this test
  during migration.
- [ ] `lgraph/tests/lgtuple_test.cpp` (lgtuple utility — depends on
  the legacy create_node_const + node-pin model) — obsolete. User
  confirmed 2026-05-14: deletable after migration; lgraph_test and
  node_test "may be" relevant and should be kept (re-examine).
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
punch list in `TODO.md`. Pick up by:
1. Read the latest "Phased plan" section above; pick the lowest
   un-finished Phase Gn.
2. Run `bazel test //... --keep_going` for a fresh baseline before
   touching anything.
3. Patch missing HHDS API upstream in `../hhds`, not locally.
4. Cross-reference `hhds_migration.md §4` (side-map convention)
   for any pass-local node→T data structures.

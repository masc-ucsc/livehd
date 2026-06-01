# inou/yosys HHDS migration — implementation skeleton

This document captures the planned structure for the `inou/yosys`
migration to `hhds::Graph`. It is a follow-up to the cgen migration
landed on 2026-05-15 (`cgen: migrate inou.cgen.verilog to hhds::Graph`).

## Scope

| File | LOC | Status | Notes |
| --- | --- | --- | --- |
| `lgyosys_tolg.cpp` | 2817 | PENDING | Yosys RTLIL → graph builder. The bulk. |
| `lgyosys_dump.{hpp,cpp}` | ~1180 | PENDING | Graph → Yosys writer. Used only by `inou.yosys.fromlg` (not on yosys_compile.sh critical path). |
| `lgyosys_fromlg.cpp` | 107 | PENDING | Thin wrapper around `Lgyosys_dump`. |
| `inou_yosys_api.cpp` | 432 | PENDING | Eprp-pass dispatcher. |
| `yosys_driver.cpp` | 681 | PENDING | Yosys binary driver — minimal Lgraph use. |

## Pipeline this migration unlocks

Current state (lgraph-based producer + HHDS-based cgen):
```
inou.yosys.tolg [Lgraph]
    |> lgraph.save [Lgraph]
    |> lgraph.match [Lgraph]
    |> pass.cprop [Lgraph]                    # mirror-writes to HHDS shadow
    |> inou.cgen.verilog [reads var.graphs]   # MIGRATED — consumes HHDS
```

After this migration (HHDS-only producer + HHDS-based cgen, cprop
still on the legacy lgraph path):
```
inou.yosys.tolg [hhds::Graph, drops //lgraph]
    |> lgraph.save [Lgraph wrap-around or new GraphLibrary::save]
    |> lgraph.match
    |> pass.cprop [still Lgraph]
    |> inou.cgen.verilog
```

NB: yosys_compile.sh's equivalence check depends on cprop's constant
folding. The full HHDS-only pipeline only works after cprop also
migrates. For now, the yosys-side migration can land independently
because `Eprp_var::add(Lgraph*)` lockstep-populates `var.graphs`.

## API translation table (yosys → HHDS)

These are the call sites in `lgyosys_tolg.cpp` and their HHDS equivalents,
verified against the cgen migration.

### Graph creation

```cpp
// Lgraph (current):
auto* g = library->create_lgraph(mod_name, "-");

// HHDS:
auto gio = hhds_lib.create_io(mod_name);
auto g   = gio->create_graph();
```

### IO declaration

```cpp
// Lgraph:
g->add_graph_input(wname, port_id, width);
g->add_graph_output(wname, port_id, width);
g->has_graph_input(wname);
auto pin = g->get_graph_input(wname);

// HHDS:
auto gio = g->get_io();
gio->add_input(wname, port_id);
gio->set_bits(wname, width);
gio->add_output(wname, port_id);
gio->set_bits(wname, width);
gio->has_input(wname);
auto pin = g->get_input_pin(wname);  // returns driver counterpart
```

### Node creation

```cpp
// Lgraph:
auto node = g->create_node(Ntype_op::Or, width);  // type + size

// HHDS (via Lgraph-style encoded type):
auto node = g->create_node();
node.set_type(static_cast<hhds::Type>(static_cast<uint16_t>(Ntype_op::Or) << 1));
node.create_driver_pin(0).attr(livehd::attrs::bits).set(width);
```

### Constant node

```cpp
// Lgraph:
auto dpin = g->create_node_const(Dlop::create_integer(v)).setup_driver_pin();

// HHDS (LiveHD convention: regular Nconst node + const_value attr):
auto cnode = g->create_node();
cnode.set_type(static_cast<hhds::Type>(static_cast<uint16_t>(Ntype_op::Nconst) << 1));
cnode.attr(livehd::attrs::const_value).set(Dlop::create_integer(v)->serialize());
auto dpin = cnode.create_driver_pin(0);
```

### Sink / driver pin lookup

```cpp
// Lgraph:
auto spin = node.setup_sink_pin("a");

// HHDS (for non-Sub nodes, translate name→pid):
auto pid  = Ntype::get_sink_pid(livehd::graph_util::type_op_of(node), "a");
auto spin = node.create_sink_pin(static_cast<hhds::Port_id>(pid));

// HHDS (for Sub nodes — name lookup via sub-graph's GraphIO):
auto spin = node.get_sink_pin("a");  // works for Sub only
```

### Edges

```cpp
// Lgraph:
g->add_edge(driver_pin, sink_pin);

// HHDS:
g->add_edge(driver_pin, sink_pin);  // same API
// or
driver_pin.connect_sink(sink_pin);
```

### Per-pin / per-node attributes

```cpp
// Lgraph:
pin.set_offset(off);
pin.set_name(wname);
pin.set_bits(w);
pin.set_unsign();
node.set_source(fname);
node.set_loc1(line);
node.set_color(c);

// HHDS:
pin.attr(livehd::attrs::pin_offset).set(off);
pin.attr(livehd::attrs::pin_name).set(std::string{wname});
pin.attr(livehd::attrs::bits).set(w);
pin.attr(livehd::attrs::pin_signed).del();  // unsigned/default
node.attr(livehd::attrs::source).set(std::string{fname});
node.attr(livehd::attrs::loc).set(livehd::attrs::loc_t::value_type{line, 0});
node.attr(livehd::attrs::color).set(c);
```

### Static state retyping

`lgyosys_tolg.cpp` has module-static maps keyed on Yosys types and valued
by LiveHD wrappers. After migration:

```cpp
// Before:
static absl::flat_hash_map<const RTLIL::Wire*, Node_pin>  wire2pin;
static absl::flat_hash_map<const RTLIL::Cell*, Node>      cell2node;
static absl::flat_hash_map<Pick_ID, Node_pin>             picks;

// After (keyed by HHDS opaque indices):
static absl::flat_hash_map<const RTLIL::Wire*, hhds::Pin_class>  wire2pin;
static absl::flat_hash_map<const RTLIL::Cell*, hhds::Node_class> cell2node;
static absl::flat_hash_map<Pick_ID, hhds::Pin_class>             picks;
// Pick_ID's hash function uses driver.get_class_index() instead of get_compact().
```

### Subnode (Sub-cell) creation

```cpp
// Lgraph:
auto sub_node = g->create_node_sub(sub_name);
auto sink = sub_node.setup_sink_pin("port_name");

// HHDS:
auto node = g->create_node();
node.set_type(static_cast<hhds::Type>(static_cast<uint16_t>(Ntype_op::Sub) << 1));
auto sub_gio = hhds_lib.find_io(sub_name);
node.set_subnode(sub_gio);
node.attr(livehd::attrs::subid).set(sub_gio->get_gid());
auto sink = node.get_sink_pin("port_name");  // sub-graph name resolution
```

## Migration steps (recommended order)

1. **Static state retyping.** Change the module-scope maps to HHDS handle
   types. This forces every helper to adopt HHDS types in one cascade.
2. **set_loc, resolve_constant.** Small and self-contained.
3. **look_for_wire.** First touchpoint with graph-IO declarations.
4. **create_pick_operator / create_pick_concat_dpin.** Pure-graph
   transformations, no special-cell logic.
5. **resolve_memory.** Memory cell — uses the per-port pin name
   convention (`"0addr"`, `"1clock_pin"`). Reuse the cgen migration's
   `Ntype::get_sink_name(Ntype_op::Memory, raw_pid)` approach.
6. **process_cells, process_cell_drivers_initialization, process_assigns,
   process_connect_outputs.** The big middle. Mostly mechanical
   per-cell-type handling.
7. **The Pass::execute body.** Replace `library->create_lgraph(...)`
   with `hhds_lib.create_io(...) + create_graph()`. The Eprp dispatch
   stays in `inou_yosys_api.cpp` until that file migrates.
8. **inou_yosys_api.cpp.** Switch the `var.add(lgs)` block to populate
   `var.graphs` directly (existing `var.add(Lgraph*)` lockstep populate
   makes this transparent during the transition).
9. **lgyosys_fromlg.cpp + lgyosys_dump.{hpp,cpp}.** The output side.
   Not on the yosys_compile.sh critical path; can land later.
10. **BUILD changes.** Drop `//lgraph` from `inou/yosys/BUILD`'s deps
    once no source file references Lgraph types.

## Validation gate

Each phase preserves the 215/11/1 bazel test baseline and the 82/85
`yosys_compile.sh` baseline (the 3 pre-existing failures stay; no
new regression).

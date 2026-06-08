# graph/ — LGraph IR (HHDS-backed)

LGraph is LiveHD's low-level hardware IR: a graph of typed **nodes** (logic,
arithmetic, muxes, registers, memories, constants, sub-graphs) wired through
**pins** and **edges**. One LGraph is a Verilog-module-like unit; complex designs
are many LGraphs linked as sub-graphs.

> **Renamed / migrated.** This directory was once `lgraph/`. The graph container,
> nodes, pins, and edges now come from **HHDS** (`hhds::Graph`,
> `../hhds/hhds/graph.hpp`); this directory holds the LiveHD layer on top:
> cell-type semantics, per-pin attributes, and the graph library. If you are
> hunting for `lgraph.hpp`, the handle types live in HHDS.

The conceptual + cell-type reference (Sum, Mult, Mux, Flop, Memory, Sub, …) lives
in the docs: <https://masc-ucsc.github.io/docs/livehd/05-lgraph/>. This README is
just the directory orientation.

## Concepts (orientation)

- **Node** — a vertex with a cell type and a set of pins.
- **Pin** — a connection point. A **driver** pin is a node output (has drive
  strength); a **sink** pin is an input. One driver fans out to many sinks.
- **Edge** — a connected (driver, sink) pair.
- **Black-box graph** — IO known, implementation unknown; instantiated by name
  and matched at codegen once an implementation appears (no re-elaboration of
  instantiators).
- **Single vs hierarchical traversal** — single-graph traversal stays inside one
  LGraph; hierarchical traversal walks a top graph and its sub-graphs as a
  "virtual flat" graph (only black-box sub-graphs stay opaque).
- **Class vs hierarchical attribute** — a *class* attribute is shared across all
  instances of a graph; a *hierarchical* attribute holds a per-instance value.

## Key files

- `cell.hpp` / `cell.cpp` — cell-type catalog and per-cell semantics.
- `attrs.hpp` — per-pin / per-node attributes (e.g. `bits`, `sign`).
- `graph_library_singleton.{hpp,cpp}` — the process graph library.
- `node_util.hpp`, `const_pin.cpp`, `ann_place.hpp` — node helpers / annotations.

> Per-pin `bits`/`sign` are HHDS attributes today; the native-storage campaign is
> tracked in the TODO hub (`todo/livehd/hhds-pinentry.html`).

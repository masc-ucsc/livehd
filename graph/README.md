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

## Hotmux ports

`Hotmux` uses contiguous `(control, value)` pairs: `p0/p1`, `p2/p3`,
and so on. Controls are one-bit predicates and must be mutually exclusive.
A control is ACTIVE when it is NON-ZERO, not when it equals 1: nothing narrows
a control carrier, so every consumer (`cgen_verilog`'s `(ctl) != 1'b0` case
item, the SMT encoders' `DISTINCT(ctl, 0)`, `hlop`'s `Slop`/`Dlop::hotmux_op`)
tests it that way.
An optional trailing even port carries a default value, selected when all
controls are zero; without that port the result is zero. The default is a
value, not another predicate, and is excluded from the uniqueness check.

For example, `hotmux(c0, a, c1, b, d)` selects `a` for `c0`, `b` for `c1`,
and `d` when neither is active. Frontends preserve source-level fall-through
and register holds through this default port. No packed selector or explicit
none-of predicate is needed in the graph. Regenerate persisted graphs from
builds that used the previous packed-selector Hotmux encoding.

## Transparent hierarchy wrappers

`__flat___<suffix>` is reserved for transformation-generated **instance** names
that do not contribute to logical hierarchy. For example,
`foo.__flat___region42.bar.q` has logical name `foo.bar.q`. Nested transparent
wrappers are skipped too. An ordinary instance (including a similar but not
identical prefix) retains its name. A signal whose own leaf name starts with
`__flat___` is not a wrapper and retains that name.

Use `graph_util::logical_hier_name` for correspondence names and
`logical_instance_prefix` when physically inlining. HHDS occurrence paths and
`get_hier_name()` remain physical identities; do not use logical names as
same-design node IDs or wiring/cache keys. Semdiff and LEC normalize names
through the shared helper; a semdiff cut instance still keys on its own
instance name, so transparent siblings stay distinct compare points. Anonymous
partition instances remain anonymous in LGraph; cgen assigns reserved-prefix
names (`__flat___<module>`, built from the flat, dot-free module identifier)
when Verilog requires an identifier, so transparency survives emission and
reload.

A transparent wrapper groups existing logical state; it does not introduce a
new state namespace. A transformation duplicating state must give the copies
distinct logical names or use ordinary named instances. LEC rejects ambiguous
state cuts instead of merging them, and semdiff leaves a compare-point key that
repeats on one side undecidable. Replicated loop instances must retain their
ordinary occurrence names/ordinals; they are not transparent wrappers.

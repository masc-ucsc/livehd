# Source Map Contract

Status: draft.

This document describes the source-map approach for mapping transformed
LiveHD IR back to original Pyrope source without storing full source
locations on every LNAST or LGraph node.

The design goal is useful, stable source attribution for diagnostics,
`sigref`, waveform click-through, hot-probe insertion, generated Verilog
comments, agent-editable debug output, and conversion to standard
ECMA-426 source maps for tools that already understand JavaScript and
TypeScript source maps. It is not a promise of exact LLVM-style debug
info after every optimization.

## Core invariants

1. **One global, append-only source map per compilation unit.** SourceIds
   are stable across the entire pipeline. Transforms only *add* entries;
   existing entries never change meaning.
2. **The source map is an optional sidecar.** The IR is semantically valid
   without it. All compiler passes must function correctly when it is
   absent. Only diagnostic, debug, and provenance queries depend on it.
3. **Transformations are non-destructive.** Passes produce new LNAST trees
   (or LGraphs); originals are preserved. The source map records how
   definitions in new artifacts relate to definitions in originals.
4. **Definition-level tracking.** Each definition-bearing node gets a
   SourceId. Operand refs do not. This is the granularity unit.

## Terminology

`SourceId`
: Compact integer ID for a source-map entry. Stable for the lifetime of
  the compilation unit. Used by LNAST, LGraph, sigref, sidecar files, and
  debug queries. Format is `(FileId, local_index)` packed into the
  integer so IDs are stable regardless of file processing order.
  `FileId` is derived from canonical path or content hash.

`SymbolId`
: Stable identity for a source-level declaration (variable, function,
  partition, port, register, memory). One symbol can have many
  definitions. **SymbolId is the primary identity for source-level
  declarations.** Name/scope is a debug convenience derived from it.

`DefId`
: Identity of an IR definition. For LNAST, conceptually the tree handle
  plus HHDS position of a def-bearing node. For LGraph, the graph handle
  plus node/pin identity for a produced value. DefIds are not stable
  across tree versions; SourceIds are.

`Anchor`
: Original source span: file ID, byte start/end, source kind, lexical
  `ScopeId`, optional declared name, optional `SymbolId`. Line/column are
  derived from byte offset on demand using a per-file line table.

`ScopeId`
: Interned identifier for a lexical scope path. Scope paths repeat
  heavily and are interned to keep entries small.

`Derived entry`
: Source-map entry produced by a transformation. Points to one or more
  parent SourceIds plus a derivation kind, instead of a single original
  source anchor.

## Canonical source map

At Pyrope parse/lowering time, tree-sitter nodes provide byte ranges. The
source map records this information once per definition.

Each *original* source-map entry contains:

- `SourceId`
- `FileId`
- byte start/end
- source kind: declaration, assignment, expression, field access,
  partition, port, register, memory, assertion
- `ScopeId`
- optional declared/source name
- optional `SymbolId`

Line/column ranges are derivable from byte offsets and are not stored.
The original LNAST node may be referenced for in-memory acceleration,
but the entry must carry enough span/name/scope/file information to be
serialized and used after the original LNAST is no longer in memory.

## Storage layout

The source map is held as a separable global table per compilation unit.
It is **not** stored inside the IR.

Each new tree or graph carries:

- A small **tree-level header** listing the original tree(s) it derives
  from. A transformed artifact can derive from more than one original
  (inlining merges multiple originals; partitioning splits one into
  many).
- A **side map** `(handle, HHDS position) → SourceId` for def-bearing
  nodes. Operand refs have no entry.

HHDS attributes are *not* used for source-map data. Coupling provenance
to IR storage would prevent the sidecar from being dropped without
modifying the IR.

The original LNAST is the dense-by-default case: its side map covers
every def-bearing node minted at ingress. Transformed artifacts add
side-map entries only for definitions they introduce or alter.

## LNAST granularity

The source map tracks LNAST *definition* nodes, not operand refs.

Plain `assign` is one definition form, but it is not the only one. Many
LNAST nodes define a destination through their first child:

```text
assign      dst = rhs
tuple_get   dst = base.field
eq          dst = a == b
log_and     dst = a && b
func_call   dst = call(...)
```

The destination ref of such a node is the value being defined. Operand
refs do not need source-map entries.

Example:

```text
tuple_get ___2 case1 x
eq        ___3 ___2 3
tuple_get ___4 case1 y
eq        ___5 ___4 4
log_and   ___6 ___3 ___5
cassert   ___6
```

Useful attribution lives on the defining nodes:

```text
def ___2 -> source span: case1.x
def ___3 -> source span: case1.x == 3
def ___4 -> source span: case1.y
def ___5 -> source span: case1.y == 4
def ___6 -> source span: case1.x == 3 and case1.y == 4
cassert  -> source span: whole cassert statement
```

The operand refs to `case1`, `___2`, `___3`, and similar values carry no
individual source entries. SSA temps like `___2` have no useful surface
name, so name/scope fallback cannot help them — their SourceId is the
only way to recover provenance. In practice nearly every def-bearing
node in the ingress tree gets a SourceId. The savings versus per-node
storage come from operand refs, not from sparse defs.

## SSA

All SSA versions of a source variable share one `SymbolId`. Each SSA
version corresponds to a distinct assignment or expression and gets its
own SourceId.

For example:

```text
a = 1
a = b + 3
a = c ? a : d
```

These three writes refer to the same source-level symbol `a` (one
SymbolId), but they are three different definitions (three SourceIds).
A debug query for a current SSA value resolves to the assignment or
expression that produced that version, while reporting that the source
symbol is `a`.

## Aliases and derived entries

Alias chains are represented structurally, not eagerly flattened.

For:

```text
a = b
c = a
```

the source map stores `c` as an alias of `a`, and `a` as an alias of
`b`. Queries may request direct parents or a bounded transitive
closure. Default diagnostic display prefers the closest user source
span and avoids unbounded alias expansion.

When a pass combines values, it creates a derived entry:

```text
new SourceId -> parents: [SourceId A, SourceId B], kind: <derivation>
```

Derivation kinds:

- `alias`
- `rename`
- `tuple_expansion`
- `inline` (see inlining rules below)
- `partition_boundary` (see partitioning rules below)
- `fold`, `mux`, `arithmetic`, `compare`
- `fallback` (no better anchor; parent is the enclosing definition)

## Inlining

When a pass produces a new tree by inlining callee LNAST into a caller,
each inlined definition in the new tree gets a fresh derived SourceId
with `kind: inline` and parents `[callee_def_source, call_site_source]`.

The new tree's side map points its def nodes at these fresh derived
entries. The callee's original SourceIds are untouched. A debug query
on the new tree can reconstruct the full inline stack by walking the
parent chain.

## Partitioning

When a pass produces multiple new trees by splitting one (for size or
structural reasons), each resulting tree's side map handles two cases:

1. **Preserved definitions** — the new def is essentially a copy of an
   original def. The side map points at the original SourceId directly.
   No new source-map entry is created.
2. **Boundary definitions** — new ports/wires synthesized at the cut.
   These get a fresh derived entry with `kind: partition_boundary`.
   - If the cut is at a user-declared partition or function boundary,
     the parent is the SourceId of that declaration.
   - If the cut is compiler-chosen for size, the parent is the
     enclosing original definition (fallback).

## Removed and folded definitions

Because transforms are non-destructive, the original definition still
exists in its original tree. Tombstones are therefore not required to
preserve queryability of the original — that is automatic.

Tombstones remain useful for diagnostics that need to report "this
user-visible definition has no representation in the final IR":

```text
old SourceId -> folded to constant 7   (kind: fold)
old SourceId -> removed as dead        (kind: dead)
old SourceId -> replaced by SourceId N (kind: rename)
```

These are *new* entries that reference the old one; they do not mutate
the old entry. The append-only invariant holds.

## LGraph

LGraph is treated as one more transformation target of the same global
source map. The same machinery applies:

- The LGraph carries a tree-level header listing the LNAST tree(s) it
  derives from.
- A side map `(graph handle, node/pin id) → SourceId` attributes
  def-bearing artifacts (cells, registers, memories, ports, partition
  boundaries, internal wires).
- Definitions that are essentially copies of an upstream LNAST def
  reuse the upstream SourceId. New definitions introduced during
  lowering get derived entries with the appropriate kind.
- There is no special "mandatory locations vs internal fallback"
  distinction. Every def-bearing artifact has a SourceId; the
  derivation kind tells you the quality.

Generated Verilog can emit source-map comments or sidecar references
from these IDs.

## Serialized sidecars

The canonical serialized format is a LiveHD source-map sidecar, written
next to each emitted artifact that needs persistent provenance. Use the
extension `.lhsm.json` until the format is compacted or versioned
elsewhere.

The canonical sidecar stores the full LiveHD model:

- File table: `FileId`, canonical path, optional content hash, optional
  embedded content.
- Scope table: `ScopeId` and interned lexical scope path.
- Symbol table: `SymbolId`, source-level name, kind, declaration
  `SourceId` when known.
- Source entries: `SourceId`, original anchor or derived parent list,
  derivation kind, query quality, optional `SymbolId`.
- Artifact maps: textual generated spans or IR handles mapped to
  `SourceId`.

Field names should stay close to the ECMA-426 terminology where the
meaning overlaps: `sources`, `sourcesContent`, `names`, `mappings`,
`line`, and `column`. LiveHD-specific identity fields keep their LiveHD
names: `source_id`, `file_id`, `scope_id`, `symbol_id`, `parents`,
`kind`, and `quality`. Keeping the shared names aligned makes conversion
mechanical and avoids inventing a second vocabulary for the same
concepts.

Line and column values in serialized text-facing records follow
ECMA-426/source-map conventions: generated and original lines are
0-based, columns are 0-based UTF-16 code-unit offsets. Canonical anchors
still store byte offsets; line/column values are derived from each
file's line table when exporting. Human-facing diagnostics can convert
these to the usual 1-based line display at presentation time.

## ECMA-426 interchange

[ECMA-426](https://tc39.es/ecma426/) source maps are the standard
interchange format for JavaScript and TypeScript tooling. LiveHD should
emit them as a derived view for textual artifacts, not as the canonical
store, because ECMA-426 maps a generated position to a single original
position and optional name. It does not directly model multi-parent
derivations, alias chains, optimized-away values, tombstones, or
source-quality markers.

For every textual egress artifact that is intended for humans or
external tooling, emit both:

```text
top.v
top.v.map
top.v.lhsm.json
```

The `.map` file is an ECMA-426 source map. The `.lhsm.json` file is the
canonical LiveHD sidecar. The generated artifact should include a
source-map URL comment when the output language can carry comments
without changing semantics:

```verilog
//# sourceMappingURL=top.v.map
```

The ECMA-426 map should contain only the lossy, broadly compatible
projection:

```json
{
  "version": 3,
  "file": "top.v",
  "sources": ["src/foo.prp"],
  "sourcesContent": [null],
  "names": ["alu.flag_z"],
  "mappings": "...",
  "x_livehd": {
    "schema": 1,
    "source_map": "top.v.lhsm.json",
    "compilation_unit": "sha256:...",
    "artifact": "top.v"
  }
}
```

All LiveHD-only metadata in an ECMA-426 map must live under
`x_livehd`. Generic source-map consumers can ignore this extension, while
LiveHD-aware tools can use it to open the canonical sidecar.

Conversion from the canonical LiveHD sidecar to ECMA-426 follows these
rules:

- Exact original anchors export directly to `sources`, `mappings`, and
  `names`.
- Derived, alias, inline, and fallback entries choose one display anchor
  for the ECMA-426 segment. The ranking is the same one used by source
  queries: prefer `exact`, then closest user-visible `alias` or
  `derived`, then `fallback`, then `unknown`.
- Full parent lists, derivation kind, query quality, tombstones, and
  reverse-query data stay in `.lhsm.json`; they are not encoded into
  `names`.
- `names` contains source-level names such as symbols, ports, registers,
  memories, or user-visible generated names. It must not use `SourceId`
  as the primary name encoding.
- `sources` uses stable repo-relative or input-package-relative paths
  when possible. Absolute paths are avoided unless the input itself was
  absolute and no stable workspace-relative root is known.
- `sourcesContent` is optional and defaults to `null` for persistent
  build artifacts. Tools may opt in to embedding content for hermetic
  debug bundles.
- Concatenated outputs use an ECMA-426 index source map with `sections`
  when preserving per-file offsets is clearer than flattening all
  mappings into one artifact map.

Conversion in the other direction is intentionally partial. Importing a
plain ECMA-426 map can recover file, line, column, source content, and
optional names, but it cannot reconstruct LiveHD derivation metadata.
Such imported entries get `quality: exact` for direct mappings and
`kind: external` unless an `x_livehd.source_map` extension points to a
canonical sidecar.

## Worked Pyrope example

This example shows how original Pyrope parsed by tree-sitter can map
back from a final generated Pyrope artifact after LiveHD transformations.

Original input, `orig.prp`:

```pyrope
comb inc(a) -> (y) {
  y = a + 1
}

comb top(a) -> (z) {
  z = inc(a)
}
```

At ingress, tree-sitter gives byte ranges for the definitions. LiveHD
creates original source entries once:

```json
{
  "sources": [
    {
      "file_id": 1,
      "path": "orig.prp",
      "content_hash": "sha256:..."
    }
  ],
  "entries": [
    {
      "source_id": 100,
      "kind": "partition",
      "file_id": 1,
      "span": {
        "start_byte": "<comb inc start>",
        "end_byte": "<comb inc end>"
      },
      "name": "inc"
    },
    {
      "source_id": 101,
      "kind": "assignment",
      "file_id": 1,
      "span": {
        "start_byte": "<y = a + 1 start>",
        "end_byte": "<y = a + 1 end>"
      },
      "symbol_id": 10,
      "name": "inc.y"
    },
    {
      "source_id": 200,
      "kind": "partition",
      "file_id": 1,
      "span": {
        "start_byte": "<comb top start>",
        "end_byte": "<comb top end>"
      },
      "name": "top"
    },
    {
      "source_id": 201,
      "kind": "assignment",
      "file_id": 1,
      "span": {
        "start_byte": "<z = inc(a) start>",
        "end_byte": "<z = inc(a) end>"
      },
      "symbol_id": 20,
      "name": "top.z"
    }
  ]
}
```

After inlining `inc` into `top`, a final Pyrope writer may emit:

```pyrope
comb top(a) -> (z) {
  z = a + 1
}
```

The final assignment is not a brand-new unrelated source location. It is
a derived definition: the expression body comes from `inc.y`, and the
placement/name comes from the call site assigning `top.z`.

```json
{
  "entries": [
    {
      "source_id": 300,
      "kind": "inline",
      "quality": "derived",
      "parents": [101, 201],
      "symbol_id": 20,
      "name": "top.z"
    }
  ],
  "artifact_maps": [
    {
      "artifact": "final.prp",
      "generated": {
        "line": 1,
        "column": 2,
        "end_line": 1,
        "end_column": 11
      },
      "source_id": 300
    }
  ]
}
```

A LiveHD-aware query on `final.prp:2:3` resolves `source_id: 300`,
then walks the parent list:

```text
final.prp:2:3  z = a + 1
  derived inline result for top.z
  call site: orig.prp:6  z = inc(a)
  inlined body: orig.prp:2  y = a + 1
```

The ECMA-426 projection for the same final Pyrope keeps only one display
anchor, because standard source maps cannot encode the full parent list.
For this example, the display anchor is the call site, and the LiveHD
extension points to the canonical sidecar:

```json
{
  "version": 3,
  "file": "final.prp",
  "sources": ["orig.prp"],
  "sourcesContent": [null],
  "names": ["top.z"],
  "mappings": "...",
  "x_livehd": {
    "schema": 1,
    "source_map": "final.prp.lhsm.json",
    "artifact": "final.prp"
  }
}
```

So generic source-map tooling can still jump from generated
`final.prp` back to `orig.prp` at the selected display location, while
LiveHD tooling can open `final.prp.lhsm.json` and recover both original
parents.

## Runtime debug artifacts

Runtime debug artifacts derived from the IR should carry a SourceId so
they can self-attribute back to source without a separate lookup:

- `sigref` references
- generated Verilog comments
- waveform signals
- hot-probe injection points

This is part of the contract because these artifacts often outlive the
compilation invocation and are inspected by humans or agents.

## Query result quality

Source queries should expose attribution quality:

- `exact` — direct source definition or expression
- `symbol` — declaration-level symbol match
- `alias` — value aliases one or more source definitions
- `derived` — value combines or transforms multiple source definitions
- `fallback` — partition or enclosing-statement fallback
- `optimized_away` — source value was folded or removed
- `unknown` — no usable mapping

The quality marker is part of the contract. It lets debug tools and
agents use approximate mappings without confusing them with exact
source spans.

## Reverse queries

Reverse queries (source span → set of IR nodes across all live trees)
are needed for use cases like "user clicks line 42, highlight every IR
or waveform value derived from it."

The contract does **not** require a maintained reverse index. Reverse
queries are performed by scanning the forward map at query time. If
profiling later shows this is a bottleneck, an index can be built on
demand; this is an implementation choice, not a contract requirement.

## Pass obligations

Pass authors do not propagate source locations on every IR edit. They
update the source map only when they change the identity of a
definition.

Rules:

- Copying a definition unchanged: the new tree's side-map entry points
  at the existing SourceId. No new entry.
- Renaming a destination: a new derived entry with `kind: rename`
  parented at the old SourceId.
- Creating an SSA version: a new SourceId tied to the assignment or
  expression that produced it. Same SymbolId as other versions.
- Inlining: derived entry per inlined def with `kind: inline` and both
  callee and call-site parents.
- Tuple expansion: derived entries mapping each expanded field back to
  the tuple declaration, field access, or both.
- Partitioning: boundary defs get `kind: partition_boundary` entries
  (see partitioning section).
- Combining values: derived entry with parent SourceIds and appropriate
  arithmetic/compare/mux/etc. kind.
- Removing or folding: a new tombstone or replacement entry (see
  removed/folded section).
- Synthesized values with no better anchor: derived entry with
  `kind: fallback` parented at the enclosing partition or statement.

## Open implementation choices

- Exact canonical `.lhsm.json` schema details and whether it stays JSON
  or grows a compact binary encoding.
- Implementation of the global source-map side maps (hash map keyed on
  HHDS position vs. flat_storage backing — `flat_storage` is reasonable
  here since SourceIds are persistent provenance, not pass-local
  transient state).
- How LGraph names scope source-map lookup after hierarchy flattening.
- Alias-closure query cap and ranking rules.
- Exact ECMA-426 segment-density policy for generated Verilog: per
  definition, per emitted statement, or per assign/wire declaration.
- CI checks that every def-bearing node in a transformed tree or LGraph
  resolves to a SourceId.

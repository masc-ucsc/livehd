# Source Map Contract

Status: draft.

This document describes the source-map approach for mapping transformed
LiveHD IR back to original Pyrope source without storing full source
locations on every LNAST or LGraph node.

The design goal is useful, stable source attribution for diagnostics,
`sigref`, waveform click-through, hot-probe insertion, generated Verilog
comments, and agent-editable debug output. It is not a promise of exact
LLVM-style debug info after every optimization.

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

- Exact serialized source-map format and on-disk layout.
- Implementation of the global source-map side maps (hash map keyed on
  HHDS position vs. flat_storage backing — `flat_storage` is reasonable
  here since SourceIds are persistent provenance, not pass-local
  transient state).
- How LGraph names scope source-map lookup after hierarchy flattening.
- Alias-closure query cap and ranking rules.
- CI checks that every def-bearing node in a transformed tree or LGraph
  resolves to a SourceId.

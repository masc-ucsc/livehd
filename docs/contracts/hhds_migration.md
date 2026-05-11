# HHDS Migration — Architecture

LiveHD's hand-rolled tree libraries (`core/lhtree.hpp`, `core/lhtree2.hpp`)
and the LNAST half of HIF persistence have been replaced by HHDS
(`/Users/renau/projs/hhds`). The graph-side migration (`@hif` for `Lgraph`,
`core/graph_core*.hpp`) is deferred to a separate effort.

This doc records the *resulting* shape of LiveHD's tree layer and the
design decisions behind it. Phase 7 (Forest-centric ownership for upass)
is still planned and is captured at the end.

## 1. Goal and scope

Maximise deletion of LiveHD code; do not preserve old APIs or binary
formats. Only the LNAST stack moved to HHDS:

- LNAST trees, the AST parser, and the (now-deleted) `lgraph::Node_tree`.
- LNAST persistence: HIF reader/writer for LNAST is gone; LNAST round-trips
  via `Tree::save_body` / `Forest::save`.

Out of scope (still on `@hif`):

- `lgraph::Lgraph` HIF persistence (`lgraph.cpp`, `graph_library.cpp`).
- `core/graph_core*.hpp`, `core/graph_sizing.hpp`. These are covered by
  the future graph migration to `hhds::graph`.

`lh::woothash64` / `lh::waterhash` (in `core/woothash.hpp`,
`core/waterhash.hpp`) survive — they are unrelated hashing utilities, not
tree code.

## 2. HHDS surface

LiveHD relies on this slice of HHDS:

- `hhds::Tree` (`hhds/tree.hpp`) with `Node_class` handles: `add_child`,
  `append_sibling`, `insert_next_sibling`, `parent`, `first_child`,
  `next_sibling`, `is_root`, `is_leaf`, `pre_order_class`,
  `post_order_class`, `sibling_order`, `set_type`, `set_subnode`,
  `del_node`.
- `hhds::Forest` for tree-of-trees: `create_io`, `find_io`, `find_tree`,
  `add_reference`, `delete_tree`, `save`, `load`.
- `Attr_host` + `Attribute` concept (`hhds/attr.hpp`) with `flat_storage`
  for per-node mutable payload.
- `Tree::save_body` / `load_body` and `Forest::save` / `load`
  (`hhds/tree_serial.cpp`) — binary on-disk format. Replaces HIF for LNAST.
- `Tree::print` / `dump` (`hhds/tree_print.cpp`) — UTF-8 box-drawing
  pretty printer. Replaces ad-hoc lhtree dumps.

Bazel target: `@hhds//hhds:core`. Header includes are
`#include "hhds/tree.hpp"`, `"hhds/attr.hpp"`, `"hhds/attrs/name.hpp"`.
HHDS pinned at `98937f6e1d4a637b2a4d6e659f829bd86bf28467` in
`MODULE.bazel`.

## 3. Payload mapping

`hhds::Tree` stores only a small `Type` per node; everything else is an
`Attr_host` attribute. Each LiveHD payload field is a registered attribute
tag with `flat_storage`.

### 3.1 LNAST nodes

`lnast::Lnast` is a wrapper over `std::shared_ptr<hhds::Tree>` inside a
`hhds::Forest`. `Lnast_nid` aliases `hhds::Tree::Node_class`. Per-node
payload split:

| LiveHD field        | HHDS storage                                    |
| ------------------- | ----------------------------------------------- |
| `Lnast_ntype`       | native `Tree::Type` slot (uint16)               |
| token text          | `hhds::attrs::name`                             |
| `loc1 / loc2 / line / tok` | packed `lnast::attrs::loc` (flat_storage, trivially copyable) |
| `fname`             | `lnast::attrs::fname` (flat_storage, std::string) |
| `subs` (SSA)        | `lnast::attrs::ssa_subs` (flat_storage, int16)  |

All Lnast attribute tags are pre-registered at static-init time in
`lnast/lnast.cpp`. **Why:** the HHDS attribute registry races on
first-touch under threaded benches; pre-registering before `main()` keeps
the lazy `attr()` path on the early-return branch.

### 3.2 AST parser nodes

`parser::Ast_parser` is rewritten over `hhds::Tree`. Streaming
`down/up/add` semantics preserved. `rule_id` rides in `Tree::Type`;
`token_entry` lives in `ast_parser::attrs::token_entry` (flat_storage).

### 3.3 Legacy `Node_tree` is gone

`lgraph/node_tree.{hpp,cpp}` was deleted rather than ported. Its
constructor was already `assert(false); // TO DEPRECATE SOON`, and
`lgraph::Hierarchy` only `#include`d the header without using it.
Downstream casualties:

- ArchFP's `Node_tree`-using methods (`FPObject::findNode`,
  `FPObject::outputLgraphLayout`, `FPContainer::outputLgraphLayout`,
  `gridLayout::outputLGraphLayout`) deleted. ArchFP keeps its hot-spot
  layout entrypoints; the lgraph-output path was untested
  (`tags = ["fixme"]`) and depended entirely on `Node_tree`.
- Dead test fixtures removed (`tree_lgdb_setup.hpp`,
  `traversal_hierarchy_test.cpp`, `traverse_test.cpp`, the stale
  `graph_bench` BUILD target).

## 4. Side-map convention

Pass-local data keyed by node uses
`absl::flat_hash_map<hhds::Tree_class_index, T>` via
`Node_class::get_class_index()`. Cross-tree maps use
`hhds::Tree_flat_index` (holds `{tid, value}`) via
`Node_class::get_flat_index()`. **Why:** HHDS's `Node_class` is a value
type — it is not stable across mutations the way a positional `(level,
pos)` tuple was, but the index helpers give a stable hashable key.

Per-tree wrapper metadata (module name, source filename) belongs on the
tree itself (`tree->set_name(...)` or a tree-scope attribute), not in an
external map. `TreeIO::get_tid()` is only useful when keying *which slot*
across replaces, which is rare.

This is the pattern documented in `AGENTS.md` for new tree work and is
already memorialized in user memory.

## 5. HIF persistence boundary

LNAST persistence flows through HHDS now:

- `inou/lnast/pass_lnast_load.cpp` reads via `Forest::load` and populates
  `var.lnasts`.
- `inou/lnast/pass_lnast_save.cpp` writes via `Forest::save` (or per-tree
  `save_body`).

Pass names `lnast.load` / `lnast.save` were kept; the on-disk magic was
bumped so HHDS rejects old HIF dumps loudly.

`@hif//hif` is removed from `lnast/BUILD` and `benchmark/BUILD`. The
`@hif` `http_archive` in `MODULE.bazel` stays — `Lgraph` still needs it
until the graph migration.

## 6. `lh::` namespace and compat shims

The `lh::` namespace alias was retired in migrated files because the
lhtree-side struct and the new `Node_class` alias collided when both
headers were transitively included. Migrated code uses
`hhds::Tree::Node_class` directly or the domain-specific `Lnast_nid`
alias.

`core/tree_compat.hpp` provides `level_of()`, `pos_of()`, `hash_of()`
shims for diagnostic-print sites that still ask for legacy `(level,
pos)` / hash on a `Node_class`. **Why:** there are many such sites; a
shim header lets them keep printing without each becoming a port.

## 7. Phase 7 (planned) — Forest-centric ownership and clone/replace

This phase is **not landed**. It targets HHDS top
`1f0e8c8770e32fc6a6c2fa6651e9af5957889697`, which adds threaded Forest
plus `Tree::clone()` / `TreeIO::replace()`.

### 7.1 Why

Today every transform that produces a new LNAST allocates a fresh
`Lnast` (with its own Forest) and `Eprp_var::replace(old, new)` swaps
the `shared_ptr` in `var.lnasts`. Two problems:

1. Anyone holding the old `shared_ptr<Lnast>` (function registries,
   caches, downstream pass state) keeps pointing at the old body.
2. The pointer-swap dance has no equivalent at the HHDS layer; once
   the Forest model lands properly we want the slot identity (`Tid`)
   to be the stable handle, not the wrapper object.

HHDS exposes the right pattern: `Forest::create_tree_temp(name_hint)`
(or `Tree::clone()`) returns an unattached body;
`TreeIO::replace(new_body)` atomically installs it. Slot `Tid` and
slot name stay stable, so any holder of the `TreeIO` — or an `Lnast`
that caches a `shared_ptr<TreeIO>` — picks up the new body on the
next `get_tree()` call.

### 7.2 Resulting shape

- `Lnast` stores its `shared_ptr<hhds::TreeIO>` and exposes:
  - `treeio()` accessor.
  - `replace_body(std::shared_ptr<hhds::Tree> new_body)` — calls
    `treeio_->replace(new_body)` and refreshes the cached `tree_` so
    existing accessors see the new body.
  - Constructor `Lnast(std::shared_ptr<hhds::Tree>, std::string_view)`
    that wraps an already-built tree (no Forest, no TreeIO). Used to
    wrap a staging body so the existing `set_data` / `add_child` API
    works during construction.
- `uPass_runner::run` builds its staging tree from
  `lnast->forest()->create_tree_temp("staging-" + module_name)` and
  wraps it in a tree-only Lnast for emission.
- `pass_upass.cpp` calls `ln->replace_body(staged->tree_ptr())` instead
  of `var.replace(ln, staged)`. The `Lnast` object inside
  `var.lnasts[i]` is the same pointer before and after; the body
  underneath is swapped.

### 7.2.1 Partition descriptor as tree-level attribute

Each tree carries a `Partition` descriptor (see `architecture.md §3`)
as a tree-scope attribute on the `hhds::Tree`: kind
(`comb`/`pipe`/`mod`), latency range, clock domain, port list,
external boundaries, and the `interface_hash` / `state_shape_hash`
content hashes. The clone/replace dance from §7.2 must preserve the
descriptor on the new body — it is part of the partition's identity,
not the tree body. `TreeIO::replace` should be passed (or recompute)
the descriptor at swap time.

### 7.3 Out of scope for Phase 7

- Hoisting Forest ownership to `Eprp_var` so every LNAST in a run shares
  one Forest. Each `Lnast` still owns a private Forest in this phase.
  Required later for cross-module hierarchy via `set_subnode` /
  `add_reference`.
- Switching `Eprp_var::lnasts` from `vector<shared_ptr<Lnast>>` to
  `vector<shared_ptr<TreeIO>>`. Bigger surface change; deferred.
- Removing `Eprp_var::replace`. The pointer-swap variant is left in
  place while we audit straggler callers (the only live one is upass;
  the `pass/locator` site is already commented out).

## 8. Open risks and follow-ups

1. **Attribute hot path.** `Lnast.subs` is mutated very frequently
   during SSA. `flat_storage` is a hashmap lookup; lhtree's `ref_data`
   was a direct array index. Benchmark before/after on
   `pass_lnast_fromlg` and `cprop`. If regression is real, add a typed
   dense-vector storage to HHDS rather than reverting.
2. **Missing `get_tree_width(level)`.** ~8 diagnostic call sites used
   it; replacements use inline preorder-count or have been dropped.
3. **`Node_class` is a value type**, lhtree's `Tree_index` was a
   `(level, pos)` struct. Anywhere code stored a positional handle
   long-term and assumed positional stability across mutations needs
   review against the side-map convention in §4.
4. **`inou/prp` end-to-end pyrope tests.** 23 failures concentrated in
   `//inou/prp:prp-*` after the migration. Constprop's verifier reports
   unresolved cassert operands on tests that previously passed.
   Suspect: how the new attribute-backed `set_data` / `get_data`
   round-trips interact with `Symbol_table` keys (`get_sname` mixing
   name+subs). Open for a focused debug pass.
5. **Forest sharing semantics for hierarchy.** `lgraph::Hierarchy`
   stores per-instance state. Decide whether each hierarchical
   instance is a separate Forest tree (instance-data via
   `hier_storage`) or whether instances share a Tree body and
   disambiguate via `hier_pos`. The latter matches HHDS's intended
   Forest model.
6. **Concurrency.** HHDS Trees are not thread-safe. Confirm no LiveHD
   pass builds an LNAST/Node_tree concurrently from multiple threads.

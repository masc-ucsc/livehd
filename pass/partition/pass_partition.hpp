// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"
#include "pass.hpp"

namespace livehd::partition {

// One colored region handed to a body-builder hook. The region module's IO is
// already declared + materialized on `body`; the hook fills the body internals.
// `src` is the original (pre-partition) graph, kept alive so the hook can read
// node types, attributes and constant values, and walk the region connectivity.
struct Region_body {
  hhds::Graph* body = nullptr;  // fresh body to populate (IO pins materialized)
  hhds::Graph* src  = nullptr;  // original graph (read-only)
  int          color = 0;
  std::string  module_name;

  struct Port {
    std::string     name;        // body IO pin name
    hhds::Pin_class src_driver;  // input: external driver pin feeding the port
                                 // output: internal driver pin exported by the port
    int             bits = 0;
    bool            sign = false;
  };
  std::vector<Port> inputs;
  std::vector<Port> outputs;
  // Region nodes (handles into `src`). A non-owning view into a buffer the
  // partitioner keeps alive for the duration of the synchronous hook call (and
  // frees right after) -- so a whole-design flatten does not copy an O(nodes)
  // Node_class vector (~1.4 GB on a flat XSCore) into every Region_body just to
  // iterate it once. Do NOT stash this span past the hook's return.
  std::span<const hhds::Node_class> nodes;
};

// Called once per region. If unset, partition recreates the original logic.
using Body_builder = std::function<void(const Region_body&)>;

// Whole-design flatten policy for build_decomposition. `automatic` flattens
// exactly when the top's active coloring was produced by `pass.color flat`
// (coloring_info algorithm=="flat") — a flat one-color-for-everything coloring
// is the user asking for whole-design synthesis, so the hierarchy is inlined
// and a single module comes out; any other (or no) coloring keeps the classic
// per-def decomposition.
enum class Flatten_mode { off, on, automatic };

// Parse the shared `flatten` label value ("auto"|"true"|"false", plus 0/1);
// anything else is a fatal diag under `pass` and returns off.
[[nodiscard]] Flatten_mode parse_flatten_mode(std::string_view v, std::string_view pass);

// Whether build_decomposition will inline `g`'s whole hierarchy into a single
// flat def (vs. the per-def decomposition). `on`/`off` are literal; `automatic`
// resolves against g's active coloring (flat coloring => whole-design). Lets a
// caller (e.g. pass.abc's size gate) tell, before running, whether it is about
// to bit-blast the entire flattened design as one unit.
[[nodiscard]] bool flatten_is_whole_design(hhds::Graph* g, Flatten_mode mode);

// Resolve a sub-instance's child def inside `outlib`: an already-partitioned
// def resolves by name; a BODY-LESS def (a black box — a liberty cell or tie
// cell in an already-mapped netlist, an external IP decl) is cloned as an
// IO-only decl so the instance stays an opaque box and a mapped netlist can be
// re-partitioned / re-synthesized like any other lg. Returns nullptr when the
// def has a body but is missing from `outlib` (a children-first ordering bug —
// the caller reports it).
std::shared_ptr<hhds::GraphIO> resolve_or_clone_subdef(hhds::GraphLibrary* outlib, const hhds::Node_class& inst);

}  // namespace livehd::partition

// pass.partition — build a new graph_library from the colors produced by
// pass.color (task 2p-partition). One module per colored region, instantiated
// once per region in a fresh top that is LEC-equivalent to the original.
// A prior pass.color is optional: color 0 (an uncolored node) is treated as
// just another color, so an uncolored design folds into one color-0 region
// (with a single warning) instead of being rejected.
class Pass_partition : public Pass {
public:
  explicit Pass_partition(const Eprp_var& var);

  static void setup();
  static void partition(Eprp_var& var);

  // Reusable decomposition seam (task 2a-abc): partition the whole hierarchy
  // reachable from `top` (children-first) into `outlib`. With a `hook`, each
  // region body is filled by the caller (e.g. an ABC-mapped netlist) instead of
  // the original logic. When flattening (see Flatten_mode), the hierarchy is
  // structurally inlined first and ONE Partitioner runs on the flat def; a
  // single resulting region is emitted directly under the top's own name (no
  // wrapper), so a flat coloring yields exactly one output module. Returns
  // false on a fatal collect/flatten error.
  static bool build_decomposition(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, hhds::GraphLibrary* outlib,
                                  std::string_view top, bool debug_color, const livehd::partition::Body_builder& hook = {},
                                  livehd::partition::Flatten_mode flatten = livehd::partition::Flatten_mode::off);
};

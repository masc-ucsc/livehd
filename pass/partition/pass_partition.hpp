// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
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
  std::vector<Port>             inputs;
  std::vector<Port>             outputs;
  std::vector<hhds::Node_class> nodes;  // region nodes (handles into `src`)
};

// Called once per region. If unset, partition recreates the original logic.
using Body_builder = std::function<void(const Region_body&)>;

}  // namespace livehd::partition

// pass.partition — build a new graph_library from the colors produced by
// pass.color (task 2p-partition). One module per colored region, instantiated
// once per region in a fresh top that is LEC-equivalent to the original.
class Pass_partition : public Pass {
public:
  explicit Pass_partition(const Eprp_var& var);

  static void setup();
  static void partition(Eprp_var& var);

  // Reusable decomposition seam (task 2a-abc): partition the whole hierarchy
  // reachable from `top` (children-first) into `outlib`. With a `hook`, each
  // region body is filled by the caller (e.g. an ABC-mapped netlist) instead of
  // the original logic. Returns false on a fatal collect error.
  static bool build_decomposition(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, hhds::GraphLibrary* outlib,
                                  std::string_view top, bool debug_color, const livehd::partition::Body_builder& hook = {});
};

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"

namespace livehd::abc {

// Repeated port lookup while importing one node. HHDS inp_edges() eagerly
// gathers the entire node's edges; doing that once per instance port makes a
// wide region interface quadratic. Fanins must remain unchanged until the
// caller moves to another node. import_def only reads them.
class Fanin_lookup {
  // `Node_class::operator==` compares the raw nid ALONE, and a default-built
  // handle carries nid 0 -- which is a legal id (the builtin INPUT_NODE sits at
  // index 0). Without an explicit "have I filled the map yet" bit, the very
  // first lookup on such a node would match the empty cache and report every
  // port as undriven instead of reading the graph.
  bool                                                primed = false;
  hhds::Node_class                                    cached_node;
  absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> drivers;

public:
  hhds::Pin_class operator()(const hhds::Node_class& node, hhds::Port_id pid) {
    if (!primed || node != cached_node) {
      primed      = true;
      cached_node = node;
      // Do not carry a wide instance's allocation through every later cell.
      if (drivers.capacity() > 128) {
        decltype(drivers) empty;
        drivers.swap(empty);
      } else {
        drivers.clear();
      }
      const auto edges = node.inp_edges();
      drivers.reserve(edges.size());
      for (const auto& edge : edges) {
        drivers.try_emplace(edge.sink.get_port_id(), edge.driver);  // retain the first driver, as before
      }
    }
    const auto found = drivers.find(pid);
    return found == drivers.end() ? hhds::Pin_class{} : found->second;
  }
};

}  // namespace livehd::abc

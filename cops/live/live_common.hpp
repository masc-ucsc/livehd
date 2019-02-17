//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "absl/container/flat_hash_set.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

namespace Live {

class Graph_Node { // FIXME: remove an use Node_pin (same info)
public:
  LGraph *    module;
  Index_ID    idx;
  uint32_t    bit;
  std::string instance;
  // we need to take into account PIDs for subgraphs
  // for primitives, it doesn't matter since most have a single output
  Port_ID pid;

  Graph_Node(LGraph *module, Index_ID idx, uint32_t bit, const std::string &instance, Port_ID pid)
      : module(module), idx(idx), bit(bit), instance(instance), pid(pid) {}

  Graph_Node() {}

  bool operator<(const Graph_Node &rhs) const {
    return (module < rhs.module) || (module == rhs.module && idx < rhs.idx) ||
           (module == rhs.module && idx == rhs.idx && pid < rhs.pid) ||
           (module == rhs.module && idx == rhs.idx && pid == rhs.pid && bit < rhs.bit) ||
           (module == rhs.module && idx == rhs.idx && pid == rhs.pid && bit == rhs.bit && instance < rhs.instance);
  }
};

// resolves which bits are dependencies of the current bit based on node type
// when propagating backwards
int resolve_bit(LGraph *graph, Index_ID idx, uint32_t current_bit, Port_ID pin, absl::flat_hash_set<uint32_t> &bits);

// resolves which bits are dependencies of the current bit based on node type
// when propagating backwards
int resolve_bit_fwd(LGraph *graph, Index_ID idx, uint32_t current_bit, Port_ID pin, absl::flat_hash_set<uint32_t> &bits);
}  // namespace Live

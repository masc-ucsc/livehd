//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <set>
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "lgraph.hpp"

namespace Live_synth {

// FIXME: can I reduce the dependency on string by using IDs from LGraph?

// typedef std::string Net_ID;
typedef std::pair<WireName_ID, uint32_t> Net_ID;
typedef std::string                      Graph_ID;
typedef std::string                      Instance_name;

typedef Index_ID Gate_ID;

typedef absl::flat_hash_set<Net_ID>        Net_set;
typedef absl::flat_hash_set<Instance_name> Instance_set;
typedef absl::flat_hash_set<Graph_ID>      Graph_set;

typedef absl::flat_hash_set<Gate_ID> Gate_set;
}  // namespace Live_synth

using namespace Live_synth;
class Invariant_boundaries {
public:
  absl::flat_hash_map<Net_ID, Net_set>         invariant_cones;       // sips
  absl::flat_hash_map<Instance_name, Graph_ID> instance_type_map;     // all_instances
  absl::flat_hash_map<Graph_ID, Instance_set>  instance_collection;   // instances
  absl::flat_hash_map<Net_ID, Gate_set>        invariant_cone_cells;  // gate_count
  absl::flat_hash_map<Graph_ID, Graph_set>     hierarchy_tree;        // tree

  absl::flat_hash_map<Gate_ID, uint32_t> gate_appearances;  // shared_gates

  std::string top;
  std::string hierarchical_separator;

  Invariant_boundaries() {}

  Invariant_boundaries(const std::string &hier_sep) : hierarchical_separator(hier_sep) {}

  static void                  serialize(Invariant_boundaries *ib, std::ostream &ofs);
  static Invariant_boundaries *deserialize(std::istream &ifs);

  static Graph_ID get_graphID(LGraph *g) {
    return std::string(
        g->get_name());  // FIXME: It would be better to use string_view, BUT, lgraph can delete/add lgraphs names (can it???)
  }

  static LGraph *get_graph(Graph_ID id, const std::string &lgdb) { return LGraph::open(lgdb, id); }

  bool is_invariant_boundary(Net_ID net) const {
    if (net.first == 0) return false;

    return invariant_cones.find(net) != invariant_cones.end();
  }
};

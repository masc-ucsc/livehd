//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <set>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "lgraph.hpp"

namespace Live_synth {

// FIXME: can I reduce the dependency on string by using IDs from Lgraph?

// typedef std::string Net_ID;
typedef std::pair<WireName_ID, uint32_t> Net_ID;
typedef std::string                      Graph_ID;
typedef std::string                      Instance_name;

struct Net_ID_hash {
  size_t operator()(const std::pair<WireName_ID, uint32_t> &obj) const {
    size_t v = obj.first;
    v <<= 16;
    v ^= obj.second;
    return v;
  }
};

typedef absl::flat_hash_set<Net_ID, Net_ID_hash> Net_set;
typedef absl::flat_hash_set<Instance_name>       Instance_set;
typedef absl::flat_hash_set<Graph_ID>            Graph_set;

typedef absl::flat_hash_set<Index_id, Index_id_hash> Gate_set;
}  // namespace Live_synth

using namespace Live_synth;
class Invariant_boundaries {
public:
  absl::flat_hash_map<Instance_name, Graph_ID> instance_type_map;    // all_instances
  absl::flat_hash_map<Graph_ID, Instance_set>  instance_collection;  // instances
  absl::flat_hash_map<Graph_ID, Graph_set>     hierarchy_tree;       // tree

  absl::flat_hash_map<Net_ID, Net_set, Net_ID_hash>      invariant_cones;       // sips
  absl::flat_hash_map<Net_ID, Gate_set, Net_ID_hash>     invariant_cone_cells;  // gate_count
  absl::flat_hash_map<Index_id, uint32_t, Index_id_hash> gate_appearances;      // shared_gates

  std::string top;
  std::string hierarchical_separator;

  Invariant_boundaries() {}

  Invariant_boundaries(const std::string &hier_sep) : hierarchical_separator(hier_sep) {}

  static void                  serialize(Invariant_boundaries *ib, std::ostream &ofs);
  static Invariant_boundaries *deserialize(std::istream &ifs);

  static Graph_ID get_graphID(Lgraph *g) {
    return std::string(
        g->get_name());  // FIXME: It would be better to use string_view, BUT, Lgraph can delete/add Lgraphs names (can it???)
  }

  static Lgraph *get_graph(Graph_ID id, const std::string &lgdb) { return Lgraph::open(lgdb, id); }

  bool is_invariant_boundary(Net_ID net) const {
    if (net.first == 0)
      return false;

    return invariant_cones.find(net) != invariant_cones.end();
  }
};

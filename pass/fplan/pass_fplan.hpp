#pragma once

#include <functional>

#include "absl/container/flat_hash_map.h"
#include "i_resolve_header.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "pass.hpp"

class Pass_fplan : public Pass {
public:
  Pass_fplan(const Eprp_var& var);

  static void setup();

  LGraph* root_lg;  // public for debugging

  void discover_hier() {}  // skipped for now

  void collapse_hier(double area_thresh);

  void discover_reg(unsigned int beam_width);

  void pretty_dump(LGraph* lg, int indent);

  static void pass(Eprp_var& v);

private:
  void mark_hier_rec(LGraph* lg);

  void collapse_hier_rec(double area_thresh, LGraph* lg);

  void compute_depth(LGraph* lg, unsigned int depth);

  /*
  void each_graph_group(LGraph* lg, const std::function<void(LGraph*)> f, absl::flat_hash_map<LGraph*, bool>& cond) {
    f(root);

    lg->each_sub_fast([&](Node& n, Lg_type_id lgid) -> bool {
      LGraph* sub_lg = LGraph::open(path, lgid);

      // check if a valid subgraph
      if (!sub_lg || sub_lg->is_empty() || cond[sub_lg]) {
        return true;
      }

      each_graph_group(sub_lg, f, cond);

      return true;
    });
  }
  */

  using Lg_pattern = absl::flat_hash_map<LGraph*, unsigned int>;
  std::pair<int, Lg_pattern> find_most_frequent_pattern(std::vector<Lg_pattern>& l, unsigned int beam_width);

  // all data structures for a partially collapsed hierarchy are wrapped in a struct
  // to make it easy to operate on different collapsed hierarchies at the same time
  struct collapsed_info {
    absl::flat_hash_map<LGraph*, bool> area;   // true if LGraph is "collapsed"
    absl::flat_hash_map<LGraph*, int>  depth;  // depth of an LGraph
  } cli;
};

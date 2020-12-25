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

  int find_most_frequent_pattern(const unsigned int beam_width);

  // all data structures for a partially collapsed hierarchy are wrapped in a struct
  // to make it easy to operate on different collapsed hierarchies at the same time
  struct collapsed_info {
    absl::flat_hash_map<LGraph*, bool> area;   // true if LGraph is "collapsed"
  } cli;
};

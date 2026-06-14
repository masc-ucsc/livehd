// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Flop-path coloring (ported from pass/label/label_path, 2c-color). Colors
// nodes adjacent to flops/registers, propagating along FF paths and stopping at
// real wire names; clock/reset wires are skipped to avoid false aliases. An
// optional instance-name seed restricts seeding to named nodes and propagates
// forward only.

#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "color_common.hpp"
#include "hhds/graph.hpp"

namespace livehd::color {

class Color_path {
public:
  Color_path(Color_opts opts, std::string_view instance_csv = {});
  void label(hhds::Graph* g);

  static std::vector<std::string> parse_instance_names(std::string_view instance_csv);

private:
  Color_opts                     opts;
  const std::vector<std::string> instance_names;
  int                            last_free_id = 0;

  absl::flat_hash_map<hhds::Node_class, std::vector<int>> node2colors;
  absl::flat_hash_map<int, std::string>                   color2instance;
  absl::flat_hash_map<std::string, int>                   instance2color;

  int  get_free_id() { return ++last_free_id; }
  bool should_stop_fwd(const hhds::Node_class& node, int color) const;
  void propagate_fwd(const hhds::Node_class& node, int color);
  void propagate_bwd(const hhds::Node_class& node, int color);
};

}  // namespace livehd::color

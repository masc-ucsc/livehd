//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <string_view>
#include <utility>
#include <vector>

#include "cell.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "pass.hpp"
#include "absl/container/flat_hash_map.h"

class Traverse_lg : public Pass {
private:
  absl::node_hash_map<unsigned int, std::pair<std::vector<std::string>, std::vector<std::string>>> nodeIOmap;
protected:
  void do_travers(Lgraph* g);
  //FOR DEBUG:
  void get_input_node(const Node_pin &pin, std::ofstream& ofs);
  void get_output_node(const Node_pin &pin, std::ofstream& ofs);
  //FOR VECT PART:
  void get_input_node(const Node_pin &pin, std::vector<std::string>& in_vec);
  void get_output_node(const Node_pin &pin, std::vector<std::string>& out_vec);

public:
  static void travers(Eprp_var& var);

  Traverse_lg(const Eprp_var& var);

  static void setup();
};

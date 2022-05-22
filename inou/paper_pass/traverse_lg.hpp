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
#include "absl/container/flat_hash_set.h"

/*define only 1 of these:*/
//#define DEBUG //print everything
//#define KEEP_DUP //use vector
#define DE_DUP //use set 

class Traverse_lg : public Pass {
private:
#ifdef KEEP_DUP
  absl::node_hash_map<unsigned int, std::pair<std::vector<std::string>, std::vector<std::string>>> nodeIOmap;
#endif
#ifdef DE_DUP
  absl::node_hash_map<unsigned int, std::pair<absl::flat_hash_set<std::string>, absl::flat_hash_set<std::string>>> nodeIOmap;
#endif
protected:
  void do_travers(Lgraph* g);
  //FOR DEBUG:
  void get_input_node(const Node_pin &pin, std::ofstream& ofs);
  void get_output_node(const Node_pin &pin, std::ofstream& ofs);
  //FOR VECT PART:
  void get_input_node(const Node_pin &pin, std::vector<std::string>& in_vec);
  void get_output_node(const Node_pin &pin, std::vector<std::string>& out_vec);
  //FOR SET PART:
  void get_input_node(const Node_pin &pin, absl::flat_hash_set<std::string>& in_set);
  void get_output_node(const Node_pin &pin, absl::flat_hash_set<std::string>& out_set);

public:
  static void travers(Eprp_var& var);

  Traverse_lg(const Eprp_var& var);

  static void setup();
};

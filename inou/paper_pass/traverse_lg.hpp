//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <string_view>

#include "cell.hpp"
#include "lgraph.hpp"
#include "pass.hpp"

class Traverse_lg : public Pass {
protected:
  void do_travers(Lgraph* g, std::string_view module_name);
  void get_input_node(const Node_pin &pin, std::ofstream& ofs);
  void get_output_node(const Node_pin &pin, std::ofstream& ofs);

public:
  static void travers(Eprp_var& var);

  Traverse_lg(const Eprp_var& var);

  static void setup();
};

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>
#include <string_view>
#include <string>

#include "lgraph_base_core.hpp"

class LGraph_Hierarchy {
private:
  std::string_view wire_name;
  std::string_view module_name;
  std::string_view inst_names;
  std::vector<std::string_view> inst_hierarchy;

public:
  LGraph_Hierarchy();
  ~LGraph_Hierarchy();
  bool set_hierarchy(std::string_view hierarchy);
  void report();
  std::string_view get_module_name();
  std::string_view get_wire_name();
  std::string_view get_last_instance();
  std::string_view get_first_instance();
  std::string_view get_instance(Hierarchy_id i);
  std::string get_hierarchy();
  std::string get_hierarchy_upto(Hierarchy_id depth);
  Hierarchy_id get_hierarchy_depth();
  Hierarchy_id get_inst_num();
};


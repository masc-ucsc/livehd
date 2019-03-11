//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#define HIERARCHY_DEBUG 1

#include "fmt/format.h"
#include <vector>
#include <string_view>
#include <string>
#include <iostream>
#include "absl/strings/substitute.h"
#include <cassert>

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
    std::string_view get_instance(uint16_t i);
    std::string get_hierarchy();
    std::string get_hierarchy_upto(uint16_t depth);
    uint16_t get_hierarchy_depth(); 
    uint16_t get_inst_num();
};

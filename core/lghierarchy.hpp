//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#define HIERARCHY_DEBUG 1

#include "fmt/format.h"
#include <vector>
#include <map>
#include <string_view>
#include <string>
#include <iostream>
#include "absl/strings/substitute.h"
#include <cassert>
#include "lgraph.hpp"
#include "lgbench.hpp"

class LGraph_Hierarchy {
  private:
    std::string wire_name;
    std::string module_name;
    std::string inst_names;
    std::vector<std::string> inst_hierarchy;
    std::map<std::string, Lg_type_id> inst_lgid;

  public:
    LGraph_Hierarchy();
    ~LGraph_Hierarchy();
    uint16_t set_hierarchy(LGraph *top, std::string_view hierarchy);
    void report();
    std::string get_module_name();
    std::string get_wire_name();
    std::string get_last_instance();
    std::string get_first_instance();
    std::string get_instance(uint16_t i);
    std::string get_hierarchy();
    std::string get_hierarchy_upto(uint16_t depth);
    uint16_t get_hierarchy_depth(); 
    uint16_t get_inst_num();
    Lg_type_id get_lgid(std::string hierarchy);
    Lg_type_id get_lgid(uint32_t depth);
};

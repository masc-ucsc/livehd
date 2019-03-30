//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "absl/strings/substitute.h"

#include "lgraph.hpp"
#include "lghierarchy.hpp"

#define HIERARCHY_DEBUG 1

LGraph_Hierarchy::LGraph_Hierarchy() {

}

LGraph_Hierarchy::~LGraph_Hierarchy() {

}

bool LGraph_Hierarchy::set_hierarchy(std::string_view hierarchy) {

  auto module_name_end   = hierarchy.find_first_of(":") - 1;
  auto wire_name_end     = hierarchy.size() - 1;
  auto inst_names_end    = hierarchy.find_first_of("->") - 1;
  auto inst_names_start  = module_name_end + 2;

  bool result = true;
  if (module_name_end == std::string::npos) {
    result = false;
  } else if (wire_name_end == std::string::npos) {
    result = false;
  } else if (inst_names_end < inst_names_start) {
    result = false;
  }

  if (!result)
    return false;

  auto module_name_start = 0;
  auto module_name_size  = module_name_end - module_name_start + 1;
  auto inst_names_size   = inst_names_end - inst_names_start + 1;
  auto wire_name_start   = hierarchy.find_first_of("->") + 2;
  auto wire_name_size    = wire_name_end - wire_name_start + 1;

  module_name       = hierarchy.substr(module_name_start, module_name_size);
  inst_names        = hierarchy.substr(inst_names_start, inst_names_size);
  wire_name         = hierarchy.substr(wire_name_start, wire_name_size);

  inst_hierarchy = absl::StrSplit(inst_names, '.');
#ifdef HIERARCHY_DEBUG
  report();
#endif

  return true;
}

void LGraph_Hierarchy::report() {
  fmt::print("module name:     {}\n", module_name);
  fmt::print("inst hierarchy:  ");
  for (Hierarchy_id i = 0; i < inst_hierarchy.size(); i++) {
    fmt::print("{} ", inst_hierarchy[i]);
  }
  fmt::print("\nwire name:       {}\n", wire_name);
}

std::string_view LGraph_Hierarchy::get_module_name() {
  return module_name;
}

std::string_view LGraph_Hierarchy::get_wire_name() {
  return wire_name;
}

std::string_view LGraph_Hierarchy::get_last_instance() {
  return inst_hierarchy[inst_hierarchy.size() - 1];
}

std::string_view LGraph_Hierarchy::get_first_instance() {
  return inst_hierarchy[0];
}

std::string_view LGraph_Hierarchy::get_instance(Hierarchy_id depth) {
  I(depth < inst_hierarchy.size());
  return inst_hierarchy[depth];
}

std::string LGraph_Hierarchy::get_hierarchy() {
  return fmt::format("{}.{}", module_name, inst_names);
}

std::string LGraph_Hierarchy::get_hierarchy_upto(Hierarchy_id depth) {
  I(depth < inst_hierarchy.size() + 1);
  std::string inst_hier = "";
  for (Hierarchy_id i = 0; i < depth; i++) {
    inst_hier += inst_names[i];
  }
  return fmt::format("{}.{}", module_name, inst_hier);
}

Hierarchy_id LGraph_Hierarchy::get_hierarchy_depth() {
  return inst_names.size() + 1; // +1 is a module
}

Hierarchy_id LGraph_Hierarchy::get_inst_num() {
  return inst_hierarchy.size();
}

#include "lghierarchy.hpp"

LGraph_Hierarchy::LGraph_Hierarchy() {
  
}

LGraph_Hierarchy::~LGraph_Hierarchy() {

}


bool LGraph_Hierarchy::set_hierarchy(std::string_view hierarchy) {
  bool result = true;

  auto module_name_start = 0;
  auto module_name_end   = hierarchy.find_first_of(":") - 1;
  auto wire_name_start   = hierarchy.find_first_of("->") + 2;
  auto wire_name_end     = hierarchy.size() - 1;
  auto inst_names_start  = module_name_end + 2;
  auto inst_names_end    = hierarchy.find_first_of("->") - 1;
  auto module_name_size  = module_name_end - module_name_start + 1;
  auto inst_names_size   = inst_names_end - inst_names_start + 1;
  auto wire_name_size    = wire_name_end - wire_name_start + 1;
  auto module_name       = hierarchy.substr(module_name_start, module_name_size);
  auto inst_names        = hierarchy.substr(inst_names_start, inst_names_size);
  auto wire_name         = hierarchy.substr(wire_name_start, wire_name_size);

  if (module_name_end == std::string::npos) {
    result = false;
  } else if (wire_name_end == std::string::npos) {
    result = false;
  } else if (inst_names_end < inst_names_start) {
    result = false;
  }

  if (result) {
    this->wire_name      = wire_name;
    this->module_name    = module_name;
    this->inst_names     = inst_names;
    this->inst_hierarchy = absl::StrSplit(inst_names, '.');
#if HIERARCHY_DEBUG
    this->report();  
#endif
  }

  return result;
}

void LGraph_Hierarchy::report() {
  fmt::print("module name:     {}\n", this->module_name);
  fmt::print("inst hierarchy:  ");
  for (uint16_t i = 0; i < this->inst_hierarchy.size(); i++) {
    fmt::print("{} ", this->inst_hierarchy[i]);
  }
  fmt::print("\nwire name:       {}\n", this->wire_name);
}

std::string_view LGraph_Hierarchy::get_module_name() {
  return this->module_name;
}

std::string_view LGraph_Hierarchy::get_wire_name() {
  return this->wire_name;
}

std::string_view LGraph_Hierarchy::get_last_instance() {
  return this->inst_hierarchy[this->inst_hierarchy.size() - 1];
}

std::string_view LGraph_Hierarchy::get_first_instance() {
  return this->inst_hierarchy[0];
}

std::string_view LGraph_Hierarchy::get_instance(uint16_t depth) {
  assert(depth < this->inst_hierarchy.size());
  return this->inst_hierarchy[depth];
}

std::string LGraph_Hierarchy::get_hierarchy() {
  return fmt::format("{}.{}", this->module_name, this->inst_names);
}

std::string LGraph_Hierarchy::get_hierarchy_upto(uint16_t depth) {
  assert(depth < this->inst_hierarchy.size() + 1);
  std::string inst_hier = "";
  for (uint16_t i = 0; i < depth; i++) {
    inst_hier += this->inst_names[i];
  }
  return fmt::format("{}.{}", this->module_name, inst_hier);
}

uint16_t LGraph_Hierarchy::get_hierarchy_depth() {
  return this->inst_names.size() + 1; // +1 is a module
}

uint16_t LGraph_Hierarchy::get_inst_num() {
  return this->inst_hierarchy.size();
}

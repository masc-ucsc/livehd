#include "lghierarchy.hpp"

LGraph_Hierarchy::LGraph_Hierarchy() {
  this->wire_name = "";
  this->module_name = "";
  this->inst_names = "";
  this->inst_hierarchy.clear();
  this->inst_lgid.clear();
}

LGraph_Hierarchy::~LGraph_Hierarchy() {

}


uint16_t LGraph_Hierarchy::set_hierarchy(LGraph *top, std::string_view hierarchy) {
  // parsing arguments
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
    return 1;
  } else if (wire_name_end == std::string::npos) {
    return 1;
  } else if (inst_names_end < inst_names_start) {
    return 1;
  }

  this->wire_name      = wire_name;
  this->module_name    = module_name;
  this->inst_names     = inst_names;
  this->inst_hierarchy = absl::StrSplit(inst_names, '.');
#if HIERARCHY_DEBUG
  this->report();  
#endif

  // checking graph for existance of provided arguments
  const auto hier = top->get_hierarchy();
  for(auto &[name,lgid]:hier) {
    if (this->get_hierarchy().find(name) != std::string::npos) {
      this->inst_lgid[name] = lgid;
    }
  }

  if (this->inst_lgid.size() == 0) {
    return 2;
  } else if (this->inst_lgid.find(this->get_hierarchy()) == this->inst_lgid.end()) {
    return 2;
  }

  return 0;
}

void LGraph_Hierarchy::report() {
  fmt::print("module name:     {}\n", this->module_name);
  fmt::print("inst hierarchy:  ");
  for (uint16_t i = 0; i < this->inst_hierarchy.size(); i++) {
    fmt::print("{} ", this->inst_hierarchy[i]);
  }
  fmt::print("\nwire name:       {}\n", this->wire_name);
}

std::string LGraph_Hierarchy::get_module_name() {
  return this->module_name;
}

std::string LGraph_Hierarchy::get_wire_name() {
  return this->wire_name;
}

std::string LGraph_Hierarchy::get_last_instance() {
  return this->inst_hierarchy[this->inst_hierarchy.size() - 1];
}

std::string LGraph_Hierarchy::get_first_instance() {
  return this->inst_hierarchy[0];
}

std::string LGraph_Hierarchy::get_instance(uint16_t depth) {
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
    inst_hier += this->inst_hierarchy[i];
    if (i < depth - 1) {
      inst_hier += ".";
    }
  }
  return fmt::format("{}.{}", this->module_name, inst_hier);
}

uint16_t LGraph_Hierarchy::get_hierarchy_depth() {
  return this->inst_hierarchy.size() + 1; // +1 is a module
}

uint16_t LGraph_Hierarchy::get_inst_num() {
  return this->inst_hierarchy.size();
}

Lg_type_id LGraph_Hierarchy::get_lgid(std::string hierarchy) {
  return this->inst_lgid[hierarchy];
}

Lg_type_id LGraph_Hierarchy::get_lgid(uint32_t depth) {
  return this->get_lgid(this->get_hierarchy_upto(depth));
}

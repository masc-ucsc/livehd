
#include "cgen_variable_manager.hpp"

void Cgen_variable_manager::insert_variable(std::string_view new_var_name) {
  fmt::print("cgen_variable_manager: insert_variable: {}: size: {}\n", new_var_name, variable_map.size());
  // if exists, do nothing
  // else, create a variable options
  if (variable_map.find(new_var_name) == variable_map.end()) {
    variable_map.insert(std::pair<std::string_view, Variable_options*>(new_var_name, new Variable_options()));
  }
  fmt::print("cgen_variable_manager: insert_variable: final size: {}\n", variable_map.size());
}

void Cgen_variable_manager::merge_single(std::pair<std::string_view, Variable_options*> new_var) {
  // if exists, do nothing
  // else, create a variable options
  std::map<std::string_view, Variable_options*>::iterator old_var;
  old_var = variable_map.find(new_var.first);
  if (old_var != variable_map.end()) {
    Variable_options* default_var= new Variable_options();

    if (new_var.second->bits != default_var->bits) {
      old_var->second->bits = new_var.second->bits;
    }
    if (new_var.second->posedge != default_var->posedge) {
      old_var->second->posedge = new_var.second->posedge;
    }
    if (new_var.second->last != default_var->last) {
      old_var->second->last = new_var.second->last;
    }
    if (new_var.second->size != default_var->size) {
      old_var->second->size = new_var.second->size;
    }
    if (new_var.second->latch != default_var->latch) {
      old_var->second->latch = new_var.second->latch;
    }
    if (new_var.second->clk_pin != default_var->clk_pin) {
      old_var->second->clk_pin = new_var.second->clk_pin;
    }
    if (new_var.second->clk_rd_pin != default_var->clk_rd_pin) {
      old_var->second->clk_rd_pin = new_var.second->clk_rd_pin;
    }
    if (new_var.second->clk_wr_pin != default_var->clk_wr_pin) {
      old_var->second->clk_wr_pin = new_var.second->clk_wr_pin;
    }
    if (new_var.second->reset != default_var->reset) {
      old_var->second->reset = new_var.second->reset;
    }
    if (new_var.second->reset_pin != default_var->reset_pin) {
      old_var->second->reset_cycles = new_var.second->reset_cycles;
    }
    if (new_var.second->reset_cycles != default_var->reset_cycles) {
      old_var->second->reset_cycles = new_var.second->reset_cycles;
    }
    if (new_var.second->reset_async != default_var->reset_async) {
      old_var->second->reset_async = new_var.second->reset_async;
    }
  } else {
    variable_map.insert(new_var);
  }
}

void Cgen_variable_manager::merge_multiple(std::vector<std::pair<std::string_view, Variable_options*>> var_vector) {
  for (auto ele : var_vector) {
    merge_single(ele);
  }
}

std::vector<std::pair<std::string_view, Variable_options*>> Cgen_variable_manager::pop(std::set<std::string_view> outgoing_vars) {
  std::vector<std::pair<std::string_view, Variable_options*>> return_vars;

  std::map<std::string_view, Variable_options*>::iterator it;
  for (auto ele : outgoing_vars) {
    it = variable_map.find(ele);
    if (it != variable_map.end()) {
      return_vars.push_back(std::pair<std::string_view, Variable_options*>(it->first, it->second));
      variable_map.erase(it);
    }
  }

  return return_vars;
}


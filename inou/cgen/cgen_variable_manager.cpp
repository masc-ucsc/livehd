
#include "cgen_variable_manager.hpp"

void Cgen_variable_manager::insert_variable(std::string new_var_name) {
  // if exists, do nothing
  // else, create a variable options
  fmt::print("cgen_variable_manager : insert_variable : new_key : {}\n", new_var_name);
  if (variable_map.find(new_var_name) == variable_map.end()) {
    variable_map.insert(std::pair<std::string_view, Variable_options*>(new_var_name, new Variable_options()));
  }
  fmt::print("cgen_variable_manager: insert_variable: final size: {}\n", variable_map.size());
}

void Cgen_variable_manager::insert_variable(std::string_view new_var_name) {
  // if exists, do nothing
  // else, create a variable options
  fmt::print("cgen_variable_manager : insert_variable : new_key : {}\n", new_var_name);
  std::string var_name = (std::string) new_var_name;
  if (variable_map.find(var_name) == variable_map.end()) {
    fmt::print("cgen_variable_manager : adding variable to map : {}\n", var_name);
    variable_map.insert(std::pair<std::string_view, Variable_options*>(var_name, new Variable_options()));
  }
  fmt::print("cgen_variable_manager: insert_variable: final size: {}\n", variable_map.size());
}

Variable_options* Cgen_variable_manager::get(std::string var_name) {
  fmt::print("attr_var: {}\n", var_name);
  return variable_map.at(var_name);
}

Variable_options* Cgen_variable_manager::get(std::string_view var_name) {
  fmt::print("attr_var: {}\n", var_name);
  return variable_map.at((std::string) var_name);
}

void Variable_options::update_attr(std::string test_string) {
  std::vector<std::string> split_str = absl::StrSplit(test_string, "=");

  if (split_str[0] == "__bits") {
    bits = std::stoi(split_str[1], nullptr);
  } else if (split_str[0] == "__posedge") {
    if (split_str[1] == "true") {
      posedge = true;
    } else {
      posedge = false;
    }
  } else if (split_str[0] == "__last") {
    last = std::stoi(split_str[1]);
  } else if (split_str[0] == "__size") {
    size = std::stoi(split_str[1]);
  } else if (split_str[0] == "__latch") {
    if (split_str[1] == "true") {
      latch = true;
    } else {
      latch = false;
    }
  } else if (split_str[0] == "__clk_pin") {
    clk_pin = split_str[1];
  } else if (split_str[0] == "__clk_rd_pin") {
    clk_rd_pin = split_str[1];
  } else if (split_str[0] == "__clk_wr_pin") {
    clk_wr_pin = split_str[1];
  } else if (split_str[0] == "__reset") {
    reset = split_str[1];
  } else if (split_str[0] == "__reset_pin") {
    reset_pin = split_str[1];
  } else if (split_str[0] == "__reset_cycles") {
    reset_cycles = std::stoi(split_str[1]);
  } else if (split_str[0] == "__reset_async") {
    if (split_str[1] == "true") {
      reset_async = true;
    } else {
      reset_async = false;
    }
  }
}



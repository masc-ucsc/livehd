
#pragma once

#include <vector>
#include <string>
#include <set>

#include "fmt/format.h"
#include "absl/strings/substitute.h"

class Variable_options {
public:
  // flop / latch / SRAM Speific
  uint32_t bits = 0;
  bool posedge = true;
  int32_t last = -1;
  int32_t size = -1;
  bool latch = false;
  std::string clk_pin = "";
  std::string clk_rd_pin = "";
  std::string clk_wr_pin = "";
  std::string reset = "";
  std::string reset_pin = "";
  uint32_t reset_cycles = 1;
  bool reset_async = false;

  // Generic

  void update_attr(std::string test_string);
};

class Cgen_variable_manager {
private:
public:
  std::map<std::string_view, Variable_options*> variable_map;

  void insert_variable(std::string_view new_var_name);
  // merge
  void merge_single(std::pair<std::string_view, Variable_options*> new_var);
  void merge_multiple(std::vector<std::pair<std::string_view, Variable_options*>> var_vector);
  std::vector<std::pair<std::string_view, Variable_options*>> pop(std::set<std::string_view> outgoing_vars);
  Variable_options* get(std::string_view var_name);
};


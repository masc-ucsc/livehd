
#pragma once

#include <unordered_map>
#include <string>

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
  std::unordered_map<std::string, Variable_options*> variable_map;

  void insert_variable(std::string new_var_name);
  void insert_variable(std::string_view new_var_name);
  // merge
  Variable_options* get(std::string var_name);
  Variable_options* get(std::string_view var_name);
};



#pragma once

#include <vector>
#include <string>
#include <set>

#include "fmt/format.h"
#include "absl/strings/substitute.h"

class Verilog_variable_options {
private:
  // Flop / Latch / SRAM Specific
  uint32_t bits = 0;
  bool posedge = true;
  uint32_t last; // do not know what this does
  uint32_t size; // do not know what this does
  bool latch = false;
  std::string clk_pin;
  std::string clk_rd_pin;
  std::string clk_wr_pin;
  std::string reset; // check what this does
  std::string reset_pin;
  uint32_t reset_cycles = 2;
  bool reset_async = false;

  // Generic
};

class Verilog_parser_module {
private:
  std::vector<std::pair<int32_t, std::string>> node_str_buffer;
  std::set<std::string_view> statefull_set;
  std::vector<std::vector<std::pair<int32_t, std::string>>> sts_buffer_stack;
  std::vector<std::vector<std::pair<int32_t, std::string>>> sts_buffer_queue;

  std::string create_header();
  std::string create_footer();
  std::string create_always();
  std::string create_next();

  std::string indent_buffer(int32_t size);

public:
  std::string filename;

  void add_to_buffer_single(std::pair<int32_t, std::string> next, std::set<std::string_view> new_vars);
  void add_to_buffer_multiple(std::vector<std::pair<int32_t, std::string>> nodes, std::set<std::string_view> new_vars);
  void node_buffer_stack();
  void node_buffer_queue();
  std::vector<std::pair<int32_t, std::string>> pop_queue();
  std::string create_file();

  uint32_t get_variable_type(std::string_view var_name);

  Verilog_parser_module() {};
  Verilog_parser_module(std::string m_filename) : filename(m_filename) {};
};



#pragma once

#include <set>
#include <string>
#include <vector>

#include "absl/strings/substitute.h"
#include "cgen_variable_manager.hpp"
#include "fmt/format.h"

class Cpp_parser_module {
private:
  std::vector<std::pair<int32_t, std::string>>              node_str_buffer;
  std::vector<std::vector<std::pair<int32_t, std::string>>> sts_buffer_stack;
  std::vector<std::vector<std::pair<int32_t, std::string>>> sts_buffer_queue;

  std::string create_header();
  std::string create_implementation();

  std::string combinational_str;
  std::string initial_output_str;

  std::string indent_buffer(int32_t size);
  uint32_t    indent_buffer_size = 0;
  uint32_t    if_counter         = 0;

public:
  std::string           filename;
  Cgen_variable_manager var_manager;

  std::vector<std::string>                                output_vars;
  std::vector<std::string>                                arg_vars;
  std::vector<std::pair<std::string, Cpp_parser_module*>> func_calls;

  bool has_sequential = false;

  void                                         add_to_buffer_single(std::pair<int32_t, std::string> next);
  void                                         add_to_buffer_multiple(std::vector<std::pair<int32_t, std::string>> nodes);
  void                                         node_buffer_stack();
  void                                         node_buffer_queue();
  std::vector<std::pair<int32_t, std::string>> pop_queue();
  std::pair<std::string, std::string>          create_files();
  void                                         inc_indent_buffer();
  void                                         dec_indent_buffer();
  uint32_t                                     get_indent_buffer();
  void                                         inc_if_counter();
  void                                         dec_if_counter();
  uint32_t                                     get_if_counter();

  uint32_t    get_variable_type(std::string_view var_name);
  std::string process_variable(std::string_view var_name);

  Cpp_parser_module(){};
  Cpp_parser_module(std::string_view _filename) : filename(_filename){};
};

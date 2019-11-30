
#pragma once
#include "lnast.hpp"
#include "lnast_parser.hpp"
#include "cpp_parser_module.hpp"
#include "cgen_variable_manager.hpp"

class Lnast_to_cpp_parser {
private:
  // infustructure to process the nodes
  mmap_lib::Tree_level curr_statement_level = -1;
  mmap_lib::Tree_level prev_statement_level = -1;
  std::vector<mmap_lib::Tree_level> level_stack;
  std::vector<std::vector<Lnast_node>> buffer_stack;
  std::vector<Lnast_node> node_buffer;
  std::string_view memblock;
  Lnast *lnast;
  Lnast_parser lnast_parser;

  // infustructure for multiple modules
  std::string root_filename;
  Cpp_parser_module *curr_module;
  std::map<std::string, std::string> file_map;
  // key, pair(value, variables)
  std::map<std::string_view, std::string> ref_map;
  std::vector<Cpp_parser_module*> module_stack;

  // references
  std::string_view get_node_name(Lnast_node node);
  std::string get_filename(std::string filepath);
  std::map<std::string, Cpp_parser_module*> func_map;

  // infustructure
  void process_node(const mmap_lib::Tree_index &it);
  void process_top(mmap_lib::Tree_level level);
  void push_statement(mmap_lib::Tree_level level, Lnast_ntype type); // prepare for next statement
  void pop_statement();
  void flush_statements();
  void add_to_buffer(Lnast_node node);
  void process_buffer();

  bool is_number(std::string_view test_string);
  std::string_view process_number(std::string_view num);
  bool is_ref(std::string_view test_string);
  bool is_attr(std::string_view test_string);
  void inc_indent_buffer();
  void dec_indent_buffer();

  void process_assign();
  void process_as();
  void process_label();
  /*
  void process_and();
  void process_xor();
  void process_plus();
  void process_gt();
  */
  void process_if();
  void process_func_call();
  void process_func_def();

  void process_operator();

public:
  std::string buffer;
  int32_t indent_buffer_size = -1;

  Lnast_to_cpp_parser(std::string_view _memblock, Lnast *_lnast)
    : memblock(_memblock), lnast(_lnast) { };

  std::map<std::string, std::string> stringify(std::string filepath);
};


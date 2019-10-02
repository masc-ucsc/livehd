
#pragma once
#include "lnast.hpp"
#include "lnast_parser.hpp"

class Verilog_parser_module {
private:
  std::vector<std::pair<int32_t, std::string>> node_str_buffer;
  std::set<std::string> statefull_set;

  std::string create_header();
  std::string create_footer();
  std::string create_always();
  std::string create_next();

  std::string indent_buffer(int32_t size);

public:
  std::string filename;
  void add_to_buffer(std::pair<int32_t, std::string> next, std::set<std::string> new_vars);
  std::string create_file();

  uint32_t get_variable_type(std::string var_name);

  Verilog_parser_module() {};
  Verilog_parser_module(std::string filename) : filename(filename) {};
};

class Lnast_to_verilog_parser {
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
  Verilog_parser_module *curr_module;
  std::map<std::string, std::string> file_map;
  // key, pair(value, variables)
  std::map<std::string_view, std::pair<std::string, std::set<std::string>>> ref_map;
  std::vector<std::string> sts_buffer_stack;
  std::vector<std::string> sts_buffer_queue;

  // references
  absl::flat_hash_map<Lnast_ntype_id, std::string> ntype2str;
  void setup_ntype_str_mapping();
  std::string_view get_node_name(Lnast_node node);
  std::string get_filename(std::string filepath);

  // infustructure
  void process_node(const mmap_lib::Tree_index &it);
  void process_top(mmap_lib::Tree_level level);
  void push_statement(mmap_lib::Tree_level level, Lnast_ntype_id type); // prepare for next statement
  void pop_statement(mmap_lib::Tree_level level, Lnast_ntype_id type);
  void add_to_buffer(Lnast_node node);
  void process_buffer();

  bool is_number(std::string_view test_string);
  std::string_view process_number(std::string_view num);
  bool is_ref(std::string_view test_string);
  void inc_indent_buffer();
  void dec_indent_buffer();

  void process_pure_assign();
  /*
  void process_as();
  */
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

  Lnast_to_verilog_parser(std::string_view _memblock, Lnast *_lnast)
    : memblock(_memblock), lnast(_lnast) { setup_ntype_str_mapping(); };
  std::string ntype_dbg(Lnast_ntype_id ntype);
  std::map<std::string, std::string> stringify(std::string filepath);
};


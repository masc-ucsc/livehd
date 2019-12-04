
#pragma once
#include "lnast.hpp"
#include "lnast_parser.hpp"

class Lnast_to_pyrope_parser {
private:
  mmap_lib::Tree_level curr_statement_level = -1;
  mmap_lib::Tree_level prev_statement_level = -1;
  std::vector<mmap_lib::Tree_level> level_stack;
  std::vector<std::vector<Lnast_node>> buffer_stack;
  std::vector<Lnast_node> node_buffer;
  std::string_view memblock;
  Lnast *lnast;
  std::string node_str_buffer;

  std::map<std::string_view, std::string> ref_map;
  std::vector<std::string> sts_buffer_stack;
  std::vector<std::string> sts_buffer_queue;
  int32_t indent_buffer_size = -1;

  void process_node(const mmap_lib::Tree_index &it);
  void process_top(mmap_lib::Tree_level level);
  void push_statement(mmap_lib::Tree_level level, Lnast_ntype type); // prepare for next statement
  void pop_statement();
  void flush_statements();
  void add_to_buffer(Lnast_node node);
  void process_buffer();

  std::string_view get_node_name(Lnast_node node);
  void flush_it(std::vector<Lnast_node>::iterator it);
  std::string_view join_it(std::vector<Lnast_node>::iterator it, std::string del);
  bool is_number(std::string_view test_string);
  std::string_view process_number(std::string_view num_string);
  bool is_ref(std::string_view test_string);
  void inc_indent_buffer();
  void dec_indent_buffer();
  std::string indent_buffer();

  void process_assign(std::string_view str);
  void process_label();

  void process_if();
  void process_func_call();
  void process_func_def();

  void process_operator();

public:
  std::string buffer;

  Lnast_to_pyrope_parser(std::string_view m_memblock, Lnast *m_lnast)
    : memblock(m_memblock), lnast(m_lnast) { };
  std::string stringify();
};


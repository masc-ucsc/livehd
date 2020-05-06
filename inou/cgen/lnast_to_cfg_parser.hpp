#pragma once

#include "lnast_to_xxx.hpp"

class Lnast_to_cfg_parser : public Lnast_to_xxx {
private:
  uint32_t                             k_next;
  mmap_lib::Tree_level                 curr_statement_level = -1;
  mmap_lib::Tree_level                 prev_statement_level = -1;
  std::vector<mmap_lib::Tree_level>    level_stack;
  std::vector<uint32_t>                k_stack;
  std::vector<std::vector<Lnast_node>> buffer_stack;
  std::vector<Lnast_node>              node_buffer;
  std::vector<std::vector<uint32_t>>   if_buffer_stack;
  std::vector<uint32_t>                if_buffer;
  std::string                          node_str_buffer;

  void process_node(const mmap_lib::Tree_index &it);
  void process_top(mmap_lib::Tree_level level);
  void push_statement(mmap_lib::Tree_level level);  // prepare for next statement
  void pop_statement(mmap_lib::Tree_level level, Lnast_ntype type);
  void flush_stmts();
  void add_to_buffer(Lnast_node node);
  void process_buffer();

  std::string_view get_node_name(Lnast_node node);
  void             flush_it(std::vector<Lnast_node>::iterator it);

  void process_if();
  void process_func_call();
  void process_func_def();

  void process_operator();

public:
  /* Lnast_to_cfg_parser(std::shared_ptr<Lnast> _lnast, std::string_view _path) : Lnast_to_xxx(_lnast, _path){}; */
  Lnast_to_cfg_parser(std::unique_ptr<Lnast> _lnast, std::string_view _path) : Lnast_to_xxx(std::move(_lnast), _path){};

  void generate() final;
};

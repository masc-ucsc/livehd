
#pragma once
#include "lnast.hpp"
#include "lnast_parser.hpp"

class Lnast_to_cfg_parser {
private:
  uint32_t k_num;
  Tree_level curr_statement_level = -1;
  Tree_level prev_statement_level = -1;
  std::vector<Tree_level> level_stack;
  std::vector<std::vector<Lnast_node>> buffer_stack;
  std::vector<Lnast_node> node_buffer;
  std::string_view memblock;
  Language_neutral_ast<Lnast_node> *lnast;
  Lnast_parser lnast_parser;

public:
  Lnast_to_cfg_parser(std::string_view memblock, Language_neutral_ast<Lnast_node> *lnast) 
    : memblock(memblock), lnast(lnast) { setup_ntype_str_mapping(); };
  std::string ntype_dbg(Lnast_ntype_id ntype);
  void stringify();

private:
  absl::flat_hash_map<Lnast_ntype_id, std::string> ntype2str;
  void setup_ntype_str_mapping();

  void process_top(Tree_level level);
  void process_statements(Tree_level level); // prepare for next statement
  void pop_statement();
  void add_to_buffer(Lnast_node node);
  void process_buffer();
};


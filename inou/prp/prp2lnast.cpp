//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>

#include "prp2lnast.hpp"

#include "fmt/format.h"
#include "pass.hpp"

extern "C" TSLanguage *tree_sitter_pyrope();

Prp2lnast::Prp2lnast(const mmap_lib::str filename, const mmap_lib::str module_name) {
  lnast = std::make_unique<Lnast>(module_name);

  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

  {
    auto ss = std::ostringstream{};
    std::ifstream file(filename.to_s());
    ss << file.rdbuf();
    prp_file = ss.str();
  }

  parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_pyrope());

  TSTree *tst_tree = ts_parser_parse_string(parser, NULL, prp_file.data(), prp_file.size());
  ts_root_node = ts_tree_root_node(tst_tree);
}

Prp2lnast::~Prp2lnast() {

  ts_parser_delete(parser);
}

void Prp2lnast::process_stmt_seq(const TSNode &node) {
  mmap_lib::str node_type(ts_node_type(node));
  if (node_type != "stmt_seq") {
    return Pass::error("invalid tree-sitter stmt_seq node");
  }

}

void Prp2lnast::process_root(const TSNode &node) {
  mmap_lib::str node_type(ts_node_type(node));
  if (node_type != "start") {
    return Pass::error("invalid tree-sitter root node");
  }

  auto num_children = ts_node_child_count(node);
  if (num_children==0)
    return;

  if (num_children >1) {
    return Pass::error("invalid tree-sitter root number of children");
  }

  TSNode child = ts_node_child(node, 0);
  process_stmt_seq(child);
}


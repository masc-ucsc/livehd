//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>

#include "lhtree.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "hif/hif_read.hpp"

class Lnast_hif_reader {
public:
  explicit Lnast_hif_reader(std::string_view _filename)
    : filename(_filename) {}
  
  std::unique_ptr<Lnast> read_all() {
    lnast = std::make_unique<Lnast>();
    rd = Hif_read::open(filename);
    is_top = true;
    process_header_stmts();
    while (rd->next_stmt()) {
      process_hif_stmt();
    } ;
    rd = nullptr;
    return std::move(lnast);
  }

protected:
  bool is_top;

  std::string filename;

  std::unique_ptr<Lnast> lnast;
  std::shared_ptr<Hif_read> rd;

  std::stack<lh::Tree_index> tree_index;

  Hif_read::Statement cur_stmt;

  void process_header_stmts() {
    // NOTE: Skip first header (HIF version & tool name/version)
    rd->next_stmt();
    auto cur_stmt = rd->get_current_stmt();
    for (const auto &te : cur_stmt.attr) {
      if (te.lhs == "module_name") {
        lnast->set_top_module_name(te.rhs);
      }
    }
    rd->next_stmt();
  }

  void process_hif_stmt() {
    cur_stmt = rd->get_current_stmt();
    if (cur_stmt.is_end()) {
      tree_index.pop();
      return;
    }
    auto n = Lnast_node(
      Lnast_ntype(static_cast<Lnast_ntype::Lnast_ntype_int>(cur_stmt.type & 0x00FF)),
      State_token(0, 0, 0, 0, cur_stmt.instance)
    );
    lh::Tree_index i;
    if (is_top) {
      lnast->set_root(n);
      i = lh::Tree_index::root();
      is_top = false;
    } else {
      i = lnast->add_child(tree_index.top(), n);
    }
    if (cur_stmt.is_open_call()) {
      tree_index.push(i);
    }
  }

};

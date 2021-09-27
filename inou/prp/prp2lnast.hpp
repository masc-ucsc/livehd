//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "tree_sitter/api.h"

#include "lnast.hpp"
#include "lnast_ntype.hpp"

class Prp2lnast {
protected:
  std::unique_ptr<Lnast> lnast;

  std::string prp_file;
  TSParser   *parser;
  TSNode      ts_root_node;

  void process_multiple_stmt(TSTreeCursor *tc);
  void process_stmt_seq(TSTreeCursor *tc);
  void process_root();

public:
  Prp2lnast(const mmap_lib::str filename, const mmap_lib::str module_name);

  ~Prp2lnast();

  std::unique_ptr<Lnast> get_lnast() {
    return std::move(lnast);
  }
  void dump_tree_sitter() const;
  void dump_tree_sitter(TSTreeCursor *tc, int level) const;
  void dump() const;
};


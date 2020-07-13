#pragma once

#include "lnast.hpp"

class Code_gen {
protected:
  //Lnast *top;
  std::shared_ptr<Lnast> lnast;
  std::string_view       path;
  std::string            buffer_to_print;
public:
  Code_gen(std::shared_ptr<Lnast>_lnast, std::string_view _path);
  //virtual void generate() = 0;
  void generate();
  void do_stmts(const mmap_lib::Tree_index& stmt_node_index);
  virtual std::string_view stmt_sep() = 0;
};


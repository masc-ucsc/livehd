#pragma once

#include "lnast.hpp"

class Code_gen {
protected:
  //Lnast *top;
  std::shared_ptr<Lnast> lnast;
  std::string_view       path;
  std::string            buffer_to_print = "";
public:
  Code_gen(std::shared_ptr<Lnast>_lnast, std::string_view _path);
  //virtual void generate() = 0;
  void generate();
  void do_stmts(const mmap_lib::Tree_index& stmt_node_index);
  void do_assign(const mmap_lib::Tree_index& assign_node_index);
  bool is_temp_var(std::string_view test_string);//can go to private/protected section!?
  std::string_view get_node_name(Lnast_node node);//can go to private/protected section!?
  virtual std::string_view stmt_sep() = 0;
};


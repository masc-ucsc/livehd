#pragma once

#include "lnast.hpp"
#include "inou_code_gen.hpp"
#include "lnast_generic_parser.hpp"
#include "code_gen_all_lang.hpp"

class Code_gen {
protected:
  //Lnast *top;
  std::shared_ptr<Lnast> lnast;
  std::string_view       path;
  std::string            buffer_to_print = "";
  std::map<std::string_view, std::string> ref_map;
  //enum class Code_gen_type { Type_verilog, Type_prp, Type_cfg, Type_cpp };
private:
  std::unique_ptr<Code_gen_all_lang> lnast_to;
  int indendation = 0;
  std::string indent();
public:
  Code_gen(Inou_code_gen::Code_gen_type code_gen_type, std::shared_ptr<Lnast>_lnast, std::string_view _path);
  //virtual void generate() = 0;
  void generate();
  void do_stmts(const mmap_lib::Tree_index& stmt_node_index);
  void do_assign(const mmap_lib::Tree_index& assign_node_index);
  void do_op(const mmap_lib::Tree_index& op_node_index);
  void do_dot(const mmap_lib::Tree_index& dot_node_index);
  void do_if(const mmap_lib::Tree_index& dot_node_index);
  void do_cond(const mmap_lib::Tree_index& cond_node_index);
  void do_tuple(const mmap_lib::Tree_index& tuple_node_index);
  std::string resolve_tuple_assign(const mmap_lib::Tree_index& tuple_assign_index);
  bool is_temp_var(std::string_view test_string);//can go to private/protected section!?
  std::string_view get_node_name(Lnast_node node);//can go to private/protected section!?
  bool             is_number(std::string_view test_string);
  std::string_view process_number(std::string_view num_string);
 // virtual std::string_view stmt_sep() = 0;
};


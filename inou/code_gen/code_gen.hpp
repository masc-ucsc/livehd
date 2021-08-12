#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "code_gen_all_lang.hpp"
#include "inou_code_gen.hpp"
#include "lnast.hpp"
#include "lnast_generic_parser.hpp"
#include "file_output.hpp"

class Code_gen {
protected:
  // Lnast *top;
  std::shared_ptr<Lnast>             lnast;
  mmap_lib::str                      path;
  mmap_lib::str                      odir;
  std::shared_ptr<File_output>       buffer_to_print;
  std::map<mmap_lib::str, mmap_lib::str> ref_map;
  // enum class Code_gen_type { Type_verilog, Type_prp, Type_cfg, Type_cpp };
private:
  std::unique_ptr<Code_gen_all_lang> lnast_to;
  int                                indendation = 0;
  mmap_lib::str                        indent() const;
  std::vector<mmap_lib::str>      const_vect;

public:
  Code_gen(Inou_code_gen::Code_gen_type code_gen_type, std::shared_ptr<Lnast> _lnast, const mmap_lib::str &_path,
           const mmap_lib::str &_odir);
  // virtual void generate() = 0;
  void        generate();
  void        do_stmts(const mmap_lib::Tree_index& stmt_node_index);
  void        do_assign(const mmap_lib::Tree_index& assign_node_index, std::vector<mmap_lib::str>& hier_tup_vec, bool hier_tup_assign = false);
  void        do_for(const mmap_lib::Tree_index& assign_node_index);
  void        do_while(const mmap_lib::Tree_index& assign_node_index);
  void        do_op(const mmap_lib::Tree_index& op_node_index, const mmap_lib::str& op_type);
  void        do_dot(const mmap_lib::Tree_index& dot_node_index, const mmap_lib::str& select_type);
  void        do_if(const mmap_lib::Tree_index& dot_node_index);
  void        do_cond(const mmap_lib::Tree_index& cond_node_index);
  void        do_tuple(const mmap_lib::Tree_index& tuple_node_index);
  void        do_select(const mmap_lib::Tree_index& select_node_index, const mmap_lib::str& select_type);
  void        do_func_def(const mmap_lib::Tree_index& func_def_node_index);
  void        do_func_call(const mmap_lib::Tree_index& func_def_node_index);
  void        do_get_mask(const mmap_lib::Tree_index& tposs_node_index);
  void        do_set_mask(const mmap_lib::Tree_index& tposs_node_index);
  void        do_tposs(const mmap_lib::Tree_index& tposs_node_index);

  mmap_lib::str resolve_tuple_assign(const mmap_lib::Tree_index& tuple_assign_index);
  mmap_lib::str resolve_func_cond(const mmap_lib::Tree_index& func_cond_index);

  static bool    is_temp_var(const mmap_lib::str &test_string);        // can go to private/protected section!?
  static bool    has_DblUndrScor(const mmap_lib::str &test_string);    // can go to private/protected section!?
  static bool    is_number(const mmap_lib::str &test_string);

  static mmap_lib::str process_number(const mmap_lib::str &num_string);
  
  mmap_lib::str get_fname(const mmap_lib::str &fname, const mmap_lib::str &outdir);

};

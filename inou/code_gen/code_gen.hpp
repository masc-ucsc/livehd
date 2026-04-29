#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "code_gen_all_lang.hpp"
#include "file_output.hpp"
#include "inou_code_gen.hpp"
#include "lnast.hpp"
#include "lnast_generic_parser.hpp"

class Code_gen {
protected:
  // Lnast *top;
  std::shared_ptr<Lnast>             lnast;
  std::string                        path;
  std::string                        odir;
  std::shared_ptr<File_output>       buffer_to_print;
  std::map<std::string, std::string> ref_map;
  // enum class Code_gen_type { Type_verilog, Type_prp, Type_cfg, Type_cpp };
private:
  std::unique_ptr<Code_gen_all_lang> lnast_to;
  int                                indendation = 0;
  std::string                        indent() const;
  std::vector<std::string>           const_vect;

public:
  Code_gen(Inou_code_gen::Code_gen_type code_gen_type, std::shared_ptr<Lnast> _lnast, std::string_view _path,
           std::string_view _odir);
  // virtual void generate() = 0;
  void generate();
  void do_stmts(const Lnast_nid& stmt_node_index);
  void do_assign(const Lnast_nid& assign_node_index, std::vector<std::string>& hier_tup_vec, bool hier_tup_assign = false);
  void do_for(const Lnast_nid& assign_node_index);
  void do_while(const Lnast_nid& assign_node_index);
  void do_op(const Lnast_nid& op_node_index, std::string_view op_type);
  void do_dot(const Lnast_nid& dot_node_index, std::string_view select_type);
  void do_if(const Lnast_nid& dot_node_index);
  void do_cond(const Lnast_nid& cond_node_index);
  void do_tuple(const Lnast_nid& tuple_node_index);
  void do_select(const Lnast_nid& select_node_index, std::string_view select_type);
  void do_attr_set(const Lnast_nid& attr_set_node_index);
  void do_delay_assign(const Lnast_nid& delay_assign_node_index);
  void do_func_def(const Lnast_nid& func_def_node_index);
  void do_func_call(const Lnast_nid& func_def_node_index);
  void do_get_mask(const Lnast_nid& tposs_node_index);
  void do_set_mask(const Lnast_nid& tposs_node_index);
  void do_tposs(const Lnast_nid& tposs_node_index);

  std::string resolve_tuple_assign(const Lnast_nid& tuple_assign_index);
  std::string resolve_func_cond(const Lnast_nid& func_cond_index);

  static bool is_temp_var(std::string_view test_string);      // can go to private/protected section!?
  static bool has_DblUndrScor(std::string_view test_string);  // can go to private/protected section!?
  static bool is_number(std::string_view test_string);

  static std::string process_number(std::string_view num_string);

  std::string get_fname(std::string_view fname, std::string_view outdir);
};

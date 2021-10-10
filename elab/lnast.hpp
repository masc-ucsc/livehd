//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <stack>

#include "elab_scanner.hpp"
#include "lnast_ntype.hpp"
#include "mmap_tree.hpp"
#include "mmap_str.hpp"

using Lnast_nid                     = mmap_lib::Tree_index;
using Phi_rtable                    = absl::flat_hash_map<mmap_lib::str, Lnast_nid>;  // rtable = resolve_table
using Cnt_rtable                    = absl::flat_hash_map<mmap_lib::str, int16_t>;
using Selc_lrhs_table               = absl::flat_hash_map<Lnast_nid, std::pair<bool, Lnast_nid>>;  // sel -> (lrhs, paired opr node)
using Tuple_var_1st_scope_ssa_table = absl::flat_hash_map<mmap_lib::str, Lnast_nid>;            // rtable = resolve_table

// tricky old C macro to avoid redundant code from function overloadings
#define CREATE_LNAST_NODE(type)                                                                                \
  static Lnast_node create##type() { return Lnast_node(Lnast_ntype::create##type(), Etoken(0, 0, 0, 0, ""_str)); } \
  static Lnast_node create##type(const mmap_lib::str str) {                                                 \
    return Lnast_node(Lnast_ntype::create##type(), Etoken(0, 0, 0, 0, str));                                 \
  }                                                                                                            \
  static Lnast_node create##type(const mmap_lib::str str, uint32_t line_num) {                                  \
    return Lnast_node(Lnast_ntype::create##type(), Etoken(0, 0, 0, line_num, str));                          \
  }                                                                                                            \
  static Lnast_node create##type(const mmap_lib::str str, uint32_t line_num, uint64_t pos1, uint64_t pos2) {    \
    return Lnast_node(Lnast_ntype::create##type(), Etoken(0, pos1, pos2, line_num, str));                    \
  }                                                                                                            \
  static Lnast_node create##type(const Etoken &new_token) { return Lnast_node(Lnast_ntype::create##type(), new_token); }

#define CREATE_LNAST_NODE_sv(type)                                                                          \
  static Lnast_node create##type(std::string_view sview) {                                                  \
    return Lnast_node(Lnast_ntype::create##type(), Etoken(0, 0, 0, 0, mmap_lib::str(sview)));               \
  }                                                                                                         \
  static Lnast_node create##type(std::string_view sview, uint32_t line_num) {                               \
    return Lnast_node(Lnast_ntype::create##type(), Etoken(0, 0, 0, line_num, mmap_lib::str(sview)));        \
  }                                                                                                         \
  static Lnast_node create##type(std::string_view sview, uint32_t line_num, uint64_t pos1, uint64_t pos2) { \
    return Lnast_node(Lnast_ntype::create##type(), Etoken(0, pos1, pos2, line_num, mmap_lib::str(sview)));  \
  }                                                                                                         \

struct Lnast_node {
  Lnast_ntype type;
  Etoken      token;
  int16_t     subs;  // ssa subscript

  constexpr Lnast_node() : type(Lnast_ntype::create_invalid()), subs(0) {}
  constexpr Lnast_node(Lnast_ntype _type) : type(_type), subs(0) {}
  constexpr Lnast_node(Lnast_ntype _type, const Etoken &_token) : type(_type), token(_token), subs(0) {}

  Lnast_node(Lnast_ntype _type, const Etoken &_token, int16_t _subs) : type(_type), token(_token), subs(_subs) {
    I(!type.is_invalid());
  }

  constexpr bool is_invalid() const { return type.is_invalid(); }
  void           dump() const;

  CREATE_LNAST_NODE(_invalid)

  CREATE_LNAST_NODE(_top)
  CREATE_LNAST_NODE(_stmts)
  CREATE_LNAST_NODE(_if)
  CREATE_LNAST_NODE(_uif)
  CREATE_LNAST_NODE(_for)
  CREATE_LNAST_NODE(_while)
  CREATE_LNAST_NODE(_func_call)
  CREATE_LNAST_NODE(_func_def)

  CREATE_LNAST_NODE(_assign)
  CREATE_LNAST_NODE(_dp_assign)
  CREATE_LNAST_NODE(_mut)

  CREATE_LNAST_NODE(_bit_and)
  CREATE_LNAST_NODE(_bit_or)
  CREATE_LNAST_NODE(_bit_not)
  CREATE_LNAST_NODE(_bit_xor)

  CREATE_LNAST_NODE(_reduce_or)

  CREATE_LNAST_NODE(_logical_and)
  CREATE_LNAST_NODE(_logical_or)
  CREATE_LNAST_NODE(_logical_not)

  CREATE_LNAST_NODE(_plus)
  CREATE_LNAST_NODE(_minus)
  CREATE_LNAST_NODE(_mult)
  CREATE_LNAST_NODE(_div)
  CREATE_LNAST_NODE(_mod)

  CREATE_LNAST_NODE(_shl)
  CREATE_LNAST_NODE(_sra)

  CREATE_LNAST_NODE(_sext)
  CREATE_LNAST_NODE(_set_mask)

  CREATE_LNAST_NODE(_get_mask)
  CREATE_LNAST_NODE(_mask_and)
  CREATE_LNAST_NODE(_mask_popcount)
  CREATE_LNAST_NODE(_mask_xor)

  CREATE_LNAST_NODE(_is)
  CREATE_LNAST_NODE(_ne)
  CREATE_LNAST_NODE(_eq)
  CREATE_LNAST_NODE(_lt)
  CREATE_LNAST_NODE(_le)
  CREATE_LNAST_NODE(_gt)
  CREATE_LNAST_NODE(_ge)

  CREATE_LNAST_NODE(_ref)
  CREATE_LNAST_NODE(_const)

  CREATE_LNAST_NODE(_tuple_concat)
  CREATE_LNAST_NODE(_tuple_add)
  CREATE_LNAST_NODE(_tuple_get)
  CREATE_LNAST_NODE(_tuple_set)

  CREATE_LNAST_NODE(_attr_set)
  CREATE_LNAST_NODE(_attr_get)

  CREATE_LNAST_NODE(_err_flag)
  CREATE_LNAST_NODE(_phi)
  CREATE_LNAST_NODE(_hot_phi)
};

class Lnast : public mmap_lib::tree<Lnast_node> {
private:
  mmap_lib::str      top_module_name;
  mmap_lib::str      source_filename;
  Lnast_nid        undefined_var_nid;
  uint32_t         tmp_var_cnt = 0;

  void      do_ssa_trans(const Lnast_nid &top_nid);
  void      ssa_lhs_handle_a_statement(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid);
  void      ssa_rhs_handle_a_statement(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid);
  void      ssa_lhs_if_subtree(const Lnast_nid &if_nid);
  void      ssa_rhs_if_subtree(const Lnast_nid &if_nid);
  void      opr_lhs_merge_if_subtree(const Lnast_nid &if_nid);
  void      opr_lhs_merge_handle_a_statement(const Lnast_nid &opr_nid);
  void      ssa_handle_phi_nodes(const Lnast_nid &if_nid);
  void      resolve_phi_nodes(const Lnast_nid &cond_nid, Phi_rtable &true_table, Phi_rtable &false_table);
  void      update_phi_resolve_table(const Lnast_nid &psts_nid, const Lnast_nid &target_nid);
  bool      has_else_stmts(const Lnast_nid &if_nid);
  void      add_phi_node(const Lnast_nid &cond_nid, const Lnast_nid &t_nid, const Lnast_nid &f_nid);
  Lnast_nid get_complement_nid(const mmap_lib::str &brother_name, const Lnast_nid &psts_nid, bool false_path);
  Lnast_nid check_phi_table_parents_chain(const mmap_lib::str &brother_name, const Lnast_nid &psts_nid);
  void      resolve_ssa_lhs_subs(const Lnast_nid &psts_nid);
  void      resolve_ssa_rhs_subs(const Lnast_nid &psts_nid);
  void      opr_lhs_merge(const Lnast_nid &psts_nid);
  void      update_global_lhs_ssa_cnt_table(const Lnast_nid &target_nid);
  void      respect_latest_global_lhs_ssa(const Lnast_nid &target_nid);
  int8_t    check_rhs_cnt_table_parents_chain(const Lnast_nid &psts_nid, const Lnast_nid &target_key);
  void      update_rhs_ssa_cnt_table(const Lnast_nid &psts_nid, const Lnast_nid &target_key);
  void      analyze_selc_lrhs(const Lnast_nid &psts_nid);
  void      analyze_selc_lrhs_if_subtree(const Lnast_nid &if_nid);
  void      analyze_selc_lrhs_handle_a_statement(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid);
  void      insert_implicit_dp_parent(const Lnast_nid &opr_nid);

  bool is_special_case_of_sel_rhs(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid);
  void ssa_rhs_handle_a_operand(const Lnast_nid &gpsts_nid, const Lnast_nid &opd_nid);  // gpsts = grand parent
  void ssa_rhs_handle_a_operand_special(const Lnast_nid &gpsts_nid, const Lnast_nid &opd_nid);

  // tuple operator process
  void             trans_tuple_opr(const Lnast_nid &pats_nid);  // from sel to tuple_add/get
  void             trans_tuple_opr_if_subtree(const Lnast_nid &if_nid);
  void             trans_tuple_opr_handle_a_statement(const Lnast_nid &pats_nid, const Lnast_nid &opr_nid);
  bool             check_tuple_table_parents_chain(const Lnast_nid &psts_nid, const mmap_lib::str &ref_name);
  void             sel2local_tuple_chain(const Lnast_nid &pats_nid, Lnast_nid &sel_nid);
  void             merge_tconcat_paired_assign(const Lnast_nid &psts_nid, const Lnast_nid &concat_nid);
  void             rename_to_real_tuple_name(const Lnast_nid &psts_nid, const Lnast_nid &tup_nid);
  bool             is_scalar_attribute_related(const Lnast_nid &opr_nid);
  void             selc2attr_set_get(const Lnast_nid &psts_nid, Lnast_nid &opr_nid);
  void             update_tuple_var_table(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid);
  bool             update_tuple_var_1st_scope_ssa_table(const Lnast_nid &psts_nid, const Lnast_nid &target_nid);
  bool             check_tuple_var_1st_scope_ssa_table_parents_chain(const Lnast_nid &psts_nid, const mmap_lib::str &ref_name,
                                                                     const Lnast_nid &src_if_nid);
  void             merge_hierarchical_attr_set(Lnast_nid &opr_nid);
  void             collect_hier_tuple_nids(Lnast_nid &opr_nid, std::stack<Lnast_nid> &stk_tuple_fields);
  mmap_lib::str    create_tmp_var();

  // hierarchical statements node -> symbol table
  absl::flat_hash_map<Lnast_nid, Phi_rtable>       phi_resolve_tables;
  absl::flat_hash_map<Lnast_nid, Cnt_rtable>       ssa_rhs_cnt_tables;
  absl::flat_hash_map<Lnast_nid, Selc_lrhs_table>  selc_lrhs_tables;
  absl::flat_hash_map<Lnast_nid, Phi_rtable>       new_added_phi_node_tables;  // for each if-subtree scope
  absl::flat_hash_set<mmap_lib::str>               tuplized_table;
  absl::flat_hash_map<mmap_lib::str, Lnast_nid>    candidates_update_phi_resolve_table;
  absl::flat_hash_map<mmap_lib::str, int16_t>      global_ssa_lhs_cnt_table;

  // for chaining parent tuple-chain and local tuple chain, only record the first tuple variable appeared in each local scope
  absl::flat_hash_map<Lnast_nid, Phi_rtable>       tuple_var_1st_scope_ssa_tables;

  absl::flat_hash_set<mmap_lib::str> collected_hier_tuple_reg_name;

  // populated during LG->LN pass, maps name -> bitwidth
  absl::flat_hash_map<mmap_lib::str, uint32_t> from_lgraph_bw_table;

  uint32_t tup_internal_cnt = 0;

public:
  explicit Lnast() : top_module_name("noname"), source_filename("") {}
  ~Lnast();
  explicit Lnast(const mmap_lib::str &_module_name) : top_module_name(_module_name), source_filename("") {}
  explicit Lnast(const mmap_lib::str &_module_name, const mmap_lib::str &_file_name)
      : top_module_name(_module_name), source_filename(_file_name) {}

  void ssa_trans() { do_ssa_trans(mmap_lib::Tree_index::root()); };

  const mmap_lib::str &get_top_module_name() const { return top_module_name; }
  const mmap_lib::str &get_source() const { return source_filename; }

  bool             is_lhs(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid) const;
  static bool      is_register(const mmap_lib::str &name) { return name.front() == '#'; }
  static bool      is_output(const mmap_lib::str &name) { return name.front() == '%'; }
  static bool      is_input(const mmap_lib::str &name) { return name.front() == '$'; }
  const mmap_lib::str &get_name(const Lnast_nid &nid) const { return get_data(nid).token.get_text(); }
  const mmap_lib::str &get_vname(const Lnast_nid &nid) const {
    return get_data(nid).token.get_text();
  }

  Lnast_ntype get_type(const Lnast_nid &nid) const { return get_data(nid).type; }
  int16_t     get_subs(const Lnast_nid &nid) const { return get_data(nid).subs; }
  Etoken      get_token(const Lnast_nid &nid) const { return get_data(nid).token; }
  mmap_lib::str get_sname(const Lnast_nid &nid) const {  // sname = ssa name
    if (get_type(nid).is_const())
      return get_name(nid);
    return mmap_lib::str::concat(get_name(nid), "_"_str, get_subs(nid));
  }

  // bitwidth table functions
  bool     is_in_bw_table(const mmap_lib::str &name) const;
  uint32_t get_bitwidth(const mmap_lib::str &name) const;
  void     set_bitwidth(const mmap_lib::str &name, const uint32_t bitwidth);

  void dump(const Lnast_nid &root) const;
  void dump() const {
    dump(Lnast_nid::root());
  }

  template <typename S, typename... Args>
  static void info(const S &format, Args &&...args) {
    auto txt = fmt::format(format, args...);
    fmt::print("info:{}\n", txt);
  }

  class error : public std::runtime_error {
  public:
    template <typename S, typename... Args>
    error(const S &format, Args &&...args) : std::runtime_error(fmt::format(format, args...)) {
      fmt::print("error:lnast {}\n", what());
      throw std::runtime_error(std::string(what()));
    };
  };

};

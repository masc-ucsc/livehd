//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <stack>

#include "elab_scanner.hpp"
#include "lhtree.hpp"
#include "lnast_ntype.hpp"

using Lnast_nid                     = lh::Tree_index;
using Phi_rtable                    = absl::flat_hash_map<std::string, Lnast_nid>;  // rtable = resolve_table
using Cnt_rtable                    = absl::flat_hash_map<std::string, int16_t>;
using Selc_lrhs_table               = absl::flat_hash_map<Lnast_nid, std::pair<bool, Lnast_nid>>;  // sel -> (lrhs, paired opr node)
using Tuple_var_1st_scope_ssa_table = absl::flat_hash_map<std::string, Lnast_nid>;                 // rtable = resolve_table

// tricky old C macro to avoid redundant code from function overloadings
#define CREATE_LNAST_NODE(type)                                                                                       \
  static Lnast_node create_##type() { return Lnast_node(Lnast_ntype::create_##type(), State_token(0, 0, 0, 0, "")); } \
  static Lnast_node create_##type(std::string_view str) {                                                             \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, 0, 0, 0, str));                                    \
  }                                                                                                                   \
  static Lnast_node create_##type(std::string_view str, uint32_t line_num) {                                          \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, 0, 0, line_num, str));                             \
  }                                                                                                                   \
  static Lnast_node create_##type(std::string_view str, uint32_t line_num, uint64_t pos1, uint64_t pos2) {            \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, pos1, pos2, line_num, str));                       \
  }                                                                                                                   \
  static Lnast_node create_##type(const State_token &new_token) { return Lnast_node(Lnast_ntype::create_##type(), new_token); }

#define CREATE_LNAST_NODE_sv(type)                                                                           \
  static Lnast_node create_##type(std::string_view sview) {                                                  \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, 0, 0, 0, sview));                         \
  }                                                                                                          \
  static Lnast_node create_##type(std::string_view sview, uint32_t line_num) {                               \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, 0, 0, line_num, sview));                  \
  }                                                                                                          \
  static Lnast_node create_##type(std::string_view sview, uint32_t line_num, uint64_t pos1, uint64_t pos2) { \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, pos1, pos2, line_num, sview));            \
  }

struct Lnast_node {
  Lnast_ntype type;
  State_token token;
  int16_t     subs;  // ssa subscript

  Lnast_node() : type(Lnast_ntype::create_invalid()), subs(0) {}
  Lnast_node(Lnast_ntype _type) : type(_type), subs(0) {}
  Lnast_node(Lnast_ntype _type, const State_token &_token) : type(_type), token(_token), subs(0) {}

  Lnast_node(Lnast_ntype _type, const State_token &_token, int16_t _subs) : type(_type), token(_token), subs(_subs) {
    I(!type.is_invalid());
  }

  constexpr bool is_invalid() const { return type.is_invalid(); }
  void           dump() const;

#define LNAST_NODE(NAME, VERBAL) CREATE_LNAST_NODE(NAME)
#include "lnast_nodes.def"

  static Lnast_node create_const(int64_t v) {
    return Lnast_node(Lnast_ntype::create_const(), State_token(0, 0, 0, 0, std::to_string(v)));
  }
};

class Lnast : public lh::tree<Lnast_node> {
private:
  std::string top_module_name;
  std::string source_filename;
  Lnast_nid   undefined_var_nid;
  uint32_t    tmp_var_cnt = 0;

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
  Lnast_nid get_complement_nid(std::string_view brother_name, const Lnast_nid &psts_nid, bool false_path);
  Lnast_nid check_phi_table_parents_chain(std::string_view brother_name, const Lnast_nid &psts_nid);
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
  void        trans_tuple_opr(const Lnast_nid &pats_nid);  // from sel to tuple_add/get
  void        trans_tuple_opr_if_subtree(const Lnast_nid &if_nid);
  void        trans_tuple_opr_handle_a_statement(const Lnast_nid &pats_nid, const Lnast_nid &opr_nid);
  bool        check_tuple_table_parents_chain(const Lnast_nid &psts_nid, std::string_view ref_name);
  void        sel2local_tuple_chain(const Lnast_nid &pats_nid, Lnast_nid &sel_nid);
  void        merge_tconcat_paired_assign(const Lnast_nid &psts_nid, const Lnast_nid &concat_nid);
  void        rename_to_real_tuple_name(const Lnast_nid &psts_nid, const Lnast_nid &tup_nid);
  bool        is_scalar_attribute_related(const Lnast_nid &opr_nid);
  void        selc2attr_set_get(const Lnast_nid &psts_nid, Lnast_nid &opr_nid);
  void        update_tuple_var_table(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid);
  bool        update_tuple_var_1st_scope_ssa_table(const Lnast_nid &psts_nid, const Lnast_nid &target_nid);
  bool        check_tuple_var_1st_scope_ssa_table_parents_chain(const Lnast_nid &psts_nid, std::string_view ref_name,
                                                                const Lnast_nid &src_if_nid);
  void        merge_hierarchical_attr_set(Lnast_nid &opr_nid);
  void        collect_hier_tuple_nids(Lnast_nid &opr_nid, std::stack<Lnast_nid> &stk_tuple_fields);
  std::string create_tmp_var();

  // hierarchical statements node -> symbol table
  absl::flat_hash_map<Lnast_nid, Phi_rtable>      phi_resolve_tables;
  absl::flat_hash_map<Lnast_nid, Cnt_rtable>      ssa_rhs_cnt_tables;
  absl::flat_hash_map<Lnast_nid, Selc_lrhs_table> selc_lrhs_tables;
  absl::flat_hash_map<Lnast_nid, Phi_rtable>      new_added_phi_node_tables;  // for each if-subtree scope
  absl::flat_hash_set<std::string>                tuplized_table;
  absl::flat_hash_map<std::string, Lnast_nid>     candidates_update_phi_resolve_table;
  absl::flat_hash_map<std::string, int16_t>       global_ssa_lhs_cnt_table;

  // for chaining parent tuple-chain and local tuple chain, only record the first tuple variable appeared in each local scope
  absl::flat_hash_map<Lnast_nid, Phi_rtable> tuple_var_1st_scope_ssa_tables;

  absl::flat_hash_set<std::string> collected_hier_tuple_reg_name;

  // populated during LG->LN pass, maps name -> bitwidth
  absl::flat_hash_map<std::string, uint32_t> from_lgraph_bw_table;

  uint32_t tup_internal_cnt = 0;

public:
  static constexpr char version[] = "0.1.0";

  explicit Lnast() : top_module_name("noname"), source_filename("") {}
  ~Lnast();
  explicit Lnast(std::string_view _module_name) : top_module_name(_module_name), source_filename("") {}
  explicit Lnast(std::string_view _module_name, std::string_view _file_name)
      : top_module_name(_module_name), source_filename(_file_name) {}

  void ssa_trans() { do_ssa_trans(lh::Tree_index::root()); };

  std::string_view get_top_module_name() const { return top_module_name; }
  std::string_view get_source() const { return source_filename; }

  void set_top_module_name(std::string_view name) { top_module_name = name; }

  bool             is_lhs(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid) const;
  static bool      is_register(std::string_view name) { return name.front() == '#'; }
  static bool      is_output(std::string_view name) { return name.front() == '%'; }
  static bool      is_input(std::string_view name) { return name.front() == '$'; }
  std::string_view get_name(const Lnast_nid &nid) const { return get_data(nid).token.get_text(); }
  std::string_view get_vname(const Lnast_nid &nid) const { return get_data(nid).token.get_text(); }

  Lnast_ntype        get_type(const Lnast_nid &nid) const { return get_data(nid).type; }
  int16_t            get_subs(const Lnast_nid &nid) const { return get_data(nid).subs; }
  const State_token &get_token(const Lnast_nid &nid) const { return get_data(nid).token; }
  std::string        get_sname(const Lnast_nid &nid) const {  // sname = ssa name
    if (get_type(nid).is_const())
      return std::string(get_name(nid));
    return absl::StrCat(get_name(nid), "_", get_subs(nid));
  }

  // bitwidth table functions
  bool     is_in_bw_table(std::string_view name) const;
  uint32_t get_bitwidth(std::string_view name) const;
  void     set_bitwidth(std::string_view name, const uint32_t bitwidth);

  void dump(const Lnast_nid &root) const;
  void dump() const { dump(Lnast_nid::root()); }

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

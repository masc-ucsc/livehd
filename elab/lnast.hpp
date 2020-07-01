//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "mmap_tree.hpp"
#include "lnast_ntype.hpp"

// FIXME->sh: need ordered map to guarantee phi-node generation order to be able
// to test LNAST-SSA, better to use absl::btree_map

/* using Phi_rtable = absl::flat_hash_map<std::string_view, Lnast_nid>; // rtable = resolve_table */
using Lnast_nid       = mmap_lib::Tree_index;
using Phi_rtable      = std::map<std::string_view, Lnast_nid>; // rtable = resolve_table
using Cnt_rtable      = absl::flat_hash_map<std::string_view, int8_t>;
using Dot_lrhs_table  = absl::flat_hash_map<Lnast_nid, std::pair<bool, Lnast_nid>>;  // for both dot and selection, dot -> (lrhs, related assign node)
using Tuple_var_table = absl::flat_hash_set<std::string_view>;

//tricky old C macro to avoid redundant code from function overloadings
#define CREATE_LNAST_NODE(type) \
        static Lnast_node create##type(std::string_view sview){return Lnast_node(Lnast_ntype::create##type(), Token(0, 0, 0, 0, sview));}\
        static Lnast_node create##type(std::string_view sview, uint32_t line_num){return Lnast_node(Lnast_ntype::create##type(), Token(0, 0, 0, line_num, sview));}\
        static Lnast_node create##type(std::string_view sview, uint32_t line_num, uint64_t pos1, uint64_t pos2){return Lnast_node(Lnast_ntype::create##type(), Token(0, pos1, pos2, line_num, sview));}\
        static Lnast_node create##type(const Token &new_token){return Lnast_node(Lnast_ntype::create##type(), new_token);}

struct Lnast_node {
  Lnast_ntype type;
  Token       token;
  uint16_t    subs; //ssa subscript

  Lnast_node(): subs(0) { }

  Lnast_node(Lnast_ntype _type)
    :type(_type), subs(0) { I(!type.is_invalid());}

  Lnast_node(Lnast_ntype _type, const Token &_token)
    :type(_type), token(_token), subs(0) { I(!type.is_invalid());}

  Lnast_node(Lnast_ntype _type, const Token &_token, uint16_t _subs)
    :type(_type), token(_token), subs(_subs) { I(!type.is_invalid());}

  void dump() const;

  CREATE_LNAST_NODE(_invalid)
  CREATE_LNAST_NODE(_top)
  CREATE_LNAST_NODE(_stmts)
  CREATE_LNAST_NODE(_cstmts)
  CREATE_LNAST_NODE(_if)
  CREATE_LNAST_NODE(_cond)
  CREATE_LNAST_NODE(_uif)
  CREATE_LNAST_NODE(_elif)
  CREATE_LNAST_NODE(_for)
  CREATE_LNAST_NODE(_while)
  CREATE_LNAST_NODE(_phi)
  CREATE_LNAST_NODE(_func_call)
  CREATE_LNAST_NODE(_func_def)
  CREATE_LNAST_NODE(_assign)
  CREATE_LNAST_NODE(_dp_assign)
  CREATE_LNAST_NODE(_as)
  CREATE_LNAST_NODE(_label)
  CREATE_LNAST_NODE(_dot)
  CREATE_LNAST_NODE(_logical_and)
  CREATE_LNAST_NODE(_logical_or)
  CREATE_LNAST_NODE(_logical_not)
  CREATE_LNAST_NODE(_and)
  CREATE_LNAST_NODE(_or)
  CREATE_LNAST_NODE(_not)
  CREATE_LNAST_NODE(_xor)
  CREATE_LNAST_NODE(_plus)
  CREATE_LNAST_NODE(_minus)
  CREATE_LNAST_NODE(_mult)
  CREATE_LNAST_NODE(_div)
  CREATE_LNAST_NODE(_eq)
  CREATE_LNAST_NODE(_same)
  CREATE_LNAST_NODE(_lt)
  CREATE_LNAST_NODE(_le)
  CREATE_LNAST_NODE(_gt)
  CREATE_LNAST_NODE(_ge)
  CREATE_LNAST_NODE(_tuple)
  CREATE_LNAST_NODE(_tuple_concat)
  CREATE_LNAST_NODE(_tuple_delete)
  CREATE_LNAST_NODE(_select)
  CREATE_LNAST_NODE(_bit_select)
  CREATE_LNAST_NODE(_range)
  CREATE_LNAST_NODE(_shift_right)
  CREATE_LNAST_NODE(_shift_left)
  CREATE_LNAST_NODE(_logic_shift_right)
  CREATE_LNAST_NODE(_arith_shift_right)
  CREATE_LNAST_NODE(_arith_shift_left)
  CREATE_LNAST_NODE(_rotate_shift_right)
  CREATE_LNAST_NODE(_rotate_shift_left)
  CREATE_LNAST_NODE(_dynamic_shift_right)
  CREATE_LNAST_NODE(_dynamic_shift_left)
  CREATE_LNAST_NODE(_ref)
  CREATE_LNAST_NODE(_const)
  CREATE_LNAST_NODE(_assert)
};


class Lnast : public mmap_lib::tree<Lnast_node> {
private:
  std::string      top_module_name;
  std::string_view memblock;
  int              memblock_fd;

  void      do_ssa_trans               (const Lnast_nid  &top_nid);
  void      ssa_lhs_handle_a_statement (const Lnast_nid  &psts_nid, const Lnast_nid &opr_nid);
  void      ssa_rhs_handle_a_statement (const Lnast_nid  &psts_nid, const Lnast_nid &opr_nid);
  void      ssa_lhs_if_subtree             (const Lnast_nid  &if_nid);
  void      ssa_rhs_if_subtree         (const Lnast_nid  &if_nid);
  void      ssa_handle_phi_nodes       (const Lnast_nid  &if_nid);
  void      resolve_phi_nodes          (const Lnast_nid  &cond_nid, Phi_rtable &true_table, Phi_rtable &false_table);
  void      update_phi_resolve_table   (const Lnast_nid  &psts_nid, const Lnast_nid &target_nid);
  bool      has_else_stmts             (const Lnast_nid  &if_nid);
  Lnast_nid add_phi_node               (const Lnast_nid  &cond_nid, const Lnast_nid &t_nid, const Lnast_nid &f_nid);
  Lnast_nid get_complement_nid            (std::string_view brother_name, const Lnast_nid &psts_nid, bool false_path);
  Lnast_nid check_phi_table_parents_chain (std::string_view brother_name, const Lnast_nid &psts_nid, bool originate_from_csts);
  void      resolve_ssa_lhs_subs                (const Lnast_nid &psts_nid);
  void      resolve_ssa_rhs_subs                (const Lnast_nid &psts_nid);
  void      update_global_lhs_ssa_cnt_table     (const Lnast_nid &target_nid);
  void      respect_latest_global_lhs_ssa       (const Lnast_nid &target_nid);
  void      reg_ini_global_lhs_ssa_cnt_table    (const Lnast_nid &target_nid); //just initialize global reg when appeared in rhs
  int8_t    check_rhs_cnt_table_parents_chain   (const Lnast_nid &psts_nid, const Lnast_nid &target_key);
  void      update_rhs_ssa_cnt_table            (const Lnast_nid &psts_nid, const Lnast_nid &target_key);
  void      analyze_dot_lrhs                    (const Lnast_nid &psts_nid);
  void      analyze_dot_lrhs_if_subtree         (const Lnast_nid &if_nid);
  void      analyze_dot_lrhs_handle_a_statement (const Lnast_nid &psts_nid, const Lnast_nid &opr_nid);

  bool      is_special_case_of_dot_rhs       (const Lnast_nid &psts_nid,  const Lnast_nid &opr_nid);
  void      ssa_rhs_handle_a_operand         (const Lnast_nid &gpsts_nid, const Lnast_nid &opd_nid); //gpsts = grand parent
  void      ssa_rhs_handle_a_operand_special (const Lnast_nid &gpsts_nid, const Lnast_nid &opd_nid);

  // tuple operator process
  void      trans_tuple_opr                    (const Lnast_nid &pats_nid); // from dot/sel to tuple_add/get
  void      trans_tuple_opr_if_subtree         (const Lnast_nid &if_nid); 
  void      trans_tuple_opr_handle_a_statement (const Lnast_nid &pats_nid, const Lnast_nid &opr_nid); 
  bool      check_tuple_table_parents_chain    (const Lnast_nid &psts_nid, std::string_view ref_name);
  void      dot2local_tuple_chain              (const Lnast_nid &pats_nid, Lnast_nid &dot_nid);
  void      dot2hier_tuple_chain               (const Lnast_nid &psts_nid, Lnast_nid &dot_nid, const Lnast_nid &cond_nid, bool is_else_sts); 
  void      merge_tconcat_paired_assign        (const Lnast_nid &psts_nid, const Lnast_nid &concat_nid);
  void      rename_to_real_tuple_name          (const Lnast_nid &psts_nid, const Lnast_nid &tup_nid);
  void      find_cond_nid                      (const Lnast_nid &psts_nid, Lnast_nid &cond_nid, bool &is_else_sts); 
  bool      is_attribute_related               (const Lnast_nid &opr_nid);
  void      dot2attr_set_get                   (const Lnast_nid &psts_nid, Lnast_nid &opr_nid);


  // hierarchical statements node -> symbol table
  absl::flat_hash_map<Lnast_nid, Phi_rtable>      phi_resolve_tables;
  absl::flat_hash_map<Lnast_nid, Cnt_rtable>      ssa_rhs_cnt_tables;
  absl::flat_hash_map<Lnast_nid, Dot_lrhs_table>  dot_lrhs_tables;
  absl::flat_hash_map<Lnast_nid, Tuple_var_table> tuple_var_tables;   
  absl::flat_hash_map<Lnast_nid, Phi_rtable>      new_added_phi_node_tables; // for each if-subtree scope
  absl::flat_hash_set<std::string_view>           tuplized_table;
  

  absl::flat_hash_map<std::string_view, uint8_t>  global_ssa_lhs_cnt_table;

  Lnast_nid  default_const_nid;
  Lnast_nid  err_var_undefined_nid;   
  Lnast_nid  register_fwd_nid;
  uint32_t   tup_internal_cnt = 0;

  std::vector<std::string *> string_pool;

public:
  Lnast() = default;
  ~Lnast();
  explicit Lnast(std::string_view _module_name): top_module_name(_module_name), memblock_fd(-1) { }
  explicit Lnast(std::string_view _module_name, std::pair<std::string_view, int> o): top_module_name(_module_name), memblock(o.first), memblock_fd(o.second) { }

  void ssa_trans() {
    do_ssa_trans(get_root());
  };

  std::string_view add_string(std::string_view str);
  std::string_view add_string(const std::string &str);

  std::string_view get_top_module_name() const { return top_module_name; }

  bool             is_lhs    (const Lnast_nid &psts_nid, const Lnast_nid &opr_nid);
  bool             is_reg    (std::string_view name) { return name.substr(0,1) == "#"; }
  std::string_view get_name  (const Lnast_nid &nid)  { return get_data(nid).token.get_text(); }
  Lnast_ntype      get_type  (const Lnast_nid &nid)  { return get_data(nid).type; }
  uint8_t          get_subs  (const Lnast_nid &nid)  { return get_data(nid).subs; }
  Token            get_token (const Lnast_nid &nid)  { return get_data(nid).token; }
  std::string      get_sname (const Lnast_nid &nid)  { //sname = ssa name
    if(get_type(nid).is_const())
      return std::string(get_name(nid));
    // FIXME: sh: any better way to concate a string_view??
    return absl::StrCat(std::string(get_name(nid)), "_", get_subs(nid));
  }

  void dump(const Lnast_nid &it) const;
  void dump() const { dump(get_root()); }

  std::string lnast_type_to_string(Lnast_ntype type) const;
};


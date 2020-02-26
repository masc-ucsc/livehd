//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "mmap_tree.hpp"
#include "lnast_ntype.hpp"

//FIXME: need ordered map to guarantee phi-node generation order to be able to test LNAST-SSA, better to use absl::btree_map
//using Phi_rtable = absl::flat_hash_map<std::string_view, Lnast_nid>; //rtable = resolve_table
using Lnast_nid = mmap_lib::Tree_index;
using Phi_rtable = std::map<std::string_view, Lnast_nid>; //rtable = resolve_table
using Cnt_rtable = absl::flat_hash_map<std::string_view, int8_t>;

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
  CREATE_LNAST_NODE(_and)
  CREATE_LNAST_NODE(_or)
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
  CREATE_LNAST_NODE(_ref)
  CREATE_LNAST_NODE(_const)
  CREATE_LNAST_NODE(_attr)
  CREATE_LNAST_NODE(_assert)

};


class Lnast : public mmap_lib::tree<Lnast_node> {
private:
  std::string top_module_name;
  void      do_ssa_trans               (const Lnast_nid  &top_nid);
  void      ssa_handle_a_statement     (const Lnast_nid  &psts_nid, const Lnast_nid &opr_nid);
  void      ssa_rhs_handle_a_statement (const Lnast_nid  &psts_nid, const Lnast_nid &opr_nid);
  void      ssa_if_subtree             (const Lnast_nid  &if_nid);
  void      ssa_rhs_if_subtree         (const Lnast_nid  &if_nid);
  void      ssa_handle_phi_nodes       (const Lnast_nid  &if_nid);
  void      resolve_phi_nodes          (const Lnast_nid  &cond_nid, Phi_rtable &true_table, Phi_rtable &false_table);
  void      update_phi_resolve_table   (const Lnast_nid  &psts_nid, const Lnast_nid &target_nid);
  bool      has_else_stmts             (const Lnast_nid  &if_nid);
  Lnast_nid add_phi_node               (const Lnast_nid  &cond_nid, const Lnast_nid &t_nid, const Lnast_nid &f_nid);
  Lnast_nid get_complement_nid            (std::string_view brother_name, const Lnast_nid &psts_nid, bool false_path);
  Lnast_nid check_phi_table_parents_chain (std::string_view brother_name, const Lnast_nid &psts_nid, bool originate_from_csts);
  void      resolve_ssa_rhs_subs                 (const Lnast_nid &psts_nid);
  void      update_global_lhs_ssa_cnt_table      (const Lnast_nid &target_nid);
  int8_t    check_rhs_cnt_table_parents_chain    (const Lnast_nid &psts_nid, const Lnast_nid &target_key);
  void      update_rhs_ssa_cnt_table             (const Lnast_nid &psts_nid, const Lnast_nid &target_key);

  std::string_view get_name  (const Lnast_nid &nid) { return get_data(nid).token.get_text(); }
  Lnast_ntype      get_type  (const Lnast_nid &nid) { return get_data(nid).type; }
  uint8_t          get_subs  (const Lnast_nid &nid) { return get_data(nid).subs; }
  Token            get_token (const Lnast_nid &nid) { return get_data(nid).token; }


  absl::flat_hash_map<std::string_view, Phi_rtable> phi_resolve_tables;
  absl::flat_hash_map<std::string_view, Cnt_rtable> ssa_rhs_cnt_tables;
  absl::flat_hash_map<std::string_view, uint8_t>    global_ssa_lhs_cnt_table;

  Phi_rtable new_added_phi_node_table;
  Lnast_nid  default_const_nid;

public:
  Lnast() = default;
  explicit Lnast(std::string_view _module_name): top_module_name(_module_name) { }
  void ssa_trans(){
    do_ssa_trans(get_root());
  };

  std::string_view get_top_module_name() const { return top_module_name; }
};





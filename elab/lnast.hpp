//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "mmap_tree.hpp"
#include "lnast_ntype.hpp"

using Lnast_nid = mmap_lib::Tree_index;
using Phi_rtable = absl::flat_hash_map<std::string_view, Lnast_nid>; //rtable = resolve_table


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

  static Lnast_node create_ref(std::string_view var) {
    return Lnast_node(Lnast_ntype::create_ref(), Token(0, 0, 0, 0, var));
  }

  static Lnast_node create_ref(std::string_view var, uint32_t line_num){
    return Lnast_node(Lnast_ntype::create_ref(), Token(0, 0, 0, line_num, var));
  }

  static Lnast_node create_ref(std::string_view var, uint32_t line_num, uint64_t pos1, uint64_t pos2){
    return Lnast_node(Lnast_ntype::create_ref(), Token(0, pos1, pos2, line_num, var));
  } //FIXME: SH: is Token_id a must be in creating a lnast node for HDLs?

  static Lnast_node create_ref(const Token &new_token){
    return Lnast_node(Lnast_ntype::create_ref(), new_token);
  }

  static Lnast_node create_const(std::string_view constant_sview) {
    return Lnast_node(Lnast_ntype::create_const(), Token(0, 0, 0, 0, constant_sview));
  }

  static Lnast_node create_const(std::string_view constant_sview, uint32_t line_num) {
    return Lnast_node(Lnast_ntype::create_const(), Token(0, 0, 0, line_num, constant_sview));
  }

  static Lnast_node create_const(std::string_view constant_sview, uint32_t line_num, uint64_t pos1, uint64_t pos2) {
    return Lnast_node(Lnast_ntype::create_const(), Token(0, pos1, pos2, line_num, constant_sview));
  }

  static Lnast_node create_const(const Token &new_token){
    return Lnast_node(Lnast_ntype::create_const(), new_token);
  }

  static Lnast_node create_pure_assign(uint32_t line_num) {
    return Lnast_node(Lnast_ntype::create_pure_assign(), Token(0, 0, 0, line_num, ""));
  }

  static Lnast_node create_pure_assign(uint32_t line_num, uint64_t pos1, uint64_t pos2) {
    return Lnast_node(Lnast_ntype::create_pure_assign(), Token(0, pos1, pos2, line_num, ""));
  }

  static Lnast_node create_statements(std::string_view sts = "") {
    return Lnast_node(Lnast_ntype::create_statements(), Token(0, 0, 0, 0, sts));
  }

  static Lnast_node create_statements(std::string_view sts = "", uint32_t line_num = 0) {
    return Lnast_node(Lnast_ntype::create_statements(), Token(0, 0, 0, line_num, sts));
  }

  static Lnast_node create_statements(std::string_view sts = "", uint32_t line_num = 0, uint64_t pos1 = 0, uint64_t pos2 = 0) {
    return Lnast_node(Lnast_ntype::create_statements(), Token(0, pos1, pos2, line_num, sts));
  }

  static Lnast_node create_statements(const Token &new_token) {
    return Lnast_node(Lnast_ntype::create_statements(), new_token);
  }
};


class Lnast : public mmap_lib::tree<Lnast_node> {
private:
  std::string top_module_name;
  void      do_ssa_trans              (const Lnast_nid  &top_nid);
  void      ssa_handle_a_statement    (const Lnast_nid  &psts_nid, const Lnast_nid &opr_nid);
  void      ssa_handle_a_cstatement   (const Lnast_nid  &psts_nid, const Lnast_nid &opr_nid);
  void      ssa_if_subtree            (const Lnast_nid  &if_nid);
  void      ssa_handle_phi_nodes      (const Lnast_nid  &if_nid);
  void      resolve_phi_nodes         (const Lnast_nid  &cond_nid, Phi_rtable &true_table, Phi_rtable &false_table);
  bool      elder_sibling_is_label    (const Lnast_nid  &self_nid);
  void      update_ssa_cnt_table      (const Lnast_nid  &target_nid);
  void      update_phi_resolve_table  (const Lnast_nid  &psts_nid, const Lnast_nid &target_nid);
  bool      has_else_statements       (const Lnast_nid  &if_nid);
  Lnast_nid add_phi_node              (const Lnast_nid  &cond_nid, const Lnast_nid &t_nid, const Lnast_nid &f_nid);
  Lnast_nid get_complement_nid             (std::string_view brother_name, const Lnast_nid &psts_nid, bool false_path);
  Lnast_nid check_phi_table_parents_chain  (std::string_view brother_name, const Lnast_nid &psts_nid, bool originate_from_csts);

  absl::flat_hash_map<std::string_view, Phi_rtable> phi_resolve_tables;
  absl::flat_hash_map<std::string_view, uint8_t >   ssa_cnt_table;
  Phi_rtable new_added_phi_node_table;

  Lnast_nid default_const_nid;
public:
  Lnast() = default;
  explicit Lnast(std::string_view _module_name): top_module_name(_module_name) { }
  void ssa_trans(){
    do_ssa_trans(get_root());
  };

  std::string_view get_top_module_name() const { return top_module_name; }
};





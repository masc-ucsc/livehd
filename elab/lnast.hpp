//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "mmap_tree.hpp"
#include "lnast_ntype.hpp"

using Lnast_nid    = mmap_lib::Tree_index;
using Rename_table = absl::flat_hash_map<std::string_view, uint8_t>;
//using Rename_table = absl::flat_hash_map<Token, uint8_t>;


struct Lnast_node {
  Lnast_ntype type;
  Token       token;
  uint32_t    loc;  //sh:fixme: wait for Akash
  uint16_t    subs; //ssa subscript

  Lnast_node()
    :loc(0), subs(0) { }

  Lnast_node(Lnast_ntype _type, Token _token)
    :type(_type), token(_token), loc(0), subs(0) { I(!type.is_invalid());}

  Lnast_node(Lnast_ntype _type, Token _token, uint16_t _subs)
    :type(_type), token(_token), loc(0), subs(_subs) { I(!type.is_invalid());}

  void dump() const;
};


class Lnast : public mmap_lib::tree<Lnast_node> {
public:
  Lnast() = default;
  explicit Lnast(std::string_view _buffer): buffer(_buffer) { I(!buffer.empty());}
  void ssa_trans(){
    do_ssa_trans(get_root());
  };

private:
  const std::string_view buffer;  // const because it can not change at runtime
  void      do_ssa_trans                  (const Lnast_nid &top_nid);
  void      ssa_handle_a_statement        (const Lnast_nid &psts_nid, const Lnast_nid &opr_nid);
  void      ssa_if_subtree                (const Lnast_nid &if_nid);
  void      ssa_handle_phi_nodes          (const Lnast_nid &if_nid);
  void      resolve_phi_nodes             (const Lnast_nid &cond_nid, Rename_table &true_table, Rename_table &false_table);
  bool      elder_sibling_is_label        (const Lnast_nid &self_nid);
  void      update_ssa_cnt_table          (Lnast_node &target_data);
  void      update_phi_resolve_table      (const Lnast_nid &psts_nid, Lnast_node &target_data);
  bool      has_else_statements           (const Lnast_nid &if_nid);
  Lnast_nid add_phi_node                (const Lnast_nid &cond_nid, const std::string_view var, const uint8_t tcnt, const uint8_t fcnt);
  //Lnast_nid add_phi_node                  (const Lnast_nid &cond_nid,  const Token var, const uint8_t tcnt, const uint8_t fcnt);

  //absl::flat_hash_map<Token, Rename_table > phi_resolve_tables;
  absl::flat_hash_map<std::string_view, Rename_table > phi_resolve_tables;
  absl::flat_hash_map<std::string_view, Token> sview2token;
  Rename_table ssa_cnt_table;
  Rename_table new_added_phi_node_table;
  Lnast_nid    last_sibling_nid;
protected:
};





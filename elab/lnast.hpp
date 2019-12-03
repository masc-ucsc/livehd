//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "mmap_tree.hpp"
#include "lnast_ntype.hpp"

using Lnast_nid    = mmap_lib::Tree_index;
using Rename_table = absl::flat_hash_map<std::string, absl::flat_hash_map<std::string_view, uint8_t >>;


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
  void do_ssa_trans                 (const Lnast_nid &top);
  void ssa_normal_subtree           (const Lnast_nid &psts_node, const Lnast_nid &opr_node);
  void ssa_if_subtree               (const Lnast_nid &if_node);
  void phi_node_insertion           (const Lnast_nid &if_node);
  bool elder_sibling_is_label       (const Lnast_nid &self);
  void update_or_insert_rename_table(const Lnast_nid &psts_node, Lnast_node& target_data);
  void update_rename_table          (const Lnast_nid &psts_node, Lnast_node& target_data);

  absl::flat_hash_map<std::string_view, absl::flat_hash_map<std::string_view, uint8_t >> rename_tables;
protected:
};





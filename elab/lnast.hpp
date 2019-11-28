//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "mmap_tree.hpp"
#include "lnast_ntype.hpp"

using Rename_table        = absl::flat_hash_map<std::string_view, u_int8_t >;
using Lnast_index         = mmap_lib::Tree_index;
using Phi_sts_table       = absl::flat_hash_map<std::string_view, Lnast_index>;
using Phi_sts_tables      = absl::flat_hash_map<Lnast_index, Phi_sts_table>;


struct Lnast_node {
  Lnast_ntype type;
  Token       token;
  uint32_t    loc;  //SH:FIXME: wait for Akash
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
  void ssa_trans();

private:
  const std::string_view buffer;  // const because it can not change at runtime
  void do_ssa_trans                 (const Lnast_index &top);
  void ssa_normal_subtree           (const Lnast_index &opr_node, Rename_table &rename_table);
  void ssa_if_subtree               (const Lnast_index &if_node,  Rename_table &rename_table);
  void phi_node_insertion           (const Lnast_index &if_node,  Rename_table &rename_table);
  bool check_else_block_existence   (const Lnast_index &if_node);
  bool elder_sibling_is_label       (const Lnast_index &self);
  bool elder_sibling_is_cond        (const Lnast_index &self);
  Lnast_index get_elder_sibling     (const Lnast_index &self);
  void update_or_insert_rename_table(std::string_view target_name, Lnast_node &target_data, Rename_table &rename_table);
  void update_rename_table          (std::string_view target_name, Rename_table &rename_table);
protected:
};





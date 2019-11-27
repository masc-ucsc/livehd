//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "mmap_tree.hpp"

using Rename_table        = absl::flat_hash_map<std::string_view, u_int8_t >;
using Lnast_ntype         = uint8_t;
using Lnast_index         = mmap_lib::Tree_index;
using Phi_sts_table       = absl::flat_hash_map<std::string_view, Lnast_index>;
using Phi_sts_tables      = absl::flat_hash_map<Lnast_index, Phi_sts_table>;

struct Lnast_node {
  Lnast_ntype type; //not const as possible fake function call ...
  Token       token;
  uint32_t    loc;  //SH:FIXME: wait for Akash
  uint16_t    subs; //ssa subscript

  Lnast_node()
    :type(0), loc(0), subs(0) { }

  Lnast_node(Lnast_ntype _type, Token _token)
    :type(_type), token(_token), loc(0), subs(0) { I(type);}

  Lnast_node(Lnast_ntype _type, Token _token, uint16_t _subs)
    :type(_type), token(_token), loc(0), subs(_subs) { I(type);}

  //Lnast_node(Lnast_ntype type, Token token, uint32_t)
  //  :type(type), token(token), loc(0), subs(0) { I(type);}

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


enum Lnast_node_type : Lnast_ntype {
  Lnast_ntype_invalid = 0,  // zero is not a valid Lnast_ntype
  //group: tree structure
  Lnast_ntype_top,
  Lnast_ntype_statements,
  Lnast_ntype_cstatements,  // statement for condition determination, ex: if ((foo+1) > 3) { ... }
  Lnast_ntype_if,
  Lnast_ntype_cond,
  Lnast_ntype_uif,
  Lnast_ntype_elif,
  Lnast_ntype_for,
  Lnast_ntype_while,
  Lnast_ntype_phi,
  Lnast_ntype_func_call,    // .()
  Lnast_ntype_func_def,     // ::{   func_def = sub-graph in lgraph

  //group: primitive operator
  Lnast_ntype_pure_assign,  // =
  Lnast_ntype_dp_assign,    // :=, dp = deprecate
  Lnast_ntype_as,           // as
  Lnast_ntype_label,        // :
  Lnast_ntype_dot,          // .
  Lnast_ntype_logical_and,  // and
  Lnast_ntype_logical_or,   // or
  Lnast_ntype_and,          // &
  Lnast_ntype_or,           // |
  Lnast_ntype_xor,          // ^
  Lnast_ntype_plus,
  Lnast_ntype_minus,
  Lnast_ntype_mult,
  Lnast_ntype_div,
  Lnast_ntype_eq,
  Lnast_ntype_same,
  Lnast_ntype_lt,
  Lnast_ntype_le,
  Lnast_ntype_gt,
  Lnast_ntype_ge,
  Lnast_ntype_tuple,        // ()

  //group: language variable
  Lnast_ntype_ref,
  Lnast_ntype_const,

  //group: attribute
  Lnast_ntype_attr_bits,    // __bits

  //group: others
  Lnast_ntype_assert,       // I
};

static inline std::string_view Lnast_type_name[] = {
  "invalid",
  //group: tree structure
  "top",
  "statements",
  "cstatements",
  "if",
  "cond",
  "uif",
  "elif",
  "for",
  "while",
  "phi",
  "func_call",
  "func_def",

  //group: primitive operator
  "pure_assign",
  "dp_assign",
  "as",
  "label",
  "dot",
  "logical_and",
  "logical_or",
  "and",
  "or",
  "xor",
  "plus",
  "minus",
  "mult",
  "div",
  "eq",
  "same",
  "lt",
  "le",
  "gt",
  "ge",
  "tuple",

  //group: language variable
  "ref",
  "const",

  //group: attribute
  "attr_bits",

  //group: others
  "assert",
};



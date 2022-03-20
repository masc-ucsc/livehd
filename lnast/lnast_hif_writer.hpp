//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnast.hpp"
#include "hif/hif_write.hpp"

class Lnast_hif_writer {
public:
  explicit Lnast_hif_writer(std::string_view _filename, std::shared_ptr<Lnast> _lnast)
    : filename(_filename), lnast(_lnast) {}

  void write_all() {
    wr = Hif_write::create(filename, "lnast", Lnast::version);
    current_nid = Lnast_nid::root();
    write_hif_stmt();
    wr = nullptr;
  }

protected:
  std::string filename;

  std::shared_ptr<Lnast> lnast;
  std::shared_ptr<Hif_write> wr;

  std::stack<Lnast_nid> nid_stack;
  Lnast_nid current_nid;

  auto current_text() {
    return lnast->get_data(current_nid).token.get_text();
  }

  bool move_to_child()   {
    nid_stack.push(current_nid);
    current_nid = lnast->get_child(current_nid);
    return !current_nid.is_invalid();
  }

  bool move_to_sibling() {
    current_nid = lnast->get_sibling_next(current_nid);
    return !current_nid.is_invalid();
  }
  
  void move_to_parent() {
    I(nid_stack.size() >= 1);
    current_nid = nid_stack.top();
    nid_stack.pop();
  }
  
  auto get_raw_ntype() {
    return lnast->get_type(current_nid).get_raw_ntype();
  }

  bool is_invalid() {
    return current_nid.is_invalid();
  }

  #define CASE_LNAST_NTYPE(lnast_type, node_type) \
  case Lnast_ntype::Lnast_ntype_##lnast_type: {   \
    write_##node_type();                          \
    break;                                        \
  }

  void write_hif_stmt() {
    switch (get_raw_ntype()) {
      CASE_LNAST_NTYPE( invalid          , leaf       )
      CASE_LNAST_NTYPE( top              , tree       )
      CASE_LNAST_NTYPE( stmts            , tree       )
      CASE_LNAST_NTYPE( if               , tree       )
      CASE_LNAST_NTYPE( uif              , tree       )
      CASE_LNAST_NTYPE( for              , tree       )
      CASE_LNAST_NTYPE( while            , tree       )
      CASE_LNAST_NTYPE( func_call        , tree       )
      CASE_LNAST_NTYPE( func_def         , tree       )
      CASE_LNAST_NTYPE( assign           , tree       )
      CASE_LNAST_NTYPE( dp_assign        , tree       )
      CASE_LNAST_NTYPE( mut              , tree       )
      CASE_LNAST_NTYPE( bit_and          , tree       )
      CASE_LNAST_NTYPE( bit_or           , tree       )
      CASE_LNAST_NTYPE( bit_not          , tree       )
      CASE_LNAST_NTYPE( bit_xor          , tree       )
      CASE_LNAST_NTYPE( reduce_or        , tree       )
      CASE_LNAST_NTYPE( logical_and      , tree       )
      CASE_LNAST_NTYPE( logical_or       , tree       )
      CASE_LNAST_NTYPE( logical_not      , tree       )
      CASE_LNAST_NTYPE( plus             , tree       )
      CASE_LNAST_NTYPE( minus            , tree       )
      CASE_LNAST_NTYPE( mult             , tree       )
      CASE_LNAST_NTYPE( div              , tree       )
      CASE_LNAST_NTYPE( mod              , tree       )
      CASE_LNAST_NTYPE( shl              , tree       )
      CASE_LNAST_NTYPE( sra              , tree       )
      CASE_LNAST_NTYPE( sext             , tree       )
      CASE_LNAST_NTYPE( set_mask         , tree       )
      CASE_LNAST_NTYPE( get_mask         , tree       )
      CASE_LNAST_NTYPE( mask_and         , tree       )
      CASE_LNAST_NTYPE( mask_popcount    , tree       )
      CASE_LNAST_NTYPE( mask_xor         , tree       )
      CASE_LNAST_NTYPE( is               , tree       )
      CASE_LNAST_NTYPE( ne               , tree       )
      CASE_LNAST_NTYPE( eq               , tree       )
      CASE_LNAST_NTYPE( lt               , tree       )
      CASE_LNAST_NTYPE( le               , tree       )
      CASE_LNAST_NTYPE( gt               , tree       )
      CASE_LNAST_NTYPE( ge               , tree       )
      CASE_LNAST_NTYPE( range            , tree       )
      CASE_LNAST_NTYPE( type_spec        , tree       )
      CASE_LNAST_NTYPE( none_type        , leaf       )
      CASE_LNAST_NTYPE( prim_type_uint   , tree       )
      CASE_LNAST_NTYPE( prim_type_sint   , tree       )
      CASE_LNAST_NTYPE( prim_type_range  , tree       )
      CASE_LNAST_NTYPE( prim_type_string , leaf       )
      CASE_LNAST_NTYPE( prim_type_boolean, leaf       )
      CASE_LNAST_NTYPE( prim_type_type   , leaf       )
      CASE_LNAST_NTYPE( prim_type_ref    , leaf       )
      CASE_LNAST_NTYPE( comp_type_tuple  , tree       )
      CASE_LNAST_NTYPE( comp_type_array  , tree       )
      CASE_LNAST_NTYPE( comp_type_mixin  , tree       )
      CASE_LNAST_NTYPE( comp_type_lambda , tree       )
      CASE_LNAST_NTYPE( expr_type        , leaf       )
      CASE_LNAST_NTYPE( unknown_type     , leaf       )
      CASE_LNAST_NTYPE( tuple_concat     , tree       )
      CASE_LNAST_NTYPE( tuple_add        , tree       )
      CASE_LNAST_NTYPE( tuple_get        , tree       )
      CASE_LNAST_NTYPE( tuple_set        , tree       )
      CASE_LNAST_NTYPE( attr_set         , tree       )
      CASE_LNAST_NTYPE( attr_get         , tree       )
      CASE_LNAST_NTYPE( err_flag         , leaf       )
      CASE_LNAST_NTYPE( phi              , leaf       )
      CASE_LNAST_NTYPE( hot_phi          , leaf       )
      CASE_LNAST_NTYPE( ref              , named_leaf )
      CASE_LNAST_NTYPE( const            , named_leaf )
    }
  }

  void write_named_leaf() {
    auto hif_stmt = Hif_write::create_node();
    hif_stmt.type = static_cast<uint16_t>(get_raw_ntype());
    hif_stmt.instance = current_text();
    wr->add(hif_stmt);
  }

  void write_leaf() {
    auto hif_stmt = Hif_write::create_node();
    hif_stmt.type = static_cast<uint16_t>(get_raw_ntype());
    wr->add(hif_stmt);
  }

  void write_tree() {
    auto hif_stmt = Hif_write::create_open_call();
    hif_stmt.type = static_cast<uint16_t>(get_raw_ntype());
    wr->add(hif_stmt);
    move_to_child();
    while (!is_invalid()) {
      write_hif_stmt();
      move_to_sibling();
    }
    move_to_parent();
    wr->add(Hif_write::create_end());
  }

};

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "bm.h"
#include "bmsparsevec.h"

#include "dense.hpp"
#include "char_array.hpp"

#include "lgraphbase.hpp"
#include "node_type_base.hpp"

typedef Char_Array_ID Const_ID;

class LGraph_Node_Type : virtual public LGraph_Base {
protected:
  Char_Array<Const_ID> consts;
  Dense<Node_Type_Op>  node_type_table;
  bm::bvector<>        const_nodes;      // FIXME: migrate to structure in node_intenral (otherwise, big meory as more nodes...
  bm::bvector<>        sub_graph_nodes;  // FIXME: migrate to structure in node_intenral (otherwise, big meory as more nodes...

  void clear();
  void reload();
  void sync();
  void emplace_back();

  void             set_type(Index_ID nid, Node_Type_Op op);
  const Node_Type &get_type(Index_ID nid) const;

  void             set_type_subgraph(Index_ID nid, Lg_type_id subgraphid);
  Lg_type_id       get_type_subgraph(Index_ID nid) const;

  void             set_type_lut(Index_ID nid, Lut_type_id lutid);
  Lut_type_id      get_type_lut(Index_ID nid) const;

  void             set_type_tmap_id(Index_ID nid, uint32_t tmapid);
  uint32_t         get_type_tmap_id(Index_ID nid) const;

  void             set_type_const_value(Index_ID nid, std::string_view value);
  void             set_type_const_sview(Index_ID nid, std::string_view value);
  void             set_type_const_value(Index_ID nid, uint32_t value);

  Index_ID         find_type_const_sview(std::string_view value) const;
  Index_ID         find_type_const_value(uint32_t value) const;

  std::string_view get_type_const_sview(Index_ID nid) const;
  uint32_t         get_type_const_value(Index_ID nid) const;

  std::string_view get_constant(Const_ID const_id) const;

public:
  LGraph_Node_Type() = delete;
  explicit LGraph_Node_Type(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept;
  virtual ~LGraph_Node_Type(){};

  const bm::bvector<> &get_const_node_ids() const { return const_nodes;     };
  const bm::bvector<> &get_sub_graph_ids() const  { return sub_graph_nodes; };
};

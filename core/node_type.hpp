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

public:
  LGraph_Node_Type() = delete;
  explicit LGraph_Node_Type(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept;
  virtual ~LGraph_Node_Type(){};

  std::string_view get_constant(Const_ID const_id) const;

  void clear();
  void reload();
  void sync();
  void emplace_back();

  void node_type_set(Index_ID nid, Node_Type_Op op);

  void     node_u32type_set(Index_ID nid, uint32_t value);
  Index_ID node_u32type_find(uint32_t value) const;
#if 1
  // WARNING: deprecated
  uint32_t node_value_get(Index_ID nid) const;
  const Node_Type &node_type_get(Index_ID nid) const;
#endif

  void     node_subgraph_set(Index_ID nid, Lg_type_id subgraphid);
  Lg_type_id subgraph_id_get(Index_ID nid) const;

  void             node_const_type_set(Index_ID nid, std::string_view value);
  void             node_const_type_set_string(Index_ID nid, std::string_view value);
  Index_ID         node_const_string_find(std::string_view value) const;
  std::string_view node_const_value_get(Index_ID nid) const;

  void     node_tmap_set(Index_ID nid, uint32_t tmapid);
  uint32_t tmap_id_get(Index_ID nid) const;

  const bm::bvector<> &get_const_node_ids() const { return const_nodes; };

  const bm::bvector<> &get_sub_graph_ids() const { return sub_graph_nodes; };
};

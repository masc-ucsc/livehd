//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "dense.hpp"
#include "mmap_map.hpp"

#include "node.hpp"
#include "lgraphbase.hpp"
#include "node_type_base.hpp"
#include "sub_node.hpp"

using Node_sview_map = mmap_map::map<std::string_view, Node::Compact_class>;
using Node_value_map = mmap_map::map<uint32_t, Node::Compact_class>;
using Node_down_map  = mmap_map::map<Node::Compact_class, Lg_type_id>;

class LGraph_Node_Type : virtual public LGraph_Base {
protected:
  Dense<Node_Type_Op>  node_type_table;

  Node_sview_map   const_sview;
  Node_value_map   const_value;
  Node_down_map    down_nodes;

  void clear();
  void reload();
  void sync();
  void emplace_back();

  void             set_type(Index_ID nid, Node_Type_Op op);
  const Node_Type &get_type(Index_ID nid) const;

  bool             is_type_const(Index_ID nid) const;

  void             set_type_sub(Index_ID nid, Lg_type_id subgraphid);
  Lg_type_id       get_type_sub(Index_ID nid) const;
  Sub_node        &get_type_sub_node(Index_ID nid);
  const Sub_node  &get_type_sub_node(Index_ID nid) const;
  Sub_node        &get_type_sub_node(std::string_view sub_name);
  const Sub_node  &get_type_sub_node(std::string_view sub_name) const;

  void             set_type_lut(Index_ID nid, Lut_type_id lutid);
  Lut_type_id      get_type_lut(Index_ID nid) const;

  void             set_type_const_value(Index_ID nid, std::string_view value);
  void             set_type_const_sview(Index_ID nid, std::string_view value);
  void             set_type_const_value(Index_ID nid, uint32_t value);

  Index_ID         find_type_const_sview(std::string_view value) const;
  Index_ID         find_type_const_value(uint32_t value) const;

  std::string_view get_type_const_sview(Index_ID nid) const;
  uint32_t         get_type_const_value(Index_ID nid) const;

  std::string_view get_constant(uint32_t const_id) const;

public:
  LGraph_Node_Type() = delete;
  explicit LGraph_Node_Type(std::string_view path, std::string_view name, Lg_type_id lgid) noexcept;
  virtual ~LGraph_Node_Type(){};

  const Node_sview_map &get_const_sview_map() const { return const_sview; };
  const Node_value_map &get_const_value_map() const { return const_value; };
  const Node_down_map  &get_down_nodes_map()  const { return down_nodes; };

};


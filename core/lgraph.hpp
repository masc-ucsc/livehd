//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "absl/container/flat_hash_map.h"

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

#include "lgedge.hpp"
#include "lgraphbase.hpp"
#include "node_type_base.hpp"
#include "node_type.hpp"

#include "node_type_base.hpp"

#include "graph_library.hpp"

#include "node_pin.hpp"
#include "node.hpp"
#include "edge.hpp"

class LGraph : public LGraph_Node_Type
{
private:
public:
protected:
  friend class Node;
  friend class Node_pin;
  friend class XEdge;
  friend class CFast_edge_iterator;
  friend class Forward_edge_iterator;
  friend class Backward_edge_iterator;
  friend class Fast_edge_iterator;

  using Hierarchy_cache = absl::flat_hash_map<Lg_type_id, Lg_type_id, Lg_type_id_hash>;

  void add_hierarchy_entry(std::string_view base, Lg_type_id lgid);

  Index_ID create_node_int() final;

  explicit LGraph(std::string_view _path, std::string_view _name, std::string_view _source, bool clear);

  bool has_node_outputs(Index_ID nid) const {
    I(nid < node_internal.size());
    return node_internal[nid].has_node_outputs();
  }

  bool has_node_inputs(Index_ID nid) const {
    I(nid < node_internal.size());
    return node_internal[nid].has_node_inputs();
  }

  Index_ID find_idx(const Node_pin &pin) const {
    if (likely(node_internal[pin.get_idx()].get_dst_pid() == pin.get_pid())) { // Common case
      return pin.get_idx();
    }
    return find_idx_from_pid(pin.get_idx(),pin.get_pid());
  }

  int get_num_outputs(Index_ID nid) const {
    I(nid < node_internal.size());
    I(node_internal[nid].is_master_root());
    return node_internal[nid].get_node_num_outputs();
  }

  int get_num_inputs(Index_ID nid) const {
    I(nid < node_internal.size());
    I(node_internal[nid].is_master_root());
    return node_internal[nid].get_node_num_inputs();
  }

  Index_ID get_node_nid(Index_ID idx) {
    I(node_internal.size() > idx);
    I(node_internal[idx].is_root());
    if (node_internal[idx].is_master_root())
      return idx;

    idx = node_internal[idx].get_nid();
    I(node_internal[idx].is_master_root());
    return idx;
  }

  Node_pin_iterator out_connected_pins(const Node &node) const;
  Node_pin_iterator inp_connected_pins(const Node &node) const;
  Node_pin_iterator out_setup_pins(const Node &node) const;
  Node_pin_iterator inp_setup_pins(const Node &node) const;

  XEdge_iterator out_edges(const Node &node) const;
  XEdge_iterator inp_edges(const Node &node) const;

  const LGraph *find_sub_lgraph_const(const Hierarchy_id hid) const;

  bool has_outputs(const Node_pin &pin) const {
    I(pin.get_idx() < node_internal.size());
    I(node_internal[pin.get_idx()].is_root());
    return node_internal[pin.get_idx()].has_pin_outputs();
  }
  bool has_inputs(const Node_pin &pin) const {
    I(pin.get_idx() < node_internal.size());
    I(node_internal[pin.get_idx()].is_root());
    return node_internal[pin.get_idx()].has_pin_inputs();
  }

  bool del_edge(const Node_pin &src, const Node_pin &dst) {
    I(node_internal.size()>src.get_idx());
    I(node_internal.size()>dst.get_idx());
    I(node_internal[src.get_idx()].is_root());
    I(node_internal[dst.get_idx()].is_root());

    return node_internal[src.get_idx()].del(dst.get_idx(),dst.get_pid(),dst.is_input());
  }

  bool is_graph_io(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    auto nid = node_internal[idx].get_nid();
    return nid == Node::Hardcoded_input_nid || nid == Node::Hardcoded_output_nid;
  }

  bool is_graph_input(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    auto nid = node_internal[idx].get_nid();
    return nid == Node::Hardcoded_input_nid;
  }

  bool is_graph_output(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    auto nid = node_internal[idx].get_nid();
    return nid == Node::Hardcoded_output_nid;
  }

  Index_ID fast_next(Index_ID nid) const {
    while (true) {
      nid.value++;
      if (nid >= static_cast<Index_ID>(node_internal.size())) return 0;
      if (!node_internal[nid].is_node_state()) continue;
      if (node_internal[nid].is_master_root()) return nid;
    }

    return 0;
  }

  Index_ID fast_next(Hierarchy_id hid, Index_ID nid) const {
    I(find_sub_lgraph(hid));

    return fast_next(nid);
  }

  bool is_sub(Index_ID nid) const {
    I(nid < node_type_table.size());
    I(node_internal[nid].is_node_state());
    I(node_internal[nid].is_master_root());

    Node_Type_Op op = node_type_table[nid];

    return op >= SubGraphMin_Op && op <= SubGraphMax_Op;
  }

  bool is_sub(Hierarchy_id hid, Index_ID nid) const {
    const LGraph *sub_g = find_sub_lgraph(hid);
    if (sub_g==0)
      return false; // This can be if the subgraph is not present (bbox)

    bool it_is_sub = sub_g->is_sub(nid);

    GI( it_is_sub, sub_g->sub_nodes.find(Node::Compact_class(nid)) != sub_g->sub_nodes.end());
    GI(!it_is_sub, sub_g->sub_nodes.find(Node::Compact_class(nid)) == sub_g->sub_nodes.end());

    return it_is_sub;
  }

public:
  LGraph()               = delete;
  LGraph(const LGraph &) = delete;

  virtual ~LGraph();

  Index_ID add_edge(const Node_pin &src, const Node_pin &dst) {
    I(!src.is_input());
    I(dst.is_input());
    I(dst.get_class_lgraph() == src.get_class_lgraph());

    return add_edge_int(dst.get_idx(), dst.get_pid(), src.get_idx(), src.get_pid());
  }

  Index_ID add_edge(const Node_pin &src, const Node_pin &dst, uint16_t bits) {
    I(!src.is_input());
    I(dst.is_input());
    I(dst.get_class_lgraph() == src.get_class_lgraph());
    Index_ID idx = add_edge_int(dst.get_idx(), dst.get_pid(), src.get_idx(), src.get_pid());
    set_bits(src.get_idx(), bits);
    return idx;
  }

  Forward_edge_iterator  forward();
  Backward_edge_iterator backward();
  Fast_edge_iterator fast(bool visit_sub=false);

  static bool    exists(std::string_view path, std::string_view name);
  static LGraph *create(std::string_view path, std::string_view name, std::string_view source);
  static LGraph *open(std::string_view path, int lgid);
  static LGraph *open(std::string_view path, std::string_view name);
  static void    rename(std::string_view path, std::string_view orig, std::string_view dest);

  void clear() override;
  void reload() override;
  void sync() override;
  void emplace_back() override;

  const LGraph *find_sub_lgraph(const Hierarchy_id hid) const {
    return find_sub_lgraph_const(hid);
  }
  LGraph *find_sub_lgraph(const Hierarchy_id hid) {
    return const_cast<LGraph *>(find_sub_lgraph_const(hid));
  }

  Node_pin add_graph_input(std::string_view str, Port_ID pos, uint16_t bits);
  Node_pin add_graph_output(std::string_view str, Port_ID pos, uint16_t bits);

  Node create_node();

  Node create_node(const Node &old_node);

  Node create_node(Node_Type_Op op);
  Node create_node(Node_Type_Op op, uint16_t bits);
  Node create_node_const(uint32_t value, uint16_t bits);
  Node create_node_const(std::string_view value);
  Node create_node_const(std::string_view value, uint16_t bits);
  Node create_node_sub(Lg_type_id sub);
  Node create_node_sub(std::string_view sub_name);

  Sub_node         &get_self_sub_node() const; // Access all input/outputs

  void dump();

  Node_pin get_graph_input(std::string_view str);
  Node_pin get_graph_output(std::string_view str);
  Node_pin get_graph_output_driver(std::string_view str);

  bool is_graph_input(std::string_view name) const;
  bool is_graph_output(std::string_view name) const;

  // Iterators defined in the lgraph_each.cpp

  void each_sorted_graph_io(std::function<void(const Node_pin &pin, Port_ID pos)> f1);
  void each_graph_input(std::function<void(const Node_pin &pin)> f1);
  void each_graph_output(std::function<void(const Node_pin &pin)> f1);

  void each_node_fast(std::function<void(const Node &node)> f1);

  void each_output_edge_fast(std::function<void(XEdge &edge)> f1);

  void each_sub_fast_direct(const std::function<bool(Node &, Lg_type_id)>);

  template <typename FN>
  void each_sub_fast(const FN f1) {
    if constexpr (std::is_invocable_r_v<bool, FN &, Node &, Lg_type_id>) {  // WARNING: bool must be before void
      each_sub_fast_direct(f1);
    } else if constexpr (std::is_invocable_r_v<void, FN &, Node &, Lg_type_id>) {
      auto f2 = [&f1](Node &node, Lg_type_id lgid) {
        f1(node, lgid);
        return true;
      };
      each_sub_fast_direct(f2);
    } else {
      I(false);
      each_sub_fast_direct(f1);  // Better error message if I keep this
    }
  };

  void each_root_fast_direct(std::function<bool(Node &)> f1);
  template <typename FN>
  void each_root_fast(const FN f1) {
    if constexpr (std::is_invocable_r_v<bool, FN &, Node &>) {  // WARNING: bool must be before void
      each_root_direct(f1);
    } else if constexpr (std::is_invocable_r_v<void, FN &, Node &>) {
      auto f2 = [&f1](Node &node) {
        f1(node);
        return true;
      };
      each_root_direct(f2);
    } else {
      I(false);
      each_root_direct(f1);  // Better error message if I keep this
    }
  };
};

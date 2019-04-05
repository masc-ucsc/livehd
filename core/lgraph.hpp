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
#include "nodebitwidth.hpp"
#include "nodedelay.hpp"
#include "nodesrcloc.hpp"
#include "node_type_base.hpp"
#include "node_type.hpp"

#include "node_type_base.hpp"

#include "graph_library.hpp"
#include "tech_library.hpp"

#include "node_pin.hpp"
#include "node.hpp"
#include "edge.hpp"

class LGraph : public LGraph_Node_Delay,
               public LGraph_Node_bitwidth,
               public LGraph_Node_Src_Loc,
               public LGraph_Node_Type
{
private:
  Index_ID add_graph_io_common();
public:
  using Hierarchy = absl::flat_hash_map<std::string, Lg_type_id>;

protected:
  friend class Node;
  friend class Node_pin;
  friend class XEdge;

  using Hierarchy_cache = absl::flat_hash_map<Lg_type_id, Lg_type_id, Lg_type_id_hash>;

  Hierarchy       hierarchy;
  Hierarchy_cache hierarchy_cache;  // Tracks used version to avoid recompute get_hierarchy

  void add_hierarchy_entry(std::string_view base, Lg_type_id lgid);

  Index_ID create_node_int() final;

  // TODO: convert std::string & to std::string_view
  explicit LGraph(const std::string &path, const std::string &name, const std::string &source, bool clear);

  bool has_node_outputs(Index_ID nid) const {
    I(nid < node_internal.size());
    return node_internal[nid].has_node_outputs();
  }

  bool has_node_inputs(Index_ID nid) const {
    I(nid < node_internal.size());
    return node_internal[nid].has_node_inputs();
  }

  Node_pin add_graph_input_int(std::string_view str, uint16_t bits);
  Node_pin add_graph_output_int(std::string_view str, uint16_t bits);

  Index_ID find_idx(const Node_pin &pin) const {
    if (likely(node_internal[pin.get_idx()].get_dst_pid() == pin.get_pid())) { // Common case
      return pin.get_idx();
    }
    return find_idx_from_pid(pin.get_idx(),pin.get_pid());
  }

  Node      get_node(Index_ID nid);

  Node_pin_iterator out_connected_pins(const Node &node) const;

  XEdge_iterator out_edges(const Node &node) const;
  XEdge_iterator inp_edges(const Node &node) const;

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
    assert(node_internal[src.get_idx()].is_root());
    assert(node_internal[dst.get_idx()].is_root());

    return node_internal[src.get_idx()].del(dst.get_idx(),dst.get_pid(),dst.is_input());
  }

  bool is_graph_io(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx].is_graph_io();
  }

  bool is_graph_input(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx].is_graph_io_input();
  }

  bool is_graph_output(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx].is_graph_io_output();
  }

public:
  LGraph()               = delete;
  LGraph(const LGraph &) = delete;

  virtual ~LGraph();

  Index_ID add_edge(const Node_pin &src, const Node_pin &dst) {
    I(!src.is_input());
    I(dst.is_input());
    I(dst.get_lgraph() == src.get_lgraph());
    GI(src.get_node().get_type().op == GraphIO_Op, dst.get_pid()); // GraphIO never have pid==0
    GI(dst.get_node().get_type().op == GraphIO_Op, src.get_pid()); // GraphIO never have pid==0

    return add_edge_int(dst.get_idx(), dst.get_pid(), src.get_idx(), src.get_pid());
  }

  Index_ID add_edge(const Node_pin &src, const Node_pin &dst, uint16_t bits) {
    I(!src.is_input());
    I(dst.is_input());
    I(dst.get_lgraph() == src.get_lgraph());
    Index_ID idx = add_edge_int(dst.get_idx(), dst.get_pid(), src.get_idx(), src.get_pid());
    set_bits(src.get_idx(), bits);
    return idx;
  }


  Forward_edge_iterator  forward();
  Backward_edge_iterator backward();
  Fast_edge_iterator fast();

  static bool    exists(std::string_view path, std::string_view name);
  static LGraph *create(std::string_view path, std::string_view name, std::string_view source);
  static LGraph *open(std::string_view path, int lgid);
  static LGraph *open(std::string_view path, std::string_view name);
  static void    rename(std::string_view path, std::string_view orig, std::string_view dest);
  bool           close() override;

  void clear() override;
  void reload() override;
  void sync() override;
  void emplace_back() override;

  Node_pin add_graph_input(std::string_view str, uint16_t bits, uint16_t offset);
  Node_pin add_graph_output(std::string_view str, uint16_t bits, uint16_t offset);

  Node create_node();

  Node create_node(Node_Type_Op op);
  Node create_node(Node_Type_Op op, uint16_t bits);
  Node create_node_const(uint32_t value, uint16_t bits);
  Node create_node_const(std::string_view value);
  Node create_node_const(std::string_view value, uint16_t bits);
  Node create_node_sub(Lg_type_id sub);

  void dump() const;

  Node_pin get_graph_input(std::string_view str);
  Node_pin get_graph_output(std::string_view str);
  Node_pin get_graph_output_driver(std::string_view str);

  bool is_graph_input(std::string_view name) const { return input_array.get_id(name) != 0; }
  bool is_graph_output(std::string_view name) const { return output_array.get_id(name) != 0; }

  // Iterators defined in the lgraph_each.cpp

  void each_graph_input(std::function<void(Node_pin &pin)> f1);
  void each_graph_output(std::function<void(Node_pin &pin)> f1);

  void each_node_fast(std::function<void(Node &node)> f1);

  void each_input_pin_fast(std::function<void(Node_pin &pin)> f1);
  void each_output_pin_fast(std::function<void(Node_pin &pin)> f1);

  void each_output_edge_fast(std::function<void(XEdge &edge)> f1);

  void each_sub_graph_fast_direct(const std::function<bool(Node &, const Lg_type_id &)>);

  template <typename FN>
  void each_sub_graph_fast(const FN f1) {
    if constexpr (std::is_invocable_r_v<bool, FN &, Node &, const Lg_type_id &>) {  // WARNING: bool must be before void
      each_sub_graph_fast_direct(f1);
    } else if constexpr (std::is_invocable_r_v<void, FN &, Node &, const Lg_type_id &>) {
      auto f2 = [&f1](Node &node, const Lg_type_id &lgid) {
        f1(node,lgid);
        return true;
      };
      each_sub_graph_fast_direct(f2);
    } else {
      I(false);
      each_sub_graph_fast_direct(f1);  // Better error message if I keep this
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

  const Hierarchy &get_hierarchy();
};


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
#include "hierarchy.hpp"

class LGraph : public LGraph_Node_Type {
protected:
  friend class Node;
  friend class Node_pin;
  friend class XEdge;
  friend class CFast_edge_iterator;
  friend class Forward_edge_iterator;
  friend class Backward_edge_iterator;
  friend class Fast_edge_iterator;

  struct Expanded_hierarchy_index {
    Expanded_hierarchy_index(uint64_t gpehidx, Lg_type_id plgid, Index_ID pnid)
        : grand_parent_ehidx(gpehidx)
        , parent_lgid(plgid)
        , parent_nid(pnid) {}
    uint64_t   grand_parent_ehidx;
    Lg_type_id parent_lgid;
    Index_ID   parent_nid;
  };

  using Hierarchy_map = mmap_map::map<Hierarchy_index, Expanded_hierarchy_index>;

  Hierarchy_map hierarchy_map;

  Lg_type_id get_hierarchy_lgid(const Hierarchy_index &hidx) const {
    if (!hidx)
      return lgid;

    Hierarchy_map_key up_hkey(hidx, 0);
    auto up_it = top_g->hierarchy_map.find(up_hkey);
    I(up_it != top_g->hierarchy_map.end());
    return top_g->hierarchy_map.get(up_it).lgid;
  }

  Hierarchy_index hierarchy_go_down(LGraph *top_g, const Hierarchy_index &hidx, Index_ID current_nid) const {

    Hierarchy_index down_hidx;

    Hierarchy_map_key up_hkey(child_hidx, 0);
    Hierarchy_map_key down_hkey(hidx, current_nid);

    auto down_it = top_g->hierarchy_map.find(down_key);
    if (down_it != top_g->hierarchy_map.end()) {
#ifndef NDEBUG
      auto up_it = top_g->hierarchy_map.find(up_hkey);
      I(up_it != top_g->hierarchy_map.end());
      const auto &up_data = top_g->hierarchy_map.get(up_it);
      I(up_data.hidx == hidx);
      I(up_data.nid  == current_nid);
      I(up_data.lgid == lgid);
#endif

      const auto &data = top_g->hierarchy_map.get(down_it);
      return data.hidx;
    }

    uint64_t child_hidx = top_g->hierarchy_map.size();

    auto child_lgid = get_type_sub(current_nid);
    auto up_it = top_g->hierarchy_map.find(up_hkey);
    if (up_it == top_g->hierarchy_map.end()) {
      top_g->hierarchy_map.set(up_hkey, {hidx, current_nid, lgid});
    } else {
      const auto &up_data = top_g->hierarchy_map.get(up_it);
      I(up_data.hidx == hidx);
      I(up_data.nid  == current_nid);
      I(up_data.lgid == lgid);
    }

    top_g->hierarchy_map.set(down_key, {hidx, current_nid, child_lgid});

    I(get_hierarchy_lgid(child_hidx) == child_lgid);

    return child_hidx;
  }

  Index_ID create_node_int() final;

  explicit LGraph(std::string_view _path, std::string_view _name, std::string_view _source, bool clear);

  bool has_node_outputs(Index_ID idx) const {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    return node_internal[idx].has_node_outputs();
  }

  bool has_node_inputs(Index_ID idx) const {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    return node_internal[idx].has_node_inputs();
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

  const LGraph *find_sub_lgraph_const(const Hierarchy_index &hidx) const {

    if (!hidx)
      return this;

    auto class_lgid = get_hierarchy_class_lgid(hidx);
    auto *current_g = LGraph::open(path, library->get_name(class_lgid));
    I(current_g);

    auto [p_lgid, p_nid] = get_hierarchy_parent_class(hidx);
    auto *parent_g = LGraph::open(path, p_lgid);
    I(parent_g);
    auto child_lgid = parent_g->get_type_sub(p_nid);

    auto *child_g = LGraph::open(path, child_lgid);
    I(child_g);

    return child_g;
  }

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

  bool is_sub(Index_ID nid) const { // Very common function (shoud be fast)
    I(nid < node_type_table.size());
    I(node_internal[nid].is_node_state());
    I(node_internal[nid].is_master_root());

    Node_Type_Op op = node_type_table[nid];

    return op >= SubGraphMin_Op && op <= SubGraphMax_Op;
  }

public:
  LGraph()               = delete;
  LGraph(const LGraph &) = delete;

  virtual ~LGraph();

  Hierarchy_index get_hierarchy_root() const {
    return 0; // 0 is hierarchy_root
  }

  bool has_edge(const Node_pin &driver, const Node_pin &sink) const;

  Index_ID add_edge(const Node_pin &src, const Node_pin &dst) {
    I(!src.is_input());
    I(dst.is_input());
    I(dst.get_class_lgraph() == src.get_class_lgraph());
    // Do not loop back unless pipelined or subgraph
    GI(src.get_node().get_nid() == dst.get_node().get_nid(), src.get_node().get_type().is_pipelined());

    return add_edge_int(dst.get_idx(), dst.get_pid(), src.get_idx(), src.get_pid());
  }

  Index_ID add_edge(const Node_pin &src, const Node_pin &dst, uint16_t bits) {
    Index_ID idx = add_edge(src, dst);
    I(idx = src.get_idx());
    GI(bits!=get_bits(idx), !is_type_const(node_internal[idx].get_nid())); // Do not overwrite bits in constants
    set_bits(idx, bits);
    return idx;
  }

  Forward_edge_iterator  forward(bool visit_sub=false);
  Backward_edge_iterator backward(bool visit_sub=false);
  Fast_edge_iterator fast(bool visit_sub=false);

  LGraph *clone_skeleton(std::string_view extended_name);

  static bool    exists(std::string_view path, std::string_view name);
  static LGraph *create(std::string_view path, std::string_view name, std::string_view source);
  static LGraph *open(std::string_view path, Lg_type_id lgid);
  static LGraph *open(std::string_view path, std::string_view name);
  static void    rename(std::string_view path, std::string_view orig, std::string_view dest);

  void clear() override;
  void reload() override;
  void sync() override;
  void emplace_back() override;

  const LGraph *find_sub_lgraph(const Hierarchy_index &hidx) const {
    return find_sub_lgraph_const(hidx);
  }
  LGraph *find_sub_lgraph(const Hierarchy_index &hidx) {
    return const_cast<LGraph *>(find_sub_lgraph_const(hidx));
  }

  Node_pin add_graph_input(std::string_view str, Port_ID pos, uint16_t bits);
  Node_pin add_graph_output(std::string_view str, Port_ID pos, uint16_t bits);

  Node create_node();

  Node create_node(const Node &old_node);

  Node create_node(Node_Type_Op op);
  Node create_node(Node_Type_Op op, uint16_t bits);
  Node create_node_const(uint32_t value);
  //Node create_node_const(std::string_view value);
  Node create_node_const(std::string_view value, uint16_t bits);
  Node create_node_sub(Lg_type_id sub);
  Node create_node_sub(std::string_view sub_name);

  Sub_node         &get_self_sub_node() const; // Access all input/outputs

  void dump();
  void dump_down_nodes();

  Node get_graph_input_node();
  Node get_graph_output_node();

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

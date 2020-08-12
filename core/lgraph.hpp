//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "absl/container/flat_hash_map.h"
#include "edge.hpp"
#include "graph_library.hpp"
#include "hierarchy.hpp"
#include "lgedge.hpp"
#include "lgraphbase.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "node_type.hpp"
#include "node_type_base.hpp"

class LGraph : public LGraph_Node_Type {
protected:
  friend class Node;
  friend class Node_pin;
  friend class XEdge;
  friend class CFast_edge_iterator;
  friend class Fwd_edge_iterator;
  friend class Bwd_edge_iterator;
  friend class Fast_edge_iterator;

  // Memoize tables that provide hints (not certainty because add/del operations)
  std::array<Index_ID, 8> memoize_const_hint;

  Hierarchy_tree htree;

  Hierarchy_tree *ref_htree() {
    if (htree.empty())
      htree.regenerate();
    return &htree;
  }

  explicit LGraph(std::string_view _path, std::string_view _name, std::string_view _source);

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
    if (likely(node_internal[pin.get_idx()].get_dst_pid() == pin.get_pid())) {  // Common case
      return pin.get_idx();
    }
    return find_idx_from_pid(pin.get_idx(), pin.get_pid());
  }

  Index_ID get_node_nid(Index_ID idx) const {
    if (node_internal[idx].is_master_root())
      return idx;

    return node_internal[idx].get_nid();
  }

  int get_node_num_outputs(Index_ID nid) const {
    I(nid < node_internal.size());
    I(node_internal[nid].is_master_root());
    return node_internal[nid].get_node_num_outputs();
  }

  int get_node_num_inputs(Index_ID nid) const {
    I(nid < node_internal.size());
    I(node_internal[nid].is_master_root());
    return node_internal[nid].get_node_num_inputs();
  }

  int get_node_pin_num_outputs(Index_ID idx) const {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    Index_ID nid = get_node_nid(idx);
    return node_internal[nid].get_node_pin_num_outputs(idx);
  }

  int get_node_pin_num_inputs(Index_ID idx) const {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    Index_ID nid = get_node_nid(idx);
    return node_internal[nid].get_node_pin_num_inputs(idx);
  }

  Node_pin_iterator out_connected_pins(const Node &node) const;
  Node_pin_iterator inp_connected_pins(const Node &node) const;
  Node_pin_iterator out_setup_pins(const Node &node) const;
  Node_pin_iterator inp_setup_pins(const Node &node) const;

  Node_pin_iterator inp_drivers(const Node &node, const absl::flat_hash_set<Node::Compact> &exclude) const;

  XEdge_iterator out_edges(const Node &node) const;
  XEdge_iterator inp_edges(const Node &node) const;

  XEdge_iterator out_edges_ordered(const Node &node) const;
  XEdge_iterator inp_edges_ordered(const Node &node) const;

  XEdge_iterator out_edges_ordered_reverse(const Node &node) const;
  XEdge_iterator inp_edges_ordered_reverse(const Node &node) const;

  XEdge_iterator out_edges(const Node_pin &pin) const;
  XEdge_iterator inp_edges(const Node_pin &pin) const;

  Node_pin_iterator inp_driver(const Node_pin &spin) const; // 1 or 0 drivers allowed for correct graphs

  bool has_outputs(const Node_pin &pin) const {
    I(!pin.is_invalid());
    I(pin.get_idx() < node_internal.size());
    I(node_internal[pin.get_idx()].is_root());
    GI(node_internal[pin.get_idx()].has_pin_outputs(), node_internal[pin.get_idx()].is_driver_setup());

    return node_internal[pin.get_idx()].is_driver_setup() && node_internal[pin.get_idx()].has_pin_outputs();
  }
  bool has_inputs(const Node_pin &pin) const {
    I(!pin.is_invalid());
    I(pin.get_idx() < node_internal.size());
    I(node_internal[pin.get_idx()].is_root());
    GI(node_internal[pin.get_idx()].has_pin_inputs(), node_internal[pin.get_idx()].is_sink_setup());

    return node_internal[pin.get_idx()].is_sink_setup() && node_internal[pin.get_idx()].has_pin_inputs();
  }

  void del_driver2node_int(Node &driver, const Node &sink);
  void del_sink2node_int(const Node &driver, Node &sink);

  bool del_edge_driver_int(const Node_pin &dpin, const Node_pin &spin);
  bool del_edge_sink_int(const Node_pin &dpin, const Node_pin &spin);

  void del_node(const Node &node);
  bool del_edge(const Node_pin &dpin, const Node_pin &spin);

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
      if (nid >= static_cast<Index_ID>(node_internal.size()))
        return 0;
      if (!node_internal[nid].is_valid())
        continue;
      if (is_graph_io(nid))
        continue;
      if (node_internal[nid].is_master_root())
        return nid;
    }

    return 0;
  }

  Index_ID fast_first() const {
    static_assert(Node::Hardcoded_output_nid > Node::Hardcoded_input_nid);
    return fast_next(Node::Hardcoded_output_nid);
  }

  bool is_sub(Index_ID nid) const {  // Very common function (shoud be fast)
    I(node_internal[nid].is_node_state());
    I(node_internal[nid].is_master_root());

    return node_internal[nid].get_type() == SubGraph_Op;
  }

  void trace_back2driver(Node_pin_iterator &xiter, Node_pin dpin) const;
  Node_pin_iterator trace_forward2sink(Node_pin pin) const;

public:
  LGraph()               = delete;
  LGraph(const LGraph &) = delete;

  virtual ~LGraph();

  bool is_empty() const { return fast_first() == 0; }

  Index_ID add_edge(const Node_pin &dpin, const Node_pin &spin) {
    I(dpin.is_driver());
    I(spin.is_sink());
    I(spin.get_class_lgraph() == dpin.get_class_lgraph());
    // Do not loop back unless pipelined or subgraph
    GI(!spin.is_graph_io() && !dpin.is_graph_io() && dpin.get_node().get_nid() == spin.get_node().get_nid(),
       dpin.get_node().get_type().is_pipelined());

    return add_edge_int(spin.get_idx(), spin.get_pid(), dpin.get_idx(), dpin.get_pid());
  }

  Index_ID add_edge(const Node_pin &dpin, const Node_pin &spin, uint32_t bits) {
    Index_ID idx = add_edge(dpin, spin);
    I(idx = dpin.get_idx());
    GI(bits != get_bits(idx), !is_type_const(node_internal[idx].get_nid()));  // Do not overwrite bits in constants
    set_bits(idx, bits);
    return idx;
  }

  Fwd_edge_iterator  forward(bool visit_sub = false);
  Bwd_edge_iterator  backward(bool visit_sub = false);
  Fast_edge_iterator fast(bool visit_sub = false);

  LGraph *clone_skeleton(std::string_view extended_name);

  static bool    exists(std::string_view path, std::string_view name);
  static LGraph *create(std::string_view path, std::string_view name, std::string_view source);
  static LGraph *open(std::string_view path, Lg_type_id lgid);
  static LGraph *open(std::string_view path, std::string_view name);
  static void    rename(std::string_view path, std::string_view orig, std::string_view dest);

  void clear() override;
  void sync() override;

  Node_pin add_graph_input(std::string_view str, Port_ID pos, uint32_t bits);
  Node_pin add_graph_output(std::string_view str, Port_ID pos, uint32_t bits);

  Node create_node();

  Node create_node(const Node &old_node);

  Node create_node(Node_Type_Op op);
  Node create_node(Node_Type_Op op, uint32_t bits);
  Node create_node_const(const Lconst &value);
  Node create_node_sub(Lg_type_id sub);
  Node create_node_sub(std::string_view sub_name);

  const Sub_node &get_self_sub_node() const;  // Access all input/outputs
  Sub_node *      ref_self_sub_node();        // Access all input/outputs

  void dump();
  void dump_down_nodes();

  Node get_graph_input_node(bool hier=false);
  Node get_graph_output_node(bool hier=false);

  Node_pin get_graph_input(std::string_view str);
  Node_pin get_graph_output(std::string_view str);
  Node_pin get_graph_output_driver_pin(std::string_view str);

  bool is_graph_input(std::string_view name) const;
  bool is_graph_output(std::string_view name) const;

  // Iterators defined in the lgraph_each.cpp

  void each_pin(const Node_pin &dpin, std::function<bool(Index_ID idx)> f1) const;
  void each_sorted_graph_io(std::function<void(Node_pin &pin, Port_ID pos)> f1);
  void each_graph_input(std::function<void(Node_pin &pin)> f1);
  void each_graph_output(std::function<void(Node_pin &pin)> f1);

  void each_node_fast(std::function<void(Node &node)> f1);

  void each_output_edge_fast(std::function<void(XEdge &edge)> f1);

  void each_sub_fast_direct(const std::function<bool(Node &, Lg_type_id)>);
  void each_sub_unique_fast(const std::function<bool(Node &, Lg_type_id)> fn);

  template <typename FN>
  void each_sub_fast(const FN f1) {
    if constexpr (std::is_invocable_r_v<bool, FN &, Node &, Lg_type_id>) {  // WARNING: bool must be before void
      each_sub_fast_direct(f1);
    } else if constexpr (std::is_invocable_r_v<void, FN &, Node &, Lg_type_id>) {
      auto f2 = [&f1](Node &node, Lg_type_id l_lgid) {
        f1(node, l_lgid);
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

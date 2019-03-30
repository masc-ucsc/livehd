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
#include "nodeplace.hpp"
#include "nodesrcloc.hpp"
#include "node_type_base.hpp"
#include "instance_names.hpp"
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
               public LGraph_Node_Place,
               public LGraph_InstanceNames,
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

public:
  LGraph()               = delete;
  LGraph(const LGraph &) = delete;

  virtual ~LGraph();

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
  // Deprecated
  //Node_pin add_graph_input(std::string_view str, uint16_t bits, uint16_t offset, Port_ID origininal_pos);
  //Node_pin add_graph_output(std::string_view str, uint16_t bits, uint16_t offset, Port_ID origininal_pos);

  XEdge_iterator out_edges(const Node &node) const;
  XEdge_iterator inp_edges(const Node &node) const;

#if 1
  // WARNING: deprecased to delete. Use Node_pin
  bool is_graph_input(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx].is_graph_io_input();
  }

  bool is_graph_output(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx].is_graph_io_output();
  }
#endif

  // Graph input/output functions
  bool is_graph_input(std::string_view name) const { return input_array.get_id(name) != 0; }
  bool is_graph_output(std::string_view name) const { return output_array.get_id(name) != 0; }

  bool is_graph_input(const Node_pin &pin) const {
    I(pin.get_idx() < node_internal.size());
    I(node_internal[pin.get_idx()].is_root());
    I(node_internal[pin.get_idx()].get_dst_pid() == pin.get_pid());
    return node_internal[pin.get_idx()].is_graph_io_input();
  }

  bool is_graph_output(const Node_pin &pin) const {
    I(pin.get_idx() < node_internal.size());
    I(node_internal[pin.get_idx()].is_root());
    I(node_internal[pin.get_idx()].get_dst_pid() == pin.get_pid());
    return node_internal[pin.get_idx()].is_graph_io_output();
  }

#if 0
  uint16_t get_bits(Node_pin &pin) const {
    I(pin.is_output());
    I(pin.get_idx() < node_internal.size());
    I(node_internal[pin.get_idx()].is_root());
    I(node_internal[pin.get_idx()].get_dst_pid() == pin.get_pid());
    return node_internal[pin.get_idx()].get_bits();
  }
  void set_bits(Node_pin &pin, uint16_t bits) {
    I(pin.is_output());
    Index_ID idx = find_idx(pin);
    I(idx);
    return node_internal[idx].set_bits(bits);
  }
#endif

#if 1
  // WARNING: deprecate: use Node create_node(xxx)
  Node create_node();
#endif

  Node create_node(Node_Type_Op op);
  Node create_node(Node_Type_Op op, uint16_t bits);
  Node create_node_u32(uint32_t value, uint16_t bits);
  Node create_node_const(std::string_view value);
  Node create_node_const(std::string_view value, uint16_t bits);
  Node create_node_sub(Lg_type_id sub);

  Index_ID get_master_nid(Index_ID idx) const { return node_internal[idx].get_master_root_nid(); }

  Forward_edge_iterator  forward();
  Backward_edge_iterator backward();
  Fast_edge_iterator fast();

  void dump() const;

  Node_pin get_graph_input(std::string_view str);
  Node_pin get_graph_output(std::string_view str);
  Node_pin get_graph_output_driver(std::string_view str);
#if 0
  Node_pin get_graph_input_sink(std::string_view str) const;
#endif

#if 1
  // WARNING: deprecated
  bool has_pin_outputs(Index_ID idx) const {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    return node_internal[idx].has_pin_outputs();
  }
  bool has_pin_inputs(Index_ID idx) const {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    return node_internal[idx].has_pin_inputs();
  }
#endif
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

#if 1
  // WARNING: deprecated. To delete once the attributes work

  uint32_t node_value_get(const Node_pin &pin) const;

#endif

  bool del_edge(const Node_pin &src, const Node_pin &dst) {
    I(node_internal.size()>src.get_idx());
    I(node_internal.size()>dst.get_idx());
    assert(node_internal[src.get_idx()].is_root());
    assert(node_internal[dst.get_idx()].is_root());

    return node_internal[src.get_idx()].del(dst.get_idx(),dst.get_pid(),dst.is_input());
  }

  // FIXME: rename and make dest first, src 2nd for consistency
  Index_ID add_edge(const Node_pin &src, const Node_pin &dst) {
    I(!src.is_input());
    I(dst.is_input());
    GI(src.get_node().get_type().op == GraphIO_Op, dst.get_pid()); // GraphIO never have pid==0
    GI(dst.get_node().get_type().op == GraphIO_Op, src.get_pid()); // GraphIO never have pid==0

    return add_edge_int(dst.get_idx(), dst.get_pid(), src.get_idx(), src.get_pid());
  }
  // FIXME: rename and make dest first, src 2nd for consistency
  Index_ID add_edge(const Node_pin &src, const Node_pin &dst, uint16_t bits) {
    I(!src.is_input());
    I(dst.is_input());
    Index_ID idx = add_edge_int(dst.get_idx(), dst.get_pid(), src.get_idx(), src.get_pid());
    set_bits(src.get_idx(), bits);
    return idx;
  }
  // Iterators defined in the lgraph_each.cpp

  void each_graph_input(std::function<void(Node_pin &pin)> f1);
  void each_graph_output(std::function<void(Node_pin &pin)> f1);

  void each_node_fast(std::function<void(Node &node)> f1);

  void each_input_pin_fast(std::function<void(Node_pin &pin)> f1);
  void each_output_pin_fast(std::function<void(Node_pin &pin)> f1);

  void each_output_edge_fast(std::function<void(Index_ID, Port_ID, Index_ID, Port_ID)> f1) const;

  void each_sub_graph_fast_direct(const std::function<bool(const Index_ID &, const Lg_type_id &, std::string_view)>) const;

  template <typename FN>
  void each_sub_graph_fast(const FN f1) const {
    if constexpr (std::is_invocable_r_v<bool, FN &, const Index_ID &, const Lg_type_id &,
                                        std::string_view>) {  // WARNING: bool must be before void
      each_sub_graph_fast_direct(f1);
    } else if constexpr (std::is_invocable_r_v<void, FN &, const Index_ID &, const Lg_type_id &, std::string_view>) {
      auto f2 = [&f1](const Index_ID &idx, const Lg_type_id &lgid, std::string_view iname) {
        f1(idx, lgid, iname);
        return true;
      };
      each_sub_graph_fast_direct(f2);
    } else if constexpr (std::is_invocable_r_v<bool, FN &, const Index_ID &, const Lg_type_id &>) {
      auto f2 = [&f1](const Index_ID &idx, const Lg_type_id &lgid, std::string_view iname) { return f1(idx, lgid); };
      each_sub_graph_fast_direct(f2);
    } else if constexpr (std::is_invocable_r_v<void, FN &, const Index_ID &, const Lg_type_id &>) {
      auto f2 = [&f1](const Index_ID &idx, const Lg_type_id &lgid, std::string_view iname) {
        f1(idx, lgid);
        return true;
      };
      each_sub_graph_fast_direct(f2);
    } else {
      I(false);
      each_sub_graph_fast_direct(f1);  // Better error message if I keep this
    }
  };

  void each_root_fast_direct(std::function<bool(const Index_ID &)> f1) const;
  template <typename FN>
  void each_root_fast(const FN f1) const {
    if constexpr (std::is_invocable_r_v<bool, FN &, const Index_ID &>) {  // WARNING: bool must be before void
      each_root_direct(f1);
    } else if constexpr (std::is_invocable_r_v<void, FN &, const Index_ID &>) {
      auto f2 = [&f1](const Index_ID &idx) {
        f1(idx);
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


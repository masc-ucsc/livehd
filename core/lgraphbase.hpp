//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <iostream>
#include <map>
#include <type_traits>
#include <vector>

#include "iassert.hpp"
#include "char_array.hpp"
#include "instance_names.hpp"
#include "lgraph_base_core.hpp"
#include "nodetype.hpp"

class Edge_iterator;
class Forward_edge_iterator;
class Backward_edge_iterator;

class LGraph_Base : public LGraph_Node_Type, public LGraph_InstanceNames {
private:
  Index_ID add_graph_io_common(std::string_view str, Index_ID nid, uint16_t bits);

protected:
  struct IO_port {
    Index_ID nid;
    Port_ID  pos;
    Port_ID  original_pos;
    bool     original_set;

    IO_port(Index_ID _nid, Port_ID _opos, bool force) : nid(_nid), pos(_opos), original_pos(_opos), original_set(force){};
  };

  // typedef std::pair<Index_ID, Port_ID> io_t; // node id and position at verilog

  Char_Array<IO_port> input_array;
  Char_Array<IO_port> output_array;

  static inline constexpr std::string_view unknown_io = "unknow name";

  Index_ID         create_node_space(Index_ID idx, Port_ID dst_pid, Index_ID master_nid, Index_ID root_nid);
  Index_ID         get_space_output_pin(Index_ID idx, Port_ID dst_pid, Index_ID &root_nid);
  Index_ID         get_space_output_pin(Index_ID master_nid, Index_ID idx, Port_ID dst_pid, Index_ID root_nid);
  Index_ID         get_space_input_pin(Index_ID master_nid, Index_ID idx, bool large = false);
  virtual Index_ID create_node_int() = 0;

  Index_ID add_edge_int(Index_ID dst_nid, Port_ID dst_pid, Index_ID src_nid, Port_ID inp_pid);

  void recompute_io_ports();

  Index_ID add_graph_input_int(std::string_view str, Index_ID nid, uint16_t bits);
  Index_ID add_graph_output_int(std::string_view str, Index_ID nid, uint16_t bits);
  Index_ID add_graph_input_int(std::string_view str, Index_ID nid, uint16_t bits, Port_ID original_pos);
  Index_ID add_graph_output_int(std::string_view str, Index_ID nid, uint16_t bits, Port_ID original_pos);

  void del_int_node(Index_ID idx);

  Index_ID find_idx_from_pid_int(Index_ID nid, Port_ID pid) const;

  friend Forward_edge_iterator;
  friend Backward_edge_iterator;

public:
  LGraph_Base() = delete;

  LGraph_Base(const LGraph_Base &) = delete;

  explicit LGraph_Base(const std::string &path, const std::string &_name, Lg_type_id lgid) noexcept;
  virtual ~LGraph_Base();

  virtual bool close();

  std::string_view get_subgraph_name(Index_ID nid) const;

  virtual void clear();
  virtual void sync();

  virtual void reload();
  virtual void emplace_back();

  // Graph input/output functions
  bool is_graph_input(std::string_view name) const { return input_array.get_id(name) != 0; }
  bool is_graph_output(std::string_view name) const { return output_array.get_id(name) != 0; }

  bool is_graph_input(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx].is_graph_io_input();
  }

  bool is_graph_output(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx].is_graph_io_output();
  }

  std::string_view get_graph_input_name(Index_ID nid) const;
  std::string_view get_graph_output_name(Index_ID nid) const;

  // get internal nid from given pid
  Index_ID get_graph_input_nid_from_pid(Port_ID pid) const;
  // get internal nid from given pid
  Index_ID get_graph_output_nid_from_pid(Port_ID pid) const;

  // get external pid from internal nid
  Port_ID get_graph_pid_from_nid(Index_ID nid) const;

  std::string_view get_graph_input_name_from_pid(Port_ID pid) const;
  std::string_view get_graph_output_name_from_pid(Port_ID pid) const;

  Node_Pin get_graph_input(std::string_view str) const;
  Node_Pin get_graph_output(std::string_view str) const;

  // get extra (non-master root) node for port_id pid
  // will allocate space if none is available
  Index_ID get_idx_from_pid(Index_ID nid, Port_ID pid);
  Index_ID find_idx_from_pid(Index_ID nid, Port_ID pid) const;
  void     set_bits_pid(Index_ID nid, Port_ID pid, uint16_t bits);
  uint16_t get_bits_pid(Index_ID nid, Port_ID pid) const;
  uint16_t get_bits_pid(Index_ID nid, Port_ID pid);

  // Graph Node functions
  uint16_t get_bits(Index_ID idx) const {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    return node_internal[idx].get_bits();
  }
  void set_bits(Index_ID idx, uint16_t bits) {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    node_internal[idx].set_bits(bits);
  }

  void add_edge(const Index_ID dst_idx, const Index_ID src_idx) {
    I(src_idx < node_internal.size());
    I(node_internal[src_idx].is_root());
    I(dst_idx < node_internal.size());
    I(node_internal[dst_idx].is_root());
    I(src_idx != dst_idx);

    add_edge_int(dst_idx, node_internal[dst_idx].get_dst_pid(), src_idx, node_internal[src_idx].get_dst_pid());
  }

  Index_ID add_edge(const Node_Pin src, const Node_Pin dst) {
    I(!src.is_input());
    I(dst.is_input());
    return add_edge_int(dst.get_nid(), dst.get_pid(), src.get_nid(), src.get_pid());
  }

  void del_edge(const Edge &edge);
  void del_node(Index_ID idx);

  Index_ID add_edge(const Node_Pin src, const Node_Pin dst, uint16_t bits) {
    I(!src.is_input());
    I(dst.is_input());
    Index_ID idx = add_edge_int(dst.get_nid(), dst.get_pid(), src.get_nid(), src.get_pid());
    set_bits(idx, bits);
    return idx;
  }

  bool has_outputs(Index_ID idx) const {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    return node_internal[idx].has_outputs();
  }
  bool has_inputs(Index_ID idx) const {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    return node_internal[idx].has_inputs();
  }

  Edge_iterator inp_edges(Index_ID nid) const;
  Edge_iterator out_edges(Index_ID nid) const;

  void print_stats() const;

  const Node_Internal &get_node_int(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx];
  }

  Node_Internal &get_node_int(Index_ID idx) {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx];
  }

  Port_ID get_dst_pid(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    I(node_internal[idx].is_root());
    return node_internal[idx].get_dst_pid();
  }

  bool is_root(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx].is_root();
  }

  static size_t max_size() { return (((size_t)1) << Index_Bits) - 1; }
  size_t        size() const { return node_internal.size(); }

  bool empty() const { return node_internal.size() == 0; }

  class _init {
  public:
    _init();
  } _static_initializer;

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
};

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <iostream>
#include <map>
#include <type_traits>
#include <vector>

#include "dense.hpp"
#include "iassert.hpp"
#include "char_array.hpp"
#include "lgraph_base_core.hpp"
#include "lgedge.hpp"

class Edge_raw_iterator;
class Forward_edge_iterator;
class Backward_edge_iterator;
class Fast_edge_iterator;

class LGraph_Base : public Lgraph_base_core {
private:

protected:
  struct IO_port {
    Index_ID nid;
    Port_ID  pos;
    Port_ID  original_pos;
    bool     original_set;

    IO_port(Index_ID _nid, Port_ID _opos, bool force) : nid(_nid), pos(_opos), original_pos(_opos), original_set(force){};
  };

  // typedef std::pair<Index_ID, Port_ID> io_t; // node id and position at verilog

  Port_ID             max_io_port_pid;
  Char_Array<IO_port> input_array;
  Char_Array<IO_port> output_array;
  Dense<Node_Internal> node_internal;

  static inline constexpr std::string_view unknown_io = "unknown";

  Index_ID         create_node_space(const Index_ID idx, const Port_ID dst_pid, const Index_ID master_nid, const Index_ID root_nid);
  Index_ID         get_space_output_pin(const Index_ID idx, const Port_ID dst_pid, Index_ID &root_nid);
  Index_ID         get_space_output_pin(const Index_ID master_nid, const Index_ID idx, const Port_ID dst_pid, const Index_ID root_nid);
  Index_ID         get_space_input_pin(const Index_ID master_nid, const Index_ID idx, bool large = false);
  virtual Index_ID create_node_int() = 0;

  Index_ID add_edge_int(Index_ID dst_nid, Port_ID dst_pid, Index_ID src_nid, Port_ID inp_pid);

  Port_ID recompute_io_ports(const Index_ID track_nid);

  void del_int_node(const Index_ID idx);

  Index_ID find_idx_from_pid(const Index_ID idx, const Port_ID pid) const;
  Index_ID setup_idx_from_pid(const Index_ID nid, const Port_ID pid);

  friend Forward_edge_iterator;
  friend Backward_edge_iterator;

  friend Fast_edge_iterator;
  Index_ID fast_next(Index_ID nid) const {
    while (true) {
      nid.value++;
      if (nid >= static_cast<Index_ID>(node_internal.size())) return 0;
      if (!node_internal[nid].is_node_state()) continue;
      if (node_internal[nid].is_master_root()) return nid;
    }

    return 0;
  }

  void del_node(Index_ID idx);
  void del_edge(const Edge_raw *edge_raw);

  Index_ID get_master_nid(Index_ID idx) const { return node_internal[idx].get_master_root_nid(); }

public:
  LGraph_Base() = delete;

  LGraph_Base(const LGraph_Base &) = delete;

  explicit LGraph_Base(const std::string &path, const std::string &_name, Lg_type_id lgid) noexcept;
  virtual ~LGraph_Base();

  virtual bool close();

  virtual void clear();
  virtual void sync();

  virtual void reload();
  virtual void emplace_back();

#if 1
  // WARNING: deprecated: Use get/set_bits(const Node_pin)
  void     set_bits_pid(Index_ID nid, Port_ID pid, uint16_t bits);
  uint16_t get_bits_pid(Index_ID nid, Port_ID pid) const;
  uint16_t get_bits_pid(Index_ID nid, Port_ID pid);
#endif

#if 1
  // WARNING: deprecated, move to protected
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
#endif

  // get internal nid from given pid
  Index_ID get_graph_input_nid_from_pid(Port_ID pid) const;
  // get internal nid from given pid
  Index_ID get_graph_output_nid_from_pid(Port_ID pid) const;

  // get external pid from internal nid
  Port_ID get_graph_pid_from_nid(Index_ID nid) const;

  std::string_view get_graph_input_name_from_pid(Port_ID pid) const;
  std::string_view get_graph_output_name_from_pid(Port_ID pid) const;

  void add_edge(const Index_ID dst_idx, const Index_ID src_idx) {
    I(src_idx < node_internal.size());
    I(node_internal[src_idx].is_root());
    I(dst_idx < node_internal.size());
    I(node_internal[dst_idx].is_root());
    I(src_idx != dst_idx);

    add_edge_int(dst_idx, node_internal[dst_idx].get_dst_pid(), src_idx, node_internal[src_idx].get_dst_pid());
  }

  Edge_raw_iterator inp_edges_raw(Index_ID nid) const;
  Edge_raw_iterator out_edges_raw(Index_ID nid) const;

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

  static size_t max_size() { return (((size_t)1) << Index_bits) - 1; }
  size_t        size() const { return node_internal.size(); }

  bool empty() const { return node_internal.size() == 0; }

  class _init {
  public:
    _init();
  } _static_initializer;


};


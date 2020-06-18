//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <iostream>
#include <map>
#include <type_traits>
#include <vector>

#include "iassert.hpp"
#include "lgedge.hpp"
#include "lgraph_base_core.hpp"
#include "mmap_vector.hpp"

class Fwd_edge_iterator;
class Bwd_edge_iterator;
class Fast_edge_iterator;
class Graph_library;

class LGraph_Base : public Lgraph_base_core {
private:
protected:
  mmap_lib::vector<Node_Internal> node_internal;

  static inline constexpr std::string_view unknown_io = "unknown";
  Graph_library *                          library;

  Index_ID create_node_space(const Index_ID idx, const Port_ID dst_pid, const Index_ID master_nid, const Index_ID root_nid);
  Index_ID get_space_output_pin(const Index_ID idx, const Port_ID dst_pid, Index_ID &root_nid);
  Index_ID get_space_output_pin(const Index_ID master_nid, const Index_ID idx, const Port_ID dst_pid, const Index_ID root_nid);
  // Index_ID         get_space_input_pin(const Index_ID master_nid, const Index_ID idx, bool large = false);
  Index_ID create_node_int();

  Index_ID add_edge_int(Index_ID dst_nid, Port_ID dst_pid, Index_ID src_nid, Port_ID inp_pid);

  Port_ID recompute_io_ports(const Index_ID track_nid);

  Index_ID find_idx_from_pid_int(const Index_ID idx, const Port_ID pid) const;
  Index_ID find_idx_from_pid(const Index_ID idx, const Port_ID pid) const {
    if (likely(node_internal[idx].get_dst_pid() == pid)) {  // Common case
      return idx;
    }
    return find_idx_from_pid_int(idx, pid);
  }

  Index_ID setup_idx_from_pid(const Index_ID nid, const Port_ID pid);

  void setup_driver(const Index_ID idx) {
    I(idx < node_internal.size());
    node_internal.ref(idx)->set_driver_setup();
  }
  void setup_sink(const Index_ID idx) {
    I(idx < node_internal.size());
    node_internal.ref(idx)->set_sink_setup();
  }

  Index_ID get_master_nid(Index_ID idx) const { return node_internal[idx].get_master_root_nid(); }

public:
  LGraph_Base() = delete;

  LGraph_Base(const LGraph_Base &) = delete;

  explicit LGraph_Base(std::string_view _path, std::string_view _name, Lg_type_id lgid) noexcept;
  virtual ~LGraph_Base();

  virtual void clear();
  virtual void sync();

  void emplace_back();

#if 1
  // WARNING: deprecated: Use get/set_bits(const Node_pin)
  void     set_bits_pid(Index_ID nid, Port_ID pid, uint32_t bits);
  uint32_t get_bits_pid(Index_ID nid, Port_ID pid) const;
  uint32_t get_bits_pid(Index_ID nid, Port_ID pid);
#endif

#if 1
  // WARNING: deprecated, move to protected
  uint32_t get_bits(Index_ID idx) const {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    return node_internal[idx].get_bits();
  }
  void set_bits(Index_ID idx, uint32_t bits) {
    I(idx < node_internal.size());
    I(node_internal[idx].is_root());
    node_internal.ref(idx)->set_bits(bits);
  }
#endif

  void add_edge(const Index_ID dst_idx, const Index_ID src_idx) {
    I(src_idx < node_internal.size());
    I(node_internal[src_idx].is_root());
    I(dst_idx < node_internal.size());
    I(node_internal[dst_idx].is_root());
    I(src_idx != dst_idx);

    add_edge_int(dst_idx, node_internal[dst_idx].get_dst_pid(), src_idx, node_internal[src_idx].get_dst_pid());
  }

  void print_stats() const;

  const Node_Internal &get_node_int(Index_ID idx) const {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx];
  }

  /*
  Node_Internal &get_node_int(Index_ID idx) {
    I(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx];
  }
  */

  bool is_valid_node(Index_ID nid) const {
    if (nid >= node_internal.size()) return false;
    return node_internal[nid].is_valid() && node_internal[nid].is_master_root();
  }

  bool is_valid_node_pin(Index_ID idx) const {
    if (idx >= node_internal.size()) return false;
    return node_internal[idx].is_valid() && node_internal[idx].is_root();
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

  class _init {
  public:
    _init();
  } _static_initializer;

  const Graph_library &get_library() const { return *library; }
  Graph_library *      ref_library() const { return library; }
};

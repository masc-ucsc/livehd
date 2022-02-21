//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <iostream>
#include <map>
#include <type_traits>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "edge.hpp"
#include "iassert.hpp"
#include "lgedge.hpp"
#include "lgraph_base_core.hpp"

#include "aligned_vector.hpp"

class Fwd_edge_iterator;
class Bwd_edge_iterator;
class Fast_edge_iterator;
class Graph_library;

class Lgraph_Base : public Lgraph_base_core {
private:
protected:
  is::aligned_vector<Node_internal, 4096> node_internal;

  Graph_library *                          library;

  absl::flat_hash_map<uint32_t, uint32_t> idx_insert_cache;

  Index_id create_node_space(const Index_id idx, const Port_ID dst_pid, const Index_id master_nid, const Index_id root_nid);
  Index_id get_space_output_pin(const Index_id idx, const Port_ID dst_pid, Index_id &root_nid);
  Index_id get_space_output_pin(const Index_id master_nid, const Index_id idx, const Port_ID dst_pid, const Index_id root_nid);
  // Index_id         get_space_input_pin(const Index_id master_nid, const Index_id idx, bool large = false);
  Index_id create_node_int();

  void add_edge_int(Index_id dst_nid, Port_ID dst_pid, Index_id src_nid, Port_ID inp_pid);

  Port_ID recompute_io_ports(const Index_id track_nid);

  Index_id find_idx_from_pid_int(const Index_id nid, const Port_ID pid) const;
  Index_id find_idx_from_pid(const Index_id nid, const Port_ID pid) const {
    if (likely(node_internal[nid].get_dst_pid() == pid)) {  // Common case
      return nid;
    }
    return find_idx_from_pid_int(nid, pid);
  }

  Index_id setup_idx_from_pid(const Index_id nid, const Port_ID pid);

  Index_id get_master_nid(Index_id idx) const { return node_internal[idx].get_master_root_nid(); }

  uint32_t get_bits(Index_id idx) const {
    I(idx < node_internal.size());
    return node_internal[idx].get_bits();
  }

  void set_bits(Index_id idx, uint32_t bits) {
    I(idx < node_internal.size());
    //node_internal.ref_lock();
    node_internal[idx].set_bits(bits);
    //node_internal.ref_unlock();
  }

public:
  Lgraph_Base() = delete;

  Lgraph_Base(const Lgraph_Base &) = delete;

  explicit Lgraph_Base(std::string_view _path, std::string_view _name, Lg_type_id _lgid, Graph_library *_lib) noexcept;
  virtual ~Lgraph_Base();

  virtual void clear();

  void emplace_back();

  void add_edge(const Index_id dst_idx, const Index_id src_idx) {
    I(src_idx < node_internal.size());
    I(dst_idx < node_internal.size());
    I(src_idx != dst_idx);

    add_edge_int(dst_idx, node_internal[dst_idx].get_dst_pid(), src_idx, node_internal[src_idx].get_dst_pid());
  }

  void print_stats() const;

  bool is_valid_node(Index_id nid) const {
    if (nid >= node_internal.size())
      return false;

    //node_internal.ref_lock();
    const auto *ref = &node_internal[nid];
    auto ret        = ref->is_valid() && ref->is_master_root();
    //node_internal.ref_unlock();

    return ret;
  }

  bool is_valid_node_pin(Index_id idx) const {
    if (idx >= node_internal.size())
      return false;

    //node_internal.ref_lock();
    const auto *ref = &node_internal[idx];
    auto ret        = ref->is_valid() && ref->is_root();
    //node_internal.ref_unlock();

    return ret;
  }

  Port_ID get_dst_pid(Index_id idx) const {
    I(static_cast<Index_id>(node_internal.size()) > idx);
    return node_internal[idx].get_dst_pid();
  }

  bool is_root(Index_id idx) const {
    I(static_cast<Index_id>(node_internal.size()) > idx);

    //node_internal.ref_lock();
    const auto *ref = &node_internal[idx];
    auto ret        = ref->is_root();
    //node_internal.ref_unlock();

    return ret;
  }

  static size_t max_size() { return (((size_t)1) << Index_bits) - 1; }
  size_t        size() const { return node_internal.size(); }

  class _init {
  public:
    _init();
  } _static_initializer;

  const Graph_library &get_library() const { return *library; }
  Graph_library *      ref_library() const { return library; }

  static void error_int(std::string_view text);
  static void warn_int(std::string_view text);
  static void info_int(std::string_view text);

  template <typename S, typename... Args>
  static void error(const S &format, Args &&...args) {
    error_int(fmt::format(format, args...));
  }

  template <typename S, typename... Args>
  static void warn(const S &format, Args &&...args) {
    warn_int(fmt::format(format, args...));
  }

  template <typename S, typename... Args>
  static void info(const S &format, Args &&...args) {
    info_int(fmt::format(format, args...));
  }
};

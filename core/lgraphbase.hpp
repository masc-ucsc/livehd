//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LGRAPHBASE_H
#define LGRAPHBASE_H

#include <stdint.h>

#include <iostream>
#include <map>
#include <vector>

#include "char_array.hpp"
#include "graph_library.hpp"
#include "lglog.hpp"
#include "lgraph_base_core.hpp"
#include "nodetype.hpp"
#include "tech_library.hpp"

#ifndef likely
#define likely(x)       __builtin_expect((x),1)
#endif
#ifndef unlikely
#define unlikely(x)     __builtin_expect((x),0)
#endif

class Edge_iterator;

class LGraph_Base : public LGraph_Node_Type {
protected:
  bool        locked;
  Port_ID     io_nums = 1;
  int         lgraph_id;

  struct str_cmp_i { // case insensitive string compare for IO
    bool operator()(char const *a, char const *b) const {
      return strcasecmp(a, b) < 0;
    }
  };

  struct IO_port {
    Index_ID nid;
    Port_ID  pos;
    Port_ID  original_pos;

    IO_port(Index_ID _nid, Port_ID _opos)
      :nid(_nid)
      ,pos(0)
      ,original_pos(_opos) {
    };
  };

  //typedef std::pair<Index_ID, Port_ID> io_t; // node id and position at verilog

  Char_Array<IO_port> input_array;
  Char_Array<IO_port> output_array;

  // Integrate graph and tech library?
  Graph_library *library;
  Tech_library * tlibrary;

  Index_ID         create_node_space(Index_ID idx, Port_ID out_pid, Index_ID master_nid, Index_ID root_nid);
  Index_ID         get_space_output_pin(Index_ID idx, Port_ID out_pid, Index_ID &root_nid);
  Index_ID         get_space_output_pin(Index_ID master_nid, Index_ID idx, Port_ID out_pid, Index_ID root_nid);
  Index_ID         get_space_input_pin(Index_ID master_nid, Index_ID idx, bool large = false);
  virtual Index_ID create_node_int() = 0;

  Index_ID add_edge_int(Index_ID dst_nid, Port_ID out_pid, Index_ID src_nid, Port_ID inp_pid);

  void recompute_io_ports();

  Index_ID add_graph_input(const char *str, Index_ID nid = 0, uint16_t bits = 0, Port_ID original_pos = 0);
  Index_ID add_graph_output(const char *str, Index_ID nid = 0, uint16_t bits = 0, Port_ID original_pos = 0);

  void del_int_node(Index_ID idx);

  Index_ID find_idx_from_pid_int(Index_ID nid, Port_ID pid) const;

  friend Forward_edge_iterator;
  friend Backward_edge_iterator;

public:
  LGraph_Base() = delete;

  LGraph_Base(const LGraph_Base&) = delete;

  explicit LGraph_Base(const std::string &path, const std::string &_name) noexcept;
  virtual ~LGraph_Base();

  void close();

  int lg_id() const { return lgraph_id; }

  const Graph_library *get_library() const { return library; }

  const Tech_library *get_tlibrary() const { return tlibrary; }
  Tech_library *get_tech_library() { return tlibrary; }

  const std::string &get_subgraph_name(Index_ID nid) const;

  void get_lock();
  // Method called after the char_arrays and node_internal are reloaded
  virtual void reload();
  virtual void clear();
  virtual void sync();
  virtual void emplace_back();

  // Graph input/output functions
  bool is_graph_input(const char *str) const;
  bool is_graph_output(const char *str) const;

  bool is_graph_input(const std::string &str) const {
    return is_graph_input(str.c_str());
  }
  bool is_graph_output(const std::string &str) const {
    return is_graph_output(str.c_str());
  }

  bool is_graph_input(Index_ID idx) const;
  bool is_graph_output(Index_ID idx) const;

  const char *get_graph_input_name(Index_ID nid) const;
  const char *get_graph_output_name(Index_ID nid) const;

  // get internal nid from given pid
  Index_ID get_graph_input_nid_from_pid(Port_ID pid) const;
  // get internal nid from given pid
  Index_ID get_graph_output_nid_from_pid(Port_ID pid) const;

  // get external pid from internal nid
  Port_ID get_graph_pid_from_nid(Index_ID nid) const;

  const char *get_graph_input_name_from_pid(Port_ID pid) const;
  const char *get_graph_output_name_from_pid(Port_ID pid) const;

  Node_Pin get_graph_input(const char *str) const;
  Node_Pin get_graph_output(const char *str) const;

  Node_Pin get_graph_input(const std::string &str) const {
    return get_graph_input(str.c_str());
  }
  Node_Pin get_graph_output(const std::string &str) const {
    return get_graph_output(str.c_str());
  }

  // get extra (non-master root) node for port_id pid
  // will allocate space if none is available
  Index_ID get_idx_from_pid(Index_ID nid, Port_ID pid);
  Index_ID find_idx_from_pid(Index_ID nid, Port_ID pid) const;
  void     set_bits_pid(Index_ID nid, Port_ID pid, uint16_t bits);
  uint16_t get_bits_pid(Index_ID nid, Port_ID pid) const;
  uint16_t get_bits_pid(Index_ID nid, Port_ID pid);

  // Graph Node functions
  uint16_t get_bits(Index_ID idx) const {
    assert(idx < node_internal.size());
    assert(node_internal[idx].is_root());
    return node_internal[idx].get_bits();
  }
  void set_bits(Index_ID idx, uint16_t bits) {
    assert(idx < node_internal.size());
    assert(node_internal[idx].is_root());
    node_internal[idx].set_bits(bits);
  }

  Index_ID add_edge(const Node_Pin src, const Node_Pin dst) {
    assert(!src.is_input());
    assert(dst.is_input());
    return add_edge_int(dst.get_nid(), dst.get_pid(), src.get_nid(), src.get_pid());
  }

  void del_edge(const Edge &edge);
  void del_node(Index_ID idx);

  Index_ID add_edge(const Node_Pin src, const Node_Pin dst, uint16_t bits) {
    assert(!src.is_input());
    assert(dst.is_input());
    Index_ID idx = add_edge_int(dst.get_nid(), dst.get_pid(), src.get_nid(), src.get_pid());
    set_bits(idx, bits);
    return idx;
  }

  Edge_iterator inp_edges(Index_ID nid) const;
  Edge_iterator out_edges(Index_ID nid) const;

  void print_stats() const;

  const Node_Internal &get_node_int(Index_ID idx) const {
    assert(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx];
  }

  Node_Internal &get_node_int(Index_ID idx) {
    assert(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx];
  }

  bool is_root(Index_ID idx) const {
    assert(static_cast<Index_ID>(node_internal.size()) > idx);
    return node_internal[idx].is_root();
  }

  static size_t max_size() {
    return (((size_t)1) << Index_Bits) - 1;
  }
  size_t size() const {
    return node_internal.size();
  }

  bool empty() const {
    return node_internal.size() == 0;
  }

  class _init {
  public:
    _init();
  } _static_initializer;
};

#endif

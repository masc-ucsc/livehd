//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LGRAPH_H
#define LGRAPH_H

#include "lgedge.hpp"
#include "lgraphbase.hpp"
#include "lgwirenames.hpp"
#include "nodebitwidth.hpp"
#include "nodedelay.hpp"
#include "nodeplace.hpp"
#include "nodesrcloc.hpp"
#include "nodetype.hpp"

class Node;
class ConstNode;
class Edge_iterator;

class LGraph : public LGraph_Node_Delay,
               public LGraph_Node_bitwidth,
               public LGraph_Node_Src_Loc,
               public LGraph_WireNames,
               public LGraph_Node_Place {
public:
  using Hierarchy = absl::flat_hash_map<std::string, Lg_type_id>;

protected:
  friend class ConstNode;
  friend class Node;
  friend class Node_pin;

  using Hierarchy_cache = absl::flat_hash_map<Lg_type_id, Lg_type_id>;

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
  Node_pin add_graph_input(std::string_view str, uint16_t bits, uint16_t offset, Port_ID origininal_pos);
  Node_pin add_graph_output(std::string_view str, uint16_t bits, uint16_t offset, Port_ID origininal_pos);

#if 1
  // WARNING: deprecate: use Node create_node(xxx)
  Node create_node();
#endif

  Node create_node(Node_Type_Op op);
  Node create_node(Node_Type_Op op, uint16_t bits);
  Node create_node_u32(uint32_t value, uint16_t bits);
  Node create_node_const(std::string_view value);
  Node create_node_const(std::string_view value, uint16_t bits);

#if 1
  // WARNING: deprecated: move to protected
  Node      get_node(Index_ID nid);
  ConstNode get_node(Index_ID nid) const;
#endif

  ConstNode get_node(const Node_pin &pin) const;
  Node      get_node(const Node_pin &pin);

  ConstNode get_dest_node(const Edge &edge) const;
  Node      get_dest_node(const Edge &edge);

  Index_ID get_master_nid(Index_ID idx) const { return node_internal[idx].get_master_root_nid(); }

  Forward_edge_iterator  forward() const;
  Backward_edge_iterator backward() const;

  void dump() const;

  Node_pin get_graph_input(std::string_view str) const;
  Node_pin get_graph_output(std::string_view str) const;
  Node_pin get_graph_output_driver(std::string_view str) const;
#if 0
  Node_pin get_graph_input_sink(std::string_view str) const;
#endif

  // Iterators defined in the lgraph_each.cpp

  void each_graph_input(std::function<void(const Node_pin &pin)> f1) const;
  template <class Func, class T>
  void each_graph_input(Func &&func, T *first) const {
    each_graph_input(std::bind(func, first, std::placeholders::_1));
  }

  void each_graph_output(std::function<void(const Node_pin &pin)> f1) const;
  template <class Func, class T>
  void each_graph_output(Func &&func, T *first) const {
    each_graph_output(std::bind(func, first, std::placeholders::_1));
  }

  void each_node_fast(std::function<void(ConstNode &node)> f1) const;
  void each_node_fast(std::function<void(Node &node)> f1);
  template <class Func, class T>
  void each_node_fast(Func &&func, T *first) {
    each_node_fast(std::bind(func, first, std::placeholders::_1));
  }
  template <class Func, class T>
  void each_node_fast(Func &&func, const T *first) const {
    each_node_fast(std::bind(func, first, std::placeholders::_1));
  }

  void each_input_pin_fast(std::function<void(const Node_pin &pin)> f1) const;
  template <class Func, class T>
  void each_input_pin_fast(Func &&func, T *first) const {
    each_input_pin_fast(std::bind(func, first, std::placeholders::_1));
  }

  void each_output_pin_fast(std::function<void(const Node_pin &pin)> f1) const;
  template <class Func, class T>
  void each_output_pin_fast(Func &&func, T *first) const {
    each_output_pin_fast(std::bind(func, first, std::placeholders::_1));
  }

  void each_output_edge_fast(std::function<void(Index_ID, Port_ID, Index_ID, Port_ID)> f1) const;
  template <class Func, class T>
  void each_output_edge_fast(Func &&func, T *first) const {
    each_output_edge_fast(
        std::bind(func, first, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
  }

  const Hierarchy &get_hierarchy();
};

// Clean interface/iterator for most operations. It must call graph
class ConstNode {
private:
  const LGraph *g;

protected:
  Index_ID nid;

public:
  ConstNode(const LGraph *_g, Index_ID _nid) {
    g   = _g;
    nid = _nid;
  };

#if 1
  // WARNING: deprecated: Move to protected used just by lgraph
  Index_ID get_nid() const {
    assert(nid);
    return nid;
  }
#endif
  Node_Type  get_type() const { return g->node_type_get(nid); }
  float      get_delay() const { return g->node_delay_get(nid); }
  Node_Place get_place() const { return g->node_place_get(nid); }

  Node_pin get_driver_pin() const {
    I(g->node_type_get(nid).has_single_output());
    return Node_pin(nid,0,false);
  }
  Node_pin get_driver_pin(Port_ID pid) const {
    I(g->node_type_get(nid).has_output(pid));
    Index_ID idx = g->find_idx_from_pid(nid,pid);
    I(idx);
    return Node_pin(idx, pid, false);
  }
  Node_pin get_sink_pin(Port_ID pid) const {
    I(g->node_type_get(nid).has_input(pid));
    Index_ID idx = g->find_idx_from_pid(nid,pid);
    I(idx);
    return Node_pin(idx,pid,true);
  }
  Node_pin get_sink_pin() const {
    I(g->node_type_get(nid).has_single_input());
    return Node_pin(nid,0,true);
  }

  virtual const Edge_iterator inp_edges() const;
  virtual const Edge_iterator out_edges() const;

  bool has_inputs () const { return g->has_node_inputs (nid); }
  bool has_outputs() const { return g->has_node_outputs(nid); }

  bool is_root() const { return g->is_root(nid); }
//  bool is_graph_input() const { return g->is_graph_input(nid); }
//  bool is_graph_output() const { return g->is_graph_output(nid); }
};

class Node : public ConstNode {
private:
  LGraph *g;

protected:
public:
  Node(LGraph *_g, Index_ID _nid) : ConstNode(_g, _nid) { g = _g; };

#if 1
  // WARNING: deprecated: do not set type after creating
  void set(const Node_Type_Op op) { g->node_type_set(nid, op); }
#endif

  Node_pin setup_driver_pin(Port_ID pid) {
    I(g->node_type_get(nid).has_output(pid));
    Index_ID idx = g->setup_idx_from_pid(nid,pid);
    return Node_pin(idx, pid, false);
  }
  Node_pin setup_driver_pin() const {
    I(g->node_type_get(nid).has_single_output());
    return Node_pin(nid,0,false);
  }

  Node_pin setup_sink_pin(Port_ID pid) {
    I(g->node_type_get(nid).has_input(pid));
    Index_ID idx = g->setup_idx_from_pid(nid,pid);
    return Node_pin(idx,pid,true);
  }
  Node_pin setup_sink_pin() const {
    I(g->node_type_get(nid).has_single_input());
    return Node_pin(nid,0,true);
  }

  const Edge_iterator inp_edges() const override;
  const Edge_iterator out_edges() const override;
};

#endif

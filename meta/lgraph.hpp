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
  using Hierarchy_cache = absl::flat_hash_map<Lg_type_id, Lg_type_id>;

  Hierarchy       hierarchy;
  Hierarchy_cache hierarchy_cache;  // Tracks used version to avoid recompute get_hierarchy

  void add_hierarchy_entry(std::string_view base, Lg_type_id lgid);

  Index_ID create_node_int() final;

  // TODO: convert std::string & to std::string_view
  explicit LGraph(const std::string &path, const std::string &name, const std::string &source, bool clear);

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

  Index_ID add_graph_input(std::string_view str, Index_ID nid, uint16_t bits, uint16_t offset);
  Index_ID add_graph_output(std::string_view str, Index_ID nid, uint16_t bits, uint16_t offset);
  Index_ID add_graph_input(std::string_view str, Index_ID nid, uint16_t bits, uint16_t offset, Port_ID origininal_pos);
  Index_ID add_graph_output(std::string_view str, Index_ID nid, uint16_t bits, uint16_t offset, Port_ID origininal_pos);

  Node            create_node();
  Node            get_node(Index_ID nid);
  const ConstNode get_node(Index_ID nid) const;

  ConstNode get_dest_node(const Edge &edge) const;
  Node      get_dest_node(const Edge &edge);

  uint16_t get_offset(Index_ID nid) const override { return LGraph_WireNames::get_offset(nid); }

  Index_ID get_master_nid(Index_ID idx) const { return node_internal[idx].get_master_root_nid(); }

  Forward_edge_iterator  forward() const;
  Backward_edge_iterator backward() const;

  void dump() const;

  // Iterators defined in the lgraph_each.cpp

  void each_input(std::function<void(Index_ID)> f1) const;
  void each_input(std::function<void(Index_ID, Port_ID)> f1) const;
  template <class Func, class T>
  void each_input(Func &&func, T *first) const {
    each_input(std::bind(func, first, std::placeholders::_1, std::placeholders::_2));
  }

  void each_output(std::function<void(Index_ID)> f1) const;
  void each_output(std::function<void(Index_ID, Port_ID)> f1) const;
  template <class Func, class T>
  void each_output(Func &&func, T *first) const {
    each_output(std::bind(func, first, std::placeholders::_1, std::placeholders::_2));
  }

  void each_master_root_fast(std::function<void(Index_ID)> f1) const;
  template <class Func, class T>
  void each_master_root_fast(Func &&func, T *first) const {
    each_master_root_fast(std::bind(func, first, std::placeholders::_1));
  }

  void each_root_fast(std::function<void(Index_ID)> f1) const;
  template <class Func, class T>
  void each_root_fast(Func &&func, T *first) const {
    each_root_fast(std::bind(func, first, std::placeholders::_1));
  }

  void each_input_root_fast(std::function<void(Index_ID, Port_ID)> f1) const;
  template <class Func, class T>
  void each_input_root_fast(Func &&func, T *first) const {
    each_input_root_fast(std::bind(func, first, std::placeholders::_1, std::placeholders::_2));
  }

  void each_output_root_fast(std::function<void(Index_ID, Port_ID)> f1) const;
  template <class Func, class T>
  void each_output_root_fast(Func &&func, T *first) const {
    each_output_root_fast(std::bind(func, first, std::placeholders::_1, std::placeholders::_2));
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

  Index_ID get_nid() const {
    assert(nid);
    return nid;
  }

  Node_Type  type_get() const { return g->node_type_get(nid); }
  float      delay_get() const { return g->node_delay_get(nid); }
  Node_Place place_get() const { return g->node_place_get(nid); }

  virtual const Edge_iterator inp_edges() const;
  virtual const Edge_iterator out_edges() const;

  bool is_root() const { return g->is_root(nid); }
  bool is_graph_input() const { return g->is_graph_input(nid); }
  bool is_graph_output() const { return g->is_graph_output(nid); }
};

class Node : public ConstNode {
private:
  LGraph *g;

protected:
public:
  Node(LGraph *_g, Index_ID _nid) : ConstNode(_g, _nid) { g = _g; };

  void set(const Node_Type_Op op) { g->node_type_set(nid, op); }

  const Edge_iterator inp_edges() const override;
  const Edge_iterator out_edges() const override;
};

#endif

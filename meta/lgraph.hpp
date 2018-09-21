//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LGRAPH_H
#define LGRAPH_H

#include "graph_library.hpp"
#include "instance_names.hpp"
#include "lgedge.hpp"
#include "lgraphbase.hpp"
#include "lgwirenames.hpp"
#include "nodedelay.hpp"
#include "nodeplace.hpp"
#include "nodesrcloc.hpp"
#include "nodetype.hpp"
#include "symboltable.hpp"
#include "tech_library.hpp"

class Node;
class ConstNode;
class Edge_iterator;
class Graph_library;

class LGraph : public LGraph_Node_Delay,
               public LGraph_Node_Src_Loc,
               public LGraph_WireNames,
               public LGraph_InstanceNames,
               public LGraph_Node_Place,
               public LGraph_Symbol_Table {
protected:
  // FIXME: for live I need one instance per lgdb. Do it similar to library, or
  // keep references to lgraphs in the library
  static std::map<std::string, std::map<std::string, LGraph *>> name2lgraph;
  static uint32_t                                               lgraph_counter;

  // singleton object, assumes all graph within a program are in the same
  // directory
  Graph_library *library;
  Tech_library * tlibrary;

  int lgraph_id;

  Index_ID create_node_int() final;

public:
  LGraph() = delete;
  explicit LGraph(const std::string &path);
  explicit LGraph(const std::string &path, const std::string &name, bool clear);

  static LGraph *find_lgraph(const std::string &path, const std::string &name);
  static LGraph *open_lgraph(const std::string &path, const std::string &name);

  ~LGraph() {
    console->debug("lgraph destructor\n");
  }

  int lg_id() const {
    return lgraph_id;
  }
  void clear() override;
  void reload() override;
  void sync() override;
  void emplace_back() override;

  Index_ID add_graph_input(const char *str, Index_ID nid = 0, uint16_t bits = 0, uint16_t offset = 0);
  Index_ID add_graph_output(const char *str, Index_ID nid = 0, uint16_t bits = 0, uint16_t offset = 0);

  Node            create_node();
  Node            get_node(Index_ID nid);
  const ConstNode get_node(Index_ID nid) const;

  ConstNode get_dest_node(const Edge &edge) const;
  Node      get_dest_node(const Edge &edge);

  const Graph_library *get_library() const {
    return library;
  }
  const std::string &get_subgraph_name(Index_ID nid) const;

  const Tech_library *get_tlibrary() const {
    return tlibrary;
  }
  Tech_library *get_tech_library() {
    return tlibrary;
  }

  uint16_t get_offset(Index_ID nid) const override {
    return LGraph_WireNames::get_offset(nid);
  }

  Index_ID get_master_nid(Index_ID idx) const {
    return node_internal[idx].get_master_root_nid();
  }

  Forward_edge_iterator  forward() const;
  Backward_edge_iterator backward() const;

  void dump() const;
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

  Node_Type type_get() const {
    return g->node_type_get(nid);
  }
  float delay_get() const {
    return g->node_delay_get(nid);
  }
  Node_Place place_get() const {
    return g->node_place_get(nid);
  }

  uint16_t get_bits() const {
    return g->get_bits(nid);
  }

  virtual const Edge_iterator inp_edges() const;
  virtual const Edge_iterator out_edges() const;

  bool is_root() const {
    return g->is_root(nid);
  }
  bool is_graph_input() const {
    return g->is_graph_input(nid);
  }
  bool is_graph_output() const {
    return g->is_graph_output(nid);
  }
};

class Node : public ConstNode {
private:
  LGraph *g;

protected:
public:
  Node(LGraph *_g, Index_ID _nid)
      : ConstNode(_g, _nid) {
    g = _g;
  };

  void add_input(Node &src, uint16_t bits, Port_ID inp_pid = 0, Port_ID out_pid = 0) {
    Node_Pin src_pin(src.nid, out_pid, false);
    Node_Pin dst_pin(nid, inp_pid, true);
    g->add_edge(src_pin, dst_pin, bits);
  };

  void set(const Node_Type_Op op) {
    g->node_type_set(nid, op);
  }

  void set_bits(uint16_t bits) {
    g->set_bits(nid, bits);
  }

  void delay_set(float t) {
    g->node_delay_set(nid, t);
  }

  const Edge_iterator inp_edges() const override;
  const Edge_iterator out_edges() const override;
};

#endif

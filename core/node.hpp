//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "node_pin.hpp"

class Edge_raw_iterator;
class LGraph;
class Node_Type;

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
  const Node_Type &get_type() const;
  //float      get_delay() const { return g->node_delay_get(nid); }
  //Node_Place get_place() const { return g->node_place_get(nid); }

  Node_pin get_driver_pin() const;
  Node_pin get_driver_pin(Port_ID pid) const;
  Node_pin get_sink_pin(Port_ID pid) const;
  Node_pin get_sink_pin() const;

  virtual const Edge_raw_iterator inp_edges() const;
  virtual const Edge_raw_iterator out_edges() const;

  bool has_inputs () const;
  bool has_outputs() const;

  bool is_root() const;
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
  void set(const Node_Type_Op op);
#endif

  Node_pin setup_driver_pin(std::string_view name);
  Node_pin setup_driver_pin(Port_ID pid);
  Node_pin setup_driver_pin() const;

  Node_pin setup_sink_pin(std::string_view name);
  Node_pin setup_sink_pin(Port_ID pid);
  Node_pin setup_sink_pin() const;

  const Edge_raw_iterator inp_edges() const override;
  const Edge_raw_iterator out_edges() const override;

};

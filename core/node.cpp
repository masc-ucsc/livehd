//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "annotate.hpp"

Node_pin Node::get_driver_pin() const {
  I(g->get_type(nid).has_single_output());
  return Node_pin(g,hid,nid,0,false);
}

Node_pin Node::get_driver_pin(Port_ID pid) const {
  I(g->get_type(nid).has_output(pid));
  Index_ID idx = g->find_idx_from_pid(nid,pid);
  I(idx);
  return Node_pin(g,hid,idx, pid, false);
}

Node_pin Node::get_sink_pin(Port_ID pid) const {
  I(g->get_type(nid).has_input(pid));
  Index_ID idx = g->find_idx_from_pid(nid,pid);
  I(idx);
  return Node_pin(g,hid,idx,pid,true);
}

Node_pin Node::get_sink_pin() const {
  I(g->get_type(nid).has_single_input());
  return Node_pin(g,hid,nid,0,true);
}

bool Node::has_inputs () const {
  return g->has_node_inputs (nid);
}

bool Node::has_outputs() const {
  return g->has_node_outputs(nid);
}

bool Node::is_root() const {
  return g->is_root(nid);
}

Node_pin Node::setup_driver_pin(Port_ID pid) {
  I(g->get_type(nid).has_output(pid));
  Index_ID idx = g->setup_idx_from_pid(nid,pid);
  return Node_pin(g,hid,idx, pid, false);
}
Node_pin Node::setup_driver_pin() const {
  I(g->get_type(nid).has_single_output());
  return Node_pin(g,hid,nid,0,false);
}

const Node_Type &Node::get_type() const { return g->get_type(nid); }

void Node::set_type(const Node_Type_Op op) {
  g->set_type(nid,op);
}

void Node::set_type_subgraph(Lg_type_id subid) {
  g->set_type_subgraph(nid,subid);
}

Lg_type_id Node::get_type_subgraph() const {
  return g->get_type_subgraph(nid);
}

Node_pin Node::setup_driver_pin(std::string_view name) {
  auto type = get_type();

  auto pid = type.get_output_match(name);
  if (pid != Port_invalid) {
    auto idx = nid;
    if (pid)
      idx = g->setup_idx_from_pid(nid,pid);
    return Node_pin(g,hid,idx,pid,false);
  }

  if (type.op == SubGraph_Op) {
    Lg_type_id id2 = g->get_type_subgraph(nid);
    LGraph *g2 = LGraph::open(g->get_path(), id2);
    I(g2);
    auto internal_pin = g2->get_graph_output(name);
    pid = internal_pin.get_pid();
    Index_ID idx = g->setup_idx_from_pid(nid,pid);
    return Node_pin(g,hid,idx,pid,false);
  }

  I(type.op == TechMap_Op); // TechFile still not handled. Anything else missing?
  I(false); // TechFile still not handled. Anything else missing?

  return Node_pin(g,hid,nid,0,false);
}

Node_pin Node::setup_sink_pin(std::string_view name) {
  auto type = get_type();

  auto pid = type.get_input_match(name);
  if (pid != Port_invalid) {
    auto idx = nid;
    if (pid)
      idx = g->setup_idx_from_pid(nid,pid);
    return Node_pin(g,hid,idx,pid,true);
  }

  if (type.op == SubGraph_Op) {
    Lg_type_id id2 = g->get_type_subgraph(nid);
    LGraph *g2 = LGraph::open(g->get_path(), id2);
    I(g2);
    auto internal_pin = g2->get_graph_input(name);
    pid = internal_pin.get_pid();
    Index_ID idx = g->setup_idx_from_pid(nid,pid);
    return Node_pin(g,hid,idx,pid,true);
  }

  I(type.op == TechMap_Op); // TechFile still not handled. Anything else missing?
  I(false); // TechFile still not handled. Anything else missing?

  return Node_pin(g,hid,nid,0,true);
}

Node_pin Node::setup_sink_pin(Port_ID pid) {
  I(g->get_type(nid).has_input(pid));
  Index_ID idx = g->setup_idx_from_pid(nid,pid);
  return Node_pin(g,hid,idx,pid,true);
}

Node_pin Node::setup_sink_pin() const {
  I(g->get_type(nid).has_single_input());
  return Node_pin(g,hid,nid,0,true);
}

XEdge_iterator Node::inp_edges() const { return g->inp_edges(*this); }
XEdge_iterator Node::out_edges() const { return g->out_edges(*this); }

void Node::del_node() {
  g->del_node(nid);
}

void Node::set_name(std::string_view iname) {
  Ann_node_name::set(*this,iname);
}

std::string_view Node::get_name() const {
  return Ann_node_name::get(*this);
}

bool Node::has_name() const {
  return Ann_node_name::has(*this);
}

const Node_place &Node::get_place() const {
  return Ann_node_place::get(*this);
}

Node_place *Node::ref_place() {
  if (!Ann_node_place::has(*this))
    Ann_node_place::set(*this,Node_place()); // Empty

  return &Ann_node_place::at(*this);
}

bool Node::has_place() const {
  return Ann_node_place::has(*this);
}

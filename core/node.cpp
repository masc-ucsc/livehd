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

void Node::set_type(const Node_Type_Op op, uint16_t bits) {
  g->set_type(nid, op);

  setup_driver_pin().set_bits(bits); // bits only possible when the cell has a single output
}

bool Node::is_type(const Node_Type_Op op) const {
  return g->get_type(nid).op == op;
}

void Node::set_type_subgraph(Lg_type_id subid) {
  g->set_type_subgraph(nid,subid);
}

Lg_type_id Node::get_type_subgraph() const {
  return g->get_type_subgraph(nid);
}

void Node::set_type_tmap_id(uint32_t tmap_id) {
  g->set_type_tmap_id(nid,tmap_id);
}

uint32_t Node::get_type_tmap_id() const {
  return g->get_type_tmap_id(nid);
}

const Tech_cell *Node::get_type_tmap_cell() const {
  return g->get_tlibrary().get_const_cell(g->get_type_tmap_id(nid));
}

/* DEPRECATED
void Node::set_type_const_value(std::string_view str) {
  g->set_type_const_value(nid, str);
}

void Node::set_type_const_sview(std::string_view str) {
  g->set_type_const_sview(nid, str);
}

void Node::set_type_const_value(uint32_t val) {
  g->set_type_const_value(nid, val);
}
*/

uint32_t Node::get_type_const_value() const {
  return g->get_type_const_value(nid);
}

std::string_view Node::get_type_const_sview() const {
  return g->get_type_const_sview(nid);
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
Node_pin_iterator Node::out_connected_pins() const { return g->out_connected_pins(*this); }

void Node::del_node() {
  g->del_node(nid);
}

std::string_view Node::set_name(std::string_view iname) {
  return Ann_node_name::set(*this,iname);
}

std::string_view Node::create_name() const {
  if (Ann_node_name::has(*this))
    return Ann_node_name::get(*this);

  std::string signature = absl::StrCat("lg_", get_type().get_name(), std::to_string(nid));
  return Ann_node_name::set(*this, signature);
  // FIXME: HERE. Does not scale for large designs (too much recursion)

  if (get_type().op == SubGraph_Op) {
    absl::StrAppend(&signature, "subid_", get_type_subgraph().value);
  }else if (get_type().op == TechMap_Op) {
    const Tech_cell *tcell = get_type_tmap_cell();

    if (tcell) {
      for(const auto &e:inp_edges()) {
        if(!e.driver.has_name() && e.driver.get_pid() < tcell->n_outs()) {
          absl::StrAppend(&signature, "_", tcell->get_name(e.driver.get_pid()));
        }
      }
    }
  }

  auto nod = Ann_node_name::find(g, signature);
  if (nod.is_invalid()) {
    return Ann_node_name::set(*this, signature);
  }

  for(const auto &dpin:out_connected_pins()) {
    absl::StrAppend(&signature,"_",dpin.create_name());
  }

  auto nod2 = Ann_node_name::find(g, signature);
  if (nod2.is_invalid()) {
    return Ann_node_name::set(*this, signature);
  }

  absl::StrAppend(&signature,"_nid", std::to_string(nid)); // OK, add to stop trying

  I(Ann_node_name::find(g, signature).is_invalid());

  return Ann_node_name::set(*this, signature);
}

std::string_view Node::get_name() const {
  return Ann_node_name::get(*this);
}

std::string Node::debug_name() const {
#ifdef NDEBUG
  static int conta = 0;
  if (conta<10) {
    conta++;
    fmt::print("WARNING: Node::debug_name should not be called during release (Slowww!)\n");
  }
#endif
  std::string name;
  if (Ann_node_name::has(*this))
    name = Ann_node_name::get(*this);

  return absl::StrCat("node_", name, std::to_string(nid));
  //not a acceptable format for dot
  //return absl::StrCat("node_", std::to_string(nid), "(", name ,")");
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

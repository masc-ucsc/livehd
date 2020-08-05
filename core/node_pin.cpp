//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "node_pin.hpp"

#include "annotate.hpp"
#include "lgraph.hpp"

Node_pin::Node_pin(LGraph *_g, LGraph *_c_g, const Hierarchy_index &_hidx, Index_ID _idx, Port_ID _pid, bool _sink)
    : top_g(_g), current_g(_c_g), hidx(_hidx), idx(_idx), pid(_pid), sink(_sink) {
  I(_g);
  I(_idx);
}

Node_pin::Node_pin(LGraph *_g, Compact comp)
    : top_g(_g), hidx(comp.hidx), idx(comp.idx), pid(_g->get_dst_pid(comp.idx)), sink(comp.sink) {
  I(!comp.hidx.is_invalid()); // Why to Compact. Use Compact_class
  current_g = top_g->ref_htree()->ref_lgraph(hidx);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(LGraph *_g, Compact_driver comp)
    : top_g(_g), hidx(comp.hidx), idx(comp.idx), pid(_g->get_dst_pid(comp.idx)), sink(true) {
  I(!hidx.is_invalid());
  current_g = top_g->ref_htree()->ref_lgraph(hidx);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(LGraph *_g, Compact_class comp)
    : top_g(_g), hidx(Hierarchy_tree::invalid_index()), idx(comp.idx), pid(_g->get_dst_pid(comp.idx)), sink(comp.sink) {
  current_g = top_g;  // top_g->ref_htree()->ref_lgraph(hid);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(LGraph *_g, Compact_class_driver comp)
    : top_g(_g), hidx(Hierarchy_tree::invalid_index()), idx(comp.idx), pid(_g->get_dst_pid(comp.idx)), sink(false) {
  current_g = top_g;  // top_g->ref_htree()->ref_lgraph(hid);
  I(current_g->is_valid_node_pin(idx));
}


Node_pin::Compact Node_pin::get_compact() const {
  if(hidx.is_invalid())
    return Compact(Hierarchy_tree::root_index(), idx, sink);
  return Compact(hidx, idx, sink);
}

Node_pin::Compact_driver Node_pin::get_compact_driver() const {
  I(!sink);
  if(hidx.is_invalid())
    return Compact_driver(Hierarchy_tree::root_index(), idx);
  return Compact_driver(hidx, idx);
}

bool Node_pin::has_inputs() const { return current_g->has_inputs(*this); }

bool Node_pin::has_outputs() const { return current_g->has_outputs(*this); }

bool Node_pin::is_graph_io() const { return current_g->is_graph_io(idx); }

bool Node_pin::is_graph_input() const { return current_g->is_graph_input(idx); }

bool Node_pin::is_graph_output() const { return current_g->is_graph_output(idx); }

Node_pin Node_pin::get_sink_from_output() const {
  I(is_graph_output());
  if(is_sink())
    return *this;

  return Node_pin(top_g, current_g, hidx, idx, pid, true);
}

Node_pin Node_pin::get_driver_from_output() const {
  I(is_graph_output());
  if (is_driver())
    return *this;

  return Node_pin(top_g, current_g, hidx, idx, pid, false);
}

Node Node_pin::get_node() const {
  auto nid = current_g->get_node_nid(idx);

  return Node(top_g, current_g, hidx, nid);
}

Node Node_pin::get_driver_node() const { return get_driver_pin().get_node(); }

Node_pin Node_pin::get_driver_pin() const {
  I(is_sink() || is_graph_output());
  auto piter = current_g->inp_driver(*this);
  if (piter.empty())
    return Node_pin(); // disconnected driver
  I(piter.size()==1); // If there can be many drivers, use the inp_driver iterator
  return piter.back();
}

Node_pin_iterator Node_pin::inp_driver() const {
  I(is_sink() || is_graph_output());
  return current_g->inp_driver(*this);
}

void Node_pin::del_sink(Node_pin &spin) {
  I(spin.is_sink());
  I(is_driver());
  I(current_g == spin.current_g);  // Use punch otherwise
  current_g->del_edge(*this, spin);
}

void Node_pin::del_driver(Node_pin &dpin) {
  I(dpin.is_driver());
  I(is_sink());
  I(current_g == dpin.current_g);
  current_g->del_edge(dpin, *this);
}

void Node_pin::connect_sink(Node_pin &spin) {
  I(spin.is_sink());
  I(is_driver());
  I(current_g == spin.current_g);  // Use punch otherwise
  current_g->add_edge(*this, spin);
}

void Node_pin::connect_driver(Node_pin &dpin) {
  I(dpin.is_driver());
  I(is_sink());
  I(current_g == dpin.current_g);
  current_g->add_edge(dpin, *this);
}

int Node_pin::get_num_edges() const {
  if (is_sink())
    return current_g->get_node_pin_num_inputs(idx);

  return current_g->get_node_pin_num_outputs(idx);
}

uint32_t Node_pin::get_bits() const {
  I(is_driver());
  return current_g->get_bits(idx);
}

void Node_pin::set_bits(uint32_t bits) {
  I(is_driver());
  current_g->set_bits(idx, bits);
}

bool Node_pin::is_signed() const {
  I(is_driver());
  return current_g->is_signed(idx);
}
bool Node_pin::is_unsigned() const {
  I(is_driver());
  return current_g->is_unsigned(idx);
}

void Node_pin::set_signed() {
  I(is_driver());
  current_g->set_signed(idx);
}

void Node_pin::set_unsigned() {
  I(is_driver());
  current_g->set_unsigned(idx);
}

std::string_view Node_pin::get_type_sub_io_name() const {
  auto &sub_node = get_node().get_type_sub_node();
  return sub_node.get_name_from_graph_pos(pid);
}

std::string_view Node_pin::get_type_sub_pin_name() const {
  auto node = get_node();
  I(node.is_type_sub());

  return node.get_type_sub_node().get_name_from_graph_pos(pid);
}

float Node_pin::get_delay() const { return Ann_node_pin_delay::ref(top_g)->get(get_compact_driver()); }

void Node_pin::set_delay(float val) { Ann_node_pin_delay::ref(top_g)->set(get_compact_driver(), val); }

void Node_pin::set_name(std::string_view wname) { Ann_node_pin_name::ref(current_g)->set(get_compact_class_driver(), wname); }

// FIXME->sh: could be deprecated if ann_ssa could be mmapped for a std::string_view
void Node_pin::set_prp_vname(std::string_view prp_vname) {
  Ann_node_pin_prp_vname::ref(current_g)->set(get_compact_class_driver(), prp_vname);
}

void Node_pin::dump_all_prp_vname() const {
  auto *ref = Ann_node_pin_prp_vname::ref(current_g);

  for (auto it : *ref) {
    if (current_g->is_valid_node_pin(it.first.idx)) {
      Node_pin a(current_g, it.first);
      fmt::print("prp_vname pin:{} vname:{}\n", a.debug_name(), ref->get_sview(it.second));
    }
  }
}

void Node_pin::nuke() {
  I(false);  // TODO:
}

std::string Node_pin::debug_name() const {
#ifndef NDEBUG
  static uint16_t conta = 8192;
  if (conta++ == 0) {
    fmt::print("WARNING: Node_pin::debug_name should not be called during release (Slowww!)\n");
  }
#endif
  if (idx == 0) {  // legal for invalid node/pins
    return "invalid_pin";
  }
  I(current_g);

  std::string name;
  if (!sink)
    if (Ann_node_pin_name::ref(current_g)->has_key(get_compact_class_driver()))
      name = Ann_node_pin_name::ref(current_g)->get_val(get_compact_class_driver());

  if (name.empty()) {
    const auto node = get_node();
    if (node.is_type_sub()) {
      name = node.get_type_sub_node().get_name_from_graph_pos(pid);
    } else if (node.has_name()) {
      name = node.get_name();
    }
  }

  return absl::StrCat("node_pin_",
                      "n",
                      std::to_string(get_node().nid),
                      "_",
                      name,
                      "_",
                      sink ? "s" : "d",
                      std::to_string(pid),
                      "_lg_",
                      current_g->get_name());
}

std::string_view Node_pin::get_name() const {
#ifndef NDEBUG
  if (!is_graph_io()) {
    I(is_driver());
    I(has_name());  // get_name should be called for named driver_pins
  }
#endif
  // NOTE: Not the usual get_compact_class_driver() to handle IO change from driver/sink
  return Ann_node_pin_name::ref(current_g)->get_val(Compact_class_driver(idx));
}

std::string_view Node_pin::get_prp_vname() const {
#ifndef NDEBUG
  if (!is_graph_io()) {
    I(is_driver());
    I(has_prp_vname());  // get_name should be called for named driver_pins
  }
#endif
  // NOTE: Not the usual get_compact_class_driver() to handle IO change from driver/sink
  return Ann_node_pin_prp_vname::ref(current_g)->get(Compact_class_driver(idx));
}

std::string_view Node_pin::create_name() const {
  auto ref = Ann_node_pin_name::ref(current_g);

  if (ref->has_key(get_compact_class_driver()))
    return ref->get_val(get_compact_class_driver());

  std::string signature(get_node().create_name());

  if (is_driver()) {
    for (auto &e : inp_edges()) {
      absl::StrAppend(&signature, "_p", std::to_string(e.driver.get_pid()), "_", e.driver.create_name());
    }
  }

  auto found = ref->has_val(signature);
  if (!found) {
    absl::StrAppend(&signature, "_nid", std::to_string(get_node().get_nid()));
  }

  const auto it = ref->set(get_compact_class_driver(), signature);
  return ref->get_val(it);
}

bool Node_pin::has_name() const { return Ann_node_pin_name::ref(current_g)->has_key(get_compact_class_driver()); }

bool Node_pin::has_prp_vname() const { return Ann_node_pin_prp_vname::ref(current_g)->has(get_compact_class_driver()); }

Node_pin Node_pin::find_driver_pin(LGraph *top, std::string_view wname) {
  auto       ref = Ann_node_pin_name::ref(top);
  const auto it  = ref->find_val(wname);
  if (it == ref->end()) {
    return Node_pin();
  }

  return Node_pin(top, ref->get_key(it));
}

std::string_view Node_pin::get_pin_name() const {
  if (get_node().is_type_sub())
    return get_type_sub_io_name();
  if (is_driver())
    return get_node().get_type().get_output_match(pid);
  else
    return get_node().get_type().get_input_match(pid);
}

void Node_pin::set_offset(Bits_t offset) {
  if (offset == 0)
    return;

  Ann_node_pin_offset::ref(current_g)->set(get_compact_class_driver(), offset);
}

Bits_t Node_pin::get_offset() const {
  auto ref = Ann_node_pin_offset::ref(current_g);
  if (!ref->has(get_compact_class_driver()))
    return 0;

  auto off = ref->get(get_compact_class_driver());
  I(off);
  return off;
}

const Ann_ssa &Node_pin::get_ssa() const {
  const auto *data = Ann_node_pin_ssa::ref(top_g)->ref(get_compact_class_driver());
  I(data);
  return *data;
}

Ann_ssa *Node_pin::ref_ssa() {
  auto *ref = Ann_node_pin_ssa::ref(top_g);

  auto it = ref->find(get_compact_class_driver());
  if (it != ref->end()) {
    return ref->ref(it);
  }

  auto it2 = ref->set(get_compact_class_driver(), Ann_ssa());  // Empty
  return ref->ref(it2);
}

bool Node_pin::has_ssa() const { return Ann_node_pin_ssa::ref(top_g)->has(get_compact_class_driver()); }

bool Node_pin::is_connected() const {
  if (is_invalid())
    return false;

  if (is_driver())
    return current_g->has_outputs(*this);

  return current_g->has_inputs(*this);
}

bool Node_pin::is_connected(const Node_pin &pin) const {
  if (pin.is_driver()) {
    for (auto &other : inp_driver()) {
      if (other == pin)
        return true;
    }
    return false;
  }
  if (likely(is_driver())) { // sink can not be connected to another sink
    for (auto &other : pin.inp_driver()) {
      if (other == *this)
        return true;
    }
  }

  return false;
}

Node_pin Node_pin::get_down_pin() const {
  auto node = get_node();
  I(node.is_type_sub_present());
  I(!top_g->ref_htree()->is_leaf(hidx));

  // 1st: Get down_hidx
  const auto *tree_pos = Ann_node_tree_pos::ref(current_g);
  I(tree_pos);
  I(tree_pos->has(node.get_compact_class()));
  auto pos = tree_pos->get(node.get_compact_class());

  Hierarchy_index down_hidx(hidx.level + 1, pos);

  // 2nd: get down_pid
  I(pid != Port_invalid);
  auto down_pid = node.get_type_sub_node().get_instance_pid_from_graph_pos(pid);
  I(down_pid != Port_invalid);

  // 3rd: get down_current_g
  auto *down_current_g = top_g->ref_htree()->ref_lgraph(down_hidx);

  // 4th: get down_idx
  Index_ID down_idx
      = down_current_g->find_idx_from_pid(is_driver() ? Node::Hardcoded_output_nid : Node::Hardcoded_input_nid, down_pid);
  I(down_idx);

  bool down_sink = is_driver();  // top driver goes to an down output which should be a sink
  return Node_pin(top_g, down_current_g, down_hidx, down_idx, down_pid, down_sink);
}

Node_pin Node_pin::get_up_pin() const {
  I(get_node().is_graph_io());

  bool up_sink = get_node().is_graph_input();  // down input, must be a sink on instance

  // 1st: get up_pid
  I(pid != Port_invalid);
  const auto &io_pin = current_g->get_self_sub_node().get_io_pin_from_instance_pid(pid);

  if (io_pin.is_input() != up_sink) {
    // IO port is not found (different direction is there)
    return Node_pin();  // Invalid, the input is not connected
  }

  auto up_pid = io_pin.get_graph_pos();
  I(up_pid != Port_invalid);

  // 2nd: get up_current_g
  auto up_node = top_g->ref_htree()->get_instance_up_node(hidx);

  // 3rd: get up_idx
  Index_ID up_idx = up_node.get_class_lgraph()->find_idx_from_pid(up_node.get_nid(), up_pid);
  if (up_idx.is_invalid())
    return Node_pin();  // Invalid, the input is not connected

  I(up_idx);

  return Node_pin(top_g, up_node.get_class_lgraph(), up_node.get_hidx(), up_idx, up_pid, up_sink);
}

XEdge_iterator Node_pin::inp_edges() const { return current_g->inp_edges(*this); }

XEdge_iterator Node_pin::out_edges() const { return current_g->out_edges(*this); }

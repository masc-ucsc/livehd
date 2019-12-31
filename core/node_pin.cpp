//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "node_pin.hpp"

#include "annotate.hpp"
#include "lgraph.hpp"

Node_pin::Node_pin(LGraph *_g, LGraph *_c_g, const Hierarchy_index &_hidx, Index_ID _idx, Port_ID _pid, bool _sink)
    : top_g(_g), current_g(_c_g), hidx(_hidx), idx(_idx), pid(_pid), sink(_sink) {
  I(current_g->is_valid_node_pin(idx));
  I(_g);
  I(_idx);
}

Node_pin::Node_pin(LGraph *_g, Compact comp)
    : top_g(_g), hidx(comp.hidx), idx(comp.idx), pid(_g->get_dst_pid(comp.idx)), sink(comp.sink) {
  current_g = top_g->ref_htree()->ref_lgraph(hidx);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(LGraph *_g, Compact_driver comp)
    : top_g(_g), hidx(comp.hidx), idx(comp.idx), pid(_g->get_dst_pid(comp.idx)), sink(true) {
  I(!hidx.is_invalid());
  current_g = top_g->ref_htree()->ref_lgraph(hidx);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(LGraph *_g, Compact_class_driver comp)
    : top_g(_g), hidx(Hierarchy_tree::root_index()), idx(comp.idx), pid(_g->get_dst_pid(comp.idx)), sink(false) {
  current_g = top_g;  // top_g->ref_htree()->ref_lgraph(hid);
  I(current_g->is_valid_node_pin(idx));
}

bool Node_pin::has_inputs() const { return current_g->has_inputs(*this); }

bool Node_pin::has_outputs() const { return current_g->has_outputs(*this); }

bool Node_pin::is_graph_io() const { return current_g->is_graph_io(idx); }

bool Node_pin::is_graph_input() const { return current_g->is_graph_input(idx); }

bool Node_pin::is_graph_output() const { return current_g->is_graph_output(idx); }

Node Node_pin::get_node() const {
  auto nid = current_g->get_node_nid(idx);

  return Node(top_g, current_g, hidx, nid);
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

uint32_t Node_pin::get_bits() const { return current_g->get_bits(idx); }

void Node_pin::set_bits(uint32_t bits) {
  I(is_driver());
  current_g->set_bits(idx, bits);
}

std::string_view Node_pin::get_type_sub_io_name() const {
  auto &sub_node = get_node().get_type_sub_node();
  return sub_node.get_name_from_instance_pid(pid);
}

std::string_view Node_pin::get_type_sub_pin_name() const {
  auto node = get_node();
  I(node.is_type_sub());

  return node.get_type_sub_node().get_name_from_graph_pos(pid);
}

float Node_pin::get_delay() const { return Ann_node_pin_delay::ref(top_g)->get(get_compact_driver()); }

void Node_pin::set_delay(float val) { Ann_node_pin_delay::ref(top_g)->set(get_compact_driver(), val); }

void Node_pin::set_name(std::string_view wname) { Ann_node_pin_name::ref(current_g)->set(get_compact_class_driver(), wname); }

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
  std::string name;
  if (!sink)
    if (Ann_node_pin_name::ref(current_g)->has_key(get_compact_class_driver()))
      name = Ann_node_pin_name::ref(current_g)->get_val_sview(get_compact_class_driver());

  if (name.empty()) {
    const auto node = get_node();
    if (node.is_type_sub()) {
      name = node.get_type_sub_node().get_name_from_graph_pos(pid);
    } else if (node.has_name()) {
      name = node.get_name();
    }
  }

  return absl::StrCat("node_pin_", std::to_string(get_node().nid), "_", name, "_", std::to_string(pid), sink ? "s" : "d", "_lg_",
                      current_g->get_name());
}

std::string_view Node_pin::get_name() const {
  I(has_name());  // get_name should be called for named driver_pins
  return Ann_node_pin_name::ref(current_g)->get_val_sview(get_compact_class_driver());
}

std::string_view Node_pin::create_name() const {
  auto ref = Ann_node_pin_name::ref(current_g);

  if (ref->has_key(get_compact_class_driver())) return ref->get_val_sview(get_compact_class_driver());

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
  return ref->get_val_sview(it);
}

bool Node_pin::has_name() const { return Ann_node_pin_name::ref(current_g)->has_key(get_compact_class_driver()); }

void Node_pin::set_offset(uint16_t offset) {
  if (offset == 0) return;

  Ann_node_pin_offset::ref(current_g)->set(get_compact_class_driver(), offset);
}

uint16_t Node_pin::get_offset() const {
  auto ref = Ann_node_pin_offset::ref(current_g);
  if (!ref->has(get_compact_class_driver())) return 0;

  auto off = ref->get(get_compact_class_driver());
  I(off);
  return off;
}

const Ann_bitwidth &Node_pin::get_bitwidth() const {
  const auto *data = Ann_node_pin_bitwidth::ref(top_g)->ref(get_compact_driver());
  I(data);
  return *data;
}

Ann_bitwidth *Node_pin::ref_bitwidth() {
  auto *ref = Ann_node_pin_bitwidth::ref(top_g);

  auto it = ref->find(get_compact_driver());
  if (it != ref->end()) {
    return ref->ref(it);
  }

  auto it2 = ref->set(get_compact_driver(), Ann_bitwidth());  // Empty
  return ref->ref(it2);
}

bool Node_pin::has_bitwidth() const { return Ann_node_pin_bitwidth::ref(top_g)->has(get_compact_driver()); }

bool Node_pin::is_connected() const {
  if (is_driver()) return current_g->has_outputs(*this);

  return current_g->has_inputs(*this);
}

Node_pin Node_pin::get_down_pin() const {
  auto node = get_node();
  I(node.is_type_sub_present());
  I(!top_g->ref_htree()->is_leaf(hidx));

  // 1st: Get down_hidx
  auto *tree_pos = Ann_node_tree_pos::ref(current_g);
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
  Index_ID down_idx =
      down_current_g->find_idx_from_pid(is_driver() ? Node::Hardcoded_output_nid : Node::Hardcoded_input_nid, down_pid);
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
  if (up_idx.is_invalid()) return Node_pin();  // Invalid, the input is not connected

  I(up_idx);

  return Node_pin(top_g, up_node.get_class_lgraph(), up_node.get_hidx(), up_idx, up_pid, up_sink);
}

XEdge_iterator Node_pin::inp_edges() const { return current_g->inp_edges(*this); }

XEdge_iterator Node_pin::out_edges() const { return current_g->out_edges(*this); }

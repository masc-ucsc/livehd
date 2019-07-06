//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph.hpp"

#include "node_pin.hpp"

#include "annotate.hpp"

Node_pin::Node_pin(LGraph *_g, LGraph *_c_g, const Hierarchy_index &_hidx, Index_ID _idx, Port_ID _pid, bool _sink)
  :top_g(_g)
  ,current_g(_c_g)
  ,hidx(_hidx)
  ,idx(_idx)
  ,pid(_pid)
  ,sink(_sink) {

  I(current_g->is_valid_node_pin(idx));
  I(_g);
  I(_idx);
}

Node_pin::Node_pin(LGraph *_g, Compact comp)
  :top_g(_g)
  ,hidx(comp.hidx)
  ,idx(comp.idx)
  ,pid(_g->get_dst_pid(comp.idx))
  ,sink(comp.sink) {
  current_g = top_g->find_sub_lgraph(hidx);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(LGraph *_g, Compact_driver comp)
  :top_g(_g)
  ,hidx(comp.hidx)
  ,idx(comp.idx)
  ,pid(_g->get_dst_pid(comp.idx))
  ,sink(true) {

  I(hidx);
  current_g = top_g->find_sub_lgraph(hidx);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(LGraph *_g, Compact_class_driver comp)
  :top_g(_g)
  ,hidx(_g->hierarchy_root())
  ,idx(comp.idx)
  ,pid(_g->get_dst_pid(comp.idx))
  ,sink(false) {

  current_g = top_g; // top_g->find_sub_lgraph(hid);
  I(current_g->is_valid_node_pin(idx));
}

bool Node_pin::is_graph_io() const {
  return current_g->is_graph_io(idx);
}

bool Node_pin::is_graph_input() const {
  return current_g->is_graph_input(idx);
}

bool Node_pin::is_graph_output() const {
  return current_g->is_graph_output(idx);
}

Node Node_pin::get_node() const {
  auto nid = current_g->get_node_nid(idx);

  return Node(top_g, current_g, hidx, nid);
}

void Node_pin::connect_sink(Node_pin &spin) {
  I(spin.is_sink());
  I(is_driver());
  I(current_g == spin.current_g); // Use punch otherwise
  current_g->add_edge(*this,spin);
}

void Node_pin::connect_driver(Node_pin &dpin) {
  I(dpin.is_driver());
  I(is_sink());
  I(current_g == dpin.current_g);
  current_g->add_edge(dpin, *this);
}

uint16_t Node_pin::get_bits() const {
  return current_g->get_bits(idx);
}

void Node_pin::set_bits(uint16_t bits) {
  I(is_driver());
  current_g->set_bits(idx, bits);
}

std::string_view Node_pin::get_type_sub_io_name() const {
  auto &sub_node = get_node().get_type_sub_node();
  return sub_node.get_name_from_instance_pid(pid);
}

float Node_pin::get_delay() const {
  return Ann_node_pin_delay::ref(top_g)->get(get_compact_driver());
}

void Node_pin::set_delay(float val) {
  Ann_node_pin_delay::ref(top_g)->set(get_compact_driver(), val);
}

void Node_pin::set_name(std::string_view wname) {
  Ann_node_pin_name::ref(current_g)->set(get_compact_class_driver(), wname);
}

void Node_pin::nuke() {
  I(false); // TODO:
}

std::string Node_pin::debug_name() const {
#ifndef NDEBUG
  static uint16_t conta = 8192;
  if (conta++==0) {
    fmt::print("WARNING: Node_pin::debug_name should not be called during release (Slowww!)\n");
  }
#endif
  std::string name;
  if (!sink)
    if (Ann_node_pin_name::ref(current_g)->has_key(get_compact_class_driver()))
      name = Ann_node_pin_name::ref(current_g)->get_val(get_compact_class_driver());

  return absl::StrCat("node_pin_", std::to_string(get_node().nid), "_", name, ":", std::to_string(pid), sink?"s":"d");
  //not a acceptable format for dot
  //return absl::StrCat("node_pin_", std::to_string(idx), ":", std::to_string(pid), sink?"s":"d", "(", name ,")");
}

std::string_view Node_pin::get_name() const {
  return Ann_node_pin_name::ref(current_g)->get_val(get_compact_class_driver());
}

std::string_view Node_pin::create_name() const {
  auto ref = Ann_node_pin_name::ref(current_g);

  if (ref->has_key(get_compact_class_driver()))
    return ref->get_val(get_compact_class_driver());

  std::string signature(get_node().create_name());

  if (is_driver()) {
    for(auto &e:get_node().inp_edges()) {
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

bool Node_pin::has_name() const {
  return Ann_node_pin_name::ref(current_g)->has_key(get_compact_class_driver());
}

void Node_pin::set_offset(uint16_t offset) {
	if (offset==0)
    return;

  Ann_node_pin_offset::ref(current_g)->set(get_compact_class_driver(), offset);
}

uint16_t Node_pin::get_offset() const {
  auto ref = Ann_node_pin_offset::ref(current_g);
  if (!ref->has(get_compact_class_driver()))
    return 0;

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

  auto it2 = ref->set(get_compact_driver(),Ann_bitwidth()); // Empty
  return ref->ref(it2);
}

bool Node_pin::has_bitwidth() const {
  return Ann_node_pin_bitwidth::ref(top_g)->has(get_compact_driver());
}


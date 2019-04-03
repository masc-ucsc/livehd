//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph.hpp"

#include "node_pin.hpp"

#include "annotate.hpp"

Node_pin::Node_pin(LGraph *_g, Hierarchy_id _hid, Compact comp)
  :idx(comp.idx)
  ,pid(_g->get_dst_pid(comp.idx))
  ,g(_g)
  ,hid(_hid)
  ,sink(comp.sink) {
}

Node_pin::Node_pin(LGraph *_g, Hierarchy_id _hid, Compact comp, Node_pin_mode mode)
  :idx(comp.idx)
  ,pid(_g->get_dst_pid(comp.idx))
  ,g(_g)
  ,hid(_hid)
  ,sink(mode==Node_pin_mode::Both?comp.sink:(mode==Node_pin_mode::Sink)) {
}

bool Node_pin::is_graph_io() const {
  return g->is_graph_io(idx);
}

bool Node_pin::is_graph_input() const {
  return g->is_graph_input(idx);
}

bool Node_pin::is_graph_output() const {
  return g->is_graph_output(idx);
}

Node Node_pin::get_node() const {
  I(hid==0);
  return g->get_node(idx);
}

void Node_pin::connect_sink(Node_pin &spin) {
  I(g == spin.g);
  g->add_edge(*this,spin);
}

void Node_pin::connect_driver(Node_pin &dpin) {
  I(g == dpin.g);
  g->add_edge(dpin, *this);
}

uint16_t Node_pin::get_bits() const {
  I(hid==0);
  return g->get_bits(idx);
}

void Node_pin::set_bits(uint16_t bits) {
  I(hid==0);
  I(is_driver());
  g->set_bits(idx, bits);
}

std::string_view Node_pin::get_type_subgraph_io_name() const {
  auto sub_id = get_node().get_type_subgraph();
  LGraph *subgraph = LGraph::open(g->get_path(), sub_id);
  I(subgraph);
  return subgraph->get_graph_output_name_from_pid(pid);
}

std::string_view Node_pin::get_type_tmap_io_name() const {
  const Tech_cell *tcell = get_node().get_type_tmap_cell();
  if (sink)
    return tcell->get_input_name(pid);

  return tcell->get_output_name(pid);
}

std::string_view Node_pin::set_name(std::string_view wname) {
  return Ann_node_pin_name::set(*this, wname);
}

std::string_view Node_pin::get_name() const {
  return Ann_node_pin_name::get(*this);
}

std::string_view Node_pin::create_name() const {
  if (Ann_node_pin_name::has(*this))
    return Ann_node_pin_name::get(*this);

  std::string signature(get_node().create_name());

  if (is_driver()) {
    for(auto &e:get_node().inp_edges()) {
      absl::StrAppend(&signature, "_p", std::to_string(e.driver.get_pid()), "_", e.driver.create_name());
    }
  }

  auto found = Ann_node_pin_name::find(g, signature);
  if (!found.is_invalid()) {
    static std::vector<int> last_used;
    last_used.resize(g->get_lgid().value+1);
    while(true) {
      int t = last_used[g->get_lgid().value]++;

      auto tmp = absl::StrCat(signature, "_t", t);

      auto found = Ann_node_pin_name::find(g, tmp);
      if (found.is_invalid()) {
        signature = tmp;
        break;
      }
    }
  }

  return Ann_node_pin_name::set(*this, signature);
}

bool Node_pin::has_name() const {
  return Ann_node_pin_name::has(*this);
}

void Node_pin::set_offset(uint16_t offset) {
	if (offset)
		Ann_node_pin_offset::set(*this, offset);
}

uint16_t Node_pin::get_offset() const {
	if (!Ann_node_pin_offset::has(*this))
			return 0;
	auto off = Ann_node_pin_offset::get(*this);
	I(off);
	return off;
}


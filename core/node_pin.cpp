//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "node_pin.hpp"

#include "annotate.hpp"
#include "lgraph.hpp"
#include "lgtuple.hpp"
#include "node.hpp"

Node_pin::Node_pin(Lgraph *_g, const Compact &comp) : top_g(_g), hidx(comp.hidx), idx(comp.idx), sink(comp.sink) {
  I(!Hierarchy::is_invalid(comp.hidx));  // Why to Compact. Use Compact_class
  current_g = top_g->ref_htree()->ref_lgraph(hidx);
  pid       = current_g->get_dst_pid(idx);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(const mmap_lib::str &path, const Compact_flat &comp) {
  current_g = Lgraph::open(path, Lg_type_id(comp.lgid));
  top_g     = current_g;

  hidx = Hierarchy::non_hierarchical();
  idx  = comp.idx;
  pid  = top_g->get_dst_pid(comp.idx);
  sink = comp.sink;

  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(Lgraph *_g, const Compact_driver &comp) : top_g(_g), hidx(comp.hidx), idx(comp.idx), sink(true) {
  I(!Hierarchy::is_invalid(hidx));
  current_g = top_g->ref_htree()->ref_lgraph(hidx);
  pid       = current_g->get_dst_pid(idx);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(Lgraph *_g, const Hierarchy_index &_hidx, const Compact_class &comp)
    : top_g(_g), hidx(_hidx), idx(comp.idx), sink(comp.sink) {
  I(!Hierarchy::is_invalid(hidx));
  current_g = top_g->ref_htree()->ref_lgraph(hidx);
  pid       = current_g->get_dst_pid(idx);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(Lgraph *_g, const Compact_class &comp)
    : top_g(_g), hidx(Hierarchy::non_hierarchical()), idx(comp.idx), pid(_g->get_dst_pid(comp.idx)), sink(comp.sink) {
  current_g = top_g;  // top_g->ref_htree()->ref_lgraph(hid);
  I(current_g->is_valid_node_pin(idx));
}

Node_pin::Node_pin(Lgraph *_g, const Compact_class_driver &comp)
    : top_g(_g), hidx(Hierarchy::non_hierarchical()), idx(comp.idx), pid(_g->get_dst_pid(comp.idx)), sink(false) {
  current_g = top_g;  // top_g->ref_htree()->ref_lgraph(hid);
  I(current_g->is_valid_node_pin(get_root_idx()));
}

const Index_id Node_pin::get_root_idx() const {
  if (unlikely(current_g == nullptr))
    return 0;
  return current_g->get_root_idx(idx);
}

Node_pin::Compact_flat Node_pin::get_compact_flat() const {
  I(!is_invalid());
  return Compact_flat(current_g->get_lgid(), get_root_idx(), sink);
}

Node_pin::Compact_driver Node_pin::get_compact_driver() const {
  I(!sink);
  if (Hierarchy::is_invalid(hidx))
    return Compact_driver(Hierarchy::hierarchical_root(), get_root_idx());
  return Compact_driver(hidx, get_root_idx());
}

bool Node_pin::has_inputs() const { return current_g->has_inputs(*this); }

bool Node_pin::has_outputs() const { return current_g->has_outputs(*this); }

bool Node_pin::is_graph_io() const { return current_g->has_graph_io(idx); }

bool Node_pin::is_graph_input() const { return current_g->has_graph_input(idx); }

bool Node_pin::is_graph_output() const { return current_g->has_graph_output(idx); }

bool Node_pin::is_type_const() const {
  auto nid = current_g->get_node_nid(idx);
  return current_g->is_type_const(nid);
}

bool Node_pin::is_type_tup() const {
  auto nid = current_g->get_node_nid(idx);
  auto op  = current_g->get_type_op(nid);
  return op == Ntype_op::TupAdd || op == Ntype_op::TupGet;
}

bool Node_pin::is_type_flop() const {
  auto nid = current_g->get_node_nid(idx);
  auto op  = current_g->get_type_op(nid);
  return op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Fflop;
}

bool Node_pin::is_type_register() const {
  auto nid = current_g->get_node_nid(idx);
  auto op  = current_g->get_type_op(nid);

  return op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Memory || op == Ntype_op::Latch;
}

bool Node_pin::is_type(const Ntype_op op) const {
  auto nid = current_g->get_node_nid(idx);
  return op == current_g->get_type_op(nid);
}

Lconst Node_pin::get_type_const() const {
  auto nid = current_g->get_node_nid(idx);
  return current_g->get_type_const(nid);
}

Node_pin Node_pin::get_non_hierarchical() const {
  I(is_hierarchical());
  return Node_pin(current_g, current_g, Hierarchy::non_hierarchical(), idx, pid, sink);
}

Node_pin Node_pin::get_hierarchical() const {
  I(!is_hierarchical());
  return Node_pin(current_g, current_g, Hierarchy::hierarchical_root(), idx, pid, sink);
}

Node_pin Node_pin::switch_to_sink() const {
  if (is_sink())
    return *this;

  return Node_pin(top_g, current_g, hidx, idx, pid, true);
}

Node_pin Node_pin::switch_to_driver() const {
  if (is_driver())
    return *this;

  return Node_pin(top_g, current_g, hidx, idx, pid, false);
}

Node Node_pin::get_node() const {
  auto nid = current_g->get_node_nid(idx);

  return Node(top_g, current_g, hidx, nid);
}

Index_id Node_pin::get_node_nid() const { return current_g->get_node_nid(idx); }

Ntype_op Node_pin::get_type_op() const {
  auto nid = current_g->get_node_nid(idx);
  return current_g->get_type_op(nid);
}

Node Node_pin::get_driver_node() const { return get_driver_pin().get_node(); }

Node_pin Node_pin::get_driver_pin() const {
  if (is_invalid())
    return *this;
  I(is_sink() || is_graph_output());
  auto piter = current_g->inp_drivers(*this);
  if (piter.empty())
    return Node_pin();   // disconnected driver
  I(piter.size() == 1);  // If there can be many drivers, use the inp_driver iterator
  return piter.back();
}

Node_pin_iterator Node_pin::inp_drivers() const {
  I(is_sink() || is_graph_output());
  return current_g->inp_drivers(*this);
}

Node_pin_iterator Node_pin::out_sinks() const {
  I(is_driver());
  return current_g->out_sinks(*this);
}

Node Node_pin::create(Ntype_op op) const {
  auto node  = current_g->create_node(op);
  node.top_g = top_g;
  node.hidx  = hidx;
  return node;
}

Node Node_pin::create_const(const Lconst &value) const {
  auto node  = current_g->create_node_const(value);
  node.top_g = top_g;
  node.hidx  = hidx;
  return node;
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

void Node_pin::connect_sink(const Node_pin &spin) const {
  I(spin.is_sink());
  I(is_driver());
  I(current_g == spin.current_g);  // Use punch otherwise
  current_g->add_edge(*this, spin);
}

void Node_pin::connect_sink(const Node &node) const { connect_sink(node.setup_sink_pin()); }

void Node_pin::connect_driver(const Node_pin &dpin) const {
  I(dpin.is_driver());
  I(is_sink());
  I(current_g == dpin.current_g);
  current_g->add_edge(dpin, *this);
}

void Node_pin::connect_driver(const Node &node) const { connect_driver(node.setup_driver_pin()); }

int Node_pin::get_num_edges() const {
  if (is_sink())
    return current_g->get_num_inp_edges(*this);

  return current_g->get_num_out_edges(*this);
}

uint32_t Node_pin::get_bits() const {
  I(is_driver());
  return current_g->get_bits(get_root_idx());
}

void Node_pin::set_size(const Node_pin &dpin) {
  I(is_driver());
  I(dpin.is_driver());

  current_g->set_bits(get_root_idx(), dpin.get_bits());
  if (dpin.is_unsign())
    set_unsign();
  else
    set_sign();
}

void Node_pin::set_bits(uint32_t bits) {
  I(is_driver());
  current_g->set_bits(get_root_idx(), bits);
}

void Node_pin::set_unsign() { Ann_node_pin_unsign::ref(get_lg())->set(get_compact_driver(), true); }

void Node_pin::set_sign() { Ann_node_pin_unsign::ref(get_lg())->erase(get_compact_driver()); }

bool Node_pin::is_unsign() const { return Ann_node_pin_unsign::ref(top_g)->has(get_compact_driver()) ? true : false; }

mmap_lib::str Node_pin::get_type_sub_pin_name() const {
  const auto node = get_node();

  const auto &sub    = node.get_type_sub_node();
  const auto &io_pin = sub.get_io_pin_from_instance_pid(pid);

  GI(is_driver(), !io_pin.is_input());  // it can be invalid
  GI(is_sink(), !io_pin.is_output());   // it can be invalid

  return io_pin.name;
}

void  Node_pin::set_delay(float val) { Ann_node_pin_delay::ref(top_g)->set(get_compact_driver(), val); }
bool  Node_pin::has_delay() const { return Ann_node_pin_delay::ref(top_g)->has(get_compact_driver()); }
float Node_pin::get_delay() const { return Ann_node_pin_delay::ref(top_g)->get(get_compact_driver()); }

void Node_pin::del_delay() { Ann_node_pin_delay::ref(top_g)->erase(get_compact_driver()); }

void Node_pin::set_name(const mmap_lib::str &wname) {
  I(wname.size());  // empty names not allowed
  Ann_node_pin_name::ref(current_g)->set(get_compact_class_driver(), wname);
}

void Node_pin::reset_name(const mmap_lib::str &wname) {
  auto *ref = Ann_node_pin_name::ref(current_g);

  auto it = ref->find(get_compact_class_driver());
  if (it != ref->end()) {
    if (it->second == wname)
      return;

    ref->erase(it);
  }

  ref->set(get_compact_class_driver(), wname);
}

void Node_pin::del() {
  if (is_graph_output() && sink) {
    auto dpin = change_to_driver_from_graph_out_sink();
    dpin.del();
    return;
  }

  if (!sink) {
    if (has_name()) {  // works for sink/driver if needed
      del_name();
    }
    if (has_delay()) {
      del_delay();
    }
  }

  get_lg()->del_pin(*this);

  invalidate();
}

void Node_pin::del_name() {
  I(has_name());
  if (is_hierarchical()) {
    I(false);
    // the flow still does not have hierarchical compact_class delete. Are you
    // sure that you do not need a not hierarchical node?
    //
    // pin.get_non_hierarchical().del() will work if you got a hierarchical pin for some reason
  } else {
    Ann_node_pin_name::ref(current_g)->erase_key(get_compact_class_driver());
  }
}

// FIXME->sh: could be deprecated if ann_ssa could be mmapped for a mmap_lib::str
void Node_pin::set_prp_vname(const mmap_lib::str &prp_vname) {
  Ann_node_pin_prp_vname::ref(current_g)->set(get_compact_class_driver(), prp_vname);
}

void Node_pin::dump_all_prp_vname() const {
  auto *ref = Ann_node_pin_prp_vname::ref(current_g);

  for (const auto &it : *ref) {
    if (current_g->is_valid_node_pin(it.first.idx)) {
      Node_pin a(current_g, it.first);
      fmt::print("prp_vname pin:{} vname:{}\n", a.debug_name(), it.second);
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
      name = Ann_node_pin_name::ref(current_g)->get_val(get_compact_class_driver()).to_s();

  const auto node = get_node();
  if (name.empty()) {
    if (node.is_type_sub()) {
      name = node.get_type_sub_node().get_name_from_instance_pid(pid).to_s();
    } else if (node.is_type_const()) {
      name = node.get_type_const().to_pyrope().to_s();
      return absl::StrCat("pin_", "n", std::to_string(node.nid), "_", name);
    } else if (node.has_name()) {
      name = node.get_name().to_s();
    } else {
      if (is_sink()) {
        name = Ntype::get_sink_name(node.get_type_op(), pid).to_s();
      } else {
        if (Ntype::is_multi_driver(node.get_type_op()))
          name = std::to_string(pid);
        else
          name = "Y";
      }
    }
  }

  return absl::StrCat("pin_",
                      "n",
                      std::to_string(node.nid),
                      "_",
                      name,
                      "_",
                      sink ? "s" : "d",
                      std::to_string(pid),
                      "_lg",
                      current_g->get_name().to_s());
}

mmap_lib::str Node_pin::get_wire_name() const {
  if (is_sink()) {
    auto dpin = get_driver_pin();
    if (dpin.is_invalid())
      return ""_str;
    return dpin.get_wire_name();
  }

  if (!is_connected())
    return "";

  mmap_lib::str name;

  if (is_hierarchical()) {
    name = mmap_lib::str::concat("lg", current_g->get_name(), "_hidx", hidx);
  }

  if (has_name()) {
    name = mmap_lib::str::concat(name, get_name());
    return name;
  }

  if (name.empty())
    name = "t";

  name = mmap_lib::str::concat(name, "_pin", get_root_idx().value, "_", pid);

  return name;
}

mmap_lib::str Node_pin::get_name() const {
#ifndef NDEBUG
  if (!is_graph_io()) {
    I(is_driver());
    I(has_name());  // get_name should be called for named driver_pins
  }
#endif
  // NOTE: Not the usual get_compact_class_driver() to handle IO change from driver/sink
  return Ann_node_pin_name::ref(current_g)->get_val(Compact_class_driver(get_root_idx()));
}

mmap_lib::str Node_pin::get_prp_vname() const {
#ifndef NDEBUG
  if (!is_graph_io()) {
    I(is_driver());
    I(has_prp_vname());  // get_name should be called for named driver_pins
  }
#endif
  // NOTE: Not the usual get_compact_class_driver() to handle IO change from driver/sink
  return Ann_node_pin_prp_vname::ref(current_g)->get(Compact_class_driver(get_root_idx()));
}

bool Node_pin::has_name() const { return Ann_node_pin_name::ref(current_g)->has_key(get_compact_class_driver()); }

bool Node_pin::has_prp_vname() const { return Ann_node_pin_prp_vname::ref(current_g)->has(get_compact_class_driver()); }

Node_pin Node_pin::find_driver_pin(Lgraph *top, mmap_lib::str wname) {
  auto       ref = Ann_node_pin_name::ref(top);
  {
    const auto it  = ref->find_val(wname);
    if (it != ref->end()) {
      return Node_pin(top, it->first);
    }
  }

  auto can_wname = Lgtuple::get_canonical_name(wname);
  if (can_wname != wname) {
    const auto it2 = ref->find_val(can_wname);
    if (it2 != ref->end()) {
      return Node_pin(top, it2->first);
    }
  }

  return Node_pin();
}

mmap_lib::str Node_pin::get_pin_name() const {
  if (is_graph_io()) {
    if (pid == 0) {
      return is_graph_output() ? "%" : "$";
    }
    const auto &sub    = current_g->get_self_sub_node();
    const auto &io_pin = sub.get_io_pin_from_instance_pid(pid);
    return io_pin.name;
  }

  auto op = get_type_op();
  if (op == Ntype_op::Sub) {
    if (pid == 0) {
      return is_driver() ? "%" : "$";
    }
    return get_type_sub_pin_name();
  }

  if (is_driver())
    return Ntype::get_driver_name(op);

  return Ntype::get_sink_name(op, pid);
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

uint32_t Node_pin::get_ssa() const {
  return Ann_node_pin_ssa::ref(top_g)->get(get_compact_class_driver());
}

void Node_pin::set_ssa(uint32_t v) {
  Ann_node_pin_ssa::ref(top_g)->set(get_compact_class_driver(), v);
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
    for (auto &other : inp_drivers()) {
      if (other == pin)
        return true;
    }
    return false;
  }
  if (likely(is_driver())) {  // sink can not be connected to another sink
    for (auto &other : pin.inp_drivers()) {
      if (other == *this)
        return true;
    }
  }

  return false;
}

Node_pin Node_pin::get_down_pin() const {
  auto node = get_node();
  I(node.is_type_sub_present());

  // 1st: Get down_hidx
	auto down_hidx = top_g->get_htree().go_down(node);

  // 2nd: get down_pid
  I(pid != Port_invalid);
  I(node.get_type_sub_node().has_instance_pin(pid) || pid == 0);
  auto down_pid = pid;
  I(down_pid != Port_invalid);

  // 3rd: get down_current_g
  auto *down_current_g = node.ref_type_sub_lgraph();

  // 4th: get down_idx
  Index_id down_idx = down_current_g->find_idx_from_pid(is_driver() ? Hardcoded_output_nid : Hardcoded_input_nid, down_pid);
  I(down_idx);

  bool down_sink = is_driver();  // top driver goes to an down output which should be a sink
  return Node_pin(top_g, down_current_g, down_hidx, down_idx, down_pid, down_sink);
}

Node_pin Node_pin::get_up_pin() const {
  I(get_node().is_graph_io());

  bool up_sink = get_node().is_graph_input();  // down input, must be a sink on instance

  // 1st: get up_pid
  I(pid != Port_invalid);
  I(pid);  // FIXME: implement the case of PID = 0 (% out, $ inp)
  const auto &io_pin = current_g->get_self_sub_node().get_io_pin_from_instance_pid(pid);

  if (io_pin.is_input() != up_sink) {
    // IO port is not found (different direction is there)
    return Node_pin();  // Invalid, the input is not connected
  }

  auto up_pid = pid;
  I(up_pid != Port_invalid);

  // 2nd: get up_current_g
  auto up_node = top_g->ref_htree()->get_instance_up_node(hidx);

  // 3rd: get up_idx
  Index_id up_idx = up_node.get_class_lgraph()->find_idx_from_pid(up_node.get_nid(), up_pid);
  if (up_idx.is_invalid())
    return Node_pin();  // Invalid, the input is not connected

  I(up_idx);

  return Node_pin(top_g, up_node.get_class_lgraph(), up_node.get_hidx(), up_idx, up_pid, up_sink);
}

XEdge_iterator Node_pin::inp_edges() const { return current_g->inp_edges(*this); }

XEdge_iterator Node_pin::out_edges() const { return current_g->out_edges(*this); }

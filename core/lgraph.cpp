//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <assert.h>
#include <dirent.h>
#include <sys/types.h>

#include <fstream>
#include <iostream>
#include <set>

#include "graph_library.hpp"
#include "lgedgeiter.hpp"
#include "pass.hpp"

#include "lgraph.hpp"
#include "annotate.hpp"

LGraph::LGraph(std::string_view _path, std::string_view _name, std::string_view _source, bool _clear)
    :LGraph_Base(_path, _name, Graph_library::instance(_path)->register_lgraph(_name, _source, this))
    ,LGraph_Node_Type(_path, _name, get_lgid()) {
  I(_name == get_name());
  if (_clear) {  // Create
    clear();
    sync();
  } else {  // open
    reload();
    // I(node_internal.size()); // WEIRD, but possible to have an empty lgraph
  }
}

LGraph::~LGraph() {
  sync();
  library->unregister(name, lgid, this);
}

bool LGraph::exists(std::string_view path, std::string_view name) { return Graph_library::try_find_lgraph(path, name) != nullptr; }

LGraph *LGraph::create(std::string_view path, std::string_view name, std::string_view source) {
  auto *lib = Graph_library::instance(path);
  LGraph *lg = lib->try_find_lgraph(name);
  if (lg)
    return new (lg) LGraph(path, name, source, true);

  return new LGraph(path, name, source, true);
}

LGraph *LGraph::open(std::string_view path, int lgid) {
  std::string_view name = Graph_library::instance(path)->get_name(lgid);

  return open(path, name);
}

void LGraph::rename(std::string_view path, std::string_view orig, std::string_view dest) {
  bool valid = Graph_library::instance(path)->rename_name(orig, dest);
  if (valid)
    Pass::warn("lgraph::rename find original graph {} in path {}", orig, path);
  else
    Pass::error("cannot find original graph {} in path {}", orig, path);
}

LGraph *LGraph::open(std::string_view path, std::string_view name) {
  auto *lib = Graph_library::instance(path);
  if (lib == 0)
    return 0;

  LGraph *lg = lib->try_find_lgraph(path, name);
  if (lg) {
    I(name == lg->get_name());
    return lg;
  }

  if(!lib->has_name(name))
    return 0;

  auto source = lib->get_source(name);

  return new LGraph(path, name, source, false);
}

void LGraph::reload() {
  LGraph_Base::reload();
  LGraph_Node_Type::reload();
}

void LGraph::clear() {
  LGraph_Node_Type::clear();

  LGraph_Base::clear();  // last. Removes lock at the end

  Ann_support::clear(this);

  auto nid1 = create_node_int();
  auto nid2 = create_node_int();

  I(nid1 == Node::Hardcoded_input_nid);
  I(nid2 == Node::Hardcoded_output_nid);

  set_type(nid1, GraphIO_Op);
  set_type(nid2, GraphIO_Op);
}

void LGraph::sync() {
  LGraph_Node_Type::sync();

  LGraph_Base::sync();  // last. Removes lock at the end
}

void LGraph::emplace_back() {
  LGraph_Base::emplace_back();
  LGraph_Node_Type::emplace_back();
}


Node_pin LGraph::get_graph_input(std::string_view str) {

  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this,0,Node::Hardcoded_input_nid).setup_driver_pin(io_pid);
}

Node_pin LGraph::get_graph_output(std::string_view str) {
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this,0,Node::Hardcoded_output_nid).setup_sink_pin(io_pid);
}

Node_pin LGraph::get_graph_output_driver(std::string_view str) {
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this,0,Node::Hardcoded_output_nid).setup_driver_pin(io_pid);
}

bool LGraph::is_graph_input(std::string_view name) const {

  bool alt=false;
#ifndef NDEBUG
  if (get_self_sub_node().has_pin(name)) {
    const auto &io_pin = get_self_sub_node().get_pin(name);
    alt = io_pin.dir == Sub_node::Direction::Input;
  } else {
    alt = false;
  }
#endif

  auto ref = Ann_node_pin_name::ref(this);
  const auto it = ref->find_val(name);
  if (it == ref->end()) {
    I(!alt);
    return false;
  }
  auto compact = ref->get_key(it);
  auto dpin = Node_pin(const_cast<LGraph *>(this), compact);
  bool cond = dpin.get_node().get_nid() == Node::Hardcoded_input_nid;

  I(cond == alt);

  return cond;
}

bool LGraph::is_graph_output(std::string_view name) const {

  bool alt = false;
#ifndef NDEBUG
  if (get_self_sub_node().has_pin(name)) {
    const auto &io_pin = get_self_sub_node().get_pin(name);
    alt = io_pin.dir == Sub_node::Direction::Output;
  } else {
    alt = false;
  }
#endif

  auto ref = Ann_node_pin_name::ref(this);
  const auto it = ref->find_val(name);
  if (it == ref->end()) {
    I(!alt);
    return false;
  }
  auto compact = ref->get_key(it);
  auto dpin = Node_pin(const_cast<LGraph *>(this), compact);
  bool cond = dpin.get_node().get_nid() == Node::Hardcoded_output_nid;

  I(cond == alt);

  return cond;
}

Node_pin LGraph::add_graph_input(std::string_view str, Port_ID pos, uint16_t bits) {
  I(!is_graph_output(str));

  fmt::print("add_input {} {} {}\n", name, str, pos);

  Port_ID inst_pid;
  if (get_self_sub_node().has_pin(str)) {
    // reset pin stats
    inst_pid = get_self_sub_node().map_graph_pos(str, pos);
  }else{
    inst_pid = get_self_sub_node().add_pin(str, Sub_node::Direction::Input, pos);
  }
  I(node_type_table[Node::Hardcoded_input_nid] == GraphIO_Op);

  auto idx = setup_idx_from_pid(Node::Hardcoded_input_nid, inst_pid);
  setup_driver(idx); // Just driver, no sink

  Node_pin pin(this, this, 0, idx, inst_pid, false);

  pin.set_name(str);
  pin.set_bits(bits);

  return pin;
}

Node_pin LGraph::add_graph_output(std::string_view str, Port_ID pos, uint16_t bits) {
  I(!is_graph_input(str));

  fmt::print("add_output {} {} {}\n", name, str, pos);

  Port_ID inst_pid;
  if (get_self_sub_node().has_pin(str)) {
    // reset pin stats
    inst_pid = get_self_sub_node().map_graph_pos(str, pos);
  }else{
    inst_pid = get_self_sub_node().add_pin(str, Sub_node::Direction::Output, pos);
  }
  I(node_type_table[Node::Hardcoded_output_nid] == GraphIO_Op);

  auto idx = setup_idx_from_pid(Node::Hardcoded_output_nid, inst_pid);
  setup_sink(idx);
  setup_driver(idx); // outputs can also drive internal nodes. So both sink/driver

  Node_pin pin(this, this, 0, idx, inst_pid, false);
  pin.set_name(str);
  pin.set_bits(bits);

  return Node_pin(this, this, 0, idx, inst_pid, true);
}

Node_pin_iterator LGraph::out_connected_pins(const Node &node) const {
  I(node.get_class_lgraph() == this);
  Node_pin_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[idx2].is_master_root());

  absl::flat_hash_set<uint32_t> visited;

  while (true) {
    auto n = node_internal[idx2].get_num_local_outputs();
    if (n>0) {
      if (node_internal[idx2].is_root()) {
        xiter.emplace_back(Node_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hid(), idx2, node_internal[idx2].get_dst_pid(), false));
        visited.insert(node_internal[idx2].get_dst_pid());
      }else{
        if (visited.find(node_internal[idx2].get_dst_pid()) == visited.end()) {
          auto master_nid = node_internal[idx2].get_nid();
          xiter.emplace_back(Node_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hid(), master_nid, node_internal[idx2].get_dst_pid(), false));
        }
      }
    }

    if (node_internal[idx2].is_last_state()) break;

    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}

Node_pin_iterator LGraph::inp_connected_pins(const Node &node) const {
  I(node.get_class_lgraph() == this);
  Node_pin_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[idx2].is_master_root());

  absl::flat_hash_set<uint32_t> visited;

  while (true) {
    auto n = node_internal[idx2].get_num_local_inputs();
    if (n>0) {
      if (node_internal[idx2].is_root()) {
        xiter.emplace_back(Node_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hid(), idx2, node_internal[idx2].get_dst_pid(), true));
        visited.insert(node_internal[idx2].get_dst_pid());
      }else{
        if (visited.find(node_internal[idx2].get_dst_pid()) == visited.end()) {
          auto master_nid = node_internal[idx2].get_nid();
          xiter.emplace_back(Node_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hid(), master_nid, node_internal[idx2].get_dst_pid(), true));
        }
      }
    }

    if (node_internal[idx2].is_last_state()) break;

    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}

Node_pin_iterator LGraph::out_setup_pins(const Node &node) const {
  I(node.get_class_lgraph() == this);
  Node_pin_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[idx2].is_master_root());

  while (true) {
    if (node_internal[idx2].is_root()  && node_internal[idx2].is_driver_setup())
      xiter.emplace_back(Node_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hid(), idx2, node_internal[idx2].get_dst_pid(), false));

    if (node_internal[idx2].is_last_state()) break;

    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}

Node_pin_iterator LGraph::inp_setup_pins(const Node &node) const {
  I(node.get_class_lgraph() == this);
  Node_pin_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[idx2].is_master_root());

  while (true) {
    if (node_internal[idx2].is_root()  && node_internal[idx2].is_sink_setup())
      xiter.emplace_back(Node_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hid(), idx2, node_internal[idx2].get_dst_pid(), true));

    if (node_internal[idx2].is_last_state()) break;

    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}

XEdge_iterator LGraph::out_edges(const Node &node) const {
  I(node.get_class_lgraph() == this);
  XEdge_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[node.get_nid()].is_master_root());

  while (true) {
    auto n = node_internal[idx2].get_num_local_outputs();
    uint8_t i;
    const Edge_raw *redge;
    for(i=0, redge = node_internal[idx2].get_output_begin()
        ;i<n
        ;i++,redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      xiter.emplace_back(redge->get_out_pin(node.get_top_lgraph(), node.get_hid())
                        ,redge->get_inp_pin(node.get_top_lgraph(), node.get_hid())
                        );
    }
    if (node_internal[idx2].is_last_state()) break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}

XEdge_iterator LGraph::inp_edges(const Node &node) const {
  I(node.get_class_lgraph() == this);
  XEdge_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[node.get_nid()].is_master_root());

  while (true) {
    auto n = node_internal[idx2].get_num_local_inputs();
    uint8_t i;
    const Edge_raw *redge;
    for(i=0, redge = node_internal[idx2].get_input_begin()
        ;i<n
        ;i++,redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      xiter.emplace_back(redge->get_out_pin(node.get_top_lgraph(),node.get_hid()),redge->get_inp_pin(node.get_top_lgraph(),node.get_hid()));
    }
    if (node_internal[idx2].is_last_state()) break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}

const LGraph *LGraph::find_sub_lgraph_const(const Hierarchy_id hid) const {
  if (hid==0)
    return this;

  auto hid_bits = get_hid_bits();
  Index_ID level_0_sub_nid = hid & ((1<<hid_bits)-1);
  //auto level_n_sub_hid = hid >> hid_bits;

  I(sub_nodes.find(Node::Compact_class(level_0_sub_nid)) != sub_nodes.end());

  auto sub_lgid = get_type_sub(level_0_sub_nid);
  if (sub_lgid==0)
    return 0; // No subgraph present (bbox)

  return open(path, sub_lgid);
}

Node LGraph::create_node() {
  Index_ID nid = create_node_int();
  return Node(this, 0, nid);
}

Node LGraph::create_node(const Node &old_node) {

  // TODO: We can just copy the node_type_table AND update the tracking (graphio, consts)

  Node_Type_Op op = old_node.get_type().op;

  if (op == LUT_Op ) {
    auto new_node = create_node();
    new_node.set_type_lut(old_node.get_type_lut());
    return new_node;
  }

  if (op == SubGraph_Op) {
    return create_node_sub(old_node.get_type_sub());
  }

  if (op == U32Const_Op) {
    return create_node_const(old_node.get_type_const_value(), old_node.get_driver_pin().get_bits());
  }

  if (op == StrConst_Op) {
    return create_node_const(old_node.get_type_const_sview(), old_node.get_driver_pin().get_bits());
  }

  return create_node(op);
}

Node LGraph::create_node(Node_Type_Op op) {
  Index_ID nid = create_node_int();
  set_type(nid, op);

  I(op!=SubGraph_Op); // Do not build by steps. call create_node_sub

  return Node(this, 0, nid);
}

Node LGraph::create_node(Node_Type_Op op, uint16_t bits) {

  auto node = create_node(op);

  node.setup_driver_pin().set_bits(bits);

  return node;
}

Node LGraph::create_node_const(uint32_t value, uint16_t bits) {

  auto nid = find_type_const_value(value);
  if (nid==0 || node_internal[nid].get_bits() != bits) {
    nid = create_node_int();
    set_type_const_value(nid, value);

    set_bits(nid, bits);
  }

  I(node_internal[nid].get_dst_pid()==0);
  I(node_internal[nid].is_master_root());

  return Node(this,0,nid);
}

Node LGraph::create_node_const(std::string_view value) {

  auto nid = find_type_const_sview(value);
  if (unlikely(nid)) {
    nid = create_node_int();
    set_type_const_sview(nid, value);
  }

  return Node(this,0,nid);
}

Node LGraph::create_node_sub(Lg_type_id sub_id) {
  I(get_lgid() != sub_id); // It can not point to itself (in fact, no recursion of any type)

  auto nid = create_node().get_nid();
  set_type_sub(nid, sub_id);

  return Node(this,0,nid);
}

Node LGraph::create_node_sub(std::string_view sub_name) {
  I(name != sub_name); // It can not point to itself (in fact, no recursion of any type)

  auto nid = create_node().get_nid();
  auto &sub = library->setup_sub(sub_name);
  set_type_sub(nid, sub.get_lgid());

  return Node(this,0,nid);
}

Sub_node &LGraph::get_self_sub_node() const {
  return library->get_sub(get_lgid());
}

Node LGraph::create_node_const(std::string_view value, uint16_t bits) {

  auto nid = find_type_const_sview(value);

  if (unlikely(nid==0 || node_internal[nid].get_bits() != bits)) {
    nid = create_node_int();
    set_type_const_sview(nid, value);
    set_bits(nid,bits);
  }

  I(node_internal[nid].get_dst_pid()==0);
  I(node_internal[nid].is_master_root());

  return Node(this,0,nid);
}

Index_ID LGraph::create_node_int() {
  get_lock();  // FIXME: change to Copy on Write permissions (mmap exception, and remap)
  emplace_back();

  if (node_internal.back().is_page_align()) {
    Node_Internal_Page *page = (Node_Internal_Page *)&node_internal.back();

    page->set_page(node_internal.size() - 1);
    emplace_back();
  }

  assert(node_internal[node_internal.size() - 1].get_dst_pid() == 0);

  I(node_internal[node_internal.size() - 1].get_nid() == node_internal.size()-1);
  return node_internal.size() - 1;
}

Forward_edge_iterator LGraph::forward() { return Forward_edge_iterator(this); }

Backward_edge_iterator LGraph::backward() { return Backward_edge_iterator(this); }

// Skip after 1, but first may be deleted, so fast_next
Fast_edge_iterator LGraph::fast(bool visit_sub) { return Fast_edge_iterator(this, 0, fast_next(0), visit_sub);  }

void LGraph::dump() {
  fmt::print("lgraph name:{} size:{}\n", name, node_internal.size());

  for(const auto &io_pin:get_self_sub_node().get_io_pins()) {
    fmt::print("io {} pid:{} {}\n", io_pin.name, io_pin.graph_io_pos, io_pin.dir==Sub_node::Direction::Input?"input":"output");
  }

#if 1
  for (size_t i = 0; i < node_internal.size(); ++i) {
    if (!node_internal[i].is_node_state())
      continue;
    if (!node_internal[i].is_master_root())
      continue;
    auto node = Node(this,Node::Compact_class(i)); // NOTE: To remove once new iterators are finished
    fmt::print("nid:{} type:{}\n", node.nid, node.get_type().get_name());
    if (node.get_type().get_name()=="lut") {
      fmt::print("  lut_id=0x{:x}\n",node.get_type_lut());
    }
    for(const auto &edge : node.inp_edges()) {
      fmt::print("  inp pid:{} from nid:{} pid:{} name:{}\n", edge.sink.get_pid(), edge.driver.get_node().nid,
                 edge.driver.get_pid(), edge.driver.debug_name());
    }
    for(const auto &edge : node.out_edges()) {
      fmt::print("  out pid:{} name:{} to nid:{} pid:{}\n", edge.driver.get_pid(), edge.driver.debug_name(),
                 edge.sink.get_node().nid, edge.sink.get_pid());
    }
  }
#endif
}



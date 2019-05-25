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

Index_ID LGraph::add_graph_io_common() {

  auto nid = create_node_int();
  set_type(nid, GraphIO_Op);
  I(node_internal[nid].is_master_root());

  return nid;
}

Node_pin LGraph::add_graph_input_int(std::string_view str, uint16_t bits) {
  I(input_array.get_id(str) == 0);  // No name duplication

  auto nid = add_graph_io_common();
  node_internal[nid].set_graph_io_input();

  auto pid = ++max_io_port_pid;
  IO_port p(nid, pid, false);
  input_array.create_id(str, p);

  auto idx = setup_idx_from_pid(nid, pid);
  set_bits(idx, bits);
  return Node_pin(this, 0, idx, pid, false);
}

Node_pin LGraph::add_graph_output_int(std::string_view str, uint16_t bits) {
  I(output_array.get_id(str) == 0);  // No name duplication

  auto nid = add_graph_io_common();
  node_internal[nid].set_graph_io_output();

  auto pid = ++max_io_port_pid;
  IO_port p(nid, pid, false);
  output_array.create_id(str, p);

  auto idx = setup_idx_from_pid(nid, pid);
  set_bits(idx, bits);
  return Node_pin(this, 0, idx, pid, true);
}

LGraph::LGraph(const std::string &path, const std::string &_name, const std::string &_source, bool _clear)
    // Lgraph_base_core(path, _name, Graph_library::instance(path)->register_lgraph(_name, _source, this))
    :LGraph_Base(path, _name, Graph_library::instance(path)->register_lgraph(_name, _source, this))
    ,LGraph_Node_Delay(path, _name, lg_id())
    ,LGraph_Node_bitwidth(path, _name, lg_id())
    ,LGraph_Node_Src_Loc(path, _name, lg_id())
    ,LGraph_Node_Type(path, _name, lg_id()) {
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
  bool deleted = library->unregister_lgraph(name, lgraph_id, this);
  I(deleted);
}

bool LGraph::exists(std::string_view path, std::string_view name) { return Graph_library::try_find_lgraph(path, name) != nullptr; }

LGraph *LGraph::create(std::string_view path, std::string_view name, std::string_view source) {
  LGraph *lg = Graph_library::try_find_lgraph(path, name);
  if (lg) {
    assert(Graph_library::instance(path));
    // Overwriting old lgraph. Delete old pointer (but better be sure that nobody has it)
    bool deleted = lg->close();
    if (deleted) {
      // Call expunge id delete LGraph object
      lg->library->expunge_lgraph(name, lg);
      delete lg;
    }
  }

  return new LGraph(std::string(path), std::string(name), std::string(source),
                    true);  // TODO: Remove these nasty std::string (create local)
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
  LGraph *lg = Graph_library::try_find_lgraph(path, name);
  if (lg) {
    // I(lg->node_internal.size()); // WEIRD, but possible to have an empty lgraph
    assert(Graph_library::instance(path));
    auto source = Graph_library::instance(path)->get_source(name);
    auto lgid   = Graph_library::instance(path)->register_lgraph(name, source, lg);
    I(name == lg->get_name());
    assert(lg->lgraph_id == lgid);

    return lg;
  }

  if (!Graph_library::instance(path)->include(name)) return 0;

#if 0
  std::string lock = absl::StrCat(path, "/", std::to_string(lgraph_id), ".lock");
  if (access(lock.c_str(), R_OK) != -1) {
    Pass::error("trying to open a locked {} (broken?) graph {}", lock, name);
    return 0;
  }
#endif
  const auto &source = Graph_library::instance(path)->get_source(name);

  return new LGraph(std::string(path), std::string(name), std::string(source), false);
}

bool LGraph::close() {
  bool deleted = library->unregister_lgraph(name, lgraph_id, this);

  sync();

  LGraph_Base::close();

  return deleted;
}

void LGraph::reload() {
  LGraph_Base::reload();
  LGraph_Node_Delay::reload();
  LGraph_Node_bitwidth::reload();
  LGraph_Node_Src_Loc::reload();
  LGraph_Node_Type::reload();
}

void LGraph::clear() {
  LGraph_Node_Delay::clear();
  LGraph_Node_bitwidth::clear();
  LGraph_Node_Src_Loc::clear();
  LGraph_Node_Type::clear();

  LGraph_Base::clear();  // last. Removes lock at the end

  Ann_support::clear(this);
}

void LGraph::sync() {
  LGraph_Node_Delay::sync();
  LGraph_Node_bitwidth::sync();
  LGraph_Node_Src_Loc::sync();
  LGraph_Node_Type::sync();

  LGraph_Base::sync();  // last. Removes lock at the end

  Ann_support::sync(this);
}

void LGraph::emplace_back() {
  LGraph_Base::emplace_back();
  LGraph_Node_Delay::emplace_back();
  LGraph_Node_bitwidth::emplace_back();
  LGraph_Node_Src_Loc::emplace_back();
  LGraph_Node_Type::emplace_back();
}

Node_pin LGraph::get_graph_input(std::string_view str) {
  I(input_array.get_id(str) != 0);

  const auto &p = input_array.get_field(str);
  return get_node(p.nid).get_driver_pin(p.pos);
}

Node_pin LGraph::get_graph_output(std::string_view str) {
  I(output_array.get_id(str) != 0);

  const auto &p = output_array.get_field(str);
  return get_node(p.nid).get_sink_pin(p.pos);
}

Node_pin LGraph::get_graph_output_driver(std::string_view str) {
  I(output_array.get_id(str) != 0);

  const auto &p = output_array.get_field(str);
  return get_node(p.nid).get_driver_pin(p.pos);
}

#if 0
Node_pin LGraph::get_graph_output_sink(std::string_view str) const {
  I(output_array.get_id(str) != 0);

  const auto &p = output_array.get_field(str);
  return get_node(p.nid).get_sink_pin(p.pos);
}
#endif

Node_pin LGraph::add_graph_input(std::string_view str, uint16_t bits, uint16_t offset) {
  I(!is_graph_output(str));

  auto pin = add_graph_input_int(str, bits);

  pin.set_offset(offset);
  pin.set_name(str);

#ifndef NDEBUG
  const auto &p = input_array.get_field(str);
  I(pin.get_pid() == p.pos && p.nid != pin.get_idx());
#endif
  return pin;
}

Node_pin LGraph::add_graph_output(std::string_view str, uint16_t bits, uint16_t offset) {
  I(!is_graph_input(str));

  auto pin = add_graph_output_int(str, bits);

  pin.set_offset(offset);
  pin.get_node().get_driver_pin(pin.get_pid()).set_name(str); // Only driver pins have names

#ifndef NDEBUG
  const auto &p = output_array.get_field(str);
  I(pin.get_pid() == p.pos && p.nid != pin.get_idx());
#endif
  return pin;
}

Node_pin_iterator LGraph::out_connected_pins(const Node &node) const {
  I(node.get_lgraph() == this);
  Node_pin_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[idx2].is_master_root());

  std::set<uint16_t> visited;

  while (true) {
    auto n = node_internal[idx2].get_num_local_outputs();
    if (n>0) {
      if (node_internal[idx2].is_root()) {
        xiter.emplace_back(Node_pin(node.get_lgraph(),node.get_hid(), idx2, node_internal[idx2].get_dst_pid(), false));
        visited.insert(node_internal[idx2].get_dst_pid());
      }else{
        if (visited.find(node_internal[idx2].get_dst_pid()) == visited.end()) {
          auto master_nid = node_internal[idx2].get_nid();
          xiter.emplace_back(Node_pin(node.get_lgraph(),node.get_hid(), master_nid, node_internal[idx2].get_dst_pid(), false));
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

XEdge_iterator LGraph::out_edges(const Node &node) const {
  I(node.get_lgraph() == this);
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
      xiter.emplace_back(redge->get_out_pin(node.get_lgraph(),node.get_hid()),redge->get_inp_pin(node.get_lgraph(),node.get_hid()));
    }
    if (node_internal[idx2].is_last_state()) break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}
XEdge_iterator LGraph::inp_edges(const Node &node) const {
  I(node.get_lgraph() == this);
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
      xiter.emplace_back(redge->get_out_pin(node.get_lgraph(),node.get_hid()),redge->get_inp_pin(node.get_lgraph(),node.get_hid()));
    }
    if (node_internal[idx2].is_last_state()) break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}

Node LGraph::create_node() {
  Index_ID nid = create_node_int();
  return Node(this, 0, nid);
}

Node LGraph::create_node(Node_Type_Op op) {
  Index_ID nid = create_node_int();
  set_type(nid, op);

  return Node(this, 0, nid);
}

Node LGraph::create_node(Node_Type_Op op, uint16_t bits) {

  auto node = create_node(op);

  node.setup_driver_pin().set_bits(bits);

  return node;
}

Node LGraph::create_node_const(uint32_t value, uint16_t bits) {

  Index_ID nid = find_type_const_value(value);
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

  Index_ID nid = find_type_const_sview(value);
  if (nid==0) {
    nid = create_node_int();
    set_type_const_sview(nid, value);
  }

  return Node(this,0,nid);
}

Node LGraph::create_node_sub(Lg_type_id sub_id) {
  I(lg_id() != sub_id); // It can not point to itself (in fact, no recursion of any type)

  auto nid = create_node().get_nid();
  set_type_subgraph(nid, sub_id);

  return Node(this,0,nid);
}

Node LGraph::create_node_const(std::string_view value, uint16_t bits) {
  Index_ID nid = find_type_const_sview(value);
  if (nid==0 || node_internal[nid].get_bits() != bits) {
    nid = create_node_int();
    set_type_const_sview(nid, value);
    set_bits(nid,bits);
  }

  I(node_internal[nid].get_dst_pid()==0);
  I(node_internal[nid].is_master_root());

  return Node(this,0,nid);
}

Node LGraph::get_node(Index_ID idx) {
  I(node_internal.size() > idx);
  I(node_internal[idx].is_root());
  if (node_internal[idx].is_master_root())
    return Node(this, 0, idx);

  idx = node_internal[idx].get_nid();
  I(node_internal[idx].is_master_root());
  return Node(this, 0, idx);
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

  return node_internal[node_internal.size() - 1].get_nid();
}


Forward_edge_iterator LGraph::forward() { return Forward_edge_iterator(this); }

Backward_edge_iterator LGraph::backward() { return Backward_edge_iterator(this); }

// Skip after 1, but first may be deleted, so fast_next
Fast_edge_iterator LGraph::fast() { return Fast_edge_iterator(fast_next(0), this);  }

void LGraph::dump() {
  fmt::print("lgraph name:{} size:{}\n", name, node_internal.size());

  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    const auto &p = it.get_field();
    fmt::print("inp {} nid:{} pid:{}\n", it.get_name(), p.nid, p.pos);
  }
  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    const auto &p = it.get_field();
    fmt::print("out {} nid:{} pid:{}\n", it.get_name(), p.nid, p.pos);
  }

#if 1
  for (size_t i = 0; i < node_internal.size(); ++i) {
    if (!node_internal[i].is_node_state())
      continue;
    if (!node_internal[i].is_master_root())
      continue;
    auto node = Node(this,0,Node::Compact(i)); // NOTE: To remove once new iterators are finished
    fmt::print("nid:{} type:{}\n", node.nid, node.get_type().get_name());
    if (node.get_type().get_name()=="lut") {
      fmt::print("  lut_id=0x{:x}\n",node.get_type_lut());
    }
    for(const auto &edge : node.inp_edges()) {
      fmt::print("  inp pid:{} from pid:{} name:{}\n", edge.sink.get_pid(), edge.driver.get_pid(), edge.driver.debug_name());
    }
    for(const auto &edge : node.out_edges()) {
      fmt::print("  out pid:{} name:{} to nid:{} pid:{}\n", edge.driver.get_pid(), edge.driver.debug_name(),
                 edge.sink.get_node().nid, edge.sink.get_pid());
    }
  }
#endif
}

void LGraph::add_hierarchy_entry(std::string_view base, Lg_type_id lgid) {
  hierarchy[base] = lgid;
  if (hierarchy_cache.find(lgid) == hierarchy_cache.end()) {
    hierarchy_cache[lgid] = library->get_version(lgid);
  }
}

const LGraph::Hierarchy &LGraph::get_hierarchy() {
  if (!hierarchy.empty()) {
    bool all_ok = true;
    for (auto &[lgid, version] : hierarchy_cache) {
      if (library->get_version(lgid) == version) continue;
      all_ok = false;
      break;
    }
    if (all_ok) return hierarchy;
  }
  hierarchy.clear();
  hierarchy_cache.clear();

  struct Entry {
    std::string base;
    LGraph *    top;
    LGraph *    lg;
    Entry(std::string_view _base, LGraph *_top, LGraph *_lg) : base(_base), top(_top), lg(_lg) {}
  };
  std::vector<Entry> pending;

  pending.emplace_back(get_name(), this, this);

  while (!pending.empty()) {
    auto entry = pending.back();
    pending.pop_back();

    entry.top->add_hierarchy_entry(entry.base, entry.lg->lg_id());

    entry.lg->each_sub_graph_fast([&entry, &pending](Node &node, const Lg_type_id lgid) {
      if (!node.has_name()) return;
      LGraph *lg = LGraph::open(entry.top->get_path(), lgid);

      if (lg == 0) {
        Pass::error("hierarchy for {} could not open instance {} with lgid {}", entry.base, node.get_name(), lgid);
      } else {
        auto base2 = absl::StrCat(entry.base, ".", node.get_name());
        pending.emplace_back(base2, entry.top, lg);
      }
    });

    entry.lg->close();
  }

  return hierarchy;
}


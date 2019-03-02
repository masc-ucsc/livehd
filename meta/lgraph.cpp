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

LGraph::LGraph(const std::string &path, const std::string &_name, const std::string &_source, bool _clear)
    : Lgraph_base_core(path, _name, Graph_library::instance(path)->register_lgraph(_name, _source, this))
    , LGraph_Base(path, _name, lg_id())
    , LGraph_Node_Delay(path, _name, lg_id())
    , LGraph_Node_bitwidth(path, _name, lg_id())
    , LGraph_Node_Src_Loc(path, _name, lg_id())
    , LGraph_WireNames(path, _name, lg_id())
    , LGraph_Node_Place(path, _name, lg_id()) {
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

  std::string lock;
  lock.append(path);
  lock.append("/lgraph_");
  lock.append(name);
  lock.append(".lock");

  if (access(lock.c_str(), R_OK) != -1) {
    Pass::error("trying to open a locked {} (broken?) graph {}", lock, name);
    return 0;
  }
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
  LGraph_Node_Place::reload();
  LGraph_Node_Delay::reload();
  LGraph_Node_bitwidth::reload();
  LGraph_Node_Src_Loc::reload();
  LGraph_WireNames::reload();
}

void LGraph::clear() {
  LGraph_Node_Place::clear();
  LGraph_Node_Delay::clear();
  LGraph_Node_bitwidth::clear();
  LGraph_Node_Src_Loc::clear();
  LGraph_WireNames::clear();

  LGraph_Base::clear();  // last. Removes lock at the end
}

void LGraph::sync() {
  LGraph_Node_Place::sync();
  LGraph_Node_Delay::sync();
  LGraph_Node_bitwidth::sync();
  LGraph_Node_Src_Loc::sync();
  LGraph_WireNames::sync();

  LGraph_Base::sync();  // last. Removes lock at the end
}

void LGraph::emplace_back() {
  LGraph_Base::emplace_back();
  LGraph_Node_Place::emplace_back();
  LGraph_Node_Delay::emplace_back();
  LGraph_Node_bitwidth::emplace_back();
  LGraph_Node_Src_Loc::emplace_back();
  LGraph_WireNames::emplace_back();
}

Node_pin LGraph::get_graph_input(std::string_view str) const {
  I(input_array.get_id(str) != 0);

  const auto &p = input_array.get_field(str);
  return get_node(p.nid).get_driver_pin(p.pos);
}

Node_pin LGraph::get_graph_output(std::string_view str) const {
  I(output_array.get_id(str) != 0);

  const auto &p = output_array.get_field(str);
  return get_node(p.nid).get_sink_pin(p.pos);
}

Node_pin LGraph::get_graph_output_driver(std::string_view str) const {
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
  assert(!is_graph_output(str));
  assert(!has_wirename(str));

  auto pin = LGraph_Base::add_graph_input_int(str, bits);
  set_offset(pin.get_idx(), offset);
  set_node_wirename(pin, str);

  const auto &p = input_array.get_field(str);
  I(pin.get_pid() == p.pos && p.nid != pin.get_idx());
  return pin;
}

/*
Node_pin LGraph::add_graph_input(std::string_view str, uint16_t bits, uint16_t offset, Port_ID original_pos) {
  assert(!is_graph_output(str));
  assert(!has_wirename(str));

  auto pin = LGraph_Base::add_graph_input_int(str, bits, original_pos);
  set_offset(pin.get_idx(), offset);
  set_node_wirename(pin, str);

  const auto &p = input_array.get_field(str);
  I(pin.get_pid() == p.pos && p.nid != pin.get_idx());
  return pin;
}
*/

Node_pin LGraph::add_graph_output(std::string_view str, uint16_t bits, uint16_t offset) {
  assert(!is_graph_input(str));
  assert(!has_wirename(str));

  auto pin = LGraph_Base::add_graph_output_int(str, bits);
  LGraph_WireNames::set_offset(pin.get_idx(), offset);
  LGraph_WireNames::set_node_wirename(pin, str);

  const auto &p = output_array.get_field(str);
  I(pin.get_pid() == p.pos && p.nid != pin.get_idx());
  return pin;
}

/*
Node_pin LGraph::add_graph_output(std::string_view str, uint16_t bits, uint16_t offset, Port_ID original_pos) {
  assert(!is_graph_input(str));
  assert(!has_wirename(str));

  auto pin = LGraph_Base::add_graph_output_int(str, bits, original_pos);
  LGraph_WireNames::set_offset(pin.get_idx(), offset);
  LGraph_WireNames::set_node_wirename(pin, str);

  const auto &p = output_array.get_field(str);
  I(pin.get_pid() == p.pos && p.nid != pin.get_idx()); // PID==0 not used, so it must be different
  return pin;
}
*/

Node LGraph::create_node() {
  Index_ID nid = create_node_int();
  return Node(this, nid);
}

Node LGraph::create_node(Node_Type_Op op) {
  Index_ID nid = create_node_int();
  node_type_set(nid, op);

  return Node(this, nid);
}

Node LGraph::create_node(Node_Type_Op op, uint16_t bits) {

  auto node = create_node(op);
  I(node_type_get(node.get_nid()).has_single_output());

  auto pin0 = node.setup_driver_pin();
  set_bits(pin0,bits);

  return node;
}

Node LGraph::create_node_u32(uint32_t value, uint16_t bits) {

  Index_ID nid = node_u32type_find(value);
  if (nid==0 || node_internal[nid].get_bits() != bits) {
    nid = create_node_int();
    node_u32type_set(nid, value);

    set_bits(nid, bits);
  }

  I(node_internal[nid].get_dst_pid()==0);
  I(node_internal[nid].is_master_root());

  Node node(this,nid);

  return node;
}

Node LGraph::create_node_const(std::string_view value) {

  Index_ID nid = node_const_string_find(value);
  if (nid==0) {
    nid = create_node_int();
    node_const_type_set_string(nid, value);
  }

  Node node(this,nid);

  return node;
}

Node LGraph::create_node_const(std::string_view value, uint16_t bits) {
  Index_ID nid = node_const_string_find(value);
  if (nid==0 || node_internal[nid].get_bits() != bits) {
    nid = create_node_int();
    node_const_type_set_string(nid, value);
    set_bits(nid,bits);
  }

  I(node_internal[nid].get_dst_pid()==0);
  I(node_internal[nid].is_master_root());

  Node node(this,nid);

  return node;
}

Node LGraph::get_node(Index_ID nid) {
  I(node_internal.size() > nid);
  I(node_internal[nid].is_root());
  nid = node_internal[nid].get_nid();
  I(node_internal[nid].is_master_root());
  return Node(this, nid);
}

ConstNode LGraph::get_node(Index_ID nid) const {
  I(node_internal.size() > nid);
  I(node_internal[nid].is_root());
  nid = node_internal[nid].get_nid();
  I(node_internal[nid].is_master_root());
  return ConstNode(this, nid);
}

Node LGraph::get_node(const Node_pin &pin) {
  I(node_internal.size() > pin.get_idx());
  I(node_internal[pin.get_idx()].is_root());

  if (pin.get_pid()==0)
    return Node(this, pin.get_idx());

  Index_ID nid = node_internal[pin.get_idx()].get_nid(); // since it is root, no need to call the slower get_master_root_nid()
  I(node_internal[nid].is_master_root());
  return Node(this, nid);
}

ConstNode LGraph::get_node(const Node_pin &pin) const {
  I(node_internal.size() > pin.get_idx());
  I(node_internal[pin.get_idx()].is_root());
  if (pin.get_pid()==0)
    return ConstNode(this, pin.get_idx());

  Index_ID nid = node_internal[pin.get_idx()].get_nid(); // since it is root, no need to call the slower get_master_root_nid()
  I(node_internal[nid].is_master_root());
  return ConstNode(this, nid);
}

Node LGraph::get_dest_node(const Edge &edge) {
  Index_ID idx = edge.get_self_idx();

  I(node_internal.size() > idx);
  I(node_internal[idx].is_root());

  return Node(this, idx);
}

ConstNode LGraph::get_dest_node(const Edge &edge) const {
  Index_ID idx = edge.get_self_idx();

  I(node_internal.size() > idx);
  I(node_internal[idx].is_root());

  return ConstNode(this, idx);
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

const Edge_iterator ConstNode::inp_edges() const { return g->inp_edges(nid); }

const Edge_iterator ConstNode::out_edges() const { return g->out_edges(nid); }

const Edge_iterator Node::inp_edges() const { return g->inp_edges(nid); }

const Edge_iterator Node::out_edges() const { return g->out_edges(nid); }

Forward_edge_iterator LGraph::forward() const { return Forward_edge_iterator(this); }

Backward_edge_iterator LGraph::backward() const { return Backward_edge_iterator(this); }

void LGraph::dump() const {
  fmt::print("lgraph name:{} size:{}\n", name, node_internal.size());

  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    const auto &p = it.get_field();
    fmt::print("inp {} nid:{} pid:{}\n", it.get_name(), p.nid, p.pos);
  }
  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    const auto &p = it.get_field();
    fmt::print("out {} nid:{} pid:{}\n", it.get_name(), p.nid, p.pos);
  }

  dump_wirenames();

#if 1
  for (size_t i = 0; i < node_internal.size(); ++i) {
    fmt::print("{} ", i);
    node_internal[i].dump();
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

    entry.lg->each_sub_graph_fast([&entry, &pending](const Index_ID idx, const Lg_type_id lgid, std::string_view iname) {
      if (iname.empty()) return;
      LGraph *lg = LGraph::open(entry.top->get_path(), lgid);

      if (lg == 0) {
        Pass::error("hierarchy for {} could not open instance {} with lgid {}", entry.base, iname, lgid);
      } else {
        auto base2 = absl::StrCat(entry.base, ".", iname);
        pending.emplace_back(base2, entry.top, lg);
      }
    });

    entry.lg->close();
  }

  return hierarchy;
}

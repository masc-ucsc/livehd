//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <assert.h>
#include <sys/types.h>
#include <dirent.h>

#include <fstream>
#include <iostream>
#include <set>

#include "graph_library.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

uint32_t LGraph::lgraph_counter = 0;

// FIXME: name2lgraph functionality should be moved to graph_library
std::map<std::string, std::map<std::string, LGraph *>> LGraph::name2lgraph;

LGraph::LGraph(const std::string &path)
    : Lgraph_base_core(path, "lg" + std::to_string(lgraph_counter))
    , LGraph_Base(path, "lg" + std::to_string(lgraph_counter))
    , LGraph_Node_Delay(path, "lg" + std::to_string(lgraph_counter))
    , LGraph_Node_Src_Loc(path, "lg" + std::to_string(lgraph_counter))
    , LGraph_WireNames(path, "lg" + std::to_string(lgraph_counter))
    , LGraph_InstanceNames(path, "lg" + std::to_string(lgraph_counter))
    , LGraph_Node_Place(path, "lg" + std::to_string(lgraph_counter))
{

  library  = Graph_library::instance(path);
  tlibrary = Tech_library::instance(path);

  name2lgraph[path][name] = this;
  lgraph_counter++; // Only for unnamed graphs
  lgraph_id = library->get_id(std::to_string(lgraph_counter));

  clear();
}

LGraph::LGraph(const std::string &path, const std::string &_name, bool _clear)
    : Lgraph_base_core(path, _name)
    , LGraph_Base(path, _name)
    , LGraph_Node_Delay(path, _name)
    , LGraph_Node_Src_Loc(path, _name)
    , LGraph_WireNames(path, _name)
    , LGraph_InstanceNames(path, _name)
    , LGraph_Node_Place(path, _name)
{

  library  = Graph_library::instance(path);
  tlibrary = Tech_library::instance(path);

  name2lgraph[path][name] = this;
  lgraph_id = library->reset_id(_name); // May keep same ID

  if(_clear) {
    clear();
    sync();
  } else {
    reload();
  }
}

LGraph *LGraph::find_lgraph(const std::string &path, const std::string &name) {

  if(name2lgraph.find(path) == name2lgraph.end() || name2lgraph[path].find(name) == name2lgraph[path].end()) {
    if(Graph_library::instance(path)->include(name))
      return open_lgraph(path, name);

    if (!is_path_ok(path)) {
      console->warn("find_lgraph trying {} path which does not exit", path);
    }
    return 0;
  }

  return name2lgraph[path][name];
}

LGraph *LGraph::open_lgraph(const std::string &path, const std::string &name) {
  char cadena[4096];
  snprintf(cadena, 4096, "%s/lgraph_%s_nodes", path.c_str(), name.c_str());
  if(access(cadena, R_OK | W_OK) == -1) {
    return nullptr;
  }
  return new LGraph(path, name, false);
}

void LGraph::reload() {
  LGraph_Base::reload();
  LGraph_Node_Place::reload();
  LGraph_Node_Delay::reload();
  LGraph_Node_Src_Loc::reload();
  LGraph_WireNames::reload();
  LGraph_InstanceNames::reload();
}

void LGraph::clear() {
  LGraph_Node_Place::clear();
  LGraph_Node_Delay::clear();
  LGraph_Node_Src_Loc::clear();
  LGraph_WireNames::clear();
  LGraph_InstanceNames::clear();

  LGraph_Base::clear(); // last. Removes lock at the end
}

void LGraph::sync() {
  LGraph_Node_Place::sync();
  LGraph_Node_Delay::sync();
  LGraph_Node_Src_Loc::sync();
  LGraph_WireNames::sync();
  LGraph_InstanceNames::sync();

  if (locked)
    library->update(name);

  library->sync();
  tlibrary->sync();

  LGraph_Base::sync(); // last. Removes lock at the end
}

void LGraph::emplace_back() {
  LGraph_Base::emplace_back();
  LGraph_Node_Place::emplace_back();
  LGraph_Node_Delay::emplace_back();
  LGraph_Node_Src_Loc::emplace_back();
  LGraph_WireNames::emplace_back();
  LGraph_InstanceNames::emplace_back();
}

Index_ID LGraph::add_graph_input(const char *str, Index_ID nid, uint16_t bits, uint16_t offset) {

#if DEBUG
  assert(!is_graph_output(str));
  assert(!has_name(str));
#endif

  Index_ID idx = LGraph_Base::add_graph_input(str, nid, bits);
  LGraph_WireNames::set_offset(idx, offset);
  node_type_set(idx, GraphIO_Op);

  return idx;
}

Index_ID LGraph::add_graph_output(const char *str, Index_ID nid, uint16_t bits, uint16_t offset) {

#if DEBUG
  assert(!is_graph_input(str));
  assert(!has_name(str));
#endif

  Index_ID idx = LGraph_Base::add_graph_output(str, nid, bits);
  LGraph_WireNames::set_offset(idx, offset);
  node_type_set(idx, GraphIO_Op);

  return idx;
}

Node LGraph::create_node() {
  Index_ID nid = create_node_int();

  console->debug("create_node nid:{}", nid);

  return Node(this, nid);
}

Node LGraph::get_node(Index_ID nid) {
  return Node(this, nid);
}

const ConstNode LGraph::get_node(Index_ID nid) const {
  return ConstNode(this, nid);
}

Node LGraph::get_dest_node(const Edge &edge) {
  Index_ID idx = edge.get_self_idx();

  assert(is_root(idx)); // get_dest_node can only be called for root nodes

  return Node(this, idx);
}

ConstNode LGraph::get_dest_node(const Edge &edge) const {
  Index_ID idx = edge.get_self_idx();

  assert(is_root(idx)); // get_dest_node can only be called for root nodes

  return ConstNode(this, idx);
}

const std::string &LGraph::get_subgraph_name(Index_ID nid) const {
  assert(node_type_get(nid).op == SubGraph_Op);
  return library->get_name(subgraph_id_get(nid));
}

Index_ID LGraph::create_node_int() {

  get_lock(); // FIXME: change to Copy on Write permissions (mmap exception, and remap)
  emplace_back();

  if(node_internal.back().is_page_align()) {
    Node_Internal_Page *page = (Node_Internal_Page *)&node_internal.back();

    page->set_page(node_internal.size() - 1);
    emplace_back();
  }

  assert(node_internal[node_internal.size() - 1].get_out_pid() == 0);
  console->debug("create_node_int nid:{}", node_internal[node_internal.size() - 1].get_nid());

  return node_internal[node_internal.size() - 1].get_nid();
}

const Edge_iterator ConstNode::inp_edges() const {
  return g->inp_edges(nid);
}

const Edge_iterator ConstNode::out_edges() const {
  return g->out_edges(nid);
}

const Edge_iterator Node::inp_edges() const {
  return g->inp_edges(nid);
}

const Edge_iterator Node::out_edges() const {
  return g->out_edges(nid);
}

Forward_edge_iterator LGraph::forward() const {
  return Forward_edge_iterator(this);
}

Backward_edge_iterator LGraph::backward() const {
  return Backward_edge_iterator(this);
}

void LGraph::dump() const {
  fmt::print("lgraph name:{} size:{}\n", name, node_internal.size());

  for(const auto &ent : inputs2node) {
    fmt::print("input {} idx:{} pid:{}\n", ent.first, ent.second.nid, ent.second.pos);
  }
  for(const auto &ent : outputs2node) {
    fmt::print("output {} idx:{} pid:{}\n", ent.first, ent.second.nid, ent.second.pos);
  }

#if 1
  for(Index_ID i = 0; i < node_internal.size(); i++) {
    fmt::print("{} ", i);
    node_internal[i].dump();
  }
#endif
}

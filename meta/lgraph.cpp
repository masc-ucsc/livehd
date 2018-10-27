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

LGraph::LGraph(const std::string &path, const std::string &_name, bool _clear)
    : Lgraph_base_core(path, _name)
    , LGraph_Base(path, _name)
    , LGraph_Node_Delay(path, _name)
    , LGraph_Node_Src_Loc(path, _name)
    , LGraph_WireNames(path, _name)
    , LGraph_InstanceNames(path, _name)
    , LGraph_Node_Place(path, _name)
{
  lgraph_id = library->register_lgraph(name, this);

  if(_clear) {
    clear();
    sync();
  } else {
    reload();
  }
}

LGraph::~LGraph() {
  console->debug("lgraph destructor\n");
  library->unregister_lgraph(name, lgraph_id, this);
}

LGraph *LGraph::create(const std::string &path, const std::string &name) {
  LGraph *lg = Graph_library::try_find_lgraph(path,name);
  if (lg) {
    assert(Graph_library::instance(path));
    // Overwriting old lgraph. Delete old pointer (but better be sure that nobody has it)
    lg->close();
    bool done = Graph_library::instance(path)->expunge_lgraph(name, lg);
    if (done)
      delete lg;
  }

  return new LGraph(path, name, true);
}

LGraph *LGraph::open(const std::string &path, int lgid) {
  const std::string &name = Graph_library::instance(path)->get_name(lgid);

  return open(path,name);
}

LGraph *LGraph::open(const std::string &path, const std::string &name) {
  LGraph *lg = Graph_library::try_find_lgraph(path,name);
  if (lg) {
    assert(Graph_library::instance(path));
    Graph_library::instance(path)->register_lgraph(name, lg);
    return lg;
  }

  if (!Graph_library::instance(path)->include(name))
    return 0;

  return new LGraph(path, name, false);
}

void LGraph::close() {
	library->unregister_lgraph(name, lgraph_id, this);

  if (locked)
    library->update(lgraph_id);

	sync();
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

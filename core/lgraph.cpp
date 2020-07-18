//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph.hpp"

#include <dirent.h>
#include <sys/types.h>

#include <fstream>
#include <iostream>
#include <set>

#include "annotate.hpp"
#include "graph_library.hpp"
#include "lgedgeiter.hpp"

LGraph::LGraph(std::string_view _path, std::string_view _name, std::string_view _source)
    : LGraph_Base(_path, _name, Graph_library::instance(_path)->register_lgraph(_name, _source, this))
    , LGraph_Node_Type(_path, _name, get_lgid())
    , htree(this) {
  I(_name.find('/') == std::string::npos);  // No path in name

  I(_name == get_name());
}

LGraph::~LGraph() {
  sync();
  library->unregister(name, lgid, this);
}

bool LGraph::exists(std::string_view path, std::string_view name) { return Graph_library::try_find_lgraph(path, name) != nullptr; }

LGraph *LGraph::create(std::string_view path, std::string_view name, std::string_view source) {
  LGraph *lg = Graph_library::try_find_lgraph(path, name);
  if (lg == nullptr) {
    lg = new LGraph(path, name, source);
  }

  lg->clear();

  return lg;
}

LGraph *LGraph::clone_skeleton(std::string_view extended_name) {
  auto    lg_source = get_library().get_source(get_lgid());
  auto    lg_name   = absl::StrCat(get_name(), extended_name);
  LGraph *new_lg    = LGraph::create(get_path(), lg_name, lg_source);

  auto *new_sub = new_lg->ref_self_sub_node();
  new_sub->reset_pins();  // NOTE: it may have been created before. Clear to keep same order/attributes

  for (const auto &old_io_pin : get_self_sub_node().get_io_pins()) {
    new_sub->add_pin(old_io_pin.name, old_io_pin.dir, old_io_pin.graph_io_pos);
  }

  auto inp_node = Node(this, Hierarchy_tree::root_index(), Node::Hardcoded_input_nid);
  for (const auto &pin : inp_node.out_setup_pins()) {
    auto pos = get_self_sub_node().get_graph_pos_from_instance_pid(pin.get_pid());
    I(pin.is_graph_input());
    auto dpin = new_lg->add_graph_input(pin.get_name(), pos, pin.get_bits());
    I(dpin.get_pid() == pin.get_pid());  // WARNING: pins created in same order should match
  }

  auto out_node = Node(this, Hierarchy_tree::root_index(), Node::Hardcoded_output_nid);
  for (const auto &pin : out_node.out_setup_pins()) {
    auto pos = get_self_sub_node().get_graph_pos_from_instance_pid(pin.get_pid());
    I(pin.is_graph_output());
    auto dpin = new_lg->add_graph_output(pin.get_name(), pos, pin.get_bits());
    I(dpin.get_pid() == pin.get_pid());  // WARNING: pins created in same order should match
  }

  return new_lg;
}

LGraph *LGraph::open(std::string_view path, Lg_type_id lgid) {
  auto *lib = Graph_library::instance(path);
  if (unlikely(lib == nullptr))
    return nullptr;

  LGraph *lg = lib->try_find_lgraph(lgid);
  if (likely(lg != nullptr)) {
    return lg;
  }

  if (!lib->exists(lgid))
    return nullptr;

  auto name   = lib->get_name(lgid);
  auto source = lib->get_source(lgid);

  return new LGraph(path, name, source);
}

LGraph *LGraph::open(std::string_view path, std::string_view name) {
  LGraph *lg = Graph_library::try_find_lgraph(path, name);
  if (lg) {
    return lg;
  }

  auto *lib = Graph_library::instance(path);
  if (lib == nullptr)
    return nullptr;

  if (unlikely(!lib->has_name(name)))
    return nullptr;

  auto source = lib->get_source(name);

  return new LGraph(path, name, source);
}

void LGraph::rename(std::string_view path, std::string_view orig, std::string_view dest) {
  bool valid = Graph_library::instance(path)->rename_name(orig, dest);
  if (valid)
    warn("lgraph::rename find original graph {} in path {}", orig, path);
  else
    error("cannot find original graph {} in path {}", orig, path);
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

  htree.clear();

  std::fill(memoize_const_hint.begin(), memoize_const_hint.end(), 0);  // Not needed but neat
}

void LGraph::sync() {
  LGraph_Node_Type::sync();

  LGraph_Base::sync();  // last. Removes lock at the end
}

Node_pin LGraph::get_graph_input(std::string_view str) {
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this, Hierarchy_tree::root_index(), Node::Hardcoded_input_nid).setup_driver_pin(io_pid);
}

Node_pin LGraph::get_graph_output(std::string_view str) {
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this, Hierarchy_tree::root_index(), Node::Hardcoded_output_nid).setup_sink_pin(io_pid);
}

Node_pin LGraph::get_graph_output_driver_pin(std::string_view str) {
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this, Hierarchy_tree::root_index(), Node::Hardcoded_output_nid).setup_driver_pin(io_pid);
}

bool LGraph::is_graph_input(std::string_view io_name) const {
  bool alt = false;
#ifndef NDEBUG
  if (get_self_sub_node().has_pin(io_name)) {
    const auto &io_pin = get_self_sub_node().get_pin(io_name);
    alt                = io_pin.dir == Sub_node::Direction::Input;
  } else {
    alt = false;
  }
#endif

  auto       ref = Ann_node_pin_name::ref(this);
  const auto it  = ref->find_val(io_name);
  if (it == ref->end()) {
    return false;
  }
  auto compact = ref->get_key(it);
  auto dpin    = Node_pin(const_cast<LGraph *>(this), compact);
  bool cond    = dpin.get_node().get_nid() == Node::Hardcoded_input_nid;

  I(cond == alt);

  return cond;
}

bool LGraph::is_graph_output(std::string_view io_name) const {
  bool alt = false;
#ifndef NDEBUG
  if (get_self_sub_node().has_pin(io_name)) {
    const auto &io_pin = get_self_sub_node().get_pin(io_name);
    alt                = io_pin.dir == Sub_node::Direction::Output;
  } else {
    alt = false;
  }
#endif

  auto       ref = Ann_node_pin_name::ref(this);
  const auto it  = ref->find_val(io_name);
  if (it == ref->end()) {
    return false;
  }
  auto compact = ref->get_key(it);
  auto dpin    = Node_pin(const_cast<LGraph *>(this), compact);
  bool cond    = dpin.get_node().get_nid() == Node::Hardcoded_output_nid;

  I(cond == alt);

  return cond;
}

Node_pin LGraph::add_graph_input(std::string_view str, Port_ID pos, uint32_t bits) {
  I(!is_graph_output(str));

  Port_ID inst_pid;
  if (get_self_sub_node().has_pin(str)) {
    inst_pid = ref_self_sub_node()->map_graph_pos(str, Sub_node::Direction::Input, pos);  // reset pin stats
  } else {
    inst_pid = ref_self_sub_node()->add_pin(str, Sub_node::Direction::Input, pos);
  }
  I(node_internal[Node::Hardcoded_input_nid].get_type() == GraphIO_Op);

  I(!find_idx_from_pid(Node::Hardcoded_input_nid, inst_pid));  // Just added, so it should not be there
  Index_ID root_idx = 0;
  auto     idx      = get_space_output_pin(Node::Hardcoded_input_nid, inst_pid, root_idx);

  // auto idx = setup_idx_from_pid(Node::Hardcoded_input_nid, inst_pid);
  setup_driver(idx);  // Just driver, no sink

  Node_pin pin(this, this, Hierarchy_tree::root_index(), idx, inst_pid, false);

  pin.set_name(str);
  pin.set_bits(bits);

  return pin;
}

Node_pin LGraph::add_graph_output(std::string_view str, Port_ID pos, uint32_t bits) {
  I(!is_graph_input(str));

  Port_ID inst_pid;
  if (get_self_sub_node().has_pin(str)) {
    inst_pid = ref_self_sub_node()->map_graph_pos(str, Sub_node::Direction::Output, pos);  // reset pin stats
  } else {
    inst_pid = ref_self_sub_node()->add_pin(str, Sub_node::Direction::Output, pos);
  }
  I(node_internal[Node::Hardcoded_output_nid].get_type() == GraphIO_Op);

  I(!find_idx_from_pid(Node::Hardcoded_output_nid, inst_pid));  // Just added, so it should not be there
  Index_ID root_idx = 0;
  auto     idx      = get_space_output_pin(Node::Hardcoded_output_nid, inst_pid, root_idx);
  // auto idx = setup_idx_from_pid(Node::Hardcoded_output_nid, inst_pid);
  setup_sink(idx);
  setup_driver(idx);  // outputs can also drive internal nodes. So both sink/driver

  Node_pin dpin(this, this, Hierarchy_tree::root_index(), idx, inst_pid, false);
  dpin.set_name(str);
  dpin.set_bits(bits);

  return Node_pin(this, this, Hierarchy_tree::root_index(), idx, inst_pid, true);
}

Node_pin_iterator LGraph::out_connected_pins(const Node &node) const {
  I(node.get_class_lgraph() == this);
  Node_pin_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[idx2].is_master_root());

  absl::flat_hash_set<uint32_t> visited;

  while (true) {
    auto n = node_internal[idx2].get_num_local_outputs();
    if (n > 0) {
      if (node_internal[idx2].is_root()) {
        xiter.emplace_back(Node_pin(node.get_top_lgraph(),
                                    node.get_class_lgraph(),
                                    node.get_hidx(),
                                    idx2,
                                    node_internal[idx2].get_dst_pid(),
                                    false));
        visited.insert(node_internal[idx2].get_dst_pid());
      } else {
        if (visited.find(node_internal[idx2].get_dst_pid()) == visited.end()) {
          auto master_nid = node_internal[idx2].get_nid();
          xiter.emplace_back(Node_pin(node.get_top_lgraph(),
                                      node.get_class_lgraph(),
                                      node.get_hidx(),
                                      master_nid,
                                      node_internal[idx2].get_dst_pid(),
                                      false));
        }
      }
    }

    if (node_internal[idx2].is_last_state())
      break;

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
    if (n > 0) {
      if (node_internal[idx2].is_root()) {
        xiter.emplace_back(Node_pin(node.get_top_lgraph(),
                                    node.get_class_lgraph(),
                                    node.get_hidx(),
                                    idx2,
                                    node_internal[idx2].get_dst_pid(),
                                    true));
        visited.insert(node_internal[idx2].get_dst_pid());
      } else {
        if (visited.find(node_internal[idx2].get_dst_pid()) == visited.end()) {
          auto master_nid = node_internal[idx2].get_nid();
          xiter.emplace_back(Node_pin(node.get_top_lgraph(),
                                      node.get_class_lgraph(),
                                      node.get_hidx(),
                                      master_nid,
                                      node_internal[idx2].get_dst_pid(),
                                      true));
        }
      }
    }

    if (node_internal[idx2].is_last_state())
      break;

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
    if (node_internal[idx2].is_root() && node_internal[idx2].is_driver_setup())
      xiter.emplace_back(Node_pin(node.get_top_lgraph(),
                                  node.get_class_lgraph(),
                                  node.get_hidx(),
                                  idx2,
                                  node_internal[idx2].get_dst_pid(),
                                  false));

    if (node_internal[idx2].is_last_state())
      break;

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
    if (node_internal[idx2].is_root() && node_internal[idx2].is_sink_setup())
      xiter.emplace_back(
          Node_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), idx2, node_internal[idx2].get_dst_pid(), true));

    if (node_internal[idx2].is_last_state())
      break;

    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}

bool LGraph::has_edge(const Node_pin &driver, const Node_pin &sink) const {
  I(driver.get_class_lgraph() == this);
  I(sink.get_class_lgraph() == this);

  Index_ID idx2 = driver.get_node().get_nid();
  I(node_internal[idx2].is_master_root());

  while (true) {
    if (node_internal[idx2].get_dst_pid() == driver.get_pid()) {
      auto            n = node_internal[idx2].get_num_local_outputs();
      uint8_t         i;
      const Edge_raw *redge;
      for (i = 0, redge = node_internal[idx2].get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(!redge->is_input());

        if (redge->get_idx() == sink.get_idx() && redge->get_inp_pid() == sink.get_pid())
          return true;
      }
    }
    if (node_internal[idx2].is_last_state())
      break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return false;
}

Node_pin_iterator LGraph::inp_drivers(const Node &node, const absl::flat_hash_set<Node::Compact> &exclude) const {
  I(node.get_class_lgraph() == this);
  Node_pin_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[node.get_nid()].is_master_root());

  while (true) {
    auto n = node_internal[idx2].get_num_local_inputs();

    if (n) {
      uint8_t         i;
      const Edge_raw *redge;

      for (i = 0, redge = node_internal[idx2].get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(redge->is_input());
        auto driver_pin_idx    = redge->get_idx();
        auto driver_pin_pid    = redge->get_inp_pid();
        auto driver_master_nid = node_internal[driver_pin_idx].get_nid();
        I(node_internal[driver_master_nid].is_master_root());

        if (exclude.count(Node::Compact(node.get_hidx(), driver_master_nid)))
          continue;

        xiter.emplace_back(
            Node_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), driver_pin_idx, driver_pin_pid, false));
        I(xiter.back() == redge->get_out_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx()));
      }
    }

    if (node_internal[idx2].is_last_state())
      break;
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

  Index_ID master_idx = idx2;
  while (true) {
    auto n = node_internal[idx2].get_num_local_outputs();

    if (n) {
      uint8_t         i;
      const Edge_raw *redge;
      Node_pin        dpin(node.get_top_lgraph(),
                    node.get_class_lgraph(),
                    node.get_hidx(),
                    master_idx,
                    node_internal[idx2].get_dst_pid(),
                    false);

      for (i = 0, redge = node_internal[idx2].get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(dpin == redge->get_out_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx()));
        xiter.emplace_back(dpin, redge->get_inp_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx()));
      }
    }
    if (node_internal[idx2].is_last_state())
      break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
    if (node_internal[idx2].is_root())
      master_idx = idx2;
    else
      master_idx = node_internal[idx2].get_nid();
  }

  return xiter;
}

XEdge_iterator LGraph::inp_edges(const Node &node) const {
  I(node.get_class_lgraph() == this);
  XEdge_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[node.get_nid()].is_master_root());

  Index_ID master_idx = idx2;
  while (true) {
    auto n = node_internal[idx2].get_num_local_inputs();

    if (n) {
      uint8_t         i;
      const Edge_raw *redge;
      Node_pin        spin(node.get_top_lgraph(),
                    node.get_class_lgraph(),
                    node.get_hidx(),
                    master_idx,
                    node_internal[idx2].get_dst_pid(),
                    true);

      for (i = 0, redge = node_internal[idx2].get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(spin == redge->get_inp_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx()));
        xiter.emplace_back(redge->get_out_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx()), spin);
      }
    }
    if (node_internal[idx2].is_last_state())
      break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
    if (node_internal[idx2].is_root())
      master_idx = idx2;
    else
      master_idx = node_internal[idx2].get_nid();
  }

  return xiter;
}

XEdge_iterator LGraph::inp_edges_ordered(const Node &node) const {
  auto iter = inp_edges(node);

  std::sort(iter.begin(), iter.end(), [](const XEdge &a, const XEdge &b) -> bool { return a.sink.get_pid() < b.sink.get_pid(); });

  return iter;
}

XEdge_iterator LGraph::out_edges_ordered(const Node &node) const {
  auto iter = out_edges(node);

  std::sort(iter.begin(), iter.end(), [](const XEdge &a, const XEdge &b) -> bool {
    return a.driver.get_pid() < b.driver.get_pid();
  });

  return iter;
}

XEdge_iterator LGraph::inp_edges_ordered_reverse(const Node &node) const {
  auto iter = inp_edges(node);

  std::sort(iter.begin(), iter.end(), [](const XEdge &a, const XEdge &b) -> bool { return a.sink.get_pid() > b.sink.get_pid(); });

  return iter;
}

XEdge_iterator LGraph::out_edges_ordered_reverse(const Node &node) const {
  auto iter = out_edges(node);

  std::sort(iter.begin(), iter.end(), [](const XEdge &a, const XEdge &b) -> bool {
    return a.driver.get_pid() > b.driver.get_pid();
  });

  return iter;
}

XEdge_iterator LGraph::out_edges(const Node_pin &pin) const {
  I(pin.get_class_lgraph() == this);
  XEdge_iterator xiter;

  Index_ID idx2 = pin.get_idx();
  I(node_internal[idx2].is_root());

  while (true) {
    auto            n = node_internal[idx2].get_num_local_outputs();
    uint8_t         i;
    const Edge_raw *redge;
    if (pin.get_pid() == node_internal[idx2].get_dst_pid()) {  // Only add edges with same source
      for (i = 0, redge = node_internal[idx2].get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        xiter.emplace_back(pin, redge->get_inp_pin(pin.get_top_lgraph(), pin.get_class_lgraph(), pin.get_hidx()));
      }
    }
    if (node_internal[idx2].is_last_state())
      break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}

XEdge_iterator LGraph::inp_edges(const Node_pin &pin) const {
  I(pin.get_class_lgraph() == this);
  XEdge_iterator xiter;

  Index_ID idx2 = pin.get_idx();
  I(node_internal[idx2].is_root());

  while (true) {
    auto            n = node_internal[idx2].get_num_local_inputs();
    uint8_t         i;
    const Edge_raw *redge;
    if (pin.get_pid() == node_internal[idx2].get_dst_pid()) {  // Only add edges with same source
      for (i = 0, redge = node_internal[idx2].get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        xiter.emplace_back(redge->get_out_pin(pin.get_top_lgraph(), pin.get_class_lgraph(), pin.get_hidx()), pin);
      }
    }
    if (node_internal[idx2].is_last_state())
      break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  return xiter;
}

void LGraph::del_node(const Node &node) {
  auto idx2 = node.get_nid();

  auto op = node_internal[idx2].get_type();

  if (op == Const_Op) {
    const_map.erase(node.get_compact_class());
  } else if (op == GraphIO_Op) {
    I(false);  // add the case once we have a testing case
  } else if (op == LUT_Op) {
    lut_map.erase(node.get_compact_class());
  } else if (op == SubGraph_Op) {
    subid_map.erase(node.get_compact_class());
  }

  // In hierarchy, not allowed to remove nodes (mark as deleted attribute?)
  I(node.get_class_lgraph() == node.get_top_lgraph());

  while (true) {
    auto *node_int_ptr = node_internal.ref(idx2);

    {
      absl::flat_hash_set<uint32_t> deleted;

      auto            n = node_int_ptr->get_num_local_inputs();
      int             i;
      const Edge_raw *redge = nullptr;
      for (i = 0, redge = node_int_ptr->get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(redge->is_input());

        auto other_nid = node_internal[redge->get_idx()].get_nid();
        if (deleted.count(other_nid))
          continue;
        deleted.insert(other_nid);

        Node other_driver(this, this, Hierarchy_tree::root_index(), other_nid);
        del_driver2node_int(other_driver, node);
      }
    }

    {
      absl::flat_hash_set<uint32_t> deleted;

      auto            n = node_int_ptr->get_num_local_outputs();
      uint8_t         i;
      const Edge_raw *redge = nullptr;
      for (i = 0, redge = node_int_ptr->get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(!redge->is_input());

        auto other_nid = node_internal[redge->get_idx()].get_nid();
        if (deleted.count(other_nid))
          continue;
        deleted.insert(other_nid);

        Node other_sink(this, this, Hierarchy_tree::root_index(), other_nid);
        del_sink2node_int(node, other_sink);
      }
    }

    if (node_int_ptr->is_last_state()) {
      break;
    }
    idx2 = node_int_ptr->get_next();
  }

  idx2 = node.get_nid();
  while (true) {
    auto *node_int_ptr = node_internal.ref(idx2);
    if (node_int_ptr->is_last_state()) {
      node_int_ptr->try_recycle();
      return;
    }
    idx2 = node_int_ptr->get_next();
    node_int_ptr->try_recycle();
  }
}

// sink node has been deleted. Anything in driver pointing to sink should be deleted
void LGraph::del_driver2node_int(Node &driver, const Node &sink) {
  // In hierarchy, not allowed to remove nodes (mark as deleted attribute?)
  I(driver.get_class_lgraph() == driver.get_top_lgraph());
  I(sink.get_class_lgraph() == sink.get_top_lgraph());
  I(sink.get_class_lgraph() == driver.get_top_lgraph());

  Index_ID idx2 = driver.get_nid();

  while (true) {
    auto *node_int_ptr = node_internal.ref(idx2);

    auto            n = node_int_ptr->get_num_local_outputs();
    uint8_t         i;
    const Edge_raw *redge;
    for (i = 0, redge = node_int_ptr->get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      auto master_nid = node_internal[redge->get_idx()].get_nid();
      if (master_nid == sink.get_nid()) {
        node_int_ptr->del_output_int(redge);
      }
      // NOTE: unlike inputs, the outputs are added to the end, no copy movement (advance pointer) at delete
    }
    if (node_int_ptr->is_last_state())
      return;

    idx2 = node_internal[idx2].get_next();
  }
}

void LGraph::del_sink2node_int(const Node &driver, Node &sink) {
  // In hierarchy, not allowed to remove nodes (mark as deleted attribute?)
  I(driver.get_class_lgraph() == driver.get_top_lgraph());
  I(sink.get_class_lgraph() == sink.get_top_lgraph());
  I(sink.get_class_lgraph() == driver.get_top_lgraph());

  Index_ID idx2 = sink.get_nid();

  while (true) {
    auto *node_int_ptr = node_internal.ref(idx2);

    auto            n = node_int_ptr->get_num_local_inputs();
    uint8_t         i;
    const Edge_raw *redge;
    for (i = 0, redge = node_int_ptr->get_input_begin(); i < n; i++) {
      I(redge->get_self_idx() == idx2);
      auto master_nid = node_internal[redge->get_idx()].get_nid();
      if (master_nid == driver.get_nid()) {
        node_int_ptr->del_input_int(redge);
      } else {
        redge += redge->next_node_inc();  // NOTE: delete copies data, sort of advances the pointer
      }
    }
    if (node_int_ptr->is_last_state())
      return;

    idx2 = node_internal[idx2].get_next();
  }
}

bool LGraph::del_edge_driver_int(const Node_pin &dpin, const Node_pin &spin) {
  // WARNING: The edge can be anywhere from get_node().nid to end BUT more
  // likely to find it early starting from idx. Start from idx, and go back to
  // start (nid) again once at the end. If idx again, then it is not anywhere.

  // In hierarchy, not allowed to remove nodes (mark as deleted attribute?)
  I(dpin.get_class_lgraph() == dpin.get_top_lgraph());
  I(spin.get_class_lgraph() == spin.get_top_lgraph());
  I(spin.get_class_lgraph() == dpin.get_top_lgraph());

  Index_ID idx2 = dpin.get_idx();
  I(node_internal[idx2].is_root());

  while (true) {
    auto *node_int_ptr = node_internal.ref(idx2);

    I(node_int_ptr->get_dst_pid() == dpin.get_pid());

    auto            n = node_int_ptr->get_num_local_outputs();
    uint8_t         i;
    const Edge_raw *redge;
    for (i = 0, redge = node_int_ptr->get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      if (redge->get_idx() == spin.get_idx() && redge->get_inp_pid() == spin.get_pid()) {
        node_int_ptr->del_output_int(redge);
        return true;
      }
    }
    do {
      // Just look for next idx2 with same pid
      if (node_int_ptr->is_last_state()) {
        idx2 = dpin.get_node().get_nid();
      }
      Index_ID tmp = node_internal[idx2].get_next();
      if (tmp == dpin.get_idx()) {
        return false;
      }
      I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
      idx2 = tmp;
    } while (node_internal[idx2].get_dst_pid() != dpin.get_pid());
  }

  return false;
}

bool LGraph::del_edge_sink_int(const Node_pin &dpin, const Node_pin &spin) {
  // WARNING: The edge can be anywhere from get_node().nid to end BUT more
  // likely to find it early starting from idx. Start from idx, and go back to
  // start (nid) again once at the end. If idx again, then it is not anywhere.

  I(dpin.get_class_lgraph() == dpin.get_top_lgraph());
  I(spin.get_class_lgraph() == spin.get_top_lgraph());
  I(spin.get_class_lgraph() == dpin.get_top_lgraph());

  Index_ID idx2 = spin.get_idx();
  I(node_internal[idx2].is_root());

  while (true) {
    auto *node_int_ptr = node_internal.ref(idx2);

    I(node_int_ptr->get_dst_pid() == spin.get_pid());

    auto            n = node_int_ptr->get_num_local_inputs();
    uint8_t         i;
    const Edge_raw *redge;
    for (i = 0, redge = node_int_ptr->get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      if (redge->get_idx() == dpin.get_idx() && redge->get_inp_pid() == dpin.get_pid()) {
        node_int_ptr->del_input_int(redge);
        return true;
      }
    }
    do {
      // Just look for next idx2 with same pid
      if (node_internal[idx2].is_last_state()) {
        idx2 = spin.get_node().get_nid();
      }
      Index_ID tmp = node_internal[idx2].get_next();
      if (tmp == spin.get_idx()) {
        return false;
      }
      I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
      idx2 = tmp;
    } while (node_internal[idx2].get_dst_pid() != spin.get_pid());
  }

  return false;
}

bool LGraph::del_edge(const Node_pin &dpin, const Node_pin &spin) {
  I(dpin.is_driver());
  I(spin.is_sink());

  bool found = del_edge_driver_int(dpin, spin);
  if (!found)
    return false;

  found = del_edge_sink_int(dpin, spin);
  I(found);

  return true;
}

Node LGraph::create_node() {
  Index_ID nid = create_node_int();
  return Node(this, Hierarchy_tree::root_index(), nid);
}

Node LGraph::create_node(const Node &old_node) {
  // TODO: We can just copy the node_type_table AND update the tracking (graphio, consts)

  Node new_node;

  Node_Type_Op op = old_node.get_type().op;

  if (op == LUT_Op) {
    new_node = create_node();
    new_node.set_type_lut(old_node.get_type_lut());
  } else if (op == SubGraph_Op) {
    new_node = create_node_sub(old_node.get_type_sub());
  } else if (op == Const_Op) {
    new_node = create_node_const(old_node.get_type_const());
    I(new_node.get_driver_pin().get_bits() == old_node.get_driver_pin().get_bits());
  } else {
    I(op != GraphIO_Op);  // Special case, must use add input/output API
    new_node = create_node(op);
  }

  for (auto old_dpin : old_node.out_setup_pins()) {
    auto new_dpin = new_node.setup_driver_pin(old_dpin.get_pid());
    new_dpin.set_bits(old_dpin.get_bits());
  }

  return new_node;
}

Node LGraph::create_node(Node_Type_Op op) {
  Index_ID nid = create_node_int();
  set_type(nid, op);

  I(op != GraphIO_Op);   // Special case, must use add input/output API
  I(op != SubGraph_Op);  // Do not build by steps. call create_node_sub

  return Node(this, Hierarchy_tree::root_index(), nid);
}

Node LGraph::create_node(Node_Type_Op op, uint32_t bits) {
  auto node = create_node(op);

  node.setup_driver_pin(0).set_bits(bits);

  return node;
}

Node LGraph::create_node_const(const Lconst &value) {
  Index_ID nid = memoize_const_hint[value.hash() % memoize_const_hint.size()];
  if (nid == 0 || nid >= node_internal.size() || !node_internal[nid].is_valid() || node_internal[nid].get_type() != Const_Op
      || get_type_const(nid) != value) {
    nid = create_node_int();
    set_type_const(nid, value);
    memoize_const_hint[value.hash() % memoize_const_hint.size()] = nid;
  }

  I(node_internal[nid].get_dst_pid() == 0);
  I(node_internal[nid].is_master_root());

  return Node(this, Hierarchy_tree::root_index(), nid);
}

Node LGraph::create_node_sub(Lg_type_id sub_id) {
  I(get_lgid() != sub_id);  // It can not point to itself (in fact, no recursion of any type)

  auto nid = create_node().get_nid();
  set_type_sub(nid, sub_id);

  return Node(this, Hierarchy_tree::root_index(), nid);
}

Node LGraph::create_node_sub(std::string_view sub_name) {
  I(name != sub_name);  // It can not point to itself (in fact, no recursion of any type)

  auto  nid = create_node().get_nid();
  auto &sub = library->setup_sub(sub_name);
  set_type_sub(nid, sub.get_lgid());

  return Node(this, Hierarchy_tree::root_index(), nid);
}

const Sub_node &LGraph::get_self_sub_node() const { return library->get_sub(get_lgid()); }

Sub_node *LGraph::ref_self_sub_node() { return library->ref_sub(get_lgid()); }

Fwd_edge_iterator LGraph::forward(bool visit_sub) { return Fwd_edge_iterator(this, visit_sub); }
Bwd_edge_iterator LGraph::backward(bool visit_sub) { return Bwd_edge_iterator(this, visit_sub); }

// Skip after 1, but first may be deleted, so fast_next
Fast_edge_iterator LGraph::fast(bool visit_sub) { return Fast_edge_iterator(this, visit_sub); }

void LGraph::dump() {
  fmt::print("lgraph name:{} size:{}\n", name, node_internal.size());

  for (const auto &io_pin : get_self_sub_node().get_io_pins()) {
    fmt::print("io {} pos:{} pid:{} {}\n",
               io_pin.name,
               io_pin.graph_io_pos,
               get_self_sub_node().get_instance_pid(io_pin.name),
               io_pin.dir == Sub_node::Direction::Input ? "input" : "output");
  }

#if 1
  for (size_t i = 0; i < node_internal.size(); ++i) {
    if (!node_internal[i].is_node_state())
      continue;
    if (!node_internal[i].is_master_root())
      continue;
    auto node = Node(this, Node::Compact_class(i));  // NOTE: To remove once new iterators are finished

    if (!node.has_inputs() && !node.has_outputs())
      continue;

    node.dump();
  }
#endif

  each_sub_fast([this](Node &node, Lg_type_id lgid) {
    LGraph *child = LGraph::open(get_path(), node.get_type_sub());

    fmt::print("node:{} lgid:{} sub:{}\n", node.debug_name(), lgid, child->get_name());
  });
}

void LGraph::dump_down_nodes() {
  for (auto &cnode : subid_map) {
    fmt::print(" sub:{}\n", cnode.first.get_node(this).debug_name());
  }
}

Node LGraph::get_graph_input_node() { return Node(this, Hierarchy_tree::root_index(), Node::Hardcoded_input_nid); }

Node LGraph::get_graph_output_node() { return Node(this, Hierarchy_tree::root_index(), Node::Hardcoded_output_nid); }

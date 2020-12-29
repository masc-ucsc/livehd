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

LGraph *LGraph::clone_skeleton(std::string_view new_lg_name) {
  std::string  lg_source{get_library().get_source(get_lgid())}; // string, create can free it
  auto    lg_name   = absl::StrCat(new_lg_name);
  LGraph *new_lg    = LGraph::create(get_path(), lg_name, lg_source);

  auto *new_sub = new_lg->ref_self_sub_node();
  new_sub->reset_pins();  // NOTE: it may have been created before. Clear to keep same order/attributes

  for (const auto *old_io_pin : get_self_sub_node().get_io_pins()) {
    if (old_io_pin->is_input()) {
      auto old_dpin = get_graph_input(old_io_pin->name);
      new_lg->add_graph_input(old_io_pin->name, old_io_pin->graph_io_pos, old_dpin.get_bits());
    } else {
      auto old_spin = get_graph_output(old_io_pin->name);
      new_lg->add_graph_output(old_io_pin->name, old_io_pin->graph_io_pos, old_spin.get_driver_pin().get_bits());
    }
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
  std::string source{lib->get_source(lgid)};

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

  std::string source{lib->get_source(name)};

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

  I(nid1 == Hardcoded_input_nid);
  I(nid2 == Hardcoded_output_nid);

  set_type(nid1, Ntype_op::IO);
  set_type(nid2, Ntype_op::IO);

  htree.clear();

  std::fill(memoize_const_hint.begin(), memoize_const_hint.end(), 0);  // Not needed but neat
}

void LGraph::sync() {
  Ann_support::sync(this);

  LGraph_Node_Type::sync();

  LGraph_Base::sync();  // last. Removes lock at the end
}

Node_pin LGraph::get_graph_input(std::string_view str) {
  I(get_self_sub_node().is_input(str)); // The input does not exist, do not call get_input
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this, Hierarchy_tree::root_index(), Hardcoded_input_nid).setup_driver_pin_raw(io_pid);
}

Node_pin LGraph::get_graph_output(std::string_view str) {
  I(get_self_sub_node().is_output(str)); // The output does not exist, do not call get_output
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this, Hierarchy_tree::root_index(), Hardcoded_output_nid).setup_sink_pin_raw(io_pid);
}

Node_pin LGraph::get_graph_output_driver_pin(std::string_view str) {
  I(get_self_sub_node().is_output(str)); // The output does not exist, do not call get_output
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this, Hierarchy_tree::root_index(), Hardcoded_output_nid).setup_driver_pin_raw(io_pid);
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
  bool cond    = dpin.get_node().get_nid() == Hardcoded_input_nid;

  I(cond == alt);

  return cond;
}

bool LGraph::is_graph_output(std::string_view io_name) const {
  if (!get_self_sub_node().is_output(io_name)) 
    return false;
 
  auto inst_pid = get_self_sub_node().get_instance_pid(io_name);  
  auto idx = find_idx_from_pid(Hardcoded_output_nid, inst_pid);
  return (idx != 0);
}

Node_pin LGraph::add_graph_input(std::string_view str, Port_ID pos, uint32_t bits) {
  I(!is_graph_output(str));

  Port_ID inst_pid;
  if (get_self_sub_node().has_pin(str)) {
    inst_pid = ref_self_sub_node()->map_graph_pos(str, Sub_node::Direction::Input, pos);  // reset pin stats
  } else {
    inst_pid = ref_self_sub_node()->add_pin(str, Sub_node::Direction::Input, pos);
  }
  I(node_internal[Hardcoded_input_nid].get_type() == Ntype_op::IO);

  Index_ID root_idx = 0;
  auto idx = find_idx_from_pid(Hardcoded_input_nid, inst_pid);
  if (idx==0)
    idx = get_space_output_pin(Hardcoded_input_nid, inst_pid, root_idx);

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
  I(node_internal[Hardcoded_output_nid].get_type() == Ntype_op::IO);

  Index_ID root_idx = 0;
  auto idx = find_idx_from_pid(Hardcoded_output_nid, inst_pid);
  if (idx==0)
    idx = get_space_output_pin(Hardcoded_output_nid, inst_pid, root_idx);

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

  absl::flat_hash_set<Port_ID> xiter_set;

  auto pid = node_internal[idx2].get_dst_pid();
  while (true) {
    I(!xiter_set.contains(pid));
    auto n = node_internal[idx2].get_num_local_outputs();
    if (n > 0) {
      auto root_idx = idx2;
      if (!node_internal[idx2].is_root())
        root_idx = node_internal[idx2].get_nid();

      xiter.emplace_back(Node_pin(node.get_top_lgraph(),
            node.get_class_lgraph(),
            node.get_hidx(),
            root_idx,
            pid,
            false));

      xiter_set.insert(pid);
    }

    do {
      if (node_internal[idx2].is_last_state())
        return xiter;

      Index_ID tmp = node_internal[idx2].get_next();
      I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
      idx2 = tmp;
      pid = node_internal[idx2].get_dst_pid();
    }while(xiter_set.contains(pid));
  }

  return xiter;
}

Node_pin_iterator LGraph::inp_connected_pins(const Node &node) const {
  I(node.get_class_lgraph() == this);
  Node_pin_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[idx2].is_master_root());
  absl::flat_hash_set<Port_ID> xiter_set;

  auto pid = node_internal[idx2].get_dst_pid();
  while (true) {
    I(!xiter_set.contains(pid));
    auto n = node_internal[idx2].get_num_local_inputs();
    if (n > 0) {
      auto root_idx = idx2;
      if (!node_internal[idx2].is_root())
        root_idx = node_internal[idx2].get_nid();

      xiter.emplace_back(Node_pin(node.get_top_lgraph(),
            node.get_class_lgraph(),
            node.get_hidx(),
            root_idx,
            pid,
            true));
      xiter_set.insert(pid);
    }

    do {
      if (node_internal[idx2].is_last_state())
        return xiter;

      Index_ID tmp = node_internal[idx2].get_next();
      I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
      idx2 = tmp;
      pid = node_internal[idx2].get_dst_pid();
    }while(xiter_set.contains(pid));
  }

  return xiter;
}

Node_pin_iterator LGraph::inp_drivers(const Node &node, const absl::flat_hash_set<Node::Compact> &restrict_to) const {
  I(node.get_class_lgraph() == this);

  Node_pin_iterator xiter;

  Index_ID idx2 = node.get_nid();
  I(node_internal[node.get_nid()].is_master_root());

  const bool hier = node.is_hierarchical();

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
        I(node_internal[driver_pin_idx].get_dst_pid() == driver_pin_pid);
        auto driver_master_nid = node_internal[driver_pin_idx].get_nid();
        I(node_internal[driver_master_nid].is_master_root());

//        if (!restrict_to.contains(Node::Compact(node.get_hidx(), driver_master_nid)))
//          continue;

        Node_pin dpin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), driver_pin_idx, driver_pin_pid, false);

        if (hier) {
          trace_back2driver(xiter, dpin);
        } else {
          xiter.emplace_back(dpin);
          I(xiter.back() == redge->get_out_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), idx2));
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

XEdge_iterator LGraph::out_edges(const Node &node) const {
  I(node.get_class_lgraph() == this);

  XEdge_iterator xiter;

  const bool hier = node.is_hierarchical();
  if (hier && node.is_graph_output()) {

    for(auto out_spin:node.inp_connected_pins()) {
      for(auto e:out_spin.inp_edges()) {
        trace_forward2sink(xiter, e.driver, out_spin);
      }
    }

    return xiter;
  }

  Index_ID idx2 = node.get_nid();
  I(node_internal[node.get_nid()].is_master_root());

  while (true) {
    auto n = node_internal[idx2].get_num_local_outputs();

    if (n) {
      uint8_t         i;
      const Edge_raw *redge;
      Node_pin        dpin(node.get_top_lgraph(),
                            node.get_class_lgraph(),
                            node.get_hidx(),
                            idx2,
                            node_internal[idx2].get_dst_pid(),
                            false);

      I(hier == dpin.is_hierarchical());

      if (hier) {
        for (i = 0, redge = node_internal[idx2].get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
          I(redge->get_self_idx() == idx2);
          I(dpin == redge->get_out_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), idx2));
          auto spin = redge->get_inp_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), idx2);
          trace_forward2sink(xiter, dpin, spin);
        }
      }else{
        for (i = 0, redge = node_internal[idx2].get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
          auto spin = redge->get_inp_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), idx2);
          xiter.emplace_back(dpin, spin);
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

XEdge_iterator LGraph::inp_edges(const Node &node) const {
  I(node.get_class_lgraph() == this);

  XEdge_iterator xiter;

  const bool hier = node.is_hierarchical();
  if (hier && node.is_graph_input()) {

    for(auto inp_dpin:node.out_connected_pins()) {
      Node_pin_iterator piter;
      trace_back2driver(piter, inp_dpin);

      for(auto out_spin:inp_dpin.out_edges()) {
        for (auto &dpin2 : piter) {
          xiter.emplace_back(dpin2, out_spin.sink);
        }
      }
    }

    return xiter;
  }

  Index_ID idx2 = node.get_nid();
  I(node_internal[node.get_nid()].is_master_root());

  while (true) {
    auto n = node_internal[idx2].get_num_local_inputs();

    if (n) {
      uint8_t         i;
      const Edge_raw *redge;
      Node_pin        spin(node.get_top_lgraph(),
                    node.get_class_lgraph(),
                    node.get_hidx(),
                    idx2,
                    node_internal[idx2].get_dst_pid(),
                    true);

      for (i = 0, redge = node_internal[idx2].get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
        auto dpin = redge->get_out_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), idx2);

        if (hier) {
          Node_pin_iterator piter;
          trace_back2driver(piter, dpin);
          for (auto &dpin2 : piter) {
            xiter.emplace_back(dpin2, spin);
          }
        } else {
          xiter.emplace_back(dpin, spin);
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

XEdge_iterator LGraph::out_edges(const Node_pin &dpin) const {
  I(dpin.is_driver());
  I(dpin.get_class_lgraph() == this);

  XEdge_iterator xiter;
  each_pin(dpin, [this, &xiter, &dpin](Index_ID idx2) {

    auto            n = node_internal[idx2].get_num_local_outputs();
    uint8_t         i;
    const Edge_raw *redge;

    for (i = 0, redge = node_internal[idx2].get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      auto spin = redge->get_inp_pin(dpin.get_top_lgraph(), dpin.get_class_lgraph(), dpin.get_hidx(), idx2);
      if (dpin.is_hierarchical()) {
        trace_forward2sink(xiter, dpin, spin);
      } else {
        xiter.emplace_back(dpin, spin);
      }
    }

    return true; // continue the iterations
  });

  return xiter;
}

XEdge_iterator LGraph::inp_edges(const Node_pin &spin) const {
  I(spin.is_sink() || spin.is_graph_input());
  I(spin.get_class_lgraph() == this);

  XEdge_iterator xiter;
  each_pin(spin, [this, &xiter, spin](Index_ID idx2) {
    auto            n = node_internal[idx2].get_num_local_inputs();
    uint8_t         i;
    const Edge_raw *redge;
    for (i = 0, redge = node_internal[idx2].get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      auto dpin = redge->get_out_pin(spin.get_top_lgraph(), spin.get_class_lgraph(), spin.get_hidx(), idx2);
      if (spin.is_hierarchical()) {
        Node_pin_iterator piter;
        trace_back2driver(piter, dpin);
        for (auto &dpin2 : piter) {
          xiter.emplace_back(dpin2, spin);
        }
      } else {
        xiter.emplace_back(dpin, spin);
      }
    }
    return true; // continue the iterations
  });

  return xiter;
}

Node_pin_iterator LGraph::inp_driver(const Node_pin &spin) const {
  I(!spin.is_invalid());
  I(spin.is_sink());
  I(spin.get_class_lgraph() == this);

  Node_pin_iterator piter;
  each_pin(spin, [this, &piter, &spin](Index_ID idx2) {
    auto            n = node_internal[idx2].get_num_local_inputs();
    uint8_t         i;
    const Edge_raw *redge;
    for (i = 0, redge = node_internal[idx2].get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      auto dpin = redge->get_out_pin(spin.get_top_lgraph(), spin.get_class_lgraph(), spin.get_hidx(), idx2);
      if (dpin.is_hierarchical()) {
        trace_back2driver(piter, dpin);
      } else {
        piter.emplace_back(dpin);
      }
    }

    return true; // continue the iterations
  });

  return piter;
}

bool LGraph::has_outputs(const Node &node) const {
  auto idx2 = node.get_nid();
  while (true) {
    if (node_internal[idx2].has_local_outputs())
      return true;

    if (node_internal[idx2].is_last_state())
      return false;

    idx2 = node_internal[idx2].get_next();
  }
}

bool LGraph::has_inputs(const Node &node) const {
  auto idx2 = node.get_nid();
  while (true) {
    if (node_internal[idx2].has_local_inputs())
      return true;

    if (node_internal[idx2].is_last_state())
      return false;

    idx2 = node_internal[idx2].get_next();
  }
}

bool LGraph::has_outputs(const Node_pin &pin) const {
  I(pin.is_driver());
  auto idx = pin.get_root_idx();

  auto idx2 = pin.get_root_idx();
  while (true) {
    if (node_internal[idx2].get_dst_pid() == pin.get_pid())
      if (node_internal[idx2].has_local_outputs())
        return true;

    if (node_internal[idx2].is_last_state())
      return false;

    idx2 = node_internal[idx2].get_next();
  }
}

bool LGraph::has_inputs(const Node_pin &pin) const {
  I(pin.is_sink());

  auto idx = pin.get_root_idx();

  auto idx2 = pin.get_root_idx();
  while (true) {
    if (node_internal[idx2].get_dst_pid() == pin.get_pid())
      if (node_internal[idx2].has_local_inputs())
        return true;

    if (node_internal[idx2].is_last_state())
      return false;

    idx2 = node_internal[idx2].get_next();
  }
}

int LGraph::get_num_out_edges(const Node &node) const {
  auto idx2 = node.get_nid();
  int total = 0;
  while (true) {
    total += node_internal[idx2].get_num_local_outputs();

    if (node_internal[idx2].is_last_state())
      return total;

    idx2 = node_internal[idx2].get_next();
  }
  return -1;
}

int LGraph::get_num_inp_edges(const Node &node) const {
  auto idx2 = node.get_nid();
  int total = 0;
  while (true) {
    total += node_internal[idx2].get_num_local_inputs();

    if (node_internal[idx2].is_last_state())
      return total;

    idx2 = node_internal[idx2].get_next();
  }
  return -1;
}

int LGraph::get_num_edges(const Node &node) const {
  auto idx2 = node.get_nid();
  int total = 0;
  while (true) {
    total += node_internal[idx2].get_num_local_edges();

    if (node_internal[idx2].is_last_state())
      return total;

    idx2 = node_internal[idx2].get_next();
  }
  return -1;
}

int LGraph::get_num_out_edges(const Node_pin &pin) const {
  I(pin.is_driver());
  int total = 0;
  auto idx = pin.get_root_idx();

  auto idx2 = pin.get_root_idx();
  while (true) {
    if (node_internal[idx2].get_dst_pid() == pin.get_pid())
      total += node_internal[idx2].get_num_local_outputs();

    if (node_internal[idx2].is_last_state())
      return total;

    idx2 = node_internal[idx2].get_next();
  }
  return -1;
}

int LGraph::get_num_inp_edges(const Node_pin &pin) const {
  I(pin.is_sink());
  int total = 0;
  auto idx = pin.get_root_idx();

  auto idx2 = pin.get_root_idx();
  while (true) {
    if (node_internal[idx2].get_dst_pid() == pin.get_pid())
      total += node_internal[idx2].get_num_local_inputs();

    if (node_internal[idx2].is_last_state())
      return total;

    idx2 = node_internal[idx2].get_next();
  }
}

void LGraph::del_pin(const Node_pin &pin) {
  Node_pin inv;

  if (pin.is_graph_io()) {
    ref_self_sub_node()->del_pin(pin.get_pid());
  }

  if (pin.is_driver()) {
    del_edge_driver_int(pin, inv);
  }else{
    del_edge_sink_int(inv, pin);
  }
}

void LGraph::del_node(const Node &node) {
  auto idx2 = node.get_nid();
  I(node_internal.size()>idx2);

  auto op = node_internal[idx2].get_type();

  if (op == Ntype_op::Const) {
    const_map.erase(node.get_compact_class());
  } else if (op == Ntype_op::IO) {
    I(false);  // add the case once we have a testing case
  } else if (op == Ntype_op::LUT) {
    lut_map.erase(node.get_compact_class());
  } else if (op == Ntype_op::Sub) {
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
      Node_pin        spin(this,
                           this,
                           Hierarchy_tree::invalid_index(),
                           idx2,
                           node_internal[idx2].get_dst_pid(),
                           true);
      for (i = 0, redge = node_int_ptr->get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(redge->is_input());

#if 0
        auto other_nid = node_internal[redge->get_idx()].get_nid();
        if (deleted.count(other_nid))
          continue;
        deleted.insert(other_nid);

        Node other_driver(this, this, Hierarchy_tree::invalid_index(), other_nid);
        del_driver2node_int(other_driver, node);
#else
        auto dpin_idx = redge->get_idx();
        auto dpin_pid = redge->get_inp_pid(); // node_internal[dpin_idx].get_dst_pid();
        I(dpin_pid == node_internal[dpin_idx].get_dst_pid());
        Node_pin dpin(this, this, Hierarchy_tree::invalid_index(), dpin_idx, dpin_pid, false);
        del_edge_driver_int(dpin, spin);
#endif
      }
    }

    {
      absl::flat_hash_set<uint32_t> deleted;

      auto            n = node_int_ptr->get_num_local_outputs();
      uint8_t         i;
      const Edge_raw *redge = nullptr;

      std::vector<Node_pin> spins;
      for (i = 0, redge = node_int_ptr->get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(!redge->is_input());

        // Better if there are lots of pointers to this node (rare)
        auto other_nid = node_internal[redge->get_idx()].get_nid();
        if (deleted.count(other_nid))
          continue;
        deleted.insert(other_nid);

        Node other_sink(this, this, Hierarchy_tree::invalid_index(), other_nid);
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

  Index_ID idx2         = driver.get_nid();
  auto *   node_int_ptr = node_internal.ref(idx2);
  node_int_ptr->clear_full_hint();

  Index_ID last_idx = idx2;

  while (true) {
    auto            n = node_int_ptr->get_num_local_outputs();
    if (n) {
      uint8_t         i;
      const Edge_raw *redge;
      int n_deleted = 0;
      for (i = 0, redge = node_int_ptr->get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        auto master_nid = node_internal[redge->get_idx()].get_nid();
        if (master_nid == sink.get_nid()) {
          node_int_ptr->del_output_int(redge);
          n_deleted++;
        }
        // NOTE: unlike inputs, the outputs are added to the end, no copy movement (advance pointer) at delete
      }
      if (n_deleted==n) {
        try_del_node_int(last_idx, idx2); // can delete idx2
        if (node_int_ptr->is_free_state()) {
          idx2 = last_idx;
        }
      }
    }
    if (node_internal[idx2].is_last_state()) { // no ptr because it may be deleted
      return;
    }

    last_idx     = idx2;
    idx2         = node_internal[idx2].get_next();
    node_int_ptr = node_internal.ref(idx2);
  }

}

void LGraph::del_sink2node_int(const Node &driver, Node &sink) {
  // In hierarchy, not allowed to remove nodes (mark as deleted attribute?)
  I(driver.get_class_lgraph() == driver.get_top_lgraph());
  I(sink.get_class_lgraph() == sink.get_top_lgraph());
  I(sink.get_class_lgraph() == driver.get_top_lgraph());

  Index_ID idx2         = sink.get_nid();
  auto *   node_int_ptr = node_internal.ref(idx2);
  node_int_ptr->clear_full_hint();

  Index_ID last_idx = idx2;

  while (true) {
    auto            n = node_int_ptr->get_num_local_inputs();
    if (n) {
      int n_deleted = 0;
      uint8_t         i;
      const Edge_raw *redge;
      for (i = 0, redge = node_int_ptr->get_input_begin(); i < n; i++) {
        I(redge->get_self_idx() == idx2);
        auto master_nid = node_internal[redge->get_idx()].get_nid();
        if (master_nid == driver.get_nid()) {
          node_int_ptr->del_input_int(redge);
          n_deleted++;
        } else {
          redge += redge->next_node_inc();  // NOTE: delete copies data, sort of advances the pointer
        }
      }
      if (n_deleted ==n) {
        try_del_node_int(last_idx, idx2); // can delete idx2
        if (node_int_ptr->is_free_state()) {
          idx2 = last_idx;
        }
      }
    }
    if (node_internal[idx2].is_last_state()) // no ptr because it may be deleted
      return;

    last_idx     = idx2;
    idx2         = node_internal[idx2].get_next();
    node_int_ptr = node_internal.ref(idx2);
  }
}

void LGraph::try_del_node_int(Index_ID last_idx, Index_ID idx) {
  return;
  auto *idx_ptr = node_internal.ref(idx);
  if (idx == last_idx || idx_ptr->has_local_edges() || idx_ptr->is_root())
    return; // nothing to do

  auto *last_ptr = node_internal.ref(last_idx);

  I(last_ptr->get_next() == idx);
  if (idx_ptr->is_last_state()) {
    last_ptr->set_last_state();
  }else{
    last_ptr->set_next_state(idx_ptr->get_next());
  }
  idx_ptr->try_recycle();
}

bool LGraph::del_edge_driver_int(const Node_pin &dpin, const Node_pin &spin) {
  // WARNING: The edge can be anywhere from get_node().nid to end BUT more
  // likely to find it early starting from idx. Start from idx, and go back to
  // start (nid) again once at the end. If idx again, then it is not anywhere.

  // In hierarchy, not allowed to remove nodes (mark as deleted attribute?)
  GI(!spin.is_invalid(), dpin.get_class_lgraph() == dpin.get_top_lgraph());
  GI(!spin.is_invalid(), spin.get_class_lgraph() == spin.get_top_lgraph());
  GI(!spin.is_invalid(), spin.get_class_lgraph() == dpin.get_top_lgraph());

  node_internal.ref(dpin.get_root_idx())->clear_full_hint();

  Index_ID idx2         = dpin.get_idx();
  auto *   node_int_ptr = node_internal.ref(idx2);

  Index_ID last_idx = idx2;

  Index_ID spin_root_idx = 0;
  if (!spin.is_invalid())
    spin_root_idx = spin.get_root_idx();

  while (true) {
    I(node_int_ptr->get_dst_pid() == dpin.get_pid());

    auto            n = node_int_ptr->get_num_local_outputs();
    uint8_t         i;
    const Edge_raw *redge;
    for (i = 0, redge = node_int_ptr->get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      if (spin_root_idx==0 || redge->get_idx() == spin_root_idx) {
        GI(spin_root_idx, redge->get_inp_pid() == spin.get_pid());
        node_int_ptr->del_output_int(redge);
        try_del_node_int(last_idx, idx2);
        if (spin_root_idx)
          return true;
      }
    }
    do {
      // Just look for next idx2 with same pid
      if (node_int_ptr->is_last_state()) {
        idx2 = node_internal[idx2].get_nid(); // idx2 may not be master
        idx2 = node_internal[idx2].get_nid();
        I(idx2==dpin.get_node().get_nid());
        last_idx = idx2;
      }
      Index_ID tmp = node_internal[idx2].get_next();
      if (tmp == dpin.get_idx()) {
        return false;
      }
      last_idx = idx2;
      idx2 = tmp;
      node_int_ptr = node_internal.ref(idx2);
    } while (node_int_ptr->get_dst_pid() != dpin.get_pid());
  }

  return false;
}

bool LGraph::del_edge_sink_int(const Node_pin &dpin, const Node_pin &spin) {
  // WARNING: The edge can be anywhere from get_node().nid to end BUT more
  // likely to find it early starting from idx. Start from idx, and go back to
  // start (nid) again once at the end. If idx again, then it is not anywhere.

  GI(!dpin.is_invalid(), dpin.get_class_lgraph() == dpin.get_top_lgraph());
  GI(!dpin.is_invalid(), spin.get_class_lgraph() == spin.get_top_lgraph());
  GI(!dpin.is_invalid(), spin.get_class_lgraph() == dpin.get_top_lgraph());

  Index_ID idx2         = spin.get_idx();
  auto *   node_int_ptr = node_internal.ref(idx2);
  node_internal.ref(spin.get_root_idx())->clear_full_hint();

  Index_ID dpin_root_idx = 0;
  if (!dpin.is_invalid())
    dpin_root_idx = dpin.get_root_idx();

  Index_ID last_idx = idx2;
  while (true) {
    I(node_int_ptr->get_dst_pid() == spin.get_pid());

    auto            n = node_int_ptr->get_num_local_inputs();
    uint8_t         i;
    const Edge_raw *redge;
    for (i = 0, redge = node_int_ptr->get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      if (dpin_root_idx == 0 || redge->get_idx() == dpin_root_idx) {
        GI(dpin_root_idx, redge->get_inp_pid() == dpin.get_pid());
        node_int_ptr->del_input_int(redge);
        try_del_node_int(last_idx, idx2);
        if (dpin_root_idx)
          return true;
      }
    }
    do {
      // Just look for next idx2 with same pid
      if (node_internal[idx2].is_last_state()) {
        idx2 = spin.get_node().get_nid();
        last_idx = idx2;
      }
      Index_ID tmp = node_internal[idx2].get_next();
      if (tmp == spin.get_idx()) {
        return false;
      }
      last_idx = idx2;
      idx2 = tmp;
      node_int_ptr = node_internal.ref(idx2);
    } while (node_int_ptr->get_dst_pid() != spin.get_pid());

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

  Ntype_op op = old_node.get_type_op();

  if (op == Ntype_op::LUT) {
    new_node = create_node();
    new_node.set_type_lut(old_node.get_type_lut());
  } else if (op == Ntype_op::Sub) {
    new_node = create_node_sub(old_node.get_type_sub());
  } else if (op == Ntype_op::Const) {
    new_node = create_node_const(old_node.get_type_const());
    I(new_node.get_driver_pin().get_bits() == old_node.get_driver_pin().get_bits());
  } else {
    I(op != Ntype_op::IO);  // Special case, must use add input/output API
    new_node = create_node(op);
  }

  // TODO: What happens to all the node/pin attributes??
  for (const auto &old_dpin : old_node.out_connected_pins()) {
    // WARNING: If pin has bits, but it is not connected, the attribute is not copied
    auto new_dpin = new_node.setup_driver_pin_raw(old_dpin.get_pid());
    new_dpin.set_bits(old_dpin.get_bits());
  }

  return new_node;
}

Node LGraph::create_node(const Ntype_op op) {
  Index_ID nid = create_node_int();
  set_type(nid, op);

  I(op != Ntype_op::IO);   // Special case, must use add input/output API
  I(op != Ntype_op::Sub);  // Do not build by steps. call create_node_sub

  return Node(this, Hierarchy_tree::root_index(), nid);
}

Node LGraph::create_node(const Ntype_op op, Bits_t bits) {
  auto node = create_node(op);

  I(!Ntype::is_multi_driver(op));
  node.setup_driver_pin().set_bits(bits);

  return node;
}

Node LGraph::create_node_const(const Lconst &value) {
  // WARNING: There is a const_map, but it is NOT a bimap (speed). Just from
  // nid to const.
  Index_ID nid = memoize_const_hint[value.hash() % memoize_const_hint.size()];
  if (nid == 0
      || nid >= node_internal.size()
      || !node_internal[nid].is_valid()
      || node_internal[nid].get_type() != Ntype_op::Const
      || get_type_const(nid) != value
      || get_type_const(nid).get_bits() != value.get_bits()) {
    nid = create_node_int();
    set_type_const(nid, value);
    memoize_const_hint[value.hash() % memoize_const_hint.size()] = nid;
  }

  I(node_internal[nid].get_dst_pid() == 0);
  I(node_internal[nid].is_master_root());

  return Node(this, Hierarchy_tree::root_index(), nid);
}

Node LGraph::create_node_lut(const Lconst &lut) {

  auto nid = create_node().get_nid();
  set_type_lut(nid, lut);

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

void LGraph::trace_back2driver(Node_pin_iterator &xiter, const Node_pin &dpin) const {
  I(dpin.get_class_lgraph() == this);
  I(dpin.is_hierarchical());
  I(dpin.is_driver());

  if (dpin.is_graph_input() && dpin.is_down_node()) {
    auto up_pin = dpin.get_up_pin();
    if (up_pin.is_connected()) {
      for (auto &e : up_pin.inp_edges()) {
        e.driver.get_class_lgraph()->trace_back2driver(xiter, e.driver);
      }
    } else {
      xiter.emplace_back(dpin);
    }
  }else if (dpin.get_node().is_type_sub_present()) {
    auto down_pin = dpin.get_down_pin();
    if (down_pin.is_connected()) {
      for (auto &e : down_pin.inp_edges()) {
        e.driver.get_class_lgraph()->trace_back2driver(xiter, e.driver);
      }
    } else {
      xiter.emplace_back(dpin);
    }
  } else {
    xiter.emplace_back(dpin);
  }
}

void LGraph::trace_forward2sink(XEdge_iterator &xiter, const Node_pin &dpin, const Node_pin &spin) const {
  I(spin.get_class_lgraph() == this);
  I(spin.is_hierarchical());
  I(spin.is_sink());

  if (spin.is_graph_output() && spin.is_down_node()) {
    auto up_pin = spin.get_up_pin();
    if (up_pin.is_connected()) {
      for (auto &e : up_pin.out_edges()) {
        e.sink.get_class_lgraph()->trace_forward2sink(xiter, dpin, e.sink);
      }
    } else {
      xiter.emplace_back(dpin, spin);
    }
  }else if (spin.get_node().is_type_sub_present()) {
    auto down_pin = spin.get_down_pin();
    if (down_pin.is_connected()) {
      for (auto &e : down_pin.out_edges()) {
        e.sink.get_class_lgraph()->trace_forward2sink(xiter, dpin, e.sink);
      }
    } else {
      xiter.emplace_back(dpin, spin);
    }
  } else {
    xiter.emplace_back(dpin, spin);
  }

}

Fwd_edge_iterator LGraph::forward(bool visit_sub) { return Fwd_edge_iterator(this, visit_sub); }
Bwd_edge_iterator LGraph::backward(bool visit_sub) { return Bwd_edge_iterator(this, visit_sub); }

// Skip after 1, but first may be deleted, so fast_next
Fast_edge_iterator LGraph::fast(bool visit_sub) { return Fast_edge_iterator(this, visit_sub); }

void LGraph::dump() {
  fmt::print("lgraph name:{} size:{}\n", name, node_internal.size());

#if 0
  int n6=0;
  int n8=0;
  int n12=0;
  int nlarge=0;
  for(auto n:fast()) {
    int last = n.get_nid();
    fmt::print("nid:{}\n",last);
    for(auto e:n.out_edges()) {
      int delta = (int)last-(int)e.sink.get_idx().value;
      if (delta>-31 && delta<31)
        n6++;
      else if (delta>-127 && delta<128)
        n8++;
      else if (delta>-1024 && delta<1024)
        n12++;
      else
        nlarge++;
      fmt::print("  {}\n", (int)last - (int)e.sink.get_idx().value);
      //last = e.sink.get_idx().value;
    }
  }
  fmt::print("n6:{} n8:{} n12:{} nlarge:{}\n",n6,n8,n12,nlarge);
  return;
#endif

  for (const auto *io_pin : get_self_sub_node().get_io_pins()) {
    fmt::print("io {} pos:{} pid:{} {}\n",
               io_pin->name,
               io_pin->graph_io_pos,
               get_self_sub_node().get_instance_pid(io_pin->name),
               io_pin->dir == Sub_node::Direction::Input ? "input" : "output");
  }

#if 1
  for (size_t i = 0; i < node_internal.size(); ++i) {
    if (!node_internal[i].is_node_state())
      continue;
    if (!node_internal[i].is_master_root())
      continue;
    auto node = Node(this, Node::Compact_class(i));  // NOTE: To remove once new iterators are finished

    node.dump();
  }
#endif

  each_sub_fast([this](Node &node, Lg_type_id lgid2) {
    LGraph *child = LGraph::open(get_path(), node.get_type_sub());

    fmt::print("  lgid:{} sub:{}\n", node.debug_name(), lgid2, child->get_name());
  });

  fmt::print("FORWARD....\n");
  for(auto node:forward()) {
    node.dump();
  }
}

void LGraph::dump_down_nodes() {
  for (auto &cnode : subid_map) {
    fmt::print(" sub:{}\n", cnode.first.get_node(this).debug_name());
  }
}

Node LGraph::get_graph_input_node(bool hier) {
  if (hier)
    return Node(this, Hierarchy_tree::root_index(), Hardcoded_input_nid);
  else
    return Node(this, Hierarchy_tree::invalid_index(), Hardcoded_input_nid);
}

Node LGraph::get_graph_output_node(bool hier) {
  if (hier)
    return Node(this, Hierarchy_tree::root_index(), Hardcoded_output_nid);
  else
    return Node(this, Hierarchy_tree::invalid_index(), Hardcoded_output_nid);
}

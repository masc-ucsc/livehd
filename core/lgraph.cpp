//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph.hpp"

#include <dirent.h>
#include <sys/types.h>

#include <fstream>
#include <iostream>
#include <set>

#include "absl/strings/match.h"
#include "annotate.hpp"
#include "graph_library.hpp"
#include "lgedgeiter.hpp"
#include "lgraph_base_core.hpp"

Lgraph::Lgraph(const mmap_lib::str &_path, const mmap_lib::str &_name, Lg_type_id _lgid, Graph_library *_lib)
    : Lgraph_Base(_path, _name, _lgid, _lib), Lgraph_Node_Type(_path, _name, _lgid, _lib), htree(this) {
  I(!_name.contains('/'));  // No path in name
  I(_name == get_name());
}

Lgraph::~Lgraph() {
  sync();
  library->unregister(name, lgid, this);
}

bool Lgraph::exists(const mmap_lib::str &path, const mmap_lib::str &name) { return Graph_library::try_find_lgraph(path, name) != nullptr; }

Lgraph *Lgraph::create(const mmap_lib::str &path, const mmap_lib::str &name, const mmap_lib::str &source) {
  auto *lib = Graph_library::instance(path);
  I(lib);
  auto *lg = lib->setup_lgraph(name, source);
  lg->clear();

  return lg;
}

Lgraph *Lgraph::clone_skeleton(const mmap_lib::str &new_lg_name) {
  auto  lg_source = get_library().get_source(get_lgid());
  auto  *new_lg   = Lgraph::create(get_path(), new_lg_name, lg_source);

  auto *new_sub = new_lg->ref_self_sub_node();
  new_sub->reset_pins();  // NOTE: it may have been created before. Clear to keep same order/attributes

  for (const auto &old_io_pin : get_self_sub_node().get_io_pins()) {
    if (old_io_pin.is_invalid())
      continue;

    if (old_io_pin.is_input()) {
      auto old_dpin = get_graph_input(old_io_pin.name);
      new_lg->add_graph_input(old_io_pin.name, old_io_pin.graph_io_pos, old_dpin.get_bits());
    } else {
      auto old_spin = get_graph_output(old_io_pin.name);
      new_lg->add_graph_output(old_io_pin.name, old_io_pin.graph_io_pos, old_spin.get_driver_pin().get_bits());
    }
  }

  return new_lg;
}

Lgraph *Lgraph::open(const mmap_lib::str &path, Lg_type_id lgid) {
  auto *lib = Graph_library::instance(path);
  if (unlikely(lib == nullptr))
    return nullptr;

  Lgraph *lg = lib->try_find_lgraph(lgid);
  if (likely(lg != nullptr)) {
    return lg;
  }

  if (!lib->exists(lgid))
    return nullptr;

  return lib->setup_lgraph(lib->get_name(lgid), lib->get_source(lgid));
}

Lgraph *Lgraph::open(const mmap_lib::str &path, const mmap_lib::str &name) {
  Lgraph *lg = Graph_library::try_find_lgraph(path, name);
  if (lg) {
    return lg;
  }

  auto *lib = Graph_library::instance(path);
  if (lib == nullptr)
    return nullptr;

  if (unlikely(!lib->has_name(name)))
    return nullptr;

  return lib->setup_lgraph(name, lib->get_source(name));
}

void Lgraph::rename(const mmap_lib::str &path, const mmap_lib::str &orig, const mmap_lib::str &dest) {
  bool valid = Graph_library::instance(path)->rename_name(orig, dest);
  if (valid)
    warn("lgraph::rename find original graph {} in path {}", orig, path);
  else
    error("cannot find original graph {} in path {}", orig, path);
}

void Lgraph::clear() {
  Lgraph_Node_Type::clear();

  Lgraph_Base::clear();  // last. Removes lock at the end

  Ann_support::clear(this);

  auto nid1 = create_node_int();
  auto nid2 = create_node_int();

  I(nid1 == Hardcoded_input_nid);
  I(nid2 == Hardcoded_output_nid);

  set_type(nid1, Ntype_op::IO);
  set_type(nid2, Ntype_op::IO);

  std::fill(memoize_const_hint.begin(), memoize_const_hint.end(), 0);  // Not needed but neat
}

void Lgraph::sync() {
  Ann_support::sync(this);

  Lgraph_Node_Type::sync();

  Lgraph_Base::sync();  // last. Removes lock at the end
}

Node_pin Lgraph::get_graph_input(const mmap_lib::str &str) {
  I(get_self_sub_node().is_input(str));  // The input does not exist, do not call get_input
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this, Hierarchy::hierarchical_root(), Hardcoded_input_nid).setup_driver_pin_raw(io_pid);
}

Node_pin Lgraph::get_graph_output(const mmap_lib::str &str) {
  I(get_self_sub_node().is_output(str));  // The output does not exist, do not call get_output
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this, Hierarchy::hierarchical_root(), Hardcoded_output_nid).setup_sink_pin_raw(io_pid);
}

Node_pin Lgraph::get_graph_output_driver_pin(const mmap_lib::str &str) {
  I(get_self_sub_node().is_output(str));  // The output does not exist, do not call get_output
  auto io_pid = get_self_sub_node().get_instance_pid(str);

  return Node(this, Hierarchy::hierarchical_root(), Hardcoded_output_nid).setup_driver_pin_raw(io_pid);
}

bool Lgraph::has_graph_input(const mmap_lib::str &io_name) const {
  if (!get_self_sub_node().is_input(io_name))
    return false;

  auto inst_pid = get_self_sub_node().get_instance_pid(io_name);

  auto idx = find_idx_from_pid(Hardcoded_input_nid, inst_pid);
  return (idx != 0);
}

bool Lgraph::has_graph_output(const mmap_lib::str &io_name) const {
  if (!get_self_sub_node().is_output(io_name))
    return false;

  auto inst_pid = get_self_sub_node().get_instance_pid(io_name);
  auto idx      = find_idx_from_pid(Hardcoded_output_nid, inst_pid);
  return (idx != 0);
}

Node_pin Lgraph::add_graph_input(const mmap_lib::str str, Port_ID pos, uint32_t bits) {
  I(str != "$");
  I(!has_graph_output(str));

  Port_ID inst_pid;
  if (get_self_sub_node().has_pin(str)) {
    inst_pid = ref_self_sub_node()->map_graph_pos(str, Sub_node::Direction::Input, pos);  // reset pin stats
  } else {
    inst_pid = ref_self_sub_node()->add_pin(str, Sub_node::Direction::Input, pos);
  }
  I(node_internal[Hardcoded_input_nid].get_type() == Ntype_op::IO);

  Index_id root_idx = 0;
  auto     idx      = find_idx_from_pid(Hardcoded_input_nid, inst_pid);
  if (idx == 0)
    idx = get_space_output_pin(Hardcoded_input_nid, inst_pid, root_idx);

  Node_pin pin(this, this, Hierarchy::hierarchical_root(), idx, inst_pid, false);

  pin.set_name(str);
  pin.set_bits(bits);

  return pin;
}

Node_pin Lgraph::add_graph_output(const mmap_lib::str str, Port_ID pos, uint32_t bits) {
  I(str != "%");
  I(!has_graph_input(str));

  Port_ID inst_pid;
  if (get_self_sub_node().has_pin(str)) {
    inst_pid = ref_self_sub_node()->map_graph_pos(str, Sub_node::Direction::Output, pos);  // reset pin stats
  } else {
    inst_pid = ref_self_sub_node()->add_pin(str, Sub_node::Direction::Output, pos);
  }
  I(node_internal[Hardcoded_output_nid].get_type() == Ntype_op::IO);

  Index_id root_idx = 0;
  auto     idx      = find_idx_from_pid(Hardcoded_output_nid, inst_pid);
  if (idx == 0)
    idx = get_space_output_pin(Hardcoded_output_nid, inst_pid, root_idx);

  Node_pin dpin(this, this, Hierarchy::hierarchical_root(), idx, inst_pid, false);
  dpin.set_name(str);
  dpin.set_bits(bits);

  return Node_pin(this, this, Hierarchy::hierarchical_root(), idx, inst_pid, true);
}

Node_pin_iterator Lgraph::out_connected_pins(const Node &node) const {
  I(node.get_class_lgraph() == this);

  node_internal.ref_lock();

  Node_pin_iterator            xiter;
  absl::flat_hash_set<Port_ID> xiter_set;

  Index_id idx2 = node.get_nid();
  I(node_internal.ref(idx2)->is_master_root());

  auto pid = node_internal.ref(idx2)->get_dst_pid();
  while (true) {
    I(!xiter_set.contains(pid));
    auto n = node_internal.ref(idx2)->get_num_local_outputs();
    if (n > 0) {
      auto root_idx = idx2;
      if (!node_internal.ref(idx2)->is_root())
        root_idx = node_internal.ref(idx2)->get_nid();

      xiter.emplace_back(Node_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), root_idx, pid, false));

      xiter_set.insert(pid);
    }

    do {
      if (node_internal.ref(idx2)->is_last_state()) {
        node_internal.ref_unlock();
        return xiter;
      }

      Index_id tmp = node_internal.ref(idx2)->get_next();
      I(node_internal.ref(tmp)->get_master_root_nid() == node_internal.ref(idx2)->get_master_root_nid());
      idx2 = tmp;
      pid  = node_internal.ref(idx2)->get_dst_pid();
    } while (xiter_set.contains(pid));
  }

  node_internal.ref_unlock();
  return xiter;
}

Node_pin_iterator Lgraph::inp_connected_pins(const Node &node) const {
  I(node.get_class_lgraph() == this);
  Node_pin_iterator            xiter;
  absl::flat_hash_set<Port_ID> xiter_set;

  node_internal.ref_lock();
  Index_id idx2 = node.get_nid();
  I(node_internal.ref(idx2)->is_master_root());

  auto pid = node_internal.ref(idx2)->get_dst_pid();
  while (true) {
    I(!xiter_set.contains(pid));
    auto n = node_internal.ref(idx2)->get_num_local_inputs();
    if (n > 0) {
      auto root_idx = idx2;
      if (!node_internal.ref(idx2)->is_root())
        root_idx = node_internal.ref(idx2)->get_nid();

      xiter.emplace_back(Node_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), root_idx, pid, true));
      xiter_set.insert(pid);
    }

    do {
      if (node_internal.ref(idx2)->is_last_state()) {
        node_internal.ref_unlock();
        return xiter;
      }

      Index_id tmp = node_internal.ref(idx2)->get_next();
      I(node_internal.ref(tmp)->get_master_root_nid() == node_internal.ref(idx2)->get_master_root_nid());
      idx2 = tmp;
      pid  = node_internal.ref(idx2)->get_dst_pid();
    } while (xiter_set.contains(pid));
  }

  node_internal.ref_unlock();
  return xiter;
}

Node_pin_iterator Lgraph::inp_drivers(const Node &node) const {
  I(node.get_class_lgraph() == this);

  Node_pin_iterator xiter;

  node_internal.ref_lock();

  Index_id idx2 = node.get_nid();
  I(node_internal.ref(node.get_nid())->is_master_root());

  const bool hier = node.is_hierarchical();

  while (true) {
    auto n = node_internal.ref(idx2)->get_num_local_inputs();

    if (n) {
      auto root_idx = idx2;
      if (!node_internal.ref(idx2)->is_root())
        root_idx = node_internal.ref(idx2)->get_nid();

      const Node_pin spin(node.get_top_lgraph(),
                          node.get_class_lgraph(),
                          node.get_hidx(),
                          root_idx,
                          node_internal.ref(idx2)->get_dst_pid(),
                          true);

      uint8_t         i;
      const Edge_raw *redge;

      std::vector<Node_pin> pin_list;  // NOTE: insert in pin_list because the mmap can dissapaear if touching other nodes
      for (i = 0, redge = node_internal.ref(idx2)->get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(redge->is_input());
        auto driver_pin_idx = redge->get_idx();
        auto driver_pin_pid = redge->get_inp_pid();
        I(node_internal.ref(driver_pin_idx)->get_dst_pid() == driver_pin_pid);
        auto driver_master_nid = node_internal.ref(driver_pin_idx)->get_nid();
        I(node_internal.ref(driver_master_nid)->is_master_root());

        Node_pin dpin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), driver_pin_idx, driver_pin_pid, false);
        pin_list.emplace_back(dpin);
      }

      node_internal.ref_unlock();
      for (auto &dpin : pin_list) {
        if (hier) {
          trace_back2driver(xiter, dpin, spin);
        } else {
          xiter.emplace_back(dpin);
        }
      }
      node_internal.ref_lock();
    }

    if (node_internal.ref(idx2)->is_last_state())
      break;
    Index_id tmp = node_internal.ref(idx2)->get_next();
    I(node_internal.ref(tmp)->get_master_root_nid() == node_internal.ref(idx2)->get_master_root_nid());
    idx2 = tmp;
  }

  node_internal.ref_unlock();
  return xiter;
}

XEdge_iterator Lgraph::out_edges(const Node &node) const {
  I(node.get_class_lgraph() == this);

  XEdge_iterator xiter;

  const bool hier = node.is_hierarchical();
  if (hier && node.is_graph_output()) {
    for (auto out_spin : node.inp_connected_pins()) {
      for (auto e : out_spin.inp_edges()) {
        trace_forward2sink(xiter, e.driver, out_spin);
      }
    }

    return xiter;
  }

  node_internal.ref_lock();

  Index_id idx2 = node.get_nid();
  I(node_internal.ref(node.get_nid())->is_master_root());

  while (true) {
    auto n = node_internal.ref(idx2)->get_num_local_outputs();

    if (n) {
      uint8_t         i;
      const Edge_raw *redge;

      Node_pin dpin(node.get_top_lgraph(),
                    node.get_class_lgraph(),
                    node.get_hidx(),
                    idx2,
                    node_internal.ref(idx2)->get_dst_pid(),
                    false);

      I(hier == dpin.is_hierarchical());

      std::vector<Node_pin> pin_list;  // NOTE: insert in pin_list because the mmap can dissapaear if touching other nodes
      for (i = 0, redge = node_internal.ref(idx2)->get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
        auto spin = redge->get_inp_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), idx2);
        pin_list.emplace_back(spin);
      }
      if (hier) {
        node_internal.ref_unlock();
        for (auto &spin : pin_list) {
          trace_forward2sink(xiter, dpin, spin);
        }
        node_internal.ref_lock();
      } else {
        for (auto &spin : pin_list) {
          xiter.emplace_back(dpin, spin);
        }
      }
    }
    if (node_internal.ref(idx2)->is_last_state())
      break;
    Index_id tmp = node_internal.ref(idx2)->get_next();
    I(node_internal.ref(tmp)->get_master_root_nid() == node_internal.ref(idx2)->get_master_root_nid());
    idx2 = tmp;
  }

  node_internal.ref_unlock();
  return xiter;
}

XEdge_iterator Lgraph::inp_edges(const Node &node) const {
  I(node.get_class_lgraph() == this);

  XEdge_iterator xiter;
  const bool     hier = node.is_hierarchical();
  if (hier && node.is_graph_input()) {
    for (auto inp_dpin : node.out_connected_pins()) {
      Node_pin_iterator piter;
      Node_pin          invalid_spin;
      trace_back2driver(piter, inp_dpin, invalid_spin);

      for (auto out_spin : inp_dpin.out_edges()) {
        for (auto &dpin2 : piter) {
          XEdge edge(dpin2, out_spin.sink);
          xiter.emplace_back(edge);
        }
      }
    }

    return xiter;
  }

  node_internal.ref_lock();

  Index_id idx2 = node.get_nid();
  I(node_internal.ref(node.get_nid())->is_master_root());

  while (true) {
    auto n = node_internal.ref(idx2)->get_num_local_inputs();
    if (n) {
      Node_pin spin(node.get_top_lgraph(),
                    node.get_class_lgraph(),
                    node.get_hidx(),
                    idx2,
                    node_internal.ref(idx2)->get_dst_pid(),
                    true);

      uint8_t         i;
      const Edge_raw *redge;

      std::vector<Node_pin> pin_list;  // NOTE: insert in pin_list because the mmap can dissapaear if touching other nodes
      for (i = 0, redge = node_internal.ref(idx2)->get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
        auto dpin = redge->get_out_pin(node.get_top_lgraph(), node.get_class_lgraph(), node.get_hidx(), idx2);
        pin_list.emplace_back(dpin);
      }

      node_internal.ref_unlock();
      for (auto &dpin : pin_list) {
        if (hier) {
          Node_pin_iterator piter;
          trace_back2driver(piter, dpin, spin);
          for (auto &dpin2 : piter) {
            XEdge edge(dpin2, spin);
            xiter.emplace_back(edge);
          }
        } else {
          XEdge edge(dpin, spin);
          xiter.emplace_back(edge);
        }
      }
      node_internal.ref_lock();
    }
    if (node_internal.ref(idx2)->is_last_state())
      break;
    Index_id tmp = node_internal.ref(idx2)->get_next();
    I(node_internal.ref(tmp)->get_master_root_nid() == node_internal.ref(idx2)->get_master_root_nid());
    idx2 = tmp;
  }

  node_internal.ref_unlock();
  return xiter;
}

XEdge_iterator Lgraph::inp_edges_ordered(const Node &node) const {
  auto iter = inp_edges(node);

  std::sort(iter.begin(), iter.end(), [](const XEdge &a, const XEdge &b) -> bool { return a.sink.get_pid() < b.sink.get_pid(); });

  return iter;
}

XEdge_iterator Lgraph::out_edges_ordered(const Node &node) const {
  auto iter = out_edges(node);

  std::sort(iter.begin(), iter.end(), [](const XEdge &a, const XEdge &b) -> bool {
    return a.driver.get_pid() < b.driver.get_pid();
  });

  return iter;
}

XEdge_iterator Lgraph::inp_edges_ordered_reverse(const Node &node) const {
  auto iter = inp_edges(node);

  std::sort(iter.begin(), iter.end(), [](const XEdge &a, const XEdge &b) -> bool { return a.sink.get_pid() > b.sink.get_pid(); });

  return iter;
}

XEdge_iterator Lgraph::out_edges_ordered_reverse(const Node &node) const {
  auto iter = out_edges(node);

  std::sort(iter.begin(), iter.end(), [](const XEdge &a, const XEdge &b) -> bool {
    return a.driver.get_pid() > b.driver.get_pid();
  });

  return iter;
}

XEdge_iterator Lgraph::out_edges(const Node_pin &dpin) const {
  I(dpin.is_driver());
  I(dpin.get_class_lgraph() == this);

  XEdge_iterator xiter;

  each_pin(dpin, [this, &xiter, &dpin](Index_id idx2) {
    node_internal.ref_lock();
    auto            n = node_internal.ref(idx2)->get_num_local_outputs();
    uint8_t         i;
    const Edge_raw *redge;

    std::vector<Node_pin> pin_list;  // NOTE: insert in pin_list because the mmap can dissapaear if touching other nodes
    for (i = 0, redge = node_internal.ref(idx2)->get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      auto spin = redge->get_inp_pin(dpin.get_top_lgraph(), dpin.get_class_lgraph(), dpin.get_hidx(), idx2);
      pin_list.emplace_back(spin);
    }
    node_internal.ref_unlock();

    if (dpin.is_hierarchical()) {
      for (auto &spin : pin_list) {
        trace_forward2sink(xiter, dpin, spin);
      }
    } else {
      for (auto &spin : pin_list) {
        xiter.emplace_back(dpin, spin);
      }
    }

    return true;  // continue the iterations
  });

  return xiter;
}

XEdge_iterator Lgraph::inp_edges(const Node_pin &spin) const {
  I(spin.is_sink() || spin.is_graph_input());
  I(spin.get_class_lgraph() == this);

  XEdge_iterator xiter;

  each_pin(spin, [this, &xiter, &spin](Index_id idx2) {
    node_internal.ref_lock();
    auto            n = node_internal.ref(idx2)->get_num_local_inputs();
    uint8_t         i;
    const Edge_raw *redge;

    std::vector<Node_pin> pin_list;  // NOTE: insert in pin_list because the mmap can dissapaear if touching other nodes
    for (i = 0, redge = node_internal.ref(idx2)->get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      auto dpin = redge->get_out_pin(spin.get_top_lgraph(), spin.get_class_lgraph(), spin.get_hidx(), idx2);
      pin_list.emplace_back(dpin);
    }
    node_internal.ref_unlock();

    for (auto &dpin : pin_list) {
      if (spin.is_hierarchical()) {
        Node_pin_iterator piter;
        trace_back2driver(piter, dpin, spin);
        for (auto &dpin2 : piter) {
          XEdge edge(dpin2, spin);
          xiter.emplace_back(edge);
        }
      } else {
        xiter.emplace_back(dpin, spin);
      }
    }
    return true;  // continue the iterations
  });

  return xiter;
}

Node_pin_iterator Lgraph::inp_drivers(const Node_pin &spin) const {
  I(!spin.is_invalid());
  I(spin.is_sink());
  I(spin.get_class_lgraph() == this);

  Node_pin_iterator piter;

  each_pin(spin, [this, &piter, &spin](Index_id idx2) {
    node_internal.ref_lock();

    auto            n = node_internal.ref(idx2)->get_num_local_inputs();
    uint8_t         i;
    const Edge_raw *redge;

    std::vector<Node_pin> pin_list;  // NOTE: insert in pin_list because the mmap can dissapaear if touching other nodes
    for (i = 0, redge = node_internal.ref(idx2)->get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      auto dpin = redge->get_out_pin(spin.get_top_lgraph(), spin.get_class_lgraph(), spin.get_hidx(), idx2);
      pin_list.emplace_back(dpin);
    }
    node_internal.ref_unlock();

    for (auto &dpin : pin_list) {
      if (dpin.is_hierarchical()) {
        trace_back2driver(piter, dpin, spin);
      } else {
        piter.emplace_back(dpin);
      }
    }

    return true;  // continue the iterations
  });

  return piter;
}

Node_pin_iterator Lgraph::out_sinks(const Node_pin &dpin) const {
  // NOTE: Not very efficient. Hopefully with Graph_core, we can do faster/better
  I(!dpin.is_invalid());
  I(dpin.is_driver());
  I(dpin.get_class_lgraph() == this);

  Node_pin_iterator piter;
  for (auto e : dpin.get_node().out_edges()) {
    if (e.driver != dpin)
      continue;
    piter.emplace_back(e.sink);
  }

  return piter;
}

Node_pin_iterator Lgraph::out_sinks(const Node &node) const {
  // NOTE: Not very efficient. Hopefully with Graph_core, we can do faster/better
  I(!node.is_invalid());
  I(node.get_class_lgraph() == this);

  Node_pin_iterator piter;
  for (auto e : node.out_edges()) {
    piter.emplace_back(e.sink);
  }

  return piter;
}

bool Lgraph::has_outputs(const Node &node) const {
  auto idx2 = node.get_nid();

  node_internal.ref_lock();

  while (true) {
    if (node_internal.ref(idx2)->has_local_outputs()) {
      node_internal.ref_unlock();
      return true;
    }

    if (node_internal.ref(idx2)->is_last_state()) {
      node_internal.ref_unlock();
      return false;
    }

    idx2 = node_internal.ref(idx2)->get_next();
  }

  node_internal.ref_unlock();
  I(false);
  return false;
}

bool Lgraph::has_inputs(const Node &node) const {
  node_internal.ref_lock();

  auto idx2 = node.get_nid();
  while (true) {
    if (node_internal.ref(idx2)->has_local_inputs()) {
      node_internal.ref_unlock();
      return true;
    }

    if (node_internal.ref(idx2)->is_last_state()) {
      node_internal.ref_unlock();
      return false;
    }

    idx2 = node_internal.ref(idx2)->get_next();
  }

  node_internal.ref_unlock();
  I(false);
  return false;
}

bool Lgraph::has_outputs(const Node_pin &pin) const {
  I(pin.is_driver());

  auto idx2 = pin.get_root_idx();

  node_internal.ref_lock();
  while (true) {
    if (node_internal.ref(idx2)->get_dst_pid() == pin.get_pid())
      if (node_internal.ref(idx2)->has_local_outputs()) {
        node_internal.ref_unlock();
        return true;
      }

    if (node_internal.ref(idx2)->is_last_state()) {
      node_internal.ref_unlock();
      return false;
    }

    idx2 = node_internal.ref(idx2)->get_next();
  }

  I(false);
  node_internal.ref_unlock();

  return false;
}

bool Lgraph::has_inputs(const Node_pin &pin) const {
  I(pin.is_sink());

  auto idx2 = pin.get_root_idx();

  node_internal.ref_lock();
  while (true) {
    if (node_internal.ref(idx2)->get_dst_pid() == pin.get_pid())
      if (node_internal.ref(idx2)->has_local_inputs()) {
        node_internal.ref_unlock();
        return true;
      }

    if (node_internal.ref(idx2)->is_last_state()) {
      node_internal.ref_unlock();
      return false;
    }

    idx2 = node_internal.ref(idx2)->get_next();
  }

  I(false);
  node_internal.ref_unlock();

  return false;
}

int Lgraph::get_num_out_edges(const Node &node) const {
  auto idx2  = node.get_nid();
  int  total = 0;

  node_internal.ref_lock();
  while (true) {
    const auto *ref = node_internal.ref(idx2);

    total += ref->get_num_local_outputs();

    if (ref->is_last_state()) {
      node_internal.ref_unlock();
      return total;
    }

    idx2 = ref->get_next();
  }

  I(false);
  node_internal.ref_unlock();
  return -1;
}

int Lgraph::get_num_inp_edges(const Node &node) const {
  auto idx2  = node.get_nid();
  int  total = 0;

  node_internal.ref_lock();
  while (true) {
    const auto *ref = node_internal.ref(idx2);

    total += ref->get_num_local_inputs();

    if (ref->is_last_state()) {
      node_internal.ref_unlock();
      return total;
    }

    idx2 = ref->get_next();
  }

  I(false);
  node_internal.ref_unlock();
  return -1;
}

int Lgraph::get_num_edges(const Node &node) const {
  auto idx2  = node.get_nid();
  int  total = 0;

  node_internal.ref_lock();
  while (true) {
    const auto *ref = node_internal.ref(idx2);

    total += ref->get_num_local_edges();

    if (ref->is_last_state()) {
      node_internal.ref_unlock();
      return total;
    }

    idx2 = ref->get_next();
  }

  I(false);
  node_internal.ref_unlock();
  return -1;
}

int Lgraph::get_num_out_edges(const Node_pin &pin) const {
  I(pin.is_driver());

  int total = 0;

  auto idx2 = pin.get_root_idx();

  node_internal.ref_lock();
  while (true) {
    const auto *ref = node_internal.ref(idx2);

    if (ref->get_dst_pid() == pin.get_pid())
      total += ref->get_num_local_outputs();

    if (ref->is_last_state()) {
      node_internal.ref_unlock();
      return total;
    }

    idx2 = ref->get_next();
  }

  I(false);
  node_internal.ref_unlock();
  return -1;
}

int Lgraph::get_num_inp_edges(const Node_pin &pin) const {
  I(pin.is_sink());

  int total = 0;

  auto idx2 = pin.get_root_idx();

  node_internal.ref_lock();
  while (true) {
    const auto *ref = node_internal.ref(idx2);

    if (ref->get_dst_pid() == pin.get_pid())
      total += ref->get_num_local_inputs();

    if (ref->is_last_state()) {
      node_internal.ref_unlock();
      return total;
    }

    idx2 = ref->get_next();
  }

  I(false);
  node_internal.ref_unlock();
  return 0;
}

void Lgraph::del_pin(const Node_pin &pin) {
  if (pin.is_graph_io()) {
    ref_self_sub_node()->del_pin(pin.get_pid());
    return;
  }

  if (pin.is_driver()) {
    for (auto &e : out_edges(pin)) e.del_edge();
  } else {
    for (auto &e : inp_edges(pin)) e.del_edge();
  }
}

void Lgraph::del_node(const Node &node) {
  auto idx2 = node.get_nid();
  I(node_internal.size() > idx2);

  node_internal.ref_lock();

  auto op = node_internal.ref(idx2)->get_type();

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
      auto            n = node_int_ptr->get_num_local_inputs();
      int             i;
      const Edge_raw *redge = nullptr;
      Node_pin        spin(this, this, Hierarchy::non_hierarchical(), idx2, node_internal.ref(idx2)->get_dst_pid(), true);

      std::vector<Node_pin> pin_list;  // NOTE: insert in pin_list because the mmap can dissapaear if touching other nodes
      for (i = 0, redge = node_int_ptr->get_input_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(redge->is_input());
        auto     dpin_idx = redge->get_idx();
        auto     dpin_pid = redge->get_inp_pid();
        Node_pin dpin(this, this, Hierarchy::non_hierarchical(), dpin_idx, dpin_pid, false);
        pin_list.emplace_back(dpin);
      }

      node_internal.ref_unlock();
      for (auto &dpin : pin_list) {
        del_edge_driver_int(dpin, spin);
      }
      node_internal.ref_lock();
    }

    {
      auto            n = node_int_ptr->get_num_local_outputs();
      uint8_t         i;
      const Edge_raw *redge = nullptr;

      std::vector<Node> node_list;  // NOTE: insert in pin_list because the mmap can dissapaear if touching other nodes
      for (i = 0, redge = node_int_ptr->get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
        I(redge->get_self_idx() == idx2);
        I(!redge->is_input());

        auto other_nid = node_internal.ref(redge->get_idx())->get_nid();
        Node other_sink(this, this, Hierarchy::non_hierarchical(), other_nid);
        node_list.emplace_back(other_sink);
      }

      for (auto &other_node : node_list) {
        del_sink2node_int(node, other_node);
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
      node_internal.ref_unlock();
      return;
    }
    idx2 = node_int_ptr->get_next();
    node_int_ptr->try_recycle();
  }

  node_internal.ref_unlock();
}

void Lgraph::del_sink2node_int(const Node &driver, Node &sink) {
  I(driver.get_class_lgraph() == driver.get_top_lgraph());
  I(sink.get_class_lgraph() == sink.get_top_lgraph());
  I(sink.get_class_lgraph() == driver.get_top_lgraph());

  Index_id idx2         = sink.get_nid();
  auto    *node_int_ptr = node_internal.ref(idx2);
  node_int_ptr->clear_full_hint();

  Index_id last_idx = idx2;

  while (true) {
    auto n = node_int_ptr->get_num_local_inputs();
    if (n) {
      int             n_deleted = 0;
      uint8_t         i;
      const Edge_raw *redge;
      for (i = 0, redge = node_int_ptr->get_input_begin(); i < n; i++) {
        I(redge->get_self_idx() == idx2);
        auto master_nid = node_internal.ref(redge->get_idx())->get_nid();
        if (master_nid == driver.get_nid()) {
          node_int_ptr->del_input_int(redge);
          n_deleted++;
        } else {
          redge += redge->next_node_inc();  // NOTE: delete copies data, sort of advances the pointer
        }
      }
      if (n_deleted == n) {
        try_del_node_int(last_idx, idx2);  // can delete idx2
        if (node_int_ptr->is_free_state()) {
          idx2 = last_idx;
        }
      }
    }
    if (node_internal.ref(idx2)->is_last_state()) {  // no ptr because it may be deleted
      return;
    }

    last_idx     = idx2;
    idx2         = node_internal.ref(idx2)->get_next();
    node_int_ptr = node_internal.ref(idx2);
  }
}

void Lgraph::try_del_node_int(Index_id last_idx, Index_id idx) {
  (void)last_idx;
  (void)idx;
  return;
}

bool Lgraph::del_edge_driver_int(const Node_pin &dpin, const Node_pin &spin) {
  // WARNING: The edge can be anywhere from get_node().nid to end BUT more
  // likely to find it early starting from idx. Start from idx, and go back to
  // start (nid) again once at the end. If idx again, then it is not anywhere.

  GI(!spin.is_invalid(), dpin.get_class_lgraph() == dpin.get_top_lgraph());
  GI(!spin.is_invalid(), spin.get_class_lgraph() == spin.get_top_lgraph());
  GI(!spin.is_invalid(), spin.get_class_lgraph() == dpin.get_top_lgraph());

  auto root_idx = dpin.get_root_idx();

  node_internal.ref_lock();

  node_internal.ref(root_idx)->clear_full_hint();

  Index_id idx2         = dpin.get_idx();
  auto    *node_int_ptr = node_internal.ref(idx2);

  Index_id last_idx = idx2;

  Index_id spin_root_idx = 0;
  if (!spin.is_invalid()) {
    node_internal.ref_unlock();
    spin_root_idx = spin.get_root_idx();
    node_internal.ref_lock();
  }

  while (true) {
    I(node_int_ptr->get_dst_pid() == dpin.get_pid());

    auto            n = node_int_ptr->get_num_local_outputs();
    uint8_t         i;
    const Edge_raw *redge;

    for (i = 0, redge = node_int_ptr->get_output_begin(); i < n; i++, redge += redge->next_node_inc()) {
      I(redge->get_self_idx() == idx2);
      if (spin_root_idx == 0 || redge->get_idx() == spin_root_idx) {
        GI(spin_root_idx, redge->get_inp_pid() == spin.get_pid());
        node_int_ptr->del_output_int(redge);
        try_del_node_int(last_idx, idx2);
        if (spin_root_idx) {
          node_internal.ref_unlock();
          return true;
        }
      }
    }

    do {
      // Just look for next idx2 with same pid
      if (node_int_ptr->is_last_state()) {
        idx2 = node_internal.ref(idx2)->get_nid();  // idx2 may not be master
        idx2 = node_internal.ref(idx2)->get_nid();
        I(idx2 == dpin.get_node().get_nid());
        last_idx = idx2;
      }
      Index_id tmp = node_internal.ref(idx2)->get_next();
      if (tmp == dpin.get_idx()) {
        node_internal.ref_unlock();
        return false;
      }
      last_idx     = idx2;
      idx2         = tmp;
      node_int_ptr = node_internal.ref(idx2);
    } while (node_int_ptr->get_dst_pid() != dpin.get_pid());
  }

  node_internal.ref_unlock();
  return false;
}

bool Lgraph::del_edge_sink_int(const Node_pin &dpin, const Node_pin &spin) {
  // WARNING: The edge can be anywhere from get_node().nid to end BUT more
  // likely to find it early starting from idx. Start from idx, and go back to
  // start (nid) again once at the end. If idx again, then it is not anywhere.

  GI(!dpin.is_invalid(), dpin.get_class_lgraph() == dpin.get_top_lgraph());
  GI(!dpin.is_invalid(), spin.get_class_lgraph() == spin.get_top_lgraph());
  GI(!dpin.is_invalid(), spin.get_class_lgraph() == dpin.get_top_lgraph());

  Index_id idx2     = spin.get_idx();
  auto     root_idx = spin.get_root_idx();

  node_internal.ref_lock();
  auto *node_int_ptr = node_internal.ref(idx2);
  node_internal.ref(root_idx)->clear_full_hint();

  Index_id dpin_root_idx = 0;
  if (!dpin.is_invalid()) {
    node_internal.ref_unlock();
    dpin_root_idx = dpin.get_root_idx();
    node_internal.ref_lock();
  }

  Index_id last_idx = idx2;
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
        if (dpin_root_idx) {
          node_internal.ref_unlock();
          return true;
        }
      }
    }
    do {
      // Just look for next idx2 with same pid
      if (node_internal.ref(idx2)->is_last_state()) {
        idx2     = spin.get_node().get_nid();
        last_idx = idx2;
      }
      Index_id tmp = node_internal.ref(idx2)->get_next();
      if (tmp == spin.get_idx()) {
        node_internal.ref_unlock();
        return false;
      }

      last_idx     = idx2;
      idx2         = tmp;
      node_int_ptr = node_internal.ref(idx2);
    } while (node_int_ptr->get_dst_pid() != spin.get_pid());
  }

  node_internal.ref_unlock();
  return false;
}

void Lgraph::del_edge(const Node_pin &dpin, const Node_pin &spin) {
  I(dpin.is_driver());
  I(spin.is_sink());

  I(spin.get_top_lgraph() == dpin.get_top_lgraph());

  bool found = del_edge_driver_int(dpin, spin);
  if (!found)
    return;

  found = del_edge_sink_int(dpin, spin);
  I(found);

  return;
}

Node Lgraph::create_node() {
  Index_id nid = create_node_int();
  return Node(this, Hierarchy::hierarchical_root(), nid);
}

Node Lgraph::create_node(const Node &old_node) {
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

Node Lgraph::create_node(const Ntype_op op) {
  Index_id nid = create_node_int();
  set_type(nid, op);

  I(op != Ntype_op::IO);   // Special case, must use add input/output API
  I(op != Ntype_op::Sub);  // Do not build by steps. call create_node_sub

  return Node(this, Hierarchy::hierarchical_root(), nid);
}

Node Lgraph::create_node(const Ntype_op op, Bits_t bits) {
  auto node = create_node(op);

  I(!Ntype::is_multi_driver(op));
  node.setup_driver_pin().set_bits(bits);

  return node;
}

Node Lgraph::create_node_const(const Lconst &value) {
  // WARNING: There is a const_map, but it is NOT a bimap (speed). Just from
  // nid to const.
  Index_id nid = memoize_const_hint[value.hash() % memoize_const_hint.size()];
  if (nid == 0 || nid >= node_internal.size() || !node_internal[nid].is_valid() || node_internal[nid].get_type() != Ntype_op::Const
      || get_type_const(nid) != value || get_type_const(nid).get_bits() != value.get_bits()) {
    nid = create_node_int();
    set_type_const(nid, value);
    memoize_const_hint[value.hash() % memoize_const_hint.size()] = nid;
  }

  I(node_internal[nid].get_dst_pid() == 0);

  return Node(this, Hierarchy::hierarchical_root(), nid);
}

Node Lgraph::create_node_lut(const Lconst &lut) {
  auto nid = create_node().get_nid();
  set_type_lut(nid, lut);

  return Node(this, Hierarchy::hierarchical_root(), nid);
}

Node Lgraph::create_node_sub(Lg_type_id sub_id) {
  I(get_lgid() != sub_id);  // It can not point to itself (in fact, no recursion of any type)

  auto nid = create_node().get_nid();
  set_type_sub(nid, sub_id);

  return Node(this, Hierarchy::hierarchical_root(), nid);
}

Node Lgraph::create_node_sub(const mmap_lib::str &sub_name) {

  auto  nid = create_node().get_nid();
  auto &sub = library->setup_sub(sub_name);
  set_type_sub(nid, sub.get_lgid());

  return Node(this, Hierarchy::hierarchical_root(), nid);
}

const Sub_node &Lgraph::get_self_sub_node() const { return library->get_sub(get_lgid()); }

Sub_node *Lgraph::ref_self_sub_node() { return library->ref_sub(get_lgid()); }

void Lgraph::trace_back2driver(Node_pin_iterator &xiter, const Node_pin &dpin, const Node_pin &spin) {
  I(dpin.is_hierarchical());
  I(dpin.is_driver());
  I(spin.is_invalid() || dpin.get_top_lgraph() == spin.get_top_lgraph());

  if (dpin.is_graph_input() && dpin.is_down_node()) {
    auto up_pin = dpin.get_up_pin();
    if (up_pin.is_connected()) {
      for (auto &e : up_pin.inp_edges()) {
        trace_back2driver(xiter, e.driver, spin);
      }
    } else {
      xiter.emplace_back(dpin);
    }
  } else if (dpin.get_node().is_type_sub_present()) {
    auto down_pin = dpin.get_down_pin();
    if (down_pin.is_connected()) {
      for (auto &e : down_pin.inp_edges()) {
        trace_back2driver(xiter, e.driver, spin);
      }
    } else {
      xiter.emplace_back(dpin);
    }
  } else {
    xiter.emplace_back(dpin);
  }
}

void Lgraph::trace_forward2sink(XEdge_iterator &xiter, const Node_pin &dpin, const Node_pin &spin) {
  I(spin.is_hierarchical());
  I(spin.is_sink());

  if (spin.is_graph_output() && spin.is_down_node()) {
    auto up_pin = spin.get_up_pin();
    if (up_pin.is_connected()) {
      for (auto &e : up_pin.out_edges()) {
        trace_forward2sink(xiter, dpin, e.sink);
      }
    } else {
      xiter.emplace_back(dpin, spin);
    }
  } else if (spin.get_node().is_type_sub_present()) {
    auto down_pin = spin.get_down_pin();
    if (down_pin.is_connected()) {
      for (auto &e : down_pin.out_edges()) {
        trace_forward2sink(xiter, dpin, e.sink);
      }
    } else {
      xiter.emplace_back(dpin, spin);
    }
  } else {
    xiter.emplace_back(dpin, spin);
  }
}

void Lgraph::add_edge(const Node_pin &dpin, const Node_pin &spin) {
  I(dpin.is_driver());
  I(spin.is_sink());
  I(spin.get_top_lgraph() == dpin.get_top_lgraph());

  add_edge_int(spin.get_root_idx(), spin.get_pid(), dpin.get_root_idx(), dpin.get_pid());
}

Fwd_edge_iterator Lgraph::forward(bool visit_sub) { return Fwd_edge_iterator(this, visit_sub); }
Bwd_edge_iterator Lgraph::backward(bool visit_sub) { return Bwd_edge_iterator(this, visit_sub); }

// Skip after 1, but first may be deleted, so fast_next
Fast_edge_iterator Lgraph::fast(bool visit_sub) { return Fast_edge_iterator(this, visit_sub); }

void Lgraph::dump() {
  fmt::print("lgraph name: {}, size: {}\n", name, node_internal.size());

  for (const auto &io_pin : get_self_sub_node().get_io_pins()) {
    if (io_pin.is_invalid())
      continue;
    fmt::print("  lgraph io name: {}, port pos: {}, pid: {}, i/o: {}\n",
               io_pin.name,
               io_pin.graph_io_pos,
               get_self_sub_node().get_instance_pid(io_pin.name),
               io_pin.is_input() ? "input" : "output");
  }

  fmt::print("\n");

#if 1
  node_internal.ref_lock();
  for (size_t i = 0; i < node_internal.size(); ++i) {
    if (!node_internal.ref(i)->is_node_state())
      continue;
    if (!node_internal.ref(i)->is_master_root())
      continue;
    auto node = Node(this, Node::Compact_class(i));  // NOTE: To remove once new iterators are finished

    node.dump();
  }
  node_internal.ref_unlock();
#endif

  fmt::print("\n");
  each_local_unique_sub_fast([](Lgraph *sub_lg) -> bool {
    fmt::print("  sub lgraph name:{}\n", sub_lg->get_name());

    return true;
  });
}

void Lgraph::dump_down_nodes() {
  for (auto &cnode : subid_map) {
    fmt::print(" sub:{}\n", cnode.first.get_node(this).debug_name());
  }
}

Node Lgraph::get_graph_input_node(bool hier) {
  if (hier)
    return Node(this, Hierarchy::hierarchical_root(), Hardcoded_input_nid);
  else
    return Node(this, Hierarchy::non_hierarchical(), Hardcoded_input_nid);
}

Node Lgraph::get_graph_output_node(bool hier) {
  if (hier)
    return Node(this, Hierarchy::hierarchical_root(), Hardcoded_output_nid);
  else
    return Node(this, Hierarchy::non_hierarchical(), Hardcoded_output_nid);
}

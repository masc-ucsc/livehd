//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraphbase.hpp"

#include <iostream>
#include <set>

#include "attribute.hpp"
#include "graph_library.hpp"
#include "iassert.hpp"
#include "lgedgeiter.hpp"
#include "pass.hpp"

// Checks internal invalid insertions. Worth only if the node_internal is patched
// #define DEBUG_SLOW

LGraph_Base::LGraph_Base(std::string_view _path, std::string_view _name, Lg_type_id _lgid) noexcept
    : Lgraph_base_core(_path, _name, _lgid), node_internal(path, absl::StrCat("lg_", std::to_string(_lgid), "_nodes")) {
  I(lgid);  // No id zero allowed

  library = Graph_library::instance(path);
}

LGraph_Base::~LGraph_Base() {
  // TODO: This is NOT a bug. The reason is that we need to preserve the
  // string pointers for graph name. Then, we can use string_view maps for all
  // of them. Otherwise, we can not.
  //
  // If deleting becomes a problem, we should preserve the graph names as a
  // table that lives forever once a graph/lgraph is opened.
  //
  // I(false);
}

LGraph_Base::_init::_init() {
  // fmt::print("LGraph_Base static init done\n");
  // Add here sequence of static initialization that may be needed
}

void LGraph_Base::clear() {
  node_internal.clear();

  Lgraph_base_core::clear();

  library->clear(lgid);
}

void LGraph_Base::sync() {
  Lgraph_base_core::sync();
  library->sync();
}

void LGraph_Base::emplace_back() {
  I(locked);

  node_internal.emplace_back();

  Index_ID nid = node_internal.size() - 1;
  if (!node_internal.back().is_page_align()) {
    new (node_internal.ref(nid)) Node_Internal();  // call constructor
    node_internal.ref(nid)->set_nid(nid);          // self by default
  }
}

Index_ID LGraph_Base::create_node_space(const Index_ID last_idx, const Port_ID dst_pid, const Index_ID master_nid,
                                        const Index_ID root_idx) {
  Index_ID idx2 = create_node_int();

  I(dst_pid<(1<<Port_bits));

  I(node_internal[master_nid].is_master_root());
  I(node_internal[last_idx].get_master_root_nid() == master_nid);

  auto *nidx2 = node_internal.ref(idx2);

  if (root_idx) {
    I(node_internal[root_idx].is_root());
    nidx2->clear_root();
    nidx2->set_nid(root_idx);
  } else {
    I(nidx2->is_root());
    nidx2->set_nid(master_nid);
  }
  I(nidx2->get_master_root_nid() == master_nid);

  nidx2->set_dst_pid(dst_pid);

  if (node_internal[last_idx].has_next_space()) {
    node_internal.ref(last_idx)->push_next_state(idx2);
    I(node_internal[idx2].has_space_long());
    return idx2;
  }

  const auto &dbg_master = node_internal[last_idx].get_master_root();
  int32_t     dbg_ni     = dbg_master.get_node_num_inputs();
  int32_t     dbg_no     = dbg_master.get_node_num_outputs();

  if (node_internal[last_idx].get_dst_pid() == dst_pid) {
    I(root_idx);
    // Nove stuff to idx2, legal

    node_internal.ref(idx2)->assimilate_edges(node_internal.ref(last_idx));
    I(node_internal[last_idx].has_next_space());

    I(node_internal[last_idx].get_master_root_nid() == master_nid);
    I(node_internal[idx2].get_master_root_nid() == master_nid);

    node_internal.ref(last_idx)->push_next_state(idx2);

    I(dbg_master.get_node_num_inputs() == dbg_ni);
    I(dbg_master.get_node_num_outputs() == dbg_no);

    if (!node_internal[last_idx].has_space_long()) {
      if (node_internal[idx2].has_space_long()) return idx2;
      // This can happen if 3 sedges transfered to 3 ledges in dest
      return create_node_space(idx2, dst_pid, master_nid, root_idx);
    }
    return last_idx;
  }

  // last_idx (pid1) -> idx2 (dst_pid) -> idx3 (pid1)

  Index_ID idx3 = create_node_int();

  // make space in idx so that we can push_next_state
  node_internal.ref(idx3)->set_dst_pid(node_internal[last_idx].get_dst_pid());
  node_internal.ref(idx3)->clear_root();
  if (node_internal[last_idx].is_root())
    node_internal.ref(idx3)->set_nid(last_idx);
  else
    node_internal.ref(idx3)->set_nid(node_internal[last_idx].get_nid());
  if (!node_internal[last_idx].has_next_space()) node_internal.ref(idx3)->assimilate_edges(node_internal.ref(last_idx));

  I(node_internal[last_idx].has_next_space());
  I(node_internal[idx2].has_next_space());
  // Link chain
  node_internal.ref(last_idx)->push_next_state(idx2);
  node_internal.ref(idx2)->push_next_state(idx3);

  // May or may not be I( node_internal[last_idx ].is_root());
  I(!node_internal[idx3].is_root());

  I(!node_internal[last_idx].is_last_state());
  I(!node_internal[idx2].is_last_state());
  I(node_internal[idx3].is_last_state());

  I(node_internal[last_idx].get_master_root_nid() == master_nid);
  I(node_internal[idx3].get_master_root_nid() == master_nid);
  I(node_internal[idx2].get_master_root_nid() == master_nid);

  I(node_internal[last_idx].get_master_root().get_node_num_inputs() == dbg_ni);
  I(node_internal[last_idx].get_master_root().get_node_num_outputs() == dbg_no);

  I(node_internal[idx2].has_space_long());
  return idx2;
}

void LGraph_Base::print_stats() const {
  double bytes = 0;

  size_t n_nodes       = 1;
  size_t n_extra       = 1;
  size_t n_master      = 1;
  size_t n_roots       = 1;
  size_t n_long_edges  = 1;
  size_t n_short_edges = 1;
  for (size_t i = 0; i < node_internal.size(); i++) {
    if (node_internal[i].is_node_state()) {
      n_long_edges += node_internal[i].get_num_local_long();
      n_short_edges += node_internal[i].get_num_local_short();
      n_nodes++;
      if (node_internal[i].is_root()) {
        n_roots++;
        if (node_internal[i].is_master_root()) n_master++;
      }
    } else {
      n_extra++;
    }
  }

  bytes += node_internal.size() * sizeof(Node_Internal);
  // bytes += node_type_op.size() * sizeof(Node_Type_Op);
  // bytes += node_delay.size()    * sizeof(Node_Delay);
  auto n_edges = n_short_edges + n_long_edges;

  fmt::print("path:{} name:{}\n", path, name);
  fmt::print("  size:{} kbytes:{} bytes/node:{:.2f} bytes/edge:{:.2f} edges/master:{:.2f}\n", node_internal.size(), bytes / 1024,
             bytes / (1 + n_nodes), bytes / (1 + n_edges), (double)n_edges/(1+n_master));
  fmt::print("  total master:{} root:{} node:{} extra:{} root/ratio:{:.2f} extra/ratio:{:.2f}\n", n_master, n_roots, n_nodes, n_extra,
             n_roots / (1.0 + n_nodes + n_extra), n_extra / (1.0 + n_nodes + n_extra));
  fmt::print("  total bytes/master:{:.2f} bytes/root:{:.2f} bytes/node:{:.2f} bytes/extra:{:.2f}\n", bytes / n_master, bytes / n_roots,
             bytes / n_nodes, bytes / n_extra);

  bytes = node_internal.size() * sizeof(Node_Internal) + 1;
  fmt::print("  edges bytes/root:{:.2f} bytes/node:{:.2f} bytes/extra:{:.2f}\n", bytes / n_roots, bytes / n_nodes, bytes / n_extra);
  bytes = n_nodes + 1;
  fmt::print("  edges short/node:{:.2f} long/node:{:.2f} short/ratio:{:.2f}\n", n_short_edges / bytes, n_long_edges / bytes,
             (n_short_edges) / (1.0 + n_short_edges + n_long_edges));
}

Index_ID LGraph_Base::get_space_output_pin(const Index_ID master_nid, const Index_ID start_nid, const Port_ID dst_pid,
                                           const Index_ID root_idx) {
#ifdef DEBUG_SLOW
  I(node_internal[master_nid].is_root());
  I(node_internal[start_nid].is_node_state());
#endif
  if (node_internal[start_nid].get_dst_pid() == dst_pid && node_internal[start_nid].has_space_long()) {
    return start_nid;
  }

  // Look for space
  Index_ID idx = start_nid;

  while (true) {
    if (node_internal[idx].get_dst_pid() == dst_pid) {
      // GI(node_internal[idx].is_root(), root_idx == idx);

      if (node_internal[idx].has_space_long()) return idx;
    }

    if (node_internal[idx].is_last_state()) return create_node_space(idx, dst_pid, master_nid, root_idx);

    Index_ID idx2 = node_internal[idx].get_next();
#ifdef DEBUG_SLOW
    I(node_internal[idx2].get_master_root_nid() == node_internal[idx].get_master_root_nid());
#endif
    idx = idx2;
  }

  I(false);

  return 0;
}

Index_ID LGraph_Base::find_idx_from_pid_int(const Index_ID idx, const Port_ID pid) const {
  I(node_internal[idx].get_dst_pid() != pid);
  Index_ID nid  = node_internal[idx].get_master_root_nid();
  Index_ID idx2 = nid;

  while (true) {
    if (node_internal[idx2].get_dst_pid() == pid && node_internal[idx2].is_root()) {
      return idx2;
    }

    if (node_internal[idx2].is_last_state()) {
      return 0;
    }

    idx2 = node_internal[idx2].get_next();
    I(node_internal[idx2].get_master_root_nid() == nid);
  }

  I(false);
  return 0;
}

Index_ID LGraph_Base::setup_idx_from_pid(const Index_ID nid, const Port_ID pid) {
  Index_ID pos = find_idx_from_pid(nid, pid);
  if (pos) {
    I(node_internal[pos].is_root());
    return pos;
  }

  Index_ID root_idx = 0;
  Index_ID idx_new  = get_space_output_pin(nid, pid, root_idx);
  if (root_idx == 0) root_idx = idx_new;

  I(node_internal[root_idx].is_root());
  I(node_internal[root_idx].get_dst_pid() == pid);

  return root_idx;
}

void LGraph_Base::set_bits_pid(const Index_ID nid, const Port_ID pid, uint32_t bits) {
  Index_ID idx = setup_idx_from_pid(nid, pid);
  set_bits(idx, bits);
}

uint32_t LGraph_Base::get_bits_pid(const Index_ID nid, const Port_ID pid) const {
  I(node_internal.size() > nid);
  I(node_internal[nid].is_master_root());
  Index_ID idx = find_idx_from_pid(nid, pid);
  return get_bits(idx);
}

uint32_t LGraph_Base::get_bits_pid(const Index_ID nid, const Port_ID pid) {
  Index_ID idx = setup_idx_from_pid(nid, pid);
  return get_bits(idx);
}

Index_ID LGraph_Base::get_space_output_pin(const Index_ID start_nid, const Port_ID dst_pid, Index_ID &root_idx) {
  I(node_internal[start_nid].is_root());
  I(node_internal[start_nid].is_node_state());
  if (node_internal[start_nid].has_space_short() && node_internal[start_nid].get_dst_pid() == dst_pid) {
    root_idx = start_nid;
    return start_nid;
  }

  Index_ID idx = start_nid;

  while (true) {
    if (node_internal[idx].get_dst_pid() == dst_pid) {
      if (node_internal[idx].is_root()) root_idx = idx;

      if (node_internal[idx].has_space_short()) {
        GI(root_idx != start_nid, node_internal[start_nid].get_dst_pid() != node_internal[root_idx].get_dst_pid());
        I(node_internal[root_idx].is_root());

        return idx;
      }
    }

    if (node_internal[idx].is_last_state()) {
      Index_ID idx_new;
      idx_new = create_node_space(idx, dst_pid, start_nid, root_idx);
      if (root_idx == 0) root_idx = idx_new;
      I(node_internal[root_idx].is_root());
      return idx_new;
    }

    Index_ID idx2 = node_internal[idx].get_next();
    I(node_internal[idx2].get_master_root_nid() == node_internal[idx].get_master_root_nid());
    idx = idx2;
  }

  I(false);

  return 0;
}

#if 0
Index_ID LGraph_Base::get_space_input_pin(const Index_ID start_nid, const Index_ID idx, bool large) {
  // FIXME: Change the get_space_input_pin to insert when dst_pid matches. Then
  // the forward and reverse edge can have src/dst pid
  //
  // The interface has to become more closer to get_space_output_pin

  I(large || node_internal[idx].is_node_state());
  if (node_internal[idx].has_space(large)) return idx;

  Index_ID idx2 = idx;
  // Look for space
  while (true) {
    if (node_internal[idx2].has_space(large)) return idx2;

    if (node_internal[idx2].is_last_state()) {
      I(node_internal[start_nid].is_root());
      Port_ID dst_pid = node_internal[start_nid].get_dst_pid();  // Inputs can go anywhere
      return create_node_space(idx2, dst_pid, start_nid, start_nid);
    }

    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  I(false);

  return 0;
}
#endif

Index_ID LGraph_Base::add_edge_int(const Index_ID dst_idx, const Port_ID inp_pid, Index_ID src_idx, Port_ID dst_pid) {
  bool            sused;
  SEdge_Internal *sedge;
  LEdge_Internal *ledge;

  I(node_internal.size() > src_idx);
  I(node_internal.size() > dst_idx);

  // Do not point to intermediate nodes which can be remapped, just root nodes
  I(node_internal[dst_idx].is_root());
  I(node_internal[src_idx].is_root());

  Index_ID src_nid = node_internal[src_idx].get_master_root_nid();
  Index_ID dst_nid = node_internal[dst_idx].get_master_root_nid();

  I(node_internal[dst_idx].is_node_state());
  I(node_internal[src_idx].is_node_state());

#ifdef DEBUG_SLOW
  // Do not insert twice check (too slow for many benchmarks)
  {
    for (const auto &v : out_edges_raw(src_nid)) {
      if (v.get_idx() == dst_idx && v.get_dst_pid() == dst_pid && v.get_inp_pid() == inp_pid) {
        I(false);  // edge added twice
      }
    }
  }
#endif

  Index_ID idx;
  //-----------------------
  // ADD output edge in source (src_nid) with dst_pid to the destination (dst_idx) with inp_pid
  Index_ID root_idx = src_idx;
  idx               = get_space_output_pin(src_nid, src_idx, dst_pid, root_idx);
  I(root_idx != 0);
  I(node_internal[root_idx].is_root());

  int o = node_internal[idx].next_free_output_pos();

  sedge = (SEdge_Internal *)&node_internal[idx].sedge[o];
  sused = sedge->set(dst_idx, inp_pid, false);

  if (sused) {
    node_internal.ref(idx)->inc_outputs();
  } else {
    idx = get_space_output_pin(src_nid, idx, dst_pid, root_idx);
    o   = node_internal.ref(idx)->next_free_output_pos() - (2 - 1);

    ledge = (LEdge_Internal *)(&node_internal[idx].sedge[o]);
    ledge->set(dst_idx, inp_pid, false);

    node_internal.ref(idx)->inc_outputs(true);  // WARNING: Before next_free_output_pos to reserve space (decreasing insert)
  }
  Index_ID out_idx = idx;

  //-----------------------
  // Reverse from before:
  // ADD input edge in destination (dst_nid) with ANY inp_pid (0) to  (src_nid) with dst_pid

  Index_ID inp_root_nid = dst_idx;
  idx                   = get_space_output_pin(dst_nid, dst_idx, inp_pid, inp_root_nid);
  I(inp_root_nid != 0);
  I(node_internal[inp_root_nid].is_root());
  int i = node_internal[idx].next_free_input_pos();

  sedge = (SEdge_Internal *)&node_internal[idx].sedge[i];
  sused = sedge->set(src_idx, dst_pid, true);

  if (sused) {
    node_internal.ref(idx)->inc_inputs();
  } else {
    idx   = get_space_output_pin(dst_nid, idx, inp_pid, inp_root_nid);
    i     = node_internal[idx].next_free_input_pos();
    ledge = (LEdge_Internal *)(&node_internal[idx].sedge[i]);
    ledge->set(src_idx, dst_pid, true);

    node_internal.ref(idx)->inc_inputs(true);  // WARNING: after next_free_input_pos (increasing insert)
  }

#ifdef DEBUG_SLOW
  Index_ID master_nid = src_nid;
  I(node_internal[master_nid].is_master_root());
  Index_ID          j = master_nid;
  std::set<Port_ID> mset;

  while (true) {
    if (node_internal[j].is_root()) {
      if (mset.find(node_internal[j].get_dst_pid()) != mset.end())
        fmt::print("multiple nid:{} port:{}\n", j, node_internal[j].get_dst_pid());

      I(mset.find(node_internal[j].get_dst_pid()) == mset.end());
    }
    mset.insert(node_internal[j].get_dst_pid());

    if (node_internal[j].is_last_state()) break;
    j = node_internal[j].get_next();
  }
#endif

  if (root_idx != src_nid) I(node_internal[src_nid].get_dst_pid() != node_internal[root_idx].get_dst_pid());

  if (node_internal[src_nid].get_dst_pid() == node_internal[out_idx].get_dst_pid() && src_nid != out_idx)
    I(!node_internal[out_idx].is_root());

  I(node_internal[root_idx].is_root());
  return root_idx;
}

void LGraph_Base::del_edge(const Edge_raw *edge_raw) {
  I(edge_raw->get_idx() < node_internal.size());
  I(edge_raw->get_self_idx() < node_internal.size());
  Node_Internal::get(edge_raw).del(edge_raw);
}

void LGraph_Base::del_node(const Index_ID idx) {
  // TODO: do this more effiently (no need to build iterator)

  fmt::print("del_node {}\n", idx);

  I(node_internal[idx].is_master_root());
  // Deleting can break the iterator. Restart each time
  bool deleted;
  do {
    deleted = false;
    for (auto &c : inp_edges_raw(idx)) {
      fmt::print("del_node {} inp {}\n", idx, c.get_self_idx());
      I(Node_Internal::get(&c).get_master_root_nid() == idx);
      node_internal.ref(c.get_self_idx())->del(&c);
      deleted = true;
      break;
    }
  } while (deleted);

  do {
    deleted = false;
    for (auto &c : out_edges_raw(idx)) {
      fmt::print("del_node {} out {}\n", idx, c.get_self_idx());
      I(Node_Internal::get(&c).get_master_root_nid() == idx);
      node_internal.ref(c.get_self_idx())->del(&c);
      deleted = true;
      break;
    }
  } while (deleted);

  I(node_internal[idx].get_inp_pos() == 0);
  I(node_internal[idx].get_out_pos() == 0);

  // clear child nodes
  del_int_node(idx);
}

void LGraph_Base::del_int_node(const Index_ID idx) {
  if (!node_internal[idx].is_last_state()) {
    del_int_node(node_internal[idx].get_next());
  }
  node_internal.ref(idx)->reset();
}

Edge_raw_iterator LGraph_Base::out_edges_raw(const Index_ID idx) const {
  Index_ID idx2 = node_internal[idx].get_master_root_nid();
  I(node_internal[idx2].is_master_root());

  const SEdge *s = 0;
  while (true) {
    s = node_internal[idx2].get_output_begin();
    if (node_internal[idx2].is_last_state()) break;
    if (node_internal[idx2].has_local_outputs()) break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  const SEdge *e = 0;
  if (node_internal[idx2].has_local_outputs()) e = node_internal[idx2].get_output_end();

  while (true) {
    if (node_internal[idx2].is_last_state()) break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
    if (node_internal[idx2].has_local_outputs()) e = node_internal[idx2].get_output_end();
  }

  if (e == 0)  // empty list of outputs
    e = s;

  I(Node_Internal::get(e).is_node_state());
  return Edge_raw_iterator(s, e, false);
}

Edge_raw_iterator LGraph_Base::inp_edges_raw(const Index_ID idx) const {
  I(node_internal[idx].is_master_root());

  const SEdge *s = 0;

  Index_ID idx2 = idx;

  while (true) {
    s = node_internal[idx2].get_input_begin();
    if (node_internal[idx2].is_last_state()) break;
    if (node_internal[idx2].has_local_inputs()) break;
    Index_ID tmp = node_internal[idx2].get_next();
    if (tmp >= node_internal.size()) break;
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
  }

  const SEdge *e = 0;
  if (node_internal[idx2].has_local_inputs()) e = node_internal[idx2].get_input_end();

  while (true) {
    if (node_internal[idx2].is_last_state()) break;
    Index_ID tmp = node_internal[idx2].get_next();
    I(node_internal[tmp].get_master_root_nid() == node_internal[idx2].get_master_root_nid());
    idx2 = tmp;
    if (node_internal[idx2].has_local_inputs()) {
      e = node_internal[idx2].get_input_end();
    }
  }

  if (e == 0)  // empty list of inputs
    e = s;

  return Edge_raw_iterator(s, e, true);
}

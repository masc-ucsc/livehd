//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraphbase.hpp"

#include <iostream>
#include <set>

#include "attribute.hpp"
#include "graph_library.hpp"
#include "iassert.hpp"
#include "lgedgeiter.hpp"

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

  idx_insert_cache.clear();

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
  auto *   ptr = node_internal.ref(nid);

  if (ptr->is_page_align()) {
    auto *page = (Node_Internal_Page *)ptr;

    page->set_page(nid);
    node_internal.emplace_back();
    nid.value++;

    ptr = node_internal.ref(nid);
  }

  I(!ptr->is_page_align());
  new (ptr) Node_Internal();  // call constructor
  ptr->set_nid(nid);          // self by default
}

Index_ID LGraph_Base::create_node_int() {
  get_lock();  // FIXME: change to Copy on Write permissions (mmap exception, and remap)
  emplace_back();

  I(node_internal[node_internal.size() - 1].get_dst_pid() == 0);
  I(node_internal[node_internal.size() - 1].get_nid() == node_internal.size() - 1);
  return node_internal.size() - 1;
}

Index_ID LGraph_Base::create_node_space(const Index_ID last_idx, const Port_ID dst_pid, const Index_ID master_nid,
                                        const Index_ID root_idx) {
  Index_ID idx2 = create_node_int();

  I(dst_pid < (1 << Port_bits));

  I(node_internal[master_nid].is_master_root());
  I(node_internal[last_idx].get_master_root_nid() == master_nid);

  auto *nidx2 = node_internal.ref(idx2);
  nidx2->set_dst_pid(dst_pid);

  if (root_idx) {
    // There there are already 3 nodes, place after root (faster to find space in future checks)
    auto *root_ptr = node_internal.ref(root_idx);
    if (!root_ptr->is_last_state()) {
      auto root_next_idx = root_ptr->get_next();

      root_ptr->set_next_state(idx2);

      I(nidx2->has_next_space());
      nidx2->force_next_state(root_next_idx);
      nidx2->clear_root();
      nidx2->set_nid(root_idx);

      return idx2;
    }
  }

  if (root_idx) {
    I(node_internal[root_idx].is_root());
    nidx2->clear_root();
    nidx2->set_nid(root_idx);
  } else {
    I(nidx2->is_root());
    nidx2->set_nid(master_nid);
  }
  I(nidx2->get_master_root_nid() == master_nid);

  if (node_internal[last_idx].has_next_space()) {
    node_internal.ref(last_idx)->push_next_state(idx2);
    I(node_internal[idx2].has_space_long());
    return idx2;
  }

#ifdef DEBUG_SLOW
  const auto &dbg_master = node_internal[last_idx].get_master_root();
  int32_t     dbg_ni     = dbg_master.get_node_num_inputs();
  int32_t     dbg_no     = dbg_master.get_node_num_outputs();
#endif

  if (node_internal[last_idx].get_dst_pid() == dst_pid) {
    I(root_idx);
    // Nove stuff to idx2, legal

    node_internal.ref(idx2)->assimilate_edges(node_internal.ref(last_idx));
    I(node_internal[last_idx].has_next_space());

    I(node_internal[last_idx].get_master_root_nid() == master_nid);
    I(node_internal[idx2].get_master_root_nid() == master_nid);

    node_internal.ref(last_idx)->push_next_state(idx2);

#ifdef DEBUG_SLOW
    I(dbg_master.get_node_num_inputs() == dbg_ni);
    I(dbg_master.get_node_num_outputs() == dbg_no);
#endif

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

#ifdef DEBUG_SLOW
  I(node_internal[last_idx].get_master_root().get_node_num_inputs() == dbg_ni);
  I(node_internal[last_idx].get_master_root().get_node_num_outputs() == dbg_no);
#endif

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
             bytes / (1 + n_nodes), bytes / (1 + n_edges), (double)n_edges / (1 + n_master));
  fmt::print("  total master:{} root:{} node:{} extra:{} root/ratio:{:.2f} extra/ratio:{:.2f}\n", n_master, n_roots, n_nodes,
             n_extra, n_roots / (1.0 + n_nodes + n_extra), n_extra / (1.0 + n_nodes + n_extra));
  fmt::print("  total bytes/master:{:.2f} bytes/root:{:.2f} bytes/node:{:.2f} bytes/extra:{:.2f}\n", bytes / n_master,
             bytes / n_roots, bytes / n_nodes, bytes / n_extra);

  bytes = node_internal.size() * sizeof(Node_Internal) + 1;
  fmt::print("  edges bytes/root:{:.2f} bytes/node:{:.2f} bytes/extra:{:.2f}\n", bytes / n_roots, bytes / n_nodes, bytes / n_extra);
  bytes = n_nodes + 1;
  fmt::print("  edges short/node:{:.2f} long/node:{:.2f} short/ratio:{:.2f}\n", n_short_edges / bytes, n_long_edges / bytes,
             (n_short_edges) / (1.0 + n_short_edges + n_long_edges));
}

#if 0
static inline unsigned long long get_cycles(void) {
  unsigned int low, high;

  asm volatile("rdtsc" : "=a" (low), "=d" (high));

  return low | ((unsigned long long)high) << 32;
}
#endif

Index_ID LGraph_Base::get_space_output_pin(const Index_ID master_nid, const Index_ID start_nid, const Port_ID dst_pid,
                                           const Index_ID root_idx) {

#if 0
  auto start = get_cycles();
  static long long total_cycles_1 = 0;
  static long long total_1 = 0;
  static long long total_cycles_2 = 0;
  static long long total_2 = 0;
  static long long total_cycles_3 = 0;
  static long long total_3 = 0;
#endif

#ifdef DEBUG_SLOW
  I(node_internal[master_nid].is_root());
  I(node_internal[start_nid].is_node_state());
#endif
  const auto *ptr = node_internal.ref(start_nid);
  if (ptr->get_dst_pid() == dst_pid && ptr->has_space_long()) {
#if 0
      auto end = get_cycles();
      total_cycles_1+= (end-start);
      total_1++;
      static int conta=0;
      if (conta++>1000) {
        fmt::print("_1:{}/{} _2:{}/{} _3:{}/{}\n", total_cycles_1, total_1, total_cycles_2, total_2, total_cycles_3, total_3);
        conta = 0;
      }
#endif
    return start_nid;
  }

  // Look for space
  Index_ID idx = start_nid;

  // Trick to avoid checking frequently that there is extra space. Most of the
  // time the list if full, no need to traverse. If traversed, it will find a
  // node and set it to find.
  if (node_internal[root_idx].is_next_state()) {
    static int conta = 5;
    if (conta > 0) {
      conta--;
      return create_node_space(idx, dst_pid, master_nid, root_idx);
    } else {
      conta = 5;
    }
  }

  while (true) {
    if (ptr->is_last_state()) {
#if 0
        auto end = get_cycles();
        total_cycles_2 += (end - start);
        total_2++;
#endif
      return create_node_space(idx, dst_pid, master_nid, root_idx);
    }

    Index_ID idx2 = ptr->get_next();
#ifdef DEBUG_SLOW
    I(node_internal[idx2].get_master_root_nid() == ptr->get_master_root_nid());
#endif
    idx = idx2;

    ptr = node_internal.ref(idx);

    if (ptr->get_dst_pid() == dst_pid && ptr->has_space_long()) {
#if 0
      auto end = get_cycles();
      total_cycles_3 += (end - start);
      total_3++;
#endif
      return idx;
    }
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

Index_ID LGraph_Base::add_edge_int(const Index_ID dst_idx, const Port_ID inp_pid, Index_ID src_idx, Port_ID dst_pid) {

  // Do not point to intermediate nodes which can be remapped, just root nodes
  I(node_internal[dst_idx].is_root());
  I(node_internal[src_idx].is_root());
  node_internal.ref(dst_idx)->set_sink_setup();
  node_internal.ref(src_idx)->set_driver_setup();

  Index_ID root_idx = src_idx;

  bool out_done = false;
  auto it = idx_insert_cache.find(src_idx);
  if (it != idx_insert_cache.end()) {
    auto idx = it->second;
    I(node_internal[idx].has_space_short());

    int o = node_internal[idx].next_free_output_pos();

    auto *sedge = (SEdge_Internal *)&node_internal[idx].sedge[o];
    bool sused = sedge->set(dst_idx, inp_pid, false);

    if (sused) {
        node_internal.ref(idx)->inc_outputs();
      out_done = true;
    } else {
      if (node_internal[idx].has_space_long()) {
        o--;

        auto *ledge = (LEdge_Internal *)(&node_internal[idx].sedge[o]);
        ledge->set(dst_idx, inp_pid, false);

        node_internal.ref(idx)->inc_outputs(true);
        out_done = true;
      }
    }

    if (!node_internal[idx].has_space_short())
      idx_insert_cache.erase(it);
  }

  if (!out_done) {
    Index_ID src_nid = node_internal[src_idx].get_master_root_nid();

    I(node_internal[dst_idx].is_node_state());
    I(node_internal[src_idx].is_node_state());

    //-----------------------
    // ADD output edge in source (src_nid) with dst_pid to the destination (dst_idx) with inp_pid
    auto idx = get_space_output_pin(src_nid, src_idx, dst_pid, root_idx);
    I(root_idx != 0);
    I(node_internal[root_idx].is_root());

    int o = node_internal[idx].next_free_output_pos();

    auto *sedge = (SEdge_Internal *)&node_internal[idx].sedge[o];
    bool sused = sedge->set(dst_idx, inp_pid, false);

    if (sused) {
      node_internal.ref(idx)->inc_outputs();
    } else {
#if 0
      idx = get_space_output_pin(src_nid, idx, dst_pid, root_idx);
      o   = node_internal.ref(idx)->next_free_output_pos() - (2 - 1);
#else
      o--;
#endif
      I(node_internal[idx].has_space_long());

      auto *ledge = (LEdge_Internal *)(&node_internal[idx].sedge[o]);
      ledge->set(dst_idx, inp_pid, false);

      node_internal.ref(idx)->inc_outputs(true);  // WARNING: Before next_free_output_pos to reserve space (decreasing insert)
    }

    if (node_internal[idx].has_space_short())
      idx_insert_cache[src_idx] = idx;
  }

  //-----------------------
  // Reverse from before:
  // ADD input edge in destination (dst_nid) with ANY inp_pid (0) to  (src_nid) with dst_pid
  bool inp_done = false;
  it = idx_insert_cache.find(dst_idx);
  if (it != idx_insert_cache.end()) {
    auto idx = it->second;
    I(node_internal[idx].has_space_short());

    int i = node_internal[idx].next_free_input_pos();

    auto *sedge = (SEdge_Internal *)&node_internal[idx].sedge[i];
    bool  sused = sedge->set(src_idx, dst_pid, true);

    if (sused) {
      node_internal.ref(idx)->inc_inputs();
      inp_done = true;
    } else {
      if (node_internal[idx].has_space_long()) {
        auto *ledge = (LEdge_Internal *)(&node_internal[idx].sedge[i]);
        ledge->set(src_idx, dst_pid, true);

        node_internal.ref(idx)->inc_inputs(true);  // WARNING: after next_free_input_pos (increasing insert)
        inp_done = true;
      }
    }

    if (!node_internal[idx].has_space_short())
      idx_insert_cache.erase(it);
  }

  if (!inp_done) {
    Index_ID dst_nid      = node_internal[dst_idx].get_master_root_nid();
    Index_ID inp_root_nid = dst_idx;
    Index_ID idx          = get_space_output_pin(dst_nid, dst_idx, inp_pid, inp_root_nid);
    I(inp_root_nid != 0);
    I(node_internal[inp_root_nid].is_root());
    int i = node_internal[idx].next_free_input_pos();

    auto *sedge = (SEdge_Internal *)&node_internal[idx].sedge[i];
    bool  sused = sedge->set(src_idx, dst_pid, true);

    if (sused) {
      node_internal.ref(idx)->inc_inputs();
    } else {
#if 0
      idx   = get_space_output_pin(dst_nid, idx, inp_pid, inp_root_nid);
      i     = node_internal[idx].next_free_input_pos();
#endif
      I(node_internal[idx].has_space_long());

      auto *ledge = (LEdge_Internal *)(&node_internal[idx].sedge[i]);
      ledge->set(src_idx, dst_pid, true);

      node_internal.ref(idx)->inc_inputs(true);  // WARNING: after next_free_input_pos (increasing insert)
    }

    if (node_internal[idx].has_space_short())
      idx_insert_cache[dst_idx] = idx;
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

  I(node_internal[root_idx].is_root());
  return root_idx;
}

void LGraph_Base::error(std::string_view text) {
  fmt::print("error:{}\n", text);
  throw std::runtime_error(std::string(text));
}

void LGraph_Base::warn(std::string_view text) {
  fmt::print("warning:{}\n", text);
}

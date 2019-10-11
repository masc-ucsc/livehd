//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cassert>

#include "iassert.hpp"
#include "lgedge.hpp"
#include "node_pin.hpp"

#include "lgraph.hpp"

static_assert(sizeof(LEdge) == 8, "LEdge should be 8 bytes");
static_assert(sizeof(LEdge) == sizeof(LEdge_Internal), "LEdge should be 8 bytes");
static_assert(sizeof(SEdge) == 2, "SEdge should be 2 bytes");
static_assert(sizeof(SEdge) == sizeof(SEdge_Internal), "SEdge should be 2 bytes");
static_assert(sizeof(Edge_raw) == 2, "Edge_raw should be 2 bytes like SEdge");
static_assert(sizeof(Node_Internal) == 64, "Node should be 64 bytes and 64 bytes aligned");
static_assert(sizeof(Node_Internal_Page) == 64, "Node should 64 32 bytes and 64 bytes aligned");
static_assert((1ULL << Index_bits) <= MMAPA_MAX_ENTRIES, "Max number of entries in Dense");

Index_ID SEdge_Internal::get_page_idx() const { return Node_Internal_Page::get(this).get_idx(); }

Index_ID Edge_raw::get_page_idx() const { return Node_Internal_Page::get(this).get_idx(); }

bool Edge_raw::is_last_input() const {
  const auto &node = Node_Internal::get(this);

  int sz = 1;
  if (!snode) sz = 4;

  return ((this + sz) >= node.get_input_end());
}

bool Edge_raw::is_last_output() const {
  const auto &node = Node_Internal::get(this);
  int         sz   = 1;
  if (!snode) sz = 4;

  return ((this + sz) >= node.get_output_end());
}

const Edge_raw *Edge_raw::find_edge(const Edge_raw *bt, const Edge_raw *et, Index_ID ptr_idx, Port_ID inp_pid, Port_ID dst_pid) {
  const Edge_raw *eit = bt;
  while (eit != et) {
    if (eit->get_idx() == ptr_idx && eit->get_inp_pid() == inp_pid && eit->get_dst_pid() == dst_pid) return eit;

    if (eit->is_snode())
      eit++;
    else
      eit += 4;
  }

  return nullptr;
}

const Edge_raw *Edge_raw::get_reverse_for_deletion() const {
  I(is_root());
  Node_Internal *ptr_node = &Node_Internal::get(this);
  SIndex_ID       ptr_idx  = ptr_node->get_self_idx();

  SIndex_ID       dst_idx = get_idx();
  Node_Internal *ptr_inp2 = &ptr_node[dst_idx - ptr_idx];
  const Node_Internal *ptr_inp = &(ptr_inp2->get_master_root());

  I(ptr_inp->is_master_root());

  //Index_ID dst_pid = get_out_pin().get_pid();
  //Index_ID inp_pid = get_inp_pin().get_pid();
  Index_ID dst_pid;
  Index_ID inp_pid;
  if (is_input()) {
    dst_pid = get_inp_pid();
    inp_pid = get_dst_pid();
  }else{
    dst_pid = get_dst_pid();
    inp_pid = get_inp_pid();
  }

  do {
    const Edge_raw *eit=nullptr;
    if (input)
      eit = find_edge(ptr_inp->get_output_begin(), ptr_inp->get_output_end(), ptr_idx, inp_pid, dst_pid);
    else
      eit = find_edge(ptr_inp->get_input_begin(), ptr_inp->get_input_end(), ptr_idx, dst_pid, inp_pid);

    if (eit) return eit;
    I(!ptr_inp->is_last_state());  // Not found all over

    ptr_inp = &ptr_node[ptr_inp->get_next() - ptr_idx];
  } while (true);

  I(false);
  return ptr_inp->sedge;  // Any random thing
}

Index_ID Edge_raw::get_self_idx() const {
  const auto &root_page = Node_Internal_Page::get(this);
  const auto &root_self = Node_Internal::get(this);

  SIndex_ID delta = &root_self - (const Node_Internal *)&root_page; // Signed and bigger than Index_ID
  I(delta<64 && delta>0); // 64 entries between page aligned

  SIndex_ID idx = delta + root_page.get_idx();

  return static_cast<Index_ID>(idx);
}

Index_ID Edge_raw::get_self_root_idx() const {
  const auto &root_page = Node_Internal_Page::get(this);
  const auto &root_self = Node_Internal::get(this);

  SIndex_ID delta = &root_self - (const Node_Internal *)&root_page;
  I(delta<64 && delta>0); // 64 entries between page aligned

  SIndex_ID self_idx = delta + root_page.get_idx();
  if (root_self.is_root())
    return static_cast<Index_ID>(self_idx);

  return root_self.get_nid();
}

Index_ID Node_Internal::get_self_idx() const {
  const auto &root_page = Node_Internal_Page::get(this);

  SIndex_ID delta = this - (const Node_Internal *)&root_page;

  return delta + root_page.get_idx();
}

int32_t Node_Internal::get_node_num_inputs() const {
  I(is_master_root());

  int32_t total = get_num_local_inputs();
  if (is_last_state()) return total;

  const Node_Internal *node = this;
  do {
    node = &get(node->get_next());
    total += node->get_num_local_inputs();
  } while (!node->is_last_state());

  return total;
}

int32_t Node_Internal::get_node_num_outputs() const {
  I(is_master_root());

  int32_t total = get_num_local_outputs();
  if (is_last_state()) return total;

  const Node_Internal *node = this;
  do {
    node = &get(node->get_next());
    total += node->get_num_local_outputs();
  } while (!node->is_last_state());

  return total;
}

bool Node_Internal::has_node_inputs() const {
  I(is_master_root());

  int32_t total = get_num_local_inputs();
  if (total) return true;

  if (is_last_state()) return false;

  const Node_Internal *node = this;
  do {
    node  = &get(node->get_next());
    total = node->get_num_local_inputs();
    if (total) return true;
  } while (!node->is_last_state());

  return false;
}

bool Node_Internal::has_pin_inputs() const {
  I(is_root());

  int32_t total = get_num_local_inputs();
  if (total) return true;

  if (is_last_state()) return false;

  Port_ID              pid  = get_dst_pid();
  const Node_Internal *node = this;
  do {
    node  = &get(node->get_next());
    total = node->get_num_local_inputs();
    if (total && node->get_dst_pid() == pid) return true;
  } while (!node->is_last_state());

  return false;
}

bool Node_Internal::has_node_outputs() const {
  I(is_master_root());

  int32_t total = get_num_local_outputs();
  if (total) return true;

  if (is_last_state()) return false;

  const Node_Internal *node = this;
  do {
    node  = &get(node->get_next());
    total = node->get_num_local_outputs();
    if (total) return true;
  } while (!node->is_last_state());

  return false;
}

bool Node_Internal::has_pin_outputs() const {
  I(is_root());

  int32_t total = get_num_local_outputs();
  if (total) return true;

  if (is_last_state()) return false;

  Port_ID              pid  = get_dst_pid();
  const Node_Internal *node = this;
  do {
    node  = &get(node->get_next());
    total = node->get_num_local_outputs();
    if (total && node->get_dst_pid() == pid) return true;
  } while (!node->is_last_state());

  return false;
}

const Node_Internal &Node_Internal::get_root() const {
  I(nid);
  if (is_root()) return *this;

  const auto &         root_page = Node_Internal_Page::get(this);
  const Node_Internal *root_ptr  = (const Node_Internal *)&root_page;

  SIndex_ID delta = static_cast<SIndex_ID>(nid) - root_page.get_idx(); // Signed and bigger than Index_ID
  root_ptr       = (root_ptr + delta);
  I(root_ptr->is_root());
  I(root_ptr->is_node_state());

  return *root_ptr;
}

const Node_Internal &Node_Internal::get_master_root() const {
  I(nid);
  if (is_master_root()) return *this;

  const auto &         root_page = Node_Internal_Page::get(this);
  const Node_Internal *root_ptr;

  SIndex_ID delta = static_cast<SIndex_ID>(nid) - root_page.get_idx(); // Signed and bigger than Index_ID
  root_ptr       = ((const Node_Internal *)&root_page) + delta;
  I(root_ptr->is_root());
  I(root_ptr->is_node_state());
  if (root_ptr->is_master_root()) return *root_ptr;

  delta    = static_cast<SIndex_ID>(root_ptr->get_nid()) - root_page.get_idx();
  root_ptr = ((const Node_Internal *)&root_page) + delta;

  I(root_ptr->is_root());
  I(root_ptr->is_master_root());
  I(root_ptr->is_node_state());

  return *root_ptr;
}

void Node_Internal::try_recycle() {
  if (out_pos != 0 || inp_pos != 0) return;
  I(!is_free_state());

  // Recycle nid

  if (is_root()) return;  // Keep node

  Node_Internal *root_ptr     = (Node_Internal *)&get_root();
  Index_ID       root_idx = root_ptr->get_nid();
  I(root_idx == root_ptr->get_self_idx());

  SIndex_ID self_idx = get_self_idx();
  SIndex_ID prev_idx = root_idx;
  I(prev_idx != self_idx);  // because it is not a root_ptr

  while (root_ptr[prev_idx - root_idx].get_next() != self_idx) {
    I(root_ptr[prev_idx - root_idx].is_next_state());
    prev_idx = root_ptr[prev_idx - root_idx].get_next();
  }

  if (is_next_state()) {
    root_ptr[prev_idx - root_idx].set_next_state(get_next());
  } else {
    root_ptr[prev_idx - root_idx].set_last_state();
  }

  set_free_state();
  I(root_ptr[-root_idx].is_page_state());

  Node_Internal_Page master_page = Node_Internal_Page::get(root_ptr[-root_idx].sedge);

  nid                  = master_page.free_idx;
  master_page.free_idx = self_idx;
}

void Node_Internal::del_input_int(const Edge_raw *inp_edge) {
  I(((uint64_t)inp_edge) >> 6 == ((uint64_t)this) >> 6);

  int pos = (SEdge *)inp_edge - sedge;

  I(pos >= 0 && pos <= Num_SEdges);
  I((inp_pos + get_input_begin_pos_int()) > pos);

  int sz = 1;
  if (!inp_edge->is_snode()) {
    sz = 4;
    I(inp_long);
    inp_long--;
  }

  if (pos != (inp_pos - sz + get_input_begin_pos_int())) {
    for (int i = pos; i < (inp_pos + get_input_begin_pos_int()); i++) {
      sedge[i] = sedge[i + sz];
    }
  }

  I(inp_pos >= sz);

  inp_pos -= sz;
  for (int i = 0; i < inp_pos;) {
    if (sedge[get_input_begin_pos_int() + i].is_snode()) {
      i++;
    } else {
      i += 4;
    }
  }

  I(inp_pos >= (4 * inp_long));
}

void Node_Internal::del_output_int(const Edge_raw *out_edge) {
  I(((uint64_t)out_edge) >> 6 == ((uint64_t)this) >> 6);

  int pos = (SEdge *)out_edge - sedge;

  I(pos >= 0 && pos <= Num_SEdges);
  I((Num_SEdges - out_pos) <= pos);

  int sz = 1;
  if (!out_edge->is_snode()) {
    sz = 4;
    I(out_long > 0);
    out_long--;
  }

  if (pos != (Num_SEdges - out_pos)) {
    for (int i = pos - 1; i >= Num_SEdges - out_pos; i--) {
      // fmt::print("copy from {} to {}\n",i,i+sz);
      sedge[i + sz] = sedge[i];
    }
  }

  out_pos -= sz;
  I(out_pos >= 0);
}

bool Node_Internal::del(Index_ID src_idx, Port_ID pid, bool input) {
  const Edge_raw *eit=nullptr;
  if (input)
    eit = Edge_raw::find_edge(get_output_begin(), get_output_end(), src_idx, pid, get_dst_pid());
  else
    eit = Edge_raw::find_edge(get_input_begin(), get_input_end(), src_idx, get_dst_pid(), pid);

  if (eit==nullptr)
    return false;

  del(eit);

  return true;
}

void Node_Internal::del(const Edge_raw *edge_raw) {
  if (edge_raw->is_input())
    del_input(edge_raw);
  else
    del_output(edge_raw);
}

// LCOV_EXCL_START
void Node_Internal::dump() const {
  fmt::print("nid:{} pid:{} state:{} inp_pos:{} out_pos:{} root:{}\n", nid, dst_pid, state, inp_pos, out_pos, root);

  const Edge_raw *out = get_output_begin();
  while (out != get_output_end()) {
    fmt::print("  out idx:{} pid:{}\n", out->get_idx(), out->get_inp_pid());
    if (out->is_snode())
      out++;
    else
      out += 4;
  }

  out = get_input_begin();
  while (out != get_input_end()) {
    fmt::print("  inp idx:{} pid:{}\n", out->get_idx(), out->get_inp_pid());
    if (out->is_snode())
      out++;
    else
      out += 4;
  }
}
// LCOV_EXCL_STOP

// LCOV_EXCL_START
void Node_Internal::dump_full() const {
  Node_Internal *root_ptr = (Node_Internal *)&get_root();

  Index_ID             root_idx = root_ptr->get_nid();
  const Node_Internal *node     = this;

  node->dump();
  while (true) {
    Index_ID idx = node->get_next();
    node         = &root_ptr[idx - root_idx];

    node->dump();

    if (node->is_last_state()) return;
  }
}
// LCOV_EXCL_STOP

void Node_Internal::assimilate_edges(Node_Internal *other_ptr) {
  Node_Internal &other = *other_ptr; // to avoid -> all the time

  I(inp_pos == 0);
  I(out_pos == 0);
  I(dst_pid == other.dst_pid);
  I(is_last_state());
  I(!is_root());  // Could not be root if it is assimulating

  // TODO: Currently goes in-order assimilating. It would be better to do:
  //
  // 1st: Transfer the ledges (maybe become sedges or same at worst)
  // 2nd: Transfer sedges (if and only iff no ledges were transfered).
  // 3rd: First, transfer sedges that are sedges at dest too
  int other_pos = 0;
  int other_end = other.inp_pos;
  if (!other.is_last_state()) {
    other_pos = 2;
    other_end += 2;
  }
  int original_start_pos = other_pos;

  {
    int self_pos = 0;

    int other_inp_long_removed = 0;

    for (int i = 0; i < other.inp_pos;) {
      if (other.sedge[other_pos].is_snode()) {
        SEdge_Internal *sedge_i = (SEdge_Internal *)&sedge[self_pos];

        bool done = sedge_i->set(other.sedge[other_pos].get_idx(), other.sedge[other_pos].get_inp_pid(), true);
        if (done) {
          self_pos++;
          inc_inputs(false);
        } else {
          if (self_pos >= (Num_SEdges - 4 - 2) || inp_long>=3) break;

          LEdge_Internal *ledge_i = (LEdge_Internal *)&sedge[self_pos];
          ledge_i->set(other.sedge[other_pos].get_idx(), other.sedge[other_pos].get_inp_pid(), true  // input
              );

          self_pos += 4;
          inc_inputs(true);
        }
        other_pos++;
        i += 1;
      } else {
        if (self_pos >= (Num_SEdges - 4 - 2) || inp_long>=3) break;
        sedge[self_pos++] = other.sedge[other_pos++];
        sedge[self_pos++] = other.sedge[other_pos++];
        sedge[self_pos++] = other.sedge[other_pos++];
        sedge[self_pos++] = other.sedge[other_pos++];
        i += 4;
        inc_inputs(true);
        other_inp_long_removed++;
      }

      if (self_pos >= (Num_SEdges - 1 - 2))  // at least an snode
        break;
      I(has_space_short());
    }

    int start_pos = original_start_pos;
    for (int j = other_pos; j < other_end; j++) {
      other.sedge[start_pos++] = other.sedge[j];
    }

    I(other_end >= other_pos);
    other.inp_pos -= (other_pos - original_start_pos);
    I(other_inp_long_removed <= other.inp_long);
    other.inp_long -= other_inp_long_removed;
    I(other.inp_pos >= (4 * other.inp_long));
  }

  if (!has_space_short())
    return;

  // try transfer outputs if there is space in current

  while (other.out_pos) {
    const Edge_raw *other_out = other.get_output_begin();

    int self_pos = next_free_output_pos();
    I(self_pos < Num_SEdges);
    SEdge_Internal *sedge_i = (SEdge_Internal *)&sedge[self_pos];   // became an sedge
    bool done = sedge_i->set(other_out->get_idx(), other_out->get_inp_pid(), false);

    if (done) {
      inc_outputs(false);
    } else {
      if (!has_space_long_out()) break; // We need long space
      self_pos -= (4-1);
      LEdge_Internal *ledge_i = (LEdge_Internal *)&sedge[self_pos];   // became an ledge
      ledge_i->set(other_out->get_idx(), other_out->get_inp_pid(), false); // output
      inc_outputs(true);
    }
    if (other_out->is_snode()) {
      I(other.out_pos > 0);
      other.out_pos -= 1;
    } else {
      I(other.out_pos >= 4);
      other.out_pos -= 4;
      I(other.out_long > 0);
      other.out_long--;
    }

    if (!has_space_short()) break;
  }
}

Port_ID Edge_raw::get_dst_pid() const { return Node_Internal::get(this).get_dst_pid(); }

Node_pin Edge_raw::get_out_pin(LGraph *g, LGraph *cg, const Hierarchy_index &hidx) const {
  if (is_input())
    return Node_pin(g, cg, hidx, get_idx(), get_inp_pid(), false);
  else
    return Node_pin(g, cg, hidx, get_self_root_idx(), get_dst_pid(), false);
}

Node_pin Edge_raw::get_inp_pin(LGraph *g, LGraph *cg, const Hierarchy_index &hidx) const {
  if (is_input())
    return Node_pin(g, cg, hidx, get_self_root_idx(), get_dst_pid(), true);
  else
    return Node_pin(g, cg, hidx, get_idx(), get_inp_pid(), true);
}

Index_ID Edge_raw::get_self_nid() const { return Node_Internal::get(this).get_nid(); }

uint32_t Edge_raw::get_bits() const {
  const auto &node = Node_Internal::get(this);

  if (node.is_root()) return node.get_bits();

  return node.get_root().get_bits();
}

bool Edge_raw::is_root() const { return Node_Internal::get(this).is_root(); }

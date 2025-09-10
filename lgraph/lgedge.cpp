//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedge.hpp"

#include <cassert>
#include <format>
#include <iostream>

#include "iassert.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"

static_assert(sizeof(LEdge) == 6, "LEdge should be 6 bytes");
static_assert(sizeof(LEdge) == sizeof(LEdge_Internal), "LEdge should be 6 bytes");
static_assert(sizeof(SEdge) == 3, "SEdge should be 3 bytes");
static_assert(sizeof(SEdge) == sizeof(SEdge_Internal), "SEdge should be 2 bytes");
static_assert(sizeof(Edge_raw) == 3, "Edge_raw should be 3 bytes like SEdge");
static_assert(sizeof(Node_internal) == 32, "Node should be 32 bytes and 32 bytes aligned");
static_assert(sizeof(Node_internal_Page) == 32, "Node should 32 bytes and 32 bytes aligned");

Index_id SEdge_Internal::get_page_idx() const { return Node_internal_Page::get(this).get_idx(); }

Index_id Edge_raw::get_page_idx() const { return Node_internal_Page::get(this).get_idx(); }

const Edge_raw *Edge_raw::find_edge(const Edge_raw *bt, const Edge_raw *et, Index_id ptr_idx, Port_ID inp_pid, Port_ID dst_pid) {
  const Edge_raw *eit = bt;
  while (eit != et) {
    if (eit->get_idx() == ptr_idx && eit->get_dst_pid() == dst_pid) {
      I(eit->get_inp_pid() == inp_pid);
      return eit;
    }

    if (eit->is_snode()) {
      eit++;
    } else {
      eit += 2;
    }
  }

  return nullptr;
}

Index_id Edge_raw::get_self_idx() const {
  const auto &root_page = Node_internal_Page::get(this);
  const auto &root_self = Node_internal::get(this);

  SIndex_id delta = &root_self - (const Node_internal *)&root_page;  // Signed and bigger than Index_id
  I(delta < 4096 / sizeof(Node_internal) && delta > 0);

  SIndex_id idx = delta + root_page.get_idx();

  return static_cast<Index_id>(idx);
}

Index_id Node_internal::get_self_idx() const {
  const auto &root_page = Node_internal_Page::get(this);

  SIndex_id delta = this - (const Node_internal *)&root_page;

  return delta + root_page.get_idx();
}

const Node_internal &Node_internal::get_root() const {
  I(nid);
  if (is_root()) {
    return *this;
  }

  const auto          &root_page = Node_internal_Page::get(this);
  const Node_internal *root_ptr  = (const Node_internal *)&root_page;

  SIndex_id delta = static_cast<SIndex_id>(nid) - root_page.get_idx();  // Signed and bigger than Index_id
  root_ptr        = (root_ptr + delta);
  I(root_ptr->is_root());
  I(root_ptr->is_node_state());

  return *root_ptr;
}

const Node_internal &Node_internal::get_master_root() const {
  I(nid);
  if (is_master_root()) {
    return *this;
  }

  const auto          &root_page = Node_internal_Page::get(this);
  const Node_internal *root_ptr;

  SIndex_id delta = static_cast<SIndex_id>(nid) - root_page.get_idx();  // Signed and bigger than Index_id
  root_ptr        = ((const Node_internal *)&root_page) + delta;
  I(root_ptr->is_root());
  I(root_ptr->is_node_state());
  if (root_ptr->is_master_root()) {
    return *root_ptr;
  }

  delta    = static_cast<SIndex_id>(root_ptr->get_nid()) - root_page.get_idx();
  root_ptr = ((const Node_internal *)&root_page) + delta;

  I(root_ptr->is_root());
  I(root_ptr->is_master_root());
  I(root_ptr->is_node_state());

  return *root_ptr;
}

void Node_internal::try_recycle() {
  inp_pos  = 0;
  out_pos  = 0;
  inp_long = 0;
  out_long = 0;

  I(!is_free_state());

  // TODO: recycle the node no matter what

  SIndex_id self_idx = get_self_idx();

  set_free_state();

  Node_internal_Page master_page = Node_internal_Page::get(this);

  nid                  = master_page.free_idx;
  master_page.free_idx = self_idx;
}

void Node_internal::del_input_int(const Edge_raw *inp_edge) {
  I(((uint64_t)inp_edge) >> 5 == ((uint64_t)this) >> 5);  // 32 byte alignment

  int pos = (SEdge *)inp_edge - sedge;

  I(pos >= 0 && pos <= Num_SEdges);
  I((inp_pos + get_input_begin_pos_int()) > pos);

  int sz = 1;
  if (!inp_edge->is_snode()) {
    sz = 2;
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
      i += 2;
    }
  }

  I(inp_pos >= (2 * inp_long));
}

void Node_internal::del_output_int(const Edge_raw *out_edge) {
  I(((uint64_t)out_edge) >> 5 == ((uint64_t)this) >> 5);  // 32 byte alignment

  int pos = (SEdge *)out_edge - sedge;

  I(pos >= 0 && pos <= Num_SEdges);
  I((Num_SEdges - out_pos) <= pos);

  int sz = 1;
  if (!out_edge->is_snode()) {
    sz = 2;
    I(out_long > 0);
    out_long--;
  }

  if (pos != (Num_SEdges - out_pos)) {
    for (int i = pos - 1; i >= Num_SEdges - out_pos; i--) {
      // std::print("copy from {} to {}\n",i,i+sz);
      sedge[i + sz] = sedge[i];
    }
  }

  out_pos -= sz;
  I(out_pos >= 0);
}

#if 0
bool Node_internal::xxx(Index_id src_idx, Port_ID pid, bool input) {
  const Edge_raw *eit = nullptr;
  if (input)
    eit = Edge_raw::find_edge(get_output_begin(), get_output_end(), src_idx, pid, get_dst_pid());
  else
    eit = Edge_raw::find_edge(get_input_begin(), get_input_end(), src_idx, get_dst_pid(), pid);

  if (eit == nullptr) return false;

  del(eit);

  return true;
}

void Node_internal::xxx(const Edge_raw *edge_raw) {
  if (edge_raw->is_input())
    del_input(edge_raw);
  else
    del_output(edge_raw);
}
#endif

// LCOV_EXCL_START
void Node_internal::dump() const {
  std::print("nid:{} pid:{} state:{} inp_pos:{} out_pos:{} root:{}\n",
             (int)nid,
             (int)dst_pid,
             (int)state,
             (int)inp_pos,
             out_pos,
             root);

  const Edge_raw *out = get_output_begin();
  while (out != get_output_end()) {
    std::print("  out idx:{} pid:{}\n", (int)out->get_idx(), (int)out->get_inp_pid());
    if (out->is_snode()) {
      out++;
    } else {
      out += 2;
    }
  }

  out = get_input_begin();
  while (out != get_input_end()) {
    std::print("  inp idx:{} pid:{}\n", (int)out->get_idx(), (int)out->get_inp_pid());
    if (out->is_snode()) {
      out++;
    } else {
      out += 2;
    }
  }
}
// LCOV_EXCL_STOP

// LCOV_EXCL_START
void Node_internal::dump_full() const {
  Node_internal *root_ptr = (Node_internal *)&get_root();

  Index_id             root_idx = root_ptr->get_nid();
  const Node_internal *node     = this;

  node->dump();
  while (true) {
    Index_id idx = node->get_next();
    node         = &root_ptr[idx - root_idx];

    node->dump();

    if (node->is_last_state()) {
      return;
    }
  }
}
// LCOV_EXCL_STOP

void Node_internal::assimilate_edges(Node_internal *other_ptr) {
  Node_internal &other = *other_ptr;  // to avoid -> all the time

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
          if (self_pos >= (Num_SEdges - 2 - 2) || inp_long >= 8) {
            break;
          }

          LEdge_Internal *ledge_i = (LEdge_Internal *)&sedge[self_pos];
          ledge_i->set(other.sedge[other_pos].get_idx(), other.sedge[other_pos].get_inp_pid(), true  // input
          );

          self_pos += 2;
          inc_inputs(true);
        }
        other_pos++;
        i += 1;
      } else {
        if (self_pos >= (Num_SEdges - 2 - 2) || inp_long >= 8) {
          break;
        }
        sedge[self_pos++] = other.sedge[other_pos++];
        sedge[self_pos++] = other.sedge[other_pos++];
        i += 2;
        inc_inputs(true);
        other_inp_long_removed++;
      }

      if (self_pos >= (Num_SEdges - 1 - 2)) {  // at least an snode
        break;
      }
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
    I(other.inp_pos >= (2 * other.inp_long));
  }

  if (!has_space_short()) {
    return;
  }

  // try transfer outputs if there is space in current

  while (other.out_pos) {
    const Edge_raw *other_out = other.get_output_begin();

    int self_pos = next_free_output_pos();
    I(self_pos < Num_SEdges);
    SEdge_Internal *sedge_i = (SEdge_Internal *)&sedge[self_pos];  // became an sedge
    bool            done    = sedge_i->set(other_out->get_idx(), other_out->get_inp_pid(), false);

    if (done) {
      inc_outputs(false);
    } else {
      if (!has_space_long()) {
        break;  // We need long space
      }
      self_pos -= (2 - 1);
      LEdge_Internal *ledge_i = (LEdge_Internal *)&sedge[self_pos];         // became an ledge
      ledge_i->set(other_out->get_idx(), other_out->get_inp_pid(), false);  // output
      inc_outputs(true);
    }
    if (other_out->is_snode()) {
      I(other.out_pos > 0);
      other.out_pos -= 1;
    } else {
      I(other.out_pos >= 2);
      other.out_pos -= 2;
      I(other.out_long > 0);
      other.out_long--;
    }

    if (!has_space_short()) {
      break;
    }
  }
}

Port_ID Edge_raw::get_dst_pid() const { return Node_internal::get(this).get_dst_pid(); }

Node_pin Edge_raw::get_out_pin(Lgraph *g, Lgraph *cg, const Hierarchy_index &hidx, Index_id self_idx) const {
  I(get_self_idx() == self_idx);
  if (is_input()) {
    return Node_pin(g, cg, hidx, get_idx(), get_inp_pid(), false);
  } else {
    return Node_pin(g, cg, hidx, self_idx, get_dst_pid(), false);
  }
}

Node_pin Edge_raw::get_inp_pin(Lgraph *g, Lgraph *cg, const Hierarchy_index &hidx, Index_id self_idx) const {
  I(get_self_idx() == self_idx);
  if (is_input()) {
    return Node_pin(g, cg, hidx, self_idx, get_dst_pid(), true);
  } else {
    return Node_pin(g, cg, hidx, get_idx(), get_inp_pid(), true);
  }
}

Index_id Edge_raw::get_self_nid() const { return Node_internal::get(this).get_nid(); }

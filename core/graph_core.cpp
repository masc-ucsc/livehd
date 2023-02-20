//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_core.hpp"

#include <algorithm>
#include <iterator>
#include <string>

#include "fmt/format.h"
#include "iassert.hpp"
#include "likely.hpp"

bool Graph_core::Master_entry::add_sedge(int16_t rel_id) {
HERE: FIrst insert in sedge, then the other places

#if 1
  if (n_edges < Num_sedges) {
    I(sedge[n_edges]==0);
    sedge[n_edges] = rel_id;
    inp_mask |= ((out?0:1) << n_edges);

    ++n_edges;
    return true;
  }
#else
  for (auto i = 0u; i < Num_sedges; ++i) {
    if (sedge[i]) {
      continue;
    }

    sedge[i] = rel_id;
    ++n_edges;
    inp_mask |= ((out?0:1) << i);
    return true;
  }
#endif

  if (is_node() && sedge2_or_pid == 0) {
    sedge2_or_pid = rel_id;
    I(n_edges == Num_sedges);
    n_edges = Num_sedges+1;
    inp_mask |= ((out?0:1) << Num_sedges);
    return true;
  }

  return false;
}

bool Graph_core::Master_entry::add_ledge(uint32_t id, bool out) {
  if (is_node() && ledge0_or_prev == 0) {
    ledge0_or_prev = id;
    ++n_edges;
    inp_mask |= ((out?0:1) << (Num_sedges + 1));
    return true;
  }

  if (!overflow_link && ledge1_or_overflow == 0) {
    ledge1_or_overflow = id;
    ++n_edges;
    inp_mask |= ((out?0:1) << (Num_sedges + 2));
    return true;
  }

  return false;
}

/* function that deletes values from the edge storage of an Entry16
 *
 * @params uint8_t rel_index
 * @returns 0 if success and 1 if empty
 */
bool Graph_core::Master_entry::delete_edge(uint32_t self_id, uint32_t other_id, bool out) {
  if (!out && !inp_mask) {
    return false;  // no input in master
  }

  int32_t rel_id    = other_id - self_id;
  bool    short_rel = INT16_MIN < rel_id && rel_id < INT16_MAX;

  if (short_rel) {
    for (auto i = 0u; i < Num_sedges; ++i) {
      if (sedge[i] != rel_id) {
        continue;
      }

      if ((inp_mask & (1 << i)) == out) {
        continue;
      }

      sedge[i] = 0;
      --n_edges;
      if (!out) {
        inp_mask ^= (1 << i);
      }

      return true;
    }
    if (node_vertex && sedge2_or_pid == rel_id) {
      if ((inp_mask & (1 << Num_sedges)) != out) {
        sedge2_or_pid = 0;
        --n_edges;
        if (!out) {
          inp_mask ^= (1 << Num_sedges);
        }

        return true;
      }
    }
  }

  if (node_vertex && ledge0_or_prev == other_id && (inp_mask & (1 << (Num_sedges + 1))) != out) {
    ledge0_or_prev = 0;
    --n_edges;
    if (!out) {
      inp_mask ^= (1 << (Num_sedges + 1));
    }

    return true;
  }

  if (!overflow_link && ledge1_or_overflow == other_id && (inp_mask & (1 << (Num_sedges + 2))) != out) {
    ledge1_or_overflow = 0;
    --n_edges;
    if (!out) {
      inp_mask ^= (1 << (Num_sedges + 2));
    }

    return true;
  }

  return false;
}


uint32_t Graph_core::allocate_overflow() {
  uint32_t oid;
  if (free_overflow_id) {
    oid              = free_overflow_id;
    free_overflow_id = table[oid].remove_free_next();

  } else {
    oid = table.size();
    table.emplace_back();  // 2 spaces for one overflow
    table.emplace_back();
  }

  Overflow_entry *ent  = (Overflow_entry *)&table[oid];
  ent->overflow_vertex = 1;

  return oid;
}

void Graph_core::add_edge_int(uint32_t self_id, uint32_t other_id, bool out) {
  Master_entry &ent       = table[self_id];
  int32_t       rel_index = other_id - self_id;
  bool          short_rel = INT16_MIN < rel_index && rel_index < INT16_MAX;

  if (short_rel) {
    bool ok = ent.add_sedge(static_cast<int16_t>(rel_index));
    if (ok) {
      return;
    }
  }

  bool ok = ent.add_ledge(other_id, out);
  if (ok) {
    return;
  }

  absl::InlinedVector<uint32_t, 40> pending_inp;
  absl::InlinedVector<uint32_t, 40> pending_out;

  ent.readjust_edges(self_id, pending_inp, pending_out);

  if (out) {
    pending_out.emplace_back(other_id);
  } else {
    pending_inp.emplace_back(other_id);
  }

  std::sort(pending_inp.begin(), pending_inp.end(), std::greater<uint32_t>());  // sort reverse order
  std::sort(pending_out.begin(), pending_out.end(), std::greater<uint32_t>());  // sort reverse order

  ent.inp_mask  = 0;
  ent.n_edges   = 0;

  auto overflow_id = ent.get_overflow_id();
  if (overflow_id == 0) {
    I(ent.ledge1_or_overflow == 0);
    overflow_id = allocate_overflow();
    // Can not use ent, allocate_overflow can change pointers
    table[self_id].set_overflow(overflow_id);
  }
  I(overflow_id);

  std::vector<uint32_t> add_to_end;
  uint32_t              prev_over_id = 0;

  while (true) {
    auto *over_ptr = ref_overflow(overflow_id);
    I(over_ptr);

    if (over_ptr->is_full()) {  // There was no space. The rest are full
      auto new_over_id = allocate_overflow();
      if (prev_over_id) {
        // Before: A(nonfull) -> prev(nonfull)       ->         over (full) -> B(full)
        // After : A(nonfull) -> prev(nonfull) -> new(empty) -> over (full) -> B(full)
        ref_overflow(prev_over_id)->overflow_next_id = new_over_id;
      } else {
        // Before: master     ->    over(full) -> B(full)
        // After : master -> new -> over(full) -> B(full)
        I(table[self_id].ledge1_or_overflow == overflow_id);
        table[self_id].ledge1_or_overflow = new_over_id;
      }
      over_ptr                   = ref_overflow(new_over_id);
      over_ptr->overflow_next_id = overflow_id;
      overflow_id                = new_over_id;
    }

    I(over_ptr->n_edges < Overflow_entry::max_edges);
    over_ptr->readjust_edges(pending_inp, pending_out);

    if (over_ptr->is_full()) {  // Move to the end. Where fulls start
      auto next_id            = over_ptr->get_overflow_id();
      bool should_move_to_end = true;
      if (next_id == 0 || ref_overflow(next_id)->is_full()) {
        // No need to move, if it is the last or next is full
        should_move_to_end = false;
      }

      if (should_move_to_end) {
        if (prev_over_id) {
          // Before: A(nonfull) -> prev(nonfull) -> over (full) -> next(nonfull)
          // After : A(nonfull) -> prev(nonfull)                -> next(nonfull)
          ref_overflow(prev_over_id)->overflow_next_id = next_id;
        } else {
          // Before: master(??) -> over (full) -> next(nonfull)
          // After : master(??)                -> next(nonfull)
          table[self_id].ledge1_or_overflow = next_id;
        }
        // Add to pending and chain as needed
        if (!add_to_end.empty()) {
          ref_overflow(add_to_end.back())->overflow_next_id = overflow_id;
        }
        add_to_end.emplace_back(overflow_id);
        overflow_id = next_id;
        if (!pending_inp.empty() || !pending_out.empty()) {
          // prev_over_id did not change
          over_ptr = ref_overflow(next_id);
          continue;
        }
      }
    }

    if (pending_inp.empty() && pending_out.empty()) {
      if (add_to_end.empty()) {
        return;  // Nothing to do
      }

      // Find the last or the first empty (start with overflow_id)
      // enqueue the add_to_end
      uint32_t next_id = overflow_id;
      uint32_t last_id = 0;
      while (true) {
        last_id = next_id;
        next_id = over_ptr->get_overflow_id();
        if (next_id == 0) {
          break;
        }
        over_ptr = ref_overflow(next_id);
        if (over_ptr->is_full()) {
          break;
        }
      }

      ref_overflow(last_id)->overflow_next_id           = add_to_end.front();
      ref_overflow(add_to_end.back())->overflow_next_id = next_id;

      return;
    }

    prev_over_id = overflow_id;
    overflow_id  = over_ptr->get_overflow_id();
    if (overflow_id == 0) {
      overflow_id = allocate_overflow();
      // readjust_edges can change the table (move it), so must recompute
      ref_overflow(prev_over_id)->overflow_next_id = overflow_id;
    }
  }
}

void Graph_core::del_pin(uint32_t self_id) {
  table[self_id].delete_node(self_id, table);

  auto            over_id  = table[self_id].get_overflow_id();

  while (over_id) {
    auto *over_ptr = ref_overflow(over_id);
    auto over_ptr_id = over_id;
    over_id  = over_ptr->get_overflow_id();

    over_ptr->delete_node(self_id, table);

    if (free_overflow_id) {
      table[over_ptr_id].insert_free_next(free_overflow_id);
    }else{
      over_ptr->clear();
    }
    free_overflow_id = over_ptr_id;
  }

  if (table[self_id].set_link)
    delete table[self_id].set;

  table[self_id].clear();
}

void Graph_core::del_node(uint32_t self_id) {
  if (table[self_id].is_pin()) {
    self_id = table[self_id].get_prev_ptr(); // point to master
  }
  auto next_pin_id = table[self_id].next_pin_ptr;
  del_pin(self_id);
  while (next_pin_id) {
    auto id = table[next_pin_id].next_pin_ptr;
    del_pin(next_pin_id);
    next_pin_id = id;
  }
}

void Graph_core::del_edge_int(uint32_t self_id, uint32_t other_id, bool out) {
  bool done1 = table[self_id].delete_edge(self_id, other_id, out);
  if (done1) {
    return;
  }

  auto            over_id  = table[self_id].get_overflow_id();
  Overflow_entry *prev_ptr = nullptr;
  while (over_id) {
    auto *over_ptr = ref_overflow(over_id);
    bool  done2    = over_ptr->delete_edge(other_id, out);
    if (done2) {
      if (over_ptr->has_local_edges()) {
        if (prev_ptr == nullptr) {
          return;
        }

        if (!prev_ptr->is_full()) {
          return;
        }

        // If prev is full, move to first position (fulls must be at the end)
        prev_ptr->overflow_next_id        = over_ptr->get_overflow_id();
        auto tmp_id                       = table[self_id].get_overflow_id();
        table[self_id].ledge1_or_overflow = over_id;
        over_ptr->overflow_next_id        = tmp_id;
        return;
      }
      if (prev_ptr) {
        prev_ptr->overflow_next_id = over_ptr->overflow_next_id;
      } else {
        I(table[self_id].overflow_link);
        table[self_id].ledge1_or_overflow = over_id;
      }
      return;
    }
    prev_ptr = over_ptr;
    over_id  = over_ptr->get_overflow_id();
  }

  I(false);  // WARNING: trying to delete a node that does not exist!!
}

Graph_core::Graph_core(std::string_view path, std::string_view name) {
  (void)path;
  (void)name;

  table.emplace_back();             // Reserve entry 0 as is_invalid
  table[0].overflow_vertex = true;  // mark invalid type

  free_master_id   = 0;
  free_overflow_id = 0;
}

/*  Create a node node
 *
 *  @params uint8_t type
 *  @returns uint32_t of the node
 */

uint32_t Graph_core::create_node() {
  uint32_t id;

  if (free_master_id) {
    id             = free_master_id;
    free_master_id = table[id].remove_free_next();
  } else {
    id = table.size();
    table.emplace_back();
  }
  I(!table[id].has_edges() && table[id].node_vertex == 0);  // initialized to zero

  table[id].node_vertex = 1;

  return id;
}

/*  Create a pin and point to pin root m
 *
 *  @params const Index_ID node_id, const Port_ID pid
 *  @returns Index_ID of the node
 */

uint32_t Graph_core::create_pin(const uint32_t node_id, const Port_ID pid) {
  I(node_id && node_id < table.size());

  auto  id      = create_node();
  auto &pin_ent = table[id];

  pin_ent.node_vertex    = 0;
  pin_ent.ledge0_or_prev = node_id;
  pin_ent.set_pid(pid);

  auto &root_ent = table[node_id];
  I(root_ent.is_node());

  if (root_ent.next_pin_ptr == 0) {
    root_ent.next_pin_ptr = id;  // easy case, first pin
    return id;
  }

  // Insert pin in pid order (easier search)
  auto prev_ptr = node_id;
  auto ptr      = root_ent.next_pin_ptr;
  while (ptr && table[ptr].get_pid() > pid) {
    prev_ptr = ptr;
    ptr      = table[ptr].next_pin_ptr;
  }
  I(prev_ptr);

  auto insert_ptr          = table[prev_ptr].next_pin_ptr;
  table[prev_ptr].next_pin_ptr = id;
  table[id].next_pin_ptr       = insert_ptr;

  return id;
}

std::pair<size_t, size_t> Graph_core::get_num_pin_edges(uint32_t id) const {
  I(!is_invalid(id));

  size_t t_inp = 0;
  size_t t_out = 0;

  {
    auto [l_inp, l_out] = table[id].get_num_local_edges();
    t_inp += l_inp;
    t_out += l_out;
  }

  auto over_id = table[id].get_overflow_id();
  while (over_id) {
    auto *over_ptr      = ref_overflow(over_id);
    auto [l_inp, l_out] = over_ptr->get_num_local_edges();
    t_inp += l_inp;
    t_out += l_out;

    over_id = over_ptr->get_overflow_id();
  }

  return std::pair(t_inp, t_out);
}

void Graph_core::Master_entry::dump(uint32_t self_id) const {
  const auto [n_i, n_o] = get_num_local_edges();

  if (node_vertex) {
    fmt::print("node:{} bits:{} n_inputs:{} n_outputs:{} next:{} over:{}\n", self_id, bits, n_i, n_o, next_pin_ptr, get_overflow_id());
  } else {
    fmt::print("pin:{} pid:{} bits:{} n_inputs:{} n_outputs:{} next:{} over:{} node:{}\n",
               self_id,
               get_pid(),
               bits,
               n_i,
               n_o,
               next_pin_ptr,
               get_overflow_id(),
               ledge0_or_prev);
  }

  fmt::print("  edges:");
  for (auto i = 0u; i < Num_sedges; ++i) {
    if (sedge[i]) {
      fmt::print(" {}", self_id + sedge[i]);
    }
  }
  if (node_vertex && sedge2_or_pid) {
    fmt::print(" {}", self_id + sedge2_or_pid);
  }
  if (node_vertex && ledge0_or_prev) {
    fmt::print(" {}", ledge0_or_prev);
  }
  if (!overflow_link && ledge1_or_overflow) {
    fmt::print(" {}", ledge1_or_overflow);
  }
  fmt::print("\n");
}

void Graph_core::Master_entry::delete_node(uint32_t self_id, std::vector<Master_entry> &mtable) {
  for (auto i = 0u; i < Num_sedges; ++i) {
    if (sedge[i] == 0) {
      continue;
    }

    if (!(inp_mask & (1 << i))) {
      auto id = self_id + sedge[i];
      mtable[id].delete_edge(id, self_id, false);
    }
    sedge[i] = 0;
  }
  if (is_node() && sedge2_or_pid) {
    if (!(inp_mask & (1 << Num_sedges))) {
      auto id = self_id + sedge2_or_pid;
      mtable[id].delete_edge(id, self_id, false);
    }
    sedge2_or_pid = 0;
  }

  if (is_node() && ledge0_or_prev) {
    uint32_t tmp   = ledge0_or_prev;
    ledge0_or_prev = 0;
    if (!(inp_mask & (1 << (Num_sedges + 1)))) {
      mtable[tmp].delete_edge(tmp, self_id, false);
    }
  }
  if (!overflow_link && ledge1_or_overflow) {
    uint32_t tmp       = ledge1_or_overflow;
    ledge1_or_overflow = 0;
    if (!(inp_mask & (1 << (Num_sedges + 2)))) {
      mtable[tmp].delete_edge(tmp, self_id, false);
    }
  }
}

bool Graph_core::Overflow_entry::del_sedge(uint16_t id) {
  auto it = std::lower_bound(sedges.begin(), sedges.begin()+n_sedges, id);
  if (*it != id)
    return false;

  --n_sedges;

  int positions = sedges.end()-it-1;
  if (positions>0) // no memmove for last element erase
    memmove(it, it+1, sizeof(uint16_t)*positions);

  return true;
}

bool Graph_core::Overflow_entry::del_ledge(uint32_t id) {
  auto it = std::lower_bound(ledges.begin(), ledges.begin()+n_ledges, id);
  if (*it != id)
    return false;

  --n_ledges;

  int positions = ledges.end()-it-1;
  if (positions>0) // no memmove for last element erase
    memmove(it, it+1, sizeof(uint32_t)*positions);

  return true;
}

bool Graph_core::Overflow_entry::add_sedge(uint16_t id) {

  auto it = std::lower_bound(sedges.begin(), sedges.begin()+n_sedges, id);
  if (*it == id)
    return true;

  if (n_sedges >= max_sedges)
    return false;

  users.insert(it, id);
  ++n_sedges;

  return true;
}

bool Graph_core::Overflow_entry::add_ledge(uint32_t id) {

  auto it = std::lower_bound(ledges.begin(), ledges.begin()+n_ledges, id);
  if (*it == id)
    return true;

  if (n_ledges >= max_ledges)
    return false;

  users.insert(it, id);
  ++n_ledges;

  return true;
}

void Graph_core::Overflow_entry::dump(uint32_t self_id) const {
  fmt::print("  over:{} n_ledges:{} n_sedges:{}\n", self_id, n_ledges, n_sedges);

  fmt::print("    sedges:");
  for (auto i = 0u; i < n_sedges; ++i) {
    fmt::print(" {:>8}", self_id + sedges[i]);
  }
  fmt::print("\n");
  fmt::print("    ledges:");
  for (auto i = 0u; i < n_ledges; ++i) {
    fmt::print(" {:>8}", ledges[i]);
  }
  fmt::print("\n");
}

void Graph_core::dump(uint32_t id) const {
  I(!is_invalid(id));

  table[id].dump(id);

  auto over_id = table[id].get_overflow_id();
  if (over_id) {
    over_ptr->dump(over_id);
  }
}

uint32_t Graph_core::fast_next(uint32_t id) const {
  I(!is_invalid(id));

  while (true) {
    ++id;

    if (id>=table.size())
      return 0;
    if (table[id].is_node()) {
      return id;
    }
  }

  return 0;
}

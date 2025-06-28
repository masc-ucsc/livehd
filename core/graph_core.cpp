//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_core.hpp"

#include <iostream>
#include <algorithm>
#include <iterator>
#include <string>

#include <format>
#include "iassert.hpp"
#include "likely.hpp"

bool Graph_core::Master_entry::add_sedge(int16_t rel_id) {
HERE:
  FIrst insert in sedge, then the other places
#if 1
      if (n_edges < Num_sedges) {
    I(sedge[n_edges] == 0);
    sedge[n_edges] = rel_id;
    inp_mask |= ((out ? 0 : 1) << n_edges);

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
    inp_mask |= ((out ? 0 : 1) << i);
    return true;
  }
#endif

  if (is_node() && sedge2_or_pid == 0) {
    sedge2_or_pid = rel_id;
    I(n_edges == Num_sedges);
    n_edges = Num_sedges + 1;
    inp_mask |= ((out ? 0 : 1) << Num_sedges);
    return true;
  }

  return false;
}

bool Graph_core::Master_entry::add_ledge(uint32_t id, bool out) {
  if (is_node() && ledge0_or_prev == 0) {
    ledge0_or_prev = id;
    ++n_edges;
    inp_mask |= ((out ? 0 : 1) << (Num_sedges + 1));
    return true;
  }

  if (!overflow_link && ledge1_or_overflow == 0) {
    ledge1_or_overflow = id;
    ++n_edges;
    inp_mask |= ((out ? 0 : 1) << (Num_sedges + 2));
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

std::pair<Graph_core_overflow *, uint32_t> Graph_core::allocate_overflow() {
  uint32_t oid;
  if (free_overflow_id == 0) {
    oid = table.size();
    table.emplace_back();  // 2 spaces for one overflow
    table.emplace_back();
  }

  Graph_core_free_overflow *free_ent = (Graph_core_free_overflow *)&table[oid];

  free_overflow_id = free_ent->next_ptr;
  auto *ov         = free_ent->ref_overflow();
  ov->clear();
  return std::pair(ov, oid);
}

void Graph_core::add_edge_int(uint32_t self_id, uint32_t other_id) {
  Graph_core_free &ent = table[self_id];

  if (ent.is_node()) {
    bool ok = ent.ref_node()->add_edge(self_id, other_id);
    if (ok) {
      return;
    }
  } else {
    I(ent.is_pin());
    bool ok = ent.ref_pin()->add_edge(self_id, other_id);
    if (ok) {
      return;
    }
  }

HERE:
  Switch the node / pin to overflow or set
}

void Graph_core::del_pin(uint32_t self_id) {}

void Graph_core::del_node(uint32_t self_id) {
  if (table[self_id].is_pin()) {
    self_id = table[self_id].get_node_id();  // point to master
  }

HERE:
  auto next_pin_id = table[self_id].next_pin_ptr;
  del_pin(self_id);
  while (next_pin_id) {
    auto id = table[next_pin_id].next_pin_ptr;
    del_pin(next_pin_id);
    next_pin_id = id;
  }
}

void Graph_core::del_edge_int(uint32_t self_id, uint32_t other_id) {
  I(false);  // WARNING: trying to delete a node that does not exist!!
}

Graph_core::Graph_core(std::string_view n) : name(n) {
  table.emplace_back();  // Reserve entry 0 as is_invalid
  table.emplace_back();  // allocation must be 32 bytes aligned
  I(table[0].is_free());

  free_master_id   = 0;
  free_overflow_id = 0;
}

uint32_t Graph_core::create_node() {
  uint32_t id;

  if (free_master_id) {
  }

  return id;
}

uint32_t Graph_core::create_pin(const uint32_t node_id, const Port_ID pid) {
  I(node_id && node_id < table.size());

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
  if (over_id) {
    auto *over_ptr      = ref_overflow(over_id);
    auto [l_inp, l_out] = over_ptr->get_num_local_edges();
    t_inp += l_inp;
    t_out += l_out;
  }
HERE:
  set

      return std::pair(t_inp, t_out);
}

void Graph_core::Master_entry::dump(uint32_t self_id) const {
  const auto [n_i, n_o] = get_num_local_edges();

  if (node_vertex) {
    std::cout << std::format("node:{} bits:{} n_inputs:{} n_outputs:{} next:{} over:{}\n",
               self_id,
               bits,
               n_i,
               n_o,
               next_pin_ptr,
               get_overflow_id());
  } else {
    std::cout << std::format("pin:{} pid:{} bits:{} n_inputs:{} n_outputs:{} next:{} over:{} node:{}\n",
               self_id,
               get_pid(),
               bits,
               n_i,
               n_o,
               next_pin_ptr,
               get_overflow_id(),
               ledge0_or_prev);
  }

  std::cout << "  edges:";
  for (auto i = 0u; i < Num_sedges; ++i) {
    if (sedge[i]) {
      std::cout << std::format(" {}", self_id + sedge[i]);
    }
  }
  if (node_vertex && sedge2_or_pid) {
    std::cout << std::format(" {}", self_id + sedge2_or_pid);
  }
  if (node_vertex && ledge0_or_prev) {
    std::cout << std::format(" {}", ledge0_or_prev);
  }
  if (!overflow_link && ledge1_or_overflow) {
    std::cout << std::format(" {}", ledge1_or_overflow);
  }
  std::cout << "\n";
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
  auto it = std::lower_bound(sedges.begin(), sedges.begin() + n_sedges, id);
  if (*it != id) {
    return false;
  }

  --n_sedges;

  int positions = sedges.end() - it - 1;
  if (positions > 0) {  // no memmove for last element erase
    memmove(it, it + 1, sizeof(uint16_t) * positions);
  }

  return true;
}

bool Graph_core::Overflow_entry::del_ledge(uint32_t id) {
  auto it = std::lower_bound(ledges.begin(), ledges.begin() + n_ledges, id);
  if (*it != id) {
    return false;
  }

  --n_ledges;

  int positions = ledges.end() - it - 1;
  if (positions > 0) {  // no memmove for last element erase
    memmove(it, it + 1, sizeof(uint32_t) * positions);
  }

  return true;
}

bool Graph_core::Overflow_entry::add_sedge(uint16_t id) {
  auto it = std::lower_bound(sedges.begin(), sedges.begin() + n_sedges, id);
  if (*it == id) {
    return true;
  }

  if (n_sedges >= max_sedges) {
    return false;
  }

  users.insert(it, id);
  ++n_sedges;

  return true;
}

bool Graph_core::Overflow_entry::add_ledge(uint32_t id) {
  auto it = std::lower_bound(ledges.begin(), ledges.begin() + n_ledges, id);
  if (*it == id) {
    return true;
  }

  if (n_ledges >= max_ledges) {
    return false;
  }

  users.insert(it, id);
  ++n_ledges;

  return true;
}

void Graph_core::Overflow_entry::dump(uint32_t self_id) const {
  std::cout << std::format("  over:{} n_ledges:{} n_sedges:{}\n", self_id, n_ledges, n_sedges);

  std::cout << "    sedges:";
  for (auto i = 0u; i < n_sedges; ++i) {
    std::cout << std::format(" {:>8}", self_id + sedges[i]);
  }
  std::cout << "\n";
  std::cout << "    ledges:";
  for (auto i = 0u; i < n_ledges; ++i) {
    std::cout << std::format(" {:>8}", ledges[i]);
  }
  std::cout << "\n";
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

    if (id >= table.size()) {
      return 0;
    }
    if (table[id].is_node()) {
      return id;
    }
  }

  return 0;
}

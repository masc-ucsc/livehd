//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_core.hpp"


#include <algorithm>
#include <iterator>
#include <string>

void Graph_core::Master_entry::readjust_edges(uint32_t self_id, boost::container::static_vector<uint32_t,40> &pending_inp,
                                              boost::container::static_vector<uint32_t,40> &pending_out) {
  for (auto i = 0u; i < Num_sedges; ++i) {
    if (sedge[i] == 0)
      continue;

    if (inp_mask & (1 << i)) {
      pending_inp.emplace_back(self_id + sedge[i]);
    } else {
      pending_out.emplace_back(self_id + sedge[i]);
    }
    sedge[i] = 0;
  }
  if (is_node() && sedge2_or_pid) {
    if (inp_mask & (1 << Num_sedges)) {
      pending_inp.emplace_back(self_id + sedge2_or_pid);
    } else {
      pending_out.emplace_back(self_id + sedge2_or_pid);
    }
    sedge2_or_pid = 0;
  }

  if (is_node() && ledge0_or_prev) {
    uint32_t tmp   = ledge0_or_prev;
    ledge0_or_prev = 0;
    if (inp_mask & (1 << (Num_sedges + 1))) {
      pending_inp.emplace_back(tmp);
    } else {
      pending_out.emplace_back(tmp);
    }
  }
  if (!overflow_link && ledge1_or_overflow) {
    uint32_t tmp       = ledge1_or_overflow;
    ledge1_or_overflow = 0;
    if (inp_mask & (1 << (Num_sedges + 2))) {
      pending_inp.emplace_back(tmp);
    } else {
      pending_out.emplace_back(tmp);
    }
  }
}

bool Graph_core::Master_entry::insert_sedge(int16_t rel_id, bool out) {
  for (auto i = 0u; i < Num_sedges; ++i) {
    if (sedge[i])
      continue;

    sedge[i] = rel_id;
    if (out) {
      ++n_outputs;
    } else {
      inp_mask |= (1 << i);
    }
    return true;
  }

  if (is_node() && sedge2_or_pid == 0) {
    sedge2_or_pid = rel_id;
    if (out) {
      ++n_outputs;
    } else {
      inp_mask |= (1 << Num_sedges);
    }
    return true;
  }

  return false;
}

bool Graph_core::Master_entry::insert_ledge(uint32_t id, bool out) {
  if (is_node() && ledge0_or_prev == 0) {
    ledge0_or_prev = id;
    if (out) {
      ++n_outputs;
    } else {
      inp_mask |= (1 << (Num_sedges + 1));
    }
    return true;
  }

  if (!overflow_link && ledge1_or_overflow == 0) {
    ledge1_or_overflow = id;
    if (out) {
      ++n_outputs;
    } else {
      inp_mask |= (1 << (Num_sedges + 2));
    }
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
  if (out && n_outputs == 0)
    return false;  // no output in master
  if (!out && !inp_mask)
    return false;  // no input in master

  int32_t rel_id    = other_id - self_id;
  bool    short_rel = INT16_MIN < rel_id && rel_id < INT16_MAX;

  if (short_rel) {
    for (auto i = 0u; i < Num_sedges; ++i) {
      if (sedge[i] != rel_id)
        continue;

      if ((inp_mask & (1 << i)) == out)
        continue;

      sedge[i] = 0;
      if (out)
        --n_outputs;
      else
        inp_mask ^= (1 << i);

      return true;
    }
    if (node_vertex && sedge2_or_pid == rel_id) {
      if ((inp_mask & (1 << Num_sedges)) != out) {
        sedge2_or_pid = 0;
        if (out)
          --n_outputs;
        else
          inp_mask ^= (1 << Num_sedges);

        return true;
      }
    }
  }

  if (node_vertex && ledge0_or_prev == other_id && (inp_mask & (1 << (Num_sedges + 1))) != out) {
    ledge0_or_prev = 0;
    if (out)
      --n_outputs;
    else
      inp_mask ^= (1 << (Num_sedges + 1));

    return true;
  }

  if (!overflow_link && ledge1_or_overflow == other_id && (inp_mask & (1 << (Num_sedges + 2))) != out) {
    ledge1_or_overflow = 0;
    if (out)
      --n_outputs;
    else
      inp_mask ^= (1 << (Num_sedges + 2));

    return true;
  }

  return false;
}

void Graph_core::Overflow_entry::extract_all(boost::container::static_vector<uint32_t,40> &expanded) {
  I(ledge_min && ledge1 && ledge_max);

  auto v = ledge_min;
  expanded.emplace_back(v);

  for (auto i = 0u; i < sedge0_size; ++i) {
    if (sedge0[i] == 0)
      continue;

    auto v2 = ledge_min + sedge0[i];
    expanded.emplace_back(v2);
  }

  v = ledge1;
  expanded.emplace_back(v);

  for (auto i = 0u; i < sedge1_size; ++i) {
    if (sedge1[i] == 0)
      continue;

    auto v2 = ledge1 + sedge1[i];
    expanded.emplace_back(v2);
  }

  v = ledge_max;
  expanded.emplace_back(v);

  std::sort(expanded.begin(), expanded.end(), std::greater<uint32_t>());  // sort reverse order

  auto tmp_inputs           = inputs;
  auto tmp_overflow_next_id = overflow_next_id;
  clear();
  inputs           = tmp_inputs;
  overflow_next_id = tmp_overflow_next_id;
}

void Graph_core::Overflow_entry::readjust_edges(boost::container::static_vector<uint32_t,40> &pending_inp, boost::container::static_vector<uint32_t,40> &pending_out) {
  I(n_edges < max_edges);

  boost::container::static_vector<uint32_t,40> *pending = nullptr;

  if (n_edges == 0) {
    inputs = !pending_inp.empty();
  }
  if (inputs) {
    pending = &pending_inp;
  } else {
    pending = &pending_out;
  }
  if (pending->empty()) {
    I(n_edges);  // if n_edges==0, it should insert, and either inp/out should have edges
    return;
  }

  if (n_edges || pending->size()<=3) {
    // First populate the 3 ledges
    if (ledge_min == 0) {
      ledge_min = pending->back();
      I(ledge1 == 0);
      I(ledge_max == 0);
      pending->pop_back();
      ++n_edges;
      if (pending->empty())
        return;
    }
    if (ledge_max == 0) {
      ledge_max = pending->back();
      pending->pop_back();
      ++n_edges;
      I(ledge1 == 0);
      if (ledge_max < ledge_min) {
        auto tmp  = ledge_min;
        ledge_min = ledge_max;
        ledge_max = tmp;
      }
      if (pending->empty())
        return;
    }
    if (ledge1 == 0) {
      ledge1 = pending->back();
      if (ledge_max < ledge1) {
        auto tmp  = ledge1;
        ledge1    = ledge_max;
        ledge_max = tmp;
      } else if (ledge_min > ledge1) {
        auto tmp  = ledge1;
        ledge1    = ledge_min;
        ledge_min = tmp;
      }
      pending->pop_back();
      ++n_edges;
      if (pending->empty())
        return;
    }

    // TODO: Add the most common cases to avoid the large/slow expand case
    extract_all(*pending);
  }

  I(ledge_min == 0);
  I(ledge1 == 0);
  I(ledge_max == 0);

  auto     pos     = 0u;
  uint32_t last_id = 0;

  {
    // First is easy to insert, just ledge_min
    ledge_min = pending->back();
    last_id   = ledge_min;
    pending->pop_back();
    ++n_edges;
    ++pos;
  }

  while (pending->size() > 2) {
    int32_t rel_id    = pending->back() - last_id;
    bool    short_rel = INT16_MIN < rel_id && rel_id < INT16_MAX;
    if (pos < (1 + sedge0_size)) {
      I(last_id == ledge_min);
      if (short_rel) {
        sedge0[pos - 1] = rel_id;
        ++pos;
      } else {
        ledge1  = pending->back();
        last_id = ledge1;
        pos     = 1 + 1 + sedge0_size;
      }
      // KEEP last_id per sedge group last_id = pending->back();
      pending->pop_back();
      ++n_edges;
      continue;
    }

    if (pos == (1 + sedge0_size)) {
      I(ledge1 == 0);
      ledge1  = pending->back();
      last_id = ledge1;

      ++pos;

      pending->pop_back();
      ++n_edges;
      continue;
    }

    if (pos < (1 + sedge0_size + 1 + sedge1_size)) {
      I(last_id == ledge1);
      if (!short_rel) {
        ledge_max = pending->back();
        pending->pop_back();
        ++n_edges;
        return;
      }

      sedge1[pos - (1 + sedge0_size + 1)] = rel_id;
      ++pos;
      // KEEP last_id per sedge group last_id = pending->back();
      pending->pop_back();
      ++n_edges;
      continue;
    }

    if (pos == (1 + sedge0_size + 1 + sedge1_size)) {
      ledge_max = pending->back();
      pending->pop_back();
      ++n_edges;
      return;
    }
  }

  I(ledge_max == 0);
  if (ledge1 == 0) {
    I(pending->size() == 2);
    ledge1    = (*pending)[1];
    ledge_max = (*pending)[0];
    pending->clear();
    n_edges += 2;
  } else {
    ledge_max = pending->back();
    pending->pop_back();
    ++n_edges;
  }
}

bool Graph_core::Overflow_entry::delete_edge_rebalance_ledges(uint32_t other_id) {
  if (n_edges <= 3) {
    if (ledge_min == other_id) {
      if (ledge1) {
        ledge_min = ledge1;
        ledge1    = 0;
      } else {
        ledge_min = ledge_max;
        ledge_max = 0;
      }
      --n_edges;
      return true;
    }
    if (ledge1 == other_id) {
      I(n_edges == 3);
      ledge1 = 0;
      --n_edges;
      return true;
    }
    if (ledge_max == other_id) {
      ledge_max = ledge1;
      ledge1    = 0;
      --n_edges;
      return true;
    }
    return false;
  }

  auto deleted_id = 0;
  if (ledge_min == other_id) {
    deleted_id = ledge_min;
    ledge_min  = 0;
  } else if (ledge1 == other_id) {
    deleted_id = ledge1;
    ledge1     = 0;
  } else if (ledge_max == other_id) {
    deleted_id = ledge_max;
    ledge_max  = 0;
  } else {
    return false;
  }
  --n_edges;

  if (ledge_min == 0) {  // Move something to ledge_min
    I(ledge1 && ledge_max);

    // Look for smallest delta in sedge0
    int     pos      = -1;
    int16_t smallest = INT16_MAX;
    for (auto i = 0u; i < sedge0_size; ++i) {
      if (sedge0[i] && sedge0[i] < smallest) {
        smallest = sedge0[i];
        pos      = i;
      }
    }
    if (pos >= 0) {
      ledge_min   = deleted_id + smallest;
      sedge0[pos] = 0;

      for (auto i = 0u; i < sedge0_size; ++i) {  // adjust deltas
        if (sedge0[i] == 0)
          continue;

        int32_t s = sedge0[i] - smallest;
        if (INT16_MIN > s || s > INT16_MAX) {
          I(false);  // FIXME: complex rebalance needed
        }
        sedge0[i] = static_cast<int16_t>(s);
      }
      return true;
    }

    deleted_id = ledge1;
    ledge_min  = ledge1;
    ledge1     = 0;
  }

  if (ledge1 == 0) {  // Move something to ledge
    I(ledge_min && ledge_max);

    // Look for smallest delta in sedge1
    {
      int     pos      = -1;
      int16_t smallest = INT16_MAX;
      for (auto i = 0u; i < sedge1_size; ++i) {
        if (sedge1[i] && sedge1[i] < smallest) {
          smallest = sedge1[i];
          pos      = i;
        }
      }
      if (pos >= 0) {
        ledge1      = deleted_id + smallest;
        sedge1[pos] = 0;

        for (auto i = 0u; i < sedge1_size; ++i) {  // adjust deltas
          if (sedge1[i] == 0)
            continue;

          int32_t s = sedge1[i] - smallest;
          if (INT16_MIN > s || s > INT16_MAX) {
            I(false);  // FIXME: complex rebalance needed
          }
          sedge1[i] = static_cast<int16_t>(s);
        }
        return true;
      }
    }

    // This means that there was nothing in sedge1. If so, there must be
    // something in sedge0 to replace ledge1

    // Look for largest delta in sedge0
    {
      int     pos     = -1;
      int16_t largest = INT16_MIN;
      for (auto i = 0u; i < sedge0_size; ++i) {
        if (sedge0[i] && sedge0[i] > largest) {
          largest = sedge0[i];
          pos     = i;
        }
      }
      I(pos >= 0);
      ledge1      = ledge_min + largest;
      sedge0[pos] = 0;
    }
    return true;
  }

  I(ledge_max == 0);  // Move something to ledge_max
  I(ledge_min && ledge1);

  // Look for largest delta in sedge1
  int     pos     = -1;
  int16_t largest = INT16_MIN;
  for (auto i = 0u; i < sedge1_size; ++i) {
    if (sedge1[i] && sedge1[i] > largest) {
      largest = sedge1[i];
      pos     = i;
    }
  }

  if (pos >= 0) {
    sedge1[pos] = 0;
    ledge_max   = ledge1 + largest;
    return true;
  }

  ledge_max = ledge1;

  // Look for largest in sedge0 and move to ledge1
  pos     = -1;
  largest = INT16_MIN;
  for (auto i = 0u; i < sedge0_size; ++i) {
    if (sedge0[i] && sedge0[i] > largest) {
      largest = sedge0[i];
      pos     = i;
    }
  }
  I(pos >= 0);
  sedge0[pos] = 0;
  ledge1      = ledge_min + largest;
  return true;
}

bool Graph_core::Overflow_entry::delete_edge(uint32_t other_id, bool out) {

  if (likely(inputs == out || other_id < ledge_min || (other_id > ledge_max && ledge_max)))
    return false;

  bool done = delete_edge_rebalance_ledges(other_id);
  if (done)
    return true;

  {
    int32_t rel_index = other_id - ledge_min;
    bool    short_rel = INT16_MIN < rel_index && rel_index < INT16_MAX;

    if (short_rel) {
      for (auto i = 0u; i < sedge0_size; ++i) {
        if (sedge0[i] == static_cast<int16_t>(rel_index)) {
          sedge0[i] = 0;
          --n_edges;
          ++i;
          return true;
        }
      }
    }
  }
  I(ledge1 != other_id);

  {
    int32_t rel_index = other_id - ledge1;
    bool    short_rel = INT16_MIN < rel_index && rel_index < INT16_MAX;

    if (short_rel) {
      for (auto i = 0u; i < sedge1_size; ++i) {
        if (sedge1[i] == static_cast<int16_t>(rel_index)) {
          sedge1[i] = 0;
          --n_edges;
          ++i;
          return true;
        }
      }
    }
  }

  I(ledge_max != other_id);

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
    bool ok = ent.insert_sedge(static_cast<int16_t>(rel_index), out);
    if (ok)
      return;
  }

  bool ok = ent.insert_ledge(other_id, out);
  if (ok)
    return;

  boost::container::static_vector<uint32_t,40> pending_inp;
  boost::container::static_vector<uint32_t,40> pending_out;

  ent.readjust_edges(self_id, pending_inp, pending_out);

  if (out)
    pending_out.emplace_back(other_id);
  else
    pending_inp.emplace_back(other_id);

  std::sort(pending_inp.begin(), pending_inp.end(), std::greater<uint32_t>());  // sort reverse order
  std::sort(pending_out.begin(), pending_out.end(), std::greater<uint32_t>());  // sort reverse order

  ent.inp_mask  = 0;
  ent.n_outputs = 0;

  auto overflow_id = ent.get_overflow_id();
  if (overflow_id == 0) {
    I(ent.ledge1_or_overflow == 0);
    overflow_id = allocate_overflow();
    // Can not use ent, allocate_overflow can change pointers
    table[self_id].set_overflow(overflow_id);
  }
  I(overflow_id);

  std::vector<uint32_t> add_to_end;
  uint32_t prev_over_id = 0;

  while (true) {
    auto *over_ptr = ref_overflow(overflow_id);
    I(over_ptr);

    if (over_ptr->is_full()) { // There was no space. The rest are full
			auto new_over_id = allocate_overflow();
      if (prev_over_id) {
        // Before: A(nonfull) -> prev(nonfull)       ->         over (full) -> B(full)
        // After : A(nonfull) -> prev(nonfull) -> new(empty) -> over (full) -> B(full)
        ref_overflow(prev_over_id)->overflow_next_id = new_over_id;
      }else{
        // Before: master     ->    over(full) -> B(full)
        // After : master -> new -> over(full) -> B(full)
        I(table[self_id].ledge1_or_overflow == overflow_id);
        table[self_id].ledge1_or_overflow = new_over_id;
      }
      over_ptr = ref_overflow(new_over_id);
      over_ptr->overflow_next_id  = overflow_id;
      overflow_id = new_over_id;
    }

    I(over_ptr->n_edges < Overflow_entry::max_edges);
    over_ptr->readjust_edges(pending_inp, pending_out);

    if (over_ptr->is_full()) { // Move to the end. Where fulls start
      auto next_id            = over_ptr->get_overflow_id();
      bool should_move_to_end = true;
      if (next_id==0 || ref_overflow(next_id)->is_full()) {
        // No need to move, if it is the last or next is full
        should_move_to_end = false;
      }

      if (should_move_to_end) {
        if (prev_over_id) {
          // Before: A(nonfull) -> prev(nonfull) -> over (full) -> next(nonfull)
          // After : A(nonfull) -> prev(nonfull)                -> next(nonfull)
          ref_overflow(prev_over_id)->overflow_next_id = next_id;
        }else{
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
      if (add_to_end.empty())
        return; // Nothing to do

      // Find the last or the first empty (start with overflow_id)
      // enqueue the add_to_end
      uint32_t next_id = overflow_id;
      uint32_t last_id = 0;
      while (true) {
        last_id = next_id;
        next_id = over_ptr->get_overflow_id();
        if (next_id==0)
          break;
        over_ptr = ref_overflow(next_id);
        if (over_ptr->is_full())
          break;
      }

      ref_overflow(last_id)->overflow_next_id = add_to_end.front();
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

void Graph_core::del_edge_int(uint32_t self_id, uint32_t other_id, bool out) {
  bool done1 = table[self_id].delete_edge(self_id, other_id, out);
  if (done1)
    return;

  auto            over_id  = table[self_id].get_overflow_id();
  Overflow_entry *prev_ptr = nullptr;
  while (over_id) {
    auto *over_ptr = ref_overflow(over_id);
    bool  done2    = over_ptr->delete_edge(other_id, out);
    if (done2) {
      if (over_ptr->has_local_edges()) {
        if (prev_ptr==nullptr)
          return;

        if (!prev_ptr->is_full())
          return;

        // If prev is full, move to first position (fulls must be at the end)
        prev_ptr->overflow_next_id = over_ptr->get_overflow_id();
        auto tmp_id = table[self_id].get_overflow_id();
        table[self_id].ledge1_or_overflow = over_id;
        over_ptr->overflow_next_id = tmp_id;
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

  if (root_ent.next_ptr == 0) {
    root_ent.next_ptr = id;  // easy case, first pin
    return id;
  }

  // Insert pin in pid order (easier search)
  auto prev_ptr = node_id;
  auto ptr      = root_ent.next_ptr;
  while (ptr && table[ptr].get_pid() > pid) {
    prev_ptr = ptr;
    ptr      = table[ptr].next_ptr;
  }
  I(prev_ptr);

  auto insert_ptr          = table[prev_ptr].next_ptr;
  table[prev_ptr].next_ptr = id;
  table[id].next_ptr       = insert_ptr;

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
    fmt::print("node:{} bits:{} n_inputs:{} n_outputs:{} next:{} over:{}\n", self_id, bits, n_i, n_o, next_ptr, get_overflow_id());
  } else {
    fmt::print("pin:{} pid:{} bits:{} n_inputs:{} n_outputs:{} next:{} over:{} node:{}\n",
               self_id,
               get_pid(),
               bits,
               n_i,
               n_o,
               next_ptr,
               get_overflow_id(),
               ledge0_or_prev);
  }

  fmt::print("  edges:");
  for (auto i = 0u; i < Num_sedges; ++i) {
    if (sedge[i])
      fmt::print(" {}", self_id + sedge[i]);
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

void Graph_core::Overflow_entry::dump(uint32_t self_id) const {
  fmt::print("  over:{} {} n_edges:{} next_id:{}\n", self_id, inputs ? "inp" : "out", n_edges, overflow_next_id);

  fmt::print("    edges:");
  if (ledge_min)
    fmt::print(" min:{}", ledge_min);

  for (auto i = 0u; i < sedge0_size; ++i) {
    if (sedge0[i])
      fmt::print(" f{}", ledge_min + sedge0[i]);
  }
  if (ledge1) {
    fmt::print(" ledge1:{}", ledge1);
  }
  for (auto i = 0u; i < sedge1_size; ++i) {
    if (sedge1[i])
      fmt::print(" s{}", ledge1 + sedge1[i]);
  }
  if (ledge_max)
    fmt::print(" max:{}", ledge_max);
  fmt::print("\n");
}

void Graph_core::dump(uint32_t id) const {
  I(!is_invalid(id));

  table[id].dump(id);

  auto over_id = table[id].get_overflow_id();
  while (over_id) {
    auto *over_ptr = ref_overflow(over_id);
    over_ptr->dump(over_id);
    over_id = over_ptr->get_overflow_id();
  }
}

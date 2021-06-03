//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_core.hpp"

#include <algorithm>
#include <iterator>
#include <string>

void Graph_core::Master_entry::readjust_edges(uint32_t self_id, std::vector<uint32_t> &pending_inp, std::vector<uint32_t> &pending_out) {
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
  if (is_master_root() && sedge2_or_pid) {
    if (inp_mask & (1 << Num_sedges)) {
      pending_inp.emplace_back(self_id + sedge2_or_pid);
    } else {
      pending_out.emplace_back(self_id + sedge2_or_pid);
    }
    sedge2_or_pid = 0;
  }

  if (is_master_root() && ledge0_or_prev) {
    uint32_t tmp = ledge0_or_prev;
    ledge0_or_prev = 0;
    if (inp_mask & (1 << (Num_sedges+1))) {
      pending_inp.emplace_back(tmp);
    } else {
      pending_out.emplace_back(tmp);
    }
  }
  if (!overflow_link && ledge1_or_overflow) {
    uint32_t tmp = ledge1_or_overflow;
    ledge1_or_overflow = 0;
    if (inp_mask & (1 << (Num_sedges+2))) {
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
  if (is_master_root() && sedge2_or_pid == 0) {
    sedge2_or_pid = rel_id;
    if (out) {
      ++n_outputs;
    } else {
      inp_mask |= (1 << Num_sedges);
    }
  }
  return false;
}

bool Graph_core::Master_entry::insert_ledge(uint32_t id, bool out) {
  if (is_master_root() && ledge0_or_prev == 0) {
    ledge0_or_prev = id;
    if (out) {
      ++n_outputs;
    } else {
      inp_mask |= (1 << (Num_sedges+1));
    }
    return true;
  }

  if (!overflow_link && ledge1_or_overflow == 0) {
    ledge1_or_overflow = id;
    if (out) {
      ++n_outputs;
    } else {
      inp_mask |= (1 << (Num_sedges+2));
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
bool Graph_core::Master_entry::delete_edge(uint32_t self_id, uint32_t id) {
  (void)self_id;
  (void)id;

  I(false); // FIXME: TODO

  return true;
}

void Graph_core::Overflow_entry::extract_all(uint32_t self_id, std::vector<uint32_t> &expanded) {

  uint32_t last=self_id;

  I(ledge_min && ledge1 && ledge_max);

  last = ledge_min;
  expanded.emplace_back(last);

  for(auto i=0u;i<sedge0_size;++i) {
    if (sedge0[i]==0)
      continue;

    last = last + sedge0[i];
    expanded.emplace_back(last);
  }

  last = ledge1;
  expanded.emplace_back(last);

  for(auto i=0u;i<sedge1_size;++i) {
    if (sedge1[i]==0)
      continue;
    last = last + sedge1[i];
    expanded.emplace_back(last);
  }

  last = ledge_max;
  expanded.emplace_back(last);

  std::sort(expanded.begin(), expanded.end(), std::greater<uint32_t>()); // sort reverse order

  auto tmp_inputs = inputs;
  auto tmp_overflow_next_id = overflow_next_id;
  clear();
  inputs = tmp_inputs;
  overflow_next_id = tmp_overflow_next_id;
}

void Graph_core::Overflow_entry::readjust_edges(uint32_t overflow_id, std::vector<uint32_t> &pending_inp,
                                            std::vector<uint32_t> &pending_out) {

  if (n_edges >= max_edges)
    return; // Nothing to do, next bucket

  std::vector<uint32_t> *pending=nullptr;

  if (inputs) {
    pending = &pending_inp;
  }else{
    pending = &pending_out;
  }
  if (pending->empty())
    return;

  auto id = pending->back();

  // First populate the 3 ledges
  if (n_edges<=2) { //0, 1, 2
    I(ledge_max);
    if (ledge_max<id) {
      ledge_min = ledge1;
      ledge1 = ledge_max;
      ledge_max = id;
    }else if (ledge1<id){
      ledge_min = ledge1;
      ledge1 = id;
    }else{
      ledge_min = id;
    }
    ++n_edges;
    pending->pop_back();
    return;
  }

  // TODO: Add the most common cases to avoid the large/slow expand case

  extract_all(overflow_id, *pending);

  auto pos=0u;
  uint32_t last_id=0;
  while(!pending->empty()) {
    if (pos==0) {
      ledge_min = pending->back();
      last_id = ledge_min;
      pending->pop_back();
      ++n_edges;
      continue;
    }

    int32_t rel_id = last_id - pending->back();
    bool short_rel = INT32_MIN > rel_id && rel_id < INT32_MAX;
    if (pos<(1+sedge0_size)) {
      if (short_rel) {
        sedge0[pos-11] = short_rel;
        ++pos;
      }else{
        ledge1 = pending->back();
        pos = 1+1+sedge0_size;
      }
      pending->pop_back();
      ++n_edges;
      continue;
    }

    if (pos == (1+sedge0_size)) {
      ledge1 = pending->back();
      last_id = ledge1;
      pending->pop_back();
      ++n_edges;
      continue;
    }

    if (pos<(1+sedge0_size+1+sedge1_size)) {
      if (short_rel) {
        sedge1[pos-11] = short_rel;
        ++pos;
      }else{
        ledge_max = pending->back();
        pos = 1+1+sedge0_size+1+sedge1_size;
      }
      pending->pop_back();
      ++n_edges;
      continue;
    }

    if (pos==(1+sedge0_size+1+sedge1_size)) {
      ledge1 = pending->back();
      last_id = ledge1;
      pending->pop_back();
      ++n_edges;
      return;
    }
  }
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

  Overflow_entry *ent    = (Overflow_entry *)&table[oid];
  ent->overflow_node = 1;

  return oid;
}

void Graph_core::add_edge_int(uint32_t self_id, uint32_t other_id, bool out) {
  Master_entry &ent       = table[self_id];
  int32_t      rel_index = self_id - other_id;
  bool         short_rel = INT32_MIN > rel_index && rel_index < INT32_MAX;

  if (short_rel) {
    bool ok = ent.insert_sedge(static_cast<int16_t>(rel_index), out);
    if (ok)
      return;
  }

  bool ok = ent.insert_ledge(other_id, out);
  if (ok)
    return;

  std::vector<uint32_t> pending_inp;
  std::vector<uint32_t> pending_out;

  ent.readjust_edges(self_id, pending_inp, pending_out);

  if (out)
    pending_out.emplace_back(other_id);
  else
    pending_inp.emplace_back(other_id);

  std::sort(pending_inp.begin(), pending_inp.end(), std::greater<uint32_t>()); // sort reverse order
  std::sort(pending_out.begin(), pending_out.end(), std::greater<uint32_t>()); // sort reverse order

  ent.inp_mask  = 0;
  ent.n_outputs = 0;

  auto overflow_id = ent.get_overflow_id();
  if (overflow_id == 0) {
    I(ent.ledge1_or_overflow == 0);
    overflow_id = allocate_overflow();
    ent.set_overflow(overflow_id);
  }
  I(overflow_id);

  while (true) {
    auto *ref_overflow = (Overflow_entry *)&table[overflow_id];
    I(ref_overflow);
    ref_overflow->readjust_edges(overflow_id, pending_inp, pending_out);

    if (pending_inp.empty() && pending_out.empty())
      return;

    overflow_id           = ref_overflow->get_overflow_id();
    if (overflow_id == 0) {
      overflow_id                    = allocate_overflow();
      ref_overflow->overflow_next_id = overflow_id;
    }
  }
}

void Graph_core::del_edge_int(uint32_t self_id, uint32_t other_id, bool out) {
  (void)self_id;
  (void)other_id;
  (void)out;
  I(false);  // HERE
}

Graph_core::Graph_core(std::string_view path, std::string_view name) {
  (void)path;
  (void)name;

  table.emplace_back(); // Reserve entry 0 as is_invalid
  table[0].overflow_node = true; // mark invalid type

  free_master_id = 0;
  free_overflow_id = 0;
}

/*  Create a master_root node
 *
 *  @params uint8_t type
 *  @returns uint32_t of the node
 */

uint32_t Graph_core::create_master_root() {
  uint32_t id;

  if (free_master_id) {
    id             = free_master_id;
    free_master_id = table[id].remove_free_next();
  } else {
    id = table.size();
    table.emplace_back();
  }
  I(!table[id].has_edges() && table[id].master_root_node == 0);  // initialized to zero

  table[id].master_root_node = 1;

  return id;
}

/*  Create a master and point to master root m
 *
 *  @params const Index_ID master_root_id, const Port_ID pid
 *  @returns Index_ID of the node
 */

uint32_t Graph_core::create_master(const uint32_t master_root_id, const Port_ID pid) {
  I(master_root_id && master_root_id < table.size());

  auto  id         = create_master_root();
  auto &master_ent = table[id];

  master_ent.master_root_node  = 0;
  master_ent.ledge0_or_prev     = master_root_id;
  master_ent.set_pid(pid);

  auto &root_ent = table[master_root_id];
  I(root_ent.is_master_root());

  if (root_ent.next_ptr == 0) {
    root_ent.next_ptr = id;  // easy case, first master
    return id;
  }

  // Insert master in pid order (easier search)

  auto prev_ptr = master_root_id;
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

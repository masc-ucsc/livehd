// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_synth.hpp"

#include "cell.hpp"
#include "pass.hpp"

Label_synth::Label_synth(bool _verbose, bool _hier) : verbose(_verbose), hier(_hier) { (void)verbose; }


int Label_synth::get_free_id() {
  return last_free_id++;
}

void Label_synth::set_id(const Node &node, int id) {
  auto [it, inserted] = flat_node2id.insert({node.get_compact_flat(), id});
  if (inserted || id == it->second)
    return;

  auto it2 = flat_merges.find(it->second);
  if (it2 == flat_merges.end())
    flat_merges[id] = it->second;
  else
    flat_merges[id] = it2->second;
}

void Label_synth::mark_ids(Lgraph *g) {

  g->each_graph_input([&](const Node_pin &pin) {
    auto id = get_free_id();
    for(const auto &e:pin.out_edges()) {
      set_id(e.sink.get_node(), id);
    }
  });

#if 0
  g->each_graph_output([&](const Node_pin &pin) {
    (void)pin;  // to avoid warning
  });
#endif

  for (auto node : g->forward(hier)) {
    if (node.is_type_loop_last())
      continue;
    if (node.is_type_const())
      continue; // consts do not need to create new IDs

    int id = 0;
    auto it = flat_node2id.find(node.get_compact_flat());
    if (it == flat_node2id.end()) {
      id = get_free_id();
    }else{
      id = it->second;
    }

    for(const auto &e:node.out_edges()) {
      set_id(e.sink.get_node(), id);
    }
  }
}

int Label_synth::collapse_merge(int dst) {
  auto it = flat_merges.find(dst);
  if (it == flat_merges.end())
    return dst;

  it->second = collapse_merge(it->second);
  return it->second;
}

void Label_synth::merge_ids() {

  // 1st collapse flat_merges
  {
    bool updated;
    do {
      updated = false;
      for(auto &it:flat_merges) {
        auto id = collapse_merge(it.second);
        if (id==it.second)
          continue;
        it.second = id;
        updated = true;
      }
    }while(updated);
  }

  // 2nd relabel
  for(auto &it:flat_node2id) {
    auto it2 = flat_merges.find(it.second);
    if (it2 == flat_merges.end())
      continue;
    if (it2->second == it.second)
      continue;

    it.second = it2->second;
  }

#ifndef NDEBUG
  for(auto &it:flat_node2id) {
    auto it2 = flat_merges.find(it.second);
    if (it2 == flat_merges.end())
      continue;
    if (it2->second == it.second)
      continue;
    auto id1 = collapse_merge(it.second);
    auto id2 = collapse_merge(it2->second);
    if (id1 == id2 && it2->second == id2)
      continue;
    I(false); // The collapse_merge avoid to trigger this many times
  }
#endif

}

void Label_synth::dump() const {
  for(auto &it:flat_merges) {
    fmt::print("{} -> {}\n", it.first, it.second);
  }
}

void Label_synth::label(Lgraph *g) {

  last_free_id = 1;

  mark_ids(g);
  merge_ids();

  dump();

  for(auto &it:flat_node2id) {
    Node node(g, it.first);
    fmt::print(":{} node:{}\n", it.second, node.debug_name());
  }
}


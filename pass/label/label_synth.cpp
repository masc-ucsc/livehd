// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_synth.hpp"

#include "annotate.hpp"
#include "cell.hpp"
#include "pass.hpp"

Label_synth::Label_synth(bool _verbose, bool _hier, std::string_view alg) : verbose(_verbose), hier(_hier) {

	if (alg =="pipe") {
		synth = false;
	}else if (alg == "synth") {
		synth = true;
	}else{
		Pass::error("unknown algorithm {} for pass.label.synth", alg);
	}
}

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

#if 1
  // Do we cluster inputs? (FIXME: option)
  g->each_graph_input([&](const Node_pin &pin) {
    auto id = get_free_id();
    for(const auto &e:pin.out_edges()) {
      auto node = e.sink.get_node();
      if (!node.is_type_loop_last())
        set_id(node, id);
    }
  });
#endif

  for (auto node : g->forward(hier)) {
    if (node.is_type_loop_last())
      continue;
    if (node.is_type_const())
      continue; // consts do not need to create new IDs

		if (synth) {
			auto op = node.get_type_op();
			if (op == Ntype_op::Mult || op == Ntype_op::Div)
				continue;
			auto b = node.get_driver_pin().get_bits();
			if (op == Ntype_op::Sum && b > 8)
				continue;
		}

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

static int recursion=0;

void Label_synth::collapse_merge(int dst) {
  if (recursion>100) {
    //dump();
    fmt::print("d:{}\n",dst);
  }

  if (collapse_set_min > dst)
    collapse_set_min = dst;

  auto it = flat_merges.find(dst);
  if (it == flat_merges.end())
    return;

  collapse_set.insert(dst);
  if (collapse_set.contains(it->second))
    return;

  recursion++;
  collapse_merge(it->second);
  recursion--;

  flat_merges[dst]=collapse_set_min;
}

void Label_synth::merge_ids() {

  // 1st collapse flat_merges
  {
    bool updated;
    do {
      updated = false;
      for(auto &it:flat_merges) {
        collapse_set.clear();
        collapse_set.insert(it.second);
        collapse_set_min = it.second;
        collapse_merge(it.second);
        if (collapse_set_min==it.second)
          continue;
        it.second = collapse_set_min;
        updated = true;
      }
    }while(updated);
  }

  // 2nd relabel
  for(auto &it:flat_node2id) {
    auto it2 = flat_merges.find(it.second);
    if (it2 == flat_merges.end())
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
    collapse_set.clear();
    collapse_set_min = it.second;
    collapse_merge(it.second);
    collapse_merge(it2->second);
    if (it2->second == it.second && it.second == collapse_set_min)
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

  if (hier) {
    g->each_hier_unique_sub_bottom_up([](Lgraph *lg) {
      Ann_node_color::clear(lg);
    });
  }
  Ann_node_color::clear(g);

  for(auto &it:flat_node2id) {
    Node node(g, it.first);
    node.set_color(it.second);
  }

  if (verbose) {
    //dump();

    for(auto &it:flat_node2id) {
      Node node(g, it.first);
      fmt::print(":{} node:{}\n", it.second, node.debug_name());
    }
  }
}


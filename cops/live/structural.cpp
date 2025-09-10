/*
 * @Author: your name
 * @Date: 2021-03-29 20:17:37
 * @LastEditTime: 2021-03-29 22:23:22
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: /livehd1/cops/live/structural.cpp
 */
//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "structural.hpp"

#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <queue>

#include "lgedgeiter.hpp"

Live_structural::Live_structural(Stitch_pass_options &pack) {
  std::ifstream invariant_file(pack.boundaries_name);

  if (!invariant_file.good()) {
    Pass::error(std::format("Error reading boundaries file {}", pack.boundaries_name));
    return;
  }

  boundaries = Invariant_boundaries::deserialize(invariant_file);
  invariant_file.close();

  original = Lgraph_open(pack.osynth_lgdb, boundaries->top);

  if (!original) {
    Pass::error(std::format("I was not able to open original synthesized netlist {} in {}", boundaries->top, pack.osynth_lgdb));
  }
}

Node_pin Live_structural::get_inp_edge(Lgraph *current, Index_id nid, Port_ID pid) {
  Node_pin candidate;
  bool     found = false;
  for (auto &inp : current->inp_edges(nid)) {
    if (inp.get_inp_pin().get_pid()) {
      I(!found);
      candidate = inp.get_inp_pin();
    }
  }
  I(found);
  return candidate;
}

// void Live_structural::replace(Lgraph* nsynth, std::set<Net_ID>& diffs) {
void Live_structural::replace(Lgraph *nsynth) {
  std::map<Index_id, Index_id> candidate_equiv;
  std::set<Index_id>           no_match;
  std::set<Index_id>           visited;

  std::set<Index_id>                                                      outputs;
  std::priority_queue<queue_element, std::vector<queue_element>, Compare> discovered;
  for (auto &idx : nsynth->fast()) {
    if (nsynth->is_graph_output(idx)) {
      outputs.insert(idx);
      discovered.push(queue_element(idx, 0));
      if (Index_id oid = get_candidate(idx, nsynth)) {
        candidate_equiv[idx] = oid;
      } else {
        I(false);
      }
    }
  }

  while (discovered.size() > 1) {
    queue_element current_ = discovered.top();
    int           prio     = current_.priority;
    Index_id      current  = current_.id;
    discovered.pop();
    visited.insert(current);
    if (candidate_equiv.find(current) == candidate_equiv.end()) {
      // just add it to the end
      // FIXME: what happens if there is a loop?
      discovered.push(current_);
      // std::print("pushing back {} due to no candidate equiv\n", current);
      continue;
    }

    Index_id candidate_current = candidate_equiv[current];

    for (auto &pred : nsynth->inp_edges(current)) {
      discovered.push(queue_element(pred.get_inp_pin().get_pid(), current_.priority + 1));
      for (auto &orig_pred : original->inp_edges(candidate_current)) {
        if (orig_pred.get_inp_pin().get_pid() == pred.get_inp_pin().get_pid()) {
          if (candidate_equiv.find(pred.get_out_pin().get_idx()) == candidate_equiv.end()) {
            candidate_equiv[pred.get_out_pin().get_idx()] = orig_pred.get_out_pin().get_idx();
          } else {
            if (candidate_equiv[pred.get_out_pin().get_idx()] != orig_pred.get_out_pin().get_idx()) {
              no_match.insert(pred.get_out_pin().get_idx());
            }
          }
          if (visited.find(pred.get_out_pin().get_idx()) == visited.end()) {
            discovered.push(queue_element(pred.get_out_pin().get_idx(), prio + 1));
          }
        }
      }
    }
  }

  int                count = 0;
  std::set<Index_id> match;
  for (auto &equivs : candidate_equiv) {
    if (no_match.find(equivs.first) != no_match.end()) {
      continue;
    }

    count++;
  }

  std::print("nomatch {}, match {}\n", no_match.size(), count);
}

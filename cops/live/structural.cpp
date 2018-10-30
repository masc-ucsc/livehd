//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "structural.hpp"
#include "lgedgeiter.hpp"
#include <functional>
#include <queue>

Live_structural::Live_structural(Stitch_pass_options &pack) {

  std::ifstream    invariant_file(pack.boundaries_name);

  if(!invariant_file.good()) {
    console->error("Error reading boundaries file {}\n", pack.boundaries_name);
    exit(1);
  }

  boundaries = Invariant_boundaries::deserialize(invariant_file);
  invariant_file.close();

  original = LGraph::open(pack.osynth_lgdb, boundaries->top);

  if(!original) {
    console->error("I was not able to open original synthesized netlist {} in {}\n", boundaries->top, pack.osynth_lgdb);
    exit(1);
  }
}


Node_Pin Live_structural::get_inp_edge(LGraph *current, Index_ID nid, Port_ID pid) {
  Node_Pin candidate(1, 1, false);
  bool     found = false;
  for(auto &inp : current->inp_edges(nid)) {
    if(inp.get_inp_pin().get_pid()) {
      assert(!found);
      candidate = inp.get_inp_pin();
    }
  }
  assert(found);
  return candidate;
}

//void Live_structural::replace(LGraph* nsynth, std::set<Net_ID>& diffs) {
void Live_structural::replace(LGraph *nsynth) {

  std::map<Index_ID, Index_ID> candidate_equiv;
  std::set<Index_ID>           no_match;
  std::set<Index_ID>           visited;

  std::set<Index_ID>                                                      outputs;
  std::priority_queue<queue_element, std::vector<queue_element>, Compare> discovered;
  for(auto &idx : nsynth->fast()) {
    if(nsynth->is_graph_output(idx)) {
      outputs.insert(idx);
      discovered.push(queue_element(idx, 0));
      if(Index_ID oid = get_candidate(idx, nsynth))
        candidate_equiv[idx] = oid;
      else
        assert(false);
    }
  }

  while(discovered.size() > 1) {
    queue_element current_ = discovered.top();
    int           prio     = current_.priority;
    Index_ID      current  = current_.id;
    discovered.pop();
    visited.insert(current);
    if(candidate_equiv.find(current) == candidate_equiv.end()) {
      //just add it to the end
      //FIXME: what happens if there is a loop?
      discovered.push(current_);
      //fmt::print("pushing back {} due to no candidate equiv\n", current);
      continue;
    }

    Index_ID candidate_current = candidate_equiv[current];

    for(auto &pred : nsynth->inp_edges(current)) {
      discovered.push(queue_element(pred.get_inp_pin().get_pid(), current_.priority + 1));
      for(auto &orig_pred : original->inp_edges(candidate_current)) {
        if(orig_pred.get_inp_pin().get_pid() == pred.get_inp_pin().get_pid()) {
          if(candidate_equiv.find(pred.get_out_pin().get_nid()) == candidate_equiv.end()) {
            candidate_equiv[pred.get_out_pin().get_nid()] = orig_pred.get_out_pin().get_nid();
          } else {
            if(candidate_equiv[pred.get_out_pin().get_nid()] != orig_pred.get_out_pin().get_nid()) {
              no_match.insert(pred.get_out_pin().get_nid());
            }
          }
          if(visited.find(pred.get_out_pin().get_nid()) == visited.end()) {
            discovered.push(queue_element(pred.get_out_pin().get_nid(), prio + 1));
          }
        }
      }
    }
  }

  int                count = 0;
  std::set<Index_ID> match;
  for(auto &equivs : candidate_equiv) {
    if(no_match.find(equivs.first) != no_match.end())
      continue;

    count++;
  }

  fmt::print("nomatch {}, match {}\n", no_match.size(), count);
}

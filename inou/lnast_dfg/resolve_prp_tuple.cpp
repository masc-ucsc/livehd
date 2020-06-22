// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <queue>
#include "inou_lnast_dfg.hpp"

void Inou_lnast_dfg::resolve_tuples(Eprp_var &var) {
  Inou_lnast_dfg p(var);
  std::vector<LGraph *> lgs;

  for (const auto &l : var.lgs) {
    p.do_resolve_tuples(l);
  }
}

void Inou_lnast_dfg::do_resolve_tuples(LGraph *dfg) {

  //resolve TupGet source and destination
  absl::flat_hash_set<Node> to_be_deleted;
  absl::flat_hash_map<Node_pin, Node_pin> tg2actual_dpin; //record tuple_get to its actual reference dpin
  for (const auto &node : dfg->fast()) {
    if (node.get_type().op == TupAdd_Op) {
      collect_node_for_deleting(node, to_be_deleted);
      continue;
    }

    if (node.get_type().op == TupGet_Op and tuple_get_has_key_name(node)) {

      auto tup_get_target = node.get_sink_pin(KN).get_driver_pin().get_name();


      if (tup_get_target.substr(0,6) == "__bits") {
        continue; // __bits @rhs, cannot know the bits information after the pass/bitwidh, keep this tuple_get and handle it until BW
      }
      
      collect_node_for_deleting(node, to_be_deleted);

      if (tup_get_target.substr(0,7) == "__q_pin") {
        reconnect_to_ff_qpin(dfg, node);
        continue;
      }


      auto chain_itr = node.get_sink_pin(TN).get_driver_node();
      while (chain_itr.get_type().op != TupRef_Op) {
        Node next_itr;
        if (chain_itr.get_type().op == Or_Op) { //it's ok to have Or_Op(aka assign_op) in the tuple_chain, just ignore it and continue to find target tuple_add
          I(chain_itr.inp_edges().size() == 1); //or as assign
          next_itr = chain_itr.get_sink_pin(0).get_driver_node();
          chain_itr = next_itr;
          continue;
        }

        if (chain_itr.setup_sink_pin(KN).is_connected() && is_tup_get_target(chain_itr, tup_get_target)) {
          auto value_dpin = chain_itr.setup_sink_pin(KV).get_driver_pin();
          if (value_dpin.get_node().get_type().op == TupGet_Op)
            value_dpin = tg2actual_dpin[value_dpin];
          else
            tg2actual_dpin[node.get_driver_pin()] = value_dpin;

          auto value_spin = node.get_driver_pin().out_edges().begin()->sink;
          dfg->add_edge(value_dpin, value_spin);
          break;
        }
        next_itr = chain_itr.setup_sink_pin(TN).get_driver_node();
        chain_itr = next_itr;
      }


    } else if (node.get_type().op == TupGet_Op and tuple_get_has_key_pos(node)) {
      collect_node_for_deleting (node, to_be_deleted);

      auto tup_get_target = node.get_sink_pin(KP).get_driver_node().get_type_const().to_i(); //FIXME->sh: need lgmem.prp
      auto chain_itr = node.get_sink_pin(TN).get_driver_node();

      while (chain_itr.get_type().op != TupRef_Op) {
        //this case occurs when the scalar variable is not yet turn into a tuple but some lhs try to access var[0] or var.0
        if (tup_get_target == 0 && chain_itr.get_type().op != TupAdd_Op && chain_itr.inp_edges().begin()->driver.get_node().get_type().op != TupAdd_Op ) {  
          I(chain_itr.get_type().op == Or_Op);
          auto value_dpin = chain_itr.setup_driver_pin(0);
          auto value_spin = node.get_driver_pin().out_edges().begin()->sink;
          dfg->add_edge(value_dpin, value_spin);
          break;
        }


        I(chain_itr.get_type().op == TupAdd_Op);
        if (chain_itr.setup_sink_pin(KP).is_connected() and is_tup_get_target(chain_itr, tup_get_target)) {
          auto value_dpin = chain_itr.setup_sink_pin(KV).get_driver_pin();
          if (value_dpin.get_node().get_type().op == TupGet_Op)
            value_dpin = tg2actual_dpin[value_dpin];
          else
            tg2actual_dpin[node.get_driver_pin()] = value_dpin;

          auto value_spin = node.get_driver_pin().out_edges().begin()->sink;
          dfg->add_edge(value_dpin, value_spin);
          break;
        }
        auto next_itr = chain_itr.setup_sink_pin(TN).get_driver_node();
        chain_itr = next_itr;
      }
    }
  }

  for (auto itr : to_be_deleted) {
    itr.del_node();
  }
}

bool Inou_lnast_dfg::tuple_get_has_key_name(const Node &tup_get) {
  return tup_get.get_sink_pin(KN).is_connected();
}

bool Inou_lnast_dfg::tuple_get_has_key_pos(const Node &tup_get) {
  return tup_get.get_sink_pin(KP).is_connected();
}

bool Inou_lnast_dfg::is_tup_get_target(const Node &tup_add, std::string_view tup_get_target) {
  auto tup_add_key_name = tup_add.get_sink_pin(KN).get_driver_pin().get_name();
  return (tup_add_key_name == tup_get_target);
}

bool Inou_lnast_dfg::is_tup_get_target(const Node &tup_add, uint32_t tup_get_target) {
  auto tup_add_key_pos = tup_add.get_sink_pin(KP).get_driver_node().get_type_const().to_i(); //FIXME->sh: need lgmem.prp
  return (tup_add_key_pos == tup_get_target);
}



void Inou_lnast_dfg::collect_node_for_deleting (const Node &node, absl::flat_hash_set<Node> &to_be_deleted ) {
  if (std::find(to_be_deleted.begin(), to_be_deleted.end(), node) == to_be_deleted.end())  
    to_be_deleted.insert(node);
}


void Inou_lnast_dfg::reconnect_to_ff_qpin(LGraph *dfg, const Node &tg_node) {
  //get the input edge
  auto tg_inp_driver_wname = tg_node.get_sink_pin(0).get_driver_pin().get_name();
  auto pos = tg_inp_driver_wname.find_last_of('_');
  auto target_ff_qpin_wname = std::string(tg_inp_driver_wname.substr(0, pos));
  auto target_ff_qpin = Node_pin::find_driver_pin(dfg, target_ff_qpin_wname);

  auto tg_out_sink = tg_node.get_driver_pin().out_edges().begin()->sink;
  dfg->add_edge(target_ff_qpin, tg_out_sink);
}



void Inou_lnast_dfg::assignment_or_elimination(Eprp_var &var) {
  Inou_lnast_dfg p(var);
  std::vector<LGraph *> lgs;

  for (const auto &l : var.lgs) {
    p.do_assignment_or_elimination(l);
  }
}


void Inou_lnast_dfg::do_assignment_or_elimination(LGraph *dfg) {
  for (auto node : dfg->fast()) {
    if (node.get_type().op != Or_Op)
      continue;

    bool is_or_as_assign = node.has_outputs() && node.inp_edges().size() == 1; //or as assign

    if (is_or_as_assign) {
      for (auto &out : node.out_edges()) {
        auto dpin = node.inp_edges().begin()->driver;
        if (!dpin.get_node().is_graph_input()) { // don't rename the graph inputs 
          /* dpin.set_name(node.get_driver_pin(1).get_name()); */
          dpin.set_name(node.get_driver_pin(0).get_name()); //or as assign
        }
        auto spin = out.sink;
        dfg->add_edge(dpin, spin);
      }
      node.del_node();
    }
  }
}



void Inou_lnast_dfg::dce(Eprp_var &var) {
  Inou_lnast_dfg p(var);
  std::vector<LGraph *> lgs;

  for (const auto &l : var.lgs) {
    p.do_dead_code_elimination(l);
  }
}



void Inou_lnast_dfg::do_dead_code_elimination(LGraph *dfg) {
  absl::flat_hash_set<Node> to_be_deleted;
  std::vector<Node> que;
  std::vector<Node> visited;

  for (auto node : dfg->fast()) {
    if (node.out_edges().size() != 0) 
      continue;

    if (std::find(visited.begin(), visited.end(), node) != visited.end()) 
      continue;
    
    //BFS approach
    que.emplace_back(node);
    while (!que.empty()) {
      auto n = que.back();
      que.pop_back();
      visited.emplace_back(n);
      collect_node_for_deleting(node, to_be_deleted);

      for (const auto &inp_edge : n.inp_edges()) {
        auto inp_dnode = inp_edge.driver.get_node();
        if (inp_dnode.out_edges().size() == 1 ) {
          que.emplace_back(inp_dnode);
        } else if (inp_dnode.get_type().op == Mux_Op) {
          //FIXME->sh: not necessary true, sometimes the mux selection pin is a constant bool, and never point to CompileErr_Op. We only knows it after copy-propagation. In this case, need to replace the mux node with an assignment Or_Op
          bool cond1 = inp_dnode.get_sink_pin("A").inp_edges().begin()->driver.get_node().get_type().op == CompileErr_Op;
          bool cond2 = inp_dnode.get_sink_pin("B").inp_edges().begin()->driver.get_node().get_type().op == CompileErr_Op;
          if((cond1 || cond2)) {
            que.emplace_back(inp_dnode);
          }
        }
      }
    }
  }

  for (auto itr : to_be_deleted)  
    itr.del_node();
}

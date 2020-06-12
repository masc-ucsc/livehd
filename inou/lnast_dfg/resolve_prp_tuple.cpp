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
  absl::flat_hash_set<Node::Compact> to_be_deleted;
  absl::flat_hash_map<Node_pin, Node_pin> tg2actual_dpin; //record tuple_get to its actual reference dpin
  for (const auto &node : dfg->fast()) {
    if (node.get_type().op == TupAdd_Op) {
      // I(node.get_sink_pin(KV).inp_edges().size() == 1); // not necessarily true, might get extra inp from TupGet
      to_be_deleted.insert(node.get_compact());
      continue;
    }

    if (node.get_type().op == TupGet_Op and tuple_get_has_key_name(node)) {
      to_be_deleted.insert(node.get_compact());
      auto tup_get_target = node.get_sink_pin(KN).inp_edges().begin()->driver.get_name();

      if (tup_get_target.substr(0,7) == "__q_pin") {
        reconnect_to_ff_qpin(dfg, node);
        continue;
      }

      auto chain_itr = node.get_sink_pin(TN).inp_edges().begin()->driver.get_node();
      while (chain_itr.get_type().op != TupRef_Op) {
        Node next_itr;
        if (chain_itr.get_type().op == Or_Op) { //it's ok to have Or_Op(aka assign_op) in the tuple_chain, just ignore it and continue to find target tuple_add
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
      to_be_deleted.insert(node.get_compact());
      auto tup_get_target = node.get_sink_pin(KP).get_driver_node().get_type_const().to_i(); //FIXME->sh: need lgmem.prp
      auto chain_itr = node.get_sink_pin(TN).get_driver_node();
      while (chain_itr.get_type().op != TupRef_Op) {
        I(chain_itr.get_type().op == TupAdd_Op);
        if (chain_itr.setup_sink_pin(KP).is_connected() and is_tup_get_target(chain_itr, tup_get_target)) {
          auto value_dpin = chain_itr.setup_sink_pin(KV).get_driver_pin();
          if (value_dpin.get_node().get_type().op == TupGet_Op)
            value_dpin = tg2actual_dpin[value_dpin];
          else
            tg2actual_dpin[node.get_driver_pin()] = value_dpin;

          // auto value_spin = node.get_sink_pin(TN).out_edges().begin()->sink;
          auto value_spin = node.get_driver_pin().out_edges().begin()->sink;
          dfg->add_edge(value_dpin, value_spin);
          break;
        }
        auto next_itr = chain_itr.setup_sink_pin(TN).get_driver_node();
        chain_itr = next_itr;
      }
    }
  }

  for (auto &itr : to_be_deleted) {
    itr.get_node(dfg).del_node();
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

void Inou_lnast_dfg::reconnect_to_ff_qpin(LGraph *dfg, const Node &tg_node) {
  //get the input edge
  auto tg_inp_driver_wname = tg_node.get_sink_pin(0).inp_edges().begin()->driver.get_name();
  auto pos = tg_inp_driver_wname.find_last_of('_');
  auto ori_size = tg_inp_driver_wname.size();
  // auto target_ff_qpin_wname = std::string(tg_inp_driver_wname.substr(0, ori_size-pos+1)) + "0";
  auto target_ff_qpin_wname = std::string(tg_inp_driver_wname.substr(0, pos));
  auto target_ff_qpin = Node_pin::find_driver_pin(dfg, target_ff_qpin_wname);
  fmt::print("target_ff_qpin wname:{}\n",target_ff_qpin_wname);
  fmt::print("target_ff_qpin:{}\n",target_ff_qpin.debug_name());

  auto tg_out_sink = tg_node.get_driver_pin().out_edges().begin()->sink;
  dfg->add_edge(target_ff_qpin, tg_out_sink);
}



void Inou_lnast_dfg::reduced_or_elimination(Eprp_var &var) {
  Inou_lnast_dfg p(var);
  std::vector<LGraph *> lgs;

  for (const auto &l : var.lgs) {
    p.do_reduced_or_elimination(l);
  }
}


void Inou_lnast_dfg::do_reduced_or_elimination(LGraph *dfg) {
  for (auto node : dfg->fast()) {
    if (node.get_type().op == Or_Op) {
      bool is_reduced_or = node.has_outputs() && node.out_edges().begin()->driver.get_pid() == 1;

      fmt::print("Or Node:{}\n", node.debug_name());
      if (is_reduced_or) {
        for (auto &out : node.out_edges()) {
          auto dpin = node.inp_edges().begin()->driver;
          if (!dpin.get_node().is_graph_input()) { // don't rename the graph inputs 
            dpin.set_name(node.get_driver_pin(1).get_name());
          }
          auto spin = out.sink;
          dfg->add_edge(dpin, spin);
        }

        node.del_node();
      }
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
  std::vector<Node> to_be_deleted;
  std::vector<Node> que;
  std::vector<Node> visited;

  for (auto node : dfg->fast()) {
    if (node.out_edges().size() == 0 && std::find(visited.begin(), visited.end(), node) == visited.end()) {
      //BFS approach
      que.emplace_back(node);
      while (!que.empty()) {
        auto n = que.back();
        que.pop_back();
        visited.emplace_back(n);
        to_be_deleted.emplace_back(n);
        for (const auto &inp_edge : n.inp_edges()) {
          if (inp_edge.driver.get_node().out_edges().size() == 1) {
            que.emplace_back(inp_edge.driver.get_node());
          }
        }
      }
    }
  }

  for (auto itr : to_be_deleted) {
    itr.del_node();
  }

}

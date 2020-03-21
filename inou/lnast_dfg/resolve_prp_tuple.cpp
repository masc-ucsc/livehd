// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
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
      I(node.get_sink_pin(TN).inp_edges().size() == 1);
      // I(node.get_sink_pin(3).inp_edges().size() == 1); //not necessarily true, might get extra inp from TupGet

      to_be_deleted.insert(node.get_compact());

      // handle special case: bits attribute
      if (is_bit_attr_tuple_add(node)) {
        //FIXME->sh: now I assume value pin is connected to constant node directly, but here is another copy propagation problem
        auto bits = node.get_sink_pin(KV).inp_edges().begin()->driver.get_node().get_type_const_value();
        auto target_dpin = Node_pin::find_driver_pin(dfg, node.get_driver_pin().get_name());
        target_dpin.ref_bitwidth()->e.set_ubits(bits);
      }
    } else if (node.get_type().op == TupGet_Op and tuple_get_has_key_name(node)) {
      to_be_deleted.insert(node.get_compact());
      auto tup_get_target = node.get_sink_pin(KN).inp_edges().begin()->driver.get_name();
      fmt::print("tup_get_target:{}\n", tup_get_target);
      auto chain_itr = node.get_sink_pin(TN).inp_edges().begin()->driver.get_node();
      while (chain_itr.get_type().op != TupRef_Op) {
        I(chain_itr.get_type().op == TupAdd_Op);
        if (chain_itr.setup_sink_pin(KN).is_connected() and is_tup_get_target(chain_itr, tup_get_target)) {
          auto value_dpin = chain_itr.setup_sink_pin(KV).inp_edges().begin()->driver;
          if (value_dpin.get_node().get_type().op == TupGet_Op)
            value_dpin = tg2actual_dpin[value_dpin];
          else
            tg2actual_dpin[node.get_driver_pin()] = value_dpin;
          fmt::print("driver info:{}\n", value_dpin.get_node().debug_name());

          // auto value_spin = node.get_sink_pin(TN).out_edges().begin()->sink;
          auto value_spin = node.get_driver_pin().out_edges().begin()->sink;
          fmt::print("sink info:{}\n", value_spin.get_node().debug_name());
          dfg->add_edge(value_dpin, value_spin);
          break;
        }
        auto next_itr = chain_itr.setup_sink_pin(TN).inp_edges().begin()->driver.get_node();
        chain_itr = next_itr;
      }

    } else if (node.get_type().op == TupGet_Op and tuple_get_has_key_pos(node)) {
      to_be_deleted.insert(node.get_compact());
      auto tup_get_target = node.get_sink_pin(KP).inp_edges().begin()->driver.get_node().get_type_const_value(); //FIXME->sh: need lgmem.prp
      fmt::print("tup_get_target:{}\n", tup_get_target);
      auto chain_itr = node.get_sink_pin(TN).inp_edges().begin()->driver.get_node();
      while (chain_itr.get_type().op != TupRef_Op) {
        I(chain_itr.get_type().op == TupAdd_Op);
        if (chain_itr.setup_sink_pin(KP).is_connected() and is_tup_get_target(chain_itr, tup_get_target)) {
          auto value_dpin = chain_itr.setup_sink_pin(KV).inp_edges().begin()->driver;
          if (value_dpin.get_node().get_type().op == TupGet_Op)
            value_dpin = tg2actual_dpin[value_dpin];
          else
            tg2actual_dpin[node.get_driver_pin()] = value_dpin;

          // auto value_spin = node.get_sink_pin(TN).out_edges().begin()->sink;
          auto value_spin = node.get_driver_pin().out_edges().begin()->sink;
          dfg->add_edge(value_dpin, value_spin);
          break;
        }
        auto next_itr = chain_itr.setup_sink_pin(TN).inp_edges().begin()->driver.get_node();
        chain_itr = next_itr;
      }
    }
  }

  for (auto &itr : to_be_deleted) {
    fmt::print("delete {}\n", itr.get_node(dfg).debug_name());
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
  auto tup_add_key_name = tup_add.get_sink_pin(KN).inp_edges().begin()->driver.get_name();
  return (tup_add_key_name == tup_get_target);
}

bool Inou_lnast_dfg::is_tup_get_target(const Node &tup_add, uint32_t tup_get_target) {
  auto tup_add_key_pos = tup_add.get_sink_pin(KP).inp_edges().begin()->driver.get_node().get_type_const_value(); //FIXME->sh: need lgmem.prp
  return (tup_add_key_pos == tup_get_target);
}

void Inou_lnast_dfg::reduced_or_elimination(Eprp_var &var) {
  Inou_lnast_dfg p(var);
  std::vector<LGraph *> lgs;

  for (const auto &l : var.lgs) {
    p.do_reduced_or_elimination(l);
  }
}


void Inou_lnast_dfg::do_reduced_or_elimination(LGraph *dfg) {
  absl::flat_hash_set<Node::Compact> to_be_deleted;
  for (const auto &node : dfg->fast()) {
    if (node.get_type().op == Or_Op) {
      bool is_reduced_or = node.out_edges().begin()->driver.get_pid() == 1;
      if (is_reduced_or) {
        I(node.inp_edges().size() == 1);
        for (auto &out : node.out_edges()) {
          auto dpin = node.inp_edges().begin()->driver;
          dpin.set_name(node.get_driver_pin(1).get_name());
          auto spin = out.sink;
          dfg->add_edge(dpin, spin);
        }
        to_be_deleted.insert(node.get_compact());
      }
    }
  }

  for (auto &itr : to_be_deleted) {
    fmt::print("delete {}\n", itr.get_node(dfg).debug_name());
    itr.get_node(dfg).del_node();
  }
}


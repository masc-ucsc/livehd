//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_cprop.hpp"

#include <string>

#include "lbench.hpp"
#include "lgcpp_plugin.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgtuple.hpp"

#define TRACE(x)
//#define TRACE(x) x

static Pass_plugin sample("pass_cprop", Pass_cprop::setup);

void Pass_cprop::setup() {
  Eprp_method m1("pass.cprop", "in-place copy propagation", &Pass_cprop::optimize);

  register_pass(m1);
}

Pass_cprop::Pass_cprop(const Eprp_var &var) : Pass("pass.cprop", var) {}

void Pass_cprop::optimize(Eprp_var &var) {
  Pass_cprop pass(var);

  for (auto &l : var.lgs) {
    pass.trans(l);
  }
}

void Pass_cprop::collapse_forward_same_op(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type().op;

	absl::flat_hash_map<Node_pin, int> repetitions;

  for (auto &out : node.out_edges()) {
    if (out.sink.get_node().get_type().op != op)
      continue;
    if (out.driver.get_pid() != out.sink.get_pid())
      continue;

    for (auto &inp : inp_edges_ordered) {
      TRACE(fmt::print("cprop same_op pin:{} to pin:{}\n", inp.driver.debug_name(), out.sink.debug_name()));
      auto it = repetitions.find(inp.driver);
			if (it == repetitions.end()) {
				repetitions[inp.driver] = 1;
				out.sink.connect_driver(inp.driver);
			} else {
				if (op == Xor_Op) {
					fmt::print("cprop simplified forward xor pin:{}\n",inp.driver.debug_name());
					out.sink.del_driver(inp.driver);
				} else if (op == Or_Op || op == And_Op) {
					fmt::print("cprop simplified forward or/and pin:{}\n",inp.driver.debug_name());
				} else {
					I(op != Sum_Op); // handled at collapse_forward_sum
					out.sink.connect_driver(inp.driver);
				}
			}
    }
    TRACE(fmt::print("cprop same_op del_edge pin:{} to pin:{}\n", out.driver.debug_name(), out.sink.debug_name()));
    out.del_edge();
  }
}

void Pass_cprop::collapse_forward_sum(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type().op;
  I(op == Sum_Op);
  bool all_edges_deleted = true;
  for (auto &out : node.out_edges()) {
    if (out.sink.get_node().get_type().op != Sum_Op) {
      all_edges_deleted = false;
      continue;
    }

    auto next_sum_node = out.sink.get_node();
    for (auto &inp : inp_edges_ordered) {
      TRACE(fmt::print("cprop same_op pin:{} to pin:{}\n", inp.driver.debug_name(), out.sink.debug_name()));
      auto next_sum_spin = next_sum_node.setup_sink_pin(inp.sink.get_pid());  // Connect same PID
      next_sum_spin.connect_driver(inp.driver);
    }
    TRACE(fmt::print("cprop same_op del_edge pin:{} to pin:{}\n", out.driver.debug_name(), out.sink.debug_name()));
    out.del_edge();
  }

  if (all_edges_deleted) {
    node.del_node();
  }
}

#if 0
void Pass_cprop::collapse_forward_shiftleft(Node &node) {
  I(node.get_type().op==ShiftLeft_Op);

  // a <<n | b -> join(a,b) if b.bits>=n
  for (auto &out : node.out_edges()) {
    auto sink_node = out.sink.get_node();
    if (sink_node.get_type().op != Or_Op) continue;

    if (!sink_node.get_driver_pin(0).has_outputs()) continue;

    HERE
  }
}
#endif

// Collase forward single node but only for pid!=0 (not reduction ops)
void Pass_cprop::collapse_forward_always_pin0(Node &node, XEdge_iterator &inp_edges_ordered) {
  bool can_delete = true;

  for (auto &out : node.out_edges()) {
    if (out.driver.get_pid()) {
      can_delete = false;
      continue;
    }

    for (auto &inp : inp_edges_ordered) {
      if (inp.sink.get_pid()) {
        can_delete = false;
        continue;
      }
      TRACE(fmt::print("cprop forward_always pin:{} to pin:{}\n", inp.driver.debug_name(), out.sink.debug_name()));
      out.sink.connect_driver(inp.driver);
    }
  }

  if (can_delete) {
    TRACE(fmt::print("cprop forward_always del_node node:{}\n", node.debug_name()));
    node.del_node();
  }
}

void Pass_cprop::collapse_forward_for_pin(Node &node, Node_pin &new_dpin) {
  for (auto &out : node.out_edges()) {
    new_dpin.connect_sink(out.sink);
  }

  node.del_node();
}

void Pass_cprop::try_constant_prop(Node &node, XEdge_iterator &inp_edges_ordered) {
  int n_inputs_constant = 0;
  int n_inputs          = 0;
  for (auto e : inp_edges_ordered) {
    n_inputs++;
    if (e.driver.get_node().is_type_const())
      n_inputs_constant++;
  }

  if (n_inputs == n_inputs_constant && n_inputs) {
    replace_all_inputs_const(node, inp_edges_ordered);
  } else if (n_inputs) {
    replace_part_inputs_const(node, inp_edges_ordered);
  }
}

void Pass_cprop::try_collapse_forward(Node &node, XEdge_iterator &inp_edges_ordered) {
  // No need to collapse things like const -> join because the Lconst will be forward eval

  auto op = node.get_type().op;

  if (inp_edges_ordered.size() == 1) {
    if (op == Sum_Op || op == Mult_Op || op == Div_Op || op == Mod_Op || op == Join_Op || op == And_Op || op == Or_Op
        || op == Xor_Op) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
      return;
    }
  }

  if (op == Sum_Op) {
    collapse_forward_sum(node, inp_edges_ordered);
  } else if (op == Mult_Op || op == And_Op || op == Or_Op || op == Xor_Op) {
    collapse_forward_same_op(node, inp_edges_ordered);
  } else if (op == Mux_Op) {
    // If all the options are the same. Collapse forward
    auto &a_pin = inp_edges_ordered[1].driver;
    for (auto i = 2u; i < inp_edges_ordered.size(); ++i) {
      if (a_pin != inp_edges_ordered[i].driver)
        return;
    }
    collapse_forward_for_pin(node, a_pin);
  }
}

void Pass_cprop::replace_part_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered) {
  auto op = node.get_type().op;
  if (op == Mux_Op) {
    auto s_node = inp_edges_ordered[0].driver.get_node();
    if (!s_node.is_type_const())
      return;

    I(s_node.get_type_const().is_i());  // string with ??? in mux? Give me test case to debug
    size_t sel = s_node.get_type_const().to_i();

    Node_pin a_pin;
    if ((sel + 1) >= inp_edges_ordered.size()) {
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
      a_pin = node.get_class_lgraph()->create_node_const(0).get_driver_pin();
    } else {
      a_pin = inp_edges_ordered[sel + 1].driver;
    }

    collapse_forward_for_pin(node, a_pin);
  } else if (op == Sum_Op) {
    int n_replace = 0;
    for (auto &i : inp_edges_ordered) {
      if (i.driver.get_node().is_type_const())
        n_replace++;
    }

    if (n_replace > 1) {
      Lconst result;
      for (auto &i : inp_edges_ordered) {
        if (!i.driver.get_node().is_type_const())
          continue;

        auto c = i.driver.get_node().get_type_const();
        if (i.sink.get_pid() == 0 || i.sink.get_pid() == 1) {
          result = result + c;
        } else {
          result = result - c;
        }
        i.del_edge();
      }
      auto new_node = node.get_class_lgraph()->create_node_const(result);
      auto dpin     = new_node.get_driver_pin();
      if (result < 0) {
        node.setup_sink_pin(1).connect_driver(dpin);  // signed pin
      } else {
        node.setup_sink_pin(0).connect_driver(dpin);  // unsigned pin
      }
    }
  }
}

void Pass_cprop::replace_all_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered) {
  // simple constant propagation
  auto op = node.get_type().op;
  if (op == Join_Op) {
    Lconst result;

    for (auto rit = inp_edges_ordered.rbegin(); rit != inp_edges_ordered.rend(); ++rit) {
      auto &i = *rit;

      result = result << i.driver.get_bits();
      auto c = i.driver.get_node().get_type_const();
      I(c.get_bits() <= i.driver.get_bits());
      result = result | c;
    }
    result = result.adjust_bits(node.get_driver_pin().get_bits());

    TRACE(fmt::print("cprop: join to {}\n", result.to_pyrope()));

    replace_node(node, result);

  } else if (op == ShiftLeft_Op) {
    Lconst val = node.get_sink_pin("A").get_driver_node().get_type_const();
    Lconst amt = node.get_sink_pin("B").get_driver_node().get_type_const();

    Lconst result = val << amt;

    TRACE(fmt::print("cprop: shl to {} ({}<<{})\n", result.to_pyrope(), val.to_pyrope(), amt.to_pyrope()));

    replace_node(node, result);
  } else if (op == Sum_Op) {
    Lconst result;
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (i.sink.get_pid() == 0 || i.sink.get_pid() == 1) {
        result = result + c;
      } else {
        result = result - c;
      }
    }

    TRACE(fmt::print("cprop: add node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  } else if (op == Or_Op) {
    Bits_t max_bits = 0;
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (c.get_bits() > max_bits)
        max_bits = c.get_bits();
    }
    Lconst result(0);
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      result = result.or_op(c.adjust_bits(max_bits));
    }

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    Lconst result_reduced = result == 0 ? 0 : 1;

    replace_logic_node(node, result, result_reduced);

  } else if (op == And_Op) {
    Bits_t max_bits = 0;
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (c.get_bits() > max_bits)
        max_bits = c.get_bits();
    }
    Lconst result("-1s");
    for (auto &i : inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      result = result.and_op(c.adjust_bits(max_bits));
    }

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    Lconst result_reduced = result == Lconst("-1s") ? 1 : 0;

    replace_logic_node(node, result, result_reduced);

  } else if (op == Equals_Op) {
    bool eq = true;
    I(inp_edges_ordered.size() > 1);
    auto first = inp_edges_ordered[0].driver.get_node().get_type_const();
    for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
      auto c = inp_edges_ordered[i].driver.get_node().get_type_const();
      eq     = eq && first.eq_op(c);
    }

    Lconst result(eq ? 1 : 0);

    TRACE(fmt::print("cprop: eq node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  } else if (op == Mux_Op) {
    auto sel_const = inp_edges_ordered[0].driver.get_node().get_type_const();
    I(sel_const.is_i());  // string with ??? in mux? Give me test case to debug

    size_t sel = sel_const.to_i();

    Lconst result;
    if ((sel + 1) >= inp_edges_ordered.size()) {
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
    } else {
      result = inp_edges_ordered[sel + 1].driver.get_node().get_type_const();
    }

    replace_node(node, result);
  } else {
    fmt::print("FIXME: cprop still does not copy prop node:{}\n", node.debug_name());
  }
}

void Pass_cprop::replace_node(Node &node, const Lconst &result) {
  auto new_node = node.get_class_lgraph()->create_node_const(result);
  auto dpin     = new_node.get_driver_pin();

  for (auto &out : node.out_edges()) {
    if (dpin.get_bits() == out.driver.get_bits() || out.driver.get_bits() == 0) {
      TRACE(fmt::print("cprop: const:{} to out.driver:{}\n", result.to_pyrope(), out.driver.debug_name()));
      dpin.connect_sink(out.sink);
    } else {
      // create new const node to preserve bits
      auto result2 = result.adjust_bits(out.driver.get_bits());

      auto dpin2 = node.get_class_lgraph()->create_node_const(result2).get_driver_pin();

      TRACE(fmt::print("creating const:{} {}bits {}  from const:{} {}bits\n",
                       result2.to_pyrope(),
                       out.driver.get_bits(),
                       dpin2.get_bits(),
                       result.to_pyrope(),
                       dpin.get_bits()));

      dpin2.connect_sink(out.sink);
    }

    // out.del_edge();
  }

  node.del_node();
}

void Pass_cprop::replace_logic_node(Node &node, const Lconst &result, const Lconst &result_reduced) {
  Node_pin dpin_0;
  Node_pin dpin_1;

  for (auto &out : node.out_edges()) {
    if (out.driver.get_pid()) {
      // Reduction
      if (dpin_1.is_invalid()) {
        dpin_1 = node.get_class_lgraph()->create_node_const(result_reduced).get_driver_pin();
      }
      dpin_1.connect_sink(out.sink);
    } else {
      // bitwise op
      if (dpin_0.is_invalid()) {
        dpin_0 = node.get_class_lgraph()->create_node_const(result).get_driver_pin();
      }
      dpin_0.connect_sink(out.sink);
    }
  }

  node.del_node();
}

void Pass_cprop::process_subgraph(Node &node) {
  if (node.is_type_sub_present())
    return;

  auto *sub = node.ref_type_sub_node();

  const auto &reg = Lgcpp_plugin::get_registry();

  auto it = reg.find(sub->get_name());
  if (it == reg.end())
    return;

  fmt::print("cprop subgraph:{} is not present, found lgcpp...\n", sub->get_name());

  std::shared_ptr<Lgtuple> inp;
  std::shared_ptr<Lgtuple> out = std::make_shared<Lgtuple>();
  it->second(node.get_class_lgraph(), inp, out);

  fmt::print("cprop subgraph:{} has out\n", sub->get_name());
  out->dump("  ");

  for (auto dpin : node.out_connected_pins()) {
    fmt::print("dpin:{} pid:{} testing...\n", dpin.debug_name(), dpin.get_pid());
    if (dpin.has_name()) {
      if (out->has_key_name(dpin.get_name())) {
        fmt::print("replace dpin:{}\n", dpin.get_name());
      } else {
        fmt::print("dpin:{} disconnected. name Remove\n", dpin.get_name());
      }
    } else {
      if (out->has_key_pos(dpin.get_pid())) {
        fmt::print("replace dpin:{} pid:{}\n", dpin.debug_name(), dpin.get_pid());
      } else {
        fmt::print("dpin:{} disconnected. pos Remove\n", dpin.debug_name());
      }
    }
  }

#if 1
  Port_ID instance_pid = 0;
  for (const auto &io_pin : sub->get_io_pins()) {
    instance_pid++;
    if (io_pin.is_input())
      continue;
    if (out->has_key_name(io_pin.name)) {
      fmt::print("replace io_pin:{}\n", io_pin.name);
    } else {
      fmt::print("disconnected io_pin:{}\n", io_pin.name);
    }

    fmt::print("iopin:{} pos:{} instance_pid:{}...\n", io_pin.name, io_pin.graph_io_pos, instance_pid);
  }
#endif
}

void Pass_cprop::process_attr_q_pin(Node &node, Node_pin &parent_dpin) {
  // Get variable name
  auto driver_wname = parent_dpin.get_name();

  // remove the SSA from name
  auto pos   = driver_wname.find_last_of('_');
  auto wname = driver_wname.substr(0, pos);

  // Find flop
  auto target_ff_qpin = Node_pin::find_driver_pin(node.get_class_lgraph(), wname);

  collapse_forward_for_pin(node, target_ff_qpin);
}


bool Pass_cprop::process_attr_get(Node &node) {
  I(node.get_type_op() == AttrGet_Op);

  if (!node.has_sink_pin_connected(0) || !node.has_sink_pin_connected(1))
    return false;

  // Either pos or name
  auto parent_dpin   = node.get_sink_pin(0).get_driver_pin();
  auto key_name_dpin = node.get_sink_pin(1).get_driver_pin();

  I(key_name_dpin.get_node().get_type().op == TupKey_Op);
  I(key_name_dpin.has_name());
  auto key_name = key_name_dpin.get_name();
  if (key_name.substr(0, 2) == "__") {
    if (key_name.substr(0, 7) == "__q_pin") {
      fmt::print("process_attr_q_pin parent_dpin:{} node:{}\n", parent_dpin.debug_name(), node.debug_name());
      process_attr_q_pin(node, parent_dpin);
      return true;
    }
  }
  return false;
}

std::tuple<std::string_view, int> Pass_cprop::get_tuple_name_key(Node &node) {
  std::string_view key_name;
  int key_pos = -1;
  if (node.has_sink_pin_connected(1)) {
    auto node2 = node.get_sink_pin(1).get_driver_node();
    if (node2.get_type_op() == TupKey_Op) 
      key_name = node2.get_driver_pin().get_name();
  }

  if (node.has_sink_pin_connected(2)) {
    auto node2 = node.get_sink_pin(2).get_driver_node();
    if (node2.is_type_const()) 
      key_pos = node2.get_type_const().to_i();
  }

  // I(!key_name.empty() || key_pos != -1);  // At least one defined // FIXME->sh: not necessarily true, could be resolved at later
  // TupAdd merge step

  return std::make_tuple(key_name, key_pos);
}


bool Pass_cprop::process_tuple_get(Node &tg_node, LGraph *lg) {
  I(tg_node.get_type_op() == TupGet_Op);

  auto parent_dpin = tg_node.get_sink_pin(0).get_driver_pin();
  auto parent_node = parent_dpin.get_node();

  auto ptup_it             = node2tuple.find(parent_node.get_compact());
  auto [key_name, key_pos] = get_tuple_name_key(tg_node);

  // special case when TG try to get a scalar variable by accessing pos 0
  if (parent_node.get_type_op() != TupAdd_Op && key_pos == 0 && !parent_dpin.is_invalid()) {
    collapse_forward_for_pin(tg_node, parent_dpin);
    return true;
  }

  std::string tup_name;
  if (parent_dpin.has_name()) {
    tup_name = parent_dpin.get_name();
  } else {
    tup_name = tg_node.debug_name();
  }

  if (ptup_it == node2tuple.end()) {  // ptup_it = parent_node
    std::string key;
    if (key_name.empty())
      key = std::to_string(key_pos);
    else
      key = key_name;

    Pass::error("for tuple_get {} parent_node {}, there is no tuple of {}, so no valid field {}\n",
                tg_node.debug_name(),
                parent_node.debug_name(),
                tup_name,
                key);
    return false;
  }

  
  Node_pin val_dpin;
  auto &ctup = ptup_it->second;

  if (!key_name.empty()) {
    if (ctup->has_key_name(key_name)) {
      val_dpin = ctup->get_value_dpin(key_pos, key_name);
    /*// FIXME->sh*/
    /* } else if (tg_node.out_edges()[0].sink.get_node().get_type_op() == Mux_Op) { */
    /*   I(tg_node.get_driver_pin().out_edges().size() == 1); */
    /*   // this is the case of tuple-if, where TG try to get upper scope tuple field but the target is not there. */
    /*   // keep this TG and wait cprop to try to resolve the mux at compile time. */ 
    /*   // If success, maybe never choose the TG path */
    /*   return false; */
    } else {
      ctup->dump();
      fmt::print("tg_node:{}\n", tg_node.debug_name());
      Pass::error("tuple {} does not have field key {}\n", tup_name, key_name);
      return false;
    }
  } else if (key_pos >= 0) {
    auto &ctup = ptup_it->second;
    if (ctup->has_key_pos(key_pos)) {
      val_dpin = ctup->get_value_dpin(key_pos, key_name);
    } else {
      ctup->dump();
      Pass::error("tuple {} does not have field pos {}\n", tup_name, key_pos);
      return false;
    }
  }


  // case of accessing __bits from a hier-tg-chain, ex: x = foo.a.b.__bits
  bool followed_by_a_tg = false;
  Node follower_tg_node;
  for (auto &out : tg_node.out_edges()) {
    const auto &sink_node = out.sink.get_node();
    if (sink_node.get_type_op() == TupGet_Op) {
      followed_by_a_tg = true;
      follower_tg_node = sink_node;
      break;
    }
  }

  if (followed_by_a_tg) {
    bool follower_want_attr = false;
    auto follower_key_name = follower_tg_node.setup_sink_pin(1).get_driver_pin().get_name();
    if (follower_key_name.substr(0,6) == "__bits") // extend to other attribute
      follower_want_attr = true;
    
    // case of accessing a attribute field in hier tg chain
    if (follower_want_attr) {
      if (val_dpin.get_node().get_type_op() == TupAdd_Op) {
        Pass::error("access __bits attribute from a tuple chain. Follower tg:{}, current_tg:{}\n", follower_tg_node.debug_name(), tg_node.debug_name());
        return false;
      }  
      
      //construct attr-set/get sub-struct
      auto attr_set_node  = lg->create_node(AttrSet_Op);      
      auto attr_set_an_spin = attr_set_node.setup_sink_pin(1);
      auto attr_set_an_dpin = lg->create_node(TupKey_Op).setup_driver_pin();
      auto attr_set_av_spin  = attr_set_node.setup_sink_pin(2);
      attr_set_an_dpin.set_name(follower_key_name);
      attr_set_an_dpin.connect_sink(attr_set_an_spin);


      // search in lgraph chain to get where __bits value defined. note: you cannot avoid this 
      // iteration even with lgtuple, but theoretically, the __bits node won't be defined too far
      // FIXME->sh: instead of iterate lgraph, try to leverage the new key2bits table in Lgtuple
      Node_pin attr_set_av_dpin;
      auto gp_node = parent_node.setup_sink_pin(0).get_driver_node(); // grand_parent_node
      while (gp_node.get_type_op()== TupAdd_Op) {
        auto gp_node_key_dpin = gp_node.setup_sink_pin(1).get_driver_pin();
        if (gp_node_key_dpin.has_name() && gp_node_key_dpin.get_name() == key_name) {
          auto node2 = gp_node.setup_sink_pin(3).get_driver_node();
          auto node2_key_dpin = node2.setup_sink_pin(1).get_driver_pin();
          if (node2_key_dpin.get_name() != follower_key_name) {
            Pass::error("trying to access scalar attribute from a tuple\n");
          }
          attr_set_av_dpin = node2.setup_sink_pin(3).get_driver_pin();
          node2.del_node();
          break;
        }
      }
      attr_set_av_dpin.connect_sink(attr_set_av_spin);

      auto attr_get_node = lg->create_node(AttrGet_Op);      
      auto attr_get_vn_spin = attr_get_node.setup_sink_pin(0);
      auto attr_get_vn_dpin = attr_set_node.setup_driver_pin(0);
      attr_get_vn_dpin.connect_sink(attr_get_vn_spin);

      auto attr_get_an_spin = attr_get_node.setup_sink_pin(1);
      auto attr_get_an_dpin = lg->create_node(TupKey_Op).setup_driver_pin();
      attr_get_an_dpin.set_name(follower_key_name);
      attr_get_an_dpin.connect_sink(attr_get_an_spin);

      auto attr_get_dpin = attr_get_node.setup_driver_pin();
      collapse_forward_for_pin(follower_tg_node, attr_get_dpin);

      tg_node.del_node();
      return true;
    }
  }

  // case of accessing a normal field in both hier/non-hier tg chain
  if (!val_dpin.is_invalid()) {
    for (auto &e : tg_node.out_edges()) {
      if (val_dpin.get_node() == e.sink.get_node()) {
        Pass::error("tuple {} assignment loop detected", tup_name);
        return false;
      }
    }


    auto attr_bits = ctup->get_bits_from_key(key_name);
    if (val_dpin.get_node().get_type_op() != TupAdd_Op && attr_bits != -1) { // is scalar and bits has been set
    // when access scalar, check if corresponding attr is set -> yes -> insert attr_set in between
      
      auto attr_set_node  = lg->create_node(AttrSet_Op);      
      auto attr_set_node_dpin = attr_set_node.setup_driver_pin(0);

      auto attr_set_vn_spin = attr_set_node.setup_sink_pin(0);
      auto attr_set_vn_dpin = val_dpin;
      attr_set_vn_dpin.connect_sink(attr_set_vn_spin);


      auto attr_set_an_spin = attr_set_node.setup_sink_pin(1);
      auto attr_set_an_dpin = lg->create_node(TupKey_Op).setup_driver_pin();
      attr_set_an_dpin.set_name("__bits");
      attr_set_an_dpin.connect_sink(attr_set_an_spin);
      
      auto attr_set_av_spin  = attr_set_node.setup_sink_pin(2);
      auto attr_set_av_dpin  = lg->create_node_const(attr_bits).setup_driver_pin();
      attr_set_av_dpin.connect_sink(attr_set_av_spin);

      collapse_forward_for_pin(tg_node, attr_set_node_dpin);
    } else {
      fmt::print("TupGet node:{} pos:{} key:{} val:{}\n", tg_node.debug_name(), key_pos, key_name, val_dpin.debug_name());
      collapse_forward_for_pin(tg_node, val_dpin);
      return true;
    }  
  }
  return false;
}


void Pass_cprop::process_tuple_add(Node &node, LGraph *lg) {
  I(node.get_type_op() == TupAdd_Op);

  auto parent_dpin = node.get_sink_pin(0).get_driver_pin();
  auto parent_node = parent_dpin.get_node();

  bool                     parent_could_be_deleted = false;
  std::shared_ptr<Lgtuple> ctup;


  auto ptup_it = node2tuple.find(parent_node.get_compact());
  if (ptup_it == node2tuple.end()) {
    ctup = std::make_shared<Lgtuple>();
  } else {
    auto parent_out_edges   = parent_dpin.out_edges();
    parent_could_be_deleted = parent_out_edges.size() == 1;  // I'm the only one child

    if (!parent_could_be_deleted) {
      // if all other parent out edges are tup_get and can be resolved, this parent can still be deleted
      bool loop_exit = false;
      for (auto e : parent_out_edges) {
        auto dest_node = e.sink.get_node();
        if (dest_node == node)
          continue;  // this node

        if (dest_node.get_type().op == TupGet_Op) {
          // WARNING: no testing case, but it should work
          bool deleted = process_tuple_get(dest_node, lg);
          if (deleted)
            continue;
        }

        loop_exit = true;  // as long as one of the sink node is not TG or the TG cannot be resolved, this parent cannot be removed.
        break;
      }

      if (!loop_exit)
        parent_could_be_deleted = true;
    }

    if (parent_could_be_deleted) {
      ctup = ptup_it->second;
      node2tuple.erase(ptup_it);
    } else {
      ctup = std::make_shared<Lgtuple>(*(ptup_it->second));
    }
  }

  auto [key_name, key_pos] = get_tuple_name_key(node);

  // not all tuple_add has value pin connected, for example, the __last_value TA doesn't have 
  Node_pin val_dpin;
  if (node.has_sink_pin_connected(3))
    val_dpin = node.get_sink_pin(3).get_driver_pin();


  bool is_attr_set = false;
  int  attr_bits = -1;
  if (!val_dpin.is_invalid()) {
    auto val_dnode = val_dpin.get_node();
    if (val_dnode.get_type_op() == TupAdd_Op && 
        val_dnode.has_sink_pin_connected(1)  && 
        val_dnode.setup_sink_pin(1).get_driver_pin().get_name().substr(0,6) == "__bits") {

      is_attr_set = true;
      I(val_dnode.setup_sink_pin(3).get_driver_node().is_type_const());
      attr_bits = val_dnode.setup_sink_pin(3).get_driver_node().get_type_const().to_i();
    }
  }

  merge_to_tuple(ctup, node, parent_node, parent_dpin, key_pos, key_name, val_dpin, is_attr_set, attr_bits);

  fmt::print("TupAdd node:{} pos:{} key:{} val:{}\n", node.debug_name(), key_pos, key_name, val_dpin.debug_name());
  /* } */

  if (parent_could_be_deleted)
    parent_node.del_node();
}


void Pass_cprop::merge_to_tuple(std::shared_ptr<Lgtuple> ctup, Node &node, Node &parent_node, Node_pin &parent_dpin, int key_pos,
                                std::string_view key_name, Node_pin &val_dpin, bool is_attr_set, int attr_bits) {
  bool compile_error = false;

  if (is_attr_set) {
    ctup->set_key2bits(key_name, attr_bits);
    node2tuple[node.get_compact()] = ctup; //notice in this case, ctup = parent tup
    return;
  }
  

  if (parent_node.get_type().op == TupRef_Op) {
    // First tuple
    bool ok = ctup->set(key_pos, key_name, val_dpin);
    if (!ok)
      compile_error = true;
  } else {
    if (parent_node.get_type().op != TupAdd_Op) {
      std::string unnamed;
      bool        ok = ctup->set(0, unnamed, parent_dpin);  // includes the parent into Lgtuple where the parent is not TupAdd
      if (!ok)
        compile_error = true;
    }

    bool is_connected_has_name = node.has_sink_pin_connected(1) && node.get_sink_pin(1).get_driver_pin().has_name();
    if (is_connected_has_name && node.get_sink_pin(1).get_driver_pin().get_name() == "__last_value") {
      I(node.get_type().op == TupAdd_Op);
      node2tuple[node.get_compact()] = ctup;
      return;  // for the __last_value tuple_add, no new tuple_chain element need to add, just inherit it's parent Lgtuple, i.e. ctup
    }

    if (key_pos < 0 && key_name.empty()) {
      if (val_dpin.get_node().get_type().op == TupAdd_Op) {  // hier-tuple
        auto it2 = node2tuple.find(val_dpin.get_node().get_compact());
        I(it2 != node2tuple.end());
        bool ok = ctup->add(it2->second);
        if (!ok) {
          compile_error = true;
          ctup->dump();
          it2->second->dump();
          Pass::error("tuples {} and {} can not be merged\n", "XX", "XX");
        }
      } else {
        ctup->add(val_dpin);
      }
    } else {
      bool ok = ctup->set(key_pos, key_name, val_dpin);
      if (!ok)
        compile_error = true;
    }
  }

  if (compile_error)
    Pass::error("tuples {} could not add field \n", "XX", "XX");

  node2tuple[node.get_compact()] = ctup;
}


void Pass_cprop::trans(LGraph *lg) {
  Lbench b("pass.cprop");

  for (auto node : lg->forward()) {
    auto op = node.get_type().op;
    // fmt::print("NEXT: node:{}\n",node.debug_name());

    // Special cases to handle in cprop
    if (op == AttrGet_Op) {
      process_attr_get(node);
      continue;
    } else if (op == AttrSet_Op) {
      continue;  // Nothing to do in cprop
    } else if (op == SubGraph_Op) {
      process_subgraph(node);
      continue;
    } else if (op == SFlop_Op || op == AFlop_Op || op == Latch_Op || op == FFlop_Op || op == Memory_Op || op == SubGraph_Op) {
      fmt::print("cprop skipping node:{}\n", node.debug_name());
      // FIXME: if flop feeds itself (no update, delete, replace for zero)
      // FIXME: if flop is disconnected *after AttrGet processed*, the flop was not used. Delete
      continue;
    } else if (!node.has_outputs()) {
      fmt::print("cprop deleting node:{}\n", node.debug_name());
      node.del_node();
      continue;
    } else if (op == TupAdd_Op) {
      process_tuple_add(node, lg);
      continue;
    } else if (op == TupGet_Op) {
      process_tuple_get(node, lg);
      continue;
    }

    // Normal copy prop and strength reduction
    auto inp_edges_ordered = node.inp_edges_ordered();
    try_constant_prop(node, inp_edges_ordered);

    if (node.is_invalid())
      continue;  // It got deleted

    if (inp_edges_ordered.size() > 64) {
#ifndef NDEBUG
      fmt::print("node:{} is already quite large. Skipping cprop\n", node.debug_name());
#endif
      continue;
    }
    // fmt::print("node:{} inp:{} out:{}\n",node.debug_name(), node.get_num_inputs(), node.get_num_outputs());

    try_collapse_forward(node, inp_edges_ordered);
  }

  for (auto node : lg->fast()) {
    if (!node.has_outputs()) {
      if (!node.is_type_sub() && !node.is_type_attr())
        node.del_node();
      continue;
    }
  }
}

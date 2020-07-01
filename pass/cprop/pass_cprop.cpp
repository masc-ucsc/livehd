//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_cprop.hpp"

#include <string>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgtuple.hpp"
#include "lgcpp_plugin.hpp"

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
  for (auto &out : node.out_edges()) {
    if (out.sink.get_node().get_type().op!=op)
      continue;
    if (out.driver.get_pid() != out.sink.get_pid())
      continue;

    for (auto &inp : inp_edges_ordered) {
      TRACE(fmt::print("cprop same_op pin:{} to pin:{}\n",inp.driver.debug_name(), out.sink.debug_name()));
      out.sink.connect_driver(inp.driver);
    }
    TRACE(fmt::print("cprop same_op del_edge pin:{} to pin:{}\n",out.driver.debug_name(), out.sink.debug_name()));
    out.del_edge();
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
      TRACE(fmt::print("cprop forward_always pin:{} to pin:{}\n",inp.driver.debug_name(), out.sink.debug_name()));
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

void Pass_cprop::try_collapse_forward(Node &node, XEdge_iterator &inp_edges_ordered) {

  // No need to collapse things like const -> join because the Lconst will be forward eval

  auto op = node.get_type().op;

  if (inp_edges_ordered.size() == 1) {
    if (op == Sum_Op || op == Mult_Op || op == Div_Op || op == Mod_Op || op == Join_Op || op == And_Op || op == Or_Op ||
        op == Xor_Op) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
      return;
    }
  }

  if (op == Sum_Op || op == Mult_Op) {
    collapse_forward_same_op(node, inp_edges_ordered);
  } else if (op == And_Op || op == Or_Op || op == Xor_Op) {
    collapse_forward_same_op(node, inp_edges_ordered);
  } else if (op == Mux_Op) {
    // If all the options are the same. Collapse forward
    auto &a_pin = inp_edges_ordered[1].driver;
    for(auto i=2u;i<inp_edges_ordered.size();++i) {
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

    I(s_node.get_type_const().is_i()); // string with ??? in mux? Give me test case to debug
    size_t sel = s_node.get_type_const().to_i();

    Node_pin a_pin;
    if ((sel+1) >= inp_edges_ordered.size()) {
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
      a_pin = node.get_class_lgraph()->create_node_const(0).get_driver_pin();
    } else {
      a_pin = inp_edges_ordered[sel+1].driver;
    }

    collapse_forward_for_pin(node, a_pin);
  }
}

void Pass_cprop::replace_all_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered) {

  // simple constant propagation
  auto op = node.get_type().op;
  if (op == Join_Op) {
    Lconst result;

    for (auto rit = inp_edges_ordered.rbegin(); rit!= inp_edges_ordered.rend(); ++rit) {
      auto &i = *rit;

      result = result << i.driver.get_bits();
      auto c = i.driver.get_node().get_type_const();
      I(c.get_bits()<=i.driver.get_bits());
      result  = result | c;
    }
    result = result.adjust_bits(node.get_driver_pin().get_bits());

    TRACE(fmt::print("cprop: join to {}\n", result.to_pyrope()));

    replace_node(node, result);

  } else if (op == ShiftLeft_Op) {

    Lconst val = node.get_sink_pin("A").get_driver_node().get_type_const();
    Lconst amt = node.get_sink_pin("B").get_driver_node().get_type_const();

    Lconst result = val<<amt;

    TRACE(fmt::print("cprop: shl to {} ({}<<{})\n", result.to_pyrope(), val.to_pyrope(), amt.to_pyrope()));

    replace_node(node, result);
  } else if (op == Sum_Op) {

    Lconst result;
    for(auto &i:inp_edges_ordered) {
      auto c = i.driver.get_node().get_type_const();
      if (i.sink.get_pid()==0 || i.sink.get_pid()==1) {
        result = result + c;
      } else {
        result = result - c;
      }
    }

    TRACE(fmt::print("cprop: add node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  } else if (op == Or_Op) {
		uint16_t max_bits = 0;
		for(auto &i:inp_edges_ordered) {
			auto c = i.driver.get_node().get_type_const();
			if (c.get_bits() > max_bits)
				max_bits = c.get_bits();
		}
		Lconst result(0);
		for(auto &i:inp_edges_ordered) {
			auto c = i.driver.get_node().get_type_const();
			result = result.or_op(c.adjust_bits(max_bits));
		}

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

		Lconst result_reduced = result==0?0:1;

    replace_logic_node(node, result, result_reduced);

  } else if (op == And_Op) {
		uint16_t max_bits = 0;
		for(auto &i:inp_edges_ordered) {
			auto c = i.driver.get_node().get_type_const();
			if (c.get_bits() > max_bits)
				max_bits = c.get_bits();
		}
		Lconst result("-1s");
		for(auto &i:inp_edges_ordered) {
			auto c = i.driver.get_node().get_type_const();
			result = result.and_op(c.adjust_bits(max_bits));
		}

    TRACE(fmt::print("cprop: and node:{} to {}\n", node.debug_name(), result.to_pyrope()));

		Lconst result_reduced = result==Lconst("-1s")?1:0;

    replace_logic_node(node, result, result_reduced);

  } else if (op == Equals_Op) {
    bool eq=true;
    I(inp_edges_ordered.size()>1);
    auto first = inp_edges_ordered[0].driver.get_node().get_type_const();
    for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
      auto c = inp_edges_ordered[i].driver.get_node().get_type_const();
      eq     = eq && first.equals_op(c);
    }

    Lconst result(eq?1:0);

    TRACE(fmt::print("cprop: eq node:{} to {}\n", node.debug_name(), result.to_pyrope()));

    replace_node(node, result);
  } else if (op == Mux_Op) {
    auto sel_const = inp_edges_ordered[0].driver.get_node().get_type_const();
    I(sel_const.is_i()); // string with ??? in mux? Give me test case to debug

    size_t sel = sel_const.to_i();

    Lconst result;
    if ((sel+1) >= inp_edges_ordered.size()) {
      fmt::print("WARNING: mux selector:{} goes over limit:{} in mux. Using zero\n", sel, inp_edges_ordered.size());
    } else {
      result = inp_edges_ordered[sel + 1].driver.get_node().get_type_const();
    }

    replace_node(node, result);
  }
}

void Pass_cprop::replace_node(Node &node, const Lconst &result) {

  auto new_node = node.get_class_lgraph()->create_node_const(result);
  auto dpin     = new_node.get_driver_pin();

  for(auto &out:node.out_edges()) {
    if (dpin.get_bits() == out.driver.get_bits() || out.driver.get_bits()==0) {
      TRACE(fmt::print("cprop: const:{} to out.driver:{}\n", result.to_pyrope(), out.driver.debug_name()));
      dpin.connect_sink(out.sink);
    } else {
      // create new const node to preserve bits
      auto result2 = result.adjust_bits(out.driver.get_bits());

      auto dpin2   = node.get_class_lgraph()->create_node_const(result2).get_driver_pin();

      TRACE(fmt::print("creating const:{} {}bits {}  from const:{} {}bits\n"
          , result2.to_pyrope(), out.driver.get_bits(), dpin2.get_bits()
          , result.to_pyrope() , dpin.get_bits()));

      dpin2.connect_sink(out.sink);
    }

    //out.del_edge();
  }

  node.del_node();
}

void Pass_cprop::replace_logic_node(Node &node, const Lconst &result, const Lconst &result_reduced) {

	Node_pin dpin_0;
	Node_pin dpin_1;

  for(auto &out:node.out_edges()) {
		if (out.driver.get_pid()) {
			// Reduction
			if (dpin_1.is_invalid()) {
				dpin_1   = node.get_class_lgraph()->create_node_const(result_reduced).get_driver_pin();
			}
			dpin_1.connect_sink(out.sink);
		} else {
			// bitwise op
			if (dpin_0.is_invalid()) {
				dpin_0   = node.get_class_lgraph()->create_node_const(result).get_driver_pin();
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
  auto io_pins = sub->get_io_pins();
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

void Pass_cprop::process_tuple_q_pin(Node &node, Node_pin &parent_dpin) {
	// Get variable name
  auto driver_wname   = parent_dpin.get_name();

  // remove the SSA from name
  auto pos   = driver_wname.find_last_of('_');
	auto wname = driver_wname.substr(0, pos);

	// Find flop
  auto target_ff_qpin = Node_pin::find_driver_pin(node.get_class_lgraph(), wname);

  collapse_forward_for_pin(node, target_ff_qpin);
}

void Pass_cprop::merge_to_tuple(std::shared_ptr<Lgtuple> ctup, Node &node, Node
		&parent_node, Node_pin &parent_dpin, int key_pos, std::string_view
		key_name, Node_pin &val_dpin) {

	bool compile_error = false;

	if (parent_node.get_type().op == TupRef_Op) {
		// First tuple
		bool ok = ctup->set(key_pos, key_name, val_dpin);
		if (!ok)
			compile_error = true;
	} else {
		if (parent_node.get_type().op != TupAdd_Op) {
			std::string unnamed;
			bool ok = ctup->set(0, unnamed, parent_dpin); // includes the parent into Lgtuple where the parent is not TupAdd 
			if (!ok)
				compile_error = true;
		}

		if (key_pos < 0 && key_name.empty()) {
			if (val_dpin.get_node().get_type().op == TupAdd_Op) {
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

	if (compile_error) {
		Pass::error("tuples {} could not add field \n", "XX", "XX");
	}

	node2tuple[node.get_compact()] = ctup;
}

bool Pass_cprop::process_tuples(Node &node, XEdge_iterator &inp_edges_ordered) {

   auto op = node.get_type().op;
   if (op != TupAdd_Op && op != TupGet_Op)
     return false;

   I(inp_edges_ordered.size() >= 1);

   uint8_t pid = 1;

   // Either pos or name
   Node_pin key_pos_dpin;
   Node_pin key_name_dpin;

   int key_pos = -1;
   std::string key_name;

#if 1
   for (auto e : inp_edges_ordered) {
     fmt::print("edge sink.pid:{} driver_node:{}\n",e.sink.get_pid(), e.driver.get_node().debug_name());
   }
#endif

   auto parent_dpin = inp_edges_ordered[0].driver;
   auto parent_node = parent_dpin.get_node();


   if (inp_edges_ordered[pid].sink.get_pid() == 1) { // key name is connected
     key_name_dpin = inp_edges_ordered[pid].driver;
     I(key_name_dpin.get_node().get_type().op == TupKey_Op);
     I(key_name_dpin.has_name());
     key_name = key_name_dpin.get_name();
     if (key_name.substr(0, 2) == "__") {
       if (key_name.substr(0, 7) == "__q_pin") {
				 fmt::print("process_tuple_q_pin parent_dpin:{} node:{}\n", parent_dpin.debug_name(), node.debug_name());
				 process_tuple_q_pin(node, parent_dpin);
				 return true;
       }
       return false;  // do not deal with __XXX like __bits
     }
     pid++;
   }

   if (inp_edges_ordered.size() > pid && inp_edges_ordered[pid].sink.get_pid() == 2) { // key pos is connected
     key_pos_dpin = inp_edges_ordered[pid].driver;
     if (key_pos_dpin.get_node().is_type_const()) {
       key_pos = key_pos_dpin.get_node().get_type_const().to_i();
     }
     pid++;
   }


#if 0
   for(auto e:node2tuple) {
     fmt::print("parent_node:{}..\n", e.first.get_nid());
     e.second->dump();
   }
#endif

   auto ptup_it = node2tuple.find(parent_node.get_compact()); //ptup = tuple chain of the parent node
   if (op == TupAdd_Op) {
     I(inp_edges_ordered.size() >= 2, "at least key_name or key_pos should be connected");

		 std::shared_ptr<Lgtuple> ctup; //ctup = tuple chain of the current node
		 bool parent_could_be_deleted = false;
     if (ptup_it == node2tuple.end()) {
       // First tuple entry
       ctup = std::make_shared<Lgtuple>(); // No tuple root name?
     } else {
			 auto parent_out_edges = parent_dpin.out_edges();        
			 parent_could_be_deleted = parent_out_edges.size() == 1; // This is the only one child

			 if (!parent_could_be_deleted) {
				 // if all the parent out edges are tuple get AND ONLY this parent can be deleted
				 bool loop_exit = false;
				 for (auto e:parent_out_edges) {
					 auto dest_node = e.sink.get_node();
					 if (dest_node == node)
						 continue; // this node

					 if (dest_node.get_type().op == TupGet_Op) {
						 // WARNING: no testing case, but it should work
						 auto t = dest_node.inp_edges_ordered();
						 bool deleted = process_tuples(dest_node, t);
						 if (deleted)
							 continue;
					 }

					 loop_exit = true; // as long as one of the sink node is not TG, this parent has other purpose and cannot be removed.
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


     // so far, current tup inherited entire parent tup chain but not yet merge the current tuple_add node.
     I(pid == 3);
     auto val_dpin = inp_edges_ordered[pid].driver;
     pid++;
		 I(pid == inp_edges_ordered.size());

		 merge_to_tuple(ctup, node, parent_node, parent_dpin, key_pos, key_name, val_dpin);

     fmt::print("TupAdd node:{} pos:{} key:{} val:{}\n", node.debug_name(), key_pos, key_name, val_dpin.debug_name());

		 if (parent_could_be_deleted)
			 parent_node.del_node();

   } else {
     I(op == TupGet_Op);

		 if (ptup_it == node2tuple.end()) { //ptup_it = parent_node 
			 std::string_view tup_name;
			 if (parent_dpin.has_name())
				 tup_name = parent_dpin.get_name();
			 else
				 tup_name = "FIXME"; // maybe access vname

			 std::string key;
			 if (key_name.empty())
				 key = std::to_string(key_pos);
			 else
				 key = key_name;

			 Pass::error("there is no tuple in {}, so no valid field {}\n", tup_name, key);
			 return false;
		 }

     auto ctup = ptup_it->second; 

		 Node_pin val_dpin;

		 if (!key_name.empty()) {
			 if (ctup->has_key_name(key_name)) {
				 val_dpin = ctup->get_value_dpin(key_pos, key_name);
			 } else {

				 std::string_view ptup_name;
				 if (parent_dpin.has_name())
					 ptup_name = parent_dpin.get_name();
				 else
					 ptup_name = ctup->get_parent_key_name(); // FIXME: we should have a better way to get tuple name for error reporting

				 ctup->dump();
				 Pass::error("tuple {} does not have field {}\n", ptup_name, key_name);
				 return false;
			 }
		 } else if (key_pos >= 0) {
			 if (ctup->has_key_pos(key_pos)) {
				 val_dpin = ctup->get_value_dpin(key_pos, key_name);
			 } else {
				 std::string_view ptup_name;
				 if (parent_dpin.has_name())
					 ptup_name = parent_dpin.get_name();
				 else
					 ptup_name = ctup->get_parent_key_name(); // FIXME: we should have a better way to get tuple name for error reporting

				 ctup->dump();
				 Pass::error("tuple {} does not have field {}\n", ptup_name, key_pos);
				 return false;
			 }
		 }

     if (!val_dpin.is_invalid()) {
			 fmt::print("TupGet node:{} pos:{} key:{} val:{}\n", node.debug_name(), key_pos, key_name, val_dpin.debug_name());
			 collapse_forward_for_pin(node, val_dpin);
			 return true;
     }
   }

	 return false;
}

void Pass_cprop::trans(LGraph *g) {

  for (auto node : g->forward()) {

		// No subs, inside side-effects, or flops/mems that that get connected latter
    auto op = node.get_type().op;

    if (op == SubGraph_Op) {
      process_subgraph(node);
      continue;
    }

    if (op == SFlop_Op || op == AFlop_Op || op == Latch_Op || op == FFlop_Op ||
				op == Memory_Op || op == SubGraph_Op) {
			fmt::print("cprop skipping node:{}\n", node.debug_name());
			continue;
		}

		if (!node.has_outputs()) {
			fmt::print("cprop deleting node:{}\n", node.debug_name());
			node.del_node();
			continue;
		}
		fmt::print("cprop node:{}\n",node.debug_name());

    int  n_inputs_constant   = 0;
    int  n_inputs            = 0;

    auto inp_edges_ordered = node.inp_edges_ordered();

    for (auto e : inp_edges_ordered) {
      n_inputs++;
      if (e.driver.get_node().is_type_const())
        n_inputs_constant++;
    }

    bool get_deleted = process_tuples(node, inp_edges_ordered);
    if (get_deleted) {
      I(node.is_invalid());
      continue;
    }
		I(!node.is_invalid());

    if (n_inputs == n_inputs_constant && n_inputs) {
      replace_all_inputs_const(node, inp_edges_ordered);
    } else if (n_inputs) {
      replace_part_inputs_const(node, inp_edges_ordered);
    }
    if (node.is_invalid()) {
      continue;  // It got deleted
    }

    // Try again to collapse forward if some inputs were constant
    try_collapse_forward(node, inp_edges_ordered);
  }

  for(auto node:g->fast()) {
    if (!node.has_outputs()) {
      if (!node.is_type_sub())
        node.del_node();
      continue;
    }
  }

  g->sync();
}

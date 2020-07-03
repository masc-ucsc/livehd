//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <algorithm>
#include <vector>

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_bitwidth.hpp"

// Useful for debug
//#define PRESERVE_ATTR_NODE

static Pass_plugin sample("pass_bitwidth", Pass_bitwidth::setup);

void Pass_bitwidth::setup() {
  Eprp_method m1("pass.bitwidth", "MIT algorithm for bitwidth optimization", &Pass_bitwidth::trans);

  m1.add_label_optional("max_iterations", "maximum number of iterations to try", "10");

  register_pass(m1);
}

Pass_bitwidth::Pass_bitwidth(const Eprp_var &var) : Pass("pass.bitwidth", var) {
  auto miters = var.get("max_iterations");

  bool ok = absl::SimpleAtoi(miters, &max_iterations);
  if (!ok || max_iterations > 100 || max_iterations <= 0) {
    error("pass.bitwidth max_iterations:{} should be bigger than zero and less than 100", max_iterations);
    return;
  }
}

void Pass_bitwidth::trans(Eprp_var &var) {
  Pass_bitwidth p(var);

  std::vector<const LGraph *> lgs;
  for (const auto &l : var.lgs) {
    p.do_trans(l);
  }
}

void Pass_bitwidth::do_trans(LGraph *lg) {
  Lbench b("pass.bitwidth");
  bw_pass(lg);

}

void Pass_bitwidth::process_const(Node &node) {
	auto dpin = node.get_driver_pin();
  auto it = bwmap.emplace(dpin.get_compact(), Bitwidth_range(node.get_type_const()));
	adjust_dpin_bits(dpin, it.first->second);
}

void Pass_bitwidth::process_mux(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size()); // Dangling sum??? (delete)

	Lconst max_val;
	Lconst min_val;
  for (auto e:inp_edges) {
    auto it3 = bwmap.find(e.driver.get_compact());
    if (it3 != bwmap.end()) {
			if (max_val < it3->second.get_max())
				max_val = it3->second.get_max();

			if (min_val == 0 || min_val > it3->second.get_min())
				min_val = it3->second.get_min();
    } else if (e.driver.get_bits()) {
			Lconst b(1);
			b = b.lsh_op(e.driver.get_bits());
			if (b > max_val)
				max_val = b;

			min_val = Lconst(0)-max_val;
		}else{
			if (e.driver.has_name())
				fmt::print("pass.bitwidth sum:{} has input pin:{} unconstrained\n", node.debug_name(), e.driver.get_name());
			else
				fmt::print("pass.bitwidth sum:{} has some inputs unconstrained\n", node.debug_name());
      not_finished = true;
      return;
    }
  }

  bwmap.emplace(node.get_driver_pin(0).get_compact(), Bitwidth_range(min_val, max_val));
}

void Pass_bitwidth::process_sum(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size()); // Dangling sum??? (delete)

	Lconst max_val;
	Lconst min_val;
  for (auto e:inp_edges) {
    auto it3 = bwmap.find(e.driver.get_compact());
    if (it3 == bwmap.end()) {
			if (e.driver.has_name())
				fmt::print("pass.bitwidth sum:{} has input pin:{} unconstrained\n", node.debug_name(), e.driver.get_name());
			else
				fmt::print("pass.bitwidth sum:{} has some inputs unconstrained\n", node.debug_name());
      not_finished = true;
      return;
    }
		max_val = max_val + it3->second.get_max();
		min_val = min_val + it3->second.get_min();
  }

  bwmap.emplace(node.get_driver_pin(0).get_compact(), Bitwidth_range(min_val, max_val));
}

void Pass_bitwidth::process_comparator(Node &node) { bwmap.emplace(node.get_driver_pin(0).get_compact(), Bitwidth_range(1)); }

void Pass_bitwidth::process_logic(Node &node, XEdge_iterator &inp_edges) {
	bool logic_op        = node.has_driver_pin_connected(0);
	bool logic_reduction = node.has_driver_pin_connected(1);

	if (logic_reduction) {
		bwmap.emplace(node.get_driver_pin(1).get_compact(), Bitwidth_range(1));
	}

	if (logic_op && inp_edges.size() >= 1) {
		uint16_t max_bits=0;

		for (auto e : inp_edges) {
			auto it = bwmap.find(e.driver.get_compact());
			uint16_t bits = 0;
			if (it == bwmap.end()) {
				bits = e.driver.get_bits();
			} else {
				bits = it->second.get_bits();
			}
			if (bits == 0) {
				if (e.driver.has_name()) {
					fmt::print("pass.bitwidth gate:{} has input pin:{} unconstrained\n", node.debug_name(), e.driver.get_name());
				}else{
					fmt::print("pass.bitwidth gate:{} has some inputs unconstrained\n", node.debug_name());
				}
				not_finished = true;
				return;
			}
			if (bits > max_bits) max_bits = bits;
		}

		bwmap.emplace(node.get_driver_pin(0).get_compact(), Bitwidth_range(max_bits));
	}
}

void Pass_bitwidth::process_logic_and(Node &node, XEdge_iterator &inp_edges) {
  bool logic_op        = node.has_driver_pin_connected(0);
  bool logic_reduction = node.has_driver_pin_connected(1);

  if (logic_reduction) {
    bwmap.emplace(node.get_driver_pin(1).get_compact(), Bitwidth_range(1));
  }

  if (logic_op && inp_edges.size() >= 1) {
		uint16_t max_bits=UINT16_MAX;

		for (auto e : inp_edges) {
			auto it = bwmap.find(e.driver.get_compact());
			uint16_t bits = 0;
			if (it == bwmap.end()) {
				bits = e.driver.get_bits();
			} else {
				bits = it->second.get_bits();
			}
			if (bits && bits < max_bits) max_bits = bits;
		}
		if (max_bits == UINT16_MAX) {
			fmt::print("pass.bitwidth and:{} does not have any constrained input\n", node.debug_name());
			not_finished = true;
			return;
		}

		bwmap.emplace(node.get_driver_pin(0).get_compact(), Bitwidth_range(max_bits));

		for (auto e:inp_edges) {
			if (e.driver.get_num_edges() > 1) {
				must_perform_backward = must_perform_backward || e.driver.get_bits()==0;
			} else {
				auto bits = e.driver.get_bits();
				if (bits == 0 || bits > max_bits)
					e.driver.set_bits(max_bits);
			}
		}
  }
}


Pass_bitwidth::Attr Pass_bitwidth::get_key_attr(std::string_view key) {
  if (key.substr(0, 6) == "__bits") return Attr::Set_bits;

  if (key.substr(0, 5) == "__max") return Attr::Set_max;

  if (key.substr(0, 5) == "__min") return Attr::Set_min;

  return Attr::Set_other;
}

void Pass_bitwidth::process_attr_set_new_attr(Node &node, XEdge_iterator &inp_edges) {
	auto dpin_key = inp_edges[1].driver;
	if (!dpin_key.get_node().is_type(TupKey_Op))
		return; // Can not handle now

	I(dpin_key.has_name());
	auto key  = dpin_key.get_name();
  auto attr = get_key_attr(key);
  if (attr==Attr::Set_other)
    return;

	auto dpin_val = inp_edges[2].driver;
	if (!dpin_val.get_node().is_type_const())
		return; // Can not handle now

  auto attr_dpin = node.get_driver_pin(0);

	std::string_view dpin_name;
  if (attr_dpin.has_name())
    dpin_name = attr_dpin.get_name();

	auto val = dpin_val.get_node().get_type_const();

  auto it = bwmap.find(inp_edges[0].driver.get_compact());
  if (it == bwmap.end()) {
    fmt::print("attr_set unable to find bwmap to AttrSet name:{} key:{} val:{}\n", dpin_name, key, val.to_pyrope());
    not_finished = true;
    return;
  }
  auto &bw = it->second;

  fmt::print("attr_set name:{} key:{} val:{} bw_bits:{}\n", dpin_name, key, val.to_pyrope(), bw.get_bits());

  if (attr == Attr::Set_bits) {
    if (bw.get_bits() && bw.get_bits() > val.to_i()) {
      Pass::error("bitwidth missmatch. Variable {} needs {}bits, but constrained to {}bits\n", dpin_name, bw.get_bits(), val.to_i());
    } else {
      if (bw.is_always_positive())
        bw.set_ubits(val.to_i());
      else
        bw.set_sbits(val.to_i());
    }
  }else if (attr == Attr::Set_max) {
    I(false); // FIXME: todo
  } else if (attr == Attr::Set_min) {
    I(false);  // FIXME: todo
  }

  for (auto out_dpin : node.out_connected_pins()) {
    out_dpin.set_bits(bw.get_bits());
    bwmap.emplace(out_dpin.get_compact(), bw);
  }

  // dpin_val.dump_all_prp_vname();
}

void Pass_bitwidth::process_attr_set_propagate(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size()==2);
  I(inp_edges[0].sink.get_pid() == 0); // value
  I(inp_edges[1].sink.get_pid() == 3); // parent AttrSet

  auto attr_dpin = node.get_driver_pin(0);
	std::string_view dpin_name;
  if (attr_dpin.has_name())
    dpin_name = attr_dpin.get_name();

  auto data_dpin = inp_edges[0].driver;
  auto parent_attr_dpin = inp_edges[1].driver;

  auto data_it = bwmap.find(data_dpin.get_compact());
  if (data_it == bwmap.end()) {
    fmt::print("attr_set propagate bwmap to AttrSet name:{}\n", dpin_name);
    not_finished = true;
    return;
  }
  auto &data_bw = data_it->second;

  auto parent_attr_it = bwmap.find(parent_attr_dpin.get_compact());
  if (parent_attr_it == bwmap.end()) {
    fmt::print("attr_set propagate bwmap to AttrSet name:{}\n", dpin_name);
    not_finished = true;
    return;
  }
  auto &parent_attr_bw = parent_attr_it->second;

  fmt::print("attr_set name:{} parent_attr.bits:{} data_bw.bits:{}\n", dpin_name, parent_attr_bw.get_bits(), data_bw.get_bits());

  if (parent_attr_bw.get_bits() && data_bw.get_bits()) {
    if (parent_attr_bw.get_bits() < data_bw.get_bits()) {
      Pass::error("bitwidth missmatch. Variable {} needs {}bits, but constrained to {}bits\n", dpin_name, data_bw.get_bits(), parent_attr_bw.get_bits());
    }else if (parent_attr_bw.get_max() < data_bw.get_max()) {
      Pass::error("bitwidth missmatch. Variable {} needs {}max, but constrained to {}max\n", dpin_name, data_bw.get_max().to_pyrope(), parent_attr_bw.get_max().to_pyrope());
    } else if (parent_attr_bw.get_min() > data_bw.get_min()) {
      Pass::error("bitwidth missmatch. Variable {} needs {}min, but constrained to {}min\n", dpin_name, data_bw.get_min().to_pyrope(), parent_attr_bw.get_min().to_pyrope());
    }
  }

  for (auto out_dpin : node.out_connected_pins()) {
    out_dpin.set_bits(parent_attr_bw.get_bits());
    bwmap.emplace(out_dpin.get_compact(), parent_attr_bw);
  }
}

void Pass_bitwidth::process_attr_set(Node &node, XEdge_iterator &inp_edges) {

  if (node.has_sink_pin_connected(1)) {
    process_attr_set_new_attr(node, inp_edges);
  }else{
    process_attr_set_propagate(node, inp_edges);
  }

}

void Pass_bitwidth::garbage_collect_support_structures(XEdge_iterator &inp_edges) {
  for (auto e : inp_edges) {
    auto it = outcountmap.find(e.driver.get_node().get_compact());
    I(it != outcountmap.end()); // fwd traversal could not be deleted already (flops?)
    auto n = it->second;
    if (n <= 1) {
      outcountmap.erase(it);
      for (auto parent_dpin : e.driver.get_node().out_connected_pins()) {
        auto it2 = bwmap.find(parent_dpin.get_compact());
        if (it2 != bwmap.end()) { // Not all the nodes create bwmap (impossible to infer do not)
          adjust_dpin_bits(parent_dpin, it2->second);
          bwmap.erase(it2);
        }
      }
    } else {
      it->second = n - 1;
    }
  }
}

void Pass_bitwidth::adjust_dpin_bits(Node_pin &dpin, Bitwidth_range &bw) {
  auto bw_bits = bw.get_bits();
  if (bw_bits && bw_bits != dpin.get_bits()) {
    fmt::print("bitwidth: bits:{}->{} for dpin:{}\n", dpin.get_bits(), bw_bits, dpin.debug_name());
    dpin.set_bits(bw_bits);
  }
}

void Pass_bitwidth::bw_pass(LGraph *lg) {

  must_perform_backward = false;
  not_finished = false;

  lg->each_graph_input([&](Node_pin &dpin) {
		if (dpin.get_bits())
		  bwmap.emplace(dpin.get_compact(), Bitwidth_range(dpin.get_bits()));
  });
	outcountmap[lg->get_graph_input_node().get_compact()] = lg->get_graph_input_node().get_num_outputs();

  for (auto node : lg->forward()) {

    auto inp_edges = node.inp_edges();
    auto op        = node.get_type_op();

    if (inp_edges.empty() && (op!=Const_Op && op!=SubGraph_Op && op!=LUT_Op && op!=TupKey_Op)) {
      fmt::print("pass.bitwidth: removing dangling node:{}\n",node.debug_name());
      node.del_node();
      continue;
    }

    outcountmap[node.get_compact()] = node.get_num_outputs();

    if (op == Const_Op) {
      process_const(node);
    } else if (op == TupKey_Op || op == TupGet_Op || op == TupAdd_Op) {
			// Nothing to do for this
    } else if (op == Or_Op || op == Xor_Op) {
      process_logic(node, inp_edges);
    } else if (op == And_Op) {
      process_logic_and(node, inp_edges);
    } else if (op == AttrSet_Op) {
			process_attr_set(node, inp_edges);
    } else if (op == Sum_Op) {
			process_sum(node, inp_edges);
    } else if (op == Mux_Op) {
			process_mux(node, inp_edges);
    } else if (op == GreaterThan_Op || op == LessThan_Op || op == LessEqualThan_Op || op == Equals_Op || op == GreaterEqualThan_Op) {
			process_comparator(node);
    } else {
			fmt::print("FIXME: node:{} still not handled by bitwidth\n", node.debug_name());
    }

    garbage_collect_support_structures(inp_edges);
  }

  lg->each_graph_output([&](Node_pin &dpin) {
    auto spin = dpin.get_sink_from_output();
    if (!spin.has_inputs())
      return;

    auto d_pin = spin.get_driver_pin();

    I(!d_pin.is_invalid());
    auto it = bwmap.find(d_pin.get_compact());
    if (it != bwmap.end()) {
      adjust_dpin_bits(d_pin, it->second);

      bwmap.erase(it);
    }

    if (d_pin.get_bits()) {
      dpin.set_bits(d_pin.get_bits());
    }

  });

#ifndef PRESERVE_ATTR_NODE
  if (not_finished) {
    fmt::print("pass_bitwidth: could not converge\n");
  } else {
    // FIXME: this code may need to move to cprop if we have several types of
    // attributes. Delete only if all the attributes are finished
    //
    // Delete all the attr_set/get for bitwidth
    for (auto node : lg->fast()) {
      auto op = node.get_type_op();

      if (op == AttrSet_Op) {
        if (node.has_sink_pin_connected(1)) {
          auto key_dpin = node.get_sink_pin(1).get_driver_pin();
          auto attr     = get_key_attr(key_dpin.get_name());
          if (attr == Attr::Set_other) continue;
        }

        auto data_dpin = node.get_sink_pin(0).get_driver_pin();

        for (auto e : node.out_edges()) {
          if (e.driver.get_pid() == 0) {
            e.sink.connect_driver(data_dpin);
          }
        }
        node.del_node();
      } else if (op == AttrGet_Op) {
        I(false); // AttGet_Op should be gone if finished (if bits/max/min)
      }
    }
  }
#endif

#ifndef NDEBUG
  for(auto it:bwmap) {
    fmt::print("bwmap left bits:{}\n", it.second.get_bits());
  }
#endif

  if (must_perform_backward) {
    fmt::print("pass_bitwidth: some nodes need to back propagate width\n");
  }
}


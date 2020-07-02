//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <algorithm>
#include <vector>

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_bitwidth.hpp"

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

void Pass_bitwidth::process_logic(Node &node, XEdge_iterator &inp_edges, bool and_op) {
  bool logic_op        = node.has_driver_pin_connected(0);
  bool logic_reduction = node.has_driver_pin_connected(1);

  if (logic_reduction) {
    bwmap.emplace(node.get_driver_pin(1).get_compact(), Bitwidth_range(1));
  }

  if (logic_op) {
    if (inp_edges.size() >= 1) {
      auto it = bwmap.find(inp_edges[0].driver.get_compact());
      if(it == bwmap.end())
				return;

      auto it2 = bwmap.emplace(node.get_driver_pin(0).get_compact(), it->second);   // Just copy BW for 1 input
      auto &bw = it2.first->second;
      if (inp_edges.size() > 1) {
        for (size_t i = 1; i < inp_edges.size(); ++i) {
          auto it3 = bwmap.find(inp_edges[1].driver.get_compact());
          if (it3 == bwmap.end()) {
						bwmap.erase(it2.first);
						return;
          }
          if (and_op) {
            bw.and_op(it3->second);
          }else{
            bw.expand(it3->second, true);
          }
        }
        if (and_op) {
          for (auto e:inp_edges) {
            if (e.driver.get_num_edges() > 1) {
              must_perform_backward = must_perform_backward || e.driver.get_bits()==0;
            } else {
              auto bits = e.driver.get_bits();
              if (bits == 0 || bits > bw.get_bits())
                e.driver.set_bits(bw.get_bits());
            }
          }
        }
      }
    }
  }

}

void Pass_bitwidth::process_attr_set(Node &node, XEdge_iterator &inp_edges) {


	auto dpin_key = node.get_sink_pin(1).get_driver_pin();
	if (!dpin_key.get_node().is_type(TupKey_Op))
		return; // Can not handle now

	I(dpin_key.has_name());
	auto key = dpin_key.get_name();
	if (key.substr(0,6) !="__bits" && key.substr(0,5) != "__max" && key.substr(0,5) !="__min")
		return; // attr to be handled by someone else

	auto dpin_val = node.get_sink_pin(2).get_driver_pin();
	if (!dpin_val.get_node().is_type_const())
		return; // Can not handle now

	std::string_view dpin_name;

	auto val = dpin_val.get_node().get_type_const();

	fmt::print("attr_set name:{} key:{} val:{}\n", dpin_name, key, val.to_pyrope());
}

void Pass_bitwidth::garbage_collect_support_structures(XEdge_iterator &inp_edges) {
  for (auto e : inp_edges) {
    auto it = outcountmap.find(e.driver.get_node().get_compact());
    I(it != outcountmap.end()); // fwd traversal could not be deleted already (flops?)
    auto n = it->second;
    if (n <= 1) {
      outcountmap.erase(it);
      for (auto parent_dpin : e.driver.get_node().out_connected_pins()) {
        auto it = bwmap.find(parent_dpin.get_compact());
        if (it != bwmap.end()) {
          adjust_dpin_bits(parent_dpin, it->second);
          bwmap.erase(it);
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

  lg->each_graph_input([&](Node_pin &dpin) {
    bwmap.emplace(dpin.get_compact(), Bitwidth_range(dpin.get_bits()));
  });
	outcountmap[lg->get_graph_input_node().get_compact()] = lg->get_graph_input_node().get_num_outputs();

  for (auto node : lg->forward()) {

    auto inp_edges = node.inp_edges();

    outcountmap[node.get_compact()] = node.get_num_outputs();

    auto op = node.get_type_op();

    if (op == Const_Op) {
      process_const(node);
    } else if (op == Or_Op || op == And_Op || op == Xor_Op) {
      process_logic(node, inp_edges, op == And_Op);
    } else if (op == TupKey_Op || op == TupGet_Op || op == TupAdd_Op) {
			// Nothing to do for this
    } else if (op == AttrSet_Op) {
			process_attr_set(node, inp_edges);
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

  for(auto it:bwmap) {
    fmt::print("bwmap left bits:{}\n", it.second.get_bits());
  }

  if (must_perform_backward) {
    fmt::print("pass_bitwidth: some nodes need to back propagate width\n");
  }
}


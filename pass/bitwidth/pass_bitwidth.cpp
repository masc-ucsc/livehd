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
  bwmap.emplace(node.get_driver_pin().get_compact(), Bitwidth_range(node.get_type_const()));
}

void Pass_bitwidth::process_logic(Node &node, XEdge_iterator &inp_edges) {
  bool logic_op        = node.has_driver_pin_connected(0);
  bool logic_reduction = node.has_driver_pin_connected(1);

  if (logic_op) {
    if (inp_edges.size() >= 1) {
      auto it = bwmap.find(inp_edges[0].driver.get_compact());
      I(it != bwmap.end());
      auto it2 = bwmap.emplace(node.get_driver_pin(0).get_compact(), it->second);   // Just copy BW for 1 input
      auto &bw = it2.first->second;
      if (inp_edges.size() > 1) {
        for (size_t i = 1; i < inp_edges.size(); ++i) {
          auto it3 = bwmap.find(inp_edges[1].driver.get_compact());
          I(it3 != bwmap.end());
          bw.expand(it3->second, true);
        }
      }
    }
  }
  if (logic_reduction) {
    bwmap.emplace(node.get_driver_pin(1).get_compact(), Bitwidth_range(1));
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
        auto it = bwmap.find(parent_dpin.get_compact());
        I(it!=bwmap.end());

        adjust_dpin_bits(parent_dpin, it->second);

        bwmap.erase(it);
      }
    } else {
      it->second = n - 1;
    }
  }
}

void Pass_bitwidth::adjust_dpin_bits(Node_pin &dpin, Bitwidth_range &bw) {
  auto bw_bits = bw.get_bits();
  if (dpin.get_bits() < bw_bits && bw_bits) {
    fmt::print("bitwidth: set_bits:{} for dpin:{}\n", bw_bits, dpin.debug_name());
    dpin.set_bits(bw_bits);
  }
}

void Pass_bitwidth::bw_pass(LGraph *lg) {

  for (auto node : lg->forward()) {

    auto inp_edges = node.inp_edges();

    outcountmap[node.get_compact()] = node.get_num_outputs();

    auto op = node.get_type_op();

    if (op == Const_Op) {
      process_const(node);
    } else if (op == Or_Op || op == And_Op || op == Xor_Op) {
      process_logic(node, inp_edges);
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
}


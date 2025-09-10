//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_sample.hpp"

#include <format>
#include <iostream>
#include <string>

#include "cell.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "perf_tracing.hpp"

static Pass_plugin sample("pass_sample", Pass_sample::setup);

void Pass_sample::setup() {
  Eprp_method m1("pass.sample", "counts number of nodes in an lgraph", &Pass_sample::work);
  m1.add_label_optional("data", "just a sample parameter");

  register_pass(m1);

  Eprp_method m2("pass.sample.wirecount", "counts number of wires the hierarchy", &Pass_sample::wirecount);
  register_pass(m2);
}

Pass_sample::Pass_sample(const Eprp_var &var) : Pass("pass.sample", var) {}

void Pass_sample::do_work(Lgraph *g) {
  compute_histogram(g);
  compute_max_depth(g);
  // annotate_placement(g);
  create_sample_graph(g);
}

void Pass_sample::work(Eprp_var &var) {
  Pass_sample p(var);

  for (const auto &g : var.lgs) {
    p.do_work(g);
  }
}

void Pass_sample::wirecount(Eprp_var &var) {
  TRACE_EVENT("pass", "SAMPLE_wirecount");
  Pass_sample p(var);

  for (const auto &g : var.lgs) {
    p.do_wirecount(g, 0);
  }
}

void Pass_sample::do_wirecount(Lgraph *g, int indent) {
  int i_num  = 0;
  int i_bits = 0;
  g->each_graph_input([&i_num, &i_bits](const Node_pin &pin) {
    i_num++;
    i_bits += pin.get_bits();
  });

  int o_num  = 0;
  int o_bits = 0;
  g->each_graph_output([&o_num, &o_bits](const Node_pin &pin) {
    o_num++;
    o_bits += pin.get_bits();
  });

  int n_wire      = 0;
  int n_wire_bits = 0;
  int n_nodes     = 0;
  for (auto node : g->fast()) {
    n_nodes++;
    for (const auto &edge : node.out_edges()) {
      n_wire++;
      n_wire_bits += edge.driver.get_bits();
    }
  }

  std::string space;
  for (int i = 0; i < indent; i++) {
    space.append("  ");
  }

  std::print("{}module {} : inputs {} bits {} : outputs {} bits {} : nodes {} : submodules {} : wire {} bits {}\n",
             space,
             g->get_name(),
             i_num,
             i_bits,
             o_num,
             o_bits,
             n_nodes,
             g->get_down_nodes_map().size(),
             n_wire,
             n_wire_bits);

  g->each_local_sub_fast([this, indent, space](Node &node, Lg_type_id lgid) {
    (void)node;

    auto *lib = Graph_library::instance(path);

    Lgraph *sub_lg = lib->open_lgraph(Lg_type_id(lgid));
    if (!sub_lg) {
      return;
    }

    if (sub_lg->is_empty()) {
      int n_inp = 0;
      int n_out = 0;
      for (const auto &io_pin : sub_lg->get_self_sub_node().get_io_pins()) {
        if (io_pin.is_input()) {
          n_inp++;
        } else if (io_pin.is_output()) {
          n_out++;
        }
      }
      std::print("{}  module {} BBOX : inputs {} outputs {}\n", space, sub_lg->get_name(), n_inp, n_out);

      return;  // No blackboxes
    }

    do_wirecount(sub_lg, indent + 1);
  });
}

void Pass_sample::compute_histogram(Lgraph *g) {
  TRACE_EVENT("pass", "SAMPLE_compute_histogram");

  std::map<Ntype_op, int> histogram;

  int cells = 0;
  for (const auto &node : g->forward()) {
    cells++;
    auto type = node.get_type_op();

    histogram[type]++;
  }

  for (auto &e : histogram) {
    std::print("{} {}\n", (int)e.first, e.second);
  }

  std::print("Pass: cells {}\n", cells);
}

void Pass_sample::compute_max_depth(Lgraph *g) {
  TRACE_EVENT("pass", "SAMPLE_max_depth");

  absl::flat_hash_map<Node::Compact, int> depth;

  int max_depth = 0;
  for (const auto &node : g->forward()) {
    int local_max = 0;
    for (const auto &edge : node.inp_edges()) {
      int d = depth[edge.driver.get_node().get_compact()];
      if (local_max <= d) {
        local_max = d + 1;
      }
    }
    std::print("{} {}\n", node.debug_name(), local_max);
    depth[node.get_compact()] = local_max;
  }

  std::print("Pass: max_depth {}\n", max_depth);
}

void Pass_sample::annotate_placement(Lgraph *g) {
  TRACE_EVENT("pass", "SAMPLE_replace_inline");

  int x_pos = 0;

  g->ref_node_place_map()->clear();

  for (auto node : g->forward()) {
    auto p = node.get_place();
    p.replace(x_pos++, 0.0, 1.0, 0.0);
    node.set_place(p);
  }

  for (auto node : g->fast()) {
    const auto &place = node.get_place();
    std::print("1.cell {} placed at x:{}\n", node.get_or_create_name(), place.get_x());
  }
  for (auto node : g->forward()) {
    auto place = node.get_place();
    std::print("2.cell {} placed at x:{}\n", node.get_or_create_name(), place.get_x());
  }
}

void Pass_sample::create_sample_graph(Lgraph *g) {
  // auto lg_path = g->get_path();

  auto *lg = g->ref_library()->create_lgraph("pass_sample", "-");

  std::cout << "Creating new sample Lgraph...\n";
  auto graph_inp_a = lg->add_graph_input("g_inp_a", 0, 4);  // First io in module, 4 bits
  auto graph_inp_b = lg->add_graph_input("g_inp_b", 1, 1);  // Module position 1, 1 bit
  auto graph_out   = lg->add_graph_output("g_out", 2, 3);   // Module possition 2, 3 bits

  auto shr_node    = lg->create_node(Ntype_op::SRA);
  auto shr_out_drv = shr_node.setup_driver_pin();
  shr_out_drv.set_bits(8);
  shr_out_drv.set_name("shr_out");

  auto s_const = lg->create_node_const(Lconst(2));
  I(s_const.get_driver_pin().get_bits() == 3);  // Automatically set bits for const

  auto a_sink = shr_node.setup_sink_pin("a");
  auto b_sink = shr_node.setup_sink_pin("b");
  lg->add_edge(graph_inp_a, a_sink);
  lg->add_edge(graph_inp_b, b_sink);
  lg->add_edge(shr_out_drv, graph_out);

  std::cout << "Finished.\n";
}

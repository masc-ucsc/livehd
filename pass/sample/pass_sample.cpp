//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_sample.hpp"

#include "annotate.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void setup_pass_sample() { Pass_sample::setup(); }

void Pass_sample::setup() {
  Eprp_method m1("pass.sample", "counts number of nodes in an lgraph", &Pass_sample::work);
  m1.add_label_optional("data", "just a sample parameter");

  register_pass(m1);

  Eprp_method m2("pass.sample.wirecount", "counts number of wires the hierarchy", &Pass_sample::wirecount);
  register_pass(m2);
}

Pass_sample::Pass_sample(const Eprp_var &var) : Pass("pass.sample", var) {}

void Pass_sample::do_work(LGraph *g) {
  compute_histogram(g);
  compute_max_depth(g);
  annotate_placement(g);
  create_sample_graph(g);
}

void Pass_sample::work(Eprp_var &var) {
  Pass_sample p(var);

  for (const auto &g : var.lgs) {
    p.do_work(g);
  }
}

void Pass_sample::wirecount(Eprp_var &var) {
  Lbench      b("pass.sample.wirecount");
  Pass_sample p(var);

  for (const auto &g : var.lgs) {
    p.do_wirecount(g, 0);
  }
}

void Pass_sample::do_wirecount(LGraph *g, int indent) {
  int i_num  = 0;
  int i_bits = 0;
  g->each_graph_input([this, &i_num, &i_bits](const Node_pin &pin) {
    i_num++;
    i_bits += pin.get_bits();
  });

  int o_num  = 0;
  int o_bits = 0;
  g->each_graph_output([this, &o_num, &o_bits](const Node_pin &pin) {
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
  for (int i = 0; i < indent; i++) space.append("  ");

  fmt::print("{}module {} : inputs {} bits {} : outputs {} bits {} : nodes {} : wire {} bits {}\n", space,
             g->get_name(), i_num, i_bits, o_num, o_bits, n_nodes, g->get_down_nodes_map().size(),
             n_wire, n_wire_bits);

  g->each_sub_fast([this, indent, space](Node &node, Lg_type_id lgid) {
    (void)node;

    LGraph *sub_lg = LGraph::open(path, lgid);
    if (!sub_lg) return;
    if (sub_lg->is_empty()) {
      int n_inp = 0;
      int n_out = 0;
      for (auto io_pin : sub_lg->get_self_sub_node().get_io_pins()) {
        if (io_pin.is_input()) n_inp++;
        if (io_pin.is_output()) n_out++;
      }
      fmt::print("{}  module {} BBOX : inputs {} outputs {}\n", space, sub_lg->get_name(), n_inp, n_out);

      return;  // No blackboxes
    }

    this->do_wirecount(sub_lg, indent + 1);
  });
}

void Pass_sample::compute_histogram(LGraph *g) {
  Lbench b("pass.sample.compute_histogram");

  std::map<std::string, int> histogram;

  int cells = 0;
  for (const auto node : g->forward()) {
    cells++;
    std::string name(node.get_type().get_name());
    for (const auto &edge : node.inp_edges()) {
      absl::StrAppend(&name, "_i", std::to_string(edge.get_bits()));
    }
    for (const auto &edge : node.out_edges()) {
      absl::StrAppend(&name, "_o", std::to_string(edge.get_bits()));
    }

    histogram[name]++;
  }

  for (auto it = histogram.begin(); it != histogram.end(); it++) {
    fmt::print("{} {}\n", it->first, it->second);
  }

  fmt::print("Pass: cells {}\n", cells);
}

void Pass_sample::compute_max_depth(LGraph *g) {
  Lbench b("pass.sample.max_depth");

  absl::flat_hash_map<Node::Compact, int> depth;

  int max_depth = 0;
  for (const auto node : g->forward()) {
    int local_max = 0;
    for (const auto &edge : node.inp_edges()) {
      int d = depth[edge.driver.get_node().get_compact()];
      if (local_max <= d) local_max = d + 1;
    }
    fmt::print("{} {}\n", node.debug_name(), local_max);
    depth[node.get_compact()] = local_max;
  }

  fmt::print("Pass: max_depth {}\n", max_depth);
}

void Pass_sample::annotate_placement(LGraph *g) {
  Lbench b("pass.sample.replace_inline");

  int x_pos = 0;

  Ann_node_place::clear(g);  // Not needed, but clears all the previous placement info

  for (auto node : g->forward()) {
    auto *p = node.ref_place();
    p->replace(x_pos++, 0);
  }

  for (auto node : g->fast()) {
    const auto &place = node.get_place();
    fmt::print("1.cell {} placed at x:{}\n", node.create_name(), place.get_x());
  }
  for (auto node : g->forward()) {
    auto *place = node.ref_place();
    fmt::print("2.cell {} placed at x:{}\n", node.create_name(), place->get_x());
  }
}

void Pass_sample::create_sample_graph(LGraph *g) {
  auto lg_path   = g->get_path();
  auto lg_source = g->get_library().get_source(g->get_lgid());

  LGraph *lg = LGraph::create(lg_path, "pass_sample", lg_source);
  fmt::print("Creating new sample LGraph...\n");
  auto graph_inp_a = lg->add_graph_input("g_inp_a", 0, 4);  // First io in module, 4 bits
  auto graph_inp_b = lg->add_graph_input("g_inp_b", 1, 1);  // Module position 1, 1 bit
  auto graph_out   = lg->add_graph_output("g_out", 2, 3);   // Module possition 2, 3 bits

  auto shr_node     = lg->create_node(ShiftRight_Op);
  auto shr_inp_sink = shr_node.setup_sink_pin(0);
  auto shr_out_drv  = shr_node.setup_driver_pin(0);
  shr_out_drv.set_bits(8);
  shr_out_drv.set_name("shr_out");

  // auto b_const = lg->create_node_const(3, 2);
  // auto b_drv = b_const.setup_driver_pin(0);
  // b_drv.set_name("b_drv");

  auto s_const = lg->create_node_const(Lconst(2, 2));
  I(s_const.get_driver_pin().get_bits() == 2);  // Automatically set bits for const
  auto s_drv = s_const.setup_driver_pin(0);
  s_drv.set_name("s_drv");

  auto b_sink = shr_node.setup_sink_pin(1);
  auto s_sink = shr_node.setup_sink_pin(2);
  lg->add_edge(graph_inp_a, shr_inp_sink);
  lg->add_edge(shr_out_drv, graph_out);
  // lg->add_edge(b_drv, b_sink);
  lg->add_edge(graph_inp_b, b_sink);
  lg->add_edge(s_drv, s_sink);

  fmt::print("Finished.\n");
}

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_sample.hpp"

#include <format>
#include <iostream>
#include <map>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "perf_tracing.hpp"

using livehd::Hhds_graph_library;
using livehd::graph_util::bits_of;
using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::debug_name;
using livehd::graph_util::default_instance_name;
using livehd::graph_util::set_bits;
using livehd::graph_util::set_pin_name;
using livehd::graph_util::setup_sink_by_name;
using livehd::graph_util::type_op_of;

static Pass_plugin sample("pass_sample", Pass_sample::setup);

void Pass_sample::setup() {
  Eprp_method m1("pass.sample", "counts number of nodes in an lgraph", &Pass_sample::work);
  m1.add_label_optional("data", "just a sample parameter");

  register_pass(m1);

  Eprp_method m2("pass.sample.wirecount", "counts number of wires the hierarchy", &Pass_sample::wirecount);
  register_pass(m2);
}

Pass_sample::Pass_sample(const Eprp_var& var) : Pass("pass.sample", var) {}

void Pass_sample::do_work(hhds::Graph* g) {
  compute_histogram(g);
  compute_max_depth(g);
  create_sample_graph(g);
}

void Pass_sample::work(Eprp_var& var) {
  Pass_sample p(var);

  for (const auto& g : var.graphs) {
    p.do_work(g.get());
  }
}

void Pass_sample::wirecount(Eprp_var& var) {
  TRACE_EVENT("pass", "SAMPLE_wirecount");
  Pass_sample p(var);

  for (const auto& g : var.graphs) {
    p.do_wirecount(g.get(), 0);
  }
}

void Pass_sample::do_wirecount(hhds::Graph* g, int indent) {
  if (g == nullptr) {
    return;
  }
  auto gio = g->get_io();
  if (!gio) {
    return;
  }

  int i_num  = 0;
  int i_bits = 0;
  for (const auto& d : gio->get_input_pin_decls()) {
    ++i_num;
    auto dpin = g->get_input_pin(d.name);
    if (auto b = bits_of(dpin); b != 0) {
      i_bits += b;
    } else {
      i_bits += static_cast<int>(d.bits);
    }
  }

  int o_num  = 0;
  int o_bits = 0;
  for (const auto& d : gio->get_output_pin_decls()) {
    ++o_num;
    auto dpin = g->get_output_pin(d.name);
    if (auto b = bits_of(dpin); b != 0) {
      o_bits += b;
    } else {
      o_bits += static_cast<int>(d.bits);
    }
  }

  int n_wire      = 0;
  int n_wire_bits = 0;
  int n_nodes     = 0;
  int n_subs      = 0;
  for (auto node : g->fast_class()) {
    ++n_nodes;
    if (type_op_of(node) == Ntype_op::Sub) {
      ++n_subs;
    }
    for (const auto& edge : node.out_edges()) {
      ++n_wire;
      n_wire_bits += bits_of(edge.driver);
    }
  }

  std::string space;
  for (int i = 0; i < indent; ++i) {
    space.append("  ");
  }

  std::print("{}module {} : inputs {} bits {} : outputs {} bits {} : nodes {} : submodules {} : wire {} bits {}\n",
             space,
             std::string{gio->get_name()},
             i_num,
             i_bits,
             o_num,
             o_bits,
             n_nodes,
             n_subs,
             n_wire,
             n_wire_bits);

  // Walk immediate sub-instances. Recurse only into bodies that exist; sub
  // declarations with no body (black-box) get a single-line summary.
  for (auto node : g->fast_class()) {
    if (type_op_of(node) != Ntype_op::Sub) {
      continue;
    }
    auto sub_io = node.get_subnode_io();
    if (!sub_io) {
      continue;
    }
    auto sub_g = node.get_subnode_graph();
    if (!sub_g) {
      int n_inp = 0;
      int n_out = 0;
      for (const auto& d : sub_io->get_input_pin_decls()) {
        (void)d;
        ++n_inp;
      }
      for (const auto& d : sub_io->get_output_pin_decls()) {
        (void)d;
        ++n_out;
      }
      std::print("{}  module {} BBOX : inputs {} outputs {}\n", space, std::string{sub_io->get_name()}, n_inp, n_out);
      continue;
    }
    do_wirecount(sub_g.get(), indent + 1);
  }
}

void Pass_sample::compute_histogram(hhds::Graph* g) {
  TRACE_EVENT("pass", "SAMPLE_compute_histogram");

  std::map<Ntype_op, int> histogram;

  int cells = 0;
  for (const auto& node : g->forward_class()) {
    ++cells;
    auto type = type_op_of(node);
    ++histogram[type];
  }

  for (auto& e : histogram) {
    std::print("{} {}\n", static_cast<int>(e.first), e.second);
  }

  std::print("Pass: cells {}\n", cells);
}

void Pass_sample::compute_max_depth(hhds::Graph* g) {
  TRACE_EVENT("pass", "SAMPLE_max_depth");

  absl::flat_hash_map<hhds::Class_index, int> depth;

  int max_depth = 0;
  for (const auto& node : g->forward_class()) {
    int local_max = 0;
    for (const auto& edge : node.inp_edges()) {
      int d = depth[edge.driver.get_master_node().get_class_index()];
      if (local_max <= d) {
        local_max = d + 1;
      }
    }
    std::print("{} {}\n", debug_name(node), local_max);
    depth[node.get_class_index()] = local_max;
    if (local_max > max_depth) {
      max_depth = local_max;
    }
  }

  std::print("Pass: max_depth {}\n", max_depth);
}

void Pass_sample::create_sample_graph(hhds::Graph* g) {
  // Use the same library this graph was created on so the sub-graph is
  // visible to subsequent passes / saves.
  auto* owner_lib = g->get_io() ? g->get_io()->get_library() : nullptr;
  if (owner_lib == nullptr) {
    return;
  }

  std::cout << "Creating new sample Lgraph...\n";

  auto sub_io = owner_lib->find_io("pass_sample");
  if (!sub_io) {
    sub_io = owner_lib->create_io("pass_sample");
  }
  sub_io->add_input("g_inp_a", 0);
  sub_io->set_bits("g_inp_a", 4);
  sub_io->add_input("g_inp_b", 1);
  sub_io->set_bits("g_inp_b", 1);
  sub_io->add_output("g_out", 2);
  sub_io->set_bits("g_out", 3);

  auto lg = sub_io->has_graph() ? sub_io->get_graph() : sub_io->create_graph();

  auto graph_inp_a = lg->get_input_pin("g_inp_a");
  auto graph_inp_b = lg->get_input_pin("g_inp_b");
  auto graph_out   = lg->get_output_pin("g_out");

  auto shr_node    = create_typed_node(*lg, Ntype_op::SRA);
  auto shr_out_drv = shr_node.create_driver_pin(0);
  set_bits(shr_out_drv, 8);
  set_pin_name(shr_out_drv, "shr_out");

  auto s_const = create_const(*lg, *Dlop::create_integer(2));
  (void)s_const;  // sample: we don't wire the const into the SRA's `b` here.

  auto a_sink = setup_sink_by_name(shr_node, "a");
  auto b_sink = setup_sink_by_name(shr_node, "b");
  graph_inp_a.connect_sink(a_sink);
  graph_inp_b.connect_sink(b_sink);
  shr_out_drv.connect_sink(graph_out);

  std::cout << "Finished.\n";
}

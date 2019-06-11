//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "annotate.hpp"
#include "pass_sample.hpp"

void setup_pass_sample() {
  Pass_sample p;
  p.setup();
}

void Pass_sample::setup() {
  Eprp_method m1("pass.sample", "counts number of nodes in an lgraph", &Pass_sample::work);

  register_pass(m1);
}

Pass_sample::Pass_sample()
    : Pass("sample") {
}

void Pass_sample::do_work(LGraph *g) {
  compute_histogram(g);
  compute_max_depth(g);
  annotate_placement(g);
  create_sample_graph(g);
}

void Pass_sample::work(Eprp_var &var) {
  Pass_sample pass;

  for(const auto &g : var.lgs) {
    pass.do_work(g);
  }
}

void Pass_sample::compute_histogram(LGraph *g) {
  LGBench b("pass.sample.compute_histogram");

  std::map<std::string, int> histogram;

  int cells = 0;
  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    cells++;
    std::string name(node.get_type().get_name());
    for(const auto &edge : node.inp_edges()) {
      absl::StrAppend(&name, "_i", std::to_string(edge.get_bits()));
    }
    for(const auto &edge : node.out_edges()) {
      absl::StrAppend(&name, "_o", std::to_string(edge.get_bits()));
    }

    histogram[name]++;
  }

  for(auto it=histogram.begin(); it != histogram.end(); it++) {
    fmt::print("{} {}\n",it->first,it->second);
  }

  fmt::print("Pass: cells {}\n", cells);
}

void Pass_sample::compute_max_depth(LGraph *g) {
  LGBench b("pass.sample.max_depth");

  absl::flat_hash_map<Node::Compact, int>  depth;

  int max_depth = 0;
  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    int local_max = 0;
    for(const auto &edge : node.inp_edges()) {
      int d = depth[edge.driver.get_node().get_compact()];
      if (local_max<=d)
        local_max = d+1;
    }
    depth[node.get_compact()] = local_max;
  }

  fmt::print("Pass: max_depth {}\n", max_depth);
}

void Pass_sample::annotate_placement(LGraph *g) {
  LGBench b("pass.sample.replace_inline");

  int x_pos = 0;

  Ann_node_place::clear(g); // Not needed, but clears all the previous placement info

  for(const auto &nid : g->backward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    Node_place p(x_pos++,0);
    Ann_node_place::set(node, p);
  }

  for(const auto &nid : g->fast()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    auto &place = Ann_node_place::get(node);
    fmt::print("1.cell {} placed at x:{}\n",node.create_name(), place.get_x());
  }
  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    auto &place = Ann_node_place::get(node);
    fmt::print("2.cell {} placed at x:{}\n",node.create_name(), place.get_x());
  }
}

void Pass_sample::create_sample_graph(LGraph *g) {
  auto lg_path = g->get_path();
  auto lg_source = g->get_library().get_source(g->get_lgid());
  LGraph *lg = LGraph::create(lg_path, "pass_sample", lg_source);
  fmt::print("Creating new sample LGraph...\n");
  auto shr_node = lg->create_node(ShiftRight_Op);
  auto graph_inp = lg->add_graph_input("g_inp", 8, 0);
  auto graph_out = lg->add_graph_output("g_out", 8, 0);
  auto shr_inp_sink = shr_node.setup_sink_pin(0);
  auto shr_out_drv = shr_node.setup_driver_pin(0);
  shr_out_drv.set_bits(8);
  shr_out_drv.set_name("shr_ou");
  auto b_const = lg->create_node_const(3, 2);
  auto b_drv = b_const.setup_driver_pin(0);
  b_drv.set_name("b_drv");
  auto s_const = lg->create_node_const(1, 2);
  auto s_drv = s_const.setup_driver_pin(0);
  s_drv.set_name("s_drv");
  auto b_sink = shr_node.setup_sink_pin(1);
  auto s_sink = shr_node.setup_sink_pin(2);
  lg->add_edge(graph_inp, shr_inp_sink);
  lg->add_edge(shr_out_drv, graph_out);
  lg->add_edge(b_drv, b_sink);
  lg->add_edge(s_drv, s_sink);

  fmt::print("Finished.\n");
  lg->close();
}

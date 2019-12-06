//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "annotate.hpp"
#include "inou_pyrope.hpp"

void setup_inou_pyrope() {
  Inou_pyrope p;
  p.setup();
}

void Inou_pyrope::setup() {
  Eprp_method m1("inou.pyrope", "counts number of nodes in an lgraph", &Inou_pyrope::work);

  register_pass(m1);
}

Inou_pyrope::Inou_pyrope()
    : Pass("sample") {
}

void Inou_pyrope::work(Eprp_var &var) {
  Inou_pyrope pass;

  for(const auto &g : var.lgs) {
    pass.compute_histogram(g);
    pass.compute_max_depth(g);
  }
}

void Inou_pyrope::compute_histogram(LGraph *g) {
  Lbench b("inou.pyrope.compute_histogram");

  std::map<std::string, int> histogram;

  int cells = 0;
  for(const auto node : g->forward()) {

    cells++;
    std::string tmp_name(node.get_type().get_name());
    for(const auto &edge : node.inp_edges()) {
      absl::StrAppend(&tmp_name, "_i", std::to_string(edge.get_bits()));
    }
    for(const auto &edge : node.out_edges()) {
      absl::StrAppend(&tmp_name, "_o", std::to_string(edge.get_bits()));
    }

    histogram[tmp_name]++;
  }

  for(auto it=histogram.begin(); it != histogram.end(); it++) {
    fmt::print("{} {}\n",it->first,it->second);
  }

  fmt::print("Pass: cells {}\n", cells);
}

void Inou_pyrope::compute_max_depth(LGraph *g) {
  Lbench b("inou.pyrope.max_depth");

  absl::flat_hash_map<Node::Compact, int>  depth;

  int max_depth = 0;
  for(const auto node : g->forward()) {

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


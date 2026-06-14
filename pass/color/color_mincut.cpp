// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_mincut.hpp"

#include <sys/stat.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <print>
#include <string>

#include "algorithms/global_mincut/algorithms.h"
#include "algorithms/global_mincut/minimum_cut.h"
#include "common/configuration.h"
#include "common/definitions.h"
#include "data_structure/graph_access.h"
#include "data_structure/mutable_graph.h"
#include "io/graph_io.h"
#include "str_tools.hpp"
#include "tools/random_functions.h"
#include "tools/string.h"
#include "tools/timer.h"

namespace livehd::color {

using GraphPtr = std::shared_ptr<mutable_graph>;

Color_mincut::Color_mincut(Color_opts opts_, int iters_, int seed_, std::string_view alg_)
    : opts(opts_), iters(iters_), seed(seed_), alg(alg_) {}

void Color_mincut::gather_ids(hhds::Graph* g) {
  int next = 0;
  for (auto n : g->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    node2id[n]    = next;
    id2node[next] = n;
    next++;
  }
  num_nodes = next;
}

void Color_mincut::gather_neighs(hhds::Graph* g) {
  (void)g;
  num_edges = 0;
  for (const auto& [curr_id, curr_node] : id2node) {
    if (!is_partitionable(curr_node)) {
      continue;
    }
    for (const auto& e : curr_node.out_edges()) {
      auto snode = e.sink.get_master_node();
      if (!is_partitionable(snode)) {
        continue;
      }
      auto it = node2id.find(snode);
      if (it == node2id.end()) {
        continue;
      }
      if (curr_id != it->second) {
        id2neighs[curr_id].insert(it->second);
        num_edges++;
      }
    }
    for (const auto& e : curr_node.inp_edges()) {
      auto dnode = e.driver.get_master_node();
      if (!is_partitionable(dnode)) {
        continue;
      }
      auto it = node2id.find(dnode);
      if (it == node2id.end()) {
        continue;
      }
      if (curr_id != it->second) {
        id2neighs[curr_id].insert(it->second);
      }
    }
  }
}

void Color_mincut::lg_to_metis(hhds::Graph* g, const std::string& metis_path) {
  gather_ids(g);
  gather_neighs(g);

  std::ofstream out_file(metis_path);
  out_file << std::format(" {} {} 0\n", num_nodes, num_edges);
  for (int i = 0; i < num_nodes; i++) {
    auto it = id2neighs.find(i);
    if (it != id2neighs.end()) {
      for (auto neigh : it->second) {
        out_file << std::format(" {}", neigh);
      }
    }
    out_file << "\n";
  }
  out_file.close();
}

// Borrowed from VieCut/app/mincut.cpp (file-based, decoupled from the graph).
void Color_mincut::viecut_cut(const std::string& metis_path, const std::string& out_path) {
  size_t num_iterations = static_cast<size_t>(iters <= 0 ? 1 : iters);
  auto   cfg            = configuration::getConfig();

  cfg->graph_filename          = metis_path;
  cfg->algorithm               = alg;
  cfg->output_path             = out_path;
  cfg->save_cut                = true;
  cfg->find_lowest_conductance = true;
  cfg->seed                    = seed;
  if (cfg->find_lowest_conductance) {
    cfg->find_most_balanced_cut = true;
  }

  GraphPtr G = graph_io::readGraphWeighted<mutable_graph>(cfg->graph_filename);

  for (size_t i = 0; i < num_iterations; ++i) {
    random_functions::setSeed(cfg->seed);
    auto mc      = selectMincutAlgorithm<GraphPtr>(cfg->algorithm);
    cfg->threads = 1;
    mc->perform_minimum_cut(G);
    if (!cfg->output_path.empty() && cfg->save_cut) {
      graph_io::writeCut(G, cfg->output_path);
    }
  }
}

void Color_mincut::viecut_label(const std::string& result_path) {
  struct stat buff{};
  if (stat(result_path.c_str(), &buff) != 0) {
    return;  // no result file (e.g. trivial graph) -> leave uncolored
  }
  std::ifstream in_file(result_path);
  std::string   one_line;
  int           line_tracker = 0;
  if (in_file.is_open()) {
    while (std::getline(in_file, one_line)) {
      if (one_line.empty()) {
        continue;
      }
      auto it = id2node.find(line_tracker++);
      if (it == id2node.end()) {
        continue;
      }
      auto t_col           = str_tools::to_i(one_line);
      node2color[it->second] = (t_col == NO_COLOR) ? (t_col + 1) : t_col;  // +1 keeps 0 out of the coloring
    }
  }
  in_file.close();
}

void Color_mincut::label(hhds::Graph* g) {
  node2id.clear();
  id2node.clear();
  id2neighs.clear();
  node2color.clear();
  num_nodes = 0;
  num_edges = 0;

  namespace fs       = std::filesystem;
  auto        tmpdir = fs::temp_directory_path();
  std::string base   = std::string{g->get_name()};
  std::string metis  = (tmpdir / (base + ".mincut.metis")).string();
  std::string result = (tmpdir / (base + ".mincut.result")).string();

  lg_to_metis(g, metis);
  viecut_cut(metis, result);
  viecut_label(result);

  int n_colors = apply_coloring(g, node2color, opts);
  if (opts.verbose) {
    std::print("[color.mincut] {} -> {} cut regions\n", g->get_name(), n_colors);
  }
}

}  // namespace livehd::color

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_mincut.hpp"

#include "algorithms/global_mincut/algorithms.h"
#include "algorithms/global_mincut/minimum_cut.h"
#include "common/configuration.h"
#include "common/definitions.h"
#include "data_structure/graph_access.h"
#include "data_structure/mutable_graph.h"
#include "io/graph_io.h"
#include "tools/random_functions.h"
#include "tools/string.h"
#include "tools/timer.h"

//#define M_DEBUG 1

// typedef graph_access graph_type;
typedef std::shared_ptr<mutable_graph> GraphPtr;

/* * * * * * * * *
 * Constructor, initialize all the counters
 * * * * * * * * */
Label_mincut::Label_mincut(bool _v, bool _h, int _i, int _s, std::string_view _a)
    : verbose(_v), hier(_h), iters(_i), seed(_s), alg(_a) {
  node_id     = 0;
  num_nodes   = 0;
  num_edges   = 0;
  base_path   = "lgdb/";          // assuming things are run from livehd/
  metis_name  = "mincut.metis";   // file name for metis graph file
  result_name = "mincut.result";  // where the output goes
}

/* * * * * * * * *
 * This function will populate id2node and node2id
 * * * * * * * * */
void Label_mincut::gather_ids(Lgraph *g) {
  node_id = 0;                       // ensure reset
  for (auto n : g->forward(hier)) {  // forward iteration for order
    if (n.get_type_op() == Ntype_op::IO)
      continue;
    if (n.get_type_op() == Ntype_op::Const)
      continue;
    node2id[n.get_compact()] = node_id;
    id2node[node_id]         = n.get_compact();
    node_id++;
  }
  num_nodes = node_id;
}

/* * * * * * * * *
 * This function will populate id2neighs
 * * * * * * * * */
void Label_mincut::gather_neighs(Lgraph *g) {
  for (auto &it : id2node) {
    auto curr_id   = it.first;
    auto curr_node = it.second;

    Node tmp_n(g, curr_node);

    if (tmp_n.get_type_op() == Ntype_op::Const)
      continue;
    // gather the sinks
    for (auto &e : tmp_n.out_edges()) {
      auto spin        = e.sink;
      auto dpin        = e.driver;
      auto snode       = spin.get_node();
      auto dnode       = dpin.get_node();
      auto outgoing_id = node2id[snode.get_compact()];
      auto this_id     = node2id[dnode.get_compact()];

      if (snode.get_type_op() == Ntype_op::Const)
        continue;
      if (snode.get_type_op() != Ntype_op::IO) {
        if ((curr_id != outgoing_id) && (curr_id == this_id)) {
          id2neighs[curr_id].insert(outgoing_id);
          num_edges++;
        }
      }
    }

    // gather the drivers
    for (auto &e : tmp_n.inp_edges()) {
      auto spin        = e.sink;
      auto dpin        = e.driver;
      auto snode       = spin.get_node();
      auto dnode       = dpin.get_node();
      auto incoming_id = node2id[dnode.get_compact()];
      auto this_id     = node2id[snode.get_compact()];

      if (dnode.get_type_op() == Ntype_op::Const)
        continue;
      if (dnode.get_type_op() != Ntype_op::IO) {
        if ((curr_id != incoming_id) && (curr_id == this_id)) {
          id2neighs[curr_id].insert(incoming_id);
        }
      }
    }
  }  // END of for loop going through all nodes
}

/* * * * * * * * *
 * This function generates a metis graph file from Lgraph
 *   creates metis graph file with at base_path + metis_name
 * * * * * * * * */
void Label_mincut::lg_to_metis(Lgraph *g) {
  gather_ids(g);
  gather_neighs(g);

  std::ofstream out_file(base_path + metis_name);  // file create
  out_file << fmt::format(" {} {} 0\n", num_nodes, num_edges);

  for (int i = 0; i < num_nodes; i++) {
    auto neighs = id2neighs[i];
    for (auto &it : neighs) {
      out_file << fmt::format(" {}", it);
    }
    out_file << "\n";
  }

  out_file.close();
}

/* * * * * * * * *
 * All code borrowed from VieCut/app/mincut.cpp
 * * * * * * * * */
void Label_mincut::viecut_cut(std::string inp_metis_path, std::string out_path) {
  size_t num_iterations = iters;
  // size_t num_iterations = 3;
  auto cfg = configuration::getConfig();

  cfg->graph_filename = inp_metis_path;
  cfg->algorithm      = std::string(alg);
  // cfg->algorithm = "cactus";
  cfg->output_path             = out_path;
  cfg->save_cut                = true;
  cfg->find_lowest_conductance = true;
  cfg->seed                    = seed;
  // cfg->seed = 12;

  if (cfg->find_lowest_conductance)
    cfg->find_most_balanced_cut = true;

  std::vector<int> numthreads;
  timer            t;
  timer            tdegs;

  GraphPtr G = graph_io::readGraphWeighted<mutable_graph>(cfg->graph_filename);

  if (numthreads.empty())
    numthreads.emplace_back(1);

  for (size_t i = 0; i < num_iterations; ++i) {
    for (int numthread : numthreads) {
      random_functions::setSeed(cfg->seed);

      auto mc      = selectMincutAlgorithm<GraphPtr>(cfg->algorithm);
      cfg->threads = numthread;

      t.restart();
#ifdef M_DEBUG
      EdgeWeight cut;
      // number of specified edges mismatch
      // due to discarding of self loops
      cut = mc->perform_minimum_cut(G);
#else
      mc->perform_minimum_cut(G);
#endif

      if (cfg->output_path != "") {
        if (cfg->save_cut) {
          graph_io::writeCut(G, cfg->output_path);
        }
      }

      std::string graphname  = string::basename(cfg->graph_filename);
      std::string algprint   = cfg->algorithm;
      std::string queue_type = cfg->pq;

      if (cfg->disable_limiting) {
        algprint += "-unlimited";
      }

#ifdef M_DEBUG
      NodeID n = G->number_of_nodes();
      EdgeID m = G->number_of_edges();
      std::cout << "RESULT:\n algo=" << algprint << "\n queue=" << queue_type << "\n graph=" << graphname
                << "\n time=" << t.elapsed() << "\n cut=" << cut << "\n n=" << n << "\n m=" << m / 2 << "\n processes=" << numthread
                << "\n edge_select=" << cfg->edge_selection << "\n seed=" << cfg->seed << std::endl;
#endif
    }
  }
}

/* * * * * * * * *
 * Labels lg based on a result file generated from VieCut and fills id2color
 * * * * * * * * */
void Label_mincut::viecut_label(std::string result_path) {
  struct stat buff;
  if (stat(result_path.c_str(), &buff) != 0) {
    fmt::print("viecut result file, {}, does no exist inside {}\n", result_name, base_path);
    return;
  }
  std::ifstream in_file(result_path);
  std::string   one_line;
  int           line_tracker = 0;
  if (in_file.is_open()) {
    while (in_file) {
      std::getline(in_file, one_line);
      if (one_line.size() == 0)
        continue;
      auto curr_node = id2node[line_tracker++];
      // Add one to avoid 0 as a color
      auto t_col            = str_tools::to_i(one_line);
      node2color[curr_node] = (t_col == NO_COLOR) ? (t_col + 1) : (t_col);
    }
  }
  in_file.close();
}

/* * * * * * * * *
 * Dumps all info about mincut label pass structures
 * * * * * * * * */
void Label_mincut::dump(Lgraph *g) {
  fmt::print("---- Label MinCut Dump ----\n");
  fmt::print("Num Nodes: {}, Num Edges: {}\n", num_nodes, num_edges);
  fmt::print("=== id2node ===\n");
  for (auto &it : id2node) {
    auto curr_id = it.first;
    Node tmp_n(g, it.second);
    auto curr_node = tmp_n.debug_name();
    fmt::print("  ID: {}, Node: {}\n", curr_id, curr_node);
  }

  fmt::print("=== node2id ===\n");
  for (auto &it : node2id) {
    auto curr_id = it.second;
    Node tmp_n(g, it.first);
    auto curr_node = tmp_n.debug_name();
    fmt::print("  Node: {}, ID: {}\n", curr_id, curr_node);
  }

  fmt::print("=== id2neighs ===\n");
  for (auto &it : id2neighs) {
    auto curr_id = it.first;
    Node tmp_n(g, id2node[curr_id]);
    auto curr_node = tmp_n.debug_name();
    auto neighbors = it.second;  // IntSet
    fmt::print("  Node: {}, ID: {}\n", curr_node, curr_id);
    for (auto &n : neighbors) {
      fmt::print("  {}", n);
    }
    fmt::print("\n");
  }

  fmt::print("=== node2color ===\n");
  for (auto &it : node2color) {
    Node tmp_n(g, it.first);
    auto curr_col = it.second;
    fmt::print("  Node: {}, Color: {}\n", tmp_n.debug_name(), curr_col);
  }

  fmt::print("---- fin ----\n");
}

/* * * * * * * * *
 * Where the actual labeling happens
 * * * * * * * * */
void Label_mincut::label(Lgraph *g) {
  if (hier) {
    g->each_hier_unique_sub_bottom_up([](Lgraph *lg) { lg->ref_node_color_map()->clear(); });
  }
  g->ref_node_color_map()->clear();

  std::string metis_location  = base_path + metis_name;
  std::string result_location = base_path + result_name;
  lg_to_metis(g);
  viecut_cut(metis_location, result_location);
  viecut_label(result_location);

  for (auto n : g->fast(hier)) {
    auto nc = n.get_compact();
    if (node2color.contains(nc)) {
      n.set_color(node2color[nc]);
    } else {
      n.set_color(NO_COLOR);
    }
  }

  if (verbose)
    dump(g);
}

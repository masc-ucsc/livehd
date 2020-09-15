#include "pass_fplan.hpp"

#include <string>  // for std::to_string

#include "hier_tree.hpp"
#include "i_resolve_header.hpp"
#include "profile_time.hpp"
#include "thread_pool.hpp"

void setup_pass_fplan() { Pass_fplan::setup(); }

constexpr unsigned int def_min_tree_nodes      = 1;
constexpr unsigned int def_num_collapsed_hiers = 6;
constexpr double       def_min_tree_area       = 6.0;
constexpr unsigned int def_max_pats            = 15;
constexpr unsigned int def_max_optimal_nodes   = 15;

void Pass_fplan::setup() {
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan::pass);
  m.add_label_optional("num_collapsed_hiers",
                       "number of (partially) collapsed hierarchies to generate",
                       std::to_string(def_num_collapsed_hiers));
  m.add_label_optional("min_tree_nodes",
                       "minimum number of components to trigger analysis of a subtree",
                       std::to_string(def_min_tree_nodes));
  m.add_label_optional("min_tree_area",
                       "area (mm^2) threshold below which nodes will be collapsed together",
                       std::to_string(def_min_tree_area));
  m.add_label_optional("max_pats", "maximum number of intermediate patterns to keep", std::to_string(def_max_pats));
  m.add_label_optional("max_optimal_nodes",
                       "crossover point between exhaustive branch-and-bound and simulated annealing",
                       std::to_string(def_max_optimal_nodes));

  register_pass(m);

  auto dhm
      = Eprp_method("pass.fplan.dumphier", "dump a DOT file representing the recreated hierarchy", &Pass_fplan_dump::dump_hier);
  register_pass(dhm);

  auto dtm = Eprp_method("pass.fplan.dumptree", "dump a DOT file representing the hierarchy tree", &Pass_fplan_dump::dump_tree);
  register_pass(dtm);
  m.add_label_optional("min_tree_count", "minimum number of components to trigger analysis of a subtree", "1");
}

#include <thread>

void Pass_fplan::pass(Eprp_var& var) {
  auto t       = profile_time::timer();
  auto whole_t = profile_time::timer();

  /*
  Thread_pool tp;
  if (tp.size() > 16) {
    // high thread count
    fmt::print("\ncomfortable sheets detected!\n");
  }
  */

  fmt::print("generating floorplan...\n");
  whole_t.start();
  Pass_fplan p(var);

  fmt::print("  making floorplan graph...\n");
  t.start();
  Hier_tree h(var);
  fmt::print("  done ({} ms).\n", t.time());

  unsigned int mtn = std::stoi(var.get("min_tree_nodes").data());
  fmt::print("  discovering hierarchy (min nodes: {})...\n", mtn);
  t.start();
  h.discover_hierarchy(mtn);

  fmt::print("  done ({} ms).\n", t.time());

  const double       mta = std::stod(var.get("min_tree_area").data());
  const unsigned int nch = std::stoi(var.get("num_collapsed_hiers").data());

  h.make_hierarchies(nch);

  for (size_t i = 0; i < nch; i++) {
    fmt::print("  generating collapsed hierarchy (hier {}/{}, min area: {})...", i, nch, i * mta);
    t.start();
    h.collapse(i, i * mta);
    fmt::print("done ({} ms).\n", t.time());
  }

  const unsigned int mp = std::stoi(var.get("max_pats").data());

  fmt::print("  discovering regularity (max patterns: {})...\n", mp);
  t.start();
  h.discover_regularity(0, mp);

  fmt::print("  done ({} ms).\n", t.time());

  h.dump_patterns();

  h.make_leaf_dims();

  const unsigned int mon = std::stoi(var.get("max_optimal_nodes").data());
  fmt::print("  constructing boundary curve (max nodes: {})...\n", mon);
  t.start();
  h.construct_bounds(mon);
  fmt::print("  done ({} ms).\n", t.time());

  h.make_dag();

  fmt::print("  constructing floorplans...");
  t.start();
  h.construct_floorplans();
  fmt::print("done ({} ms).\n", t.time());

  fmt::print("floorplan generated ({} ms).\n\n", whole_t.time());
}

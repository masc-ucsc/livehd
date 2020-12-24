#include "pass_fplan.hpp"

#include <string>  // for std::to_string

#include "profile_time.hpp"
//#include "thread_pool.hpp"

void setup_pass_fplan() { Pass_fplan::setup(); }

// generate more information for debugging stuff
constexpr unsigned int def_min_tree_nodes      = 1;
constexpr unsigned int def_num_collapsed_hiers = 3;  // patterns only get found up to collapsed hier 3 in hier_test
constexpr double       def_min_tree_area       = 0.0;
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
}

Pass_fplan::Pass_fplan(const Eprp_var &var) : Pass("pass.fplan", var) {
  if (var.lgs.size() == 0) {
    throw std::invalid_argument("no lgraphs!");
  }

  if (var.lgs.size() > 1) {
    throw std::invalid_argument("more than one lgraph!");
  }

  root_lg = var.lgs[0];
}

void Pass_fplan::pass(Eprp_var& var) {
  auto t       = profile_time::timer();
  auto whole_t = profile_time::timer();

  fmt::print("generating floorplan...\n");
  whole_t.start();
  Pass_fplan p(var);

  p.pretty_dump(p.root_lg, 0);

  const double       mta = std::stod(var.get("min_tree_area").data());
  const unsigned int nch = std::stoi(var.get("num_collapsed_hiers").data());
  fmt::print("  collapsing {} hierarchies (min area: {})...\n", nch, mta);
  t.start();
  p.collapse_hier(mta);
  fmt::print("  done ({} ms).\n", t.time());

  const unsigned int mp = std::stoi(var.get("max_pats").data());
  fmt::print("  analyzing {} hierarchies for patterns (maximum patterns: {})...\n", nch, mp);
  t.start();
  p.discover_reg(mp);
  fmt::print("  done ({} ms).\n", t.time());

  fmt::print("floorplans generated ({} ms).\n\n", whole_t.time());
}

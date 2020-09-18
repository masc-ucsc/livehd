#include <random>
#include <sstream>
#include <unordered_map>

#include "dag.hpp"
#include "fmt/format.h"
#include "hier_tree.hpp"
#include "profile_time.hpp"

void Hier_tree::construct_bounds(const size_t dag_id, const unsigned int optimal_thresh) {
  auto& d = dags[dag_id];

  // set dimensions of leaf nodes in the dag
  std::function<void(const Dag::pdag)> assign_leaf_dims = [&](const Dag::pdag pd) {
    if (pd->is_leaf()) {
      static constexpr double               max_aspect_ratio = 1.0 / 5.0;
      static std::default_random_engine     gen;
      static std::uniform_real_distribution dist(max_aspect_ratio, 1.0 - max_aspect_ratio);

      double width_factor = dist(gen);
      pd->width           = pd->area * width_factor;
      pd->height          = pd->area * (1.0 - width_factor);
    }

    for (auto child : pd->children) {
      assign_leaf_dims(child);
    }
  };

  assign_leaf_dims(d.root);

  std::function<void(const Dag::pdag)> floorplan_patterns = [&](const Dag::pdag pd) {
    // avoid floorplanning leaves (no point, since they only contain a single generic node)
    if (pd->is_leaf()) {
      return;
    }

    // make sure all children are floorplanned before we start floorplanning ourselves
    for (auto child : pd->children) {
      floorplan_patterns(child);
    }

    // avoid floorplanning the entire design for now
    if (pd->is_root()) {
      return;
    }

    std::stringstream outstr;

    // only record bounding box ("bounding curve") information right now
    // if the pattern is small, we can afford to use an exhaustive approach to finding floorplans.
    floorplan_dag_node(pd, outstr, optimal_thresh);

    double pat_width, pat_height;
    outstr >> pat_width;
    outstr >> pat_height;

    pd->width  = pat_width;
    pd->height = pat_height;
  };

  floorplan_patterns(d.root);
}

void Hier_tree::construct_bounds(const unsigned int optimal_thresh) {
  for (size_t i = 0; i < dags.size(); i++) {
    construct_bounds(i, optimal_thresh);
  }
}

void Hier_tree::floorplan_dag_node(const Dag::pdag pd, std::stringstream& outstr, const unsigned int ot) {
  std::stringstream instr;

  I(pd->children.size() > 0);

  instr << fmt::format("{}\n", pd->children.size());
  
  for (size_t child_i = 0; child_i < pd->children.size(); child_i++) {
    auto child = pd->children[child_i];
    I(child->width != 0);
    I(child->height != 0);
    // account for the fact that there may be more than one children of a given type
    for (size_t i = 0; i < pd->child_edge_count[child_i]; i++) {
      // keep a few digits of precision to ourselves
      instr << fmt::format("{:.12f} {:.12f}\n", child->width, child->height);
    }
  }

  if (bound_verbose) {
    fmt::print("\ninput string stream:\n{}", instr.str());
  }

  invoke_blobb(instr, outstr, pd->children.size() > ot);
}
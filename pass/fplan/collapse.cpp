#include "pass_fplan.hpp"

void Pass_fplan::collapse_hier(double area_thresh) { collapse_hier_rec(area_thresh, root_lg); }

void Pass_fplan::mark_hier_rec(LGraph* lg) {
  cli.area[lg] = true;

  lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
    LGraph* sub_lg = LGraph::open(path, lgid);
    mark_hier_rec(sub_lg);
  });
}

void Pass_fplan::collapse_hier_rec(double area_thresh, LGraph* lg) {
  unsigned int num_nodes = 0;
  for (auto node : lg->fast()) {
    num_nodes++;
  }

  if (num_nodes > area_thresh) {
    lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
      LGraph* sub_lg = LGraph::open(path, lgid);
      collapse_hier_rec(area_thresh, sub_lg);
    });
  } else {
    lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
      LGraph* sub_lg = LGraph::open(path, lgid);
      mark_hier_rec(sub_lg);
    });
  }
}

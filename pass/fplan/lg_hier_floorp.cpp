//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lg_hier_floorp.hpp"

#include <functional>

#include "lgedgeiter.hpp"
#include "node_type_area.hpp"

Lg_hier_floorp::Lg_hier_floorp(Node_tree&& nt_arg) : Lhd_floorplanner(std::move(nt_arg)) {}

// can't return geogLayouts since ArchFP calls totalArea on components, which breaks if geogLayouts have no children (returns 0 for
// area)
geogLayout* Lg_hier_floorp::load_lg_modules(LGraph* lg) {
  const std::string_view path = root_lg->get_path();

  // count and load subnodes only
  absl::flat_hash_map<LGraph*, unsigned int> sub_lg_count;
  lg->each_sub_fast([&](Node& n, Lg_type_id lgid) {
    (void)n;
    LGraph* sub_lg = LGraph::open(path, lgid);
    sub_lg_count[sub_lg]++;
  });

  geogLayout* l = new geogLayout(sub_lg_count.size());
  l->setName(lg->get_name().data());

  if (sub_lg_count.empty()) {
    l->setType(Ntype_op::Const);

    Ntype_area narea(path);

    float area = 0.0;
    for (auto n : lg->fast()) {
      Ntype_op op = n.get_type_op();
      if (Ntype::is_synthesizable(op)) {
        auto d = narea.get_dim(op);
        area += d.area;
      }
    }

    l->setArea(area);
    return l;
  }
  
  l->setType(Ntype_op::Sub);

  for (auto pair : sub_lg_count) {
    const auto& sub_lg = pair.first;

    if (debug_print) {
      fmt::print("generating subcomponent {}\n", sub_lg->get_name());
    }

    geogLayout* subl = load_lg_modules(sub_lg);

    if (debug_print) {
      fmt::print("done, area {:.3f}.\n", subl->getArea());
    }

    if (debug_print) {
      fmt::print("adding {} of subcomponent {} to cluster of lg {}\n", sub_lg_count[sub_lg], sub_lg->get_name(), lg->get_name());
    }

    unsigned int count = sub_lg_count[sub_lg];
    if (subl->getType() == Ntype_op::Const) {
      l->addComponentCluster(subl->getName(),
                             count,
                             subl->getArea(),
                             8.0,
                             1.0,
                             randomHint(count));
    } else {
      l->addComponent(subl, count, randomHint(count));
    }
  }

  return l;
}

void Lg_hier_floorp::load() {
  root_layout = load_lg_modules(root_lg);
}
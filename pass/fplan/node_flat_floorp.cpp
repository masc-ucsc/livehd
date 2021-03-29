//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "node_flat_floorp.hpp"

#include "AnnLayout.hpp"
#include "node_type_area.hpp"

Node_flat_floorp::Node_flat_floorp(Node_tree&& nt_arg) : Lhd_floorplanner(std::move(nt_arg)) {}

void Node_flat_floorp::load() {
  unsigned int count = 0;  // avoid tons of memory reallocations when creating nodes
  for (auto n : nt.get_root_lg()->fast(true)) {
    (void)n;
    count++;
  }

  root_layout = new annLayout(count);

  Ntype_area narea(nt.get_root_lg()->get_path());

  for (auto n : nt.get_root_lg()->fast(true)) {
    Ntype_op op = n.get_type_op();

    std::string_view name;
    if (n.has_name()) {
      name = n.get_name();
    } else {
      name = Ntype::get_name(op);
    }

    if (Ntype::is_synthesizable(op)) {
      if (debug_print) {
        fmt::print("adding component {} to root\n", name);
      }

      if (!(narea.has_dim(op))) {
        std::string errstr = "node type ";
        errstr.append(Ntype::get_name(op));
        errstr.append(" has no area information!");
        throw std::runtime_error(errstr);
      }

      auto d = narea.get_dim(op);

      root_layout->addComponentCluster(op, 1, d.area, d.max_aspect, d.min_aspect);
    }
  }
}

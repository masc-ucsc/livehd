#include "node_flat_floorp.hpp"

#include "node_type_area.hpp"

void Node_flat_floorp::load(LGraph* root, const std::string_view lgdb_path) {
  (void)lgdb_path;

  Ntype_area na;

  for (auto n : root->fast(true)) {
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

      if (!(na.has_dim(op))) {
        std::string errstr = "node type ";
        errstr.append(Ntype::get_name(op));
        errstr.append(" has no area information!");
        throw std::runtime_error(errstr);
      }

      auto d = na.get_dim(op);

      root_layout->addComponentCluster(name.data(), 1, d.area, d.max_aspect, d.min_aspect, Center);
    }
  }
}
#include "node_flat_floorp.hpp"

void Node_flat_floorp::load_lg(LGraph* root, const std::string_view lgdb_path) {
  (void)lgdb_path;

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

      // TODO: node area?
      root_layout->addComponentCluster(name.data(), 1, 1.0, 8.0, 1.0, Center);
    }
  }
}
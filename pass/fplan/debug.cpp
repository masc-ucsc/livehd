#include "pass_fplan.hpp"

void Pass_fplan::pretty_dump(LGraph *g, int indent) {
  int i_num  = 0;
  int i_bits = 0;
  g->each_graph_input([this, &i_num, &i_bits](const Node_pin &pin) {
    i_num++;
    i_bits += pin.get_bits();
  });

  int o_num  = 0;
  int o_bits = 0;
  g->each_graph_output([this, &o_num, &o_bits](const Node_pin &pin) {
    o_num++;
    o_bits += pin.get_bits();
  });

  int n_wire      = 0;
  int n_wire_bits = 0;
  int n_nodes     = 0;
  for (auto node : g->fast()) {
    n_nodes++;
    for (const auto &edge : node.out_edges()) {
      n_wire++;
      n_wire_bits += edge.driver.get_bits();
    }
  }

  std::string space;
  for (int i = 0; i < indent; i++) space.append("  ");

  fmt::print("{}module {} (lgid {}) : inputs {} bits {} : outputs {} bits {} : nodes {} : submodules {} : wire {} bits {}\n",
             space,
             g->get_name(),
             g->get_lgid(),
             i_num,
             i_bits,
             o_num,
             o_bits,
             n_nodes,
             g->get_down_nodes_map().size(),
             n_wire,
             n_wire_bits);

  g->each_sub_fast([this, indent, space](Node &node, Lg_type_id lgid) {
    (void)node;

    LGraph *sub_lg = LGraph::open(path, lgid);

    if (!sub_lg) {
      return;
    }

    if (cli.area[sub_lg]) {
      return;
    }

    if (sub_lg->is_empty()) {
      int n_inp = 0;
      int n_out = 0;
      for (auto io_pin : sub_lg->get_self_sub_node().get_io_pins()) {
        if (io_pin->is_input())
          n_inp++;
        if (io_pin->is_output())
          n_out++;
      }
      fmt::print("{}  module {} BBOX : inputs {} outputs {}\n", space, sub_lg->get_name(), n_inp, n_out);

      return;  // No blackboxes
    }

    pretty_dump(sub_lg, indent + 1);
  });
}
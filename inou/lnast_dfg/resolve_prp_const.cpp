#include <string_view>
#include "inou_lnast_dfg.hpp"

Node Inou_lnast_dfg::resolve_constant(LGraph *g, const Lconst &val) {
  auto node = g->create_node_const(val);
  return node;
}



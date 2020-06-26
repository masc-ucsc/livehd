#include <string_view>
#include "inou_lnast_dfg.hpp"

Node Inou_lnast_dfg::resolve_constant(LGraph *g, const Lconst &val) {

  auto node = g->create_node_const(val);
  node.setup_driver_pin().ref_bitwidth()->e.set_const(val); // FIXME: I do not think that we need this
  if (!node.has_cfcnt()) 
    node.set_cfcnt(++cfcnt);

  return node;
}



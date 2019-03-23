//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph.hpp"

#include "node_pin.hpp"

#if 0
Node_pin Node_pin::get_out_pin(const Edge_raw *edge_raw) {
  if (edge_raw->is_input())
    return Node_pin(edge_raw->get_idx(), edge_raw->get_inp_pid(), false);
  else
    return Node_pin(edge_raw->get_self_root_idx(), edge_raw->get_dst_pid(), false);
}

Node_pin Node_pin::get_inp_pin(const Edge_raw *edge_raw) {
  if (edge_raw->is_input())
    return Node_pin(edge_raw->get_self_root_idx(), edge_raw->get_dst_pid(), true);
  else
    return Node_pin(edge_raw->get_idx(), edge_raw->get_inp_pid(), true);
}
#endif

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "live_common.hpp"

namespace Live {

int resolve_bit(Lgraph *graph, Index_id idx, uint32_t current_bit, Port_ID pin, absl::flat_hash_set<uint32_t> &bits) {
  if (graph->node_type_get(idx).op == Pick_Op) {
    I(graph->get_bits(graph->get_node(idx).get_driver_pin()) >= current_bit);
    if (pin != 0) {
      return -1;  // do not propagate through this pid
    }
    Node_pin picked;
    Node_pin offset;
    for (auto &c : graph->inp_edges(idx)) {
      if (c.get_inp_pin().get_pid() == 0) {
        picked = c.get_out_pin();
      } else if (c.get_inp_pin().get_pid() == 1) {
        offset = c.get_out_pin();
      }
    }
    I(picked.is_valid());
    I(offset.is_valid());
    I(offset.get_node().get_type_op() == U32Const_Op);
    I(graph->node_value_get(offset) + current_bit <= graph->get_bits(picked));
    bits.insert(graph->node_value_get(offset) + current_bit);
    return 0;
  } else if (graph->node_type_get(idx).op == Join_Op) {
    std::map<Index_id, uint32_t> port_size;
    for (auto &c : graph->inp_edges(idx)) {
      port_size[c.get_inp_pin().get_pid()] = graph->get_bits(c.get_out_pin());
    }
    uint32_t offset = 0;
    int      last   = -1;
    for (auto &ps : port_size) {
      I(last + 1 == (int)ps.first);  // need to traverse in order
      last = ps.first;
      if (offset + ps.second > current_bit) {
        if (ps.first != pin) {
          return -1;  // do not propagate through this pid
        }
        I(current_bit >= offset);
        bits.insert(current_bit - offset);
        return 0;
      }
      offset += ps.second;
    }
    I(false);  // internal error in join
    return -1;
  } else if (graph->node_type_get(idx).op == Mux_Op) {
    if (pin == 2) {
      bits.insert(1);
    } else {
      bits.insert(current_bit);
    }
  } else if (graph->node_type_get(idx).op == ShiftRight_Op) {
    if (pin == 2) {
      bits.insert(0);
      return 0;
    }
    int      const_shift   = -1;
    Index_id relevant_port = 0;

    for (auto &c : graph->inp_edges(idx)) {
      if (c.get_inp_pin().get_pid() == 1 && graph->node_type_get(c.get_idx()).op == U32Const_Op) {
        const_shift = graph->node_value_get(c.get_idx());
      }
      if (c.get_inp_pin().get_pid() == pin) {
        relevant_port = c.get_idx();
      }
    }
    if (const_shift >= 0) {
      if (pin == 1) {
        bits.insert(0);
      } else if (pin == 0) {
        bits.insert(current_bit + const_shift);
      } else {
        I(false);
      }
      return 0;
    } else {
      for (int bit = 0; bit < graph->get_bits(relevant_port); bit++) {
        bits.insert(bit);
      }
    }
    return 0;
  } else if (graph->node_type_get(idx).op == ShiftLeft_Op) {
    int      const_shift   = -1;
    Index_id relevant_port = 0;

    for (auto &c : graph->inp_edges(idx)) {
      if (c.get_inp_pin().get_pid() == 1 && graph->node_type_get(c.get_idx()).op == U32Const_Op) {
        const_shift = graph->node_value_get(c.get_idx());
      }
      if (c.get_inp_pin().get_pid() == pin) {
        relevant_port = c.get_idx();
      }
    }
    if (const_shift >= 0) {
      if (pin == 1) {
        bits.insert(0);
      } else if (pin == 0) {
        I(current_bit >= static_cast<uint32_t>(const_shift));
        bits.insert(current_bit - const_shift);
      } else {
        I(false);
      }
      return 0;
    } else {
      for (int bit = 0; bit < graph->get_bits(relevant_port); bit++) {
        bits.insert(bit);
      }
    }
    return 0;
  } else if (graph->node_type_get(idx).op == Equals_Op || graph->node_type_get(idx).op == GreaterThan_Op
             || graph->node_type_get(idx).op == GreaterEqualThan_Op || graph->node_type_get(idx).op == LessEqualThan_Op
             || graph->node_type_get(idx).op == LessThan_Op || graph->node_type_get(idx).op == TechMap_Op) {
    Index_id relevant_port = 0;

    for (auto &c : graph->inp_edges(idx)) {
      if (c.get_inp_pin().get_pid() == pin) {
        relevant_port = c.get_idx();
        break;
      }
    }
    for (int bit = 0; bit < graph->get_bits(relevant_port); bit++) {
      bits.insert(bit);
    }
  } else {
    bits.insert(current_bit);
  }
  return 0;
}

// resolves which bits are dependencies of the current bit based on node type
// when propagating backwards
int resolve_bit_fwd(Lgraph *graph, Index_id idx, uint32_t current_bit, Port_ID pin, absl::flat_hash_set<uint32_t> &bits) {
  if (graph->node_type_get(idx).op == Pick_Op) {
    if (pin != 0) {
      return -1;  // do not propagate through this pid
    }
    Index_id picked = 0, offset = 0;
    for (auto &c : graph->inp_edges(idx)) {
      if (c.get_inp_pin().get_pid() == 0) {
        picked = c.get_out_pin().get_idx();
      } else if (c.get_inp_pin().get_pid() == 1) {
        offset = graph->get_node(c.get_out_pin()).get_nid();
      }
    }
    I(picked);
    I(offset);

    if (current_bit < graph->node_value_get(offset)) {
      // ignore this bit, not used by pick
      return -1;
    }

    bits.insert(current_bit - graph->node_value_get(offset));
    return 0;  // graph->node_value_get(offset) + current_bit;
  } else if (graph->node_type_get(idx).op == Join_Op) {
    std::map<Index_id, uint32_t> port_size;
    for (auto &c : graph->inp_edges(idx)) {
      if (c.get_inp_pin().get_pid() >= pin) {
        continue;
      }
      port_size[c.get_inp_pin().get_pid()] = graph->get_bits(c.get_out_pin());
    }
    uint32_t offset = 0;
    int      last   = -1;
    for (auto &ps : port_size) {
      I(last + 1 == static_cast<int>(ps.first));  // need to traverse in order
      last = ps.first;
      offset += ps.second;
    }
    bits.insert(current_bit + offset);
    return 0;
  } else if (graph->node_type_get(idx).op == Mux_Op) {
    if (pin == 2) {
      // select affects all output bits
      for (int bit = 0; bit < graph->get_bits(idx); bit++) {
        bits.insert(bit);
      }
      return 0;
    } else {
      bits.insert(current_bit);
      return 0;
    }
  } else if (graph->node_type_get(idx).op == ShiftRight_Op) {
    if (pin > 0) {
      // shift amount and sign extension can affect all output bits
      for (int bit = 0; bit < graph->get_bits(idx); bit++) {
        bits.insert(bit);
      }
      return 0;
    }
    int  const_shift = -1;
    bool sign        = false;

    for (auto &c : graph->inp_edges(idx)) {
      if (c.get_inp_pin().get_pid() == 1 && graph->node_type_get(c.get_idx()).op == U32Const_Op) {
        const_shift = graph->node_value_get(c.get_idx());
      } else if (c.get_inp_pin().get_pid() == 2) {
        sign = (graph->node_value_get(c.get_idx()) == 2);
      }
    }
    if (const_shift >= 0 && (!sign || static_cast<int>(current_bit) != graph->get_bits(idx) - 1)) {
      // if there is sign extension, MSB affects all bits
      // bits lower than shift amount do no affect any bit
      if (current_bit >= static_cast<uint32_t>(const_shift)) {
        bits.insert(current_bit - const_shift);
      }
    } else {
      for (int bit = graph->get_bits(idx) - 1; bit >= 0; bit--) {
        // each bit can only affect lower bits
        bits.insert(bit);
      }
    }
    return 0;
  } else if (graph->node_type_get(idx).op == ShiftLeft_Op) {
    if (pin > 0) {
      // shift amount affects all output bits
      for (int bit = 0; bit < graph->get_bits(idx); bit++) {
        bits.insert(bit);
      }
      return 0;
    }

    int const_shift = -1;

    for (auto &c : graph->inp_edges(idx)) {
      if (c.get_inp_pin().get_pid() == 1 && graph->node_type_get(c.get_idx()).op == U32Const_Op) {
        const_shift = graph->node_value_get(c.get_idx());
      }
    }
    if (const_shift >= 0) {
      if (current_bit + const_shift < graph->get_bits(idx)) {
        bits.insert(current_bit + const_shift);
      }
      return 0;
    } else {
      for (int bit = current_bit; bit < graph->get_bits(idx); bit++) {
        bits.insert(bit);
      }
    }
    return 0;
  } else if (graph->node_type_get(idx).op == Equals_Op || graph->node_type_get(idx).op == GreaterThan_Op
             || graph->node_type_get(idx).op == GreaterEqualThan_Op || graph->node_type_get(idx).op == LessEqualThan_Op
             || graph->node_type_get(idx).op == LessThan_Op) {
    bits.insert(1);
  } else {
    bits.insert(current_bit);
  }
  return 0;
}
}  // namespace Live

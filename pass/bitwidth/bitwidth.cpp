//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_bitwidth.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "bitwidth.hpp"
#include "bitwidth_range.hpp"
#include "lbench.hpp"
#include "lgraph.hpp"

// Useful for debug
//#define PRESERVE_ATTR_NODE

Bitwidth::Bitwidth(bool _hier, int _max_iterations, BWMap &_bwmap) :  max_iterations(_max_iterations), hier(_hier), bwmap(_bwmap) {}

void Bitwidth::do_trans(LGraph *lg) {
  /* Lbench b("pass.bitwidth"); */
  bw_pass(lg);
}

void Bitwidth::process_const(Node &node) {
  auto dpin = node.get_driver_pin();
  auto it = bwmap.insert_or_assign(dpin.get_compact(), Bitwidth_range(node.get_type_const()));
  forward_adjust_dpin(dpin, it.first->second);
}

void Bitwidth::process_flop(Node &node) {
  I(node.is_sink_connected("din"));
  auto d_dpin = node.get_sink_pin("din").get_driver_pin();
  auto qpin = node.get_driver_pin();

  Lconst max_val;
  Lconst min_val;
  auto   it_d_dpin = bwmap.find(d_dpin.get_compact());
  auto   it_qpin   = bwmap.find(qpin.get_compact());
  if (it_d_dpin != bwmap.end()) {
    max_val = it_d_dpin->second.get_max();
    min_val = it_d_dpin->second.get_min();
    bwmap.insert_or_assign(node.get_driver_pin().get_compact(), Bitwidth_range(min_val, max_val));
    return;
  } else if (it_qpin != bwmap.end()) {  // At least propagate backward the width
    auto tmp_min  = Lconst(it_qpin->second.min);
    auto tmp_max  = Lconst(it_qpin->second.max);
    bwmap.insert_or_assign(d_dpin.get_compact(), Bitwidth_range(tmp_min, tmp_max));
    return;
  } else {
    debug_unconstrained_msg(node, d_dpin);
    not_finished = true;
    return;
  }
}


void Bitwidth::process_ror(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  

  Bitwidth_range bw;
  bw.set_sbits_range(1);
  bwmap.insert_or_assign(node.get_driver_pin().get_compact(), bw);
}

void Bitwidth::process_not(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling???

  Lconst max_val;
  Lconst min_val;
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_compact());
    if (it != bwmap.end()) {
      auto pmax = it->second.get_max().to_i(); //pmax = parent_max
      auto pmin = it->second.get_min().to_i();
      //calculate 1s'complemet value
      auto pmax_1scomp = ~pmax;
      auto pmin_1scomp = ~pmin;
      max_val = Lconst(std::max(pmax_1scomp, pmin_1scomp));
      min_val = Lconst(std::min(pmax_1scomp, pmin_1scomp));
    } else {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
  }

  bwmap.insert_or_assign(node.get_driver_pin().get_compact(), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_mux(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling???

  Lconst max_val;
  Lconst min_val;
  for (auto e : inp_edges) {
    if (e.sink.get_pid() == 0)
      continue;  // Skip select

    auto it = bwmap.find(e.driver.get_compact());
    if (it != bwmap.end()) {
      if (max_val < it->second.get_max())
        max_val = it->second.get_max();

      if (min_val > it->second.get_min())
        min_val = it->second.get_min();

    } else {
      // update as soon as possible, don't wait everyone ready, so you could break the flop-loop
      bwmap.insert_or_assign(node.get_driver_pin().get_compact(), Bitwidth_range(min_val, max_val));

      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
  }
  bwmap.insert_or_assign(node.get_driver_pin().get_compact(), Bitwidth_range(min_val, max_val));
}


void Bitwidth::process_shl(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);

  auto a_dpin = node.get_sink_pin("a").get_driver_pin();
  auto n_dpin = node.get_sink_pin("b").get_driver_pin();

  auto a_it = bwmap.find(a_dpin.get_compact());
  auto n_it = bwmap.find(n_dpin.get_compact());

  Bitwidth_range a_bw(0);
  if (a_it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  } else {
    a_bw = a_it->second;
  }

  Bitwidth_range n_bw(0);
  if (n_it == bwmap.end()) {
    debug_unconstrained_msg(node, n_dpin);
    not_finished = true;
    return;
  } else {
    n_bw = n_it->second;
  }

  if (a_bw.get_sbits() == 0 || n_bw.get_sbits() == 0) {
    debug_unconstrained_msg(node, a_bw.get_sbits() == 0 ? a_dpin : n_dpin);
    return;
  }

  auto max    = a_bw.get_max();
  auto min    = a_bw.get_min();
  auto amount = n_bw.get_max().to_i();
  auto max_val = Lconst(Lconst(max.get_raw_num()) << Lconst(amount));
  auto min_val = Lconst(Lconst(min.get_raw_num()) << Lconst(amount));
  Bitwidth_range bw(min_val, max_val);
  bwmap.insert_or_assign(node.get_driver_pin().get_compact(), bw);
}


void Bitwidth::process_sra(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);

  auto a_dpin = node.get_sink_pin("a").get_driver_pin();
  auto n_dpin = node.get_sink_pin("b").get_driver_pin();

  auto a_it = bwmap.find(a_dpin.get_compact());
  auto n_it = bwmap.find(n_dpin.get_compact());

  Bitwidth_range a_bw(0);
  if (a_it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  } else {
    a_bw = a_it->second;
  }

  Bitwidth_range n_bw(0);
  if (n_it == bwmap.end()) {
    debug_unconstrained_msg(node, n_dpin);
    not_finished = true;
    return;
  } else {
    n_bw = n_it->second;
  }

  if (a_bw.get_sbits() == 0 || n_bw.get_sbits() == 0) {
    debug_unconstrained_msg(node, a_bw.get_sbits() == 0 ? a_dpin : n_dpin);
    return;
  }

  // FIXME->sh: lgraph should only support arithmetic shift right now,
  //            how to express it using Lconst? for now the temporary solution
  //            is convert everything back to C++ integer and perform a division :(
  if (n_bw.get_min() > 0 && n_bw.get_min().is_i()) {
    auto max    = a_bw.get_max().to_i();
    auto min    = a_bw.get_min().to_i();
    auto amount = n_bw.get_min().to_i();
    max = floor(max/pow(2, amount));
    min = floor(min/pow(2, amount));

    auto min_val = Lconst(min);
    auto max_val = Lconst(max);
    Bitwidth_range bw(min_val, max_val);
    bwmap.insert_or_assign(node.get_driver_pin().get_compact(), bw);
  } else {
    bwmap.insert_or_assign(node.get_driver_pin().get_compact(), a_bw);
  }
}

void Bitwidth::process_sum(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling sum??? (delete)

  Lconst max_val;
  Lconst min_val;
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_compact());
    if (it != bwmap.end()) {
      if (e.sink.get_pin_name() == "A") {
        max_val = max_val + it->second.get_max();
        min_val = min_val + it->second.get_min();
      } else {
        max_val = max_val - it->second.get_min();
        min_val = min_val - it->second.get_max();
      }
    } else {
      debug_unconstrained_msg(node, e.driver);
      GI(hier, false, "Assert! bwmap entry should be ready at final bitwidth pass, entry:{}\n", e.driver.debug_name());

      not_finished = true;
      return;
    }
  }

  bwmap.insert_or_assign(node.get_driver_pin().get_compact(), Bitwidth_range(min_val, max_val));
}


void Bitwidth::process_mult(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling sum??? (delete)

  int max_val = 1;
  int min_val = 1;
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_compact());
    if (it != bwmap.end()) {
      max_val = max_val * it->second.get_max().to_i();
      min_val = min_val * it->second.get_min().to_i();
    } else {
      debug_unconstrained_msg(node, e.driver);
      GI(hier, false, "Assert! bwmap entry should be ready at final bitwidth pass, entry:{}\n", e.driver.debug_name());

      not_finished = true;
      return;
    }
  }
  Bitwidth_range(Lconst(min_val), Lconst(max_val)).dump();
  bwmap.insert_or_assign(node.get_driver_pin().get_compact(), Bitwidth_range(Lconst(min_val), Lconst(max_val)));
}

void Bitwidth::process_tposs(Node &node, XEdge_iterator &inp_edges) {
  Lconst max_val, min_val;

  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_compact());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }

    auto &bw = it->second;
    if (bw.is_always_positive()) {
      max_val = bw.get_max() ;
      min_val = 0;
    } else {
      max_val = (Lconst(1UL) << Lconst(bw.get_sbits())) - 1;
      min_val = 0;
      if (max_val.to_i() == 0 && min_val.to_i() == 0)
        I(false);
    }
  }

  bwmap.insert_or_assign(node.get_driver_pin().get_compact(), Bitwidth_range(min_val, max_val));
}


void Bitwidth::process_comparator(Node &node) {
  Bitwidth_range bw;
  bw.set_sbits_range(1);
  bwmap.insert_or_assign(node.get_driver_pin().get_compact(), bw);
}

void Bitwidth::process_assignment_or(Node &node, XEdge_iterator &inp_edges) {
  Lconst max_val, min_val;
  for (auto e : inp_edges) {
    auto   it = bwmap.find(e.driver.get_compact());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    } 
    max_val = it->second.get_max();
    min_val = it->second.get_min();

  }
  bwmap.insert_or_assign(node.get_driver_pin().get_compact(), Bitwidth_range(min_val, max_val));
  return;

}


void Bitwidth::process_logic_or_xor(Node &node, XEdge_iterator &inp_edges) {
  // handle normal Or_Op/Xor_Op
  I(inp_edges.size() > 1);
  Bits_t max_bits = 0;

  for (auto e : inp_edges) {
    auto   it   = bwmap.find(e.driver.get_compact());
    Bits_t bits = 0;
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    } 

    if (it->second.is_always_positive())
      bits = it->second.get_sbits() - 1;
    else
      bits = it->second.get_sbits();

    if (bits == 0) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }

    if (bits > max_bits)
      max_bits = bits;
  }

  auto max_val = (Lconst(1UL) << Lconst(max_bits-1)) - 1;
  auto min_val = Lconst(-1)-max_val;
  bwmap.insert_or_assign(node.get_driver_pin().get_compact(), Bitwidth_range(min_val, max_val)); // the max/min of AND MASK should be unsigned
}


void Bitwidth::process_logic_and(Node &node, XEdge_iterator &inp_edges) {
  // note: the goal is to get the min requiered bits in advance of final global BW and calculate the min range of (max, min)
  if (inp_edges.size() >= 1) {
    Bits_t min_sbits = Bits_max;
    bool one_always_positive = false;

    bool any_constrained = false;
    for (auto e : inp_edges) {
      auto   it  = bwmap.find(e.driver.get_compact());
      if (it == bwmap.end()) {
        continue;
      }

      Bits_t bw_sbits      = it->second.get_sbits();
      any_constrained      = true;
      one_always_positive |= it->second.is_always_positive();

      if (bw_sbits && bw_sbits < min_sbits)
        min_sbits = bw_sbits;
    }

    if (min_sbits == Bits_max || any_constrained == false) {
      fmt::print("BW-> and:{} does not have any constrained input\n", node.debug_name());
      not_finished = true;
      return;
    }

    auto  max_val = ((Lconst(1UL) << Lconst(min_sbits -1))) - 1;
    Lconst min_val;
    if (one_always_positive)
      min_val = Lconst(0); // note: this could avoid the (max, min) of AND to pollute the later Sum_op if the AND is really just a mask
    else
      min_val = Lconst(-1)-max_val;

    if (max_val == Lconst(0) && min_val == Lconst(0)) {//FIXME->sh: handle specially to keep signed-lgraph BW working properly
      max_val = (Lconst(1));
      /* min_val = (Lconst(-1)); */
    }
    
    bwmap.insert_or_assign(node.get_driver_pin().get_compact(), Bitwidth_range(min_val, max_val)); 

    for (auto e : inp_edges) {
      auto bw_bits = e.driver.get_bits();
      if (bw_bits)
        continue;  // only handle unconstrained inputs

      if (e.driver.get_num_edges() > 1) {
        must_perform_backward = true;
      } else if (bw_bits == 0 || bw_bits > min_sbits) {
        bwmap.insert_or_assign(e.driver.get_compact(), Bitwidth_range(min_val, max_val));
      }
    }
  }
}


Bitwidth::Attr Bitwidth::get_key_attr(std::string_view key) {
  if (key.substr(0, 7) == "__ubits")
    return Attr::Set_ubits;

  if (key.substr(0, 7) == "__sbits")
    return Attr::Set_sbits;

  if (key.substr(0, 5) == "__max")
    return Attr::Set_max;

  if (key.substr(0, 5) == "__min")
    return Attr::Set_min;

  if (key.substr(0, 11) == "__dp_assign")
    return Attr::Set_dp_assign;

  return Attr::Set_other;
}

void Bitwidth::process_attr_get(Node &node) {
  I(node.is_sink_connected("field"));
  auto dpin_key = node.get_sink_pin("field").get_driver_pin();
  I(dpin_key.get_node().is_type(Ntype_op::TupKey));

  auto key  = dpin_key.get_name();
  auto attr = get_key_attr(key);
  I(attr != Attr::Set_dp_assign);  // Not get attr with __dp_assign
  if (attr == Attr::Set_other) {
    not_finished = true;
    return;
  }

  I(node.is_sink_connected("name"));
  auto dpin_val = node.get_sink_pin("name").get_driver_pin();

  auto it = bwmap.find(dpin_val.get_compact());
  if (it == bwmap.end()) {
    not_finished = true;
    return;
  }
  auto &bw = it->second;

  Lconst result;
  if (attr == Attr::Set_ubits) {
    result = bw.get_min() >= 0 ? Lconst(bw.get_sbits() - 1) : Lconst(bw.get_sbits());
    fmt::print("min:{}, result:{}\n", bw.get_min().to_i(), result.to_i());
  } else if (attr == Attr::Set_sbits) {
    result = Lconst(bw.get_sbits());
  } else if (attr == Attr::Set_max) {
    result = bw.get_max();
  } else if (attr == Attr::Set_min) {
    result = bw.get_min();
  }


  auto new_node = node.get_class_lgraph()->create_node_const(result);
  auto new_dpin = new_node.get_driver_pin();
  for (auto &out : node.out_edges()) {
    new_dpin.connect_sink(out.sink);
  }

  if (!hier) // FIXME: once hier del works
    node.del_node();
}

// lhs := rhs
void Bitwidth::process_attr_set_dp_assign(Node &node_dp, Fwd_edge_iterator::Fwd_iter &fwd_it) {
  auto dpin_lhs = node_dp.get_sink_pin("value").get_driver_pin();
  auto dpin_rhs = node_dp.get_sink_pin("name").get_driver_pin();

  auto it = bwmap.find(dpin_lhs.get_compact());
  Bitwidth_range bw_lhs(0);
  if (it != bwmap.end()) {
    bw_lhs = it->second;
  } else {
    fmt::print("BW-> LHS isn't ready, wait for next iteration\n");
    return;
  }

  auto it2 = bwmap.find(dpin_rhs.get_compact());
  Bitwidth_range bw_rhs(0);
  if (it2 != bwmap.end()) {
    bw_rhs = it2->second;
  } else {
    fmt::print("BW-> RHS isn't ready, wait for next iteration\n");
    return;
  }


  auto lhs_node = dpin_lhs.get_node();
  bool lhs_is_attr_bit_set = false;
  // note: if lhs is attr_bit_set, the dp should respect it and create a mask to guarantee the will reflect the lhs bits
  if (lhs_node.get_type_op() == Ntype_op::Tposs) {
    auto attr_node = lhs_node.setup_sink_pin("a").get_driver_node();
    auto c1 = attr_node.setup_sink_pin("field").get_driver_pin().get_name() == "__ubits";
    auto c2 = attr_node.setup_sink_pin("field").get_driver_pin().get_name() == "__sbits";
    lhs_is_attr_bit_set = c1 || c2;
  }


  if (bw_rhs.get_sbits() >= bw_lhs.get_sbits() || lhs_is_attr_bit_set) {
    auto mask_node  = node_dp.get_class_lgraph()->create_node(Ntype_op::And);
    auto mask_dpin  = mask_node.get_driver_pin();

    auto bw_lhs_bits = bw_lhs.is_always_positive() ? bw_lhs.get_sbits() - 1 : bw_lhs.get_sbits();

    auto mask_const = (Lconst(1UL) << Lconst(bw_lhs_bits)) - 1;
    auto all_one_node = node_dp.get_class_lgraph()->create_node_const(mask_const);
    auto all_one_dpin = all_one_node.setup_driver_pin();


    // Note: I set the unsigned k-bits (max, min) for the mask
    bwmap.insert_or_assign(mask_dpin.get_compact(), Bitwidth_range(Lconst(0), mask_const));
    dpin_rhs.connect_sink(mask_node.setup_sink_pin("A"));
    all_one_dpin.connect_sink(mask_node.setup_sink_pin("A"));
    for (auto e : node_dp.out_edges())
      mask_dpin.connect_sink(e.sink);

    fwd_it.add_node(mask_node);
    fwd_it.add_node(all_one_node);

  } else { // bw_rhs.bits < bw_lhs.bits, already match
    for (auto e : node_dp.out_edges())
      dpin_rhs.connect_sink(e.sink);
  }


  if (!hier) // FIXME: once hier del works
    node_dp.del_node();
}

void Bitwidth::process_attr_set_new_attr(Node &node_attr, Fwd_edge_iterator::Fwd_iter &fwd_it) {
  I(node_attr.is_sink_connected("field"));
  auto dpin_key = node_attr.get_sink_pin("field").get_driver_pin();
  auto key      = dpin_key.get_name();
  auto attr     = get_key_attr(key);
  if (attr == Attr::Set_other) {
    not_finished = true;
    return;
  }

  if (attr == Attr::Set_dp_assign) {
    process_attr_set_dp_assign(node_attr, fwd_it);
    return;
  }

  I(node_attr.is_sink_connected("value"));
  auto dpin_val = node_attr.get_sink_pin("value").get_driver_pin();

  if (!dpin_key.get_node().is_type(Ntype_op::TupKey)) {
    not_finished = true;
    return;  // can not handle now
  }

  I(dpin_key.has_name());

  auto attr_dpin = node_attr.get_driver_pin("Y");

  std::string_view dpin_name;
  if (attr_dpin.has_name())
    dpin_name = attr_dpin.get_name();

  // copy parent's bw for some judgement and then update to attr_set value
  Bitwidth_range bw(0);
  bool parent_pending = false;
  
  if (node_attr.is_sink_connected("name")) {
    auto through_dpin = node_attr.get_sink_pin("name").get_driver_pin();
    auto it           = bwmap.find(through_dpin.get_compact());
    if (it != bwmap.end()) {
      bw = it->second;
    } else {
      parent_pending = true;
    }
  }

  if (attr == Attr::Set_ubits || attr == Attr::Set_sbits) {
    I(dpin_val.get_node().is_type_const());
    auto val = dpin_val.get_node().get_type_const();

    if (attr == Attr::Set_ubits) {
      if (bw.get_sbits() && (bw.get_sbits() - 1) > (val.to_i()))
        Pass::error("bitwidth mismatch. Variable {} needs {}sbits, but constrained to {}ubits\n", dpin_name, bw.get_sbits(), val.to_i());

      bw.set_sbits_range(val.to_i()); //note: still set sbits range and rely on the Tposs to turn max/min to positive
      bool tposs_existed = false;
      for (auto e : node_attr.out_edges()) {
        if (e.sink.get_node().get_type_op() == Ntype_op::Tposs) {
          tposs_existed = true;
          break;
        }
      }
      if (!tposs_existed && dpin_name != "$clock_0" && dpin_name != "$reset_0")
        insert_tposs_nodes(node_attr, fwd_it);

    } else { // Attr::Set_sbits
      if (bw.get_sbits() && bw.get_sbits() > (val.to_i()))
        Pass::error("bitwidth mismatch. Variable {} needs {}sbits, but constrained to {}sbits\n", dpin_name, bw.get_sbits(), val.to_i());

      bw.set_sbits_range(val.to_i());
    }
  } else if (attr == Attr::Set_max) {
    I(false);  // FIXME: todo
  } else if (attr == Attr::Set_min) {
    I(false);  // FIXME: todo
  } else {
    I(false);
    // note-I:  Attr::unsigned may need insert Tposs also
    // note-II: Attr::set_dp_assign handled in another method
  }

  for (auto out_dpin : node_attr.out_connected_pins()) {
    bwmap.insert_or_assign(out_dpin.get_compact(), bw);
  }

  // upwards propagate for one step node_attr, most graph input bits are set here
  if (parent_pending) {
    auto through_dpin = node_attr.get_sink_pin("name").get_driver_pin();
    bwmap.insert_or_assign(through_dpin.get_compact(), bw);
    fmt::print("DEBUG through_dpin:{}\n", through_dpin.debug_name());
    bw.dump();
  }
}


// insert tposs after attr node when ubits
void Bitwidth::insert_tposs_nodes(Node &node_attr, Fwd_edge_iterator::Fwd_iter &fwd_it)  {
  I(node_attr.get_sink_pin("field").get_driver_pin().get_name() == "__ubits") ;

  std::vector<Node_pin> attr_dpins;
  attr_dpins.emplace_back(node_attr.setup_driver_pin("Y"));
  if (node_attr.setup_driver_pin("chain").is_connected())
    attr_dpins.emplace_back(node_attr.setup_driver_pin("chain"));

  for (auto attr_dpin : attr_dpins) {
    if (hier) {
      attr_dpin = attr_dpin.get_non_hierarchical(); // insert locally not through hierarchy
    }
    auto ntposs = node_attr.get_class_lgraph()->create_node(Ntype_op::Tposs);
    auto ntposs_dpin = ntposs.setup_driver_pin();
    attr_dpin.connect_sink(ntposs.setup_sink_pin("a"));
    for (auto &e : attr_dpin.out_edges()) {
      if (e.sink.get_node() != ntposs) {
        ntposs_dpin.connect_sink(e.sink);
        e.del_edge();
      }
    }
    fwd_it.add_node(ntposs); // add once the edges are added
  }
}


void Bitwidth::process_attr_set_propagate(Node &node_attr) {
  auto             attr_dpin = node_attr.get_driver_pin("Y");
  std::string_view dpin_name;
  if (attr_dpin.has_name())
    dpin_name = attr_dpin.get_name();

  I(node_attr.is_sink_connected("name"));
  bool parent_data_pending = false;
  auto data_dpin = node_attr.get_sink_pin("name").get_driver_pin();

  I(node_attr.is_sink_connected("chain"));
  auto parent_attr_dpin = node_attr.get_sink_pin("chain").get_driver_pin();

  Bitwidth_range data_bw(0);
  auto data_it = bwmap.find(data_dpin.get_compact());
  if (data_it != bwmap.end()) {
    data_bw = data_it->second;
  } else {
    parent_data_pending = true;
  }

  auto parent_attr_it = bwmap.find(parent_attr_dpin.get_compact());
  if (parent_attr_it == bwmap.end()) {
    fmt::print("attr_set propagate bwmap to AttrSet name:{}\n", dpin_name);
    not_finished = true;
    return;
  }
  const auto parent_attr_bw = parent_attr_it->second;

  if (parent_attr_bw.get_sbits() && data_bw.get_sbits()) {
    if (parent_attr_bw.get_sbits() < data_bw.get_sbits()) {
      Pass::error("bitwidth mismatch. Variable {} needs {}bits, but constrained to {}bits\n", dpin_name, data_bw.get_sbits(), parent_attr_bw.get_sbits());
    } else if (parent_attr_bw.get_max() < data_bw.get_max()) {
      Pass::error("bitwidth mismatch. Variable {} needs {}max, but constrained to {}max\n", dpin_name, data_bw.get_max().to_pyrope(), parent_attr_bw.get_max().to_pyrope());
    } else if (parent_attr_bw.get_min() > data_bw.get_min()) {
      Pass::warn("bitwidth mismatch. Variable {} needs {}min, but constrained to {}min\n", dpin_name, data_bw.get_min().to_pyrope(), parent_attr_bw.get_min().to_pyrope());
    }
  }

  for (auto out_dpin : node_attr.out_connected_pins())
    bwmap.insert_or_assign(out_dpin.get_compact(), parent_attr_bw);


  if (parent_data_pending)
    bwmap.insert_or_assign(data_dpin.get_compact(), parent_attr_bw);

}

void Bitwidth::process_attr_set(Node &node, Fwd_edge_iterator::Fwd_iter &fwd_it) {
  if (node.is_sink_connected("field")) {
    process_attr_set_new_attr(node, fwd_it);
  } else {
    process_attr_set_propagate(node);
  }
}


void Bitwidth::forward_adjust_dpin(Node_pin &dpin, Bitwidth_range &bw) {
  auto bw_bits = bw.get_sbits();
  if (bw_bits && bw_bits < dpin.get_bits() && (dpin.get_node().get_type_op() != Ntype_op::Tposs))
    dpin.set_bits(bw_bits);
}

void Bitwidth::set_graph_boundary(Node_pin &dpin, Node_pin &spin) {
  I(hier); // do not call unless hierarchy is set

  if (dpin.get_class_lgraph() == spin.get_class_lgraph())
    return;

  I(dpin.get_hidx() != spin.get_hidx());
  auto same_level_spin = spin.get_non_hierarchical();
  for (auto dpin_sub : same_level_spin.inp_driver()) {
    if (!dpin_sub.get_node().is_type(Ntype_op::Sub))
      continue;

    if (dpin_sub.get_bits() == 0 || dpin_sub.get_bits() > dpin.get_bits())
      dpin_sub.set_bits(dpin.get_bits());

  }
}


void Bitwidth::debug_unconstrained_msg(Node &node, Node_pin &dpin) {
  if (dpin.has_name()) {
    fmt::print("BW-> gate:{} has input pin:{} unconstrained\n", node.debug_name(), dpin.debug_name());
  } else {
    fmt::print("BW-> gate:{} has some inputs unconstrained\n", node.debug_name());
  }
}

void Bitwidth::bw_pass(LGraph *lg) {
  must_perform_backward = false;
  not_finished          = false;

  // note: lg input bits must be set by attr_set node, it will be handled through the algorithm runs
  lg->each_graph_input([this](Node_pin &dpin) {
    if (dpin.get_bits()) {
      Bitwidth_range bw;
      bw.set_sbits_range(dpin.get_bits());
      bwmap.insert_or_assign(dpin.get_compact(), bw);
      fmt::print("DEBUG graph input:{}, bits:{}\n", dpin.debug_name(), dpin.get_bits());
    }
  }, hier);

  auto lgit = lg->forward(hier); // the design pattern for traverse newly created nodes in same iteration
  for (auto fwd_it = lgit.begin(); fwd_it != lgit.end() ; ++fwd_it) {
    auto node = *fwd_it;
    fmt::print("{}\n", node.debug_name());
    auto inp_edges = node.inp_edges();
    auto op        = node.get_type_op();

    if (inp_edges.empty() && (op != Ntype_op::Const && op != Ntype_op::Sub && op != Ntype_op::LUT && op != Ntype_op::TupKey)) {
      fmt::print("BW-> removing dangling node:{}\n", node.debug_name());
      if (!hier) // FIXME: once hier del works
        node.del_node();
      continue;
    }


    if (op == Ntype_op::Const) {
      process_const(node);
    } else if (op == Ntype_op::TupKey || op == Ntype_op::TupGet || op == Ntype_op::TupAdd) {
      continue; // Nothing to do for this
    } else if (op == Ntype_op::Or) {
      if (inp_edges.size() == 1)
        process_assignment_or(node, inp_edges);
      else
        process_logic_or_xor(node, inp_edges);
    } else if (op == Ntype_op::Xor) {
      process_logic_or_xor(node, inp_edges);
    } else if (op == Ntype_op::Ror) {
      process_ror(node, inp_edges);
    } else if (op == Ntype_op::And) {
      process_logic_and(node, inp_edges);
    } else if (op == Ntype_op::AttrSet) {
      process_attr_set(node, fwd_it);
      if (node.is_invalid())
        continue;
    } else if (op == Ntype_op::AttrGet) {
      process_attr_get(node);
      if (node.is_invalid())
        continue;
    } else if (op == Ntype_op::Sum) {
      process_sum(node, inp_edges);
    } else if (op == Ntype_op::Mult) {
      process_mult(node, inp_edges);
    } else if (op == Ntype_op::SRA) {
      process_sra(node, inp_edges);
    } else if (op == Ntype_op::SHL) {
      process_shl(node, inp_edges);
    } else if (op == Ntype_op::Not) {
      process_not(node, inp_edges);
    } else if (op == Ntype_op::Sflop || op == Ntype_op::Aflop || op == Ntype_op::Fflop) {
      process_flop(node);
    } else if (op == Ntype_op::Mux) {
      process_mux(node, inp_edges);
    } else if (op == Ntype_op::GT || op == Ntype_op::LT || op == Ntype_op::EQ) {
      process_comparator(node);
    } else if (op == Ntype_op::Tposs) {
      process_tposs(node, inp_edges);
    } else if (op == Ntype_op::Sub) {
      set_subgraph_boundary_bw(node);
    } else {
      fmt::print("FIXME: node:{} still not handled by bitwidth\n", node.debug_name());
    }


    if (hier) {
      for (auto e:inp_edges)
        set_graph_boundary(e.driver, e.sink);
    }

    for (auto dpin : node.out_connected_pins()) {
      auto it = bwmap.find(dpin.get_compact());
      if (it == bwmap.end())
        continue;

      auto bw_bits = it->second.get_sbits();
      if (bw_bits == 0 && it->second.is_overflow()) {
        fmt::print("BW-> dpin:{} has over {}bits (simplify first!)\n", dpin.debug_name(), it->second.get_raw_max());
        continue;
      }

      if (dpin.get_bits() && dpin.get_bits() >= bw_bits)
        continue;

      dpin.set_bits(bw_bits);
    }

    //debug
    if (op != Ntype_op::Sub) {
      fmt::print("    ");
      auto it = bwmap.find(node.get_driver_pin("Y").get_compact());
      if (it != bwmap.end())
        it->second.dump();
    }

  }// end of lg->forward()



  // set bits for graph input and output
  lg->each_graph_input([this](Node_pin &dpin) {
    if (dpin.get_name() == "$")
      return;
    auto it = bwmap.find(dpin.get_compact());
    if (it != bwmap.end()) {
      auto &bw = it->second;
      auto bw_bits = bw.get_sbits();
      if (bw.is_always_positive())
        dpin.set_bits(bw_bits - 1);
      else
        dpin.set_bits(bw_bits);
    }
  }, hier);

  lg->each_graph_output([this](Node_pin &dpin) {
    if (dpin.get_name() == "%")
      return;
    auto spin = dpin.change_to_sink_from_graph_out_driver();
    auto out_driver = spin.get_driver_pin();

    I(!out_driver.is_invalid());
    auto it = bwmap.find(out_driver.get_compact());
    if (it == bwmap.end()) {
      return;
    } else {
      forward_adjust_dpin(out_driver, it->second);
    }

    if (out_driver.get_bits()) {
      if (out_driver.get_node().get_type_op() == Ntype_op::Tposs) {
        dpin.set_bits(out_driver.get_bits() - 1); //Tposs should not affect bits of graph output
      } else {
        dpin.set_bits(out_driver.get_bits());
      }

      if (hier)
        set_graph_boundary(out_driver, spin);
    }

    bwmap.insert_or_assign(dpin.get_compact(), it->second);
  }, hier);


#ifndef PRESERVE_ATTR_NODE
  if (not_finished) {
    fmt::print("BW-> could not converge\n");
  } else {
    // FIXME: this code may need to move to cprop if we have several types of
    // attributes. Delete only if all the attributes are finished

    // delete all the attr_set/get for bitwidth
    for (auto node : lg->fast(hier)) {
      auto op = node.get_type_op();
      if (op == Ntype_op::AttrSet) {
        try_delete_attr_node(node);
      }  
    } // end of lg->fast()
  }
#endif

  if (must_perform_backward) {
    fmt::print("BW-> some nodes need to back propagate width\n");
  }
}


void Bitwidth::try_delete_attr_node(Node &node) {
  if (node.is_sink_connected("field")) {
    auto key_dpin = node.get_sink_pin("field").get_driver_pin();
    auto attr     = get_key_attr(key_dpin.get_name());
    if (attr == Attr::Set_other)
      return;
  }

  auto node_non_hier = node.get_non_hierarchical();
  if (node_non_hier.is_sink_connected("name")) {
    auto data_dpin = node_non_hier.get_sink_pin("name").get_driver_pin();

    for (auto e : node_non_hier.out_edges()) {
      if (e.driver.get_pid() == 0)
        e.sink.connect_driver(data_dpin);
    }
  }

  if (!hier) // FIXME: once hier del works
    node.del_node();
}

void Bitwidth::set_subgraph_boundary_bw(Node &node) {
  auto *library = Graph_library::instance(node.get_class_lgraph()->get_path());
  Sub_node* sub;
  (void)sub;

  auto sub_name = node.get_type_sub_node().get_name();
  if (library->has_name(sub_name)) {
    auto lgid = library->get_lgid(node.get_type_sub_node().get_name());
    sub   = library->ref_sub(lgid);
  } else {
    Pass::error("Global IO connection pass cannot find existing subgraph {} in lgdb\n", sub_name);
    return;
  }

  // get the BW of the driver of sub_graph_output, and set it to the corresponding subg_node in lg
  // FIXME->sh: any other way that doesn't need to open a lgraph?
  auto sub_lg = LGraph::open(node.get_class_lgraph()->get_path(), sub_name) ; 
  sub_lg->each_graph_output([&node, this](Node_pin &dpin_gout) {
    if (bwmap.find(dpin_gout.get_compact()) == bwmap.end())
      return; // not ready yet, only possible from Pyrope front-end

    auto node_subg_dpin = node.setup_driver_pin(dpin_gout.get_name());
    bwmap.insert_or_assign(node_subg_dpin.get_compact(), bwmap[dpin_gout.get_compact()]);
  });
}

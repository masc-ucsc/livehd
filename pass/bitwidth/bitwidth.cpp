//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "bitwidth.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "bitwidth_range.hpp"
#include "lbench.hpp"
#include "lgraph.hpp"
#include "pass_bitwidth.hpp"

Bitwidth::Bitwidth(bool _hier, int _max_iterations, BWMap_flat &_flat_bwmap, BWMap_hier &_hier_bwmap)
    : max_iterations(_max_iterations), hier(_hier), flat_bwmap(_flat_bwmap), hier_bwmap(_hier_bwmap) {}

void Bitwidth::do_trans(Lgraph *lg) {
  Lbench b("pass.bitwidth");
  bw_pass(lg);
}

void Bitwidth::process_const(Node &node) {
  auto dpin = node.get_driver_pin();
  auto it   = flat_bwmap.insert_or_assign(dpin.get_compact_flat(), Bitwidth_range(node.get_type_const()));
  forward_adjust_dpin(dpin, it.first->second);
}

void Bitwidth::process_flop(Node &node) {
  I(node.is_sink_connected("din"));
  std::vector<Node_pin::Compact_flat> flop_cpins;

  if (node.is_sink_connected("din")) {
    flop_cpins.emplace_back(node.get_sink_pin("din").get_driver_pin().get_compact_flat());
  }

  flop_cpins.emplace_back(node.get_driver_pin().get_compact_flat());
  if (node.is_sink_connected("initial")) {
    flop_cpins.emplace_back(node.get_sink_pin("initial").get_driver_pin().get_compact_flat());
  }

  Lconst max_val;
  Lconst min_val;

  bool a_constrain_found = false;
  for(auto &cpin:flop_cpins) {
    auto it = flat_bwmap.find(cpin);
    if (it == flat_bwmap.end()) {
      continue;
    }
    a_constrain_found = true;

    auto a = it->second.get_max();
    if (max_val<a) {
      if (max_val!=0)
        not_finished = true;

      max_val = a;
    }
    auto b = it->second.get_max();
    if (min_val>b) {
      if (min_val!=0)
        not_finished = true;
      min_val = b;
    }
  }

  if (a_constrain_found) {
    for(auto &cpin:flop_cpins) {
      flat_bwmap.insert_or_assign(cpin, Bitwidth_range(min_val, max_val));
    }
  }else{
    auto dpin = node.setup_driver_pin();
    debug_unconstrained_msg(node, dpin);
    not_finished = true;
  }
}

void Bitwidth::process_ror(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());

  Bitwidth_range bw;
  bw.set_sbits_range(1);
  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), bw);
}

void Bitwidth::process_not(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling???

  Lconst max_val;
  Lconst min_val;
  for (auto e : inp_edges) {
    auto it = flat_bwmap.find(e.driver.get_compact_flat());
    if (it != flat_bwmap.end()) {
      auto pmax = it->second.get_max().to_i();  // pmax = parent_max
      auto pmin = it->second.get_min().to_i();
      // calculate 1s'complemet value
      auto pmax_1scomp = ~pmax;
      auto pmin_1scomp = ~pmin;
      max_val          = Lconst(std::max(pmax_1scomp, pmin_1scomp));
      min_val          = Lconst(std::min(pmax_1scomp, pmin_1scomp));
    } else {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
  }

  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_mux(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling???

  Lconst max_val;
  Lconst min_val;
  for (auto e : inp_edges) {
    if (e.sink.get_pid() == 0) {
      auto bits = 0u;
      auto it   = flat_bwmap.find(e.driver.get_compact_flat());
      if (it == flat_bwmap.end()) {
        Bitwidth_range bw(0, inp_edges.size() - 1);  // -1 for the mux sel
        flat_bwmap.insert_or_assign(e.driver.get_compact_flat(), bw);
        bits = bw.get_sbits();
      } else {
        if (it->second.get_min() < -1) {  // 0,-1 is common for conditionals (OK)
          Pass::info("unconstrained mux sel pin:{}, runtime can be negative with min:{} max:{}",
                     e.driver.debug_name(),
                     it->second.get_min().to_pyrope(),
                     it->second.get_max().to_pyrope());
          continue;
        }
        if (it->second.get_max() >= inp_edges.size()) {
          Pass::info("unconstrained mux sel pin:{}, runtime can overflow with min:{} max:{}",
                     e.driver.debug_name(),
                     it->second.get_min().to_pyrope(),
                     it->second.get_max().to_pyrope());
          continue;
        }
        bits = it->second.get_sbits();
      }

      if (e.driver.get_bits() && e.driver.get_bits() >= bits) {
        e.driver.set_bits(bits);
      }
      continue;
    }

    auto it = flat_bwmap.find(e.driver.get_compact_flat());
    if (it != flat_bwmap.end()) {
      if (max_val < it->second.get_max())
        max_val = it->second.get_max();

      if (min_val > it->second.get_min())
        min_val = it->second.get_min();

    } else {
      // update as soon as possible, don't wait everyone ready, so you could break the flop-loop
      flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), Bitwidth_range(min_val, max_val));

      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
  }
  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_shl(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);

  auto a_dpin = node.get_sink_pin("a").get_driver_pin();
  auto n_dpin = node.get_sink_pin("b").get_driver_pin();

  auto a_it = flat_bwmap.find(a_dpin.get_compact_flat());
  auto n_it = flat_bwmap.find(n_dpin.get_compact_flat());

  Bitwidth_range a_bw(0);
  if (a_it == flat_bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  } else {
    a_bw = a_it->second;
  }

  Bitwidth_range n_bw(0);
  if (n_it == flat_bwmap.end()) {
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

  auto           max     = a_bw.get_max();
  auto           min     = a_bw.get_min();
  auto           amount  = n_bw.get_max().to_i();
  auto           max_val = Lconst(Lconst(max.get_raw_num()) << Lconst(amount));
  auto           min_val = Lconst(Lconst(min.get_raw_num()) << Lconst(amount));
  Bitwidth_range bw(min_val, max_val);
  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), bw);
}

void Bitwidth::process_sra(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);

  auto a_dpin = node.get_sink_pin("a").get_driver_pin();
  auto n_dpin = node.get_sink_pin("b").get_driver_pin();

  auto a_it = flat_bwmap.find(a_dpin.get_compact_flat());
  auto n_it = flat_bwmap.find(n_dpin.get_compact_flat());

  Bitwidth_range a_bw(0);
  if (a_it == flat_bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  } else {
    a_bw = a_it->second;
  }

  Bitwidth_range n_bw(0);
  if (n_it == flat_bwmap.end()) {
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
    auto max     = a_bw.get_max();
    auto min     = a_bw.get_min();
    auto amount  = Lconst(Lconst(1) << n_bw.get_min());
    auto max_val = max.div_op(amount);
    auto min_val = min.div_op(amount);

    Bitwidth_range bw(min_val, max_val);
    flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), bw);
  } else {
    flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), a_bw);
  }
}

void Bitwidth::process_sum(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling sum??? (delete)

  Lconst max_val;
  Lconst min_val;
  for (auto e : inp_edges) {
    auto it = flat_bwmap.find(e.driver.get_compact_flat());
    if (it != flat_bwmap.end()) {
      if (e.sink.get_pin_name() == "A") {
        max_val = max_val + it->second.get_max();
        min_val = min_val + it->second.get_min();
      } else {
        max_val = max_val - it->second.get_min();
        min_val = min_val - it->second.get_max();
      }
    } else {
      debug_unconstrained_msg(node, e.driver);
      GI(hier, false, "Assert! flat_bwmap entry should be ready at final bitwidth pass, entry:{}\n", e.driver.debug_name());

      not_finished = true;
      return;
    }
  }

  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_mult(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling sum??? (delete)

  Lconst max_val(1);
  Lconst min_val(1);
  for (auto e : inp_edges) {
    auto it = flat_bwmap.find(e.driver.get_compact_flat());
    if (it != flat_bwmap.end()) {
      max_val = max_val.mult_op(it->second.get_max());
      min_val = min_val.mult_op(it->second.get_min());
      if (max_val < min_val) {
        auto tmp = max_val;
        max_val  = min_val;
        min_val  = tmp;
      }
    } else {
      debug_unconstrained_msg(node, e.driver);
      GI(hier, false, "Assert! flat_bwmap entry should be ready at final bitwidth pass, entry:{}\n", e.driver.debug_name());

      not_finished = true;
      return;
    }
  }
  // Bitwidth_range(min_val, max_val).dump();
  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_set_mask(Node &node) {
  (void)node;
  I(false);  // FIXME:
  not_finished = true;
}

void Bitwidth::process_get_mask(Node &node) {
  auto a_dpin = node.get_sink_pin("a").get_driver_pin();
  if (a_dpin.is_invalid()) {
    if (!not_finished) {
      node.del_node();
    }
    return;
  }

  auto mask_dpin = node.get_sink_pin("mask").get_driver_pin();

  auto it = flat_bwmap.find(a_dpin.get_compact_flat());
  if (it == flat_bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }

  auto   it2 = flat_bwmap.find(mask_dpin.get_compact_flat());
  Lconst mask_max;
  Lconst mask_min;
  if (it2 == flat_bwmap.end()) {
    if (!mask_dpin.is_type_const()) {
      debug_unconstrained_msg(node, mask_dpin);
      not_finished = true;
      return;
    }
    mask_max = mask_dpin.get_type_const();
    mask_min = mask_max;
  } else {
    mask_max = it2->second.get_max();
    mask_min = it2->second.get_min();
  }

  const Lconst val     = it->second.get_max().get_mask_op(mask_min);
  Lconst       min_val = val;
  Lconst       max_val = val;

  Lconst val2 = it->second.get_max().get_mask_op(mask_max);
  Lconst val3 = it->second.get_min().get_mask_op(mask_min);
  Lconst val4 = it->second.get_min().get_mask_op(mask_max);

  if (val2 > max_val)
    max_val = val2;
  if (val2 < min_val)
    min_val = val2;

  if (val3 > max_val)
    max_val = val3;
  if (val3 < min_val)
    min_val = val3;

  if (val4 > max_val)
    max_val = val4;
  if (val4 < min_val)
    min_val = val4;

  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), Bitwidth_range(min_val, max_val));
  // flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), Bitwidth_range(0,
  // (Lconst(1)<<(max_val.get_bits()-1))-Lconst(1) ));
}

void Bitwidth::process_sext(Node &node, XEdge_iterator &inp_edges) {
  auto wire_dpin = inp_edges[0].driver;
  auto pos_dpin  = inp_edges[1].driver;

  I(inp_edges[0].sink.get_pin_name() == "a");
  I(inp_edges[1].sink.get_pin_name() == "b");

  auto wire_it = flat_bwmap.find(wire_dpin.get_compact_flat());

  auto sign_max = Bits_max;
  if (pos_dpin.is_type_const()) {
    sign_max = pos_dpin.get_type_const().to_i();
  }

  if (sign_max == Bits_max && wire_it == flat_bwmap.end()) {
    debug_unconstrained_msg(node, pos_dpin);
    not_finished = true;
    return;
  }

  if (wire_it != flat_bwmap.end()) {
    auto b = wire_it->second.get_sbits();
    if (b <= sign_max) {  // sext is useless
      sign_max = b;
      for (auto &e : node.out_edges()) {
        e.sink.connect_driver(inp_edges[0].driver);
      }
      node.del_node();
      return;
    }
  }

  Bitwidth_range bw;
  bw.set_sbits_range(sign_max);
  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), bw);

  if (!not_finished && wire_it != flat_bwmap.end()) {
    auto wire_node = wire_dpin.get_node();
    auto wire_op   = wire_node.get_type_op();
    if (wire_op == Ntype_op::Sext || wire_op == Ntype_op::And) {
      // sext1(sext2(X,a),b) == a<=b ? sext2(X,a):sext1(X,b)
      // sext(and(X,a),b) == a<b ? and(X,a):sext(X,b)
      if (wire_it->second.get_sbits() <= sign_max) {
        for (auto &e : node.out_edges()) {
          e.sink.connect_driver(wire_dpin);
        }
        node.del_node();
      } else {
        Node_pin grandpa_dpin;
        if (wire_op == Ntype_op::Sext) {
          grandpa_dpin = wire_node.get_sink_pin("a").get_driver_pin();
        } else {  // Find the other in the And(other,MASK)
          auto mask_e = wire_node.inp_edges();
          if (mask_e.size() != 2)
            return;
          auto n0  = mask_e[0].driver.get_node();
          auto n1  = mask_e[1].driver.get_node();
          auto n0c = n0.is_type_const();
          auto n1c = n1.is_type_const();
          if (!n0c && !n1c)
            return;

          if (n0c && n0.get_type_const().is_mask()) {
            grandpa_dpin = mask_e[1].driver;
          } else if (n1c && n1.get_type_const().is_mask()) {
            grandpa_dpin = mask_e[0].driver;
          } else {
            return;
          }
        }
        inp_edges[0].del_edge();
        node.setup_sink_pin("a").connect_driver(grandpa_dpin);
      }
    }
  }
}

void Bitwidth::process_comparator(Node &node) {
  Bitwidth_range bw;
  bw.set_sbits_range(1);
  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), bw);
}

void Bitwidth::process_assignment_or(Node &node, XEdge_iterator &inp_edges) {
  Lconst max_val, min_val;
  for (auto e : inp_edges) {
    auto it = flat_bwmap.find(e.driver.get_compact_flat());
    if (it == flat_bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
    max_val = it->second.get_max();
    min_val = it->second.get_min();
  }
  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), Bitwidth_range(min_val, max_val));
  return;
}

void Bitwidth::process_logic_or_xor(Node &node, XEdge_iterator &inp_edges) {
  // handle normal Or_Op/Xor_Op
  I(inp_edges.size() > 1);
  Bits_t max_bits = 0;

  for (auto e : inp_edges) {
    auto   it   = flat_bwmap.find(e.driver.get_compact_flat());
    Bits_t bits = 0;
    if (it == flat_bwmap.end()) {
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

  auto max_val = (Lconst(1UL) << Lconst(max_bits - 1)) - 1;
  auto min_val = Lconst(-1) - max_val;
  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(),
                              Bitwidth_range(min_val, max_val));  // the max/min of AND MASK should be unsigned
}

void Bitwidth::process_logic_and(Node &node, XEdge_iterator &inp_edges) {
  // note: the goal is to get the min requiered bits in advance of final global BW and calculate the min range of (max, min)
  I(!inp_edges.empty());

  int mask_pos          = -1;
  int pos_min_sbits_pos = -1;

  Bits_t pos_min_sbits = Bits_max;  // always positive max bits
  Bits_t unk_max_sbits = 0;         // may be negative minimum number of bits

  for (auto i = 0u; i < inp_edges.size(); ++i) {
    const auto &e  = inp_edges[i];
    auto        it = flat_bwmap.find(e.driver.get_compact_flat());
    if (it == flat_bwmap.end()) {
      unk_max_sbits = Bits_max;  // We do not know
      continue;
    }

    Bits_t bw_sbits = it->second.get_sbits();
    I(bw_sbits);

    if (it->second.is_always_positive()) {
      if (bw_sbits <= pos_min_sbits) {
        if (!hier && e.driver.is_type_const()) {
          mask_pos = i;
          if (pos_min_sbits_pos < 0)
            pos_min_sbits_pos = i;
        } else {
          pos_min_sbits_pos = i;
        }
        pos_min_sbits = bw_sbits;
      }
    } else {
      if (bw_sbits > unk_max_sbits)
        unk_max_sbits = bw_sbits;
    }
  }
  mask_pos = -1;

  if (unk_max_sbits == Bits_max && pos_min_sbits == Bits_max) {
    // Nothing to do
    Pass::info("could not find constrains for AND node:{}\n", node.debug_name());
    return;
  }

  if (mask_pos >= 0 && pos_min_sbits != Bits_max && pos_min_sbits_pos != mask_pos) {
    // There is a MASK that it is not needed (something else constrains the same or more)
    //
    auto v = (Lconst(1) << Lconst(pos_min_sbits - 1)) - inp_edges[mask_pos].driver.get_node().get_type_const();
    if (v == Lconst(1)) {  // Check that mask was right
      if (inp_edges.size() == 2) {
        // collapse forward the non-mask pin (not if input)
        int pos = mask_pos == 0 ? 1 : 0;
        if (inp_edges[pos].driver.is_graph_io()) {
          mask_pos = -1;
        } else {
          for (auto &out : node.out_edges()) {
            inp_edges[pos].driver.connect_sink(out.sink);
          }
          node.del_node();
          return;
        }
      }
    } else {
      mask_pos = -1;
    }
  }

  Lconst max_val;
  Lconst min_val;

  if (pos_min_sbits != Bits_max) {
    max_val = (Lconst(1) << Lconst(pos_min_sbits - 1)) - 1;
    min_val
        = Lconst(0);  // note: this could avoid the (max, min) of AND to pollute the later Sum_op if the AND is really just a mask
  } else {
    max_val = (Lconst(1) << Lconst(unk_max_sbits - 1)) - 1;
    min_val = Lconst(-1) - max_val;
  }

  Bitwidth_range bw(min_val, max_val);

  flat_bwmap.insert_or_assign(node.get_driver_pin().get_compact_flat(), bw);

  for (auto e : inp_edges) {
    auto bw_bits = e.driver.get_bits();
    if (bw_bits)
      continue;  // only handle unconstrained inputs

    if (e.driver.is_graph_io() || e.driver.get_node().is_type_loop_last())
      e.driver.set_bits(bw.get_sbits());
  }

  if (mask_pos >= 0) {
    inp_edges[mask_pos].del_edge();
  }
}

Bitwidth::Attr Bitwidth::get_key_attr(std::string_view key) {
  // FIXME: code duplicated in Firmap. Create a separate class for Attr
  const auto sz = key.size();

  if (sz<5)
    return Attr::Set_other;

  if (key.substr(sz - 5, sz) == "__max")
    return Attr::Set_max;

  if (key.substr(sz - 5, sz) == "__min")
    return Attr::Set_min;

  if (sz<7)
    return Attr::Set_other;

  if (key.substr(sz - 7, sz) == "__ubits")
    return Attr::Set_ubits;

  if (key.substr(sz - 7, sz) == "__sbits")
    return Attr::Set_sbits;

  if (sz<11)
    return Attr::Set_other;

  if (key.substr(sz - 11, sz) == "__dp_assign")
    return Attr::Set_dp_assign;

  return Attr::Set_other;
}

void Bitwidth::process_attr_get(Node &node) {
  I(node.is_sink_connected("field"));
  auto dpin_key = node.get_sink_pin("field").get_driver_pin();
  I(dpin_key.get_node().is_type_const());

  auto key  = dpin_key.get_type_const().to_string();
  auto attr = get_key_attr(key);
  I(attr != Attr::Set_dp_assign);  // Not get attr with __dp_assign
  if (attr == Attr::Set_other) {
    not_finished = true;
    return;
  }

  I(node.is_sink_connected("name"));
  auto dpin_val = node.get_sink_pin("name").get_driver_pin();

  auto it = flat_bwmap.find(dpin_val.get_compact_flat());
  if (it == flat_bwmap.end()) {
    not_finished = true;
    return;
  }
  auto &bw = it->second;

  Lconst result;
  if (attr == Attr::Set_ubits) {
    result = bw.get_min() >= 0 ? Lconst(bw.get_sbits() - 1) : Lconst(bw.get_sbits());

#ifndef NDEBUG
    fmt::print("min:{}, result:{}\n", bw.get_min().to_i(), result.to_i());
#endif

  } else if (attr == Attr::Set_sbits) {
    result = Lconst(bw.get_sbits());
  } else if (attr == Attr::Set_max) {
    result = bw.get_max();
  } else if (attr == Attr::Set_min) {
    result = bw.get_min();
  }

  for(auto pin:node.inp_connected_pins()) {
    pin.del();
  }
  node.set_type_const(result);
}

// lhs := rhs
void Bitwidth::process_attr_set_dp_assign(Node &node_dp) {
  I(node_dp.is_sink_connected("value"));
  I(node_dp.is_sink_connected("name"));

  auto dpin_lhs = node_dp.get_sink_pin("name").get_driver_pin();
  auto dpin_rhs = node_dp.get_sink_pin("value").get_driver_pin();

  auto it = flat_bwmap.find(dpin_lhs.get_compact_flat());
  if (it == flat_bwmap.end()) {
#ifndef NDEBUG
    fmt::print("BW-> LHS isn't ready, wait for next iteration\n");
#endif
    not_finished = true;
    return;
  }

  for (auto out_dpin : node_dp.out_connected_pins()) {
    flat_bwmap.insert_or_assign(out_dpin.get_compact_flat(), it->second);
  }

  auto it2 = flat_bwmap.find(dpin_rhs.get_compact_flat());
  if (it2 == flat_bwmap.end()) {
#ifndef NDEBUG
    fmt::print("BW-> RHS isn't ready, wait for next iteration\n");
#endif
    not_finished = true;
    return;
  }
}

void Bitwidth::process_attr_set_new_attr(Node &node_attr, Fwd_edge_iterator::Fwd_iter &fwd_it) {
  I(node_attr.is_sink_connected("field"));

  auto dpin_key = node_attr.get_sink_pin("field").get_driver_pin();
  auto key      = dpin_key.get_type_const().to_string();
  auto attr     = get_key_attr(key);

  if (attr == Attr::Set_other) {
    not_finished = true;
    return;
  }

  if (attr == Attr::Set_dp_assign) {
    process_attr_set_dp_assign(node_attr);
    return;
  }

  I(node_attr.is_sink_connected("value"));
  auto dpin_val = node_attr.get_sink_pin("value").get_driver_pin();

  if (!dpin_key.get_node().is_type_const()) {
    not_finished = true;
    return;  // can not handle now
  }

  auto attr_dpin = node_attr.get_driver_pin("Y");

  std::string_view dpin_name;
  if (attr_dpin.has_name())
    dpin_name = attr_dpin.get_name();

  // copy parent's bw for some judgement and then update to attr_set value
  Bitwidth_range bw(0);
  bool           parent_pending = true;

  if (node_attr.is_sink_connected("name")) {
    auto through_dpin = node_attr.get_sink_pin("name").get_driver_pin();
    auto it           = flat_bwmap.find(through_dpin.get_compact_flat());
    if (it != flat_bwmap.end()) {
      bw = it->second;
      parent_pending = false;
    }
  }

  if (attr == Attr::Set_ubits || attr == Attr::Set_sbits) {
    I(dpin_val.get_node().is_type_const());
    auto val = dpin_val.get_node().get_type_const();

    if (attr == Attr::Set_ubits) {
      if ((bw.get_sbits() - 1) > (val.to_i()))
        Pass::error("bitwidth mismatch at node {}. \nVariable {} needs {}sbits, but constrained to {}ubits\n",
                    node_attr.debug_name(),
                    dpin_name,
                    bw.get_sbits(),
                    val.to_i());

      if (parent_pending)
        bw.set_ubits_range(val.to_i());
      insert_tposs_nodes(node_attr, val.to_i(), fwd_it);
    } else {  // Attr::Set_sbits
      if (bw.get_sbits() > (val.to_i()))
        Pass::error("bitwidth mismatch at node {}. \nVariable {} needs {}sbits, but constrained to {}sbits\n",
                    node_attr.debug_name(),
                    dpin_name,
                    bw.get_sbits(),
                    val.to_i());

      if (parent_pending)
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
    flat_bwmap.insert_or_assign(out_dpin.get_compact_flat(), bw);
  }

  // upwards propagate for one step node_attr, most graph input BW are handled here
  if (parent_pending && node_attr.is_sink_connected("name")) {
    auto through_dpin = node_attr.get_sink_pin("name").get_driver_pin();
    flat_bwmap.insert_or_assign(through_dpin.get_compact_flat(), bw);
    // bw.dump();
  }
}

// insert tposs after attr node when ubits
void Bitwidth::insert_tposs_nodes(Node &node_attr_hier, Bits_t ubits, Fwd_edge_iterator::Fwd_iter &fwd_it) {
  I(node_attr_hier.get_sink_pin("field").get_driver_pin().get_type_const().to_string().find("__ubits") != std::string::npos);

  auto node_attr = node_attr_hier.get_non_hierarchical();  // insert locally not through hierarchy
  auto name_dpin = node_attr.get_sink_pin("name").get_driver_pin();
  if (name_dpin.is_invalid()) {
    return;
  }

  auto mask = (Lconst(1) << Lconst(ubits)) - Lconst(1);

  Node ntposs;

  for (auto &e : node_attr.out_edges()) {
    if (e.driver.get_pin_name() == "chain")
      continue;

    I(e.driver.get_pid() == 0);  // chain has pid 1

    if (e.sink.get_type_op() == Ntype_op::Get_mask) {
      auto m = e.sink.get_node().get_sink_pin("mask").get_driver_pin().get_type_const();
      if (m == mask)
        continue;
    }
    if (ntposs.is_invalid()) {
      ntposs = node_attr.get_class_lgraph()->create_node(Ntype_op::Get_mask);
      ntposs.setup_sink_pin("mask").connect_driver(node_attr.get_class_lgraph()->create_node_const(mask));
      ntposs.setup_sink_pin("a").connect_driver(name_dpin);
    }

    ntposs.setup_driver_pin().connect_sink(e.sink);
    e.del_edge();
  }

  if (!ntposs.is_invalid())
    fwd_it.add_node(ntposs);  // add once the edges are added
}

void Bitwidth::process_attr_set_propagate(Node &node_attr) {
  if (node_attr.out_connected_pins().size() == 0)
    return;

  auto             attr_dpin = node_attr.get_driver_pin("Y");
  std::string_view dpin_name;
  if (attr_dpin.has_name())
    dpin_name = attr_dpin.get_name();

  I(node_attr.is_sink_connected("name"));
  bool parent_data_pending = false;
  auto data_dpin           = node_attr.get_sink_pin("name").get_driver_pin();

  I(node_attr.is_sink_connected("chain"));
  auto parent_attr_dpin = node_attr.get_sink_pin("chain").get_driver_pin();

  Bitwidth_range data_bw(0);
  auto           data_it = flat_bwmap.find(data_dpin.get_compact_flat());
  if (data_it != flat_bwmap.end()) {
    data_bw = data_it->second;
  } else {
    parent_data_pending = true;
  }

  auto parent_attr_it = flat_bwmap.find(parent_attr_dpin.get_compact_flat());
  if (parent_attr_it == flat_bwmap.end()) {
#ifndef NDEBUG
    fmt::print("attr_set propagate flat_bwmap to AttrSet name:{}\n", dpin_name);
#endif
    not_finished = true;
    return;
  }
  const auto parent_attr_bw = parent_attr_it->second;

  if (parent_attr_bw.get_sbits() && data_bw.get_sbits()) {
    if (parent_attr_bw.get_sbits() < data_bw.get_sbits()) {
      Pass::error("bitwidth mismatch. Variable {} needs {}bits, but constrained to {}bits\n",
                  dpin_name,
                  data_bw.get_sbits(),
                  parent_attr_bw.get_sbits());
    } else if (parent_attr_bw.get_max() < data_bw.get_max()) {
      Pass::error("bitwidth mismatch. Variable {} needs {}max, but constrained to {}max\n",
                  dpin_name,
                  data_bw.get_max().to_pyrope(),
                  parent_attr_bw.get_max().to_pyrope());
    } else if (parent_attr_bw.get_min() > data_bw.get_min()) {
      Pass::warn("bitwidth mismatch. Variable {} needs {}min, but constrained to {}min\n",
                 dpin_name,
                 data_bw.get_min().to_pyrope(),
                 parent_attr_bw.get_min().to_pyrope());
    }
  }

  // Do not pass the parent_attr_bw. data_bw is smaller (or failed), pass the smallest possible

  for (auto out_dpin : node_attr.out_connected_pins()) {
    flat_bwmap.insert_or_assign(out_dpin.get_compact_flat(), data_bw);
  }

  if (parent_data_pending) {
    flat_bwmap.insert_or_assign(data_dpin.get_compact_flat(), data_bw);
  }
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
  if (bw_bits && bw_bits < dpin.get_bits() && (dpin.get_node().get_type_op() != Ntype_op::Get_mask))
    dpin.set_bits(bw_bits);
}

void Bitwidth::set_graph_boundary(Node_pin &dpin, Node_pin &spin) {
  I(hier);  // do not call unless hierarchy is set

  if (dpin.get_class_Lgraph() == spin.get_class_Lgraph())
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
  (void)node;
  if (dpin.has_name()) {
#ifndef NDEBUG
    fmt::print("BW-> gate:{} has input pin:{} unconstrained\n", node.debug_name(), dpin.debug_name());
#endif
  } else {
#ifndef NDEBUG
    fmt::print("BW-> gate:{} has some inputs unconstrained\n", node.debug_name());
#endif
  }
}

void Bitwidth::bw_pass(Lgraph *lg) {
  discovered_some_backward_nodes_try_again = true;
  not_finished                             = true;

  int n_iterations = 0;

  while (discovered_some_backward_nodes_try_again || not_finished) {
    discovered_some_backward_nodes_try_again = false;
    not_finished                             = false;

    // If the inputs have bits, use as a constrain
    lg->each_graph_input(
        [this](Node_pin &dpin) {
          if (dpin.get_bits()) {
            Bitwidth_range bw;
            bw.set_sbits_range(dpin.get_bits());  // We do not know if it was sign/unsigned start +1 in case
            flat_bwmap.insert_or_assign(dpin.get_compact_flat(), bw);
          }
        },
        hier);

    // FIXME->sh: Theoretically, Pyrope could have two module instances with different bits, to support
    // this, we have to reference a new bw table (hier_bwmap) which records the hidx so you could index different
    // nodes in different module instance. We only need to write/read new content to hier_bwmap at the final
    // global BW pass.
    // pseudo code
    // auto it = hier_bwmap.find(dpin.get_compact()) //hier_bwmap has hierarchy info, hidx
    // if (it == hier_bwmap.end()) {
    //   it2 = flat_bwmap.find(dpin.get_compact_flat());
    //   ...
    //   original BW algorithm stuff
    //   ...
    // }
    //
    //  hier_bwmap.insert_or_assign(dpin.get_compact());

    auto lgit = lg->forward(hier);
    for (auto fwd_it = lgit.begin(); fwd_it != lgit.end(); ++fwd_it) {
      auto node = *fwd_it;
      // fmt::print("{}\n", node.debug_name());
      auto inp_edges = node.inp_edges();
      auto op        = node.get_type_op();

      if (inp_edges.empty() && (op != Ntype_op::Const && op != Ntype_op::Sub && op != Ntype_op::LUT)) {
        if (!hier)  // FIXME: once hier del works
          node.del_node();
        continue;
      }

      if (op == Ntype_op::Const) {
        process_const(node);
      } else if (op == Ntype_op::TupGet || op == Ntype_op::TupAdd) {
        continue;  // Nothing to do for this
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
      } else if (op == Ntype_op::AttrGet) {
        process_attr_get(node);
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
      } else if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
        process_flop(node);
      } else if (op == Ntype_op::Mux) {
        process_mux(node, inp_edges);
      } else if (op == Ntype_op::GT || op == Ntype_op::LT || op == Ntype_op::EQ) {
        process_comparator(node);
      } else if (op == Ntype_op::Get_mask) {
        process_get_mask(node);
      } else if (op == Ntype_op::Set_mask) {
        process_set_mask(node);
      } else if (op == Ntype_op::Sext) {
        process_sext(node, inp_edges);
      } else if (op == Ntype_op::Sub) {
        set_subgraph_boundary_bw(node);
      } else {
#ifndef NDEBUG
        fmt::print("FIXME: node:{} still not handled by bitwidth\n", node.debug_name());
#endif
      }
      if (node.is_invalid())
        continue;

      if (hier) {
        for (auto e : inp_edges) set_graph_boundary(e.driver, e.sink);
      }

      for (auto dpin : node.out_connected_pins()) {
        auto it = flat_bwmap.find(dpin.get_compact_flat());
        if (it == flat_bwmap.end())
          continue;

        auto bw_bits = it->second.get_sbits();
        if (bw_bits == 0 && it->second.is_overflow()) {
#ifndef NDEBUG
          fmt::print("BW-> dpin:{} has over {}bits (simplify first!)\n", dpin.debug_name(), it->second.get_raw_max());
#endif
          continue;
        }

        if (dpin.get_bits() && dpin.get_bits() >= bw_bits)
          continue;

        dpin.set_bits(bw_bits);
      }
    }  // end of lg->forward()

    // set bits for graph input and output
    lg->each_graph_input(
        [this](Node_pin &dpin) {
          if (dpin.get_name() == "$")
            return;
          auto it = flat_bwmap.find(dpin.get_compact_flat());
          if (it != flat_bwmap.end()) {
            auto &bw      = it->second;
            auto  bw_bits = bw.get_sbits();
#if 1
            dpin.set_bits(bw_bits);
#else
            if (bw.is_always_positive())
              dpin.set_bits(bw_bits - 1);
            else
              dpin.set_bits(bw_bits);
#endif
          }
        },
        hier);

    lg->each_graph_output(
        [this](Node_pin &dpin) {
          if (dpin.get_name() == "%")
            return;
          auto spin       = dpin.change_to_sink_from_graph_out_driver();
          auto out_driver = spin.get_driver_pin();

          if (out_driver.is_invalid()) {  // not driven output pin
            return;
          }
          auto it = flat_bwmap.find(out_driver.get_compact_flat());
          if (it == flat_bwmap.end()) {
            return;
          } else {
            forward_adjust_dpin(out_driver, it->second);
          }

          if (out_driver.get_bits()) {
            if (out_driver.get_node().get_type_op() == Ntype_op::Get_mask) {
              dpin.set_bits(out_driver.get_bits() - 1);  // Tposs should not affect bits of graph output
            } else {
              dpin.set_bits(out_driver.get_bits());
            }

            if (hier)
              set_graph_boundary(out_driver, spin);
          }

          flat_bwmap.insert_or_assign(dpin.get_compact_flat(), it->second);
        },
        hier);

    if (discovered_some_backward_nodes_try_again && n_iterations < max_iterations) {
      Pass::info("BW-> some nodes need to back propagate width\n");
      discovered_some_backward_nodes_try_again = false;
    }
    ++n_iterations;
    if (n_iterations > max_iterations) {
      Pass::info("BW aborting after {} iterations", max_iterations);
      break;
    }
  }

  if (!hier && !not_finished) {
    // delete all the attr_set/get for bitwidth
    for (auto node : lg->fast()) { // Non-hierarchical erase attr
      auto op = node.get_type_op();
      if (op == Ntype_op::AttrSet) {
        if (op == Ntype_op::AttrSet) {
          try_delete_attr_node(node);
        }
      }
    }
  }

#ifndef NDEBUG
  for (auto node : lg->fast(hier)) {
    for (auto dpin : node.out_connected_pins()) {
      auto it = flat_bwmap.find(dpin.get_compact_flat());
      if (it == flat_bwmap.end()) {
        fmt::print("node:{} {} UNKNOWN\n", node.debug_name(), dpin.get_pin_name());
      } else if (it->second.is_always_positive()) {
        fmt::print("node:{} {} pos  |", node.debug_name(), dpin.get_pin_name());
        it->second.dump();
      } else if (it->second.is_always_negative()) {
        fmt::print("node:{} {} neg  |", node.debug_name(), dpin.get_pin_name());
        it->second.dump();
      } else {
        fmt::print("node:{} {} both |", node.debug_name(), dpin.get_pin_name());
        it->second.dump();
      }
    }
  }
#endif
}

void Bitwidth::try_delete_attr_node(Node &node) {
  I(!hier);

  Attr attr = Attr::Set_other;
  if (node.is_sink_connected("field")) {
    auto key_dpin = node.get_sink_pin("field").get_driver_pin();
    attr     = get_key_attr(key_dpin.get_type_const().to_string());
    if (attr == Attr::Set_other)
      return;
  }

  if (attr == Attr::Set_dp_assign) {
    auto dpin_lhs = node.get_sink_pin("name").get_driver_pin();
    auto dpin_rhs = node.get_sink_pin("value").get_driver_pin();
    auto      it1 = flat_bwmap.find(dpin_lhs.get_compact_flat());
    auto      it2 = flat_bwmap.find(dpin_rhs.get_compact_flat());

    I(it1!=flat_bwmap.end());
    I(it2!=flat_bwmap.end());

    auto bw_lhs = it1->second;
    auto bw_rhs = it2->second;

    if (bw_rhs.get_sbits() >= bw_lhs.get_sbits()) {
#if 0
      auto sext_node  = node.get_class_lgraph()->create_node(Ntype_op::Sext);
#else
      auto mask_node = node.get_class_lgraph()->create_node(Ntype_op::And);
      auto mask_dpin = mask_node.get_driver_pin();

      auto bw_lhs_bits = bw_lhs.is_always_positive() ? bw_lhs.get_sbits() - 1 : bw_lhs.get_sbits();

      auto mask_const   = (Lconst(1UL) << Lconst(bw_lhs_bits)) - 1;
      auto all_one_node = node.get_class_lgraph()->create_node_const(mask_const);
      auto all_one_dpin = all_one_node.setup_driver_pin();

      // Note: I set the unsigned k-bits (max, min) for the mask
      flat_bwmap.insert_or_assign(mask_dpin.get_compact_flat(), Bitwidth_range(Lconst(0), mask_const));
      dpin_rhs.connect_sink(mask_node.setup_sink_pin("A"));
      all_one_dpin.connect_sink(mask_node.setup_sink_pin("A"));
      for (auto e : node.out_edges()) mask_dpin.connect_sink(e.sink);
#endif
      node.del_node();
      return;
    }
  }

  // auto node_non_hier = node.get_non_hierarchical();
  if (node.is_sink_connected("name")) {
    auto data_dpin = node.get_sink_pin("name").get_driver_pin();

    for (auto e : node.out_edges()) {
      if (e.driver.get_pid() == 0) // No chain pin
        e.sink.connect_driver(data_dpin);
    }
  }
  node.del_node();
}

void Bitwidth::set_subgraph_boundary_bw(Node &node) {
  if (!node.is_type_sub_present()) {
    auto sub_lgid = node.get_type_sub();
    auto sub_name = node.get_class_lgraph()->get_library().get_name(sub_lgid);
    // No error because sometimes we could infer backwards the size of outputs
    Pass::info("Global IO connection pass cannot find existing subgraph {} in lgdb\n", sub_name);
    return;
  }

  auto sub_lg = node.ref_type_sub_lgraph();

  sub_lg->each_graph_output([&node, this](Node_pin &dpin_gout) {
    auto node_subg_dpin = node.setup_driver_pin(dpin_gout.get_name());

    if (flat_bwmap.find(dpin_gout.get_compact_flat()) == flat_bwmap.end()) {
      const auto bits = dpin_gout.get_bits();
      if (dpin_gout.get_bits()) {
        Bitwidth_range bw;
        bw.set_sbits_range(bits);
        flat_bwmap.insert_or_assign(node_subg_dpin.get_compact_flat(), bw);
      }

      return;
    }

    flat_bwmap.insert_or_assign(node_subg_dpin.get_compact_flat(), flat_bwmap[dpin_gout.get_compact_flat()]);
  });
}

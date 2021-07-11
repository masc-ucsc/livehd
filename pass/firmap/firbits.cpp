//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <cmath>
#include <vector>

#include "firmap.hpp"
#include "lbench.hpp"
#include "lgraph.hpp"
#include "struct_firbits.hpp"

void Firmap::dump() const {
  for (const auto &maps_it : fbmaps) {
    fmt::print("processing lgraph:{}\n", maps_it.first->get_name());

    fmt::print("  fbmap:\n");
    for (const auto &it : maps_it.second) {
      Node_pin dpin(maps_it.first, it.first);

      fmt::print("    pin:{} ", dpin.debug_name());
      it.second.dump();
    }
  }
}

void Firmap::add_map_entry(Lgraph *lg) {  // single-thread
  // WARNING: This MUST be called to map before the reads are created
  fbmaps.insert_or_assign(lg, FBMap());
  pinmaps.insert_or_assign(lg, PinMap());
  spinmaps_xorr.insert_or_assign(lg, XorrMap());
}

void Firmap::do_firbits_analysis(Lgraph *lg) {  // multi-threaded
  Lbench b("pass.firbits");

  I(fbmaps.find(lg) != fbmaps.end());  // call add_map_entry
  auto &fbmap = fbmaps.find(lg)->second;

  int firbits_iters = 0;
  do {
    if (firbits_iters > 10)
      Pass::error("FIRBITS cannot converge within 10 iterations!\n");

#ifndef NDEBUG
    fmt::print("\nFIRBITS Iteration:{}\n", firbits_iters);
#endif

    lg->regenerate_htree();  // called bottom up, and the hierarchy may have been unfinished before
    firbits_wait_flop = false;
    for (auto node : lg->forward()) {
#ifndef NDEBUG
      fmt::print("{}\n", node.debug_name());
#endif
      auto op = node.get_type_op();

      I(op != Ntype_op::Or && op != Ntype_op::Xor && op != Ntype_op::And && op != Ntype_op::Sum && op != Ntype_op::Mult
            && op != Ntype_op::SRA && op != Ntype_op::SHL && op != Ntype_op::Not && op != Ntype_op::GT && op != Ntype_op::LT
            && op != Ntype_op::EQ && op != Ntype_op::Div,
        "basic op should be a fir_op_subnode before the firmap pass!");

      if (op == Ntype_op::Ror) {
        fbmap.insert_or_assign(node.get_driver_pin().get_compact_class_driver(), Firrtl_bits(1, false));
      } else if (op == Ntype_op::Sub) {
        auto subname = node.get_type_sub_node().get_name();
        if (subname.substr(0, 6) == "__fir_")
          analysis_fir_ops(node, subname, fbmap);
        else
          continue;
      } else if (op == Ntype_op::Const) {
        analysis_lg_const(node, fbmap);
      } else if (op == Ntype_op::TupGet || op == Ntype_op::TupAdd) {
        continue;  // Nothing to do for this
      } else if (op == Ntype_op::AttrSet) {
        analysis_lg_attr_set(node, fbmap);
        if (node.is_invalid())
          continue;
      } else if (op == Ntype_op::AttrGet) {
        I(false, "firrtl ir should not have any attr_get node, it's achieved by firrtl bits op");
      } else if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
        analysis_lg_flop(node, fbmap);
      } else if (op == Ntype_op::Mux) {
        analysis_lg_mux(node, fbmap);
      } else {
        fmt::print("FIXME: node:{} still not handled by firrtl bits analysis\n", node.debug_name());
      }
    }  // end of lg->forward()
    firbits_iters++;
  } while (firbits_wait_flop);
}

void Firmap::analysis_lg_flop(Node &node, FBMap &fbmap) {
  I(node.is_sink_connected("din"));

  auto d_dpin    = node.get_sink_pin("din").get_driver_pin();
  auto it_d_dpin = fbmap.find(d_dpin.get_compact_class_driver());

  if (it_d_dpin != fbmap.end()) {
    auto bits = it_d_dpin->second.get_bits();
    auto sign = it_d_dpin->second.get_sign();
    fbmap.insert_or_assign(node.get_driver_pin().get_compact_class_driver(), Firrtl_bits(bits, sign));
    return;
  } else {
#ifndef NDEBUG
    auto qpin = node.get_driver_pin();
    fmt::print("    {} input driver {} not ready\n", node.debug_name(), d_dpin.debug_name());
    fmt::print("    {} flop q_pin {} not ready\n", node.debug_name(), qpin.debug_name());
#endif

    firbits_issues = true;
    return;
  }
}

void Firmap::analysis_lg_mux(Node &node, FBMap &fbmap) {
  auto inp_edges = node.inp_edges();
  I(inp_edges.size());  // Dangling???

  Bits_t max_bits       = 0;
  bool   sign           = false;
  bool   no_one_ready   = true;
  bool   some_one_ready = false;
  for (auto e : inp_edges) {
    if (e.sink.get_pid() == 0)
      continue;  // Skip select

    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it != fbmap.end()) {
      if (some_one_ready) {
        /* I(max_bits == it->second.get_bits()); */
        max_bits = (max_bits < it->second.get_bits()) ? it->second.get_bits() : max_bits;
        I(sign == it->second.get_sign());
        continue;
      }
      max_bits       = it->second.get_bits();
      sign           = it->second.get_sign();
      no_one_ready   = false;
      some_one_ready = true;
    }
  }

  if (no_one_ready) {
    // should wait till at least one of the inputs is ready
    firbits_issues = true;
    return;
  }
  fbmap.insert_or_assign(node.get_driver_pin().get_compact_class_driver(), Firrtl_bits(max_bits, sign));
}

void Firmap::analysis_lg_const(Node &node, FBMap &fbmap) {
  auto dpin = node.get_driver_pin();
  auto bits = node.get_type_const().get_bits() - 1;  // -1 for turn sbits to ubits
  fbmap.insert_or_assign(dpin.get_compact_class_driver(), Firrtl_bits(bits, false));
}

void Firmap::analysis_lg_attr_set(Node &node, FBMap &fbmap) {
  if (node.is_sink_connected("field")) {
    analysis_lg_attr_set_new_attr(node, fbmap);
  } else {
    analysis_lg_attr_set_propagate(node, fbmap);
  }
}

// from firrtl front-end, the only possible attr_set_propagate will happen is the variable pre-declaration before mux, so the
// attr_propagate could just follow the chain fbits
void Firmap::analysis_lg_attr_set_propagate(Node &node_attr, FBMap &fbmap) {
  I(node_attr.is_sink_connected("parent"));
  I(false);  // Chain is not longer in use. When is this called?
  auto parent_attr_dpin = node_attr.get_sink_pin("chain").get_driver_pin();

  auto parent_attr_it = fbmap.find(parent_attr_dpin.get_compact_class_driver());
  if (parent_attr_it == fbmap.end()) {
#ifndef NDEBUG
    fmt::print("    {} input driver {} not ready\n", node_attr.debug_name(), parent_attr_dpin.debug_name());
#endif
    firbits_issues = true;
    return;
  }

  const auto parent_attr_bw = parent_attr_it->second;
  for (auto out_dpin : node_attr.out_connected_pins()) {
    fbmap.insert_or_assign(out_dpin.get_compact_class_driver(), parent_attr_bw);
  }
}

void Firmap::analysis_lg_attr_set_new_attr(Node &node_attr, FBMap &fbmap) {
  I(node_attr.is_sink_connected("field"));
  auto dpin_key = node_attr.get_sink_pin("field").get_driver_pin();
  auto key      = dpin_key.get_type_const().to_string();
  auto attr     = get_key_attr(key);
  if (attr == Attr::Set_other) {
    return;  // don't care
  }

  if (attr == Attr::Set_dp_assign) {
    analysis_lg_attr_set_dp_assign(node_attr, fbmap);
    return;
  }

  I(node_attr.is_sink_connected("value"));
  auto dpin_val = node_attr.get_sink_pin("value").get_driver_pin();

  I(dpin_key.get_node().is_type_const());

  // copy parent's bw for some judgement and then update to attr_set value
  Firrtl_bits fb(0);
  bool        parent_pending   = false;
  bool        is_set_graph_inp = false;
  bool        has_through_dpin = false;
  if (node_attr.is_sink_connected("parent")) {
    auto through_dpin = node_attr.get_sink_pin("parent").get_driver_pin();
    is_set_graph_inp  = through_dpin.is_graph_input();
    auto it           = fbmap.find(through_dpin.get_compact_class_driver());
    if (it != fbmap.end()) {
      fb               = it->second;
      has_through_dpin = true;
    } else {
      parent_pending = true;
    }
  }

  if (attr == Attr::Set_ubits || attr == Attr::Set_sbits) {
    I(dpin_val.get_node().is_type_const());
    auto val = dpin_val.get_node().get_type_const();
    // auto attr_dpin = node_attr.get_driver_pin("Y");

    if (attr == Attr::Set_ubits) {
#if 0
      // NO checks. The lgtuple is not built and compares things like foo.bar.__ubits with foo.xxx.__ubits
      if (fb.get_sign() == true && has_through_dpin && !is_set_graph_inp)
        I(false, "cannot set ubits to a signed parent node in firrtl!");

      if (fb.get_bits() && (fb.get_bits()) > (val.to_i())) {
        Pass::error("Firrtl bitwidth mismatch. Variable {} needs {}ubits, but constrained to {}ubits\n", attr_dpin.debug_name(), fb.get_bits(), val.to_i());
			}
#else
      (void)has_through_dpin;
      (void)is_set_graph_inp;
#endif

      fb.set_bits_sign(val.to_i(), false);

    } else {  // Attr::Set_sbits
#if 0
      // NO checks. The lgtuple is not built and compares things like foo.bar.__ubits with foo.xxx.__ubits
      if (fb.get_sign() == false && has_through_dpin && !is_set_graph_inp)
        I(false, "cannot set sbits to an unsigned parent node in firrtl!");

      if (fb.get_bits() && fb.get_bits() > (val.to_i()))
        Pass::error("Firrtl bitwidth mismatch. Variable {} needs {}sbits, but constrained to {}sbits\n", attr_dpin.debug_name(), fb.get_bits(), val.to_i());
#endif

      fb.set_bits_sign(val.to_i(), true);
    }
  } else {
    I(false);
  }

  for (auto out_dpin : node_attr.out_connected_pins()) {
    fbmap.insert_or_assign(out_dpin.get_compact_class_driver(), fb);
  }

  // upwards propagate for one step node_attr, most graph input bits are set here
  if (parent_pending) {
    auto through_dpin = node_attr.get_sink_pin("parent").get_driver_pin();
    fbmap.insert_or_assign(through_dpin.get_compact_class_driver(), fb);
  }
}

void Firmap::analysis_lg_attr_set_dp_assign(Node &node_dp, FBMap &fbmap) {
  auto dpin_lhs = node_dp.get_sink_pin("parent").get_driver_pin();
  auto dpin_rhs = node_dp.get_sink_pin("value").get_driver_pin();

  auto        it = fbmap.find(dpin_lhs.get_compact_class_driver());
  Firrtl_bits fb_lhs(0);
  if (it != fbmap.end()) {
    fb_lhs = it->second;
  } else {
#ifndef NDEBUG
    fmt::print("    {} input driver {} not ready\n", node_dp.debug_name(), dpin_lhs.debug_name());
#endif
    firbits_issues = true;
    return;
  }

  auto        it2 = fbmap.find(dpin_rhs.get_compact_class_driver());
  Firrtl_bits fb_rhs(0);
  if (it2 != fbmap.end()) {
    fb_rhs = it2->second;
  } else {
#ifndef NDEBUG
    fmt::print("    {} input driver {} not ready\n", node_dp.debug_name(), dpin_rhs.debug_name());
#endif
    firbits_issues = true;
    return;
  }

  // note: hiFirrtl could have bitwidth mismatch between lhs/rhs in the "Connect". The LNAST
  //       will insert a dp_assign for a "Connect" when lhs is not a temporary variable (such as __F10).
  //       in such case, the lhs bitwidth must be pre-defined somewhere in the hi-firrtl, so in the firbits
  //       analysis, we just propagate the lhs bitwidth just as dp_assign lhs:= rhs in Pyrope.

  /* I(fb_lhs.get_bits() >= fb_rhs.get_bits()); */
  I(fb_lhs.get_sign() == fb_rhs.get_sign());

  fbmap.insert_or_assign(node_dp.setup_driver_pin("Y").get_compact_class_driver(), fb_lhs);
}

Firmap::Attr Firmap::get_key_attr(std::string_view key) {
  // FIXME: code duplicated in bitwidth. Create a separate class for Attr
  const auto sz = key.size();

  if (sz < 5)
    return Attr::Set_other;

  if (key.substr(sz - 5, sz) == "__max")
    return Attr::Set_max;

  if (key.substr(sz - 5, sz) == "__min")
    return Attr::Set_min;

  if (sz < 7)
    return Attr::Set_other;

  if (key.substr(sz - 7, sz) == "__ubits")
    return Attr::Set_ubits;

  if (key.substr(sz - 7, sz) == "__sbits")
    return Attr::Set_sbits;

  if (sz < 11)
    return Attr::Set_other;

  if (key.substr(sz - 11, sz) == "__dp_assign")
    return Attr::Set_dp_assign;

  return Attr::Set_other;
}

void Firmap::analysis_fir_ops(Node &node, std::string_view op, FBMap &fbmap) {
  // TODO: Create a map that indexed by op and returns a std::function (faster)

  auto inp_edges = node.inp_edges();
  if (op == "__fir_const") {
    analysis_fir_const(node, fbmap);
  } else if (op == "__fir_add" || op == "__fir_sub") {
    analysis_fir_add_sub(node, inp_edges, fbmap);
  } else if (op == "__fir_mul") {
    analysis_fir_mul(node, inp_edges, fbmap);
  } else if (op == "__fir_div") {
    analysis_fir_div(node, inp_edges, fbmap);
  } else if (op == "__fir_rem") {
    analysis_fir_rem(node, inp_edges, fbmap);
  } else if (op == "__fir_lt" || op == "__fir_leq" || op == "__fir_gt" || op == "__fir_geq" || op == "__fir_eq"
             || op == "__fir_neq") {
    analysis_fir_comp(node, inp_edges, fbmap);
  } else if (op == "__fir_pad") {
    analysis_fir_pad(node, inp_edges, fbmap);
  } else if (op == "__fir_as_uint") {
    analysis_fir_as_uint(node, inp_edges, fbmap);
  } else if (op == "__fir_as_sint") {
    analysis_fir_as_sint(node, inp_edges, fbmap);
  } else if (op == "__fir_as_clock") {
    analysis_fir_as_clock(node, inp_edges, fbmap);
  } else if (op == "__fir_as_async") {
    I(false);  // TODO
  } else if (op == "__fir_shl") {
    analysis_fir_shl(node, inp_edges, fbmap);
  } else if (op == "__fir_shr") {
    analysis_fir_shr(node, inp_edges, fbmap);
  } else if (op == "__fir_dshl") {
    analysis_fir_dshl(node, inp_edges, fbmap);
  } else if (op == "__fir_dshr") {
    analysis_fir_dshr(node, inp_edges, fbmap);
  } else if (op == "__fir_cvt") {
    analysis_fir_cvt(node, inp_edges, fbmap);
  } else if (op == "__fir_neg") {
    analysis_fir_neg(node, inp_edges, fbmap);
  } else if (op == "__fir_not") {
    analysis_fir_not(node, inp_edges, fbmap);
  } else if (op == "__fir_and" || op == "__fir_or" || op == "__fir_xor") {
    analysis_fir_bitwise(node, inp_edges, fbmap);
  } else if (op == "__fir_andr" || op == "__fir_orr" || op == "__fir_xorr") {
    analysis_fir_bitwire_reduction(node, inp_edges, fbmap);
  } else if (op == "__fir_bits") {
    analysis_fir_bits_extract(node, inp_edges, fbmap);
  } else if (op == "__fir_cat") {
    analysis_fir_cat(node, inp_edges, fbmap);
  } else if (op == "__fir_head") {
    analysis_fir_head(node, inp_edges, fbmap);
  } else if (op == "__fir_tail") {
    analysis_fir_tail(node, inp_edges, fbmap);
  } else {
    I(false, "typo?");
  }
}

FBMap::iterator Firmap::get_fbits_from_hierarchy(XEdge &e) {
  auto h_spin      = e.sink.get_hierarchical();
  auto driver_list = h_spin.inp_drivers();
  I(driver_list.size() == 1);
  auto h_dpin = driver_list[0];

  I(!h_dpin.is_invalid());  // connected
  I(h_dpin != e.driver);
  auto *hier_lg = h_dpin.get_node().get_class_lgraph();
  I(fbmaps.find(hier_lg) != fbmaps.end());
  auto &hier_fbmap = fbmaps[hier_lg];

  auto it = hier_fbmap.find(h_dpin.get_compact_class_driver());
  if (it == hier_fbmap.end()) {
#ifndef NDEBUG
    Pass::error("{} input driver {} not ready\n", e.sink.get_node().debug_name(), e.driver.debug_name());
#endif
    firbits_issues = true;
  }
  return it;
}

void Firmap::analysis_fir_tail(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  for (auto &e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1 - bits2, false));
}

void Firmap::analysis_fir_head(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits2 = 0;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      continue;
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits2, false));
}

void Firmap::analysis_fir_bits_extract(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 3);
  Bits_t hi = 0, lo = 0;
  bool   sign = false;
  (void)sign;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      sign = it->second.get_sign();
    } else if (e.sink.get_pin_name() == "e2") {
      hi = e.driver.get_node().get_type_const().to_i();
    } else {
      lo = e.driver.get_node().get_type_const().to_i();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(hi - lo + 1, false));
}

void Firmap::analysis_fir_cat(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign());  // inputs of firrtl add must have same sign
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1 + bits2, false));
}

void Firmap::analysis_fir_bitwire_reduction(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 1);
  // a single bit wire, need no firbits propagation
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(1, false));
}
void Firmap::analysis_fir_bitwise(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign());  // inputs of firrtl bitwise must have same sign
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(std::max(bits1, bits2), sign));
}

void Firmap::analysis_fir_not(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 1);

  Bits_t bits1 = 0;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    }
  }

  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1, false));
}

void Firmap::analysis_fir_neg(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 1);

  Bits_t bits1 = 0;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    }
  }

  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1 + 1, true));
}

void Firmap::analysis_fir_cvt(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 1);

  Bits_t bits1 = 0;
  bool   sign  = false;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    }
  }

  if (sign) {
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1, true));
  } else {
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1 + 1, true));
  }
}

void Firmap::analysis_fir_dshr(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);
  Bits_t bits1 = 0, bits2 = 0;
  (void)bits2;
  bool sign = 0;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1, sign));
}

void Firmap::analysis_fir_dshl(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1 + std::pow(2, bits2) - 1, sign));
}
void Firmap::analysis_fir_shr(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      bits2 = it->second.get_bits();
    }
  }

  if ((bits1 - bits2) < 1) {
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(1, sign));
  } else {
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1 - bits2, sign));
  }
}

void Firmap::analysis_fir_shl(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1 + bits2, sign));
}

void Firmap::analysis_fir_as_sint(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 1);

  Bits_t bits1 = 0;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1, true));
}

void Firmap::analysis_fir_as_uint(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 1);

  Bits_t bits1 = 0;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1, false));
}

void Firmap::analysis_fir_as_clock(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 1);

  Bits_t bits1 = 0;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1, false));
}

void Firmap::analysis_fir_pad(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(std::max(bits1, bits2), sign));
}

void Firmap::analysis_fir_comp(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size());
  bool sign;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      sign = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign());  // inputs of firrtl div must have same sign
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(1, false));
}

void Firmap::analysis_fir_rem(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size());

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign());  // inputs of firrtl rem must have same sign
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(std::min(bits1, bits2), sign));
}

void Firmap::analysis_fir_div(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size());

  Bits_t bits1 = 0;
  bool   sign  = false;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign());  // inputs of firrtl div must have same sign
    }
  }

  if (sign)
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1 + 1, sign));
  else
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1, sign));
}

void Firmap::analysis_fir_mul(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign());  // inputs of firrtl mul must have same sign
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1 + bits2, sign));
}

void Firmap::analysis_fir_const(Node &node, FBMap &fbmap) {
  Bits_t      bits;
  bool        sign;
  std::string const_str = (std::string)node.setup_driver_pin("Y").get_name();
  auto        pos1      = const_str.find("ubits");  // ex: 0ubits8
  auto        pos2      = const_str.find("sbits");
  I(pos1 != std::string_view::npos || pos2 != std::string_view::npos);
  if (pos1 != std::string_view::npos) {
    sign = false;
    bits = std::stoi(const_str.substr(pos1 + 5));
  } else {
    sign = true;
    bits = std::stoi(const_str.substr(pos2 + 5));
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits, sign));
}

void Firmap::analysis_fir_add_sub(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true)
        return;

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }

      it = get_fbits_from_hierarchy(e);
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign());  // inputs of firrtl add must have same sign
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(std::max(bits1, bits2) + 1, sign));
}

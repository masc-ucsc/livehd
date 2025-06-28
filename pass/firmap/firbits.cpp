//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <format>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>

#include "firmap.hpp"
#include "lgraph.hpp"
#include "perf_tracing.hpp"
#include "str_tools.hpp"
#include "struct_firbits.hpp"

void Firmap::dump() const {
  for (const auto &maps_it : fbmaps) {
    std::print("processing lgraph:{}\n", maps_it.first->get_name());

    std::cout << "  fbmap:\n";
    for (const auto &it : maps_it.second) {
      Node_pin dpin(maps_it.first, it.first);

      std::print("    pin:{} ", dpin.debug_name());
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

void Firmap::do_firbits_analysis(Lgraph *lg) {  // multi-threade
  // TRACE_EVENT("pass", nullptr, [&lg](perfetto::EventContext ctx) { ctx.event()->set_name("firbits." + lg->get_name()); });
  //
  // note: tricks to make perfetto display different color on sub-modules
  TRACE_EVENT("pass", nullptr, [&lg](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    ctx.event()->set_name(absl::StrCat(converted_str, lg->get_name()));
  });

  I(fbmaps.find(lg) != fbmaps.end());  // call add_map_entry
  auto &fbmap = fbmaps.find(lg)->second;

  int firbits_iters = 0;
  do {
    if (firbits_iters > 10) {
      Pass::error("FIRBITS cannot converge within 10 iterations!\n");
    }

#ifndef NDEBUG
    std::print("\nFIRBITS Iteration:{}\n", firbits_iters);
#endif

    firbits_wait_flop = false;

    for (auto node : lg->forward()) {
      auto op = node.get_type_op();

      I(op != Ntype_op::Or && op != Ntype_op::Xor && op != Ntype_op::And && op != Ntype_op::Sum && op != Ntype_op::Mult
            && op != Ntype_op::SRA && op != Ntype_op::SHL && op != Ntype_op::Not && op != Ntype_op::GT && op != Ntype_op::LT
            && op != Ntype_op::EQ && op != Ntype_op::Div,
        "basic op should be a fir_op_subnode before the firmap pass!");

      if (op == Ntype_op::Ror) {
        fbmap.insert_or_assign(node.get_driver_pin().get_compact_class_driver(), Firrtl_bits(1, false));
      } else if (op == Ntype_op::Sub) {
        auto subname = node.get_type_sub_node().get_name();
        if (subname.substr(0, 6) == "__fir_") {
          analysis_fir_ops(node, subname, fbmap);
        } else {
          continue;
        }
      } else if (op == Ntype_op::Const) {
        analysis_lg_const(node, fbmap);
      } else if (op == Ntype_op::TupGet || op == Ntype_op::TupAdd) {
        I(false);  // cprop should clean up all the TupAdd and TupGet nodes
      } else if (op == Ntype_op::AttrSet) {
        analysis_lg_attr_set(node, fbmap);
        if (node.is_invalid()) {
          continue;
        }
      } else if (op == Ntype_op::AttrGet) {
        I(false, "firrtl ir should not have any attr_get node, it's achieved by firrtl bits op");
      } else if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
        analysis_lg_flop(node, fbmap);
      } else if (op == Ntype_op::Mux) {
        analysis_lg_mux(node, fbmap);
      } else if (op == Ntype_op::Memory) {
        continue;
      } else {
        Pass::error("FIXME: node:{} still not handled by firrtl bits analysis\n", node.debug_name());
      }
    }  // end of lg->forward()
    firbits_iters++;
  } while (firbits_wait_flop);
}

/* * *
 * we know firrtl flop bits originally, set on the qpin directly, no need to be propagated from other node
 * * */
void Firmap::analysis_lg_flop(Node &node, FBMap &fbmap) {
  I(node.is_sink_connected("din"));
  auto qpin = node.get_driver_pin();
  // I(qpin.get_bits() > 0);
  if (qpin.get_bits() > 0) {
    auto bits = qpin.get_bits() - 1;  // turn from lgraph signed bits back to firrtl ubits
    auto it   = fbmap.find(qpin.get_compact_class_driver());
    if (it == fbmap.end()) {
      fbmap.insert_or_assign(qpin.get_compact_class_driver(), Firrtl_bits(bits, false));
    }
    return;
  }

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
    std::print("    {} input driver {} not ready\n", node.debug_name(), d_dpin.debug_name());
    std::print("    {} flop q_pin {} not ready\n", node.debug_name(), qpin.debug_name());
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
    if (e.sink.get_pid() == 0) {
      continue;  // Skip select
    }

    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true) {
        return;
      }

      // driver is not from other sugraph, wait next iteration for Flop being solved
      if (e.driver.get_type_op() == Ntype_op::Flop) {
        firbits_wait_flop = true;
        return;
      }
      it = get_fbits_from_hierarchy(e);

      if (some_one_ready) {
        max_bits = (max_bits < it->second.get_bits()) ? it->second.get_bits() : max_bits;
        I(sign == it->second.get_sign());
        continue;
      }
      max_bits       = it->second.get_bits();
      sign           = it->second.get_sign();
      no_one_ready   = false;
      some_one_ready = true;
      fbmap.insert_or_assign(node.get_driver_pin().get_compact_class_driver(), Firrtl_bits(max_bits, sign));
    } else {
      if (some_one_ready) {
        max_bits = (max_bits < it->second.get_bits()) ? it->second.get_bits() : max_bits;
        I(sign == it->second.get_sign());
        continue;
      }
      max_bits       = it->second.get_bits();
      sign           = it->second.get_sign();
      no_one_ready   = false;
      some_one_ready = true;
      fbmap.insert_or_assign(node.get_driver_pin().get_compact_class_driver(), Firrtl_bits(max_bits, sign));
    }
  }

  if (no_one_ready) {
    node.dump();
    std::cout << "          driver 1\n";
    node.get_sink_pin_raw(1).get_driver_node().dump();
    std::cout << "          driver 2\n";
    node.get_sink_pin_raw(2).get_driver_node().dump();

    // should wait till at least one of the inputs is ready
    firbits_issues = true;
    return;
  }
  // fbmap.insert_or_assign(node.get_driver_pin().get_compact_class_driver(), Firrtl_bits(max_bits, sign));
}

void Firmap::analysis_lg_const(Node &node, FBMap &fbmap) {
  auto dpin = node.get_driver_pin();
  auto bits = node.get_type_const().get_bits() - 1;  // -1 for turn sbits to ubits
  bits      = bits == -1 ? 1 : bits;                 // the case of const 0
  fbmap.insert_or_assign(dpin.get_compact_class_driver(), Firrtl_bits(bits, false));
}

void Firmap::analysis_lg_attr_set(Node &node_attr, FBMap &fbmap) {
  I(node_attr.is_sink_connected("field"));
  auto dpin_key = node_attr.get_sink_pin("field").get_driver_pin();
  auto key      = dpin_key.get_type_const().to_firrtl();
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

  // copy parent's fb for some judgement and then update to attr_set value
  Firrtl_bits fb;
  bool        parent_pending = false;
  auto        parent_dpin    = node_attr.get_sink_pin("parent").get_driver_pin();
  if (node_attr.is_sink_connected("parent")) {
    auto it = fbmap.find(parent_dpin.get_compact_class_driver());
    if (it != fbmap.end()) {
      fb = it->second;
    } else {
      parent_pending = true;
    }
  }

  I(attr == Attr::Set_ubits || attr == Attr::Set_sbits);
  I(dpin_val.get_node().is_type_const());
  I(!parent_dpin.is_invalid());

  auto val  = dpin_val.get_node().get_type_const();
  auto bits = static_cast<Bits_t>(val.to_i());

  if (attr == Attr::Set_ubits) {
    fb.set_bits_sign(bits, false);
  } else {  // Attr::Set_sbits
    fb.set_bits_sign(bits, true);
  }

  auto attr_dpin = node_attr.get_driver_pin();

  // original
  if (parent_dpin.is_graph_input()) {
    for (auto out_dpin : node_attr.out_connected_pins()) {
      fbmap.insert_or_assign(out_dpin.get_compact_class_driver(), fb);
    }
  } else if (attr_dpin.has_name() && attr_dpin.get_name().at(0) == '%') {
    fbmap.insert_or_assign(attr_dpin.get_compact_class_driver(), fb);
  } else {
    for (auto &e : node_attr.out_edges()) {
      auto sink_node = e.sink.get_node();
      if (sink_node.is_type_flop()) {
        auto dpin_of_sink_node = sink_node.get_driver_pin("Y");
        dpin_of_sink_node.set_bits(bits
                                   + 1);  // lgraph assumes signed bits, so the ubits needs to be incremented by 1 to be signed bits
        fbmap.insert_or_assign(dpin_of_sink_node.get_compact_class_driver(), fb);
      }
    }
    fbmap.insert_or_assign(attr_dpin.get_compact_class_driver(), fb);
  }

  // upwards propagate for one step node_attr, most graph input bits are set here
  if (parent_pending) {
    fbmap.insert_or_assign(parent_dpin.get_compact_class_driver(), fb);
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
    std::print("    {} input driver {} not ready\n", node_dp.debug_name(), dpin_lhs.debug_name());
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
    std::print("    {} input driver {} not ready\n", node_dp.debug_name(), dpin_rhs.debug_name());
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

  if (sz < 5) {
    return Attr::Set_other;
  }

  if (str_tools::ends_with(key, "__max")) {
    return Attr::Set_max;
  }

  if (str_tools::ends_with(key, "__min")) {
    return Attr::Set_min;
  }

  if (sz < 7) {
    return Attr::Set_other;
  }

  if (str_tools::ends_with(key, "__ubits")) {
    return Attr::Set_ubits;
  }

  if (str_tools::ends_with(key, "__sbits")) {
    return Attr::Set_sbits;
  }

  if (sz < 11) {
    return Attr::Set_other;
  }

  if (str_tools::ends_with(key, "__dp_assign")) {
    return Attr::Set_dp_assign;
  }

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
  } else if (op == "__fir_lt" || op == "__fir_leq" || op == "__fir_gt" || op == "__fir_geq" || op == "__fir_eq"
             || op == "__fir_neq") {
    analysis_fir_comp(node, inp_edges, fbmap);
  } else if (op == "__fir_pad") {
    analysis_fir_pad(node, inp_edges, fbmap);
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
  } else if (op == "__fir_as_sint") {
    analysis_fir_single_input_op(node, inp_edges, fbmap, true);
  } else if (op == "__fir_as_uint" || op == "__fir_as_sint" || op == "__fir_as_async" || op == "__fir_as_clock"
             || op == "__fir_not") {
    analysis_fir_single_input_op(node, inp_edges, fbmap, false);
  } else if (op == "__fir_rem") {
    analysis_fir_rem(node, inp_edges, fbmap);
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
  // I(h_dpin != e.driver);
  auto *hier_lg = h_dpin.get_node().get_class_lgraph();
  I(fbmaps.find(hier_lg) != fbmaps.end());
  auto &hier_fbmap = fbmaps[hier_lg];

  auto it = hier_fbmap.find(h_dpin.get_compact_class_driver());
  if (it == hier_fbmap.end()) {
#ifndef NDEBUG
    // hier_lg->dump();
    std::cout << "----------------------\n";
    std::cout << "DEBUG driver node dump\n";
    e.driver.get_node().dump();
    std::cout << "DEBUG sink node dump\n";
    e.sink.get_node().dump();
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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(std::max(bits1, bits2), false));
}

void Firmap::analysis_fir_not(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 1);

  Bits_t bits1 = 0;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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

void Firmap::analysis_fir_single_input_op(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap, bool sign) {
  I(inp_edges.size() == 1);

  Bits_t bits1 = 0;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true) {
        return;
      }

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
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1, sign));
}

void Firmap::analysis_fir_as_async_reset(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 1);

  Bits_t bits1 = 0;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
  auto dpin_cmpt = node.get_driver_pin("Y").get_compact_class_driver();
  auto it2       = fbmap.find(dpin_cmpt);
  if (it2 != fbmap.end()) {
    return;
  }

  I(inp_edges.size());
  bool sign;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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
      if (firbits_wait_flop == true) {
        return;
      }

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

  if (sign) {
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1 + 1, sign));
  } else {
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits1, sign));
  }
}

void Firmap::analysis_fir_mul(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true) {
        return;
      }

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
  Bits_t bits;
  bool   sign;
  auto   const_str = node.setup_driver_pin("Y").get_name();
  auto   pos1      = const_str.find("ubits");  // ex: 0ubits8
  if (pos1 != std::string_view::npos) {
    sign = false;
    bits = str_tools::to_i(const_str.substr(pos1 + 5));
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact_class_driver(), Firrtl_bits(bits, sign));
    return;
  }

  auto pos2 = const_str.find("sbits");
  I(pos1 != std::string_view::npos || pos2 != std::string_view::npos);
  if (pos2 != std::string_view::npos) {
    sign = true;
    bits = str_tools::to_i(const_str.substr(pos2 + 5));
    return;
  }
}

void Firmap::analysis_fir_add_sub(Node &node, XEdge_iterator &inp_edges, FBMap &fbmap) {
  I(inp_edges.size() == 2);

  Bits_t bits1 = 0, bits2 = 0;
  bool   sign = false;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end()) {
      if (firbits_wait_flop == true) {
        return;
      }

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

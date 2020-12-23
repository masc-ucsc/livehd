//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <cmath>
#include <vector>

#include "firmap.hpp"
#include "lbench.hpp"
#include "lgraph.hpp"
#include "struct_firbits.hpp"

void Firmap::do_firbits_analysis(LGraph *lg) {
  for (auto node : lg->forward()) {
    fmt::print("{}\n", node.debug_name());
    auto op        = node.get_type_op();

    I(op != Ntype_op::Or  && op != Ntype_op::Xor  && op != Ntype_op::Ror && op != Ntype_op::And &&
      op != Ntype_op::Sum && op != Ntype_op::Mult && op != Ntype_op::SRA && op != Ntype_op::SHL && 
      op != Ntype_op::Not && op != Ntype_op::GT   && op != Ntype_op::LT  && op != Ntype_op::EQ  &&
      op != Ntype_op::Div, "basic op should be a fir_op_subnode before the firmap pass!");

    if (op == Ntype_op::Sub) {
      auto subname = node.get_type_sub_node().get_name();
      if ( subname.substr(0,5) == "__fir") 
        analysis_fir_ops(node, subname);
      else 
        continue;
    } else if (op == Ntype_op::Const) {
      analysis_lg_const(node);
    } else if (op == Ntype_op::TupKey || op == Ntype_op::TupGet || op == Ntype_op::TupAdd) {
      continue; // Nothing to do for this
    } else if (op == Ntype_op::AttrSet) {
      analysis_lg_attr_set(node);
      if (node.is_invalid())
        continue;
    } else if (op == Ntype_op::AttrGet) {
      I(false, "firrtl ir should not have any attr_get node, it's achieved by firrtl bits op");
    } else if (op == Ntype_op::Sflop || op == Ntype_op::Aflop || op == Ntype_op::Fflop) {
      analysis_lg_flop(node);
    } else if (op == Ntype_op::Mux) {
      analysis_lg_mux(node);
    } else {
      fmt::print("FIXME: node:{} still not handled by firrtl bits analysis\n", node.debug_name());
    }

    //debug
    auto it = fbmap.find(node.get_driver_pin("Y").get_compact());
    if (it != fbmap.end()) {
      fmt::print("    ");
      it->second.dump();
    }
  } // end of lg->forward()
}


void Firmap::analysis_lg_flop(Node &node) {
  I(node.is_sink_connected("din"));
  auto d_dpin = node.get_sink_pin("din").get_driver_pin();
  auto qpin = node.get_driver_pin();

  auto   it_d_dpin = fbmap.find(d_dpin.get_compact());
  auto   it_qpin   = fbmap.find(qpin.get_compact());

  if (it_d_dpin != fbmap.end()) {
    auto bits = it_d_dpin->second.get_bits(); 
    auto sign = it_d_dpin->second.get_sign();
    fbmap.insert_or_assign(node.get_driver_pin().get_compact(), Firrtl_bits(bits, sign));
    return;
  } else if (it_qpin != fbmap.end()) {  // At least propagate backward the width
    auto bits = it_qpin->second.get_bits(); 
    auto sign = it_qpin->second.get_sign();
    fbmap.insert_or_assign(d_dpin.get_compact(), Firrtl_bits(bits, sign));
    return;
  } else {
    fmt::print("    {} input driver {} not ready\n", node.debug_name(), d_dpin.debug_name());
    fmt::print("    {} flop q_pin {} not ready\n", node.debug_name(), qpin.debug_name());
    not_finished = true;
    return;
  }
}

void Firmap::analysis_lg_mux(Node &node) {
  auto inp_edges = node.inp_edges();
  I(inp_edges.size());  // Dangling???

  Bits_t max_bits = 0;
  bool sign = false;
  bool is_1st_input = true;
  for (auto e : inp_edges) {
    if (e.sink.get_pid() == 0)
      continue;  // Skip select

    auto it = fbmap.find(e.driver.get_compact());
    if (it != fbmap.end()) {
      if (is_1st_input) {
        max_bits = it->second.get_bits();
        sign = it->second.get_sign();
        is_1st_input = false;
      } else {
        I (sign == it->second.get_sign());
        max_bits = (max_bits < it->second.get_bits()) ? it->second.get_bits() : max_bits;
      }
    } else {
      // should wait till every input is ready
      not_finished = true;
      return;
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin().get_compact(), Firrtl_bits(max_bits, sign));
}

void Firmap::analysis_lg_const(Node &node) {
  auto dpin = node.get_driver_pin();
  auto bits = node.get_type_const().get_bits() - 1 ; // -1 for turn sbits to ubits
  fbmap.insert_or_assign(dpin.get_compact(), Firrtl_bits(bits, false));
}


void Firmap::analysis_lg_attr_set(Node &node) {
  if (node.is_sink_connected("field")) {
    analysis_lg_attr_set_new_attr(node);
  } else {
    analysis_lg_attr_set_propagate(node);
  }
}

// from firrtl front-end, the only possible attr_set_propagate will happen is the variable pre-declaration before mux, so the attr_propagate could just follow the chain fbits
void Firmap::analysis_lg_attr_set_propagate(Node &node_attr) {
  I(node_attr.is_sink_connected("name"));
  I(node_attr.is_sink_connected("chain"));
  auto parent_attr_dpin = node_attr.get_sink_pin("chain").get_driver_pin();

  auto parent_attr_it = fbmap.find(parent_attr_dpin.get_compact());
  if (parent_attr_it == fbmap.end()) {
    fmt::print("    {} input driver {} not ready\n", node_attr.debug_name(), parent_attr_dpin.debug_name());
    not_finished = true;
    return;
  }

  const auto parent_attr_bw = parent_attr_it->second;
  for (auto out_dpin : node_attr.out_connected_pins())
    fbmap.insert_or_assign(out_dpin.get_compact(), parent_attr_bw);
}



void Firmap::analysis_lg_attr_set_new_attr(Node &node_attr) {
  I(node_attr.is_sink_connected("field"));
  auto dpin_key = node_attr.get_sink_pin("field").get_driver_pin();
  auto key      = dpin_key.get_name();
  auto attr     = get_key_attr(key);
  if (attr == Attr::Set_other) {
    return; // don't care
  }

  if (attr == Attr::Set_dp_assign) {
    analysis_lg_attr_set_dp_assign(node_attr);
    return;
  }

  I(node_attr.is_sink_connected("value"));
  auto dpin_val = node_attr.get_sink_pin("value").get_driver_pin();

  I(dpin_key.get_node().is_type(Ntype_op::TupKey));
  I(dpin_key.has_name());

  auto attr_dpin = node_attr.get_driver_pin("Y");

  std::string_view dpin_name;
  if (attr_dpin.has_name())
    dpin_name = attr_dpin.get_name();

  // copy parent's bw for some judgement and then update to attr_set value
  Firrtl_bits fb(0);
  bool parent_pending = false;
  bool is_set_graph_inp = false;
  bool has_through_dpin = false;
  if (node_attr.is_sink_connected("name")) {
    auto through_dpin = node_attr.get_sink_pin("name").get_driver_pin();
    is_set_graph_inp = through_dpin.is_graph_input();
    auto it           = fbmap.find(through_dpin.get_compact());
    if (it != fbmap.end()) {
      fb = it->second;
      has_through_dpin = true;
    } else {
      parent_pending = true;
    }
  }

  if (attr == Attr::Set_ubits || attr == Attr::Set_sbits) {
    I(dpin_val.get_node().is_type_const());
    auto val = dpin_val.get_node().get_type_const();
    
    if (attr == Attr::Set_ubits) {
      if (fb.get_sign() == true && has_through_dpin && !is_set_graph_inp)
        I(false, "cannot set ubits to a signed parent node in firrtl!");

      if (fb.get_bits() && (fb.get_bits()) > (val.to_i()))   
        Pass::error("Firrtl bitwidth mismatch. Variable {} needs {}ubits, but constrained to {}ubits\n", dpin_name, fb.get_bits(), val.to_i());
      fb.set_bits_sign(val.to_i(), false); 

    } else { // Attr::Set_sbits
      if (fb.get_sign() == false && has_through_dpin && !is_set_graph_inp)
        I(false, "cannot set sbits to an unsigned parent node in firrtl!");

      if (fb.get_bits() && fb.get_bits() > (val.to_i())) 
        Pass::error("Firrtl bitwidth mismatch. Variable {} needs {}sbits, but constrained to {}sbits\n", dpin_name, fb.get_bits(), val.to_i());
      
      fb.set_bits_sign(val.to_i(), true);
    }
  } else {
    I(false); 
  }

  for (auto out_dpin : node_attr.out_connected_pins()) {
    fbmap.insert_or_assign(out_dpin.get_compact(), fb);
  }

  // upwards propagate for one step node_attr, most graph input bits are set here
  if (parent_pending) {
    auto through_dpin = node_attr.get_sink_pin("name").get_driver_pin();
    fbmap.insert_or_assign(through_dpin.get_compact(), fb);
  }
}


void Firmap::analysis_lg_attr_set_dp_assign(Node &node_dp) {
  auto dpin_lhs = node_dp.get_sink_pin("value").get_driver_pin(); 
  auto dpin_rhs = node_dp.get_sink_pin("name").get_driver_pin();  

  auto it = fbmap.find(dpin_lhs.get_compact());
  Firrtl_bits fb_lhs(0);
  if (it != fbmap.end()) {
    fb_lhs = it->second;
  } else {
    Pass::error("dp lhs firrtl bits must be ready even at first traverse, lhs:{}\n", dpin_lhs.debug_name());
  }

  auto it2 = fbmap.find(dpin_rhs.get_compact());
  Firrtl_bits fb_rhs(0);
  if (it2 != fbmap.end()) {
    fb_rhs = it2->second;
  } else {
    Pass::error("dp rhs firrtl bits must be ready even at first traverse, rhs:{}\n", dpin_rhs.debug_name());
  }

  // note: up to now, if something has been converted to dp, the lhs_firbits must >= rhs
  I(fb_lhs.get_bits() >= fb_rhs.get_bits());
  I(fb_lhs.get_sign() == fb_rhs.get_sign());

  fbmap.insert_or_assign(node_dp.setup_driver_pin("Y").get_compact(), fb_lhs);
}


Firmap::Attr Firmap::get_key_attr(std::string_view key) {
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


void Firmap::analysis_fir_ops(Node &node, std::string_view op) {
  auto inp_edges = node.inp_edges();
  if (op == "__fir_add" || op == "__fir_sub") {
    analysis_fir_add_sub(node, inp_edges);
  } else if (op == "__fir_mul") {
    analysis_fir_mul(node, inp_edges);
  } else if (op == "__fir_div") {
    analysis_fir_div(node, inp_edges);
  } else if (op == "__fir_rem") {
    analysis_fir_rem(node, inp_edges);
  } else if (op == "__fir_lt" || op == "__fir_leq" || op == "__fir_gt" || op == "__fir_geq" || op == "__fir_eq" || op == "__fir_neq") {
    analysis_fir_comp(node, inp_edges);
  } else if (op == "__fir_pad") {
    analysis_fir_pad(node, inp_edges);
  } else if (op == "__fir_as_uint" ) {
    analysis_fir_as_uint(node, inp_edges);
  } else if (op == "__fir_as_sint") {
    analysis_fir_as_sint(node, inp_edges);
  } else if (op == "__fir_shl") {
    analysis_fir_shl(node, inp_edges);
  } else if (op == "__fir_shr") {
    analysis_fir_shr(node, inp_edges);
  } else if (op == "__fir_dshl") {
    analysis_fir_dshl(node, inp_edges);
  } else if (op == "__fir_dshr") {
    analysis_fir_dshr(node, inp_edges);
  } else if (op == "__fir_cvt") {
    analysis_fir_cvt(node, inp_edges);
  } else if (op == "__fir_neg") {
    analysis_fir_neg(node, inp_edges);
  } else if (op == "__fir_not") {
    analysis_fir_not(node, inp_edges);
  } else if (op == "__fir_and" || op == "__fir_or" || op == "__fir_xor") {
    analysis_fir_bitwise(node, inp_edges);
  } else if (op == "__fir_andr" || op == "__fir_orr" || op == "__fir_xorr") {
    analysis_fir_bitwire_reduction(node, inp_edges);
  } else if (op == "__fir_bits") {
    analysis_fir_bits_extract(node, inp_edges);
  } else if (op == "__fir_cat") {
    analysis_fir_cat(node, inp_edges);
  } else if (op == "__fir_head") {
    analysis_fir_head(node, inp_edges);
  } else if (op == "__fir_tail") {
    analysis_fir_tail(node, inp_edges);
  } else {
    I(false, "typo?");
  }
}

void Firmap::analysis_fir_tail(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits1, bits2;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1 - bits2, false));
}


void Firmap::analysis_fir_head(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits2;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      continue;
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits2, false));
}


void Firmap::analysis_fir_bits_extract(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 3);  

  Bits_t hi, lo;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      sign  = it->second.get_sign();
    } else if (e.sink.get_pin_name() == "e2"){
      hi = e.driver.get_node().get_type_const().to_i();
    } else {
      lo = e.driver.get_node().get_type_const().to_i();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(hi - lo + 1, false));
}

void Firmap::analysis_fir_cat(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits1, bits2;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign()); // inputs of firrtl add must have same sign
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1 + bits2, false));
}


void Firmap::analysis_fir_bitwire_reduction(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 1);  
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(1, false));
}
void Firmap::analysis_fir_bitwise(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits1, bits2;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign()); // inputs of firrtl bitwise must have same sign
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(std::max(bits1, bits2), sign));
}


void Firmap::analysis_fir_not(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 1);  
  
  Bits_t bits1;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    } 
  }

  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1, false));
}


void Firmap::analysis_fir_neg(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 1);  
  
  Bits_t bits1;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    } 
  }

  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1 + 1, true));
}


void Firmap::analysis_fir_cvt(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 1);  
  
  Bits_t bits1;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } 
  }

  if (sign) {
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1, true));
  } else {
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1 + 1, true));
  }
}


void Firmap::analysis_fir_dshr(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits1, bits2;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1, sign));
}


void Firmap::analysis_fir_dshl(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits1, bits2;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1 + std::pow(2, bits2) - 1, sign));
}
void Firmap::analysis_fir_shr(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits1, bits2;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      bits2 = it->second.get_bits();
    }
  }

  if ((bits1 - bits2) < 1) {
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(1, sign));
  } else {
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1 - bits2, sign));
  }
}

void Firmap::analysis_fir_shl(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits1, bits2;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1 + bits2, sign));
}

void Firmap::analysis_fir_as_sint(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 1);  

  Bits_t bits1;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    }  
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1, true));
}

void Firmap::analysis_fir_as_uint(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 1);  

  Bits_t bits1;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
    }  
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1, false));
}

void Firmap::analysis_fir_pad(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits1, bits2;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(std::max(bits1, bits2), sign));
}

void Firmap::analysis_fir_comp(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  
  bool sign;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      sign = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign()); // inputs of firrtl div must have same sign
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(1, false));
}


void Firmap::analysis_fir_rem(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  

  Bits_t bits1, bits2;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign()); // inputs of firrtl rem must have same sign
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(std::min(bits1, bits2), sign));
}

void Firmap::analysis_fir_div(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  

  Bits_t bits1;
  bool sign;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign()); // inputs of firrtl div must have same sign
    }
  }

  if (sign)
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1 + 1, sign));
  else 
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1, sign));
}

void Firmap::analysis_fir_mul(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits1, bits2;
  bool sign;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign()); // inputs of firrtl mul must have same sign
      bits2 = it->second.get_bits();
    }
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(bits1 + bits2, sign));
}


void Firmap::analysis_fir_add_sub(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);  

  Bits_t bits1, bits2;
  bool sign;
  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end()) {
      fmt::print("    {} input driver {} not ready\n", node.debug_name(), e.driver.debug_name());
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "e1") {
      bits1 = it->second.get_bits();
      sign  = it->second.get_sign();
    } else {
      I(sign == it->second.get_sign()); // inputs of firrtl add must have same sign
      bits2 = it->second.get_bits();
    }
  }
  
    fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(std::max(bits1, bits2) + 1, sign));
}



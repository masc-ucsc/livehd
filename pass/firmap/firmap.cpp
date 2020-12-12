//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <cmath>
#include <vector>

#include "firmap.hpp"
#include "lbench.hpp"
#include "lgraph.hpp"


Firmap::Firmap() {}

void Firmap::do_analysis(LGraph *lg) {
  /* Lbench b("pass.firmap"); */
  firbits_analysis(lg);
}


LGraph* Firmap::do_mapping(LGraph *lg) {
  /* Lbench b("pass.firmap"); */
  return firrtl_mapping(lg);
}

LGraph* Firmap::firrtl_mapping(LGraph *lg) {
  auto lg_name = lg->get_name();
  auto pos = lg_name.find("_firrtl");
  LGraph *new_lg = lg->clone_skeleton(lg_name.substr(0, pos));

  for (auto node : lg->forward()) {
    auto op = node.get_type_op();
    fmt::print("{}\n", node.debug_name());
    if (op == Ntype_op::Sub) {
      auto subname = node.get_type_sub_node().get_name();
      if ( subname.substr(0,5) == "__fir") 
        map_fir_ops(node, subname, new_lg);
      else 
        clone_lg_ops(node, new_lg);
    } else {
      clone_lg_ops(node, new_lg); 
    }
  }
  return new_lg;
}

void Firmap::map_fir_ops(Node &node, std::string_view op, LGraph *new_lg) {
  if (op == "__fir_add") {
    map_fir_add(node, new_lg);
  } else if (op == "__fir_sub") {
    map_fir_sub(node, new_lg);
  }
}

void Firmap::map_fir_add(Node &node, LGraph *new_lg) {
;//FIXME->sh:todo
}

void Firmap::map_fir_sub(Node &node, LGraph *new_lg) {
;//FIXME->sh:todo
}

void Firmap::clone_lg_ops(Node &old_node, LGraph *new_lg) {
  auto inp_edges = old_node.inp_edges();
  auto new_node = new_lg->create_node(old_node);
  for (auto e : old_node.inp_edges()) {
    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin_raw(e.sink.get_pid()));
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin_raw(old_dpin.get_pid()));
  }
}


void Firmap::firbits_analysis(LGraph *lg) {
  auto lgit = lg->forward(true); // the design pattern for traverse newly created nodes in same iteration
  for (auto fwd_it = lgit.begin(); fwd_it != lgit.end() ; ++fwd_it) {
    auto node = *fwd_it;
    fmt::print("{}\n", node.debug_name());
    auto inp_edges = node.inp_edges();
    auto op        = node.get_type_op();

    if (inp_edges.empty() && (op != Ntype_op::Const && op != Ntype_op::Sub && op != Ntype_op::LUT && op != Ntype_op::TupKey)) {
      fmt::print("Firmap-> dangling node detected:{}\n", node.debug_name());
      continue;
    }


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
      /* analysis_lg_attr_get(node); */
      if (node.is_invalid())
        continue;
    } else if (op == Ntype_op::Sflop || op == Ntype_op::Aflop || op == Ntype_op::Fflop) {
      /* analysis_lg_flop(node); */
    } else if (op == Ntype_op::Mux) {
      /* analysis_lg_mux(node, inp_edges); */
    } else {
      fmt::print("FIXME: node:{} still not handled by bitwidth\n", node.debug_name());
    }

    //debug
    auto it = fbmap.find(node.get_driver_pin("Y").get_compact());
    if (it != fbmap.end()) {
      fmt::print("    ");
      it->second.dump();
    }
  } // end of lg->forward()
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
    I(false, "Todo...");
    /* analysis_lg_attr_set_propagate(node); */
  }
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
  if (node_attr.is_sink_connected("name")) {
    auto through_dpin = node_attr.get_sink_pin("name").get_driver_pin();
    auto it           = fbmap.find(through_dpin.get_compact());
    if (it != fbmap.end()) {
      fb = it->second;
    } else {
      parent_pending = true;
    }
  }

  if (attr == Attr::Set_ubits || attr == Attr::Set_sbits) {
    I(dpin_val.get_node().is_type_const());
    auto val = dpin_val.get_node().get_type_const();
    
    if (attr == Attr::Set_ubits) {
      if (fb.get_signedness() == true)
        I(false, "cannot set ubits to a signed parent node in firrtl!");

      if (fb.get_bits() && (fb.get_bits()) > (val.to_i()))   
        Pass::error("Firrtl bitwidth mismatch. Variable {} needs {}ubits, but constrained to {}ubits\n", dpin_name, fb.get_bits(), val.to_i());
      fb.set_bits_signedness(val.to_i(), false); 

    } else { // Attr::Set_sbits
      if (fb.get_signedness() == false)
        I(false, "cannot set sbits to an unsigned parent node in firrtl!");

      if (fb.get_bits() && fb.get_bits() > (val.to_i())) 
        Pass::error("Firrtl bitwidth mismatch. Variable {} needs {}sbits, but constrained to {}sbits\n", dpin_name, fb.get_bits(), val.to_i());
      
      fb.set_bits_signedness(val.to_i(), true);
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

  // note: up to now, if something has been converted to dp, the lhs and rhs
  // firrtl bits must be the same in firrtl bits analysis
  I(fb_lhs.get_bits() == fb_rhs.get_bits());
  I(fb_lhs.get_signedness() == fb_rhs.get_signedness());

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
  } 
}


void Firmap::analysis_fir_add_sub(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling sum??? (delete)

  Bits_t bits1, bits2;
  Node_pin e1_dpin, e2_dpin;
  bool signedness;

  for (auto e : inp_edges) {
    auto it = fbmap.find(e.driver.get_compact());
    if (it == fbmap.end())
      I(false); //FIXME->sh: really? what about there is a loop from flop?

    if (e.sink.get_pin_name() == "A") {
      bits1 = it->second.get_bits();
      signedness = it->second.get_signedness();
      e1_dpin = e.driver;
    } else {
      I(signedness == it->second.get_signedness()); // inputs of firrtl add must have same signedness
      bits2 = it->second.get_bits();
      e2_dpin = e.driver;
    }
    e.del_edge();
  }
  fbmap.insert_or_assign(node.get_driver_pin("Y").get_compact(), Firrtl_bits(std::max(bits1, bits2) + 1, signedness));
}



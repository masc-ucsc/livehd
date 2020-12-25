//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <bits/stdint-uintn.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "firmap.hpp"
#include "lbench.hpp"
#include "lgraph.hpp"


Firmap::Firmap() {}

LGraph* Firmap::do_firrtl_mapping(LGraph *lg) {

  fmt::print("DEBUG\n");
  for (auto &it : fbmap) {
    Node_pin dpin(lg->get_path(), it.first); 
    fmt::print("it->first dpin:{}\n", dpin.debug_name()); //
    it.second.dump();
  }



  auto lg_name = lg->get_name();
  auto pos = lg_name.find("_firrtl");
  std::string  lg_source{lg->get_library().get_source(lg->get_lgid())}; // string, create can free it
  LGraph *new_lg = LGraph::create(lg->get_path(), lg_name.substr(0, pos), lg_source);
  
  // clone graph input 
  lg->each_graph_input([new_lg, this](Node_pin &dpin) {
      auto new_ginp = new_lg->add_graph_input(dpin.get_name(), dpin.get_pid(), dpin.get_bits());
      o2n_dpin.insert_or_assign(dpin, new_ginp);
  });

  // clone graph output 
  lg->each_graph_output([new_lg, this](Node_pin &dpin) {
      auto new_gout = new_lg->add_graph_output(dpin.get_name(), dpin.get_pid(), dpin.get_bits());
      o2n_dpin.insert_or_assign(dpin, new_gout);
  });

  // clone graph main body
  for (auto node : lg->forward()) {
    auto op = node.get_type_op();
    fmt::print("{}\n", node.debug_name());
    if (op == Ntype_op::Sub) {
      auto subname = node.get_type_sub_node().get_name();
      if ( subname.substr(0,5) == "__fir") 
        map_fir_ops(node, subname, new_lg);
      else 
        clone_subgraph_node(node, new_lg); 

      continue;
    } 
    clone_lg_ops_amap(node, new_lg);  // amap = as much as possible
  }

  // clone edges that cannot resolved at clone_lg_ops_amap() due to flop loop
  // Assumption: the issue only happens at flop input edges as flops are also
  // starts points of lg->forward(), input driver may not been created in
  // new_lg yet!
  for (auto &it : o2n_dpin) {
    auto old_node = it.first.get_node();
    auto new_node = it.second.get_node();
    if (old_node.get_type_op() != Ntype_op::Sflop && old_node.get_type_op() != Ntype_op::Aflop && 
        old_node.get_type_op() != Ntype_op::Latch && old_node.get_type_op() != Ntype_op::Fflop)
      continue;

    if (old_node.get_num_inp_edges() == new_node.get_num_inp_edges()) // all old edges are cloned
      continue; 

    for (auto &e : old_node.inp_edges()) {
      auto pid = e.sink.get_pid();
      if (!new_node.setup_sink_pin_raw(pid).has_inputs()) //FIXME->sh: only true for the cases of single-input sink pin ...
        o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin_raw(pid));
    }
  }


  // connect graph output to its driver
  lg->each_graph_output([this](Node_pin &dpin) {
    auto spin = dpin.change_to_sink_from_graph_out_driver();
    auto out_driver = spin.get_driver_pin();

    if (o2n_dpin.find(out_driver) == o2n_dpin.end())
      Pass::error("graph-out {} cannot find corresponding driver in the new lgraph\n", out_driver.debug_name());

    if (o2n_dpin.find(dpin) == o2n_dpin.end()) 
      Pass::error("graph-out {} cannot find corresponding graph-out in the new lgraph\n", dpin.debug_name());

    o2n_dpin[out_driver].connect_sink(o2n_dpin[dpin]);
  });

  return new_lg;
}

void Firmap::map_fir_ops(Node &node, std::string_view op, LGraph *new_lg) {
  if (op == "__fir_add") {
    map_fir_add(node, new_lg);
  } else if (op == "__fir_sub") {
    map_fir_sub(node, new_lg);
  } else if (op == "__fir_mul") {
    map_fir_mul(node, new_lg);
  } else if (op == "__fir_div") {
    map_fir_div(node, new_lg);
  } else if (op == "__fir_rem") {
    map_fir_rem(node, new_lg); //todo
  } else if (op == "__fir_lt"  || op == "__fir_gt") {
    map_fir_lt_gt(node, new_lg, op);
  } else if (op == "__fir_leq" || op == "__fir_geq") {
    map_fir_leq_geq(node, new_lg, op);
  } else if (op == "__fir_eq") {
    map_fir_eq(node, new_lg);
  } else if (op == "__fir_neq") {
    map_fir_neq(node, new_lg);
  } else if (op == "__fir_pad") {
    map_fir_pad(node, new_lg);
  } else if (op == "__fir_as_uint") {
    map_fir_as_uint(node, new_lg);
  } else if (op == "__fir_as_sint") {
    map_fir_as_sint(node);
  } else if (op == "__fir_shl") {
    map_fir_shl(node, new_lg);
  } else if (op == "__fir_shr") {
    map_fir_shr(node, new_lg);
  } else if (op == "__fir_dshl") {
    map_fir_dshl(node, new_lg);
  } else if (op == "__fir_dshr") {
    map_fir_dshr(node, new_lg);
  } else if (op == "__fir_cvt") {
    map_fir_cvt(node);
  } else if (op == "__fir_neg") {
    map_fir_neg(node, new_lg);
  } else if (op == "__fir_not") {
    map_fir_not(node, new_lg);
  } else if (op == "__fir_and" || op == "__fir_or" || op == "__fir_xor") {
    map_fir_and_or_xor(node, new_lg, op);
  } else if (op == "__fir_andr") {
    map_fir_andr(node, new_lg);
  } else if (op == "__fir_orr") {
    map_fir_orr(node, new_lg);
  } else if (op == "__fir_xorr") {
    map_fir_xorr(node, new_lg);
  } else if (op == "__fir_cat") {
    map_fir_cat(node, new_lg);
  } else if (op == "__fir_bits") {
    map_fir_bits(node, new_lg);
  } else if (op == "__fir_head") {
    map_fir_head(node, new_lg);
  } else if (op == "__fir_tail") {
    map_fir_tail(node, new_lg);
  } else {
    I(false, "typo?");
  }
}

// e1 tail n = e1 & (pow(e1.fbits - n) - 1)
void Firmap::map_fir_tail(Node &old_node, LGraph *new_lg) {
  auto new_node_mask = new_lg->create_node(Ntype_op::And);
  auto new_node_tp   = new_lg->create_node(Ntype_op::Tposs);
  
  Lconst e1_bits;
  Lconst n;
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())         
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());
    if (fbmap.find(e.driver.get_compact_flat()) == fbmap.end()) 
      Pass::error("dpin:{} cannot found in fbmap", e.driver.debug_name());
      
    if (e.sink == old_node.setup_sink_pin("e1")) {
      e1_bits = fbmap[e.driver.get_compact_flat()].get_bits();
      o2n_dpin[e.driver].connect_sink(new_node_mask.setup_sink_pin("A")); // e1 -> mask
    } else { //e2
      n = e.driver.get_node().get_type_const();
    }
  }

  auto mask_bits = e1_bits - n;
  auto mask_const = std::pow(2, mask_bits.to_i()) - 1;

  auto new_node_const = new_lg->create_node_const(mask_const);
  new_node_const.setup_driver_pin().connect_sink(new_node_mask.setup_sink_pin("A")); // mask_val -> mask
  new_node_tp.setup_sink_pin("a").connect_driver(new_node_mask.setup_driver_pin());  // mask -> tp

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
}

// e1 head n = tposs (((e1 >> (e1.fbits - n)) & ((1<<n)-1)))
// FIXME->sh: put a mask to constrain the bits in lgraph? -> yes
void Firmap::map_fir_head(Node &old_node, LGraph *new_lg) {
  auto new_node_sra  = new_lg->create_node(Ntype_op::SRA);
  auto new_node_tp   = new_lg->create_node(Ntype_op::Tposs);
  auto new_node_mask = new_lg->create_node(Ntype_op::And);
  Lconst e1_bits;
  Lconst n; 
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())         
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());
    if (fbmap.find(e.driver.get_compact_flat()) == fbmap.end()) 
      Pass::error("dpin:{} cannot found in fbmap", e.driver.debug_name());
      
    if (e.sink == old_node.setup_sink_pin("e1")) {
      e1_bits = fbmap[e.driver.get_compact_flat()].get_bits();
      o2n_dpin[e.driver].connect_sink(new_node_sra.setup_sink_pin("a")); // e1 -> sra
    } else { //e2
      n = e.driver.get_node().get_type_const();
    }
  }

  auto shamt = e1_bits - n;
  auto new_node_const_sra = new_lg->create_node_const(shamt);

  new_node_const_sra.setup_driver_pin().connect_sink(new_node_sra.setup_sink_pin("b")); // amount -> sra

  auto const_mask = (1 << n.to_i()) - 1; //n bits mask
  auto new_node_const_mask = new_lg->create_node_const(const_mask);
  new_node_sra.setup_driver_pin().connect_sink(new_node_mask.setup_sink_pin("A"));        // sra -> mask
  new_node_const_mask.setup_driver_pin().connect_sink(new_node_mask.setup_sink_pin("A")); // mask_const-> mask

  new_node_tp.setup_sink_pin("a").connect_driver(new_node_mask.setup_driver_pin());  // mask -> tp

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
}




// (e1 >> lo) & ((1<<(hi - lo)) - 1)
void Firmap::map_fir_bits(Node &old_node, LGraph *new_lg) {
  auto new_node_sra  = new_lg->create_node(Ntype_op::SRA);
  auto new_node_mask = new_lg->create_node(Ntype_op::And);
  auto new_node_tp   = new_lg->create_node(Ntype_op::Tposs);
  Node new_node_mask_const;
  Node new_node_lo_const;
  uint32_t hi, lo;
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node_sra.setup_sink_pin("a"));
    } else if (e.sink == old_node.setup_sink_pin("e2")){
      hi = e.driver.get_node().get_type_const().to_i();
    } else {
      lo = e.driver.get_node().get_type_const().to_i();
      new_node_lo_const = new_lg->create_node_const(lo);
    }
  }

  auto mask_const = (1 << (hi - lo + 1)) - 1;
  new_node_mask_const = new_lg->create_node_const(mask_const);

  new_node_lo_const.setup_driver_pin().connect_sink(new_node_sra.setup_sink_pin("b"));
  new_node_sra.setup_driver_pin().connect_sink(new_node_mask.setup_sink_pin("A"));
  new_node_mask_const.setup_driver_pin().connect_sink(new_node_mask.setup_sink_pin("A"));
  new_node_mask.setup_driver_pin().connect_sink(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
}


// e1 cat e2 = tposs (e1 << e2.fbits || e2)
void Firmap::map_fir_cat(Node &old_node, LGraph *new_lg) {
  auto new_node_shl = new_lg->create_node(Ntype_op::SHL);
  auto new_node_tp  = new_lg->create_node(Ntype_op::Tposs);
  auto new_node_or  = new_lg->create_node(Ntype_op::Or);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())         
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());
    if (fbmap.find(e.driver.get_compact_flat()) == fbmap.end()) 
      Pass::error("dpin:{} cannot found in fbmap", e.driver.debug_name());
      
    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node_shl.setup_sink_pin("a")); // e1 -> shl
    } else { //e2
      auto e2_bits = fbmap[e.driver.get_compact_flat()].get_bits();
      auto new_node_const = new_lg->create_node_const(e2_bits);
      new_node_const.setup_driver_pin().connect_sink(new_node_shl.setup_sink_pin("b")); // e2.fbits -> shl
      o2n_dpin[e.driver].connect_sink(new_node_or.setup_sink_pin("A")); // e2 -> or
    }
  }

  new_node_or.setup_sink_pin("A").connect_driver(new_node_shl.setup_driver_pin()); // (e1 << e2.fbits) -> or
  new_node_tp.setup_sink_pin("a").connect_driver(new_node_or.setup_driver_pin());  // or -> tp

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
}



void Firmap::map_fir_orr(Node &old_node, LGraph *new_lg) {
  auto new_node_tp = new_lg->create_node(Ntype_op::Tposs);
  Node new_node_logic = new_lg->create_node(Ntype_op::Ror);

  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end()) 
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());
    o2n_dpin[e.driver].connect_sink(new_node_logic.setup_sink_pin("A"));
  }
  new_node_logic.setup_driver_pin().connect_sink(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
}


void Firmap::map_fir_xorr(Node &old_node, LGraph *new_lg) {
  auto new_node_tp  = new_lg->create_node(Ntype_op::Tposs);
  auto new_node_and = new_lg->create_node(Ntype_op::And);
  auto new_node_xor = new_lg->create_node(Ntype_op::Xor);
  auto new_node_const_1 = new_lg->create_node_const(1);

  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node_xor.setup_sink_pin("A"));
      auto e1_bits = fbmap[e.driver.get_compact_flat()].get_bits();
      for (uint32_t i = 1; i < e1_bits; i++) {
        auto new_node_sra = new_lg->create_node(Ntype_op::SRA);
        new_node_sra.setup_sink_pin("a").connect_driver(o2n_dpin[e.driver]);
        new_node_sra.setup_sink_pin("b").connect_driver(new_lg->create_node_const(i));
        new_node_xor.setup_sink_pin("A").connect_driver(new_node_sra.setup_driver_pin());
      }
    }
  }

  new_node_const_1.setup_driver_pin().connect_sink(new_node_and.setup_sink_pin("A"));
  new_node_xor.setup_driver_pin().connect_sink(new_node_and.setup_sink_pin("A"));
  new_node_and.setup_driver_pin().connect_sink(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
}

void Firmap::map_fir_andr(Node &old_node, LGraph *new_lg) {
  // Andr(e1) = Tposs(And(Not(Ror(Not(And(e1, mask(e.fbits))))), e1.MSB))
  // e1.MSB   = e1 >> (e1.fbits-1)
  auto new_node_not1 = new_lg->create_node(Ntype_op::Not);
  auto new_node_not2 = new_lg->create_node(Ntype_op::Not);
  auto new_node_ror  = new_lg->create_node(Ntype_op::Ror);
  auto new_node_tp   = new_lg->create_node(Ntype_op::Tposs);

  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      auto e1_bits = fbmap[e.driver.get_compact_flat()].get_bits();
      auto e1_sign = fbmap[e.driver.get_compact_flat()].get_sign();

      // unsigned graph input will have a tposs in the future(BW will insert
      // it), the extra MSB-0 of this tposs will cause the following Not_op
      // semantic wrong, hence, we add an Or_op with MSB-1 to avoid problem
      auto parent_node = e.driver.get_node();
      bool cond1 = parent_node.get_type_op() == Ntype_op::AttrSet;
      bool cond2 = cond1 && parent_node.setup_sink_pin("name").get_driver_pin().is_graph_input();
      if (cond2 && !e1_sign) {        
        auto new_node_or    = new_lg->create_node(Ntype_op::Or); 
        auto new_node_const = new_lg->create_node_const(Lconst(1UL) << Lconst(e1_bits));
        o2n_dpin[e.driver].connect_sink(new_node_or.setup_sink_pin("A"));
        new_node_const.setup_driver_pin().connect_sink(new_node_or.setup_sink_pin("A"));
        new_node_or.setup_driver_pin().connect_sink(new_node_not1.setup_sink_pin("a"));
      } else {
        o2n_dpin[e.driver].connect_sink(new_node_not1.setup_sink_pin("a"));
      }
    }
  }

  new_node_not1.setup_driver_pin().connect_sink(new_node_ror.setup_sink_pin("A"));
  new_node_ror.setup_driver_pin().connect_sink(new_node_not2.setup_sink_pin("a"));
  new_node_not2.setup_driver_pin().connect_sink(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
}


void Firmap::map_fir_and_or_xor(Node &old_node, LGraph *new_lg, std::string_view op) {
  auto new_node_tp = new_lg->create_node(Ntype_op::Tposs);
  Node new_node_logic;
  if (op == "__fir_and") {
    new_node_logic = new_lg->create_node(Ntype_op::And);
  } else if (op == "__fir_or") {
    new_node_logic = new_lg->create_node(Ntype_op::Or);
  } else if (op == "__fir_xor") {
    new_node_logic = new_lg->create_node(Ntype_op::Xor);
  }

  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end()) 
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());
    o2n_dpin[e.driver].connect_sink(new_node_logic.setup_sink_pin("A"));
  }
  new_node_logic.setup_driver_pin().connect_sink(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
}

void Firmap::map_fir_not(Node &old_node, LGraph *new_lg) {
  auto new_node_not = new_lg->create_node(Ntype_op::Not);
  auto new_node_tp = new_lg->create_node(Ntype_op::Tposs);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end()) 
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      auto e1_bits = fbmap[e.driver.get_compact_flat()].get_bits();
      auto e1_sign = fbmap[e.driver.get_compact_flat()].get_sign();

      // unsigned graph input will have a tposs in the future(BW will insert
      // it), the extra MSB-0 of this tposs will cause the following Not_op
      // semantic wrong, hence, we add an Or_op with MSB-1 to avoid problem
      auto parent_node = e.driver.get_node();
      bool cond1 = parent_node.get_type_op() == Ntype_op::AttrSet;
      bool cond2 = cond1 && parent_node.setup_sink_pin("name").get_driver_pin().is_graph_input();
      if (cond2 && !e1_sign) {        
        auto new_node_or    = new_lg->create_node(Ntype_op::Or); 
        auto new_node_const = new_lg->create_node_const(Lconst(1UL) << Lconst(e1_bits));
        o2n_dpin[e.driver].connect_sink(new_node_or.setup_sink_pin("A"));
        new_node_const.setup_driver_pin().connect_sink(new_node_or.setup_sink_pin("A"));
        new_node_or.setup_driver_pin().connect_sink(new_node_not.setup_sink_pin("a"));
      } else {
        o2n_dpin[e.driver].connect_sink(new_node_not.setup_sink_pin("a"));
      }
    }
  }
  new_node_not.setup_driver_pin().connect_sink(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
}


void Firmap::map_fir_neg(Node &old_node, LGraph *new_lg) {
  auto new_node_sum   = new_lg->create_node(Ntype_op::Sum);
  auto new_node_const = new_lg->create_node_const(0);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) 
      o2n_dpin[e.driver].connect_sink(new_node_sum.setup_sink_pin("B"));
  }
  
  new_node_const.setup_driver_pin().connect_sink(new_node_sum.setup_sink_pin("B"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_sum.setup_driver_pin());
}


void Firmap::map_fir_cvt(Node &old_node) {
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    auto sink_pid_old = e.sink.get_pid();
    auto sink_node_dpin_old = e.sink.get_node().get_driver_pin();
    auto spin_new = o2n_dpin[sink_node_dpin_old].get_node().setup_sink_pin_raw(sink_pid_old);
    o2n_dpin[e.driver].connect_sink(spin_new);
  }
}

void Firmap::map_fir_dshr(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::SRA);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end()) 
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("a"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("b"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
}


void Firmap::map_fir_dshl(Node &old_node, LGraph *new_lg) {
  auto new_node_shl = new_lg->create_node(Ntype_op::SHL);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())         
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());
    if (fbmap.find(e.driver.get_compact_flat()) == fbmap.end()) 
      Pass::error("dpin:{} cannot found in fbmap", e.driver.debug_name());
      
    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node_shl.setup_sink_pin("a"));
    } else { //e2
      o2n_dpin[e.driver].connect_sink(new_node_shl.setup_sink_pin("b"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_shl.setup_driver_pin());
}


void Firmap::map_fir_shl(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::SHL);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end()) 
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("a"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("b"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
}


void Firmap::map_fir_shr(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::SRA);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end()) 
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("a"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("b"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
}


void Firmap::map_fir_as_uint(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::Tposs);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end()) 
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("a"));
  }
  
  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
} 


void Firmap::map_fir_as_sint(Node &old_node) {
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    auto sink_pid_old = e.sink.get_pid();
    auto sink_node_dpin_old = e.sink.get_node().get_driver_pin();
    auto spin_new = o2n_dpin[sink_node_dpin_old].get_node().setup_sink_pin_raw(sink_pid_old);
    o2n_dpin[e.driver].connect_sink(spin_new);
  }
} 


void Firmap::map_fir_rem(Node &old_node, LGraph *new_lg) {
  ; // todo
} 


void Firmap::map_fir_pad(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::Or); // note: this is a wiring OR, we need this since the sink_node_new_lg is not created yet in the new_lg as we traverse
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
    }   
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
} 

// A neq B == ~(A eq B) 
void Firmap::map_fir_neq(Node &old_node, LGraph *new_lg) {
  auto new_node_eq = new_lg->create_node(Ntype_op::EQ);
  auto new_node_not = new_lg->create_node(Ntype_op::Not);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    o2n_dpin[e.driver].connect_sink(new_node_eq.setup_sink_pin("A"));
  }
  
  new_node_eq.setup_driver_pin().connect_sink(new_node_not.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_not.setup_driver_pin());
}

void Firmap::map_fir_eq(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::EQ);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
}


// A geq B == ~(A lt B) ; A leq B == ~(A gt B)
void Firmap::map_fir_leq_geq(Node &old_node, LGraph *new_lg, std::string_view op) {

  Node new_node_not = new_lg->create_node(Ntype_op::Not);
  Node new_node_cmp;
  if (op == "__fir_leq")
    new_node_cmp = new_lg->create_node(Ntype_op::GT);
  else 
    new_node_cmp = new_lg->create_node(Ntype_op::LT);

  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node_cmp.setup_sink_pin("A"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node_cmp.setup_sink_pin("B"));
    }
  }

  new_node_cmp.setup_driver_pin().connect_sink(new_node_not.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_not.setup_driver_pin());
}



void Firmap::map_fir_lt_gt(Node &old_node, LGraph *new_lg, std::string_view op) {
  Node new_node;
  if (op == "__fir_lt")
    new_node = new_lg->create_node(Ntype_op::LT);
  else 
    new_node = new_lg->create_node(Ntype_op::GT);

  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("B"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
}


void Firmap::map_fir_div(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::Div);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("{} cannot find corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("a"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("b"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
} 


void Firmap::map_fir_mul(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::Mult);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("{} cannot find corresponding dpin in the new lgraph", e.driver.debug_name());

    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
} 


void Firmap::map_fir_add(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::Sum);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("{} cannot find corresponding dpin in the new lgraph", e.driver.debug_name());

    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
} 


void Firmap::map_fir_sub(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::Sum);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("B"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
}


void Firmap::clone_lg_ops_amap(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(old_node);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end()) {
      fmt::print("dpin:{} cannot found corresponding dpin in the new lgraph", e.driver.debug_name());
      continue;
    }
    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin_raw(e.sink.get_pid()));
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin_raw(old_dpin.get_pid()));
    if (old_dpin.has_name())
      new_node.setup_driver_pin_raw(old_dpin.get_pid()).set_name(old_dpin.get_name());
  }
}

void Firmap::clone_subgraph_node(Node &old_node, LGraph *new_lg) {
  auto *library = Graph_library::instance(new_lg->get_path());
  Node new_node_subg;
  Sub_node* new_sub;
  Sub_node* old_sub = old_node.get_class_lgraph()->ref_self_sub_node();

  // get rid of _firrtl postfix to get correct new_subg_name
  auto tmp_new_subg_name = old_node.get_type_sub_node().get_name();  
  I(tmp_new_subg_name.find("_firrtl") != std::string::npos);
  auto pos = tmp_new_subg_name.find("_firrtl");
  auto new_subg_name = tmp_new_subg_name.substr(0, pos);


  // create new_lg subgraph node and its affiliate Sub_node
  if (library->has_name(new_subg_name)) {
    auto lgid = library->get_lgid(new_subg_name);
    new_node_subg = new_lg->create_node_sub(lgid);
    new_sub = library->ref_sub(lgid);
  } else {
    new_node_subg = new_lg->create_node_sub(new_subg_name);
    new_sub = library->ref_sub(new_subg_name);
  }

  // clone all old_sub io to new_sub_io and setup all sink_pins and driver_pins for the new_sub node
  for (const auto *old_io_pin : old_sub->get_io_pins()) {
    I(!old_io_pin->is_invalid());
    auto old_io_name = old_io_pin->name; 
    if (old_io_pin->is_input()) {
      new_node_subg.setup_sink_pin(old_io_name);
      if (!new_sub->has_pin(old_io_name)) 
        new_sub->add_input_pin(old_io_name, Port_invalid);

      // clone the old_sub input edges 
      for (auto e : old_node.inp_edges()) {
        if (o2n_dpin.find(e.driver) == o2n_dpin.end())
          Pass::error("{} cannot find corresponding dpin in the new lgraph", e.driver.debug_name());

        o2n_dpin[e.driver].connect_sink(new_node_subg.setup_sink_pin(old_io_name));
      }
      continue;
    } 

    // handle old_io_pin->is_output()
    auto new_dpin = new_node_subg.setup_driver_pin(old_io_name);
    if (!new_sub->has_pin(old_io_name)) 
      new_sub->add_output_pin(old_io_name, Port_invalid);

    o2n_dpin.insert_or_assign(old_node.setup_driver_pin(old_io_name), new_dpin);
  }

  // get the firbits of the driver of old_graph_output, and set it to the corresponding subg_node in new_lg 
  old_node.get_class_lgraph()->each_graph_output([&new_node_subg, this](Node_pin &old_dpin_gout) {
    auto old_spin_gout = old_dpin_gout.change_to_sink_from_graph_out_driver();
    auto old_gout_driver = old_spin_gout.get_driver_pin();
    I(fbmap.find(old_gout_driver.get_compact_flat()) != fbmap.end());
    I(o2n_dpin.find(old_dpin_gout) != o2n_dpin.end());

    auto new_subg_dpin = new_node_subg.setup_driver_pin(old_dpin_gout.get_name()); 
    fbmap.insert_or_assign(new_subg_dpin.get_compact_flat(), fbmap[old_dpin_gout.get_compact_flat()]);

  }, false);
}

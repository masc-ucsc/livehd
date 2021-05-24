//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "firmap.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "lbench.hpp"
#include "lgraph.hpp"

Firmap::Firmap(absl::node_hash_map<Lgraph *, FBMap> &_fbmaps, absl::node_hash_map<Lgraph *, PinMap> &_pinmaps,
               absl::node_hash_map<Lgraph *, XorrMap> &_spinmaps_xorr)
    : fbmaps(_fbmaps), pinmaps(_pinmaps), spinmaps_xorr(_spinmaps_xorr) {}

Lgraph *Firmap::do_firrtl_mapping(Lgraph *lg) {
  Lbench b("pass.firmap");

  auto        lg_name = lg->get_name();
  std::string lg_source{lg->get_library().get_source(lg->get_lgid())};
  Lgraph *    new_lg = Lgraph::create(lg->get_path(), lg_name.substr(9), lg_source);

  I(pinmaps.find(lg) != pinmaps.end());  // call add_map_entry first (needed for multithreaded)

  auto &fbmap        = fbmaps.find(lg)->second;
  auto &pinmap       = pinmaps.find(lg)->second;
  auto &spinmap_xorr = spinmaps_xorr.find(lg)->second;

  // clone graph input pins
  lg->each_graph_input([new_lg, &pinmap](Node_pin &old_dpin) {
    auto new_ginp = new_lg->add_graph_input(old_dpin.get_name(), Port_invalid, old_dpin.get_bits());
    pinmap.insert_or_assign(old_dpin, new_ginp);
  });

  // clone graph output pins
  lg->each_graph_output([new_lg, &pinmap](Node_pin &old_dpin) {
    auto old_spin = old_dpin.change_to_sink_from_graph_out_driver();
    auto new_gout = new_lg->add_graph_output(old_dpin.get_name(), Port_invalid, old_dpin.get_bits());
    pinmap.insert_or_assign(old_spin, new_gout.change_to_sink_from_graph_out_driver());
    pinmap.insert_or_assign(old_dpin, new_gout);
  });

  // I. clone graph main body nodes

  for (auto node : lg->fast()) {
    auto op = node.get_type_op();
    /* fmt::print("Node Map {}\n", node.debug_name()); */
    if (op == Ntype_op::Sub) {
      auto subname = node.get_type_sub_node().get_name();
      if (subname.substr(0, 6) == "__fir_")
        map_node_fir_ops(node, subname, new_lg, fbmap, pinmap, spinmap_xorr);
      else
        clone_subgraph_node(node, new_lg, pinmap);
      continue;
    }
    clone_lg_ops_node(node, new_lg, pinmap);
  }

  // II. clone graph main body edges
  for (auto node : lg->fast()) {
    auto op = node.get_type_op();
    // fmt::print("Edge Map {}\n", node.debug_name());
    if (op == Ntype_op::Sub && node.get_type_sub_node().get_name().substr(0, 10) == "__fir_xorr") {
      clone_edges_fir_xorr(node, pinmap, spinmap_xorr);
      continue;
    }
    clone_edges(node, pinmap);
  }

  // connect graph output to its driver
  lg->each_graph_output([&pinmap](Node_pin &dpin) {
    auto spin       = dpin.change_to_sink_from_graph_out_driver();
    auto out_driver = spin.get_driver_pin();

    if (pinmap.find(out_driver) == pinmap.end())
      Pass::error("graph-out {} cannot find corresponding driver in the new lgraph\n", out_driver.debug_name());

    if (pinmap.find(dpin) == pinmap.end())
      Pass::error("graph-out {} cannot find corresponding graph-out in the new lgraph\n", dpin.debug_name());

    pinmap[out_driver].connect_sink(pinmap[dpin]);
  });

  pinmap.clear();
  return new_lg;
}

void Firmap::clone_edges(Node &node, PinMap &pinmap) {
  for (auto &old_spin : node.inp_connected_pins()) {
    auto old_dpin = old_spin.get_driver_pin();
    auto it       = pinmap.find(old_spin);
    if (it == pinmap.end()) {  // e.g. fir_tail has two inputs, but only e1 need to be mapped
      continue;
    }

    pinmap[old_dpin].connect_sink(pinmap[old_spin]);
  }
}

void Firmap::clone_edges_fir_xorr(Node &node, PinMap &pinmap, XorrMap &spinmap_xorr) {
  for (auto &old_spin : node.inp_connected_pins()) {
    auto old_dpin = old_spin.get_driver_pin();
    for (auto &new_spin : spinmap_xorr[old_spin]) pinmap[old_dpin].connect_sink(new_spin);
  }
}

void Firmap::map_node_fir_ops(Node &node, std::string_view op, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap,
                              XorrMap &spinmap_xorr) {
  // TODO: Create a map that indexed by op and returns a std::function (faster)

  if (op == "__fir_const") {
    map_node_fir_const(node, new_lg, pinmap);
  } else if (op == "__fir_add") {
    map_node_fir_add(node, new_lg, pinmap);
  } else if (op == "__fir_sub") {
    map_node_fir_sub(node, new_lg, pinmap);
  } else if (op == "__fir_mul") {
    map_node_fir_mul(node, new_lg, pinmap);
  } else if (op == "__fir_div") {
    map_node_fir_div(node, new_lg, pinmap);
  } else if (op == "__fir_rem") {
    map_node_fir_rem(node, new_lg, pinmap);  // todo
  } else if (op == "__fir_lt" || op == "__fir_gt") {
    map_node_fir_lt_gt(node, new_lg, op, pinmap);
  } else if (op == "__fir_leq" || op == "__fir_geq") {
    map_node_fir_leq_geq(node, new_lg, op, pinmap);
  } else if (op == "__fir_eq") {
    map_node_fir_eq(node, new_lg, pinmap);
  } else if (op == "__fir_neq") {
    map_node_fir_neq(node, new_lg, pinmap);
  } else if (op == "__fir_pad") {
    map_node_fir_pad(node, new_lg, pinmap);
  } else if (op == "__fir_as_uint") {
    map_node_fir_as_uint(node, new_lg, pinmap);
  } else if (op == "__fir_as_sint") {
    map_node_fir_as_sint(node, new_lg, pinmap);
  } else if (op == "__fir_as_clock") {
    map_node_fir_as_clock(node, new_lg, pinmap);
  } else if (op == "__fir_as_async") {
    I(false);  // TODO
  } else if (op == "__fir_shl") {
    map_node_fir_shl(node, new_lg, pinmap);
  } else if (op == "__fir_shr") {
    map_node_fir_shr(node, new_lg, pinmap);
  } else if (op == "__fir_dshl") {
    map_node_fir_dshl(node, new_lg, pinmap);
  } else if (op == "__fir_dshr") {
    map_node_fir_dshr(node, new_lg, pinmap);
  } else if (op == "__fir_cvt") {
    map_node_fir_cvt(node, new_lg, pinmap);
  } else if (op == "__fir_neg") {
    map_node_fir_neg(node, new_lg, pinmap);
  } else if (op == "__fir_and" || op == "__fir_or" || op == "__fir_xor") {
    map_node_fir_and_or_xor(node, new_lg, op, pinmap);
  } else if (op == "__fir_orr") {
    map_node_fir_orr(node, new_lg, pinmap);
  } else if (op == "__fir_bits") {
    map_node_fir_bits(node, new_lg, pinmap);
  } else if (op == "__fir_not") {
    map_node_fir_not(node, new_lg, fbmap, pinmap);
  } else if (op == "__fir_andr") {
    map_node_fir_andr(node, new_lg, fbmap, pinmap);
  } else if (op == "__fir_xorr") {
    map_node_fir_xorr(node, new_lg, fbmap, pinmap, spinmap_xorr);
  } else if (op == "__fir_cat") {
    map_node_fir_cat(node, new_lg, fbmap, pinmap);
  } else if (op == "__fir_head") {
    map_node_fir_head(node, new_lg, fbmap, pinmap);
  } else if (op == "__fir_tail") {
    map_node_fir_tail(node, new_lg, fbmap, pinmap);
  } else {
    I(false, "typo?");
  }
}

// e1 tail n = e1 & (pow(2, (e1.fbits - n) - 1))
void Firmap::map_node_fir_tail(Node &old_node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap) {
  auto new_node_mask = new_lg->create_node(Ntype_op::And);

  Lconst e1_bits;
  Lconst n;
  I(old_node.inp_edges_ordered().size() == 2);  // e1 and e2
  I(old_node.is_type_sub());

  for (auto &e : old_node.inp_edges()) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end())
      it = get_fbits_from_hierarchy(e);

    if (e.sink.get_type_sub_pin_name() == "e1") {
      e1_bits = it->second.get_bits();
      pinmap.insert_or_assign(e.sink, new_node_mask.setup_sink_pin("A"));  // e1 -> mask
    } else {                                                               // e2
      n = e.driver.get_type_const();
    }
  }

  auto mask_bits  = e1_bits - n;
  auto mask_const = std::pow(2, mask_bits.to_i()) - 1;

  auto new_node_const = new_lg->create_node_const(mask_const);
  new_node_const.connect_driver(new_node_mask.setup_sink_pin("A"));  // mask_val -> mask

  auto new_node_tp = new_lg->create_node(Ntype_op::Get_mask);
  new_node_tp.setup_sink_pin("a").connect_driver(new_node_mask.setup_driver_pin());  // mask -> tp
  new_node_tp.setup_sink_pin("mask").connect_driver(new_lg->create_node_const(-1));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
  }
}

// e1 head n = tposs (((e1 >> (e1.fbits - n)) & ((1<<n)-1)))
// FIXME->sh: put a mask to constrain the bits in lgraph? -> yes
void Firmap::map_node_fir_head(Node &old_node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap) {
  auto   new_node_sra  = new_lg->create_node(Ntype_op::SRA);
  auto   new_node_mask = new_lg->create_node(Ntype_op::And);
  Lconst e1_bits;
  Lconst n;

  for (auto &e : old_node.inp_edges()) {
    auto it = fbmap.find(e.driver.get_compact_class_driver());
    if (it == fbmap.end())
      it = get_fbits_from_hierarchy(e);

    if (e.sink.get_type_sub_pin_name() == "e1") {
      e1_bits = it->second.get_bits();
      pinmap.insert_or_assign(e.sink, new_node_sra.setup_sink_pin("a"));  // e1 -> sra
    } else {                                                              // e2
      n = e.driver.get_type_const();
    }
  }

  auto shamt              = e1_bits - n;
  auto new_node_const_sra = new_lg->create_node_const(shamt);

  new_node_const_sra.connect_driver(new_node_sra.setup_sink_pin("b"));  // amount -> sra

  auto const_mask          = (1 << n.to_i()) - 1;  // n bits mask
  auto new_node_const_mask = new_lg->create_node_const(const_mask);
  new_node_sra.connect_driver(new_node_mask.setup_sink_pin("A"));         // sra -> mask
  new_node_const_mask.connect_driver(new_node_mask.setup_sink_pin("A"));  // mask_const-> mask

  auto new_node_tp = new_lg->create_node(Ntype_op::Get_mask);
  new_node_tp.setup_sink_pin("a").connect_driver(new_node_mask.setup_driver_pin());  // mask -> tp
  new_node_tp.setup_sink_pin("mask").connect_driver(new_lg->create_node_const(-1));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
  }
}

// (e1 >> lo) & ((1<<(hi - lo)) - 1)
void Firmap::map_node_fir_bits(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto     new_node_sra  = new_lg->create_node(Ntype_op::SRA);
  auto     new_node_mask = new_lg->create_node(Ntype_op::And);
  Node     new_node_mask_const;
  Node     new_node_lo_const;
  uint32_t hi = 0, lo = 0;
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node_sra.setup_sink_pin("a"));
    } else if (old_spin == old_node.setup_sink_pin("e2")) {
      hi = old_spin.get_driver_node().get_type_const().to_i();
    } else {
      lo                = old_spin.get_driver_node().get_type_const().to_i();
      new_node_lo_const = new_lg->create_node_const(lo);
    }
  }

  auto mask_const     = (1 << (hi - lo + 1)) - 1;
  new_node_mask_const = new_lg->create_node_const(mask_const);

  new_node_lo_const.connect_driver(new_node_sra.setup_sink_pin("b"));
  new_node_sra.connect_driver(new_node_mask.setup_sink_pin("A"));
  new_node_mask_const.connect_driver(new_node_mask.setup_sink_pin("A"));

  auto new_node_tp = new_lg->create_node(Ntype_op::Get_mask);
  new_node_tp.setup_sink_pin("mask").connect_driver(new_lg->create_node_const(-1));
  new_node_mask.connect_driver(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
  }
}

// e1 cat e2 = tposs (e1 << e2.fbits || e2)
void Firmap::map_node_fir_cat(Node &old_node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap) {
  auto new_node_shl = new_lg->create_node(Ntype_op::SHL);
  auto new_node_or  = new_lg->create_node(Ntype_op::Or);

  for (auto &e : old_node.inp_edges()) {
    if (e.sink == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(e.sink, new_node_shl.setup_sink_pin("a"));  // e1 -> shl
    } else {                                                              // e2
      auto it = fbmap.find(e.driver.get_compact_class_driver());
      if (it == fbmap.end())
        it = get_fbits_from_hierarchy(e);

      auto e2_bits        = it->second.get_bits();
      auto new_node_const = new_lg->create_node_const(e2_bits);
      new_node_const.connect_driver(new_node_shl.setup_sink_pin("B"));   // e2.fbits -> shl
      pinmap.insert_or_assign(e.sink, new_node_or.setup_sink_pin("A"));  // e2 -> or
    }
  }

  new_node_or.setup_sink_pin("A").connect_driver(new_node_shl.setup_driver_pin());  // (e1 << e2.fbits) -> or

  auto new_node_tp = new_lg->create_node(Ntype_op::Get_mask);
  new_node_tp.setup_sink_pin("a").connect_driver(new_node_or.setup_driver_pin());  // or -> tp
  new_node_tp.setup_sink_pin("mask").connect_driver(new_lg->create_node_const(-1));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
  }
}

void Firmap::map_node_fir_orr(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  Node new_node_logic = new_lg->create_node(Ntype_op::Ror);

  for (auto old_spin : old_node.inp_connected_pins()) pinmap.insert_or_assign(old_spin, new_node_logic.setup_sink_pin("A"));

  auto new_node_tp = new_lg->create_node(Ntype_op::Get_mask);
  new_node_logic.connect_driver(new_node_tp.setup_sink_pin("a"));
  new_node_tp.setup_sink_pin("mask").connect_driver(new_lg->create_node_const(-1));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
  }
}

void Firmap::map_node_fir_xorr(Node &old_node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap, XorrMap &spinmap_xorr) {
  auto new_node_and   = new_lg->create_node(Ntype_op::And);
  auto new_node_xor   = new_lg->create_node(Ntype_op::Xor);
  auto new_node_const = new_lg->create_node_const(1);

  for (auto &e : old_node.inp_edges()) {
    if (e.sink.get_type_sub_pin_name() == "e1") {
      auto it = fbmap.find(e.driver.get_compact_class_driver());
      if (it == fbmap.end())
        it = get_fbits_from_hierarchy(e);

      std::vector<Node_pin> new_spins;
      new_spins.emplace_back(new_node_xor.setup_sink_pin("A"));
      auto e1_bits = it->second.get_bits();
      for (uint32_t i = 1; i < e1_bits; i++) {
        auto new_node_sra = new_lg->create_node(Ntype_op::SRA);
        new_spins.emplace_back(new_node_sra.setup_sink_pin("a"));
        new_node_sra.setup_sink_pin("b").connect_driver(new_lg->create_node_const(i));
        new_node_xor.setup_sink_pin("A").connect_driver(new_node_sra.setup_driver_pin());
      }
      spinmap_xorr.insert_or_assign(e.sink, new_spins);
    }
  }

  new_node_const.connect_driver(new_node_and.setup_sink_pin("A"));
  new_node_xor.connect_driver(new_node_and.setup_sink_pin("A"));

  auto new_node_tp = new_lg->create_node(Ntype_op::Get_mask);
  new_node_tp.setup_sink_pin("mask").connect_driver(new_lg->create_node_const(-1));
  new_node_and.connect_driver(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
  }
}

void Firmap::map_node_fir_andr(Node &old_node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap) {
  // Andr(e1) = Tposs(Not(Ror(Not(And(e1, mask(e.fbits)))))
  auto new_node_not1 = new_lg->create_node(Ntype_op::Not);
  auto new_node_not2 = new_lg->create_node(Ntype_op::Not);
  auto new_node_ror  = new_lg->create_node(Ntype_op::Ror);
  (void)fbmap;

  for (auto &e : old_node.inp_edges()) {
    if (e.sink.get_type_sub_pin_name() == "e1") {
      pinmap.insert_or_assign(e.sink, new_node_not1.setup_sink_pin("a"));
    }
  }

  new_node_not1.connect_driver(new_node_ror.setup_sink_pin("A"));
  new_node_ror.connect_driver(new_node_not2.setup_sink_pin("a"));

  auto new_node_tp = new_lg->create_node(Ntype_op::Get_mask);
  new_node_tp.setup_sink_pin("mask").connect_driver(new_lg->create_node_const(-1));
  new_node_not2.connect_driver(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
  }
}

void Firmap::map_node_fir_and_or_xor(Node &old_node, Lgraph *new_lg, std::string_view op, PinMap &pinmap) {
  Node new_node_logic;
  if (op == "__fir_and") {
    new_node_logic = new_lg->create_node(Ntype_op::And);
  } else if (op == "__fir_or") {
    new_node_logic = new_lg->create_node(Ntype_op::Or);
  } else if (op == "__fir_xor") {
    new_node_logic = new_lg->create_node(Ntype_op::Xor);
  }

  for (auto &old_spin : old_node.inp_connected_pins()) pinmap.insert_or_assign(old_spin, new_node_logic.setup_sink_pin("A"));

  auto new_node_tp = new_lg->create_node(Ntype_op::Get_mask);
  new_node_tp.setup_sink_pin("mask").connect_driver(new_lg->create_node_const(-1));
  new_node_logic.connect_driver(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
  }
}

void Firmap::map_node_fir_not(Node &old_node, Lgraph *new_lg, FBMap &fbmap, PinMap &pinmap) {
  auto new_node_not = new_lg->create_node(Ntype_op::Not);
  (void)fbmap;

  for (auto &e : old_node.inp_edges()) {
    if (e.sink.get_type_sub_pin_name() == "e1") {
      // pinmap.insert_or_assign(e.sink, new_node_not.setup_sink_pin("a"));
      auto it = fbmap.find(e.driver.get_compact_class_driver());
      if (it == fbmap.end())
        it = get_fbits_from_hierarchy(e);

      auto e1_bits = it->second.get_bits();
      auto e1_sign = it->second.get_sign();

      // unsigned graph input will have a tposs in the future(BW will insert
      // it), the extra MSB(0) of this tposs will cause the following Not_op
      // semantic wrong, hence, we add an Or_op with MSB(1) to avoid problem
      auto parent_node = e.driver.get_node();
      bool cond1       = parent_node.get_type_op() == Ntype_op::AttrSet;
      bool cond2       = cond1 && parent_node.setup_sink_pin("parent").get_driver_pin().is_graph_input();
      if (cond2 && !e1_sign) {
        auto new_node_or    = new_lg->create_node(Ntype_op::Or);
        auto new_node_const = new_lg->create_node_const(Lconst(1UL) << Lconst(e1_bits));
        pinmap.insert_or_assign(e.sink, new_node_or.setup_sink_pin("A"));
        new_node_const.connect_driver(new_node_or.setup_sink_pin("A"));
        new_node_or.connect_driver(new_node_not.setup_sink_pin("a"));
      } else {
        pinmap.insert_or_assign(e.sink, new_node_not.setup_sink_pin("a"));
      }
    }
  }

  auto new_node_tp = new_lg->create_node(Ntype_op::Get_mask);
  new_node_tp.setup_sink_pin("mask").connect_driver(new_lg->create_node_const(-1));
  new_node_not.connect_driver(new_node_tp.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_tp.setup_driver_pin());
  }
}

void Firmap::map_node_fir_neg(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node_sum   = new_lg->create_node(Ntype_op::Sum);
  auto new_node_const = new_lg->create_node_const(0);
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1"))
      pinmap.insert_or_assign(old_spin, new_node_sum.setup_sink_pin("B"));
  }

  new_node_const.connect_driver(new_node_sum.setup_sink_pin("A"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_sum.setup_driver_pin());
  }
}

void Firmap::map_node_fir_cvt(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::Or);  // note: this is a wiring OR
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("A"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_dshr(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::SRA);
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("a"));
    } else {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("b"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_dshl(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node_shl = new_lg->create_node(Ntype_op::SHL);
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node_shl.setup_sink_pin("a"));
    } else {  // e2
      pinmap.insert_or_assign(old_spin, new_node_shl.setup_sink_pin("B"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_shl.setup_driver_pin());
  }
}

void Firmap::map_node_fir_shl(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::SHL);
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("a"));
    } else {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("B"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_shr(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::SRA);
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("a"));
    } else {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("b"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_as_uint(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::Get_mask);
  new_node.setup_sink_pin("mask").connect_driver(new_lg->create_node_const(-1));

  for (auto old_spin : old_node.inp_connected_pins()) pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_as_sint(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::Or);  // note: this is a wiring OR
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("A"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_as_clock(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::Or);  // note: this is a wiring OR
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("A"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_rem(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  // TODO
  (void)old_node;
  (void)new_lg;
  (void)pinmap;
}

void Firmap::map_node_fir_pad(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::Or);  // note: this is a wiring OR
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("A"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

// A neq B == ~(A eq B)
void Firmap::map_node_fir_neq(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node_eq  = new_lg->create_node(Ntype_op::EQ);
  auto new_node_not = new_lg->create_node(Ntype_op::Not);
  for (auto old_spin : old_node.inp_connected_pins()) pinmap.insert_or_assign(old_spin, new_node_eq.setup_sink_pin("A"));

  new_node_eq.connect_driver(new_node_not.setup_sink_pin("a"));
  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_not.setup_driver_pin());
  }
}

void Firmap::map_node_fir_eq(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::EQ);
  for (auto old_spin : old_node.inp_connected_pins()) pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("A"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

// A geq B == ~(A lt B) ; A leq B == ~(A gt B)
void Firmap::map_node_fir_leq_geq(Node &old_node, Lgraph *new_lg, std::string_view op, PinMap &pinmap) {
  Node new_node_not = new_lg->create_node(Ntype_op::Not);
  Node new_node_cmp;
  if (op == "__fir_leq")
    new_node_cmp = new_lg->create_node(Ntype_op::GT);
  else
    new_node_cmp = new_lg->create_node(Ntype_op::LT);

  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node_cmp.setup_sink_pin("A"));
    } else {
      pinmap.insert_or_assign(old_spin, new_node_cmp.setup_sink_pin("B"));
    }
  }

  new_node_cmp.connect_driver(new_node_not.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node_not.setup_driver_pin());
  }
}

void Firmap::map_node_fir_lt_gt(Node &old_node, Lgraph *new_lg, std::string_view op, PinMap &pinmap) {
  Node new_node;
  if (op == "__fir_lt")
    new_node = new_lg->create_node(Ntype_op::LT);
  else
    new_node = new_lg->create_node(Ntype_op::GT);

  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("A"));
    } else {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("B"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_div(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::Div);
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("a"));
    } else {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("b"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_mul(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::Mult);
  for (auto old_spin : old_node.inp_connected_pins()) pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("A"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_const(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto const_str = old_node.get_driver_pin("Y").get_name();
  auto pos       = const_str.find("bits");
  auto new_node  = new_lg->create_node_const(
      const_str.substr(0, pos - 1));  // -1 becasue I ignore the difference between ubits and sbits strings
  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_add(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::Sum);
  for (auto old_spin : old_node.inp_connected_pins()) pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("A"));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::map_node_fir_sub(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(Ntype_op::Sum);
  for (auto old_spin : old_node.inp_connected_pins()) {
    if (old_spin == old_node.setup_sink_pin("e1")) {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("A"));
    } else {
      pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin("B"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin());
    /* fmt::print("    {} maps to {}\n", old_dpin.debug_name(), new_node.setup_driver_pin().debug_name()); */
  }
}

void Firmap::clone_lg_ops_node(Node &old_node, Lgraph *new_lg, PinMap &pinmap) {
  auto new_node = new_lg->create_node(old_node);
  for (auto old_spin : old_node.inp_connected_pins())
    pinmap.insert_or_assign(old_spin, new_node.setup_sink_pin_raw(old_spin.get_pid()));

  for (auto old_dpin : old_node.out_connected_pins()) {
    pinmap.insert_or_assign(old_dpin, new_node.setup_driver_pin_raw(old_dpin.get_pid()));
    if (old_dpin.has_name())
      new_node.setup_driver_pin_raw(old_dpin.get_pid()).set_name(old_dpin.get_name());
  }
}

void Firmap::clone_subgraph_node(Node &old_node_subg, Lgraph *new_lg, PinMap &pinmap) {
  auto *    library = Graph_library::instance(new_lg->get_path());
  Node      new_node_subg;
  Sub_node *new_sub;
  Sub_node *old_sub;

  auto old_sub_name = old_node_subg.get_type_sub_node().get_name();
  if (library->has_name(old_sub_name)) {
    auto old_lgid = library->get_lgid(old_node_subg.get_type_sub_node().get_name());
    old_sub       = library->ref_sub(old_lgid);
  } else {
    Pass::error("Global IO connection pass cannot find existing subgraph {} in lgdb\n", old_sub_name);
    return;
  }

  // get rid of _firrtl_ prefix to get correct new_subg_name
  I(old_sub_name.substr(0, 9) == "__firrtl_");
  auto new_subg_name = old_sub_name.substr(9);

  // create new_lg subgraph node and its affiliate Sub_node
  if (library->has_name(new_subg_name)) {
    auto new_lgid = library->get_lgid(new_subg_name);
    new_node_subg = new_lg->create_node_sub(new_lgid);
    new_sub       = library->ref_sub(new_lgid);
  } else {
    new_node_subg = new_lg->create_node_sub(new_subg_name);
    new_sub       = library->ref_sub(new_subg_name);
  }

  // clone all old_sub io to new_sub_io and setup all sink_pins and driver_pins for the new_sub node
  for (const auto &old_io_pin : old_sub->get_io_pins()) {
    if (old_io_pin.is_invalid())
      continue;
    const auto &old_io_name = old_io_pin.name;
    if (old_io_pin.is_input()) {
      Node_pin new_spin;
#if 1
      if (!new_sub->has_pin(old_io_name)) {
        // Maybe the pin got deleted
        continue;
      }
      new_spin = new_node_subg.setup_sink_pin(old_io_name);
#else
      if (!new_sub->has_pin(old_io_name)) {
        new_sub->add_input_pin(old_io_name, Port_invalid);
        new_spin = new_node_subg.setup_sink_pin(old_io_name);
      } else {
        new_spin = new_node_subg.setup_sink_pin(old_io_name);
      }
#endif

      // map the old_sub sink_pins
      auto old_spin = old_node_subg.setup_sink_pin(old_io_name);
      pinmap.insert_or_assign(old_spin, new_spin);
      continue;
    }

    // handle old_io_pin->is_output()
    I(old_io_pin.is_output());
    Node_pin new_dpin;
#if 1
    if (!new_sub->has_pin(old_io_name)) {
      continue;
    }
    new_dpin = new_node_subg.setup_driver_pin(old_io_name);
#else
    if (!new_sub->has_pin(old_io_name)) {
      new_sub->add_output_pin(old_io_name, Port_invalid);
      new_dpin = new_node_subg.setup_driver_pin(old_io_name);
    } else {
      new_dpin = new_node_subg.setup_driver_pin(old_io_name);
    }
#endif
    auto old_dpin = old_node_subg.setup_driver_pin(old_io_name);
    pinmap.insert_or_assign(old_dpin, new_dpin);
  }
}

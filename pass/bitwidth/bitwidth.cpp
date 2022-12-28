//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bitwidth.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "absl/strings/match.h"
#include "bitwidth_range.hpp"
#include "lgraph.hpp"
#include "pass_bitwidth.hpp"
#include "perf_tracing.hpp"

Bitwidth::Bitwidth(bool _hier, int _max_iterations) : max_iterations(_max_iterations), hier(_hier) {}

void Bitwidth::do_trans(Lgraph *lg) {
  // TRACE_EVENT("pass", nullptr, [&lg](perfetto::EventContext ctx) { ctx.event()->set_name("bitwidth." + lg->get_name()); });

  // note: tricks to make perfetto display different color on sub-modules
  TRACE_EVENT("pass", nullptr, [&lg](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    ctx.event()->set_name(converted_str + lg->get_name());
  });

  bw_pass(lg);
}

void Bitwidth::set_bw_1bit(Node_pin &dpin) {
  dpin.set_bits(1);
  dpin.set_sign();
  bwmap.insert_or_assign(dpin.get_compact_class(), Bitwidth_range(-1, 0));
}

void Bitwidth::set_bw_1bit(Node_pin &&dpin) {
  dpin.set_bits(1);
  dpin.set_sign();
  bwmap.insert_or_assign(dpin.get_compact_class(), Bitwidth_range(-1, 0));
}

void Bitwidth::set_bits_sign(Node_pin &dpin, const Bitwidth_range &bw) {
  auto b = bw.get_sbits();
  dpin.set_bits(b);
  if (bw.is_always_positive() && b > 0) {
    dpin.set_unsign();
    I(b > 1);
  } else {
    dpin.set_sign();
  }
}

void Bitwidth::adjust_bw(Node_pin &&dpin, const Bitwidth_range &bw) {
  if (!not_finished && !bw.is_overflow() && dpin.is_type_single_driver()) {
    if (bw.get_min() == bw.get_max()) {
      auto node = dpin.get_node();

      for (auto pin : node.inp_connected_pins()) {
        pin.del();
      }
      node.set_type_const(bw.get_min());
      auto [it, inserted] = bwmap.insert_or_assign(dpin.get_compact_class(), bw);
      set_bits_sign(dpin, it->second);
      return;
    }
  }

  auto [it, inserted] = bwmap.insert({dpin.get_compact_class(), bw});  // not use insert_or_assign because it could a bw update
  if (inserted) {
    set_bits_sign(dpin, bw);
    return;
  }

  it->second.set_wider_range(bw);
  set_bits_sign(dpin, it->second);
}

void Bitwidth::adjust_bw(Node_pin &dpin, const Bitwidth_range &bw) {
  auto [it, inserted] = bwmap.insert({dpin.get_compact_class(), bw});
  if (inserted) {
    set_bits_sign(dpin, bw);
    return;
  }

  it->second.set_wider_range(bw);
  set_bits_sign(dpin, it->second);
}

void Bitwidth::process_const(Node &node) {
  auto dpin = node.get_driver_pin();

  auto [it, inserted] = bwmap.insert_or_assign(dpin.get_compact_class(), Bitwidth_range(node.get_type_const()));
  if (inserted) {
    set_bits_sign(dpin, it->second);
  }
}

void Bitwidth::process_flop(Node &node) {
  I(node.is_sink_connected("din"));
  std::vector<Node_pin> flop_cpins;

  if (node.is_sink_connected("din")) {
    flop_cpins.emplace_back(node.get_sink_pin("din").get_driver_pin());
  }

  if (node.is_sink_connected("initial")) {
    flop_cpins.emplace_back(node.get_sink_pin("initial").get_driver_pin());
  }

  Lconst max_val;
  Lconst min_val;

  for (auto &cpin : flop_cpins) {
    auto           it = bwmap.find(cpin.get_compact_class());
    Bitwidth_range bw;

    if (it == bwmap.end()) {
      auto bits = cpin.get_bits();
      if (bits) {
        if (cpin.is_unsign()) {
          bw.set_ubits_range(bits - 1);
        } else {
          bw.set_sbits_range(bits);
        }
      } else {
        // bwmap.insert_or_assign(cpin.get_compact_class(), bw); // do not overwrite if already assigned
        debug_unconstrained_msg(node, cpin);
        not_finished = true;
        return;
      }
    } else {
      bw = it->second;
    }

    auto a = bw.get_max();
    if (max_val < a) {
      if (max_val != 0) {
        not_finished = true;
      }

      max_val = a;
    }
    auto b = bw.get_min();
    if (min_val > b) {
      if (min_val != 0) {
        not_finished = true;
      }
      min_val = b;
    }
  }

  auto           dpin = node.setup_driver_pin();
  Bitwidth_range bw(min_val, max_val);
  for (auto &cpin : flop_cpins) {
    adjust_bw(cpin, bw);
  }
  adjust_bw(dpin, bw);
}

void Bitwidth::process_ror(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());

  set_bw_1bit(node.get_driver_pin());
}

void Bitwidth::process_not(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling???

  Lconst max_val;
  Lconst min_val;
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_compact_class());
    if (it != bwmap.end()) {
      auto pmax = it->second.get_max();  // pmax = parent_max
      auto pmin = it->second.get_min();
      // calculate 1s'complemet value
      auto pmax_1scomp = pmax.not_op();
      auto pmin_1scomp = pmin.not_op();
      if (pmax_1scomp > max_val) {
        max_val = pmax_1scomp;
      }

      if (pmin_1scomp > max_val) {
        max_val = pmin_1scomp;
      }

      if (pmax_1scomp < min_val) {
        min_val = pmax_1scomp;
      }

      if (pmin_1scomp < min_val) {
        min_val = pmin_1scomp;
      }

    } else {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
  }

  adjust_bw(node.setup_driver_pin(), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_mux(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling???

  Bitwidth_range bw;

  for (auto e : inp_edges) {
    if (e.sink.get_pid() == 0) {
      auto           n_data_mux = inp_edges.size() - 1;  // -1 for the selector itself
      Bitwidth_range bw2(-(n_data_mux / 2), (n_data_mux / 2) - 1);
      adjust_bw(e.driver, bw2);
      continue;
    }

    auto it = bwmap.find(e.driver.get_compact_class());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
    bw.set_wider_range(it->second);
  }
  adjust_bw(node.get_driver_pin(), bw);
}

void Bitwidth::process_shl(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);

  auto a_dpin = node.get_sink_pin("a").get_driver_pin();
  auto a_it   = bwmap.find(a_dpin.get_compact_class());

  Bitwidth_range a_bw(0);
  if (a_it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  } else {
    a_bw = a_it->second;
  }

  Bitwidth_range n_bw(0);
  bool           n_bw_initialized = false;

  for (auto &n_dpin : node.get_sink_pin("B").inp_drivers()) {
    auto n_it = bwmap.find(n_dpin.get_compact_class());

    if (unlikely(n_it == bwmap.end())) {
      debug_unconstrained_msg(node, n_dpin);
      not_finished = true;
      return;
    }

    if (unlikely(!n_it->second.is_always_positive())) {
      node.dump();
      n_it->second.dump();
      Pass::error("node {} can be negative and feeds a SHL (only positive allowed)", n_dpin.get_node().debug_name());
    }

    if (n_bw_initialized) {
      n_bw.set_wider_range(n_it->second);
    } else {
      n_bw             = n_it->second;
      n_bw_initialized = true;
    }
  }

  if (n_bw.get_sbits() == 0 || a_bw.get_sbits() == 0) {  // zero shift or zero amount to shift
    auto zero_dpin = node.create_const(0).setup_driver_pin();
    for (auto &e : node.out_edges()) {
      zero_dpin.connect(e.sink);
    }
    node.del_node();
    return;
  }
  if (!n_bw.is_always_positive()) {
    not_finished = true;
    return;
  }

  auto max     = a_bw.get_max();
  auto min     = a_bw.get_min();
  auto max_val = max << n_bw.get_max();
  auto min_val = min << n_bw.get_min();

  Bitwidth_range bw(min_val, max_val);

  adjust_bw(node.get_driver_pin(), bw);
}

void Bitwidth::process_sra(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 2);

  auto a_dpin = node.get_sink_pin("a").get_driver_pin();
  auto n_dpin = node.get_sink_pin("b").get_driver_pin();

  auto a_it = bwmap.find(a_dpin.get_compact_class());
  auto n_it = bwmap.find(n_dpin.get_compact_class());

  if (a_it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }
  auto a_bw = a_it->second;

  if (n_it == bwmap.end()) {
    debug_unconstrained_msg(node, n_dpin);
    not_finished = true;
    return;
  }
  auto n_bw = n_it->second;

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
    adjust_bw(node.get_driver_pin(), bw);
  } else {
    adjust_bw(node.get_driver_pin(), a_bw);
  }
}

void Bitwidth::process_sum(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling sum??? (delete)

  Lconst max_val;
  Lconst min_val;

  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_compact_class());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }

    if (e.sink.get_pin_name() == "A") {
      max_val = max_val + it->second.get_max();
      min_val = min_val + it->second.get_min();
    } else {
      max_val = max_val - it->second.get_min();
      min_val = min_val - it->second.get_max();
    }
  }

  adjust_bw(node.get_driver_pin(), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_memory(Node &node) {
  int64_t               mem_size              = 0;
  Bits_t                mem_bits              = 0;
  Bits_t                mem_din_bits          = 0;
  bool                  mem_din_bits_missing  = false;
  Bits_t                mem_addr_bits         = 0;
  bool                  mem_addr_bits_missing = false;
  std::vector<Node_pin> din_drivers;
  std::vector<Node_pin> addr_drivers;
  {
    for (auto &e : node.inp_edges_ordered()) {
      auto n = e.sink.get_pin_name();
      if (str_tools::ends_with(n, "clock")) {
        auto it = bwmap.find(e.driver.get_compact_class());
        if (it == bwmap.end()) {
          set_bw_1bit(e.driver);

          discovered_some_backward_nodes_try_again = true;
        }
      } else if (n == "bits" || n == "size") {
        auto val = e.driver.get_type_const();
        if (!e.driver.is_type_const()) {
          node.dump();
          Pass::error("Memory node:{} has {} connected to a non-constant pin:{}", node.debug_name(), n, e.driver.debug_name());
          return;
        }
        if (!val.is_i()) {
          node.dump();
          Pass::error("Memory node:{} has {} connected to a non-integer value of {}", node.debug_name(), n, val.to_pyrope());
          return;
        }
        auto v = val.to_i();
        if (n == "bits") {
          mem_bits = v;
        } else {
          I(n == "size");
          mem_size = v;
        }
      } else {
        auto n_din  = str_tools::ends_with(n, "din");
        auto n_addr = str_tools::ends_with(n, "addr");
        if (n_din || n_addr) {
          auto   it    = bwmap.find(e.driver.get_compact_class());
          Bits_t dbits = 0;
          if (it != bwmap.end()) {
            dbits = it->second.get_sbits();
            if (n_addr && it->second.is_always_positive()) {
              // TODO: drop bits from din too (but requires to add get_mask at the read-out)
              --dbits;
            }
          } else {
            if (n_din) {
              mem_din_bits_missing = true;
            } else {
              mem_addr_bits_missing = true;
            }
          }

          if (n_din) {
            mem_din_bits = std::max(dbits, mem_din_bits);
            din_drivers.emplace_back(e.driver);
          } else {
            I(n_addr);
            mem_addr_bits = std::max(dbits, mem_addr_bits);
            addr_drivers.emplace_back(e.driver);
          }
        }
      }
    }

#ifndef NDEBUG
    fmt::print("Memory {} has bits:{} (din bits:{}) and size:{} (addr size:{})\n",
               node.debug_name(),
               mem_bits,
               mem_din_bits,
               mem_size,
               (1UL << mem_addr_bits));
#endif
  }

  if (mem_bits && mem_din_bits) {
    if (mem_bits < mem_din_bits) {  // Compile error ahead (overflow in inputs)
      for (auto &dpin : din_drivers) {
        auto it = bwmap.find(dpin.get_compact_class());
        if (it == bwmap.end()) {
          continue;
        }

        if (it->second.get_sbits() <= mem_bits) {
          continue;
        }

        Pass::error("memory {} input pin:{} has {} bits but memory only has {} bits",
                    node.debug_name(),
                    dpin.debug_name(),
                    it->second.get_sbits(),
                    mem_bits);
        return;
      }
    } else if (!mem_din_bits_missing && mem_bits != mem_din_bits) {
      Pass::info("memory {} requests {} bits, but only {} needed (optimizing)", node.debug_name(), mem_bits, mem_din_bits);
      mem_bits = mem_din_bits;

      node.setup_sink_pin("bits").del();  // disconnect old const
      auto node_const = node.create_const(mem_bits);
      node.setup_sink_pin("bits").connect_driver(node_const);
    }
  } else if (mem_din_bits && !mem_din_bits_missing) {
    I(mem_bits == 0);
    mem_bits        = mem_din_bits;  // inferring size
    auto node_const = node.create_const(mem_bits);
    node.setup_sink_pin("bits").connect_driver(node_const);
  }

  {
    int64_t new_mem_size = 0;

    if (!mem_addr_bits_missing) {
      for (const auto &dpin : addr_drivers) {
        auto it = bwmap.find(dpin.get_compact_class());
        if (it == bwmap.end()) {
          continue;
        }

        if (!it->second.get_range().is_i()) {
          Pass::error("memory {} size {} exceeds limit", it->second.get_range().to_pyrope());
          return;
        }

        auto sz      = it->second.get_range().to_i();
        new_mem_size = std::max(sz, new_mem_size);

        // if (sz > mem_size && mem_size != 0) {
        //   Pass::error("memory {} input pin:{} needs from {} to {} but memory only has {} entries",
        //               node.debug_name(),
        //               dpin.debug_name(),
        //               it->second.get_max().to_pyrope(),
        //               it->second.get_min().to_pyrope(),
        //               mem_size);
        //   return;
        // }
      }
    }
    if (new_mem_size == 0 && mem_size == 0) {
      not_finished = true;
      Pass::info("memory {} could not infer memory size (trying again)", node.debug_name());
    }

    if (new_mem_size && (mem_size == 0 || mem_size > new_mem_size)) {
      if (mem_size) {
        node.setup_sink_pin("size").del();  // disconnect old const
        Pass::info("memory {} size requested {} but only {} needed (optimizing)", node.debug_name(), mem_size, new_mem_size);
      } else {
        Pass::info("memory {} inferring size of {}", node.debug_name(), new_mem_size);
      }
      mem_size        = new_mem_size;
      auto node_const = node.create_const(mem_size);
      node.setup_sink_pin("size").connect_driver(node_const);
    }

    if (mem_size && mem_addr_bits_missing) {
      Bitwidth_range addr_bw(-mem_size / 2, mem_size / 2 - 1);
      auto           addr_sbits = addr_bw.get_sbits();
      for (auto &dpin : addr_drivers) {
        auto it = bwmap.find(dpin.get_compact_class());
        if (it != bwmap.end()) {
          I(it->second.get_sbits() <= addr_sbits);
          ;  // error already detected in last iteration
        } else {
          bwmap.insert_or_assign(dpin.get_compact_class(), addr_bw);
          discovered_some_backward_nodes_try_again = true;
        }
      }
    }
  }

  if (mem_bits == 0) {
    Pass::info("memory {} could not infer the entry size in bits (trying again)", node.debug_name());
    not_finished = true;
    return;
  }

  Bitwidth_range data_bw;
  data_bw.set_sbits_range(mem_bits);
  for (auto &dpin : din_drivers) {
    auto it = bwmap.find(dpin.get_compact_class());
    if (it == bwmap.end()) {
      bwmap.insert_or_assign(dpin.get_compact_class(), data_bw);
    }
  }

  Bitwidth_range bw_din;
  bw_din.set_sbits_range(mem_bits);

  for (auto dpin : node.out_connected_pins()) {
    adjust_bw(dpin, bw_din);
  }
}

void Bitwidth::process_mult(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size());  // Dangling sum??? (delete)

  Lconst max_val(1);
  Lconst min_val(1);
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_compact_class());
    if (it != bwmap.end()) {
      max_val = max_val.mult_op(it->second.get_max());
      min_val = min_val.mult_op(it->second.get_min());
      if (max_val < min_val) {
        auto tmp = max_val;
        max_val  = min_val;
        min_val  = tmp;
      }
    } else {
      debug_unconstrained_msg(node, e.driver);
      GI(hier, false, "Assert! bwmap entry should be ready at final bitwidth pass, entry\n");  // e.driver.debug_name());

      not_finished = true;
      return;
    }
  }

  adjust_bw(node.get_driver_pin(), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_set_mask(Node &node) {
  auto a_dpin = node.get_sink_pin("a").get_driver_pin();
  if (a_dpin.is_invalid()) {
    node.dump();
    Pass::error("set_mask can not have an undefined input");
  }

  auto it = bwmap.find(a_dpin.get_compact_class());
  if (it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }
  Bitwidth_range bw{it->second};

  auto mask_dpin = node.get_sink_pin("mask").get_driver_pin();
  if (mask_dpin.is_invalid()) {
    node.dump();
    Pass::error("set_mask can not have an undefined mask");
  }

  if (!mask_dpin.is_type_const()) {
#if 0
    if (!not_finished) {
      node.dump();
      Pass::info("set_mask can not have a non-constant mask");
    }
#endif
    not_finished = true;
    return;
  }

  auto mask = mask_dpin.get_type_const();

  auto value_dpin = node.get_sink_pin("value").get_driver_pin();
  if (value_dpin.is_invalid()) {
    node.dump();
    Pass::error("set_mask can not have an undefined value");
  }

  auto           it2 = bwmap.find(value_dpin.get_compact_class());
  Bitwidth_range value_bw;
  if (it2 == bwmap.end()) {
    if (value_dpin.is_type_const()) {
      value_bw.set_range(Lconst(0), value_dpin.get_type_const());
    } else if (mask.is_negative()) {
      debug_unconstrained_msg(node, value_dpin);
      not_finished = true;
      return;
    } else {
      // Pass::info("bw pin:{} is not constrained but constraining to mask size", value_dpin.debug_name());
      bw.set_wider_range(Lconst(0), mask);
      adjust_bw(node.get_driver_pin(), bw);
      return;
    }
  } else {
    value_bw = it2->second;
  }

  if (mask == Lconst(-1)) {  // Just pick all the bits from value
    adjust_bw(node.get_driver_pin(), value_bw);
    return;
  }
  if (mask == Lconst(0)) {  // just do not pick anything and keep "a"
    adjust_bw(node.get_driver_pin(), bw);
    return;
  }

  auto max_val = value_bw.get_max();
  auto min_val = value_bw.get_min();
  {
    auto mask_bits = mask.get_bits();
    if (!mask.is_negative()) {  // drop upper bits from value if non negative mask
      if (mask_bits < max_val.get_bits()) {
        max_val = max_val.rsh_op(max_val.get_bits() - mask_bits);
      }
      if (mask_bits < min_val.get_bits()) {
        min_val = min_val.rsh_op(min_val.get_bits() - mask_bits);
      }
    }
  }
  {
    size_t n_zeroes_in_mask;
    if (mask.is_negative()) {  // Keep full value bits and shift left as much as mask zeroes
      auto not_mask = mask.not_op();
      I(!not_mask.is_negative());
      n_zeroes_in_mask = not_mask.popcount();

    } else {
      n_zeroes_in_mask = mask.get_trailing_zeroes();  // mask_affects_sign, so -1
    }

    max_val = max_val.lsh_op(n_zeroes_in_mask);
    min_val = min_val.lsh_op(n_zeroes_in_mask);
  }

  if (mask.is_negative()) {
    adjust_bw(node.get_driver_pin(), Bitwidth_range(min_val, max_val));
  } else {
    bw.set_wider_range(min_val, max_val);
    adjust_bw(node.get_driver_pin(), bw);
  }
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

  auto it = bwmap.find(a_dpin.get_compact_class());
  if (it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }

  auto   it2 = bwmap.find(mask_dpin.get_compact_class());
  Lconst mask_val;
  if (it2 == bwmap.end()) {
    if (!mask_dpin.is_type_const()) {
      debug_unconstrained_msg(node, mask_dpin);
      not_finished = true;
      return;
    }
    mask_val = mask_dpin.get_type_const();
  } else {
    mask_val = it2->second.get_max().or_op(it2->second.get_min());
  }

  Lconst a_max = it->second.get_max();
  Lconst a_min = it->second.get_min();

  Lconst res_max;
  if (a_max == a_min) {
    res_max = a_max.get_mask_op(mask_val);
  } else {
    res_max = a_max.get_mask_value().get_mask_op(mask_val);
  }

  Lconst res_min = res_max;

  if (a_min < 0) {  // 2s complement usual
    auto tmp = Lconst(-1).get_mask_op(mask_val);
    if (tmp > res_max) {
      res_max = tmp;
    }
    res_min = Lconst(0);

    a_min = a_min.neg_op();
  }

  Lconst val2 = a_min.get_mask_op(mask_val);
  if (val2 > res_max) {
    res_max = val2;
  }
  if (val2 < res_min) {
    res_min = val2;
  }

  if (res_min == 0 && res_max == 0 && !not_finished) {
    auto zero_dpin = node.create_const(0).setup_driver_pin();
    for (auto &e : node.out_edges()) {
      zero_dpin.connect(e.sink);
    }
    node.del_node();
    return;
  }

  adjust_bw(node.get_driver_pin(), Bitwidth_range(res_min, res_max));
}

void Bitwidth::process_sext(Node &node, XEdge_iterator &inp_edges) {
  auto wire_dpin = inp_edges[0].driver;
  auto pos_dpin  = inp_edges[1].driver;

  I(inp_edges[0].sink.get_pin_name() == "a");
  I(inp_edges[1].sink.get_pin_name() == "b");

  bool no_wire = !bwmap.contains(wire_dpin.get_compact_class());

  auto sign_max = Bits_max;
  if (pos_dpin.is_type_const()) {
    sign_max = pos_dpin.get_type_const().to_i();
  }

  if (sign_max == Bits_max && no_wire) {
    debug_unconstrained_msg(node, pos_dpin);
    not_finished = true;
    return;
  }

  Bitwidth_range bw;
  bw.set_sbits_range(sign_max);
  adjust_bw(node.get_driver_pin(), bw);

  if (hier || not_finished || no_wire) {
    return;
  }

  auto wire_it = bwmap.find(wire_dpin.get_compact_class());
  {
    auto b = wire_it->second.get_sbits();
    if (b <= sign_max) {  // sext is useless
      sign_max = b;
      for (auto &e : node.out_edges()) {
        e.sink.connect_driver(inp_edges[0].driver);
      }
      node.del_node();
    }
  }

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
        if (mask_e.size() != 2) {
          return;
        }
        auto n0  = mask_e[0].driver.get_node();
        auto n1  = mask_e[1].driver.get_node();
        auto n0c = n0.is_type_const();
        auto n1c = n1.is_type_const();
        if (!n0c && !n1c) {
          return;
        }

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

void Bitwidth::process_comparator(Node &node) { set_bw_1bit(node.get_driver_pin()); }

void Bitwidth::process_assignment_or(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() == 1);

  auto it = bwmap.find(inp_edges[0].driver.get_compact_class());
  if (it == bwmap.end()) {
    debug_unconstrained_msg(node, inp_edges[0].driver);
    not_finished = true;
    return;
  }
  for (auto &out : node.out_edges()) {
    inp_edges[0].driver.connect_sink(out.sink);
  }
  node.del_node();

  // adjust_bw(node.get_driver_pin(), it->second);
}

void Bitwidth::process_bit_or(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() > 1);
  Bits_t max_bits = 0;

  bool any_negative = false;
  for (auto e : inp_edges) {
    auto   it   = bwmap.find(e.driver.get_compact_class());
    Bits_t bits = 0;
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }

    any_negative = any_negative || !it->second.is_always_positive();

    bits = it->second.get_sbits();
    if (bits > max_bits) {
      max_bits = bits;
    }
  }

  if (max_bits == 0) {
    auto zero_dpin = node.create_const(0).setup_driver_pin();
    for (auto &e : node.out_edges()) {
      zero_dpin.connect(e.sink);
    }
    node.del_node();
    return;
  }

  Lconst max_val(0);
  Lconst min_val(0);
  if (!any_negative) {
    max_val = (Lconst(1UL) << Lconst(max_bits - 1)) - 1;
  } else {
    min_val = ((Lconst(1UL) << Lconst(max_bits - 1)) - Lconst(1)).not_op();
  }

  adjust_bw(node.get_driver_pin(), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_bit_xor(Node &node, XEdge_iterator &inp_edges) {
  I(inp_edges.size() > 1);
  Bits_t max_bits = 0;

  for (auto e : inp_edges) {
    auto   it   = bwmap.find(e.driver.get_compact_class());
    Bits_t bits = 0;
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }

    if (it->second.is_always_positive()) {
      bits = it->second.get_sbits() - 1;
    } else {
      bits = it->second.get_sbits();
    }

    if (bits > max_bits) {
      max_bits = bits;
    }
  }

  auto max_val = (Lconst(1UL) << Lconst(max_bits - 1)) - 1;  // 2^62 -1
  auto min_val = Lconst(-1) - max_val;

  adjust_bw(node.get_driver_pin(), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_bit_and(Node &node, XEdge_iterator &inp_edges) {
  // note: the goal is to get the min requiered bits in advance of final global BW and calculate the min range of (max, min)
  I(!inp_edges.empty());

  int mask_pos          = -1;
  int pos_min_sbits_pos = -1;

  Bits_t pos_min_sbits = Bits_max;  // always positive max bits
  Bits_t unk_max_sbits = 0;         // may be negative minimum number of bits

  for (auto i = 0u; i < inp_edges.size(); ++i) {
    const auto &e  = inp_edges[i];
    auto        it = bwmap.find(e.driver.get_compact_class());
    if (it == bwmap.end()) {
      unk_max_sbits = Bits_max;  // We do not know
      continue;
    }

    Bits_t bw_sbits = it->second.get_sbits();
    if (bw_sbits == 0) {
      auto zero_dpin = node.create_const(0).setup_driver_pin();
      for (auto &e2 : node.out_edges()) {
        zero_dpin.connect(e2.sink);
      }
      node.del_node();
      return;
    }

    if (it->second.is_always_positive()) {
      if (bw_sbits <= pos_min_sbits) {
        if (!hier && e.driver.is_type_const()) {
          mask_pos = i;
          if (pos_min_sbits_pos < 0) {
            pos_min_sbits_pos = i;
          }
        } else {
          pos_min_sbits_pos = i;
        }
        pos_min_sbits = bw_sbits;
      }
    } else {
      if (bw_sbits > unk_max_sbits) {
        unk_max_sbits = bw_sbits;
      }
    }
  }
  mask_pos = -1;

  if (unk_max_sbits == Bits_max && pos_min_sbits == Bits_max) {
    // Nothing to do
    Pass::info("could not find constrains for AND node:{}\n", node.debug_name());
    return;
  }

  if (!hier && mask_pos >= 0 && pos_min_sbits != Bits_max && pos_min_sbits_pos != mask_pos) {
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
    min_val = Lconst(0);  // note: this could avoid the (max, min) of AND to pollute the later Sum_op
  } else {
    max_val = (Lconst(1) << Lconst(unk_max_sbits - 1)) - 1;
    min_val = Lconst(-1) - max_val;
  }

  Bitwidth_range bw(min_val, max_val);

  adjust_bw(node.get_driver_pin(), bw);

  for (auto e : inp_edges) {
    auto bw_bits = e.driver.get_bits();
    if (bw_bits) {
      continue;  // only handle unconstrained inputs
    }

    if (e.driver.is_graph_io() || e.driver.get_node().is_type_loop_last()) {
      set_bits_sign(e.driver, bw);
    }
  }

  if (mask_pos >= 0) {
    I(!hier);
    inp_edges[mask_pos].del_edge();
  }
}

Bitwidth::Attr Bitwidth::get_key_attr(std::string_view key) {
  // FIXME: code duplicated in Firmap. Create a separate class for Attr

  if (str_tools::ends_with(key, "__max")) {
    return Attr::Set_max;
  }

  if (str_tools::ends_with(key, "__min")) {
    return Attr::Set_min;
  }

  if (str_tools::ends_with(key, "__ubits")) {
    return Attr::Set_ubits;
  }

  if (str_tools::ends_with(key, "__sbits")) {
    return Attr::Set_sbits;
  }

  if (str_tools::ends_with(key, "__dp_assign")) {
    return Attr::Set_dp_assign;
  }

  return Attr::Set_other;
}

void Bitwidth::process_attr_get(Node &node) {
  I(node.is_sink_connected("field"));
  auto dpin_key = node.get_sink_pin("field").get_driver_pin();
  I(dpin_key.get_node().is_type_const());

  auto key  = dpin_key.get_type_const().to_field();
  auto attr = get_key_attr(key);
  I(attr != Attr::Set_dp_assign);  // Not get attr with __dp_assign
  if (attr == Attr::Set_other) {
    not_finished = true;
    return;
  }

  I(node.is_sink_connected("parent"));
  auto dpin_val = node.get_sink_pin("parent").get_driver_pin();

  auto it = bwmap.find(dpin_val.get_compact_class());
  if (it == bwmap.end()) {
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

  for (auto pin : node.inp_connected_pins()) {
    pin.del();
  }
  node.set_type_const(result);
}

// lhs := rhs
void Bitwidth::process_attr_set_dp_assign(Node &node_dp) {
  I(node_dp.is_sink_connected("value"));
  I(node_dp.is_sink_connected("parent"));

  auto dpin_lhs = node_dp.get_sink_pin("parent").get_driver_pin();
  auto dpin_rhs = node_dp.get_sink_pin("value").get_driver_pin();

  auto it = bwmap.find(dpin_lhs.get_compact_class());
  if (it == bwmap.end()) {
    if (!not_finished) {
      Pass::info("node:{} dp_assign lhs node:{} is not ready (another iteration)",
                 node_dp.debug_name(),
                 dpin_lhs.get_node().debug_name());
    }
    not_finished = true;
    return;
  }

  for (auto out_dpin : node_dp.out_connected_pins()) {
    bwmap.insert_or_assign(out_dpin.get_compact_class(), it->second);
  }

  auto it2 = bwmap.find(dpin_rhs.get_compact_class());
  if (it2 == bwmap.end()) {
    if (!not_finished) {
      Pass::info("node:{} dp_assign rhs node:{} is not ready (another iteration)",
                 node_dp.debug_name(),
                 dpin_rhs.get_node().debug_name());
    }
    not_finished = true;
    return;
  }
}

void Bitwidth::process_attr_set_bw(Node &node_attr, Bitwidth::Attr attr, Fwd_edge_iterator::Fwd_iter &fwd_it) {
  I(node_attr.is_sink_connected("value"));

  auto dpin_val  = node_attr.get_sink_pin("value").get_driver_pin();
  auto attr_dpin = node_attr.get_driver_pin("Y");

  // copy parent's bw for some judgement and then update to attr_set value
  Bitwidth_range bw;
  bool           parent_pending = true;

  Node_pin parent_dpin;
  if (node_attr.is_sink_connected("parent")) {
    parent_dpin = node_attr.get_sink_pin("parent").get_driver_pin();
    auto it     = bwmap.find(parent_dpin.get_compact_class());
    if (it != bwmap.end()) {
      bw             = it->second;
      parent_pending = false;
    }
  }

  I(dpin_val.get_node().is_type_const());
  auto val = dpin_val.get_node().get_type_const();

  if (attr == Attr::Set_ubits) {
    auto bits = static_cast<Bits_t>(val.to_i());

    if (!parent_pending) {
      Bitwidth_range set_bw;
      set_bw.set_ubits_range(bits);
    } else {
      bw.set_ubits_range(bits);
    }

    if (bw.get_sbits() <= bits) {  // no need to constrain, just connect parent
      if (parent_dpin.is_invalid()) {
        parent_dpin    = node_attr.create_const(0).setup_driver_pin();
        parent_pending = false;
      }

      for (auto &e : node_attr.out_edges()) {
        parent_dpin.connect(e.sink);
      }
      node_attr.del_node();
    } else {
      insert_tposs_nodes(node_attr, bits, fwd_it);
    }
  } else if (attr == Attr::Set_sbits) {
    auto bits = static_cast<Bits_t>(val.to_i());

    if (!parent_pending) {
      Bitwidth_range set_bw;
      set_bw.set_sbits_range(bits);
      if (bw.get_min() < set_bw.get_min()) {
        Pass::error("bitwidth mismatch at node {}. \nVariable {} min is {}, but constrained to {}sbits\n",
                    node_attr.debug_name(),
                    attr_dpin.debug_name(),
                    bw.get_min(),
                    bits);
      }
    } else {
      bw.set_sbits_range(bits);
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

  if (!node_attr.is_invalid()) {
    set_bits_sign(attr_dpin, bw);
    bwmap.insert_or_assign(attr_dpin.get_compact_class(), bw);
    for (auto &e : attr_dpin.out_edges()) {
      if (!e.sink.is_type_flop()) {
        continue;
      }

      auto reg_qpin = e.sink.get_node().setup_driver_pin();
      if (reg_qpin.get_bits() == 0) {
        continue;
      }

      set_bits_sign(reg_qpin, bw);
      bwmap.insert_or_assign(reg_qpin.get_compact_class(), bw);
    }
  }

  // upwards propagate for one step node_attr, most graph input BW are handled here
  if (parent_pending && !parent_dpin.is_invalid()) {
    set_bits_sign(parent_dpin, bw);
    bwmap.insert_or_assign(parent_dpin.get_compact_class(), bw);
  }
}

// for (auto &e :node_attr.out_edges()) {
// }

// insert tposs after attr node when ubits
void Bitwidth::insert_tposs_nodes(Node &node_attr_hier, Bits_t ubits, Fwd_edge_iterator::Fwd_iter &fwd_it) {
  I(absl::StrContains(node_attr_hier.get_sink_pin("field").get_driver_pin().get_type_const().to_field(), "__ubits"));

  auto node_attr = node_attr_hier.get_non_hierarchical();  // insert locally not through hierarchy
  auto name_dpin = node_attr.get_sink_pin("parent").get_driver_pin();
  if (name_dpin.is_invalid()) {
    return;
  }

  auto mask = (Lconst(1) << Lconst(ubits)) - Lconst(1);

  Node ntposs;

  for (auto &e : node_attr.out_edges()) {
    I(e.driver.get_pid() == 0);
    auto sink_type = e.sink.get_type_op();

    // this kind of assign_or comes from (1) fir_as_sint (2) fir_cvt (3) fir_as_clock (4) fir_as_async
    // all of these types don't need a tposs convertion
    if (sink_type == Ntype_op::Or && e.sink.get_node().out_edges().size() == 1) {
      continue;
    }

    if (sink_type == Ntype_op::Get_mask) {
      auto m = e.sink.get_node().get_sink_pin("mask").get_driver_pin().get_type_const();
      if (m == mask) {
        continue;
      }
    }

    if (ntposs.is_invalid()) {
      ntposs = node_attr.create(Ntype_op::Get_mask);
      ntposs.setup_sink_pin("mask").connect_driver(node_attr.create_const(mask));
      ntposs.setup_sink_pin("a").connect_driver(name_dpin);
    }

    ntposs.setup_driver_pin().connect_sink(e.sink);
    e.del_edge();
  }

  if (!ntposs.is_invalid()) {
    process_get_mask(ntposs);

    fwd_it.add_node(ntposs);  // add once the edges are added
  }
}

void Bitwidth::process_attr_set(Node &node_attr, Fwd_edge_iterator::Fwd_iter &fwd_it) {
  I(node_attr.is_sink_connected("field"));

  auto dpin_key = node_attr.get_sink_pin("field").get_driver_pin();
  auto key      = dpin_key.get_type_const().to_field();
  auto attr     = get_key_attr(key);

  if (attr == Attr::Set_other) {
    not_finished = true;
    return;
  }

  if (attr == Attr::Set_dp_assign) {
    process_attr_set_dp_assign(node_attr);
  } else {
    if (!dpin_key.get_node().is_type_const()) {
      not_finished = true;
      return;  // can not handle now
    }
    process_attr_set_bw(node_attr, attr, fwd_it);
  }
}

void Bitwidth::set_graph_boundary(const Node_pin &dpin, Node_pin &spin) {
  I(hier);  // do not call unless hierarchy is set

  if (dpin.get_class_lgraph() == spin.get_class_lgraph()) {
    return;
  }

  I(dpin.get_hidx() != spin.get_hidx());
  auto same_level_spin = spin.get_non_hierarchical();
  for (auto dpin2 : same_level_spin.inp_drivers()) {
    dpin2.set_size(dpin);
  }

  auto same_level_dpin = dpin.get_non_hierarchical();
  for (auto e : same_level_dpin.out_edges()) {
    if (e.sink.is_graph_output()) {
      e.sink.change_to_driver_from_graph_out_sink().set_size(dpin);
    }
  }
}

void Bitwidth::debug_unconstrained_msg(Node &node, Node_pin &dpin) {
  (void)node;
  (void)dpin;
#if 0  // ENABLE ONLY FOR TRACING BW
#ifndef NDEBUG
  if (dpin.has_name()) {
    fmt::print("BW-> gate:{} has input pin:{} unconstrained (lg:{})\n", node.debug_name(), dpin.debug_name(), node.get_top_lgraph()->get_name());
  } else {
    fmt::print("BW-> gate:{} has some inputs unconstrained (lg:{})\n", node.debug_name(), node.get_top_lgraph()->get_name());
  }
#endif
#endif
}

void Bitwidth::bw_pass(Lgraph *lg) {
  discovered_some_backward_nodes_try_again = true;
  // not_finished                             = true;
  not_finished = false;

  int n_iterations = 0;

  while (discovered_some_backward_nodes_try_again || not_finished) {
    discovered_some_backward_nodes_try_again = false;
    not_finished                             = false;

    // If the inputs have bits, use as a constrain
    lg->each_graph_input(
        [this](Node_pin &dpin) {
          if (dpin.get_bits()) {
            Bitwidth_range bw;

            if (dpin.is_unsign()) {
              bw.set_ubits_range(dpin.get_bits() - 1);
            } else {
              bw.set_sbits_range(dpin.get_bits());
            }
            bwmap.insert_or_assign(dpin.get_compact_class(), bw);
          }
        },
        hier);

    // FIXME->sh: Theoretically, Pyrope could have two module instances with different bits, to support
    // this, we have to reference a new bw table (hier_bwmap) which records the hidx so you could index different
    // nodes in different module instance. We only need to write/read new content to hier_bwmap at the final
    // global BW pass.
    // pseudo code
    // auto it = hier_bwmap.find(dpin.get_compact_class()) //hier_bwmap has hierarchy info, hidx
    // if (it == hier_bwmap.end()) {
    //   it2 = bwmap.find(dpin.get_compact_class());
    //   ...
    //   original BW algorithm stuff
    //   ...
    // }
    //
    //  hier_bwmap.insert_or_assign(dpin.get_compact_class());

    auto lgit = lg->forward(hier);
    for (auto fwd_it = lgit.begin(); fwd_it != lgit.end(); ++fwd_it) {
      auto node      = *fwd_it;
      auto inp_edges = node.inp_edges();
      auto op        = node.get_type_op();

      if (inp_edges.empty() && (op != Ntype_op::Const && op != Ntype_op::Sub && op != Ntype_op::LUT)) {
        if (!hier) {  // FIXME: once hier del works
          node.del_node();
        }
        continue;
      }

      if (op == Ntype_op::Const) {
        process_const(node);
        continue;
      } else if (!Ntype::is_multi_driver(op)) {
        auto dpin = node.get_driver_pin();
        auto bits = dpin.get_bits();
        if (bits) {
          Bitwidth_range bw;

          if (dpin.is_unsign()) {
            bw.set_ubits_range(bits - 1);
          } else {
            bw.set_sbits_range(bits);
          }
          bwmap.insert_or_assign(dpin.get_compact_class(), bw);  // do not overwrite if already assigned
          continue;                                              // once bits are set, do not overwrite
        }
      }

      if (op == Ntype_op::TupGet || op == Ntype_op::TupAdd) {
        continue;  // Nothing to do for this
      } else if (op == Ntype_op::Or) {
        if (inp_edges.size() == 1) {
          process_assignment_or(node, inp_edges);
        } else {
          process_bit_or(node, inp_edges);
        }
      } else if (op == Ntype_op::Xor) {
        process_bit_xor(node, inp_edges);
      } else if (op == Ntype_op::Ror) {
        process_ror(node, inp_edges);
      } else if (op == Ntype_op::And) {
        process_bit_and(node, inp_edges);
      } else if (op == Ntype_op::AttrSet) {
        process_attr_set(node, fwd_it);
      } else if (op == Ntype_op::AttrGet) {
        process_attr_get(node);
      } else if (op == Ntype_op::Memory) {
        process_memory(node);
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
      if (node.is_invalid()) {
        continue;
      }

      if (hier && !not_finished) {
        for (auto e : node.out_edges()) {
          set_graph_boundary(e.driver, e.sink);
        }
      }

    }  // end of lg->forward()

    // dump(lg);

    // set bits for graph input and output
    lg->each_graph_input(
        [this](Node_pin &dpin) {
          if (dpin.get_name() == "$") {
            return;
          }
          auto it = bwmap.find(dpin.get_compact_class());
          if (it != bwmap.end()) {
            set_bits_sign(dpin, it->second);
          }
        },
        hier);

    lg->each_graph_output(
        [this](Node_pin &dpin) {
          if (dpin.get_name() == "%") {
            return;
          }
          auto spin       = dpin.change_to_sink_from_graph_out_driver();
          auto out_driver = spin.get_driver_pin();

          if (out_driver.is_invalid()) {  // not driven output pin
            return;
          }
          auto it = bwmap.find(out_driver.get_compact_class());
          if (it == bwmap.end()) {
            return;
          }

          set_bits_sign(dpin, it->second);
          if (hier) {
            set_graph_boundary(out_driver, spin);
          }

          bwmap.insert_or_assign(dpin.get_compact_class(), it->second);
        },
        hier);

    if (discovered_some_backward_nodes_try_again && n_iterations < max_iterations) {
      Pass::info("BW-> some nodes need to back propagate width\n");
      discovered_some_backward_nodes_try_again = false;
    }
    ++n_iterations;
    if (n_iterations > max_iterations) {
      Pass::info("BW aborting after {} iterations (may require a cprop pass)", max_iterations);
      break;
    }
  }

  if (!hier && !not_finished) {
    auto *ref_sub = lg->ref_self_sub_node();

    // set bits for graph input and output
    lg->each_graph_input(
        [ref_sub](Node_pin &dpin) {
          if (dpin.get_name() == "$") {
            return;
          }
          auto b = dpin.get_bits();
          if (b && ref_sub->is_input(dpin.get_name())) {
            ref_sub->set_bits(dpin.get_name(), dpin.get_bits());  // TODO: Sign?
          }
        },
        hier);

    lg->each_graph_output(
        [ref_sub](Node_pin &dpin) {
          if (dpin.get_name() == "%") {
            return;
          }
          auto spin       = dpin.change_to_sink_from_graph_out_driver();
          auto out_driver = spin.get_driver_pin();

          if (out_driver.is_invalid()) {  // not driven output pin
            return;
          }

          auto b = dpin.get_bits();
          if (b && ref_sub->is_output(dpin.get_name())) {
            ref_sub->set_bits(dpin.get_name(), dpin.get_bits());  // TODO: Sign?
          }
        },
        hier);

    // delete all the attr_set/get for bitwidth
    for (auto node : lg->fast()) {  // Non-hierarchical erase attr
      auto op = node.get_type_op();
      if (op == Ntype_op::AttrSet) {
        try_delete_attr_node(node);
        continue;
      }
#if 0
      // DISABLED because the fast iterator can delete inputs for
      // attr_set node before the attr_set is evaluated

      if (op == Ntype_op::Const)
        continue;
      for (auto dpin : node.out_connected_pins()) {
        auto it = bwmap.find(dpin.get_compact_class());
        if (it == bwmap.end())
          continue;
        auto v = it->second.get_max();
        if (it->second.get_min() != v)
          continue;
        fmt::print("could delete node:{} with value:{}\n",node.debug_name(), v.to_pyrope());
        auto data_dpin = node.create_const(v);
        for (auto e : dpin.out_edges()) {
          e.sink.connect_driver(data_dpin);
        }
        dpin.del();
      }
#endif
    }
  }

#if 0
  if (not_finished) {
    for(auto node:lg->fast(hier)) {
      if (node.is_type_const())
        continue; // 0 constant has 0 bits :)
      for(auto dpin:node.out_connected_pins()) {
        if (dpin.get_bits()==0) {
          node.dump();
          Pass::info("BW failed to constraint pin:{}", dpin.debug_name());
          break;
        }
      }
    }
  }
#endif

  // note: only enable for detailed bitwidth debug
  // dump(lg);
}

void Bitwidth::dump(Lgraph *lg) {
  for (auto node : lg->fast(hier)) {
    for (auto dpin : node.out_connected_pins()) {
      auto it = bwmap.find(dpin.get_compact_class());
      if (it == bwmap.end()) {
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
}

void Bitwidth::try_delete_attr_node(Node &node) {
  I(!hier);

  Attr attr = Attr::Set_other;
  if (node.is_sink_connected("field")) {
    auto key_dpin = node.get_sink_pin("field").get_driver_pin();
    attr          = get_key_attr(key_dpin.get_type_const().to_field());
    if (attr == Attr::Set_other) {
      return;
    }
  }

  if (attr == Attr::Set_dp_assign) {
    auto dpin_lhs = node.get_sink_pin("parent").get_driver_pin();
    auto dpin_rhs = node.get_sink_pin("value").get_driver_pin();
    auto it1      = bwmap.find(dpin_lhs.get_compact_class());
    auto it2      = bwmap.find(dpin_rhs.get_compact_class());

    I(it1 != bwmap.end());
    I(it2 != bwmap.end());

    auto bw_lhs = it1->second;
    auto bw_rhs = it2->second;

    if (bw_rhs.get_sbits() > bw_lhs.get_sbits()) {
      auto bw_lhs_bits = bw_lhs.is_always_positive() ? bw_lhs.get_sbits() - 1 : bw_lhs.get_sbits();

      auto mask_node = node.create(Ntype_op::And);
      auto mask_dpin = mask_node.get_driver_pin();

      mask_dpin.set_bits(bw_lhs_bits);

      auto mask_const   = (Lconst(1UL) << Lconst(bw_lhs_bits)) - 1;
      auto all_one_node = node.create_const(mask_const);
      auto all_one_dpin = all_one_node.setup_driver_pin();

      // Note: I set the unsigned k-bits (max, min) for the mask
      bwmap.insert_or_assign(mask_dpin.get_compact_class(), Bitwidth_range(Lconst(0), mask_const));
      dpin_rhs.connect_sink(mask_node.setup_sink_pin("A"));
      all_one_dpin.connect_sink(mask_node.setup_sink_pin("A"));
      for (auto e : node.out_edges()) {
        mask_dpin.connect_sink(e.sink);
      }
      node.del_node();
      return;
    } else {
      // just connect the lhs and rhs
      for (auto e : node.out_edges()) {
        if (e.driver.get_pid() == 0) {  // No chain pin
          e.sink.connect_driver(dpin_rhs);
        }
      }
      node.del_node();
      return;
    }
  }

  // auto node_non_hier = node.get_non_hierarchical();
  if (node.is_sink_connected("parent")) {
    auto data_dpin = node.get_sink_pin("parent").get_driver_pin();
    for (auto e : node.out_edges()) {
      I(e.driver.get_pid() == 0);  // No chain pin
      e.sink.connect_driver(data_dpin);
    }
  } else {
    auto data_dpin = node.create_const(0);
    for (auto e : node.out_edges()) {
      e.sink.connect_driver(data_dpin);
    }
  }
  node.del_node();
}

void Bitwidth::set_subgraph_boundary_bw(Node &node) {
  auto sub_lg = node.ref_type_sub_lgraph();
  if (sub_lg == nullptr) {
    auto sub_lgid = node.get_type_sub();
    auto sub_name = node.get_class_lgraph()->get_library().get_name(sub_lgid);
    // No subnode, but it can be a blackbox (liberty)

    auto *sub_node = node.ref_type_sub_node();
    if (sub_node == nullptr) {
      // No error because sometimes we could infer backwards the size of outputs
      Pass::info("Global IO connection pass cannot find existing subgraph {} in lgdb\n", sub_name);
    } else {
      for (const auto &iopin : sub_node->get_io_pins()) {
        if (!iopin.is_output()) {
          continue;
        }

        if (iopin.bits == 0) {
          Pass::info("Global IO can not find pin {} bits from subgraph {} in lgdb\n", iopin.name, sub_name);
        }
        auto top_dpin = node.setup_driver_pin(iopin.name);

        Bitwidth_range bw;
        bw.set_sbits_range(iopin.bits);

        adjust_bw(top_dpin, bw);
      }
    }
    return;
  }

  sub_lg->each_graph_output([&node, this](Node_pin &dpin_gout) {
    auto top_dpin = node.setup_driver_pin(dpin_gout.get_name());

    Bitwidth_range bw;
    if (dpin_gout.is_unsign()) {
      if (dpin_gout.get_bits() == 1) {
        bw.set_range(
            0,
            1);  // FIXME-sh: it's for sure not an overflow case. But if use set_ubits_range(), 1-ubit will be judged as overflow
      } else {
        bw.set_ubits_range(dpin_gout.get_bits() - 1);
      }
    } else {
      bw.set_sbits_range(dpin_gout.get_bits());
    }

    adjust_bw(top_dpin, bw);
  });
}

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bitwidth.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "bitwidth_range.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "pass_bitwidth.hpp"
#include "perf_tracing.hpp"

using livehd::graph_util::bits_of;
using livehd::graph_util::const_value_of;
using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::debug_name;
using livehd::graph_util::find_sink_pin;
using livehd::graph_util::get_driver_of_sink_name;
using livehd::graph_util::inp_drivers_of;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::is_sink_connected;
using livehd::graph_util::is_type_const;
using livehd::graph_util::is_type_flop;
using livehd::graph_util::set_bits;
using livehd::graph_util::set_sign;
using livehd::graph_util::set_type_const_serialized;
using livehd::graph_util::set_type_op;
using livehd::graph_util::set_unsign;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

namespace {

// Sort inp_edges by sink port_id (LiveHD's inp_edges_ordered behaviour).
void sort_inp(std::vector<hhds::Edge_class>& edges) {
  std::sort(edges.begin(), edges.end(), [](const hhds::Edge_class& a, const hhds::Edge_class& b) {
    return a.sink.get_port_id() < b.sink.get_port_id();
  });
}

using livehd::graph_util::hydrate_const;

// Delete every edge incident to a sink pin (drivers feeding it).
void clear_sink(const hhds::Pin_class& spin) {
  if (spin.is_invalid()) {
    return;
  }
  for (auto e : spin.inp_edges()) {
    e.del_edge();
  }
}

// Delete every edge incident to a node's inputs.
void clear_all_sinks(const hhds::Node_class& node) {
  for (auto e : node.inp_edges()) {
    e.del_edge();
  }
}

// Find-or-create sink pin by LiveHD sink name (used when we need to connect
// a driver to a specific named sink that may or may not exist yet).
[[nodiscard]] hhds::Pin_class setup_sink_by_name(const hhds::Node_class& node, std::string_view name) {
  auto op = type_op_of(node);
  if (op == Ntype_op::Sub) {
    return node.create_sink_pin(name);
  }
  auto pid = Ntype::get_sink_pid(op, name);
  if (pid == livehd::Port_invalid) {
    return {};
  }
  return node.create_sink_pin(pid);
}

}  // namespace

Bitwidth::Bitwidth(bool _hier, int _max_iterations) : max_iterations(_max_iterations), hier(_hier) {}

void Bitwidth::do_trans(const std::shared_ptr<hhds::Graph>& g) {
  if (!g) {
    return;
  }

  auto gio  = g->get_io();
  auto name = gio ? std::string{gio->get_name()} : std::string{};

  TRACE_EVENT("pass", nullptr, [&name](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    ctx.event()->set_name(absl::StrCat(converted_str, name));
  });

  bw_pass(g.get());
}

void Bitwidth::set_bw_1bit(hhds::Pin_class dpin) {
  if (dpin.is_invalid()) {
    return;
  }
  set_bits(dpin, 1);
  set_sign(dpin);
  bwmap.insert_or_assign(dpin.get_class_index(), Bitwidth_range(-1, 0));
}

void Bitwidth::set_bits_sign(hhds::Pin_class& dpin, const Bitwidth_range& bw) {
  if (dpin.is_invalid()) {
    return;
  }
  auto b = bw.get_sbits();
  set_bits(dpin, b);
  if (bw.is_always_positive() && b > 0) {
    set_unsign(dpin);
    I(b > 1);
  } else {
    set_sign(dpin);
  }
}

void Bitwidth::adjust_bw(hhds::Pin_class dpin, const Bitwidth_range& bw) {
  if (dpin.is_invalid()) {
    return;
  }
  // LiveHD's "single driver" optimisation: if a node has a single driver and
  // a fully resolved (min==max) range, replace the entire input subgraph with
  // a constant. We approximate "single driver" by checking the node has no
  // multi-driver type.
  auto master = dpin.get_master_node();
  auto op     = type_op_of(master);
  if (!not_finished && !bw.is_overflow() && !Ntype::is_multi_driver(op)) {
    if (bw.get_min().same_repr(bw.get_max())) {
      // Disconnect all inputs and retype to Nconst with the resolved value.
      clear_all_sinks(master);
      set_type_const_serialized(master, bw.get_min().serialize());
      auto [it, inserted] = bwmap.insert_or_assign(dpin.get_class_index(), bw);
      set_bits_sign(dpin, it->second);
      return;
    }
  }

  auto [it, inserted] = bwmap.insert({dpin.get_class_index(), bw});
  if (inserted) {
    set_bits_sign(dpin, bw);
    return;
  }
  it->second.set_wider_range(bw);
  set_bits_sign(dpin, it->second);
}

void Bitwidth::process_const(hhds::Node_class& node) {
  auto dpin = node.create_driver_pin(0);
  auto val  = hydrate_const(node);

  auto [it, inserted] = bwmap.insert_or_assign(dpin.get_class_index(), Bitwidth_range(val));
  if (inserted) {
    set_bits_sign(dpin, it->second);
  }
}

void Bitwidth::process_flop(hhds::Node_class& node) {
  I(is_sink_connected(node, "din"));
  std::vector<hhds::Pin_class> flop_cpins;

  if (is_sink_connected(node, "din")) {
    flop_cpins.emplace_back(get_driver_of_sink_name(node, "din"));
  }
  if (is_sink_connected(node, "initial")) {
    flop_cpins.emplace_back(get_driver_of_sink_name(node, "initial"));
  }

  Const max_val;
  Const min_val;

  for (auto& cpin : flop_cpins) {
    auto           it = bwmap.find(cpin.get_class_index());
    Bitwidth_range bw;
    if (it == bwmap.end()) {
      auto bits = bits_of(cpin);
      if (bits) {
        if (livehd::graph_util::is_unsign(cpin)) {
          bw.set_ubits_range(bits - 1);
        } else {
          bw.set_sbits_range(bits);
        }
      } else {
        debug_unconstrained_msg(node, cpin);
        not_finished = true;
        return;
      }
    } else {
      bw = it->second;
    }

    auto a = bw.get_max();
    if (max_val.lt_op(a)->is_known_true()) {
      if (!max_val.is_known_zero()) {
        not_finished = true;
      }
      max_val = a;
    }
    auto b = bw.get_min();
    if (min_val.gt_op(b)->is_known_true()) {
      if (!min_val.is_known_zero()) {
        not_finished = true;
      }
      min_val = b;
    }
  }

  auto           dpin = node.create_driver_pin(0);
  Bitwidth_range bw(min_val, max_val);
  for (auto& cpin : flop_cpins) {
    adjust_bw(cpin, bw);
  }
  adjust_bw(dpin, bw);
}

void Bitwidth::process_ror(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(inp_edges.size());
  set_bw_1bit(node.create_driver_pin(0));
}

void Bitwidth::process_not(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(inp_edges.size());

  Const max_val;
  Const min_val;
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_class_index());
    if (it != bwmap.end()) {
      auto pmax        = it->second.get_max();
      auto pmin        = it->second.get_min();
      auto pmax_1scomp = pmax.not_op();
      auto pmin_1scomp = pmin.not_op();
      if (pmax_1scomp->gt_op(max_val)->is_known_true()) {
        max_val = pmax_1scomp;
      }
      if (pmin_1scomp->gt_op(max_val)->is_known_true()) {
        max_val = pmin_1scomp;
      }
      if (pmax_1scomp->lt_op(min_val)->is_known_true()) {
        min_val = pmax_1scomp;
      }
      if (pmin_1scomp->lt_op(min_val)->is_known_true()) {
        min_val = pmin_1scomp;
      }
    } else {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
  }

  adjust_bw(node.create_driver_pin(0), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_mux(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(inp_edges.size());
  Bitwidth_range bw;

  for (auto e : inp_edges) {
    if (e.sink.get_port_id() == 0) {
      auto           n_data_mux = inp_edges.size() - 1;
      Bitwidth_range bw2(-static_cast<int64_t>(n_data_mux / 2), static_cast<int64_t>(n_data_mux / 2) - 1);
      adjust_bw(e.driver, bw2);
      continue;
    }
    auto it = bwmap.find(e.driver.get_class_index());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
    bw.set_wider_range(it->second);
  }
  adjust_bw(node.create_driver_pin(0), bw);
}

void Bitwidth::process_shl(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(inp_edges.size() == 2);

  auto a_dpin = get_driver_of_sink_name(node, "a");
  auto a_it   = bwmap.find(a_dpin.get_class_index());

  Bitwidth_range a_bw{};
  if (a_it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }
  a_bw = a_it->second;

  Bitwidth_range n_bw{};
  bool           n_bw_initialized = false;

  for (auto& n_dpin : inp_drivers_of(node, "B")) {
    auto n_it = bwmap.find(n_dpin.get_class_index());
    if (unlikely(n_it == bwmap.end())) {
      debug_unconstrained_msg(node, n_dpin);
      not_finished = true;
      return;
    }
    if (unlikely(!n_it->second.is_always_positive())) {
      Pass::error("SHL B input can be negative (only positive allowed)");
    }
    if (n_bw_initialized) {
      n_bw.set_wider_range(n_it->second);
    } else {
      n_bw             = n_it->second;
      n_bw_initialized = true;
    }
  }

  if (n_bw.get_sbits() == 0 || a_bw.get_sbits() == 0) {
    auto zero_dpin = create_const(*current_graph, *Dlop::create_integer(0));
    for (auto& e : node.out_edges()) {
      zero_dpin.connect_sink(e.sink);
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
  auto max_val = max.lsh_op(n_bw.get_max().to_i());
  auto min_val = min.lsh_op(n_bw.get_min().to_i());

  Bitwidth_range bw(min_val, max_val);
  adjust_bw(node.create_driver_pin(0), bw);
}

void Bitwidth::process_sra(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(inp_edges.size() == 2);

  auto a_dpin = get_driver_of_sink_name(node, "a");
  auto n_dpin = get_driver_of_sink_name(node, "b");

  auto a_it = bwmap.find(a_dpin.get_class_index());
  auto n_it = bwmap.find(n_dpin.get_class_index());

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

  if (n_bw.get_min().is_positive() && n_bw.get_min().is_i()) {
    auto max     = a_bw.get_max();
    auto min     = a_bw.get_min();
    auto amount  = Dlop::create_integer(1)->lsh_op(n_bw.get_min().to_i());
    auto max_val = max.div_op(*amount);
    auto min_val = min.div_op(*amount);

    Bitwidth_range bw(min_val, max_val);
    adjust_bw(node.create_driver_pin(0), bw);
  } else {
    adjust_bw(node.create_driver_pin(0), a_bw);
  }
}

void Bitwidth::process_sum(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(inp_edges.size());

  Const max_val;
  Const min_val;

  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_class_index());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
    auto pid = e.sink.get_port_id();
    // Sum: sink "A" (pid 0) adds, sink "B" (pid 1) subtracts.
    if (pid == 0) {
      max_val = max_val.add_op(it->second.get_max());
      min_val = min_val.add_op(it->second.get_min());
    } else {
      max_val = max_val.sub_op(it->second.get_min());
      min_val = min_val.sub_op(it->second.get_max());
    }
  }

  adjust_bw(node.create_driver_pin(0), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_memory(hhds::Node_class& node) {
  int64_t                       mem_size              = 0;
  Bits_t                        mem_bits              = 0;
  Bits_t                        mem_din_bits          = 0;
  bool                          mem_din_bits_missing  = false;
  Bits_t                        mem_addr_bits         = 0;
  bool                          mem_addr_bits_missing = false;
  std::vector<hhds::Pin_class>  din_drivers;
  std::vector<hhds::Pin_class>  addr_drivers;

  auto inp = node.inp_edges();
  sort_inp(inp);
  for (auto& e : inp) {
    auto raw_pid = static_cast<int>(e.sink.get_port_id());
    auto n       = Ntype::get_sink_name(Ntype_op::Memory, raw_pid % 11);
    if (str_tools::ends_with(n, "clock")) {
      auto it = bwmap.find(e.driver.get_class_index());
      if (it == bwmap.end()) {
        set_bw_1bit(e.driver);
        discovered_some_backward_nodes_try_again = true;
      }
    } else if (n == "bits" || n == "size") {
      auto val = hydrate_const(e.driver);
      if (!is_const_pin(e.driver)) {
        Pass::error("Memory node:{} has {} connected to a non-constant pin", debug_name(node), n);
        return;
      }
      if (!val.is_i()) {
        Pass::error("Memory node:{} has {} connected to a non-integer value", debug_name(node), n);
        return;
      }
      auto v = val.to_i();
      if (n == "bits") {
        mem_bits = v;
      } else {
        mem_size = v;
      }
    } else {
      auto n_din  = str_tools::ends_with(n, "din");
      auto n_addr = str_tools::ends_with(n, "addr");
      if (n_din || n_addr) {
        auto   it    = bwmap.find(e.driver.get_class_index());
        Bits_t dbits = 0;
        if (it != bwmap.end()) {
          dbits = it->second.get_sbits();
          if (n_addr && it->second.is_always_positive()) {
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
          mem_addr_bits = std::max(dbits, mem_addr_bits);
          addr_drivers.emplace_back(e.driver);
        }
      }
    }
  }

  if (mem_bits && mem_din_bits) {
    if (mem_bits < mem_din_bits) {
      for (auto& dpin : din_drivers) {
        auto it = bwmap.find(dpin.get_class_index());
        if (it == bwmap.end()) {
          continue;
        }
        if (it->second.get_sbits() <= mem_bits) {
          continue;
        }
        Pass::error("memory {} input has more bits than memory width", debug_name(node));
        return;
      }
    } else if (!mem_din_bits_missing && mem_bits != mem_din_bits) {
      Pass::info("memory {} requests {} bits, but only {} needed (optimizing)", debug_name(node), mem_bits, mem_din_bits);
      mem_bits = mem_din_bits;

      clear_sink(find_sink_pin(node, "bits"));
      auto cdpin = create_const(*current_graph, *Dlop::create_integer(mem_bits));
      setup_sink_by_name(node, "bits").connect_driver(cdpin);
    }
  } else if (mem_din_bits && !mem_din_bits_missing) {
    I(mem_bits == 0);
    mem_bits   = mem_din_bits;
    auto cdpin = create_const(*current_graph, *Dlop::create_integer(mem_bits));
    setup_sink_by_name(node, "bits").connect_driver(cdpin);
  }

  {
    int64_t new_mem_size = 0;
    if (!mem_addr_bits_missing) {
      for (const auto& dpin : addr_drivers) {
        auto it = bwmap.find(dpin.get_class_index());
        if (it == bwmap.end()) {
          continue;
        }
        if (!it->second.get_range().is_i()) {
          Pass::error("memory {} size exceeds limit", debug_name(node));
          return;
        }
        auto sz      = it->second.get_range().to_i();
        new_mem_size = std::max(sz, new_mem_size);
      }
    }
    if (new_mem_size == 0 && mem_size == 0) {
      not_finished = true;
      Pass::info("memory {} could not infer memory size (trying again)", debug_name(node));
    }

    if (new_mem_size && (mem_size == 0 || mem_size > new_mem_size)) {
      if (mem_size) {
        clear_sink(find_sink_pin(node, "size"));
        Pass::info("memory {} size requested {} but only {} needed (optimizing)", debug_name(node), mem_size, new_mem_size);
      } else {
        Pass::info("memory {} inferring size of {}", debug_name(node), new_mem_size);
      }
      mem_size   = new_mem_size;
      auto cdpin = create_const(*current_graph, *Dlop::create_integer(mem_size));
      setup_sink_by_name(node, "size").connect_driver(cdpin);
    }

    if (mem_size && mem_addr_bits_missing) {
      Bitwidth_range addr_bw(-mem_size / 2, mem_size / 2 - 1);
      auto           addr_sbits = addr_bw.get_sbits();
      for (auto& dpin : addr_drivers) {
        auto it = bwmap.find(dpin.get_class_index());
        if (it != bwmap.end()) {
          I(it->second.get_sbits() <= addr_sbits);
        } else {
          bwmap.insert_or_assign(dpin.get_class_index(), addr_bw);
          discovered_some_backward_nodes_try_again = true;
        }
      }
    }
  }

  if (mem_bits == 0) {
    Pass::info("memory {} could not infer the entry size in bits (trying again)", debug_name(node));
    not_finished = true;
    return;
  }

  Bitwidth_range data_bw;
  data_bw.set_sbits_range(mem_bits);
  for (auto& dpin : din_drivers) {
    auto it = bwmap.find(dpin.get_class_index());
    if (it == bwmap.end()) {
      bwmap.insert_or_assign(dpin.get_class_index(), data_bw);
    }
  }

  Bitwidth_range bw_din;
  bw_din.set_sbits_range(mem_bits);

  // Out-connected pins: walk out_edges, collect unique driver pins.
  absl::flat_hash_set<hhds::Class_index> seen;
  for (auto& e : node.out_edges()) {
    if (seen.insert(e.driver.get_class_index()).second) {
      adjust_bw(e.driver, bw_din);
    }
  }
}

void Bitwidth::process_mult(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(inp_edges.size());

  Const max_val;
  max_val = Dlop::create_integer(1);
  Const min_val;
  min_val = Dlop::create_integer(1);
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_class_index());
    if (it != bwmap.end()) {
      max_val = max_val.mult_op(it->second.get_max());
      min_val = min_val.mult_op(it->second.get_min());
      if (max_val.lt_op(min_val)->is_known_true()) {
        auto tmp = max_val;
        max_val  = min_val;
        min_val  = tmp;
      }
    } else {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
  }

  adjust_bw(node.create_driver_pin(0), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_set_mask(hhds::Node_class& node) {
  auto a_dpin = get_driver_of_sink_name(node, "a");
  if (a_dpin.is_invalid()) {
    Pass::error("set_mask can not have an undefined input");
  }

  auto it = bwmap.find(a_dpin.get_class_index());
  if (it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }
  Bitwidth_range bw{it->second};

  auto mask_dpin = get_driver_of_sink_name(node, "mask");
  if (mask_dpin.is_invalid()) {
    Pass::error("set_mask can not have an undefined mask");
  }

  if (!is_const_pin(mask_dpin)) {
    not_finished = true;
    return;
  }
  auto mask = hydrate_const(mask_dpin);

  auto value_dpin = get_driver_of_sink_name(node, "value");
  if (value_dpin.is_invalid()) {
    Pass::error("set_mask can not have an undefined value");
  }

  auto           it2 = bwmap.find(value_dpin.get_class_index());
  Bitwidth_range value_bw;
  if (it2 == bwmap.end()) {
    if (is_const_pin(value_dpin)) {
      value_bw.set_range(Dlop::create_integer(0), hydrate_const(value_dpin));
    } else if (mask.is_negative()) {
      debug_unconstrained_msg(node, value_dpin);
      not_finished = true;
      return;
    } else {
      bw.set_wider_range(Dlop::create_integer(0), mask);
      adjust_bw(node.create_driver_pin(0), bw);
      return;
    }
  } else {
    value_bw = it2->second;
  }

  if (mask.is_i() && mask.to_i() == -1) {
    adjust_bw(node.create_driver_pin(0), value_bw);
    return;
  }
  if (mask.is_known_zero()) {
    adjust_bw(node.create_driver_pin(0), bw);
    return;
  }

  auto max_val = value_bw.get_max();
  auto min_val = value_bw.get_min();
  {
    auto mask_bits = mask.get_bits();
    if (!mask.is_negative()) {
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
    if (mask.is_negative()) {
      auto not_mask = mask.not_op();
      I(!not_mask->is_negative());
      n_zeroes_in_mask = not_mask->popcount();
    } else {
      n_zeroes_in_mask = mask.get_trailing_zeroes();
    }
    max_val = max_val.lsh_op(n_zeroes_in_mask);
    min_val = min_val.lsh_op(n_zeroes_in_mask);
  }

  if (mask.is_negative()) {
    adjust_bw(node.create_driver_pin(0), Bitwidth_range(min_val, max_val));
  } else {
    bw.set_wider_range(min_val, max_val);
    adjust_bw(node.create_driver_pin(0), bw);
  }
}

void Bitwidth::process_get_mask(hhds::Node_class& node) {
  auto a_dpin = get_driver_of_sink_name(node, "a");
  if (a_dpin.is_invalid()) {
    if (!not_finished) {
      node.del_node();
    }
    return;
  }

  auto mask_dpin = get_driver_of_sink_name(node, "mask");

  auto it = bwmap.find(a_dpin.get_class_index());
  if (it == bwmap.end()) {
    debug_unconstrained_msg(node, a_dpin);
    not_finished = true;
    return;
  }

  auto  it2 = bwmap.find(mask_dpin.get_class_index());
  Const mask_val;
  if (it2 == bwmap.end()) {
    if (!is_const_pin(mask_dpin)) {
      debug_unconstrained_msg(node, mask_dpin);
      not_finished = true;
      return;
    }
    mask_val = hydrate_const(mask_dpin);
  } else {
    mask_val = it2->second.get_max().or_op(it2->second.get_min());
  }

  Const a_max = it->second.get_max();
  Const a_min = it->second.get_min();

  Const res_max;
  if (a_max.same_repr(a_min)) {
    res_max = a_max.get_mask_op(mask_val);
  } else {
    res_max = a_max.get_mask_value()->get_mask_op(mask_val);
  }

  Const res_min = res_max;

  if (a_min.is_negative()) {
    Const tmp;
    tmp = Dlop::create_integer(-1)->get_mask_op(mask_val);
    if (tmp.gt_op(res_max)->is_known_true()) {
      res_max = tmp;
    }
    res_min = *Dlop::create_integer(0);
    a_min   = a_min.neg_op();
  }

  Const val2;
  val2 = a_min.get_mask_op(mask_val);
  if (val2.gt_op(res_max)->is_known_true()) {
    res_max = val2;
  }
  if (val2.lt_op(res_min)->is_known_true()) {
    res_min = val2;
  }

  if (res_min.is_known_zero() && res_max.is_known_zero() && !not_finished) {
    auto zero_dpin = create_const(*current_graph, *Dlop::create_integer(0));
    for (auto& e : node.out_edges()) {
      zero_dpin.connect_sink(e.sink);
    }
    node.del_node();
    return;
  }

  adjust_bw(node.create_driver_pin(0), Bitwidth_range(res_min, res_max));
}

void Bitwidth::process_sext(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  // inp_edges may not be in pid order — sort by sink port_id so [0]==a, [1]==b.
  sort_inp(inp_edges);
  I(inp_edges.size() >= 2);
  auto wire_dpin = inp_edges[0].driver;
  auto pos_dpin  = inp_edges[1].driver;

  bool no_wire = !bwmap.contains(wire_dpin.get_class_index());

  auto sign_max = Bits_max;
  if (is_const_pin(pos_dpin)) {
    sign_max = hydrate_const(pos_dpin).to_i();
  }

  if (sign_max == Bits_max && no_wire) {
    debug_unconstrained_msg(node, pos_dpin);
    not_finished = true;
    return;
  }

  Bitwidth_range bw;
  bw.set_sbits_range(sign_max);
  adjust_bw(node.create_driver_pin(0), bw);

  if (hier || not_finished || no_wire) {
    return;
  }

  auto wire_it = bwmap.find(wire_dpin.get_class_index());
  {
    auto b = wire_it->second.get_sbits();
    if (b <= sign_max) {
      sign_max = b;
      for (auto& e : node.out_edges()) {
        e.sink.connect_driver(inp_edges[0].driver);
      }
      node.del_node();
    }
  }

  if (node.is_invalid()) {
    return;
  }

  auto wire_node = wire_dpin.get_master_node();
  auto wire_op   = type_op_of(wire_node);
  if (wire_op == Ntype_op::Sext || wire_op == Ntype_op::And) {
    if (wire_it->second.get_sbits() <= sign_max) {
      for (auto& e : node.out_edges()) {
        e.sink.connect_driver(wire_dpin);
      }
      node.del_node();
    } else {
      hhds::Pin_class grandpa_dpin;
      if (wire_op == Ntype_op::Sext) {
        grandpa_dpin = get_driver_of_sink_name(wire_node, "a");
      } else {
        auto mask_e = wire_node.inp_edges();
        if (mask_e.size() != 2) {
          return;
        }
        auto n0  = mask_e[0].driver.get_master_node();
        auto n1  = mask_e[1].driver.get_master_node();
        auto n0c = is_type_const(n0);
        auto n1c = is_type_const(n1);
        if (!n0c && !n1c) {
          return;
        }
        if (n0c && hydrate_const(n0).is_mask()) {
          grandpa_dpin = mask_e[1].driver;
        } else if (n1c && hydrate_const(n1).is_mask()) {
          grandpa_dpin = mask_e[0].driver;
        } else {
          return;
        }
      }
      inp_edges[0].del_edge();
      setup_sink_by_name(node, "a").connect_driver(grandpa_dpin);
    }
  }
}

void Bitwidth::process_comparator(hhds::Node_class& node) { set_bw_1bit(node.create_driver_pin(0)); }

void Bitwidth::process_assignment_or(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(inp_edges.size() == 1);

  auto it = bwmap.find(inp_edges[0].driver.get_class_index());
  if (it == bwmap.end()) {
    debug_unconstrained_msg(node, inp_edges[0].driver);
    not_finished = true;
    return;
  }
  for (auto& out : node.out_edges()) {
    inp_edges[0].driver.connect_sink(out.sink);
  }
  node.del_node();
}

void Bitwidth::process_bit_or(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(inp_edges.size() > 1);
  Bits_t max_bits     = 0;
  bool   any_negative = false;
  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_class_index());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
    any_negative = any_negative || !it->second.is_always_positive();
    auto bits    = it->second.get_sbits();
    if (bits > max_bits) {
      max_bits = bits;
    }
  }

  if (max_bits == 0) {
    auto zero_dpin = create_const(*current_graph, *Dlop::create_integer(0));
    for (auto& e : node.out_edges()) {
      zero_dpin.connect_sink(e.sink);
    }
    node.del_node();
    return;
  }

  Const max_val;
  max_val = Dlop::create_integer(0);
  Const min_val;
  min_val = Dlop::create_integer(0);
  if (!any_negative) {
    max_val = Dlop::get_mask_value(max_bits - 1);
  } else {
    min_val = Dlop::get_neg_mask_value(max_bits - 1);
  }

  adjust_bw(node.create_driver_pin(0), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_bit_xor(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(inp_edges.size() > 1);
  Bits_t max_bits = 0;

  for (auto e : inp_edges) {
    auto it = bwmap.find(e.driver.get_class_index());
    if (it == bwmap.end()) {
      debug_unconstrained_msg(node, e.driver);
      not_finished = true;
      return;
    }
    Bits_t bits;
    if (it->second.is_always_positive()) {
      bits = it->second.get_sbits() - 1;
    } else {
      bits = it->second.get_sbits();
    }
    if (bits > max_bits) {
      max_bits = bits;
    }
  }

  auto max_val = Dlop::get_mask_value(max_bits - 1);
  auto min_val = Dlop::create_integer(-1)->sub_op(max_val);

  adjust_bw(node.create_driver_pin(0), Bitwidth_range(min_val, max_val));
}

void Bitwidth::process_bit_and(hhds::Node_class& node, std::vector<hhds::Edge_class>& inp_edges) {
  I(!inp_edges.empty());

  int mask_pos          = -1;
  int pos_min_sbits_pos = -1;

  Bits_t pos_min_sbits = Bits_max;
  Bits_t unk_max_sbits = 0;

  for (auto i = 0u; i < inp_edges.size(); ++i) {
    const auto& e  = inp_edges[i];
    auto        it = bwmap.find(e.driver.get_class_index());
    if (it == bwmap.end()) {
      unk_max_sbits = Bits_max;
      continue;
    }
    Bits_t bw_sbits = it->second.get_sbits();
    if (bw_sbits == 0) {
      auto zero_dpin = create_const(*current_graph, *Dlop::create_integer(0));
      for (auto& e2 : node.out_edges()) {
        zero_dpin.connect_sink(e2.sink);
      }
      node.del_node();
      return;
    }

    if (it->second.is_always_positive()) {
      if (bw_sbits <= pos_min_sbits) {
        if (!hier && is_const_pin(e.driver)) {
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
    Pass::info("could not find constrains for AND node:{}\n", debug_name(node));
    return;
  }

  if (!hier && mask_pos >= 0 && pos_min_sbits != Bits_max && pos_min_sbits_pos != mask_pos) {
    auto v = Dlop::create_integer(1)
                 ->lsh_op(pos_min_sbits - 1)
                 ->sub_op(hydrate_const(inp_edges[mask_pos].driver));
    if (v->is_i() && v->to_i() == 1) {
      if (inp_edges.size() == 2) {
        int pos = mask_pos == 0 ? 1 : 0;
        if (is_graph_input_pin(inp_edges[pos].driver) || is_graph_output_pin(inp_edges[pos].driver)) {
          mask_pos = -1;
        } else {
          for (auto& out : node.out_edges()) {
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

  Const max_val;
  Const min_val;

  if (pos_min_sbits != Bits_max) {
    max_val = Dlop::get_mask_value(pos_min_sbits - 1);
    min_val = Dlop::create_integer(0);
  } else {
    max_val = Dlop::get_mask_value(unk_max_sbits - 1);
    min_val = Dlop::create_integer(-1)->sub_op(max_val);
  }

  Bitwidth_range bw(min_val, max_val);

  adjust_bw(node.create_driver_pin(0), bw);

  for (auto e : inp_edges) {
    auto bw_bits = bits_of(e.driver);
    if (bw_bits) {
      continue;
    }
    auto drv_node = e.driver.get_master_node();
    if (is_graph_input_pin(e.driver) || Ntype::is_loop_last(type_op_of(drv_node))) {
      set_bits_sign(e.driver, bw);
    }
  }

  if (mask_pos >= 0) {
    I(!hier);
    inp_edges[mask_pos].del_edge();
  }
}

Bitwidth::Attr Bitwidth::get_key_attr(std::string_view key) {
  if (str_tools::ends_with(key, "__max")) {
    return Attr::Set_max;
  }
  if (str_tools::ends_with(key, "__min")) {
    return Attr::Set_min;
  }
  if (str_tools::ends_with(key, "__ubits")) {
    return Attr::Set_ubits;
  }
  if (str_tools::ends_with(key, "__bits") || str_tools::ends_with(key, "__sbits")) {
    return Attr::Set_sbits;
  }
  if (str_tools::ends_with(key, "__dp_assign")) {
    return Attr::Set_dp_assign;
  }
  return Attr::Set_other;
}

void Bitwidth::process_attr_set_dp_assign(hhds::Node_class& node_dp) {
  I(is_sink_connected(node_dp, "value"));
  I(is_sink_connected(node_dp, "parent"));

  auto dpin_lhs = get_driver_of_sink_name(node_dp, "parent");
  auto dpin_rhs = get_driver_of_sink_name(node_dp, "value");

  auto it = bwmap.find(dpin_lhs.get_class_index());
  if (it == bwmap.end()) {
    if (!not_finished) {
      Pass::info("node:{} dp_assign lhs is not ready", debug_name(node_dp));
    }
    not_finished = true;
    return;
  }

  const Bitwidth_range lhs_bw = it->second;
  // Walk out_connected_pins (unique drivers from out_edges).
  absl::flat_hash_set<hhds::Class_index> seen_out;
  for (auto& e : node_dp.out_edges()) {
    if (seen_out.insert(e.driver.get_class_index()).second) {
      bwmap.insert_or_assign(e.driver.get_class_index(), lhs_bw);
    }
  }

  auto it2 = bwmap.find(dpin_rhs.get_class_index());
  if (it2 == bwmap.end()) {
    if (!not_finished) {
      Pass::info("node:{} dp_assign rhs is not ready", debug_name(node_dp));
    }
    not_finished = true;
    return;
  }
}

void Bitwidth::process_attr_set_bw(hhds::Node_class& node_attr, Bitwidth::Attr attr) {
  I(is_sink_connected(node_attr, "value"));

  auto dpin_val  = get_driver_of_sink_name(node_attr, "value");
  auto attr_dpin = node_attr.create_driver_pin("Y");

  Bitwidth_range bw;
  bool           parent_pending = true;

  hhds::Pin_class parent_dpin;
  if (is_sink_connected(node_attr, "parent")) {
    parent_dpin = get_driver_of_sink_name(node_attr, "parent");
    auto it     = bwmap.find(parent_dpin.get_class_index());
    if (it != bwmap.end()) {
      bw             = it->second;
      parent_pending = false;
    }
  }

  I(is_const_pin(dpin_val));
  auto val = hydrate_const(dpin_val);

  if (attr == Attr::Set_ubits) {
    auto bits = static_cast<Bits_t>(val.to_i());

    if (!parent_pending) {
      Bitwidth_range set_bw;
      set_bw.set_ubits_range(bits);
    } else {
      bw.set_ubits_range(bits);
    }

    if (bw.get_sbits() <= bits) {
      if (parent_dpin.is_invalid()) {
        parent_dpin    = create_const(*current_graph, *Dlop::create_integer(0));
        parent_pending = false;
      }
      for (auto& e : node_attr.out_edges()) {
        parent_dpin.connect_sink(e.sink);
      }
      node_attr.del_node();
    } else {
      insert_tposs_nodes(node_attr, bits);
    }
  } else if (attr == Attr::Set_sbits) {
    auto bits = static_cast<Bits_t>(val.to_i());

    if (!parent_pending) {
      Bitwidth_range set_bw;
      set_bw.set_sbits_range(bits);
      if (bw.get_min().lt_op(set_bw.get_min())->is_known_true()) {
        Pass::error("bitwidth mismatch at node {}: min {} exceeds {}sbits", debug_name(node_attr), bw.get_min(), bits);
      }
    } else {
      bw.set_sbits_range(bits);
    }
  } else {
    I(false);
  }

  if (!node_attr.is_invalid()) {
    set_bits_sign(attr_dpin, bw);
    bwmap.insert_or_assign(attr_dpin.get_class_index(), bw);
    for (auto& e : attr_dpin.out_edges()) {
      auto sink_node = e.sink.get_master_node();
      if (!is_type_flop(sink_node)) {
        continue;
      }
      auto reg_qpin = sink_node.create_driver_pin(0);
      if (bits_of(reg_qpin) == 0) {
        continue;
      }
      set_bits_sign(reg_qpin, bw);
      bwmap.insert_or_assign(reg_qpin.get_class_index(), bw);
    }
  }

  if (parent_pending && !parent_dpin.is_invalid()) {
    set_bits_sign(parent_dpin, bw);
    bwmap.insert_or_assign(parent_dpin.get_class_index(), bw);
  }
}

void Bitwidth::insert_tposs_nodes(hhds::Node_class& node_attr, Bits_t ubits) {
  I(absl::StrContains(hydrate_const(get_driver_of_sink_name(node_attr, "field")).to_field(), "__ubits"));

  auto name_dpin = get_driver_of_sink_name(node_attr, "parent");
  if (name_dpin.is_invalid()) {
    return;
  }

  auto mask = Dlop::get_mask_value(ubits);

  hhds::Node_class ntposs;

  for (auto& e : node_attr.out_edges()) {
    I(e.driver.get_port_id() == 0);
    auto sink_node = e.sink.get_master_node();
    auto sink_type = type_op_of(sink_node);

    if (sink_type == Ntype_op::Or && sink_node.out_edges().size() == 1) {
      continue;
    }
    if (sink_type == Ntype_op::Get_mask) {
      auto m = hydrate_const(get_driver_of_sink_name(sink_node, "mask"));
      if (m.same_repr(*mask)) {
        continue;
      }
    }

    if (ntposs.is_invalid()) {
      ntposs            = create_typed_node(*current_graph, Ntype_op::Get_mask);
      auto mask_cnode   = create_const(*current_graph, *mask);
      setup_sink_by_name(ntposs, "mask").connect_driver(mask_cnode);
      setup_sink_by_name(ntposs, "a").connect_driver(name_dpin);
    }

    ntposs.create_driver_pin(0).connect_sink(e.sink);
    e.del_edge();
  }

  if (!ntposs.is_invalid()) {
    process_get_mask(ntposs);
    pending_added_nodes.push_back(ntposs);
  }
}

void Bitwidth::process_attr_set(hhds::Node_class& node_attr) {
  I(is_sink_connected(node_attr, "field"));

  auto dpin_key = get_driver_of_sink_name(node_attr, "field");
  auto key      = hydrate_const(dpin_key).to_field();
  auto attr     = get_key_attr(key);

  if (attr == Attr::Set_other) {
    not_finished = true;
    return;
  }
  if (attr == Attr::Set_dp_assign) {
    process_attr_set_dp_assign(node_attr);
  } else {
    if (!is_const_pin(dpin_key)) {
      not_finished = true;
      return;
    }
    process_attr_set_bw(node_attr, attr);
  }
}

void Bitwidth::debug_unconstrained_msg(hhds::Node_class& node, hhds::Pin_class& dpin) {
  (void)node;
  (void)dpin;
}

void Bitwidth::bw_pass(hhds::Graph* g) {
  current_graph                            = g;
  discovered_some_backward_nodes_try_again = true;
  not_finished                             = false;

  int  n_iterations = 0;
  auto gio          = g->get_io();

  while (discovered_some_backward_nodes_try_again || not_finished) {
    discovered_some_backward_nodes_try_again = false;
    not_finished                             = false;

    // Seed bw from input pin declarations.
    if (gio) {
      for (const auto& d : gio->get_input_pin_decls()) {
        auto pin = g->get_input_pin(d.name);
        if (pin.is_invalid()) {
          continue;
        }
        auto b = bits_of(pin);
        if (b == 0) {
          b = static_cast<Bits_t>(d.bits);
        }
        if (b) {
          Bitwidth_range bw;
          bool unsign = livehd::graph_util::is_unsign(pin) || d.unsign;
          if (unsign) {
            bw.set_ubits_range(b - 1);
          } else {
            bw.set_sbits_range(b);
          }
          bwmap.insert_or_assign(pin.get_class_index(), bw);
        }
      }
    }

    // Forward-iterate nodes, collect work.
    pending_added_nodes.clear();
    std::vector<hhds::Node_class> to_visit;
    for (auto node : g->forward_class()) {
      to_visit.push_back(node);
    }
    // Also include any deferred-added nodes from prior iterations.
    for (size_t i = 0; i < to_visit.size(); ++i) {
      auto node = to_visit[i];
      if (node.is_invalid()) {
        continue;
      }
      auto inp_edges = node.inp_edges();
      sort_inp(inp_edges);
      auto op = type_op_of(node);

      if (inp_edges.empty() && op != Ntype_op::Nconst && op != Ntype_op::Sub && op != Ntype_op::LUT) {
        if (!hier) {
          node.del_node();
        }
        continue;
      }

      if (op == Ntype_op::Nconst) {
        process_const(node);
        continue;
      } else if (!Ntype::is_multi_driver(op)) {
        auto dpin = node.create_driver_pin(0);
        auto bits = bits_of(dpin);
        if (bits) {
          Bitwidth_range bw;
          if (livehd::graph_util::is_unsign(dpin)) {
            bw.set_ubits_range(bits - 1);
          } else {
            bw.set_sbits_range(bits);
          }
          bwmap.insert_or_assign(dpin.get_class_index(), bw);
          continue;
        }
      }

      if (op == Ntype_op::Or) {
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
        process_attr_set(node);
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
      }
      if (node.is_invalid()) {
        continue;
      }
    }

    // Drain pending added nodes (Get_mask inserted by insert_tposs_nodes).
    while (!pending_added_nodes.empty()) {
      auto add_batch = std::move(pending_added_nodes);
      pending_added_nodes.clear();
      for (auto add : add_batch) {
        if (!add.is_invalid()) {
          process_get_mask(add);
        }
      }
    }

    // Update graph IO bits from final bwmap.
    if (gio) {
      for (const auto& d : gio->get_input_pin_decls()) {
        if (d.name == "$") {
          continue;
        }
        auto pin = g->get_input_pin(d.name);
        if (pin.is_invalid()) {
          continue;
        }
        auto it = bwmap.find(pin.get_class_index());
        if (it != bwmap.end()) {
          set_bits_sign(pin, it->second);
        }
      }
      for (const auto& d : gio->get_output_pin_decls()) {
        if (d.name == "%") {
          continue;
        }
        auto out_sink = g->get_output_pin(d.name);
        if (out_sink.is_invalid()) {
          continue;
        }
        auto inps = out_sink.inp_edges();
        if (inps.empty()) {
          continue;
        }
        auto out_driver = inps.front().driver;
        auto it         = bwmap.find(out_driver.get_class_index());
        if (it == bwmap.end()) {
          continue;
        }
        set_bits_sign(out_sink, it->second);
        const Bitwidth_range src_bw = it->second;
        bwmap.insert_or_assign(out_sink.get_class_index(), src_bw);
      }
    }

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

  // Propagate IO bits back to the GraphIO declarations.
  if (!hier && !not_finished && gio) {
    for (const auto& d : gio->get_input_pin_decls()) {
      if (d.name == "$") {
        continue;
      }
      auto pin = g->get_input_pin(d.name);
      if (pin.is_invalid()) {
        continue;
      }
      auto b = bits_of(pin);
      if (b && gio->has_input(d.name)) {
        gio->set_bits(d.name, b);
      }
    }
    for (const auto& d : gio->get_output_pin_decls()) {
      if (d.name == "%") {
        continue;
      }
      auto out_sink = g->get_output_pin(d.name);
      if (out_sink.is_invalid()) {
        continue;
      }
      auto b = bits_of(out_sink);
      if (b && gio->has_output(d.name)) {
        gio->set_bits(d.name, b);
      }
    }

    // Delete leftover AttrSet nodes.
    std::vector<hhds::Node_class> attrs;
    for (auto node : g->fast_class()) {
      if (type_op_of(node) == Ntype_op::AttrSet) {
        attrs.push_back(node);
      }
    }
    for (auto node : attrs) {
      if (!node.is_invalid()) {
        try_delete_attr_node(node);
      }
    }
  }
}

void Bitwidth::dump(hhds::Graph* g) {
  for (auto node : g->fast_class()) {
    auto dpin = node.create_driver_pin(0);
    auto it   = bwmap.find(dpin.get_class_index());
    if (it == bwmap.end()) {
      std::print("node:{} UNKNOWN\n", debug_name(node));
    } else if (it->second.is_always_positive()) {
      std::print("node:{} pos |", debug_name(node));
      it->second.dump();
    } else if (it->second.is_always_negative()) {
      std::print("node:{} neg |", debug_name(node));
      it->second.dump();
    } else {
      std::print("node:{} both|", debug_name(node));
      it->second.dump();
    }
  }
}

void Bitwidth::try_delete_attr_node(hhds::Node_class& node) {
  I(!hier);

  Attr attr = Attr::Set_other;
  if (is_sink_connected(node, "field")) {
    auto key_dpin = get_driver_of_sink_name(node, "field");
    attr          = get_key_attr(hydrate_const(key_dpin).to_field());
    if (attr == Attr::Set_other) {
      return;
    }
  }

  if (attr == Attr::Set_dp_assign) {
    auto dpin_lhs = get_driver_of_sink_name(node, "parent");
    auto dpin_rhs = get_driver_of_sink_name(node, "value");
    auto it1      = bwmap.find(dpin_lhs.get_class_index());
    auto it2      = bwmap.find(dpin_rhs.get_class_index());

    I(it1 != bwmap.end());
    I(it2 != bwmap.end());

    auto bw_lhs = it1->second;
    auto bw_rhs = it2->second;

    if (bw_rhs.get_sbits() > bw_lhs.get_sbits()) {
      auto bw_lhs_bits = bw_lhs.is_always_positive() ? bw_lhs.get_sbits() - 1 : bw_lhs.get_sbits();

      auto mask_node = create_typed_node(*current_graph, Ntype_op::And);
      auto mask_dpin = mask_node.create_driver_pin(0);
      set_bits(mask_dpin, bw_lhs_bits);

      auto mask_const  = Dlop::get_mask_value(bw_lhs_bits);
      auto all_one_dpin = create_const(*current_graph, *mask_const);

      bwmap.insert_or_assign(mask_dpin.get_class_index(), Bitwidth_range(Dlop::create_integer(0), mask_const));
      dpin_rhs.connect_sink(setup_sink_by_name(mask_node, "A"));
      all_one_dpin.connect_sink(setup_sink_by_name(mask_node, "A"));
      for (auto e : node.out_edges()) {
        mask_dpin.connect_sink(e.sink);
      }
      node.del_node();
      return;
    } else {
      for (auto e : node.out_edges()) {
        if (e.driver.get_port_id() == 0) {
          e.sink.connect_driver(dpin_rhs);
        }
      }
      node.del_node();
      return;
    }
  }

  if (is_sink_connected(node, "parent")) {
    auto data_dpin = get_driver_of_sink_name(node, "parent");
    for (auto e : node.out_edges()) {
      I(e.driver.get_port_id() == 0);
      e.sink.connect_driver(data_dpin);
    }
  } else {
    auto data_dpin = create_const(*current_graph, *Dlop::create_integer(0));
    for (auto e : node.out_edges()) {
      e.sink.connect_driver(data_dpin);
    }
  }
  node.del_node();
}

void Bitwidth::set_subgraph_boundary_bw(hhds::Node_class& node) {
  // Look up the sub-graph via library.
  auto sub_graph = node.get_subnode_graph();
  if (!sub_graph) {
    Pass::info("Global IO connection pass cannot find existing subgraph in lgdb");
    return;
  }
  auto sub_gio2 = sub_graph->get_io();
  if (!sub_gio2) {
    return;
  }

  for (const auto& d : sub_gio2->get_output_pin_decls()) {
    auto top_dpin = node.create_driver_pin(d.name);
    auto sub_out  = sub_graph->get_output_pin(d.name);

    Bitwidth_range bw;
    auto           bits = static_cast<Bits_t>(d.bits);
    if (sub_out.is_invalid() || bits == 0) {
      continue;
    }
    bool sub_unsign = d.unsign;
    if (sub_unsign) {
      if (bits == 1) {
        bw.set_range(0, 1);
      } else {
        bw.set_ubits_range(bits - 1);
      }
    } else {
      bw.set_sbits_range(bits);
    }
    adjust_bw(top_dpin, bw);
  }
}

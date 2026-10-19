//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Test-only evaluators shared by the pass/cprop gtests and by pass/aegraph's
// aeg_parity_smoke / aeg_diff_smoke, so the two constant-propagation engines are
// judged by ONE oracle and pass/cprop's own tests gain no aegraph dependency.
// It includes gtest and is only for _test / _smoke targets (//pass/cprop:cprop_test_eval).
//
//  * mux_eval   int64 evaluator for small combinational cones (the value-oracle
//               cprop tests); unsupported operators FAIL rather than guess.
//  * Mux_graph  the fixture those tests build their cones in.
//  * dlop_eval  unlimited-precision Dlop evaluator (the low-lane tests and the
//               differential fuzzer); a '?' literal is resolved by the caller
//               through `Literal_values`, one value per literal node at ALL its uses.

#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace livehd::cprop_testing {

namespace gu      = livehd::graph_util;
using Test_pin    = hhds::Pin_class;
using Test_values = std::unordered_map<uint64_t, int64_t>;

// Independent integer evaluator for the small combinational transition cones
// below. Inputs and current state are supplied explicitly; memoization preserves
// DAG sharing. Unsupported operators fail rather than supplying a golden value.
inline int64_t mux_eval(Test_pin pin, Test_values& values) {
  if (pin.is_const()) {
    return gu::const_of(pin).to_just_i64();
  }
  auto key = static_cast<uint64_t>(pin.get_class_index().value);
  if (auto it = values.find(key); it != values.end()) {
    return it->second;
  }
  auto node = pin.get_master_node();
  auto op   = gu::type_op_of(node);
  auto at   = [&](int pid) {
    auto ds = node.get_sink_pin(pid).get_driver_pins();
    EXPECT_EQ(ds.size(), 1);
    return ds.empty() ? int64_t{0} : mux_eval(ds.front(), values);
  };
  int64_t result = 0;
  if (op == Ntype_op::Mux) {
    const auto count  = node.inp_pins_snapshot().size() - 1;
    const auto select = at(0);
    const auto arm    = count == 2                                             ? size_t(select != 0)
                        : select < 0 || static_cast<uint64_t>(select) >= count ? count - 1
                                                                               : size_t(select);
    result            = at(arm + 1);
  } else if (op == Ntype_op::Not) {
    result = ~at(0);
  } else if (op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::LT || op == Ntype_op::GT) {
    int64_t a = op == Ntype_op::Mult ? 1 : 0, b = 0;
    for (auto sink : node.inp_sorted_pins()) {
      const auto value = mux_eval(sink.get_driver_pin(), values);
      if (Ntype::sink_bank(op, sink.get_port_id()) == 1) {
        b += value;
      } else if (op == Ntype_op::Mult) {
        a *= value;
      } else {
        a += value;
      }
    }
    result = op == Ntype_op::LT ? a < b : op == Ntype_op::GT ? a > b : a - b;
  } else if (op == Ntype_op::Div || op == Ntype_op::Rem) {
    const auto a = at(0), b = at(1);
    EXPECT_NE(b, 0);
    result = b == 0 ? 0 : op == Ntype_op::Div ? a / b : a % b;
  } else if (op == Ntype_op::Sext) {
    const auto bit   = at(1) - 1;
    const auto mask  = (int64_t{1} << bit) - 1;
    const auto value = at(0);
    result           = (value & (int64_t{1} << bit)) ? (value | ~mask) : (value & mask);
  } else if (op == Ntype_op::Rxor || op == Ntype_op::Popcount) {
    const auto value = static_cast<uint64_t>(at(0)) & ((uint64_t{1} << at(1)) - 1);
    const auto count = std::popcount(value);
    result           = op == Ntype_op::Rxor ? count % 2 : count;
  } else if (op == Ntype_op::Hotmux) {
    auto inputs = gu::hotmux_inputs(node);
    bool found  = false;
    for (const auto& [control, value] : inputs.arms) {
      if (mux_eval(control, values) != 0) {
        EXPECT_FALSE(found) << "a rewritten Hotmux must remain one-hot";
        found  = true;
        result = mux_eval(value, values);
      }
    }
    if (!found && !inputs.fallback.is_invalid()) {
      result = mux_eval(inputs.fallback, values);
    }
  } else if (op == Ntype_op::Concat) {
    for (const auto& lane : gu::concat_lanes(node)) {
      const auto mask  = (uint64_t{1} << lane.width) - 1;
      result          |= static_cast<int64_t>((static_cast<uint64_t>(mux_eval(lane.value, values)) & mask) << lane.offset);
    }
  } else if (op == Ntype_op::Set_mask) {
    const auto [lo, hi] = *gu::bit_range(node);
    const auto mask     = ((uint64_t{1} << (hi - lo)) - 1) << lo;
    const auto base     = static_cast<uint64_t>(mux_eval(gu::get_driver_of_sink_name(node, "a"), values));
    const auto value    = static_cast<uint64_t>(mux_eval(gu::get_driver_of_sink_name(node, "value"), values));
    result              = static_cast<int64_t>((base & ~mask) | ((value << lo) & mask));
  } else if (op == Ntype_op::Get_mask) {
    // Constant contiguous window only: bits [lo,hi) of `a`, LSB-aligned.
    const auto [lo, hi] = *gu::bit_range(node);
    EXPECT_GE(lo, 0);
    const auto value = mux_eval(gu::get_driver_of_sink_name(node, "a"), values);
    result           = (value >> lo) & ((int64_t{1} << (hi - lo)) - 1);
  } else if (op == Ntype_op::SHL || op == Ntype_op::SRA) {
    // Non-negative test values only, so SRA is a logical shift here.
    const auto value  = mux_eval(gu::get_driver_of_sink_name(node, "a"), values);
    const auto amount = mux_eval(gu::get_driver_of_sink_name(node, "b"), values);
    result            = op == Ntype_op::SHL ? value << amount : value >> amount;
  } else if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Ror || op == Ntype_op::EQ || op == Ntype_op::Xor) {
    result           = op == Ntype_op::And ? -1 : 0;
    bool    first    = true;
    int64_t previous = 0;
    for (auto e_sink : node.inp_sorted_pins()) {
      for (auto e_drv : e_sink.get_driver_pins()) {
        auto value = mux_eval(e_drv, values);
        if (op == Ntype_op::And) {
          result &= value;
        } else if (op == Ntype_op::Or || op == Ntype_op::Ror) {
          result |= value;
        } else if (op == Ntype_op::Xor) {
          result ^= value;
        } else {
          if (first) {
            result = 1;
          } else {
            result &= previous == value;
          }
          previous = value;
          first    = false;
        }
      }
    }
    if (op == Ntype_op::Ror) {
      result = result != 0;
    }
  } else {
    ADD_FAILURE() << "unbound input/state or unsupported evaluator op " << gu::debug_name(node);
  }
  values.emplace(key, result);
  return result;
}

// The graph library Mux_graph builds in. A test that runs the same case under
// several engines points this at a library of its own per engine (the io names
// of a case repeat).
inline std::string mux_graph_library = "lgdb_cprop_mux_sharing";

struct Mux_graph {
  std::shared_ptr<hhds::Graph> graph;
  int                          width;
  bool                         signed_data;
  std::vector<Test_pin>        controls;
  Test_pin                     a, b, en, clock;

  Mux_graph(const std::string& name, int count, int bits = 32, bool sign = false, bool wide_control = false)
      : width(bits), signed_data(sign) {
    auto& lib = livehd::Hhds_graph_library::instance(mux_graph_library);
    auto  io  = lib.create_io(name);
    for (int i = 0; i < count; ++i) {
      const auto s = "c" + std::to_string(i);
      io->add_input(s, i + 1);
      io->set_bits(s, wide_control ? 8 : 1);
    }
    io->add_input("a", count + 1);
    io->set_bits("a", bits);
    io->add_input("b", count + 2);
    io->set_bits("b", bits);
    io->add_input("en", count + 3);
    io->set_bits("en", 1);
    io->add_input("clock", count + 4);
    io->set_bits("clock", 1);
    io->add_output("out", count + 5);
    io->set_bits("out", bits);
    io->add_output("observe", count + 6);
    io->set_bits("observe", bits);
    graph = io->create_graph();
    for (int i = 0; i < count; ++i) {
      controls.push_back(graph->get_input_pin("c" + std::to_string(i)));
    }
    a     = graph->get_input_pin("a");
    b     = graph->get_input_pin("b");
    en    = graph->get_input_pin("en");
    clock = graph->get_input_pin("clock");
    if (sign) {
      gu::set_sbits(a, bits);
      gu::set_sbits(b, bits);
    }
  }
  Test_pin         constant(int64_t value) { return gu::create_const(*graph, *Dlop::create_integer(value)); }
  hhds::Node_class node(Ntype_op op, int bits = 0) {
    auto n = gu::create_typed_node(*graph, op, bits ? bits : width);
    if (signed_data && !bits) {
      gu::set_sbits(n.create_driver_pin(0), width);
    } else {
      gu::set_ubits(n.create_driver_pin(0), bits ? bits : width);
    }
    return n;
  }
  Test_pin mux(Test_pin s, Test_pin f, Test_pin t) {
    auto n = node(Ntype_op::Mux);
    n.create_sink_pin(0).connect_driver(s);
    n.create_sink_pin(1).connect_driver(f);
    n.create_sink_pin(2).connect_driver(t);
    return n.create_driver_pin(0);
  }
  Test_pin eq(Test_pin selector, int64_t value) {
    auto n = node(Ntype_op::EQ, 1);
    // EQ is single-bank: its two operands take consecutive pids, not one pin.
    gu::setup_sink_pid(n, 0).connect_driver(selector);
    gu::setup_sink_pid(n, 0).connect_driver(constant(value));
    return n.create_driver_pin(0);
  }
  // tolg's `x != 0`: (x == 0) ^ 1.
  Test_pin not_zero(Test_pin x) {
    auto n = node(Ntype_op::Xor, 1);
    gu::setup_sink_pid(n, 0).connect_driver(eq(x, 0));
    gu::setup_sink_pid(n, 0).connect_driver(constant(1));
    return n.create_driver_pin(0);
  }
  Test_pin logic(Ntype_op op, std::initializer_list<Test_pin> operands) {
    auto n = node(op, 1);
    for (auto p : operands) {
      gu::setup_sink_pid(n, 0).connect_driver(p);
    }
    return n.create_driver_pin(0);
  }
  Test_pin output() { return graph->get_output_pin("out").get_driver_pins().front(); }
  Test_pin observed() { return graph->get_output_pin("observe").get_driver_pins().front(); }
  size_t   count(Ntype_op op) {
    size_t result = 0;
    for (auto n : graph->body().nodes()) {
      result += gu::type_op_of(n) == op;
    }
    return result;
  }
  Test_values inputs(uint64_t mask, int64_t av, int64_t bv, bool enabled = true, bool wide = false) {
    Test_values result;
    auto        put = [&](Test_pin p, int64_t v) { result.emplace(p.get_class_index().value, v); };
    put(a, av);
    put(b, bv);
    put(en, enabled);
    put(clock, 0);
    for (size_t i = 0; i < controls.size(); ++i) {
      put(controls[i], mask & (uint64_t{1} << (i % 64)) ? (wide ? 32 : 1) : 0);
    }
    return result;
  }
};

// `if c_i { acc OP= b + k_i }` unrolled: mux(c_i, acc, acc OP x_i) per link, or
// the update on the false arm. `muxes`/`updates` are the chain's cells per link.
struct Guard_chain {
  std::vector<hhds::Node_class> muxes, updates;
};
inline Guard_chain build_guard_chain(Mux_graph& f, Ntype_op op, bool update_true) {
  Guard_chain chain;
  auto        acc = f.a;
  for (int i = 0; i < 4; ++i) {
    auto term = f.node(Ntype_op::Sum);
    gu::setup_sink_pid(term, 0).connect_driver(f.b);
    gu::setup_sink_pid(term, 0).connect_driver(f.constant(3 * i + 1));
    auto upd = f.node(op);
    gu::setup_sink_pid(upd, 0).connect_driver(acc);
    gu::setup_sink_pid(upd, 0).connect_driver(term.create_driver_pin(0));
    acc = update_true ? f.mux(f.controls[i], acc, upd.create_driver_pin(0)) : f.mux(f.controls[i], upd.create_driver_pin(0), acc);
    chain.updates.push_back(upd);
    chain.muxes.push_back(acc.get_master_node());
  }
  acc.connect_sink(f.graph->get_output_pin("out"));
  return chain;
}

// ---- unlimited-precision Dlop evaluator ----------------------------------------

// Concrete values for the '?' literals of a graph, keyed by the literal const
// pin's class index. A literal missing from the map evaluates as written (the
// result may then carry unknown bits).
using Literal_values = std::unordered_map<uint64_t, Dlop>;
using Dlop_memo      = std::unordered_map<uint64_t, Dlop>;

inline uint64_t pin_key(const hhds::Pin_class& p) { return static_cast<uint64_t>(p.get_class_index().value); }

inline Dlop dlop_eval(const hhds::Pin_class& p, Dlop_memo& memo, const Literal_values* literals = nullptr) {
  if (p.is_const()) {
    if (literals != nullptr) {
      if (auto it = literals->find(pin_key(p)); it != literals->end()) {
        return it->second;
      }
    }
    return gu::const_of(p);
  }
  const auto key = pin_key(p);
  if (auto it = memo.find(key); it != memo.end()) {
    return it->second;
  }
  const auto                                  n  = p.get_master_node();
  const auto                                  op = gu::type_op_of(n);
  std::vector<std::pair<hhds::Port_id, Dlop>> in;
  for (auto s : n.inp_sorted_pins()) {
    for (auto d : s.get_driver_pins()) {
      in.emplace_back(s.get_port_id(), dlop_eval(d, memo, literals));
    }
  }
  const auto at = [&](hhds::Port_id pid) -> Dlop {
    for (const auto& [q, v] : in) {
      if (q == pid) {
        return v;
      }
    }
    ADD_FAILURE() << "missing pid " << pid << " on " << gu::debug_name(n);
    return *Dlop::create_integer(0);
  };
  Dlop r = *Dlop::create_integer(0);
  switch (op) {
    case Ntype_op::Sum:
      for (const auto& [q, v] : in) {
        r = Ntype::sink_bank(op, q) == 1 ? *r.sub_op(v) : *r.add_op(v);
      }
      break;
    case Ntype_op::Mult:
      r = *Dlop::create_integer(1);
      for (const auto& [q, v] : in) {
        r = *r.mult_op(v);
      }
      break;
    case Ntype_op::And:
      r = *Dlop::create_integer(-1);
      for (const auto& [q, v] : in) {
        r = *r.and_op(v);
      }
      break;
    case Ntype_op::Or:
      for (const auto& [q, v] : in) {
        r = *r.or_op(v);
      }
      break;
    case Ntype_op::Xor:
      for (const auto& [q, v] : in) {
        r = *r.xor_op(v);
      }
      break;
    case Ntype_op::Ror:
      // 1 when any operand has a set bit.
      for (const auto& [q, v] : in) {
        r = *r.or_op(v);
      }
      r = *r.ror_op();
      break;
    case Ntype_op::Not     : r = *at(0).not_op(); break;
    case Ntype_op::SHL     : r = *at(0).shl_op(at(1)); break;
    case Ntype_op::SRA     : r = *at(0).sra_op(at(1)); break;
    case Ntype_op::Sext: {
      // `b` is the kept bit COUNT (sign bit at b-1), as in cprop's folds.
      r = *at(0).sext_op(*at(1).sub_op(*Dlop::create_integer(1)));
      break;
    }
    case Ntype_op::Get_mask: {
      const auto [lo, hi] = *gu::bit_range(n);
      r                   = *at(0).get_mask_op_opt(lo, hi);
      break;
    }
    case Ntype_op::Mux: {
      // pin 0 selects; two arms: zero -> arm 1, else arm 2. More arms: an
      // in-range index picks its arm, anything else the last (Q1).
      const auto arms   = in.size() - 1;
      const auto select = at(0);
      if (arms == 2) {
        r = at(select.is_known_zero() ? 1 : 2);
      } else {
        const auto index = select.to_just_i64();
        r                = at(static_cast<hhds::Port_id>(index < 0 || static_cast<size_t>(index) >= arms ? arms : index + 1));
      }
      break;
    }
    case Ntype_op::EQ: r = *in[0].second.eq_op(in[1].second); break;
    case Ntype_op::LT: r = *at(0).lt_op(at(1)); break;
    case Ntype_op::GT: r = *at(0).gt_op(at(1)); break;
    case Ntype_op::Hotmux: {
      // The first active arm's value; none active -> the fallback (0 without one).
      const auto inputs = gu::hotmux_inputs(n);
      bool       found  = false;
      for (const auto& [control, value] : inputs.arms) {
        if (!dlop_eval(control, memo, literals).is_known_zero()) {
          r     = dlop_eval(value, memo, literals);
          found = true;
          break;
        }
      }
      if (!found && !inputs.fallback.is_invalid()) {
        r = dlop_eval(inputs.fallback, memo, literals);
      }
      break;
    }
    case Ntype_op::Set_mask: {
      // bits [lo,hi) of `a` replaced by the low (hi-lo) bits of `value`.
      const auto [lo, hi] = *gu::bit_range(n);
      const auto base     = dlop_eval(gu::get_driver_of_sink_name(n, "a"), memo, literals);
      const auto value    = dlop_eval(gu::get_driver_of_sink_name(n, "value"), memo, literals);
      const auto window   = *Dlop::create_integer(((int64_t{1} << (hi - lo)) - 1) << lo);
      const auto moved    = *value.get_mask_op_opt(0, hi - lo)->shl_op(*Dlop::create_integer(lo));
      r                   = *base.and_op(*window.not_op())->or_op(moved);
      break;
    }
    case Ntype_op::Concat: {
      // Each lane masked into its window (graph/cell.hpp Concat).
      for (const auto& l : gu::concat_lanes(n)) {
        r = *r.or_op(*dlop_eval(l.value, memo, literals).get_mask_op_opt(0, l.width)->shl_op(*Dlop::create_integer(l.offset)));
      }
      break;
    }
    default: ADD_FAILURE() << "unsupported op " << gu::debug_name(n); break;
  }
  // Comparisons read as 0/1 integers.
  if (r.is_bool()) {
    r = *Dlop::create_integer(r.is_known_true() ? 1 : 0);
  }
  memo.emplace(key, r);
  return r;
}

}  // namespace livehd::cprop_testing

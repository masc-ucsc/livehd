//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Low-lane narrowing (cprop_lowlane.cpp): each case is evaluated in unlimited
// precision on random signed inputs before and after Cprop{true}, and must
// agree; the cases marked `narrows` must also have been rewritten.
#include <functional>
#include <random>
#include <string>
#include <unordered_map>

#include "cprop.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace {
namespace gu = livehd::graph_util;
using Pin    = hhds::Pin_class;
using Node   = hhds::Node_class;

// Unlimited-precision evaluator over the ops the rewrites read and create.
Dlop eval(const Pin& p, std::unordered_map<uint64_t, Dlop>& memo) {
  if (p.is_const()) {
    return gu::const_of(p);
  }
  const auto key = static_cast<uint64_t>(p.get_class_index().value);
  if (auto it = memo.find(key); it != memo.end()) {
    return it->second;
  }
  const auto n  = p.get_master_node();
  const auto op = gu::type_op_of(n);
  std::vector<std::pair<hhds::Port_id, Dlop>> in;
  for (auto s : n.inp_sorted_pins()) {
    for (auto d : s.get_driver_pins()) {
      in.emplace_back(s.get_port_id(), eval(d, memo));
    }
  }
  const auto at = [&](hhds::Port_id pid) -> Dlop {
    for (const auto& [q, v] : in) {
      if (q == pid) {
        return v;
      }
    }
    ADD_FAILURE() << "missing pid " << pid;
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
    case Ntype_op::Not: r = *at(0).not_op(); break;
    case Ntype_op::SHL: r = *at(0).shl_op(at(1)); break;
    case Ntype_op::SRA: r = *at(0).sra_op(at(1)); break;
    case Ntype_op::Sext: r = *at(0).sext_op(at(1)); break;
    case Ntype_op::Get_mask: {
      const auto [lo, hi] = at(2).get_mask_range();  // mask is pid 2
      r                   = *at(0).get_mask_op_opt(lo, hi);
      break;
    }
    case Ntype_op::Mux: r = at(at(0).is_known_zero() ? 1 : 2); break;
    case Ntype_op::EQ: r = *in[0].second.eq_op(in[1].second); break;
    case Ntype_op::LT: r = *at(0).lt_op(at(1)); break;
    case Ntype_op::GT: r = *at(0).gt_op(at(1)); break;
    case Ntype_op::Concat: {
      // Each lane masked into its window (graph/cell.hpp Concat).
      for (const auto& l : gu::concat_lanes(n)) {
        r = *r.or_op(*eval(l.value, memo).get_mask_op_opt(0, l.width)->shl_op(*Dlop::create_integer(l.offset)));
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

struct Fixture {
  std::shared_ptr<hhds::Graph> g;
  Pin                          a, b, c, s;

  explicit Fixture(const std::string& name) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_cprop_lowlane");
    auto  io  = lib.create_io(name);
    int   pos = 1;
    for (const auto* in : {"a", "b", "c", "s"}) {
      io->add_input(in, pos++);
      io->set_bits(in, 16);
    }
    io->add_output("o", pos);
    io->set_bits("o", 64);
    g = io->create_graph();
    a = g->get_input_pin("a");
    b = g->get_input_pin("b");
    c = g->get_input_pin("c");
    s = g->get_input_pin("s");
  }
  Pin k(int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); }
  Pin node(Ntype_op op, std::initializer_list<std::pair<const char*, Pin>> ins) {
    auto n = gu::create_typed_node(*g, op);
    for (const auto& [name, p] : ins) {
      gu::setup_sink_by_name(n, name).connect_driver(p);
    }
    return n.create_driver_pin(0);
  }
  Pin shl(const Pin& x, int k_) { return node(Ntype_op::SHL, {{"a", x}, {"b", k(k_)}}); }
  Pin sra(const Pin& x, int k_) { return node(Ntype_op::SRA, {{"a", x}, {"b", k(k_)}}); }
  Pin low(const Pin& x, int lo, int hi) {
    return gu::create_get_mask(*g, x, gu::create_const(*g, *Dlop::get_mask_value(hi - 1, lo))).create_driver_pin(0);
  }
  Pin orr(const Pin& x, const Pin& y) { return node(Ntype_op::Or, {{"as", x}, {"as", y}}); }
  Pin mux(const Pin& sel, const Pin& f, const Pin& t) {
    auto n = gu::create_typed_node(*g, Ntype_op::Mux);
    n.create_sink_pin(0).connect_driver(sel);
    n.create_sink_pin(1).connect_driver(f);
    n.create_sink_pin(2).connect_driver(t);
    return n.create_driver_pin(0);
  }
  Pin concat(const Pin& hi, int hw, const Pin& lo, int lw) {
    auto n = gu::create_typed_node(*g, Ntype_op::Concat);
    n.create_sink_pin(0).connect_driver(hi);
    n.create_sink_pin(1).connect_driver(k(hw));
    n.create_sink_pin(2).connect_driver(lo);
    n.create_sink_pin(3).connect_driver(k(lw));
    return n.create_driver_pin(0);
  }
};

struct Case {
  std::string                     name;
  bool                            narrows;
  std::function<Pin(Fixture&)>    build;
};

std::vector<Dlop> run(Fixture& f, const std::vector<std::array<int64_t, 4>>& vectors) {
  std::vector<Dlop> out;
  const auto        o = f.g->get_output_pin("o").get_driver_pin();
  for (const auto& v : vectors) {
    std::unordered_map<uint64_t, Dlop> memo;
    const Pin                          ins[4] = {f.a, f.b, f.c, f.s};
    for (int i = 0; i < 4; ++i) {
      memo.emplace(static_cast<uint64_t>(ins[i].get_class_index().value), *Dlop::create_integer(v[i]));
    }
    out.push_back(eval(o, memo));
  }
  return out;
}

TEST(CpropLowLane, RewritesKeepTheValue) {
  const std::vector<Case> cases{
      {"sum_shl_any", true, [](Fixture& f) { return f.node(Ntype_op::Sum, {{"as", f.shl(f.a, 2)}, {"as", f.b}}); }},
      {"sum_two_forms_const",
       true,
       [](Fixture& f) { return f.node(Ntype_op::Sum, {{"as", f.shl(f.a, 3)}, {"as", f.shl(f.b, 2)}, {"as", f.k(7)}}); }},
      {"sum_carry_const", false, [](Fixture& f) { return f.node(Ntype_op::Sum, {{"as", f.shl(f.a, 2)}, {"as", f.k(-5)}}); }},
      {"sub_forms",
       true,
       [](Fixture& f) { return f.node(Ntype_op::Sum, {{"as", f.shl(f.a, 2)}, {"bs", f.shl(f.b, 2)}, {"bs", f.k(3)}}); }},
      {"sub_arbitrary_kept", false, [](Fixture& f) { return f.node(Ntype_op::Sum, {{"as", f.shl(f.a, 2)}, {"bs", f.b}}); }},
      {"sum_chain",
       true,
       [](Fixture& f) {
         auto s1 = f.node(Ntype_op::Sum, {{"as", f.shl(f.a, 2)}, {"as", f.b}});
         return f.node(Ntype_op::Sum, {{"as", s1}, {"as", f.shl(f.c, 2)}});
       }},
      {"mult_forms", true, [](Fixture& f) { return f.node(Ntype_op::Mult, {{"as", f.shl(f.a, 2)}, {"as", f.shl(f.b, 1)}}); }},
      {"mult_const", true, [](Fixture& f) { return f.node(Ntype_op::Mult, {{"as", f.shl(f.a, 2)}, {"as", f.k(12)}}); }},
      {"mult_and_mask",
       true,
       [](Fixture& f) { return f.node(Ntype_op::Mult, {{"as", f.node(Ntype_op::And, {{"as", f.a}, {"as", f.k(-8)}})}, {"as", f.b}}); }},
      {"and_zero_low", true, [](Fixture& f) { return f.node(Ntype_op::And, {{"as", f.shl(f.a, 2)}, {"as", f.b}}); }},
      {"or_two_forms",
       true,
       [](Fixture& f) { return f.node(Ntype_op::Or, {{"as", f.orr(f.shl(f.a, 2), f.k(1))}, {"as", f.orr(f.shl(f.b, 2), f.k(2))}}); }},
      {"xor_any", true, [](Fixture& f) { return f.node(Ntype_op::Xor, {{"as", f.shl(f.a, 2)}, {"as", f.b}}); }},
      {"xor_forms",
       true,
       [](Fixture& f) { return f.node(Ntype_op::Xor, {{"as", f.orr(f.shl(f.a, 2), f.k(3))}, {"as", f.shl(f.b, 3)}}); }},
      {"mux_forms", true, [](Fixture& f) { return f.mux(f.s, f.shl(f.a, 2), f.orr(f.shl(f.b, 2), f.k(1))); }},
      {"mux_const", true, [](Fixture& f) { return f.mux(f.s, f.shl(f.a, 2), f.k(-8)); }},
      {"eq_any", true, [](Fixture& f) { return f.node(Ntype_op::EQ, {{"as", f.shl(f.a, 2)}, {"as", f.b}}); }},
      {"eq_low_differs",
       true,
       [](Fixture& f) { return f.node(Ntype_op::EQ, {{"as", f.orr(f.shl(f.a, 2), f.k(1))}, {"as", f.shl(f.b, 2)}}); }},
      {"lt_forms", true, [](Fixture& f) { return f.node(Ntype_op::LT, {{"as", f.shl(f.a, 2)}, {"bs", f.shl(f.b, 2)}}); }},
      {"gt_same_low",
       true,
       [](Fixture& f) {
         return f.node(Ntype_op::GT, {{"as", f.orr(f.shl(f.a, 2), f.k(3))}, {"bs", f.orr(f.shl(f.b, 2), f.k(3))}});
       }},
      {"sra_below", true, [](Fixture& f) { return f.sra(f.orr(f.shl(f.a, 4), f.low(f.c, 0, 4)), 2); }},
      {"sra_above", true, [](Fixture& f) { return f.sra(f.orr(f.shl(f.a, 4), f.low(f.c, 0, 4)), 5); }},
      {"sra_and_mask", true, [](Fixture& f) { return f.sra(f.node(Ntype_op::And, {{"as", f.a}, {"as", f.k(-8)}}), 5); }},
      {"get_mask_high",
       true,
       [](Fixture& f) { return f.low(f.orr(f.shl(f.a, 4), f.low(f.c, 0, 4)), 4, 10); }},
      {"get_mask_low", true, [](Fixture& f) { return f.low(f.orr(f.shl(f.a, 4), f.low(f.c, 0, 4)), 0, 3); }},
      {"get_mask_straddle", false, [](Fixture& f) { return f.low(f.orr(f.shl(f.a, 4), f.low(f.c, 0, 4)), 2, 6); }},
      {"sext", true, [](Fixture& f) { return f.node(Ntype_op::Sext, {{"a", f.orr(f.shl(f.a, 2), f.k(1))}, {"b", f.k(8)}}); }},
      {"not", true, [](Fixture& f) { return f.node(Ntype_op::Not, {{"a", f.orr(f.shl(f.a, 2), f.low(f.c, 0, 2))}}); }},
      {"concat_form",
       true,
       [](Fixture& f) {
         return f.node(Ntype_op::Sum, {{"as", f.concat(f.low(f.a, 0, 4), 4, f.low(f.b, 0, 2), 2)}, {"as", f.shl(f.c, 2)}});
       }},
  };

  std::mt19937_64                          rng(7);
  std::uniform_int_distribution<int64_t>   dist(-5000, 5000);
  std::vector<std::array<int64_t, 4>>      vectors;
  for (int i = 0; i < 200; ++i) {
    vectors.push_back({dist(rng), dist(rng), dist(rng), static_cast<int64_t>(i & 1)});
  }
  for (const auto& tc : cases) {
    SCOPED_TRACE(tc.name);
    Fixture f(tc.name);
    auto    top = tc.build(f);
    top.connect_sink(f.g->get_output_pin("o"));
    const auto top_node = top.get_master_node();
    const auto before   = run(f, vectors);
    Cprop cp{true};
    cp.do_trans(f.g);
    const auto after = run(f, vectors);
    ASSERT_EQ(before.size(), after.size());
    for (size_t i = 0; i < before.size(); ++i) {
      ASSERT_TRUE(before[i].is_known_eq(after[i])) << "vector " << i << ": " << before[i].to_pyrope() << " != " << after[i].to_pyrope();
    }
    if (tc.narrows) {
      EXPECT_TRUE(top_node.is_invalid()) << "expected the rewrite";
    }
  }
}

// Off by default: plain Cprop leaves the operation alone.
TEST(CpropLowLane, OffByDefault) {
  Fixture f("off_by_default");
  auto    top = f.node(Ntype_op::Sum, {{"as", f.shl(f.a, 2)}, {"as", f.b}});
  top.connect_sink(f.g->get_output_pin("o"));
  const auto n = top.get_master_node();
  Cprop      cp;
  cp.do_trans(f.g);
  EXPECT_FALSE(n.is_invalid());
}
}  // namespace

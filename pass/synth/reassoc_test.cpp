// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "reassoc.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace livehd::synth {
namespace {
namespace gu = livehd::graph_util;

struct In {
  std::string name;
  int         bits;
  bool        sign;
};

// A tiny design: inputs, a builder for the cells, and one output `y`.
struct Design {
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::Graph> graph;
  std::vector<In>              ins;

  Design(std::vector<In> inputs, int out_bits) : ins(std::move(inputs)) {
    auto          io  = library.create_io("top");
    hhds::Port_id pid = 1;
    for (const auto& in : ins) {
      io->add_input(in.name, pid++);
      io->set_bits(in.name, static_cast<uint32_t>(in.bits));
      io->set_unsign(in.name, !in.sign);
    }
    io->add_output("y", pid);
    io->set_bits("y", static_cast<uint32_t>(out_bits));
    graph = io->create_graph();
    for (const auto& in : ins) {
      auto pin = graph->get_input_pin(in.name);
      in.sign ? gu::set_sbits(pin, in.bits) : gu::set_ubits(pin, in.bits);
    }
  }
  hhds::Pin_class in(std::string_view name) const { return graph->get_input_pin(name); }
  hhds::Node_class cell(Ntype_op op, std::initializer_list<hhds::Pin_class> operands, int bits, int color = 1, bool sign = false) {
    auto          node = gu::create_typed_node(*graph, op);
    hhds::Port_id pid  = 0;
    for (const auto& operand : operands) {
      node.create_sink_pin(pid++).connect_driver(operand);
    }
    auto out = node.create_driver_pin(0);
    sign ? gu::set_sbits(out, bits) : gu::set_ubits(out, bits);
    if (color != 0) {
      gu::set_color(node, color);
    }
    return node;
  }
  // Mux(sel, when_false, when_true), the two-arm `if` shape.
  hhds::Node_class mux(hhds::Pin_class sel, hhds::Pin_class when_false, hhds::Pin_class when_true, int bits, int color = 1) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Mux);
    node.create_sink_pin(0).connect_driver(sel);
    node.create_sink_pin(1).connect_driver(when_false);
    node.create_sink_pin(2).connect_driver(when_true);
    gu::set_ubits(node.create_driver_pin(0), bits);
    if (color != 0) {
      gu::set_color(node, color);
    }
    return node;
  }
  void output(const hhds::Node_class& node) const { node.create_driver_pin(0).connect_sink(graph->get_output_pin("y")); }

  size_t count(Ntype_op op) const {
    size_t n = 0;
    for (const auto node : graph->body().nodes()) {
      n += gu::type_op_of(node) == op ? 1 : 0;
    }
    return n;
  }
  hhds::Node_class root() const { return graph->get_output_pin("y").get_driver_pin().get_master_node(); }
  size_t           arity(const hhds::Node_class& node) const {
    size_t n = 0;
    for (const auto& sink : node.inp_sorted_pins()) {
      (void)sink;
      ++n;
    }
    return n;
  }

  // One column per driver, one row per input assignment. Evaluate each gate
  // once across all rows, including shared fan-in and both Mux arms.
  std::vector<uint64_t> truth(int out_bits) const {
    using Column = std::vector<uint64_t>;
    absl::node_hash_map<hhds::Pin_class, Column> columns;
    size_t                                       rows = 1;
    for (const auto& input : ins) {
      rows <<= input.bits;
    }
    const auto extend = [](uint64_t value, int bits, bool sign) {
      const uint64_t mask  = (uint64_t{1} << bits) - 1;
      value               &= mask;
      return sign && (value & (uint64_t{1} << (bits - 1))) ? value | ~mask : value;
    };
    size_t stride = rows;
    for (const auto& input : ins) {
      stride       >>= input.bits;
      auto& column   = columns[in(input.name)];
      column.reserve(rows);
      for (size_t row = 0; row < rows; ++row) {
        column.push_back(extend(row / stride, input.bits, input.sign));
      }
    }
    const auto evaluate = [&](auto& self, const hhds::Pin_class& pin) -> const Column& {
      auto [it, fresh] = columns.try_emplace(pin);
      auto& result     = it->second;
      if (!fresh) {
        return result;
      }
      if (pin.is_const()) {
        result.assign(rows, gu::const_of(pin).to_just_i64());
        return result;
      }
      const auto node = pin.get_master_node();
      const auto op   = gu::type_op_of(node);
      if (op == Ntype_op::Mux) {
        const auto& cond = self(self, node.get_sink_pin(0).get_driver_pin());
        const auto& low  = self(self, node.get_sink_pin(1).get_driver_pin());
        const auto& high = self(self, node.get_sink_pin(2).get_driver_pin());
        for (size_t row = 0; row < rows; ++row) {
          result.push_back(cond[row] ? high[row] : low[row]);
        }
      } else {
        EXPECT_TRUE(op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor);
        result.assign(rows, op == Ntype_op::And ? ~uint64_t{0} : 0);
        for (const auto sink : node.inp_sorted_pins()) {
          const auto& operand = self(self, sink.get_driver_pin());
          for (size_t row = 0; row < rows; ++row) {
            if (op == Ntype_op::And) {
              result[row] &= operand[row];
            } else if (op == Ntype_op::Or) {
              result[row] |= operand[row];
            } else {
              result[row] ^= operand[row];
            }
          }
        }
      }
      const int width = gu::bits_of(pin);
      if (width > 0) {
        const bool sign = !gu::is_unsign(pin);
        for (auto& value : result) {
          value = extend(value, width, sign);
        }
      }
      return result;
    };
    auto result = evaluate(evaluate, graph->get_output_pin("y").get_driver_pin());
    for (auto& value : result) {
      value = extend(value, out_bits, false);
    }
    return result;
  }
};

Reassoc_stats run(Design& d) {
  Reassoc_stats stats;
  EXPECT_TRUE(reassociate_chains(d.graph.get(), stats));
  return stats;
}

TEST(Reassoc, LoopAccumulatorChainBecomesOneNaryCell) {
  // acc |= t_i over five iterations, the shape an expanded carried loop leaves.
  for (const auto op : {Ntype_op::Or, Ntype_op::And, Ntype_op::Xor}) {
    Design d({{"a", 2, false}, {"b", 2, false}, {"c", 2, false}, {"e", 2, false}, {"f", 2, false}}, 2);
    auto   acc = d.cell(op, {d.in("a"), d.in("b")}, 2);
    acc        = d.cell(op, {acc.create_driver_pin(0), d.in("c")}, 2);
    acc        = d.cell(op, {d.in("e"), acc.create_driver_pin(0)}, 2);  // either operand position
    acc        = d.cell(op, {acc.create_driver_pin(0), d.in("f")}, 2);
    d.output(acc);
    const auto before = d.truth(2);
    const auto stats  = run(d);
    EXPECT_EQ(stats.roots, 1U);
    EXPECT_EQ(stats.absorbed, 3U);
    EXPECT_EQ(d.count(op), 1U);
    EXPECT_EQ(d.arity(d.root()), 5U);
    EXPECT_EQ(d.truth(2), before);
  }
}

TEST(Reassoc, RegionCutsSharedValuesAndOtherOperatorsStayBoundaries) {
  Design d({{"a", 1, false}, {"b", 1, false}, {"c", 1, false}, {"e", 1, false}}, 1);
  auto   other_color = d.cell(Ntype_op::Or, {d.in("a"), d.in("b")}, 1, 2);
  auto   mid         = d.cell(Ntype_op::Or, {other_color.create_driver_pin(0), d.in("c")}, 1, 1);
  auto   shared      = d.cell(Ntype_op::Or, {mid.create_driver_pin(0), d.in("e")}, 1, 1);
  auto   and_cell    = d.cell(Ntype_op::And, {shared.create_driver_pin(0), d.in("a")}, 1, 1);
  auto   root        = d.cell(Ntype_op::Or, {shared.create_driver_pin(0), and_cell.create_driver_pin(0)}, 1, 1);
  d.output(root);
  const auto before = d.truth(1);
  const auto stats  = run(d);
  // `mid` folds into `shared` (single consumer, same color). `other_color` is
  // a region cut, `shared` has two consumers, and the And is another operator.
  EXPECT_EQ(stats.absorbed, 1U);
  EXPECT_EQ(d.count(Ntype_op::Or), 3U);
  EXPECT_EQ(d.arity(shared), 3U);
  EXPECT_EQ(d.arity(root), 2U);
  EXPECT_EQ(d.truth(1), before);
}

// A virtual-flat coloring stores a sub-definition's regions per instance path
// (hier_color); two cells share a region only when every path agrees.
TEST(Reassoc, PerInstanceRegionsMustAgreeOnEveryPath) {
  for (const bool split_on_second_path : {false, true}) {
    Design     d({{"a", 1, false}, {"b", 1, false}, {"c", 1, false}}, 1);
    auto       link = d.cell(Ntype_op::Or, {d.in("a"), d.in("b")}, 1, 0);
    auto       root = d.cell(Ntype_op::Or, {link.create_driver_pin(0), d.in("c")}, 1, 0);
    const auto set  = [&](const hhds::Node_class& node, hhds::Nid site, int32_t color) {
      const auto key = hhds::make_node_attr_key(static_cast<uint64_t>(node.get_debug_nid() & ~static_cast<hhds::Nid>(3)));
      hhds::AttrRef<livehd::attrs::hier_color_t>{d.graph.get(), key, d.graph->get_gid(), {{d.graph->get_gid(), site, std::nullopt}}}
          .set(color);
    };
    set(link, 8, 1);
    set(root, 8, 1);
    set(link, 12, 2);
    set(root, 12, split_on_second_path ? 3 : 2);
    d.output(root);
    const auto stats = run(d);
    EXPECT_EQ(stats.absorbed, split_on_second_path ? 0U : 1U);
  }
}

TEST(Reassoc, NarrowerLinksFoldOnlyWhenTheirExtensionIsValueNeutral) {
  {
    // A 2-bit unsigned link over 3-bit operands truncates bit 2: keep it.
    Design d({{"a", 3, false}, {"b", 3, false}, {"c", 3, false}}, 3);
    auto   link = d.cell(Ntype_op::Or, {d.in("a"), d.in("b")}, 2);
    auto   root = d.cell(Ntype_op::Or, {link.create_driver_pin(0), d.in("c")}, 3);
    d.output(root);
    const auto before = d.truth(3);
    EXPECT_EQ(run(d).absorbed, 0U);
    EXPECT_EQ(d.count(Ntype_op::Or), 2U);
    EXPECT_EQ(d.truth(3), before);
  }
  {
    // A 2-bit unsigned link over 2-bit unsigned operands: zeros above either way.
    Design d({{"a", 2, false}, {"b", 2, false}, {"c", 3, false}}, 3);
    auto   link = d.cell(Ntype_op::Xor, {d.in("a"), d.in("b")}, 2);
    auto   root = d.cell(Ntype_op::Xor, {link.create_driver_pin(0), d.in("c")}, 3);
    d.output(root);
    const auto before = d.truth(3);
    EXPECT_EQ(run(d).absorbed, 1U);
    EXPECT_EQ(d.truth(3), before);
  }
  {
    // A signed 2-bit link over a signed 2-bit and an unsigned 1-bit operand
    // sign-extends exactly like its operands do.
    Design d({{"a", 2, true}, {"b", 1, false}, {"c", 3, true}}, 3);
    auto   link = d.cell(Ntype_op::And, {d.in("a"), d.in("b")}, 2, 1, true);
    auto   root = d.cell(Ntype_op::And, {link.create_driver_pin(0), d.in("c")}, 3, 1, true);
    d.output(root);
    const auto before = d.truth(3);
    EXPECT_EQ(run(d).absorbed, 1U);
    EXPECT_EQ(d.truth(3), before);
  }
  {
    // A signed link over an unsigned operand of its full width would turn the
    // operand's top bit into a sign: keep it.
    Design d({{"a", 2, false}, {"b", 2, true}, {"c", 3, true}}, 3);
    auto   link = d.cell(Ntype_op::Or, {d.in("a"), d.in("b")}, 2, 1, true);
    auto   root = d.cell(Ntype_op::Or, {link.create_driver_pin(0), d.in("c")}, 3, 1, true);
    d.output(root);
    const auto before = d.truth(3);
    EXPECT_EQ(run(d).absorbed, 0U);
    EXPECT_EQ(d.truth(3), before);
  }
  {
    // The rejected narrow link still merges its own subtree at its own width.
    Design d({{"a", 3, false}, {"b", 3, false}, {"c", 3, false}, {"e", 3, false}}, 3);
    auto   inner = d.cell(Ntype_op::Or, {d.in("a"), d.in("b")}, 2);
    auto   link  = d.cell(Ntype_op::Or, {inner.create_driver_pin(0), d.in("c")}, 2);
    auto   root  = d.cell(Ntype_op::Or, {link.create_driver_pin(0), d.in("e")}, 3);
    d.output(root);
    const auto before = d.truth(3);
    EXPECT_EQ(run(d).absorbed, 1U);
    EXPECT_EQ(d.arity(link), 3U);
    EXPECT_EQ(d.truth(3), before);
  }
}

TEST(Reassoc, XorParityAndIdempotentDuplicates) {
  Design d({{"a", 2, false}, {"b", 2, false}}, 2);
  auto   link = d.cell(Ntype_op::Xor, {d.in("a"), d.in("b")}, 2);
  auto   root = d.cell(Ntype_op::Xor, {link.create_driver_pin(0), d.in("a")}, 2);
  d.output(root);
  const auto before = d.truth(2);
  EXPECT_EQ(run(d).absorbed, 1U);
  EXPECT_EQ(d.arity(d.root()), 1U);  // a ^ b ^ a == b
  EXPECT_EQ(d.truth(2), before);

  Design o({{"a", 2, false}, {"b", 2, false}}, 2);
  auto   olink = o.cell(Ntype_op::Or, {o.in("a"), o.in("b")}, 2);
  auto   oroot = o.cell(Ntype_op::Or, {olink.create_driver_pin(0), o.in("a")}, 2);
  o.output(oroot);
  const auto obefore = o.truth(2);
  EXPECT_EQ(run(o).absorbed, 1U);
  EXPECT_EQ(o.arity(o.root()), 2U);
  EXPECT_EQ(o.truth(2), obefore);
}

TEST(Reassoc, ClockGateAndAndPolicyCellsKeepTheirShape) {
  Design d({{"clk", 1, false}, {"en", 1, false}, {"x", 1, false}}, 1);
  auto   latch = gu::create_typed_node(*d.graph, Ntype_op::Latch);
  gu::setup_sink_by_name(latch, "din").connect_driver(d.in("en"));
  gu::set_ubits(latch.create_driver_pin(0), 1);
  auto gate = d.cell(Ntype_op::And, {d.in("clk"), latch.create_driver_pin(0)}, 1);
  auto root = d.cell(Ntype_op::And, {gate.create_driver_pin(0), d.in("x")}, 1);
  d.output(root);
  EXPECT_EQ(run(d).absorbed, 0U);
  EXPECT_EQ(d.count(Ntype_op::And), 2U);

  Design p({{"a", 1, false}, {"b", 1, false}, {"c", 1, false}}, 1);
  auto   link = p.cell(Ntype_op::Or, {p.in("a"), p.in("b")}, 1);
  link.attr(livehd::attrs::synth_policy).set(std::string{"keep"});
  auto proot = p.cell(Ntype_op::Or, {link.create_driver_pin(0), p.in("c")}, 1);
  p.output(proot);
  EXPECT_EQ(run(p).absorbed, 0U);
}

TEST(Reassoc, LongChainIsLinear) {
  // 4096 links: one pass, each absorbed once.
  constexpr int kLinks = 4096;
  Design        d({{"a", 1, false}, {"b", 1, false}}, 1);
  auto          acc = d.cell(Ntype_op::Or, {d.in("a"), d.in("b")}, 1);
  for (int i = 0; i < kLinks; ++i) {
    acc = d.cell(Ntype_op::Or, {acc.create_driver_pin(0), i % 2 ? d.in("a") : d.in("b")}, 1);
  }
  d.output(acc);
  const auto stats = run(d);
  EXPECT_EQ(stats.absorbed, static_cast<size_t>(kLinks));
  EXPECT_EQ(d.count(Ntype_op::Or), 1U);
  EXPECT_EQ(d.arity(d.root()), 2U);  // duplicates of a and b collapse
}

// `if s_k { acc OP= x_k }` unrolled: Mux(s_k, acc, acc OP x_k) per link (or
// the update on the false arm). The guard moves onto the term and the chain
// merges into one n-ary cell: AND-OR, not a serial MUX chain.
TEST(Reassoc, GuardedAccumulatorChainBecomesOneNaryCell) {
  for (const auto op : {Ntype_op::Or, Ntype_op::And, Ntype_op::Xor}) {
    for (const int update : {2, 1}) {
      Design d({{"a", 2, false},
                {"s0", 1, false},
                {"s1", 1, false},
                {"s2", 1, false},
                {"s3", 1, false},
                {"x0", 2, false},
                {"x1", 2, false},
                {"x2", 2, false},
                {"x3", 2, false}},
               2);
      auto acc = d.in("a");
      for (int k = 0; k < 4; ++k) {
        const auto term = d.cell(op, {acc, d.in("x" + std::to_string(k))}, 2).create_driver_pin(0);
        const auto sel  = d.in("s" + std::to_string(k));
        acc             = (update == 2 ? d.mux(sel, acc, term, 2) : d.mux(sel, term, acc, 2)).create_driver_pin(0);
      }
      d.output(acc.get_master_node());
      const auto before = d.truth(2);
      const auto stats  = run(d);
      EXPECT_EQ(stats.guarded, 4U);
      EXPECT_EQ(stats.absorbed, 3U);
      EXPECT_EQ(d.count(op), 1U);
      EXPECT_EQ(d.count(Ntype_op::Mux), 4U);  // the guards, one per term
      EXPECT_EQ(d.arity(d.root()), 5U);
      EXPECT_EQ(d.truth(2), before);
    }
  }
}

TEST(Reassoc, GuardKeepsRegionCutsSharedTermsNarrowTermsAndPolicyCells) {
  const auto check = [](const char* what, auto&& build, size_t guarded) {
    SCOPED_TRACE(what);
    Design d({{"a", 2, false}, {"s", 1, false}, {"x", 2, false}, {"z", 2, false}}, 2);
    d.output(build(d));
    const auto before = d.truth(2);
    EXPECT_EQ(run(d).guarded, guarded);
    EXPECT_EQ(d.truth(2), before);
  };
  check(
      "same region",
      [](Design& d) {
        auto term = d.cell(Ntype_op::Or, {d.in("a"), d.in("x")}, 2);
        return d.mux(d.in("s"), d.in("a"), term.create_driver_pin(0), 2);
      },
      1);
  check(
      "region cut",
      [](Design& d) {
        auto term = d.cell(Ntype_op::Or, {d.in("a"), d.in("x")}, 2, 2);
        return d.mux(d.in("s"), d.in("a"), term.create_driver_pin(0), 2, 1);
      },
      0);
  check(
      "shared update",
      [](Design& d) {
        auto term = d.cell(Ntype_op::Or, {d.in("a"), d.in("x")}, 2);
        auto m    = d.mux(d.in("s"), d.in("a"), term.create_driver_pin(0), 2);
        return d.cell(Ntype_op::And, {m.create_driver_pin(0), term.create_driver_pin(0)}, 2);
      },
      0);
  check(
      "narrower update",
      [](Design& d) {
        auto term = d.cell(Ntype_op::Or, {d.in("a"), d.in("x")}, 1);
        return d.mux(d.in("s"), d.in("a"), term.create_driver_pin(0), 2);
      },
      0);
  check(
      "update without the held value",
      [](Design& d) {
        auto term = d.cell(Ntype_op::Or, {d.in("z"), d.in("x")}, 2);
        return d.mux(d.in("s"), d.in("a"), term.create_driver_pin(0), 2);
      },
      0);
  check(
      "policy",
      [](Design& d) {
        auto term = d.cell(Ntype_op::Or, {d.in("a"), d.in("x")}, 2);
        auto m    = d.mux(d.in("s"), d.in("a"), term.create_driver_pin(0), 2);
        m.attr(livehd::attrs::synth_policy).set(std::string{"keep"});
        return m;
      },
      0);
}

TEST(Reassoc, NestedGuardsRewriteFromTheInside) {
  // if c1 { if c2 { acc |= x } }: the outer Mux is examined first (no match)
  // and again once the inner one became an Or.
  Design d({{"a", 2, false}, {"c1", 1, false}, {"c2", 1, false}, {"x", 2, false}}, 2);
  auto   term  = d.cell(Ntype_op::Or, {d.in("a"), d.in("x")}, 2);
  auto   inner = d.mux(d.in("c2"), d.in("a"), term.create_driver_pin(0), 2);
  auto   outer = d.mux(d.in("c1"), d.in("a"), inner.create_driver_pin(0), 2);
  d.output(outer);
  const auto before = d.truth(2);
  const auto stats  = run(d);
  EXPECT_EQ(stats.guarded, 2U);
  EXPECT_EQ(d.count(Ntype_op::Or), 1U);
  EXPECT_EQ(gu::type_op_of(d.root()), Ntype_op::Or);
  EXPECT_EQ(d.truth(2), before);
}

// Reassoc_trees::guarded (pass.usyn) merges a chain only when every link has a
// guard operand -- Mux(c, x, id) / Mux(c, id, x), id the operator's identity --
// whether guard_terms just made it (from Mux(s, acc, acc OP x)) or cprop's
// guard_accumulation did (acc OP Mux(s, id, x)). A plain chain, or a chain
// with one unguarded link, stays exactly as written.
TEST(Reassoc, GuardedTreesMergeOnlyGuardedAccumulatorChains) {
  const auto guarded_run = [](Design& d) {
    Reassoc_stats stats;
    EXPECT_TRUE(reassociate_chains(d.graph.get(), stats, nullptr, {}, Reassoc_trees::guarded));
    return stats;
  };
  const std::vector<In> ins{{"a", 2, false},
                            {"s0", 1, false},
                            {"s1", 1, false},
                            {"s2", 1, false},
                            {"s3", 1, false},
                            {"x0", 2, false},
                            {"x1", 2, false},
                            {"x2", 2, false},
                            {"x3", 2, false}};
  for (const auto op : {Ntype_op::Or, Ntype_op::And, Ntype_op::Xor}) {
    SCOPED_TRACE(static_cast<int>(op));
    {
      // Mux(s_k, acc, acc OP x_k): guard_terms rewrites, then the chain merges.
      Design d(ins, 2);
      auto   acc = d.in("a");
      for (int k = 0; k < 4; ++k) {
        const auto term = d.cell(op, {acc, d.in("x" + std::to_string(k))}, 2).create_driver_pin(0);
        acc             = d.mux(d.in("s" + std::to_string(k)), acc, term, 2).create_driver_pin(0);
      }
      d.output(acc.get_master_node());
      const auto before = d.truth(2);
      const auto stats  = guarded_run(d);
      EXPECT_EQ(stats.guarded, 4U);
      EXPECT_EQ(stats.absorbed, 3U);
      EXPECT_EQ(d.count(op), 1U);
      EXPECT_EQ(d.arity(d.root()), 5U);
      EXPECT_EQ(d.truth(2), before);
    }
    // acc OP Mux(s_k, id, x_k), cprop's spelling; the And identity as -1 and
    // as the all-ones value at the guard's width.
    for (const int64_t and_id : {int64_t{-1}, int64_t{3}}) {
      Design     d(ins, 2);
      const auto id  = gu::create_const(*d.graph, *Dlop::create_integer(op == Ntype_op::And ? and_id : 0));
      auto       acc = d.in("a");
      for (int k = 0; k < 4; ++k) {
        const auto guard = d.mux(d.in("s" + std::to_string(k)), id, d.in("x" + std::to_string(k)), 2).create_driver_pin(0);
        acc              = d.cell(op, {acc, guard}, 2).create_driver_pin(0);
      }
      d.output(acc.get_master_node());
      const auto before = d.truth(2);
      const auto stats  = guarded_run(d);
      EXPECT_EQ(stats.absorbed, 3U);
      EXPECT_EQ(d.count(op), 1U);
      EXPECT_EQ(d.arity(d.root()), 5U);
      EXPECT_EQ(d.truth(2), before);
    }
    {
      // A plain `acc OP= x_k` chain: kept (Reassoc_trees::all merges it).
      Design d(ins, 2);
      auto   acc = d.in("a");
      for (int k = 0; k < 4; ++k) {
        acc = d.cell(op, {acc, d.in("x" + std::to_string(k))}, 2).create_driver_pin(0);
      }
      d.output(acc.get_master_node());
      const auto before = d.truth(2);
      EXPECT_EQ(guarded_run(d).absorbed, 0U);
      EXPECT_EQ(d.count(op), 4U);
      EXPECT_EQ(d.truth(2), before);
    }
    {
      // Three guarded links and one plain one: the whole chain is kept.
      Design     d(ins, 2);
      const auto id  = gu::create_const(*d.graph, *Dlop::create_integer(op == Ntype_op::And ? -1 : 0));
      auto       acc = d.in("a");
      for (int k = 0; k < 4; ++k) {
        const auto x = d.in("x" + std::to_string(k));
        const auto t = k == 2 ? x : d.mux(d.in("s" + std::to_string(k)), id, x, 2).create_driver_pin(0);
        acc          = d.cell(op, {acc, t}, 2).create_driver_pin(0);
      }
      d.output(acc.get_master_node());
      const auto before = d.truth(2);
      EXPECT_EQ(guarded_run(d).absorbed, 0U);
      EXPECT_EQ(d.count(op), 4U);
      EXPECT_EQ(d.truth(2), before);
    }
  }
  {
    // A Mux whose constant arm is not the operator's identity is no guard.
    Design     d(ins, 2);
    const auto one = gu::create_const(*d.graph, *Dlop::create_integer(1));
    auto       acc = d.in("a");
    for (int k = 0; k < 4; ++k) {
      const auto t = d.mux(d.in("s" + std::to_string(k)), one, d.in("x" + std::to_string(k)), 2).create_driver_pin(0);
      acc          = d.cell(Ntype_op::Or, {acc, t}, 2).create_driver_pin(0);
    }
    d.output(acc.get_master_node());
    EXPECT_EQ(guarded_run(d).absorbed, 0U);
    EXPECT_EQ(d.count(Ntype_op::Or), 4U);
  }
}

// prepare_design scopes the merge to the cells a loop expansion minted: a
// source tree outside the scope keeps its shape, and a link merges only into
// an in-scope consumer.
TEST(Reassoc, ScopeLimitsWhatMerges) {
  Design d({{"a", 1, false}, {"b", 1, false}, {"c", 1, false}, {"e", 1, false}, {"f", 1, false}}, 1);
  auto   src0 = d.cell(Ntype_op::Or, {d.in("a"), d.in("b")}, 1);  // source
  auto   src1 = d.cell(Ntype_op::Or, {src0.create_driver_pin(0), d.in("c")}, 1);  // source
  auto   new0 = d.cell(Ntype_op::Or, {src1.create_driver_pin(0), d.in("e")}, 1);  // minted
  auto   new1 = d.cell(Ntype_op::Or, {new0.create_driver_pin(0), d.in("f")}, 1);  // minted
  d.output(new1);
  const auto before = d.truth(1);
  const auto minted = [&](const hhds::Node_class& n) { return n == new0 || n == new1; };
  Reassoc_stats stats;
  EXPECT_TRUE(reassociate_chains(d.graph.get(), stats, nullptr, minted));
  EXPECT_EQ(stats.absorbed, 1U);  // new0 into new1; src1 -> new0 crosses the scope
  EXPECT_EQ(d.count(Ntype_op::Or), 3U);
  EXPECT_EQ(d.arity(new1), 3U);
  EXPECT_EQ(d.arity(src1), 2U);
  EXPECT_EQ(d.truth(1), before);
}

}  // namespace
}  // namespace livehd::synth

//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cprop.hpp"

#include <functional>
#include <tuple>
#include <unordered_map>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace {

// A folded producer and an existing literal may intern to the same pin.
// Their arithmetic multiplicity must survive, including a second collision
// when 2 + 2 becomes an already-connected 4 (or 2 * 2 becomes 4).
TEST(CpropConstants, FoldedConstantsPreserveOperandMultiplicity) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_CpropConstants_multiplicity");
  int   index  = 0;
  for (const auto [op, pid, expected] : {
           std::tuple{ Ntype_op::Xor, 0,  0},
           std::tuple{ Ntype_op::Sum, 0,  8},
           std::tuple{ Ntype_op::Sum, 1, -8},
           std::tuple{Ntype_op::Mult, 0, 16},
           std::tuple{  Ntype_op::LT, 0,  0},
           std::tuple{  Ntype_op::GT, 0,  1}
  }) {
    auto io = lib.create_io("multiplicity_" + std::to_string(index++));
    io->add_output("out", 1);
    auto g        = io->create_graph();
    auto constant = [&](int value) { return gu::create_const(*g, *Dlop::create_integer(value)); };
    auto producer = gu::create_typed_node(*g, Ntype_op::SHL);
    constant(1).connect_sink(producer.create_sink_pin(0));
    constant(1).connect_sink(producer.create_sink_pin(1));
    auto consumer = gu::create_typed_node(*g, op);
    auto sink     = consumer.create_sink_pin(pid);
    constant(2).connect_sink(sink);
    producer.create_driver_pin(0).connect_sink(sink);
    if (op == Ntype_op::Sum || op == Ntype_op::Mult) {
      constant(4).connect_sink(sink);
    } else if (op == Ntype_op::LT || op == Ntype_op::GT) {
      constant(3).connect_sink(consumer.create_sink_pin(1));
    }
    consumer.create_driver_pin(0).connect_sink(g->get_output_pin("out"));
    Cprop{}.do_trans(g, false);
    auto edges = g->get_output_pin("out").inp_edges();
    ASSERT_EQ(edges.size(), 1);
    ASSERT_TRUE(edges.begin()->driver.is_const()) << index;
    EXPECT_EQ(gu::const_of(edges.begin()->driver).to_just_i64(), expected) << index;
  }
}

// The first latch sweep cannot know that `x | -1` is an always-open enable.
// The scalar sweep exposes that constant, so the SECOND latch sweep removes
// the latch. Its reserved clock-shaping input then becomes dead strictly after
// the earlier pack DCE point and must be collected by Cprop's final cleanup.
TEST(CpropCleanup, RunsAfterFinalCanonicalization) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cprop_test");
  auto  gio = lib.create_io("cprop_cleanup_after_latch");
  gio->add_input("d", 1);
  gio->set_bits("d", 1);
  gio->add_input("x", 2);
  gio->set_bits("x", 1);
  gio->add_input("aux", 3);
  gio->set_bits("aux", 1);
  gio->add_output("q", 4);
  gio->set_bits("q", 1);
  auto g = gio->create_graph();

  auto enable = livehd::graph_util::create_typed_node(*g, Ntype_op::Or, 1);
  g->get_input_pin("x").connect_sink(livehd::graph_util::setup_sink_by_name(enable, "as"));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(-1))
      .connect_sink(livehd::graph_util::setup_sink_by_name(enable, "as"));

  auto clock_shape = livehd::graph_util::create_typed_node(*g, Ntype_op::Not, 1);
  g->get_input_pin("aux").connect_sink(livehd::graph_util::setup_sink_by_name(clock_shape, "a"));

  auto latch = livehd::graph_util::create_typed_node(*g, Ntype_op::Latch, 1);
  g->get_input_pin("d").connect_sink(livehd::graph_util::setup_sink_by_name(latch, "din"));
  enable.create_driver_pin(0).connect_sink(livehd::graph_util::setup_sink_by_name(latch, "enable"));
  clock_shape.create_driver_pin(0).connect_sink(livehd::graph_util::setup_sink_by_name(latch, "clock_pin"));
  latch.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

  Cprop cp;
  cp.do_trans(g, /*check_input_sized=*/false);

  EXPECT_TRUE(latch.is_invalid()) << "the now-always-open latch should become a wire";
  EXPECT_TRUE(clock_shape.is_invalid()) << "final cleanup must remove the control cone orphaned by that rewrite";
}

// Conditional lane writes mint Get_mask reads while factoring their word muxes.
// Once those reads resolve, the intermediate writers become private and must
// collapse in the SAME invocation, without another compile to discover them.
TEST(CpropCleanup, ConditionalPackReachesFixedPoint) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_pack_test");
  auto  io     = lib.create_io("conditional_pack");
  io->add_input("d", 1);
  io->set_bits("d", 4);
  for (int i = 0; i < 9; ++i) {
    auto name = "s" + std::to_string(i);
    io->add_input(name, i + 2);
    io->set_bits(name, 1);
  }
  io->add_output("q", 11);
  io->set_bits("q", 12);
  auto g     = io->create_graph();
  auto value = gu::create_const(*g, *Dlop::create_integer(0));
  for (int i = 0; i < 9; ++i) {
    auto write = gu::create_typed_node(*g, Ntype_op::Set_mask, 12);
    gu::set_ubits(write.create_driver_pin(0), 12);
    gu::setup_sink_by_name(write, "a").connect_driver(value);
    gu::setup_sink_by_name(write, "mask").connect_driver(gu::create_const(*g, *Dlop::create_integer(15LL << (4 * (i / 3)))));
    gu::setup_sink_by_name(write, "value").connect_driver(g->get_input_pin("d"));
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 12);
    gu::set_ubits(mux.create_driver_pin(0), 12);
    mux.create_sink_pin(0).connect_driver(g->get_input_pin("s" + std::to_string(i)));
    mux.create_sink_pin(1).connect_driver(value);
    mux.create_sink_pin(2).connect_driver(write.create_driver_pin(0));
    value = mux.create_driver_pin(0);
  }
  value.connect_sink(g->get_output_pin("q"));
  Cprop cp;
  cp.do_trans(g);
  size_t before = 0;
  for (auto n : g->body().nodes()) {
    ++before;
    EXPECT_NE(gu::type_op_of(n), Ntype_op::Set_mask);
  }
  cp.do_trans(g);
  size_t after = 0;
  for ([[maybe_unused]] auto n : g->body().nodes()) {
    ++after;
  }
  EXPECT_EQ(before, after);
}

TEST(CpropCleanup, PackedWritesKeepTruncation) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_truncated_pack_test");
  auto  io     = lib.create_io("truncated_pack");
  io->add_input("d", 1);
  io->set_bits("d", 16);
  io->add_input("s", 2);
  io->set_bits("s", 1);
  io->add_output("q", 3);
  io->set_bits("q", 8);
  auto g     = io->create_graph();
  auto value = gu::create_const(*g, *Dlop::create_integer(0));
  // The frontend's enclosing expression hint is narrower than this unlimited
  // mux's actual input. It must not justify removing a lane's truncation.
  auto mux   = gu::create_typed_node(*g, Ntype_op::Mux, 4);
  gu::set_ubits(mux.create_driver_pin(0), 4);
  mux.create_sink_pin(0).connect_driver(g->get_input_pin("s"));
  mux.create_sink_pin(1).connect_driver(value);
  mux.create_sink_pin(2).connect_driver(g->get_input_pin("d"));
  for (int lane = 0; lane < 2; ++lane) {
    auto write = gu::create_typed_node(*g, Ntype_op::Set_mask, 8);
    gu::set_ubits(write.create_driver_pin(0), 8);
    gu::setup_sink_by_name(write, "a").connect_driver(value);
    gu::setup_sink_by_name(write, "mask").connect_driver(gu::create_const(*g, *Dlop::create_integer(15 << (4 * lane))));
    gu::setup_sink_by_name(write, "value").connect_driver(mux.create_driver_pin(0));
    value = write.create_driver_pin(0);
  }
  value.connect_sink(g->get_output_pin("q"));
  Cprop cp;
  cp.do_trans(g);
  size_t concats = 0;
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Concat) {
      continue;
    }
    ++concats;
    auto lanes = gu::concat_lanes(n);
    EXPECT_EQ(gu::concat_total_width(lanes), 8);
    EXPECT_TRUE(gu::concat_lane_violation(lanes).empty());
    for (const auto& lane : lanes) {
      EXPECT_EQ(gu::type_op_of(lane.value.get_master_node()), Ntype_op::Get_mask);
      EXPECT_EQ(gu::bits_of(lane.value), 4);
    }
  }
  EXPECT_EQ(concats, 1);
}

TEST(CpropMux, NonzeroConstantConditionSelectsTrueArm) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_mux_condition");
  int   id     = 0;
  for (const auto literal : {"0", "1", "32", "-1", "0x100000000000000000000"}) {
    for (bool constant_arms : {false, true}) {
      auto io = lib.create_io("mux_condition_" + std::to_string(id++));
      io->add_input("a", 1);
      io->set_bits("a", 4);
      io->add_input("b", 2);
      io->set_bits("b", 4);
      io->add_output("q", 3);
      io->set_bits("q", 4);
      auto g         = io->create_graph();
      auto mux       = gu::create_typed_node(*g, Ntype_op::Mux, 4);
      auto false_arm = constant_arms ? gu::create_const(*g, *Dlop::create_integer(5)) : g->get_input_pin("a");
      auto true_arm  = constant_arms ? gu::create_const(*g, *Dlop::create_integer(9)) : g->get_input_pin("b");
      mux.create_sink_pin(0).connect_driver(gu::create_const(*g, *Dlop::from_pyrope(literal)));
      mux.create_sink_pin(1).connect_driver(false_arm);
      mux.create_sink_pin(2).connect_driver(true_arm);
      mux.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
      Cprop cp;
      cp.do_trans(g);
      const auto drivers = g->get_output_pin("q").get_driver_pins();
      ASSERT_EQ(drivers.size(), 1);
      EXPECT_EQ(drivers.front(), std::string_view(literal) == "0" ? false_arm : true_arm) << literal;
    }
  }
}

}  // namespace

TEST(CpropCleanup, EnabledFlopDoesNotNeedItsDataHoldMux) {
  namespace gu = livehd::graph_util;
  for (bool shared : {false, true}) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_cprop_flop_hold_test");
    auto  io  = lib.create_io(shared ? "shared_hold" : "private_hold");
    io->add_input("d", 1);
    io->set_bits("d", 8);
    io->add_input("en", 2);
    io->set_bits("en", 1);
    io->add_input("clock", 3);
    io->set_bits("clock", 1);
    io->add_output("q", 4);
    io->set_bits("q", 8);
    if (shared) {
      io->add_output("observe", 5);
      io->set_bits("observe", 8);
    }
    auto g    = io->create_graph();
    auto flop = gu::create_typed_node(*g, Ntype_op::Flop, 8);
    auto q    = flop.create_driver_pin(0);
    gu::set_ubits(q, 8);
    q.connect_sink(g->get_output_pin("q"));
    g->get_input_pin("en").connect_sink(gu::setup_sink_by_name(flop, "enable"));
    g->get_input_pin("clock").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    gu::create_const(*g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(flop, "posclk"));
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 8);
    auto d   = mux.create_driver_pin(0);
    gu::set_ubits(d, 8);
    g->get_input_pin("en").connect_sink(gu::setup_sink_by_name(mux, "s"));
    q.connect_sink(gu::setup_sink_by_name(mux, "p1"));
    g->get_input_pin("d").connect_sink(gu::setup_sink_by_name(mux, "p2"));
    d.connect_sink(gu::setup_sink_by_name(flop, "din"));
    if (shared) {
      d.connect_sink(g->get_output_pin("observe"));
    }
    Cprop{}.do_trans(g, false);
    EXPECT_FALSE(flop.is_invalid());
    EXPECT_EQ(gu::get_driver_of_sink_name(flop, "din"), g->get_input_pin("d"));
    if (shared) {
      EXPECT_FALSE(mux.is_invalid());
      EXPECT_EQ(gu::get_driver_of_sink_name(mux, "p1"), q);
    }
  }
}

TEST(CpropHotmux, ConstantControlsSelectValuesAndDefault) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_hotmux");
  for (int selected : {-1, 0, 1, 64}) {
    for (bool fallback : {false, true}) {
      auto io = lib.create_io("hotmux_" + std::to_string(selected + 1) + (fallback ? "_default" : "_zero"));
      io->add_output("q", 1);
      io->set_bits("q", 8);
      auto g   = io->create_graph();
      auto hot = gu::create_typed_node(*g, Ntype_op::Hotmux, 8);
      gu::set_ubits(hot.create_driver_pin(0), 8);
      for (int i = 0; i < 65; ++i) {
        hot.create_sink_pin(2 * i).connect_driver(gu::create_const(*g, *Dlop::create_integer(i == selected ? 1 : 0)));
        hot.create_sink_pin(2 * i + 1).connect_driver(gu::create_const(*g, *Dlop::create_integer(i + 10)));
      }
      if (fallback) {
        hot.create_sink_pin(130).connect_driver(gu::create_const(*g, *Dlop::create_integer(99)));
      }
      hot.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
      Cprop cp;
      cp.do_trans(g);
      auto edges = g->get_output_pin("q").inp_edges();
      ASSERT_EQ(edges.size(), 1);
      ASSERT_TRUE(edges[0].driver.is_const());
      EXPECT_EQ(gu::const_of(edges[0].driver).to_just_i64(), selected >= 0 ? selected + 10 : fallback ? 99 : 0);
    }
  }
}

TEST(CpropHotmux, UnusedOverlapSurvivesForFormal) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_hotmux_overlap");
  auto  io     = lib.create_io("unused_overlap");
  auto  g      = io->create_graph();
  auto  hot    = gu::create_typed_node(*g, Ntype_op::Hotmux, 8);
  for (int i = 0; i < 4; ++i) {
    hot.create_sink_pin(i).connect_driver(gu::create_const(*g, *Dlop::create_integer(1)));
  }
  Cprop cp;
  cp.do_trans(g);
  EXPECT_FALSE(hot.is_invalid());
}

// RULING (2026-09-07): identical arms collapse for a Hotmux exactly as for a
// Mux. When every arm value AND the all-controls-zero result are the same pin,
// the controls cannot change the output, so the cell (and the one-hot obligation
// riding on it) is dropped rather than kept alive as a decode cone plus its own
// ABC region. Without a default port the zero-control result is a literal 0, so
// that shape collapses only when the shared value IS zero.
TEST(CpropHotmux, IdenticalArmsCollapse) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_hotmux_identical");
  // shared: 0 = the arms share a runtime value, 1 = they share the constant 0.
  for (int shared : {0, 1}) {
    for (bool fallback : {false, true}) {
      auto io = lib.create_io(std::string{"identical_"} + (shared ? "zero" : "value") + (fallback ? "_default" : "_nodefault"));
      io->add_input("c0", 1);
      io->set_bits("c0", 1);
      io->add_input("c1", 2);
      io->set_bits("c1", 1);
      io->add_input("v", 3);
      io->set_bits("v", 8);
      io->add_output("q", 4);
      io->set_bits("q", 8);
      auto g     = io->create_graph();
      auto value = shared ? gu::create_const(*g, *Dlop::create_integer(0)) : g->get_input_pin("v");

      auto hot = gu::create_typed_node(*g, Ntype_op::Hotmux, 8);
      gu::set_ubits(hot.create_driver_pin(0), 8);
      hot.create_sink_pin(0).connect_driver(g->get_input_pin("c0"));
      hot.create_sink_pin(1).connect_driver(value);
      hot.create_sink_pin(2).connect_driver(g->get_input_pin("c1"));
      hot.create_sink_pin(3).connect_driver(value);
      if (fallback) {
        hot.create_sink_pin(4).connect_driver(value);
      }
      hot.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

      Cprop cp;
      cp.do_trans(g);

      // No default port and a non-zero shared value: the zero-control case reads
      // 0, which the arms do not, so the cell must SURVIVE.
      const bool collapses = fallback || shared;
      EXPECT_EQ(hot.is_invalid(), collapses);
      auto edges = g->get_output_pin("q").inp_edges();
      ASSERT_EQ(edges.size(), 1);
      if (collapses) {
        EXPECT_TRUE(edges[0].driver == value);
      }
    }
  }
}

namespace {
namespace gu      = livehd::graph_util;
using Test_pin    = hhds::Pin_class;
using Test_values = std::unordered_map<uint64_t, int64_t>;

// Independent integer evaluator for the small combinational transition cones
// below. Inputs and current state are supplied explicitly; memoization preserves
// DAG sharing. Unsupported operators fail rather than supplying a golden value.
int64_t mux_eval(Test_pin pin, Test_values& values) {
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
    result = at(at(0) != 0 ? 2 : 1);
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
  } else if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Ror || op == Ntype_op::EQ) {
    result           = op == Ntype_op::And ? -1 : 0;
    bool    first    = true;
    int64_t previous = 0;
    for (const auto& e : node.inp_edges()) {
      auto value = mux_eval(e.driver, values);
      if (op == Ntype_op::And) {
        result &= value;
      } else if (op == Ntype_op::Or || op == Ntype_op::Ror) {
        result |= value;
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
    if (op == Ntype_op::Ror) {
      result = result != 0;
    }
  } else {
    ADD_FAILURE() << "unbound input/state or unsupported evaluator op " << gu::debug_name(node);
  }
  values.emplace(key, result);
  return result;
}

struct Mux_graph {
  std::shared_ptr<hhds::Graph> graph;
  int                          width;
  bool                         signed_data;
  std::vector<Test_pin>        controls;
  Test_pin                     a, b, en, clock;

  Mux_graph(const std::string& name, int count, int bits = 32, bool sign = false, bool wide_control = false)
      : width(bits), signed_data(sign) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_cprop_mux_sharing");
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
    n.create_sink_pin(0).connect_driver(selector);
    n.create_sink_pin(0).connect_driver(constant(value));
    return n.create_driver_pin(0);
  }
  Test_pin output() { return graph->get_output_pin("out").get_driver_pins().front(); }
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

TEST(CpropMuxSharing, PriorityTreePreservesEveryControlCombination) {
  for (bool sign : {false, true}) {
    Mux_graph f(sign ? "priority_signed" : "priority_unsigned", 6, 32, sign, true);
    auto      root = f.b;
    for (int i = 5; i >= 0; --i) {
      root = f.mux(f.controls[i], root, i % 2 ? f.b : f.a);
    }
    root.connect_sink(f.graph->get_output_pin("out"));
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.count(Ntype_op::Mux), 0);
    EXPECT_EQ(f.count(Ntype_op::Hotmux), 1);
    EXPECT_EQ(gu::bits_of(f.output()), 32);
    EXPECT_EQ(gu::is_unsign(f.output()), !sign);
    for (uint64_t mask = 0; mask < 64; ++mask) {
      for (int64_t av : {0, 1, 127}) {
        const int64_t bv       = sign ? -117 : 219;
        int64_t       expected = bv;
        for (int i = 0; i < 6; ++i) {
          if (mask & (uint64_t{1} << i)) {
            expected = i % 2 ? bv : av;
            break;
          }
        }
        auto values = f.inputs(mask, av, bv, true, true);
        EXPECT_EQ(mux_eval(f.output(), values), expected) << mask;
      }
    }
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.count(Ntype_op::Hotmux), 1);
  }
}

TEST(CpropMuxSharing, HotmuxGroupsOnlyEstablishedExclusiveControls) {
  for (int proof : {0, 1, 2, 3}) {
    for (bool fallback : {false, true}) {
      Mux_graph f("hot_group_" + std::to_string(proof) + (fallback ? "_default" : "_zero"), 4);
      auto      n = f.node(Ntype_op::Hotmux);
      for (int i = 0; i < 4; ++i) {
        // Proof 1 is a decode; proof 2 is an existing formal certificate;
        // proof 3 deliberately repeats a decoded constant and must be refused.
        auto c = proof == 1 || proof == 3 ? f.eq(f.a, proof == 3 ? i % 2 : i) : f.controls[i];
        n.create_sink_pin(2 * i).connect_driver(c);
        n.create_sink_pin(2 * i + 1).connect_driver(i % 2 ? f.b : f.constant(17));
      }
      if (fallback) {
        n.create_sink_pin(8).connect_driver(f.constant(99));
      }
      if (proof == 2) {
        gu::set_proven(n, gu::kFormalOnehot);
      }
      n.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
      Cprop{}.do_trans(f.graph);
      const auto arms = gu::hotmux_inputs(f.output().get_master_node());
      EXPECT_EQ(arms.arms.size(), proof == 1 || proof == 2 ? 2 : 4);
      if (proof == 0 || proof == 3) {
        EXPECT_FALSE(gu::has_proven(n));
        continue;
      }
      for (uint64_t selection = 0; selection < 5; ++selection) {
        auto          values   = f.inputs(selection < 4 ? uint64_t{1} << selection : 0, selection, 211);
        const int64_t expected = selection == 4 ? (fallback ? 99 : 0) : (selection % 2 ? 211 : 17);
        EXPECT_EQ(mux_eval(f.output(), values), expected);
      }
    }
  }
}

TEST(CpropMuxSharing, DistributedHoldExtractsEnableOnlyForSingleStagePrivateFlop) {
  for (int variant : {0, 1, 2}) {
    Mux_graph f("distributed_hold_" + std::to_string(variant), 6);
    auto      flop = f.node(Ntype_op::Flop);
    auto      q    = flop.create_driver_pin(0);
    gu::setup_sink_by_name(flop, "enable").connect_driver(f.en);
    gu::setup_sink_by_name(flop, "clock_pin").connect_driver(f.clock);
    gu::setup_sink_by_name(flop, "posclk").connect_driver(f.constant(1));
    gu::setup_sink_by_name(flop, "reset_pin").connect_driver(f.controls[5]);
    gu::setup_sink_by_name(flop, "initial").connect_driver(f.constant(37));
    if (variant == 1) {
      gu::setup_sink_by_name(flop, "pipe_min").connect_driver(f.constant(2));
    }
    auto root = q;
    for (int i = 4; i >= 0; --i) {
      root = f.mux(f.controls[i], root, i % 2 ? q : f.a);
    }
    root.connect_sink(gu::setup_sink_by_name(flop, "din"));
    if (variant == 2) {
      root.connect_sink(f.graph->get_output_pin("observe"));
    }
    q.connect_sink(f.graph->get_output_pin("out"));
    Cprop{}.do_trans(f.graph);
    const auto enable = gu::get_driver_of_sink_name(flop, "enable");
    const auto data   = gu::get_driver_of_sink_name(flop, "din");
    EXPECT_EQ(enable == f.en, variant != 0);
    EXPECT_EQ(gu::get_driver_of_sink_name(flop, "reset_pin"), f.controls[5]);
    EXPECT_EQ(gu::const_of(gu::get_driver_of_sink_name(flop, "initial")).to_just_i64(), 37);
    for (uint64_t mask = 0; mask < 64; ++mask) {
      for (bool enabled : {false, true}) {
        auto values                       = f.inputs(mask, 73, 211, enabled);
        values[q.get_class_index().value] = 149;
        int64_t expected                  = 149;
        if (enabled) {
          for (int i = 0; i < 5; ++i) {
            if (mask & (uint64_t{1} << i)) {
              expected = i % 2 ? 149 : 73;
              break;
            }
          }
        }
        // The single-stage transition includes reset, whose priority must stay
        // outside the newly derived enable. For a pipeline compare the input
        // and enable separately, rather than pretending its last Q is stage 1.
        const auto actual = mux_eval(enable, values) ? mux_eval(data, values) : 149;
        EXPECT_EQ(actual, expected);
        if (variant == 0) {
          EXPECT_EQ(mask & 32 ? 37 : actual, mask & 32 ? 37 : expected);
        }
      }
    }
  }
}

TEST(CpropMuxSharing, SharedSubconeRemainsAnOpaqueTerminal) {
  Mux_graph f("shared_boundary", 6);
  auto      shared = f.mux(f.controls[0], f.a, f.b);
  shared.connect_sink(f.graph->get_output_pin("observe"));
  auto root = shared;
  for (int i = 5; i >= 1; --i) {
    root = f.mux(f.controls[i], root, i % 2 ? shared : f.a);
  }
  root.connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  EXPECT_FALSE(shared.is_invalid());
  EXPECT_EQ(gu::type_op_of(shared.get_master_node()), Ntype_op::Mux);
  for (uint64_t mask = 0; mask < 64; ++mask) {
    auto values          = f.inputs(mask, 71, 193);
    auto expected_shared = mask & 1 ? 193 : 71;
    auto expected        = expected_shared;
    for (int i = 1; i < 6; ++i) {
      if (mask & (uint64_t{1} << i)) {
        expected = i % 2 ? expected_shared : 71;
        break;
      }
    }
    EXPECT_EQ(mux_eval(f.output(), values), expected);
    EXPECT_EQ(mux_eval(shared, values), expected_shared);
  }
}

TEST(CpropMuxSharing, DeepPriorityChainHasLinearGeneratedSize) {
  // A path-literal implementation would copy roughly depth^2/2 predicates.
  // This exercises the iterative traversal and checks the generated graph,
  // rather than putting a flaky wall-clock threshold in a regression.
  constexpr int depth = 2048;
  Mux_graph     f("deep_chain", depth);
  auto          root = f.b;
  for (int i = depth - 1; i >= 0; --i) {
    root = f.mux(f.controls[i], root, i % 2 ? f.b : f.a);
  }
  root.connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  size_t nodes = 0, edges = 0;
  for (auto n : f.graph->body().nodes()) {
    ++nodes;
    edges += n.inp_edges().size();
  }
  EXPECT_EQ(f.count(Ntype_op::Mux), 0);
  EXPECT_EQ(f.count(Ntype_op::Hotmux), 1);
  EXPECT_LT(nodes, 5 * depth);
  EXPECT_LT(edges, 12 * depth);
}

TEST(CpropMuxSharing, NarrowAndColoredMuxesAreNotExpanded) {
  for (bool colored : {false, true}) {
    Mux_graph f(colored ? "colored_mux" : "narrow_mux", 6, colored ? 32 : 1);
    auto      root = f.b;
    for (int i = 5; i >= 0; --i) {
      root = f.mux(f.controls[i], root, i % 2 ? f.b : f.a);
      if (colored) {
        gu::set_color(root.get_master_node(), 7);
      }
    }
    root.connect_sink(f.graph->get_output_pin("out"));
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.count(Ntype_op::Hotmux), 0);
  }
}
}  // namespace

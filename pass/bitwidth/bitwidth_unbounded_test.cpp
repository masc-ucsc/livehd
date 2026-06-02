//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>

#include "bitwidth.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace {

[[nodiscard]] bool has_unbounded_warning(const livehd::diag::Sink& sink) {
  for (const auto& d : sink.records()) {
    if (d.code == "bitwidth-unbounded") {
      return true;
    }
  }
  return false;
}

// A Sum fed by two graph inputs that carry NO declared width: bitwidth cannot
// derive a range, so the Sum driver pin stays unbounded (bits == 0) and
// report_unbounded() must surface a `bitwidth-unbounded` warning.
TEST(BitwidthUnbounded, WarnsOnUnboundedDriverPin) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_unbounded_test");
  auto  gio = lib.create_io("bw_unbounded");
  gio->add_input("a", 1);  // no set_bits -> declared width stays 0
  gio->add_input("b", 2);
  gio->add_output("o", 3);
  auto g = gio->create_graph();

  auto sum = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);  // no bits
  g->get_input_pin("a").connect_sink(sum.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sum.create_sink_pin(1));
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_jsonl_path("off");
  sink.set_human_stderr(false);

  Bitwidth bw(/*hier=*/false, /*max_iterations=*/3);
  bw.do_trans(g);

  EXPECT_TRUE(has_unbounded_warning(sink)) << "expected a bitwidth-unbounded warning for the unbounded Sum pin";
  sink.clear();
}

// A fully-typed design must NOT produce a bitwidth-unbounded warning.
TEST(BitwidthUnbounded, NoWarnWhenAllBounded) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_unbounded_test");
  auto  gio = lib.create_io("bw_bounded");
  gio->add_input("a", 1);
  gio->set_bits("a", 8);
  gio->add_input("b", 2);
  gio->set_bits("b", 8);
  gio->add_output("o", 3);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  auto sum = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum, 8);
  g->get_input_pin("a").connect_sink(sum.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sum.create_sink_pin(1));
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_jsonl_path("off");
  sink.set_human_stderr(false);

  Bitwidth bw(/*hier=*/false, /*max_iterations=*/3);
  bw.do_trans(g);

  EXPECT_FALSE(has_unbounded_warning(sink)) << "a fully-typed design must not warn about unbounded pins";
  sink.clear();
}

}  // namespace

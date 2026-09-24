// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_stages.hpp"

#include "gtest/gtest.h"
#include "node_util.hpp"

namespace gu = livehd::graph_util;
using livehd::satopt::parse_stages;
using livehd::satopt::Profile;
using livehd::satopt::Stage;
using livehd::satopt::Stage_set;
using livehd::satopt::Stage_state;

TEST(SatoptStages, ParseNamesNoneDefaultAndRejectsTypos) {
  EXPECT_EQ(parse_stages("none", Profile::shared)->text(), "none");
  EXPECT_TRUE(parse_stages("none", Profile::shared)->empty());
  EXPECT_EQ(*parse_stages("default", Profile::shared), livehd::satopt::default_stages(Profile::shared));
  EXPECT_EQ(*parse_stages("", Profile::synthesis), livehd::satopt::default_stages(Profile::synthesis));
  // Listing order is not a recipe: execution order is fixed.
  const auto set = parse_stages("hotmux, constants", Profile::shared);
  ASSERT_TRUE(set.has_value());
  EXPECT_EQ(set->text(), "constants,hotmux");
  std::string error;
  EXPECT_FALSE(parse_stages("constant", Profile::shared, &error).has_value());
  EXPECT_NE(error.find("unknown stage 'constant'"), std::string::npos) << error;
  EXPECT_FALSE(parse_stages("equiv,equiv", Profile::shared, &error).has_value());
  EXPECT_NE(error.find("twice"), std::string::npos) << error;
}

TEST(SatoptStages, ExperimentalStagesAreNeverDefault) {
  for (const auto p : {Profile::shared, Profile::synthesis}) {
    const auto d = livehd::satopt::default_stages(p);
    EXPECT_FALSE(d.has(Stage::resub));
    EXPECT_FALSE(d.has(Stage::odc));
  }
}

namespace {
// o = mux(x == x + 1, x, y): a never-true select.
std::shared_ptr<hhds::Graph> never_select(hhds::GraphLibrary& lib, std::string_view name) {
  auto io = lib.create_io(name);
  io->add_input("x", 1);
  io->set_bits("x", 8);
  io->set_unsign("x", true);
  io->add_input("y", 2);
  io->set_bits("y", 8);
  io->set_unsign("y", true);
  io->add_output("o", 3);
  io->set_bits("o", 8);
  auto g = io->create_graph();
  auto x = g->get_input_pin("x");
  auto y = g->get_input_pin("y");
  gu::set_ubits(x, 8);
  gu::set_ubits(y, 8);
  auto sum = gu::create_typed_node(*g, Ntype_op::Sum);
  x.connect_sink(gu::setup_sink_pid(sum, 0));
  gu::create_const(*g, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_pid(sum, 0));
  gu::set_ubits(sum.create_driver_pin(0), 9);
  auto eq = gu::create_typed_node(*g, Ntype_op::EQ);
  x.connect_sink(gu::setup_sink_pid(eq, 0));
  sum.create_driver_pin(0).connect_sink(gu::setup_sink_pid(eq, 0));
  gu::set_ubits(eq.create_driver_pin(0), 1);
  auto m = gu::create_typed_node(*g, Ntype_op::Mux);
  eq.create_driver_pin(0).connect_sink(m.create_sink_pin(0));
  x.connect_sink(m.create_sink_pin(1));
  y.connect_sink(m.create_sink_pin(2));
  gu::set_ubits(m.create_driver_pin(0), 8);
  m.create_driver_pin(0).connect_sink(g->get_output_pin("o"));
  return g;
}
}  // namespace

// A disabled stage makes no rewrite; stages with nothing to inspect say so.
TEST(SatoptStages, DisabledStagesDoNothingAndInapplicableOnesSayWhy) {
  hhds::GraphLibrary lib;
  auto               g = never_select(lib, "stages_none");
  const auto         before = g->body_epoch();
  livehd::satopt::Options opts;
  opts.stages = {};
  auto report = livehd::satopt::run({g}, opts);
  EXPECT_EQ(g->body_epoch(), before);
  EXPECT_EQ(report.changed_graphs, 0u);
  for (auto s : livehd::satopt::kStageOrder) {
    EXPECT_EQ(report.at(s).state, Stage_state::disabled) << livehd::satopt::stage_name(s);
  }
  opts.stages = {Stage::constants, Stage::memory};
  report      = livehd::satopt::run({g}, opts);
  EXPECT_EQ(report.at(Stage::constants).state, Stage_state::completed);
  EXPECT_EQ(report.at(Stage::constants).proven, 1u);
  EXPECT_EQ(report.at(Stage::memory).state, Stage_state::inapplicable);  // no memory
  EXPECT_EQ(report.at(Stage::hotmux).state, Stage_state::disabled);
  EXPECT_EQ(report.changed_graphs, 1u);
  EXPECT_NE(report.json().find(R"("constants":{"state":"completed")"), std::string::npos) << report.json();
}

TEST(SatoptStages, BudgetKnobsParseAndRejectBadValues) {
  const auto b = livehd::satopt::parse_budget("work=5,queries=7,budget_k=9,cone_max=11,samples=13,time_ms=15");
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->work, 5u);
  EXPECT_EQ(b->queries, 7u);
  EXPECT_EQ(b->budget_k, 9);
  EXPECT_EQ(b->cone_max, 11);
  EXPECT_EQ(b->samples, 13u);
  EXPECT_EQ(b->time_ms, 15u);
  EXPECT_EQ(livehd::satopt::parse_budget("")->work, livehd::satopt::Budget{}.work);
  std::string error;
  EXPECT_FALSE(livehd::satopt::parse_budget("work=x", &error).has_value());
  EXPECT_NE(error.find("pass.satopt.work"), std::string::npos) << error;
  EXPECT_FALSE(livehd::satopt::parse_budget("samples=0", &error).has_value());
  EXPECT_FALSE(livehd::satopt::parse_budget("budget_k=-1", &error).has_value());
  EXPECT_FALSE(livehd::satopt::parse_budget("bogus=1", &error).has_value());
  EXPECT_NE(error.find("unknown budget knob 'bogus'"), std::string::npos) << error;
  EXPECT_FALSE(livehd::satopt::parse_budget("work", &error).has_value());
  // The proof key names what a verdict depends on, not the totals.
  livehd::satopt::Budget a, c;
  c.work = 1;
  EXPECT_EQ(a.proof_key(), c.proof_key());
  c.budget_k = 1;
  EXPECT_NE(a.proof_key(), c.proof_key());
}

// A stage may spend half of what is left while another waits; the last one
// gets the rest. An exhausted stage refuses every later charge, and the next
// stage starts over with its own share.
TEST(SatoptStages, MeterSharesTheRunBudgetBetweenStages) {
  livehd::satopt::Budget budget;
  budget.work    = 100;
  budget.queries = 4;
  livehd::satopt::Meter m(budget);
  m.begin_stage(1);
  EXPECT_TRUE(m.work(50));
  EXPECT_TRUE(m.query());
  EXPECT_TRUE(m.query());
  EXPECT_FALSE(m.query());  // a third query is past half of 4
  EXPECT_TRUE(m.exhausted());
  EXPECT_FALSE(m.work(0));
  EXPECT_EQ(m.stage_work(), 50u);
  EXPECT_EQ(m.stage_queries(), 2u);  // refused queries consume no budget
  m.begin_stage(0);
  EXPECT_FALSE(m.exhausted());
  EXPECT_TRUE(m.work(50));  // all that is left
  EXPECT_TRUE(m.query());
  EXPECT_TRUE(m.query());  // both remaining queries belong to this stage
  EXPECT_FALSE(m.work(1));
  EXPECT_TRUE(m.exhausted());
  // An unlimited meter never runs out.
  livehd::satopt::Meter unlimited;
  unlimited.begin_stage(3);
  EXPECT_TRUE(unlimited.work(uint64_t{1} << 40));
  EXPECT_TRUE(unlimited.query());
}

TEST(SatoptStages, CacheReplayCannotOverflowItsRemainingBudget) {
  livehd::satopt::Meter m;
  m.begin_stage(0);
  EXPECT_TRUE(m.work(1));
  EXPECT_TRUE(m.query());
  EXPECT_FALSE(m.replay(UINT64_MAX, 0));
  EXPECT_FALSE(m.replay(0, UINT64_MAX));
  EXPECT_EQ(m.work_done(), 1u);
  EXPECT_EQ(m.queries_done(), 1u);
  EXPECT_FALSE(m.exhausted());  // an unaffordable cache row may be searched
}

TEST(SatoptStages, MergedReportsKeepTheMostTellingState) {
  livehd::satopt::Report a, b;
  a.at(Stage::constants).state   = Stage_state::completed;
  b.at(Stage::constants).state   = Stage_state::exhausted;
  a.at(Stage::hotmux).state      = Stage_state::inapplicable;
  b.at(Stage::hotmux).state      = Stage_state::completed;
  a.at(Stage::hotmux).proven     = 2;
  b.at(Stage::hotmux).proven     = 3;
  b.at(Stage::memory).state      = Stage_state::disabled;
  a.graphs                       = 1;
  b.graphs                       = 2;
  a.merge(b);
  EXPECT_EQ(a.at(Stage::constants).state, Stage_state::exhausted);
  EXPECT_EQ(a.at(Stage::hotmux).state, Stage_state::completed);
  EXPECT_EQ(a.at(Stage::hotmux).proven, 5u);
  EXPECT_EQ(a.at(Stage::memory).state, Stage_state::disabled);
  EXPECT_EQ(a.graphs, 3u);
}

// Out of budget, the stage says so and rewrites nothing it did not prove.
TEST(SatoptStages, ExhaustedStageReportsItselfAndRewritesNothingUnproven) {
  hhds::GraphLibrary      lib;
  auto                    g = never_select(lib, "stages_starved");
  const auto              before = g->body_epoch();
  livehd::satopt::Options opts;
  opts.stages         = {Stage::constants};
  opts.budget.queries = 0;
  auto report         = livehd::satopt::run({g}, opts);
  EXPECT_EQ(report.at(Stage::constants).state, Stage_state::exhausted);
  EXPECT_EQ(report.at(Stage::constants).proven, 0u);
  // The select, and the value sweep's `x == x + 1` output, go unasked.
  EXPECT_EQ(report.at(Stage::constants).budget_skips, 2u);
  EXPECT_EQ(g->body_epoch(), before);
  EXPECT_EQ(report.changed_graphs, 0u);
  opts.budget = {};
  report      = livehd::satopt::run({g}, opts);
  EXPECT_EQ(report.at(Stage::constants).state, Stage_state::completed);
  EXPECT_EQ(report.at(Stage::constants).proven, 1u);
  EXPECT_GT(report.at(Stage::constants).work, 0u);
}

TEST(SatoptStages, ReportJsonRoundTrips) {
  livehd::satopt::Report r;
  r.graphs                          = 3;
  r.changed_graphs                  = 1;
  r.at(Stage::constants).state      = Stage_state::completed;
  r.at(Stage::constants).proven     = 7;
  r.at(Stage::hotmux).state         = Stage_state::exhausted;
  r.at(Stage::hotmux).budget_skips  = 2;
  r.at(Stage::memory).state         = Stage_state::inapplicable;
  const auto back                   = livehd::satopt::Report::parse(r.json());
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->json(), r.json());
  EXPECT_FALSE(livehd::satopt::Report::parse("{}").has_value());
  EXPECT_FALSE(livehd::satopt::Report::parse("not json").has_value());
}

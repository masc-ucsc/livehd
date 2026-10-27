// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "custom_sizing.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "growth_budget.hpp"
#include "gtest/gtest.h"
#include "inou/lefdef/lef_writer.hpp"
#include "node_util.hpp"
#include "place_test_fixture.hpp"
#include "rapidjson/document.h"
#include "resize_engine.hpp"

namespace livehd::placement {
namespace {
namespace gu = graph_util;

const std::string kFamily = "__lhd_domino_sz_t0";
std::string       variant(unsigned rank) { return std::format("__lhd_domino_sz_v{}_t0", rank); }

// A chain of sky130 cells c0..c11 in which c1, c5 and c9 are instances of a
// two-row custom family (a dfxtp_1 abstract twice as tall, D renamed R0) whose
// variants are `sites` wide; each such Q also drives f<i> (an inverter) to
// the port z<i>. `start` picks an instance's initial rank (0 otherwise).
struct Domino_chain {
  std::vector<std::string>              lefs{"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  hhds::GraphLibrary                    source, flat;
  std::shared_ptr<hhds::Graph>          graph;
  Options                               options = plain_options();
  std::shared_ptr<const Custom_catalog> catalog;
  physical::Technology                  tech;
  std::vector<unsigned>                 sites;
  std::string                           dir;
  explicit Domino_chain(const std::string& name, const std::map<unsigned, unsigned>& start = {},
                        std::vector<unsigned> widths = {16, 19, 22, 25})
      : sites(std::move(widths)), dir(std::string(std::getenv("TEST_TMPDIR")) + "/" + name) {
    std::filesystem::create_directories(dir);
    write_variants();

    auto io = source.create_io("sized");
    io->add_input("a", 1);
    io->add_input("clk", 2);
    io->add_output("y", 3);
    for (const auto* port : {"a", "clk", "y"}) {
      io->set_bits(port, 1);
    }
    for (unsigned i = 1; i < 12; i += 4) {
      io->add_output(std::format("z{}", i), 4 + i);
      io->set_bits(std::format("z{}", i), 1);
    }
    graph    = io->create_graph();
    auto inv = source.create_io("sky130_fd_sc_hd__inv_1");
    inv->add_input("A", 1);
    inv->add_output("Y", 2);
    std::map<unsigned, std::shared_ptr<hhds::GraphIO>> declared;
    const auto                                         declare = [&](unsigned rank) {
      auto& cell = declared[rank];
      if (!cell) {  // the generator's pin rule: rails, CLK, then Q (a loop break)
        cell = source.create_io(variant(rank));
        cell->add_input("R0", 1);
        cell->add_input("CLK", 2);
        cell->add_output("Q", 3, true);
      }
      return cell;
    };
    auto signal = graph->get_input_pin("a");
    gu::set_bits(signal, 1);
    for (unsigned i = 0; i < 12; ++i) {
      auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
      node.set_name(std::format("c{}", i));
      if (i % 4 == 1) {
        node.set_subnode(declare(start.contains(i) ? start.at(i) : 0));
        node.create_sink_pin("R0").connect_driver(signal);
        node.create_sink_pin("CLK").connect_driver(graph->get_input_pin("clk"));
        signal = node.create_driver_pin("Q");
        gu::set_bits(signal, 1);
        auto fanout = gu::create_typed_node(*graph, Ntype_op::Sub);
        fanout.set_subnode(inv);
        fanout.set_name(std::format("f{}", i));
        fanout.create_sink_pin("A").connect_driver(signal);
        auto z = fanout.create_driver_pin("Y");
        gu::set_bits(z, 1);
        graph->get_output_pin(std::format("z{}", i)).connect_driver(z);
      } else {
        node.set_subnode(inv);
        node.create_sink_pin("A").connect_driver(signal);
        signal = node.create_driver_pin("Y");
        gu::set_bits(signal, 1);
      }
    }
    graph->get_output_pin("y").connect_driver(signal);
    options.lefs                                        = {lefs[0], lefs[1], dir + "/domino.lef"};
    options.liberty                                     = "inou/lefdef/tests/data/sky130hd.lib";
    options.output_dir                                  = dir + "/out";
    options.iterations                                  = 0;
    options.bins                                        = 4;
    options.density                                     = .5;
    options.utilization                                 = .15;
    options.optimization_pending                        = false;
    options.terminal_legalize_options.critical_fraction = 0;
    options.row_access                                  = physical::Row_access_convention{};
    for (unsigned rank = 0; rank < sites.size(); ++rank) {
      options.multirow_templates[variant(rank)] = {
          {physical::Row_access::horizontal, physical::Row_access::vertical}
      };
    }
    options.custom_cells = catalog;
    tech                 = physical::read_lef(options.lefs);
  }
  // Writes the family's LEF and catalog for `sites` (rewritten by widen()).
  void write_variants() {
    const auto base_tech  = physical::read_lef(lefs);
    auto       base       = base_tech.macros.at("sky130_fd_sc_hd__dfxtp_1");
    base.height          *= 2;
    base.pins["R0"]       = base.pins.at("D");
    base.pins.erase("D");
    std::map<std::string, physical::Macro> macros;
    std::vector<Custom_variant>            variants;
    for (unsigned rank = 0; rank < sites.size(); ++rank) {
      auto macro            = base;
      macro.width           = sites[rank] * base_tech.sites.at("unithd").width;
      macros[variant(rank)] = macro;
      Custom_variant v;
      v.name      = variant(rank);
      v.family    = kFamily;
      v.rank      = rank;
      v.rails     = {"R0"};
      v.area_um2  = macro.width * macro.height;
      v.width_dbu = std::llround(macro.width * base_tech.dbu);
      v.clock_ff  = 2 + rank;
      variants.push_back(v);
    }
    std::ofstream(dir + "/domino.lef") << physical::write_lef_macros(macros, base_tech.dbu);
    catalog              = custom_catalog(variants);
    options.custom_cells = catalog;
    // Options caches the LEF/Liberty by path (immutable within a run); this
    // fixture rewrites domino.lef in place, so drop what a placement cached.
    options.technology_cache.reset();
    options.resize_library_cache.reset();
  }
  // The top variant `over` sites wider than a row of `fp`. At -1 it fits the
  // core, but no row pair holds it with a padding site on both sides.
  void widen_top(const physical::Floorplan& fp, int over = -1) {
    sites.back() = static_cast<unsigned>(fp.rows.front().count + over);
    write_variants();
    tech = physical::read_lef(options.lefs);
  }
};

// The variant table: each custom instance's output slack by rank (+50 when
// not listed), plus `buffer_gain` once its output is buffered; `side` gives,
// by an instance's rank, the slack of two other endpoints it loads. Slack is
// in period terms, as the stage timer reports it. The period is 1000 - WNS,
// or by rank an `internal` period outside WNS when larger; the area is the
// LEF footprint of every placed cell. Values are ps, reported in `unit` ps.
struct Table_timing {
  const Custom_catalog*                      catalog = nullptr;
  int64_t                                    dbu     = 1000;
  std::map<std::string, std::vector<double>> slack;
  double                                     buffer_gain = 20;
  std::map<std::string, std::vector<double>> side, internal;
  double                                     unit = 1;
  Timing_feedback                            operator()(const Result& state) const {
    Timing_feedback f;
    f.time_unit_ps = unit;
    double     wns = 50, tns = 0, floor = 0;
    const auto endpoint = [&](const std::string& pin, double q) {
      f.pins[pin].slack  = q / unit;
      wns                = std::min(wns, q);
      tns               += std::min(0., q);
    };
    for (const auto& c : state.floorplan.components) {
      f.area        += double(c.place.width) * double(c.place.height) / double(dbu * dbu);
      const auto* v  = catalog->find(c.macro);
      if (!v) {
        continue;
      }
      const auto it = slack.find(c.name);
      double     q  = it == slack.end() ? 50 : it->second.at(v->rank);
      if (std::any_of(state.floorplan.components.begin(), state.floorplan.components.end(), [&](const auto& other) {
            return other.name.starts_with(c.name + "_obuf");
          })) {
        q += buffer_gain;
      }
      endpoint(c.name + ":Q", q);
      if (const auto loads = side.find(c.name); loads != side.end()) {
        endpoint(c.name + "_side0:D", loads->second.at(v->rank));
        endpoint(c.name + "_side1:D", loads->second.at(v->rank));
      }
      if (const auto period = internal.find(c.name); period != internal.end()) {
        floor = std::max(floor, period->second.at(v->rank));
      }
    }
    f.wns   = wns / unit;
    f.tns   = tns / unit;
    f.delay = std::max(1000 - wns, floor) / unit;
    return f;
  }
};

const physical::Component& at(const physical::Floorplan& fp, const std::string& name) {
  const auto it = std::find_if(fp.components.begin(), fp.components.end(), [&](const auto& c) { return c.name == name; });
  if (it == fp.components.end()) {
    throw std::out_of_range("no component " + name);
  }
  return *it;
}
rapidjson::Document parse(const std::string& text) {
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  EXPECT_FALSE(doc.HasParseError());
  return doc;
}
std::vector<std::string> trial_moves(const rapidjson::Document& report) {
  std::vector<std::string> out;
  for (const auto& trial : report["trials"].GetArray()) {
    out.push_back(std::string(trial["move"].GetString()) + ":" + trial["outcome"].GetString());
  }
  return out;
}

TEST(CustomSizing, UpSizesDisplaceNeighborsThenTheOutputIsBuffered) {
  Domino_chain chain("sizing_up");
  const auto   cold      = place(*chain.graph, chain.flat, chain.options);
  // The reference: the custom cells at sites 2, 26 and 50 of rows 0-1, c0
  // right past c1's padding site (an in-place growth must displace it), the
  // other cells on rows 2 and up.
  auto         legal     = cold;
  auto&        reference = legal.floorplan;
  std::sort(reference.rows.begin(), reference.rows.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
  const auto sw = std::llround(chain.tech.sites.at("unithd").width * chain.tech.dbu);
  ASSERT_GE(reference.rows.size(), 4U);
  ASSERT_GE(reference.rows.front().count, 73);
  ASSERT_EQ(chain.options.terminal_legalize_options.padding_sites, 1U);
  unsigned next = 0;
  for (auto& c : reference.components) {
    size_t  row  = 0;
    int64_t site = 0;
    if (chain.catalog->find(c.macro)) {
      site = 2 + 24 * (std::stoi(c.name.substr(1)) / 4);
    } else if (c.name == "c0") {
      site = 2 + 16 + 1;
    } else {
      row  = 2 + next / 6;
      site = 2 + 6 * (next++ % 6);
    }
    ASSERT_LT(row, reference.rows.size());
    c.place.x           = reference.rows[row].x + site * sw;
    c.place.y           = reference.rows[row].y;
    c.place.orientation = reference.rows[row].orientation;
  }
  ASSERT_NO_THROW(check_legality(chain.tech, reference));
  chain.widen_top(reference);

  Table_timing timing{chain.catalog.get(), chain.tech.dbu, {{"c1", {-30, -20, -10, -5}}}};
  const auto   sized  = size_custom_placed(legal,
                                        chain.options,
                                        timing,
                                           {
                                               .eco = {.shift_sites = 0, .max_displaced = 16}
  });
  const auto   report = parse(sized.report);
  // Up twice (each accepted), the third growth cannot be packed, so the
  // output is buffered: never before the up moves were exhausted.
  EXPECT_EQ(trial_moves(report),
            (std::vector<std::string>{"up:accepted", "up:accepted", "up:rejected_packing", "buffer:accepted"}));
  EXPECT_EQ(report["counts"]["up_accepted"].GetUint(), 2U);
  EXPECT_EQ(report["counts"]["buffer_accepted"].GetUint(), 1U);
  EXPECT_EQ(report["counts"]["rejected_packing"].GetUint(), 1U);
  // The ECO total sums the accepted trials (the buffer's ECO moves no
  // two-row cell); qor.multirow_eco is the buffer trial's alone.
  const auto& eco = report["eco"];
  EXPECT_EQ(eco["accepted_trials"].GetUint(), 3U);
  EXPECT_EQ(eco["cells"].GetUint64(), 2U);
  EXPECT_GE(eco["displaced_cells"].GetUint64(), 1U);
  EXPECT_EQ(eco["grown_sites"].GetInt64(), 12);  // 16 -> 19 -> 22 sites in both rows
  EXPECT_EQ(eco["displaced_cells"].GetUint64(),
            report["trials"][0]["eco"]["displaced_cells"].GetUint64() + report["trials"][1]["eco"]["displaced_cells"].GetUint64()
                + report["trials"][3]["eco"]["displaced_cells"].GetUint64());
  EXPECT_TRUE(report["trials"][2]["eco"].IsNull() || !report["trials"][2].HasMember("eco"));
  const auto& fp = sized.placed.floorplan;
  EXPECT_NO_THROW(check_legality(chain.tech, fp));
  EXPECT_EQ(at(fp, "c1").macro, variant(2));
  EXPECT_EQ(at(fp, "c1").place.x, at(reference, "c1").place.x);  // grown in place
  EXPECT_EQ(at(fp, "c1").place.width, 22 * sw);
  EXPECT_NE(at(fp, "c0").place, at(reference, "c0").place);  // displaced by the growth
  EXPECT_EQ(at(fp, "c1_obuf0").macro, "sky130_fd_sc_hd__buf_1");
  for (const auto& c : fp.components) {
    EXPECT_EQ(c.place.status, physical::Status::PLACED) << c.name;
  }
  EXPECT_EQ(report["variants"]["after"][variant(2).c_str()].GetUint(), 1U);
  EXPECT_EQ(report["variants"]["after"][variant(0).c_str()].GetUint(), 2U);
  EXPECT_EQ(report["clock"]["delta_ff"].GetDouble(), 2);  // c1: 2 fF -> 4 fF
  // Geometry, pins and timing agree with a fresh evaluation of the result.
  ASSERT_TRUE(sized.timing);
  EXPECT_EQ(timing(sized.placed).delay, sized.timing->delay);
  EXPECT_EQ(sized.timing->delay, 990);
  const auto qor  = parse(sized.placed.qor);
  auto       data = fp;  // qor.hpwl_um leaves out the ideal clock net
  std::erase_if(data.nets, [](const auto& net) { return net.name == "clk"; });
  ASSERT_EQ(data.nets.size() + 1, fp.nets.size());
  EXPECT_NEAR(lef_hpwl_um(chain.tech, data), qor["hpwl_um"].GetDouble(), 1e-6);
  EXPECT_TRUE(qor.HasMember("domino_sizing"));
  EXPECT_TRUE(qor.HasMember("multirow"));  // the cold anchor report, carried
  EXPECT_TRUE(qor.HasMember("multirow_eco"));
  EXPECT_EQ(qor["multirow_eco"]["cells"].GetUint64(), 0U);
  bool netlist_has_v2 = false;
  for (auto n : sized.placed.graph->body().nodes()) {
    if (n.get_name() == "c1") {
      netlist_has_v2 = n.get_subnode_io()->get_name() == variant(2);
    }
  }
  EXPECT_TRUE(netlist_has_v2);
  EXPECT_TRUE(std::filesystem::exists(chain.options.output_dir + "/domino_sizing.json"));
}

TEST(CustomSizing, DownSizesReleaseSitesAndARejectedGrowthChangesNothing) {
  Domino_chain chain("sizing_down",
                     {
                         {5, 2}
  });  // c5 starts at the 22-site variant
  const auto   cold = place(*chain.graph, chain.flat, chain.options);
  Table_timing spare{chain.catalog.get(), chain.tech.dbu, {{"c5", {40, 40, 40, 40}}}};
  const auto   down   = size_custom_placed(cold, chain.options, spare, {});
  const auto   report = parse(down.report);
  EXPECT_EQ(trial_moves(report), (std::vector<std::string>{"down:accepted", "down:accepted"}));
  const auto c5 = at(down.placed.floorplan, "c5");
  EXPECT_EQ(c5.macro, variant(0));
  EXPECT_EQ(c5.place.x, at(cold.floorplan, "c5").place.x);  // a down-size keeps the pose
  EXPECT_EQ(c5.place.y, at(cold.floorplan, "c5").place.y);
  EXPECT_NO_THROW(check_legality(chain.tech, down.placed.floorplan));
  const auto qor = parse(down.placed.qor);
  ASSERT_TRUE(qor.HasMember("multirow_eco"));
  EXPECT_EQ(qor["multirow_eco"]["released_sites"].GetInt64(), 6);  // 19 -> 16 sites in both rows: the last ECO only
  // The run's ECO total covers both accepted trials (22 -> 19 -> 16).
  EXPECT_EQ(report["eco"]["accepted_trials"].GetUint(), 2U);
  EXPECT_EQ(report["eco"]["cells"].GetUint64(), 2U);
  EXPECT_EQ(report["eco"]["in_place"].GetUint64(), 2U);
  EXPECT_EQ(report["eco"]["released_sites"].GetInt64(), 12);
  EXPECT_EQ(report["trials"][0]["eco"]["released_sites"].GetInt64(), 6);
  EXPECT_LT(down.timing->area, spare(cold).area);

  // A growth that cannot be packed anywhere is a rejected trial: the state,
  // its DEF and its netlist are untouched (buffers off, so nothing else runs).
  Domino_chain grow("sizing_reject",
                    {
                        {1, 2}
  });
  const auto   start = place(*grow.graph, grow.flat, grow.options);
  grow.widen_top(start.floorplan);
  std::ifstream     def_in(grow.options.output_dir + "/out.def");
  const std::string def{std::istreambuf_iterator<char>(def_in), {}};
  Table_timing      tight{grow.catalog.get(), grow.tech.dbu, {{"c1", {-30, -20, -10, -5}}}};
  const auto        rejected = size_custom_placed(start, grow.options, tight, {.buffers = false});
  EXPECT_EQ(trial_moves(parse(rejected.report)), (std::vector<std::string>{"up:rejected_packing"}));
  EXPECT_EQ(rejected.placed.floorplan.components, start.floorplan.components);
  EXPECT_EQ(rejected.placed.graph, start.graph);
  EXPECT_TRUE(rejected.libraries.empty());
  std::ifstream     def_out(grow.options.output_dir + "/out.def");
  const std::string after{std::istreambuf_iterator<char>(def_out), {}};
  EXPECT_EQ(after, def);
  EXPECT_EQ(rejected.timing->delay, tight(start).delay);
  // Wider than every row: rejected before any placement would refuse it.
  grow.widen_top(start.floorplan, 3);
  tight.catalog       = grow.catalog.get();  // the catalog was rebuilt
  const auto too_wide = parse(size_custom_placed(start, grow.options, tight, {.buffers = false}).report);
  ASSERT_EQ(trial_moves(too_wide), (std::vector<std::string>{"up:rejected_packing"}));
  EXPECT_NE(std::string(too_wide["trials"][0]["failure"].GetString()).find("wider than every row"), std::string::npos);
}

TEST(CustomSizing, ThePeriodToleranceIsInPicoseconds) {
  // Feedback in ns: a down-size that costs 0.5 ps of internal period is
  // rejected although it is within epsilon_ps (1e-3) in timer units.
  Domino_chain chain("sizing_units",
                     {
                         {5, 2}
  });
  const auto   cold = place(*chain.graph, chain.flat, chain.options);
  Table_timing ns{chain.catalog.get(), chain.tech.dbu, {{"c5", {40, 40, 40, 40}}}};
  ns.internal = {
      {"c5", {960, 960.5, 960, 960}}
  };
  ns.unit           = 1000;
  const auto sized  = size_custom_placed(cold, chain.options, ns, {});
  const auto report = parse(sized.report);
  EXPECT_EQ(trial_moves(report), (std::vector<std::string>{"down:rejected_timing"}));
  EXPECT_EQ(report["trials"][0]["timing"]["delay_ps"].GetDouble(), 960.5);
  EXPECT_EQ(at(sized.placed.floorplan, "c5").macro, variant(2));
}

TEST(CustomSizing, AShorterPeriodIsAcceptedAlthoughTnsWorsens) {
  // c1's Q (a half-cycle -4 ps raw: -8 in period terms) steps up to -1 (-2),
  // while two full-cycle endpoints it loads drop from 0 to -4: WNS -8 -> -4,
  // TNS -8 -> -10, period 1008 -> 1004. The next step gains no period and
  // loses no TNS, so it is rejected.
  Domino_chain chain("sizing_period");
  const auto   cold = place(*chain.graph, chain.flat, chain.options);
  Table_timing timing{chain.catalog.get(), chain.tech.dbu, {{"c1", {-8, -2, -2, -2}}}};
  timing.side = {
      {"c1", {0, -4, -4, -4}}
  };
  const auto before = timing(cold);
  ASSERT_EQ(before.delay, 1008);
  ASSERT_EQ(before.tns, -8);
  const auto sized  = size_custom_placed(cold, chain.options, timing, {.buffers = false});
  const auto report = parse(sized.report);
  EXPECT_EQ(trial_moves(report), (std::vector<std::string>{"up:accepted", "up:rejected_timing"}));
  EXPECT_EQ(sized.timing->delay, 1004);
  EXPECT_EQ(sized.timing->tns, -10);
  EXPECT_FALSE(resize_accepts(before, *sized.timing));  // the WNS/TNS gate alone would refuse it
  EXPECT_EQ(at(sized.placed.floorplan, "c1").macro, variant(1));
  EXPECT_NO_THROW(check_legality(chain.tech, sized.placed.floorplan));
}

TEST(CustomSizing, AnUnmeasuredTrialIsRejectedNotFatal) {
  // Rank 1 has no measurable period (as a variant whose register lacks a
  // setup arc): its trial is rejected with the reason, the state stays, and
  // the buffer is tried next. Only an unmeasured initial state throws.
  Domino_chain       chain("sizing_unmeasured");
  const auto         cold = place(*chain.graph, chain.flat, chain.options);
  const Table_timing table{chain.catalog.get(), chain.tech.dbu, {{"c1", {-30, -20, -10, -5}}}};
  const auto         rank = [&](const Result& state, unsigned r) {
    return std::any_of(state.floorplan.components.begin(), state.floorplan.components.end(), [&](const auto& c) {
      return c.macro == variant(r);
    });
  };
  const Evaluate evaluate = [&](const Result& state) {
    if (rank(state, 1)) {
      throw std::runtime_error("domino sizing needs a measured stage period: unmeasured setup_capture_ps");
    }
    return table(state);
  };
  const auto sized  = size_custom_placed(cold, chain.options, evaluate, {});
  const auto report = parse(sized.report);
  EXPECT_EQ(trial_moves(report), (std::vector<std::string>{"up:rejected_failed", "buffer:accepted"}));
  EXPECT_NE(std::string(report["trials"][0]["failure"].GetString()).find("unmeasured setup_capture_ps"), std::string::npos);
  EXPECT_EQ(report["counts"]["rejected_failed"].GetUint(), 1U);
  EXPECT_EQ(at(sized.placed.floorplan, "c1").macro, variant(0));
  EXPECT_NO_THROW(check_legality(chain.tech, sized.placed.floorplan));
  const Evaluate never = [](const Result&) -> Timing_feedback { throw std::runtime_error("unmeasured"); };
  EXPECT_THROW(size_custom_placed(cold, chain.options, never, {}), std::runtime_error);
}

TEST(CustomSizing, AGrowthPastTheSharedBudgetIsRejectedBeforePlacement) {
  // Domino sizing shares full placement's cell/area growth budget (3% over the
  // mapped netlist, growth_budget.hpp) with the other placement edits. c1's
  // first step (16 -> 19 sites on two rows, ~7.5 um2) is past 3% of this
  // ~165 um2 chain, and the buffer (one cell) is past 3% of its 15 cells:
  // both are rejected_growth before any trial placement; the state stays.
  Domino_chain chain("sizing_growth");
  const auto   cold = place(*chain.graph, chain.flat, chain.options);
  initialize_growth_budget(*chain.graph, chain.options);
  ASSERT_EQ(chain.options.growth_budget->baseline.cells, 15U);
  const Table_timing timing{chain.catalog.get(), chain.tech.dbu, {{"c1", {-30, -20, -10, -5}}}};
  const auto         sized  = size_custom_placed(cold, chain.options, timing, {});
  const auto         report = parse(sized.report);
  EXPECT_EQ(trial_moves(report), (std::vector<std::string>{"up:rejected_growth", "buffer:rejected_growth"}));
  EXPECT_EQ(std::string(report["trials"][0]["failure"].GetString()), "physical cell/area growth budget exceeded");
  EXPECT_EQ(report["counts"]["rejected_growth"].GetUint(), 2U);
  EXPECT_EQ(chain.options.growth_budget->rejected, 2U);
  EXPECT_EQ(sized.placed.floorplan.components, cold.floorplan.components);
  EXPECT_EQ(sized.placed.graph, cold.graph);
  // The same step fits a 10% budget.
  chain.options.growth_budget->fraction = .1;
  EXPECT_EQ(trial_moves(parse(size_custom_placed(cold, chain.options, timing, {.buffers = false}).report))[0], "up:accepted");
}

TEST(CustomSizing, UsageErrors) {
  Domino_chain chain("sizing_usage");
  const auto   cold = place(*chain.graph, chain.flat, chain.options);
  Table_timing timing{chain.catalog.get(), chain.tech.dbu, {}};
  auto         options = chain.options;
  options.custom_cells.reset();
  EXPECT_THROW(size_custom_placed(cold, options, timing, {}), std::invalid_argument);
  options                      = chain.options;
  options.optimization_pending = true;
  EXPECT_THROW(size_custom_placed(cold, options, timing, {}), std::invalid_argument);
  const Evaluate unmeasured = [](const Result&) { return Timing_feedback{}; };
  EXPECT_THROW(size_custom_placed(cold, chain.options, unmeasured, {}), std::invalid_argument);
}
}  // namespace
}  // namespace livehd::placement

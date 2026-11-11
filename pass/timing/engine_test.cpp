// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "test_library.hpp"

namespace fast = livehd::timing::fast;
namespace lt   = livehd::liberty::timing;
using fast::test::library_text;

namespace {
constexpr float inf = std::numeric_limits<float>::infinity();

// The reference model, in double: the arrival of the rise and the fall and their slews at a point.
struct S {
  double ar, af, sr, sf;
};
// An inverter (negative unate): the output rises when the input falls.
S inv(S in, double load, double k) {
  return {in.af + 10 * k + .1 * in.sf + 2 * load,
          in.ar + 15 * k + .1 * in.sr + 2 * load,
          5 * k + .1 * in.sf + .2 * load,
          6 * k + .1 * in.sr + .2 * load};
}
S      flop_q(double k) { return {30 * k, 40 * k, 50 * k, 60 * k}; }
double setup_slack(S at, double period, double k) { return std::min(period - 10 * k - at.ar, period - 12 * k - at.af); }

// The libraries of the corners (scales 1 and 2 by default), the types, a netlist under construction.
struct Bench {
  std::vector<lt::Library>       libraries;
  std::vector<double>            scales;
  fast::Cell_types               types;
  std::unique_ptr<fast::Netlist> netlist;
  explicit Bench(std::vector<double> k = {1.0, 2.0}) : scales(std::move(k)) {
    std::vector<const lt::Library*> pointers;
    std::vector<std::string>        names;
    for (const auto scale : scales) {
      libraries.push_back(lt::parse(library_text(scale)));
    }
    for (size_t i = 0; i < scales.size(); ++i) {
      pointers.push_back(&libraries[i]);
      names.push_back("c" + std::to_string(i));
    }
    types   = fast::Cell_types::build(pointers, names);
    netlist = std::make_unique<fast::Netlist>(types);
  }
  uint32_t cell(const char* type) { return netlist->add_cell(types.find(type)); }
  uint32_t pin(uint32_t cell, const char* name) const { return netlist->pin(cell, name); }
  void     link(uint32_t from, const char* out, uint32_t to, const char* in) { netlist->connect(pin(from, out), pin(to, in)); }
  uint32_t port_pin(uint32_t port_cell) const { return netlist->pin(port_cell, 0u); }
};

fast::Constraints single_clock(double period) {
  fast::Constraints c;
  c.clocks.push_back({"clk", "clk", static_cast<float>(period), 0, 0});
  return c;
}
}  // namespace

TEST(FastEngine, ChainOfInvertersMatchesTheTables) {
  Bench      b;
  const auto in = b.netlist->add_input_port("in"), clk = b.netlist->add_input_port("clk"), out = b.netlist->add_output_port("out");
  (void)clk;
  const auto i1 = b.cell("INV"), i2 = b.cell("INV");
  b.netlist->connect(b.port_pin(in), b.pin(i1, "A"));
  b.link(i1, "Y", i2, "A");
  b.netlist->connect(b.pin(i2, "Y"), b.port_pin(out));
  b.netlist->finalize();
  EXPECT_TRUE(b.netlist->loop_pins().empty());

  auto constraints = single_clock(1000);
  constraints.outputs.push_back({"out", "", 0, 10});
  fast::Engine engine(*b.netlist, constraints);
  engine.evaluate();
  ASSERT_EQ(engine.report().endpoints, 1u);

  for (size_t k = 0; k < 2; ++k) {
    const double scale = b.scales[k];
    const S      first = inv({0, 0, 0, 0}, 2, scale), second = inv(first, 10, scale);
    const auto   y1 = b.pin(i1, "Y"), y2 = b.pin(i2, "Y"), o = b.port_pin(out);
    EXPECT_NEAR(engine.arrival(y1, k, 0), first.ar, 1e-3);
    EXPECT_NEAR(engine.arrival(y1, k, 1), first.af, 1e-3);
    EXPECT_NEAR(engine.slew(y1, k, 0), first.sr, 1e-3);
    EXPECT_NEAR(engine.slew(y1, k, 1), first.sf, 1e-3);
    EXPECT_NEAR(engine.arrival(y2, k, 0), second.ar, 1e-3);
    EXPECT_NEAR(engine.arrival(y2, k, 1), second.af, 1e-3);
    EXPECT_NEAR(engine.arrival(o, k, 0), second.ar, 1e-3);  // a wire of zero delay
    EXPECT_EQ(engine.depth(y2, k, 0), 2);
    // True required time after evaluate: the whole period, nothing downstream.
    EXPECT_NEAR(engine.required(o, k, 0), 1000, 1e-3);
    // Backward through both inverters: the input transition that ends in a rise (resp. a fall) at the output
    // has exactly the slack of that arrival.
    EXPECT_NEAR(engine.required(b.pin(i1, "A"), k, 0), 1000 - second.ar, 1e-2);
    EXPECT_NEAR(engine.required(b.pin(i1, "A"), k, 1), 1000 - second.af, 1e-2);
    EXPECT_NEAR(engine.report().corners[k].wns_ps, 1000 - std::max(second.ar, second.af), 1e-2);
    // The delay kept for the backward walk.
    EXPECT_NEAR(engine.arc_delay(i2, 0, k, 1, 0), second.ar - first.af, 1e-3);  // input falls, output rises
    EXPECT_TRUE(std::isnan(engine.arc_delay(i2, 0, k, 0, 0)));                  // not an arc of a negative unate cell
  }
  // The worst setup corner rules, and the achievable period is the period less the worst slack.
  const S slow = inv(inv({0, 0, 0, 0}, 2, 2), 10, 2);
  EXPECT_NEAR(engine.report().wns_ps, 1000 - std::max(slow.ar, slow.af), 1e-2);
  EXPECT_NEAR(engine.report().clocks[0].achievable_period_ps, std::max(slow.ar, slow.af), 1e-2);
  EXPECT_NEAR(engine.slack(b.pin(i2, "Y")), 1000 - std::max(slow.ar, slow.af), 1e-2);
  EXPECT_EQ(engine.report().tns_ps, 0.f);
  EXPECT_GT(engine.bytes(), 0u);
}

TEST(FastEngine, FlopToFlopSetupHoldAndTheWorstPath) {
  Bench      b;
  const auto d_in = b.netlist->add_input_port("d_in"), clk = b.netlist->add_input_port("clk");
  const auto ff1 = b.cell("FF"), ff2 = b.cell("FF"), i1 = b.cell("INV");
  b.netlist->connect(b.port_pin(d_in), b.pin(ff1, "D"));
  b.netlist->connect(b.port_pin(clk), b.pin(ff1, "CLK"));
  b.netlist->connect(b.port_pin(clk), b.pin(ff2, "CLK"));
  b.link(ff1, "Q", i1, "A");
  b.link(i1, "Y", ff2, "D");
  b.netlist->finalize();

  const double  period = 100;
  fast::Options options;
  options.hold_corner = 0;  // the first corner is the fast one here
  fast::Engine engine(*b.netlist, single_clock(period), options);
  engine.evaluate();
  const auto d2 = b.pin(ff2, "D"), d1 = b.pin(ff1, "D");
  ASSERT_EQ(engine.report().endpoints, 2u);
  EXPECT_EQ(engine.report().unclocked_flops, 0u);

  double wns = 1e9, tns = 0;
  for (size_t k = 0; k < 2; ++k) {
    const double scale = b.scales[k];
    const S      at    = inv(flop_q(scale), 1, scale);
    EXPECT_NEAR(engine.arrival(d2, k, 0), at.ar, 1e-3) << k;
    EXPECT_NEAR(engine.arrival(d2, k, 1), at.af, 1e-3) << k;
    EXPECT_NEAR(engine.slew(d2, k, 0), at.sr, 1e-3) << k;
    EXPECT_NEAR(engine.required(d2, k, 0), period - 10 * scale, 1e-3) << k;
    EXPECT_NEAR(engine.required(d2, k, 1), period - 12 * scale, 1e-3) << k;
    // FF1.D: a primary input at time 0.
    EXPECT_NEAR(engine.required(d1, k, 0), period - 10 * scale, 1e-3) << k;
    const double slack2 = setup_slack(at, period, scale), slack1 = std::min(period - 10 * scale, period - 12 * scale);
    wns  = std::min({wns, slack2, slack1});
    tns += std::min(0.0, slack2) + std::min(0.0, slack1);
  }
  EXPECT_NEAR(engine.report().wns_ps, wns, 1e-2);
  EXPECT_NEAR(engine.report().clocks[0].wns_ps, wns, 1e-2);
  EXPECT_NEAR(engine.report().clocks[0].achievable_period_ps, period - wns, 1e-2);
  EXPECT_NEAR(engine.slack(d2), setup_slack(inv(flop_q(2), 1, 2), period, 2), 1e-2);  // the slow corner rules

  // Hold, on the fast corner: the data of FF2 arrives after the flop's clock to output and the inverter; FF1's
  // data is the primary input at time 0 against a hold time of 4 (rise) and 6 (fall).
  EXPECT_NEAR(engine.hold_slack(d1), std::min(0 - 4.0, 0 - 6.0), 1e-3);
  const S fast_at = inv(flop_q(1), 1, 1);
  EXPECT_NEAR(engine.hold_slack(d2), std::min(fast_at.ar - 4, fast_at.af - 6), 1e-3);
  EXPECT_NEAR(engine.report().clocks[0].hold_wns_ps, -6.0, 1e-3);
  EXPECT_NEAR(engine.report().clocks[0].hold_tns_ps, -6.0, 1e-3);
  EXPECT_NEAR(engine.early_arrival(d2, 0), fast_at.ar, 1e-3);

  // The worst path: clock pin, Q, inverter in, inverter out, data pin.
  const auto path = engine.worst_path(d2);
  ASSERT_EQ(path.size(), 5u);
  EXPECT_EQ(path[0].pin, b.pin(ff1, "CLK"));
  EXPECT_EQ(path[1].pin, b.pin(ff1, "Q"));
  EXPECT_EQ(path[2].pin, b.pin(i1, "A"));
  EXPECT_EQ(path[3].pin, b.pin(i1, "Y"));
  EXPECT_EQ(path[4].pin, d2);
  for (size_t i = 1; i < path.size(); ++i) {
    EXPECT_GE(path[i].arrival_ps, path[i - 1].arrival_ps);
  }
  // Without an endpoint the engine picks the worst one.
  const auto worst = engine.worst_path();
  ASSERT_FALSE(worst.empty());
  EXPECT_EQ(worst.back().pin, d2);
}

TEST(FastEngine, CriticalityIsMeasuredAgainstTheWorstPath) {
  Bench      b({1.0});
  const auto clk = b.netlist->add_input_port("clk");
  const auto src = b.cell("FF"), a1 = b.cell("INV"), dst_short = b.cell("FF"), c1 = b.cell("INV"), c2 = b.cell("INV"),
             c3 = b.cell("INV"), dst_long = b.cell("FF");
  for (const auto ff : {src, dst_short, dst_long}) {
    b.netlist->connect(b.port_pin(clk), b.pin(ff, "CLK"));
  }
  b.link(src, "Q", a1, "A");
  b.link(a1, "Y", dst_short, "D");
  b.link(src, "Q", c1, "A");
  b.link(c1, "Y", c2, "A");
  b.link(c2, "Y", c3, "A");
  b.link(c3, "Y", dst_long, "D");
  b.netlist->finalize();

  // The long path fails; the short one passes with a margin that is inside the window.
  const double load_q = 4, period = 100;
  S            q = flop_q(1);
  (void)load_q;
  const S      short_at    = inv(q, 1, 1);
  const S      long_at     = inv(inv(inv(q, 2, 1), 2, 1), 1, 1);
  const double slack_short = setup_slack(short_at, period, 1), slack_long = setup_slack(long_at, period, 1);
  ASSERT_LT(slack_long, 0);
  ASSERT_GT(slack_short, slack_long);

  fast::Options options;
  options.window_fraction = 0.5f;  // wide enough for the short path to be partly critical
  fast::Engine engine(*b.netlist, single_clock(period), options);
  engine.rank();
  EXPECT_NEAR(engine.report().wns_ps, slack_long, 1e-2);
  const double window = 0.5 * (period - slack_long);
  // The worst path is critical 1; the short path by how far its slack is from the worst one.
  EXPECT_NEAR(engine.criticality(b.pin(dst_long, "D")), 1.0, 1e-4);
  EXPECT_NEAR(engine.criticality(b.pin(c3, "A")), 1.0, 1e-4);
  EXPECT_NEAR(engine.criticality(b.pin(dst_short, "D")), std::clamp(1 - (slack_short - slack_long) / window, 0.0, 1.0), 1e-3);
  // Rebased: after rank the worst endpoint has zero slack.
  EXPECT_NEAR(engine.slack(b.pin(dst_long, "D")), 0.0, 1e-3);
  EXPECT_NEAR(engine.slack(b.pin(dst_short, "D")), slack_short - slack_long, 1e-2);
  // The flop output serves both paths: the worse one rules.
  EXPECT_NEAR(engine.criticality(b.pin(src, "Q")), 1.0, 1e-4);

  // A narrow window leaves the short path out.
  fast::Options narrow;
  narrow.window_fraction = 0.01f;
  fast::Engine second(*b.netlist, single_clock(period), narrow);
  second.rank();
  EXPECT_EQ(second.criticality(b.pin(dst_short, "D")), 0.f);
  EXPECT_EQ(second.criticality(b.pin(a1, "Y")), 0.f);
  EXPECT_NEAR(second.criticality(b.pin(dst_long, "D")), 1.0, 1e-4);

  // When the target is met nothing is rebased: the SDC period is the reference.
  fast::Engine relaxed(*b.netlist, single_clock(1000), options);
  relaxed.evaluate();
  EXPECT_GT(relaxed.report().wns_ps, 0.f);
  EXPECT_NEAR(relaxed.slack(b.pin(dst_long, "D")), setup_slack(long_at, 1000, 1), 1e-2);
  const double relaxed_window = 0.5 * relaxed.report().clocks[0].achievable_period_ps;
  EXPECT_NEAR(relaxed.criticality(b.pin(dst_long, "D")),
              std::clamp(1 - setup_slack(long_at, 1000, 1) / relaxed_window, 0.0, 1.0),
              1e-3);
  // After evaluate the required times are the true ones, not rebased.
  EXPECT_NEAR(relaxed.required(b.pin(dst_long, "D"), 0, 0), 1000 - 10, 1e-3);
}

TEST(FastEngine, FalsePathsAndWhatIsNotTimedAreReported) {
  Bench      b({1.0});
  const auto d_in = b.netlist->add_input_port("d_in"), clk = b.netlist->add_input_port("clk");
  const auto ff1 = b.cell("FF"), ff2 = b.cell("FF"), neg = b.cell("NEGFF"), i1 = b.cell("INV");
  b.netlist->connect(b.port_pin(d_in), b.pin(ff1, "D"));
  for (const auto ff : {ff1, ff2, neg}) {
    b.netlist->connect(b.port_pin(clk), b.pin(ff, "CLK"));
  }
  b.link(ff1, "Q", i1, "A");
  b.link(i1, "Y", ff2, "D");
  b.netlist->finalize();

  fast::Engine plain(*b.netlist, single_clock(100));
  plain.evaluate();
  EXPECT_EQ(plain.report().endpoints, 2u);
  EXPECT_LT(plain.slack(b.pin(i1, "Y")), 1000.f);
  bool said = false;
  for (const auto& line : plain.report().unsupported) {
    said = said || line.find("Liberty arcs are not timed") != std::string::npos;
  }
  EXPECT_TRUE(said);

  // A false path through the inverter's input: nothing arrives behind it and the endpoint has no check.
  auto cut = single_clock(100);
  cut.cut_pins.push_back(b.pin(i1, "A"));
  fast::Engine cut_engine(*b.netlist, cut);
  cut_engine.evaluate();
  EXPECT_EQ(cut_engine.arrival(b.pin(i1, "Y"), 0, 0), -inf);
  EXPECT_EQ(cut_engine.arrival(b.pin(ff2, "D"), 0, 0), -inf);
  EXPECT_EQ(cut_engine.slack(b.pin(ff2, "D")), inf);
  EXPECT_EQ(cut_engine.report().endpoints, 1u);  // FF1.D remains

  auto excluded = single_clock(100);
  excluded.excluded_endpoints.push_back(b.pin(ff2, "D"));
  excluded.unsupported.push_back("set_false_path -from A -to B between data pins");
  fast::Engine excluded_engine(*b.netlist, excluded);
  excluded_engine.evaluate();
  EXPECT_EQ(excluded_engine.report().endpoints, 1u);
  EXPECT_NE(std::find(excluded_engine.report().unsupported.begin(),
                      excluded_engine.report().unsupported.end(),
                      "set_false_path -from A -to B between data pins"),
            excluded_engine.report().unsupported.end());

  // A flip-flop whose clock does not come from a clock port is not timed, and is counted.
  Bench      c({1.0});
  const auto clk2 = c.netlist->add_input_port("clk"), stray = c.netlist->add_input_port("stray");
  (void)clk2;
  const auto f = c.cell("FF");
  c.netlist->connect(c.port_pin(stray), c.pin(f, "CLK"));
  c.netlist->finalize();
  fast::Engine unclocked(*c.netlist, single_clock(100));
  unclocked.evaluate();
  EXPECT_EQ(unclocked.report().unclocked_flops, 1u);
  EXPECT_EQ(unclocked.report().endpoints, 0u);

  EXPECT_THROW(fast::Engine(*b.netlist,
                            [] {
                              fast::Constraints bad;
                              bad.clocks.push_back({"clk", "no_such_port", 100, 0, 0});
                              return bad;
                            }()),
               std::invalid_argument);
}

TEST(FastEngine, ACombinationalLoopIsCutAndListed) {
  Bench      b({1.0});
  const auto clk = b.netlist->add_input_port("clk");
  (void)clk;
  const auto i1 = b.cell("INV"), i2 = b.cell("INV");
  b.link(i1, "Y", i2, "A");
  b.link(i2, "Y", i1, "A");
  b.netlist->finalize();
  // Two inverters in a ring are one loop: cut at one pin, and every pin is still in the order.
  EXPECT_EQ(b.netlist->loop_pins().size(), 1u);
  EXPECT_EQ(b.netlist->order().size(), b.netlist->pins());
  fast::Engine engine(*b.netlist, single_clock(100));
  engine.evaluate();
  bool said = false;
  for (const auto& line : engine.report().unsupported) {
    said = said || line.find("combinational loop") != std::string::npos;
  }
  EXPECT_TRUE(said);
}

TEST(FastEngine, ElectricalLimitsAreCounted) {
  Bench      b;
  const auto clk = b.netlist->add_input_port("clk");
  const auto ff1 = b.cell("FF"), ff2 = b.cell("FF"), i1 = b.cell("INV"), a = b.cell("INV"), c = b.cell("INV"), d = b.cell("INV");
  for (const auto ff : {ff1, ff2}) {
    b.netlist->connect(b.port_pin(clk), b.pin(ff, "CLK"));
  }
  b.link(ff1, "Q", i1, "A");
  b.link(i1, "Y", ff2, "D");
  // A fanout of three inverters (6 fF) on a driver with a 5 fF limit.
  b.link(ff1, "Q", a, "A");
  b.link(ff1, "Q", c, "A");
  b.link(ff1, "Q", d, "A");
  b.link(a, "Y", ff2, "D");  // a second driver for D: the first is kept
  b.netlist->finalize();
  EXPECT_FALSE(b.netlist->diagnostics().empty());
  // Drive 3 inverters from an inverter too.
  Bench      e;
  const auto eclk = e.netlist->add_input_port("clk");
  const auto src = e.cell("INV"), l1 = e.cell("INV"), l2 = e.cell("INV"), l3 = e.cell("INV");
  const auto in = e.netlist->add_input_port("in");
  e.netlist->connect(e.port_pin(in), e.pin(src, "A"));
  e.link(src, "Y", l1, "A");
  e.link(src, "Y", l2, "A");
  e.link(src, "Y", l3, "A");
  (void)eclk;
  e.netlist->finalize();
  fast::Engine over(*e.netlist, single_clock(100));
  over.evaluate();
  EXPECT_EQ(over.report().capacitance_violations, 1u);
  EXPECT_NEAR(over.report().electrical_excess, (6.0 - 5.0) / 5.0, 1e-4);
  EXPECT_EQ(over.report().slew_violations, 0u);

  // The data pin of FF2 has a transition limit of 12: met on the fast corner, not on the slow one.
  Bench      f;
  const auto fclk = f.netlist->add_input_port("clk");
  const auto q = f.cell("FF"), inv_cell = f.cell("INV"), r = f.cell("FF");
  for (const auto ff : {q, r}) {
    f.netlist->connect(f.port_pin(fclk), f.pin(ff, "CLK"));
  }
  f.link(q, "Q", inv_cell, "A");
  f.link(inv_cell, "Y", r, "D");
  f.netlist->finalize();
  fast::Engine slew(*f.netlist, single_clock(1000));
  slew.evaluate();
  EXPECT_EQ(slew.report().slew_violations, 1u);
  EXPECT_GT(slew.slew(f.pin(r, "D"), 1, 0), 12.f);
  EXPECT_LT(slew.slew(f.pin(r, "D"), 0, 0), 12.f);
  fast::Options fast_only;
  fast_only.setup_corners = 1;  // only the fast corner is a setup corner: no violation
  fast::Engine only(*f.netlist, single_clock(1000), fast_only);
  only.evaluate();
  EXPECT_EQ(only.report().slew_violations, 0u);
}

TEST(FastEngine, WireModelAddsLoadAndDelay) {
  Bench      b({1.0});
  const auto in = b.netlist->add_input_port("in"), clk = b.netlist->add_input_port("clk"), out = b.netlist->add_output_port("out");
  (void)clk;
  const auto i1 = b.cell("INV");
  b.netlist->connect(b.port_pin(in), b.pin(i1, "A"));
  b.netlist->connect(b.pin(i1, "Y"), b.port_pin(out));
  b.netlist->finalize();
  auto constraints = single_clock(1000);
  constraints.outputs.push_back({"out", "", 0, 4});
  fast::Engine engine(*b.netlist, constraints);
  engine.evaluate();
  const S base = inv({0, 0, 0, 0}, 4, 1);
  EXPECT_NEAR(engine.arrival(b.port_pin(out), 0, 0), base.ar, 1e-3);

  // 6 fF of wire on the net of INV.Y and 3 ps of wire delay to the output port.
  fast::Wire_model wire;
  wire.net_cap_ff.assign(b.netlist->nets(), 0.f);
  wire.net_cap_ff[b.netlist->net_of(b.pin(i1, "Y"))] = 6.f;
  wire.sink_delay_ps.assign(b.netlist->pins(), 0.f);
  wire.sink_delay_ps[b.port_pin(out)] = 3.f;
  engine.set_wire(wire);
  engine.evaluate();
  const S wired = inv({0, 0, 0, 0}, 4 + 6, 1);
  EXPECT_NEAR(engine.arrival(b.pin(i1, "Y"), 0, 0), wired.ar, 1e-3);
  EXPECT_NEAR(engine.arrival(b.port_pin(out), 0, 0), wired.ar + 3, 1e-3);
  EXPECT_NEAR(engine.slack(b.port_pin(out)), 1000 - std::max(wired.ar, wired.af) - 3, 1e-2);
}

TEST(FastEngine, DepthMarginShrinksWithTheLogicDepth) {
  Bench      b({1.0});
  const auto clk = b.netlist->add_input_port("clk");
  const auto ff1 = b.cell("FF"), ff2 = b.cell("FF"), i1 = b.cell("INV");
  for (const auto ff : {ff1, ff2}) {
    b.netlist->connect(b.port_pin(clk), b.pin(ff, "CLK"));
  }
  b.link(ff1, "Q", i1, "A");
  b.link(i1, "Y", ff2, "D");
  b.netlist->finalize();
  const double period = 200;
  fast::Engine plain(*b.netlist, single_clock(period));
  plain.evaluate();
  fast::Options margin;
  margin.depth_margin = {0.10f};
  fast::Engine margined(*b.netlist, single_clock(period), margin);
  margined.evaluate();
  const auto d = b.pin(ff2, "D");
  // Depth at the data pin: clock to output counts 1, the inverter 1.
  EXPECT_EQ(plain.depth(d, 0, 0), 2);
  const S      at            = inv(flop_q(1), 1, 1);
  const double expected_rise = period - 10 - 0.10 * at.ar / std::sqrt(2.0);
  EXPECT_NEAR(margined.required(d, 0, 0), expected_rise, 1e-2);
  EXPECT_LT(margined.report().wns_ps, plain.report().wns_ps);
}

TEST(FastEngine, ClockGatesAndClocksThroughBuffersAreIdeal) {
  Bench      b({1.0});
  const auto clk    = b.netlist->add_input_port("clk");
  const auto buffer = b.cell("BUF"), ff1 = b.cell("FF"), ff2 = b.cell("FF"), i1 = b.cell("INV");
  b.netlist->connect(b.port_pin(clk), b.pin(buffer, "A"));
  b.link(buffer, "Y", ff1, "CLK");
  b.link(buffer, "Y", ff2, "CLK");
  b.link(ff1, "Q", i1, "A");
  b.link(i1, "Y", ff2, "D");
  b.netlist->finalize();
  fast::Engine engine(*b.netlist, single_clock(100));
  engine.evaluate();
  // The clock is ideal: the buffer is in the clock network and adds nothing to the launch time.
  EXPECT_EQ(engine.arrival(b.pin(buffer, "Y"), 0, 0), -inf);
  EXPECT_NEAR(engine.arrival(b.pin(ff1, "Q"), 0, 1), 40, 1e-3);
  EXPECT_EQ(engine.report().endpoints, 1u);
}

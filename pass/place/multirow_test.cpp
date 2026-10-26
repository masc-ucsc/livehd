// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "multirow.hpp"

#include <map>
#include <stdexcept>
#include <string>

#include "capacity_bins.hpp"
#include "gtest/gtest.h"
#include "multirow_test_frame.hpp"
#include "place.hpp"
#include "terminal_legalize.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
namespace dp = DreamPlace::native;
using ph::Orientation;
using ph::Status;

// The shared frame; "dom" and "wide" are two-row CORE masters with the
// row-access template t0 (bottom half horizontal).
struct Fixture : test::Multirow_frame {
  Multirow_options options;
  explicit Fixture(int width = 12, int height = 2, bool flip = true) : Multirow_frame(width, height, flip) {
    two_row("dom", 3);
    two_row("wide", 5);
    macro("block", "BLOCK", 2, 2);
    controls(options);
  }
  // x and y in sites and rows; a fixed cell takes its row's orientation.
  size_t cell(double x, double y, const std::string& master = "dom", Status status = Status::PLACED) {
    const auto& m = tech.macros.at(master);
    Ann_place   p(x * 100, y * 100, m.width * 100, m.height * 100);
    p.status = status;
    if (status == Status::FIXED && m.klass.starts_with("CORE")) {
      p.orientation = fp.rows.at(size_t(y)).orientation;
    }
    fp.components.push_back({"c" + std::to_string(fp.components.size()), master, p});
    return fp.components.size() - 1;
  }
  const Ann_place& at(size_t i) const { return fp.components.at(i).place; }
};

// Independent native check: pinless nodes, movable (PLACED) first, then fixed
// cells and hard placement blockages.
std::string native_violation(const Fixture& f, const ph::Floorplan& fp) {
  dp::Design d;
  d.layout       = {f.fp.die.xl, f.fp.die.yl, f.fp.die.xh, f.fp.die.yh, 100, 100};
  const auto add = [&](const ph::Rect& r, bool macro) {
    d.node_x.push_back(r.xl);
    d.node_y.push_back(r.yl);
    d.node_size_x.push_back(r.xh - r.xl);
    d.node_size_y.push_back(r.yh - r.yl);
    d.node_macro.push_back(macro);
  };
  for (bool fixed : {false, true}) {
    for (const auto& c : fp.components) {
      if ((c.place.status == Status::FIXED || c.place.status == Status::LOCKED) == fixed) {
        add(Capacity_bins::footprint(c.place), !f.tech.macros.at(c.macro).klass.starts_with("CORE"));
      }
    }
    if (!fixed) {
      d.num_movable = int(d.node_x.size());
    }
  }
  for (const auto& exclusion : ph::placement_exclusions(fp, fp.die)) {
    if (exclusion.kind == ph::Placement_mask::hard) {
      add(exclusion.rect, true);
    }
  }
  d.flat_net2pin_start = {0};
  d.build_maps();
  std::string why;
  return dp::is_legal(d, &why) ? "" : why.empty() ? "illegal" : why;
}
// Release a copy of the anchored frame and check it with check_legality and
// the native legality check; anchors keep the start row's orientation.
void expect_legal(const Fixture& f, const Multirow_report& report) {
  auto frame = f.fp;
  release_multirow_anchors(frame, report);
  for (const auto& anchor : report.anchors) {
    const auto& p = frame.components[anchor.component].place;
    EXPECT_EQ(p.status, Status::PLACED);
    EXPECT_EQ(p.orientation, frame.rows.at(size_t(p.y / 100)).orientation);
  }
  EXPECT_NO_THROW(check_legality(f.tech, frame));
  EXPECT_EQ(native_violation(f, frame), "");
}
void expect_unchanged(const ph::Floorplan& before, const ph::Floorplan& after) {
  EXPECT_EQ(before.components, after.components);
  EXPECT_EQ(before.rows, after.rows);
  EXPECT_EQ(before.blockages, after.blockages);
}
std::string failure(Fixture& f) {
  const auto before = f.fp;
  try {
    anchor_multirow_cells(f.tech, f.fp, f.options);
  } catch (const Multirow_failure& e) {
    expect_unchanged(before, f.fp);
    return e.what();
  }
  ADD_FAILURE() << "packing succeeded";
  return "";
}
bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
}  // namespace

TEST(Multirow, RowCountsAndStartChecks) {
  Fixture f;
  f.macro("triple", "CORE", 2, 3);
  f.macro("half", "CORE", 2, 1.5);
  EXPECT_EQ(multirow_rows(f.tech, f.tech.macros.at("cell")), 0);
  EXPECT_EQ(multirow_rows(f.tech, f.tech.macros.at("dom")), 2);
  EXPECT_EQ(multirow_rows(f.tech, f.tech.macros.at("triple")), 3);
  EXPECT_EQ(multirow_rows(f.tech, f.tech.macros.at("half")), 0);
  EXPECT_EQ(multirow_rows(f.tech, f.tech.macros.at("block")), 0);
  EXPECT_TRUE(is_multirow_core(f.tech, f.tech.macros.at("dom")));
  EXPECT_FALSE(is_multirow_core(f.tech, f.tech.macros.at("tap")));
  EXPECT_THROW(multirow_start_allowed(f.tech, f.fp, 0, "cell", f.options), std::invalid_argument);
  f.templates["triple"] = f.templates["dom"];
  EXPECT_THROW(multirow_start_allowed(f.tech, f.fp, 0, "triple", f.options), std::invalid_argument);
}

TEST(Multirow, EvenAndOddStartsTakeTheStartRowOrientation) {
  Fixture    f(20, 6);
  const auto even = f.cell(2, .2), odd = f.cell(10, 3.1);
  for (size_t row = 0; row < f.fp.rows.size(); ++row) {
    EXPECT_TRUE(multirow_start_allowed(f.tech, f.fp, row, "dom", f.options)) << row;
  }
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  ASSERT_EQ(report.anchors.size(), 2);
  EXPECT_EQ(f.at(even).x, 200);
  EXPECT_EQ(f.at(even).y, 0);
  EXPECT_EQ(f.at(even).orientation, Orientation::N);
  EXPECT_EQ(f.at(odd).x, 1000);
  EXPECT_EQ(f.at(odd).y, 300);
  EXPECT_EQ(f.at(odd).orientation, Orientation::FS);
  EXPECT_EQ(f.at(odd).status, Status::FIXED);
  EXPECT_EQ(report.even_starts, 1);
  EXPECT_EQ(report.odd_starts, 1);
  EXPECT_EQ(report.full_core_scans, 0);
  EXPECT_NEAR(report.total_displacement_um, .3, 1e-9);
  EXPECT_NEAR(report.max_displacement_um, .2, 1e-9);
  EXPECT_EQ(report.anchors[0].before.y, 20);
  expect_legal(f, report);
}

TEST(Multirow, RailTwinsServeBothParitiesOfAlternatingRows) {
  // Rows alternate N/FS, so the rail stack a two-row cell needs alternates:
  // t0 (VSS outside) starts on N rows, its twin t1 (VDD outside) on FS rows.
  Fixture f(20, 6);
  f.pg_rails();
  f.twin_pair("pair", 3);
  for (size_t row = 0; row < 5; ++row) {
    EXPECT_EQ(multirow_start_master(f.tech, f.fp, row, "pair", f.options), row % 2 ? "pair_t1" : "pair") << row;
    EXPECT_EQ(multirow_start_allowed(f.tech, f.fp, row, "pair", f.options), row % 2 == 0) << row;
  }
  const auto even = f.cell(2, .2, "pair"), odd = f.cell(10, 3.1, "pair");
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  EXPECT_EQ(f.at(even).y, 0);
  EXPECT_EQ(f.fp.components[even].macro, "pair");
  EXPECT_EQ(f.at(odd).y, 300);  // its nearest start, now legal through the twin
  EXPECT_EQ(f.at(odd).orientation, Orientation::FS);
  EXPECT_EQ(f.fp.components[odd].macro, "pair_t1");
  EXPECT_EQ(report.odd_starts, 1);
  EXPECT_EQ(report.rail_twins, 1);
  expect_legal(f, report);

  // A deliberately wrong parity fails the legality check: either template on
  // the other start.
  auto frame = f.fp;
  release_multirow_anchors(frame, report);
  for (const auto& [cell, wrong] : {
           std::pair{ odd,    "pair"},
           {even, "pair_t1"}
  }) {
    auto bad                   = frame;
    bad.components[cell].macro = wrong;
    try {
      check_legality(f.tech, bad);
      ADD_FAILURE() << wrong << " accepted at the other parity";
    } catch (const std::runtime_error& error) {
      EXPECT_TRUE(contains(error.what(), "rail parity")) << error.what();
    }
  }

  // Without a twin, starts are restricted to the master's own parity.
  Fixture solo(20, 6);
  solo.pg_rails();
  solo.two_row("solo", 3);
  solo.rails("solo", 0);
  const auto lone = solo.cell(10, 3.1, "solo");
  const auto only = anchor_multirow_cells(solo.tech, solo.fp, solo.options);
  EXPECT_EQ(solo.at(lone).y % 200, 0);
  EXPECT_EQ(only.odd_starts, 0);
  EXPECT_EQ(only.rail_twins, 0);
  expect_legal(solo, only);
  // An FS-first two-row frame has only an FS start: refused by name.
  Fixture flipped(8, 2);
  flipped.pg_rails();
  flipped.two_row("solo", 3);
  flipped.rails("solo", 0);
  for (auto& row : flipped.fp.rows) {
    row.orientation = row.orientation == Orientation::N ? Orientation::FS : Orientation::N;
  }
  flipped.options.access = nullptr;  // rails alone decide
  flipped.cell(1, 0, "solo");
  EXPECT_TRUE(contains(failure(flipped), "1 starts have the other PG rail parity and no rail twin fits"));
}

TEST(Multirow, AllNRowsRestrictTheTemplateToEvenStarts) {
  Fixture    f(12, 6, false);
  const auto c = f.cell(5, 3);
  EXPECT_FALSE(multirow_start_allowed(f.tech, f.fp, 3, "dom", f.options));
  EXPECT_TRUE(multirow_start_allowed(f.tech, f.fp, 2, "dom", f.options));
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  EXPECT_EQ(f.at(c).x, 500);
  EXPECT_EQ(f.at(c).y, 200);  // rows 2 and 4 tie; the lower start wins
  EXPECT_EQ(report.odd_starts, 0);
  expect_legal(f, report);

  Fixture plain(12, 6, false);
  plain.options.templates = nullptr;  // no template: orientation symmetry only
  const auto untemplated  = plain.cell(5, 3);
  expect_legal(plain, anchor_multirow_cells(plain.tech, plain.fp, plain.options));
  EXPECT_EQ(plain.at(untemplated).y, 300);
}

TEST(Multirow, StartsNeedTheTemplateAndSymmetry) {
  Fixture f(12, 4);
  for (auto& row : f.fp.rows) {  // an imported FS-first frame
    row.orientation = row.orientation == Orientation::N ? Orientation::FS : Orientation::N;
  }
  f.cell(4, 1);
  const auto message = failure(f);
  EXPECT_TRUE(contains(message, "cannot anchor c0 (dom)")) << message;
  EXPECT_TRUE(contains(message, "starts miss the row-access template")) << message;

  Fixture flipless(12, 4);
  flipless.options.templates                = nullptr;
  flipless.tech.macros.at("dom").symmetry_x = false;
  EXPECT_FALSE(multirow_start_allowed(flipless.tech, flipless.fp, 1, "dom", flipless.options));
  const auto c = flipless.cell(4, 1);
  expect_legal(flipless, anchor_multirow_cells(flipless.tech, flipless.fp, flipless.options));
  EXPECT_EQ(flipless.at(c).y, 0);
  EXPECT_EQ(flipless.at(c).orientation, Orientation::N);
  flipless.fp.components.clear();
  for (auto& row : flipless.fp.rows) {
    row.orientation = Orientation::FS;
  }
  flipless.cell(4, 1);
  EXPECT_TRUE(contains(failure(flipless), "orientations lack LEF SYMMETRY"));
}

TEST(Multirow, ACutBottomRowKeepsTheRowAccessOrigin) {
  // A generated N/FS frame whose bottom row a full-width macro removed, as
  // cut_macro_rows does: parity must still count from the original first row.
  Fixture f(12, 6);
  f.fp.rows.erase(f.fp.rows.begin());
  const auto c       = f.cell(5, 3);
  // Counting from the lowest remaining (FS) row reads it as even: every start misses t0.
  const auto message = failure(f);
  EXPECT_TRUE(contains(message, "starts miss the row-access template")) << message;
  f.options.core_origins = first_core_rows(Fixture(12, 6).fp);
  ASSERT_EQ(f.options.core_origins.at("core"), 0);
  for (size_t row = 0; row < f.fp.rows.size(); ++row) {
    EXPECT_TRUE(multirow_start_allowed(f.tech, f.fp, row, "dom", f.options)) << row;
  }
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  EXPECT_EQ(f.at(c).y, 300);
  EXPECT_EQ(f.at(c).orientation, Orientation::FS);
  EXPECT_EQ(report.odd_starts, 1);
  auto frame = f.fp;
  release_multirow_anchors(frame, report);
  EXPECT_NO_THROW(check_legality(f.tech, frame));
  EXPECT_EQ(native_violation(f, frame), "");
}

TEST(Multirow, AGapInTheUpperRowOnlyNeverSplitsTheFootprint) {
  Fixture f;
  f.cell(5, 1, "cell", Status::FIXED);
  const auto c      = f.cell(4, 0);
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  EXPECT_EQ(f.at(c).x, 200);  // sites 2 and 6 tie; the lower x wins
  EXPECT_EQ(f.at(c).y, 0);
  expect_legal(f, report);
}

TEST(Multirow, HardBlockagesTapsAndFixedLogicAreObstacles) {
  // Each obstacle alone covers site 1 of one or both rows; a soft blockage
  // over sites 2-5 does not.
  for (const int kind : {0, 1, 2}) {
    Fixture f(10, 2);
    f.fp.blockages.push_back({true, "", {{200, 0, 600, 200}}});
    if (kind == 0) {
      f.fp.blockages.push_back({false, "", {{0, 0, 200, 200}}});
    } else {
      f.cell(1, kind == 1 ? 1 : 0, kind == 1 ? "tap" : "cell", Status::FIXED);
    }
    const auto c      = f.cell(0, 0);
    const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
    EXPECT_EQ(f.at(c).x, 200) << kind;
    EXPECT_EQ(f.at(c).y, 0) << kind;
    expect_legal(f, report);
  }
}

TEST(Multirow, RowsAreClippedToTheDie) {
  Fixture f;
  f.fp.die.xh       = 800;  // rows extend four sites past the die
  const auto c      = f.cell(7, 0);
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  EXPECT_EQ(f.at(c).x, 500);
  EXPECT_EQ(report.anchors.size(), 1);
}

TEST(Multirow, CutRowsKeepEachSegmentsSiteGrid) {
  // The upper row is cut after site 2; its second segment starts on the site
  // grid at 600, or off it at 650, where no x interval can align both rows.
  for (const int64_t upper : {600, 650}) {
    Fixture f;
    f.fp.rows[1].count = 3;
    f.fp.rows.push_back({"r1b", "core", upper, 100, 5, 100, Orientation::FS});
    const auto c      = f.cell(4, 0);
    const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
    EXPECT_EQ(f.at(c).x, upper == 600 ? 600 : 0);
    expect_legal(f, report);
  }
}

TEST(Multirow, MacroHalosAreReservedInEveryRow) {
  for (const int64_t halo : {0, 100}) {
    Fixture f(12, 4);
    f.options.occupancy.macro_halo_x_dbu = f.options.occupancy.macro_halo_y_dbu = halo;
    f.cell(4, 0, "block", Status::FIXED);
    const auto c      = f.cell(4, 0);
    const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
    EXPECT_EQ(f.at(c).x, halo ? 700 : 600);
    EXPECT_EQ(f.at(c).y, 0);
    expect_legal(f, report);
  }
}

TEST(Multirow, PaddingHoldsOnEveryRowTheAnchorSpans) {
  for (const unsigned padding : {0u, 1u}) {
    Fixture f(14, 2);
    f.options.occupancy.padding_sites = padding;
    f.cell(6, 1, "cell", Status::FIXED);
    const auto a = f.cell(3, 0), b = f.cell(9, 0);
    const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
    // With padding, the anchor and the upper-row cell each keep one free
    // site, in both rows; the row's last site also stays free for b.
    EXPECT_EQ(f.at(a).x, padding ? 100 : 300);
    EXPECT_EQ(f.at(b).x, 900);
    EXPECT_EQ(f.at(b).y, 0);
    expect_legal(f, report);
  }
  Fixture pair(14, 2);
  pair.options.occupancy.padding_sites = 1;
  const auto first = pair.cell(2, 0), second = pair.cell(2, 0);
  expect_legal(pair, anchor_multirow_cells(pair.tech, pair.fp, pair.options));
  EXPECT_EQ(pair.at(first).x, 200);
  EXPECT_EQ(pair.at(second).x, 700);  // each anchor keeps its own padding site
  Fixture edge(12, 2);
  edge.options.occupancy.padding_sites = 1;
  const auto last            = edge.cell(9, 0);
  expect_legal(edge, anchor_multirow_cells(edge.tech, edge.fp, edge.options));
  EXPECT_EQ(edge.at(last).x, 800);  // its padding stays inside the row
  Fixture narrow(4, 2);
  narrow.options.occupancy.padding_sites = 1;
  narrow.cell(0, 0);
  EXPECT_TRUE(contains(failure(narrow), "plus 1 padding sites per side"));
}

TEST(Multirow, TheDensityCapMovesTheAnchorAndTerminalLegalizationAcceptsIt) {
  Fixture f(20, 2);
  f.options.occupancy.bins_x      = 2;
  f.options.occupancy.max_density = .6;
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 4; ++x) {
      f.cell(x, y, "cell", Status::FIXED);
    }
  }
  const auto c      = f.cell(5, 0);
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  // The left bin holds 8 fixed sites of 20; at most 4 more fit under 0.6.
  EXPECT_EQ(f.at(c).x, 800);
  EXPECT_EQ(report.candidates, 5);
  expect_legal(f, report);

  Terminal_legalize_options terminal;
  terminal.bins_x      = f.options.occupancy.bins_x;
  terminal.bins_y      = f.options.occupancy.bins_y;
  terminal.max_density = f.options.occupancy.max_density;
  EXPECT_NO_THROW(terminal_legalize(f.tech, f.fp, timing::Feedback{}, terminal));

  Fixture dense(20, 2);
  dense.options.occupancy.max_density = .1;
  dense.cell(5, 0);
  EXPECT_TRUE(contains(failure(dense), "exceeds the hard bin density cap"));
}

// The HighTide lfsr area-driven core (ASAP7, utilization 0.55): 104 sites by
// 21 rows, 31 two-row cells 17 sites wide (89% of the cell area), one padding
// site, cap 0.8. The fitted grid inherited the one-row bin height, so each
// bin column held one two-row cell per row and packing failed. Bins at least
// as tall as the cell average the cap over the row pair.
TEST(Multirow, TinyCoreBinsAreAtLeastAsTallAsTheTwoRowCells) {
  // The run's global-placement poses of the two-row cells, in sites and rows.
  static constexpr double gp[][2] = {
      { 1.5,  6.5},
      {65.7,  0.6},
      { 1.9, 12.1},
      {27.7,  9.3},
      {28.0, 15.3},
      {66.1, 18.5},
      {27.1, 18.4},
      { 2.7,  0.8},
      {60.1,  3.5},
      {85.6,  0.5},
      {22.6, 12.3},
      {23.9,  0.6},
      {43.9,  6.8},
      {47.0, 15.1},
      {85.6, 18.4},
      {46.5, 18.4},
      {27.8,  3.5},
      {81.5,  3.5},
      {85.3,  7.9},
      {43.0, 12.0},
      {62.8, 10.8},
      {66.5, 15.5},
      {65.1,  6.3},
      {84.7, 11.5},
      { 6.0,  9.3},
      {22.8,  6.4},
      { 8.5, 15.2},
      {85.5, 14.9},
      { 7.6, 18.4},
      { 6.4,  3.6},
      {44.4,  0.6}
  };
  const auto frame = [](Fixture& f) {
    f.two_row("dom17", 17);
    f.options.occupancy.padding_sites = 1;
    f.options.occupancy.max_density   = .8;
    for (const auto& [x, y] : gp) {
      f.cell(x, y, "dom17");
    }
    for (int i = 0; i < 19; ++i) {
      f.cell((i * 23) % 100, (i * 5) % 21, "cell");
    }
  };
  Fixture f(104, 21);
  frame(f);
  Terminal_legalize_options terminal;
  terminal.bins_x = terminal.bins_y = 64;  // the inherited placement grid
  terminal.max_density              = f.options.occupancy.max_density;
  terminal.padding_sites            = f.options.occupancy.padding_sites;
  fit_terminal_legalize_grid(f.tech, f.fp, terminal);
  EXPECT_EQ(terminal.bins_x, 4);   // 17 / 0.8 sites wide at least
  EXPECT_EQ(terminal.bins_y, 10);  // two rows tall at least, not one
  f.options.occupancy = occupancy_controls(terminal);
  Multirow_report report;
  ASSERT_NO_THROW(report = anchor_multirow_cells(f.tech, f.fp, f.options));
  EXPECT_EQ(report.anchors.size(), 31);
  ASSERT_NO_THROW(terminal_legalize(f.tech, f.fp, timing::Feedback{}, terminal));
  release_multirow_anchors(f.fp, report);
  EXPECT_NO_THROW(check_legality(f.tech, f.fp));
  EXPECT_EQ(native_violation(f, f.fp), "");

  // The one-row grid the fit used to keep fails on the same frame.
  Fixture rows(104, 21);
  frame(rows);
  rows.options.occupancy        = occupancy_controls(terminal);
  rows.options.occupancy.bins_y = 21;
  EXPECT_TRUE(contains(failure(rows), "exceeds the hard bin density cap")) << "one-row bins";

  // Single-row frames keep the one-row bin height.
  Fixture flat(104, 21);
  for (int i = 0; i < 40; ++i) {
    flat.cell((i * 23) % 100, (i * 5) % 21, "cell");
  }
  Terminal_legalize_options single;
  single.bins_x = single.bins_y = 64;
  fit_terminal_legalize_grid(flat.tech, flat.fp, single);
  EXPECT_EQ(single.bins_y, 21);
}

TEST(Multirow, DeliberateInfeasibilityIsAtomic) {
  Fixture f(8, 2);
  f.cell(3, 0, "cell", Status::FIXED);
  f.cell(5, 1, "cell", Status::FIXED);
  f.cell(1, 0);
  f.cell(5, 0);  // common free sites: 0-2, 4 and 6-7
  EXPECT_TRUE(contains(failure(f), "cannot anchor c3 (dom): no free interval"));

  Fixture single(8, 1);
  single.cell(0, 0);
  EXPECT_TRUE(contains(failure(single), "no 2 consecutive core rows of site core"));

  Fixture budget;
  budget.options.work = 1;
  budget.cell(0, 0);
  EXPECT_TRUE(contains(failure(budget), "exhausted its work budget at c0"));
}

TEST(Multirow, StaggeredTapsFragmentRowsYetEveryAnchorIsLegal) {
  Fixture f(24, 6);
  for (int y = 0; y < 6; ++y) {
    for (int x = y % 2 ? 4 : 0; x < 24; x += 8) {
      f.cell(x, y, "tap", Status::FIXED);
    }
  }
  for (const double y : {.2, .2, .2, 3.1, 3.1, 3.1}) {
    f.cell(5, y);
  }
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  ASSERT_EQ(report.anchors.size(), 6);
  for (const auto& anchor : report.anchors) {
    // Every gap common to two rows between staggered taps is three sites wide.
    const auto& p = f.at(anchor.component);
    EXPECT_EQ((p.x / 100) % 4, 1) << p.x;
  }
  // Nearest poses: a short climb to another row pair beats a long slide.
  EXPECT_EQ(report.even_starts, 3);
  EXPECT_EQ(report.odd_starts, 3);
  expect_legal(f, report);
}

TEST(Multirow, OrderIsWidestFirstThenByNameRegardlessOfInputOrder) {
  std::map<std::string, Ann_place> placed[2];
  for (const bool reversed : {false, true}) {
    Fixture f(10, 2);
    f.cell(2, 0, "dom");
    f.cell(2, 0, "wide");
    if (reversed) {
      std::swap(f.fp.components[0].macro, f.fp.components[1].macro);
      std::swap(f.fp.components[0].place, f.fp.components[1].place);
      std::swap(f.fp.components[0].name, f.fp.components[1].name);
    }
    const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
    EXPECT_EQ(f.fp.components[report.anchors.front().component].macro, "wide");
    for (const auto& c : f.fp.components) {
      placed[reversed][c.name] = c.place;
    }
    expect_legal(f, report);
  }
  EXPECT_EQ(placed[0], placed[1]);
  EXPECT_EQ(placed[0].at("c1").x, 200);  // wide
  EXPECT_EQ(placed[0].at("c0").x, 700);  // dom
}

TEST(Multirow, AnEmptyWindowFallsBackToTheWholeCore) {
  Fixture f(40, 2);
  f.macro("big", "BLOCK", 30, 2);
  f.options.window_rows  = 0;
  f.options.window_sites = 4;
  f.cell(0, 0, "big", Status::FIXED);
  const auto c      = f.cell(2, 0);
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  EXPECT_EQ(f.at(c).x, 3000);
  EXPECT_EQ(report.full_core_scans, 1);
  EXPECT_NEAR(report.max_displacement_um, 28, 1e-9);
  expect_legal(f, report);
}

TEST(Multirow, AnchorsSurviveTerminalLegalizationAndReleaseLegally) {
  Fixture f(16, 4);
  f.options.occupancy.bins_x = f.options.occupancy.bins_y = 2;
  f.options.occupancy.max_density                         = .9;
  const auto a = f.cell(2, .4), b = f.cell(9, 1.2);
  const int  xs[] = {2, 3, 9, 10, 5, 2}, ys[] = {0, 1, 1, 2, 0, 1};
  for (int i = 0; i < 6; ++i) {
    f.cell(xs[i], ys[i], "cell");  // overlapping single-row cells
  }
  const auto report  = anchor_multirow_cells(f.tech, f.fp, f.options);
  const auto anchors = f.fp;
  EXPECT_EQ(f.at(a).y, 0);
  EXPECT_EQ(f.at(b).y, 100);
  EXPECT_EQ(f.at(b).orientation, Orientation::FS);

  Terminal_legalize_options terminal;
  terminal.bins_x      = f.options.occupancy.bins_x;
  terminal.bins_y      = f.options.occupancy.bins_y;
  terminal.max_density = f.options.occupancy.max_density;
  ASSERT_NO_THROW(terminal_legalize(f.tech, f.fp, timing::Feedback{}, terminal));
  for (const auto& anchor : report.anchors) {
    EXPECT_EQ(f.fp.components[anchor.component], anchors.components[anchor.component]);
  }
  release_multirow_anchors(f.fp, report);
  EXPECT_EQ(f.at(a).status, Status::PLACED);
  EXPECT_NO_THROW(check_legality(f.tech, f.fp));
  EXPECT_EQ(native_violation(f, f.fp), "");
}

TEST(Multirow, ReleaseIsAtomicAndRequiresFixedAnchors) {
  Fixture f;
  f.cell(0, 0);
  f.cell(6, 0);
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  ASSERT_EQ(report.anchors.size(), 2);
  f.fp.components[report.anchors.back().component].place.status = Status::PLACED;
  EXPECT_THROW(release_multirow_anchors(f.fp, report), std::invalid_argument);
  EXPECT_EQ(f.fp.components[report.anchors.front().component].place.status, Status::FIXED);
}

TEST(Multirow, OnlyAdmittedMastersAreAnchored) {
  Fixture    f;
  const auto dom = f.cell(1, 0), wide = f.cell(6, 0, "wide");
  f.options.admits  = [](std::string_view master) { return master == "dom"; };
  const auto before = f.at(wide);
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  ASSERT_EQ(report.anchors.size(), 1);
  EXPECT_EQ(report.anchors[0].component, dom);
  EXPECT_EQ(f.at(dom).status, Status::FIXED);
  EXPECT_EQ(f.at(wide), before);
}

TEST(Multirow, FramesWithoutMovableMultirowCellsAreUntouched) {
  Fixture f;
  f.cell(0, 0, "cell");
  f.cell(4, 0, "dom", Status::FIXED);
  f.fp.rows.clear();  // not even rows are needed
  const auto before = f.fp;
  const auto report = anchor_multirow_cells(f.tech, f.fp, f.options);
  EXPECT_TRUE(report.anchors.empty());
  expect_unchanged(before, f.fp);
  f.options.occupancy.bins_x = 0;
  EXPECT_THROW(anchor_multirow_cells(f.tech, f.fp, f.options), std::invalid_argument);
}

TEST(Multirow, GreedyPackerLimitIsAtomicAndNamed) {
  // The known limit: each cell takes its nearest legal pose in packing order,
  // with no pairing or backtracking, so a first anchor can fragment the only
  // space a second cell needs. The failure is atomic and names that cell,
  // although another assignment holds both.
  // Across rows: the middle row pair of four rows leaves two single rows.
  Fixture across(5, 4);
  across.cell(0, 1, "wide");
  across.cell(0, 1.2, "wide");
  const auto split_rows = failure(across);
  EXPECT_TRUE(contains(split_rows, "cannot anchor c1 (wide): no free interval of 500 DBU")) << split_rows;
  // Along a row pair: a mid-gap anchor leaves two short gaps.
  Fixture along(10, 2);
  along.cell(2, 0, "wide");
  along.cell(2, 0, "wide");
  const auto split_sites = failure(along);
  EXPECT_TRUE(contains(split_sites, "cannot anchor c1 (wide): no free interval of 500 DBU")) << split_sites;

  // From other global-placement poses the same frames hold both cells.
  Fixture    stacked(5, 4);
  const auto low = stacked.cell(0, 0, "wide"), high = stacked.cell(0, 2, "wide");
  expect_legal(stacked, anchor_multirow_cells(stacked.tech, stacked.fp, stacked.options));
  EXPECT_EQ(stacked.at(low).y, 0);
  EXPECT_EQ(stacked.at(high).y, 200);
  Fixture    side(10, 2);
  const auto left = side.cell(0, 0, "wide"), right = side.cell(5, 0, "wide");
  expect_legal(side, anchor_multirow_cells(side.tech, side.fp, side.options));
  EXPECT_EQ(side.at(left).x, 0);
  EXPECT_EQ(side.at(right).x, 500);
}
}  // namespace livehd::placement

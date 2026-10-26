// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "multirow_eco.hpp"

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "place.hpp"
#include "row_occupancy.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
using ph::Orientation;
using ph::Row_access;
using ph::Status;
using Outcome = Multirow_eco_cell::Outcome;

// 100 DBU per micron; one-micron sites and rows, N/FS rows by default. "dom",
// "dom_b" (an equal-width variant), "dom5", "dom_2p5" (rounded up to three
// sites) and "dom6" are two-row CORE variants with the row-access template t0.
struct Fixture {
  ph::Technology                           tech;
  ph::Floorplan                            fp;
  ph::Row_access_convention                access;
  std::map<std::string, Multirow_template> templates;
  Multirow_eco_options                     options;
  explicit Fixture(int width = 12, int height = 2, bool flip = true) {
    tech.dbu = fp.dbu  = 100;
    tech.sites["core"] = {1, 1, true};
    macro("cell", "CORE", 1, 1);
    macro("tap", "CORE WELLTAP", 1, 1);
    for (const auto& [name, sites] : std::map<std::string, double>{
             {    "dom",   3},
             {  "dom_b",   3},
             {   "dom5",   5},
             {"dom_2p5", 2.5},
             {   "dom6",   6}
    }) {
      macro(name, "CORE", sites, 2, true);
      templates[name] = {Row_access::horizontal, Row_access::vertical};
    }
    fp.die = {0, 0, width * 100., height * 100.};
    for (int y = 0; y < height; ++y) {
      fp.rows.push_back(
          {"r" + std::to_string(y), "core", 0, y * 100, width, 100, flip && y % 2 ? Orientation::FS : Orientation::N});
    }
    options.anchor.access                  = &access;
    options.anchor.templates               = &templates;
    options.anchor.occupancy.bins_x        = 1;
    options.anchor.occupancy.bins_y        = 1;
    options.anchor.occupancy.max_density   = 1;
    options.anchor.occupancy.padding_sites = 0;  // the padding cases set their own
  }
  Fixture(const Fixture&) = delete;  // options point into the fixture
  void macro(const std::string& name, const std::string& klass, double width, double height, bool symmetry_y = false) {
    auto& m      = tech.macros[name];
    m.width      = width;
    m.height     = height;
    m.klass      = klass;
    m.site       = "core";
    m.symmetry_x = true;
    m.symmetry_y = symmetry_y;
  }
  // x and y in sites and rows; a cell takes its row's orientation.
  std::string cell(double x, double y, const std::string& master = "cell", Status status = Status::PLACED) {
    const auto& m = tech.macros.at(master);
    Ann_place   p(x * 100, y * 100, m.width * 100, m.height * 100);
    p.status      = status;
    p.orientation = fp.rows.at(size_t(y)).orientation;
    fp.components.push_back({"c" + std::to_string(fp.components.size()), master, p});
    return fp.components.back().name;
  }
  ph::Component& at(const std::string& name) {
    for (auto& c : fp.components) {
      if (c.name == name) {
        return c;
      }
    }
    throw std::out_of_range(name);
  }
  // Swap the master at the same pose, as a variant edit does; returns the reference.
  ph::Floorplan edit(const std::map<std::string, std::string>& masters) {
    const auto reference = fp;
    for (const auto& [name, master] : masters) {
      at(name).macro       = master;
      at(name).place.width = std::llround(tech.macros.at(master).width * 100);
    }
    return reference;
  }
  Multirow_eco_report run(const ph::Floorplan& reference, const std::vector<std::string>& edited) {
    return legalize_multirow_edits(tech, fp, reference, edited, options);
  }
};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
void expect_unchanged(const ph::Floorplan& before, const ph::Floorplan& after) {
  EXPECT_EQ(before.components, after.components);
  EXPECT_EQ(before.rows, after.rows);
  EXPECT_EQ(before.blockages, after.blockages);
}
std::string failure(Fixture& f, const ph::Floorplan& reference, const std::vector<std::string>& edited) {
  const auto before = f.fp;
  try {
    f.run(reference, edited);
  } catch (const Multirow_failure& e) {
    expect_unchanged(before, f.fp);
    return e.what();
  }
  ADD_FAILURE() << "the ECO succeeded";
  return "";
}
// After an accepted ECO, with the displaced cells set aside (their caller
// re-places them): the frame is legal, every pose of a two-row cell is a legal
// start in its row's orientation, the padding beside every edited cell is
// free, and every bin holds its logic within the cap.
void expect_accepted(Fixture& f, const Multirow_eco_report& report) {
  const std::set<std::string> displaced(report.displaced.begin(), report.displaced.end());
  auto                        frame = f.fp;
  std::erase_if(frame.components, [&](const auto& c) { return displaced.contains(c.name); });
  EXPECT_NO_THROW(check_legality(f.tech, frame));
  const auto pad = f.options.anchor.occupancy.padding_sites;
  for (const auto& entry : report.cells) {
    const auto& c   = f.at(entry.name);
    const auto  row = size_t(c.place.y / 100);
    EXPECT_EQ(c.place.orientation, f.fp.rows.at(row).orientation) << c.name;
    EXPECT_TRUE(multirow_start_allowed(f.tech, f.fp, row, c.macro, f.options.anchor)) << c.name;
    const auto padded = padded_rect(f.tech, c, pad);
    for (const auto& other : frame.components) {
      if (other.name != c.name) {
        EXPECT_EQ(Capacity_bins::overlap(padded, Capacity_bins::footprint(other.place)), 0) << c.name << " pads " << other.name;
      }
    }
  }
  auto logic = f.fp;  // displaced cells still count, at their current poses
  for (auto& c : logic.components) {
    if (frame_role(f.tech, c) == Frame_role::movable) {
      c.place.status = Status::FIXED;
    }
  }
  auto controls          = f.options.anchor.occupancy;
  controls.padding_sites = 0;
  EXPECT_TRUE(row_occupancy(f.tech, logic, controls, "test", false).grid.within_cap(controls.max_density + 1e-9));
}
}  // namespace

TEST(MultirowEco, ADownSizeReleasesExactlyTheRoundedSitesInBothRows) {
  for (const auto* master : {"dom", "dom_2p5"}) {
    Fixture    f;
    const auto d = f.cell(2, 0, "dom5"), left = f.cell(1, 0), right = f.cell(7, 1);
    const auto pose      = f.at(d).place;
    const auto reference = f.edit({
        {d, master}
    });
    const auto report    = f.run(reference, {d});
    ASSERT_EQ(report.cells.size(), 1);
    EXPECT_EQ(report.cells[0].outcome, Outcome::in_place);
    EXPECT_EQ(report.cells[0].released_sites, 4) << master;  // two sites in each row, 2.5 rounded up to 3
    EXPECT_EQ(report.cells[0].grown_sites, 0);
    EXPECT_EQ(report.cells[0].displacement_um, 0);
    EXPECT_TRUE(report.displaced.empty());
    EXPECT_EQ(f.at(d).place.x, pose.x);
    EXPECT_EQ(f.at(d).place.y, pose.y);
    EXPECT_EQ(f.at(d).place.orientation, pose.orientation);
    EXPECT_EQ(f.at(d).macro, master);
    EXPECT_EQ(f.at(left), reference.components[1]);
    EXPECT_EQ(f.at(right), reference.components[2]);
    expect_accepted(f, report);
  }
}

TEST(MultirowEco, EqualWidthVariantsKeepThePoseAndReleaseNothing) {
  Fixture    f;
  const auto d         = f.cell(4, 0, "dom");
  const auto reference = f.edit({
      {d, "dom_b"}
  });
  const auto report    = f.run(reference, {d});
  EXPECT_EQ(report.cells.at(0).outcome, Outcome::in_place);
  EXPECT_EQ(report.cells[0].released_sites, 0);
  EXPECT_EQ(report.cells[0].grown_sites, 0);
  EXPECT_EQ(f.at(d).place, reference.components[0].place);
  EXPECT_EQ(f.at(d).macro, "dom_b");
}

TEST(MultirowEco, GrowthIntoFreeSpaceKeepsThePose) {
  Fixture    f;
  const auto d = f.cell(2, 0, "dom");
  f.cell(1, 0);
  f.cell(9, 1);
  const auto reference = f.edit({
      {d, "dom5"}
  });
  const auto report    = f.run(reference, {d});
  EXPECT_EQ(report.cells.at(0).outcome, Outcome::in_place);
  EXPECT_EQ(report.cells[0].grown_sites, 4);
  EXPECT_EQ(f.at(d).place.x, 200);
  EXPECT_EQ(f.at(d).place.width, 500);
  EXPECT_TRUE(report.displaced.empty());
  expect_accepted(f, report);
}

TEST(MultirowEco, GrowthShiftsBeforeItDisplacesAndDisplacesInBothRows) {
  // dom grows from sites 2-4 to 2-6; single-row cells sit at site 5 of the
  // lower row and site 6 of the upper one, a fixed cell at site 0 of both.
  const auto setup = [](Fixture& f) {
    f.cell(0, 0, "cell", Status::FIXED);
    f.cell(0, 1, "cell", Status::FIXED);
    const auto d = f.cell(2, 0, "dom");
    f.cell(5, 0);
    f.cell(6, 1);
    return d;
  };
  Fixture    shift(16, 2);
  const auto d         = setup(shift);
  auto       reference = shift.edit({
      {d, "dom5"}
  });
  auto       report    = shift.run(reference, {d});
  EXPECT_EQ(report.cells.at(0).outcome, Outcome::shifted);
  EXPECT_EQ(shift.at(d).place.x, 700);  // nearest x free in both rows
  EXPECT_NEAR(report.cells[0].displacement_um, 5, 1e-9);
  EXPECT_TRUE(report.displaced.empty());
  expect_accepted(shift, report);

  for (const unsigned bound : {0u, 4u}) {
    Fixture f(16, 2);
    f.options.bounds.shift_sites = bound;
    const auto e                 = setup(f);
    reference                    = f.edit({
        {e, "dom5"}
    });
    report                       = f.run(reference, {e});
    EXPECT_EQ(report.cells.at(0).outcome, Outcome::displaced) << bound;
    EXPECT_EQ(f.at(e).place.x, 200) << bound;  // in place, the nearest
    EXPECT_EQ(report.cells[0].displaced_cells, 2);
    EXPECT_EQ(report.displaced, (std::vector<std::string>{"c3", "c4"}));
    EXPECT_EQ(f.at("c3"), reference.components[3]);  // left for the caller to re-place
    expect_accepted(f, report);
  }
}

TEST(MultirowEco, PaddingIsKeptFreeBesideTheNewFootprint) {
  for (const unsigned padding : {0u, 1u}) {
    Fixture f(16, 2);
    f.options.anchor.occupancy.padding_sites = padding;
    f.options.bounds.shift_sites             = 0;
    const auto d                             = f.cell(2, 0, "dom");
    f.cell(8, 1);  // beside the grown footprint, inside its padding
    const auto reference = f.edit({
        {d, "dom6"}
    });
    const auto report    = f.run(reference, {d});
    EXPECT_EQ(report.cells.at(0).outcome, padding ? Outcome::displaced : Outcome::in_place) << padding;
    EXPECT_EQ(report.displaced.size(), padding) << padding;
    EXPECT_EQ(f.at(d).place.x, 200);
    expect_accepted(f, report);
  }
}

TEST(MultirowEco, AGapInTheUpperRowAloneIsRefusedAtomically) {
  // The lower row is free only at sites 2-4, the upper one everywhere.
  Fixture f(8, 2);
  f.cell(0, 0, "cell", Status::FIXED);
  f.cell(1, 0, "cell", Status::FIXED);
  f.cell(5, 0, "cell", Status::FIXED);
  const auto d         = f.cell(2, 0, "dom");
  const auto reference = f.edit({
      {d, "dom5"}
  });
  const auto message   = failure(f, reference, {d});
  EXPECT_TRUE(contains(message, "two-row ECO cannot place c3 (dom5)")) << message;
  EXPECT_TRUE(contains(message, "no free interval of 500 DBU")) << message;
}

TEST(MultirowEco, ARejectedGrowthLeavesEveryEditUnapplied) {
  // c0 (widest first) grows in place; c1 then cannot grow beside it. The
  // frame, c0's master and width included, is the input byte for byte.
  Fixture f(10, 2);
  f.cell(0, 0, "dom");
  f.cell(7, 0, "dom");
  const auto reference = f.edit({
      {"c0", "dom6"},
      {"c1", "dom5"}
  });
  EXPECT_TRUE(contains(failure(f, reference, {"c0", "c1"}), "cannot place c1 (dom5)"));
}

TEST(MultirowEco, TheStartRowOrientationAndTemplateAreKeptAtEvenAndOddStarts) {
  Fixture    f(12, 6);
  const auto even = f.cell(4, 0, "dom"), odd = f.cell(4, 3, "dom");
  f.cell(7, 0, "cell", Status::FIXED);  // each grows left: the right side is taken
  f.cell(7, 4, "cell", Status::FIXED);
  const auto reference = f.edit({
      {even, "dom5"},
      { odd, "dom5"}
  });
  const auto report    = f.run(reference, {even, odd});
  ASSERT_EQ(report.cells.size(), 2);
  for (const auto& entry : report.cells) {
    EXPECT_EQ(entry.outcome, Outcome::shifted) << entry.name;
  }
  EXPECT_EQ(f.at(even).place.x, 200);
  EXPECT_EQ(f.at(even).place.y, 0);
  EXPECT_EQ(f.at(even).place.orientation, Orientation::N);
  EXPECT_EQ(f.at(odd).place.x, 200);
  EXPECT_EQ(f.at(odd).place.y, 300);
  EXPECT_EQ(f.at(odd).place.orientation, Orientation::FS);
  expect_accepted(f, report);

  // All-N rows: the template admits even starts only; an odd start is
  // re-anchored on the nearest even one.
  Fixture    plain(12, 6, false);
  const auto c        = plain.cell(4, 2, "dom");
  plain.at(c).place.y = 300;  // an odd start the template refuses
  const auto previous = plain.edit({
      {c, "dom5"}
  });
  const auto anchored = plain.run(previous, {c});
  EXPECT_EQ(anchored.cells.at(0).outcome, Outcome::re_anchored);
  EXPECT_EQ(plain.at(c).place.y, 200);
  expect_accepted(plain, anchored);
}

TEST(MultirowEco, TheDisplacementBudgetIsHonored) {
  // Growing in place covers three single-row cells; within the budget they are
  // displaced, past it the packer re-anchors the cell where it displaces none.
  const auto setup = [](Fixture& f) {
    f.options.bounds.shift_sites = 0;
    const auto d                 = f.cell(2, 0, "dom");
    f.cell(5, 0);
    f.cell(6, 0);
    f.cell(5, 1);
    return d;
  };
  for (const unsigned budget : {3u, 2u}) {
    Fixture f(10, 4);
    f.options.bounds.max_displaced = budget;
    const auto d                   = setup(f);
    const auto reference           = f.edit({
        {d, "dom5"}
    });
    const auto report              = f.run(reference, {d});
    EXPECT_EQ(report.cells.at(0).outcome, budget == 3 ? Outcome::displaced : Outcome::re_anchored);
    EXPECT_EQ(report.displaced.size(), budget == 3 ? 3 : 0);
    EXPECT_EQ(f.at(d).place.x, budget == 3 ? 200 : 0);
    EXPECT_EQ(f.at(d).place.y, 0);
    expect_accepted(f, report);
  }
  Fixture tight(10, 2);
  tight.options.bounds.max_displaced = 2;
  const auto d                       = setup(tight);
  tight.cell(0, 0, "cell", Status::FIXED);  // no free five-site pose remains
  const auto reference = tight.edit({
      {d, "dom5"}
  });
  const auto message   = failure(tight, reference, {d});
  EXPECT_TRUE(contains(message, "no pose within 0 sites of its start row displaces at most 2 single-row cells")) << message;
  EXPECT_TRUE(contains(message, "no free interval of 500 DBU")) << message;
}

TEST(MultirowEco, TheDensityCapMovesAGrowthToAnotherBin) {
  // Two bins of ten sites by two rows; the left one holds eight fixed sites
  // and, under the 0.6 cap, at most four more.
  Fixture f(20, 2);
  f.options.anchor.occupancy.bins_x      = 2;
  f.options.anchor.occupancy.max_density = .6;
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 4; ++x) {
      f.cell(x, y, "cell", Status::FIXED);
    }
  }
  const auto d         = f.cell(6, 0, "dom");
  const auto reference = f.edit({
      {d, "dom5"}
  });
  const auto report    = f.run(reference, {d});
  EXPECT_EQ(report.cells.at(0).outcome, Outcome::shifted);
  EXPECT_EQ(f.at(d).place.x, 800);  // the nearest pose with at most two columns in the left bin
  expect_accepted(f, report);
}

TEST(MultirowEco, EditsAreProcessedWidestFirstAgainstTheReferenceFootprints) {
  // c0 and c1 both grow; c0 (first by name) still sees c1's reference
  // footprint, so it shifts left; c1 then grows in place.
  Fixture f(14, 2);
  f.cell(2, 0, "dom");
  f.cell(6, 0, "dom");
  const auto reference = f.edit({
      {"c0", "dom5"},
      {"c1", "dom5"}
  });
  const auto report    = f.run(reference, {"c1", "c0", "c1"});
  ASSERT_EQ(report.cells.size(), 2);
  EXPECT_EQ(report.cells[0].name, "c0");
  EXPECT_EQ(report.cells[0].outcome, Outcome::shifted);
  EXPECT_EQ(f.at("c0").place.x, 100);
  EXPECT_EQ(report.cells[1].outcome, Outcome::in_place);
  EXPECT_EQ(f.at("c1").place.x, 600);
  expect_accepted(f, report);
}

TEST(MultirowEco, UsageErrorsAreRefused) {
  Fixture    f;
  const auto d = f.cell(2, 0, "dom"), single = f.cell(8, 0), fixed = f.cell(8, 0, "dom", Status::FIXED);
  const auto reference = f.fp;
  EXPECT_THROW(f.run(reference, {"absent"}), std::invalid_argument);
  EXPECT_THROW(f.run(reference, {single}), std::invalid_argument);
  EXPECT_THROW(f.run(reference, {fixed}), std::invalid_argument);
  EXPECT_THROW(f.run(ph::Floorplan{}, {d}), std::invalid_argument);
  EXPECT_TRUE(f.run(reference, {}).cells.empty());
  EXPECT_EQ(f.fp.components, reference.components);
  f.options.anchor.occupancy.bins_x = 0;
  EXPECT_THROW(f.run(reference, {d}), std::invalid_argument);
  EXPECT_EQ(multirow_eco_outcome_name(Outcome::re_anchored), "re_anchored");
}
}  // namespace livehd::placement

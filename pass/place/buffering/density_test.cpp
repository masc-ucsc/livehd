// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "density.hpp"

#include "gtest/gtest.h"

namespace b  = livehd::placement::buffering;
namespace dp = DreamPlace::native;
namespace {
dp::Design design() {
  dp::Design d;
  d.layout      = {0, 0, 10000, 1000, 100, 100};
  d.num_movable = 1;
  d.node_x      = {1000};
  d.node_y      = {0};
  d.node_size_x = {100};
  d.node_size_y = {100};
  d.build_maps();
  d.validate();
  return d;
}
struct Fixture {
  b::Allocation_state initial{
      b::Site_reservations(b::build_legal_sites({{"r", {0, 0, 10000, 1000}, 100}},
      {                              }
      ))
  };
  std::vector<b::Allocation_net> nets;
  b::Allocation_result           allocation{initial};
  Fixture() {
    b::Allocation_net n;
    n.owner = "net";
    b::Allocation_candidate c;
    c.sites = {
        {"r", 20, 5}
    };
    b::Buffer buffer;
    buffer.name         = "repair0";
    buffer.body         = {2, 0, 2.5, .2};
    c.candidate.buffers = {buffer};
    n.candidates        = {c};
    nets                = {n};
    EXPECT_EQ(allocation.proposal.sites.replace("net", c.sites).status, b::Reservation_status::reserved);
    b::Net_allocation selected;
    selected.owner     = "net";
    selected.candidate = 0;
    selected.timing    = b::Evaluation{};
    allocation.nets    = {selected};
  }
};
TEST(ReservationDensity, InvalidUnits) {
  Fixture f;
  EXPECT_THROW(b::bind_reservation_density(design(), {}, f.nets, f.allocation, 0, 1000), std::invalid_argument);
}
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1
TEST(ReservationDensity, UnitsAndNegotiatedCoverage) {
  Fixture f;
  auto    r = b::bind_reservation_density(design(), {}, f.nets, f.allocation, 1000, 1000);
  ASSERT_TRUE(r.supported_api);
  ASSERT_EQ(r.proposal.footprints.size(), 1u);
  EXPECT_DOUBLE_EQ(r.virtual_area, 100000);
  EXPECT_EQ(r.virtual_buffers, 1u);
  EXPECT_DOUBLE_EQ(r.proposal.footprints[0].x, 2000);
  auto d        = design();
  d.layout      = {0, 0, 10, 1, .1, .1};
  d.node_x      = {1};
  d.node_size_x = {.1};
  d.node_size_y = {.1};
  auto scaled   = b::bind_reservation_density(d, {}, f.nets, f.allocation, 1, 1000);
  EXPECT_DOUBLE_EQ(scaled.virtual_area, .1);
  EXPECT_DOUBLE_EQ(scaled.proposal.footprints[0].x, 2);
}
TEST(ReservationDensity, ClearAndProtectedOwners) {
  Fixture f;
  auto    old = b::bind_reservation_density(design(), {}, f.nets, f.allocation, 1000, 1000).proposal;
  old.footprints.push_back({"protected", 5000, 0, 100, 100, -1});
  f.nets[0].candidates[0].candidate.buffers.clear();
  f.nets[0].candidates[0].sites.clear();
  f.allocation.proposal.sites.release("net");
  auto r = b::bind_reservation_density(design(), old, f.nets, f.allocation, 1000, 1000);
  ASSERT_EQ(r.proposal.footprints.size(), 1u);
  EXPECT_EQ(r.proposal.footprints[0].owner, "protected");
  EXPECT_TRUE(r.proposal.owners.at("net").empty());
}
TEST(ReservationDensity, UnmetAndExplicitRetirement) {
  Fixture f;
  auto    old = b::bind_reservation_density(design(), {}, f.nets, f.allocation, 1000, 1000).proposal;
  f.allocation.nets[0].candidate.reset();
  auto r = b::bind_reservation_density(design(), old, f.nets, f.allocation, 1000, 1000);
  EXPECT_EQ(r.proposal, old);
  EXPECT_EQ(r.unmet, 1u);
  f.allocation.proposal.sites.release("net");
  f.allocation.nets.clear();
  r = b::bind_reservation_density(design(), old, {}, f.allocation, 1000, 1000, {}, {"net"});
  EXPECT_TRUE(r.proposal.footprints.empty());
  EXPECT_TRUE(r.proposal.owners.empty());
}
TEST(ReservationDensity, PhysicalBindingCountsOnceAndMustRefresh) {
  Fixture f;
  auto    r = b::bind_reservation_density(design(),
                                          {
  },
                                       f.nets,
                                       f.allocation,
                                       1000,
                                       1000,
                                          {{"repair0", 0}});
  EXPECT_EQ(r.virtual_buffers, 0u);
  EXPECT_EQ(r.materialized_buffers, 1u);
  EXPECT_DOUBLE_EQ(r.materialized_area, 10000);
  f.allocation.nets[0].candidate.reset();
  EXPECT_THROW(b::bind_reservation_density(design(), r.proposal, f.nets, f.allocation, 1000, 1000), std::invalid_argument);
  EXPECT_NO_THROW(b::bind_reservation_density(design(),
                                              r.proposal,
                                              f.nets,
                                              f.allocation,
                                              1000,
                                              1000,
                                              {
                                                  {"repair0", 0}
  }));
  EXPECT_THROW(b::bind_reservation_density(design(),
                                           {
  },
                                           f.nets,
                                           f.allocation,
                                           1000,
                                           1000,
                                           {{"stale", 0}}),
               std::invalid_argument);
}
TEST(ReservationDensity, RejectsLedgerBodyAndIdentityWithoutMutation) {
  Fixture    f;
  auto       old   = b::bind_reservation_density(design(), {}, f.nets, f.allocation, 1000, 1000).proposal;
  const auto saved = old;
  f.nets[0].candidates[0].candidate.buffers[0].body.xh = 3;
  EXPECT_THROW(b::bind_reservation_density(design(), old, f.nets, f.allocation, 1000, 1000), std::invalid_argument);
  EXPECT_EQ(old, saved);
  f.nets[0].candidates[0].candidate.buffers[0].body.xh = 2.5;
  f.nets[0].candidates[0].sites[0].first               = 21;
  EXPECT_THROW(b::bind_reservation_density(design(), old, f.nets, f.allocation, 1000, 1000), std::invalid_argument);
  f.nets[0].candidates[0].sites[0].first = 20;
  f.allocation.nets[0].owner             = "other";
  EXPECT_THROW(b::bind_reservation_density(design(), old, f.nets, f.allocation, 1000, 1000), std::invalid_argument);
}
TEST(ReservationDensity, RequiredTimeIsSeparateFromCriticalReservation) {
  Fixture f;
  f.allocation.nets[0].timing->meets_required = false;
  EXPECT_NO_THROW(b::bind_reservation_density(design(), {}, f.nets, f.allocation, 1000, 1000));
  f.nets[0].classification = b::Net_class::outside_window;
  EXPECT_THROW(b::bind_reservation_density(design(), {}, f.nets, f.allocation, 1000, 1000), std::invalid_argument);
}
TEST(ReservationDensity, PayloadEntersActualNativeGp) {
  Fixture f;
  auto    d              = design();
  auto    bound          = b::bind_reservation_density(d, {}, f.nets, f.allocation, 1000, 1000);
  d.density_reservations = bound.proposal.footprints;
  dp::Options o;
  o.bins_x         = 16;
  o.bins_y         = 4;
  o.target_density = .5;
  o.max_iterations = o.min_iterations = 10;
  o.legalize = o.detailed_place = false;
  auto r                        = dp::place(d, o);
  EXPECT_NEAR(r.filler_area - r.effective_filler_area, bound.virtual_area, 1e-8);
  EXPECT_EQ(d.node_x.size(), 1u);
  EXPECT_TRUE(d.pin2node.empty());
}
#else
TEST(ReservationDensity, PublishedDependencyReportsUnsupported) {
  Fixture f;
  auto    r = b::bind_reservation_density(design(), {}, f.nets, f.allocation, 1000, 1000);
  EXPECT_FALSE(r.supported_api);
  EXPECT_TRUE(r.proposal.footprints.empty());
  EXPECT_EQ(r.virtual_buffers, 0u);
}
#endif
}  // namespace

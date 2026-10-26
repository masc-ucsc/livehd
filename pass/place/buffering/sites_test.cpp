// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sites.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <random>
#include <stdexcept>

#include "gtest/gtest.h"

namespace b = livehd::placement::buffering;
namespace {
std::vector<bool> bits(const b::Row_sites& row) {
  std::vector<bool> result((row.row.body.xh - row.row.body.xl) / row.row.pitch);
  for (const auto& gap : row.gaps) {
    for (auto i = gap.first; i < gap.first + gap.count; ++i) {
      result.at(i) = true;
    }
  }
  return result;
}
bool overlap(b::Site_rect a, b::Site_rect c) { return a.xl < c.xh && c.xl < a.xh && a.yl < c.yh && c.yl < a.yh; }
}  // namespace

TEST(BufferSites, OverlappingHalosAndNonrectangularUnionCountEachSiteOnce) {
  std::vector<b::Site_row> rows{
      {"bottom",  {0, 0, 100, 10}, 10},
      {   "top", {0, 10, 100, 20}, 10}
  };
  std::vector<b::Site_rect> masks{
      {20, 0, 40, 20},
      {30, 0, 70, 10},
      {20, 0, 40, 20}
  };
  const auto sites = b::build_legal_sites(rows, masks);
  EXPECT_EQ(bits(sites.at("bottom")), (std::vector<bool>{1, 1, 0, 0, 0, 0, 0, 1, 1, 1}));
  EXPECT_EQ(bits(sites.at("top")), (std::vector<bool>{1, 1, 0, 0, 1, 1, 1, 1, 1, 1}));
  EXPECT_EQ(sites.at("bottom").gaps.size(), 2);
  const auto body = b::site_body(sites, {"bottom", 7, 3});
  EXPECT_EQ(body.xl, 70);
  EXPECT_EQ(body.xh, 100);
  EXPECT_EQ(body.yl, 0);
  EXPECT_EQ(body.yh, 10);
}

TEST(BufferSites, MarginPartialSiteTouchingAndZeroCapacity) {
  const std::vector<b::Site_row> rows{
      {"r", {-20, -5, 30, 5}, 10}
  };
  EXPECT_EQ(bits(b::build_legal_sites(rows,
                                      {
                                          {-10, -5, 0, 5}
  })
                     .at("r")),
            (std::vector<bool>{1, 0, 1, 1, 1}));
  EXPECT_EQ(bits(b::build_legal_sites(rows,
                                      {
                                          {-10, -5, 0, 5}
  },
                                      1,
                                      1)
                     .at("r")),
            (std::vector<bool>{0, 0, 0, 1, 1}));
  EXPECT_EQ(bits(b::build_legal_sites(rows,
                                      {
                                          {0, 5, 10, 8}
  })
                     .at("r")),
            (std::vector<bool>{1, 1, 1, 1, 1}));
  EXPECT_EQ(bits(b::build_legal_sites(rows,
                                      {
                                          {1, -5, 9, 5}
  })
                     .at("r")),
            (std::vector<bool>{1, 1, 0, 1, 1}));
  const auto none = b::build_legal_sites(rows,
                                         {
                                             {-100, -100, 100, 100}
  });
  EXPECT_TRUE(none.at("r").gaps.empty());
  b::Site_reservations pool(none);
  EXPECT_EQ(pool.replace("net",
                         {
                             {"r", 0, 1}
  })
                .status,
            b::Reservation_status::unavailable);
  EXPECT_TRUE(pool.reservations().empty());
}

TEST(BufferSites, RandomUnionMatchesIndependentPhysicalFootprintOracle) {
  std::mt19937             rng(1876);
  std::vector<b::Site_row> rows;
  for (int i = 0; i < 4; ++i) {
    rows.push_back({
        std::to_string(i),
        {-30, 10 * i, 70, 10 * i + 10},
        5
    });
  }
  for (unsigned trial = 0; trial < 250; ++trial) {
    std::vector<b::Site_rect> masks;
    for (unsigned j = 0; j < 12; ++j) {
      const int64_t x = int(rng() % 130) - 50, y = int(rng() % 70) - 20;
      masks.push_back({x, y, x + 1 + int64_t(rng() % 30), y + 1 + int64_t(rng() % 20)});
    }
    const int64_t halo = rng() % 3, margin = rng() % 3, grow = halo + margin;
    const auto    sites = b::build_legal_sites(rows, masks, halo, margin);
    for (const auto& row : rows) {
      const auto actual = bits(sites.at(row.name));
      for (size_t i = 0; i < actual.size(); ++i) {
        const b::Site_rect body{row.body.xl + int64_t(i) * row.pitch,
                                row.body.yl,
                                row.body.xl + int64_t(i + 1) * row.pitch,
                                row.body.yh};
        const bool         expected = std::none_of(masks.begin(), masks.end(), [&](auto mask) {
          return overlap(body, {mask.xl - grow, mask.yl - grow, mask.xh + grow, mask.yh + grow});
        });
        ASSERT_EQ(actual[i], expected) << trial << ':' << row.name << ':' << i;
      }
    }
  }
}

TEST(BufferSites, CompetingMultiSiteBuffersAndAtomicReplacement) {
  const auto sites = b::build_legal_sites(
      {
          {"r", {0, 0, 100, 10}, 10}
  },
      {{40, 0, 50, 10}});
  b::Site_reservations pool(sites);
  EXPECT_EQ(pool.replace("critical",
                         {
                             {"r", 1, 3}
  })
                .status,
            b::Reservation_status::reserved);
  auto result = pool.replace("other",
                             {
                                 {"r", 3, 1}
  });
  EXPECT_EQ(result.status, b::Reservation_status::conflict);
  EXPECT_EQ(result.owner, "critical");
  EXPECT_EQ(pool.replace("other",
                         {
                             {"r", 5, 2}
  })
                .status,
            b::Reservation_status::reserved);
  const auto before = pool.reservations();
  // The first proposed footprint is free, but the second conflicts. Neither commits.
  EXPECT_EQ(pool.replace("critical",
                         {
                             {"r", 0, 1},
                             {"r", 6, 2}
  })
                .status,
            b::Reservation_status::conflict);
  EXPECT_EQ(pool.reservations(), before);
  EXPECT_EQ(pool.replace("critical",
                         {
                             {"r", 3, 3}
  })
                .status,
            b::Reservation_status::unavailable);
  EXPECT_EQ(pool.reservations(), before);
  pool.release("other");
  EXPECT_EQ(pool.replace("critical",
                         {
                             {"r", 5, 3}
  })
                .status,
            b::Reservation_status::reserved);
  EXPECT_EQ(pool.replace("other",
                         {
                             {"r", 1, 3}
  })
                .status,
            b::Reservation_status::reserved);
  EXPECT_EQ(pool.replace("critical", {}).status, b::Reservation_status::reserved);
  EXPECT_FALSE(pool.reservations().contains("critical"));
  pool.release("absent");
}

TEST(BufferSites, SweepHandlesDecreasingRowTopsAndExpiredMasks) {
  const std::vector<b::Site_row> rows{
      {  "tall",  {0, 0, 40, 100}, 10},
      {"middle", {50, 10, 90, 20}, 10},
      { "later", {50, 50, 90, 70}, 10}
  };
  const auto sites = b::build_legal_sites(rows,
                                          {
                                              {50, 50, 60, 60},
                                              { 0,  1, 80,  5},
                                              {60, 12, 70, 15}
  });
  EXPECT_EQ(bits(sites.at("tall")), (std::vector<bool>{0, 0, 0, 0}));
  EXPECT_EQ(bits(sites.at("middle")), (std::vector<bool>{1, 0, 1, 1}));
  EXPECT_EQ(bits(sites.at("later")), (std::vector<bool>{0, 1, 1, 1}));
}

TEST(BufferSites, CopiedSnapshotsRestoreAllOwnersAndReleaseDemand) {
  b::Site_reservations pool(b::build_legal_sites(
      {
          {"r", {0, 0, 30, 10}, 10}
  },
      {}));
  ASSERT_EQ(pool.replace("a",
                         {
                             {"r", 0, 2}
  })
                .status,
            b::Reservation_status::reserved);
  const auto retained = pool;
  pool.release("a");
  ASSERT_EQ(pool.replace("b",
                         {
                             {"r", 0, 3}
  })
                .status,
            b::Reservation_status::reserved);
  pool = retained;
  EXPECT_EQ(pool.reservations(), retained.reservations());
  EXPECT_EQ(pool.replace("b",
                         {
                             {"r", 1, 2}
  })
                .status,
            b::Reservation_status::conflict);
  EXPECT_EQ(pool.replace("b",
                         {
                             {"r", 2, 1}
  })
                .status,
            b::Reservation_status::reserved);
}

TEST(BufferSites, FreeGapsAndWidthCapacityRespectOtherOwners) {
  const auto sites = b::build_legal_sites(
      {
          {"r", {0, 0, 130, 10}, 10}
  },
      {{60, 0, 70, 10}});
  EXPECT_EQ(b::buffer_slots(sites, 4), 2);
  b::Site_reservations pool(sites);
  ASSERT_EQ(pool.replace("a",
                         {
                             {"r",  0, 2},
                             {"r", 11, 2}
  })
                .status,
            b::Reservation_status::reserved);
  ASSERT_EQ(pool.replace("b",
                         {
                             {"r", 3, 2},
                             {"r", 7, 3}
  })
                .status,
            b::Reservation_status::reserved);
  const auto free = pool.available_sites();
  EXPECT_EQ(bits(free.at("r")), (std::vector<bool>{0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0}));
  EXPECT_EQ(b::buffer_slots(free, 2), 0);
  const auto replacing = pool.available_sites("b");
  EXPECT_EQ(bits(replacing.at("r")), (std::vector<bool>{0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0}));
  EXPECT_EQ(b::buffer_slots(replacing, 4), 2);
  EXPECT_THROW(b::buffer_slots(sites, 0), std::invalid_argument);
  pool.release("a");
  pool.release("b");
  EXPECT_EQ(bits(pool.available_sites().at("r")), bits(sites.at("r")));
}

TEST(BufferSites, RandomReservationsMatchIndependentPerSiteOwners) {
  b::Site_reservations pool(b::build_legal_sites(
      {
          {"r", {0, 0, 120, 10}, 10}
  },
      {{50, 0, 60, 10}}));
  std::array<int, 12>  occupancy{};
  occupancy[5] = -1;
  std::mt19937 rng(882);
  for (unsigned trial = 0; trial < 1000; ++trial) {
    const int  owner = 1 + rng() % 4;
    const auto name  = std::to_string(owner);
    if (rng() % 6 == 0) {
      pool.release(name);
      for (auto& cell : occupancy) {
        if (cell == owner) {
          cell = 0;
        }
      }
      continue;
    }
    const unsigned first = rng() % 12, count = 1 + rng() % (12 - first);
    bool           blocked = false, conflict = false;
    for (unsigned i = first; i < first + count; ++i) {
      blocked  |= occupancy[i] == -1;
      conflict |= occupancy[i] > 0 && occupancy[i] != owner;
    }
    const auto before = pool.reservations();
    const auto actual = pool.replace(name,
                                     {
                                         {"r", first, count}
    });
    if (!blocked && !conflict) {
      EXPECT_EQ(actual.status, b::Reservation_status::reserved);
      for (auto& cell : occupancy) {
        if (cell == owner) {
          cell = 0;
        }
      }
      for (unsigned i = first; i < first + count; ++i) {
        occupancy[i] = owner;
      }
    } else {
      EXPECT_NE(actual.status, b::Reservation_status::reserved);
      EXPECT_EQ(pool.reservations(), before);
    }
    std::array<int, 12> replay{};
    replay[5] = -1;
    for (const auto& [net, spans] : pool.reservations()) {
      for (const auto& span : spans) {
        for (auto i = span.first; i < span.first + span.count; ++i) {
          ASSERT_EQ(replay[i], 0);
          replay[i] = std::stoi(net);
        }
      }
    }
    EXPECT_EQ(replay, occupancy);
  }
}

TEST(BufferSites, InvalidRowsMasksAndCompressedInventoriesReject) {
  const b::Site_row row{
      "r",
      {0, 0, 100, 10},
      10
  };
  EXPECT_THROW(b::build_legal_sites({row, row}, {}), std::invalid_argument);
  EXPECT_THROW(b::build_legal_sites(
                   {
                       row,
                       {"s", {90, 5, 120, 20}, 10}
  },
                   {}),
               std::invalid_argument);
  EXPECT_NO_THROW(b::build_legal_sites(
      {
          row,
          {"s", {100, 0, 120, 20}, 10}
  },
      {}));
  EXPECT_NO_THROW(b::build_legal_sites(
      {
          row,
          {"s", {0, 10, 120, 20}, 10}
  },
      {}));
  EXPECT_THROW(b::build_legal_sites(
                   {
                       {"r", {0, 0, 101, 10}, 10}
  },
                   {}),
               std::invalid_argument);
  EXPECT_THROW(b::build_legal_sites(
                   {
                       {"r", {0, 0, 100, 10}, 0}
  },
                   {}),
               std::invalid_argument);
  EXPECT_THROW(b::build_legal_sites(
                   {
                       row
  },
                   {{1, 1, 1, 2}}),
               std::invalid_argument);
  EXPECT_THROW(b::build_legal_sites({row}, {}, -1), std::invalid_argument);
  const auto max = std::numeric_limits<int64_t>::max(), min = std::numeric_limits<int64_t>::min();
  EXPECT_THROW(b::build_legal_sites(
                   {
                       {"r", {min, 0, max, 10}, 1}
  },
                   {}),
               std::invalid_argument);
  EXPECT_THROW(b::build_legal_sites(
                   {
                       row
  },
                   {{0, 0, max, 10}},
                   1),
               std::invalid_argument);
  EXPECT_THROW(b::build_legal_sites({row}, {}, max, 1), std::invalid_argument);
  auto sites         = b::build_legal_sites({row}, {});
  sites.at("r").gaps = {
      {"r", 5, 6}
  };
  EXPECT_THROW((b::Site_reservations{sites}), std::invalid_argument);
  sites.at("r").gaps = {
      {"r", 3, 3},
      {"r", 4, 3}
  };
  EXPECT_THROW((b::Site_reservations{sites}), std::invalid_argument);
}

TEST(BufferSites, InvalidProposalCannotAlterState) {
  const auto sites = b::build_legal_sites(
      {
          {"r", {0, 0, 100, 10}, 10}
  },
      {});
  b::Site_reservations pool(sites);
  ASSERT_EQ(pool.replace("a",
                         {
                             {"r", 0, 2}
  })
                .status,
            b::Reservation_status::reserved);
  const auto before = pool.reservations();
  EXPECT_THROW(pool.replace("a",
                            {
                                {"r", 1, 2},
                                {"r", 2, 1}
  }),
               std::invalid_argument);
  EXPECT_THROW(pool.replace("a",
                            {
                                {"r", 0, 0}
  }),
               std::invalid_argument);
  EXPECT_THROW(pool.replace("a",
                            {
                                {"r", std::numeric_limits<uint64_t>::max(), 1}
  }),
               std::invalid_argument);
  EXPECT_THROW(pool.replace("", {}), std::invalid_argument);
  EXPECT_EQ(pool.replace("a",
                         {
                             {"absent", 0, 1}
  })
                .status,
            b::Reservation_status::unavailable);
  EXPECT_EQ(pool.reservations(), before);
  EXPECT_THROW(b::site_body(sites, {"r", 9, 2}), std::invalid_argument);
}

TEST(BufferSites, LargeRowsStayCompressedAndCoordinateExtremesRemainExact) {
  const int64_t count = 1000000000000;
  auto          sites = b::build_legal_sites(
      {
          {"r", {-count, 0, count, 10}, 1}
  },
      {{-1, 0, 1, 10}});
  ASSERT_EQ(sites.at("r").gaps.size(), 2);
  EXPECT_EQ(sites.at("r").gaps.back().count, uint64_t(count - 1));
  const auto body = b::site_body(sites, {"r", uint64_t(2 * count - 2), 2});
  EXPECT_EQ(body.xh, count);
  const auto min = std::numeric_limits<int64_t>::min();
  sites          = b::build_legal_sites(
      {
          {"r", {min, 0, -1, 10}, 1}
  },
      {});
  const auto edge = b::site_body(sites, {"r", uint64_t(std::numeric_limits<int64_t>::max() - 1), 1});
  EXPECT_EQ(edge.xl, -2);
  EXPECT_EQ(edge.xh, -1);
  const auto huge = b::build_legal_sites(
      {
          {"a",  {min, 0, -1, 10}, 1},
          {"b", {min, 10, -1, 20}, 1},
          {"c", {min, 20, -1, 30}, 1}
  },
      {});
  EXPECT_THROW(b::buffer_slots(huge, 1), std::invalid_argument);
}

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Exercise the actual private regional model without relying on the public hop
// chooser to select a particular boundary domain. The test links statically so
// this translation unit supplies the SAT implementation exactly once.
#include "legroute_sat.cpp"

#include <numeric>

#include "gtest/gtest.h"

namespace livehd::placement::lr {
namespace {

struct Moved_hop_fixture {
  physical::Technology  tech;
  physical::Floorplan   fp;
  Legroute_options      options;
  Grid                  grid;
  Boxes                 boxes;
  std::vector<int>      owner_tiles;
  std::vector<Net_ends> ends{1};
  std::vector<int>      horizontal, vertical, hop_room;
  Edge_reserve          reserve;

  explicit Moved_hop_fixture(int vertical_capacity) {
    options.sat_ms          = 1000;
    options.cap_scale       = 1;
    options.density_ceiling = 1;  // unit-capacity edges: the .90 ceiling is tested on its own
    options.sat_seed        = false;
    options.tiers.clear();
    grid.cols    = 4;
    grid.rows    = 3;
    grid.regions = 2;
    grid.tile_w = grid.tile_h = 100;
    grid.region.resize(grid.size());
    grid.blocked.assign(grid.size(), 0);
    for (int t = 0; t < int(grid.size()); ++t) {
      grid.region[t] = grid.col(t) == 3 ? 1 : 0;
      if (!grid.region[t]) {
        owner_tiles.push_back(t);
      }
    }
    horizontal.assign(grid.size(), 1);
    vertical.assign(grid.size(), vertical_capacity);
    reserve.h.assign(grid.size(), 0);
    reserve.v.assign(grid.size(), 0);
    hop_room.assign(grid.size(), 0);
    hop_room[chosen()]    = 1;  // the default boundary position has no room
    ends[0].per_region[0] = {
        {-1, grid.tile(0, 0)},
        {-1,  original(),  0}
    };
    fp.nets.resize(1);
    fp.nets[0].name = "moved_hop";
  }

  int original() const { return grid.tile(2, 0); }
  int chosen() const { return grid.tile(2, 2); }

  void validate(const std::vector<Region_out::Piece>& pieces) const {
    validate_region_edges(grid, 0, pieces, reserve, horizontal, vertical, options);
  }

  Region_out solve(unsigned lazy, bool skipped = false, Clock::time_point deadline = Clock::time_point::max()) {
    options.capacity_lazy = lazy;
    Design design{tech, fp, options};
    design.nets.resize(1);
    design.nets[0].routed = true;
    design.nets[0].pins.resize(2);
    Region_out result;
    result.skipped                   = skipped;
    result.rep.sat_fail_fast_skipped = skipped ? 1 : 0;
    solve_region(design,
                 options,
                 grid,
                 0,
                 {
    },
                 owner_tiles,
                 boxes,
                 ends,
                 {},
                 {},
                 {},
                 " moved-hop",
                 horizontal,
                 vertical,
                 reserve,
                 {{original(), chosen()}},
                 hop_room,
                 result,
                 deadline);
    return result;
  }

  // Independently count actual emitted edge use, deduplicated by net. This must
  // inspect the selected hop geometry, not the candidate's original tile.
  size_t overfull_edges(const Region_out& result) const {
    std::map<std::pair<int, bool>, std::set<int>> edge_nets;
    for (const auto& piece : result.conns) {
      const int hr = grid.row(piece.h_first ? piece.ta : piece.tb);
      const int vc = grid.col(piece.h_first ? piece.tb : piece.ta);
      for (int c = std::min(grid.col(piece.ta), grid.col(piece.tb)); c < std::max(grid.col(piece.ta), grid.col(piece.tb)); ++c) {
        edge_nets[{grid.tile(c, hr), true}].insert(piece.net);
      }
      for (int r = std::min(grid.row(piece.ta), grid.row(piece.tb)); r < std::max(grid.row(piece.ta), grid.row(piece.tb)); ++r) {
        edge_nets[{grid.tile(vc, r), false}].insert(piece.net);
      }
    }
    size_t overfull = 0;
    for (const auto& [edge, nets] : edge_nets) {
      const auto [tile, h]  = edge;
      const auto& capacity  = h ? horizontal : vertical;
      const int   other     = tile + (h ? 1 : grid.cols);
      overfull             += int(nets.size()) > std::min(capacity[tile], capacity[other]);
    }
    return overfull;
  }
};

struct Coarse_witness_fixture {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  Grid                 grid;
  Boxes                boxes;
  Global_routes        routes;
  std::vector<int>     horizontal, vertical;
  std::vector<int>     pin_tiles;

  Coarse_witness_fixture() {
    options.partition_cells = 100;
    options.legal_boxes = options.clock_spine = options.region_pins = options.sat_seed = false;
    options.window_tiles                                                               = 0;
    options.sat_ms                                                                     = 1000;
    options.cap_scale                                                                  = 1;
    options.density_ceiling                                                            = 1;  // unit capacities
    options.tiers.clear();
    grid.cols    = 7;
    grid.rows    = 5;
    grid.regions = 1;
    grid.tile_w = grid.tile_h = 100;
    grid.region.assign(grid.size(), 0);
    grid.blocked.assign(grid.size(), 0);
    horizontal.assign(grid.size(), 1);
    vertical.assign(grid.size(), 1);
    routes.trees.resize(1);
    routes.trees[0].routed = true;
    // A branched Z, with two bends between its main endpoints. The home route
    // cannot be represented by one L, even though its exact witness is legal.
    const std::vector<std::vector<std::pair<int, int>>> branches{
        {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 3}, {6, 4}},
        {{2, 3}, {1, 3}, {0, 3}}
    };
    for (const auto& branch : branches) {
      for (size_t i = 1; i < branch.size(); ++i) {
        const int a = grid.tile(branch[i - 1].first, branch[i - 1].second), b = grid.tile(branch[i].first, branch[i].second);
        routes.trees[0].edges.emplace_back(std::min(a, b), std::max(a, b));
      }
    }
    pin_tiles = {grid.tile(0, 0), grid.tile(6, 4), grid.tile(0, 3), grid.tile(0, 0)};
    fp.nets.resize(1);
    fp.nets[0].name = "coarse_z_branch";
    fp.terminals.resize(pin_tiles.size());
    for (size_t i = 0; i < pin_tiles.size(); ++i) {
      fp.terminals[i].x = grid.cx(grid.col(pin_tiles[i]));
      fp.terminals[i].y = grid.cy(grid.row(pin_tiles[i]));
    }
  }

  Design design() {
    Design d{tech, fp, options};
    d.nets.resize(1);
    d.nets[0].routed = true;
    d.nets[0].driver = 0;
    for (size_t i = 0; i < pin_tiles.size(); ++i) {
      Pin pin;
      pin.term = int(i);
      d.nets[0].pins.push_back(pin);
    }
    return d;
  }

  Crossings crossing() {
    auto d = design();
    auto h = horizontal, v = vertical;
    return crossings(d, grid, boxes, {}, h, v, false, &routes);
  }

  std::set<std::pair<int, int>> emitted(const Crossings& cross) {
    std::set<std::pair<int, int>> result(cross.hops.begin(), cross.hops.end());
    auto                          d       = design();
    const auto                    reserve = edge_reserve(grid, cross.hops, cross.hop_net, {}, {}, cross.pieces);
    for (int r = 0; r < grid.regions; ++r) {
      std::vector<int> tiles;
      for (int t = 0; t < int(grid.size()); ++t) {
        if (grid.region[t] == r) {
          tiles.push_back(t);
        }
      }
      Region_out answer;
      solve_region(d,
                   options,
                   grid,
                   r,
                   {},
                   tiles,
                   boxes,
                   cross.ends,
                   {},
                   {},
                   {},
                   " identity",
                   horizontal,
                   vertical,
                   reserve,
                   {},
                   {},
                   answer,
                   Clock::time_point::max());
      EXPECT_EQ(answer.status, Sat_status::satisfiable);
      append_global_pieces(answer, r, cross.pieces);
      validate_region_edges(grid, r, answer.conns, reserve, horizontal, vertical, options);
      for (const auto& p : answer.conns) {
        const int hr = grid.row(p.h_first ? p.ta : p.tb), vc = grid.col(p.h_first ? p.tb : p.ta);
        for (int c = std::min(grid.col(p.ta), grid.col(p.tb)); c < std::max(grid.col(p.ta), grid.col(p.tb)); ++c) {
          result.emplace(grid.tile(c, hr), grid.tile(c + 1, hr));
        }
        for (int y = std::min(grid.row(p.ta), grid.row(p.tb)); y < std::max(grid.row(p.ta), grid.row(p.tb)); ++y) {
          result.emplace(grid.tile(vc, y), grid.tile(vc, y + 1));
        }
      }
    }
    return result;
  }
};

TEST(LegrouteSat, CoarseForestIdentityPreservesEveryBendBranchAndDuplicateAttachment) {
  for (unsigned lazy : {0u, 2u}) {
    Coarse_witness_fixture f;
    f.options.capacity_lazy = lazy;
    const auto cross        = f.crossing();
    ASSERT_TRUE(cross.ends[0].preserve_coarse);
    ASSERT_EQ(cross.ends[0].coarse_parts.size(), 1u);
    EXPECT_EQ(cross.ends[0].per_region.at(0).size(), 3u);  // duplicate physical endpoint shares one attachment
    EXPECT_TRUE(cross.retained_parts.empty());
    EXPECT_TRUE(std::ranges::all_of(cross.pieces, [](const auto& p) { return p.backbone; }));
    const std::set<std::pair<int, int>> expected(f.routes.trees[0].edges.begin(), f.routes.trees[0].edges.end());
    EXPECT_EQ(f.emitted(cross), expected);
  }
}

TEST(LegrouteSat, CoarseForestIdentitySeparatesReenteredRegionalPartsAndCoincidentPins) {
  Coarse_witness_fixture f;
  f.grid.regions = 2;
  for (int t = 0; t < int(f.grid.size()); ++t) {
    f.grid.region[t] = (f.grid.col(t) == 3 || f.grid.col(t) == 4) ? 1 : 0;
  }
  // A real pin coincides with the first boundary hop on the owned component.
  f.pin_tiles.push_back(f.grid.tile(2, 3));
  f.fp.terminals.push_back({});
  f.fp.terminals.back().x = f.grid.cx(2);
  f.fp.terminals.back().y = f.grid.cy(3);
  const auto cross        = f.crossing();
  EXPECT_EQ(cross.ends[0].coarse_parts.size(), 3u);
  ASSERT_EQ(cross.hops.size(), 2u);
  size_t        coincident = 0;
  std::set<int> interfaces;
  for (const auto& e : cross.ends[0].per_region.at(0)) {
    if (e.tile == f.grid.tile(2, 3)) {
      ++coincident;
    }
    if (e.interface_id >= 0) {
      interfaces.insert(e.interface_id);
    }
  }
  EXPECT_EQ(coincident, 2u);  // the real pin and crossing retain separate identities
  EXPECT_EQ(interfaces.size(), 2u);
  EXPECT_TRUE(cross.retained_parts.empty());
  const std::set<std::pair<int, int>> expected(f.routes.trees[0].edges.begin(), f.routes.trees[0].edges.end());
  EXPECT_EQ(f.emitted(cross), expected);
}

TEST(LegrouteSat, CoarseForestRejectsMissingOriginalAttachmentsBeforeSat) {
  Coarse_witness_fixture f;
  f.fp.terminals[0].x = f.grid.cx(6);
  f.fp.terminals[0].y = f.grid.cy(0);
  EXPECT_THROW(f.crossing(), std::runtime_error);
}

TEST(LegrouteSat, CoarseForestRejectsDisconnectedRoutedWitnessWithAllEndpointsPresent) {
  Coarse_witness_fixture f;
  const auto             missing = std::pair{f.grid.tile(2, 2), f.grid.tile(2, 3)};
  std::erase(f.routes.trees[0].edges, missing);
  ASSERT_TRUE(f.routes.trees[0].routed);
  for (int t : f.pin_tiles) {
    ASSERT_TRUE(std::ranges::any_of(f.routes.trees[0].edges, [&](const auto& e) { return e.first == t || e.second == t; }));
  }
  try {
    f.crossing();
    FAIL() << "disconnected routed=true witness must not produce independently SAT fragments";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("disconnected original geometry"), std::string::npos);
  }
}

TEST(LegrouteSat, AdoptedAlternativeIncludesItsStillReservedCoarseBackbone) {
  Coarse_witness_fixture f;
  auto                   cross  = f.crossing();
  // Exercise the retained-backbone reference policy. The production free
  // corridor policy emits its chosen edges itself and must not append a seed.
  cross.ends[0].refine_corridor = false;
  for (auto& piece : cross.pieces) {
    piece.refinable = false;
  }
  auto             design  = f.design();
  const auto       reserve = edge_reserve(f.grid, cross.hops, cross.hop_net, {}, {}, cross.pieces);
  std::vector<int> tiles;
  for (int t = 0; t < int(f.grid.size()); ++t) {
    tiles.push_back(t);
  }
  Region_out alternative;
  solve_region(design,
               f.options,
               f.grid,
               0,
               {},
               tiles,
               f.boxes,
               cross.ends,
               {},
               {},
               {},
               " actual alternative",
               f.horizontal,
               f.vertical,
               reserve,
               {},
               {},
               alternative,
               Clock::time_point::max());
  ASSERT_EQ(alternative.status, Sat_status::satisfiable);
  ASSERT_FALSE(cross.pieces.empty());
  ASSERT_FALSE(contains_global_piece(alternative, cross.pieces.front()))
      << "the bare alternative solver deliberately emits stubs only";
  Region_out selected;
  adopt_region_alternative(selected, std::move(alternative), 0, cross.pieces);
  for (const auto& piece : cross.pieces) {
    EXPECT_TRUE(contains_global_piece(selected, piece));
  }
  const size_t count = selected.conns.size();
  append_global_pieces(selected, 0, cross.pieces);
  EXPECT_EQ(selected.conns.size(), count);  // primary/adoption attachment is idempotent
  std::vector<Global_piece> actual;
  for (const auto& piece : selected.conns) {
    actual.push_back({piece.net, 0, piece.ta, piece.tb, piece.h_first});
  }
  const auto emitted = edge_reserve(f.grid, {}, {}, {}, {}, actual);
  EXPECT_EQ(emitted.edges, reserve.edges);
  EXPECT_NO_THROW(validate_region_edges(f.grid, 0, selected.conns, reserve, f.horizontal, f.vertical, f.options));
}

TEST(LegrouteSat, CoarseStubsDoNotInventEdgesBetweenDisjointSameNetPieces) {
  for (unsigned lazy : {0u, 1u, 2u}) {
    Moved_hop_fixture f(1);
    f.options.window_tiles = 1;
    f.grid.regions         = 1;
    f.grid.region.assign(f.grid.size(), 0);
    f.owner_tiles.clear();
    for (int t = 0; t < int(f.grid.size()); ++t) {
      f.owner_tiles.push_back(t);
    }
    f.ends[0].preserve_coarse = true;
    f.ends[0].coarse_parts    = {
        {{0, 0}, {f.grid.tile(1, 0)}},
        {{0, 1}, {f.grid.tile(2, 0)}}
    };
    f.ends[0].per_region[0] = {
        {-1, f.grid.tile(0, 0), -1, 0, f.grid.tile(1, 0)},
        {-1, f.grid.tile(3, 0), -1, 1, f.grid.tile(2, 0)}
    };
    // Another physical net owns the middle edge. Tile-cover intersection would
    // invent a net-0 edge between the two separate stubs and make this UNSAT.
    f.reserve.h[f.grid.tile(1, 0)] = 1;
    f.reserve.edges.emplace(1, f.grid.tile(1, 0), 1);
    const auto result = f.solve(lazy);
    ASSERT_EQ(result.status, Sat_status::satisfiable);
    ASSERT_EQ(result.conns.size(), 2u);
    EXPECT_EQ(f.overfull_edges(result), 0u);
    for (const auto& p : result.conns) {
      EXPECT_EQ(f.grid.row(p.ta), 0);
      EXPECT_EQ(f.grid.row(p.tb), 0);
      EXPECT_EQ(std::abs(f.grid.col(p.ta) - f.grid.col(p.tb)), 1);
      EXPECT_FALSE(std::min(f.grid.col(p.ta), f.grid.col(p.tb)) == 1);
    }
  }
}

TEST(LegrouteSat, CoarseStubSharesItsOwnBackboneButCannotLeaveTheBoundedCorridor) {
  for (unsigned lazy : {0u, 1u, 2u}) {
    for (unsigned margin : {1u, 2u}) {
      Moved_hop_fixture f(1);
      f.options.window_tiles         = margin;
      f.ends[0].preserve_coarse      = true;
      f.ends[0].coarse_parts[{0, 0}] = {f.grid.tile(0, 0), f.grid.tile(1, 0), f.original()};
      f.ends[0].per_region[0]        = {
          {-1, f.grid.tile(0, 0), -1, 0, f.grid.tile(0, 0)},
          {-1, f.original(), 0, 0, f.original(), 0}
      };
      for (int c = 0; c < 2; ++c) {
        f.reserve.h[f.grid.tile(c, 0)] = 1;
        f.reserve.edges.emplace(0, f.grid.tile(c, 0), 1);
      }
      const auto answer = f.solve(lazy);
      EXPECT_EQ(answer.status, margin == 1 ? Sat_status::unsatisfiable : Sat_status::satisfiable);
      if (margin == 2) {
        EXPECT_EQ(answer.hop_choices.at(0), f.chosen());
        EXPECT_EQ(f.overfull_edges(answer), 0u);
        EXPECT_NO_THROW(f.validate(answer.conns));
      } else {
        EXPECT_TRUE(answer.conns.empty());
      }
    }
    Moved_hop_fixture f(1);
    f.options.window_tiles         = 0;
    f.ends[0].preserve_coarse      = true;
    f.ends[0].coarse_parts[{0, 0}] = {f.grid.tile(0, 0), f.grid.tile(1, 0), f.original()};
    f.ends[0].per_region[0]        = {
        {-1, f.grid.tile(0, 0), -1, 0, f.original()}
    };
    for (int c = 0; c < 2; ++c) {
      f.reserve.h[f.grid.tile(c, 0)] = 1;
      f.reserve.edges.emplace(0, f.grid.tile(c, 0), 1);
    }
    const auto shared = f.solve(lazy);
    ASSERT_EQ(shared.status, Sat_status::satisfiable);
    ASSERT_EQ(shared.conns.size(), 1u);
    EXPECT_NO_THROW(f.validate(shared.conns));
    f.reserve.edges.clear();  // identical geometry belonging to an outside net is not free
    EXPECT_EQ(f.solve(lazy).status, Sat_status::unsatisfiable);
  }
}

TEST(LegrouteSat, NearbyBackboneJunctionMakesAForcedMovedAttachmentCapacityFeasible) {
  for (unsigned lazy : {0u, 1u, 2u}) {
    SCOPED_TRACE(lazy);
    Moved_hop_fixture f(0);
    f.options.window_tiles = 1;
    std::ranges::fill(f.horizontal, 0);
    const std::vector<int> path{f.original(), f.grid.tile(1, 0), f.grid.tile(1, 1), f.grid.tile(2, 1), f.chosen()};
    f.ends[0].preserve_coarse      = true;
    f.ends[0].coarse_parts[{0, 0}] = {path.begin(), path.end()};
    f.ends[0].per_region[0]        = {
        {-1, f.chosen(), -1, 0, f.chosen()},
        {-1, f.original(), 0, 0, f.original(), 0}
    };
    for (size_t i = 1; i < path.size(); ++i) {
      const int  a = std::min(path[i - 1], path[i]), b = std::max(path[i - 1], path[i]);
      const bool h        = f.grid.row(a) == f.grid.row(b);
      auto&      capacity = h ? f.horizontal : f.vertical;
      capacity[a] = capacity[b]          = 1;
      (h ? f.reserve.h : f.reserve.v)[a] = 1;
      f.reserve.edges.emplace(0, a, int(h));
    }
    // The original fixed anchor forces a straight V stub through a zero-capacity
    // edge. The existing bent backbone is legal; its nearby bend is already an
    // emitted graph node and provides a shared, fully paid attachment instead.
    const auto original_only = f.solve(lazy);
    EXPECT_EQ(original_only.status, Sat_status::unsatisfiable);
    EXPECT_TRUE(original_only.conns.empty());
    f.ends[0].coarse_junctions[{0, 0}] = {path.begin(), path.end()};
    const auto answer                  = f.solve(lazy);
    ASSERT_EQ(answer.status, Sat_status::satisfiable);
    EXPECT_EQ(answer.hop_choices.at(0), f.chosen());
    EXPECT_EQ(answer.rep.coarse_attachment_choices, 1u);
    EXPECT_EQ(answer.rep.coarse_attachments_changed, 1u);
    EXPECT_EQ(f.overfull_edges(answer), 0u);
    EXPECT_NO_THROW(f.validate(answer.conns));
    ASSERT_EQ(answer.conns.size(), 2u);
    EXPECT_EQ(answer.conns[1].tb, f.grid.tile(2, 1));
    // Independent exact-edge check: the improvement creates no unpaid route
    // demand and does not discard any part of the retained coarse witness.
    for (const auto& piece : answer.conns) {
      const int hr = f.grid.row(piece.h_first ? piece.ta : piece.tb);
      const int vc = f.grid.col(piece.h_first ? piece.tb : piece.ta);
      for (int x = std::min(f.grid.col(piece.ta), f.grid.col(piece.tb)); x < std::max(f.grid.col(piece.ta), f.grid.col(piece.tb));
           ++x) {
        EXPECT_TRUE(f.reserve.edges.contains({piece.net, f.grid.tile(x, hr), 1}));
      }
      for (int y = std::min(f.grid.row(piece.ta), f.grid.row(piece.tb)); y < std::max(f.grid.row(piece.ta), f.grid.row(piece.tb));
           ++y) {
        EXPECT_TRUE(f.reserve.edges.contains({piece.net, f.grid.tile(vc, y), 0}));
      }
    }
  }
}

TEST(LegrouteSat, BackboneAnchorCandidatesAreBoundedAndKeepTheOriginalWitness) {
  Coarse_witness_fixture f;
  std::set<int>          tiles;
  for (int t = 0; t < int(f.grid.size()); ++t) {
    tiles.insert(t);
  }
  const int  original   = f.grid.tile(6, 4);
  const auto candidates = coarse_anchor_candidates(f.grid, tiles, tiles, original, 100);
  ASSERT_EQ(candidates.size(), 8u);
  EXPECT_EQ(candidates.front(), original);
  EXPECT_EQ(coarse_anchor_candidates(f.grid, tiles, tiles, original, 0), std::vector<int>{original});
  EXPECT_EQ(coarse_anchor_candidates(f.grid, tiles, tiles, original, 1),
            (std::vector<int>{original, f.grid.tile(6, 3), f.grid.tile(5, 4)}));
  auto invalid = tiles;
  invalid.insert(int(f.grid.size()));
  EXPECT_THROW(coarse_anchor_candidates(f.grid, tiles, invalid, original, 1), std::runtime_error);
  // Only actual emitted endpoints/bends may become anchors. Straight-run
  // interior vertices are not silently introduced into the downstream graph.
  const auto cross = f.crossing();
  for (const auto& [key, nodes] : cross.ends[0].coarse_junctions) {
    for (int tile : nodes) {
      EXPECT_TRUE(std::ranges::find(f.pin_tiles, tile) != f.pin_tiles.end()
                  || std::ranges::any_of(cross.pieces, [&](const auto& piece) {
                       return piece.region == key.first && piece.part == key.second && (piece.a == tile || piece.b == tile);
                     }));
    }
  }
  EXPECT_EQ(f.emitted(cross), (std::set<std::pair<int, int>>(f.routes.trees[0].edges.begin(), f.routes.trees[0].edges.end())));
}

TEST(LegrouteSat, UnsupportedHomeStubDoesNotRejectASupportedLegalEndpointDomain) {
  Moved_hop_fixture f(1);
  f.options.window_tiles         = 0;
  f.ends[0].preserve_coarse      = true;
  // The old interface position is outside this singleton component, but the
  // actual selectable boundary domain contains its legal, forced new position.
  f.ends[0].coarse_parts[{0, 0}] = {f.chosen()};
  f.ends[0].per_region[0]        = {
      {-1, f.original(), 0, 0, f.chosen(), 0}
  };
  const auto answer = f.solve(2);
  ASSERT_EQ(answer.status, Sat_status::satisfiable);
  EXPECT_EQ(answer.hop_choices.at(0), f.chosen());
  ASSERT_EQ(answer.conns.size(), 1u);
  EXPECT_EQ(answer.conns[0].ta, f.chosen());
  EXPECT_EQ(answer.conns[0].tb, f.chosen());
  EXPECT_NO_THROW(f.validate(answer.conns));
  // Once every possible endpoint position is geometrically unsupported, this
  // is a named encoding/route-repair blocker, not an inferred area limit.
  f.ends[0].per_region[0][0].hop = -1;
  try {
    (void)f.solve(2);
    FAIL() << "unsupported fixed endpoint unexpectedly accepted";
  } catch (const Legroute_unfit&) {
    FAIL() << "corridor representation failure must not request area growth";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("coarse stub construction blocker"), std::string::npos);
  }
}

TEST(LegrouteSat, SharedInterfaceRecountUsesBothSidesAndDistinctNetIdentity) {
  Moved_hop_fixture f(1);
  const int         a = f.grid.tile(2, 0), b = f.grid.tile(3, 0);
  f.horizontal.assign(f.grid.size(), 3);
  EXPECT_NO_THROW(validate_crossing_edges(f.grid,
                                          {
                                              {a, b},
                                              {b, a}
  },
                                          {0, 0},
                                          f.horizontal,
                                          f.vertical,
                                          .5));
  EXPECT_THROW(validate_crossing_edges(f.grid,
                                       {
                                           {a, b},
                                           {b, a}
  },
                                       {0, 1},
                                       f.horizontal,
                                       f.vertical,
                                       .5),
               std::runtime_error);
  EXPECT_NO_THROW(validate_crossing_edges(f.grid,
                                          {
                                              {a, b},
                                              {b, a}
  },
                                          {0, 1},
                                          f.horizontal,
                                          f.vertical,
                                          .95));
  f.horizontal[b] = 1;
  EXPECT_THROW(validate_crossing_edges(f.grid,
                                       {
                                           {a, b}
  },
                                       {0},
                                       f.horizontal,
                                       f.vertical,
                                       .95),
               std::runtime_error);
  // the narrower endpoint under the .90 ceiling: a 20/10 edge admits 9 distinct nets, not 10
  f.horizontal[a] = 20;
  f.horizontal[b] = 10;
  std::vector<std::pair<int, int>> ten(10, {a, b});
  std::vector<int>                 nets(10);
  std::iota(nets.begin(), nets.end(), 0);
  EXPECT_THROW(validate_crossing_edges(f.grid, ten, nets, f.horizontal, f.vertical, 1), std::runtime_error);
  ten.pop_back();
  nets.pop_back();
  EXPECT_NO_THROW(validate_crossing_edges(f.grid, ten, nets, f.horizontal, f.vertical, 1));
  // A large valid experiment scale still admits the same nine tracks under the ceiling.
  EXPECT_NO_THROW(validate_crossing_edges(f.grid, ten, nets, f.horizontal, f.vertical, 1, std::numeric_limits<double>::max()));
  // cap_scale .8 keeps 8 of 10, under the ceiling
  EXPECT_THROW(validate_crossing_edges(f.grid, ten, nets, f.horizontal, f.vertical, 1, .8), std::runtime_error);
}

TEST(LegrouteSat, MovedHopCannotHideAnOverfullEdgeFromLazyCapacity) {
  for (unsigned lazy : {0u, 1u, 2u}) {
    SCOPED_TRACE(lazy);
    Moved_hop_fixture fixture(0);
    const auto        result = fixture.solve(lazy);
    EXPECT_EQ(result.status, Sat_status::unsatisfiable);
    EXPECT_EQ(fixture.overfull_edges(result), 0u);
    EXPECT_TRUE(result.conns.empty());
    EXPECT_TRUE(result.hop_tiles.empty());
    EXPECT_GT(result.rep.sat_solves, 0u);  // exercised the model, not a pre-SAT cut rejection
  }
}

TEST(LegrouteSat, EagerAndLazyCapacityUseTheSameForcedMovedHop) {
  for (unsigned lazy : {0u, 1u, 2u}) {
    SCOPED_TRACE(lazy);
    Moved_hop_fixture fixture(1);
    const auto        result = fixture.solve(lazy);
    ASSERT_EQ(result.status, Sat_status::satisfiable);
    ASSERT_EQ(result.hop_tiles.size(), 1u);
    EXPECT_EQ(result.hop_tiles[0], (std::tuple{0, fixture.original(), fixture.chosen()}));
    ASSERT_EQ(result.conns.size(), 1u);
    EXPECT_TRUE(result.conns[0].ta == fixture.chosen() || result.conns[0].tb == fixture.chosen());
    EXPECT_EQ(fixture.overfull_edges(result), 0u);
  }
}

TEST(LegrouteSat, DecodedEdgeGuardDeduplicatesReversedSameNetPiecesOnly) {
  Moved_hop_fixture                    fixture(1);
  const int                            a = fixture.grid.tile(0, 0), b = fixture.chosen();
  // Reversing an L also reverses its first direction to preserve the geometry.
  const std::vector<Region_out::Piece> same_net{
      {0, a, b,  true},
      {0, b, a, false},
      {0, a, b,  true}
  };
  EXPECT_NO_THROW(fixture.validate(same_net));
  auto distinct_net   = same_net;
  distinct_net[1].net = 1;
  EXPECT_THROW(fixture.validate(distinct_net), std::runtime_error);
}

TEST(LegrouteSat, DecodedEdgeGuardCreditsOnlyExactReservedNetEdges) {
  Moved_hop_fixture fixture(1);
  const int         a = fixture.grid.tile(0, 0), b = fixture.grid.tile(1, 0);
  fixture.reserve.h[a] = 1;
  fixture.reserve.edges.emplace(0, a, 1);
  EXPECT_NO_THROW(fixture.validate({}));
  EXPECT_NO_THROW(fixture.validate({
      {0, a, b, true},
      {0, b, a, true}
  }));
  EXPECT_THROW(fixture.validate({
                   {1, a, b, true}
  }),
               std::runtime_error);
  // Counts without matching ownership cannot be treated as shared metal.
  fixture.reserve.edges.clear();
  EXPECT_THROW(fixture.validate({
                   {0, a, b, true}
  }),
               std::runtime_error);
  fixture.reserve.h[a] = 2;
  EXPECT_THROW(fixture.validate({}), std::runtime_error);
}

TEST(LegrouteSat, DecodedEdgeGuardChecksMeshInteriorAndZeroLengthEndpoints) {
  Moved_hop_fixture fixture(1);
  const int         a = fixture.grid.tile(0, 0), b = fixture.chosen();
  fixture.grid.region[fixture.grid.tile(1, 0)] = 1;
  EXPECT_THROW(fixture.validate({
                   {0, a, b, true}
  }),
               std::runtime_error);
  EXPECT_NO_THROW(fixture.validate({
      {0, a, b, false}
  }));
  const int foreign = fixture.grid.tile(3, 0);
  EXPECT_THROW(fixture.validate({
                   {0, foreign, foreign, true}
  }),
               std::runtime_error);
}

TEST(LegrouteSat, DecodedEdgeGuardUsesNarrowerEndpointAndScaledFloor) {
  Moved_hop_fixture fixture(1);
  const int         a = fixture.grid.tile(0, 0), b = fixture.grid.tile(1, 0);
  fixture.options.cap_scale = .5;
  fixture.horizontal[a] = fixture.horizontal[b] = 2;
  EXPECT_NO_THROW(fixture.validate({
      {0, a, b, true}
  }));
  fixture.horizontal[b] = 1;
  EXPECT_THROW(fixture.validate({
                   {0, a, b, true}
  }),
               std::runtime_error);
}

TEST(LegrouteSat, UntrackedDecodedEdgesStillMustStayInsideTheMesh) {
  Moved_hop_fixture fixture(0);
  std::ranges::fill(fixture.horizontal, 0);
  const int a = fixture.grid.tile(0, 0), b = fixture.original();
  EXPECT_THROW(fixture.validate({
                   {0, a, b, true}
  }),
               std::runtime_error);
  const std::vector<char> untracked(fixture.grid.size(), 1);
  fixture.options.untracked = &untracked;
  EXPECT_NO_THROW(fixture.validate({
      {0, a, b, true}
  }));
  fixture.grid.region[fixture.grid.tile(1, 0)] = 1;
  EXPECT_THROW(fixture.validate({
                   {0, a, b, true}
  }),
               std::runtime_error);
}

TEST(LegrouteSat, RemainingDeadlineDoesNotGrantOneMillisecondAfterExpiry) {
  using namespace std::chrono;
  const Clock::time_point start{};
  const Region_budget     budget{start, start + milliseconds(1000)};
  EXPECT_EQ(budget.remaining_ms(start + milliseconds(900)), 100u);
  EXPECT_EQ(budget.remaining_ms(start + microseconds(999500)), 0u);
  EXPECT_EQ(budget.remaining_ms(start + milliseconds(1000)), 0u);
  EXPECT_EQ(budget.remaining_ms(start + milliseconds(1100)), 0u);
}

TEST(LegrouteSat, CnfConstructionChecksTheSharedDeadlineBeforeSolving) {
  const auto    start = Clock::now();
  Region_budget budget{start, start + std::chrono::seconds(60)};
  Cnf           cnf(budget);
  const int     initial = cnf.var();
  cnf.add({initial});
  EXPECT_GT(cnf.variables, 1u);
  EXPECT_GT(cnf.clauses, 0u);
  budget.deadline     = Clock::time_point{};
  unsigned operations = 0;
  bool     expired    = false;
  try {
    for (; operations < 256; ++operations) {
      if (operations % 2) {
        cnf.add({initial});
      } else {
        cnf.var();
      }
    }
  } catch (const Region_budget_expired&) {
    expired = true;
  }
  EXPECT_TRUE(expired);
  EXPECT_LT(operations, 256u);
  // No solver invocation: expiry must interrupt actual model construction.
}

TEST(LegrouteSat, ExpiredFailFastSkipRemainsSkippedWithoutBuildingAModel) {
  Moved_hop_fixture fixture(1);
  const auto        result = fixture.solve(2, true, Clock::time_point{});
  EXPECT_EQ(result.status, Sat_status::unknown);
  EXPECT_TRUE(result.skipped);
  EXPECT_EQ(result.rep.sat_solves, 0u);
  EXPECT_EQ(result.rep.sat_variables, 0u);
  EXPECT_EQ(result.rep.sat_skipped_regions, 1u);
  EXPECT_EQ(result.rep.sat_fail_fast_skipped, 1u);
  EXPECT_EQ(result.rep.sat_unknown_regions, 0u);
  EXPECT_TRUE(result.conns.empty());
  EXPECT_TRUE(result.hop_tiles.empty());
}

// Independent finite-graph fixtures exercise the actual connectivity-cut
// helper. Their oracle walks explicit selected edges and never uses the SAT
// decoder or its connectivity implementation.
using Oracle_edge = std::pair<int, int>;

std::set<int> oracle_reachable(int source, const std::set<Oracle_edge>& edges) {
  std::set<int> reached{source};
  bool          changed = true;
  while (changed) {
    changed = false;
    for (const auto& [a, b] : edges) {
      if (reached.contains(a)) {
        changed |= reached.insert(b).second;
      }
      if (reached.contains(b)) {
        changed |= reached.insert(a).second;
      }
    }
  }
  return reached;
}

struct Corridor_oracle_fixture {
  Region_budget                            budget{Clock::now(), Clock::now() + std::chrono::seconds(20)};
  Cnf                                      cnf{budget};
  std::vector<Corridor_model>              models;
  std::map<std::tuple<int, int, int>, int> edge_literals;
  size_t                                   cuts = 0;

  std::vector<std::pair<int, int>> domain(const std::vector<int>& tiles) {
    std::vector<std::pair<int, int>> result;
    std::vector<int>                 literals;
    for (int tile : tiles) {
      const int lit = cnf.var();
      result.emplace_back(tile, lit);
      literals.push_back(lit);
    }
    cnf.add(literals);
    cnf.at_most(literals, 1);
    return result;
  }

  int edge(int net, int a, int b) {
    if (a > b) {
      std::swap(a, b);
    }
    auto [entry, fresh] = edge_literals.try_emplace({net, a, b}, 0);
    if (fresh) {
      entry->second = cnf.var();
    }
    return entry->second;
  }

  void model(int net, const std::vector<Oracle_edge>& edges, std::vector<std::vector<std::pair<int, int>>> terminals) {
    Corridor_model result;
    result.net       = net;
    result.part      = int(models.size());
    result.terminals = std::move(terminals);
    for (auto [a, b] : edges) {
      if (a > b) {
        std::swap(a, b);
      }
      result.edges.emplace_back(a, b, edge(net, a, b));
      result.tiles.insert(a);
      result.tiles.insert(b);
    }
    for (const auto& endpoint : result.terminals) {
      for (const auto& [tile, literal] : endpoint) {
        (void)literal;
        result.tiles.insert(tile);
      }
    }
    models.push_back(std::move(result));
  }

  Sat_status solve(const std::vector<int>& assumptions = {}) {
    for (unsigned round = 0; round < 256; ++round) {
      const auto status = cnf.solver.solve(assumptions, 1000000, 1000);
      if (status != Sat_status::satisfiable) {
        return status;
      }
      const auto added  = add_corridor_connectivity_cuts(cnf, models, budget);
      cuts             += added;
      if (!added) {
        return status;
      }
    }
    throw std::runtime_error("small corridor oracle did not settle");
  }
};

TEST(LegrouteSat, CorridorConditionalCutsPreserveEveryConnectedEndpointAndEdgeAssignment) {
  Corridor_oracle_fixture        f;
  const std::vector<Oracle_edge> edges{
      {0, 1},
      {1, 2},
      {3, 4},
      {4, 5},
      {0, 3},
      {1, 4},
      {2, 5}
  };
  const auto roots = f.domain({0, 3}), leaves = f.domain({2, 5});
  f.model(0, edges, {roots, leaves});
  std::vector<unsigned> assignments(4 * (1u << edges.size()));
  std::iota(assignments.begin(), assignments.end(), 0u);
  std::mt19937 random(63);
  std::shuffle(assignments.begin(), assignments.end(), random);
  size_t valid = 0, invalid = 0;
  for (unsigned assignment : assignments) {
    SCOPED_TRACE(assignment);
    const unsigned        choices = assignment >> edges.size(), mask = assignment & ((1u << edges.size()) - 1);
    const auto            root = roots[choices & 1], leaf = leaves[(choices >> 1) & 1];
    std::set<Oracle_edge> chosen;
    std::vector<int>      assumptions{root.second, leaf.second};
    for (size_t e = 0; e < edges.size(); ++e) {
      const int literal = f.edge(0, edges[e].first, edges[e].second);
      assumptions.push_back(mask & (1u << e) ? literal : -literal);
      if (mask & (1u << e)) {
        chosen.insert(edges[e]);
      }
    }
    const bool connected = oracle_reachable(root.first, chosen).contains(leaf.first);
    const auto status    = f.solve(assumptions);
    ASSERT_EQ(status, connected ? Sat_status::satisfiable : Sat_status::unsatisfiable);
    if (connected) {
      ++valid;
      const auto decoded = decode_corridor_edges(f.cnf, f.models[0], f.budget);
      EXPECT_TRUE(std::includes(chosen.begin(), chosen.end(), decoded.begin(), decoded.end()));
      EXPECT_TRUE(oracle_reachable(root.first, decoded).contains(leaf.first));
    } else {
      ++invalid;
    }
  }
  EXPECT_GT(valid, 0u);
  EXPECT_GT(invalid, 0u);
  EXPECT_GT(f.cuts, 0u);
}

TEST(LegrouteSat, CorridorAcceptsArbitraryBendsAndPreservesZeroMarginIdentity) {
  Corridor_oracle_fixture        f;
  const std::vector<Oracle_edge> path{
      {0, 3},
      {3, 6},
      {6, 7},
      {7, 8},
      {5, 8},
      {2, 5}
  };
  f.model(0, path, {{{0, f.cnf.t}}, {{2, f.cnf.t}}});
  ASSERT_EQ(f.solve(), Sat_status::satisfiable);
  const std::set<Oracle_edge> expected(path.begin(), path.end());
  EXPECT_EQ(decode_corridor_edges(f.cnf, f.models[0], f.budget), expected);
  f.models[0].seed     = expected;
  f.models[0].identity = true;
  EXPECT_EQ(decode_corridor_edges(f.cnf, f.models[0], f.budget), expected);
  // The accepted path bends three times; neither endpoint-to-endpoint L is
  // contained in this mesh, so this is not another bounded L-template test.
}

TEST(LegrouteSat, CorridorCanonicalCutDeduplicatesBothSidesAndCoincidentFixedEndpoints) {
  Corridor_oracle_fixture                       f;
  std::vector<std::vector<std::pair<int, int>>> endpoints;
  for (int i = 0; i < 8; ++i) {
    endpoints.push_back({
        {0, f.cnf.t}
    });
    endpoints.push_back({
        {3, f.cnf.t}
    });
  }
  f.model(0,
          {
              {0, 1},
              {1, 2},
              {2, 3}
  },
          endpoints);
  const auto             variables = f.cnf.variables, clauses = f.cnf.clauses;
  const std::vector<int> separated{f.edge(0, 0, 1), -f.edge(0, 1, 2), f.edge(0, 2, 3)};
  ASSERT_EQ(f.cnf.solver.solve(separated, 100000, 1000), Sat_status::satisfiable);
  EXPECT_EQ(add_corridor_connectivity_cuts(f.cnf, f.models, f.budget), 1u);
  EXPECT_EQ(f.models[0].connectivity_cuts.size(), 1u);
  EXPECT_EQ(f.cnf.variables, variables) << "fixed membership needs no auxiliary variables";
  EXPECT_EQ(f.cnf.clauses, clauses + 1) << "the cut and its complement are one constraint";
  ASSERT_EQ(f.solve(), Sat_status::satisfiable);
  EXPECT_TRUE(f.cnf.solver.value(f.edge(0, 1, 2)));
  EXPECT_EQ(add_corridor_connectivity_cuts(f.cnf, f.models, f.budget), 0u);
}

TEST(LegrouteSat, CorridorMembershipCutRemainsValidWhenBothEndpointsMoveAcrossTheCut) {
  Corridor_oracle_fixture f;
  const auto              roots = f.domain({0, 2}), leaves = f.domain({1, 3});
  f.model(0,
          {
              {0, 1},
              {1, 2},
              {2, 3}
  },
          {roots, leaves});
  const std::vector<int> split{roots[0].second, leaves[1].second, f.edge(0, 0, 1), -f.edge(0, 1, 2), f.edge(0, 2, 3)};
  ASSERT_EQ(f.cnf.solver.solve(split, 100000, 1000), Sat_status::satisfiable);
  EXPECT_EQ(add_corridor_connectivity_cuts(f.cnf, f.models, f.budget), 1u);
  EXPECT_EQ(f.cnf.solver.solve(split, 100000, 1000), Sat_status::unsatisfiable);
  for (unsigned side : {0u, 1u}) {
    const std::vector<int> same_side{roots[side].second, leaves[side].second, f.edge(0, 0, 1), -f.edge(0, 1, 2), f.edge(0, 2, 3)};
    EXPECT_EQ(f.solve(same_side), Sat_status::satisfiable);
  }
  EXPECT_EQ(f.models[0].connectivity_cuts.size(), 1u);
}

TEST(LegrouteSat, CorridorPartsSharePhysicalNetEdgesButCannotBorrowOutsideTheirGraph) {
  Corridor_oracle_fixture f;
  f.model(0,
          {
              {0, 1},
              {1, 2}
  },
          {{{0, f.cnf.t}}, {{2, f.cnf.t}}});
  f.model(0,
          {
              {1, 2},
              {2, 3}
  },
          {{{1, f.cnf.t}}, {{3, f.cnf.t}}});
  ASSERT_EQ(f.edge_literals.size(), 3u);  // shared branch has one physical net literal
  ASSERT_EQ(f.solve(), Sat_status::satisfiable);
  std::set<Oracle_edge> union_edges;
  for (const auto& model : f.models) {
    const auto decoded = decode_corridor_edges(f.cnf, model, f.budget);
    union_edges.insert(decoded.begin(), decoded.end());
  }
  EXPECT_EQ(union_edges,
            (std::set<Oracle_edge>{
                {0, 1},
                {1, 2},
                {2, 3}
  }));

  Corridor_oracle_fixture scoped;
  scoped.model(0,
               {
                   {0, 1},
                   {1, 2}
  },
               {{{0, scoped.cnf.t}}, {{2, scoped.cnf.t}}});
  scoped.model(0,
               {
                   {0, 3},
                   {3, 4},
                   {4, 5},
                   {2, 5}
  },
               {{{0, scoped.cnf.t}}, {{2, scoped.cnf.t}}});
  scoped.cnf.add({-scoped.edge(0, 1, 2)});
  EXPECT_EQ(scoped.solve(), Sat_status::unsatisfiable)
      << "the other part's external route must not satisfy this part's connectivity";
}

TEST(LegrouteSat, CorridorSharedInterfaceHasOneConsistentChoiceForBothSides) {
  for (bool open_first_crossing : {false, true}) {
    Corridor_oracle_fixture                f;
    const auto                             left = f.domain({0, 2});
    const std::vector<std::pair<int, int>> right{
        {1, left[0].second},
        {3, left[1].second}
    };
    f.model(0,
            {
                {0, 4},
                {2, 4}
    },
            {{{4, f.cnf.t}}, left});
    f.model(0,
            {
                {1, 5},
                {3, 5}
    },
            {{{5, f.cnf.t}}, right});
    f.cnf.add({-f.edge(0, 3, 5)});
    if (!open_first_crossing) {
      f.cnf.add({-f.edge(0, 0, 4)});
    }
    ASSERT_EQ(f.solve(), open_first_crossing ? Sat_status::satisfiable : Sat_status::unsatisfiable);
    if (open_first_crossing) {
      EXPECT_TRUE(f.cnf.solver.value(left[0].second));
      EXPECT_FALSE(f.cnf.solver.value(left[1].second));
    }
  }
}

TEST(LegrouteSat, FreeCorridorCanReplaceAnUnusableSeedAfterAnEndpointMoves) {
  for (unsigned lazy : {0u, 1u, 2u}) {
    SCOPED_TRACE(lazy);
    Moved_hop_fixture f(0);
    f.options.window_tiles         = 2;
    f.ends[0].preserve_coarse      = true;
    f.ends[0].coarse_parts[{0, 0}] = {f.grid.tile(0, 0), f.grid.tile(1, 0), f.original()};
    f.ends[0].coarse_edges[{
        0,
        0
    }]                        = {{f.grid.tile(0, 0), f.grid.tile(1, 0)}, {f.grid.tile(1, 0), f.original()}};
    f.ends[0].per_region[0]        = {
        {-1, f.grid.tile(0, 2), -1, 0, f.grid.tile(0, 0)},
        {-1, f.original(), 0, 0, f.original(), 0}
    };
    // No vertical resource exists. Retaining the bottom-row anchors makes
    // attachment impossible, although moving the whole path to row two works.
    EXPECT_EQ(f.solve(lazy).status, Sat_status::unsatisfiable);
    f.ends[0].refine_corridor = true;
    const auto answer         = f.solve(lazy);
    ASSERT_EQ(answer.status, Sat_status::satisfiable);
    EXPECT_EQ(answer.hop_choices.at(0), f.chosen());
    EXPECT_EQ(f.overfull_edges(answer), 0u);
    std::set<Oracle_edge> edges;
    for (const auto& piece : answer.conns) {
      EXPECT_EQ(f.grid.row(piece.ta), 2);
      EXPECT_EQ(f.grid.row(piece.tb), 2);
      edges.emplace(std::min(piece.ta, piece.tb), std::max(piece.ta, piece.tb));
    }
    EXPECT_EQ(edges,
              (std::set<Oracle_edge>{
                  {f.grid.tile(0, 2), f.grid.tile(1, 2)},
                  {f.grid.tile(1, 2), f.chosen()}
    }));
    EXPECT_NO_THROW(f.validate(answer.conns));
  }
}

TEST(LegrouteSat, FreeCorridorSharedBranchesAndSameNetReservationsAreChargedOnce) {
  for (unsigned lazy : {0u, 1u, 2u}) {
    SCOPED_TRACE(lazy);
    Moved_hop_fixture f(0);
    f.options.window_tiles = 0;
    auto& ends             = f.ends[0];
    ends.preserve_coarse = ends.refine_corridor = true;
    const int a = f.grid.tile(0, 0), b = f.grid.tile(1, 0), c = f.original();
    ends.coarse_parts = {
        {{0, 0}, {a, b, c}},
        {{0, 1},    {b, c}}
    };
    ends.coarse_edges = {
        {{0, 0}, {{a, b}, {b, c}}},
        {{0, 1},         {{b, c}}}
    };
    ends.per_region[0] = {
        {-1, a, -1, 0, a},
        {-1, c, -1, 0, c},
        {-1, b, -1, 1, b},
        {-1, c, -1, 1, c}
    };
    f.reserve.h[b] = 1;
    f.reserve.edges.emplace(0, b, 1);
    const auto answer = f.solve(lazy);
    ASSERT_EQ(answer.status, Sat_status::satisfiable);
    EXPECT_EQ(f.overfull_edges(answer), 0u);
    EXPECT_NO_THROW(f.validate(answer.conns));
    // Same geometry owned by another net consumes the only available track.
    f.reserve.edges.clear();
    f.reserve.edges.emplace(1, b, 1);
    EXPECT_EQ(f.solve(lazy).status, Sat_status::unsatisfiable);
  }
}

struct Corridor_seed_fixture : Moved_hop_fixture {
  Corridor_seed_fixture() : Moved_hop_fixture(1) {
    options.partition_cells = 100;
    options.legal_boxes     = false;
    options.window_tiles    = 1;
    ends[0].preserve_coarse = ends[0].refine_corridor = true;
    const int a = grid.tile(0, 0), b = grid.tile(1, 0), c = original();
    ends[0].coarse_parts[{0, 0}] = {a, b, c};
    ends[0].coarse_edges[{
        0,
        0
    }]                      = {{a, b}, {b, c}};
    ends[0].per_region[0]        = {
        {-1, a, -1, 0, a},
        {-1, c, -1, 0, c, 0}
    };
  }

  Region_out candidate() const {
    Region_out result;
    result.status = Sat_status::satisfiable;
    result.conns  = {
        {0, grid.tile(0, 0), original(), true}
    };
    return result;
  }

  bool verify(Region_out& answer, Clock::time_point deadline = Clock::time_point::max()) {
    Design        design{tech, fp, options};
    Region_budget budget{Clock::now(), deadline};
    return validate_corridor_seed(design,
                                  options,
                                  grid,
                                  0,
                                  {},
                                  owner_tiles,
                                  boxes,
                                  ends,
                                  {},
                                  {},
                                  reserve,
                                  horizontal,
                                  vertical,
                                  answer,
                                  budget);
  }
};

TEST(LegrouteSat, RestrictedCorridorProposalIsVerifiedAndZeroRadiusBypassesIt) {
  Corridor_seed_fixture f;
  const auto            answer = f.solve(2);
  ASSERT_EQ(answer.status, Sat_status::satisfiable);
  EXPECT_EQ(answer.rep.corridor_seed_attempts, 1u);
  EXPECT_EQ(answer.rep.corridor_seed_successes, 1u);
  EXPECT_EQ(answer.rep.corridor_checked_components, 1u);
  EXPECT_EQ(answer.rep.corridor_selected_edges, 2u);
  EXPECT_EQ(f.overfull_edges(answer), 0u);
  EXPECT_TRUE(answer.hop_choices.empty());
  EXPECT_EQ(f.reserve.h, std::vector<int>(f.grid.size(), 0));
  EXPECT_TRUE(f.reserve.edges.empty());  // seed-only original-edge reserves never escape
  f.options.window_tiles = 0;
  const auto identity    = f.solve(2);
  ASSERT_EQ(identity.status, Sat_status::satisfiable);
  EXPECT_EQ(identity.rep.corridor_seed_attempts, 0u);
  EXPECT_EQ(identity.rep.corridor_selected_edges, 2u);
}

TEST(LegrouteSat, RestrictedUnsatProposalDoesNotNarrowTheFullCorridor) {
  for (unsigned lazy : {0u, 1u, 2u}) {
    Corridor_seed_fixture f;
    f.options.window_tiles = 2;
    std::ranges::fill(f.vertical, 0);
    // The actual endpoints are on row two while the original anchors are on
    // row zero. With no vertical tracks, only replacing the whole path works.
    f.ends[0].per_region[0][0].tile = f.grid.tile(0, 2);
    f.ends[0].per_region[0][1].tile = f.chosen();
    const auto answer               = f.solve(lazy);
    ASSERT_EQ(answer.status, Sat_status::satisfiable);
    EXPECT_EQ(answer.rep.corridor_seed_attempts, 1u);
    EXPECT_EQ(answer.rep.corridor_seed_successes, 0u);
    EXPECT_GT(answer.rep.corridor_seed_solves, 0u);
    EXPECT_GT(answer.rep.corridor_checked_components, 0u);
    EXPECT_EQ(f.overfull_edges(answer), 0u);
    for (const auto& piece : answer.conns) {
      EXPECT_EQ(f.grid.row(piece.ta), 2);
      EXPECT_EQ(f.grid.row(piece.tb), 2);
    }
    EXPECT_TRUE(f.reserve.edges.empty());
  }
}

TEST(LegrouteSat, SeedVerifierRejectsMissingConnectivityCapacityAndChangedInterfaces) {
  Corridor_seed_fixture f;
  auto                  good = f.candidate();
  good.conns.push_back({0, f.original(), f.grid.tile(0, 0), false});
  ASSERT_TRUE(f.verify(good));
  EXPECT_EQ(good.conns.size(), 2u);  // canonical edge union, not repeated route pieces
  auto missing  = f.candidate();
  missing.conns = {
      {0, f.grid.tile(0, 0), f.grid.tile(1, 0), true}
  };
  EXPECT_FALSE(f.verify(missing));
  auto moved           = f.candidate();
  moved.hop_choices[0] = f.chosen();
  EXPECT_FALSE(f.verify(moved));
  auto overloaded                 = f.candidate();
  f.horizontal[f.grid.tile(1, 0)] = 0;
  EXPECT_FALSE(f.verify(overloaded));
  f.horizontal[f.grid.tile(1, 0)] = 1;
  auto expired                    = f.candidate();
  EXPECT_THROW(f.verify(expired, Clock::time_point{}), Region_budget_expired);
  EXPECT_EQ(expired.conns.size(), 1u);  // no partially canonicalized answer on immediate expiry
}

TEST(LegrouteSat, SeedVerifierCannotBorrowAnotherPartsExternalConnection) {
  Corridor_seed_fixture f;
  f.options.window_tiles = 0;
  const int a = f.grid.tile(0, 0), b = f.grid.tile(1, 0), c = f.original();
  const int u = f.grid.tile(0, 1), v = f.grid.tile(1, 1), w = f.grid.tile(2, 1);
  f.ends[0].coarse_parts[{0, 1}] = {a, u, v, w, c};
  f.ends[0].coarse_edges[{
      0,
      1
  }]                        = {{a, u}, {u, v}, {v, w}, {c, w}};
  f.ends[0].per_region[0].push_back({-1, a, -1, 1, a});
  f.ends[0].per_region[0].push_back({-1, c, -1, 1, c});
  auto answer  = f.candidate();
  answer.conns = {
      {0, a, u, false},
      {0, u, w,  true},
      {0, c, w, false}
  };
  EXPECT_FALSE(f.verify(answer));  // whole-net connectivity alone would incorrectly pass
  answer.conns.push_back({0, a, b, true});
  answer.conns.push_back({0, b, c, true});
  EXPECT_TRUE(f.verify(answer));
  EXPECT_EQ(answer.rep.corridor_checked_components, 2u);
}

TEST(LegrouteSat, SeedVerifierRejectsUnownedNetEdgesUnlessOriginallyReserved) {
  Corridor_seed_fixture f;
  f.ends.resize(2);
  const int a = f.grid.tile(0, 1), b = f.grid.tile(1, 1);
  for (bool refined_without_region : {false, true}) {
    f.ends[1].refine_corridor = refined_without_region;
    auto extra                = f.candidate();
    extra.conns.push_back({1, a, b, true});
    EXPECT_FALSE(f.verify(extra));  // valid net id and spare tracks do not establish ownership
  }
  f.reserve.edges.emplace(1, a, 1);
  f.reserve.h[a] = 1;
  auto immutable = f.candidate();
  immutable.conns.push_back({1, a, b, true});
  EXPECT_TRUE(f.verify(immutable));
  f.reserve.edges.clear();
  f.reserve.h[a]         = 0;
  f.options.window_tiles = 0;
  auto leaves_corridor   = f.candidate();
  leaves_corridor.conns.push_back({0, a, b, true});
  EXPECT_FALSE(f.verify(leaves_corridor));
}

TEST(LegrouteSat, SeedVerifierChecksPhysicalBoxFitDomainOccupancyAndFixedTile) {
  Corridor_seed_fixture f;
  f.grid.site  = 25;
  f.grid.sites = 4;
  f.grid.key.assign(f.grid.size(), 0);
  f.grid.keys = {
      {{0, 0, 4}, {1, 0, 4}}
  };
  f.boxes.boxes.resize(2);
  for (int b = 0; b < 2; ++b) {
    f.boxes.boxes[b].cells = {b};
    f.boxes.boxes[b].tile  = b;
  }
  Design design{f.tech, f.fp, f.options};
  design.width = {25, 25};
  design.tall  = {false, false};
  auto verify  = [&](Region_out answer, const std::vector<char>& pinned = std::vector<char>{}) {
    Region_budget budget{Clock::now(), Clock::time_point::max()};
    return validate_corridor_seed(design,
                                  f.options,
                                  f.grid,
                                  0,
                                  {0, 1},
                                  f.owner_tiles,
                                  f.boxes,
                                  f.ends,
                                  {},
                                  pinned,
                                  f.reserve,
                                  f.horizontal,
                                  f.vertical,
                                  answer,
                                  budget);
  };
  auto good  = f.candidate();
  good.tiles = {
      {0, 0},
      {1, 1}
  };
  good.seed_box_domains = {
      {0, {0, 1, 2}},
      {1, {0, 1, 2}}
  };
  EXPECT_TRUE(verify(good));
  auto collision            = good;
  collision.tiles[1].second = 0;
  EXPECT_FALSE(verify(collision));
  auto outside_domain                = good;
  outside_domain.seed_box_domains[1] = {2};
  EXPECT_FALSE(verify(outside_domain));
  std::vector<char> pinned(f.grid.size(), 0);
  pinned[1] = 1;
  EXPECT_FALSE(verify(good, pinned));
  f.boxes.boxes[1].fixed      = true;
  auto moved_fixed            = good;
  moved_fixed.tiles[1].second = 2;
  EXPECT_FALSE(verify(moved_fixed));
  EXPECT_TRUE(verify(good));
  design.width[1] = 125;  // five sites cannot fit either four-site row
  EXPECT_FALSE(verify(good));
  EXPECT_EQ(f.boxes.boxes[0].tile, 0);
  EXPECT_EQ(f.boxes.boxes[1].tile, 1);
}

TEST(LegrouteSat, ExpiredAndSkippedRegionsNeverEnterTheRestrictedProposal) {
  Corridor_seed_fixture f;
  auto                  expired = f.solve(2, false, Clock::time_point{});
  EXPECT_EQ(expired.status, Sat_status::unknown);
  EXPECT_EQ(expired.rep.corridor_seed_attempts, 0u);
  EXPECT_TRUE(expired.conns.empty());
  auto skipped = f.solve(2, true, Clock::time_point{});
  EXPECT_TRUE(skipped.skipped);
  EXPECT_EQ(skipped.rep.corridor_seed_attempts, 0u);
  EXPECT_EQ(skipped.rep.sat_skipped_regions, 1u);
  EXPECT_TRUE(skipped.conns.empty());
}

TEST(LegrouteSat, VerifiedPointProposalKeepsExactBackboneAndDoesNotReattachFixedInterfaces) {
  for (bool with_box : {false, true}) {
    for (unsigned lazy : {0u, 2u}) {
      SCOPED_TRACE(with_box);
      SCOPED_TRACE(lazy);
      Corridor_seed_fixture f;
      f.options.window_tiles  = 2;
      f.options.capacity_lazy = lazy;
      f.grid.site             = 25;
      f.grid.row_h            = 50;
      f.grid.key.assign(f.grid.size(), 0);
      f.grid.keys = {
          {{0, 0, 4}, {1, 0, 4}}
      };
      const int                           a = f.grid.tile(0, 0), bend = f.grid.tile(1, 0), boundary = f.grid.tile(1, 1);
      const std::set<std::pair<int, int>> original_edges{
          {   a,     bend},
          {bend, boundary}
      };
      f.ends[0].coarse_parts[{0, 0}]     = {a, bend, boundary};
      f.ends[0].coarse_edges[{0, 0}]     = original_edges;
      f.ends[0].coarse_junctions[{0, 0}] = {a, bend, boundary};
      f.ends[0].per_region[0]            = {
          {-1, a, -1, 0, a},
          {-1, boundary, -1, 0, boundary, 0}
      };
      std::vector<int> B;
      Design           design{f.tech, f.fp, f.options};
      design.nets.resize(1);
      design.nets[0].routed = true;
      design.nets[0].pins.resize(2);
      if (with_box) {
        Box box;
        box.cells = {0};
        box.tile  = a;
        box.cx    = f.grid.cx(f.grid.col(a));
        box.cy    = f.grid.cy(f.grid.row(a));
        box.sites = 1;
        f.boxes.boxes.push_back(box);
        f.boxes.box_of                  = {0};
        B                               = {0};
        design.width                    = {25};
        design.tall                     = {false};
        f.ends[0].per_region[0][0].box  = 0;
        f.ends[0].per_region[0][0].tile = -1;
      }
      const auto              original_boxes = f.boxes;
      const auto              point          = f.boxes;
      const std::vector<char> pinned(f.grid.size(), 0);
      Region_out              answer;
      solve_region(design,
                   f.options,
                   f.grid,
                   0,
                   B,
                   f.owner_tiles,
                   f.boxes,
                   f.ends,
                   {},
                   {},
                   pinned,
                   " exact-point",
                   f.horizontal,
                   f.vertical,
                   f.reserve,
                   {},
                   f.hop_room,
                   answer,
                   Clock::time_point::max(),
                   &point);
      ASSERT_EQ(answer.status, Sat_status::satisfiable);
      EXPECT_EQ(answer.rep.corridor_seed_attempts, 1u);
      EXPECT_EQ(answer.rep.corridor_seed_successes, 1u);
      EXPECT_EQ(answer.rep.coarse_attachment_choices, 0u);
      EXPECT_EQ(answer.rep.coarse_attachments_changed, 0u);
      EXPECT_TRUE(answer.hop_choices.empty());
      std::set<std::pair<int, int>> emitted;
      for (const auto& piece : answer.conns) {
        const int horizontal_row = f.grid.row(piece.h_first ? piece.ta : piece.tb);
        const int vertical_col   = f.grid.col(piece.h_first ? piece.tb : piece.ta);
        for (int x = std::min(f.grid.col(piece.ta), f.grid.col(piece.tb)); x < std::max(f.grid.col(piece.ta), f.grid.col(piece.tb));
             ++x) {
          emitted.emplace(f.grid.tile(x, horizontal_row), f.grid.tile(x + 1, horizontal_row));
        }
        for (int y = std::min(f.grid.row(piece.ta), f.grid.row(piece.tb)); y < std::max(f.grid.row(piece.ta), f.grid.row(piece.tb));
             ++y) {
          emitted.emplace(f.grid.tile(vertical_col, y), f.grid.tile(vertical_col, y + 1));
        }
      }
      EXPECT_EQ(emitted, original_edges);
      EXPECT_EQ(f.overfull_edges(answer), 0u);
      for (size_t i = 0; i < f.boxes.boxes.size(); ++i) {
        EXPECT_FALSE(f.boxes.boxes[i].fixed);
        EXPECT_EQ(f.boxes.boxes[i].cx, original_boxes.boxes[i].cx);
        EXPECT_EQ(f.boxes.boxes[i].cy, original_boxes.boxes[i].cy);
        EXPECT_EQ(f.boxes.boxes[i].cells, original_boxes.boxes[i].cells);
      }
    }
  }
}

}  // namespace
}  // namespace livehd::placement::lr

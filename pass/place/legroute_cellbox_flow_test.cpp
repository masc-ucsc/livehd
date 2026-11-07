// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gtest/gtest.h"
#include "legroute_internal.hpp"
#include "signal_route_domain.hpp"

namespace livehd::placement::lr {
namespace {
// Pure scope checks deliberately decouple physical obligations from the legacy
// coarse `routed` flag. Source endpoint records remain present in every case.
struct Scope_fixture {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  Design               design{tech, fp, options};

  Scope_fixture() {
    fp.terminals.reserve(32);  // Keep Pin::name views valid while adding later IO obligations.
    auto& master               = tech.macros["scope_cell"];
    master.klass               = "CORE";
    master.pins["A"].direction = "INPUT";
    master.pins["Y"].direction = "OUTPUT";
  }

  void add(const std::string& name, std::vector<Pin> pins, bool routed = false, bool clock = false, bool power = false,
           bool control = false) {
    physical::Net source;
    source.name = name;
    source.use  = power ? "POWER" : clock ? "CLOCK" : "SIGNAL";
    for (auto& pin : pins) {
      if (pin.comp >= 0) {
        while (fp.components.size() <= size_t(pin.comp)) {
          physical::Component cell;
          cell.name  = "c" + std::to_string(fp.components.size());
          cell.macro = "scope_cell";
          fp.components.push_back(cell);
        }
        if (pin.name.empty()) {
          pin.name = "A";
        }
        pin.lef = &tech.macros.at("scope_cell").pins.at(std::string(pin.name));
        source.connections.push_back({fp.components[size_t(pin.comp)].name, std::string(pin.name)});
      } else {
        while (fp.terminals.size() <= size_t(pin.term)) {
          physical::Terminal terminal;
          terminal.name = "io" + std::to_string(fp.terminals.size());
          fp.terminals.push_back(terminal);
        }
        auto& terminal = fp.terminals[size_t(pin.term)];
        terminal.net   = name;
        pin.name       = terminal.name;
        source.connections.push_back({"PIN", terminal.name});
      }
    }
    Net net;
    net.frame         = fp.nets.size();
    net.pins          = std::move(pins);
    net.routed        = routed;
    net.clock         = clock;
    net.power         = power;
    net.control_scope = control;
    fp.nets.push_back(std::move(source));
    design.nets.push_back(std::move(net));
  }
};

struct Domain_fixture {
  physical::Technology tech;
  physical::Floorplan  fp;
  physical::Net        net;
  Signal_route_domain  domain;

  Domain_fixture() {
    tech.dbu = fp.dbu       = 1000;
    tech.manufacturing_grid = .001;
    fp.die                  = {0, 0, 1000, 1000};
    for (int i = 1; i <= 6; ++i) {
      const auto name  = "M" + std::to_string(i);
      auto&      layer = tech.layers[name];
      layer.direction  = i % 2 ? "VERTICAL" : "HORIZONTAL";
      layer.width      = .02;
      layer.pitch_x = layer.pitch_y = .05;
      layer.offset_x = layer.offset_y = .025;
      layer.has_offset                = true;
      layer.rules.loaded              = true;
      layer.rules.area                = .0004;
      layer.rules.spacing             = {
          {.02, {}, {}}
      };
      tech.routing_layers.push_back(name);
      fp.tracks.push_back({i % 2 ? "X" : "Y", name, 25, 20, 50});
      if (i > 1) {
        const auto cut               = "V" + std::to_string(i - 1);
        tech.cut_layers[cut].width   = .01;
        tech.cut_layers[cut].spacing = {.01};
        auto& via                    = tech.vias[cut + "_DEF"];
        via.is_default               = true;
        via.shapes                   = {
            {"M" + std::to_string(i - 1),     {-.01, -.01, .01, .01}},
            {                        cut, {-.005, -.005, .005, .005}},
            {                       name,     {-.01, -.01, .01, .01}},
        };
      }
    }
    net.name = domain.owner = "signal";
    domain.pin_layer        = "M1";
    domain.local_layers     = {"M2", "M3"};
    domain.regions.push_back({
        {100, 100, 900, 900},
        {{"M2", {225}}, {"M3", {325}}}
    });
  }

  static physical::Route_path wire(const std::string& layer, int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    // Ordinary NETS inherit the 20-DBU native layer width; an explicit width is
    // a SPECIALNETS encoding and is rejected before resource validation.
    return {
        layer,
        {},
        {},
        {physical::Route_point{x0, y0, 0}, physical::Route_point{x1, y1, 0}},
        {}
    };
  }
  static physical::Route_path via(const std::string& lower, const std::string& name, int64_t x, int64_t y) {
    return {
        lower,
        {},
        {},
        {physical::Route_point{x, y, 0}, physical::Route_via{name}},
        {}
    };
  }
  std::string check(const std::vector<physical::Route_path>& paths) const {
    return validate_signal_route_domain(tech, fp, net, paths, domain);
  }
};

// Build real finite track resources from a native technology/frame. Four
// 12-site cells cannot share a row of the 22-site common box.
struct Handoff_fixture {
  physical::Technology   tech;
  physical::Floorplan    fp;
  physical::Tech_profile profile;
  Legroute_options       options;
  timing::Feedback       timing;
  Grid                   grid;

  explicit Handoff_fixture(bool wide = false) {
    Domain_fixture layers;
    tech               = std::move(layers.tech);
    tech.sites["site"] = {.05, .25, true};
    fp.design          = "native_cellbox_bridge";
    fp.dbu             = 1000;
    fp.die             = {0, 0, 6000, 4000};
    for (int i = 1; i <= 6; ++i) {
      fp.tracks.push_back({i % 2 ? "X" : "Y", "M" + std::to_string(i), 25, 120, 50});
    }
    for (int row = 0; row < 4; ++row) {
      fp.rows.push_back({"row" + std::to_string(row),
                         "site",
                         1000,
                         1000 + row * 250,
                         66,
                         50,
                         row % 2 ? physical::Orientation::FS : physical::Orientation::N});
    }
    options.critical_cellboxes = options.cellbox_full_flow = true;
    options.reserved_tracks                                = 0;
    options.reserved_tracks_per_um                         = -1;
    options.box_reserve                                    = "M2:20,M3:20";
    profile.route.access                                   = {"M1", "M2", "V1"};
    profile.route.branch_layer                             = "M3";
    profile.route.branch_cut                               = "V2";
    profile.route.signal_layers                            = {"M2", "M3", "M4", "M5", "M6"};
    timing.time_unit_ps                                    = 1;
    if (wide) {
      add("wide", 1500);
    }
    for (int i = 0; i < 4; ++i) {
      add("ordinary" + std::to_string(i), 600);
    }
    add("zero_signal", 50);
    fp.nets.push_back({"singleton", {{"ordinary0", "A"}}});
  }

  void add(const std::string& name, int width) {
    auto& master      = tech.macros[name];
    master.klass      = "CORE";
    master.site       = "site";
    master.width      = width / 1000.;
    master.height     = .25;
    master.symmetry_x = master.symmetry_y = true;
    master.pins["A"]                      = {"INPUT", "SIGNAL", {{"M1", {.015, .015, .035, .235}}}};
    Ann_place pose(1000, 1000, width, 250);
    pose.status = physical::Status::PLACED;
    fp.components.push_back({name, name, pose});
  }

  Design design() { return build_design(tech, fp, timing, options, &profile); }

  std::shared_ptr<Critical_cellbox_handoff> freeze(bool one_tile_requests = false) {
    auto d                = design();
    grid                  = build_grid(d, {});
    auto handoff          = std::make_shared<Critical_cellbox_handoff>();
    handoff->source_frame = fp;
    handoff->options      = options;
    handoff->grid         = grid;
    handoff->admission    = admit_critical_cellboxes(d, grid, timing);
    handoff->requests     = all_signal_requests(d, grid, handoff->admission.boxes);
    // The default fixture has only an intrinsic singleton. A same-tile source
    // IO obligation has a request, but needs no coarse edge between tiles.
    if (!one_tile_requests && !handoff->requests.empty()) {
      throw std::logic_error("unexpected fixture coarse demand");
    }
    handoff->routes.trees.resize(handoff->requests.size());
    for (size_t i = 0; i < handoff->requests.size(); ++i) {
      if (handoff->requests[i].endpoints.size() != 1) {
        throw std::logic_error("fixture request needs a real inter-tile forest");
      }
      handoff->routes.trees[i].routed = true;
    }
    options.cellbox_handoff = handoff;
    return handoff;
  }
};

// Structural admission is deliberately tested independently of native metal
// certification. An empty rejection still requires the separate physical audit.
struct Local_admission_fixture : Scope_fixture {
  Grid                   grid;
  Boxes                  boxes;
  Routing                routes;
  Final_planned_geometry geometry;

  Local_admission_fixture() {
    Domain_fixture native;
    tech.layers             = std::move(native.tech.layers);
    tech.routing_layers     = std::move(native.tech.routing_layers);
    tech.manufacturing_grid = .001;
    fp.dbu = tech.dbu         = 1000;
    fp.die                    = {0, 0, 1000, 1000};
    fp.tracks                 = std::move(native.fp.tracks);
    options.cellbox_full_flow = true;
    add("two_boxes", {{.comp = 0}, {.comp = 1}});
    design.cell    = {0, 1};
    design.movable = {0, 1};
    grid.dbu       = 1000;
    grid.cols      = 2;
    grid.rows      = 1;
    grid.tile_w = grid.tile_h = 500;
    grid.layers               = {
        {.name = "M3", .horizontal = false, .pitch = 50, .offset = 25, .width = 20},
        {.name = "M4",  .horizontal = true, .pitch = 50, .offset = 25, .width = 20}
    };
    boxes.box_of = {0, 1};
    boxes.boxes.resize(2);
    routes.trees.resize(1);
    routes.local = {0};
    for (int i = 0; i < 2; ++i) {
      boxes.boxes[i].tile  = i;
      boxes.boxes[i].cells = {i};
      Node node;
      node.tile = node.box    = i;
      node.pins               = {i};
      node.direct_carrier_tap = true;
      routes.trees[0].nodes.push_back(node);
    }
    routes.segments = {
        {.conn        = 0,
         .net         = 0,
         .layer       = 1,
         .band        = 0,
         .lo          = 0,
         .hi          = 1,
         .node_lo     = 0,
         .node_hi     = 1,
         .track       = 4,
         .track_state = uint8_t(Track_state::checked)}
    };
    Conn connection;
    connection.net  = 0;
    connection.a    = 0;
    connection.b    = 1;
    connection.poly = {
        {0, 0},
        {1, 0}
    };
    connection.segs = {0};
    routes.conns.push_back(connection);
    routes.trees[0].conns = {0};
    geometry.paths        = {{Domain_fixture::wire("M4", 125, 225, 625, 225)}};
    geometry.path_state   = {{uint8_t(Track_state::checked)}};
    geometry.joins.resize(1);
    geometry.io_stub_failure.resize(1);
  }

  std::string rejection() const { return local_backbone_rejections(design, grid, boxes, routes, geometry).at(0); }

  void use_io_endpoint() {
    physical::Terminal terminal;
    terminal.name   = "source_io";
    terminal.net    = fp.nets[0].name;
    terminal.x      = 625;
    terminal.y      = 225;
    terminal.shapes = {
        {"M4", {-10, -10, 10, 10}}
    };
    fp.terminals.push_back(terminal);
    design.nets[0].pins[1]    = {.term = 0, .name = fp.terminals[0].name};
    fp.nets[0].connections[1] = {"PIN", terminal.name};
    auto& node                = routes.trees[0].nodes[1];
    node.box                  = -1;
    node.direct_carrier_tap   = false;
    node.riser_layer          = 0;
    node.riser_track          = 12;
    node.riser_state          = uint8_t(Track_state::checked);
    node.stub_layer           = 1;
    node.stub_track           = 4;
    node.stub_state = node.stub_access_state = uint8_t(Track_state::checked);
  }
};
}  // namespace

TEST(LegrouteCellboxFlow, FullScopeIncludesSameCellPinsSharedPinsAndRealIO) {
  Scope_fixture f;
  f.options.critical_cellboxes = true;
  f.options.cellbox_full_flow  = true;
  f.add("same_cell",
        {
            {.comp = 0, .name = "A"},
            {.comp = 0, .name = "Y"}
  });
  f.add("internal_three", {{.comp = 1}, {.comp = 2}, {.comp = 3}}, true);
  f.add("shared_external", {{.comp = 4}, {.comp = 5}, {.term = 0}}, true);
  f.add("io_to_io", {{.term = 1}, {.term = 2}});
  f.add("untraced_clock", {{.comp = 6}, {.comp = 7}}, false, true);
  f.add("traced_control", {{.comp = 8}, {.comp = 9}}, true, false, false, true);
  f.add("power", {{.comp = 10}, {.comp = 11}}, true, false, true);
  f.add("singleton_cell_pin", {{.comp = 12}});
  f.add("singleton_io", {{.term = 3}});
  f.add("empty", {});

  const auto before = f.fp.nets;
  const auto scope  = final_net_scope(f.design);
  EXPECT_EQ(scope.required, (std::vector<char>{1, 1, 1, 1, 1, 0, 0, 0, 0, 0}));
  EXPECT_EQ(f.fp.nets, before);  // Scope selection must not erase excluded source obligations.
}

TEST(LegrouteCellboxFlow, FullScopeCannotHideDataBehindLegacySkipFlags) {
  Scope_fixture f;
  f.options.critical_cellboxes = true;
  f.options.cellbox_full_flow  = true;
  f.options.final_skip_nets    = ".*";
  f.options.final_skip_clock   = true;
  f.add("ordinary_data", {{.comp = 0}, {.comp = 1}}, true);
  f.add("untraced_clock", {{.comp = 2}, {.comp = 3}}, false, true);
  f.add("traced_control", {{.comp = 4}, {.comp = 5}}, true, true, false, true);
  EXPECT_EQ(final_net_scope(f.design).required, (std::vector<char>{1, 1, 0}));

  // Existing non-full-flow opt-outs keep their behavior.
  f.options.cellbox_full_flow = false;
  EXPECT_EQ(final_net_scope(f.design).required, (std::vector<char>{0, 0, 0}));
}

TEST(LegrouteCellboxFlow, MissingOrdinaryEndpointCannotBecomeAnIgnoredSingleton) {
  Scope_fixture f;
  f.options.critical_cellboxes = true;
  f.options.cellbox_full_flow  = true;
  f.add("two_pins", {{.comp = 0}, {.comp = 1}}, true);
  ASSERT_EQ(final_net_scope(f.design).required, (std::vector<char>{1}));
  f.design.nets[0].pins.pop_back();
  EXPECT_THROW(final_net_scope(f.design), std::invalid_argument);
  EXPECT_EQ(f.fp.nets[0].connections.size(), 2u);
}

TEST(LegrouteCellboxFlow, SourceNetMappingMustBeCompleteAndUnique) {
  Scope_fixture f;
  f.options.critical_cellboxes = true;
  f.options.cellbox_full_flow  = true;
  f.add("first", {{.comp = 0}, {.comp = 1}}, true);
  f.add("second", {{.comp = 2}, {.comp = 3}}, true);
  const auto original    = f.design.nets;
  f.design.nets[1].frame = 0;
  EXPECT_THROW(final_net_scope(f.design), std::invalid_argument);
  f.design.nets          = original;
  f.design.nets[1].frame = f.fp.nets.size();
  EXPECT_THROW(final_net_scope(f.design), std::invalid_argument);
  f.design.nets = original;
  f.design.nets.pop_back();
  EXPECT_THROW(final_net_scope(f.design), std::invalid_argument);
}

TEST(LegrouteCellboxFlow, EqualEndpointCountDoesNotAuthorizeDifferentPins) {
  Scope_fixture f;
  f.options.critical_cellboxes = true;
  f.options.cellbox_full_flow  = true;
  f.add("cell_to_io", {{.comp = 0}, {.term = 0}}, true);
  const auto pins               = f.design.nets[0].pins;
  f.design.nets[0].pins[0].name = "Y";
  EXPECT_THROW(final_net_scope(f.design), std::invalid_argument);
  f.design.nets[0].pins         = pins;
  f.design.nets[0].pins[1].name = "different_io";
  EXPECT_THROW(final_net_scope(f.design), std::invalid_argument);
}

TEST(LegrouteCellboxFlow, NativeLaneCountCannotSubstituteForExactCoordinateOwnership) {
  Domain_fixture f;
  EXPECT_EQ(f.check({f.wire("M2", 150, 225, 500, 225)}), "");
  EXPECT_FALSE(f.check({f.wire("M2", 150, 275, 500, 275)}).empty());  // Native but not reserved.
  EXPECT_FALSE(f.check({f.wire("M2", 150, 225, 950, 225)}).empty());  // Leaves owned box.
  f.domain.owner = "other_net";
  EXPECT_FALSE(f.check({f.wire("M2", 150, 225, 500, 225)}).empty());
}

TEST(LegrouteCellboxFlow, CarrierExtensionsRequireConnectedSameOwnerFiniteCollinearMetal) {
  Domain_fixture f;
  f.domain.regions.clear();
  f.domain.carriers.push_back({
      "signal",
      "incoming",
      {"M2", {200, 215, 240, 235}},
      {200, 215, 350, 235}
  });
  EXPECT_EQ(f.check({f.wire("M2", 240, 225, 330, 225)}), "");
  EXPECT_FALSE(f.check({f.wire("M2", 280, 225, 330, 225)}).empty());  // Detached from the seed.
  EXPECT_FALSE(f.check({f.wire("M2", 240, 225, 360, 225)}).empty());  // Outside authorized extent.
  EXPECT_FALSE(f.check({f.wire("M2", 240, 225, 330, 275)}).empty());  // Off-carrier bend.
  f.domain.carriers[0].owner = "other_net";
  EXPECT_FALSE(f.check({f.wire("M2", 240, 225, 330, 225)}).empty());
}

TEST(LegrouteCellboxFlow, DirectIncomingM2TapNeedsFullViaCoverageAndActualM1PinUnion) {
  Domain_fixture f;
  f.domain.regions.clear();  // A direct same-carrier VIA12 consumes no extra reserved H/V track.
  f.domain.carriers.push_back({
      "signal",
      "incoming",
      {"M2", {200, 215, 240, 235}},
      {200, 215, 350, 235}
  });
  auto& master     = f.tech.macros["cell"];
  master.width     = .1;
  master.height    = .1;
  master.klass     = "CORE";
  master.pins["A"] = {"INPUT", "SIGNAL", {{"M1", {.015, .015, .035, .035}}}};
  Ann_place pose(300, 200, 100, 100);
  pose.status = physical::Status::PLACED;
  f.fp.components.push_back({"u", "cell", pose});
  f.net.connections.push_back({"u", "A"});
  const std::vector paths{f.wire("M2", 240, 225, 335, 225), f.via("M1", "V1_DEF", 325, 225)};
  EXPECT_EQ(f.check(paths), "");
  f.domain.carriers[0].extension.xh = 320;
  EXPECT_FALSE(f.check(paths).empty());  // Upper plate overhang is not free landing metal.
  f.domain.carriers[0].extension.xh  = 350;
  master.pins["A"].shapes[0].rect.xh = .03;
  EXPECT_FALSE(f.check(paths).empty());  // Lower plate no longer fits the actual physical pin.
}

TEST(LegrouteCellboxFlow, HigherLayerStackHasFiniteBridgeAndNoUnownedUpperJog) {
  Domain_fixture f;
  f.domain.carriers.push_back({
      "signal",
      "incoming_M5",
      {"M5", {365, 200, 385, 240}},
      {365, 200, 385, 400}
  });
  f.domain.max_stack_bridge_dbu = 50;
  std::vector paths{f.wire("M5", 375, 240, 375, 335),
                    f.via("M4", "V4_DEF", 375, 325),
                    f.wire("M4", 315, 325, 385, 325),
                    f.via("M3", "V3_DEF", 325, 325),
                    f.wire("M3", 325, 315, 325, 500)};
  EXPECT_EQ(f.check(paths), "");
  f.domain.max_stack_bridge_dbu = 49;
  EXPECT_FALSE(f.check(paths).empty());
  f.domain.max_stack_bridge_dbu = 50;
  paths.push_back(f.wire("M4", 385, 325, 435, 325));
  EXPECT_FALSE(f.check(paths).empty());
}

TEST(LegrouteCellboxFlow, InstallsAllFourRowsWithoutDroppingSingletonOrZeroSignalCell) {
  Handoff_fixture f;
  const auto      handoff = f.freeze();
  ASSERT_TRUE(handoff->admission.unassigned.empty());
  ASSERT_EQ(handoff->admission.slots.size(), 1u);
  const auto nets = f.fp.nets;
  auto       d    = f.design();
  Boxes      boxes;
  ASSERT_NO_THROW(install_critical_handoff(d, f.grid, boxes));
  ASSERT_EQ(boxes.boxes.size(), 1u);
  EXPECT_EQ(boxes.boxes[0].cells.size(), 5u);
  EXPECT_EQ(f.fp.components.size(), 5u);
  EXPECT_EQ(f.fp.nets, nets);
  EXPECT_TRUE(boxes.boxes[0].critical_fixed);
  std::set<int64_t> rows;
  for (const auto& cell : f.fp.components) {
    rows.insert(cell.place.y);
    const int row = int((cell.place.y - 1000) / 250);
    EXPECT_EQ(cell.place.orientation, row % 2 ? physical::Orientation::FS : physical::Orientation::N);
  }
  EXPECT_EQ(rows, (std::set<int64_t>{1000, 1250, 1500, 1750}));
  EXPECT_EQ(boxes.boxes[0].reserved_h, handoff->admission.slots[0].reserved_h);
  EXPECT_EQ(boxes.boxes[0].reserved_v, handoff->admission.slots[0].reserved_v);
  EXPECT_NO_THROW(validate_critical_boxes(d, f.grid, boxes));
}

TEST(LegrouteCellboxFlow, WideFootprintOwnsBothTilesWithoutDoublingTheLocalBudget) {
  Handoff_fixture f(true);
  const auto      handoff = f.freeze();
  ASSERT_TRUE(handoff->admission.unassigned.empty());
  ASSERT_EQ(handoff->admission.wide_exceptions, 1u);
  auto  d = f.design();
  Boxes boxes;
  ASSERT_NO_THROW(install_critical_handoff(d, f.grid, boxes));
  const auto it = std::ranges::find(boxes.boxes, 2, &Box::span_columns);
  ASSERT_NE(it, boxes.boxes.end());
  const auto box = int(it - boxes.boxes.begin());
  EXPECT_EQ(f.grid.tile_w, 1100);
  EXPECT_EQ(f.fp.components[0].place.width, 1500);
  EXPECT_EQ(f.grid.critical_box_of_tile[size_t(it->tile)], box);
  EXPECT_EQ(f.grid.critical_box_of_tile[size_t(it->tile + 1)], box);
  EXPECT_EQ(it->reserved_h.size(), 20u);
  EXPECT_EQ(it->reserved_v.size(), 20u);
  EXPECT_EQ(f.grid.critical_reserved_h[size_t(it->tile)], it->reserved_h);
  EXPECT_EQ(f.grid.critical_reserved_h[size_t(it->tile + 1)], it->reserved_h);
  EXPECT_EQ(f.grid.critical_reserved_v[size_t(it->tile)].size() + f.grid.critical_reserved_v[size_t(it->tile + 1)].size(), 20u);
  EXPECT_NO_THROW(validate_critical_boxes(d, f.grid, boxes));
}

TEST(LegrouteCellboxFlow, StaleOrMalformedHandoffRejectsBeforeAnyPoseIsInstalled) {
  for (int mutation = 0; mutation < 6; ++mutation) {
    SCOPED_TRACE(mutation);
    Handoff_fixture f;
    const auto      handoff = f.freeze();
    ASSERT_TRUE(handoff->admission.unassigned.empty());
    switch (mutation) {
      case 0: f.fp.nets[0].name = "different_source_net"; break;
      case 1: f.fp.tracks[0].start += 50; break;
      case 2: --f.options.cellbox_connections; break;
      case 3: handoff->admission.slots[0].poses.back().x += 50; break;
      case 4: ++handoff->admission.slots[0].reserved_h[0]; break;
      case 5: handoff->routes.unrouted = 1; break;
    }
    const auto before = f.fp.components;
    auto       d      = f.design();
    Boxes      boxes;
    EXPECT_THROW(install_critical_handoff(d, f.grid, boxes), std::invalid_argument);
    EXPECT_EQ(f.fp.components, before);
    EXPECT_TRUE(boxes.boxes.empty());
    EXPECT_TRUE(f.grid.critical_box_of_tile.empty());
  }
}

TEST(LegrouteCellboxFlow, InstalledGlobalExclusionsMustMatchTheLocalReservation) {
  Handoff_fixture f(true);
  const auto      handoff = f.freeze();
  ASSERT_TRUE(handoff->admission.unassigned.empty());
  auto  d = f.design();
  Boxes boxes;
  ASSERT_NO_THROW(install_critical_handoff(d, f.grid, boxes));
  const int tile = handoff->admission.slots[0].tile;
  ASSERT_FALSE(f.grid.critical_reserved_v[size_t(tile)].empty());
  ++f.grid.critical_reserved_v[size_t(tile)][0];
  EXPECT_THROW(validate_critical_boxes(d, f.grid, boxes), std::invalid_argument);
}

TEST(LegrouteCellboxFlow, InstalledPoseAndSecondWideTileCannotBeReassigned) {
  Handoff_fixture f(true);
  const auto      handoff = f.freeze();
  auto            d       = f.design();
  Boxes           boxes;
  ASSERT_NO_THROW(install_critical_handoff(d, f.grid, boxes));
  const auto cells                = f.fp.components;
  f.fp.components.back().place.x += 50;
  EXPECT_THROW(validate_critical_boxes(d, f.grid, boxes), std::invalid_argument);
  f.fp.components = cells;
  const auto it   = std::ranges::find(handoff->admission.slots, 2, &Cellbox_slot::span);
  ASSERT_NE(it, handoff->admission.slots.end());
  f.grid.critical_box_of_tile[size_t(it->tile + 1)] = -1;
  EXPECT_THROW(validate_critical_boxes(d, f.grid, boxes), std::invalid_argument);
}

TEST(LegrouteCellboxFlow, GlobalCarrierEndExtensionCannotEnterAnOwnedLocalLane) {
  Handoff_fixture f;
  const auto      handoff = f.freeze();
  auto            d       = f.design();
  Boxes           boxes;
  ASSERT_NO_THROW(install_critical_handoff(d, f.grid, boxes));
  const auto&            box     = boxes.boxes.at(0);
  const auto             left    = f.grid.left(f.grid.col(box.tile));
  const auto             y       = box.reserved_h.at(0);
  auto                   carrier = Domain_fixture::wire("M2", left - 200, y, left - 10, y);
  Final_planned_geometry geometry;
  geometry.paths = {{carrier}};
  EXPECT_EQ(critical_backbone_conflicts(d, f.grid, geometry), 0u);
  std::get<physical::Route_point>(carrier.steps.back()).extension = 20;
  geometry.paths                                                  = {{carrier}};
  EXPECT_GT(critical_backbone_conflicts(d, f.grid, geometry), 0u);
  std::vector<std::string> rejected;
  EXPECT_GT(critical_backbone_conflicts(d, f.grid, geometry, &rejected), 0u);
  ASSERT_EQ(rejected.size(), d.nets.size());
  EXPECT_FALSE(rejected.at(0).empty());
}

TEST(LegrouteCellboxFlow, LocalRejectionFindsAnUnassignedSegmentEvenWhenEmissionOmitsIt) {
  Local_admission_fixture f;
  ASSERT_TRUE(f.rejection().empty());
  f.routes.segments[0].track = -1;
  f.geometry.paths[0].clear();
  f.geometry.path_state[0].clear();
  EXPECT_NE(f.rejection().find("main segment"), std::string::npos);
}

TEST(LegrouteCellboxFlow, NoBackboneExceptionRequiresAllPhysicalPinsInOneAdmittedBox) {
  Local_admission_fixture f;
  f.boxes.box_of         = {0, 0};
  f.boxes.boxes[0].cells = {0, 1};
  f.routes.local[0]      = 1;
  f.routes.segments.clear();
  f.routes.conns.clear();
  f.routes.trees[0] = {};
  f.geometry.paths[0].clear();
  f.geometry.path_state[0].clear();
  EXPECT_TRUE(f.rejection().empty());
  EXPECT_EQ(final_net_scope(f.design).required, (std::vector<char>{1}));
  EXPECT_EQ(f.design.nets[0].pins.size(), 2u);
  // A stale local bit must not excuse a net whose second source pin is elsewhere.
  f.boxes.box_of[1] = 1;
  EXPECT_NE(f.rejection().find("no complete backbone"), std::string::npos);
  f.boxes.box_of[1] = -1;
  EXPECT_FALSE(f.rejection().empty());
}

TEST(LegrouteCellboxFlow, SameTileSourceIoRemainsExternalDespiteZeroCoarseEdges) {
  Handoff_fixture    f;
  physical::Terminal terminal;
  terminal.name   = "source_io";
  terminal.net    = f.fp.nets[0].name;
  terminal.x      = 1025;
  terminal.y      = 1125;
  terminal.shapes = {
      {"M4", {-10, -10, 10, 10}}
  };
  f.fp.terminals.push_back(terminal);
  f.fp.nets[0].connections.push_back({"PIN", terminal.name});
  const auto source  = f.fp.nets[0].connections;
  const auto handoff = f.freeze(true);
  ASSERT_EQ(handoff->requests.size(), 1u);
  ASSERT_EQ(handoff->requests[0].endpoints.size(), 1u);
  ASSERT_TRUE(handoff->routes.trees[0].routed);
  ASSERT_TRUE(handoff->routes.trees[0].edges.empty());
  auto  d = f.design();
  Boxes boxes;
  ASSERT_NO_THROW(install_critical_handoff(d, f.grid, boxes));
  Routing         routes;
  Legroute_report report;
  ASSERT_NO_THROW(critical_local_routes(d, f.grid, boxes, routes, report));
  EXPECT_EQ(final_net_scope(d).required, (std::vector<char>{1}));
  EXPECT_EQ(f.fp.nets[0].connections, source);
  ASSERT_EQ(d.nets[0].pins.size(), 2u);
  ASSERT_EQ(routes.trees[0].nodes.size(), 1u);
  const auto& node = routes.trees[0].nodes[0];
  EXPECT_EQ(node.pins, (std::vector<int>{0, 1}));
  EXPECT_GE(node.box, 0);
  EXPECT_EQ(node.term, 0);
  EXPECT_FALSE(node.direct_carrier_tap);
  EXPECT_EQ(routes.local[0], 0);
  EXPECT_TRUE(routes.conns.empty());
  EXPECT_TRUE(routes.segments.empty());
}

TEST(LegrouteCellboxFlow, SameTileSourceIoNeedsCheckedNonemptyCarrierAndStub) {
  for (int mutation = 0; mutation < 8; ++mutation) {
    SCOPED_TRACE(mutation);
    Local_admission_fixture f;
    f.use_io_endpoint();
    f.fp.terminals[0].x = 225;
    auto node           = f.routes.trees[0].nodes[1];
    node.tile = node.box    = 0;
    node.term               = 0;
    node.pins               = {0, 1};
    node.riser_track        = 6;  // Actual M3 x325, with native source M4 access at (225,225).
    f.routes.trees[0].nodes = {node};
    f.routes.trees[0].conns.clear();
    f.routes.conns.clear();
    f.routes.segments.clear();
    f.geometry.paths[0]      = {Domain_fixture::wire("M4", 225, 225, 325, 225), Domain_fixture::wire("M3", 325, 125, 325, 325)};
    f.geometry.path_state[0] = {uint8_t(Track_state::checked), uint8_t(Track_state::checked)};
    f.geometry.joins[0]      = {
        {"M4", "M3", 325, 225}
    };
    ASSERT_TRUE(f.rejection().empty()) << f.rejection();
    switch (mutation) {
      case 0: break;
      case 1:
        f.geometry.paths[0].clear();
        f.geometry.path_state[0].clear();
        break;
      case 2: f.routes.trees[0].nodes[0].riser_state = uint8_t(Track_state::fallback); break;
      case 3: f.routes.trees[0].nodes[0].stub_state = uint8_t(Track_state::fallback); break;
      case 4: f.routes.trees[0].nodes[0].stub_access_state = uint8_t(Track_state::fallback); break;
      case 5: f.routes.trees[0].nodes[0].pins.pop_back(); break;
      case 6: f.fp.terminals[0].x = 200; break;  // No native preferred M3 access intersects this pin.
      case 7:
        f.routes.local[0] = 1;  // A stale local flag cannot turn a source terminal into an internal pin.
        f.geometry.paths[0].clear();
        f.geometry.path_state[0].clear();
        break;
    }
    EXPECT_EQ(f.rejection().empty(), mutation == 0) << f.rejection();
    EXPECT_EQ(final_net_scope(f.design).required, (std::vector<char>{1}));
    EXPECT_EQ(f.fp.nets[0].connections[1].instance, "PIN");
    EXPECT_EQ(f.fp.nets[0].connections[1].pin, "source_io");
    EXPECT_EQ(f.design.nets[0].pins.size(), 2u);
  }
}

TEST(LegrouteCellboxFlow, LocalRejectionPreservesPathPinAndConnectionObligations) {
  for (int mutation = 0; mutation < 7; ++mutation) {
    SCOPED_TRACE(mutation);
    Local_admission_fixture f;
    ASSERT_TRUE(f.rejection().empty());
    switch (mutation) {
      case 0: f.geometry.path_state[0].clear(); break;
      case 1: f.geometry.path_state[0][0] = uint8_t(Track_state::fallback); break;
      case 2: f.geometry.path_state[0][0] = uint8_t(Track_state::none); break;
      case 3: f.routes.trees[0].nodes[1].pins.clear(); break;
      case 4: f.routes.trees[0].nodes[1].pins.push_back(0); break;
      case 5: f.routes.conns[0].segs.clear(); break;
      case 6: f.routes.conns[0].segs = {1}; break;
    }
    EXPECT_FALSE(f.rejection().empty());
    EXPECT_EQ(f.fp.nets[0].connections.size(), 2u);
  }
}

TEST(LegrouteCellboxFlow, LocalIoNeedsActualNativeAccessAndCheckedStubProvenance) {
  for (int mutation = 0; mutation < 5; ++mutation) {
    SCOPED_TRACE(mutation);
    Local_admission_fixture f;
    f.use_io_endpoint();
    ASSERT_TRUE(f.rejection().empty());
    auto& node = f.routes.trees[0].nodes[1];
    switch (mutation) {
      case 0: node.stub_track = -1; break;
      case 1: node.stub_access_state = uint8_t(Track_state::fallback); break;
      case 2: node.riser_state = uint8_t(Track_state::fallback); break;
      case 3: f.fp.terminals[0].x = 600; break;  // No native M3 track intersects this pin rectangle.
      case 4: f.geometry.io_stub_failure[0] = "saved IO materialization failure"; break;
    }
    EXPECT_FALSE(f.rejection().empty());
    EXPECT_EQ(f.fp.nets[0].connections[1].instance, "PIN");
    EXPECT_EQ(f.fp.nets[0].connections[1].pin, "source_io");
  }
}

}  // namespace livehd::placement::lr

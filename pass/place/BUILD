load("//tools:regression.bzl", "cc_test", "py_test")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "rc_cache",
    srcs = ["rc_cache.cpp"],
    hdrs = ["rc_cache.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":steiner_topology"],
)

py_library(
    name = "channel_ir_lib",
    srcs = ["channel_ir.py"],
    imports = ["."],
)

py_binary(
    name = "channel_ir",
    srcs = ["channel_ir.py"],
    legacy_create_init = 0,
)

py_test(
    name = "channel_ir_test",
    srcs = ["channel_ir_test.py"],
    legacy_create_init = 0,
    deps = [":channel_ir_lib"],
)

cc_library(
    name = "handoff_pins",
    srcs = ["handoff_pins.cpp"],
    hdrs = ["handoff_pins.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//inou/lefdef:inou_lefdef"],
)
cc_test(
    name = "handoff_pins_test",
    srcs = ["handoff_pins_test.cpp"],
    copts = COPTS,
    deps = [":handoff_pins", "@googletest//:gtest_main"],
)

filegroup(
    name = "preroute_srcs",
    srcs = [
        "preroute.cpp", "preroute.hpp", "preroute_design.hpp",
        "preroute_host.cpp", "preroute_host.hpp",
        "preroute_repair.cpp", "preroute_repair.hpp",
        "repeater_wire_filter.cpp", "repeater_wire_filter.hpp",
        "preroute_demand.cpp", "preroute_demand.hpp",
        "preroute_timing.cpp", "preroute_timing.hpp",
        "route_capacity.cpp", "route_capacity.hpp",
        "routed_rc.cpp", "routed_rc.hpp",
        "signal_router.cpp", "signal_maze.cpp", "signal_router.hpp",
        "signal_route_profile.cpp", "signal_route_profile.hpp",
        "pin_escape_guard.cpp", "pin_escape_guard.hpp",
        "steiner_net.cpp", "steiner_net.hpp",
        "steiner_topology.cpp", "steiner_topology.hpp",
        "technology_rc.cpp", "technology_rc.hpp",
        "preroute_handoff.tcl", "preroute_validate.tcl", "preroute_reserve.tcl",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "preroute_repair",
    srcs = ["preroute_repair.cpp"],
    hdrs = ["preroute_repair.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":place", ":preroute", ":repeater_wire_filter", "//inou/lefdef:inou_lefdef"],
)

cc_library(
    name = "preroute_host",
    srcs = ["preroute_host.cpp"],
    hdrs = ["preroute_host.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":place", ":prects_networks", ":preroute", ":preroute_repair", ":track_capacity", "//core", "//graph", "//pass/liberty:liberty_dff", "//pass/partition:flatten", "@dreamplace", "@rapidjson"],
)

cc_library(
    name = "preroute_design",
    hdrs = ["preroute_design.hpp"],
    visibility = ["//visibility:public"],
    deps = ["@dreamplace", "//inou/lefdef:inou_lefdef"],
)

cc_library(
    name = "preroute",
    srcs = ["preroute.cpp"],
    hdrs = ["preroute.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":power_grid", ":preroute_design", ":preroute_demand", ":preroute_timing", ":signal_router", ":routed_rc"],
)

cc_library(
    name = "preroute_timing",
    srcs = ["preroute_timing.cpp"],
    hdrs = ["preroute_timing.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//core"],
)

cc_test(
    name = "rc_cache_test",
    size = "small",
    srcs = ["rc_cache_test.cpp"],
    copts = COPTS,
    deps = [":rc_cache", "@googletest//:gtest_main"],
)

cc_library(
    name = "late_timing",
    srcs = ["clock_constraints.cpp", "gate_waveform.cpp", "late_timing.cpp", "rc_tree.cpp", "sdc_subset.cpp", "star_rc.cpp", "wire_waveform.cpp"],
    hdrs = ["clock_constraints.hpp", "gate_waveform.hpp", "late_timing.hpp", "late_timing_json.hpp", "rc_tree.hpp", "sdc_subset.hpp", "star_rc.hpp", "wire_waveform.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":rc_cache", ":require", "//core", "//graph", "//pass/liberty:timing_model", "//pass/liberty:liberty_dff"],
)

# Header-only input validation shared across placement and timing code.
cc_library(
    name = "require",
    hdrs = ["require.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
)

cc_test(
    name = "star_rc_test",
    size = "small",
    srcs = ["star_rc_test.cpp"],
    copts = COPTS,
    deps = [":late_timing", "@googletest//:gtest_main"],
)

cc_library(
    name = "timing_comparison",
    srcs = ["timing_comparison.cpp"],
    hdrs = ["timing_comparison.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":late_timing"],
)

cc_test(
    name = "timing_comparison_test",
    size = "small",
    srcs = ["timing_comparison_test.cpp"],
    copts = COPTS,
    deps = [":timing_comparison", "@googletest//:gtest_main"],
)

cc_test(
    name = "sdc_subset_test",
    size = "small",
    srcs = ["sdc_subset_test.cpp"],
    copts = COPTS,
    deps = [":late_timing", "@googletest//:gtest_main"],
)

cc_test(
    name = "clock_constraints_test",
    size = "small",
    srcs = ["clock_constraints_test.cpp"],
    copts = COPTS,
    deps = [":late_timing", "@googletest//:gtest_main"],
)

cc_test(
    name = "gate_waveform_test",
    size = "small",
    srcs = ["gate_waveform_test.cpp"],
    copts = COPTS,
    deps = [":late_timing", "@googletest//:gtest_main"],
)

cc_test(
    name = "rc_tree_test",
    size = "small",
    srcs = ["rc_tree_test.cpp"],
    copts = COPTS,
    deps = [":late_timing", "@googletest//:gtest_main"],
)

cc_test(
    name = "wire_waveform_test",
    size = "small",
    srcs = ["wire_waveform_test.cpp"],
    copts = COPTS,
    deps = [":late_timing", "@googletest//:gtest_main"],
)

cc_test(
    name = "late_timing_test",
    size = "small",
    srcs = ["late_timing_test.cpp"],
    copts = COPTS,
    data = ["tests/late_timing_characterization.txt"],
    deps = [":late_timing", ":late_timing_dump_fixture", ":timing_circuit_fixture", "@googletest//:gtest_main"],
)

cc_library(
    name = "late_timing_dump_fixture",
    testonly = True,
    hdrs = ["late_timing_dump_fixture.hpp"],
    copts = COPTS,
    deps = [":late_timing"],
)

cc_library(
    name = "timing_circuit_fixture",
    testonly = True,
    hdrs = ["timing_circuit_fixture.hpp"],
    copts = COPTS,
    visibility = [
        "//pass/domplace/timing:__pkg__",
        "//pass/place:__subpackages__",
    ],
    deps = ["//graph", "//pass/liberty:timing_model"],
)

cc_library(
    name = "timeclosure",
    srcs = ["timeclosure.cpp"],
    hdrs = ["timeclosure.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
)

cc_test(
    name = "timeclosure_test",
    size = "small",
    srcs = ["timeclosure_test.cpp"],
    copts = COPTS,
    deps = [":timeclosure", "@googletest//:gtest_main"],
)

cc_library(
    name = "routed_rc",
    srcs = ["routed_rc.cpp"],
    hdrs = ["routed_rc.hpp", "technology_rc.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//core", "//inou/lefdef:inou_lefdef"],
)
cc_test(
    name = "routed_rc_test",
    srcs = ["routed_rc_test.cpp"],
    copts = COPTS,
    deps = [":routed_rc", "@googletest//:gtest_main"],
)

cc_library(
    name = "preroute_demand",
    srcs = ["preroute_demand.cpp"],
    hdrs = ["preroute_demand.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":route_capacity", "@dreamplace"],
)
cc_test(
    name = "preroute_demand_test",
    srcs = ["preroute_demand_test.cpp"],
    copts = COPTS,
    deps = [":preroute_demand", "@googletest//:gtest_main"],
)

cc_library(
    name = "route_capacity",
    srcs = ["route_capacity.cpp"],
    hdrs = ["route_capacity.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//inou/lefdef:inou_lefdef"],
)
cc_test(
    name = "route_capacity_test",
    srcs = ["route_capacity_test.cpp"],
    copts = COPTS,
    deps = [":route_capacity", "@googletest//:gtest_main"],
)

cc_library(
    name = "signal_router",
    srcs = ["signal_router.cpp", "signal_maze.cpp", "signal_route_profile.cpp", "signal_route_domain.cpp", "pin_escape_guard.cpp"],
    hdrs = ["signal_router.hpp", "signal_route_profile.hpp", "signal_route_domain.hpp", "pin_escape_guard.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":steiner_topology", "//inou/lefdef:inou_lefdef"],
)

cc_test(
    name = "signal_route_domain_test",
    size = "small",
    srcs = ["signal_route_domain_test.cpp"],
    copts = COPTS,
    deps = [":signal_router", "@googletest//:gtest_main"],
)
cc_test(
    name = "signal_router_test",
    srcs = ["signal_router_test.cpp"],
    copts = COPTS,
    deps = [":signal_router", ":routed_rc", "@googletest//:gtest_main"],
)

cc_test(
    name = "signal_maze_test",
    srcs = ["signal_maze_test.cpp"],
    copts = COPTS,
    deps = [":signal_router", "@googletest//:gtest_main"],
)

cc_library(
    name = "power_grid",
    srcs = ["power_grid.cpp", "power_grid_profiles.cpp", "power_grid_vias.cpp", "power_grid_export.cpp", "power_grid_rules.cpp"],
    hdrs = ["power_grid.hpp", "power_grid_vias.hpp", "power_grid_rules.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//inou/lefdef:index_union_find", "//inou/lefdef:inou_lefdef", "@rapidjson"],
)

cc_test(
    name = "power_grid_test",
    srcs = ["power_grid_test.cpp"],
    copts = COPTS,
    deps = [":power_grid", "@googletest//:gtest_main"],
)

cc_test(
    name = "power_grid_rules_test",
    srcs = ["power_grid_rules_test.cpp"],
    copts = COPTS,
    deps = [":power_grid", "@googletest//:gtest_main"],
)

cc_test(
    name = "power_grid_vias_test",
    srcs = ["power_grid_vias_test.cpp"],
    copts = COPTS,
    deps = [":power_grid", "@googletest//:gtest_main"],
)

cc_test(
    name = "power_grid_export_test",
    srcs = ["power_grid_export_test.cpp"],
    copts = COPTS,
    deps = [":power_grid", "@googletest//:gtest_main", "@rapidjson"],
)

cc_library(
    name = "repeater_wire_filter",
    srcs = ["repeater_wire_filter.cpp"],
    hdrs = ["repeater_wire_filter.hpp"],
    copts = COPTS,
    deps = ["//inou/lefdef:inou_lefdef"],
)

cc_library(
    name = "handoff_rows",
    srcs = ["handoff_rows.cpp"],
    hdrs = ["handoff_rows.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":repeater_wire_filter", "//inou/lefdef:inou_lefdef"],
)

cc_test(
    name = "handoff_rows_test",
    srcs = ["handoff_rows_test.cpp"],
    copts = COPTS,
    deps = [":handoff_rows", "@googletest//:gtest_main"],
)

filegroup(
    name = "place_srcs",
    srcs = [
        "//pass/place/pcrs:clock_placement_srcs",
        "//core:metadata_srcs",
        "fp_repetition.cpp",
        "fp_repetition.hpp",
        "fp_repetition_graph.cpp",
        "fp_repetition_graph.hpp",
        "area_fit.cpp",
        "area_fit.hpp",
        "handoff_rows.cpp",
        "handoff_rows.hpp",
        "repeater_wire_filter.cpp",
        "repeater_wire_filter.hpp",
        "handoff_pins.cpp",
        "handoff_pins.hpp",
        "preroute_design.hpp",
        "power_grid.cpp",
        "power_grid.hpp",
        "power_grid_profiles.cpp",
        "power_grid_export.cpp",
        "power_grid_vias.cpp",
        "power_grid_vias.hpp",
        "power_grid_rules.cpp",
        "power_grid_rules.hpp",
        "placement_session.cpp",
        "planning_selection.cpp",
        "boundary_pins.cpp",
        "pinalign.cpp",
        "busorder.cpp",
        "placement_session.hpp",
        "planning_selection.hpp",
        "boundary_pins.hpp",
        "busorder.hpp",
        "region_mesh.cpp",
        "region_mesh.hpp",
        "capacity_bins.cpp",
        "capacity_bins.hpp",
        "row_occupancy.cpp",
        "row_occupancy.hpp",
        "custom_cells.cpp",
        "custom_cells.hpp",
        "multirow.cpp",
        "multirow.hpp",
        "multirow_eco.cpp",
        "multirow_eco.hpp",
        "multirow_packer.hpp",
        "cell_criticality.cpp",
        "cell_criticality.hpp",
        "cell_jump.cpp",
        "cell_jump.hpp",
        "color_centroids.cpp",
        "color_centroids.hpp",
        "h2d.cpp",
        "h2d.hpp",
        "io_timing.cpp",
        "io_timing.hpp",
        "macro_dead_space.cpp",
        "macro_dead_space.hpp",
        "macro_anchor.cpp",
        "macro_anchor.hpp",
        "macro_edge.cpp",
        "tile_spread.cpp",
        "macro_edge.hpp",
        "tile_spread.hpp",
        "macro_snap.cpp",
        "macro_tracks.cpp",
        "macro_snap.hpp",
        "macro_state.hpp",
        "macro_tracks.hpp",
        "magnet_report.cpp",
        "gui/gui_gp_options.hpp",
        "gui/gui_gp_reports.hpp",
        "gui/gui_gp_owner.hpp",
        "gui/gui_native_wire.hpp",
        "magnet_report.hpp",
        "memory_proxy.cpp",
        "memory_proxy.hpp",
        "pin_adjust.cpp",
        "pin_adjust.hpp",
        "prects_networks.cpp",
        "prects_networks.hpp",
        "gui/gui_gp_owner.cpp",
        "place.cpp",
        "placement_hints.cpp",
        "centroid_record.cpp",
        "centroid_record.hpp",
        "placement_technology.cpp",
        "place.hpp",
        "placement_hints.hpp",
        "def_binding.hpp",
        "placement_technology.hpp",
        "growth_budget.cpp",
        "growth_budget.hpp",
        "rc_cache.cpp",
        "rc_cache.hpp",
        "clock_constraints.cpp",
        "clock_constraints.hpp",
        "filler_budget.cpp",
        "filler_budget.hpp",
        "gate_waveform.cpp",
        "gate_waveform.hpp",
        "late_timing.cpp",
        "late_timing.hpp",
        "late_timing_json.hpp",
        "require.hpp",
        "rc_tree.cpp",
        "rc_tree.hpp",
        "sdc_subset.cpp",
        "sdc_subset.hpp",
        "star_rc.cpp",
        "star_rc.hpp",
        "timing_comparison.cpp",
        "timing_comparison.hpp",
        "wire_waveform.cpp",
        "wire_waveform.hpp",
        "//pass/place/forces:timing_sources",
        "//pass/place/buffering:timing_sources",
        "//pass/place/objective:timing_sources",
        "//pass/liberty:timing_model_sources",
        "repair.cpp",
        "repair.hpp",
        "repair_internal.hpp",
        "wirelength_factor.hpp",
        "wirelength_register.hpp",
        "reset_tree.cpp",
        "reset_tree.hpp",
        "custom_sizing.cpp",
        "custom_sizing.hpp",
        "resize_engine.cpp",
        "resize_engine.hpp",
        "satlegalize.cpp",
        "satlegalize.hpp",
        "sat_solver.cpp",
        "sat_solver.hpp",
        "legroute.hpp",
        "legroute_clock.cpp",
        "legroute_global.cpp",
        "fast_route_pattern.cpp",
        "fast_route_pattern.hpp",
        "legroute_grid.cpp",
        "legroute_internal.hpp",
        "legroute_plan.cpp",
        "legroute_congestion.cpp",
        "resynthesize.cpp",
        "wirelength_resynth.cpp",
        "clock_hold.cpp",
        "clock_hold.hpp",
        "clock_network.cpp",
        "clock_bounds.cpp",
        "clock_bounds.hpp",
        "clock_clone.cpp",
        "clock_clone.hpp",
        "clock_schedule.cpp",
        "clock_schedule.hpp",
        "clock_model.hpp",
        "wirelength_factor.cpp",
        "wirelength_register.cpp",
        "steiner_repair.cpp",
        "steiner_repair.hpp",
        "io_spacing.hpp",
        "routing_tracks.cpp",
        "technology_rc.cpp",
        "routing_tracks.hpp",
        "technology_rc.hpp",
        "track_capacity.hpp",
        "timing_place.cpp",
        "terminal_legalize.cpp",
        "terminal_legalize.hpp",
        "pg_rails.cpp",
        "pg_rails.hpp",
    ],
    visibility = ["//visibility:public"],
)

genrule(
    name = "place_salt",
    srcs = [
        ":place_srcs",
        ":preroute_srcs",
        "//:MODULE.bazel",
        "//inou/lefdef:physical_srcs",
        "//packages:cvc5_sat.cpp",
        "//packages:cvc5_repo.bzl",
        "//packages:cvc5_src.BUILD",
        "//graph",
        "//inou/lefdef:inou_lefdef",
        "//pass/liberty:liberty_dff",
        "//pass/color:floorplan_color_srcs",
        "//pass/partition:partition_srcs",
        "@dreamplace",
        "@dreamplace//:macro_place",
        "@dreamplace//:macro_hhds",
        "@dreamplace//:macro_report",
        "@dreamplace//:macro_render",
        "@dreamplace//dreamplace/native:placer_srcs",
        "@dreamplace//dreamplace/ops:native_kernels",
        "@dreamplace//dreamplace/native/macro:h2d_srcs",
    ],
    outs = ["place_salt.hpp"],
    cmd = "h=$$(cat $(SRCS) | (shasum -a 256 2>/dev/null || sha256sum) | cut -c1-16); " +
          "printf '#pragma once\\n#include <cstdint>\\nnamespace livehd::placement { inline constexpr uint64_t kPlaceSalt = 0x%sULL; }\\n' \"$$h\" > $@",
)

cc_library(
    name = "filler_budget",
    srcs = ["filler_budget.cpp"],
    hdrs = ["filler_budget.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["@dreamplace//dreamplace/native:placer"],
)
cc_test(
    name = "filler_budget_test",
    size = "small",
    srcs = ["filler_budget_test.cpp"],
    copts = COPTS,
    deps = [":filler_budget", "@googletest//:gtest_main"],
)

cc_library(
    name = "region_mesh",
    srcs = ["region_mesh.cpp"],
    hdrs = ["region_mesh.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["@dreamplace//dreamplace/native:placer", "@rapidjson"],
)
cc_test(
    name = "region_mesh_test",
    size = "small",
    srcs = ["region_mesh_test.cpp"],
    copts = COPTS,
    deps = [":region_mesh", "@googletest//:gtest_main"],
)

cc_library(
    name = "capacity_bins",
    srcs = ["capacity_bins.cpp"],
    hdrs = ["capacity_bins.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//inou/lefdef:inou_lefdef"],
)
cc_test(
    name = "capacity_bins_test",
    size = "small",
    srcs = ["capacity_bins_test.cpp"],
    copts = COPTS,
    deps = [":capacity_bins", "@googletest//:gtest_main"],
)

cc_library(
    name = "row_occupancy",
    srcs = ["row_occupancy.cpp"],
    hdrs = ["row_occupancy.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":capacity_bins", "//inou/lefdef:inou_lefdef"],
)
cc_test(
    name = "row_occupancy_test",
    size = "small",
    srcs = ["row_occupancy_test.cpp"],
    copts = COPTS,
    deps = [":row_occupancy", "@googletest//:gtest_main"],
)

# Same-interface custom cell variants (2d-domplace G1): data and the swap.
cc_library(
    name = "custom_cells",
    srcs = ["custom_cells.cpp"],
    hdrs = ["custom_cells.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//core", "//pass/liberty:liberty_dff", "@hhds//hhds:core"],
)
cc_test(
    name = "custom_cells_test",
    size = "small",
    srcs = ["custom_cells_test.cpp"],
    copts = COPTS,
    deps = [
        ":custom_cells",
        "//graph",
        "//pass/usyn:usyn_semantic",  # the generator's declaration, for comparison only
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "multirow",
    srcs = [
        "multirow.cpp",
        "multirow_eco.cpp",
        "multirow_packer.hpp",
        "pg_rails.cpp",
    ],
    hdrs = [
        "multirow.hpp",
        "multirow_eco.hpp",
        "pg_rails.hpp",
    ],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":capacity_bins", ":require", ":row_occupancy", "//inou/lefdef:inou_lefdef"],
)
cc_test(
    name = "multirow_test",
    size = "small",
    srcs = [
        "multirow_test.cpp",
        "multirow_test_frame.hpp",
    ],
    copts = COPTS,
    deps = [":multirow", ":place", ":terminal_legalize", "@googletest//:gtest_main"],
)
cc_test(
    name = "multirow_eco_test",
    size = "small",
    srcs = [
        "multirow_eco_test.cpp",
        "multirow_test_frame.hpp",
    ],
    copts = COPTS,
    deps = [":multirow", ":place", "@googletest//:gtest_main"],
)

cc_library(
    name = "cell_criticality",
    srcs = ["cell_criticality.cpp"],
    hdrs = ["cell_criticality.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//core", "//inou/lefdef:inou_lefdef", "@dreamplace//dreamplace/native:placer"],
)

cc_library(
    name = "fp_repetition",
    srcs = ["fp_repetition.cpp"],
    hdrs = ["fp_repetition.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["@rapidjson"],
)

cc_test(
    name = "fp_repetition_test",
    size = "small",
    srcs = ["fp_repetition_test.cpp"],
    copts = COPTS,
    deps = [":fp_repetition", "@googletest//:gtest_main"],
)

cc_library(
    name = "macro_dead_space",
    srcs = ["macro_dead_space.cpp"],
    hdrs = ["macro_dead_space.hpp"],
    copts = COPTS,
    deps = ["@dreamplace//dreamplace/native:placer"],
)

# The centroid helper's record, its validation and its compilation to the native placer's cohesion groups (todo 2p-pcrs, G1/G2).
cc_library(
    name = "centroid_record",
    srcs = ["centroid_record.cpp"],
    hdrs = ["centroid_record.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["@dreamplace//dreamplace/native:placer", "@rapidjson"],
)

# 2i-interact: the interactive placement session, its HTTP server and the embedded page.
cc_library(
    name = "gui",
    srcs = ["gui/gui_worker.cpp","gui/gui_geometry.cpp","gui/gui_session.cpp", "gui/gui_server.cpp", "gui/gui_page.cpp", "gui/gui_render.cpp", "gui/gui_agent.cpp", "gui/gui_ipc.cpp", "gui/gui_scan.cpp", "gui/gui_clock.cpp", "gui/gui_centroid.cpp"],
    hdrs = ["gui/gui_clock.hpp", "gui/gui_centroid.hpp", "gui/gui_scan.hpp", "gui/gui_tracks.hpp", "gui/gui_chunks.hpp", "macro_state.hpp", "gui/gui_policy.hpp","gui/gui_worker.hpp","gui/gui_geometry.hpp","gui/gui_session.hpp", "gui/gui_page.hpp", "gui/gui_render.hpp", "gui/gui_ipc.hpp", "gui/gui_wire.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":centroid_record", ":macro_dead_space", "//core", "@rapidjson", "@re2//:re2"],
)

cc_test(
    name = "gui_session_test",
    srcs = ["gui/gui_session_test.cpp"],
    copts = COPTS,
    deps = [":gui", "@googletest//:gtest_main"],
)

cc_test(
    name = "gui_centroid_test",
    srcs = ["gui/gui_centroid_test.cpp"],
    copts = COPTS,
    deps = [":gui", "@googletest//:gtest_main"],
)

cc_test(
    name = "gui_clock_test",
    srcs = ["gui/gui_clock_test.cpp"],
    copts = COPTS,
    deps = [":gui", "@googletest//:gtest_main"],
)

cc_test(
    name = "gui_native_gp_smoke",
    srcs = ["gui/gui_native_gp_smoke.cpp"],
    args = ["$(location //lhd:lhd)"],
    data = ["//lhd:lhd"],
    copts = COPTS,
    deps = [":place", ":gui", "@dreamplace"],
)

cc_library(
    name = "fp_chunks",
    srcs = ["fp_chunks.cpp", "chunk_family.cpp", "chunk_regions.cpp", "chunk_geometry.cpp", "chunk_demand.cpp", "chunk_topology.cpp", "chunk_templates.cpp", "chunk_refine.cpp", "chunk_jobs.cpp", "chunk_measure.cpp", "chunk_memory.cpp"],
    hdrs = ["fp_chunks.hpp", "chunk_family.hpp", "io_spacing.hpp", "chunk_geometry.hpp", "chunk_demand.hpp", "chunk_topology.hpp", "chunk_templates.hpp", "chunk_refine.hpp", "chunk_jobs.hpp", "chunk_measure.hpp", "chunk_memory.hpp"],
    copts = COPTS,
    deps = [":centroid_record", ":fp_repetition", "//core", "@rapidjson", "@dreamplace"],
)

cc_binary(
    name = "fp_plan_summary",
    srcs = ["fp_plan_summary.cpp"],
    visibility = ["//visibility:public"],
    copts = COPTS,
    deps = [":fp_chunks"],
)

py_test(
    name = "fp_plan_summary_smoke",
    legacy_create_init = 0,
    size = "small",
    srcs = ["tests/fp_plan_summary_smoke.py"],
    main = "tests/fp_plan_summary_smoke.py",
    data = [":fp_plan_summary"],
)

cc_test(
    name = "fp_chunks_test",
    srcs = ["fp_chunks_test.cpp"],
    copts = COPTS,
    deps = [":fp_chunks", "@googletest//:gtest_main"],
)

cc_library(
    name = "place",
    srcs = [
        "area_fit.cpp",
        "cell_jump.cpp",
        "placement_session.cpp",
        "planning_selection.cpp",
        "boundary_pins.cpp",
        "pinalign.cpp",
        "busorder.cpp",
        "color_centroids.cpp",
        "h2d.cpp",
        "io_timing.cpp",
        "macro_anchor.cpp",
        "macro_edge.cpp",
        "tile_spread.cpp",
        "macro_snap.cpp",
        "macro_tracks.cpp",
        "magnet_report.cpp",
        "fp_repetition_graph.cpp",
        "memory_proxy.cpp",
        "pin_adjust.cpp",
        "gui/gui_gp_owner.cpp",
        "place.cpp",
        "placement_hints.cpp",
        "placement_technology.cpp",
        "repair.cpp",
        "growth_budget.cpp",
        "reset_tree.cpp",
        "custom_sizing.cpp",
        "resize_engine.cpp",
        "resynthesize.cpp",
        "wirelength_resynth.cpp",
        "clock_network.cpp",
        "clock_clone.cpp",
        "clock_clone.hpp",
        "clock_schedule.cpp",
        "clock_schedule.hpp",
        "clock_model.hpp",
        "wirelength_factor.cpp",
        "wirelength_register.cpp",
        "steiner_repair.cpp",
        "routing_tracks.cpp",
        "timing_place.cpp",
    ],
    hdrs = [
        "area_fit.hpp",
        "cell_jump.hpp",
        "placement_session.hpp",
        "planning_selection.hpp",
        "boundary_pins.hpp",
        "busorder.hpp",
        "color_centroids.hpp",
        "h2d.hpp",
        "io_timing.hpp",
        "macro_anchor.hpp",
        "macro_edge.hpp",
        "tile_spread.hpp",
        "macro_snap.hpp",
        "macro_state.hpp",
        "macro_tracks.hpp",
        "gui/gui_gp_options.hpp",
        "gui/gui_gp_reports.hpp",
        "gui/gui_gp_owner.hpp",
        "gui/gui_native_wire.hpp",
        "magnet_report.hpp",
        "fp_repetition_graph.hpp",
        "memory_proxy.hpp",
        "pin_adjust.hpp",
        "place.hpp",
        "placement_hints.hpp",
        "def_binding.hpp",
        "placement_technology.hpp",
        "repair.hpp",
        "growth_budget.hpp",
        "repair_internal.hpp",
        "wirelength_factor.hpp",
        "wirelength_register.hpp",
        "reset_tree.hpp",
        "custom_sizing.hpp",
        "resize_engine.hpp",
        "steiner_repair.hpp",
        "routing_tracks.hpp",
        ":place_salt",
    ],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":row_occupancy",
        "//pass/usyn:usyn_logic",
        ":centroid_record",
        ":gui",
        ":macro_dead_space",
        ":region_mesh",
        ":custom_cells",
        ":multirow",
        ":cell_criticality",
        ":track_capacity",
        "//pass/place/buffering:checkpoint",
        ":filler_budget",
        ":signal_router",
        ":handoff_rows",
        ":steiner_topology",
        ":clock_hold",
        ":clock_bounds",
        ":technology_rc",
        "//pass/timing:physical_rank",
        "//pass/timing:fast_timing",
        ":handoff_pins",
        ":preroute_design",
        ":power_grid",
        ":rc_cache",
        ":prects_networks",
        ":satlegalize",
        ":terminal_legalize",
        ":legroute",
        ":fp_repetition",
        ":fp_chunks",
        "//pass/place/pcrs",
        "//pass/liberty:timing_model",
        "//core",
        "//graph",
        "//inou/lefdef:index_union_find",
        "//inou/lefdef:inou_lefdef",
        "//pass/color:floorplan_color",
        "//pass/place/forces:gp_timing",
        "//pass/place/forces:clock_cluster",
        "//pass/place/forces:gp_rejection",
        "//pass/place/forces:legalization",
        "//pass/liberty:liberty_dff",
        "//pass/partition:flatten",
        "//pass/synth",
        "@dreamplace",
        "@dreamplace//:macro_hhds",
        "@dreamplace//:macro_place",
        "@dreamplace//:macro_render",
        "@dreamplace//:macro_report",
        "@rapidjson",
    ],
)

cc_test(
    name = "pinalign_test",
    size = "small",
    srcs = ["pinalign_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "cell_jump_test",
    size = "small",
    srcs = ["cell_jump_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "boundary_pins_test",
    size = "small",
    srcs = ["boundary_pins_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "busorder_test",
    size = "small",
    srcs = ["busorder_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "place_test",
    srcs = [
        "place_test.cpp",
        "place_test_fixture.hpp",
    ],
    copts = COPTS,
    data = ["//inou/lefdef:test_data"],
    deps = [
        ":place",
        ":preroute",
        "//pass/opentimer:pass_opentimer",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "io_timing_test",
    size = "small",
    srcs = ["io_timing_test.cpp"],
    copts = COPTS,
    data = [
        "//inou/lefdef:test_data",
        "//inou/prp:abc_tests",
    ],
    deps = [
        ":place",
        "//pass/abc:pass_abc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "steiner_replay_test",
    size = "small",
    srcs = ["steiner_replay_test.cpp"],
    copts = COPTS,
    tags = ["manual"],  # a debugging harness: set LHD_STEINER_REPLAY to a LHD_STEINER_DUMP file
    deps = [
        ":place",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "reset_tree_test",
    size = "small",
    srcs = ["reset_tree_test.cpp"],
    copts = COPTS,
    deps = [
        ":place",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "color_centroids_test",
    srcs = ["color_centroids_test.cpp"],
    copts = COPTS,
    deps = [
        ":place",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "repair_test",
    size = "small",
    srcs = ["repair_test.cpp"],
    copts = COPTS,
    data = ["//inou/lefdef:test_data"],
    deps = [
        ":place",
        "//pass/abc:pass_abc",
        "//pass/opentimer:pass_opentimer",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "technology_rc_test",
    srcs = ["technology_rc_test.cpp"],
    copts = COPTS,
    deps = [
        ":place",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "pin_adjust_test",
    srcs = ["pin_adjust_test.cpp"],
    copts = COPTS,
    deps = [
        ":place",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "macro_dead_space_test",
    size = "small",
    srcs = ["macro_dead_space_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "macro_anchor_test",
    size = "small",
    srcs = ["macro_anchor_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "area_fit_test",
    size = "small",
    srcs = [
        "area_fit_test.cpp",
        "place_test_fixture.hpp",
    ],
    copts = COPTS,
    data = ["//inou/lefdef:test_data"],
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "macro_edge_test",
    size = "small",
    srcs = ["macro_edge_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "tile_spread_test",
    size = "small",
    srcs = ["tile_spread_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "macro_snap_test",
    srcs = ["macro_snap_test.cpp"],
    copts = COPTS,
    deps = [
        ":place",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "custom_sizing_test",
    size = "small",
    srcs = [
        "custom_sizing_test.cpp",
        "place_test_fixture.hpp",
    ],
    copts = COPTS,
    data = ["//inou/lefdef:test_data"],
    deps = [
        ":place",
        "//inou/lefdef:inou_lefdef",
        "@googletest//:gtest_main",
        "@rapidjson",
    ],
)

cc_test(
    name = "resize_engine_test",
    size = "small",
    srcs = ["resize_engine_test.cpp"],
    copts = COPTS,
    data = ["//inou/lefdef:test_data"],
    deps = [
        ":place",
        "//pass/opentimer:pass_opentimer",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "sat_solver",
    srcs = ["sat_solver.cpp"],
    hdrs = ["sat_solver.hpp"],
    copts = COPTS,
    deps = ["@cvc5//:sat_solver"],
)
cc_library(
    name = "terminal_legalize",
    srcs = ["terminal_legalize.cpp"],
    hdrs = ["terminal_legalize.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":capacity_bins", ":cell_criticality", ":row_occupancy", ":prects_networks", ":satlegalize", ":sat_solver", "//core", "//inou/lefdef:inou_lefdef", "@dreamplace//dreamplace/native:placer", "@rapidjson"],
)
cc_test(
    name = "terminal_legalize_test",
    size = "small",
    srcs = ["terminal_legalize_test.cpp"],
    copts = COPTS,
    deps = [":terminal_legalize", "@googletest//:gtest_main"],
)
cc_library(
    name = "satlegalize",
    visibility = ["//tools:__pkg__"],
    srcs = ["satlegalize.cpp"],
    hdrs = ["satlegalize.hpp"],
    copts = COPTS,
    deps = [":cell_criticality", ":prects_networks", ":sat_solver", ":signal_router", "//core", "//inou/lefdef:inou_lefdef"],
)
# legroute: the routability step at the end of placement; today its minimal core (todo/livehd/2r-route.html)
cc_library(
    name = "legroute",
    srcs = [
        "legroute_clock.cpp",
        "legroute_congestion.cpp",
        "legroute_global.cpp",
        "legroute_grid.cpp",
        "legroute_internal.hpp",
        "legroute_plan.cpp",
    ],
    hdrs = ["legroute.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":fast_route_pattern",
        ":row_occupancy",
        ":steiner_topology",
        "//core",
        "//inou/lefdef:inou_lefdef",
    ],
)
cc_library(
    name = "legroute_test_frame",
    testonly = True,
    hdrs = ["legroute_internal.hpp", "legroute_test_frame.hpp"],
    deps = [":legroute", "//inou/lefdef:inou_lefdef"],
)
cc_test(
    name = "legroute_grid_test",
    size = "small",
    srcs = ["legroute_grid_test.cpp"],
    copts = COPTS,
    deps = [":legroute_test_frame", "@googletest//:gtest_main"],
)
cc_test(
    name = "legroute_clock_test",
    size = "small",
    srcs = ["legroute_clock_test.cpp"],
    copts = COPTS,
    deps = [":legroute_test_frame", "@googletest//:gtest_main"],
)
cc_test(
    name = "legroute_plan_test",
    size = "small",
    srcs = ["legroute_plan_test.cpp"],
    copts = COPTS,
    deps = [":legroute_test_frame", "@googletest//:gtest_main", "@rapidjson"],
)
cc_test(
    name = "legroute_congestion_test",
    size = "small",
    srcs = ["legroute_congestion_test.cpp", "legroute_internal.hpp"],
    copts = COPTS,
    deps = [":legroute", "@googletest//:gtest_main"],
)
cc_test(
    name = "legroute_global_test",
    size = "small",
    srcs = ["legroute_global_test.cpp", "legroute_internal.hpp"],
    copts = COPTS,
    deps = [":legroute", "@googletest//:gtest_main"],
)
cc_test(
    name = "sat_solver_test",
    srcs = ["sat_solver_test.cpp"],
    copts = COPTS,
    deps = [":sat_solver", "@cvc5", "@abc", "@googletest//:gtest_main"],
)
cc_test(
    name = "satlegalize_test",
    srcs = ["satlegalize_test.cpp"],
    copts = COPTS,
    deps = [":prects_networks", ":satlegalize", "@googletest//:gtest_main"],
)

cc_test(
    name = "macro_tracks_test",
    size = "small",
    srcs = ["macro_tracks_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "magnet_report_test",
    size = "small",
    srcs = ["magnet_report_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main", "@rapidjson"],
)

cc_library(
    name = "track_capacity",
    hdrs = ["track_capacity.hpp"],
    visibility = ["//visibility:public"],
    deps = ["//inou/lefdef:inou_lefdef"],
)

cc_library(
    name = "prects_networks",
    srcs = ["prects_networks.cpp"],
    hdrs = ["prects_networks.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//core", "//inou/lefdef:inou_lefdef", "//pass/liberty:liberty_dff", "@abseil-cpp//absl/container:flat_hash_map", "@abseil-cpp//absl/container:flat_hash_set"],
)
cc_test(
    name = "prects_networks_test",
    size = "small",
    srcs = ["prects_networks_test.cpp"],
    copts = COPTS,
    deps = [":prects_networks", "@googletest//:gtest_main"],
)

cc_test(
    name = "routing_tracks_test",
    size = "small",
    srcs = ["routing_tracks_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_library(
    name = "clock_bounds",
    srcs = ["clock_bounds.cpp"],
    hdrs = ["clock_bounds.hpp"],
    copts = COPTS,
    deps = ["//core"],
)

cc_library(
    name = "clock_hold",
    srcs = ["clock_hold.cpp"],
    hdrs = ["clock_hold.hpp"],
    copts = COPTS,
    deps = [":clock_bounds", ":row_occupancy", ":satlegalize", ":technology_rc", "//core", "//inou/lefdef:inou_lefdef", "//pass/place/pcrs"],
)

cc_test(
    name = "clock_hold_test",
    size = "small",
    srcs = ["clock_hold_test.cpp"],
    copts = COPTS,
    deps = [":clock_hold", "@googletest//:gtest_main"],
)

cc_test(
    name = "clock_gate_merge_test",
    size = "small",
    srcs = ["clock_gate_merge_test.cpp"],
    data = ["//inou/lefdef:test_data", "tests/clock_gate_pair.def"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main", "@rapidjson"],
)

cc_test(
    name = "clock_bounds_test",
    size = "small",
    srcs = ["clock_bounds_test.cpp"],
    copts = COPTS,
    deps = [":clock_bounds", "@googletest//:gtest_main"],
)

cc_library(
    name = "technology_rc",
    srcs = ["technology_rc.cpp"],
    hdrs = ["technology_rc.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":steiner_topology", "//core", "//inou/lefdef:inou_lefdef"],
)

cc_library(
    name = "steiner_topology",
    srcs = ["steiner_net.cpp", "steiner_topology.cpp"],
    hdrs = ["steiner_net.hpp", "steiner_topology.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//inou/lefdef:index_union_find", "@dreamplace//thirdparty/flute"],
)

cc_library(
    name = "fast_route_pattern",
    srcs = ["fast_route_pattern.cpp"],
    hdrs = ["fast_route_pattern.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
)

cc_test(
    name = "fast_route_pattern_test",
    size = "small",
    srcs = ["fast_route_pattern_test.cpp"],
    copts = COPTS,
    deps = [":fast_route_pattern", "@googletest//:gtest_main"],
)

cc_test(
    name = "steiner_choice_test",
    size = "small",
    srcs = ["steiner_choice_test.cpp"],
    copts = COPTS,
    data = ["tests/steiner_chain_net.txt"],
    deps = [
        ":place",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "steiner_topology_test",
    srcs = ["steiner_topology_test.cpp"],
    copts = COPTS,
    deps = [":steiner_topology", "@googletest//:gtest_main"],
)

cc_test(
    name = "pin_escape_guard_test",
    srcs = ["pin_escape_guard_test.cpp"],
    copts = COPTS,
    deps = [":signal_router", "@googletest//:gtest_main"],
)

cc_test(
    name = "placement_technology_test",
    srcs = ["placement_technology_test.cpp"],
    copts = COPTS,
    data = ["//inou/lefdef:test_data"],
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "growth_budget_test",
    srcs = ["growth_budget_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

cc_test(
    name = "centroid_record_test",
    srcs = ["centroid_record_test.cpp"],
    copts = COPTS,
    deps = [":centroid_record", "@googletest//:gtest_main"],
)

cc_test(
    name = "placement_hints_test",
    srcs = ["placement_hints_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

py_test(
    name = "legroute_default_smoke",
    srcs = ["tests/legroute_default_smoke.py"],
    main = "tests/legroute_default_smoke.py",
    legacy_create_init = 0,
    size = "small",
    data = [
        "//lhd:lhd",
        "//pass/place/tests/regularity:hint_fixtures",
    ],
)

py_test(
    name = "placement_hints_smoke",
    srcs = ["tests/placement_hints_smoke.py"],
    main = "tests/placement_hints_smoke.py",
    legacy_create_init = 0,
    size = "small",
    data = [
        "//lhd:lhd",
        "//pass/place/tests/regularity:hint_fixtures",
    ],
)

cc_test(
    name = "planning_selection_test",
    srcs = ["planning_selection_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main", "@rapidjson"],
)

cc_test(
    name = "placement_session_test",
    srcs = ["placement_session_test.cpp"],
    copts = COPTS,
    deps = [":place", "@googletest//:gtest_main"],
)

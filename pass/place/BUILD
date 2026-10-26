load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "rc_cache",
    srcs = ["rc_cache.cpp"],
    hdrs = ["rc_cache.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//core"],
)

py_library(
    name = "channel_ir_lib",
    srcs = ["channel_ir.py"],
    imports = ["."],
)

py_binary(
    name = "channel_ir",
    srcs = ["channel_ir.py"],
)

py_test(
    name = "channel_ir_test",
    srcs = ["channel_ir_test.py"],
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
    deps = [":place", ":prects_networks", ":preroute", ":preroute_repair", "//core", "//graph", "//pass/liberty:liberty_dff", "//pass/partition:flatten", "@dreamplace", "@rapidjson"],
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
    hdrs = ["clock_constraints.hpp", "gate_waveform.hpp", "late_timing.hpp", "rc_tree.hpp", "sdc_subset.hpp", "star_rc.hpp", "wire_waveform.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":rc_cache", "//core", "//graph", "//pass/liberty:timing_model", "//pass/liberty:liberty_dff"],
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
    deps = [":late_timing", "@googletest//:gtest_main"],
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
    srcs = ["signal_router.cpp", "signal_maze.cpp", "signal_route_profile.cpp", "pin_escape_guard.cpp"],
    hdrs = ["signal_router.hpp", "signal_route_profile.hpp", "pin_escape_guard.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":steiner_topology", "//inou/lefdef:inou_lefdef"],
)

cc_test(
    name = "signal_router_test",
    srcs = ["signal_router_test.cpp"],
    copts = COPTS,
    deps = [":signal_router", ":routed_rc", "@googletest//:gtest_main"],
)

cc_library(
    name = "power_grid",
    srcs = ["power_grid.cpp", "power_grid_profiles.cpp", "power_grid_vias.cpp", "power_grid_export.cpp"],
    hdrs = ["power_grid.hpp", "power_grid_vias.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//inou/lefdef:inou_lefdef", "@rapidjson"],
)

cc_test(
    name = "power_grid_test",
    srcs = ["power_grid_test.cpp"],
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
        "boundary_pins.cpp",
        "pinalign.cpp",
        "busorder.cpp",
        "boundary_pins.hpp",
        "busorder.hpp",
        "cell_jump.cpp",
        "cell_jump.hpp",
        "density_warp.cpp",
        "density_warp.hpp",
        "warpspace.cpp",
        "warpspace.hpp",
        "color_centroids.cpp",
        "color_centroids.hpp",
        "h2d.cpp",
        "h2d.hpp",
        "io_timing.cpp",
        "io_timing.hpp",
        "macro_snap.cpp",
        "macro_tracks.cpp",
        "macro_snap.hpp",
        "macro_tracks.hpp",
        "magnet_report.cpp",
        "magnet_report.hpp",
        "memory_proxy.cpp",
        "memory_proxy.hpp",
        "pin_adjust.cpp",
        "pin_adjust.hpp",
        "prects_networks.cpp",
        "prects_networks.hpp",
        "place.cpp",
        "place.hpp",
        "uroute.cpp",
        "uroute.hpp",
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
        "reset_tree.cpp",
        "reset_tree.hpp",
        "resize_engine.cpp",
        "resize_engine.hpp",
        "satlegalize.cpp",
        "satlegalize.hpp",
        "sat_solver.cpp",
        "sat_solver.hpp",
        "resynthesize.cpp",
        "steiner_repair.cpp",
        "steiner_repair.hpp",
        "routing_tracks.cpp",
        "technology_rc.cpp",
        "routing_tracks.hpp",
        "technology_rc.hpp",
        "timing_place.cpp",
        "terminal_legalize.cpp",
        "terminal_legalize.hpp",
    ],
    visibility = ["//visibility:public"],
)

genrule(
    name = "place_salt",
    srcs = [
        ":place_srcs",
        "//:MODULE.bazel",
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
    name = "place",
    srcs = [
        "cell_jump.cpp",
        "boundary_pins.cpp",
        "pinalign.cpp",
        "busorder.cpp",
        "color_centroids.cpp",
        "h2d.cpp",
        "io_timing.cpp",
        "macro_snap.cpp",
        "macro_tracks.cpp",
        "magnet_report.cpp",
        "memory_proxy.cpp",
        "pin_adjust.cpp",
        "place.cpp",
        "repair.cpp",
        "reset_tree.cpp",
        "resize_engine.cpp",
        "resynthesize.cpp",
        "steiner_repair.cpp",
        "routing_tracks.cpp",
        "technology_rc.cpp",
        "timing_place.cpp",
    ],
    hdrs = [
        "cell_jump.hpp",
        "boundary_pins.hpp",
        "busorder.hpp",
        "color_centroids.hpp",
        "h2d.hpp",
        "io_timing.hpp",
        "macro_snap.hpp",
        "macro_tracks.hpp",
        "magnet_report.hpp",
        "memory_proxy.hpp",
        "pin_adjust.hpp",
        "place.hpp",
        "repair.hpp",
        "repair_internal.hpp",
        "reset_tree.hpp",
        "resize_engine.hpp",
        "steiner_repair.hpp",
        "routing_tracks.hpp",
        "technology_rc.hpp",
        ":place_salt",
    ],
    copts = COPTS,
    data = [":uroute_pretrained"],
    visibility = ["//visibility:public"],
    deps = [
        ":uroute",
        "//pass/place/buffering:checkpoint",
        ":filler_budget",
        ":signal_router",
        ":handoff_rows",
        ":steiner_topology",
        ":handoff_pins",
        ":preroute_design",
        ":power_grid",
        ":warpspace",
        ":rc_cache",
        ":prects_networks",
        ":satlegalize",
        ":terminal_legalize",
        "//core",
        "//graph",
        "//inou/lefdef:inou_lefdef",
        "//pass/color:floorplan_color",
        "//pass/place/forces:gp_timing",
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
        "@dreamplace//thirdparty/flute",
        "@rapidjson",
        "@rules_cc//cc/runfiles",
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
    srcs = ["place_test.cpp"],
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
    name = "macro_snap_test",
    srcs = ["macro_snap_test.cpp"],
    copts = COPTS,
    deps = [
        ":place",
        "@googletest//:gtest_main",
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
    deps = [":warpspace", ":prects_networks", ":satlegalize", ":sat_solver", "//core", "//inou/lefdef:inou_lefdef", "@dreamplace//dreamplace/native:placer", "@rapidjson"],
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
    deps = [":prects_networks", ":sat_solver", ":signal_router", "//core", "//inou/lefdef:inou_lefdef"],
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
    name = "prects_networks",
    srcs = ["prects_networks.cpp"],
    hdrs = ["prects_networks.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//core", "//inou/lefdef:inou_lefdef", "//pass/liberty:liberty_dff"],
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
    name = "density_warp",
    srcs = ["density_warp.cpp"],
    hdrs = ["density_warp.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["@highs//:highs"],
)

cc_library(
    name = "warpspace",
    srcs = ["warpspace.cpp"],
    hdrs = ["warpspace.hpp"],
    copts = COPTS,
    deps = [":density_warp", "@dreamplace//dreamplace/native:placer", "@rapidjson"],
)

cc_test(
    name = "warpspace_test",
    srcs = ["warpspace_test.cpp"],
    copts = COPTS,
    deps = [":warpspace", "@googletest//:gtest_main"],
)

cc_library(
    name = "steiner_topology",
    srcs = ["steiner_topology.cpp"],
    hdrs = ["steiner_topology.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["@dreamplace//thirdparty/flute", "@rules_cc//cc/runfiles"],
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

cc_library(
    name = "uroute",
    srcs = ["uroute.cpp"],
    hdrs = ["uroute.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["@dreamplace//dreamplace/native:placer", "@rapidjson", "@llvm-project//llvm:Support"],
)

filegroup(
    name = "uroute_pretrained",
    srcs = glob(["uroute/pretrained/*"]),
    visibility = ["//visibility:public"],
)

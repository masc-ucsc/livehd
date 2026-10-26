# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@build_bazel_apple_support//xcode:xcode_config.bzl", "xcode_config")
load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_python//python:defs.bzl", "py_test")
load("//tools:copt_default.bzl", "COPTS")

cc_binary(
    name = "warpspace",
    srcs = ["warpspace.cpp"],
    copts = COPTS,
    deps = ["//pass/place:density_warp", "@zlib//:zlib", "@rapidjson"],
)

py_test(
    name = "warpspace_smoke",
    srcs = ["warpspace_smoke.py"],
    args = ["$(location :warpspace)"],
    data = [":warpspace"],
    legacy_create_init = 0,
    timeout = "short",
)

exports_files([
    "aegraph_md_fresh.sh",
    "aegraph_md_refresh.sh",
    "native_sim.py",
])

cc_binary(
    name = "rudy_metrics",
    srcs = ["rudy_metrics.cpp"],
    visibility = ["//visibility:public"],
    deps = ["@dreamplace//dreamplace/native:placer", "@dreamplace//thirdparty/flute", "@bazel_tools//tools/cpp/runfiles"],
)

cc_binary(
    name = "timing_probe",
    srcs = ["timing_probe.cpp"],
    deps = ["//pass/opentimer:pass_opentimer", "@rapidjson"],
)

cc_binary(
    name = "timeclosure_replay",
    srcs = ["timeclosure_replay.cpp"],
    copts = COPTS,
    deps = ["//pass/place:timeclosure", "@rapidjson"],
)

cc_binary(
    name = "late_timing_probe",
    srcs = ["late_timing_probe.cpp"],
    copts = COPTS,
    deps = ["//inou/lefdef:inou_lefdef", "//pass/place:late_timing", "//pass/place/objective:proxy_bridge", "@rapidjson", "@zlib//:zlib"],
)

py_test(
    name = "late_timing_probe_smoke",
    srcs = ["late_timing_probe_smoke.py"],
    args = ["$(location :late_timing_probe)"],
    data = [":late_timing_probe"],
    legacy_create_init = 0,
    timeout = "short",
)

py_test(
    name = "timeclosure_replay_smoke",
    srcs = ["timeclosure_replay_smoke.py"],
    args = ["$(location :timeclosure_replay)"],
    data = [":timeclosure_replay"],
    legacy_create_init = 0,
    timeout = "short",
)

cc_binary(
    name = "physical_repair",
    srcs = ["physical_repair.cpp"],
    deps = [
        "//graph",
        "//inou/lefdef:inou_lefdef",
        "//pass/opentimer:pass_opentimer",
        "//pass/place",
        "@rapidjson",
    ],
)

cc_binary(
    name = "timeclosure_gp_probe",
    srcs = ["timeclosure_gp_probe.cpp"],
    copts = COPTS,
    deps = ["//inou/lefdef:inou_lefdef", "//pass/place", "//pass/place:late_timing", "//pass/place:timing_comparison", "//pass/place/forces:gp_timing", "@rapidjson", "@zlib//:zlib"],
)

py_test(
    name = "timeclosure_gp_probe_smoke",
    srcs = ["timeclosure_gp_probe_smoke.py"],
    args = ["$(location :timeclosure_gp_probe)"],
    data = [":timeclosure_gp_probe"],
    legacy_create_init = 0,
    timeout = "short",
)

# Exercises the native pin timing API supplied by the published dependency.
py_test(
    name = "timeclosure_gp_force_smoke",
    srcs = ["timeclosure_gp_force_smoke.py"],
    args = ["$(location :timeclosure_gp_probe)"],
    data = [":timeclosure_gp_probe"],
    legacy_create_init = 0,
    timeout = "short",
)

cc_binary(
    name = "terminal_legalize",
    srcs = ["terminal_legalize.cpp"],
    deps = ["//pass/place:terminal_legalize", "//inou/lefdef:inou_lefdef", "@rapidjson"],
)

cc_binary(
    name = "placement_metrics",
    srcs = ["placement_metrics.cpp"],
    copts = COPTS,
    visibility = ["//tools/uroute:__pkg__"],
    deps = ["//pass/place:prects_networks", "//inou/lefdef:inou_lefdef", "@rapidjson"],
)

cc_binary(
    name = "routing_geometry",
    srcs = ["routing_geometry.cpp"],
    copts = COPTS,
    deps = ["//inou/lefdef:inou_lefdef", "@rapidjson"],
)

cc_binary(
    name = "cut_rows",
    srcs = ["cut_rows.cpp"],
    copts = COPTS,
    deps = ["//pass/place:handoff_rows", "//pass/place:place"],
)

cc_binary(
    name = "power_grid",
    srcs = ["power_grid.cpp"],
    copts = COPTS,
    deps = ["//pass/place:power_grid", "@rapidjson"],
)

py_test(
    name = "placement_metrics_smoke",
    srcs = ["placement_metrics_smoke.py"],
    args = ["$(location :placement_metrics)", "$(location :rudy_metrics)"],
    data = [":placement_metrics", ":rudy_metrics"],
    legacy_create_init = 0,
    timeout = "short",
)

# An empty Xcode version list selects apple_support's Command Line Tools
# toolchain, without discovering an unrelated /Applications/Xcode.app.
xcode_config(
    name = "command_line_tools",
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "macro_track_align",
    srcs = ["macro_track_align.cpp"],
    copts = COPTS,
    deps = [
        "//inou/lefdef:inou_lefdef",
        "//pass/place",
    ],
)

cc_binary(
    name = "buffer_geometry_probe",
    srcs = ["buffer_geometry_probe.cpp"],
    copts = COPTS,
    deps = ["//pass/place/buffering:producer", "//pass/place/buffering:checkpoint", "//pass/place:late_timing", "//inou/lefdef:inou_lefdef", "//pass/place/buffering:density", "//pass/place/buffering:gap_forces", "//pass/place/buffering:graph_search", "//pass/place/buffering:negotiation", "//pass/place/buffering:physical_sites", "@rapidjson", "@zlib//:zlib"],
)

py_test(
    name = "buffer_geometry_probe_smoke",
    srcs = ["buffer_geometry_probe_smoke.py"],
    args = ["$(location :buffer_geometry_probe)"],
    data = [":buffer_geometry_probe"],
    legacy_create_init = 0,
    timeout = "short",
)

cc_binary(
    name = "metal_rules",
    srcs = ["metal_rules.cpp"],
    copts = COPTS,
    deps = ["//inou/lefdef:inou_lefdef", "@rapidjson"],
)

cc_binary(
    name = "pin_access",
    srcs = ["pin_access.cpp"],
    copts = COPTS,
    deps = ["//inou/lefdef:inou_lefdef", "@rapidjson"],
)

cc_binary(
    name = "signal_route",
    srcs = ["signal_route.cpp"],
    copts = COPTS,
    deps = ["//pass/place:signal_router", "//pass/place:routed_rc", "//pass/place:satlegalize", "//pass/place:place", "//inou/lefdef:inou_lefdef"],
)

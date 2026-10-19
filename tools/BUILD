# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@build_bazel_apple_support//xcode:xcode_config.bzl", "xcode_config")
load("@rules_cc//cc:defs.bzl", "cc_binary")

exports_files([
    "aegraph_md_fresh.sh",
    "aegraph_md_refresh.sh",
    "native_sim.py",
])

cc_binary(
    name = "rudy_metrics",
    srcs = ["rudy_metrics.cpp"],
    deps = ["@dreamplace//dreamplace/native:placer"],
)

cc_binary(
    name = "timing_probe",
    srcs = ["timing_probe.cpp"],
    deps = ["//pass/opentimer:pass_opentimer", "@rapidjson"],
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

# An empty Xcode version list selects apple_support's Command Line Tools
# toolchain, without discovering an unrelated /Applications/Xcode.app.
xcode_config(
    name = "command_line_tools",
    visibility = ["//visibility:public"],
)

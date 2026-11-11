# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:regression.bzl", "cc_test")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

# The fast timing engine (todo/livehd/2q-fasttimer.html): Liberty corners as pooled tables, a numeric
# netlist, and a levelized propagation of all corners in one walk. No OpenTimer, no placement, no hhds.
cc_library(
    name = "fast_timing",
    srcs = [
        "cell_types.cpp",
        "engine.cpp",
        "netlist.cpp",
        "sdc.cpp",
        "wire.cpp",
    ],
    hdrs = [
        "cell_types.hpp",
        "engine.hpp",
        "netlist.hpp",
        "sdc.hpp",
        "wire.hpp",
    ],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = ["//pass/liberty:timing_model"],
)

# The binding of a flat mapped graph to the numeric netlist.
cc_library(
    name = "bind_graph",
    srcs = ["bind_graph.cpp"],
    hdrs = ["bind_graph.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":fast_timing",
        "//graph",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@hhds//hhds:graph",
    ],
)

cc_test(
    name = "fast_timing_test",
    size = "small",
    srcs = [
        "cell_types_test.cpp",
        "engine_test.cpp",
        "sdc_test.cpp",
        "test_library.hpp",
        "wire_test.cpp",
    ],
    copts = COPTS,
    deps = [
        ":fast_timing",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "bind_graph_test",
    size = "small",
    srcs = [
        "bind_graph_test.cpp",
        "test_library.hpp",
    ],
    copts = COPTS,
    deps = [
        ":bind_graph",
        ":fast_timing",
        "//graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)

# Load Liberty corners and report size, load time, what was left out, and the agreement with the model.
cc_binary(
    name = "cell_types_probe",
    srcs = ["cell_types_probe.cpp"],
    copts = COPTS,
    tags = ["manual"],
    deps = [":fast_timing"],
)

# Time a mapped design with the fast engine and report its cost; --opentimer-json prints OpenTimer beside it.
cc_binary(
    name = "fast_timing_probe",
    srcs = ["fast_timing_probe.cpp"],
    copts = COPTS,
    tags = ["manual"],
    deps = [
        ":bind_graph",
        ":fast_timing",
        "//graph",
        "//pass/partition:flatten",
        "@rapidjson",
    ],
)

cc_binary(
    name = "sdc_survey",
    srcs = ["sdc_survey.cpp"],
    copts = COPTS,
    tags = ["manual"],
    deps = [":fast_timing"],
)

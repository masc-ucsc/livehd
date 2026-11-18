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
        "sdc_resolve.cpp",
        "sdc_tcl.cpp",
        "sdc_tcl.hpp",
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

# The constraint reader also feeds pass.opentimer, whose reuse cache is salted by these sources.
filegroup(
    name = "sdc_srcs",
    srcs = [
        "sdc.cpp",
        "sdc.hpp",
        "sdc_tcl.cpp",
        "sdc_tcl.hpp",
    ],
    visibility = ["//pass/opentimer:__pkg__"],
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
        "//pass/partition:flatten",
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

# lhd synth's timing.json from the fast engine, synth.timing.engine=fast: the mapped hierarchy bound in place
# (bind_hier, no flattened copy) and evaluated once; a placed design goes through the Physical_ranker (pin labels).
cc_library(
    name = "timing_report",
    srcs = ["report.cpp"],
    hdrs = ["report.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":physical_rank",
        "//core",
        "//graph",
        "//inou/lefdef:inou_lefdef",
        "@abseil-cpp//absl/container:flat_hash_map",
        "//pass/partition:flatten",
        "@hhds//hhds:graph",
        "@rapidjson",
    ],
)

# The fast engine behind the placement rank callback: placed graph + RC snapshot in, Feedback::Ranking out.
cc_library(
    name = "physical_rank",
    srcs = ["physical_rank.cpp"],
    hdrs = ["physical_rank.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":bind_graph",
        ":fast_timing",
        "//core",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@hhds//hhds:graph",
    ],
)

cc_test(
    name = "physical_rank_test",
    size = "small",
    srcs = [
        "physical_rank_test.cpp",
        "test_library.hpp",
    ],
    copts = COPTS,
    deps = [
        ":physical_rank",
        "//graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)

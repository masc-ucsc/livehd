load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")
load("//tools:regression.bzl", "cc_test")

cc_library(
    name = "pcrs",
    srcs = ["pcrs.cpp", "clock_gate.cpp", "clock_tree.cpp"],
    hdrs = ["pcrs.hpp", "clock_gate.hpp", "clock_tree.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//inou/lefdef:inou_lefdef",
        "//pass/liberty:liberty_dff",
        "//pass/liberty:timing_model",
        "//pass/place:steiner_topology",
        "//pass/timing:fast_timing",
    ],
)

cc_library(
    name = "clock_census",
    srcs = ["clock_census.cpp"],
    hdrs = ["clock_census.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "//pass/timing:bind_graph",
        "//pass/liberty:liberty_dff",
        "//pass/liberty:timing_model",
        "@hhds//hhds:graph",
        "@rapidjson",
    ],
)

cc_test(
    name = "pcrs_test",
    size = "small",
    srcs = ["pcrs_test.cpp", "clock_gate_test.cpp", "clock_tree_test.cpp"],
    copts = COPTS,
    deps = [
        ":pcrs",
        "//pass/partition:flatten",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "clock_tree_scale_test",
    size = "large",
    srcs = ["clock_tree_scale_test.cpp"],
    copts = COPTS,
    tags = ["scalability"],
    deps = [":pcrs", "@googletest//:gtest_main"],
)

cc_test(
    name = "clock_census_test",
    size = "small",
    srcs = ["clock_census_test.cpp"],
    copts = COPTS,
    deps = [
        ":clock_census",
        "//graph",
        "@googletest//:gtest_main",
        "@rapidjson",
        "@hhds//hhds:graph",
    ],
)

filegroup(
    name = "clock_placement_srcs",
    srcs = ["clock_gate.cpp", "clock_gate.hpp", "clock_tree.cpp", "clock_tree.hpp"],
    visibility = ["//pass/place:__pkg__"],
)

# Hashed into the formal verdict-cache salt (see //pass/lec:formal_srcs): bypass_stages() decides what the prover sees.
filegroup(
    name = "formal_srcs",
    srcs = [
        "pcrs.cpp",
        "pcrs.hpp",
    ],
    visibility = ["//visibility:public"],
)

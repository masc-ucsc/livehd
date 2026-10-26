load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_test(
    name = "producer_test",
    size = "small",
    srcs = ["producer_test.cpp"],
    copts = COPTS,
    deps = [":checkpoint_fixture", ":producer", "//inou/lefdef:inou_lefdef", "@rapidjson", "@googletest//:gtest_main"],
)

cc_library(
    name = "checkpoint_fixture",
    testonly = True,
    hdrs = ["checkpoint_fixture.hpp"],
    copts = COPTS,
    deps = [":checkpoint", "//pass/place/forces:gp_timing", "//pass/place:place"],
)

cc_library(
    name = "producer",
    srcs = ["producer.cpp"],
    hdrs = ["producer.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":checkpoint", ":checks", ":physical_sites"],
)

cc_library(
    name = "checkpoint",
    srcs = ["checkpoint.cpp"],
    hdrs = ["checkpoint.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":checks", ":density", ":gap_forces", "//pass/place/forces:pin_weights"],
)

cc_test(
    name = "checkpoint_test",
    size = "small",
    srcs = ["checkpoint_test.cpp"],
    copts = COPTS,
    deps = [":checkpoint_fixture", "//inou/lefdef:inou_lefdef", "@rapidjson", "@googletest//:gtest_main"],
)

cc_library(
    name = "density",
    srcs = ["density.cpp"],
    hdrs = ["density.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":checks", ":negotiation", "@dreamplace//dreamplace/native:placer"],
)

cc_test(
    name = "density_test",
    size = "small",
    srcs = ["density_test.cpp"],
    copts = COPTS,
    deps = [":density", "@googletest//:gtest_main"],
)

cc_library(
    name = "gap_forces",
    srcs = ["gap_forces.cpp"],
    hdrs = ["gap_forces.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":checks", ":negotiation", "@dreamplace//dreamplace/native:placer"],
)

cc_test(
    name = "gap_forces_test",
    size = "small",
    srcs = ["gap_forces_test.cpp"],
    copts = COPTS,
    deps = [":gap_forces", "@googletest//:gtest_main"],
)

cc_library(
    name = "negotiation",
    srcs = ["negotiation.cpp"],
    hdrs = ["negotiation.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":checks", ":tree_search"],
)

cc_test(
    name = "negotiation_test",
    size = "small",
    srcs = ["negotiation_test.cpp"],
    copts = COPTS,
    deps = [":negotiation", "@googletest//:gtest_main"],
)

cc_library(
    name = "physical_sites",
    srcs = ["physical_sites.cpp"],
    hdrs = ["physical_sites.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":checks", ":geometry", "//inou/lefdef:inou_lefdef"],
)

cc_test(
    name = "physical_sites_test",
    size = "small",
    srcs = ["physical_sites_test.cpp"],
    copts = COPTS,
    deps = [":physical_sites", ":graph_search", "@googletest//:gtest_main"],
)

cc_library(
    name = "graph_search",
    srcs = ["graph_search.cpp"],
    hdrs = ["graph_search.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":checks", ":geometry"],
)

cc_test(
    name = "graph_search_test",
    size = "small",
    srcs = ["graph_search_test.cpp"],
    copts = COPTS,
    deps = [":graph_search", "@googletest//:gtest_main"],
)

cc_library(
    name = "geometry",
    srcs = ["geometry.cpp"],
    hdrs = ["geometry.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":checks", ":tree_search"],
)

cc_test(
    name = "geometry_test",
    size = "small",
    srcs = ["geometry_test.cpp"],
    copts = COPTS,
    deps = [":geometry", "@googletest//:gtest_main"],
)

cc_library(
    name = "tree_search",
    srcs = ["tree_search.cpp"],
    hdrs = ["tree_search.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":candidate", ":checks", ":sites"],
)

cc_test(
    name = "tree_search_test",
    size = "small",
    srcs = ["tree_search_test.cpp"],
    copts = COPTS,
    deps = [":tree_search", "@googletest//:gtest_main"],
)

cc_library(
    name = "sites",
    srcs = ["sites.cpp"],
    hdrs = ["sites.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":checks"],
)

cc_test(
    name = "sites_test",
    size = "small",
    srcs = ["sites_test.cpp"],
    copts = COPTS,
    deps = [":sites", "@googletest//:gtest_main"],
)

cc_library(
    name = "candidate",
    srcs = ["candidate.cpp"],
    hdrs = ["candidate.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":checks", "//pass/place:late_timing"],
)

# Header-only validation/bound helpers private to this package.
cc_library(
    name = "checks",
    hdrs = ["checks.hpp"],
    copts = COPTS,
)

cc_test(
    name = "candidate_test",
    size = "small",
    srcs = ["candidate_test.cpp"],
    copts = COPTS,
    deps = [":candidate", "@googletest//:gtest_main"],
)

filegroup(
    name = "timing_sources",
    srcs = glob(["*.cpp", "*.hpp"], exclude = ["*_test.cpp"]),
    visibility = ["//visibility:public"],
)

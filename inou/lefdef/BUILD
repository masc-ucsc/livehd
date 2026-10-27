load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")
cc_library(
    name = "inou_lefdef",
    srcs = ["route_graph_store.cpp", "route_store.cpp", "physical.cpp", "mapped_graph.cpp", "routing_geometry.cpp", "routing_rules.cpp", "pin_access.cpp", "routing_occupancy.cpp", "routing_connectivity.cpp", "signal_via.cpp", "cut_rules.cpp", "tech_profile.cpp", "lef_writer.cpp", "row_access.cpp"],
    hdrs = ["route_store.hpp", "physical.hpp", "mapped_graph.hpp", "routing_geometry.hpp", "routing_rules.hpp", "pin_access.hpp", "routing_occupancy.hpp", "routing_connectivity.hpp", "signal_via.hpp", "cut_rules.hpp", "tech_profile.hpp", "lef_writer.hpp", "row_access.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [":index_union_find", "//core", "//graph", "//third_party/misc/lef/lef", "//third_party/misc/def/def", "@rapidjson", "@roaring//:roaring_cpp"],
)
cc_library(
    name = "index_union_find",
    hdrs = ["index_union_find.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
)
cc_test(
    name = "index_union_find_smoke",
    srcs = ["index_union_find_smoke.cpp"],
    copts = COPTS,
    deps = [":index_union_find", "@googletest//:gtest_main"],
)
filegroup(
    name = "test_data",
    srcs = glob(["tests/data/*"]),
    visibility = ["//visibility:public"],
)
cc_test(
    name = "physical_test",
    srcs = ["physical_test.cpp"],
    copts = COPTS,
    data = [":test_data"],
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

cc_test(
    name = "lef_writer_test",
    size = "small",
    srcs = ["lef_writer_test.cpp"],
    copts = COPTS,
    data = [":test_data"],
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

cc_test(
    name = "row_access_test",
    size = "small",
    srcs = ["row_access_test.cpp"],
    copts = COPTS,
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

cc_test(
    name = "mapped_graph_test",
    srcs = ["mapped_graph_test.cpp"],
    copts = COPTS,
    deps = [":inou_lefdef", "//graph", "@googletest//:gtest_main"],
)

cc_test(
    name = "routing_geometry_test",
    srcs = ["routing_geometry_test.cpp"],
    copts = COPTS,
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

cc_test(
    name = "routing_rules_test",
    srcs = ["routing_rules_test.cpp"],
    copts = COPTS,
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

cc_test(
    name = "pin_access_test",
    srcs = ["pin_access_test.cpp"],
    copts = COPTS,
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

cc_test(
    name = "tech_profile_test",
    srcs = ["tech_profile_test.cpp"],
    copts = COPTS,
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

cc_test(
    name = "routing_occupancy_test",
    srcs = ["routing_occupancy_test.cpp"],
    copts = COPTS,
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

cc_test(
    name = "routing_connectivity_test",
    srcs = ["routing_connectivity_test.cpp"],
    copts = COPTS,
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

cc_test(
    name = "signal_via_test",
    srcs = ["signal_via_test.cpp"],
    copts = COPTS,
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

# Routing legality and DEF geometry participate in completed-placement identity.
filegroup(
    name = "physical_srcs",
    srcs = glob(["*.cpp", "*.hpp"], exclude = ["*_test.cpp"]),
    visibility = ["//visibility:public"],
)

cc_test(
    name = "route_store_test",
    srcs = ["route_store_test.cpp"],
    copts = COPTS,
    deps = [":inou_lefdef", "@googletest//:gtest_main"],
)

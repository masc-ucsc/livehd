load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")
cc_library(
    name = "inou_lefdef",
    srcs = ["physical.cpp"],
    hdrs = ["physical.hpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//graph", "//third_party/misc/lef/lef", "//third_party/misc/def/def"],
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
